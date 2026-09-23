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

## Page callbacks

All virtual. `ClassName()` and `Title()` must be overridden.

| Callback | Default | When |
|---|---|---|
| `const wchar_t *ClassName() const` | | Once, in `Create`. The window class name; unique per window type. |
| `const wchar_t *Title() const` | | Window title, also drawn in the title bar. |
| `void Layout()` | nothing | In `Create`, on resize and on DPI change. Call it yourself when the page changes shape. Start with `ClearWidgets()`. |
| `void PaintPage(const Painter &p)` | nothing | Every paint, before the controls. Draws what is not a control: headings, card backgrounds, labels. |
| `void OnDefaultAction()` | nothing | Enter, when no control has focus. |
| `void OnCancel()` | posts `WM_CLOSE` | Esc. |
| `void OnTick(float dt)` | nothing | Every animation frame. `dt` is seconds since the last frame, at most 0.1. |
| `bool AnimationWanted() const` | `false` | Before every frame. Return true to keep frames coming for an animation of the page's own. |
| `bool OnAppMessage(UINT m, WPARAM wp, LPARAM lp)` | `false` | Messages the window does not consume. Return true if handled. See [Messages](#messages). |
| `D2D1_RECT_F ClipRect() const` | empty | The scrolling part of the page. Controls with `scrolls` set are clipped to it and take input only inside it. Empty means the page does not scroll. |
| `void ContentTransform(float *dy, float *opacity) const` | `0`, `1` | Vertical offset and opacity for the scrolling controls while painting. Hit testing subtracts the same offset. For a scroll that glides or a page that fades in. `PaintPage` is not transformed. |
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

## Window state

| Member | Description |
|---|---|
| `HWND hwnd` | The window. |
| `UINT dpi` | Current DPI of the window. |
| `float scale() const` | `dpi / 96`. |
| `float ClientW() const`, `float ClientH() const` | Client size in DIPs. |
| `Palette pal` | Current colors. See [Drawing](drawing.md#palette). |
| `Fonts fonts` | Text formats. See [Drawing](drawing.md#fonts). |
| `bool micaActive` | True when DWM accepted the Mica backdrop. False on Windows 10 and Windows 11 before 22H2; the page then has an opaque background. |
| `bool resizable` | As passed to `Create`. |
| `bool active` | Whether the window is the active window. The title bar dims when it is not. |
| `bool animOn` | True while the frame loop is running. |
| `void Invalidate()` | Requests a repaint. |

## Theme

| Member | Description |
|---|---|
| `void ApplyThemeToFrame()` | Applies `pal.dark` to the frame: DWM dark mode, rounded corners, Mica. Call after replacing `pal`. |
| `void ReloadTheme()` | Rebuilds `pal` from the system theme and accent, applies it and repaints. Runs automatically when Windows switches between light and dark. |

A window can use its own theme:

```cpp
pal = micula::MakePalette(true);   // dark
ApplyThemeToFrame();
Invalidate();
```

The next system theme change replaces it through `ReloadTheme()`.

## Pictures

| Member | Description |
|---|---|
| `ID2D1Bitmap1 *Image(const std::wstring &path, UINT maxW)` | Decodes an image file through WIC, scaled down to at most `maxW` pixels wide, and caches it by path. The first call's `maxW` is the one used. Returns null if the file cannot be read, and caches that too. The window owns the bitmap. It is released when the Direct2D device is lost, so don't keep the pointer beyond one paint. Needs COM. |

## Messages

| Input | What the window does |
|---|---|
| Mouse move | Sets `hover` on the control under the pointer. The control holding capture gets `pressed` (while the pointer is over it, or throughout if it returns true from `DragsOutsideSelf`) and `OnDrag`. Every control gets `OnPointerMove`. |
| Left button down | Every other control gets `Dismiss()`. The control under the pointer takes capture, `pressed`, focus if it is focusable, and `OnPress`. A click on nothing clears focus. |
| Left button up | The captured control gets `OnRelease`, then `OnClick` if the pointer is still over it and it did not own the drag (`DragsOutsideSelf`). |
| Capture lost | A gesture cut short by alt-tab, a system modal or another application taking the mouse: the captured control gets `OnRelease` and no `OnClick`. |
| Wheel | The control under the pointer gets `OnWheel`. If it returns false, the page gets `OnAppMessage(WM_MOUSEWHEEL, wp, lp)` with `lp` holding the pointer in client pixels. |
| Key down | The focused control's `OnKey` first. If it returns false: Tab and Shift+Tab move focus, Space clicks the focused control, Enter clicks it or calls `OnDefaultAction()`, Esc calls `OnCancel()`. |
| Characters | `WM_CHAR` and `WM_IME_CHAR` go to the focused control's `OnChar`. Control characters are dropped. |
| `WM_TIMER` | Timer 2 blinks the caret. Other ids go to each control's `OnTimer` in order, then to `OnAppMessage`. |
| Deactivation | Every control gets `Dismiss()`, and `hover` is cleared. |
| Resize, DPI change | `Layout()`. |
| Light/dark change | `ReloadTheme()`. |

Messages not consumed reach `OnAppMessage` and then `DefWindowProc`: `WM_CLOSE`,
`WM_COMMAND`, `WM_APP` messages, `WM_ACTIVATE`, right and middle mouse buttons, timers
and wheel notches no control took. Mouse and keyboard messages in the table above,
`WM_SETTINGCHANGE`, `WM_SIZE` and `WM_PAINT` are consumed.

## Scrolling

Return the scrolling area from `ClipRect()`, lay controls out at `y - scroll` with
`scrolls` set, and keep one `ScrollBar` across layouts:

```cpp
struct List : micula::Window {
    static constexpr float kTop = 80, kRow = 44;
    bool checked[30] = {};
    float scroll = 0, maxScroll = 0;
    micula::ScrollBar *bar = nullptr;

    const wchar_t *ClassName() const override { return L"MyApp.List"; }
    const wchar_t *Title() const override { return L"List"; }
    D2D1_RECT_F ClipRect() const override { return { 0, kTop, ClientW(), ClientH() }; }

    void Layout() override {
        ClearWidgets();                              // keeps `bar`: it is persistent
        for (int i = 0; i < 30; i++) {
            auto *cb = Add(new micula::CheckBox(L"Item", checked[i],
                                                [this, i](bool v) { checked[i] = v; }));
            cb->rect = micula::Rect(24, kTop + i * kRow - scroll, 300, 32);
            cb->scrolls = true;
        }
        const float viewport = ClientH() - kTop, extent = 30 * kRow;
        maxScroll = extent > viewport ? extent - viewport : 0;
        if (!bar) {
            bar = Add(new micula::ScrollBar([this](float to, bool) {
                scroll = to;
                Layout();
                Invalidate();
            }));
            bar->persistent = true;
        }
        bar->rect = { ClientW() - micula::ScrollBar::kSize - 1, kTop, ClientW() - 1, ClientH() };
        bar->area = ClipRect();
        bar->viewport = viewport;
        bar->extent = extent;
        bar->value = bar->drawn = scroll;
        bar->visible = maxScroll > 0;
    }

    bool OnAppMessage(UINT m, WPARAM wp, LPARAM) override {
        if (m != WM_MOUSEWHEEL) return false;
        scroll -= GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA * 66;
        scroll = scroll < 0 ? 0 : scroll > maxScroll ? maxScroll : scroll;
        Layout();
        bar->Wake();
        bar->Poll();
        Invalidate();
        return true;
    }
};
```

`examples/gallery` is a complete scrolling page. For a scroll that glides, keep the
target in `scroll`, animate a drawn position in `OnTick`, and return the difference from
`ContentTransform`.

## Free functions and constants

| Name | Description |
|---|---|
| `void EnablePerMonitorDpi()` | Sets per-monitor DPI awareness v2, or the best the system has. Call before creating a window. |
| `std::wstring ClipboardText(HWND owner)` | The clipboard's Unicode text, or empty. |
| `void SetClipboardText(HWND owner, const std::wstring &s)` | Replaces the clipboard's contents with `s`. |
| `void StartAnimation(Window *w)` | Wakes the message loop so it checks for animation. Only needed when a control starts animating outside a message. |
| `kCaptionH` | 32. Title bar height in DIPs. |
| `kCaptionBtnW` | 46. Width of each title bar button in DIPs. |
| `kResizeGrip` | 6. Width of the resize border in DIPs. |
| `kCaretTimer` | 2. The caret's timer id. |

## Internals

Public because `Window` is a struct, but not part of the interface: `Paint`, `Resize`,
`Frame`, `Tick`, `Animating`, `RefreshHover`, `HitTest`, `CaptionHitTest`,
`PaintCaption`, `MeasureFrame`, `CreateDevice`, `ReleaseDevice`, `Proc`, the Direct2D
and DirectComposition pointers (`dw`, `d3d`, `dxgi`, `d2d`, `d2dDevice`, `dc`, `swap`,
`target`, `comp`, `compTarget`, `compVisual`, `brush`), and the `dpiapi` and `frameclock`
namespaces.
