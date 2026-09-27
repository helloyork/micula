# Changelog

Notable changes, by release. The format is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the versions are [Semantic Versioning](https://semver.org/spec/v2.0.0.html) as far as a 0.x
library can be: a minor version may break.

## [Unreleased]

### Added

- **`Dialog`.** A question with buttons on it: a title, a body, a footer of buttons on a surface of
  its own under a separator line, a border and a shadow, over the layer below it. The buttons are the
  dialog's own -- a page adds the dialog and nothing else, which is what the new `Widget::OnAdded`
  hook is for -- and `onResult` arrives with the number the page gave the button that was pressed,
  after the dialog has started leaving, so that laying the page out there cannot take the panel off
  the screen mid-fade. Esc reports `cancelResult`, `lightDismiss` stays off, and Enter presses the
  accent button unless `enterTakesPrimary` is turned off. The `nav` example has one on the Debug
  page.
- **`Painter::Shadow` got a shape and a unit.** Its last argument is the strength that lands on the
  surface rather than a per-layer alpha, and the weights now fall off quadratically, so a long shadow
  fades into what it is cast on instead of ending on a step. `StrokeRound`'s corner comes in with its
  stroke, which is the same arithmetic `Panel` already did: outer radius minus the margin is the
  inner radius, and anything else leaves the corner not quite round where the stroke meets it.
  `Palette::dialogStroke` is new -- a dialog's 1-DIP contour, heavier than a flyout's, because on
  Windows that stroke is what replaces the sharp half of a shadow.
- **`micula::Theme(ThemeMode)`** -- `Auto`, `Light` or `Dark`, the same three states the animation
  switch has. `Window::ReloadTheme` resolves through it now, so a program that has said Dark is not
  put back on the machine's answer when Windows announces that the colours changed; setting `pal` by
  hand, which is what the examples did, was exactly that. `DarkTheme()` is the resolved answer and is
  what `Create` builds the first palette from.
- The `nav` example's Settings page -- the pane's second footer row, which until now drew the "about"
  filler -- offers a theme to pick, through the switch above.

## [0.3.0] - 2026-09-28

### Added

- **Layers.** `Layer` is a control that floats over the page -- a dialog, a flyout, a menu, a tip --
  with the things a control that merely has a `z` does not have: it takes the input under it, it can
  light-dismiss, it can dim what is behind it, and it **fades in and out over `motion::kFast`** as
  one group with its smoke and the controls the page put on it. A leaving layer outlives the layout
  its dismissal causes, and so do the controls on it -- otherwise they would vanish from under a
  panel that was still on screen -- and none of them answers the pointer, the Tab ring, Enter or Esc
  while it goes. `lightDismiss` is **off by default**: a `Popup` carries `IsLightDismissEnabled` and
  a `ContentDialog` has no such property, and a question that has to be answered is not answered by
  a stray click on the dim. `Layer::Close()`, `CoverPage()`, `Body()`, `FocusRing()`,
  `DefaultButton()`, `Arrival()`. The rectangle stops at the caption bar on purpose, so dragging the
  window and the snap-layout flyout keep working with a modal open.
- **`Painter::Shadow`**, the shadow a surface over the page casts. It is the stack of rounded
  rectangles the drop-down used to build for itself, in one place and with the numbers as arguments --
  a flyout's are narrow, a dialog's are wide -- because what reads as height over a dim is the spread
  rather than the darkness.
- **`Window::DismissOthers` spares the layer that was clicked**, and a click on a layer's dim is
  handled by `Layer::OnPress` rather than by the window's "something else was clicked" broadcast --
  which cannot reach the layer it dismisses, because the layer covers the page and is itself the
  thing that was clicked.
- The `nav` example has a layer of its own on the Debug page: a panel with two controls on it, and
  a card that says how many times the button on it has been pressed without the layer going away.

## [0.2.0] - 2026-09-28

0.1.0 was the number this library was written under and was never tagged or published, so this is
its first release: what follows is what the library *is*, rather than what changed since something
else.

### Added

- **The window.** Per-monitor v2 DPI, a DirectComposition surface, and a title bar drawn by the
  library over a real one -- so dragging, double-click to maximise and the Windows 11 snap-layout
  flyout keep working without any of them being reimplemented. Mica by default, and a frame loop
  paced by the compositor clock rather than by a timer.
- **Controls.** Button, CheckBox, ToggleSwitch, Segmented, Slider, DropDown, TextBox, ProgressBar,
  ScrollBar, and the navigation pane in three styles with a footer and a scroll bar of its own.
- **Scrolling as a transform**, not a layout: a page returns `ClipRect()` and
  `ContentTransform()`, and its controls are never laid out again to scroll them.
- **Animation** from WinUI's published numbers: `motion::Track`, `Span`, `Ramp` and `Follow` for
  moving things, `Decel`, `Accel` and `InOut` for the shapes, and one switch -- `micula::Animations()`
  -- that turns all of it off. It follows the machine's "Animation effects" setting until a program
  says otherwise. Progress bars and timers are not animation and keep running.
- **Threads.** `Post` runs a callable on the window's thread, from any thread; `PostSlot` coalesces
  a worker that has more news than the window needs. Nothing in the library is synchronised: a
  control belongs to the thread that made it.
- **Text.** `Fonts`, `Painter::Text` and `TextWrapped`, an IME-aware caret, and clipboard
  copy/paste in `TextBox`.
- **A page is a `Window` subclass**: `Layout()`, `PaintPage()`, `OnTick()`, `ContentTransform()`,
  `ClipRect()`, `OnAppMessage()`, `Timer`, and a `Widget` base carrying the hooks a control of
  one's own needs.
- **The icon code points**, in `micula/glyphs.h`, named as Microsoft names them, with a fallback to
  Segoe MDL2 Assets.
- **Examples**: `nav`, a playground with every switch on a Debug page; `gallery`; `settings`.
- **Documentation** under `docs/`: the window, the controls, drawing, custom controls, and what a
  program has to provide.
