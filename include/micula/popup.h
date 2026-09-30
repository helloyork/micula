// Micula / popup.h
//
// **A top-level thing that draws and is not a window.** A menu, a tooltip, a flyout that has to be
// able to leave the rectangle it was opened from. Everything about being on a screen is a window's --
// activation, the compositor, per-monitor DPI, the pointer -- so a popup *is* a window: a real
// `WS_POPUP` one, with no caption and no client area under one. Everything above the handle is a
// `Surface`, which is the half a window and a menu have in common.
//
// **Why not a `Layer`.** A layer is drawn inside its window: it cannot cross the window's edge, it
// moves when the window moves, and it goes when the window goes. A menu has to hang over the edge of
// the window it came from -- a drop-down's list covers the page rather than the field, and that is
// already at the limit of what one window's drawing can do -- and the case that settles it is a menu
// that has to exist with *no* window behind it at all: a shell tray menu belongs to a program that has
// no window to open it from.
//
// **And it is pumped, not looped.** A popup joins the `App` like anything else, so the window under it
// goes on animating while it is up. That is the whole reason this class exists: a modal message loop
// of a menu's own -- which is how the old frameworks did it -- stops every animation in the program
// for as long as the menu is open, and makes a menu that is itself animated impossible.
//
// What it deliberately does *not* have: a caption, the page hooks, an automation tree of its own
// (`UiaElement` is the window's, and a menu's rows reach a client through the window that owns the
// tree), a frame the size of the screen. Those are `Window`'s, and a popup that grew them would be a
// window with a worse name.
//
// ```cpp
// struct SettingsMenu : Popup {
//     const wchar_t *ClassName() const override { return L"MiculaSettingsMenu"; }
//     bool OnMessage(UINT m, WPARAM wp, LPARAM lp) override {
//         if (m == WM_KEYDOWN && wp == VK_ESCAPE) { Hide(); return true; }
//         return false;
//     }
// };
//
// SettingsMenu menu;
// menu.Add(new Label(L"Settings"));
// menu.SetLayout(new StackLayout());
// menu.Show(x, y, 220.0f, 120.0f);
// app.Add(menu);              // and the window underneath keeps its frames
// ```
//
// `Show` is `HWND_TOPMOST`, so a menu is over the window it came from and over the taskbar too -- what
// a tray menu needs. It is not `WS_EX_TOOLWINDOW`'s shadow: that is there so a menu has no taskbar
// button and no Alt+Tab entry of its own, which is what a menu *is*.

#pragma once

#include "window.h"

#include <algorithm>

namespace micula {

struct Popup : Surface {
    virtual ~Popup();

    // --- to implement ---------------------------------------------------------------------------
    // The window class this popup's window is made with, registered on demand. One per popup kind,
    // like a window's, and the same rule applies: the class name has to be the same string every time
    // it is asked for.
    virtual const wchar_t *ClassName() const = 0;
    // A message this popup answers itself: a key, the wheel, the activation going away -- what the
    // thing it *is* decides. Return true when it was answered; false hands it to DefWindowProc, which
    // is where a popup that answers nothing at all should be.
    virtual bool OnMessage(UINT /*m*/, WPARAM /*wp*/, LPARAM /*lp*/) { return false; }
    // Shown, moved or resized: where a menu puts its tree, and where a tip measures its words. The
    // arrangement has already been asked for by then -- see `Surface::Frame` -- so what this is for is
    // a change of *size*, which a layout that arranges by hand cannot see for itself.
    virtual void OnPlaced() {}

    // --- showing it -----------------------------------------------------------------------------
    // Show it with its top-left corner at `x, y` **screen DIPs** and a client area of `dipW x dipH`.
    //
    // `activate` false for a popup that must not take the foreground: a tip, or a menu opened over a
    // window that already has it and should keep it -- a menu that activated would blink the caption of
    // the window it belongs to from active to inactive and back. Decide it before `Show`, because the
    // window is *made* with it.
    bool activates = true;
    bool Show(int x, int y, int dipW, int dipH);
    // Move it, in screen DIPs, without touching its size or its tree.
    void Place(int x, int y);
    void Place(int x, int y, int dipW, int dipH);
    // Take it off the screen. The window and the tree stay where they are, so showing it again is one
    // call and no rebuilding.
    void Hide();
    bool Shown() const { return hwnd && IsWindowVisible(hwnd) != FALSE; }

    // --- the tree -------------------------------------------------------------------------------
    // The root widget, made on first use: a popup is built into like a window is, and `Add` here is the
    // same call a page makes.
    View *EnsureContent() {
        if (!content) {
            content.reset(new View());
            content->host = this;
        }
        return static_cast<View *>(content.get());
    }
    template <typename T> T *Add(T *w) { return EnsureContent()->Add(w); }
    void SetLayout(Layout *l) { EnsureContent()->SetLayout(l); }

    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l);
};

inline Popup::~Popup() {
    // A popup owns its window, which is the one thing about it that is not a window's: a `Window` is
    // destroyed by whoever asks for it to close, and a menu is a local in whatever opened it, so it
    // goes when that local does.
    if (!hwnd) return;
    // And it leaves the app before the window goes, because the window is what takes the surface down:
    // see `Surface::EndPump`, which runs from WM_DESTROY below.
    if (app) app->Remove(*this);
    // A tree on its way down takes every node with it, and this is the one moment a removal must not
    // happen: see `Surface::tearingDown`.
    tearingDown = true;
    HWND h = hwnd;
    hwnd = nullptr;
    DestroyWindow(h);
}

inline bool Popup::Show(int x, int y, int dipW, int dipH) {
    // COM and the theme, exactly as a window does it -- and here it is not a formality: a program whose
    // only window is a menu (the tray case) is a program with no `Window::Create` in it at all, so this
    // is the first thing in it that needs either.
    detail::EnsureCom();
    if (!dpiapi::AwarenessSettled()) dpiapi::EnablePerMonitorV2();
    CurrentFonts();
    RefreshPalette();

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = Proc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = ClassName();
    wc.hCursor       = LoadCursorW(nullptr, kCursorArrow);
    // No background brush: every pixel comes from the composition surface, and letting USER32 erase
    // first is a flash of grey every time a menu opens.
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    // **Placed in the DPI of the monitor it lands on, which is not known until it exists.** A menu
    // opens where the pointer is, and the pointer can be on a screen other than the one the window it
    // belongs to is on, so the DIPs it is asked for are that monitor's DIPs. The window is therefore
    // created a pixel wide at the point -- on the right monitor, in the wrong pixels -- and moved once
    // its own DPI is known. It is created hidden and shown after, so none of that is visible; creating
    // it at the final size in the wrong DPI and letting WM_DPICHANGED fix it up would be, and for a
    // menu the opening is the whole of what it looks like.
    DWORD ex = WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP;
    if (!activates) ex |= WS_EX_NOACTIVATE;
    hwnd = CreateWindowExW(ex, ClassName(), L"", WS_POPUP, x, y, 1, 1,
                           nullptr, nullptr, wc.hInstance, this);
    if (!hwnd) return false;
    dpi = dpiapi::ForWindow(hwnd);
    Place(x, y, dipW, dipH);

    // A popup is built into before it is shown, so the tree is already there and owes an arrangement;
    // that happens on the way to the first paint.
    EnsureContent();
    layoutDirty = true;
    // `SW_SHOWNOACTIVATE` for the popups that asked not to activate, and `SW_SHOW` -- which is what
    // makes the menu the foreground window, and therefore what makes a click outside it possible to
    // notice -- for the ones that did not.
    ShowWindow(hwnd, activates ? SW_SHOW : SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);
    return true;
}

inline void Popup::Place(int x, int y) {
    if (!hwnd) return;
    RECT rc;
    GetWindowRect(hwnd, &rc);
    SetWindowPos(hwnd, HWND_TOPMOST, MulDiv(x, (int)dpi, 96), MulDiv(y, (int)dpi, 96),
                 rc.right - rc.left, rc.bottom - rc.top, SWP_NOACTIVATE);
    OnPlaced();
}

inline void Popup::Place(int x, int y, int dipW, int dipH) {
    if (!hwnd) return;
    SetWindowPos(hwnd, HWND_TOPMOST, MulDiv(x, (int)dpi, 96), MulDiv(y, (int)dpi, 96),
                 (std::max)(1, MulDiv(dipW, (int)dpi, 96)),
                 (std::max)(1, MulDiv(dipH, (int)dpi, 96)), SWP_NOACTIVATE);
    // The size changed under the swap chain, and the tree has to be arranged into the new box -- see
    // `Surface::Resize` and `RootBox`. WM_SIZE does both when the move came from somewhere else; here
    // it may not arrive at all while the window is hidden, which is why it is done explicitly.
    Resize();
    layoutDirty = true;
    Invalidate();
    OnPlaced();
}

inline void Popup::Hide() {
    if (hwnd) ShowWindow(hwnd, SW_HIDE);
}

inline LRESULT CALLBACK Popup::Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    Popup *self = reinterpret_cast<Popup *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        self = reinterpret_cast<Popup *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd = h;
    }
    if (!self) return DefWindowProcW(h, m, wp, lp);

    // Nothing a widget callback replaces is really freed until this message returns. The guard is the
    // popup's own rather than the loop's, because a popup that is in no app at all still dispatches its
    // own messages. See `Surface::retired` and `App::PumpMessage`.
    Surface::Dispatch frame(self);

    switch (m) {
    case WM_MOUSEACTIVATE:
        // A popup made not to activate has to say so here as well: the extended style keeps it out of
        // the foreground, and this keeps a click on it from trying to take it anyway.
        if (!self->activates) return MA_NOACTIVATE;
        break;
    case WM_PAINT: {
        // The frame loop draws outside WM_PAINT while it is animating, so a paint here would be the
        // second one in the same frame, from the state before the tick. See `App::Run`.
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        if (!self->animOn) self->Paint();
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SIZE:
        self->Resize();
        self->layoutDirty = true;
        self->Invalidate();
        return 0;
    case WM_DPICHANGED: {
        // Dragged to a monitor with another DPI -- which a menu that follows the pointer across two
        // screens does, and a tip on a laptop docked to a second one. Everything the surface draws is
        // in DIPs on a target that carries the DPI, so this is the whole of it.
        self->dpi = HIWORD(wp);
        const RECT *r = reinterpret_cast<const RECT *>(lp);
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        self->Resize();
        self->layoutDirty = true;
        self->Invalidate();
        return 0;
    }
    case WM_SETTINGCHANGE:
        // The machine switched between light and dark while the menu was open. A menu has no window of
        // its own to have heard the message for it, so it hears it itself -- see `Surface::ReloadTheme`,
        // which is the half a window shares and the palette is the process's.
        if (lp && _wcsicmp(reinterpret_cast<const wchar_t *>(lp), L"ImmersiveColorSet") == 0)
            self->ReloadTheme();
        RefreshAnimations();
        return 0;
    case WM_TIMER:
        // A timer this popup is running. See Timer, which knows whose id this is, and which is why the
        // size is asked again each turn: a callback can stop the timer it is running on.
        for (size_t i = 0; i < self->timers.size(); i++)
            if (self->timers[i]->Handle(wp)) return 0;
        break;
    case WM_ERASEBKGND:
        return 1;   // every pixel comes from the composition surface
    case WM_DESTROY:
        self->alive = false;
        self->EndPump();
        return 0;
    }

    // The three things a hand does, and then whatever this popup is: the hit test, the press and the
    // click are the surface's and are shared with a window -- a menu answers a finger exactly as a
    // window does -- and Up/Down/Enter/Esc are the menu's own. See `Surface::HandMessage`.
    if (self->HandMessage(h, m, wp, lp)) return 0;
    return self->OnMessage(m, wp, lp) ? 0 : DefWindowProcW(h, m, wp, lp);
}

}  // namespace micula
