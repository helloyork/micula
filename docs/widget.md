# Custom controls

`struct micula::Widget`, in `micula/window.h`. Every control derives from it. A new
control overrides `Paint` and the input hooks it needs.

```cpp
// A color swatch that can be picked.
struct Swatch : micula::Widget {
    D2D1_COLOR_F color;
    bool picked = false;
    std::function<void()> onPick;

    Swatch(D2D1_COLOR_F c, bool on, std::function<void()> f)
        : color(c), picked(on), onPick(std::move(f)) {}

    bool Focusable() const override { return true; }
    bool HandCursor() const override { return true; }
    void OnClick() override { if (enabled && onPick) onPick(); }

    void Paint(const micula::Painter &p) override {
        const micula::Palette &c = *p.pal;
        p.FillRound(rect, micula::metric::kRadiusControl, color);
        // hoverT runs from 0 to 1 as the pointer arrives.
        p.StrokeRound(rect, micula::metric::kRadiusControl,
                      micula::Mix(c.controlStroke, c.textSecondary, hoverT));
        if (picked) p.StrokeRound(rect, micula::metric::kRadiusControl, c.accent, 2.0f);
        if (focus && owner && owner->showFocusRing) {
            const D2D1_RECT_F ring = { rect.left - 3, rect.top - 3, rect.right + 3, rect.bottom + 3 };
            p.StrokeRound(ring, micula::metric::kRadiusControl + 3, c.textPrimary, 2.0f);
        }
    }
};
```

## Fields

| Field | Description |
|---|---|
| `D2D1_RECT_F rect` | Position in window DIPs. For a scrolling control, the laid-out position before `ContentTransform`. |
| `bool visible` | Hidden controls are not painted and take no input. |
| `bool enabled` | Disabled controls are painted but take no input. |
| `bool hover`, `pressed`, `focus` | Set by the window. `pressed` is true while the mouse button that went down on this control is held and the pointer is over it. |
| `float hoverT`, `pressT`, `focusT` | The same three states, faded from 0 to 1 over 83 ms by `Widget::Tick`. Use them for the background, as WinUI does. |
| `int z` | 0 normally. 1 paints above the rest of the page and hit-tests before it -- an open drop-down. 2 paints above the page altogether, which is a `SideNav` while it is open over one. |
| `Window *owner` | Set by `Window::Add`. |
| `bool scrolls` | Part of the scrolling area: `rect` is in the page's own coordinates, and the control is clipped to `ClipRect()` and moved by `ContentTransform()`. |
| `bool persistent` | Survives `ClearWidgets()`, together with focus or capture it holds. |

## Hooks

All virtual. Points are in DIPs.

| Hook | Default | Called |
|---|---|---|
| `void Paint(const Painter &p)` | required | Every paint while visible. |
| `bool Focusable() const` | `false` | Return true to take focus by click and Tab. |
| `void OnClick()` | nothing | Button released over the control. A click is the pointer: the point it was released at is the point it means. |
| `void OnActivate()` | `OnClick()` | Space, and Enter while the control is focused. Separate from `OnClick` because the keyboard has no pointer to read a position out of -- the default is to treat it as a click. |
| `void OnPress(float x, float y)` | nothing | Button pressed on the control, at that point. |
| `void OnDrag(float x, float y)` | nothing | Pointer moved while this control holds capture. |
| `void OnRelease()` | nothing | Button released after `OnPress`, wherever the pointer is. Before `OnClick`. |
| `void OnPointerMove(float x, float y)` | nothing | Pointer moved over the control, or over the region it declares in `ExternalRegion`. |
| `D2D1_RECT_F ExternalRegion() const` | empty | The area outside `rect`, in the control's own space, where this control also wants `OnPointerMove`. Moves only: it does not widen what a click can hit. |
| `bool OnWheel(float x, float y, float notches)` | `false` | Wheel over the control. `notches` is positive away from the user. Return true to keep it from the page. |
| `bool OnKey(WPARAM vk)` | `false` | Key down while focused. Return true to consume it. |
| `bool OnChar(wchar_t ch)` | `false` | Typed character while focused. |
| `void OnBlur()` | nothing | Focus left the control. |
| `bool OnTimer(UINT_PTR id)` | `false` | A timer the window does not own. Return true if the id is this control's. |
| `void Dismiss()` | nothing | A click elsewhere, or the window was deactivated. Close anything transient. |
| `bool TracksPointer() const` | `false` | Return true to repaint on every pointer move over the control, not only when `hover` changes. For a hover highlight inside the control. |
| `bool PressedVisual() const` | `pressed` | Return true while the press shadow should show. `pressed` is cleared as soon as the pointer leaves the control -- which is what makes a button cancellable by dragging off it -- so a drag that outlives its own rectangle, such as a slider past the end of its track, overrides this. |
| `bool HandCursor() const` | `false` | Show the hand cursor. |
| `bool TextCursor() const` | `false` | Show the I-beam cursor. |
| `bool CaretPoint(D2D1_POINT_2F *out) const` | `false` | For text input: return true and the caret position, where the IME window opens. `out` may be null. |
| `bool Animating() const` | fading | Return true while the control still has something to animate. |
| `void Tick(float dt)` | fades the three states | Every animation frame. |

Points passed to `OnPress`, `OnDrag`, `OnPointerMove` and `OnWheel` are in the control's
own space: for a scrolling control the `ContentTransform` offset is already taken off,
so they compare directly with `rect`.

## Helpers

| Member | Description |
|---|---|
| `D2D1_POINT_2F Cursor() const` | The pointer now, in the control's own space. |
| `D2D1_RECT_F VisibleArea() const` | The part of the page that is on screen, in the control's own space: `ClipRect()` through the same offset. Empty when the page does not scroll. |
| `float Want(bool on) const` | 1 if `on` and enabled, else 0. The target for a state fade. |

## Animating a control

Keep an animated value in a `motion::Track`, report it in `Animating()` and advance it in
`Tick()`. Call the base versions so the hover fades keep working:

```cpp
struct Expander : micula::Widget {
    bool open = false;
    micula::motion::Track openT;   // 0 closed, 1 open

    bool Animating() const override {
        return Widget::Animating() || openT.Wants(open ? 1.0f : 0.0f);
    }
    void Tick(float dt) override {
        Widget::Tick(dt);
        openT.To(open ? 1.0f : 0.0f);
        openT.Step(dt, micula::motion::kNormal);
    }
    void OnClick() override { open = !open; }
    void Paint(const micula::Painter &p) override {
        const float h = micula::Height(rect) * (0.5f + 0.5f * openT.value);
        p.FillRound({ rect.left, rect.top, rect.right, rect.top + h },
                    micula::metric::kRadiusControl, p.pal->cardBg);
    }
};
```

Use `Wants()` rather than `Moving()` in `Animating()`: between a click and the next frame
the new target has not been set with `To()` yet, and `Moving()` would let the window stop
drawing frames.

## Timers

A control that needs a timer calls `SetTimer(owner->hwnd, id, ms, nullptr)` and claims the
id in `OnTimer`. Micula uses ids 2 to 7. Kill the timer in the destructor if the
control can be removed while it runs.
