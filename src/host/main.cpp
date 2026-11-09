// ts2.exe – TimeSplitters 2 ohne Homefront.
//
//   ts2 --einrichten <Homefront-Ordner>   einmalig: holt TS2 aus der eigenen
//                                         Homefront-Installation nach game/
//   ts2 [--spiel <game-Ordner>] [--takte N]
//
// Beim Einrichten werden nur der TS2-Code und die Daten, die er anfasst, aus
// Homefront2_Release.exe nach game/ts2.bin kopiert (ts2_manifest.inc) und die
// TS2-Daten aus lsao_cached.pak/core_patch.pak nach game/ts2/ entpackt
// (tools/hfpak). Danach wird Homefront nicht mehr gebraucht. ts2.exe lädt
// ts2.bin, ersetzt alles, was TS2 von außerhalb ruft, durch eigenen Code
// (replace.cpp) und gibt TS2 die Engine-Schnittstellen selbst – zunächst als
// protokollierende Attrappen (fake.cpp) und ein eigenes D3D11-Gerät.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "fake.h"
#include "features.h"
#include "input.h"
#include "menu.h"
#include "audio.h"
#include "hf2.h"
#include "log.h"
#include "ts2bin.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace fs = std::filesystem;

namespace host {

namespace {
FILE* g_log = nullptr;
std::mutex g_log_m;
Ts2Image g_img;

fs::path self_dir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return fs::path(p).parent_path();
}
}  // namespace

void log(const char* fmt, ...) {
    std::lock_guard<std::mutex> g(g_log_m);
    if (!g_log) g_log = _wfopen((self_dir() / L"ts2.log").c_str(), L"w");
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (g_log) {
        va_start(ap, fmt);
        std::vfprintf(g_log, fmt, ap);
        va_end(ap);
        std::fflush(g_log);
    }
}

namespace {

// ---- Abstürze mit Adresse im Original melden
LONG CALLBACK on_exception(EXCEPTION_POINTERS* e) {
    const DWORD code = e->ExceptionRecord->ExceptionCode;
    if (code < 0x80000000u) return EXCEPTION_CONTINUE_SEARCH;  // Meldungen, keine Fehler
    const uint64_t a = uint64_t(e->ExceptionRecord->ExceptionAddress);
    const bool in_image = a >= uint64_t(g_img.base) && a < uint64_t(g_img.base) + g_img.size;
    const uint64_t orig = in_image ? a - uint64_t(g_img.base) + g_img.preferred_base : 0;
    log("[absturz] Ausnahme %08lX bei %p", code, (void*)a);
    if (in_image)
        log(" = Original-VA 0x%llX%s", (unsigned long long)orig,
            orig >= hf2::kTs2Lo && orig < hf2::kTs2Hi ? " (TS2)" : " (außerhalb von TS2: nicht übernommen)");
    if (code == EXCEPTION_ACCESS_VIOLATION) {
        const uint64_t t = e->ExceptionRecord->ExceptionInformation[1];
        log(", %s 0x%llX", e->ExceptionRecord->ExceptionInformation[0] ? "schreibend" : "lesend", (unsigned long long)t);
        if (t >= uint64_t(g_img.base) && t < uint64_t(g_img.base) + g_img.size)
            log(" (Original 0x%llX: Seite nicht in ts2.bin)", (unsigned long long)(t - uint64_t(g_img.base) + g_img.preferred_base));
    }
    const uint64_t self = uint64_t(GetModuleHandleW(nullptr));
    if (a >= self && a < self + 0x400000) log(" = ts2.exe+0x%llX (build/ts2.map)", (unsigned long long)(a - self));
    const uint64_t ret = *reinterpret_cast<uint64_t*>(e->ContextRecord->Rsp);
    if (ret >= uint64_t(g_img.base) && ret < uint64_t(g_img.base) + g_img.size)
        log(", gerufen von Original-VA 0x%llX", (unsigned long long)(ret - uint64_t(g_img.base) + g_img.preferred_base));
    log("\n  rcx=%llX rdx=%llX r8=%llX r9=%llX rsp=%llX\n", e->ContextRecord->Rcx, e->ContextRecord->Rdx,
        e->ContextRecord->R8, e->ContextRecord->R9, e->ContextRecord->Rsp);
    return EXCEPTION_CONTINUE_SEARCH;
}

fs::path find_game() {
    for (fs::path d = self_dir(); !d.empty(); d = d.parent_path()) {
        if (fs::exists(d / L"game" / L"ts2")) return d / L"game";
        if (d == d.parent_path()) break;
    }
    return self_dir() / L"game";
}

// ---- Einrichten
int setup(const fs::path& homefront, const fs::path& game) {
    const fs::path exe = homefront / L"Bin64" / L"Homefront2_Release.exe";
    if (!fs::exists(exe)) {
        log("keine Bin64\\Homefront2_Release.exe in %ls\n", homefront.c_str());
        return 1;
    }
    std::string err;
    if (!write_ts2bin(exe, game / L"ts2.bin", &err)) {
        log("Einrichten fehlgeschlagen: %s\n", err.c_str());
        return 1;
    }
    // Die TS2-Daten entpackt hfpak (liegt neben ts2.exe).
    const fs::path hfpak = self_dir() / L"hfpak.exe";
    if (!fs::exists(game / L"ts2" / L"data")) {
        if (!fs::exists(hfpak)) {
            log("hfpak.exe fehlt neben ts2.exe; TS2-Daten nicht entpackt\n");
            return 1;
        }
        for (const wchar_t* pak : {L"lsao_cached.pak", L"core_patch.pak"}) {
            std::wstring cmd = L"\"" + hfpak.wstring() + L"\" extract --exe \"" + exe.wstring() + L"\" \"" +
                               (homefront / L"gamehf2" / pak).wstring() + L"\" \"" + game.wstring() + L"\" ts2/";
            if (_wsystem((L"\"" + cmd + L"\"").c_str()) != 0) {
                log("Entpacken von %ls fehlgeschlagen\n", pak);
                return 1;
            }
        }
    }
    log("Eingerichtet in %ls. Homefront wird ab jetzt nicht mehr gebraucht.\n", game.c_str());
    return 0;
}

// ---- Fenster und D3D11
LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

struct Gfx {
    HWND wnd = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain1* swap = nullptr;
};

bool make_gfx(Gfx* g, int width, int height) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"zeitbrecher_zwo";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    // Fenster höchstens 80 % des Bildschirms, im Seitenverhältnis des Bildes
    int ww = width, wh = height;
    const int sw = GetSystemMetrics(SM_CXSCREEN) * 4 / 5, shh = GetSystemMetrics(SM_CYSCREEN) * 4 / 5;
    if (ww > sw || wh > shh) {
        const double k = std::min(double(sw) / ww, double(shh) / wh);
        ww = int(ww * k);
        wh = int(wh * k);
    }
    RECT r{0, 0, ww, wh};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    g->wnd = CreateWindowW(wc.lpszClassName, L"TimeSplitters 2", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g->wnd) return false;
    ShowWindow(g->wnd, SW_SHOW);
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &fl, 1,
                                 D3D11_SDK_VERSION, &g->dev, nullptr, &g->ctx)))
        return false;
    IDXGIDevice* xd = nullptr;
    IDXGIAdapter* ad = nullptr;
    IDXGIFactory2* fa = nullptr;
    g->dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&xd));
    xd->GetAdapter(&ad);
    ad->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&fa));
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = UINT(width);
    sd.Height = UINT(height);
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    HRESULT hr = fa->CreateSwapChainForHwnd(g->dev, g->wnd, &sd, nullptr, nullptr, &g->swap);
    fa->Release();
    ad->Release();
    xd->Release();
    return SUCCEEDED(hr);
}

// Die Halter, die TS2 statt roher D3D11-Zeiger bekommt (Feld 0 zählt).
struct DeviceHolder {
    ID3D11Device* dev;
    uint8_t rest[0x100];
};
struct ContextHolder {
    ID3D11DeviceContext* ctx;
    uint8_t rest[0x100];
};

// ---- Aufrufe in den TS2-Code, gegen Abstürze abgeschirmt
using FactoryFn = void*(__fastcall*)(void* genv, void* semaphore_holder);
using Call1Fn = void(__fastcall*)(void* obj, void* a);

void* call_factory(void* genv, void* sem) {
    __try {
        return reinterpret_cast<FactoryFn>(g_img.va(hf2::kFactory))(genv, sem);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

using RenderFn = void(__fastcall*)(void* obj, void* devh, void* ctxh, void* rtv, void* backbuffer);
bool call_render(void* obj, void* devh, void* ctxh, void* rtv, void* back) {
    __try {
        auto vt = *reinterpret_cast<void***>(obj);
        reinterpret_cast<RenderFn>(vt[hf2::kRender / 8])(obj, devh, ctxh, rtv, back);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Bildpuffer als 32-Bit-BMP speichern (zum Prüfen ohne Bildschirm).
void save_bmp(const Gfx& g, ID3D11Texture2D* back, const fs::path& out) {
    D3D11_TEXTURE2D_DESC d;
    back->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    ID3D11Texture2D* st = nullptr;
    if (FAILED(g.dev->CreateTexture2D(&d, nullptr, &st))) return;
    g.ctx->CopyResource(st, back);
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(g.ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
        BITMAPFILEHEADER fh{};
        BITMAPINFOHEADER ih{};
        ih.biSize = sizeof ih;
        ih.biWidth = LONG(d.Width);
        ih.biHeight = -LONG(d.Height);
        ih.biPlanes = 1;
        ih.biBitCount = 32;
        const uint32_t bytes = d.Width * d.Height * 4;
        fh.bfType = 0x4D42;
        fh.bfOffBits = sizeof fh + sizeof ih;
        fh.bfSize = fh.bfOffBits + bytes;
        FILE* f = _wfopen(out.c_str(), L"wb");
        if (f) {
            std::fwrite(&fh, sizeof fh, 1, f);
            std::fwrite(&ih, sizeof ih, 1, f);
            std::vector<uint8_t> row(d.Width * 4);
            for (UINT y = 0; y < d.Height; ++y) {
                const uint8_t* s = static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch;
                for (UINT x = 0; x < d.Width; ++x) {  // RGBA -> BGRA
                    row[x * 4 + 0] = s[x * 4 + 2];
                    row[x * 4 + 1] = s[x * 4 + 1];
                    row[x * 4 + 2] = s[x * 4 + 0];
                    row[x * 4 + 3] = 255;
                }
                std::fwrite(row.data(), 1, row.size(), f);
            }
            std::fclose(f);
            log("Bild gespeichert: %ls\n", out.c_str());
        }
        g.ctx->Unmap(st, 0);
    }
    st->Release();
}

bool call_vfunc(void* obj, uint32_t off, void* a) {
    __try {
        auto vt = *reinterpret_cast<void***>(obj);
        reinterpret_cast<Call1Fn>(vt[off / 8])(obj, a);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace
}  // namespace host

int wmain(int argc, wchar_t** argv) {
    using namespace host;
    fs::path game, setup_from, shot;
    std::wstring input_spec;
    int ticks = 0;  // 0 = bis das Fenster geschlossen wird
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--spiel" && i + 1 < argc) game = argv[++i];
        else if (a == L"--einrichten" && i + 1 < argc) setup_from = argv[++i];
        else if (a == L"--takte" && i + 1 < argc) ticks = _wtoi(argv[++i]);
        else if (a == L"--bild" && i + 1 < argc) shot = fs::absolute(argv[++i]);
        else if (a == L"--eingabe" && i + 1 < argc) input_spec = argv[++i];
        else {
            std::fwprintf(stderr,
                          L"Aufruf: ts2 --einrichten <Homefront-Ordner>\n"
                          L"        ts2 [--spiel <game-Ordner>] [--takte N [--bild <datei.bmp>]] [--eingabe takt:bits:anzahl,...]\n");
            return 1;
        }
    }
    if (game.empty()) game = find_game();
    if (!setup_from.empty()) return setup(setup_from, game);

    AddVectoredExceptionHandler(1, on_exception);
    std::string err;
    if (!load_ts2bin(game / L"ts2.bin", &g_img, &err)) {
        log("%s\nEinmalig einrichten: ts2 --einrichten <Homefront-Ordner>\n", err.c_str());
        return 1;
    }
    // TS2 öffnet seine Dateien als "ts2/<name>" relativ hierzu.
    SetCurrentDirectoryW(game.c_str());
    const Settings settings = load_settings(self_dir() / L"ts2.ini");
    features_install(g_img, settings);
    unlock_install(g_img, load_unlock_config(self_dir() / L"ts2.ini"));

    // gEnv füllen
    uint8_t* genv = g_img.va(hf2::kGEnv);
    Fake* fields[0x200 / 8] = {};
    for (uint32_t off : hf2::kGEnvFields) {
        char name[32];
        std::snprintf(name, sizeof name, "gEnv+0x%03X", off);
        fields[off / 8] = make_fake(_strdup(name));
        *reinterpret_cast<void**>(genv + off) = fake_object(fields[off / 8]);
    }
    // Soundsystem (gEnv+0xD0): ce_sound (0x14122829C) meldet über +0x100 seinen
    // Mixer an und bekommt ein Strom-Objekt (in 0x142FAEB88); audio.cpp.
    audio_install(g_img, load_audio_config(self_dir() / L"ts2.ini"));
    fake_set(fields[0xD0 / 8], 0x100, reinterpret_cast<const void*>(&audio_open_stream));

    Gfx gfx;
    if (!make_gfx(&gfx, settings.width, settings.height)) {
        log("D3D11 nicht verfügbar\n");
        return 1;
    }
    static DeviceHolder devh{gfx.dev, {}};
    static ContextHolder ctxh{gfx.ctx, {}};

    HANDLE sem = CreateSemaphoreW(nullptr, 0, 1, nullptr);
    void* obj = call_factory(genv, &sem);
    log("Fabrik: TS2-Objekt %p\n", obj);
    if (!obj) return 2;
    if (!call_vfunc(obj, hf2::kStart, &devh)) {
        log("Start ist abgestürzt (siehe [absturz])\n");
        return 3;
    }
    log("Start zurückgekehrt; Semaphore %s\n",
        WaitForSingleObject(sem, 0) == WAIT_OBJECT_0 ? "freigegeben" : "nicht freigegeben");

    // Bildpuffer: Render bekommt sein Render-Target-View und die Textur
    // (TS2 kopiert daraus für Bildschirmeffekte).
    input_init(gfx.wnd, obj, self_dir() / L"ts2.ini");
    if (!input_spec.empty()) input_script(input_spec.c_str());

    ID3D11Texture2D* back = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    gfx.swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back));
    gfx.dev->CreateRenderTargetView(back, nullptr, &rtv);

    bool running = true;
    int t = 0;
    for (; running && (ticks == 0 || t < ticks); ++t) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) running = false;
            input_message(m);
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        input_frame();
        features_tick(g_img, settings, gfx.wnd);
        if (!call_vfunc(obj, hf2::kTick, &devh)) {
            log("Takt %d ist abgestürzt\n", t);
            return 4;
        }
        if (!call_render(obj, &devh, &ctxh, rtv, back)) {
            log("Bild %d ist abgestürzt\n", t);
            return 5;
        }
        if (t + 1 == ticks && !shot.empty()) save_bmp(gfx, back, shot);
        gfx.swap->Present(1, 0);
    }
    audio_shutdown();
    log("%d Takte gelaufen\n", t);
    return 0;
}
