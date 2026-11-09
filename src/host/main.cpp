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
#include <dxgi1_6.h>
#include <d3d11sdklayers.h>
#include <map>
#include <timeapi.h>

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
#include "menumouse.h"
#include "ts2options.h"
#include "timing.h"
#include "postfx.h"
#include "audio.h"
#include "hf2.h"
#include "log.h"
#include "ts2bin.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "winmm.lib")

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
// ---- Vollbild: randlos über dem ganzen Monitor des Fensters. DXGIs eigenes
// Alt+Enter (exklusiv, ohne dass die Puffer angepasst werden: das Bild stand)
// ist abgeschaltet; das Flip-Modell skaliert das Bild in der Render-Auflösung
// auf jede Fenstergröße. G-Sync/VRR arbeitet unter aktuellem Windows auch im
// randlosen Vollbild.
WINDOWPLACEMENT g_windowed_place{sizeof(WINDOWPLACEMENT)};
bool g_fullscreen = false;

void set_fullscreen(HWND h, bool on) {
    if (on == g_fullscreen) return;
    if (on) {
        GetWindowPlacement(h, &g_windowed_place);
        MONITORINFO mi{sizeof mi};
        GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtrW(h, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(h, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        SetWindowLongPtrW(h, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPlacement(h, &g_windowed_place);
        SetWindowPos(h, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
    g_fullscreen = on;
    input_reclip();
    log("Vollbild %s\n", on ? "an (randlos)" : "aus");
}

LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    // Alt+Enter: randloses Vollbild ein/aus
    if (m == WM_SYSKEYDOWN && w == VK_RETURN && (l & (1 << 29)) && !(l & (1 << 30))) {
        set_fullscreen(h, !g_fullscreen);
        return 0;
    }
    if (m == WM_SYSCHAR && w == VK_RETURN) return 0;  // kein Signalton
    if (m == WM_SIZE || m == WM_MOVE) input_reclip();
    return DefWindowProcW(h, m, w, l);
}

ID3D11InfoQueue* g_info = nullptr;

// TS2_BLENDPROBE=1 (Glasfehler): protokolliert jeden Blend-Zustand, den TS2
// anlegt, und die ersten Löschfarben. Eigene Geräte-Methoden über die
// vtable des eigenen Geräts umgelenkt; nur zur Untersuchung.
using CreateBlendFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_BLEND_DESC*, ID3D11BlendState**);
using ClearRtvFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11RenderTargetView*, const FLOAT*);
CreateBlendFn g_create_blend = nullptr;
ClearRtvFn g_clear_rtv = nullptr;
int g_clear_n = 0;

HRESULT STDMETHODCALLTYPE probe_create_blend(ID3D11Device* d, const D3D11_BLEND_DESC* bd, ID3D11BlendState** out) {
    const HRESULT hr = g_create_blend(d, bd, out);
    if (bd) {
        const auto& r = bd->RenderTarget[0];
        log("[blend] %p: an %d, Farbe %d*%d op %d, Alpha %d*%d op %d, Maske %X, a2c %d, einzeln %d\n",
            out ? static_cast<void*>(*out) : nullptr, r.BlendEnable, r.SrcBlend, r.DestBlend, r.BlendOp,
            r.SrcBlendAlpha, r.DestBlendAlpha, r.BlendOpAlpha, r.RenderTargetWriteMask, bd->AlphaToCoverageEnable,
            bd->IndependentBlendEnable);
    }
    return hr;
}

// Formate der Texturen, die TS2 anlegt (sRGB oder nicht: Belichtung)
using CreateTex2DFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_TEXTURE2D_DESC*,
                                                   const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**);
using CreateRtvFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, ID3D11Resource*, const D3D11_RENDER_TARGET_VIEW_DESC*,
                                                 ID3D11RenderTargetView**);
CreateTex2DFn g_create_tex2d = nullptr;
CreateRtvFn g_create_rtv = nullptr;

HRESULT STDMETHODCALLTYPE probe_create_tex2d(ID3D11Device* d, const D3D11_TEXTURE2D_DESC* td,
                                             const D3D11_SUBRESOURCE_DATA* init, ID3D11Texture2D** out) {
    if (td)
        log("[textur] %ux%u Format %d Bind %X Mips %u\n", td->Width, td->Height, int(td->Format), td->BindFlags,
            td->MipLevels);
    return g_create_tex2d(d, td, init, out);
}

HRESULT STDMETHODCALLTYPE probe_create_rtv(ID3D11Device* d, ID3D11Resource* r, const D3D11_RENDER_TARGET_VIEW_DESC* vd,
                                           ID3D11RenderTargetView** out) {
    log("[rtv] Format %d\n", vd ? int(vd->Format) : -1);
    return g_create_rtv(d, r, vd, out);
}

void STDMETHODCALLTYPE probe_clear_rtv(ID3D11DeviceContext* c, ID3D11RenderTargetView* v, const FLOAT* col) {
    if (g_clear_n++ < 40) log("[loeschen] %p: %.3f %.3f %.3f %.3f\n", static_cast<void*>(v), col[0], col[1], col[2], col[3]);
    g_clear_rtv(c, v, col);
}

void patch_vtable(void* obj, int index, void* fn, void** orig) {
    void** vt = *reinterpret_cast<void***>(obj);
    DWORD old;
    VirtualProtect(&vt[index], sizeof(void*), PAGE_READWRITE, &old);
    *orig = vt[index];
    vt[index] = fn;
    VirtualProtect(&vt[index], sizeof(void*), old, &old);
}

// Meldungen der Debug-Schicht: jede Art einmal mit Text, am Ende die Zähler
void d3d_messages(bool final_report) {
    static std::map<int, long> counts;
    static std::map<int, std::string> first;
    if (!g_info) return;
    const UINT64 n = g_info->GetNumStoredMessages();
    for (UINT64 i = 0; i < n; ++i) {
        SIZE_T len = 0;
        g_info->GetMessage(i, nullptr, &len);
        std::vector<char> buf(len);
        auto* msg = reinterpret_cast<D3D11_MESSAGE*>(buf.data());
        if (FAILED(g_info->GetMessage(i, msg, &len))) continue;
        if (msg->Severity > D3D11_MESSAGE_SEVERITY_WARNING) continue;  // nur Fehler und Warnungen
        if (counts[msg->ID]++ == 0) {
            first[msg->ID] = std::string(msg->pDescription, msg->DescriptionByteLength ? msg->DescriptionByteLength - 1 : 0);
            log("[d3d] %s %d: %s\n", msg->Severity <= D3D11_MESSAGE_SEVERITY_ERROR ? "Fehler" : "Warnung", int(msg->ID),
                first[msg->ID].c_str());
        }
    }
    g_info->ClearStoredMessages();
    if (final_report)
        for (auto& [id, c] : counts) log("[d3d] Meldung %d: %ld mal\n", id, c);
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
    // Fenster: 80 % des Bildschirms (echte Pixel), im Seitenverhältnis des Bildes
    int ww = width, wh = height;
    const int sw = GetSystemMetrics(SM_CXSCREEN) * 4 / 5, shh = GetSystemMetrics(SM_CYSCREEN) * 4 / 5;
    {
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
    // Die leistungsstärkste GPU nehmen: mit mehreren Grafikkarten (z. B. einer
    // im Prozessor) wäre der Standard-Adapter sonst womöglich die schwächste,
    // und jedes Bild müsste zur Karte am Monitor kopiert werden.
    IDXGIAdapter1* best = nullptr;
    IDXGIFactory6* f6 = nullptr;
    if (SUCCEEDED(CreateDXGIFactory2(0, __uuidof(IDXGIFactory6), reinterpret_cast<void**>(&f6)))) {
        f6->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof(IDXGIAdapter1),
                                       reinterpret_cast<void**>(&best));
        f6->Release();
    }
    if (best) {
        DXGI_ADAPTER_DESC1 d{};
        best->GetDesc1(&d);
        char name[128];
        WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof name, nullptr, nullptr);
        log("Grafikkarte: %s (%llu MB)\n", name, static_cast<unsigned long long>(d.DedicatedVideoMemory >> 20));
    }
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    // TS2_D3DDEBUG=1: D3D11-Debug-Schicht, Meldungen ins Protokoll (d3d_messages)
    const bool debug = GetEnvironmentVariableW(L"TS2_D3DDEBUG", nullptr, 0) > 0;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | (debug ? D3D11_CREATE_DEVICE_DEBUG : 0);
    HRESULT dhr = D3D11CreateDevice(best, best ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                    &fl, 1, D3D11_SDK_VERSION, &g->dev, nullptr, &g->ctx);
    if (FAILED(dhr) && debug) {
        log("D3D11-Debug-Schicht nicht verfügbar\n");
        flags &= ~UINT(D3D11_CREATE_DEVICE_DEBUG);
        dhr = D3D11CreateDevice(best, best ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, &fl, 1,
                                D3D11_SDK_VERSION, &g->dev, nullptr, &g->ctx);
    }
    if (SUCCEEDED(dhr) && debug &&
        SUCCEEDED(g->dev->QueryInterface(__uuidof(ID3D11InfoQueue), reinterpret_cast<void**>(&g_info))))
        log("D3D11-Debug-Schicht an\n");
    if (best) best->Release();
    if (FAILED(dhr)) return false;
    if (GetEnvironmentVariableW(L"TS2_BLENDPROBE", nullptr, 0) > 0) {
        // ID3D11Device: CreateBlendState ist Methode 20; Kontext:
        // ClearRenderTargetView Methode 50 (+0x190)
        patch_vtable(g->dev, 20, reinterpret_cast<void*>(&probe_create_blend), reinterpret_cast<void**>(&g_create_blend));
        patch_vtable(g->ctx, 50, reinterpret_cast<void*>(&probe_clear_rtv), reinterpret_cast<void**>(&g_clear_rtv));
        // CreateTexture2D Methode 5, CreateRenderTargetView Methode 9
        patch_vtable(g->dev, 5, reinterpret_cast<void*>(&probe_create_tex2d), reinterpret_cast<void**>(&g_create_tex2d));
        patch_vtable(g->dev, 9, reinterpret_cast<void*>(&probe_create_rtv), reinterpret_cast<void**>(&g_create_rtv));
        log("Blend-Probe an\n");
    }
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
    // Alt+Enter übernimmt ts2.exe selbst (randlos, set_fullscreen)
    fa->MakeWindowAssociation(g->wnd, DXGI_MWA_NO_ALT_ENTER);
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
    // In echten Pixeln arbeiten: sonst skaliert Windows das Fenster bei
    // Anzeigeskalierung > 100 % nachträglich (unscharf, kein direktes Flip).
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    fs::path game, setup_from, shot;
    std::wstring input_spec, menu_mouse_spec;
    int ticks = 0;  // 0 = bis das Fenster geschlossen wird
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--spiel" && i + 1 < argc) game = argv[++i];
        else if (a == L"--einrichten" && i + 1 < argc) setup_from = argv[++i];
        else if (a == L"--takte" && i + 1 < argc) ticks = _wtoi(argv[++i]);
        else if (a == L"--bild" && i + 1 < argc) shot = fs::absolute(argv[++i]);
        else if (a == L"--eingabe" && i + 1 < argc) input_spec = argv[++i];
        else if (a == L"--menuemaus" && i + 1 < argc) menu_mouse_spec = argv[++i];
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
    // TS2 wartet mit Sleep(ms) auf seinen nächsten Takt; ohne feinere
    // Zeitgeber-Auflösung schläft Windows dabei bis zu 15,6 ms zu lange.
    timeBeginPeriod(1);
    if (const int n = ts2opt::ini_complete(self_dir() / L"ts2.ini"))
        log("ts2.ini: %d Option(en) ergänzt\n", n);
    const Settings settings = load_settings(self_dir() / L"ts2.ini");
    features_install(g_img, settings);
    timing_install(g_img, settings.tick_rate);
    unlock_install(g_img, load_unlock_config(self_dir() / L"ts2.ini"));
    {
        wchar_t code[16] = {};
        GetPrivateProfileStringW(L"sprache", L"text", L"pc", code, 16, (self_dir() / L"ts2.ini").c_str());
        language_install(g_img, game, code);
    }
    menumouse_install(g_img, GetPrivateProfileIntW(L"maus", L"menue", 1, (self_dir() / L"ts2.ini").c_str()) != 0);

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
    if (settings.fullscreen) set_fullscreen(gfx.wnd, true);
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
    if (!menu_mouse_spec.empty()) menumouse_script(menu_mouse_spec.c_str());

    ID3D11Texture2D* back = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    gfx.swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back));
    gfx.dev->CreateRenderTargetView(back, nullptr, &rtv);
    // Bildanpassung ([grafik] gamma, kontrast, helligkeit, saettigung): TS2
    // zeichnet dann in eine eigene Textur, postfx_apply schreibt sie angepasst
    // in den Bildpuffer. Neutral zeichnet TS2 wie bisher direkt hinein.
    ID3D11RenderTargetView* scene_rtv = rtv;
    ID3D11Texture2D* scene_tex = back;
    {
        const std::wstring ini = (self_dir() / L"ts2.ini").wstring();
        auto getf = [&](const wchar_t* key, const wchar_t* def) {
            wchar_t buf[32];
            GetPrivateProfileStringW(L"grafik", key, def, buf, 32, ini.c_str());
            return float(_wtof(buf));
        };
        PostFxSettings pf;
        pf.gamma = getf(L"gamma", L"1.0");
        pf.contrast = getf(L"kontrast", L"1.0");
        pf.brightness = getf(L"helligkeit", L"0.0");
        pf.saturation = getf(L"saettigung", L"1.0");
        postfx_init(gfx.dev, back, pf, &scene_rtv, &scene_tex);
    }

    // Bildzeiten protokollieren (alle 600 Bilder): Mittel, Spanne, Ausreißer
    LARGE_INTEGER qf, q0;
    QueryPerformanceFrequency(&qf);
    QueryPerformanceCounter(&q0);
    double ft_sum = 0, ft_min = 1e9, ft_max = 0;
    int ft_n = 0, ft_slow = 0;
    auto frame_stats = [&] {
        LARGE_INTEGER q;
        QueryPerformanceCounter(&q);
        const double ms = double(q.QuadPart - q0.QuadPart) * 1000.0 / double(qf.QuadPart);
        q0 = q;
        ft_sum += ms;
        ft_min = std::min(ft_min, ms);
        ft_max = std::max(ft_max, ms);
        ft_slow += ms > 20.0;
        if (++ft_n == 600) {
            log("[bild] 600 Bilder: Mittel %.2f ms (%.1f fps), %.2f-%.2f ms, %d über 20 ms\n", ft_sum / ft_n,
                1000.0 * ft_n / ft_sum, ft_min, ft_max, ft_slow);
            ft_sum = 0, ft_min = 1e9, ft_max = 0, ft_n = 0, ft_slow = 0;
        }
    };

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
        timing_frame();
        // TS2_ALPHALOESCHEN=<0..1>: Ziel vor jedem Bild auf (0,0,0,a) löschen
        // (Glasfehler: TS2 mischt mit dem Ziel-Alpha und löscht selbst nicht)
        {
            static const float clear_alpha = [] {
                wchar_t v[16] = {};
                return GetEnvironmentVariableW(L"TS2_ALPHALOESCHEN", v, 16) > 0 ? float(_wtof(v)) : -1.0f;
            }();
            if (clear_alpha >= 0.0f) {
                const FLOAT c[4] = {0.0f, 0.0f, 0.0f, clear_alpha};
                gfx.ctx->ClearRenderTargetView(scene_rtv, c);
            }
        }
        if (!call_render(obj, &devh, &ctxh, scene_rtv, scene_tex)) {
            log("Bild %d ist abgestürzt\n", t);
            return 5;
        }
        postfx_apply(gfx.ctx, rtv);
        if (t + 1 == ticks && !shot.empty()) save_bmp(gfx, back, shot);
        // TS2_BILDPAAR=1: auch das vorletzte Bild (<bild>.vorher.bmp), um zu
        // prüfen, ob sich das Bild in jedem Takt ändert
        if (t + 2 == ticks && !shot.empty() && GetEnvironmentVariableW(L"TS2_BILDPAAR", nullptr, 0) > 0)
            save_bmp(gfx, back, fs::path(shot).replace_extension(L".vorher.bmp"));
        // TS2 hält seine 60 Takte selbst ein (Sleep, dann Warteschleife,
        // 0x14107CC64); ein zusätzliches Warten auf den Monitor liefe gegen
        // diesen Takt. Ohne bildsync zeigt das Flip-Modell jedes Bild ab dem
        // nächsten Bildwechsel, ohne Zerreißen und ohne zu blockieren.
        gfx.swap->Present(settings.vsync ? 1 : 0, 0);
        frame_stats();
        d3d_messages(false);
    }
    audio_shutdown();
    d3d_messages(true);
    log("%d Takte gelaufen\n", t);
    return 0;
}
