# Window

`struct micula::Window`, in `micula/window.h`.

Subclass it, override `ClassName()`, `Title()` and `Layout()`, then call `Create()` and
`Run()`.

```cpp
struct Page : micula::Window {
    bool enabled = true;

    const wchar_t *ClassName() const override { return L"MyApp.Page"; }
    const wchar_t *Title() const override { return L"My App"; }

    void Layout() override {
        ClearWidgets();
        auto *sw = Add(new micula::ToggleSwitch(L"Enabled", enabled,
                                                [this](bool on) { enabled = on; }));
        sw->rect = micula::Rect(24, 56, 300, 32);
    }
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, wchar_t *, int) {
    micula::EnablePerMonitorDpi();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Page page;
    return page.Create(360, 120, false, nullptr) ? page.Run() : 1;
}
```

## Creating and running

| Member | Description |
|---|---|
| `bool Create(int dipW, int dipH, bool canResize, HICON icon)` | Registers the window class, creates the window centered on its monitor with a client area of `dipW` x `dipH` DIPs, applies the theme, calls `Layout()` and shows it with `showCommand`. `canResize` adds the resize border and the maximize button. `icon` may be null. Returns false if the window could not be created. |
| `int Run()` | Runs the message loop until the window is destroyed. Returns the `WM_QUIT` exit code: 0, or the value passed to `PostQuitMessage`. Releases the window's Direct2D resources before returning. |
| `int showCommand` | How `Create` shows the window. Default `SW_SHOW`. With `SW_HIDE` the window stays hidden until the program shows it. |

Esc and the title bar's close button post `WM_CLOSE`. To ask before closing, handle
`WM_CLOSE` in `OnAppMessage` and return true to keep the window open.

Frames are run only while something asks for them, and only while the window is somewhere
it can be seen. A hidden, minimised or cloaked window gets none at all -- an animation still
going does not paint into a window nobody is looking at, and a minimised window is the
worst of it, because every one of those frames is drawn and then thrown away. A control the
page has scrolled out of `ClipRect()` does not ask for frames either: it is not drawn, so it
is not animated. Neither case loses the animation -- the frame clock is picked up again when
frames resume, so an ease carries on from where it was rather than jumping forward by
however long the window was away.

Occlusion is not part of that. Windows has no query for "another window is over this one",
and the approximations of one are wrong for a partly covered window and for a layered one,
so a window that is merely covered still paints. `Visible()` is the same test the frame loop
uses, for a page that wants to ask it of itself.

## The app

`Window::Run()` runs that one window. A program with more than one -- which is what a windowed popup
needs -- makes an `App` and gives it the windows:

```cpp
int WINAPI wWinMain(HINSTANCE, HINSTANCE, wchar_t *, int) {
    App app;
    MainWindow main;
    if (!main.Create(1040, 700, true, nullptr)) return 1;
    app.Add(main);
    app.Add(tool);                 // as many as the program has
    return app.Run();
}
```

| Member | Description |
|---|---|
| `std::vector<Window *> windows` | What it pumps, in the order they were added. |
| `void Add(Window &w)` | Adds a window to the loop. It may be added while the loop is running, which is what a window a page makes should do. |
| `void Remove(Window &w)` | Takes one out. The loop ends when the last one is gone, and a window that is destroyed does this itself. |
| `int Run()` | Runs until there is nothing left to run. Returns the exit code of the `WM_QUIT` that ended it. |
| `void Quit(int code = 0)` | Ends the loop where it stands. |
| `bool Running() const` | Whether `Run` is on the stack. |

On `Window`, three things go with it:

| Member | Description |
|---|---|
| `App *app` | The app this window is in, or null. Set by `App::Add`. |
| `virtual void OnClosed()` | The window has been destroyed, and this is the last thing it does about it. |
| `int Run()` | `App app; app.Add(*this); return app.Run();` -- the same loop, for a program with one window. |

- **One loop, one thread, as many windows as you like.** Every window in the app is ticked and
painted by the same loop, on the thread that made them, so a window that did not start the loop
still animates -- and nothing in the library has to be synchronised for it.
- **The App does not have to outlive the windows**, and it is usually a local in `wWinMain`, which
means it is destroyed *first*. A window still in it clears its own pointer, and the App's destructor
clears whatever is left, so the order the two die in does not matter.
- **Owning a window a page made.** A window cannot be deleted inside its own message, so `OnClosed`
is where a page hears about it, and the usual shape is a `Post` to the window that made it -- see the
Debug page of `examples/nav`, which does exactly that with its "Open the probe" card.
- **A window made while the loop is running** is brought up to it as it joins: its frame clock and
its caret timer start there, and `WM_DESTROY` hands back its device and its fonts. The tail of
`Run()` does that only for the windows still standing when the loop ends, which is the other way out
-- a window a page opens and closes all day would otherwise take a D3D device with it every time.

## Page callbacks

All virtual. `ClassName()` and `Title()` must be overridden.

| Callback | Default | When |
|---|---|---|
| `const wchar_t *ClassName() const` | | Once, in `Create`. The window class name; unique per window type. |
| `const wchar_t *Title() const` | | Window title, also drawn in the title bar. |
| `void Layout()` | nothing | In `Create`, on resize and on DPI change. Call it yourself when the page changes shape -- and not when it scrolls: that is `ContentTransform()`. Start with `ClearWidgets()`. |
| `void PaintPage(const Painter &p)` | nothing | Every paint, before the controls. Draws what is not a control: headings, card backgrounds, labels. |
| `void OnDefaultAction()` | nothing | Enter, when no control has focus. |
| `void OnCancel()` | posts `WM_CLOSE` | Esc. |
| `void OnTick(float dt)` | nothing | Every animation frame. `dt` is seconds since the last frame, at most 0.1. |
| `bool AnimationWanted() const` | `false` | Before every frame. Return true to keep frames coming for an animation of the page's own. |
| `bool OnAppMessage(UINT m, WPARAM wp, LPARAM lp)` | `false` | Messages the window does not consume. Return true if handled. See [Messages](#messages). |
| `D2D1_RECT_F ClipRect() const` | empty | The scrolling part of the page. Controls with `scrolls` set are clipped to it and take input only inside it. A control that floats over the page -- one with a `z` -- is not: it is drawn whole, because its shadow is meant to reach outside its own rectangle, and it keeps itself inside whatever room it has. Empty means the page does not scroll. |
| `void ContentTransform(float *dy, float *opacity) const` | `0`, `1` | Vertical offset and opacity for the scrolling controls while painting. **A page's scroll lives here** (`dy = -drawn`); hit testing subtracts the same offset, and the points a control is given in its callbacks have had it taken off already. The opacity is for a page arriving or fading in. `PaintPage` is not transformed. |
| `void MinSize(int *w, int *h) const` | `0`, `0` | Smallest client size in DIPs the window can be resized to. 0 means no limit. |

## Controls

| Member | Description |
|---|---|
| `T *Add(T *widget)` | Takes ownership of `widget`, sets its `owner`, appends it and returns it. The order is paint order and Tab order. |
| `void ClearWidgets()` | Removes every control that is not `persistent`. |
| `std::vector<std::unique_ptr<Widget>> widgets` | The controls. |
| `Widget *focused` | The control with keyboard focus, or null. |
| `void SetFocusTo(Widget *w)` | Moves focus to `w` (or clears it with null). The previous control gets `OnBlur()`. |
| `void MoveFocus(int delta)` | Moves focus forward (1) or back (-1) through the focusable controls, as Tab does. |
| `Widget *capture` | The control the mouse button went down on, until it is released. |
| `bool showFocusRing` | Set when Tab moves focus, cleared by a click. Controls draw a focus ring only while it is set. |

## Layers

A `Layer` is a control that floats over the page: a dialog, a flyout, a menu, a tip. A widget with
a `z` already paints over the page and is reached by the pointer before it; a layer adds the things
that make it a layer rather than a control that happens to be raised.

| Member | Description |
|---|---|
| `bool modal` | Default true. The layer takes the input *under* it: its rectangle is the page (see `CoverPage()`), so a click that is not on something the layer itself put there lands on the layer and stops there. |
| `bool lightDismiss` | Default **false**. A click that misses `Body()`, or the window losing activation, calls `onDismiss`. **A flyout turns this on; a dialog does not** -- Windows draws the same line: `Popup` carries `IsLightDismissEnabled`, and `ContentDialog` has no property of the kind and can only be answered with a button. |
| `bool smoke` | Default false. Dims what is behind it, over black, by `smokeAlpha`. The same black the pane puts on a page it floats over. |
| `float smokeAlpha` | Default 0.14. The dim's own alpha: a dialog that wants to be the only thing on screen turns it up, a suggestion turns it down. |
| `bool escape` | Default true. Esc closes it -- the window offers Esc to the layer before the page's `OnCancel()`. A dialog with a job still running says false. |
| `std::function<void()> onDismiss` | What "closed" *means*, which is the page's: it clears its flag and lays out again. |
| `virtual D2D1_RECT_F Body() const` | The part of the layer that is the layer's own content. A click in `rect` outside it is a click on the dim. The default is the whole rectangle -- a layer with no dim, and so nothing to miss. |
| `virtual std::vector<Widget *> FocusRing()` | What Tab walks while the layer is open, in order. Empty leaves the page's own order alone, which is right for a layer that is only a picture. |
| `virtual Widget *DefaultButton()` | Enter, when nothing in the layer holds the focus. |
| `D2D1_RECT_F CoverPage() const` | What a layer covers: the page, and not the title bar above it. A page sets its layer's `rect` to this in `Layout()`. |
| `void Close()` | Asks the layer to go away, and the page's `onDismiss` runs **at once**: what the page keeps is its own state, and a page that has been told can lay itself out and put the focus somewhere sensible while the layer is still on its way out. The layer survives that layout and fades out; the window drops it when the fade is over. Calling it twice does nothing the second time. Ignored -- no fade at all -- with animations off. |
| `bool Leaving() const` | On its way out. The window takes no clicks while a layer is: what is being dismissed is not a place to be pressed again. |
| `bool HasLeft() const` | Leaving, and the fade has finished. The window sweeps these away after the frame's tick. |
| `float Arrival() const` | How far the layer has arrived: 0 as it comes up, 1 at rest, and on its way back down while it leaves. For a subclass that draws a panel of its own and wants it to come in on the same clock. |

**Add the layer first and what sits on it after it**, with a higher `z`: the hit test reaches the
last added of the raised controls while the painter orders by `z`, and this is the order that makes
the two agree.

```cpp
if (asking) {
    auto *box = new ConfirmLayer();              // your Layer subclass
    box->smoke = true;
    box->z = 1;
    box->onDismiss = [this] { asking = false; Layout(); };
    Layer *layer = Add(box);
    layer->rect = layer->CoverPage();

    // Close() first, then whatever the button is for: a page that lays itself out while the layer
    // is not yet leaving pulls the layer out before it can be seen to go.
    Button *ok = Add(new Button(L"OK", ButtonStyle::Accent, [this, box] { box->Close(); Apply(); }));
    ok->z = 2;                                   // over the layer, and after it in the list
    ok->rect = { ... };
    box->ring = { ok };                          // what its FocusRing() returns
    box->def  = ok;
}
```

Three things worth knowing before writing one:

- **A layer arrives and leaves by fading**, over `motion::kFast` (167 ms) in both directions, and
the dim fades with it. `kFast` rather than the `kNormal` a page or a flyout takes: those are
watched, and a panel is *read* -- it is legible well before the fade is over, and a quarter of a
second of dim creeping over the page is a quarter of a second of "not yet" for nothing. The curve
is `Decel`, so it is 0.9 of the way there about 90 ms in. It is one fade for the whole group: the
window paints everything over the page through a single opacity layer while it runs, because fading
each widget on its own would show the page through the gaps between them and come out darker where
two overlap. With animations off the layer is drawn whole on the first frame that asks for it and
gone on the next layout, with no fade and no invisible frame in between. **The controls the page put
on it leave with it**: they keep their places while the fade runs, so a button does not vanish from
under a panel that is still on screen, and none of them answers the pointer, the Tab ring, Enter or
Esc until it is gone.
- **The surface has to be opaque, and wants a shadow.** Painting the panel over the page in
`pal.cardBg` is exactly what the note over `pal.flyoutBg` warns about: a card is part of the page and
is meant to let it show through, so the page's own text reads through the dialog -- and the library
has no blur to hide it behind. `pal.flyoutBg` and `pal.flyoutStroke` are the colours for a surface
over the page, and `Painter::Shadow` is what puts it off the page: a dialog's numbers are wider and
softer than a flyout's, `p.Shadow(panel, radius, 1.0f, 26.0f, 8.0f, 16, 0.011f)`.
- **The caption bar stays live**, on purpose: the layer covers the page and not the title bar, so
dragging the window, double-clicking the caption and the Windows 11 snap-layout flyout all keep
working with a modal open. Windows' own modal dialogs keep their title bar too, and the smoke of a
`ContentDialog` does not reach past the client area either.

A drop-down's flyout is a raised control rather than a layer: it light-dismisses, but the page under
it is still the page. A layer is what a page reaches for when the *page* should not be reachable.
`Dialog` is this, ready made -- a title, a body, a footer of buttons, and an answer -- with the
buttons it adds itself: see [Controls](controls.md).

## Window state

| Member | Description |
|---|---|
| `HWND hwnd` | The window. |
| `UINT dpi` | Current DPI of the window. |
| `float scale() const` | `dpi / 96`. |
| `float ClientW() const`, `float ClientH() const` | Client size in DIPs. |
| `bool Visible() const` | Whether any of the window is on a screen: shown, not minimised, not cloaked. Occlusion is not part of it, and cannot be. |
| `Palette pal` | Current colors. See [Drawing](drawing.md#palette). |
| `Fonts fonts` | Text formats. See [Drawing](drawing.md#fonts). |
| `bool micaActive` | True when DWM accepted the backdrop that was asked for. False on Windows 10 and Windows 11 before 22H2; the page then has an opaque background. |
| `DWORD backdrop` | Which system backdrop to ask DWM for: `kDwmBackdropAuto`, `kDwmBackdropNone`, `kDwmBackdropMainWindow` (Mica, the default), `kDwmBackdropAcrylic` or `kDwmBackdropTabbed` (Mica Alt). Set it before `Create`; a page that switches material at run time sets it and calls `ApplyThemeToFrame()`. |
| `bool resizable` | As passed to `Create`. |
| `bool active` | Whether the window is the active window. The title bar dims when it is not. |
| `bool animOn` | True while the frame loop is running. |
| `void Invalidate()` | Requests a repaint. |

## Theme

| Member | Description |
|---|---|
| `void ApplyThemeToFrame()` | Applies `pal.dark` to the frame: DWM dark mode, rounded corners, Mica. Call after replacing `pal`. |
| `void ReloadTheme()` | Rebuilds `pal` from `DarkTheme()` and the accent, applies it and repaints. Runs automatically when Windows says the colours changed. |
| `void Theme(ThemeMode)` | Which theme the window paints in: `Auto` (the default, following the machine), `Light` or `Dark`. Set it before `Create` and the window comes up in it; set it while one is up and call `ReloadTheme()`. |
| `ThemeMode ThemeSetting()` | Which of the three it is in. For a settings page's initial selection. |
| `bool DarkTheme()` | What that resolves to right now -- the machine's answer under `Auto`, and the program's otherwise. **`ReloadTheme()` goes through this**, so a program that has said Dark stays dark when Windows announces that the colours changed. |

A window can use its own theme:

```cpp
micula::Theme(micula::ThemeMode::Dark);
ReloadTheme();                             // rebuild the palette and repaint
```

Setting `pal` yourself and calling `ApplyThemeToFrame()` still works, and it is what a page with a
palette of its own -- one that is not `MakePalette` -- should do. What it does **not** survive is a
system theme change: `ReloadTheme()` runs on that message, and without a `ThemeMode` there is nothing
for it to know that this program had an opinion.

## Pictures

| Member | Description |
|---|---|
| `ID2D1Bitmap1 *Image(const std::wstring &path, UINT maxW)` | Decodes an image file through WIC, scaled down to at most `maxW` pixels wide, and caches it by path. The first call's `maxW` is the one used. Returns null if the file cannot be read, and caches that too. The window owns the bitmap. It is released when the Direct2D device is lost, so don't keep the pointer beyond one paint. Needs COM. |

## Messages

| Input | What the window does |
|---|---|
| Mouse move | Sets `hover` on the control under the pointer, and sends `OnPointerMove` to it and to any control whose `ExternalRegion` the pointer is over. The control holding capture gets `pressed` (while the pointer is over it) and `OnDrag`. |
| Left button down | Every other control gets `Dismiss()`. The control under the pointer takes capture, `pressed`, focus if it is focusable, and `OnPress`. A click on nothing clears focus. |
| Left button up | The captured control gets `OnRelease`, then `OnClick` if the pointer is still over it. |
| Capture lost | `WM_CAPTURECHANGED` or `WM_CANCELMODE` -- alt-tab, a system modal, another application taking the mouse: the drag in progress gets `OnRelease` and no `OnClick`, and the capture is cleared. A button's release goes to whoever holds the capture, so a drag that loses it is ended here rather than never ending. |
| Wheel | The control under the pointer gets `OnWheel`. If it returns false, a control floating over the page -- one with a `z` -- is offered it. If none takes it, the page gets `OnAppMessage(WM_MOUSEWHEEL, wp, lp)` with `lp` holding the pointer in client pixels, and only when the pointer is over the page: the clip's width, from the caption bar down. A wheel over the pane's rail or the caption bar is taken and scrolls nothing. |
| Key down | The focused control's `OnKey` first. If it returns false: Tab and Shift+Tab move focus, Space activates the focused control (`OnActivate()`), Enter activates it or calls `OnDefaultAction()`, Esc calls `OnCancel()`. |
| Characters | `WM_CHAR` and `WM_IME_CHAR` go to the focused control's `OnChar`. Control characters are dropped. |
| `WM_TIMER` | Offered to the timers this window is running, by id -- see `Timer` in [Widgets](widget.md#timers). An id none of them took is a page's own, and reaches `OnAppMessage`. |
| Deactivation | Every control gets `Dismiss()`, `hover` is cleared, and a drag in progress gets its `OnRelease`. |
| Resize, DPI change | `Layout()`. |
| Drag of the border or the caption | `WM_ENTERSIZEMOVE` and `WM_EXITSIZEMOVE`. Windows runs a modal loop of its own, in which the frame loop cannot run, so the window paints from a 16 ms timer for the duration: the resize is live and animations keep running. |
| Scroll | Nothing is laid out: the controls move through `ContentTransform()` and the frame loop repaints them. |
| Settings change | `ReloadTheme()` when the machine switched between light and dark, and the animation switch is re-read -- see [Animations](drawing.md#animations). |

Messages not consumed reach `OnAppMessage` and then `DefWindowProc`: `WM_CLOSE`,
`WM_COMMAND`, `WM_APP` messages, `WM_ACTIVATE`, right and middle mouse buttons, timers
and wheel notches no control took. Mouse and keyboard messages in the table above,
`WM_SETTINGCHANGE`, `WM_SIZE` and `WM_PAINT` are consumed.

## Scrolling

Scrolling is a transform, not a layout. Return the scrolling area from `ClipRect()`, set
`scrolls` on the controls inside it, lay them out in the **page's** coordinates -- as though
the page had never been scrolled -- and move them by returning an offset from
`ContentTransform()`, glided on the frame loop:

```cpp
struct List : micula::Window {
    static constexpr float kTop = 80, kRow = 44;
    bool checked[30] = {};
    float scroll = 0, drawn = 0, maxScroll = 0;   // where it is, where it is drawn, its end
    micula::ScrollBar *bar = nullptr;

    const wchar_t *ClassName() const override { return L"MyApp.List"; }
    const wchar_t *Title() const override { return L"List"; }
    D2D1_RECT_F ClipRect() const override { return { 0, kTop, ClientW(), ClientH() }; }
    void ContentTransform(float *dy, float *opacity) const override {
        *dy = -drawn;                            // scrolled down: drawn higher up
        *opacity = 1.0f;
    }
    // The glide is the page's own animation; this is how the loop knows to run frames.
    bool AnimationWanted() const override { return drawn != scroll; }
    void OnTick(float dt) override {
        if (drawn == scroll) return;
        drawn += (scroll - drawn) * (1.0f - std::exp(-dt / 0.07f));
        if (std::fabs(scroll - drawn) < 0.5f) drawn = scroll;
        if (bar) { bar->value = scroll; bar->drawn = drawn; }
    }
    // What the wheel, the bar and its arrows call: a target, not a rebuild.
    void ScrollTo(float to, bool glide = true) {
        scroll = std::clamp(to, 0.0f, maxScroll);
        if (!glide) drawn = scroll;               // the thumb has to stay under the pointer
        if (bar) { bar->value = scroll; bar->drawn = drawn; bar->Wake(); bar->Poll(); }
        if (drawn != scroll) micula::StartAnimation(this);
        Invalidate();
    }

    void Layout() override {
        ClearWidgets();                              // keeps `bar`: it is persistent
        for (int i = 0; i < 30; i++) {
            auto *cb = Add(new micula::CheckBox(L"Item", checked[i],
                                                [this, i](bool v) { checked[i] = v; }));
            cb->rect = micula::Rect(24, kTop + i * kRow, 300, 32);   // page coordinates
            cb->scrolls = true;
        }
        const float viewport = ClientH() - kTop, extent = 30 * kRow;
        maxScroll = extent > viewport ? extent - viewport : 0;
        if (!bar) {
            bar = Add(new micula::ScrollBar([this](float to, bool glide) { ScrollTo(to, glide); }));
            bar->persistent = true;
        }
        bar->rect = { ClientW() - micula::ScrollBar::kSize - 1, kTop, ClientW() - 1, ClientH() };
        bar->area = ClipRect();
        bar->viewport = viewport;
        bar->extent = extent;
        bar->value = scroll;
        bar->drawn = drawn;
        bar->visible = maxScroll > 0;
    }

    bool OnAppMessage(UINT m, WPARAM wp, LPARAM) override {
        if (m != WM_MOUSEWHEEL) return false;
        ScrollTo(scroll - GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA * 66);
        return true;
    }
};
```

Nothing above lays the page out while it is being scrolled. The controls' rectangles are
worked out when the shape of the page changes, and a wheel notch costs one transform and one
repaint -- so everything a control is holding survives a scroll, and is lost only to a
`Layout()`: an open drop-down, a selection mid-drag, an animation that has not finished.

`examples/gallery` is a complete scrolling page. Three things in the sample are on purpose:

- Coordinates. The controls, and whatever `PaintPage()` draws, are in page coordinates.
  `PaintPage()` is not transformed, so it takes the offset off its own drawing by hand
  (`y - drawn`).
- `Widget::VisibleArea()` is `ClipRect()` in that same space, for a control that has to know
  how much room it really has -- a drop-down deciding whether its list fits below it.
- The follower in `OnTick`, rather than `motion::Track` or `motion::Ramp`: a `Track` runs for
  a duration, and a wheel spun through five notches restarts it five times inside one frame;
  `Ramp` advances by a rate in value units and is for 0..1 fades, so it cannot carry a
  distance in DIPs.

## Free functions and constants

| Name | Description |
|---|---|
| `void EnablePerMonitorDpi()` | Sets per-monitor DPI awareness v2, or the best the system has. Call before creating a window. |
| `std::wstring ClipboardText(HWND owner)` | The clipboard's Unicode text, or empty. |
| `void SetClipboardText(HWND owner, const std::wstring &s)` | Replaces the clipboard's contents with `s`. |
| `void StartAnimation(Window *w)` | Wakes the message loop so it checks for animation. Only needed when a control starts animating outside a message. |
| `double MonotonicSeconds()` | Seconds since the process started, monotonic. For an animation that is periodic and holds nothing else, so that a control rebuilt by a layout does not restart it. |
| `bool Post(Window *w, std::function<void()> fn)` | Runs `fn` on the thread that owns `w`, once the message in hand is finished. Safe to call from any thread; false if the window is already gone. See [Threads](#threads). |
| `struct PostSlot` | A place for a worker to post from, one call at a time: `slot.Post(w, fn)` drops the call when one is already pending. See [Threads](#threads). |
| `kCaptionH` | 32. Title bar height in DIPs. |
| `kCaptionBtnW` | 46. Width of each title bar button in DIPs. |
| `kResizeGrip` | 6. Width of the resize border in DIPs. |

## Threads

The window and everything in it belong to the thread that made it. Nothing in Micula
synchronises anything, so a control touched from another thread is a race and not a
slower way of doing it.

What a worker thread has instead is `Post`, which hands the window a callable to run in its
own turn:

```cpp
// On the worker. Nothing here is locked, and nothing has to be polled.
Post(&page, [&page, snapshot] { page.ShowState(snapshot); });
```

The callback runs between two messages, with the same rights a control's callback has: it
may lay the page out again, add controls, invalidate. What it may not do is block -- it is
holding the window's thread.

`PostSlot` is for a worker with more news than the window needs. One call is pending at a
time and the rest are dropped, so a thousand updates cost one turn:

```cpp
std::atomic<float> progress;   // the worker's, the page only ever reads it
PostSlot slot;

// On the worker, as often as it likes.
progress.store(done);
slot.Post(&page, [&page, &progress, &slot] {
    page.SetProgress(progress.load());   // reads the latest, does not carry a change
});
```

That is the whole contract: a call that is dropped is a change that is never reported, so
the callable reads the state rather than carrying a snapshot of it. The state stays on the
worker's side of the boundary and the page only ever sees what it asks for.

A call posted to a window that is destroyed before it runs never runs, and the window frees
it on the way out; `Post` answers false if the window is already gone.

## Internals

Public because `Window` is a struct, but not part of the interface: `Paint`, `Reaches`,
`Resize`, `Frame`, `Tick`, `Animating`, `RefreshHover`, `HitTest`, `CaptionHitTest`,
`PaintCaption`, `MeasureFrame`, `CreateDevice`, `ReleaseDevice`, `Proc`, the Direct2D
and DirectComposition pointers (`dw`, `d3d`, `dxgi`, `d2d`, `d2dDevice`, `dc`, `swap`,
`target`, `comp`, `compTarget`, `compVisual`, `brush`), and the `dpiapi` and `frameclock`
namespaces.
