# Changelog

Notable changes, by release. The format is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the versions are [Semantic Versioning](https://semver.org/spec/v2.0.0.html) as far as a 0.x
library can be: a minor version may break.

## [Unreleased]

### Added

- **`glyph::kEthernet` (`E839`) and `glyph::kRemoteDevice` (`E836`).** The two ends of a connection to
  a machine somewhere else: a device with its cable plugged in, and a device being added. The font has
  no server glyph, so the second of these is the closest honest answer for "the machine at that
  address". The names are Microsoft's own -- from the icon list Windows Terminal and the WinUI Gallery
  both ship -- because the fonts have no glyph names at all (both carry an empty `post` table), and a
  code point identified by looking at a rendered sheet is a code point that is eventually the wrong
  picture.
- **`glyph::kLanguage` (`F2B7`).** `LocaleLanguage`, a globe with a letter and a character on it -- for
  the setting that decides what language a page speaks. It sits past the `E7xx` block the rest of the
  list comes from, and the only way to know Segoe MDL2 Assets has it is to draw it there: nothing marks
  the icons Fluent Icons added, which is why that check is a drawing and not a lookup.
- **`tools/glyphpicker.ps1`.** A window for choosing the icons in `glyphs.h`: it draws a page of code
  points, dims the ones the font does not have, marks the ones that only one of the two icon fonts has,
  and copies the literal of the cell that was clicked. The range is typed once, at the top, and the
  arrows beside it flip the page -- one page, or ten. Both rules about glyphs are cheap to check
  there, and one of them is worth checking: `E963` was read as "a server" by eye, and is a smartcard.
  `-SelfTest` copies a literal and reads it back; `-Shot` renders a page to a PNG without a window.
  The window has to make the process DPI aware (and, since PowerShell starts unaware, before the first
  control exists) or Windows stretches the whole thing to the real DPI and the text goes soft -- which
  is what the first version did at 150%, and what looked like a GDI scaling bug.

- **A page is a tree now, and the tree is the release.** `window.h` no longer has a flat list of
  widgets that `Layout()` clears and builds again. A `Widget` (`widget.h`) owns the widgets under it,
  its parent's layout decides where it is, and `visible` decides whether it is anywhere at all -- so a
  page that used to be torn down to change what is on screen changes it instead. Nothing has to be
  written to survive a rebuild because there is no rebuild: a half-typed field, the focused button and
  a slider mid-drag stay exactly where they were. `docs/layout.md` is the design; `examples/gallery`
  is the page written against it.
- **The layout protocol** (`layout.h`): `Sizing`/`Axis`/`Want` for what a control says about one axis
  of itself -- content, fill, or fixed -- `Room` for what it is measured against, `Spec` for the
  numbers a settings page is made of, and `Layout` for the thing that answers with an arrangement.
  Three of them ship: `StackLayout` (a column), `RowLayout` (a row), `CustomLayout` (a function).
- **`Card`** (`card.h`). The platform's settings row as a widget rather than as four rectangles every
  page works out for itself: an optional icon, a line of text, an optional line under it, an optional
  value read back from the control, and the control itself on the right -- with its own layout giving
  the control the room the text did not take.
- **`Label` and `Heading`** (`text.h`). A page's words are widgets too, which is what puts them in the
  tree that the arrangement and the screen reader both read. A heading carries the band above itself.
- **The layout debug overlay.** Compile with `-DMICULA_DEBUG_LAYOUT=1` and the window draws what the
  layouts worked out, over the page: where each widget is, where it is on its way to, the box its own
  layout places children in, and the page's clip. Off in every build that does not ask for it, and
  switchable at run time in one that does.
- **`ArrangeSubtree`** (`widget.h`), the walk the window arranges with, as a free function -- so a
  program can place a tree and read the rectangles without a window. `micula-gallery --dump` prints
  them, which is how a layout fault is found and checked without taking a screenshot.
- **`ScrollView`** (`scroll_view.h`). A box whose children do not fit is scrolled by the box, and the
  whole of it is one idea: **scrolling is arranging the children somewhere else.** They are laid out in
  a column as tall as they come out and moved up by however far the view is scrolled, so a wheel notch
  changes a number and everything downstream follows -- the glide that already exists animates it,
  because a child whose rectangle has moved is a child whose `drawn` rectangle is on its way there;
  the hit test follows it, because it reaches a widget where it *looks*; and the clip is the view,
  which is the only part of this that is about the container rather than about the children. No scroll
  offset, no second animation, and nothing inside knows it happened. Verified by arithmetic rather
  than by eye: a 480-DIP window over a 512-DIP page gives `most = 64`, `ScrollBy(40)` moves the first
  heading from 32 to -8, `ScrollBy(200)` stops at 64 and `ScrollBy(-30)` at 0.
- **`Widget::Clips()`**, and the paint walk pushes it as a clip. A container that is a window onto its
  children is what makes a page too long for the room it has readable; nothing else changes, what is
  outside the box is drawn and simply not seen, and the hit test never reaches it because a click
  outside the container is not a click on the container either. `Widget::VisibleArea()` answers with
  the nearest container that clips rather than with the page, so a control inside one is told about
  the box it is really seen through instead of one it does not have.
- `SystemWheelLines()`, because the system's own "lines per notch" is a setting more than one control
  now reads.
- **The scroll bar is the view's own.** `ScrollView` is two children rather than one: the column a page
  adds to (which is where the page's margin lives) and the bar, in that order, because children are
  painted in order and the bar is drawn over the column. `Add` on the view forwards to the column, so a
  page never has to know there are two. A new `ScrollLayout` arranges both -- the column at minus the
  offset, the bar at the right-hand edge -- and is what tells the bar what it is looking at: how much of
  the page there is, how much of it is visible, where the page has been scrolled to, and where it is
  *drawn*, which trails the target through a glide so the thumb goes with the page rather than arriving
  before it. The bar being a child is the whole of the integration: the tree paints it, hit-tests it and
  ticks it, and the wheel walks up into the view and scrolls.
- `ScrollBar::Poll()` is called from the arrangement, and `Wake()` from anything that moves the page. A
  bar that is out is being tweened and a bar that is not out yet is not animating yet, so without the
  first of those nothing would ever come out; without the second, a wheel or a keyboard scroll would
  move the page with no sign of where it went.
- **A layer goes where it says.** A layer is the tree's modal child -- it floats over a page and takes
  the input under it -- and in the flat list it was the exception that needed the most bookkeeping: its
  contents were somewhere else in that list and had to be marked as leaving with it. Its contents are
  its children now, which leaves one question for it to answer: where it goes. That is `Widget::Cover`,
  asked by the tree of every child that answered `AsLayer`, and the default answer is the whole of the
  widget it was added to -- which is what a dialog wants, since adding it to the page covers the page.
  **A layer that is not a cover overrides it**, and one has to be able to: a drop-down's list covers
  nothing at all -- it hangs under the field that opened it, as wide as the field, as tall as its list
  came out -- and it would be in the wrong place wherever it was added.
- A layout never arranges a layer. The tree hands it a rectangle and the host's layout leaves it alone,
  in `Measure` as well as in `Arrange`: a column that measured one would have counted the height of a
  page, and every rectangle below it on the page would have moved down by that much -- on a page where
  nothing had changed. `PlatformSpec()` is there for the layer whose host has no layout of its own.
- **`Dialog` is ported** (`dialog.h`). Its buttons are its children and `DialogLayout` is what places
  them, so a page calls `AddButton` and stops; the panel, the footer band and the body's box are the
  dialog's own answer (`Dialog::FrameOf`) and `Paint` draws exactly those, which is what keeps a drawn
  panel and a hit-tested button from being able to disagree. `--dump` prints the three of them beside
  the buttons, because they are the only geometry on the page with no widget behind them -- and `--hit`
  asks one point twice, once with a question open, to show the click landing on the dialog instead.

### Changed

- **`Animating()` is what a widget *draws*, not where it is.** A widget being carried somewhere by its
  container is not animating anything: that is the container's animation, and it is the container's
  *layout* that reports it, through `Layout::Gliding()`. A widget that is only being moved answers no.
  Two things follow. A page scrolling under a stationary pointer costs nothing -- the view says it is
  animating and the forty controls in it say they are not -- and a control the pointer happens to be
  over lights up on the way past while the ones beside it stay quiet. And the walk down stops at a
  gliding layout instead of descending into a subtree whose answer is already known.
- The root widget is placed where it is arranged, every time. It is not a child of any layout, so
  nothing glides it -- and a `drawn` rectangle left behind by the window's old size said "something is
  moving" for the rest of the window's life, with the frame loop turning frames for a page standing
  still. The change above is what makes this matter: without it, a resized window would stop animating
  altogether instead of never stopping.
- **The wheel walks up the tree.** A notch was offered to the widget under the pointer and then to the
  top layer; it is offered to that widget and to each thing it is inside of, in its own space, which is
  what lets a control in a scrolling container turn the container. The layer is still offered it after
  the walk.
- **Every control answers `Measure(const Room &) const` instead of being handed a rectangle**, and
  draws inside the one it was given. A button is as wide as its label, a field is as wide as the room
  it is in, a ring is as big as it says it is.
- **`Widget::ShowFocusRing()`**, so a control asking whether to draw its focus ring no longer reaches
  through `owner` into the window -- and a control in no window answers no.

### Removed

- `Window::Layout()`, `Window::PaintPage()`, `AddWidget`, `ClearWidgets`, and the flat list of widgets
  behind them. What a page used to draw in `PaintPage` is widgets now, and what its `Layout()` used to
  compute is what a layout does.
- `Widget::owner`, `persistent`, `z`, `scrolls`, `leavingWith`, and the two rectangle lists a control
  used to find its own hit test and its own clip in. The tree answers all four questions.

## [0.8.1] - 2026-09-29

### Changed

- **A commit is only reported when there is something to commit.** `TextBox::onCommit` fired from
  `OnBlur` whether or not the text had changed, and `Slider::onCommit` fired from `OnRelease` for a
  press on the knob that left the value exactly where it was. A page that writes a file, or marks
  itself dirty, on the strength of a commit therefore did it for the act of clicking into a field or
  touching a slider -- and a "saved" that appears because somebody clicked somewhere is worse than no
  "saved" at all. Both now follow the rule `onChange` has always kept: a notification about a change
  is not sent when nothing changed. `Widget::OnFocus` is new, and is how the field knows what it
  held when it was focused; a slider remembers whether the gesture moved the value at all.

## [0.8.0] - 2026-09-28

### Added

- **`ProgressRing`.** The indeterminate ring, to WinUI's own numbers rather than to an impression of
  them: the numbers come from the Lottie visual the control actually draws (`ProgressRingIndeterminate`
  -- an 80 unit box, a radius of 35, a stroke of 7.5, round ends, 60 fps, 120 frames, two seconds a
  cycle). A cycle is a steady 450 degrees a second and an arc that grows from a dot to half the circle
  over the first second -- its head running away from a fixed tail -- and shrinks back to a dot over
  the second, its tail catching up with a head that has stopped. It follows `ProgressBar` in every
  other respect: self-driving, and its phase read from the clock rather than counted across frames, so
  that a page laying itself out does not start the animation over. The determinate state draws a track
  and fills it from the top clockwise; `RingStyle::Subtle` is the grey ring, for a panel waiting on
  something rather than a control reporting it. The gallery draws the indeterminate one.

### Changed

- **An icon-only button centres its glyph, and has no name until it is given one.** A `Button` with a
  glyph and no label had its icon placed where a label's left edge would have been, in a box the width
  of a label; it is now measured and centred, so an icon-only button is as wide as it looks. Its
  accessible name was the label even when there was no label -- an empty name rather than no name, and
  not what `Widget::AccessibleName` asks for. It answers null now, and `Widget::accessibleName` is how
  such a button gets a name, which `AccessibleLabel()` was already set up to prefer.
- **`Painter::TextWrappedCentred`.** A paragraph centred line by line and as a block, clipped to the
  rectangle. `TextWrapped` cannot say it -- the alignment of the lines belongs to the text format, and
  the format is shared -- so an empty panel's one line of grey had nowhere to come from.
- **`glyph::kCancel`.** `ChromeClose` is the caption button's and is drawn heavier than the icons a
  page puts beside it; `Cancel` (`E711`) is the thin one, next to something like Refresh.

## [0.7.0] - 2026-09-28

### Added

- **`glyph::kCellPhone`.** The icon list gained the cell phone (`E8EA`) -- the device on the far
  end of a remote session, which is a thing a page tends to need. It is the phone Fluent Icons
  shares with Segoe MDL2 Assets; the phone-with-a-screen icons next to it are Windows 11's own and
  would draw the substitution box on the fallback, which is the rule `glyphs.h` states and now also
  says how to check.

### Changed

- **A window sets up what it needs.** `Window::Create` opens the COM apartment (WIC for the caption
  icon and `Window::Image`, and UI Automation) on the thread that makes the window, and sets
  per-monitor v2 where the process has not said anything about DPI -- which is every launch with no
  manifest. What a program used to have to do in `wWinMain` it now only has to do if it wants to: a
  program that initialises COM itself keeps its own apartment, and `EnablePerMonitorDpi()` still
  overrides a manifest. The three examples lost both calls.

## [0.6.1] - 2026-09-28

### Fixed

- **The version in an installed package was whatever CMake last configured with.**
  `micula-config-version.cmake` is written at configure time and nothing made `version.h` a
  dependency of that, so bumping the version and only running a build wrote a package file that
  still answered with the previous number: `find_package(micula 0.6 CONFIG)` against a freshly
  built 0.6.0 said 0.5.0, and a request for the new minor was refused by a package of the new minor.
  Editing `version.h` re-runs CMake now, which is the only other thing that would have.

## [0.6.0] - 2026-09-28

### Added

- **UI Automation.** A control drawn by hand is invisible to a screen reader unless the window says
  otherwise, and now it does: the window answers `WM_GETOBJECT` with a provider, the page's visible
  controls become elements of it in paint order, and a control says four things about itself --
  `AccessibleName()`, `AccessibleType()`, `AccessibleToggle()` and `AccessibleValue()`, plus `tips`,
  which is also what a client reads as the help text -- and `accessibleName`, for a page that knows
  a name the control does not. Invoke and Toggle are driven through
  `OnActivate`, so a control that works from the keyboard works from a screen reader; a focus
  change is announced; a client can ask which control is at a point on the screen. Nothing runs
  unless a client is listening, and `UIAutomationCore` is looked up at run time rather than linked,
  so a program built on this takes on no new load-time dependency and one that never meets a screen
  reader never loads it. Button, CheckBox, ToggleSwitch, DropDown, Slider, ProgressBar, TextBox and
  Segmented answer; the scroll bar and the navigation pane do not yet, per-item elements are not
  there, and values are read-only.

## [0.5.0] - 2026-09-28

### Added

- **`App`**: one message loop, as many windows as it is given, on one thread -- and `Window::Run()`
  is the shorthand for the one-window case, running the same loop. A window that did not start the
  loop is ticked and painted like any other; before this, a second window never animated at all, and
  a page on it that laid itself out in a callback would have freed the widgets under its own feet,
  because the dispatch guard was the running window's. `App::Add`, `Remove`, `Run`, `Quit`,
  `Running`, and on the window `app`, `OnClosed()`, `BeginPump`/`EndPump`. A window added to a loop
  that is already running is brought up to it there and then -- its frame clock and its caret timer
  are started, where before neither was ever started, which made every frame of every animation on
  that window a hundred milliseconds long whatever the real interval was, so an 83 ms transition was
  over before the second frame of it -- and a window destroyed while the loop goes on gives back its
  device and its fonts at that point rather than at the end of `Run()`, which only ever reached the
  windows still standing.

### Fixed

- The frame loop's fallback pacing -- the path taken where the compositor's own clock is not
  available -- read the monitor's rate once per *program* rather than once per stretch of animation,
  which is what its comment claimed and what the mark it measures the wait from assumed: from the
  second animation of the run onwards, the loop paced itself against a mark taken minutes earlier and
  drew frames as fast as the machine could, a window animating at several times its display's rate.
  The state is cleared where the loop goes idle, beside the clocks.

## [0.4.0] - 2026-09-28

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
