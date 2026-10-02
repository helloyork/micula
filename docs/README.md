# Micula documentation

| Page | Contents |
|---|---|
| [Window](window.md) | `Window`: creating a window, page callbacks, input, scrolling |
| [Layout](layout.md) | `Widget`, the tree, layouts, scrolling, motion |
| [Controls](controls.md) | The controls |
| [Custom controls](widget.md) | `Widget`, the base class of every control |
| [Drawing](drawing.md) | `Painter`, palette, fonts, metrics, icons, animation helpers |

## Headers

| Header | Contents |
|---|---|
| `micula/micula.h` | The one to include. Includes the others and defines `MICULA_VERSION_MAJOR`, `_MINOR`, `_PATCH` and `MICULA_VERSION_STRING`. |
| `micula/theme.h` | Palette, fonts, metrics, animation curves, the theme mode, system queries |
| `micula/glyphs.h` | The Segoe Fluent Icons code points, and the place a page adds the ones it needs |
| `micula/layout.h` | The layout protocol: `Layout`, `Sizing`, `Want`, `Room`, and the numbers a settings page is made of |
| `micula/widget.h` | `Widget`: the node a tree is made of, and what it says about itself |
| `micula/view.h` | A box with children in it and nothing of its own, which is what a page is |
| `micula/window.h` | `Window`, `Surface`, `Popup`, `Layer`, `App`, `Painter`, `Timer`, clipboard, DPI helpers and `Post` |
| `micula/widgets.h` | The controls and the things a page is built out of: it includes the per-control headers below |

Every control has a header of its own, so a page can include the one it draws and parse nothing
else: `button.h`, `check_box.h`, `toggle_switch.h`, `segmented.h`, `slider.h`, `text_box.h`,
`progress_bar.h`, `progress_ring.h`, `card.h`, `text.h`, `scroll_bar.h`, `scroll_view.h`,
`drop_down.h`, `flyout.h`, `side_nav.h`, `navigation_view.h`, `dialog.h`, `popup.h`, `tip.h`,
`menu.h`, `stack_layout.h`, `row_layout.h` and `custom_layout.h`.

Everything is in namespace `micula`.

## Concepts

### A page is a tree, and it is updated rather than rebuilt

A window keeps its state in its own fields, and the page is a widget that owns the widgets
under it (`widget.h`). A control is created once and stays where it was added; what changes
is a field, a child added or removed, or `visible`. Nothing is thrown away to change what is
on screen, so a half-typed field, the focused button and a slider mid-drag stay exactly where
they were -- there is no `Layout()` to call and no `persistent` to write.

- `Add` gives a widget a place in the tree, and `Remove` takes it out. A callback may do
  either: a node removed while a message is being handled is destroyed after it returns.
- A widget's rectangle is in its parent's space, and its parent's layout is what decided it.
  Writing a field says nothing about geometry -- `InvalidateLayout()` marks the tree dirty,
  and the window arranges it once, at one point in the frame, before it paints.
- A page of rows and cards is a `StackLayout` of `Card`s; one with arithmetic of its own keeps
  it in a `CustomLayout`. See [Layout](layout.md).

### Scrolling

A container whose arranged children do not fit scrolls them: it keeps the offset, takes the
wheel, holds a `ScrollBar` of its own, and clips its children to itself. `ScrollView` is that
container and it is what a page is built with. A wheel notch changes a number in the
container -- nothing is rebuilt, so focus, a selection and an animation are not on the way
anywhere. See [Layout](layout.md#scrolling-belongs-to-the-container).

### Coordinates

All positions and sizes are DIPs (1/96 inch). The render target carries the DPI, so
nothing is scaled by hand. `Window::scale()` converts DIPs to pixels for Win32 calls
that need pixels.

The title bar is the top `kCaptionH` (32) DIPs of the client area. It is painted after
the page, over anything drawn there.

### Painting order

1. Background: transparent over Mica, `pal.windowBg` without it.
2. The tree, in one walk from the root: a widget paints itself and then its children, in the
   order they were added. A widget that is a window onto its children clips them to itself
   (`Clips`, `ClipBox`), which is what a scrolling container and a panel being uncovered are.
3. The title bar, last of all, so a page drawn to the top of its own area cannot run under it.

Hit testing walks the same children in reverse -- the widget drawn last is the one asked
first -- and the order is also the Tab order and the order a screen reader reads.

### Animation

While nothing moves, the window waits in `GetMessage` and uses no CPU. A frame is run while
anything under it is animating (`Animating()`), while the page says it wants one
(`AnimationWanted()`), and while the tree owes an arrangement (`InvalidateLayout()`) -- so a
change made by the message just handled becomes geometry and then paint rather than waiting
for something else to happen to paint it. Each frame arranges if the tree is dirty, calls
`Tick(dt)` down the tree and `OnTick(dt)` on the page, then paints. Frames follow the
compositor clock on Windows 11 and a high-resolution timer at the display's refresh rate on
Windows 10.

A drag of the window's border or its caption runs in a modal loop of Windows' own, in
which that frame loop gets no turn. The window stands in for it with a 16 ms timer until
the drag is over, so a resize is live and whatever was animating keeps running.

Whether anything animates at all is one switch, `micula::Animations()`, which follows
Windows unless the program says otherwise -- see [Animations](drawing.md#animations). A
window nobody can see runs no frames, whatever is animating in it.

### Requirements

- Call everything from the thread that created the window.
- a page's own timers stay its own: the window hands its timers out of a pool, and an id the pool did not take reaches `OnAppMessage`.
- And nothing else. The COM apartment a window needs -- WIC for the caption icon and
  `Window::Image`, and UI Automation -- is opened by `Window::Create` on the thread that makes the
  window, and per-monitor DPI awareness is set there too, but only for a process that has not said
  anything about it yet: a manifest, or `EnablePerMonitorDpi()` by the program, wins. A program
  that uses COM itself still initialises it -- the apartment is then the program's, and the library
  leaves it alone.
