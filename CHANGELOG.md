# Changelog

Notable changes, by release. The format is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the versions are [Semantic Versioning](https://semver.org/spec/v2.0.0.html) as far as a 0.x
library can be: a minor version may break.

## [Unreleased]

### Added

- **A finger scrolls a page.** A control that is not holding the gesture stops being asked about it as
  soon as the movement starts, and the first container above it that pans -- `ScrollView` -- takes it:
  the content follows the hand instead of gliding after it, a pull past the end of the page comes a
  little way and springs back, and letting go throws it. `Widget::Pans`, `PanMove` and `PanRelease` are
  the hooks, and the container is decided once per gesture so a page inside a page does not pass the
  same hand up. The navigation pane's rows are not children -- forty rows is one control drawing forty
  rows -- so nothing in the tree could scroll them and the pane answers `Pans` itself, which gives it a
  throw of its own; the pane has no rubber band, because a rail with rows drawn past its own end is a
  rail with rows outside it.
- **Windows decides what a hand did; Android decides what a throw is worth.** Which movement was a tap
  and which was a drag is not judged here at all -- `InteractionContext` is asked, through `NInput.dll`,
  so the slop is whatever the system's own recognizer says it is, and only the question of which
  container a drag belongs to is left to the library. A machine without the recognizer falls back to the
  eight-DIP rule (`kTouchSlop`). The fling is Android's: a distance and a duration off its own constants
  -- `DECELERATION_RATE`, the two spline tensions, and the friction behind `ViewConfiguration.getScrollFriction` --
  so a throw of 1000 DIPs a second travels about 190 DIPs in about half a second and then stops, rather
  than leaving a page creeping for seconds the way a per-frame friction does.
- **A finger and a pen are answered like a mouse.** Touch and pen arrive as `WM_POINTER` messages and
  go into the same press, move and release the mouse uses -- so a drop-down opens to a tap and a slider
  drags under a finger because they were written for a pointer -- and the window answers the pointer
  messages rather than passing them on, which is what keeps Windows from also turning every touch into
  a mouse button. Only what a hand really differs in is left over: a finger has no hover and no cursor,
  eight DIPs of travel turns a tap into a drag, a second finger takes the first one's click away, and a
  gesture belongs to the hand that began it -- the mouse moving under a finger neither un-presses it nor
  drags it. A pen goes through the same path and hovers while it is in the air.
- **The tree is announced, not only the values.** `UiaRaiseStructureChangedEvent` now carries a
  child arriving, a child leaving, the same children reordered and a list whose options were replaced
  -- the four things no property can say, because they are not about a control but about whether the
  element a client is holding still means what it did. A row also has a runtime id of its own rather
  than sharing its control's, so the announcement about a row is about that row: it used to read as
  one about the whole list.
- **A change is told, not only drawn.** `UIA_PropertyChangedEventId` now carries the four things a
  screen reader follows -- what a control says, a slider's number, whether something is open, where a
  page is scrolled to -- and which of a set is chosen is told with `SelectionItem.IsSelected` on the
  row that changed, `SelectionItem.ElementSelectedEvent` for the one that arrived and
  `Selection.InvalidatedEvent` for the set it is one of. The window works it out by comparing each
  frame it draws with the frame before, so a page that changes a control by writing its own field is
  announced exactly like one that clicked it, and the table it compares against is kept whether or not
  anybody is listening -- which is why a client is told about the change that made it attach rather
  than having it swallowed, and hears nothing at all on the frame it arrives.
- **The writing half of the same thing.** A client that can read a control can now change the ones
  that allow it: `IValueProvider::SetValue` on a field, `IRangeValueProvider::SetValue` on a slider or a
  progress bar, `ISelectionItemProvider::Select` on a row of a list, a cell of a segmented control or an
  item of a navigation pane, `IExpandCollapseProvider::Expand` and `Collapse` on a drop-down or a pane,
  and `IScrollProvider::SetScrollPercent` and `Scroll` on a page. Every one of them lands on the
  control's own path -- `SetText` and the same `onChange`, `Select` and the same `onSelect`, `SetOpen`,
  the slider's own step and `onCommit` -- so a page cannot tell a screen reader from a hand, which is
  what makes writing safe to have rather than a second kind of change that has to be kept in step. A
  widget offers the half it has through `AccessibleWritable`, `AccessibleSetValue`,
  `AccessibleSetRange`, `AccessibleSelect`, `AccessibleSetExpanded` and `AccessibleSetScroll`; a control
  that cannot do what it is asked refuses in the way a client acts on (`UIA_E_NOTSUPPORTED`,
  `UIA_E_INVALIDOPERATION`) rather than accepting the call and doing nothing, and `IsReadOnly` -- the
  property and the pattern's own -- answers for the control rather than for the pattern.
- **A widget that draws a set of things is a set of things to a screen reader.** `AccessibleItems` and
  `AccessibleItem` say what a widget is made of -- the rows of an open drop-down, the cells of a
  segmented control, the items of a navigation pane -- and a client visits them one at a time as the
  children of that one element: "30 days, 4 of 6, selected". That is what WinUI's own list reports
  without the widgets a page would need to build it. A heading is an item too and is not a place a
  choice can be, which a widget says by giving it no index.
- **The read-out is complete.** `IRangeValueProvider` for a value that is a number (a slider, a
  progress bar) alongside `IValueProvider` for one that is words, `IScrollProvider` for a container
  that scrolls, `IExpandCollapseProvider` for a drop-down's list and a navigation pane,
  `ISelectionProvider` and `ISelectionItemProvider` for which of a set is chosen, and `PositionInSet`,
  `SizeOfSet` and a bounding rectangle cut by every container that clips. `IsOffscreen` is true for a
  control a scroll has taken out of the container showing it, which is what `VisibleArea` now answers
  for all of them together rather than for the nearest one.
- **`Card::Set` names the control it is given**, unless the control answers a name of its own: a switch
  with no label is named by the line it is on, and what a drop-down is set to is its value rather than
  its name -- "Move files older than, 30 days" and not a control called "30 days".
- **A `Flyout` is uncovered from the row it hangs off.** The panel's two vertical edges travel from the
  control's line outward over `motion::kFast` and back into it as the layer leaves, and the rows inside
  the panel do not move: a list is *uncovered* rather than grown, because the chosen row is on the
  control from the first frame and a panel that grew would carry it in late. One number drives it --
  the layer's own `Arrival()`, which the layer's own painting now reads as well as the paint walk's
  group opacity -- so the way in and the way out are one animation rather than a pair to keep in step.
- **`Widget::ClipBox()`**: the box a container that clips cuts its children to, in the space its own
  `rect` is in, and its own rectangle unless the container is showing only a part of itself. The paint
  walk clips the children to it and paints the widget's own drawing *outside* that clip -- the clip is a
  window onto the children, and a flyout's shadow belongs outside the reveal that is growing over it.
  `Widget::VisibleArea()` and the two hit-test walks ask it as well, so what a container is not showing
  is not room for a control to lay itself out in and not a click it can answer.
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
  **A layer that is not a cover overrides it**, because not every layer is one: a tip beside the pointer
  is a box the size of its own words, and it would be as wrong at the host's size as in the wrong
  place. A flyout is not one of those -- it covers the page and has to, since it takes the wheel and a
  click outside the list closes it, which means the click has to land on the layer -- so its list,
  under the field that opened it, is a child of the flyout.
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
- **A flyout, and the drop-down at last** (`flyout.h`, `drop_down.h`). A flyout is the layer a panel
  that hangs off a control is put in. It covers the page, and that is what keeps the wheel and the
  clicks with it rather than with the page behind -- the layer is a sibling of the page laid over it, so
  the pointer under it is the flyout's and the wheel offered to its subtree is never offered to the
  page's -- and it light-dismisses, which needs the covering: the click that misses the panel has to
  land on the layer for the layer to hear about it. Its one child is placed under the anchor, or over it
  when there is no room, or lined up with it.
- The drop-down's list is a `ScrollView`, and it is a `ScrollView` that is **only as big as its list**
  (`shrink`): a list longer than the room scrolls under the platform's own bar, and a list that fits is
  a panel the height of its rows. The rows are one widget that draws all of them -- forty countries is
  forty rows and not forty widgets -- and the panel comes to rest covering the control with the chosen
  row on it, which is what Windows 11's combo box does and what makes a choice read as a swap rather
  than a menu. Its own popup rectangle and its own bar are the tree's now: the panel is where the flyout
  puts it and the bar is the scroll view's.
- **The accent mark is the flyout's, and not the list's** -- see `Flyout::marker` -- and that is the
  whole of what a choice looks like. A list with room to spare moves the *panel* a row to bring the
  choice to the control; a mark inside the panel would move with it, and a choice would be a thing that
  slid past. Placed at the panel's own line instead, the mark is already where the panel is going and
  the options travel under it. Where the room has pinned the panel the rows cannot move at all, and the
  mark is the only thing left that can show the change: then it travels down them, on a follower that
  is the glide, so the change is seen rather than snapped.
- A notch of the wheel steps the *choice* rather than scrolling the view, because the gesture is a walk
  down a list rather than a movement of one: a wheel that moved the list 66 DIPs, as a page's does,
  would leave the chosen row somewhere other than under the control it was chosen from. Shift asks for
  the view instead. **It steps the choice only while the control has the focus**, which is WinUI 3's own
  rule -- `ComboBox::OnPointerWheelChanged` asks `HasFocus` before it touches the selection, and
  swallows the notch it used -- and the reason for it is the page behind: a wheel that changed the value
  of whatever the pointer crossed would change a setting on the way down the page, under a pointer that
  was aimed at nothing. The click that focuses the control is the gesture that says *this* one is being
  worked on, and a drop-down nobody has clicked is transparent to the wheel, so the page scrolls as it
  did before. The two mark shapes that answer a gesture with nowhere to go are back with it: the shake
  at the end of the list, and the give-way of a letter that found nothing.
- **Words are not something the pointer acts on.** `Label` and `Heading` answer `Covers() == false`, so
  a click on a line of text is a click on the page under it -- which is what keeps a click in the blank
  space beside a field taking the focus off it. `Card` is not one of these: it covers its own box and
  keeps taking the pointer, because the controls in it are its children, and a container that refused
  the pointer refuses it to everything inside it.
- `Window::tearingDown`, because a control may own a layer outside its own subtree. A page that drops a
  drop-down while its list is open has the list unlinked from its parent -- but a *tree* coming down is
  destroying that parent's vector of children as it goes, and erasing from it is a write into memory it
  no longer owns. The removal is dropped instead, since everything under the root dies either way.
- **`SideNav` is ported** (`side_nav.h`), the last control that was still written against the flat
  list. Its rows are still one widget drawing all of them -- forty rows is not forty widgets -- and what
  it has instead is a layout of its own, `NavLayout`, which answers what the pane asks a page for
  (`Reserved`: the room the state wants) and arranges the one child a pane has. That child is its bar:
  the tree paints it, hit-tests it and ticks it now, and the pane's own plumbing for it -- forwarding
  presses, moves and drags, waking it by hand, running its frames -- is gone. Its mark, its rows, its
  arrows, its refusal at the ends of the list and the whole peek/toggle business are what they were.
- **A pane that covers draws outside the box it was arranged into**, and it is the only control here
  that does. Painting is never clipped, so it is drawn over the page -- and `Covers` answers with the
  same box, so it is *reached* over the page as well, which is what a click on a pane hanging over the
  page means. The old model wrote a `z` back into the pane every frame to raise it; what stands in for
  that is order, and a page that wants the pane over its content puts the pane after it. A pane that
  pushes is never over the page, however wide it is on the way past: the page has made room for it.
- The pane's dim stops at the edge of the page it was added to rather than at the window's, because
  that is what a pane over a page is over. `VisibleArea` answers with that box in the pane's own space,
  the same question a control inside a scrolling container asks, so a pane inside something narrower
  than the window dims that and not the window.
- **And the dim is a fade, rather than a strength worked out from the pane's width.** It used to be
  `overlap / 48`: full 48 DIP into a 212-DIP move, which the pane's own 167 ms curve covers in five
  milliseconds -- so the smoke arrived in one frame and left in one, and there was nothing to see. What
  the pane's *state* can say is whether it is over the page; how strong the dim is, is a duration, and
  a surface over another surface gets the panel's, `motion::kNormal`, Decel arriving and Accel leaving.
  It is drawn while any of it is left rather than only while the pane is past the rail, because the fade
  out is longer than the last few DIPs of a retraction -- and the pane's own state is what it answers
  to, so the smoke clears as the pane leaves rather than after it has gone.
- **`NavigationView`, the shell a navigation window is** (`navigation_view.h`), and `SideNav` stays a
  control inside it: a column that selects, and says which row was chosen. What the shell adds is the
  other half of that question, which is what a row *is*. `AddPage` adds a row and the page it shows in
  one call -- a page nobody can reach is a page that is not there, and a row that leads nowhere is a
  row with nothing to show -- and a row that is not a page, a heading or a name that is only a name, is
  `AddRow`. Which page a row shows is found by walking the rows rather than counted from where the page
  was added, because a footer row counts *after* the rows above it.
- **And the page area is drawn as the layer a navigation window's content is**: the layer colour over
  the backdrop, its top-left corner rounded and its other three square, and a border inside the two
  edges that face the rest of the window -- the far ones being the window's own, where a line would
  double a boundary that is already there. It is the shell that paints it, under the page's contents
  and whichever page is up, because it is the same surface either way. `pageSurface` off is that shell
  on a flat background, which is what a window that paints its own has nothing for a layer to be over.
- **And a page that arrives is drawn arriving** (`NavigationView::transition`): the page area comes up
  from nothing as one group -- the whole page at once rather than each control on it -- while the page
  rises the last `kPageRise` DIP into place. `Fade` is the same without the distance, `None` is the
  switch in one frame, and the first page of a window's life arrives in one frame whichever it is,
  because a page being put up for the first time has nothing to arrive *from*.
- **`Widget::opacity`**, drawn by the paint walk as **one group at that opacity** rather than each
  widget in it at that opacity: a subtree faded widget by widget shows what is behind it through the
  gaps between them, and comes out darker where two of them overlap. A layer arriving or leaving is
  the same question, asked by a layer, and both are read in one place now.
- **`examples/settings` is ported**, the last page that was written against the flat widget list this
  branch replaced: the same four pages of cards and the same system folder picker, on a
  `NavigationView` with a rail that is always out -- this window has no room the page could have and
  nothing that put the pane away -- and the `--dump` the other two examples have.
- **And `examples/nav`'s pages have their own content back**: the forty-line list, the page of real
  controls, the four switches, and the backdrop picker beside the theme switch, which between them are
  what the pane is doing anything *to*.
- **Switching a page is visibility**, and that is the whole of it: every page is in the tree and
  arranged into the same box, and the one on screen is the visible one. An open drop-down, half a typed
  field and an animation in flight are all still there when their page comes back -- nothing is
  rebuilt, which is the reason a page is a page rather than a build.
- **And a page that becomes visible is arranged for the room it has now -- and placed rather than
  glided into it.** A hidden subtree is skipped by the arrangement, so a page nobody has shown is a
  page of zero-sized widgets and nothing of it can be seen at all; a page coming back is laid out for
  the room it had *then*, which after a window resize or a pane opening is not the room it has now. The
  arrangement is what fixes the first of those, and it is asked for by the switch. The second needs the
  page to be born again (`UnplaceSubtree`), because **a glide cannot carry a size**: what glides is a
  translation, so a card whose right edge has to move is the whole card sliding, filling in a gap that
  should never have been there.
- A page whose pane pushes follows the pane's *drawn* edge -- both boxes are placed, and **the page
  area's own contents are arranged again with them**, rather than left to the frame's arrangement. The
  two are not the same thing: a card that fills the room it is in is a card whose width changed with
  the room. An arrangement runs before the tick that moves the pane's width, so a page left to it is
  laid out for the width the pane had a frame ago -- a margin that breathes while the pane moves, and
  contents that never catch up at all until something else marks the tree dirty, which after a
  retraction is a row of cards stopping two hundred DIPs short of the page's corner until it is
  scrolled.
- **And that page is placed rather than glided**, for as long as the pane is moving. A widget whose
  rectangle has moved is drawn on its way there, which is what makes a card step down when something
  above it opens -- but a page following a *moving* pane is not that: the glide closes a fraction of
  the gap per frame, so a target that keeps moving leaves it permanently a few frames behind, with the
  controls on its cards pushed out of place and then bouncing back as the animation ends. The page
  tracks the pane exactly, which is what it did before there was a tree: the old page moved its cards
  by hand, one frame at a time, and glided none of them. The placement is the shell's own method, which
  the arrangement and the shell's tick both run; the pane's bar is placed the same way, its edge being
  the pane's edge.
- `examples/nav` is written against the tree: the pane's playground -- the four styles, the width, the
  scrim, the key behaviour, the pane's own button and `nav=` rows -- as switches on the page the window
  opens on, with the same state on the command line, a theme switch on the page in its footer, and a
  `--dump` that prints the shell's own rectangles rather than a screenshot.
- **`Popup`**: a top-level thing that draws and is not a window -- a menu, a tip, a flyout that has to
  be able to leave the rectangle it came from. It is a real `WS_POPUP` window with no caption and no
  client area under one, and everything above the handle is a `Surface`, so it is ticked and painted by
  the same loop as the window it opens over. It is a window and not a `Layer` because a layer is drawn
  inside its window: it cannot cross the window's edge, and a menu has to be able to exist with no
  window behind it at all, which is what a tray menu is. `Show` places it in the DIPs of the monitor it
  lands on rather than of the one it was opened from, and `activates` decides whether it takes the
  foreground -- a tip that did would blink the caption of the window it belongs to. It has the tree a
  window has (`Add`, `SetLayout`), the three things a hand does (`Surface::HandMessage`, so a menu
  answers a finger the way every control already does), and none of what a caption or an automation
  tree needs.

### Changed

- **A surface is the common part of anything that draws.** `Window` is one and `Popup` is another, and
  what they share is all that is in `Surface`: the window handle, the composition surface it draws
  into, the tree, the clock, the whole input path, the hit tests, the focus and the timers. The frame
  loop therefore pumps surfaces -- `App::Add` takes a `Surface &` and `App` holds
  `std::vector<Surface *> surfaces` -- which is what lets a menu be open over a window without stopping
  it: both are ticked and painted by the same loop, on the same thread, and the window's animations go
  on running underneath. Nothing a surface can answer for itself changed; what a *window* keeps is its
  caption, its page hooks, its automation tree and the frame around its client area, which is why
  `RootBox()` -- where a tree starts -- is the one thing the two answer differently.
- **`Widget::window()` is `Widget::surface()`**, and `Window::Run()` is `Surface::Run()`: what a widget
  reaches up to is the surface its tree is on, which is a menu's as much as a window's, and a
  program with one surface to draw has the same shorthand it had with one window. A page that wants the
  window itself asks `surface()->AsWindow()`, which is null inside a menu -- only the things that are a
  window's (its backdrop, its automation tree) need it, and `ReloadTheme()` moved up to `Surface` so
  that a page switching the theme does not have to be in a window to say so.
- **`Window::ReloadTheme` and `Window::ApplyThemeToFrame` are public**, which is what the theme mode's
  own documentation already told a page to call: a palette is built when a window is made, so a
  settings page that says `Theme(...)` while the window is up has said nothing until the window is told
  to build it again. Nothing else about the two changed.
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
- **A window that changed size places the tree instead of gliding it.** The glide is what animates a
  layout change -- a card stepping down when something above it opens -- but a subtree *tracking* a box
  that keeps moving is not a layout change: the glide closes a fraction of the gap per frame, so the
  target runs away from it, and at the speed somebody drags a window border that is hundreds of DIPs. A
  window is being dragged by its border, every frame of it is the new size, and the arrangement that
  follows one therefore places what it arranged (`PlaceSubtree`). Every other arrangement glides as it
  did, and the navigation pane's page is placed the same way while its pane is moving, for the same
  reason.
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

### Fixed

- **A drop-down's panel is as tall as the room, not as tall as the choice left it.** Its height was the
  rows above the chosen one, the chosen one, and the rows below it -- and a choice near either end of
  the list has no rows below it, so choosing the last option opened a panel one row tall with the rest
  of the list scrollable inside it. The room decides the height now, in whole rows, and the choice only
  decides where the list sits inside it; one height for the whole list is also what keeps every notch of
  a wheel landing on a row.

- **A clipping container is clipped where it is *drawn*, not a glide past it.** The clip box came from
  the widget's `drawn` rectangle, but it is pushed in the space the paint walk's transform has just put
  on -- and that space is the parent's with the widget's own glide already taken out of it, because the
  transform is what carries a widget painting at `rect` to `drawn`. The box therefore landed one glide
  further along than the thing it clipped: a list sliding under its own panel showed a whole row past
  the panel's edge, and the row at the other edge was cut through the middle of its text. Only a
  gliding `ScrollView` could show it, and the only one there is is a flyout's panel on the frames a
  choice moves -- which is exactly when a list is being read.
- **A `DropDown` opens with its chosen row on the control's line.** The room the panel is placed from
  is worked out as the page's own box in the page's space -- the same rectangle the flyout's layout is
  handed -- and the rows above and below the chosen one are as many whole rows as that room has, so the
  alignment has the first say and the panel's edges are what give way. Worked out from a room ~10 DIP
  too generous, the panel was clamped against the box's bottom edge and took the choice off the control
  by that much.
- **The panel of a `DropDown` is a whole number of rows, and its mark answers the gesture.** A panel
  cut off in the middle of a row is a row half drawn and half of the next one's business -- at the edge
  the eye is on when a list opens, and again at every clamp -- so the height is rounded down to whole
  rows. The accent mark is placed by the control rather than by an arrangement while the list under it
  is scrolled, from the column's *drawn* rectangle: an arrangement runs before the tick that moved the
  list, so a mark left to one is drawn at the scroll of the frame before, which reads as an indicator
  trailing the list it belongs to.
- **The mark of a `DropDown` is on its row whatever moved the list.** It was placed by the wheel's own
  scroll -- a flag only that gesture raised -- so a thumb dragged on the list's scroll bar left it
  standing where it was until something else happened to put it right. What the mark asks is now asked
  of the list instead: a placement speaks for the mark only while the list is where the placement left
  it.
- **A step is the same placement as opening.** Where a step used to move the view only when it had to,
  it now moves it by the row the choice moved by, so the panel stays where the choice put it and the
  rows travel under the mark. The other way round, the chosen row ends up on some other row of the
  panel, the panel has to move to put it back on the control, and near an end of the list the room
  clamps it -- which took the choice 10 DIP off the control's line with 17 rows of room to spare. One
  piece of arithmetic for both also means one place where the view a placement asks for is sent into a
  panel that can take it: the first send is clamped against the viewport the panel has *then*, and the
  panel is being placed for a height it does not have yet.
- **A tree that owes an arrangement is a window with a frame to run.** The frame loop turns only for a
  window that is moving -- something animating, or asking for one -- and the arrangement of a change
  made by the message just handled happens inside a frame. A change made while nothing was animating
  therefore waited for whatever else happened to paint: a scroll bar dragged on an open list that was
  already woken by the pointer moved the view *two seconds* later, when the bar's auto-hide timer
  happened to repaint the window.

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
