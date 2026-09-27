# Changelog

Notable changes, by release. The format is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the versions are [Semantic Versioning](https://semver.org/spec/v2.0.0.html) as far as a 0.x
library can be: a minor version may break.

## [Unreleased]

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
