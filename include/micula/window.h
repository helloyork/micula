// Micula / window.h
//
// A Windows 11 window: Mica behind it, Direct2D on top of DirectComposition, and no
// child controls at all.
//
// Why the composition path rather than an HWND render target
// ----------------------------------------------------------
// Because of Mica, and Mica is not a colour -- it is a *material DWM draws behind the
// window*, sampled from the desktop wallpaper. For any of it to be visible the window
// has to hand DWM pixels with alpha in them, and an `ID2D1HwndRenderTarget` cannot:
// HWND render targets are documented as supporting `D2D1_ALPHA_MODE_IGNORE` only, so
// every pixel it produces is opaque and the material is covered by whatever the page
// painted. The first version of this file used one, painted an opaque `#F3F3F3`, and
// looked like a themed dialog rather than like a Windows 11 application.
//
// So the window is composed instead, the same way WinUI 3 composes its own:
//
//   WS_EX_NOREDIRECTIONBITMAP    no redirection surface, so nothing opaque is
//                                allocated for the window at all
//   swap chain for composition   B8G8R8A8 with DXGI_ALPHA_MODE_PREMULTIPLIED
//   DirectComposition visual     the swap chain's content, targeted at the HWND
//   DWMWA_SYSTEMBACKDROP_TYPE    DWMSBT_MAINWINDOW -- Mica
//
// The cost, and it is not small: **`WS_EX_NOREDIRECTIONBITMAP` means child HWNDs do
// not render.** There is no redirection surface for USER32 to draw them into. So the
// text field that used to be a real EDIT control is gone, and widgets.h has a drawn
// one in its place with its own caret, clipboard and IME handling. That is a real
// regression in exactly one control and it is the price of the whole window looking
// native rather than nearly native.
//
// What happens on a machine without Mica
// --------------------------------------
// `DWMWA_SYSTEMBACKDROP_TYPE` arrived in Windows 11 22H2 (22621). On anything older
// DwmSetWindowAttribute returns an error, `micaActive` stays false, and the page
// paints an opaque background instead -- the window still composes, it just has a flat
// colour where the material would be. Checked rather than assumed, because Windows 10
// and the first Windows 11 release are machines this window still has to appear on.
//
// The title bar is drawn too, and it has to be
// --------------------------------------------
// The first version of this file left the caption to DWM, on the reasoning that DWM
// draws a good one and a hand-drawn caption is where snap layouts and the window menu
// go to die. That reasoning was sound and the result was wrong: on a machine with
// "show accent colour on title bars" switched on -- which is a default on many
// installs -- DWM paints the caption a flat accent colour, and **the backdrop stops at
// the bottom of it**. The window body showed Mica and had a solid brick-red bar across
// the top. Half a Mica window looks worse than none.
//
// So the caption is ours: `WM_NCCALCSIZE` gives the whole window to the client area,
// the material then covers every pixel, and the title, the icon and the three buttons
// are painted in the same pass as the page. What that costs is spelled out at each
// handler below, and the two things worth not losing are kept rather than
// reimplemented -- `WM_NCHITTEST` still answers `HTMAXBUTTON` over the maximise
// button, so Windows 11's snap-layout flyout appears on hover, and `HTCAPTION`
// everywhere else, so dragging, double-click-to-maximise and the right-click window
// menu are still DefWindowProc's job.
//
// No accessibility. Every control is drawn and none publishes a UI Automation
// provider, so a screen reader sees one window and nothing in it. This is the largest
// known gap in Micula and it is stated here rather than papered over: a program that
// must be usable without sight should offer the same operations another way -- a
// command line, a config file -- until this is closed.

#pragma once

#include "theme.h"
#include "view.h"                   // the root widget a page builds into, and what a card puts a control in
#include "widget.h"                 // the tree: Widget, Layout, Spec -- see docs/layout.md

#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <imm.h>
#include <oleauto.h>                        // SysAllocString, SafeArray, the VARIANTs UIA is answered in
#include <uiautomationclient.h>             // the UIA_* property, pattern and control-type ids
#include <uiautomationcore.h>
#include <uiautomationcoreapi.h>
#include <wincodec.h>
#include <windowsx.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "imm32.lib")
// UIAutomationCore is not here on purpose: its four functions are looked up at run time, which is
// what keeps this file from adding a load-time dependency to every program that includes it. See
// the uiaapi namespace below.
#pragma comment(lib, "oleaut32.lib")      // SysAllocString, for the strings UIA is handed
#pragma comment(lib, "windowscodecs.lib")
// And the four a Visual Studio project links by default and a bare `cl` does not.
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

namespace micula {

// ------------------------------------------------------------------ the DPI calls
//
// **Resolved at run time, and the reason is the same one frameclock::Resolve gives.**
//
// These three live in user32 and none of them has always been there:
//
//     GetDpiForWindow           Windows 10 1607 (14393)
//     GetSystemMetricsForDpi    Windows 10 1607
//     SetProcessDpiAwarenessContext  Windows 10 1703 (15063)
//
// Called directly they are bound by the loader, so a machine without one of them does
// not run a program that names it -- it ends the process with STATUS_ENTRYPOINT_NOT_FOUND
// before `wWinMain`, with no window and no message. A header library cannot know which
// Windows its host program promises to support, so it must not quietly raise that
// floor for it. The compositor clock in Window::Run is resolved the same way, for the
// same reason.
//
// The fallbacks are the pre-1607 answers, not stubs: system DPI for the window, the
// plain metrics call, and the process-wide awareness Windows 8.1 and Vista offer. A
// machine old enough to take them is a machine on which per-monitor DPI did not exist,
// so nothing is being taken away from it.
namespace dpiapi {

inline HMODULE User32() {
    static const HMODULE m = GetModuleHandleW(L"user32.dll");
    return m;
}

// Pixels per layout unit for the monitor this window is on; 96 when nothing can say.
inline UINT ForWindow(HWND hwnd) {
    using Fn = UINT(WINAPI *)(HWND);
    static const Fn fn = User32() ? (Fn)GetProcAddress(User32(), "GetDpiForWindow") : nullptr;
    if (fn) return fn(hwnd);
    // The system DPI, which is what every window had before per-monitor existed.
    HDC dc = GetDC(nullptr);
    const UINT d = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return d ? d : 96;
}

inline int SystemMetric(int index, UINT dpi) {
    using Fn = int(WINAPI *)(int, UINT);
    static const Fn fn =
        User32() ? (Fn)GetProcAddress(User32(), "GetSystemMetricsForDpi") : nullptr;
    if (fn) return fn(index, dpi);
    return GetSystemMetrics(index);
}

// Whether anything has said what this process's DPI awareness is yet: a manifest, a call by the
// program, or a previous window. Only an unaware process leaves it to Create to say, which is what
// happens on a launch with no manifest at all; anything else is an answer somebody meant, and a
// library that overrode a System-aware manifest would be the third party nobody asked for. A
// machine that cannot answer says yes, which leaves it exactly where it is -- and it is also a
// machine where per-monitor v2 does not exist to be set.
//
// Two ways of asking, because there is no one call for this. The process's own answer is shcore's
// GetProcessDpiAwareness, which arrived with Windows 8.1 and is loaded rather than linked here for
// the same reason EnablePerMonitorV2 loads it. The thread's answer is the user32 pair, and a
// thread's awareness is its process's until something overrides it -- which is true here by
// construction, because this runs before the window it is called from exists.
//
// (There is no GetProcessDpiAwarenessContext. This asked user32 for that name first, and asking is
// how it was found out: the process question and the thread question are different calls.)
inline bool AwarenessSettled() {
    // PROCESS_DPI_AWARENESS, spelled out because shellscalingapi.h is not otherwise wanted here:
    // 0 unaware, 1 system-aware, 2 per-monitor.
    using ProcessFn = HRESULT(WINAPI *)(HANDLE, int *);
    if (HMODULE sh = LoadLibraryW(L"shcore.dll"))
        if (auto get = (ProcessFn)GetProcAddress(sh, "GetProcessDpiAwareness")) {
            int aware = 0;
            if (SUCCEEDED(get(GetCurrentProcess(), &aware))) return aware != 0;
        }
    using CtxFn = DPI_AWARENESS_CONTEXT(WINAPI *)(void);
    using FromFn = DPI_AWARENESS(WINAPI *)(DPI_AWARENESS_CONTEXT);
    static const CtxFn context =
        User32() ? (CtxFn)GetProcAddress(User32(), "GetThreadDpiAwarenessContext") : nullptr;
    static const FromFn from = User32()
        ? (FromFn)GetProcAddress(User32(), "GetAwarenessFromDpiAwarenessContext") : nullptr;
    if (context && from) {
        const DPI_AWARENESS_CONTEXT c = context();
        if (c) return from(c) != DPI_AWARENESS_UNAWARE;
    }
    return true;
}

// Per-monitor v2 where it exists, and the best available awareness where it does not.
// Returns what it managed, for nobody in particular: the caller cannot do anything
// with the answer, and the manifest is what takes effect on a normal launch anyway.
inline void EnablePerMonitorV2() {
    using CtxFn = BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT);
    if (User32())
        if (auto set = (CtxFn)GetProcAddress(User32(), "SetProcessDpiAwarenessContext"))
            if (set(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    // Windows 8.1: per-monitor v1, through shcore. Loaded rather than linked, so this
    // does not add a library to a build that would otherwise not need one.
    using AwareFn = HRESULT(WINAPI *)(int);
    if (HMODULE sh = LoadLibraryW(L"shcore.dll"))
        if (auto set = (AwareFn)GetProcAddress(sh, "SetProcessDpiAwareness"))
            if (SUCCEEDED(set(2))) return;             // PROCESS_PER_MONITOR_DPI_AWARE
    SetProcessDPIAware();
}

}  // namespace dpiapi

// The COM apartment, for the thread that makes a window.
//
// WIC needs it -- the caption icon and Window::Image go through it -- UI Automation needs it, and
// the OleInitialize that drag and drop will need requires this same apartment-threaded kind. It is
// what a program used to have to do by hand in wWinMain, and what it now only has to do if it
// wants to: a thread that is already in an apartment gets S_FALSE, which is the program's own
// initialisation, and this then owns nothing and uninitialises nothing.
//
// **Why here and not in App.** The first window is made before the App exists -- `Window w;
// w.Create(...); App app; app.Add(w);` is the shape every example has, and Window::Run is the same
// order in three lines -- so anything App did in its constructor would be a step too late.
//
// **Why not a global object.** Static initialisation order is not this library's to decide, a
// `static` inside an inline function is one per module in a header-only library, and an apartment
// belongs to a thread: a process-wide object owning a thread-affine resource is the wrong shape,
// which is the same reason App is not a singleton.
namespace detail {

struct ComApartment {
    ComApartment() {
        // S_OK is this thread's apartment being made now, and only that one is this object's to
        // take down again. S_FALSE is "already in one", which belongs to whoever put it there.
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        owned = (hr == S_OK);
    }
    ~ComApartment() { if (owned) CoUninitialize(); }
    bool owned;
};

inline bool EnsureCom() {
    // One per thread, made on first use and destroyed as the thread ends -- which is exactly how
    // long an apartment lasts.
    thread_local ComApartment apartment;
    return apartment.owned;
}

}  // namespace detail

// DWM attributes that are not in every SDK header. Spelled out so this builds against
// whatever the machine has, and every call is checked -- an older Windows returns
// E_INVALIDARG for an attribute it does not know, which is the signal, not a failure.
constexpr DWORD kDwmImmersiveDarkMode = 20;
constexpr DWORD kDwmCornerPreference  = 33;
constexpr DWORD kDwmSystemBackdrop    = 38;
constexpr DWORD kDwmCornerRound       = 2;
// DWMWA_SYSTEMBACKDROP_TYPE's values, which the SDK this builds against may not name.
constexpr DWORD kDwmBackdropAuto       = 0;   // DWMSBT_AUTO: let DWM choose
constexpr DWORD kDwmBackdropNone       = 1;   // DWMSBT_NONE
constexpr DWORD kDwmBackdropMainWindow = 2;   // DWMSBT_MAINWINDOW == Mica
constexpr DWORD kDwmBackdropAcrylic    = 3;   // DWMSBT_TRANSIENTWINDOW
constexpr DWORD kDwmBackdropTabbed     = 4;   // DWMSBT_TABBEDWINDOW == Mica Alt

// The three system cursors, as wide resource ids. IDC_ARROW and its siblings are
// MAKEINTRESOURCE, which follows UNICODE -- in a program built without it they are
// narrow strings, and LoadCursorW refuses them. Micula calls the W functions
// throughout and must not depend on which way the program sets that macro.
inline const LPCWSTR kCursorArrow = MAKEINTRESOURCEW(32512);   // IDC_ARROW
inline const LPCWSTR kCursorIBeam = MAKEINTRESOURCEW(32513);   // IDC_IBEAM
inline const LPCWSTR kCursorHand  = MAKEINTRESOURCEW(32649);   // IDC_HAND

// The caption, in DIPs. 32 tall and 46 wide per button are Windows 11's own numbers --
// measured off its own windows rather than derived from SM_CYCAPTION, which still
// answers with the Windows 7 caption height plus a frame.
constexpr float kCaptionH    = 32.0f;
constexpr float kCaptionBtnW = 46.0f;
// How close to an edge counts as a resize grip. The frame is gone as far as the client
// area is concerned, so this is synthesised in WM_NCHITTEST.
constexpr float kResizeGrip  = 6.0f;
// How far a finger may wander and still be a tap, in DIPs: about what a fingertip is worth of error,
// and less than any gesture the window will recognize later. A mouse has no such number -- it is a
// machine pointing at a pixel -- and a pen has a small one, but this is not it.
constexpr float kTouchSlop   = 8.0f;

// Defined beside the frame clock further down, and declared here because the input path is written
// before it: a fling is a speed, and a speed is a distance over a time.
inline double MonotonicSeconds();

// ---------------------------------------------------------------- Painter

// Everything drawn goes through one of these. It holds the device context, the fonts
// and the palette, plus a single reusable solid-colour brush.
//
// One brush, recoloured per call, rather than a brush per colour. Creating a brush is
// cheap in Direct2D and the alternative -- a cache keyed by colour -- is a table that
// has to be invalidated on every theme change and on every device loss, which is two
// more places to get wrong for no measurable gain in a window that repaints when
// somebody moves the mouse.
struct Painter {
    ID2D1DeviceContext   *rt = nullptr;
    ID2D1SolidColorBrush *br = nullptr;
    const Fonts   *font = nullptr;
    const Palette *pal  = nullptr;

    ID2D1Brush *Brush(const D2D1_COLOR_F &c) const { br->SetColor(c); return br; }

    void Fill(const D2D1_RECT_F &r, const D2D1_COLOR_F &c) const {
        rt->FillRectangle(r, Brush(c));
    }
    void FillRound(const D2D1_RECT_F &r, float radius, const D2D1_COLOR_F &c) const {
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), Brush(c));
    }
    // Stroked on the *inside* of the rectangle, and the corner comes in with it.
    //
    // Two things this gets right that a plain DrawRoundedRectangle does not. A 1-DIP stroke
    // centred on the edge straddles the pixel boundary and comes out as two half-covered rows of
    // pixels, which at 100% scaling reads as a blurry grey line instead of a crisp one -- so the
    // rectangle is inset by half of it. And the radius has to be reduced by the same half, or the
    // four curves stop being concentric with the shape they follow: the edge of an 8-DIP corner,
    // inset by 0.5, is a 7.5-DIP corner. That arithmetic -- outer radius minus the margin between
    // the two shapes is the inner radius -- is the same one `Panel` does for its border, and
    // anything else shows as a corner that is not quite round where the stroke meets it.
    void StrokeRound(const D2D1_RECT_F &r, float radius, const D2D1_COLOR_F &c,
                     float width = 1.0f) const {
        const D2D1_RECT_F in = { r.left + width / 2, r.top + width / 2,
                                 r.right - width / 2, r.bottom - width / 2 };
        const float rad = radius > width / 2 ? radius - width / 2 : 0.0f;
        rt->DrawRoundedRectangle(D2D1::RoundedRect(in, rad, rad), Brush(c), width);
    }
    void Line(float x0, float y0, float x1, float y1, const D2D1_COLOR_F &c,
              float width = 1.0f) const {
        rt->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), Brush(c), width);
    }
    // The shadow a surface over the page casts: a flyout, a dialog, a menu, a pane arriving.
    //
    // Fluent's is a blur, and there is no cheap real blur in Direct2D without an effect and a layer
    // per frame -- so this is a stack of rounded rectangles at a fraction of a per cent each, the
    // largest and faintest outermost, which read as one falloff. It has to be a stack: five at four
    // per cent was a black band with an edge on it, and the outermost step has to be faint enough to
    // disappear rather than to end.
    //
    // `reach` is how far it spreads and `drop` how far it sits below the surface, so that the light
    // reads as being above. **What reads as height is the spread rather than the darkness**: over a
    // dim, a merely darker shadow has nowhere left to be darker *than*.
    //
    // `strength` is how much darkness has piled up where the shadow meets the surface. Fluent's own
    // numbers are the reference: its ambient shadow is a blur of 8 with no offset at 20 per cent,
    // and on Windows the sharp half of the pair -- `blur 64, y 32` at the high end -- is replaced by
    // the 1-DIP stroke every elevation in Windows 11 has. So a dialog over a dim wants a *tighter*
    // shadow than it looks like it should, plus a contour somebody can see; a flyout wants a narrow
    // one and no stroke, because the page it hangs off is already the contrast.
    //
    // `opacity` fades the whole thing with whatever the caller is already animating. A surface that
    // fades while its shadow stays is the one thing that gives a fade away.
    void Shadow(const D2D1_RECT_F &r, float radius, float opacity = 1.0f, float reach = 14.0f,
                float drop = 4.0f, int layers = 12, float strength = 0.11f) const {
        for (int i = layers; i >= 1; i--) {
            const float e = reach * (float)i / (float)layers;
            // Quadratic, not one alpha for every layer. A flat stack has an outermost layer as dark
            // as its innermost, so a long shadow *ends* on a step rather than fading into the
            // surface -- the one edge that gives a fake blur away, and the reason the weights square
            // as they go out. They sum to about a third of the layers, which is where the divisor
            // comes from: what lands on the surface is `strength`, whatever `layers` is.
            const float w = (float)(layers - i + 1) / (float)layers;
            FillRound({ r.left - e, r.top - e + drop, r.right + e, r.bottom + e + drop },
                      radius + e, Rgb(0x000000, strength * opacity * w * w * 3.0f / (float)layers));
        }
    }
    // A panel whose four corners are chosen one by one, and whose border runs along any
    // combination of its edges -- WinUI's content layer is one rounded corner, three square ones,
    // a border along the two edges that face the rest of the window, and none along the two that
    // are the window.
    //
    // The shape is a path because it has to be: `FillRoundedRectangle` takes one radius for all
    // four corners, and a page's layer has exactly one of them rounded. Drawing it as a rounded
    // rectangle with the square corners patched on over the top is the same picture for an opaque
    // colour -- and a band of a lighter colour down every edge it passes for a translucent one,
    // which is what a layer colour is: two coats in one place and one everywhere else. Over Mica
    // that is a hint; over Acrylic it is a band you can measure.
    //
    // The path is built per call rather than cached, which is what a page with a handful of
    // panels can afford. A page that draws hundreds of them wants its own cache.
    void Panel(const D2D1_RECT_F &r, const Corners &c, const D2D1_COLOR_F &fill,
               const D2D1_COLOR_F &border = D2D1::ColorF(0, 0.0f),
               unsigned edges = edge::kAll, float width = 1.0f) const {
        ID2D1Factory *factory = nullptr;
        rt->GetFactory(&factory);
        if (!factory) return;
        // One figure per run of neighbouring pieces, and the pieces themselves in drawing order:
        // the top edge, the arc joining it to the right edge, the right edge, and so on round, so
        // that piece `i` runs from `pt[i]` to `pt[i + 1]`. A corner is an arc where two edges meet
        // -- and half an arc is not a corner, which is why a corner needs both of its edges.
        auto shape = [&](const D2D1_RECT_F &box, const Corners &k,
                         unsigned on) -> ID2D1PathGeometry * {
            float tl = k.r[0], tr = k.r[1], br = k.r[2], bl = k.r[3];
            // No two radii on one side may overlap, or the curve crosses itself and the fill comes
            // out inside out. XAML scales the pair down the same way.
            const float w = box.right - box.left, h = box.bottom - box.top;
            if (tl + tr > 0.0f) { const float s = (std::min)(1.0f, w / (tl + tr)); tl *= s; tr *= s; }
            if (bl + br > 0.0f) { const float s = (std::min)(1.0f, w / (bl + br)); bl *= s; br *= s; }
            if (tl + bl > 0.0f) { const float s = (std::min)(1.0f, h / (tl + bl)); tl *= s; bl *= s; }
            if (tr + br > 0.0f) { const float s = (std::min)(1.0f, h / (tr + br)); tr *= s; br *= s; }
            const D2D1_POINT_2F pt[9] = {
                D2D1::Point2F(box.left + tl, box.top),     D2D1::Point2F(box.right - tr, box.top),
                D2D1::Point2F(box.right, box.top + tr),    D2D1::Point2F(box.right, box.bottom - br),
                D2D1::Point2F(box.right - br, box.bottom), D2D1::Point2F(box.left + bl, box.bottom),
                D2D1::Point2F(box.left, box.bottom - bl),  D2D1::Point2F(box.left, box.top + tl),
                D2D1::Point2F(box.left + tl, box.top),
            };
            const float rad[8] = { 0.0f, tr, 0.0f, br, 0.0f, bl, 0.0f, tl };
            const bool lit[8] = {
                (on & edge::kTop) != 0,
                (on & edge::kTop) != 0 && (on & edge::kRight) != 0,
                (on & edge::kRight) != 0,
                (on & edge::kRight) != 0 && (on & edge::kBottom) != 0,
                (on & edge::kBottom) != 0,
                (on & edge::kBottom) != 0 && (on & edge::kLeft) != 0,
                (on & edge::kLeft) != 0,
                (on & edge::kLeft) != 0 && (on & edge::kTop) != 0,
            };
            ID2D1PathGeometry *path = nullptr;
            if (FAILED(factory->CreatePathGeometry(&path)) || !path) return nullptr;
            ID2D1GeometrySink *sink = nullptr;
            if (FAILED(path->Open(&sink)) || !sink) { path->Release(); return nullptr; }
            for (int i = 0; i < 8;) {
                if (!lit[i]) { i++; continue; }
                int j = i;
                while (j < 8 && lit[j]) j++;
                sink->BeginFigure(pt[i], D2D1_FIGURE_BEGIN_FILLED);
                for (int p = i; p < j; p++) {
                    if (rad[p] > 0.0f)
                        sink->AddArc(D2D1::ArcSegment(pt[p + 1], D2D1::SizeF(rad[p], rad[p]), 0.0f,
                                                      D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                                      D2D1_ARC_SIZE_SMALL));
                    else
                        sink->AddLine(pt[p + 1]);
                }
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                i = j;
            }
            sink->Close();
            sink->Release();
            return path;
        };
        // The fill is the whole shape whatever the border covers, and an open figure is filled as
        // if it were closed, which is what makes the two share one builder.
        if (ID2D1PathGeometry *p = shape(r, c, edge::kAll)) {
            rt->FillGeometry(p, Brush(fill));
            p->Release();
        }
        if (border.a > 0.0f && width > 0.0f && edges != 0) {
            // Inset by the stroke and drawn on that edge, which is what StrokeRound does with a
            // rectangle: a stroke centred on the edge is two half-covered rows of pixels.
            const D2D1_RECT_F in = { r.left + width / 2, r.top + width / 2,
                                     r.right - width / 2, r.bottom - width / 2 };
            Corners k = c;
            for (float &rad : k.r) rad = rad > width / 2 ? rad - width / 2 : 0.0f;
            if (ID2D1PathGeometry *p = shape(in, k, edges)) {
                rt->DrawGeometry(p, Brush(border), width);
                p->Release();
            }
        }
        factory->Release();
    }

    // One line, vertically centred in `r`, clipped. CLIP is on because a string that
    // overflows its box should be cut, not painted over the control next to it -- an
    // overflowing label that silently draws across its neighbour is the hardest kind
    // of layout fault to see in a screenshot.
    void Text(const std::wstring &s, const D2D1_RECT_F &r, IDWriteTextFormat *fmt,
              const D2D1_COLOR_F &c) const {
        if (s.empty()) return;
        rt->DrawText(s.c_str(), (UINT32)s.size(), fmt, r, Brush(c),
                     D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    // A laid-out paragraph: wraps, and reports how tall it came out so the caller can
    // put the next thing under it.
    //
    // `clip` matters more than it looks. A text layout is drawn from its origin and
    // pays no attention to the height of the rectangle it was measured against, so a
    // two-line string in a one-line box draws its second line over whatever is below.
    float TextWrapped(const std::wstring &s, const D2D1_RECT_F &r, IDWriteTextFormat *fmt,
                      const D2D1_COLOR_F &c, bool measureOnly = false,
                      bool clip = false) const {
        if (s.empty()) return 0.0f;
        IDWriteTextLayout *layout = nullptr;
        if (FAILED(font->dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), fmt,
                                              r.right - r.left, 100000.0f, &layout)) || !layout)
            return 0.0f;
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        DWRITE_TEXT_METRICS m = {};
        layout->GetMetrics(&m);
        if (!measureOnly) {
            if (clip) rt->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
            rt->DrawTextLayout(D2D1::Point2F(r.left, r.top), layout, Brush(c),
                               D2D1_DRAW_TEXT_OPTIONS_NONE);
            if (clip) rt->PopAxisAlignedClip();
        }
        layout->Release();
        return m.height;
    }

    // The same paragraph, centred: line by line in the box, and as a block in it. What an empty
    // panel has to say is one line of grey in the middle of a lot of nothing, and the wrapped
    // one above cannot say it -- the alignment of the lines belongs to the text format, which is
    // shared, so centring here means a layout of its own.
    //
    // Clipped, because a paragraph that does not fit is a layout fault and drawing it outside the
    // panel it belongs to hides the fault on somebody else's control.
    float TextWrappedCentred(const std::wstring &s, const D2D1_RECT_F &r, IDWriteTextFormat *fmt,
                             const D2D1_COLOR_F &c) const {
        if (s.empty()) return 0.0f;
        const float w = r.right - r.left;
        if (w <= 0.0f || r.bottom <= r.top) return 0.0f;
        IDWriteTextLayout *layout = nullptr;
        if (FAILED(font->dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), fmt, w, 100000.0f,
                                              &layout)) || !layout)
            return 0.0f;
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        DWRITE_TEXT_METRICS m = {};
        layout->GetMetrics(&m);
        // Centred as a block by hand: a layout is drawn from its origin downwards and takes no
        // notice of the height it was measured against, and the height it reports is the one its
        // own lines came to.
        const float top = r.top + ((r.bottom - r.top) - m.height) * 0.5f;
        rt->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
        rt->DrawTextLayout(D2D1::Point2F(r.left, top), layout, Brush(c), D2D1_DRAW_TEXT_OPTIONS_NONE);
        rt->PopAxisAlignedClip();
        layout->Release();
        return m.height;
    }

    // How wide a single line wants to be. Buttons size themselves from this rather
    // than from a guess, which is what keeps a Chinese label and an English one both
    // fitting without a magic constant per string.
    //
    // Measured once per string and format and kept there -- see Fonts::Measure. Every
    // caller here is either laying a control out or drawing one, and both of those ask
    // again next frame with the same string: a label's width is not a per-frame
    // question, and answering it is a DirectWrite layout and a shaping pass.
    float MeasureWidth(const std::wstring &s, IDWriteTextFormat *fmt) const {
        return font->Measure(fmt, s);
    }
};

// ---------------------------------------------------------------- Layer

struct Window;
struct App;
// A control that floats over the page. Forward-declared for Widget::AsLayer(), and defined here,
// after the painter it draws its dim with.
struct Layer;

// A control that floats over the page: a dialog, a flyout, a menu, a tip.
//
// Under the tree this is a small class, because most of what it used to do is what a child *is*.
// It used to live in a flat list, where the controls that came out with it were somewhere else in
// that list and had to be marked with `leavingWith`, and where it had to be added *before* them
// with a lower `z` so that the paint order and the hit-test order agreed about what was on top.
// A layer's contents are its children now, and that one fact replaces all of it: the fade takes the
// subtree, the window drops the subtree when the fade is over, the hit test reaches it first
// because it is later in the order, and a screen reader reads those controls as the layer's
// children -- which is the thing a flat list could never say.
//
// What is left is the part that is not structure:
//
//   - `modal` takes the input *under* it. The rectangle of a page-level layer is the page, so a
//     click that is not on something the layer put there lands on the layer and stops there.
//   - `lightDismiss` closes it when a click misses it or the window loses activation: both arrive
//     through the `Dismiss()` every widget gets, and what "closed" *means* is the page's, given as
//     `onDismiss`.
//
//     **It is off by default, and having it as a switch at all is the point.** A click that misses
//     a flyout closing it is what a flyout is; a question that has to be answered -- *this file has
//     unsaved changes* -- is not answered by a stray click on the dim, and Windows draws the same
//     line: `Popup` carries `IsLightDismissEnabled`, which a flyout turns on, while `ContentDialog`
//     has no property of the kind and can only be answered with a button.
//   - `smoke` dims what is behind it, and it arrives by fading in -- itself, its dim and the
//     controls on it, as one group -- rather than appearing whole on the next frame.
//
// The rectangle stops at the caption bar, and that now needs no arithmetic: a layer is added to the
// widget it covers, and that widget starts below the caption. Dragging the window, double-clicking
// the caption and the Windows 11 snap-layout flyout therefore all keep working with a modal open.
struct Layer : Widget {
    bool modal = true;
    bool lightDismiss = false;
    bool smoke = false;
    // Esc closes it: the window offers Esc to the top layer before the page's cancel. A layer that
    // must not be closed this way -- a dialog with a job still running -- says so.
    bool escape = true;
    // The dim's own alpha, over black. A dialog that wants to be the only thing on screen turns it
    // up; a suggestion can turn it down.
    float smokeAlpha = 0.14f;

    std::function<void()> onDismiss;

    // A layer arrives rather than appearing, and leaves the same way: the whole of it fades up over
    // `motion::kFast` and back down over the same, the dim with it. `kFast` rather than `kNormal`,
    // which is what a page and a flyout take: those are watched, and this is *read* -- the panel is
    // legible well before the fade is over.
    //
    // The fade is the *window's* work: it paints everything a layer is, as one group, through one
    // opacity layer, which is what a group needs -- fading each widget on its own would show the
    // page through the gaps between them and come out darker where two overlap.
    //
    // The track starts on its target, and `Arrival()` answers 1 for as long as the animation switch
    // is off -- so with animations off the layer is drawn whole on the first frame that asks for
    // it, with no invisible frame in between.
    float Arrival() const { return Animations() ? arrive.value : (leaving ? 0.0f : 1.0f); }
    bool leaving = false;
    motion::Track arrive{};
    Layer() { arrive.To(1.0f); }

    bool Leaving() const { return leaving; }
    bool HasLeft() const { return leaving && !arrive.Wants(arrive.to); }

    bool Animating() const override { return Widget::Animating() || arrive.Wants(arrive.to); }
    void Tick(float dt) override {
        Widget::Tick(dt);
        arrive.Step(dt, motion::kFast);
    }

    // The part of the layer that is the layer's own content. A click in `rect` that is not in this
    // is a click on the dim, which is what light-dismissing is. The default is the whole rectangle
    // -- a layer with no dim, and so nothing to miss.
    virtual D2D1_RECT_F Body() const { return rect; }

    // Ask it to go away. `onDismiss` fires at once rather than at the end of the fade, and that is
    // the point of the fence: what the page keeps is the page's own state, and a page that has been
    // told can put the focus somewhere sensible while the layer is still on its way out. The fade
    // runs on, and the window drops the subtree when it is over.
    void Close();

    // A click that missed. This is where light-dismissing actually happens, and it has to be here:
    // a layer *covers* the page, so a click that misses its content lands on the layer itself
    // rather than on a control beside it.
    void OnPress(float x, float y) override {
        if (!lightDismiss || !onDismiss) return;
        if (!Inside(Body(), x, y)) Close();
    }

    // The click missed what the layer put on screen, or the window was deactivated: for a layer
    // that light-dismisses, that is a dismissal.
    void Dismiss() override {
        if (lightDismiss && onDismiss) Close();
    }

    void Paint(const Painter &p) override {
        if (smoke) p.Fill(rect, Rgb(0x000000, smokeAlpha));
    }

    // Where this layer goes is `Widget::Cover`, which the tree asks of every child that answered
    // `AsLayer` here: the default is the whole of the widget it was added to, and a layer that is a
    // box of its own rather than a cover -- a tip -- overrides it. Nothing in between: a layer takes
    // no place in its host's layout, so nothing else arranges one.

    // What Tab walks while this is open, in order. Empty leaves the page's own order alone, which
    // is right for a layer that is only a picture.
    virtual std::vector<Widget *> FocusRing() { return {}; }
    // Enter, when nothing in the layer holds the focus. The page's `OnDefaultAction()` is what is
    // left after this.
    virtual Widget *DefaultButton() { return nullptr; }

    // Esc, offered by the window to the top layer first. True means it was taken.
    bool Escape() {
        // On its way out already: Esc is answered rather than passed on, or the page's cancel --
        // usually "close the window" -- would get it from a dialog that is visibly already gone.
        if (leaving) return true;
        if (!escape || !onDismiss) return false;
        Close();
        return true;
    }

    Layer *AsLayer() override { return this; }
};

// The window used to walk its *widgets* instead and ask each one `OnTimer(id)`, which required
// any owner to be in that list: a scroll bar inside a drop-down is not, so its timers arrived
// nowhere at all -- silently -- and the drop-down carried a forwarder to work around it.
class Timer {
public:
    Timer() = default;
    Timer(const Timer &) = delete;
    Timer &operator=(const Timer &) = delete;
    ~Timer();
    // Starts it, or moves it: `fn` runs `ms` from now, and again every `ms` -- a Windows timer
    // repeats until it is stopped. Called on the window's thread, like everything else here.
    void Start(Window *w, UINT ms, std::function<void()> fn);
    // Ends it, and gives the id back. Safe to call from inside the callback.
    void Stop();
    bool Running() const { return win != nullptr; }
    // One `WM_TIMER`: true when the id was this timer's.
    bool Handle(UINT_PTR which);
private:
    Window *win = nullptr;
    UINT_PTR id = 0;
    std::function<void()> tick;
};

// ---------------------------------------------------------------- Window

struct Window {
    HWND hwnd = nullptr;
    UINT dpi  = 96;
    Palette pal;
    Fonts   fonts;
    // False on Windows 11 before 22H2, and on anything that refuses the attribute.
    // The page paints an opaque background instead of letting the material through.
    bool micaActive = false;
    // Which system backdrop to ask DWM for: one of the kDwmBackdrop* values, Mica unless a page
    // says otherwise -- `kDwmBackdropAcrylic` is the translucent one, `kDwmBackdropTabbed` Mica
    // Alt, `kDwmBackdropNone` a flat window. Set it before `Create`; a page that switches
    // material at run time sets it and calls `ApplyThemeToFrame()`.
    DWORD backdrop = kDwmBackdropMainWindow;
    bool resizable = false;
    // How Create shows the window. SW_HIDE leaves it hidden for the program to show
    // later: after restoring a saved position, say, or without taking the foreground
    // (SetWindowPos with SWP_SHOWWINDOW | SWP_NOACTIVATE). SW_SHOW activates, and the
    // window that had the foreground loses it even when the activation is refused.
    int showCommand = SW_SHOW;
    // How much of the window is not client area, in pixels: the resize border on the
    // left, right and bottom. Read from the window rather than computed, because the
    // caption is client area here and no system metric describes that frame.
    SIZE frameExtra = {};
    // Put a theme or a backdrop the program has asked for into effect. Both are read when the window
    // is made -- `Theme(...)` says which, and `backdrop` above is the material -- so a page that
    // changes either while the window is up is a page that has changed nothing until one of these
    // runs. This is what a settings page does with a theme switch on it.
    void ReloadTheme();
    void ApplyThemeToFrame();
    void MeasureFrame() {
        if (!hwnd || IsIconic(hwnd) || IsZoomed(hwnd)) return;
        RECT wr, cr;
        GetWindowRect(hwnd, &wr);
        GetClientRect(hwnd, &cr);
        frameExtra = { (wr.right - wr.left) - cr.right, (wr.bottom - wr.top) - cr.bottom };
    }

    // The composition stack, in the order it has to be built and the reverse of the
    // order it has to be torn down.
    IDWriteFactory       *dw    = nullptr;
    ID3D11Device         *d3d   = nullptr;
    IDXGIDevice          *dxgi  = nullptr;
    ID2D1Factory1        *d2d   = nullptr;
    ID2D1Device          *d2dDevice = nullptr;
    ID2D1DeviceContext   *dc    = nullptr;
    IDXGISwapChain1      *swap  = nullptr;
    ID2D1Bitmap1         *target = nullptr;
    IDCompositionDevice  *comp   = nullptr;
    IDCompositionTarget  *compTarget = nullptr;
    IDCompositionVisual  *compVisual = nullptr;
    ID2D1SolidColorBrush *brush = nullptr;

    // The caption we draw. `hot` and `down` are 0/1/2 for minimise, maximise, close.
    HICON         appIcon = nullptr;
    ID2D1Bitmap1 *iconBitmap = nullptr;

    // Pictures loaded from disk, keyed by path. Device-bound, so they live next to
    // the device that owns them and die with it in ReleaseDevice -- a bitmap that
    // outlives its target is a crash rather than a blank tile.
    //
    // A failed load caches a null. A page that shows a grid of pictures asks for all of
    // them on every paint, and a missing file would otherwise be a failed decode per
    // picture per frame of every resize.
    std::map<std::wstring, ID2D1Bitmap1 *> images;
    int  captionHot = -1;
    int  captionDown = -1;
    // The three buttons' hover fades. Windows' own caption tints under the pointer
    // rather than switching, and the close button is the one people notice.
    float captionT[3] = { 0.0f, 0.0f, 0.0f };
    bool active = true;

    // The timers this window is running, and the ids they took -- see Timer, which is where both
    // the ids and the dispatch come from. Declared before `widgets` because a control's timer
    // takes itself out of this list as it stops, which happens while the controls are being
    // destroyed.
    std::vector<Timer *> timers;
    std::vector<UINT_PTR> timerIds;
    UINT_PTR TakeTimerId();
    void GiveTimerId(UINT_PTR id);
    // The window's own two, from the same pool: the caret's blink, and the frame loop's stand-in
    // while Windows is running a modal size or move loop of its own.
    Timer caretTimer, frameTimer;

    // The tree, and the one widget a page builds into: the client area, which is WinUI's
    // `Window::Content`. Everything a page adds is under it. The window's own furniture -- the
    // caption and its three buttons -- is still painted by the window rather than being a child of
    // it, which is a later pass.
    std::unique_ptr<Widget> content;
    // ---- who is pointing -------------------------------------------------------------------------
    // Three hands and one path. What a press, a move and a release *do* -- which widget is under
    // them, whether it takes the focus, what the widget's own callbacks are -- is written once and
    // reached from the two message handlers, because a control that answers a mouse and not a finger
    // is a control with two behaviours to get wrong. What differs is only what a hand really differs
    // in: a finger has no hover, it has to stay put to be a click, and a second finger takes the
    // first one's click away. See `PressAt`, `MoveTo` and `ReleaseAt`.
    enum class Hand { Mouse, Finger, Pen };
    // **A gesture belongs to the hand that made it.** A mouse that moves while a finger is down must
    // not un-press what the finger pressed, nor drag what the finger is dragging -- which is not a
    // corner case: the two are usually both on the machine, and the mouse is usually somewhere else on
    // the screen, so without this a tap would end as soon as the pointer twitched and a slider dragged
    // by a finger would follow the mouse instead.
    Hand capturing = Hand::Mouse;
    Widget *capture = nullptr;    // the widget the pointer went down on
    Widget *focused = nullptr;
    // The touch pointer being followed, or 0. A pen is one pointer by definition and needs no such
    // thing; a finger does, because a second one is another pointer entirely.
    UINT32 finger = 0;
    // Where a press landed, in client DIPs: what a tap is measured against.
    float pressX = 0.0f, pressY = 0.0f;
    // **Where the pointer last was, whoever moved it.** Every coordinate the input path is handed is
    // kept here as it arrives, because a control that asks the pointer -- an open list's hovered row, a
    // pane's -- has to be answered with the last thing that happened, and under a finger that is not the
    // mouse: a touch does not move the mouse, so `GetCursorPos` answers with a place nobody is pointing
    // at. See `Widget::Cursor`.
    float pointerX = 0.0f, pointerY = 0.0f;
    // **A drag no control has taken belongs to whatever above it scrolls.** See `Widget::Pans`. The
    // widget is decided once, when the finger has wandered far enough to say it is not tapping, and it
    // keeps the gesture until the hand lifts -- so a page that scrolled cannot leave the rest of the
    // same movement to the container above it.
    Widget *panning = nullptr;
    float panLastY = 0.0f;
    double panLastT = 0.0;
    float panSpeedY = 0.0f;
    void PressAt(float x, float y, Hand hand);
    bool MoveTo(float x, float y, Hand hand, bool contact);
    void ReleaseAt(Hand hand);
    Widget *PanTargetFor(Widget *w);
    // The tree owes an arrangement: set by Widget::InvalidateLayout and by anything that changes a
    // widget's size or a layout's spec, cleared by the arrange pass. One flag for the whole tree,
    // because an arrangement is one walk from the root -- a widget whose parent has not been
    // arranged has no rectangle whose subtree could be arranged on its own.
    bool layoutDirty = true;
    // Set when the keyboard was used to move focus. Windows only paints focus rings
    // after somebody has pressed Tab, and copying that is the difference between a
    // window that looks calm on arrival and one covered in rectangles.
    bool showFocusRing = false;
    bool caretOn = true;

    // The animation clock, and it is QueryPerformanceCounter rather than GetTickCount64
    // for a measured reason: GetTickCount64's resolution is the system tick, 15.6 ms, so
    // every dt it can report is 0, 15.6 or 31.2 -- the quantisation is the same size as
    // the frame, and the motion inherits it as a stutter. Measured before this changed:
    // frame gaps of 2 to 35 ms, median 24.
    LARGE_INTEGER qpcFreq = {};
    LARGE_INTEGER qpcLast = {};
    // True while the frame loop is the thing running, rather than GetMessage. Read by a
    // page that needs to know whether a value it was handed should animate or land
    // (motion::Track::To or Track::Set) when it lays itself out.
    bool animOn = false;
    // True while Windows is running a modal loop of its own for a drag of the border or the
    // caption, during which the frame loop cannot run and WM_PAINT is the only painting
    // there is. See WM_ENTERSIZEMOVE.
    bool inSizeMove = false;
    bool alive = true;

    float scale() const { return dpi / 96.0f; }
    float ClientW() const { RECT r; GetClientRect(hwnd, &r); return r.right / scale(); }
    float ClientH() const { RECT r; GetClientRect(hwnd, &r); return r.bottom / scale(); }

    // Whether any of this window is on a screen at all: shown, not minimised, and not cloaked --
    // which is what the system reports for a window on another virtual desktop, or one a shell has
    // put away. The frame loop asks this before it runs a frame; see Run, where the reason is.
    //
    // **Occlusion is not asked about and cannot be.** No query answers "is another window over
    // this one", and the guesses are worse than the waste: sampling points with WindowFromPoint is
    // wrong for a window that is partly covered, wrong for a layered one, and wrong for every
    // window in a session that is not the foreground one.
    bool Visible() const {
        if (!hwnd || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return false;
        DWORD cloaked = 0;
        return FAILED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) ||
               cloaked == 0;
    }

    virtual ~Window();

    // --- to implement -----------------------------------------------------------
    virtual const wchar_t *ClassName() const = 0;
    virtual const wchar_t *Title() const = 0;
    // The page no longer has a `Layout()` to override: what places a page's controls is the layout
    // of the widget they were added to (see docs/layout.md). `PaintPage` is gone the same way --
    // what a page used to draw there is widgets now, a `Heading` and a `Card` among them.
    virtual void OnDefaultAction() {}              // Enter
    virtual void OnCancel() { PostMessageW(hwnd, WM_CLOSE, 0, 0); }   // Esc
    // ~16 ms while anything is moving. `dt` is real elapsed seconds, clamped; a page
    // that animates something of its own advances it by that rather than by a constant.
    virtual void OnTick(float /*dt*/) {}
    // Does the *page* want the timer kept alive -- a progress bar, a spinner, a page
    // arriving? The controls' own animations are Animating() below and are not this
    // question; a subclass overriding this does not have to know about them.
    virtual bool AnimationWanted() const { return false; }
    virtual bool OnAppMessage(UINT, WPARAM, LPARAM) { return false; }
    // The smallest the window may be dragged to, in DIPs. Zero means no limit.
    //
    // A resizable window without one is a window somebody can drag to nothing, and a
    // layout that computes its columns by dividing the width it is given goes negative
    // at a small enough size, with the controls coming out inside out. Scrolling answers
    // "too short"; this answers "too narrow".
    virtual void MinSize(int *w, int *h) const { *w = 0; *h = 0; }

    // --- lifetime ---------------------------------------------------------------
    bool Create(int dipW, int dipH, bool canResize, HICON icon);
    // Run this window, and only this window. The shorthand for a program with one window, which is
    // what most programs have and what every example here has a use for -- see App for the rest.
    int  Run();

    // The app this window is registered with, or null. Set by App::Add, cleared by App::Remove and
    // by the App's own destructor -- which is what makes the order the two of them die in not
    // matter: the App is usually a local in wWinMain and the windows are usually locals after it,
    // so it is the App that goes first.
    App *app = nullptr;

    // The window has been destroyed, and this is the last thing it does about it. For a page that
    // made a window of its own and has to drop it: deleting a window inside its own message is not
    // something to do, so the usual answer is to `Post` to the window that made it.
    virtual void OnClosed() {}

    // Called by App as a window joins the loop and as it leaves it. These were the first and the
    // last things this window's own loop did, which was the same thing only because there was
    // ever one window in it.
    void BeginPump();
    void EndPump();

    // ---- UI Automation ------------------------------------------------------------------------
    // The element a client is handed for this window, and the widget behind an element's uid --
    // null once the page has been laid out again, which is the answer that keeps an element that
    // has outlived its widget harmless. See the UIA section near the end of this header.
    IRawElementProviderSimple *UiaRoot();
    Widget *UiaFind(int uid) const;
    // The same question asked from a widget down, because a uid belongs to the widget Add gave it to
    // and an element resolves it against the whole tree.
    Widget *FindUid(Widget *w, int uid) const;
    // Told to a client that is listening that the keyboard focus moved. Called from SetFocusTo.
    void UiaFocusChanged();
    // Told to a client that is listening what has changed since the frame before. Called from Paint,
    // which is the one moment every change has to pass through. See the UIA section of this header.
    void UiaAnnounce();
    // **What a client is told about, and what the frame before said.** A comparison rather than a
    // promise, because there is nowhere in a widget that knows it changed: a page changes a control
    // through the control's own path or by writing a field, and either way the window finds out by
    // looking. The handful below is the whole of what a change is -- a client can ask for the rest.
    struct UiaSeen {
        bool hasValue = false;
        std::wstring value;
        bool hasRange = false;
        float range = 0.0f;
        int expanded = -1;
        bool hasScroll = false;
        float scroll = 0.0f;        // 0 to 100, which is what a client is told in
        std::vector<char> chosen;    // one per item, and whether it is the chosen one
    };
    std::unordered_map<int, UiaSeen> uiaSeen, uiaNow;
    // The provider reads the widget list, the focus, the hovered control and the page's own
    // transform, and calls a control's action: it is the window's own business said from outside,
    // and it is not worth twenty accessors.
    friend struct UiaElement;
    int uidNext = 0;
    // While the frame loop is animating it draws every frame itself and clears the update
    // region after each one, so invalidating as well buys nothing -- and it costs a frame:
    // the region it sets is handed back by the next PeekMessage as a WM_PAINT, which paints
    // the window a second time in the same frame. See Window::Run and the WM_PAINT case.
    void Invalidate() { if (hwnd && !animOn) InvalidateRect(hwnd, nullptr, FALSE); }

    // Everything the window animates on its own account: every control's pointer states
    // and the three caption buttons. Distinct from AnimationWanted(), which is the
    // page's own answer -- the frame loop runs while either of them says so.
    //
    // A control the page has scrolled out of sight is not counted. Nothing it does can be seen,
    // so nothing it does is a reason to run a frame -- and a page scrolled past a control that
    // animates for ever, an indeterminate progress bar being the one that does, would otherwise
    // keep the loop turning for as long as that page was open. It is the same test `pass` uses to
    // skip *drawing* one, which is the whole point: what is not drawn is not animated either. The
    // animation is not lost, only paused -- scrolling back brings it into the strip and this
    // answers yes again, and the control lands where it was going.
    bool Animating() const {
        for (int i = 0; i < 3; i++)
            if (captionT[i] != (captionHot == i ? 1.0f : 0.0f)) return true;
        return content && content->Animating();
    }
    void Tick(float dt) {
        for (int i = 0; i < 3; i++)
            motion::Ramp(&captionT[i], captionHot == i ? 1.0f : 0.0f, dt, motion::kFaster);
        if (content) content->Tick(dt);
    }

    // One frame's worth of time, and what it is spent on. Called from the loop in Run().
    void Frame() {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = (float)((double)(now.QuadPart - qpcLast.QuadPart) /
                           (double)qpcFreq.QuadPart);
        qpcLast = now;
        // Clamped only at the top, and generously: a frame that took 100 ms is a frame
        // the machine really did take that long over, and the animation should be 100 ms
        // further along rather than pretending otherwise. What must not happen is a
        // whole animation in one step after the window was behind a modal dialog.
        if (dt < 0.0f) dt = 0.0f;
        if (dt > 0.1f) dt = 0.1f;
        // Arrange first, because everything after this reads rectangles: this is where a change made
        // by the message just handled becomes geometry, and the tick below is what glides the tree
        // toward it. Nothing else arranges -- a child added, a layout replaced and a widget hidden
        // all set the one flag, and it is cleared here.
        if (layoutDirty) ArrangeTree();
        // Then hover, from where the cursor is rather than from the last mouse message: a
        // rearrangement can have moved something under a pointer that has not moved at all.
        RefreshHover();
        Tick(dt);
        OnTick(dt);
        // A layer that has finished leaving is dropped here rather than from inside its own Tick:
        // the callback that told the page has long returned, the tick loop above is done walking the
        // list, and nothing is left that can be looking at it.
        DropGoneLayers();
    }

    // Hover, recomputed from where the cursor actually is rather than from the last
    // mouse message.
    //
    // Needed because the widget list is rebuilt more often than the mouse moves: an
    // expander opening, a page switching, a button relabelling itself and every saved
    // setting all call Layout(), and the control under a *stationary* pointer is then a
    // new object with hover false -- which used to make the highlight vanish under the
    // cursor and now would fade it out, which is worse. Called from the tick, where a
    // rebuild has just happened.
    bool RefreshHover();

    // The root widget, made on first use. A page adds to the window, so this is what it is adding
    // to; `Add` here is the same call it has always been.
    View *EnsureContent() {
        if (!content) {
            content.reset(new View());
            content->win = this;
        }
        return static_cast<View *>(content.get());
    }
    template <typename T> T *Add(T *w) { return EnsureContent()->Add(w); }
    // What arranges the page: one call, on the widget a page builds into.
    void SetLayout(Layout *l) { EnsureContent()->SetLayout(l); }
    // --- what a running callback is still standing on -----------------------------
    //
    // `Layout()` is routinely called from inside a widget's own callback, because "go
    // to the next page" is a button that replaces the page the button is on. Clearing
    // the list there frees the `std::function` whose body is currently executing --
    // and MSVC reloads a lambda's captures out of that closure after every call the
    // body makes, so the `Invalidate()` such a handler ends with reads `this` back out
    // of freed memory.
    //
    // It is a crash exactly when that block has been reused in between, which is why
    // it reads as "the program usually dies on its busiest page" rather than as a fault
    // in any one page: the busiest page is the one whose layout allocates enough to take
    // the block back. Seen in the field as an access violation at `mov rax,[rbx+8]` --
    // the captured `this`, reloaded from the closure -- on the instruction after the
    // call to `Layout`.
    //
    // The comment over `OnClick` in `Proc` had half of this already: it stopped the
    // *window* touching a widget that a click had replaced. This is the other half,
    // which is the widget touching itself. So a widget replaced while a message is
    // being dispatched is moved here rather than deleted, and this list is emptied
    // when that message has returned. Outside dispatch nothing is held: there is no
    // closure running to protect.
    std::vector<std::unique_ptr<Widget>> retired;
    int dispatchDepth = 0;
    // True while the window is taking its tree down, which is the one time a removal must not happen:
    // a control that owns a layer outside its own subtree would be unlinking a sibling from a vector
    // that is being destroyed. See ~Window and Widget::Remove.
    bool tearingDown = false;

    // One window message, and any nested ones -- a modal dialog, a drop-down running
    // its own loop -- that happen inside it. Only the outermost frees, because the
    // closure being protected belongs to the outermost.
    struct Dispatch {
        Window *w;
        explicit Dispatch(Window *win) : w(win) { w->dispatchDepth++; }
        ~Dispatch();
        Dispatch(const Dispatch &) = delete;
        Dispatch &operator=(const Dispatch &) = delete;
    };

    // --- internals --------------------------------------------------------------
    bool CreateDevice();
    // A layer that is on its way out, if there is one. The window takes no clicks while there is:
    // what is being dismissed is not a place to be pressed again, the controls on it are on their
    // way to being gone, and the page under it was not clickable a moment ago either.
    Layer *LeavingLayer();
    // Drops the layers that have finished leaving, subtree and all. Called from Frame, after the
    // tick, so that the tree is not taken apart while it is being walked.
    void DropGoneLayers();
    bool CreateSizedResources();
    void ReleaseSizedResources();
    void ReleaseDevice();
    void Resize();
    void Paint();
    void PaintCaption(const Painter &p);

    // ---- the tree --------------------------------------------------------------------------
    //
    // The geometry convention, stated once because three passes depend on it: **a widget's `rect`
    // is in its parent's space, and it draws in that space** -- so a control keeps drawing against
    // its own rectangle, exactly as it did when a page placed it by hand. Descending into a
    // widget's children moves the origin to that widget's top left, which is where their rectangles
    // are measured from: a subtree that moves is a translation, and nothing inside it has to know.
    //
    // One arrangement, from the root down, and the walk itself is `ArrangeSubtree`: the window's own
    // part of it is the root's box -- the client area below the caption -- and the fonts every
    // measurement comes out of.
    void ArrangeTree() {
        layoutDirty = false;
        if (!content) return;
        const D2D1_RECT_F box = { 0.0f, kCaptionH, ClientW(), ClientH() };
        // **A window that changed size is not a layout change worth animating.** The border is under the
        // pointer, every frame of the drag is the new size, and a tree gliding toward a box that keeps
        // moving trails it by however fast the pointer is going -- hundreds of DIPs, at the speed
        // somebody drags a border. So the arrangement that follows a resize *places* what it arranged,
        // and every other arrangement glides as usual. See PlaceSubtree.
        const bool resized = !SameRect(box, content->rect);
        content->rect = box;
        // Placed, and drawn where it is, every time. The root is not a child of any layout, so
        // nothing glides it and a window that was resized is not an animation -- and a drawn
        // rectangle left behind by the old size would say "something is moving" for the rest of the
        // window's life, with the frame loop turning frames for a page that is standing still.
        content->drawn = content->rect;
        content->placed = true;
        micula::ArrangeSubtree(content.get(), fonts);
        if (resized) micula::PlaceSubtree(content.get());
    }

    // Paint the tree. `ox, oy` is where the space `w->rect` is measured in sits in the client area:
    // the accumulated offsets of its ancestors. Paint is called under that translation, which is what
    // lets a control go on drawing at `rect` -- and why a widget that is gliding takes its children
    // with it, the origin they are handed being the position it is drawn at rather than the one it
    // was arranged into.
    void PaintTree(const Painter &p, Widget *w, float ox, float oy) {
        if (!w->visible) return;
        const D2D1_RECT_F where = w->placed ? w->drawn : w->rect;
        // A widget that is not where it was arranged is drawn where it is: the difference between
        // the two goes on as a translation for this widget alone.
        const float mx = where.left - w->rect.left, my = where.top - w->rect.top;
        p.rt->SetTransform(D2D1::Matrix3x2F::Translation(ox + mx, oy + my));
        // A subtree being faded is drawn as one group at one opacity, rather than each of its widgets
        // at that opacity: fading them one by one shows the page through the gaps between them, and
        // comes out darker where two of them overlap. Two things answer with one -- a layer arriving
        // or leaving, and any widget whose own `opacity` is below 1, which is how a page arrives. See
        // `Layer::Arrival` and `NavigationView::transition`. Nothing is pushed at 1, which is every
        // widget for all but a few frames.
        Layer *layer = w->AsLayer();
        const float op = w->opacity * (layer ? layer->Arrival() : 1.0f);
        const bool fading = op < 1.0f;
        if (fading) {
            p.rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr,
                                                  D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                  D2D1::IdentityMatrix(), op), nullptr);
        }
        // **The widget's own drawing is not clipped by its own window.** `Clips` is a window onto a
        // widget's *children* -- that is the sentence it is documented with -- and the two are only
        // the same box while nothing is arriving: a flyout draws the panel's surface *and its shadow*,
        // and a shadow cut off square at the reveal that is growing over it is the one edge in the
        // picture that nothing else explains. So the clip goes on here, after this widget has drawn,
        // and comes off before the children are done.
        w->Paint(p);
        // A widget that is a window onto its children clips them to itself -- or to the part of itself
        // it is showing, which is what a panel arriving is. The clip is pushed *after* the transform,
        // so its rectangle is read in the space that transform is in -- and that space is the parent's
        // with this widget's own glide taken *out* of it, because the transform being
        // `ox + (drawn - rect)` is exactly what carries a widget painting at `rect` to `drawn`. The
        // rectangle that means "where this widget is drawn" in here is therefore `rect` -- which is
        // what `ClipBox` answers by default -- and pushing `where` would put the box a whole glide
        // further along than the thing it clips: a row of a sliding list drawn past the panel's edge,
        // and the row at the other edge cut off. See Widget::Clips.
        const bool clips = w->Clips();
        if (clips) p.rt->PushAxisAlignedClip(w->ClipBox(), D2D1_ANTIALIAS_MODE_ALIASED);
        const float cx = ox + where.left, cy = oy + where.top;
        for (const auto &child : w->children) PaintTree(p, child.get(), cx, cy);
        if (clips) p.rt->PopAxisAlignedClip();
        if (fading) p.rt->PopLayer();
        p.rt->SetTransform(D2D1::Matrix3x2F::Identity());
    }

#if MICULA_DEBUG_LAYOUT
    // The boxes a layout worked out, drawn on top of the page: red for where a widget is, magenta
    // for where it was arranged while it is on its way there, orange for the box its own layout
    // places children in, and blue -- drawn once at the top -- for the page's own clip. Unclipped
    // on purpose: a box that overflows what it should be inside is the thing worth seeing. See
    // layout.h for the contract this draws against.
    void PaintGuides(const Painter &p, Widget *w, float ox, float oy) {
        if (!w->visible) return;
        p.rt->SetTransform(D2D1::Matrix3x2F::Translation(ox, oy));
        const D2D1_RECT_F where = w->placed ? w->drawn : w->rect;
        p.StrokeRound(where, 0.0f, Rgb(0xFF3B30, 0.85f));
        if (w->placed && !SameRect(w->drawn, w->rect))
            p.StrokeRound(w->rect, 0.0f, Rgb(0xBF5AF2, 0.85f));
        if (w->layout) {
            const D2D1_RECT_F in = w->layout->ContentBox(w->rect);
            if (in.right > in.left && in.bottom > in.top)
                p.StrokeRound(in, 0.0f, Rgb(0xFF9500, 0.85f));
        }
        const float cx = ox + where.left, cy = oy + where.top;
        for (const auto &child : w->children) PaintGuides(p, child.get(), cx, cy);
        p.rt->SetTransform(D2D1::Matrix3x2F::Identity());
    }
#endif
    void EnsureIconBitmap();
    // A picture from disk, decoded once and scaled on the way in to `maxW` pixels
    // wide. Null when the file is not there, which callers draw around rather than
    // treat as an error: a preview that has not been generated yet is a normal state,
    // not a fault. Needs COM initialised on this thread, because WIC is COM.
    ID2D1Bitmap1 *Image(const std::wstring &path, UINT maxW);
    void ReleaseImages();
    LRESULT CaptionHitTest(POINT screen) const;
    // The control under a point, in client DIPs. Walks the tree, and the reverse of the order it is
    // painted in, so whatever is drawn on top is whatever the click reaches.
    Widget *HitTest(float x, float y);
    Widget *HitTestIn(Widget *w, float x, float y);
    // Where a widget's own space begins in the client area: the accumulated origins of its
    // ancestors, which is the translation the paint walk reaches it with. A control that reads the
    // pointer or asks how much room it has wants that same answer, and this is the one place it is
    // worked out.
    D2D1_POINT_2F OriginOf(const Widget *w) const;
    // The layer calls the keyboard goes through first: the last visible one in the tree, which is
    // the same one the hit test reaches.
    Layer *TopLayer();
    Layer *TopLayerIn(Widget *w);
    void CollectTab(Widget *w, std::vector<Widget *> &out);
    void CollectLayers(Widget *w, std::vector<Layer *> &out);
    void DropGoneIn(Widget *w);
    bool SetHover(Widget *w, Widget *over);
    void DismissIn(Widget *w);
    // The pointer moved: the widget under it hears about it, and so does any widget whose watched
    // region outside itself contains it (see Widget::ExternalRegion). Returns true when something
    // under the pointer wants a repaint per move.
    bool SendMove(Widget *w, float x, float y, Widget *over);
    // The same point, in the space of a widget's own rectangle: what every input callback is handed.
    D2D1_POINT_2F LocalPoint(const Widget *w, float x, float y) const {
        const D2D1_POINT_2F o = OriginOf(w);
        return D2D1::Point2F(x - o.x, y - o.y);
    }
    Layer *LeavingIn(Widget *w);
    // Everything but `except` puts away what it is showing -- an open list, a peeked pane. From
    // a copy of the list, because a dismissal is allowed to lay the page out again and the list
    // itself may not survive that. See `retired`.
    // A click on nothing, or on something raised: everything the click was not over is told, so
    // that a drop-down left open closes. The point is in window DIPs, the space a layer's own
    // rectangle is in.
    void DismissOthers(Widget *except, float x, float y);
    // The last raised control that is a layer, which is where Esc, Enter and the Tab ring go
    // first. See Widget::AsLayer.
    void MoveFocus(int delta);
    void SetFocusTo(Widget *w);
    // Ends a gesture the pointer is no longer allowed to finish, and hands the widget
    // the release it will otherwise never see. See the WM_CAPTURECHANGED handler.
    void CancelCapture();
    void PlaceImeAtCaret();

    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l);
};

inline UINT_PTR Window::TakeTimerId() {
    // From the bottom up, and 1 is left out: `SetTimer` refuses 0, and a program that sets a timer
    // of its own is likelier to have picked 1 than 2. Timers are few, so a scan is a scan.
    for (UINT_PTR id = 2;; id++) {
        bool taken = false;
        for (UINT_PTR used : timerIds) if (used == id) { taken = true; break; }
        if (!taken) { timerIds.push_back(id); return id; }
    }
}

inline void Window::GiveTimerId(UINT_PTR id) {
    for (size_t i = 0; i < timerIds.size(); i++)
        if (timerIds[i] == id) { timerIds.erase(timerIds.begin() + i); return; }
}

inline void Timer::Start(Window *w, UINT ms, std::function<void()> fn) {
    if (!w || !w->hwnd) return;
    if (win && win != w) Stop();   // a timer belongs to one window at a time
    if (!win) {
        win = w;
        id = w->TakeTimerId();
        w->timers.push_back(this);
    }
    this->tick = std::move(fn);
    SetTimer(w->hwnd, id, ms, nullptr);   // an id that is already set is simply re-armed
}

inline void Timer::Stop() {
    if (!win) return;
    if (win->hwnd) KillTimer(win->hwnd, id);
    for (size_t i = 0; i < win->timers.size(); i++)
        if (win->timers[i] == this) { win->timers.erase(win->timers.begin() + i); break; }
    win->GiveTimerId(id);
    win = nullptr;
    id = 0;
    tick = nullptr;
}

inline Timer::~Timer() { Stop(); }

inline bool Timer::Handle(UINT_PTR which) {
    if (which != id) return false;
    // Copied, because the callback may stop this timer -- the scroll bar's state timer does --
    // and stopping clears `tick` out from under the call that is running it.
    const std::function<void()> f = tick;
    if (f) f();
    return true;
}

inline void ApplyBackdrop(HWND hwnd, bool dark, bool *micaOut,
                          DWORD backdrop = kDwmBackdropMainWindow) {
    const BOOL d = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, kDwmImmersiveDarkMode, &d, sizeof(d));
    const DWORD round = kDwmCornerRound;
    DwmSetWindowAttribute(hwnd, kDwmCornerPreference, &round, sizeof(round));
    // The one that matters, and the one that can fail. The system backdrops are Windows 11
    // 22H2 and later; before that this returns E_INVALIDARG and the caller paints an opaque
    // background instead.
    const HRESULT hr = DwmSetWindowAttribute(hwnd, kDwmSystemBackdrop, &backdrop,
                                             sizeof(backdrop));
    if (micaOut) *micaOut = SUCCEEDED(hr);
}

inline void Window::ApplyThemeToFrame() {
    ApplyBackdrop(hwnd, pal.dark, &micaActive, backdrop);
}

inline void Window::ReloadTheme() {
    // `DarkTheme()`, not `SystemUsesDarkTheme()`: a program that has said Light or Dark keeps it
    // through a system theme change, which is the whole point of there being a mode. This is the one
    // caller of it, and it is called from WM_SETTINGCHANGE when Windows says the colours changed --
    // so a page whose only statement about its theme was to set `pal` was a page that lost it the
    // moment somebody opened the personalisation settings.
    pal = MakePalette(DarkTheme());
    ApplyThemeToFrame();
    Invalidate();
}

// ---------------------------------------------------------------- device

inline bool Window::CreateDevice() {
    if (dc) return true;

    // BGRA support is required by Direct2D and is not on by default. Hardware first,
    // WARP second: this window has to appear on a machine with a broken or absent
    // display driver, because one of the things it may have to say is why.
    const D3D_DRIVER_TYPE types[] = { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP };
    for (D3D_DRIVER_TYPE t : types) {
        if (SUCCEEDED(D3D11CreateDevice(nullptr, t, nullptr,
                                        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                        nullptr, 0, D3D11_SDK_VERSION, &d3d, nullptr, nullptr)))
            break;
    }
    if (!d3d) return false;
    if (FAILED(d3d->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi))) return false;

    D2D1_FACTORY_OPTIONS opts = {};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
                                 &opts, (void **)&d2d)))
        return false;
    if (FAILED(d2d->CreateDevice(dxgi, &d2dDevice))) return false;
    if (FAILED(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc)))
        return false;

    // Grayscale, not ClearType. Subpixel antialiasing needs to know the opaque colour
    // behind the glyph, and on a composed surface with real alpha there is not one --
    // ClearType on a transparent target either fails or fringes. WinUI draws its text
    // this way for the same reason.
    dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    if (FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush))) return false;

    IDXGIAdapter *adapter = nullptr;
    IDXGIFactory2 *factory = nullptr;
    if (FAILED(dxgi->GetAdapter(&adapter)) || !adapter) return false;
    const HRESULT fhr = adapter->GetParent(__uuidof(IDXGIFactory2), (void **)&factory);
    adapter->Release();
    if (FAILED(fhr) || !factory) return false;

    RECT rc; GetClientRect(hwnd, &rc);
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width  = (UINT)(rc.right  > 0 ? rc.right  : 1);
    desc.Height = (UINT)(rc.bottom > 0 ? rc.bottom : 1);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    // The line the whole file exists for. Without premultiplied alpha the swap chain
    // is opaque and Mica is behind a solid rectangle.
    desc.AlphaMode   = DXGI_ALPHA_MODE_PREMULTIPLIED;
    const HRESULT shr = factory->CreateSwapChainForComposition(d3d, &desc, nullptr, &swap);
    factory->Release();
    if (FAILED(shr) || !swap) return false;

    if (FAILED(DCompositionCreateDevice(dxgi, __uuidof(IDCompositionDevice), (void **)&comp)))
        return false;
    if (FAILED(comp->CreateTargetForHwnd(hwnd, TRUE, &compTarget))) return false;
    if (FAILED(comp->CreateVisual(&compVisual))) return false;
    compVisual->SetContent(swap);
    compTarget->SetRoot(compVisual);
    comp->Commit();

    return CreateSizedResources();
}

inline bool Window::CreateSizedResources() {
    if (!swap || !dc) return false;
    IDXGISurface *surface = nullptr;
    if (FAILED(swap->GetBuffer(0, __uuidof(IDXGISurface), (void **)&surface)) || !surface)
        return false;
    // The DPI goes on the bitmap, so every coordinate a page or a control uses is a DIP
    // and the scale factor is applied once, by Direct2D, rather than being multiplied
    // into forty rectangles by hand.
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        (float)dpi, (float)dpi);
    const HRESULT hr = dc->CreateBitmapFromDxgiSurface(surface, &props, &target);
    surface->Release();
    if (FAILED(hr)) return false;
    dc->SetTarget(target);
    dc->SetDpi((float)dpi, (float)dpi);
    return true;
}

inline void Window::ReleaseSizedResources() {
    if (dc) dc->SetTarget(nullptr);
    if (target) { target->Release(); target = nullptr; }
}

// The icon bitmap belongs to the device context, so it goes when that does.

inline void Window::Resize() {
    if (!swap) return;
    RECT rc; GetClientRect(hwnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;
    ReleaseSizedResources();
    swap->ResizeBuffers(0, (UINT)rc.right, (UINT)rc.bottom, DXGI_FORMAT_UNKNOWN, 0);
    CreateSizedResources();
}

inline void Window::ReleaseImages() {
    for (std::map<std::wstring, ID2D1Bitmap1 *>::iterator it = images.begin();
         it != images.end(); ++it)
        if (it->second) it->second->Release();
    images.clear();
}

inline void Window::ReleaseDevice() {
    ReleaseSizedResources();
    ReleaseImages();
    if (iconBitmap) { iconBitmap->Release(); iconBitmap = nullptr; }
    if (brush)      { brush->Release();      brush = nullptr; }
    if (compVisual) { compVisual->Release(); compVisual = nullptr; }
    if (compTarget) { compTarget->Release(); compTarget = nullptr; }
    if (comp)       { comp->Release();       comp = nullptr; }
    if (swap)       { swap->Release();       swap = nullptr; }
    if (dc)         { dc->Release();         dc = nullptr; }
    if (d2dDevice)  { d2dDevice->Release();  d2dDevice = nullptr; }
    if (d2d)        { d2d->Release();        d2d = nullptr; }
    if (dxgi)       { dxgi->Release();       dxgi = nullptr; }
    if (d3d)        { d3d->Release();        d3d = nullptr; }
}

inline void Window::Paint() {
    if (!CreateDevice() || !target) return;
    // A paint the frame loop did not run -- the first one, and every one Windows asks for while it is
    // running a size or move loop of its own -- arranges for itself, because the loop is what
    // normally does it and there is no loop here. Idempotent: a paint that follows a frame finds the
    // flag already clear.
    if (layoutDirty) ArrangeTree();
    // And then said, before anything is drawn and after the tree is the tree that is about to be
    // drawn: a change is a comparison between this frame's controls and the last frame's, so it is
    // announced once and the drawing has nothing to do with it. See `UiaAnnounce`.
    UiaAnnounce();
    Painter p;
    p.rt = dc; p.br = brush; p.font = &fonts; p.pal = &pal;

    dc->BeginDraw();
    // Transparent when the material is there, opaque when it is not. This one call is
    // the difference between a Mica window and a grey one.
    dc->Clear(micaActive ? D2D1::ColorF(0, 0, 0, 0) : pal.windowBg);
    // The page is the tree now: one walk from the root, which is the client area below the caption.
    // What the flat-list passes here used to carry is now where it belongs -- furniture before the
    // page is child order, the page-wide clip is a container's own clip, and the arrival opacity is
    // what a `Layer` is drawn through. See docs/layout.md.
    PaintTree(p, content.get(), 0.0f, 0.0f);
#if MICULA_DEBUG_LAYOUT
    if (debug::layout && content) {
        // Blue: the page's own clip, which is what a container's overflow will narrow when there is
        // one. Drawn from the client area, before the walk that draws the rest.
        p.rt->SetTransform(D2D1::Matrix3x2F::Identity());
        p.StrokeRound({ 0.0f, kCaptionH, ClientW(), ClientH() }, 0.0f, Rgb(0x0A84FF, 0.85f));
        PaintGuides(p, content.get(), 0.0f, 0.0f);
    }
#endif
    // Last, so a page that draws to the top of its own area cannot run under the
    // caption -- which is now client area like any other, and has nothing but paint
    // order protecting it.
    PaintCaption(p);
    const HRESULT hr = dc->EndDraw();
    if (SUCCEEDED(hr)) {
        swap->Present(1, 0);
    } else if (hr != D2DERR_RECREATE_TARGET) {
        // Nothing to do, and the comment is the point: Direct2D batches, so an illegal
        // call is reported *here* rather than where it was made, and the entire frame
        // is discarded. That reads as "the window drew nothing", which sends you
        // looking at the layout. It has happened once already -- a FillOpacityMask in
        // the wrong antialias mode, returning D2DERR_WRONG_STATE (0x88990001) -- and
        // the way it was found was a temporary log of this HRESULT. Put one back here
        // before looking anywhere else.
    }

    // The GPU went away -- a driver update, a remote session, a virtual display driver
    // being removed. Everything device-bound is thrown away and rebuilt on the next
    // paint.
    if (hr == D2DERR_RECREATE_TARGET) {
        ReleaseDevice();
        Invalidate();
    }
}

// The app icon, as an **opacity mask** rather than as a picture. Converted once, kept.
//
// Two decisions, and the second is the one worth reading.
//
// Through WIC rather than DrawIconEx: there is no GDI device context in this window at
// all -- the surface belongs to DirectComposition -- so the icon has to arrive as a
// bitmap. `CreateBitmapFromHICON` does the AND-mask-to-alpha work that makes an icon
// with soft edges composite correctly.
//
// And converted to A8, so the caption draws it in the caption's own text colour. A
// monochrome mark -- white throughout, with the shape carried entirely in the alpha
// channel -- is fine while DWM paints the caption a dark accent colour and invisible
// the moment the caption is Mica over a light wallpaper. Drawing the alpha as a mask is
// what a monochrome mark is for, and it keeps the icon legible in both themes without
// either of them being special-cased.
//
// The cost is that a *colour* icon is drawn as its silhouette. Give the window a
// monochrome mark, or no icon at all -- the title then moves up to the left edge.
inline void Window::EnsureIconBitmap() {
    if (iconBitmap || !appIcon || !dc) return;
    IWICImagingFactory *wic = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&wic))) || !wic)
        return;
    IWICBitmap *src = nullptr;
    IWICFormatConverter *conv = nullptr;
    if (SUCCEEDED(wic->CreateBitmapFromHICON(appIcon, &src)) && src &&
        SUCCEEDED(wic->CreateFormatConverter(&conv)) && conv &&
        SUCCEEDED(conv->Initialize(src, GUID_WICPixelFormat8bppAlpha,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeMedianCut))) {
        const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_A8_UNORM, D2D1_ALPHA_MODE_STRAIGHT));
        dc->CreateBitmapFromWicBitmap(conv, &props, &iconBitmap);
    }
    if (conv) conv->Release();
    if (src) src->Release();
    wic->Release();
}

// Scaled on the way in, by WIC, not on the way out by Direct2D.
//
// Pictures are routinely far larger than they are drawn: nine screenshots at 2560x1440
// are about seventy megabytes of decoded pixels for a grid of thumbnails a hundred and
// forty pixels wide. An IWICBitmapScaler in the chain decodes straight into the size
// that will be drawn, so what reaches the GPU is the thumbnail rather than the
// wallpaper.
inline ID2D1Bitmap1 *Window::Image(const std::wstring &path, UINT maxW) {
    const std::map<std::wstring, ID2D1Bitmap1 *>::iterator hit = images.find(path);
    if (hit != images.end()) return hit->second;
    images[path] = nullptr;              // remembered even if everything below fails
    if (!dc || path.empty()) return nullptr;

    IWICImagingFactory *wic = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&wic))) || !wic)
        return nullptr;

    IWICBitmapDecoder *dec = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICBitmapScaler *scaler = nullptr;
    IWICFormatConverter *conv = nullptr;
    ID2D1Bitmap1 *out = nullptr;
    UINT w = 0, h = 0;
    if (SUCCEEDED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                 WICDecodeMetadataCacheOnDemand, &dec)) &&
        dec && SUCCEEDED(dec->GetFrame(0, &frame)) && frame &&
        SUCCEEDED(frame->GetSize(&w, &h)) && w && h &&
        SUCCEEDED(wic->CreateBitmapScaler(&scaler)) && scaler) {
        const UINT tw = maxW && w > maxW ? maxW : w;
        const UINT th = tw == w ? h : (UINT)((unsigned long long)h * tw / w);
        if (SUCCEEDED(scaler->Initialize(frame, tw, th ? th : 1,
                                         WICBitmapInterpolationModeFant)) &&
            SUCCEEDED(wic->CreateFormatConverter(&conv)) && conv &&
            SUCCEEDED(conv->Initialize(scaler, GUID_WICPixelFormat32bppPBGRA,
                                       WICBitmapDitherTypeNone, nullptr, 0.0,
                                       WICBitmapPaletteTypeMedianCut))) {
            const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_NONE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            dc->CreateBitmapFromWicBitmap(conv, &props, &out);
        }
    }
    if (conv)   conv->Release();
    if (scaler) scaler->Release();
    if (frame)  frame->Release();
    if (dec)    dec->Release();
    wic->Release();

    images[path] = out;
    return out;
}

inline void Window::PaintCaption(const Painter &p) {
    const float w = ClientW();
    const Palette &c = *p.pal;
    // Dimmed when the window is not the active one, which is the one caption cue people
    // read without knowing they are reading it.
    const D2D1_COLOR_F fg = active ? c.textPrimary : c.textDisabled;

    EnsureIconBitmap();
    if (iconBitmap) {
        // The four-argument overload, which belongs to ID2D1DeviceContext. The
        // five-argument one with the D2D1_OPACITY_MASK_CONTENT enum is
        // ID2D1RenderTarget's, and it is only legal while the antialias mode is
        // ALIASED -- called otherwise it does not fail at the call, it fails at
        // EndDraw, which throws the *entire frame* away. The window came up as a sheet
        // of bare Mica with nothing on it, and nothing in the code had moved except
        // this line.
        const D2D1_SIZE_F sz = iconBitmap->GetSize();
        const D2D1_RECT_F dst = D2D1::RectF(12, 8, 28, 24);
        const D2D1_RECT_F src = D2D1::RectF(0, 0, sz.width, sz.height);
        // ALIASED for the duration, and it is not optional: FillOpacityMask refuses in
        // per-primitive antialiasing mode and returns D2DERR_WRONG_STATE -- which
        // Direct2D reports at EndDraw, not at the call, so the whole frame vanishes and
        // the window comes up as bare Mica with nothing on it.
        //
        // Nothing is lost by it. The mode governs *geometry* edges, and there is no
        // geometry here: the mask is an A8 bitmap whose own edges are already
        // antialiased, and it is sampled, not rasterised.
        dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        dc->FillOpacityMask(iconBitmap, p.Brush(fg), &dst, &src);
        dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }

    // Beside the icon when there is one, and at the icon's own inset when there is not.
    p.Text(Title(), { iconBitmap ? 36.0f : 12.0f, 0, w - kCaptionBtnW * 3 - 8, kCaptionH },
           p.font->caption, fg);

    // Minimise, maximise/restore, close. Segoe Fluent Icons' own caption glyphs, which
    // exist at the same code points in Segoe MDL2 Assets -- see theme.h's fallback.
    const bool zoomed = IsZoomed(hwnd) != 0;
    const wchar_t *glyphs[3] = { L"\uE921", zoomed ? L"\uE923" : L"\uE922", L"\uE8BB" };
    for (int i = 0; i < 3; i++) {
        const D2D1_RECT_F r = { w - kCaptionBtnW * (3 - i), 0,
                                w - kCaptionBtnW * (2 - i), kCaptionH };
        D2D1_COLOR_F glyphColour = fg;
        const float t = captionT[i];
        if (t > 0.0f) {
            // Close goes red and its glyph goes white, which is the one caption button
            // Windows colours rather than tints. Everything else takes the subtle fill.
            // Both fade, and the fill carries its own alpha -- so there is nothing to
            // mix against and no need to know what the caption is drawn over.
            if (i == 2) {
                p.Fill(r, Fade(captionDown == i ? Rgb(0xC42B1C, 0.9f) : Rgb(0xC42B1C), t));
                glyphColour = Mix(fg, Rgb(0xFFFFFF), t);
            } else {
                p.Fill(r, Fade(captionDown == i ? c.controlBgPressed : c.subtleHover, t));
            }
        }
        const float gw = p.MeasureWidth(glyphs[i], p.font->iconSmall);
        p.Text(glyphs[i], { (r.left + r.right) / 2 - gw / 2, 0, r.right, kCaptionH },
               p.font->iconSmall, glyphColour);
    }
}

// Which part of the window a point is over, in the codes Windows expects back from
// WM_NCHITTEST.
//
// Two of these answers are load-bearing beyond hit-testing. `HTMAXBUTTON` is what makes
// Windows 11 offer its snap-layout flyout when the pointer rests on the maximise
// button -- a custom caption that returns HTCLIENT there silently loses a feature
// people use. `HTCAPTION` is what makes dragging, double-click-to-maximise and the
// right-click window menu keep working without any of them being reimplemented.
inline LRESULT Window::CaptionHitTest(POINT screen) const {
    RECT wr;
    GetWindowRect(hwnd, &wr);
    const float s = dpi / 96.0f;
    const float x = (screen.x - wr.left) / s;
    const float y = (screen.y - wr.top) / s;
    const float w = (wr.right - wr.left) / s;
    const float h = (wr.bottom - wr.top) / s;

    // The frame is gone from the client area, so the grips are synthesised. Corners
    // first, or a corner would answer as whichever edge was tested first.
    if (resizable && !IsZoomed(hwnd)) {
        const bool l = x < kResizeGrip, r = x >= w - kResizeGrip;
        const bool t = y < kResizeGrip, b = y >= h - kResizeGrip;
        if (t && l) return HTTOPLEFT;
        if (t && r) return HTTOPRIGHT;
        if (b && l) return HTBOTTOMLEFT;
        if (b && r) return HTBOTTOMRIGHT;
        if (t) return HTTOP;
        if (b) return HTBOTTOM;
        if (l) return HTLEFT;
        if (r) return HTRIGHT;
    }
    if (y < kCaptionH) {
        if (x >= w - kCaptionBtnW)     return HTCLOSE;
        if (x >= w - kCaptionBtnW * 2) return HTMAXBUTTON;
        if (x >= w - kCaptionBtnW * 3) return HTMINBUTTON;
        return HTCAPTION;
    }
    return HTCLIENT;
}

inline Layer *Window::LeavingLayer() { return LeavingIn(content.get()); }

inline Layer *Window::LeavingIn(Widget *w) {
    if (!w) return nullptr;
    if (Layer *l = w->AsLayer()) {
        if (l->Leaving()) return l;
    }
    for (const auto &child : w->children)
        if (Layer *l = LeavingIn(child.get())) return l;
    return nullptr;
}

inline Widget *Window::HitTest(float x, float y) {
    // One fade's worth of "no": while a layer is on its way out, nothing answers the pointer. What
    // is being dismissed is not a place to be pressed again, the controls on it are on their way to
    // being gone, and the page under it was not clickable a moment ago either.
    if (LeavingLayer()) return nullptr;
    return content ? HitTestIn(content.get(), x, y) : nullptr;
}

// The last child that covers the point, depth first: the reverse of the order it is painted in, so
// whatever is drawn on top is whatever the click reaches. A modul layer covers the page by
// construction, so a click that misses its contents finds the layer itself and stops there -- there
// is no "under it" to reach, which is the whole of what modal means here.
//
// `x, y` arrive in the space `w`'s *rect* is measured in, and the first thing this does is take the
// point into `w`'s own space -- the space its children's rectangles are in -- by our own origin.
// Painting does the same thing from the other end, adding each widget's origin as it goes down, and
// the two walks agreeing about it is the whole of what makes a click land on what is under the
// pointer. Leaving it out is what made every click read as thirty-two DIPs below where it was made:
// the page's own origin is the caption bar, and nothing was taking it off.
inline Widget *Window::HitTestIn(Widget *w, float x, float y) {
    const D2D1_RECT_F self = w->placed ? w->drawn : w->rect;
    x -= self.left;
    y -= self.top;
    for (auto it = w->children.rbegin(); it != w->children.rend(); ++it) {
        Widget *child = it->get();
        if (!child->visible || !child->enabled) continue;
        // The point in the child's arranged space, and then in its own space. A widget on its way
        // somewhere is reached where it *looks*: the same difference between where it was arranged
        // and where it is drawn that painting puts on is taken off here.
        const D2D1_RECT_F where = child->placed ? child->drawn : child->rect;
        if (!child->Covers(x - (where.left - child->rect.left), y - (where.top - child->rect.top)))
            continue;
        // **A container answers for itself where the point is outside its window, and for nothing under
        // it.** What is not shown is not the reader's: a row a container is not showing -- one that is
        // still under the reveal of a panel arriving here -- is not a row a click can choose. A layer is
        // the other way round and the reason this returns the container rather than ignoring it: a flyout
        // covers the page, so a click outside the panel it is holding is a click the flyout has to hear,
        // and light dismissing is what it hears it as. See Widget::Clips.
        if (child->Clips() &&
            !Inside(child->ClipBox(), x - (where.left - child->rect.left),
                    y - (where.top - child->rect.top)))
            return child;
        if (Widget *deep = HitTestIn(child, x, y)) return deep;
        return child;
    }
    return nullptr;
}

// Where a widget's own space begins in the client area: the accumulated origins of the widgets
// above it. The root's rectangle is in client coordinates -- that is what the arrange pass gives it
// -- so the walk starts at the widget's parent.
inline D2D1_POINT_2F Window::OriginOf(const Widget *w) const {
    float x = 0.0f, y = 0.0f;
    for (const Widget *at = w ? w->parent : nullptr; at; at = at->parent) {
        const D2D1_RECT_F r = at->placed ? at->drawn : at->rect;
        x += r.left;
        y += r.top;
    }
    return D2D1::Point2F(x, y);
}

inline void Window::DismissOthers(Widget *except, float x, float y) {
    std::vector<Layer *> layers;
    CollectLayers(content.get(), layers);
    // Three things survive a click. What the click was over, and the widgets above it. What is
    // *above* it -- a layer over a layer keeps its place while the lower one is being used, and
    // "above" in a tree is "later in the order", which is where the walk found it. And any layer
    // whose own body holds the click, wherever the click was aimed: a dialog sits below the button
    // on it, so an order alone would read a click on that button as a click outside the dialog.
    size_t stop = layers.size();
    if (except) {
        for (size_t i = 0; i < layers.size(); i++) {
            if (!layers[i]->Holds(except)) continue;
            stop = i;
            break;
        }
    }
    for (size_t i = 0; i < stop; i++) {
        Layer *l = layers[i];
        if (l->Holds(except)) continue;
        const D2D1_POINT_2F o = OriginOf(l);
        if (Inside(l->Body(), x - o.x, y - o.y)) continue;
        l->Dismiss();
    }
}

inline void Window::CollectLayers(Widget *w, std::vector<Layer *> &out) {
    if (!w) return;
    for (const auto &child : w->children) {
        if (!child->visible) continue;
        if (Layer *l = child->AsLayer()) out.push_back(l);
        CollectLayers(child.get(), out);
    }
}

// The layer the window's keyboard goes to first: the last of the raised controls that is one,
// which is the same one the pointer would reach -- the hit test walks the list this way round.
inline Layer *Window::TopLayer() {
    return content ? TopLayerIn(content.get()) : nullptr;
}

inline Layer *Window::TopLayerIn(Widget *w) {
    for (auto it = w->children.rbegin(); it != w->children.rend(); ++it) {
        Widget *child = it->get();
        if (!child->visible) continue;
        // Depth first, and the deepest wins: a dialog with a flyout open over it has the flyout on
        // top, and the flyout's own contents are above the flyout.
        if (Layer *l = TopLayerIn(child)) return l;
        if (Layer *l = child->AsLayer()) return l;
    }
    return nullptr;
}

inline void Layer::Close() {
    if (leaving) return;
    leaving = true;
    // The focus goes before the page is told, so that whatever the page does about it wins and no
    // caret is left in a control that is on its way out. Everything else the layer owns is its own
    // subtree, which fades with it and is dropped with it -- what used to be marked with
    // `leavingWith` and hunted for in a flat list.
    if (Window *w = window()) {
        if (w->focused && Holds(w->focused)) w->SetFocusTo(nullptr);
    }
    if (Animations()) {
        arrive.To(0.0f);
    } else {
        // Nothing to watch: gone now. `visible` false rather than a fade of no frames, so that
        // nothing waits for a fade that is never going to run.
        arrive.Set(0.0f);
        visible = false;
    }
    if (onDismiss) onDismiss();
}

inline void Window::DropGoneLayers() {
    if (content) DropGoneIn(content.get());
}

inline void Window::DropGoneIn(Widget *w) {
    for (size_t i = 0; i < w->children.size(); ) {
        Widget *child = w->children[i].get();
        Layer *l = child->AsLayer();
        if (!l || !l->HasLeft()) {
            DropGoneIn(child);
            i++;
            continue;
        }
        // A layer that has finished leaving takes its whole subtree with it: what came out with a
        // layer is its children now, so there is nothing to mark and nothing to hunt for. One at a
        // time, so that a second layer still fading is not taken with it.
        if (focused && child->Holds(focused)) focused = nullptr;
        if (capture && child->Holds(capture)) capture = nullptr;
        // Retired rather than deleted while a message is being dispatched: the page was told inside
        // a callback of its own, and a page that kept a pointer to the layer has a live object to
        // read until that message returns. See Window::retired.
        if (dispatchDepth > 0) retired.emplace_back(std::move(w->children[i]));
        w->children.erase(w->children.begin() + (ptrdiff_t)i);
    }
}

inline bool Window::RefreshHover() {
    POINT pt = {};
    if (!GetCursorPos(&pt)) return false;
    // Nothing changes state while a layer is on its way out. The pointer cannot reach anything (see
    // HitTest), so the answer is the same as last frame's -- and a highlight going out under the
    // cursor as a panel began to fade would be the one thing in the picture moving that is not the
    // fade.
    if (LeavingLayer()) return false;
    // **A finger is not hovering anything**, and the mouse's position is not where the finger is: while
    // one is down the pointer is the finger, and what is under it is what it is holding. See `MoveTo`.
    if (finger) return content ? SetHover(content.get(), nullptr) : false;
    // Whose window the pointer is actually over. A cursor resting on something else
    // must not leave a control lit: this is called from the tick, not from a mouse
    // message, so there is no WM_MOUSELEAVE to lean on.
    const bool mine = WindowFromPoint(pt) == hwnd;
    ScreenToClient(hwnd, &pt);
    const float s = scale();
    Widget *over = capture ? capture : (mine ? HitTest(pt.x / s, pt.y / s) : nullptr);
    return content ? SetHover(content.get(), over) : false;
}

// One widget is hovered and every other one is not. A walk rather than a loop over a list, and the
// point of doing it from the tick is that the widget which is *not* under the pointer has to be told
// so whether or not it ever heard about a move: a page can change shape under a pointer that has not
// moved at all.
inline bool Window::SetHover(Widget *w, Widget *over) {
    bool changed = false;
    for (const auto &child : w->children) {
        const bool now = (child.get() == over);
        if (child->hover != now) {
            child->hover = now;
            changed = true;
        }
        if (SetHover(child.get(), over)) changed = true;
    }
    return changed;
}

// The pointer moved, in client DIPs. The widget under the pointer hears about it, and so does any
// widget whose watched region outside itself contains it -- a scroll bar showing itself when the
// pointer crosses the page it scrolls is what that exists for. Nobody else hears about it: being
// told about every move in the window is not the same offer, it is a coordinate space each control
// would then have to correct by hand.
//
// The same walk as HitTestIn, and it has to be: what a control is offered a point *by* has to be
// what the hit test would have chosen at that point, or a control is told about moves that are not
// over it. `x, y` are in the space `w`'s rect is measured in; the point goes into `w`'s own space
// here, once, and the children are then handed points in theirs.
inline bool Window::SendMove(Widget *w, float x, float y, Widget *over) {
    const D2D1_RECT_F self = w->placed ? w->drawn : w->rect;
    x -= self.left;
    y -= self.top;
    bool tracks = false;
    for (const auto &child : w->children) {
        Widget *c = child.get();
        if (!c->visible || !c->enabled) continue;
        const D2D1_RECT_F where = c->placed ? c->drawn : c->rect;
        const float px = x - (where.left - c->rect.left);
        const float py = y - (where.top - c->rect.top);
        // The same walk the hit test makes, and it has to be: a container that is not showing the point
        // is not offered it, or a row under a reveal would light up before there is anything to light.
        // The container itself is still offered it, which is what a scroll bar at the edge of a page
        // hears the pointer through.
        const bool shown = !c->Clips() || Inside(c->ClipBox(), px, py);
        if ((c == over && shown) || (shown && Inside(c->ExternalRegion(), px, py))) {
            c->OnPointerMove(px, py);
            if (c->TracksPointer()) tracks = true;
        }
        if (shown && SendMove(c, x, y, over)) tracks = true;
    }
    return tracks;
}

// Everything puts away what it is showing -- an open list, a peeked pane. A walk rather than a
// broadcast to a list, and the reason it is a broadcast at all is that a control cannot see a click
// or a deactivation it did not get.
inline void Window::DismissIn(Widget *w) {
    for (const auto &child : w->children) {
        child->Dismiss();
        child->hover = false;
        DismissIn(child.get());
    }
}

// The gesture that was in progress cannot finish: the capture went to another window,
// the system took it back for a modal state of its own, or this window lost the
// activation. Nothing else here notices. WM_LBUTTONUP is delivered to whoever holds the
// capture, and from that moment on that is no longer this window, so the release is
// synthesised rather than waited for.
//
// Without it a drag has no end at all: a scroll bar with a repeat timer running keeps
// scrolling, a slider keeps its knob grabbed, and a button that was held down stays
// looking held. The capture is dropped before OnRelease runs, because a widget is free
// to lay the page out again there and this must not re-enter on the way.
inline void Window::CancelCapture() {
    Widget *w = capture;
    finger = 0;
    // The gesture is abandoned rather than finished, so nothing carries on from it: a page left to
    // coast into its own end would be an animation nobody asked for, from a hand that is not there.
    panning = nullptr;
    if (!w) return;
    capture = nullptr;
    w->pressed = false;
    if (w->enabled) w->OnRelease();
    Invalidate();
}

// ---- the three things a pointer does, whichever hand it is -----------------------------------------
//
// The messages differ and the meaning does not: a mouse, a finger and a pen all say "down here",
// "moved to here" and "up", and a control that answers one of them should be answering all three. So
// the window's own part of that -- which widget is under the pointer, whether it takes the focus, what
// the widget's callbacks are, what a click means -- is written here once and reached from two message
// handlers, and the hand decides only what a hand really decides. See `Hand`, and the two handlers in
// `Proc`.
inline void Window::PressAt(float x, float y, Hand hand) {
    pressX = x;
    pressY = y;
    pointerX = x;
    pointerY = y;
    capturing = hand;
    panning = nullptr;
    Widget *w = HitTest(x, y);
    // Everything else puts away whatever it was showing. This is what closes an open
    // drop-down when the click lands somewhere else -- including on nothing, which is
    // the case the control itself can never see.
    DismissOthers(w, x, y);
    // Those dismissals can lay the page out again -- a pane that closes tells the page, and
    // a page that lays itself out is a different list of widgets. What the pointer was over
    // is then a control that has been retired, freed when this message returns, and a press
    // taken on it would leave the capture pointing at memory that is going: the mouse-up
    // after it is the crash. So the hit test is made again, on the page that is there now.
    w = HitTest(x, y);
    // Clicking anywhere takes the focus ring away again: it is a keyboard
    // affordance, and a mouse user who has just clicked a button does not want the
    // rectangle left behind on it.
    showFocusRing = false;
    if (w) {
        SetCapture(hwnd);
        capture = w;
        w->pressed = true;
        // **A click either lands the focus or puts it away.** A control that can be operated from
        // the keyboard takes it; anything else on the page -- the page itself, a card, a line of
        // text -- is the blank part of the page, and a field that kept the focus while somebody
        // clicked there would eat the next keystroke. This is what commits a field whose text was
        // edited and left. In the flat list the page was not a widget, so "not focusable" and
        // "nothing" were one answer and one line of code served both; every box on a page is a
        // widget now, and the hit test has something to say about all of them.
        //
        // **A layer is the exception**, and it is why this is not simply the `else` branch: it
        // covers the page, so a click inside one -- a question's dim, the row of a list a drop-down
        // opened -- belongs to the layer, and what is on a layer has its own focus. A click between
        // its rows must not take the ring off whatever the page put there.
        Layer *const top = TopLayer();
        if (w->Focusable()) SetFocusTo(w);
        else if (!top || !top->Holds(w)) SetFocusTo(nullptr);
        // From the message rather than from GetCursorPos, and the difference is not
        // theoretical: the pointer can have moved between the click being queued and
        // this running, and a press position that disagrees with the hit test by a
        // pixel is a gesture that grabbed the wrong sub-region of its own control. In the
        // widget's own space, which is the space its `rect` is in.
        const D2D1_POINT_2F at = LocalPoint(w, x, y);
        w->OnPress(at.x, at.y);
    } else if (!LeavingLayer()) {
        // Nothing at all, and no layer on its way out to have swallowed the click -- and a layer
        // that is leaving *is* what swallows it: the hit test answers with nothing while one is
        // going, which is what stops the click that dismissed a flyout from also landing on the
        // page it was over. That click is the layer's; this one is the blank page's.
        SetFocusTo(nullptr);
    }
    Invalidate();
}

// Returns whether anything about what the pointer is over changed, which is the window's reason to
// draw a frame -- the same contract the two walks it drives already have.
inline bool Window::MoveTo(float x, float y, Hand hand, bool contact) {
    // **A mouse is not where a finger is.** While one is down, the pointer a page can ask about is the
    // finger -- see `Widget::Cursor` -- so a mouse that happens to be somewhere else on the screen does
    // not move it. This is not a corner case: it is the usual one, and a control reading the last thing
    // that moved would be pointed at whatever the mouse was resting on.
    if (!(finger && hand == Hand::Mouse)) {
        pointerX = x;
        pointerY = y;
    }
    // **A finger has no hover.** Nothing is "over" a control that a hand is touching, and a control
    // that lit up as the hand went by would be lighting up for nothing, so a touch clears whatever was
    // hovering rather than putting something new there. A mouse always hovers and a pen hovers while it
    // is in the air, which is what `contact` is for.
    const bool hovering = hand != Hand::Finger;
    Widget *over = capture ? capture : HitTest(x, y);
    const bool changed = content ? SetHover(content.get(), hovering ? over : nullptr) : false;

    // Only the hand that made the press drives what it is doing. See `capturing`.
    if (capture && hand == capturing) {
        // In the capture's own space, so a control dragged while something above it is still
        // moving does not un-press itself -- and from the message, so the point that decides
        // whether it is still pressed is the point it is being dragged with.
        const D2D1_POINT_2F at = LocalPoint(capture, x, y);
        bool down = contact && Inside(capture->rect, at.x, at.y);
        // **A finger that has wandered is not holding a click any more**, even while it is still over
        // the control: eight DIPs of travel is what separates a tap from a drag, and without this a
        // page scrolled with a finger would end as a click on the row it started on. The drag itself
        // goes on regardless -- a slider is dragged exactly this way, and it is the control's own
        // answer about tracking the pointer that says whether it wants to be.
        if (down && hand == Hand::Finger &&
            (std::fabs(x - pressX) > kTouchSlop || std::fabs(y - pressY) > kTouchSlop))
            down = false;
        // **And a drag that no control is using is the container's.** The control has just said it is
        // not a click, so the question is who the gesture belongs to. A container that pans takes it
        // itself; a control that is moving its own value keeps it, and one that only wants the pointer
        // so its own highlight can follow -- a list's rows -- does not, because a list's rows following
        // a finger and the list scrolling under it are the same gesture. Failing both, it is the first
        // thing above that scrolls, found once and kept for the rest of the gesture.
        if (hand == Hand::Finger && contact && !panning && !down) {
            if (capture->Pans()) panning = capture;
            else if (!capture->Dragging()) panning = PanTargetFor(capture);
            panLastY = y;
            panLastT = MonotonicSeconds();
            panSpeedY = 0.0f;
        }
        capture->pressed = down;
        if (panning) {
            const double now = MonotonicSeconds();
            const float dt = (float)(now - panLastT);
            panLastT = now;
            // Smoothed rather than taken whole: one jittery frame at the end of a fling would be the
            // whole of what the fling is worth.
            if (dt > 0.001f) panSpeedY = panSpeedY * 0.6f + ((y - panLastY) / dt) * 0.4f;
            panning->PanMove(0.0f, y - panLastY);
            panLastY = y;
        } else {
            capture->OnDrag(at.x, at.y);
        }
    }

    const bool tracks = content ? SendMove(content.get(), x, y, over) : false;
    // A cursor is a mouse's business: a finger has none, and one that appeared under a finger would
    // be a lie about where the pointer is.
    if (hand == Hand::Mouse)
        SetCursor(LoadCursorW(nullptr, !over             ? kCursorArrow
                                     : over->TextCursor() ? kCursorIBeam
                                     : over->HandCursor() ? kCursorHand
                                                          : kCursorArrow));
    return changed || tracks;
}

inline void Window::ReleaseAt(Hand hand) {
    if (hand != capturing) return;
    Widget *w = capture;
    Widget *pan = panning;
    const float speed = panSpeedY;
    capture = nullptr;
    panning = nullptr;
    ReleaseCapture();
    // A gesture that scrolled a page was over the moment it stopped being a tap: whatever it started on
    // gets its own state put back, and the container is told how fast the hand was going, which is what
    // the fling is made of. Nothing here is a click.
    if (pan) {
        if (w) w->pressed = false;
        pan->PanRelease(0.0f, speed);
        Invalidate();
        return;
    }
    if (w) {
        // A release that is a click: the pointer is still on the control, and the
        // control was not being dragged. `pressed` alone used to say both, and stops
        // saying the second the moment a widget keeps it through a drag that has left
        // its rectangle -- a slider let go three rows away is not a click on whatever
        // it was let go over.
        const bool click = w->pressed;
        w->pressed = false;
        Invalidate();
        if (w->enabled) w->OnRelease();
        // OnClick last, and after the state is already tidy: a click can replace
        // the entire widget list (that is what "next page" is), and touching `w`
        // after that is a use-after-free. OnRelease goes before it for the same
        // reason -- by the time OnClick has returned, `w` may not exist.
        if (click && w->enabled) w->OnClick();
    }
}

// The first thing above `w` that scrolls, or null. `w` itself is not asked: a press that landed on it
// has just said, by not tracking the pointer, that it does not want the drag.
inline Widget *Window::PanTargetFor(Widget *w) {
    for (Widget *up = w->parent; up; up = up->parent)
        if (up->Pans()) return up;
    return nullptr;
}

// ---- the three calls that belong to the window, which is what widget.h has only declared --------
//
// A node knows how to ask for things and the window knows what to do about them: an arrangement is
// one walk of the whole tree, and there is no per-widget dirty flag to keep honest.

inline void Widget::InvalidateLayout() {
    if (Window *w = window()) w->layoutDirty = true;
}

inline void Widget::Invalidate() {
    if (Window *w = window()) w->Invalidate();
}

inline void Widget::Remove(Widget *child) {
    // A tree on its way down takes every node with it, and this is the one moment a removal is not a
    // removal: the parent's vector of children is what is running this, and erasing from it is a write
    // into memory it no longer owns. See Window::tearingDown.
    if (Window *w = window()) {
        if (w->tearingDown) return;
    }
    for (size_t i = 0; i < children.size(); i++) {
        if (children[i].get() != child) continue;
        // Deferred while a message is being dispatched, and that is the whole of it: a control is
        // allowed to take itself out from inside its own callback, and the closure running that
        // callback lives in the block this would free. See Window::retired.
        Window *w = window();
        if (w && w->dispatchDepth > 0) {
            w->retired.emplace_back(std::move(children[i]));
            children.erase(children.begin() + i);
            return;
        }
        children.erase(children.begin() + i);
        return;
    }
}

inline D2D1_POINT_2F Widget::Cursor() const {
    Window *w = window();
    if (!w) return D2D1::Point2F(0.0f, 0.0f);
    // The window's own memory of the pointer rather than the cursor's position: see `Window::pointerX`.
    const D2D1_POINT_2F o = w->OriginOf(this);
    return D2D1::Point2F(w->pointerX - o.x, w->pointerY - o.y);
}

inline D2D1_RECT_F Widget::VisibleArea() const {
    Window *w = window();
    if (!w || !w->content) return D2D1_RECT_F{ 0, 0, 0, 0 };
    // **Every container above it that is a window onto its children, intersected**, with the page's own
    // box as the one it starts from. The nearest of them alone is a box that something above it may
    // already have cut: a control inside a list inside a panel being uncovered is inside all three, and
    // the room it really has is the smallest of them. A control in a scrolling container that asked the
    // page instead would be told it has room it does not have.
    //
    // In this widget's own space, which is the space its `rect` is written in: `OriginOf` is where a
    // widget's rectangle is measured *from* in the client, so the difference between the two origins is
    // what carries one space's rectangle into another.
    const D2D1_POINT_2F mine = w->OriginOf(this);
    const Widget *root = w->content.get();
    const D2D1_POINT_2F at = w->OriginOf(root);
    D2D1_RECT_F out = { root->rect.left + at.x - mine.x, root->rect.top + at.y - mine.y,
                        root->rect.right + at.x - mine.x, root->rect.bottom + at.y - mine.y };
    for (const Widget *box = this; box; box = box->parent) {
        if (!box->Clips()) continue;
        // The box is the one the window *shows*, which for a container that is showing only a part of
        // itself is that part -- a control in a panel that is still arriving is told the room it can be
        // reached in. See Widget::ClipBox.
        const D2D1_POINT_2F there = w->OriginOf(box);
        const D2D1_RECT_F r = box->ClipBox();
        const D2D1_RECT_F in = { r.left + there.x - mine.x, r.top + there.y - mine.y,
                                 r.right + there.x - mine.x, r.bottom + there.y - mine.y };
        out.left = (std::max)(out.left, in.left);
        out.top = (std::max)(out.top, in.top);
        out.right = (std::max)(out.left, (std::min)(out.right, in.right));
        out.bottom = (std::max)(out.top, (std::min)(out.bottom, in.bottom));
    }
    return out;
}

inline bool Widget::ShowFocusRing() const {
    Window *w = window();
    return focus && w && w->showFocusRing;
}

inline void Window::SetFocusTo(Widget *w) {
    if (focused == w) return;
    if (focused) { focused->focus = false; focused->OnBlur(); }
    focused = w;
    if (focused) { focused->focus = true; focused->OnFocus(); }
    caretOn = true;
    Invalidate();
    UiaFocusChanged();
}

inline void Window::MoveFocus(int delta) {
    std::vector<Widget *> tab;
    // A layer that names a ring takes the whole of Tab. It is what makes a modal dialog modal to
    // the keyboard as well as to the pointer, and it is how a flyout keeps the focus inside
    // itself. An empty ring leaves the page's own order alone. A layer on its way out names nothing:
    // its ring is on its way to being gone, and Tab in the middle of a dismissal belongs to the page
    // that is about to be the only thing there.
    if (Layer *top = TopLayer()) {
        if (!top->Leaving()) tab = top->FocusRing();
    }
    if (tab.empty() && content) CollectTab(content.get(), tab);
    if (tab.empty()) return;
    int at = -1;
    for (size_t i = 0; i < tab.size(); i++) if (tab[i] == focused) at = (int)i;
    at = (at < 0) ? (delta > 0 ? 0 : (int)tab.size() - 1)
                  : (int)(((size_t)at + tab.size() + delta) % tab.size());
    showFocusRing = true;
    SetFocusTo(tab[at]);
}

// Tab order is document order, which is the order a screen reader reads and the order the controls
// were added -- a page no longer has to keep anything in step. A layer that is on its way out is not
// in it: its ring is on its way to being gone, and Tab in the middle of a dismissal belongs to the
// page that is about to be the only thing there.
inline void Window::CollectTab(Widget *w, std::vector<Widget *> &out) {
    for (const auto &child : w->children) {
        if (!child->visible || !child->enabled) continue;
        if (Layer *l = child->AsLayer()) {
            if (l->Leaving()) continue;
        }
        if (child->Focusable()) out.push_back(child.get());
        CollectTab(child.get(), out);
    }
}

// Put the IME's candidate and composition windows at the caret rather than at the
// top-left corner of the window, which is where they land by default. On a machine
// with a Chinese IME that default is the difference between a usable field and one
// that types into a box floating over the title bar.
inline void Window::PlaceImeAtCaret() {
    D2D1_POINT_2F pt;
    if (!focused || !focused->CaretPoint(&pt)) return;
    HIMC imc = ImmGetContext(hwnd);
    if (!imc) return;
    COMPOSITIONFORM cf = {};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = (LONG)(pt.x * scale());
    cf.ptCurrentPos.y = (LONG)(pt.y * scale());
    ImmSetCompositionWindow(imc, &cf);
    CANDIDATEFORM caf = {};
    caf.dwStyle = CFS_CANDIDATEPOS;
    caf.ptCurrentPos = cf.ptCurrentPos;
    ImmSetCandidateWindow(imc, &caf);
    ImmReleaseContext(hwnd, imc);
}

inline bool Window::Create(int dipW, int dipH, bool canResize, HICON icon) {
    // COM, and per-monitor v2 if nobody has said otherwise, before anything below needs either:
    // WIC and UI Automation are COM and the caption icon goes through WIC, and this window is about
    // to be sized in the DPI of the monitor it lands on. See detail::ComApartment for why this is
    // here rather than in App, and dpiapi::AwarenessSettled for what "if nobody has said otherwise"
    // means: a program with its own manifest keeps what its manifest says.
    detail::EnsureCom();
    if (!dpiapi::AwarenessSettled()) dpiapi::EnablePerMonitorV2();
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown **>(&dw))))
        return false;
    if (!fonts.Create(dw)) return false;
    // The program's own answer if it has one, and the machine's otherwise -- see ThemeMode. Read
    // here rather than assumed, so that a window told before it was made comes up in it.
    pal = MakePalette(DarkTheme());

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = Proc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = ClassName();
    wc.hCursor       = LoadCursorW(nullptr, kCursorArrow);
    wc.hIcon         = icon;
    wc.hIconSm       = icon;
    // No background brush. Every pixel comes from the composition surface, and letting
    // USER32 erase first is a flash of grey on every resize.
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    // The window is created at the DPI of the monitor it lands on, which is not known
    // until it exists. So: create it small, ask what DPI it got, then size it.
    // Creating it at 96-DPI pixels and letting WM_DPICHANGED fix it up produces a
    // visible resize on every high-DPI machine, which is most of them.
    // The style bits stay exactly as they were, caption included. They are what DWM
    // reads to decide that this is an ordinary top-level window -- shadow, rounded
    // corners, snap, Alt+Tab. What changes is only where the client area starts, and
    // that is WM_NCCALCSIZE's business, not the style's.
    resizable = canResize;
    appIcon = icon;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    if (resizable) style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, ClassName(), Title(), style,
                           CW_USEDEFAULT, CW_USEDEFAULT, 100, 100,
                           nullptr, nullptr, wc.hInstance, this);
    if (!hwnd) return false;

    // dipW x dipH is the client area. The frame around it is not the one
    // AdjustWindowRectEx computes for this style -- WM_NCCALCSIZE hands the caption and
    // the top border to the client -- so it is measured from the window instead: size it
    // once, read how much of it is not client, and size it again.
    dpi = dpiapi::ForWindow(hwnd);
    const int cw = MulDiv(dipW, dpi, 96), ch = MulDiv(dipH, dpi, 96);
    SetWindowPos(hwnd, nullptr, 0, 0, cw, ch, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    MeasureFrame();
    const int w = cw + frameExtra.cx, h = ch + frameExtra.cy;
    // Centred on the monitor the window landed on, not on the primary. On a laptop
    // docked to a second screen those are different, and a window that opens on the
    // screen the pointer is not on is a small daily annoyance this can avoid.
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    const int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2;
    const int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2;
    SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER);

    ApplyThemeToFrame();
    // A page has already built its tree by the time it gets here -- Add forwards to the root widget
    // -- so there is nothing to lay out yet: the first arrangement happens on the way to the first
    // paint.
    EnsureContent();
    layoutDirty = true;
    if (showCommand != SW_HIDE) {
        ShowWindow(hwnd, showCommand);
        UpdateWindow(hwnd);
    }
    return true;
}

// Two loops in one, and which of them is running is the whole of this window's
// relationship with the clock.
//
// **Idle**: block in GetMessage, exactly as before. A settings window with nothing
// moving costs nothing, which is the state it is in for all but a few seconds of its
// life.
//
// **Animating**: drain the queue, draw one frame, and wait for the compositor's own
// clock. `DCompositionWaitForCompositorClock` returns when DWM is about to compose the
// next frame -- it is the same clock the window's visual is being composed against, so
// frames land one refresh apart by construction.
//
// What this replaces is a 16 ms WM_TIMER, and the replacement is not a preference.
// WM_TIMER is generated only when the queue is empty, is quantised to the system tick,
// and competes with the WM_PAINT it posts; measured over a page change and a scroll it
// produced gaps of 2 to 35 ms with a median of 24, which reads as stutter. Present()
// does not pace anything here either -- it was measured at 0.1 ms, because a flip-model
// composition swap chain with two buffers queues the frame and returns.
//
// The timeout is a backstop, not a cadence: if DWM ever stops ticking -- which it does
// not, here or on any machine this has been measured on -- the loop keeps turning slowly
// rather than hanging with an animation half-finished.
//
// **Windows 10 has no compositor clock.** The function is build 22000 and later, and a
// static call binds it by ordinal (#1104) through the SDK's dcomp.lib; Windows 10's
// dcomp.dll has nothing in that slot, so the loader stops the process with
// STATUS_ORDINAL_NOT_FOUND (0xC0000138) before wWinMain -- no window, no message, not
// even the program's own "this needs Windows 11" if it has one. So it is resolved at
// run time. Without it the loop waits out the rest of one display refresh on a
// high-resolution waitable timer. Not Sleep(1): Present() returns in 0.1 ms here, so a
// 1 ms yield paces nothing and the window draws as fast as the GPU will queue frames.
namespace frameclock {
using Fn = DWORD(WINAPI *)(UINT, const HANDLE *, DWORD);
inline Fn Resolve() {
    static const Fn fn = [] {
        HMODULE m = GetModuleHandleW(L"dcomp.dll");
        if (!m) m = LoadLibraryW(L"dcomp.dll");
        return m ? (Fn)GetProcAddress(m, "DCompositionWaitForCompositorClock") : nullptr;
    }();
    return fn;
}
// One refresh of the monitor the window is on, in 100 ns; 60 Hz when the display
// reports 0 or 1, which both mean "the hardware default".
inline LONGLONG RefreshPeriod(HWND hwnd) {
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi) &&
        EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) &&
        dm.dmDisplayFrequency > 1)
        return 10000000LL / dm.dmDisplayFrequency;
    return 10000000LL / 60;
}
}  // namespace frameclock

// Seconds since the process started, monotonic, off the same performance counter the frame
// loop times its frames with. QPC's frequency is fixed for the life of the process, so it
// is read once.
//
// For a control whose animation is periodic and holds nothing else. Asking the clock where
// in its cycle *now* is gives the same answer to a control that has just been built as to
// the one it replaced, and a control is rebuilt for all sorts of reasons -- a resize, a save
// that changes the shape of the page, a page that lays itself out in response to a scroll.
// A phase counted per frame starts over from the beginning every time. See ProgressBar.
inline double MonotonicSeconds() {
    static const LARGE_INTEGER freq = [] {
        LARGE_INTEGER f = {};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    return freq.QuadPart ? (double)now.QuadPart / (double)freq.QuadPart : 0.0;
}

// ---------------------------------------------------------------- App

// The application: a message loop, and the windows it pumps.
//
// An ordinary object, local to wWinMain, rather than the singleton a framework usually has -- and
// the reason is not tidiness. `inline` functions with a `static` in them are one per *module*, so a
// header-only library whose program is split across DLLs would have one "the application" for each
// of them and no way to say which. A local object has one per thread that wants one, which is what
// Qt's event loop is too: the loop belongs to the thread, and a thread pumps its own windows.
//
// ```cpp
// int WINAPI wWinMain(HINSTANCE, HINSTANCE, wchar_t *, int) {
//     App app;
//     MainWindow main;
//     if (!main.Create(1040, 700, true, nullptr)) return 1;
//     app.Add(main);
//     return app.Run();
// }
// ```
//
// A window has to have been created on this thread. It does *not* have to outlive the App -- a
// window that dies takes itself out of the list, and the App's destructor clears the back-pointer of
// anything still in it -- and it does not have to be added before the loop starts, which is what a
// windowed popup will want.
//
// `Run` returns when the last window in the list is gone, or when something posts WM_QUIT.
struct App {
    App() = default;
    App(const App &) = delete;
    App &operator=(const App &) = delete;
    ~App();

    // The windows this loop pumps, in the order they were added.
    std::vector<Window *> windows;

    // Add a window. It joins the loop at the next message that is asked for.
    void Add(Window &w);
    // Take one out. The loop stops when the last one goes, which is what closing the last window of
    // an application looks like from here.
    void Remove(Window &w);
    // Run until there is nothing left to run.
    int  Run();
    // End the loop where it stands, with that exit code, whatever is still open.
    void Quit(int code = 0);
    bool Running() const { return running; }

private:
    // Window is the other half of this: the loop is written in terms of what a window knows about
    // its own frame -- the clocks, the `animOn` flag, the dispatch guard -- and none of that is
    // worth a public API.
    friend struct Window;

    bool running = false;

    // Any window that wants frames, with each window's own `animOn` kept in step: it is what
    // Invalidate() asks before deciding whether to set an update region. See Window::Invalidate.
    bool Moving();
    // One message, inside the guard of the window it is for -- found from the handle rather than
    // from the list, because a message can arrive for a window that is in no app at all.
    void PumpMessage(MSG *msg);
};

inline App::~App() {
    // The App is usually a local in wWinMain and the windows are usually locals after it, so it is
    // the App that goes first. Nothing else would notice that a pointer it kept had died.
    for (Window *w : windows) w->app = nullptr;
}

inline void App::Add(Window &w) {
    if (w.app == this) return;
    w.app = this;
    windows.push_back(&w);
    // A window that joins a loop that is already running needs everything App::Run sets up for the
    // ones that were there when it started, and it is not the clock that is the subtle half: the
    // frequency is asked for there and nowhere else, so a window added later divides every frame by
    // zero -- an infinite dt that the clamp turns into a tenth of a second, on *every* frame, which
    // is most of an 83 ms transition gone before the first repaint. The caret's timer is the other
    // half, and it is the one that would have been noticed eventually.
    if (running) w.BeginPump();
}

inline void App::Remove(Window &w) {
    for (size_t i = 0; i < windows.size(); i++) {
        if (windows[i] != &w) continue;
        windows.erase(windows.begin() + i);
        break;
    }
    w.app = nullptr;
    if (running && windows.empty()) PostQuitMessage(0);
}

inline void App::Quit(int code) {
    running = false;
    PostQuitMessage(code);
}

inline bool App::Moving() {
    bool any = false;
    for (Window *w : windows) {
        // **A tree that owes an arrangement is a window with a frame to run.** The frame is where a
        // change made by the message just handled becomes geometry and then paint -- "a child added, a
        // layout replaced and a widget hidden all set the one flag, and it is cleared here", as
        // `Window::Frame` puts it -- so a window that owes one and is not animating has to be a window
        // the loop turns for, or the change waits for whatever else happens to paint. Which can be two
        // seconds: a dragged scroll bar leaves the view where it was, because a bar that is already out
        // is not animating, and the scroll is applied when the bar's auto-hide timer next comes up.
        const bool on = w->Visible() &&
                        (w->layoutDirty || w->Animating() || w->AnimationWanted());
        // A window that is *starting* to move picks its clock up here, which is where the one-window
        // loop did it for its window and only its window. Without it, that window's first frame
        // carries however long it spent sitting still while another window kept the loop awake -- and
        // Frame() clamps that to a tenth of a second, which is most of an animation: the pane the
        // person just opened is nearly there before the second frame. The re-base in Run covers the
        // first animation of the whole loop, which is exactly why every one after it was wrong.
        if (on && !w->animOn) QueryPerformanceCounter(&w->qpcLast);
        // A window that is not moving still has to keep its own flag honest: false means its
        // Invalidate() sets an update region and its WM_PAINT does the drawing, which is how a
        // window that is not animating is meant to be repainted.
        w->animOn = on;
        if (on) any = true;
    }
    return any;
}

inline void App::PumpMessage(MSG *msg) {
    Window *target = reinterpret_cast<Window *>(GetWindowLongPtrW(msg->hwnd, GWLP_USERDATA));
    if (!target) { DispatchMessageW(msg); return; }
    // The guard is the *target's*, and that is the whole point of looking it up: a page on the
    // second window that lays itself out in a callback would otherwise free the widgets its own
    // callback is standing on, which is the crash Window::retired exists to prevent.
    Window::Dispatch frame(target);
    DispatchMessageW(msg);
}

inline Window::~Window() {
    // A window that dies while it is still in an app takes itself out of it: the app holds bare
    // pointers, and this may be the last thing that happens to either of them.
    if (app) app->Remove(*this);
    // **The tree comes down now, and it has to be allowed to.** A control may own a layer that is not
    // in its own subtree -- a drop-down's list is added where it covers the page -- and unlinking that
    // from its parent's vector of children while that vector is being destroyed is not a removal, it
    // is a write into memory the vector no longer owns. Everything under `content` dies either way, so
    // the removal is dropped rather than performed. See Widget::Remove.
    tearingDown = true;
}

// What used to be the first and the last thing the one window's loop did, now per window because
// there can be more than one.
inline void Window::BeginPump() {
    // GetCaretBlinkTime's own default period. The window owns this timer the way a control owns
    // its own; see Timer.
    caretTimer.Start(this, 530, [this] {
        // Only repaint when there is a caret to blink. A window that invalidates twice a second
        // forever is a window that keeps a laptop's GPU awake.
        if (focused && focused->CaretPoint(nullptr)) {
            caretOn = !caretOn;
            Invalidate();
        }
    });
    QueryPerformanceFrequency(&qpcFreq);
    QueryPerformanceCounter(&qpcLast);
}

// Nothing is left to fire at, and the window is about to go back to its page: the caret's and the
// frame loop's timers are stopped here rather than in their destructors, which run with no hwnd
// left. The device goes with them, so that a window that comes back through Run() builds it again
// rather than keeping a swap chain nobody can see.
inline void Window::EndPump() {
    caretTimer.Stop();
    frameTimer.Stop();
    fonts.Release();
    ReleaseDevice();
    if (dw) { dw->Release(); dw = nullptr; }
}

inline int Window::Run() {
    // The one-window application this shorthand is: see App for the rest. `solo` rather than `app`
    // because the window has a member of that name, which is where it goes when this returns.
    App solo;
    solo.Add(*this);
    return solo.Run();
}

// The loop, which is the loop it always was with three things changed. The frame decision is made
// for every window rather than for one, so a window that did not start the loop animates anyway. A
// message is dispatched inside *its own* window's guard. And the loop ends when the last window in
// the list is gone rather than when WM_DESTROY arrives from any of them.
inline int App::Run() {
    if (windows.empty()) return 0;
    running = true;
    for (Window *w : windows) w->BeginPump();
    const frameclock::Fn clock = frameclock::Resolve();
    // Only created where it is needed. Null on a pre-1803 build too, where the wait
    // falls back to Sleep for the same remainder.
    HANDLE pace = clock ? nullptr
                        : CreateWaitableTimerExW(nullptr, nullptr,
                                                 CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                                 TIMER_ALL_ACCESS);
    LARGE_INTEGER paceMark = {};
    LONGLONG period = 0;
    bool started = false;
    bool alive = true;
    MSG msg = {};
    int exitCode = 0;
    while (alive) {
        // A page's frame, one after another, and each window's own clock for it: the frequency is the
    // process's and is asked for once, here, rather than per window.
    LARGE_INTEGER freq = {};
    QueryPerformanceFrequency(&freq);
    const LONGLONG qpcFreq = freq.QuadPart;
    // A window nobody can see runs no frames. Something animating in it -- an indeterminate
        // progress bar is the one that never stops -- would otherwise paint the whole frame at the
        // display's rate into a surface nobody is looking at, and a minimised window is where that
        // is pure waste: there is not even a "later" for it, the frames are simply thrown away.
        //
        // Nothing is lost when it does run again: animOn is cleared here and the clock is picked up
        // when it comes back on, so an animation resumes where it was rather than jumping forward
        // by however long the window spent out of sight. See Visible.
        const bool moving = Moving();
        if (!moving) {
            if (GetMessageW(&msg, nullptr, 0, 0) <= 0) { exitCode = (int)msg.wParam; break; }
            TranslateMessage(&msg);
            PumpMessage(&msg);
            // The clocks are only picked up again here: a window that sat idle for a minute must not
            // hand the first frame a minute's worth of dt. Every window, because any of them can be
            // the next one to start moving.
            for (Window *w : windows) QueryPerformanceCounter(&w->qpcLast);
            // And the stretch is over, which is the rest of what `started` means: it is what asks
            // the monitor for its rate, and a rate asked for once per process is the rate of
            // whichever monitor the first animation happened to be on.
            started = false;
            continue;
        }
        if (!started) {
            started = true;
            QueryPerformanceCounter(&paceMark);
            // Asked once per stretch of animation rather than per frame: a window dragged to a
            // monitor with a different rate mid-animation is paced at the old rate for the rest of a
            // transition that lasts a fraction of a second. The slowest rate of the windows that are
            // moving is the one taken, because the wait below is one wait for all of them -- a window
            // on a faster display simply gets a couple of frames it did not need.
            if (!clock) {
                period = 0;
                for (Window *w : windows) {
                    if (!w->animOn) continue;
                    const LONGLONG rate = frameclock::RefreshPeriod(w->hwnd);
                    period = period == 0 ? rate : (std::min)(period, rate);
                }
            }
        }
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { exitCode = (int)msg.wParam; alive = false; break; }
            TranslateMessage(&msg);
            PumpMessage(&msg);
        }
        if (!alive) break;
        for (Window *w : windows) {
            if (!w->animOn) continue;
            { Window::Dispatch frame(w); w->Frame(); }
            w->Paint();
            // Painted outside WM_PAINT, so the update region has to be cleared by hand or the next
            // PeekMessage hands back a WM_PAINT for a window that was just drawn.
            ValidateRect(w->hwnd, nullptr);
        }
        if (clock) {
            clock(0, nullptr, 32);
        } else {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            const LONGLONG spent = (now.QuadPart - paceMark.QuadPart) * 10000000LL / qpcFreq;
            const LONGLONG left = period - spent;
            if (left > 0) {
                LARGE_INTEGER due;
                due.QuadPart = -left;                 // relative, 100 ns
                if (pace && SetWaitableTimer(pace, &due, 0, nullptr, nullptr, FALSE))
                    WaitForSingleObject(pace, 32);    // the same backstop as the clock's
                else
                    Sleep((DWORD)((left + 9999) / 10000));
            }
            QueryPerformanceCounter(&paceMark);
        }
    }
    // Usually the loop ends because the last window went, before the WM_QUIT that was posted for it
    // has been read -- and then `msg` is whatever was being dispatched, an Alt+F4 keystroke for one.
    // The exit code is the quit message's, so take it from the queue.
    MSG quit;
    if (PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) exitCode = (int)quit.wParam;
    // Whatever is left is going back to a page that is about to be its owner again, so it tidies up
    // after itself -- see EndPump. The waitable timer is the loop's own and goes here.
    for (Window *w : windows) w->EndPump();
    if (pace) CloseHandle(pace);
    return exitCode;
}

// ============================================================================================
// UI Automation
//
// Windows' accessibility API, and the reason a hand-drawn control is not automatically an
// invisible one. Nothing here runs for a program whose user is not running a screen reader: the
// window answers WM_GETOBJECT with a provider only while UiaClientsAreListening() says somebody
// is, and every call after that is a call that was asked for.
//
// **One element, whatever it is an element for.** `UiaElement` implements the fragment interfaces
// and the patterns a control can answer, and dispatches all of them to the `Widget` -- or to the
// `Window`, when the element is the root. There is no class per control, and that is not a
// shortcut: what a control *is* comes from six questions (`AccessibleName`, `AccessibleType`,
// `AccessibleToggle`, `AccessibleValue`, `AccessibleRange`, `AccessibleExpanded`) and what it
// *does* comes from `OnActivate`, which is Space. A control wired up for the keyboard is wired up
// for a screen reader by the same code.
//
// **A widget pointer is not an element.** Layout() rebuilds the page for all sorts of reasons, so
// by the time a client asks its next question the widget an element was made for is usually gone.
// An element therefore holds the window, a `uid` -- the number Window::Add gave that widget -- and
// which of that widget's items it is, and resolves them on every call. A widget that has been
// thrown away answers "no such widget", which is the truth, where a stale pointer would answer
// with whatever the page built in its place, which is a lie and a crash waiting to be one.
//
// **A thing a widget draws is not a widget, and is still a child.** The rows of a list, the cells
// of a segmented control and the items of a pane are drawn by one widget each -- forty options are
// not forty widgets, which is the whole reason a list is one control with its own layout -- and a
// client has to be able to read them one at a time: what a row says, which one is chosen, where it
// is. So a widget answers `AccessibleItems` and what one of them is, and the children of its
// element are those items rather than its child widgets. That is what WinUI's own list reports --
// one element per `ListViewItem` -- without the widgets under it.
//
// **Reading and writing, and the writing goes the way the page does.** A client can read what every
// control says, where everything is, and what is chosen -- and can change the ones that say they can
// be changed: a slider's value, a field's text, the choice in a list or a pane, whether a drop-down is
// open, where a page is scrolled to. Every write lands on the control's own path (`onChange`,
// `onSelect`, `Select`, `SetOpen`), so a page cannot tell a screen reader from a hand -- which is the
// whole of what makes it safe to have. What a control cannot do, it refuses: `IsReadOnly` answers for
// the control rather than for the pattern, and a write that was never going to be taken is answered
// rather than dropped.
//
// **A change is a comparison, not a notification.** Nothing in a widget knows it changed: a page
// changes a control through the control's own path or by writing a field, and the window finds out by
// looking once per frame it draws. The events a screen reader needs are raised out of that bookkeeping
// -- see `Window::UiaAnnounce` -- rather than out of anything a widget has to remember to call, which
// is the same reason the reading half asks the control instead of being pushed at.
// ============================================================================================

// The functions this needs out of UIAutomationCore.dll, looked up rather than linked -- and
// here that is not only a matter of taste, which is why it is done rather than argued about:
// MinGW-w64 ships no import library for UIAutomationCore at all, so a static call does not link
// on that toolchain, and the SDK's own library binds these by ordinal in an import table, which is
// a load-time promise about the exports of the DLL on the machine that runs the program. A
// library that can stop a process before wWinMain over an API it uses to describe itself to a
// screen reader has the wrong priorities -- the same reasoning as frameclock::Resolve, and here
// the cost of being wrong is that the window has no UIA at all and answers WM_GETOBJECT the way
// DefWindowProc would.
//
// By name first and by ordinal second. The DLL has both (62/80/94/95/102 as this was written), and
// the names are what the documentation is written in; the ordinal is there because an import
// library that went by ordinal rather than by name is exactly how this was first noticed.
namespace uiaapi {
inline FARPROC Lookup(const char *name, WORD ordinal) {
    static HMODULE module = LoadLibraryW(L"UIAutomationCore.dll");
    if (!module) return nullptr;
    FARPROC fn = GetProcAddress(module, name);
    return fn ? fn : GetProcAddress(module, (LPCSTR)(ULONG_PTR)ordinal);
}

struct Api {
    HRESULT(WINAPI *ReturnRawElementProvider)(HWND, WPARAM, LPARAM, IRawElementProviderSimple *);
    HRESULT(WINAPI *HostProviderFromHwnd)(HWND, IRawElementProviderSimple **);
    HRESULT(WINAPI *RaiseAutomationEvent)(IRawElementProviderSimple *, EVENTID);
    HRESULT(WINAPI *ClientsAreListening)();
    // A change, said rather than drawn, and a fifth function rather than part of `Ready()`: a machine
    // whose UIA can build this tree and hand it over can be read, which is worth having even if
    // nothing arrives when what was read changes. Every use of this one checks it first.
    HRESULT(WINAPI *RaisePropertyChanged)(IRawElementProviderSimple *, PROPERTYID, VARIANT, VARIANT);

    Api()
        : ReturnRawElementProvider((decltype(ReturnRawElementProvider))
              Lookup("UiaReturnRawElementProvider", 0x66)),
          HostProviderFromHwnd((decltype(HostProviderFromHwnd))
              Lookup("UiaHostProviderFromHwnd", 0x50)),
          RaiseAutomationEvent((decltype(RaiseAutomationEvent))
              Lookup("UiaRaiseAutomationEvent", 0x5E)),
          ClientsAreListening((decltype(ClientsAreListening))
              Lookup("UiaClientsAreListening", 0x3E)),
          RaisePropertyChanged((decltype(RaisePropertyChanged))
              Lookup("UiaRaiseAutomationPropertyChangedEvent", 0x5F)) {}

    // All four or none: a provider tree that can be built but not handed over is not worth the
    // three that did resolve.
    bool Ready() const {
        return ReturnRawElementProvider && HostProviderFromHwnd &&
               RaiseAutomationEvent && ClientsAreListening;
    }
};

inline const Api &Get() {
    static const Api api;
    return api;
}
}  // namespace uiaapi

// The number UIA wants for a direction a container does not scroll in: -1, which the managed API calls
// `ScrollPattern.NoScroll` and which the native headers have no name for at all. Not zero, which is "at
// the top of it": a client reading that would think the page was as far up as it goes.
constexpr double kUiaNoScroll = -1.0;

// One VARIANT per kind of value an announcement carries. `UiaVarNone` -- empty -- is the old value
// where there is nothing to compare against, which is what UIA documents for it.
inline VARIANT UiaVarNone() {
    VARIANT v;
    VariantInit(&v);
    return v;
}
inline VARIANT UiaVarText(const std::wstring &s) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(s.c_str());
    return v;
}
inline VARIANT UiaVarReal(double d) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_R8;
    v.dblVal = d;
    return v;
}
inline VARIANT UiaVarInt(int i) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = i;
    return v;
}
inline VARIANT UiaVarBool(bool b) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BOOL;
    v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
    return v;
}

struct UiaElement : IRawElementProviderSimple,
                    IRawElementProviderFragment,
                    IRawElementProviderFragmentRoot,
                    IInvokeProvider,
                    IToggleProvider,
                    IValueProvider,
                    IRangeValueProvider,
                    IExpandCollapseProvider,
                    IScrollProvider,
                    ISelectionProvider,
                    ISelectionItemProvider {
    // `uid` 0 is the window itself, which is the root of the tree; `item` is which of that widget's
    // items this element is, or -1 for the widget itself. See `Widget::AccessibleItems`.
    UiaElement(Window *w, int uid, int item = -1) : win(w), widget(uid), item(item) {}

    // ---- IUnknown -------------------------------------------------------------------------
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IRawElementProviderSimple))
            *out = static_cast<IRawElementProviderSimple *>(this);
        else if (iid == __uuidof(IRawElementProviderFragment))
            *out = static_cast<IRawElementProviderFragment *>(this);
        else if (iid == __uuidof(IRawElementProviderFragmentRoot))
            *out = static_cast<IRawElementProviderFragmentRoot *>(this);
        else if (iid == __uuidof(IInvokeProvider))
            *out = static_cast<IInvokeProvider *>(this);
        else if (iid == __uuidof(IToggleProvider))
            *out = static_cast<IToggleProvider *>(this);
        else if (iid == __uuidof(IValueProvider))
            *out = static_cast<IValueProvider *>(this);
        else if (iid == __uuidof(IRangeValueProvider))
            *out = static_cast<IRangeValueProvider *>(this);
        else if (iid == __uuidof(IExpandCollapseProvider))
            *out = static_cast<IExpandCollapseProvider *>(this);
        else if (iid == __uuidof(IScrollProvider))
            *out = static_cast<IScrollProvider *>(this);
        else if (iid == __uuidof(ISelectionProvider))
            *out = static_cast<ISelectionProvider *>(this);
        else if (iid == __uuidof(ISelectionItemProvider))
            *out = static_cast<ISelectionItemProvider *>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG left = InterlockedDecrement(&refs);
        if (left == 0) delete this;
        return (ULONG)left;
    }

    // ---- IRawElementProviderSimple ----------------------------------------------------------
    // ServerSideProvider, and *not* UseComThreading. That flag is a promise that the provider may
    // be called from any thread, and this one may not: it walks the widget list and calls a
    // control's action, and a control belongs to the thread that made its window. Without the
    // flag, UIA marshals every call to that thread, which is where the widgets are.
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions *out) override {
        if (!out) return E_INVALIDARG;
        *out = ProviderOptions_ServerSideProvider;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown **out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        Widget *w = Target();
        if (!w) return S_OK;
        if (IsItem()) {
            // An item is a row of something: the one pattern it can have is being the chosen one.
            if (id == UIA_SelectionItemPatternId && SelectableItem())
                *out = static_cast<ISelectionItemProvider *>(this);
        } else if (id == UIA_InvokePatternId && w->AccessibleActionable()) {
            *out = static_cast<IInvokeProvider *>(this);
        } else if (id == UIA_TogglePatternId && w->AccessibleToggle() >= 0) {
            *out = static_cast<IToggleProvider *>(this);
        } else if (id == UIA_ValuePatternId && HasText(*w)) {
            *out = static_cast<IValueProvider *>(this);
        } else if (id == UIA_RangeValuePatternId && HasRange(*w)) {
            *out = static_cast<IRangeValueProvider *>(this);
        } else if (id == UIA_ExpandCollapsePatternId && w->AccessibleExpanded() >= 0) {
            *out = static_cast<IExpandCollapseProvider *>(this);
        } else if (id == UIA_ScrollPatternId && CanScroll(*w)) {
            *out = static_cast<IScrollProvider *>(this);
        } else if (id == UIA_SelectionPatternId && w->AccessibleItems() > 0) {
            *out = static_cast<ISelectionProvider *>(this);
        }
        if (*out) AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT *out) override {
        if (!out) return E_INVALIDARG;
        VariantInit(out);
        UiaRect box = {};
        get_BoundingRectangle(&box);
        Widget *w = Target();
        if (!w) {
            // The window itself: enough to place the tree inside a screen, and no more than that.
            switch (id) {
            case UIA_ControlTypePropertyId:   return SmallInt(out, UIA_WindowControlTypeId);
            case UIA_NamePropertyId:          return Text(out, win->Title());
            case UIA_IsEnabledPropertyId:     return Flag(out, true);
            case UIA_FrameworkIdPropertyId:   return Text(out, L"Micula");
            case UIA_BoundingRectanglePropertyId: return BoxOf(out, box);
            case UIA_NativeWindowHandlePropertyId:
                return SmallInt(out, (int)(INT_PTR)win->hwnd);
            }
            return S_OK;
        }
        Widget::Item it;
        const bool isItem = Item(&it);
        switch (id) {
        case UIA_ControlTypePropertyId:
            return SmallInt(out, isItem ? it.type : w->AccessibleType());
        case UIA_NamePropertyId:
            return Text(out, isItem ? it.name : w->AccessibleLabel());
        case UIA_HelpTextPropertyId:                 return Text(out, w->tips.c_str());
        case UIA_IsEnabledPropertyId:                return Flag(out, w->enabled);
        // **A control nothing can show is off screen**, and so is an item a list has carried out of its
        // panel: what is not drawn is not there as far as a client is concerned, and a screen reader
        // that read out the rows a scroll had taken away from the panel would be reading the wrong list.
        // See `VisibleArea`.
        case UIA_IsOffscreenPropertyId:
            return Flag(out, !w->visible || (isItem ? !it.onscreen : BoxIsEmpty(VisibleBox(*w))));
        case UIA_IsKeyboardFocusablePropertyId:      return Flag(out, !isItem && w->Focusable());
        case UIA_HasKeyboardFocusPropertyId:         return Flag(out, !isItem && win->focused == w);
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:         return Flag(out, true);
        case UIA_FrameworkIdPropertyId:              return Text(out, L"Micula");
        case UIA_BoundingRectanglePropertyId:        return BoxOf(out, box);
        // Where an item sits in the set it belongs to, which is what a screen reader says "3 of 40"
        // with. The place, not the page's own index: an item is usually the only one that has both.
        case UIA_PositionInSetPropertyId:            return isItem ? SmallInt(out, item + 1) : S_OK;
        case UIA_SizeOfSetPropertyId:
            return isItem ? SmallInt(out, w->AccessibleItems()) : S_OK;
        case UIA_IsInvokePatternAvailablePropertyId: return Flag(out, !isItem && w->AccessibleActionable());
        case UIA_IsTogglePatternAvailablePropertyId:
            return Flag(out, !isItem && w->AccessibleToggle() >= 0);
        case UIA_IsValuePatternAvailablePropertyId:  return Flag(out, !isItem && HasText(*w));
        case UIA_IsRangeValuePatternAvailablePropertyId:
            return Flag(out, !isItem && HasRange(*w));
        case UIA_IsScrollPatternAvailablePropertyId: return Flag(out, !isItem && CanScroll(*w));
        case UIA_IsExpandCollapsePatternAvailablePropertyId:
            return Flag(out, !isItem && w->AccessibleExpanded() >= 0);
        case UIA_IsSelectionPatternAvailablePropertyId:
            return Flag(out, !isItem && w->AccessibleItems() > 0);
        case UIA_IsSelectionItemPatternAvailablePropertyId:
            return Flag(out, isItem && it.index >= 0);
        case UIA_SelectionItemIsSelectedPropertyId:  return Flag(out, isItem && it.selected);
        case UIA_ExpandCollapseExpandCollapseStatePropertyId:
            return SmallInt(out, w->AccessibleExpanded() == 1 ? ExpandCollapseState_Expanded
                                                              : ExpandCollapseState_Collapsed);
        // The property and the pattern's own `get_IsReadOnly` are one answer: a client that asks
        // whether it may write gets the same word either way. An item has no value to write.
        case UIA_ValueIsReadOnlyPropertyId:          return Flag(out, isItem || !w->AccessibleWritable());
        case UIA_ValueValuePropertyId: {
            std::wstring v;
            return HasText(*w) && w->AccessibleValue(v) ? Text(out, v.c_str()) : S_OK;
        }
        default:
            break;
        }
        return S_OK;   // VT_EMPTY: a property this control has no answer for
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple **out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        // Only the root has a host: the HWND's own provider, which is what this process's window
        // class would otherwise have answered with. It is how a client gets from this tree to the
        // native window's own properties.
        if (widget) return S_OK;
        const uiaapi::Api &uia = uiaapi::Get();
        if (!uia.Ready()) return S_OK;
        return uia.HostProviderFromHwnd(win->hwnd, out);
    }

    // ---- IRawElementProviderFragment --------------------------------------------------------
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection dir,
                                       IRawElementProviderFragment **out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        // An item: the widget it is an item of is its parent, and the items beside it are its siblings.
        // It has no children of its own -- an item is one thing to read, and the widget that draws it
        // is what a client asks about what is inside it.
        if (item >= 0) {
            Widget *w = Target();
            if (!w) return S_OK;
            const int n = w->AccessibleItems();
            if (dir == NavigateDirection_Parent) *out = new UiaElement(win, widget, -1);
            else if (dir == NavigateDirection_NextSibling && item + 1 < n)
                *out = new UiaElement(win, widget, item + 1);
            else if (dir == NavigateDirection_PreviousSibling && item > 0)
                *out = new UiaElement(win, widget, item - 1);
            return S_OK;
        }
        // First and last child, from any element: what the children *are* is the host's business --
        // items for a widget that draws a set of them, child widgets for one that does not.
        Widget *host = Host();
        const int count = VisibleChildren(host);
        if (dir == NavigateDirection_FirstChild && count > 0) {
            *out = ChildAt(host, 0);
            return S_OK;
        }
        if (dir == NavigateDirection_LastChild && count > 0) {
            *out = ChildAt(host, count - 1);
            return S_OK;
        }
        // No parent above the window: the HWND is the root's host, not its parent.
        if (!widget) return S_OK;
        if (dir == NavigateDirection_Parent) {
            // The parent widget, and the window for one whose parent is the root widget -- which is not
            // an element of its own, because the window already is one.
            Widget *p = Target();
            Widget *up = p ? p->parent : nullptr;
            *out = new UiaElement(win, (up && up != win->content.get()) ? up->uid : 0);
            return S_OK;
        }
        // A sibling is one of the *parent's* children, so the set to count and walk is the parent's.
        Widget *p = Target();
        Widget *parent = p ? p->parent : nullptr;
        const int at = SiblingIndex(p);
        const int among = VisibleChildren(parent ? parent : win->content.get());
        if (at < 0) return S_OK;
        if (dir == NavigateDirection_NextSibling && at + 1 < among)
            *out = ChildAt(parent ? parent : win->content.get(), at + 1);
        else if (dir == NavigateDirection_PreviousSibling && at > 0)
            *out = ChildAt(parent ? parent : win->content.get(), at - 1);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY **out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        // Two numbers and no pointer. A runtime id has to be unique within the window, which the
        // uid Add handed out already is, and a pointer would change identity every time the page
        // was laid out again -- which is the one thing UIA uses the id to notice.
        SAFEARRAY *a = SafeArrayCreateVector(VT_I4, 0, 2);
        if (!a) return E_OUTOFMEMORY;
        LONG i = 0;
        // 3, which the documentation calls UIA_AppendRuntimeId. It has a name in the UIA
        // programmer's guide and not in a header, so it is a comment here instead of a constant.
        int v = 3;
        SafeArrayPutElement(a, &i, &v);
        i = 1;
        v = widget;
        SafeArrayPutElement(a, &i, &v);
        *out = a;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect *out) override {
        if (!out) return E_INVALIDARG;
        *out = {};
        const double s = win->scale();
        POINT origin = { 0, 0 };
        ClientToScreen(win->hwnd, &origin);
        if (!widget) {
            *out = { (double)origin.x, (double)origin.y,
                     (double)win->ClientW() * s, (double)win->ClientH() * s };
            return S_OK;
        }
        Widget *w = Target();
        if (!w) return S_OK;
        // Where the widget is *drawn*, in the client area, and what is left of it: the accumulated
        // origins of its ancestors plus its own drawn rectangle, cut by every container above it that
        // clips. A widget on its way somewhere is reported where it looks, and one a scroll has taken
        // out of the container showing it is reported with the sliver that is still there -- which is
        // nothing, which is an empty rectangle, which is what `IsOffscreen` says about it too.
        Widget::Item it;
        const D2D1_RECT_F r = Item(&it) ? (it.onscreen ? Clipped(*w, it.box) : D2D1_RECT_F{ 0, 0, 0, 0 })
                                        : VisibleBox(*w);
        *out = { origin.x + (double)r.left * s, origin.y + (double)r.top * s,
                 (double)(r.right - r.left) * s, (double)(r.bottom - r.top) * s };
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY **out) override {
        if (out) *out = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetFocus() override {
        Widget *w = Target();
        if (!w || !w->Focusable()) return S_OK;
        win->showFocusRing = true;
        win->SetFocusTo(w);              // the one place the focus event is raised from
        win->Invalidate();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot **out) override {
        if (!out) return E_INVALIDARG;
        *out = static_cast<IRawElementProviderFragmentRoot *>(new UiaElement(win, 0));
        return S_OK;
    }

    // ---- IRawElementProviderFragmentRoot ----------------------------------------------------
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y,
                                                       IRawElementProviderFragment **out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        // What makes "hover and identify" work, and it asks the window's own hit test rather than
        // forming a second opinion about where a control is: the point is in screen pixels and the
        // page is in DIPs, and the rest is the question a click asks.
        const double s = win->scale();
        POINT origin = { 0, 0 };
        ClientToScreen(win->hwnd, &origin);
        Widget *w = win->HitTest((float)((x - origin.x) / s), (float)((y - origin.y) / s));
        if (w) *out = new UiaElement(win, w->uid);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment **out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        if (win->focused) *out = new UiaElement(win, win->focused->uid);
        return S_OK;
    }

    // ---- what a client can ask a control to do ----------------------------------------------
    // Both are the keyboard's own action: `OnActivate` is Space, which presses a button and
    // switches a switch. A disabled control still answers and says it cannot, which is what a
    // client draws a greyed item from -- silence would look like a control without the pattern.
    HRESULT STDMETHODCALLTYPE Invoke() override {
        Widget *w = Target();
        if (!w || !w->AccessibleActionable()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!w->enabled) return UIA_E_ELEMENTNOTENABLED;
        w->OnActivate();
        win->Invalidate();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Toggle() override {
        Widget *w = Target();
        if (!w || w->AccessibleToggle() < 0) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!w->enabled) return UIA_E_ELEMENTNOTENABLED;
        w->OnActivate();
        win->Invalidate();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState *out) override {
        if (!out) return E_INVALIDARG;
        Widget *w = Target();
        const int state = w ? w->AccessibleToggle() : -1;
        if (state < 0) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = state == 1 ? ToggleState_On
             : state == 2 ? ToggleState_Indeterminate
                          : ToggleState_Off;
        return S_OK;
    }

    // ---- writing a control's value -----------------------------------------------------------
    // **Each of these is the page's own path and not a way around it**: the value goes in through the
    // same callback a gesture would have run, so a page cannot tell a screen reader from a hand. That
    // is the whole of what makes writing safe -- there is no second kind of change to get wrong -- and
    // it is why the refusals are refusals rather than silence: a client that is told no can offer the
    // control as read-only, and one that is told nothing can only guess.
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override {
        if (!value) return E_INVALIDARG;
        Widget *w = Target();
        if (!w || IsItem()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!w->enabled) return UIA_E_ELEMENTNOTENABLED;
        if (!w->AccessibleWritable()) return UIA_E_NOTSUPPORTED;
        if (!w->AccessibleSetValue(value)) return UIA_E_INVALIDOPERATION;
        win->Invalidate();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Value(BSTR *out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        Widget *w = Target();
        std::wstring v;
        if (!w || !w->AccessibleValue(v)) return S_OK;
        *out = SysAllocString(v.c_str());
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    // **Read-only is the control's answer and not the pattern's**: a value that only a pointer can move
    // is a value a client reads and does not write, and a slider is the other thing. False here means
    // the write above it will be taken, which is what a client decides what to offer from.
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL *out) override {
        if (!out) return E_INVALIDARG;
        Widget *w = Target();
        *out = (w && w->AccessibleWritable()) ? FALSE : TRUE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetValue(double value) override {
        Widget *w = Target();
        if (!w || IsItem()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!w->enabled) return UIA_E_ELEMENTNOTENABLED;
        if (!w->AccessibleWritable()) return UIA_E_NOTSUPPORTED;
        if (!w->AccessibleSetRange((float)value)) return UIA_E_INVALIDOPERATION;
        win->Invalidate();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Value(double *out) override {
        if (!out) return E_INVALIDARG;
        float v = 0.0f, lo = 0.0f, hi = 0.0f, step = 0.0f;
        Widget *w = Target();
        if (!w || !w->AccessibleRange(v, lo, hi, step)) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = (double)v;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Minimum(double *out) override {
        if (!out) return E_INVALIDARG;
        float v = 0.0f, lo = 0.0f, hi = 0.0f, step = 0.0f;
        Widget *w = Target();
        if (!w || !w->AccessibleRange(v, lo, hi, step)) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = (double)lo;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Maximum(double *out) override {
        if (!out) return E_INVALIDARG;
        float v = 0.0f, lo = 0.0f, hi = 0.0f, step = 0.0f;
        Widget *w = Target();
        if (!w || !w->AccessibleRange(v, lo, hi, step)) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = (double)hi;
        return S_OK;
    }
    // A small change is one step of the control's own -- a slider's arrow key -- and a large one is
    // what a page of the same control would move, which for a slider with a step is five of them.
    HRESULT STDMETHODCALLTYPE get_SmallChange(double *out) override {
        if (!out) return E_INVALIDARG;
        float v = 0.0f, lo = 0.0f, hi = 0.0f, step = 0.0f;
        Widget *w = Target();
        if (!w || !w->AccessibleRange(v, lo, hi, step)) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = (double)step;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_LargeChange(double *out) override {
        if (!out) return E_INVALIDARG;
        float v = 0.0f, lo = 0.0f, hi = 0.0f, step = 0.0f;
        Widget *w = Target();
        if (!w || !w->AccessibleRange(v, lo, hi, step)) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = (double)(step * 5.0f);
        return S_OK;
    }

    // ---- a control that opens and closes ------------------------------------------------------
    // A drop-down's list and a navigation pane are the two: what a client reads is whether it is open,
    // which is what says whether the rows inside it are the page or a thing that has to be opened.
    HRESULT STDMETHODCALLTYPE Expand() override { return SetExpanded(true); }
    HRESULT STDMETHODCALLTYPE Collapse() override { return SetExpanded(false); }
    HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(ExpandCollapseState *out) override {
        if (!out) return E_INVALIDARG;
        Widget *w = Target();
        const int state = w ? w->AccessibleExpanded() : -1;
        if (state < 0) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = state == 1 ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
        return S_OK;
    }

    // ---- a container that scrolls -------------------------------------------------------------
    // Read-out of where the view is and how much of the content it is showing: a screen reader says
    // "half way down" from the percent and decides whether there is more to say at all from the view
    // size. A page scrolls one way, so the horizontal numbers are the "no scrolling" ones.
    HRESULT STDMETHODCALLTYPE Scroll(ScrollAmount horizontal, ScrollAmount vertical) override {
        Widget *w = Target();
        if (!w || IsItem() || !w->AccessibleWritable()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!w->enabled) return UIA_E_ELEMENTNOTENABLED;
        // One way only, which is the axis a page has: a client that asks to move sideways is asking
        // for something this container does not do.
        if (horizontal != ScrollAmount_NoAmount) return UIA_E_NOTSUPPORTED;
        float at = 0.0f, view = 1.0f;
        bool can = false;
        if (!w->AccessibleScroll(at, view, can) || !can) return UIA_E_NOTSUPPORTED;
        // A large amount is a page of the same size as the one being read, and a small one is a
        // couple of lines of it -- which is what a wheel notch is, near enough, and fine enough.
        const float page = std::clamp(view, 0.05f, 1.0f);
        float delta = 0.0f;
        switch (vertical) {
        case ScrollAmount_SmallIncrement: delta = 0.05f; break;
        case ScrollAmount_LargeIncrement: delta = page; break;
        case ScrollAmount_NoAmount:       delta = 0.0f; break;
        case ScrollAmount_LargeDecrement: delta = -page; break;
        case ScrollAmount_SmallDecrement: delta = -0.05f; break;
        default: return E_INVALIDARG;
        }
        if (!w->AccessibleSetScroll(std::clamp(at + delta, 0.0f, 1.0f))) return UIA_E_NOTSUPPORTED;
        win->Invalidate();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetScrollPercent(double horizontal, double vertical) override {
        Widget *w = Target();
        if (!w || IsItem() || !w->AccessibleWritable()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!w->enabled) return UIA_E_ELEMENTNOTENABLED;
        // The axis this container does not scroll in: a client passes -1 for it, or 100 for "leave
        // that one where it is", and either is an answer rather than an error.
        if (horizontal != kUiaNoScroll && horizontal != 100.0) return UIA_E_NOTSUPPORTED;
        if (vertical == kUiaNoScroll) return S_OK;
        if (vertical < 0.0 || vertical > 100.0) return E_INVALIDARG;
        if (!w->AccessibleSetScroll((float)(vertical / 100.0))) return UIA_E_NOTSUPPORTED;
        win->Invalidate();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HorizontalScrollPercent(double *out) override {
        if (!out) return E_INVALIDARG;
        *out = kUiaNoScroll;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_VerticalScrollPercent(double *out) override {
        if (!out) return E_INVALIDARG;
        float at = 0.0f, view = 1.0f;
        bool can = false;
        Widget *w = Target();
        if (!w || !w->AccessibleScroll(at, view, can) || !can) {
            *out = kUiaNoScroll;
            return S_OK;
        }
        *out = (double)at * 100.0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HorizontalViewSize(double *out) override {
        if (!out) return E_INVALIDARG;
        *out = 100.0;                       // all of it: nothing is off to the side
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_VerticalViewSize(double *out) override {
        if (!out) return E_INVALIDARG;
        *out = 100.0;
        float at = 0.0f, view = 1.0f;
        bool can = false;
        Widget *w = Target();
        if (w && w->AccessibleScroll(at, view, can)) *out = (double)view * 100.0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HorizontallyScrollable(BOOL *out) override {
        if (!out) return E_INVALIDARG;
        *out = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_VerticallyScrollable(BOOL *out) override {
        if (!out) return E_INVALIDARG;
        *out = FALSE;
        float at = 0.0f, view = 1.0f;
        bool can = false;
        Widget *w = Target();
        if (w && w->AccessibleScroll(at, view, can) && can) *out = TRUE;
        return S_OK;
    }

    // ---- which of a set is chosen -------------------------------------------------------------
    // The set is the widget and the chosen thing is one of its items: one choice out of the rows of a
    // list, the cells of a segmented control, the items of a pane. Choosing is `Select`, which goes
    // through the control's own path, and `RemoveFromSelection` has nothing to do -- see below.
    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY **out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        Widget *w = Target();
        if (!w) return UIA_E_ELEMENTNOTAVAILABLE;
        std::vector<int> chosen;
        Widget::Item it;
        for (int i = 0; i < w->AccessibleItems(); i++)
            if (w->AccessibleItem(i, it) && it.selected) chosen.push_back(i);
        SAFEARRAY *a = SafeArrayCreateVector(VT_UNKNOWN, 0, (ULONG)chosen.size());
        if (!a) return E_OUTOFMEMORY;
        for (size_t k = 0; k < chosen.size(); k++) {
            IRawElementProviderSimple *p = new UiaElement(win, widget, chosen[k]);
            LONG at = (LONG)k;
            if (FAILED(SafeArrayPutElement(a, &at, p))) {   // the array holds its own reference
                p->Release();
                SafeArrayDestroy(a);
                return E_UNEXPECTED;
            }
            p->Release();
        }
        *out = a;
        return S_OK;
    }
    // One choice, and the set is never empty: a drop-down always names something and a pane is always
    // on something. A client that offers a "clear" would be offering a state the control cannot be in.
    HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL *out) override {
        if (!out) return E_INVALIDARG;
        *out = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL *out) override {
        if (!out) return E_INVALIDARG;
        *out = TRUE;
        return S_OK;
    }

    // **Choosing a row from outside**, through the control's own path: the index is the page's own and
    // not the item's place in the sequence, which is what `Item::index` carries for exactly this.
    HRESULT STDMETHODCALLTYPE Select() override {
        Widget *w = Target();
        Widget::Item it;
        if (!w || !Item(&it)) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!w->enabled) return UIA_E_ELEMENTNOTENABLED;
        if (it.index < 0) return UIA_E_NOTSUPPORTED;   // an item that is not a place a choice can be
        if (!w->AccessibleSelect(it.index)) return UIA_E_NOTSUPPORTED;
        win->Invalidate();
        return S_OK;
    }
    // The same thing here, because there is one of them: this control's set always has a choice in it
    // and cannot have two. See `get_CanSelectMultiple`.
    HRESULT STDMETHODCALLTYPE AddToSelection() override { return Select(); }
    // **Nothing can be taken out of the set.** `IsSelectionRequired` is true, so a client asking for
    // the choice to be cleared is asking for a state this control cannot be in -- which is answered
    // rather than ignored, because a client that gets an answer can stop offering it.
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override { return UIA_E_NOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL *out) override {
        if (!out) return E_INVALIDARG;
        Widget::Item it;
        if (!Item(&it)) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = it.selected ? TRUE : FALSE;
        return S_OK;
    }
    // The widget the item belongs to, which is what a client asks when it wants to know what the
    // choice is one of.
    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple **out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        if (!Target()) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = static_cast<IRawElementProviderSimple *>(new UiaElement(win, widget, -1));
        return S_OK;
    }

private:
    // The widget this element is for, resolved now: null for the root, and null for a widget the
    // page has since laid out again -- which every caller has to expect and answer for.
    Widget *Target() const { return widget ? win->UiaFind(widget) : nullptr; }
    // This element's item, when it is one. False for the widget's own element, and false for an item
    // the list no longer has -- which is the same "no such element" the widget case gets out of
    // `Target`, and is why every caller has to ask rather than hold.
    bool Item(Widget::Item *out = nullptr) const {
        Widget *w = Target();
        if (!w || item < 0 || item >= w->AccessibleItems()) return false;
        Widget::Item it;
        if (!w->AccessibleItem(item, it)) return false;
        if (out) *out = it;
        return true;
    }
    bool IsItem() const {
        Widget::Item it;
        return Item(&it);
    }
    // An item a client can *be on* rather than only read: a pane's headings are items too, and are not
    // somewhere a choice can be. The widget says which by giving one an index and the other none.
    bool SelectableItem() const {
        Widget::Item it;
        return Item(&it) && it.index >= 0;
    }
    static bool HasText(const Widget &w) {
        std::wstring v;
        return w.AccessibleValue(v);
    }
    static bool HasRange(const Widget &w) {
        float v = 0.0f, lo = 0.0f, hi = 0.0f, step = 0.0f;
        return w.AccessibleRange(v, lo, hi, step);
    }
    static bool CanScroll(const Widget &w) {
        float at = 0.0f, view = 0.0f;
        bool can = false;
        return w.AccessibleScroll(at, view, can) && can;
    }
    static bool BoxIsEmpty(const D2D1_RECT_F &r) { return r.right <= r.left || r.bottom <= r.top; }
    // Both of a control that opens and closes, which is one write with two names.
    HRESULT SetExpanded(bool open) {
        Widget *w = Target();
        if (!w || IsItem() || w->AccessibleExpanded() < 0) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!w->enabled) return UIA_E_ELEMENTNOTENABLED;
        if (!w->AccessibleSetExpanded(open)) return UIA_E_NOTSUPPORTED;
        win->Invalidate();
        return S_OK;
    }
    // **What is really on screen, in the client area and in DIPs**: where the widget is drawn, cut by
    // the containers above it that clip. A widget a scroll has taken out of the container showing it
    // comes back empty, which is what `IsOffscreen` is, and what keeps a client from drawing a
    // highlight around a row nobody can see. `VisibleArea` answers with all of them together.
    D2D1_RECT_F VisibleBox(const Widget &w) const {
        return Cut(win->OriginOf(&w), w.VisibleArea(), w.placed ? w.drawn : w.rect);
    }
    // The same for a box inside the widget -- an item's row -- which is written in the space the
    // widget's `rect` is in, and is a whole item only for as long as the container shows all of it.
    D2D1_RECT_F Clipped(const Widget &w, const D2D1_RECT_F &box) const {
        return Cut(win->OriginOf(&w), w.VisibleArea(), box);
    }
    static D2D1_RECT_F Cut(const D2D1_POINT_2F &origin, const D2D1_RECT_F &seen,
                           const D2D1_RECT_F &box) {
        const float l = (std::max)(box.left, seen.left), t = (std::max)(box.top, seen.top);
        const float r = (std::max)(l, (std::min)(box.right, seen.right));
        const float b = (std::max)(t, (std::min)(box.bottom, seen.bottom));
        return { origin.x + l, origin.y + t, origin.x + r, origin.y + b };
    }
    // The widget whose children this element reports: the widget the uid names, and for the window
    // itself the root widget -- whose box is the client area, and which is not an element of its own
    // because the window already is one.
    Widget *Host() const { return widget ? Target() : win->content.get(); }
    // How many children the host has: the items for a widget that is made of them, and the visible
    // child widgets for one that is not. An element that *is* an item has none either way: an item is
    // one thing to read, and what is inside it is the widget's business.
    int VisibleChildren(const Widget *host) const {
        if (!host) return 0;
        const int items = host->AccessibleItems();
        if (items > 0) return items;
        if (host == Target() && item >= 0) return 0;
        int n = 0;
        for (const auto &child : host->children)
            if (child->visible) n++;
        return n;
    }
    UiaElement *ChildAt(const Widget *host, int index) const {
        if (!host || index < 0) return nullptr;
        const int items = host->AccessibleItems();
        if (items > 0) return index < items ? new UiaElement(win, host->uid, index) : nullptr;
        if (host == Target() && item >= 0) return nullptr;
        for (const auto &child : host->children)
            if (child->visible && index-- == 0) return new UiaElement(win, child->uid);
        return nullptr;
    }
    // Where a widget sits among the children its parent reports, which is the order a client visits
    // them in -- and what is not drawn is not there as far as a client is concerned, so an invisible
    // child is not counted. -1 for a widget that is not among them: the child widgets of a parent that
    // is made of items are not in the tree at all.
    static int SiblingIndex(const Widget *w) {
        if (!w || !w->parent || w->parent->AccessibleItems() > 0) return -1;
        int n = 0;
        for (const auto &child : w->parent->children) {
            if (!child->visible) continue;
            if (child.get() == w) return n;
            n++;
        }
        return -1;
    }

    // A property, four ways, into a VARIANT the caller has already initialised. One that is not
    // answered stays VT_EMPTY, which is UIA's own way of saying "no value here".
    static HRESULT Text(VARIANT *out, const wchar_t *s) {
        if (!s || !*s) return S_OK;
        out->vt = VT_BSTR;
        out->bstrVal = SysAllocString(s);
        return out->bstrVal ? S_OK : E_OUTOFMEMORY;
    }
    static HRESULT SmallInt(VARIANT *out, int v) {
        out->vt = VT_I4;
        out->lVal = v;
        return S_OK;
    }
    static HRESULT Flag(VARIANT *out, bool v) {
        out->vt = VT_BOOL;
        out->boolVal = v ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
    }
    static HRESULT BoxOf(VARIANT *out, const UiaRect &r) {
        SAFEARRAY *a = SafeArrayCreateVector(VT_R8, 0, 4);
        if (!a) return E_OUTOFMEMORY;
        const double v[4] = { r.left, r.top, r.width, r.height };
        for (LONG i = 0; i < 4; i++)
            if (FAILED(SafeArrayPutElement(a, &i, (void *)&v[i]))) {
                SafeArrayDestroy(a);
                return E_UNEXPECTED;
            }
        out->vt = VT_ARRAY | VT_R8;
        out->parray = a;
        return S_OK;
    }

    Window *win;
    int widget;          // 0 for the window itself; otherwise a Widget::uid
    int item = -1;       // which of that widget's items, or -1 for the widget itself
    LONG refs = 1;
};

inline IRawElementProviderSimple *Window::UiaRoot() { return new UiaElement(this, 0); }

// The widget behind an element's uid, or null. A linear scan of a list that is a page's worth of
// controls, on a call that only happens when somebody is running a screen reader.
inline Widget *Window::UiaFind(int uid) const {
    if (!uid) return nullptr;
    return FindUid(content.get(), uid);
}

inline Widget *Window::FindUid(Widget *w, int uid) const {
    if (!w) return nullptr;
    if (w->uid == uid) return w;
    for (const auto &child : w->children)
        if (Widget *hit = FindUid(child.get(), uid)) return hit;
    return nullptr;
}

// Told to a client that is listening that the keyboard focus moved: the one event a screen reader
// cannot work without, because it is how it follows the Tab key.
inline void Window::UiaFocusChanged() {
    const uiaapi::Api &uia = uiaapi::Get();
    if (!uia.Ready() || !uia.ClientsAreListening()) return;
    UiaElement *e = new UiaElement(this, focused ? focused->uid : 0);
    uia.RaiseAutomationEvent(e, UIA_AutomationFocusChangedEventId);
    e->Release();
}

// **Everything else a client has to be told, told by comparison.** A screen reader that has just read
// a row needs to hear about the next one, and there is nowhere in a widget that knows it changed: a
// page changes a control through the control's own path or by writing a field, and either way this is
// the only place that can see both sides of it. So the window looks -- at the handful of things a
// change *is* rather than at everything a client can ask for, since everything else it can ask the
// control itself -- once per frame it draws. The table it compares against is the frame before, which
// makes this a report of what happened rather than a promise about what will.
//
// **The table is kept whether or not anybody is listening**, which is the whole of why this works: a
// client attaches between two frames, and if the table were built on the frame after that, the change
// that made the client attach in the first place would be the one thing it never heard about -- the
// table would have been filled with the answer instead of the question. Keeping it always costs a walk
// of one page's controls on the frames a window is moving anyway, and buys the two things that matter:
// nothing is said when a client arrives (what it can see has not changed since it looked), and a
// change made while nobody was connected is answered by the state rather than by silence.
inline void Window::UiaAnnounce() {
    const uiaapi::Api &uia = uiaapi::Get();
    if (!uia.Ready() || !content) return;

    // One pass, and the only pass: what a control says is asked of the control, so there is nothing
    // kept in step and nothing a page can forget to tell.
    uiaNow.clear();
    std::function<void(Widget *)> look = [&](Widget *w) {
        UiaSeen &s = uiaNow[w->uid];
        std::wstring text;
        if (w->AccessibleValue(text)) {
            s.hasValue = true;
            s.value = std::move(text);
        } else {
            s.hasValue = false;
            s.value.clear();
        }
        float lo = 0.0f, hi = 0.0f, step = 0.0f;
        s.hasRange = w->AccessibleRange(s.range, lo, hi, step);
        s.expanded = w->AccessibleExpanded();
        float view = 0.0f;
        bool can = false;
        s.hasScroll = w->AccessibleScroll(s.scroll, view, can);
        s.scroll *= 100.0f;
        const int items = w->AccessibleItems();
        s.chosen.resize((size_t)items);
        for (int i = 0; i < items; i++) {
            Widget::Item it;
            s.chosen[(size_t)i] = (w->AccessibleItem(i, it) && it.selected) ? 1 : 0;
        }
        for (const auto &child : w->children)
            if (child->visible) look(child.get());
    };
    look(content.get());

    // The old value and the new one are handed over as they are made, and cleared here: a VARIANT
    // passed by value is the same string, so exactly one side of each of these frees it.
    const bool listening = uia.ClientsAreListening();
    auto tell = [&](int uid, int item, PROPERTYID id, VARIANT was, VARIANT now) {
        if (!listening || !uia.RaisePropertyChanged) return;
        UiaElement *e = new UiaElement(this, uid, item);
        uia.RaisePropertyChanged(e, id, was, now);
        e->Release();
    };
    auto shout = [&](int uid, int item, EVENTID id) {
        if (!listening) return;
        UiaElement *e = new UiaElement(this, uid, item);
        uia.RaiseAutomationEvent(e, id);
        e->Release();
    };

    for (const auto &kv : uiaNow) {
        auto before = uiaSeen.find(kv.first);
        if (before == uiaSeen.end()) continue;      // nobody has been told this one exists yet
        const UiaSeen &a = before->second, &b = kv.second;
        const int uid = kv.first;
        if (a.hasValue != b.hasValue || a.value != b.value) {
            VARIANT was = a.hasValue ? UiaVarText(a.value) : UiaVarNone();
            VARIANT now = b.hasValue ? UiaVarText(b.value) : UiaVarNone();
            tell(uid, -1, UIA_ValueValuePropertyId, was, now);
            VariantClear(&was);
            VariantClear(&now);
        }
        if (a.hasRange && b.hasRange && a.range != b.range) {
            VARIANT was = UiaVarReal(a.range);
            VARIANT now = UiaVarReal(b.range);
            tell(uid, -1, UIA_RangeValueValuePropertyId, was, now);
            VariantClear(&was);
            VariantClear(&now);
        }
        if (a.expanded >= 0 && b.expanded >= 0 && a.expanded != b.expanded) {
            VARIANT was = UiaVarInt(a.expanded ? ExpandCollapseState_Expanded
                                               : ExpandCollapseState_Collapsed);
            VARIANT now = UiaVarInt(b.expanded ? ExpandCollapseState_Expanded
                                               : ExpandCollapseState_Collapsed);
            tell(uid, -1, UIA_ExpandCollapseExpandCollapseStatePropertyId, was, now);
            VariantClear(&was);
            VariantClear(&now);
        }
        // A page being scrolled moves every frame, and a client that heard about each of those would
        // hear about nothing else: half a percent is less than a scroll bar can show.
        if (a.hasScroll && b.hasScroll && std::fabs(a.scroll - b.scroll) >= 0.5f) {
            VARIANT was = UiaVarReal(a.scroll);
            VARIANT now = UiaVarReal(b.scroll);
            tell(uid, -1, UIA_ScrollVerticalScrollPercentPropertyId, was, now);
            VariantClear(&was);
            VariantClear(&now);
        }
        const size_t count = a.chosen.size() < b.chosen.size() ? a.chosen.size() : b.chosen.size();
        bool moved = false;
        for (size_t i = 0; i < count; i++) {
            if (a.chosen[i] == b.chosen[i]) continue;
            VARIANT was = UiaVarBool(a.chosen[i] != 0);
            VARIANT now = UiaVarBool(b.chosen[i] != 0);
            tell(uid, (int)i, UIA_SelectionItemIsSelectedPropertyId, was, now);
            VariantClear(&was);
            VariantClear(&now);
            // The event for the arriving half of it: a client that reads "4 of 6, selected" off the
            // item's own properties wants to be told which item, and this is the one that says it.
            if (b.chosen[i]) shout(uid, (int)i, UIA_SelectionItem_ElementSelectedEventId);
            moved = true;
        }
        // And once for the set: which of them is chosen is not what it was, which is a sentence
        // about the container and not about either row.
        if (moved) shout(uid, -1, UIA_Selection_InvalidatedEventId);
    }
    uiaSeen.swap(uiaNow);
}

// Both are defined at the end of this header beside Post, and both are wanted here: the
// message a deferred call arrives on, and the call that takes back the ones that never did.
inline UINT InvokeMessage();
inline void DrainInvokes(HWND hwnd);

inline LRESULT CALLBACK Window::Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    Window *self = reinterpret_cast<Window *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        self = reinterpret_cast<Window *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd = h;
    }
    if (!self) return DefWindowProcW(h, m, wp, lp);

    // Nothing a widget callback replaces is really freed until this message returns.
    // See `Window::retired`.
    Window::Dispatch frame(self);

    const float s = self->scale();
    const float mx = (float)GET_X_LPARAM(lp) / s;
    const float my = (float)GET_Y_LPARAM(lp) / s;

    // A deferred call, from Post below. Compared rather than given a case label: a registered
    // message id is not a constant. Inside the dispatch opened above, so a callback that lays
    // the page out again is doing what a widget callback does.
    if (m == InvokeMessage()) {
        std::function<void()> *call = reinterpret_cast<std::function<void()> *>(wp);
        (*call)();
        delete call;
        return 0;
    }

    switch (m) {
    case WM_NCCALCSIZE: {
        // The whole window becomes client area, which is what lets the backdrop reach
        // the top of it. DefWindowProc is still asked first: it computes the left,
        // right and bottom insets, which have to stay or a maximised window hangs off
        // the screen and the resize borders stop working. Only the top is put back.
        if (!wp) break;
        NCCALCSIZE_PARAMS *ncc = reinterpret_cast<NCCALCSIZE_PARAMS *>(lp);
        const LONG top = ncc->rgrc[0].top;
        const LRESULT r = DefWindowProcW(h, m, wp, lp);
        if (r != 0) return r;
        ncc->rgrc[0].top = top;
        // Except when maximised: a maximised window's frame really does extend past
        // the work area by the border width, and keeping the top would put the caption
        // buttons under the edge of the screen.
        if (IsZoomed(h)) {
            const UINT d = dpiapi::ForWindow(h);
            ncc->rgrc[0].top += dpiapi::SystemMetric(SM_CYSIZEFRAME, d) +
                                dpiapi::SystemMetric(SM_CXPADDEDBORDER, d);
        }
        return 0;
    }
    case WM_NCHITTEST: {
        const POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        return self->CaptionHitTest(pt);
    }
    case WM_NCMOUSEMOVE: {
        const int was = self->captionHot;
        self->captionHot = wp == HTMINBUTTON ? 0 : wp == HTMAXBUTTON ? 1
                         : wp == HTCLOSE     ? 2 : -1;
        if (self->captionHot >= 0) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE | TME_NONCLIENT, h, 0 };
            TrackMouseEvent(&tme);
        }
        if (was != self->captionHot) self->Invalidate();
        break;   // and on to DefWindowProc, which owns the caption's own behaviour
    }
    case WM_NCMOUSELEAVE:
        if (self->captionHot != -1) { self->captionHot = -1; self->Invalidate(); }
        break;
    case WM_NCLBUTTONDOWN:
        // Handled here rather than passed on, for the three buttons only. Left to
        // DefWindowProc they enter its own modal loop, which paints the pre-Windows-10
        // caption buttons into a frame that no longer exists.
        if (wp == HTMINBUTTON || wp == HTMAXBUTTON || wp == HTCLOSE) {
            self->captionDown = wp == HTMINBUTTON ? 0 : wp == HTMAXBUTTON ? 1 : 2;
            self->Invalidate();
            return 0;
        }
        break;
    case WM_NCLBUTTONUP:
        if (self->captionDown >= 0) {
            const int which = self->captionDown;
            self->captionDown = -1;
            self->Invalidate();
            const bool same = (wp == HTMINBUTTON && which == 0) ||
                              (wp == HTMAXBUTTON && which == 1) ||
                              (wp == HTCLOSE && which == 2);
            if (same) {
                if (which == 0) ShowWindow(h, SW_MINIMIZE);
                else if (which == 1) ShowWindow(h, IsZoomed(h) ? SW_RESTORE : SW_MAXIMIZE);
                else PostMessageW(h, WM_CLOSE, 0, 0);
            }
            return 0;
        }
        break;
    case WM_ACTIVATE:
        self->active = LOWORD(wp) != WA_INACTIVE;
        // A window that has just been alt-tabbed away from must not leave a flyout
        // hanging open over its own page, waiting for a click it will never get -- nor a
        // control lit under the pointer it no longer has. Alt-tab moves no mouse, so
        // there is no WM_MOUSELEAVE to do the second; the hover would sit there until the
        // pointer happened to move over this window again.
        //
        // The gesture in progress ends here too. Losing the capture has its own message
        // and usually arrives first, which is why CancelCapture is written to be harmless
        // a second time -- but a window can be deactivated while it still holds the
        // capture, and the button that was down then comes up over whatever took the
        // activation. See WM_CAPTURECHANGED.
        if (!self->active) {
            self->CancelCapture();
            if (self->content) self->DismissIn(self->content.get());
        }
        self->Invalidate();
        break;
    case WM_PAINT: {
        // The frame loop paints outside WM_PAINT while it is animating (see Window::Run),
        // directly after the Tick that moved everything. A paint here would be the second
        // one in the same frame, drawing the state from before that Tick -- and every mouse
        // message this window handles invalidates, so during a drag it is every frame, at
        // twice the drawing cost and half the frame rate.
        //
        // Unless Windows has the loop's turn in its own hands -- a drag of the border or the
        // caption -- in which case this is the only paint there is going to be, and it has
        // to happen or the window shows the size it had when the drag started.
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        if (!self->animOn || self->inSizeMove) self->Paint();
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ENTERSIZEMOVE:
        // Windows is about to run a modal loop of its own for a drag of the border or of the
        // caption. This window's frame loop -- and the compositor clock it paces itself
        // with -- gets no turn again until that loop ends, so everything the loop does has
        // to happen from a message instead, and the only message that keeps arriving is a
        // timer. Without one the window repainted only when the mouse happened to move, the
        // controls caught up with the new size only when the drag was let go, and whatever
        // was animating -- an indeterminate bar, a page's glide -- stood still until then.
        self->inSizeMove = true;
        // A frame of the loop that cannot run, in the loop's own order: tick, then paint. No
        // Dispatch of its own -- the one at the top of Proc covers the whole message, which is
        // what a tick needs to be able to lay the page out.
        self->frameTimer.Start(self, 16, [self] {
            if (!self->inSizeMove) { self->frameTimer.Stop(); return; }
            self->Frame();
            self->Paint();
            ValidateRect(self->hwnd, nullptr);
        });
        return 0;
    case WM_EXITSIZEMOVE:
        self->inSizeMove = false;
        self->frameTimer.Stop();
        // And the clock is picked up again here, or the frame loop's first frame after the
        // drag carries the whole drag's worth of `dt` -- which the loop clamps to a tenth of
        // a second, but a tenth of a second of an animation in one step is a jump.
        QueryPerformanceCounter(&self->qpcLast);
        return 0;
    case WM_SIZE:
        if (wp == SIZE_RESTORED) self->MeasureFrame();
        self->Resize();
        self->layoutDirty = true;
        self->Invalidate();
        return 0;
    case WM_DPICHANGED: {
        self->dpi = HIWORD(wp);
        const RECT *r = reinterpret_cast<const RECT *>(lp);
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        // After the move, because Resize reads the new client size and the bitmap
        // carries the DPI.
        self->Resize();
        self->layoutDirty = true;
        self->Invalidate();
        return 0;
    }
    case WM_SETTINGCHANGE:
        // The machine switched between light and dark while the window was open.
        // Windows announces it as a generic settings change with this string, which is
        // the only signal there is short of WinRT.
        if (lp && _wcsicmp(reinterpret_cast<const wchar_t *>(lp), L"ImmersiveColorSet") == 0)
            self->ReloadTheme();
        // And the animation switch, which arrives as its own action code with no string at
        // all -- so it is re-read whatever this message was for. Whatever was half way
        // somewhere is put on its target by the next frame, which is already running.
        RefreshAnimations();
        return 0;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
        TrackMouseEvent(&tme);
        if (self->MoveTo(mx, my, Window::Hand::Mouse, true)) self->Invalidate();
        return 0;
    }
    case WM_MOUSELEAVE:
        if (self->content) self->SetHover(self->content.get(), nullptr);
        self->Invalidate();
        return 0;
    case WM_LBUTTONDOWN:
        self->PressAt(mx, my, Window::Hand::Mouse);
        return 0;
    case WM_LBUTTONUP:
        self->ReleaseAt(Window::Hand::Mouse);
        return 0;
    // ---- a finger, and a pen ----------------------------------------------------------------------
    //
    // Handled rather than handed on, and that is the whole of how the promotion is stopped: Windows
    // delivers a touch as a pointer message first and only turns it into a mouse button if the window
    // *ignores* the pointer -- passes it to DefWindowProc. Answering 0 here is what makes one finger
    // one press instead of two. A pen never had a promotion to lose, and comes through the same door
    // because it is the same three things.
    //
    // The coordinates are the pointer's own: screen pixels, physical, which is a different space from
    // the one the mouse messages arrive in -- converted here rather than trusted, because a touch
    // that landed a few pixels off would be a gesture that grabbed the wrong sub-region of a control.
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP: {
        const UINT32 id = GET_POINTERID_WPARAM(wp);
        POINTER_INFO info;
        if (!GetPointerInfo(id, &info)) return 0;
        const bool touching = info.pointerType == PT_TOUCH;
        if (!touching && info.pointerType != PT_PEN) return 0;
        POINT p = { info.ptPixelLocation.x, info.ptPixelLocation.y };
        ScreenToClient(h, &p);
        const float x = (float)p.x / s, y = (float)p.y / s;
        const bool contact = (info.pointerFlags & POINTER_FLAG_INCONTACT) != 0;
        const Window::Hand hand = touching ? Window::Hand::Finger : Window::Hand::Pen;
        if (m == WM_POINTERDOWN) {
            if (!touching) {
                self->PressAt(x, y, hand);
            } else if (self->finger == 0) {
                self->finger = id;
                self->PressAt(x, y, hand);
            } else if (self->capture) {
                // **A second finger takes the first one's click away**, and leaves the gesture: what
                // one finger would have been a click on is not what the hand is doing any more, but
                // the page is still being held by it.
                self->capture->pressed = false;
            }
        } else if (m == WM_POINTERUPDATE) {
            if (!touching || self->finger == id) self->MoveTo(x, y, hand, contact);
        } else if (!touching) {
            self->ReleaseAt(hand);
        } else if (self->finger == id) {
            self->finger = 0;
            self->ReleaseAt(hand);
        }
        return 0;
    }
    // **Not the end of a gesture.** The system moves a pointer's capture on its own account as soon as
    // one is down, so this arrives at the beginning of every touch -- and treating it as "the capture
    // is gone" cancels the press that was just made, which is a tap that does nothing and a list that
    // cannot be dragged. A gesture that really loses the capture this window holds goes through
    // `WM_CAPTURECHANGED` like any other, and that is where it ends. Answered rather than handed on,
    // for the same reason as the three above: a message a window says it handles is a message the
    // system does not promote.
    case WM_POINTERCAPTURECHANGED:
        return 0;
    // The capture went away without a button-up: alt-tab, a system modal, another
    // application taking the mouse. Windows revokes it and no WM_LBUTTONUP is ever
    // coming, so a gesture left running here is one that never ends. The button stays
    // dark under a window that is not even active any more -- and worse, `capture` still
    // points at the control, so the next time the pointer crosses this window the move
    // goes straight to OnDrag and the slider follows it with no button held.
    //
    // Ended as a release that is not a click. A drag has already put its value on screen
    // and in memory, so OnRelease is what stops a settings file from disagreeing with
    // both; OnClick is the half that must not happen, because the gesture was abandoned
    // rather than finished.
    //
    // No ReleaseCapture here: it is already gone, and calling it inside this message is
    // what the documentation warns against. Nor does this double up with the case above
    // -- that clears `capture` before it releases, so the WM_CAPTURECHANGED it causes
    // arrives to find nothing left to end.
    case WM_CAPTURECHANGED:
        self->CancelCapture();
        return 0;
    case WM_CANCELMODE:
        // The same thing, announced before the capture is taken rather than after.
        // Released here so that the two handlers cannot disagree about who holds it.
        if (self->capture) ReleaseCapture();
        self->CancelCapture();
        return 0;
    case WM_GETMINMAXINFO: {
        int mw = 0, mh = 0;
        self->MinSize(&mw, &mh);
        if (mw <= 0 && mh <= 0) break;
        // The minimum is a *window* size and MinSize speaks in client DIPs. The
        // difference is the frame this window keeps, as measured -- see MeasureFrame.
        MINMAXINFO *mmi = (MINMAXINFO *)lp;
        if (mw > 0) mmi->ptMinTrackSize.x = (LONG)(mw * self->scale()) + self->frameExtra.cx;
        if (mh > 0) mmi->ptMinTrackSize.y = (LONG)(mh * self->scale()) + self->frameExtra.cy;
        return 0;
    }
    case WM_MOUSEWHEEL: {
        // Delivered in screen coordinates, unlike every other mouse message.
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(h, &pt);
        const float x = pt.x / s, y = pt.y / s;
        const float notches = (float)GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA;
        // The widget under the pointer first, and then each of the things it is inside of, in its
        // own space -- see Widget::OnWheel. The walk up is what lets a control in a scrolling
        // container turn the container: the innermost thing that wants the notch takes it, and a
        // control that has no use for one is not in the way of the thing around it.
        for (Widget *w = self->HitTest(x, y); w; w = w->parent) {
            const D2D1_POINT_2F at = self->LocalPoint(w, x, y);
            if (w->OnWheel(at.x, at.y, notches)) {
                self->Invalidate();
                return 0;
            }
        }
        // Then the layer on top, which is offered the notch whether or not the pointer was over it:
        // a flyout that is up is what a wheel over it is for, and the page under it is not the thing
        // being turned.
        if (Layer *top = self->TopLayer()) {
            const D2D1_POINT_2F at = self->LocalPoint(top, x, y);
            if (top->OnWheel(at.x, at.y, notches)) {
                self->Invalidate();
                return 0;
            }
        }
        return self->OnAppMessage(m, wp, MAKELPARAM(pt.x, pt.y)) ? 0 : DefWindowProcW(h, m, wp, lp);
    }
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
    case WM_CHAR:
    case WM_IME_CHAR: {
        // Both, because they arrive by different routes and a field that handles only
        // one is a field that works in English and not in Chinese. WM_CHAR carries
        // keyboard input and, on most IMEs, the committed result as well; WM_IME_CHAR
        // is what some of them send instead.
        const wchar_t ch = (wchar_t)wp;
        if (self->focused && ch >= 0x20 && ch != 0x7F && self->focused->OnChar(ch)) {
            self->caretOn = true;
            self->Invalidate();
            return 0;
        }
        return 0;
    }
    case WM_IME_STARTCOMPOSITION:
        self->PlaceImeAtCaret();
        return DefWindowProcW(h, m, wp, lp);
    case WM_KEYDOWN:
        if (self->focused && self->focused->OnKey(wp)) {
            self->caretOn = true;
            self->Invalidate();
            return 0;
        }
        switch (wp) {
        case VK_TAB:
            self->MoveFocus((GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
            return 0;
        case VK_SPACE:
            if (self->focused) { self->focused->OnActivate(); self->Invalidate(); }
            return 0;
        case VK_RETURN: {
            // Enter operates the focused control if it is one that can be operated,
            // and otherwise the page's default action. Without the first half, tabbing
            // to "Browse" and pressing Enter would press the page's default button. A
            // layer in between gets the turn before the page does: a dialog's default
            // button is the page's default action for as long as the dialog is up.
            Widget *operate = self->focused;
            if (!operate) {
                Layer *top = self->TopLayer();
                // Not if it is on its way out: Enter pressed again in the middle of a dismissal
                // would press a button that is already gone.
                operate = (top && !top->Leaving()) ? top->DefaultButton() : nullptr;
            }
            if (operate) operate->OnActivate();
            else         self->OnDefaultAction();
            self->Invalidate();
            return 0;
        }
        case VK_ESCAPE:
            // A layer first: Esc is how a dialog and a flyout are closed, and the page's cancel
            // -- usually "close the window" -- is what is left when there is no layer to take it.
            if (Layer *top = self->TopLayer()) {
                if (top->Escape()) {
                    self->Invalidate();
                    return 0;
                }
            }
            self->OnCancel();
            return 0;
        }
        return 0;
    case WM_GETOBJECT: {
        // UI Automation, and only while a client is actually listening: a screen reader is absent
        // in almost every run of a program built on this, and a provider tree built to answer
        // nobody is work on the UI thread for nothing. Anything else falls through to
        // DefWindowProc, which is where the MSAA half of the world's answer has always come from.
        const uiaapi::Api &uia = uiaapi::Get();
        if ((LONG)lp == UiaRootObjectId && uia.Ready() && uia.ClientsAreListening())
            return uia.ReturnRawElementProvider(h, wp, lp, self->UiaRoot());
        break;
    }
    case WM_TIMER:
        // A timer this window is running: see Timer, where the window's own and every control's
        // come from, and which knows whose id this is. By index, with the size asked again
        // each turn, because a callback can stop the timer it is running on.
        for (size_t i = 0; i < self->timers.size(); i++)
            if (self->timers[i]->Handle(wp)) return 0;
        // Anything else is a timer a page set for itself, which falls through to OnAppMessage,
        // which is where a page's messages are answered.
        break;
    case WM_ERASEBKGND:
        return 1;   // every pixel comes from the composition surface
    case WM_DESTROY:
        // Before it stops being a window: the deferred calls that never arrived are freed
        // here. See DrainInvokes.
        DrainInvokes(h);
        self->alive = false;
        // The device, the fonts and the Write factory are this window's own and go with it. The
        // tail of Run() does this for the windows that are still there when the loop ends, and that
        // is the *other* way out: a window closed while the app carries on is destroyed here and
        // nowhere else. Without this line every window a page opens and closes takes a D3D device,
        // a swap chain, a composition target and a font set with it, which is a window a page is
        // meant to be able to open and close all day.
        self->EndPump();
        self->OnClosed();
        // The app posts WM_QUIT itself, when the window that has just gone was the last one in
        // it. A window that is in no app is the whole program, as it was before there was one.
        if (self->app) self->app->Remove(*self);
        else           PostQuitMessage(0);
        return 0;
    }
    if (self->OnAppMessage(m, wp, lp)) return 0;
    return DefWindowProcW(h, m, wp, lp);
}

// Run `fn` on the thread that owns `w`, once the message being handled now is finished.
// Safe to call from any thread, which is the whole point of it.
//
// The envelope is a `std::function` on the heap, and who frees it is settled by the same
// thing that settles whether it runs: if the post fails -- the window is already gone -- the
// caller frees it and gets false back, and otherwise the window does, either when it runs it
// or, if it is destroyed first, in the drain. Windows discards a posted message when its
// window goes away, and the envelope would go with it.
//
// The `Window` has to outlive the call. A window made in `wWinMain` does: it is destroyed
// before `Run` returns, not after.
inline bool Post(Window *w, std::function<void()> fn) {
    if (!w || !w->hwnd) return false;
    auto *envelope = new std::function<void()>(std::move(fn));
    if (PostMessageW(w->hwnd, InvokeMessage(), reinterpret_cast<WPARAM>(envelope), 0))
        return true;
    delete envelope;
    return false;
}

// A place to post from, one turn at a time.
//
// A worker with a hundred updates a second has one thing to tell the window -- that there is
// something new -- and asking it a hundred times is asking once and then doing nothing
// ninety-nine times. `pending` answers that question, and it belongs to the caller, so the
// coalescing costs no lock and the library stays as single-threaded as it was.
//
// What `fn` may therefore not do is *carry* a change: a call that is dropped is a change that
// is never reported. It reads the latest state instead, which is the shape a page wants
// anyway -- the state lives on the worker's side of the boundary and the page can only ever
// see a snapshot of it.
//
// The slot has to outlive everything posted through it.
struct PostSlot {
    std::atomic<bool> pending{ false };

    bool Post(Window *w, std::function<void()> fn) {
        bool expected = false;
        if (!pending.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return false;
        const bool posted = micula::Post(w, [this, fn = std::move(fn)] {
            // Cleared before it runs, so a call made from inside one is not dropped.
            pending.store(false, std::memory_order_release);
            fn();
        });
        if (!posted) pending.store(false, std::memory_order_release);
        return posted;
    }
};

// The deferred calls that never arrived, freed as the window is destroyed -- while it is
// still a window, because that is when PeekMessage can still find what is queued for it.
// A `WM_NCDESTROY` drain would come too late: by then Windows has taken the messages away.
inline void DrainInvokes(HWND hwnd) {
    if (!hwnd) return;
    const UINT id = InvokeMessage();
    MSG msg;
    while (PeekMessageW(&msg, hwnd, id, id, PM_REMOVE))
        delete reinterpret_cast<std::function<void()> *>(msg.wParam);
}

// The message a deferred call arrives on.
//
// Registered rather than taken from the `WM_APP` range, which a page is free to use for its
// own messages: this id is unique for the whole system, so the two cannot collide, and there
// is no range for a page to remember to keep clear.
//
// Cached, because it is read on every poke at the message queue and registering is a call
// into the kernel. 0 -- the only way it fails -- is not a message anything can be posted as,
// so Post answers false rather than queueing something nothing will ever deliver.
inline UINT InvokeMessage() {
    static const UINT id = RegisterWindowMessageW(L"micula::Invoke");
    return id;
}

// Kept for its callers, and now it only has to wake the loop.
//
// There is nothing left to start: Run() asks `Animating() || AnimationWanted()` at the
// top of every turn, so a state change made anywhere is picked up by the next one. What
// it does still have to do is get the loop *to* its next turn -- if the window is
// blocked in GetMessage, invalidating is what returns from it.
inline void StartAnimation(Window *w) {
    if (w) w->Invalidate();
}

// The end of one window message. Nothing a widget callback was standing on is really
// freed until here; see `Window::retired`.
inline Window::Dispatch::~Dispatch() {
    if (--w->dispatchDepth == 0) w->retired.clear();
}

// Per-monitor v2, set from code as well as from the manifest.
//
// The manifest is what actually takes effect for a normally-launched program, and it
// is where this belongs. This call is the belt to those braces: it is what makes the
// window behave when the exe is started by something that supplies its own activation
// context -- an elevated launch through a shell extension, for one.
inline void EnablePerMonitorDpi() {
    dpiapi::EnablePerMonitorV2();
}

// ---------------------------------------------------------------- clipboard

// Both directions, for the one field that takes text. Small enough to live here and
// not worth a file: the alternative is every caller opening the clipboard by hand and
// one of them forgetting to close it, which locks it for every other program on the
// machine.
inline std::wstring ClipboardText(HWND owner) {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(owner)) return L"";
    std::wstring out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t *p = (const wchar_t *)GlobalLock(h)) {
            out = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    // Exactly what is there. What a *field* should keep of it is the field's decision --
    // see TextBox::Pasted.
    return out;
}

inline void SetClipboardText(HWND owner, const std::wstring &s) {
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    const size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void *p = GlobalLock(h)) {
            memcpy(p, s.c_str(), bytes);
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        }
    }
    CloseClipboard();
}

}  // namespace micula
