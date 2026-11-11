// Untersuchungs-Hülle um den D3D11-Kontext: TS2 bekommt sie statt des
// echten Kontexts (nur mit einer Probe wie TS2_DRAWDUMP). Alle Methoden
// leiten weiter (ctxproxy.inc, erzeugt aus d3d11.h); OMSetBlendState,
// DrawIndexed, Draw und ClearRenderTargetView rufen vorher die Haken unten;
// Ziele, Ansichten, Scheren, Kopien und Abfragen rechnet mit Supersampling
// supersample.cpp um.
// Nötig, weil die D3D11-Laufzeit die Funktionstabelle ihres Kontexts selbst
// umschreibt und eine umgelenkte vtable nicht hält.
#pragma once
#include <d3d11.h>

#include <climits>
#include <functional>

#include "supersample.h"

namespace host {

class CtxProxy final : public ID3D11DeviceContext {
public:
    explicit CtxProxy(ID3D11DeviceContext* real) : real_(real) {}
    std::function<void(ID3D11BlendState*, const FLOAT*)> on_blend;
    // vor jeder Zeichnung (Anzahl, erster Index bzw. erste Ecke, Basisecke); false: auslassen
    std::function<bool(ID3D11DeviceContext*, UINT, UINT, INT)> on_draw;
    std::function<void(ID3D11RenderTargetView*, const FLOAT*)> on_clear;
    std::function<void(ID3D11DeviceContext*)> after_draw;  // nach jeder ausgeführten Zeichnung

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) || riid == __uuidof(ID3D11DeviceContext)) {
            *out = this;
            AddRef();
            return S_OK;
        }
        return real_->QueryInterface(riid, out);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return real_->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return real_->Release(); }
    // ID3D11DeviceChild
    void STDMETHODCALLTYPE GetDevice(ID3D11Device** d) override { real_->GetDevice(d); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g, UINT* n, void* p) override { return real_->GetPrivateData(g, n, p); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g, UINT n, const void* p) override {
        return real_->SetPrivateData(g, n, p);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID g, const IUnknown* p) override {
        return real_->SetPrivateDataInterface(g, p);
    }
    // Haken
    void STDMETHODCALLTYPE OMSetBlendState(ID3D11BlendState* s, const FLOAT f[4], UINT m) override {
        if (on_blend) on_blend(s, f);
        OMSetBlendState_real(s, f, m);
    }
    void STDMETHODCALLTYPE DrawIndexed(UINT n, UINT start, INT base) override {
        if (!on_draw || on_draw(real_, n, start, base)) {
            DrawIndexed_real(n, start, base);
            if (after_draw) after_draw(real_);
        }
    }
    void STDMETHODCALLTYPE Draw(UINT n, UINT start) override {
        if (!on_draw || on_draw(real_, n, start, INT_MIN)) {
            Draw_real(n, start);
            if (after_draw) after_draw(real_);
        }
    }
    void STDMETHODCALLTYPE ClearRenderTargetView(ID3D11RenderTargetView* v, const FLOAT c[4]) override {
        if (on_clear) on_clear(v, c);
        ClearRenderTargetView_real(v, c);
    }

    // Supersampling (supersample.h): TS2 sieht Ansichten, Scheren und Kopien
    // in der Render-Auflösung; ohne Supersampling nur Weiterleitung
    void STDMETHODCALLTYPE OMSetRenderTargets(UINT n, ID3D11RenderTargetView* const* v,
                                              ID3D11DepthStencilView* d) override {
        OMSetRenderTargets_real(n, v, d);
        if (ss_on()) ss_set_targets(real_, n && v ? v[0] : nullptr, d);
    }
    void STDMETHODCALLTYPE OMSetRenderTargetsAndUnorderedAccessViews(UINT n, ID3D11RenderTargetView* const* v,
                                                                     ID3D11DepthStencilView* d, UINT us, UINT un,
                                                                     ID3D11UnorderedAccessView* const* u,
                                                                     const UINT* uc) override {
        OMSetRenderTargetsAndUnorderedAccessViews_real(n, v, d, us, un, u, uc);
        if (ss_on() && n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) ss_set_targets(real_, n && v ? v[0] : nullptr, d);
    }
    void STDMETHODCALLTYPE RSSetViewports(UINT n, const D3D11_VIEWPORT* v) override {
        if (ss_on()) ss_set_viewports(real_, n, v);
        else RSSetViewports_real(n, v);
    }
    void STDMETHODCALLTYPE RSGetViewports(UINT* n, D3D11_VIEWPORT* v) override {
        if (ss_on()) ss_get_viewports(n, v);
        else RSGetViewports_real(n, v);
    }
    void STDMETHODCALLTYPE RSSetScissorRects(UINT n, const D3D11_RECT* r) override {
        if (ss_on()) ss_set_scissors(real_, n, r);
        else RSSetScissorRects_real(n, r);
    }
    void STDMETHODCALLTYPE RSGetScissorRects(UINT* n, D3D11_RECT* r) override {
        if (ss_on()) ss_get_scissors(n, r);
        else RSGetScissorRects_real(n, r);
    }
    void STDMETHODCALLTYPE CopySubresourceRegion(ID3D11Resource* dst, UINT dsub, UINT x, UINT y, UINT z,
                                                 ID3D11Resource* src, UINT ssub, const D3D11_BOX* box) override {
        if (!ss_on() || !ss_copy_region(real_, dst, dsub, x, y, z, src, ssub, box))
            CopySubresourceRegion_real(dst, dsub, x, y, z, src, ssub, box);
    }
    void STDMETHODCALLTYPE CopyResource(ID3D11Resource* dst, ID3D11Resource* src) override {
        if (!ss_on() || !ss_copy_resource(real_, dst, src)) CopyResource_real(dst, src);
    }
    void STDMETHODCALLTYPE UpdateSubresource(ID3D11Resource* dst, UINT sub, const D3D11_BOX* box, const void* data,
                                             UINT row, UINT depth) override {
        if (ss_on()) ss_update_subresource(dst);
        UpdateSubresource_real(dst, sub, box, data, row, depth);
    }
    HRESULT STDMETHODCALLTYPE GetData(ID3D11Asynchronous* a, void* data, UINT size, UINT flags) override {
        const HRESULT hr = GetData_real(a, data, size, flags);
        if (hr == S_OK && ss_on()) ss_get_data(a, data, size);
        return hr;
    }
    void STDMETHODCALLTYPE ClearState() override {
        ClearState_real();
        if (ss_on()) ss_clear_state();
    }

#include "ctxproxy.inc"

private:
    ID3D11DeviceContext* real_;
};

}  // namespace host
