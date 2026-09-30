// ---------------------------------------------------------------- Graphics
//
// **The device, which everything on screen shares.** Direct3D's device, the DXGI device it came from,
// the Direct2D factory and the DirectComposition device are per *machine* rather than per window: what
// is per window is a *surface* -- a swap chain, a target for it, and the bitmap and device context that
// draw into it. Sharing them is not an optimization so much as the thing that makes a second window
// possible at all: two devices would mean two Direct2D factories and two sets of resources, with no way
// to lay a decoded image or a glyph cache out once.
//
// **Held for the process and never released.** A program has one of these until it exits, and a device
// released by whichever window happened to close first is a device the next window cannot use. That is
// also why nothing here is reference counted: there is no last owner to hand it back.
//
// A surface takes what it needs from `Device()` and keeps only what is its own -- see
// `Window::CreateDevice`.
#ifndef MICULA_GRAPHICS_H
#define MICULA_GRAPHICS_H

#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dcomp.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dcomp.lib")

namespace micula {

struct Graphics {
    ID3D11Device        *d3d = nullptr;
    IDXGIDevice         *dxgi = nullptr;
    IDXGIFactory2       *factory = nullptr;   // the adapter's own, which is what makes a swap chain
    ID2D1Factory1       *d2d = nullptr;
    ID2D1Device         *d2dDevice = nullptr;
    IDCompositionDevice *comp = nullptr;

    bool Ready() const { return d3d && dxgi && factory && d2d && d2dDevice && comp; }
    void Acquire();
};

inline void Graphics::Acquire() {
    if (d3d) return;
    // BGRA support is required by Direct2D and is not on by default. Hardware first, WARP second: a
    // window has to appear on a machine with a broken or absent display driver, because one of the
    // things it may have to say is why.
    const D3D_DRIVER_TYPE types[] = { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP };
    for (D3D_DRIVER_TYPE t : types) {
        if (SUCCEEDED(D3D11CreateDevice(nullptr, t, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                        nullptr, 0, D3D11_SDK_VERSION, &d3d, nullptr, nullptr)))
            break;
    }
    if (!d3d) return;
    if (FAILED(d3d->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi))) return;

    D2D1_FACTORY_OPTIONS opts = {};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts,
                                 (void **)&d2d)))
        return;
    if (FAILED(d2d->CreateDevice(dxgi, &d2dDevice))) return;
    if (FAILED(DCompositionCreateDevice(dxgi, __uuidof(IDCompositionDevice), (void **)&comp))) return;

    IDXGIAdapter *adapter = nullptr;
    if (FAILED(dxgi->GetAdapter(&adapter)) || !adapter) return;
    const HRESULT hr = adapter->GetParent(__uuidof(IDXGIFactory2), (void **)&factory);
    adapter->Release();
    (void)hr;
}

// One per process, made on the first call that needs it.
inline Graphics &Device() {
    static Graphics g;
    g.Acquire();
    return g;
}

}  // namespace micula

#endif  // MICULA_GRAPHICS_H
