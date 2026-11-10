#include "alphafix.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <unordered_map>
#include <vector>

#include "log.h"

namespace host {

namespace {

struct Fixed {
    ID3D11Texture2D* tex = nullptr;  // Kopie mit Alpha aus der Helligkeit, nullptr: Textur bleibt
    ID3D11ShaderResourceView* srv = nullptr;
};

// Ersatzinhalt aus game/xbox_alpha.bin (tools/re/xbox_alpha.py, aus dem
// eigenen Xbox-Image): je Inhalts-Hash der PC-Textur RGBA in Upload-Reihenfolge
struct XboxEntry {
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> rgba;
};

ID3D11Device* g_dev = nullptr;
bool g_on = false;
std::map<ID3D11Texture2D*, Fixed> g_fixed;  // je Textur einmal geprüft
std::unordered_map<uint32_t, XboxEntry> g_xbox;
ID3D11ShaderResourceView* g_saved_srv = nullptr;

uint8_t brightness(const uint8_t* p) { return std::max({p[0], p[1], p[2]}); }

bool load_table(const std::filesystem::path& file) {
    FILE* f = _wfopen(file.c_str(), L"rb");
    if (!f) return false;
    char magic[4] = {};
    uint32_t n = 0;
    bool ok = fread(magic, 1, 4, f) == 4 && std::equal(magic, magic + 4, "TS2A") && fread(&n, 4, 1, f) == 1;
    for (uint32_t i = 0; ok && i < n; ++i) {
        uint32_t head[3];
        ok = fread(head, 4, 3, f) == 3 && head[1] && head[2] && head[1] <= 4096 && head[2] <= 4096;
        if (!ok) break;
        XboxEntry e;
        e.w = head[1];
        e.h = head[2];
        e.rgba.resize(size_t(e.w) * e.h * 4);
        ok = fread(e.rgba.data(), 1, e.rgba.size(), f) == e.rgba.size();
        if (ok) g_xbox.emplace(head[0], std::move(e));
    }
    fclose(f);
    if (!ok) g_xbox.clear();
    return ok;
}

// FNV-1a über die oberste Stufe (wie texture_hash in main.cpp)
uint32_t content_hash(const std::vector<uint8_t>& px) {
    uint32_t h = 2166136261u;
    for (uint8_t b : px) h = (h ^ b) * 16777619u;
    return h;
}

// kleinere Stufen: Mittel aus 2×2
void downsample(const std::vector<uint8_t>& src, UINT w, UINT h, std::vector<uint8_t>& dst) {
    const UINT nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
    dst.assign(size_t(nw) * nh * 4, 0);
    for (UINT y = 0; y < nh; ++y)
        for (UINT x = 0; x < nw; ++x) {
            const UINT x0 = std::min(2 * x, w - 1), x1 = std::min(2 * x + 1, w - 1);
            const UINT y0 = std::min(2 * y, h - 1), y1 = std::min(2 * y + 1, h - 1);
            for (int k = 0; k < 4; ++k) {
                const int sum = src[(size_t(y0) * w + x0) * 4 + k] + src[(size_t(y0) * w + x1) * 4 + k] +
                                src[(size_t(y1) * w + x0) * 4 + k] + src[(size_t(y1) * w + x1) * 4 + k];
                dst[(size_t(y) * nw + x) * 4 + k] = uint8_t((sum + 2) / 4);
            }
        }
}

// Textur bis 256×256 aus game/xbox_alpha.bin: Xbox-Inhalt. Sonst
// Leuchttextur ohne Alpha: 4×4 bis 64×64, RGBA8, Alpha überall 255, Ecken
// schwarz, mindestens ein Viertel der Fläche schwarz und irgendwo hell
// (Strahl oder Schein auf Schwarz). Größere deckende Texturen mit schwarzen
// Flächen (Figuren, Schilder) und ganz schwarze bleiben, wie sie sind.
Fixed check_texture(ID3D11DeviceContext* c, ID3D11Texture2D* tex) {
    Fixed out;
    D3D11_TEXTURE2D_DESC d;
    tex->GetDesc(&d);
    if (d.Width > 256 || d.Height > 256 || d.Width < 4 || d.Height < 4 || d.Format != DXGI_FORMAT_R8G8B8A8_UNORM || d.ArraySize != 1 ||
        d.SampleDesc.Count != 1 || (d.BindFlags & D3D11_BIND_DEPTH_STENCIL) || !(d.BindFlags & D3D11_BIND_SHADER_RESOURCE))
        return out;
    D3D11_TEXTURE2D_DESC sd = d;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;
    ID3D11Texture2D* st = nullptr;
    if (FAILED(g_dev->CreateTexture2D(&sd, nullptr, &st))) return out;
    c->CopyResource(st, tex);
    // alle Stufen lesen; Stufe 0 entscheidet
    std::vector<std::vector<uint8_t>> levels(d.MipLevels);
    bool ok = true;
    for (UINT m = 0; m < d.MipLevels && ok; ++m) {
        const UINT w = std::max(1u, d.Width >> m), h = std::max(1u, d.Height >> m);
        D3D11_MAPPED_SUBRESOURCE map;
        if (FAILED(c->Map(st, m, D3D11_MAP_READ, 0, &map))) {
            ok = false;
            break;
        }
        levels[m].resize(size_t(w) * h * 4);
        for (UINT y = 0; y < h; ++y)
            std::copy_n(static_cast<const uint8_t*>(map.pData) + size_t(y) * map.RowPitch, w * 4,
                        levels[m].data() + size_t(y) * w * 4);
        c->Unmap(st, m);
    }
    st->Release();
    if (!ok) return out;
    const UINT w = d.Width, h = d.Height;
    // Xbox-Inhalt aus der Tabelle (genau), sonst Alpha aus der Helligkeit (Schätzung)
    const XboxEntry* xe = nullptr;
    if (!g_xbox.empty()) {
        auto it = g_xbox.find(content_hash(levels[0]));
        if (it != g_xbox.end() && it->second.w == w && it->second.h == h) xe = &it->second;
    }
    if (xe) {
        levels[0] = xe->rgba;
        for (UINT m = 1; m < d.MipLevels; ++m)
            downsample(levels[m - 1], std::max(1u, w >> (m - 1)), std::max(1u, h >> (m - 1)), levels[m]);
        std::vector<D3D11_SUBRESOURCE_DATA> init(d.MipLevels);
        for (UINT m = 0; m < d.MipLevels; ++m) init[m] = {levels[m].data(), std::max(1u, w >> m) * 4, 0};
        D3D11_TEXTURE2D_DESC nd = d;
        nd.Usage = D3D11_USAGE_IMMUTABLE;
        nd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        nd.CPUAccessFlags = 0;
        nd.MiscFlags = 0;
        if (FAILED(g_dev->CreateTexture2D(&nd, init.data(), &out.tex))) return Fixed{};
        if (FAILED(g_dev->CreateShaderResourceView(out.tex, nullptr, &out.srv))) {
            out.tex->Release();
            return Fixed{};
        }
        log("Transparenz: Textur %p (%ux%u) bekommt den Xbox-Inhalt (xbox_alpha.bin)\n", static_cast<void*>(tex), w, h);
        return out;
    }
    if (w > 64 || h > 64) return out;  // Schätzung nur für kleine Leuchttexturen
    const std::vector<uint8_t>& l0 = levels[0];
    size_t dark = 0;
    uint8_t peak = 0;
    for (size_t i = 0; i < size_t(w) * h; ++i) {
        if (l0[i * 4 + 3] != 255) return out;  // hat schon Alpha
        dark += brightness(&l0[i * 4]) <= 8;
        peak = std::max(peak, brightness(&l0[i * 4]));
    }
    const size_t corners[4] = {0, w - 1, size_t(h - 1) * w, size_t(h) * w - 1};
    for (size_t k : corners)
        if (brightness(&l0[k * 4]) > 16) return out;
    // ganz schwarze Texturen (black1x1: Abblenden) bleiben deckend
    if (dark * 4 < size_t(w) * h || peak < 64) return out;
    // Kopie: Alpha = größter Farbkanal, in allen Stufen
    std::vector<D3D11_SUBRESOURCE_DATA> init(d.MipLevels);
    for (UINT m = 0; m < d.MipLevels; ++m) {
        std::vector<uint8_t>& px = levels[m];
        for (size_t i = 0; i < px.size(); i += 4) px[i + 3] = brightness(&px[i]);
        init[m] = {px.data(), std::max(1u, w >> m) * 4, 0};
    }
    D3D11_TEXTURE2D_DESC nd = d;
    nd.Usage = D3D11_USAGE_IMMUTABLE;
    nd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    nd.CPUAccessFlags = 0;
    nd.MiscFlags = 0;
    if (FAILED(g_dev->CreateTexture2D(&nd, init.data(), &out.tex))) return Fixed{};
    if (FAILED(g_dev->CreateShaderResourceView(out.tex, nullptr, &out.srv))) {
        out.tex->Release();
        return Fixed{};
    }
    log("Transparenz: Textur %p (%ux%u, %zu%% schwarz) bekommt Alpha aus der Helligkeit\n", static_cast<void*>(tex), w,
        h, dark * 100 / (size_t(w) * h));
    return out;
}

}  // namespace

bool alphafix_init(ID3D11Device* dev, bool on, const std::filesystem::path& table) {
    g_dev = dev;
    g_on = on;
    if (!on) return false;
    if (load_table(table))
        log("Transparenz: %zu Texturen mit Xbox-Inhalt aus %ls, übrige Leuchttexturen: Alpha aus der Helligkeit\n",
            g_xbox.size(), table.c_str());
    else
        log("Transparenz: Leuchttexturen ohne Alpha bekommen Alpha aus der Helligkeit (%ls fehlt)\n", table.c_str());
    return on;
}

bool alphafix_before(ID3D11DeviceContext* c) {
    if (!g_on) return false;
    ID3D11ShaderResourceView* srv = nullptr;
    c->PSGetShaderResources(0, 1, &srv);
    if (!srv) return false;
    ID3D11Resource* r = nullptr;
    srv->GetResource(&r);
    ID3D11Texture2D* tex = nullptr;
    if (r) {
        r->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex));
        r->Release();
    }
    if (!tex) {
        srv->Release();
        return false;
    }
    auto it = g_fixed.find(tex);
    if (it == g_fixed.end()) it = g_fixed.emplace(tex, check_texture(c, tex)).first;
    tex->Release();
    if (!it->second.srv) {
        srv->Release();
        return false;
    }
    g_saved_srv = srv;  // Verweis bleibt bis alphafix_after
    c->PSSetShaderResources(0, 1, &it->second.srv);
    return true;
}

void alphafix_after(ID3D11DeviceContext* c) {
    c->PSSetShaderResources(0, 1, &g_saved_srv);
    if (g_saved_srv) g_saved_srv->Release(), g_saved_srv = nullptr;
}

void alphafix_forget(ID3D11Texture2D* tex) {
    auto it = g_fixed.find(tex);
    if (it == g_fixed.end()) return;
    if (it->second.srv) it->second.srv->Release();
    if (it->second.tex) it->second.tex->Release();
    g_fixed.erase(it);
}

}  // namespace host
