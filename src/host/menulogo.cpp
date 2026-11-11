// Eigener Schein für den Schriftzug im Hauptmenü.
//
// Das Hauptmenü (0x1410B5A8C) zeichnet misc/ts2newlogo und darüber
// misc/ts2glow (Index 0x72, Mischart 3 = SRC_ALPHA/ONE, pulsierend; Sprite
// 0x14108A41C, Aufruf 0x1410B5C76). Dieselbe Textur ts2glow formt im ersten
// Bildschirm das brennende Logo (0x1410B58EF, DEST_ALPHA/ONE). Ein neuer
// Schriftzug braucht im Menü einen passenden Schein, das brennende Logo soll
// aber bleiben: Deshalb bindet ts2.exe nur für die Schein-Zeichnung des
// Hauptmenüs eine eigene Textur (game/hd/ts2/textures/misc/ts2glow_menue.rgba,
// erzeugt von tools/hd/logo.py). Fehlt die Datei, bleibt alles wie bisher.
//
// Dateiformat: "TS2R", u32 Breite, u32 Höhe, dann RGBA8 zeilenweise in der
// Reihenfolge der Anzeige (oberste Zeile zuerst).
#include "menulogo.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#include "hooks.h"
#include "log.h"

namespace host {

namespace {

constexpr uint64_t kCallMenuGlow = 0x1410B5C76;  // call 0x14108A41C (Sprite ts2glow) in 0x1410B5A8C
// 0x1410B59A8 (vom Hauptmenü gerufen, wenn *0x142F1272C != -1): ts2glow nur
// ins Alpha (0x14122A6A0(0xFFFFFF)), dann Teilchen darüber; die Form des
// Scheins prägt so auch den dunklen Hof um den Schriftzug
constexpr uint64_t kCallMenuMask = 0x1410B5A55;
constexpr uint64_t kSprite = 0x14108A41C;

using SpriteFn = void(__fastcall*)(int, int, int, int, int, int, int, int, uint32_t);
SpriteFn g_sprite = nullptr;
ID3D11ShaderResourceView* g_srv = nullptr;
bool g_in_glow = false;  // Schein-Zeichnung des Hauptmenüs läuft
ID3D11ShaderResourceView* g_saved = nullptr;
bool g_seen = false;

void __fastcall menu_glow(int x0, int y0, int x1, int y1, int u0, int v0, int u1, int v1, uint32_t color) {
    g_in_glow = true;
    g_sprite(x0, y0, x1, y1, u0, v0, u1, v1, color);
    g_in_glow = false;
}

ID3D11ShaderResourceView* load(ID3D11Device* dev, const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return nullptr;
    char magic[4];
    uint32_t wh[2];
    if (!f.read(magic, 4) || std::memcmp(magic, "TS2R", 4) != 0 || !f.read(reinterpret_cast<char*>(wh), 8) ||
        wh[0] == 0 || wh[1] == 0 || wh[0] > 4096 || wh[1] > 4096) {
        log("[menülogo] %ls: Format unbekannt\n", p.c_str());
        return nullptr;
    }
    std::vector<uint8_t> px(size_t(wh[0]) * wh[1] * 4);
    if (!f.read(reinterpret_cast<char*>(px.data()), std::streamsize(px.size()))) {
        log("[menülogo] %ls: zu kurz\n", p.c_str());
        return nullptr;
    }
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = wh[0];
    d.Height = wh[1];
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA s = {px.data(), wh[0] * 4, 0};
    ID3D11Texture2D* t = nullptr;
    ID3D11ShaderResourceView* v = nullptr;
    if (SUCCEEDED(dev->CreateTexture2D(&d, &s, &t))) {
        dev->CreateShaderResourceView(t, nullptr, &v);
        t->Release();
    }
    if (v) log("[menülogo] eigener Schein für das Hauptmenü: %ux%u\n", wh[0], wh[1]);
    return v;
}

}  // namespace

bool menulogo_init(const Ts2Image& img, ID3D11Device* dev, const std::filesystem::path& game) {
    const auto p = game / L"hd" / L"ts2" / L"textures" / L"misc" / L"ts2glow_menue.rgba";
    if (!std::filesystem::exists(p)) return false;
    g_srv = load(dev, p);
    if (!g_srv) return false;
    g_sprite = reinterpret_cast<SpriteFn>(hook_call(img, kCallMenuGlow, kSprite, reinterpret_cast<const void*>(&menu_glow)));
    if (g_sprite && !hook_call(img, kCallMenuMask, kSprite, reinterpret_cast<const void*>(&menu_glow)))
        log("[menülogo] Aufruf 0x%llX unerwartet, Hof bleibt in alter Form\n", static_cast<unsigned long long>(kCallMenuMask));
    if (!g_sprite) {
        log("[menülogo] Aufruf 0x%llX unerwartet, eigener Schein aus\n", static_cast<unsigned long long>(kCallMenuGlow));
        g_srv->Release();
        g_srv = nullptr;
        return false;
    }
    return true;
}

bool menulogo_before(ID3D11DeviceContext* c) {
    if (!g_in_glow || !g_srv) return false;
    if (!g_seen) g_seen = true, log("[menülogo] Schein des Hauptmenüs mit eigener Textur gezeichnet\n");
    c->PSGetShaderResources(0, 1, &g_saved);
    c->PSSetShaderResources(0, 1, &g_srv);
    return true;
}

void menulogo_after(ID3D11DeviceContext* c) {
    c->PSSetShaderResources(0, 1, &g_saved);
    if (g_saved) g_saved->Release(), g_saved = nullptr;
}

}  // namespace host
