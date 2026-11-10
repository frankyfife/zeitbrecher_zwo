// Untersuchungs-Hülle um den D3D11-Kontext: TS2 bekommt sie statt des
// echten Kontexts (nur mit einer Probe wie TS2_DRAWDUMP). Alle Methoden
// leiten weiter (ctxproxy.inc, erzeugt aus d3d11.h); OMSetBlendState,
// DrawIndexed, Draw und ClearRenderTargetView rufen vorher die Haken unten.
// Nötig, weil die D3D11-Laufzeit die Funktionstabelle ihres Kontexts selbst
// umschreibt und eine umgelenkte vtable nicht hält.
#pragma once
#include <d3d11.h>

#include <climits>
#include <functional>

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

#include "ctxproxy.inc"

private:
    ID3D11DeviceContext* real_;
};

}  // namespace host
