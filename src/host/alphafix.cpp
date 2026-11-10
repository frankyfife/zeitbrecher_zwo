#include "alphafix.h"

#include <algorithm>
#include <map>
#include <vector>

#include "log.h"

namespace host {

namespace {

struct Fixed {
    ID3D11Texture2D* tex = nullptr;  // Kopie mit Alpha aus der Helligkeit, nullptr: Textur bleibt
    ID3D11ShaderResourceView* srv = nullptr;
};

ID3D11Device* g_dev = nullptr;
bool g_on = false;
std::map<ID3D11Texture2D*, Fixed> g_fixed;  // je Textur einmal geprüft
ID3D11ShaderResourceView* g_saved_srv = nullptr;

uint8_t brightness(const uint8_t* p) { return std::max({p[0], p[1], p[2]}); }

// Leuchttextur ohne Alpha: 4×4 bis 64×64, RGBA8, Alpha überall 255, Ecken
// schwarz, mindestens ein Viertel der Fläche schwarz und irgendwo hell
// (Strahl oder Schein auf Schwarz). Größere deckende Texturen mit schwarzen
// Flächen (Figuren, Schilder) und ganz schwarze bleiben, wie sie sind.
Fixed check_texture(ID3D11DeviceContext* c, ID3D11Texture2D* tex) {
    Fixed out;
    D3D11_TEXTURE2D_DESC d;
    tex->GetDesc(&d);
    if (d.Width > 64 || d.Height > 64 || d.Width < 4 || d.Height < 4 || d.Format != DXGI_FORMAT_R8G8B8A8_UNORM || d.ArraySize != 1 ||
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
    const std::vector<uint8_t>& l0 = levels[0];
    const UINT w = d.Width, h = d.Height;
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

bool alphafix_init(ID3D11Device* dev, bool on) {
    g_dev = dev;
    g_on = on;
    if (on) log("Transparenz: Leuchttexturen ohne Alpha bekommen Alpha aus der Helligkeit\n");
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
