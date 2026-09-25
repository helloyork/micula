# Micula documentation

| Page | Contents |
|---|---|
| [Window](window.md) | `Window`: creating a window, page callbacks, input, scrolling |
| [Controls](controls.md) | The controls |
| [Custom controls](widget.md) | `Widget`, the base class of every control |
| [Drawing](drawing.md) | `Painter`, palette, fonts, metrics, icons, animation helpers |

## Headers

| Header | Contents |
|---|---|
| `micula/micula.h` | The one to include. Includes the others and defines `MICULA_VERSION_MAJOR`, `_MINOR`, `_PATCH` and `MICULA_VERSION_STRING`. |
| `micula/theme.h` | Palette, fonts, metrics, icon code points, animation curves, system theme queries |
| `micula/window.h` | `Window`, `Widget`, `Painter`, clipboard and DPI helpers |
| `micula/widgets.h` | The controls |

Everything is in namespace `micula`.

## Concepts

### The page is rebuilt, not updated

A window keeps its state in its own fields. `Layout()` removes the controls and creates
them again from those fields. It runs when the window is created, resized or moved to a
monitor with another DPI, and whenever the page calls it.

- A control's callback writes the page's field. The control itself is discarded by the
  next `Layout()`.
- Don't keep a pointer to a control across `Layout()` unless the control is
  `persistent`.
- `Layout()` may be called from inside a control's callback. Controls removed while a
  message is being handled are destroyed after it returns.

Scrolling is not a rebuild. A scrolling page moves its controls with `ContentTransform()`
and never calls `Layout()` for a wheel notch, so nothing a control is holding -- focus, a
selection, an animation -- is lost on the way. See [Window](window.md).

### Coordinates

All positions and sizes are DIPs (1/96 inch). The render target carries the DPI, so
nothing is scaled by hand. `Window::scale()` converts DIPs to pixels for Win32 calls
that need pixels.

The title bar is the top `kCaptionH` (32) DIPs of the client area. It is painted after
the page, over anything drawn there.

### Painting order

1. Background: transparent over Mica, `pal.windowBg` without it.
2. `Window::PaintPage()`.
3. Controls with `z == 0`, then `z == 1` (an open drop-down), then `z == 2` (a navigation
   pane open over the page). Within each, controls that don't scroll come first, then
   scrolling controls, clipped to `ClipRect()` and offset by `ContentTransform()`.
4. The title bar.

Within a group, controls are painted in the order they were added. `z` orders the
painting and not the hit test, which asks only whether a control is raised at all.

### Animation

While nothing moves, the window waits in `GetMessage` and uses no CPU. When a control's
`Animating()` or the page's `AnimationWanted()` returns true, it runs a frame loop until
both are false. Each frame calls `Tick(dt)` on every control and `OnTick(dt)` on the
page, then paints. Frames follow the compositor clock on Windows 11 and a
high-resolution timer at the display's refresh rate on Windows 10.

A drag of the window's border or its caption runs in a modal loop of Windows' own, in
which that frame loop gets no turn. The window stands in for it with a 16 ms timer until
the drag is over, so a resize is live and whatever was animating keeps running.

### Requirements

- COM initialized on the UI thread, apartment-threaded, before `Window::Create`.
- Per-monitor DPI awareness, from the manifest or `EnablePerMonitorDpi()`.
- a page's own timers stay its own: the window hands its timers out of a pool, and an id the pool did not take reaches `OnAppMessage`.
- Call everything from the thread that created the window.
