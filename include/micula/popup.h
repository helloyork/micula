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

    // --- arriving ----------------------------------------------------------------------------------
    // **A popup arrives rather than appearing.** A menu or a tip that is simply *there* on the next frame
    // reads as a drawing fault rather than as something opening, and both of Fluent's overlays fade -- which
    // is also the cheapest half of what its overlays do. The ramp is the library's own, a `motion::Track`
    // walked over `kFast`, exactly as a `Layer`'s arrival is.
    //
    // **And the animation switch has the last word.** With animations off `Arrival()` answers 1 from the
    // first frame: a popup that faded in on a machine that asked for no animations would be a box that is
    // invisible for a sixth of a second, which is the one thing "no animations" must not come to mean.
    //
    // What it is *for* is the whole popup at once: a subclass that has a tree leaves `Widget::opacity` on its
    // root at this value, which is one group over everything in it -- see `Menu::Open` -- and one that draws
    // its own furniture fades the colours it draws, because there is no tree to fade. **A per-element arrival
    // is the other half of the same idea** -- every row with an offset and an opacity of its own rather than
    // the group moving as one -- and it is a change in whoever draws the elements: the menu already works out
    // each row's own box and colour, so it is that loop that would grow the second half.
    float Arrival() const { return Animations() ? arrive.value : 1.0f; }
    motion::Track arrive{};

    Popup() { arrive.To(1.0f); }

    // **Start the arrival over**, for whoever is about to show the popup: a menu opened a second time fades
    // in again rather than appearing whole. It is the caller's to call rather than this class's, because
    // `Place` is also how a popup is *moved*, and moving one must not make it fade.
    //
    // `Set` and then `To`, in that order and not the other way round: `Set` puts the value *and the target*
    // where it is told to, so stopping there would leave the track walking to zero -- a popup that fades out
    // on the frame it is shown, and an `Animating()` that stays true for as long as it is up, which is a
    // frame loop that never sleeps.
    void Arrive() {
        arrive.Set(0.0f);
        arrive.To(1.0f);
        if (content) content->opacity = Arrival();
    }

    void OnTick(float dt) override {
        arrive.Step(dt, motion::kFast);
        // The tree is drawn through the root's own opacity, which `PaintTree` turns into one layer over the
        // subtree -- so the panel, its shadow and every row fade as one group, rather than each of them
        // showing the page through the gaps between the others. See `Widget::opacity`.
        if (content) content->opacity = Arrival();
    }
    bool Animating() const override { return Surface::Animating() || arrive.Wants(1.0f); }

    // --- to implement ---------------------------------------------------------------------------
    // The window class this popup's window is made with, registered on demand. One per popup kind,
    // like a window's, and the same rule applies: the class name has to be the same string every time
    // it is asked for.
    virtual const wchar_t *ClassName() const = 0;
    // A message this popup answers itself: a key, the wheel, the activation going away -- what the
    // thing it *is* decides. Return true when it was answered; false hands it to DefWindowProc, which
    // is where a popup that answers nothing at all should be.
    virtual bool OnMessage(UINT /*m*/, WPARAM /*wp*/, LPARAM /*lp*/) { return false; }
    // **A message whose answer is its return value rather than "I handled it".** `WM_NCHITTEST` is the
    // one there is, and a menu needs it: its window is larger than its panel by the room its shadow
    // takes, and a click in that room has to reach whatever is under the menu instead of the menu -- a
    // click that landed on the shadow would both do nothing and leave the menu up over a page somebody
    // is trying to use. False lets Windows answer, which is what every other popup wants.
    virtual bool OnHitTest(POINT /*screen*/, LRESULT & /*out*/) { return false; }
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
    // Extra extended styles for the popup's own window, on top of the two it always has
    // (`WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP`). Set it before `Show`. A tip is
    // `WS_EX_TRANSPARENT`, so that a click where the tip is lands on what the tip is *about* rather
    // than on the tip: a name that swallows the click on the thing it names is a control that stops
    // working while its own name is on the screen.
    DWORD extraStyle = 0;
    // **Which monitor's DIPs the point is in.** A caller that has already worked it out -- a tip, which
    // has to know to flip on the right work area -- passes it, and one that has not passes 0 and the
    // point itself is asked (see `dpiapi::ForPoint`, which takes pixels and can only be as right as the
    // point it is given).
    bool Show(int x, int y, int dipW, int dipH, UINT dpiOf = 0);
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

inline bool Popup::Show(int x, int y, int dipW, int dipH, UINT dpiOf) {
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

    // **Placed in the DPI of the monitor it lands on**, which is asked of the *monitor* and not of a
    // window: a menu opens where the pointer is, and the pointer can be on a screen other than the one
    // the window it is opened from is on, so the DIPs it is asked for are that monitor's DIPs. Asking a
    // window instead would mean creating it first, in the wrong pixels on a machine with two different
    // screens, and moving it once the answer came back -- and for a menu the opening is the whole of
    // what it looks like. See `dpiapi::ForPoint`.
    DWORD ex = WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP | extraStyle;
    if (!activates) ex |= WS_EX_NOACTIVATE;
    dpi = dpiOf ? dpiOf : dpiapi::ForPoint({ x, y });
    hwnd = CreateWindowExW(ex, ClassName(), L"", WS_POPUP,
                           MulDiv(x, (int)dpi, 96), MulDiv(y, (int)dpi, 96),
                           (std::max)(1, MulDiv(dipW, (int)dpi, 96)),
                           (std::max)(1, MulDiv(dipH, (int)dpi, 96)),
                           nullptr, nullptr, wc.hInstance, this);
    if (!hwnd) return false;
    // **And topmost, which is not what a new window is.** `CreateWindowExW` puts a window at the top of the
    // *non-topmost* band, so a popup opened by a window that is topmost -- or by anything that has itself
    // been raised, a test harness being the one that showed this -- is behind the window it was opened
    // from until something moves it. `Place` is what carries that, so it is the one path both the first
    // show and every move go through.
    Place(x, y, dipW, dipH);
    // A popup is built into before it is shown, so the tree is already there and owes an arrangement;
    // that happens on the way to the first paint, which is what `Place` just asked for.
    EnsureContent();
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
    case WM_NCHITTEST: {
        LRESULT out = 0;
        if (self->OnHitTest({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }, out)) return out;
        break;
    }
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
