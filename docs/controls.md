# Controls

In `micula/widgets.h`, which includes one header per control -- `button.h`, `text_box.h` and so
on -- so a page that draws two of them can include those two and nothing else. Every control
derives from [`Widget`](widget.md), so each also
has `rect`, `visible`, `enabled`, `scrolls` and `persistent`. Set `rect` after `Add`.

Callbacks may call `Layout()`, except `Slider::onChange` during a drag (see
[Slider](#slider)).

| Control | Height | Value | Callback |
|---|---|---|---|
| [Button](#button) | 32 | | `onClick()` |
| [CheckBox](#checkbox) | 32, or 44 with `detail` | `checked` | `onChange(bool)` |
| [ToggleSwitch](#toggleswitch) | 32, or 44 with `detail` | `on` | `onChange(bool)` |
| [Segmented](#segmented) | 32 | `selected` | `onChange(int)` |
| [Slider](#slider) | 32 | `value` | `onChange(float)`, `onCommit(float)` |
| [DropDown](#dropdown) | 32 | `selected` | `onChange(int)` |
| [TextBox](#textbox) | 32 | `text` | `onChange(text)`, `onCommit(text)` |
| [ProgressBar](#progressbar) | any | `value` | |
| [ScrollBar](#scrollbar) | 12 wide | `value` | `onScroll(to, glide)` |

Heights are DIPs. The control height is `metric::kControlH`.

## Button

```cpp
Button(std::wstring label, ButtonStyle style, std::function<void()> onClick);
```

| Member | Description |
|---|---|
| `std::wstring label` | Text. |
| `ButtonStyle style` | `Accent` (the page's main action), `Standard`, `Subtle` (no box until hovered), `Link` (accent-colored text, hand cursor). |
| `std::wstring glyph` | Optional icon, a Segoe Fluent Icons code point such as `glyph::kFolder`, drawn at the left. |
| `bool leftAlign` | Left-align the label instead of centering it. For navigation lists. |
| `std::function<void()> onClick` | Called on click, Space or Enter while enabled. |
| `float PreferredWidth(const Painter &p) const` | Label width plus padding, at least 100 DIPs. `p` only needs `font` set. |

```cpp
micula::Painter measure;
measure.font = &fonts;
auto *ok = Add(new micula::Button(L"Save", micula::ButtonStyle::Accent, [this] { Save(); }));
ok->rect = micula::Rect(24, y, ok->PreferredWidth(measure), micula::metric::kControlH);
```

## CheckBox

```cpp
CheckBox(std::wstring label, bool checked, std::function<void(bool)> onChange);
```

| Member | Description |
|---|---|
| `std::wstring label` | Text beside the box. |
| `std::wstring detail` | Optional second line in the secondary text color. Needs a 44-DIP `rect`. |
| `bool checked` | Current value. |
| `std::function<void(bool)> onChange` | Called with the new value on click, Space or Enter. |

The box is at the left of `rect`, vertically centered. Use a checkbox for a choice that
is applied later, such as by an OK button.

## ToggleSwitch

```cpp
ToggleSwitch(std::wstring label, bool on, std::function<void(bool)> onChange);
```

| Member | Description |
|---|---|
| `std::wstring label` | Text at the left of `rect`. Empty draws only the switch. |
| `std::wstring detail` | Optional wrapped second line under the label. |
| `bool on` | Current value. |
| `std::function<void(bool)> onChange` | Called with the new value on click, on Space or Enter, and while the knob is being dragged. |

The switch is 40 x 20 DIPs at the right edge of `rect`. Use a switch for a setting that
takes effect immediately.

The knob is draggable: pressing it takes hold of it, it follows the hand anywhere between
the ends, and letting go settles it on whichever end the value came out on. A press
anywhere else -- the rest of the track, the label -- is a click, and flips the value once.

The value flips at 0.6 of the way across going on and 0.4 coming back: two points rather
than one, so that a hand resting in the middle, or a drag crossing the middle twice, does
not report two changes for one gesture. The fill and the knob are two animations of the
same length -- the fill follows the value, the knob follows the hand -- so the moment the
value changes can be seen while the knob is still being dragged. Held, the knob is a
little larger and round: a click presses it, which Fluent draws as wider and shorter, and
a drag carries it, which is not a press.

## Segmented

```cpp
Segmented(std::vector<std::wstring> options, int selected, std::function<void(int)> onChange);
```

| Member | Description |
|---|---|
| `std::vector<std::wstring> options` | A few short labels. `rect` is divided equally between them. |
| `int selected` | Index of the selected option. |
| `std::function<void(int)> onChange` | Called whenever the selection changes: on a press, on each option the pointer crosses while the button is held, and on Left and Right (which wrap around). |

The selection follows the button rather than waiting for the release: a press takes the option
under the pointer, a drag carries the selection along -- out of the control and out of the
window -- and the release only ends the gesture. The indicator slides to the new option and is
longer than one cell while it is on its way; a label the indicator is crossing is cut by its
edge and drawn in the colour of whatever is behind it. Left and Right wrap, which is the one
move that crosses the whole control.

`onChange` runs inside the mouse message rather than during a paint, but do not call `Layout()`
from it during a drag: the layout rebuilds the control holding the capture, and the gesture ends
on the option it had reached.

## Slider

```cpp
Slider(float value, float lo, float hi, float step, std::function<void(float)> onChange);
```

| Member | Description |
|---|---|
| `float value`, `lo`, `hi`, `step` | Value and range. A dragged value is rounded to a multiple of `step`. |
| `std::function<void(float)> onChange` | Called every time the value changes: during a drag, each time the pointer reaches another step, and on each key. |
| `std::function<void(float)> onCommit` | Called once when a drag ends, and after each key. Save settings here, not in `onChange`. Not a constructor argument. |

Keys: Left and Down subtract `step`, Right and Up add it, Home and End go to `lo` and
`hi`.

The knob eases to each new step over `motion::kFaster` rather than jumping to it, so a
stepped slider shows where it has moved to; the value itself is exact the moment it
changes, and it is the value that `onChange` is handed.

During a drag, `onChange` is called while the window is painting, and only when the
value moves to another step -- not once per frame. Update the page's fields there, but
don't call `Layout()` or add or remove controls. Do that in `onCommit`.

The drag belongs to the slider and not to its rectangle: dragged past either end of the
track, or off the window altogether, the knob still follows the pointer, and `onCommit`
fires when the gesture ends -- including when the window loses the capture or the
activation, which ends it there rather than leaving it hanging.

## DropDown

```cpp
DropDown(std::vector<std::wstring> options, int selected, std::function<void(int)> onChange);
```

| Member | Description |
|---|---|
| `std::vector<std::wstring> options` | The list. |
| `int selected` | Index of the selected option. |
| `std::function<void(int)> onChange` | Called with the new index when a different row is clicked, or on Up and Down (which step through the list while it is closed). |
| `bool open` | Whether the list is showing. Read only. |
| `bool wrapAround` | Whether the choice is a ring. Off by default: a step past either end has nowhere to go. |

Click, Space or Enter opens the list; a click on a row chooses it; Esc, a click
elsewhere or deactivating the window closes it. Space and Enter close it as well, keeping
what the keyboard has arrived at -- the row under the accent mark -- because the keyboard
is not pointing at a row and a mouse click is.

The list opens **over the control**, with the chosen row where the control's own row is:
what Windows 11's own combo boxes do, and what makes choosing read as swapping one name
for another rather than as picking from a menu. It grows out of the control over
`motion::kFast`, each edge travelling from the control's row outward, and collapses back
into it.

Choosing then moves the whole list rather than a marker within it. The panel slides one
row -- 32 DIPs -- per step, so the row that arrives is on the control's own row and the one
that was there has left it. That is what a notch of the wheel over an open list does, and
Up and Down, and dragging the list's scroll bar.

With `wrapAround` the choice is a ring and those two ends are the same end: a step past the
last option arrives at the first, from the wheel or from Up and Down, and whether the list
is open or closed -- a control whose keys and wheel disagree about its ends has two answers.
The list's own scroll bar is not part of it, and neither is the room the popup is shown
through: both have two ends.

The accent mark is drawn on the control's own row and stays there: the list slides past it,
so which option is under the mark is the choice, and the mark itself never travels.

The keyboard can search, **while the list is open**: typing letters chooses the option that
starts with what has been typed, and the list slides to it. Only the first letter of a search
is a step, like a notch of the wheel -- the next option that starts with it, after the one
chosen now. Every letter after that refines the search from where it has arrived: the option
already chosen stays chosen for as long as it still starts with what has been typed, so
typing a name out of a long list of names does not walk down it. The prefix is kept for a
second of quiet, and pressing the same letter again steps on to the next option that starts
with it, which is the only way to reach the second "Monthly".
Typing does nothing while the list is closed, which is what Space is for -- a closed
drop-down is a button with a label on it and has no mark on screen to show a search
result on.

A gesture with nowhere to go is answered by the mark: a letter that matches nothing makes
it shrink for a moment, and the wheel or Up and Down at the end of the list makes it give
way the way it was pressed -- both edges set off together, the one being pushed towards
more briefly and a shorter distance, the other further and easing into place, so the mark
is shorter from the first frame and never longer -- and then both spring back. With
`wrapAround` there is no end for the wheel or the keys to reach, so the mark gives way for
a letter that matched nothing and for nothing else.

The panel is as tall as the list and is not cut to the room it has: where the room runs out
the page clips it, and the far end of the list is what goes out of sight -- never the chosen
row, which stays on the control. The same room decides what a pointer can reach: a row is
only ever lit, and only ever chosen, where it is drawn and inside the popup's own box, so
neither the title bar nor the space beside the control is part of the list. Its scroll bar
appears when the list is taller than that room, which is `ClipRect()` or, on a page that
does not scroll, the client area below the title bar.

`rect` must be 32 DIPs tall: while the list is open, `rect` grows to cover it.

## SideNav

```cpp
SideNav();
```

| Member | Description |
|---|---|
| `std::vector<NavItem> items` | The pane, top to bottom: rows written `{ glyph, label }`, and group headings written `NavItem::Heading(L"...")`. |
| `std::vector<NavItem> footer` | Rows pinned to the bottom, which is where Settings goes. Their place in the numbering carries on from `items`. |
| `int selected` | Which row is chosen. Headings are not counted, so adding one to `items` does not move the page's own indices. |
| `std::function<void(int)> onSelect` | Called with the new index when a row is chosen. |
| `std::function<void(bool)> onToggle` | Called when the pane opens or closes. A page in `Push` mode lays itself out again here. |
| `PaneStyle style` | `Fixed`, `Toggle` (the default), `Peek` or `Minimal`. |
| `bool animate` | Whether the width slides or arrives in one frame. Default true. |
| `bool ownToggle` | Draw the button that opens and closes the pane. Default true; `Fixed` and `Minimal` draw none whatever it says. |
| `bool scrim` | Darken the page while an overlay pane is open. Default false. |
| `bool followsFocus` | Whether Up and Down move the choice or only the focus ring. Default true. |
| `float compactW`, `openW`, `rowH` | 48, 320 and 36 DIPs: WinUI's `CompactPaneLength`, `OpenPaneLength` and the height of one of its rows. |
| `float peekIn` | `Peek`: the seconds the pointer has to rest on the rail before it opens. Default 0.2. |

The pane draws its rows, its headings, its button and its accent bar, and nothing else. The
page beside it belongs to the window: `onSelect` is where another page happens -- write the
field the page is built from and call `Layout()`, the way a `Segmented` control's callback
does.

**Make it `persistent`.** A pane is the one control a page really is laid out while it is
being operated: choosing a page rebuilds the page, and the rebuild would take the open state,
a width half way through its animation, the hover and the accent bar's travel with it. Make
it once, mark it persistent, and hand it a fresh `rect` in every `Layout()`. Give it the left
edge, the top and the bottom; the pane owns its own right edge, so that a closed rail takes
exactly the clicks that belong to it. `Reserved()` is the width to leave for it.

Four styles, and the drawing is the same in all of them:

| Style | What it is | Where the page goes |
|---|---|---|
| `Fixed` | Part of the layout, and it never moves: `openW` is simply how wide it is. | The page is laid out past it. |
| `Toggle` | The button opens and closes it, and it stays where it was put. | The page makes room for it again on every toggle, so the window lays itself out in `onToggle`. |
| `Peek` | Resting the pointer on the rail opens it and leaving closes it, over the page. The button pins it open instead, and a pinned pane is a `Toggle`: the page lays itself out and keeps the room until it is closed again, which is what a person asking for a pane should get from it -- a pane that covers the content *and* has to be clicked shut is a pane with no reason to exist. Only the hover covers the page, and a hover is the pointer on its way somewhere else. The pointer has to rest for `peekIn` first -- crossing a rail on the way somewhere else is not asking for a pane -- and closing has no such delay, because a pane that lingers over the page after the pointer has left is in the way. This one is not WinUI: `NavigationView` has no hover, and Task Manager does not open on hover either. It is Visual Studio's auto-hide tool window. | The hover leaves the page where it is; once the button has pinned it, the page makes room, and goes on making it until the pane is back at the rail -- a retraction is the same pane leaving, not an overlay arriving. |
| `Minimal` | A rail of icons and nothing more: it never expands, so `SetOpen` and `Toggle` do nothing, and it draws no button of its own. | The page is laid out past the rail, and the pane never covers it. |

Nothing chooses between those two answers: a pane that stays is one the page is laid out around,
and a pane that comes and goes covers what it is over, because pushing a page around under a
pointer that is only passing through is worse than covering it. There was a `place` field here
and a rail-plus-overlay shape to match WinUI's `LeftCompact`; it came out of the example window
because a pane the user asked to open and which then covers the content is not worth the third
axis. WinUI earns that shape by picking it *for you* in a window too narrow to give up the room,
and this library has no such mode.

`Minimal` is the rail by itself. WinUI's own `PaneDisplayMode="Minimal"` keeps a pane that
overlays the content when its button is pressed; this one does not expand at all. A bar of
icons is a shape a window has on purpose -- a launcher, a tool window's tab strip -- and a pane
that has to be dismissed again before the page can be read is not that shape. `open` is not
asked, so a page can wire its own button to `SetOpen` or `Toggle` without asking which style
the pane is in.

The rail shows the icons alone, and the shape around one is the pane's own width: it is a
40-DIP square around the icon at rest and the whole row once the pane has arrived, so the
pill grows out of the icon box rather than being a second animation kept in step with the
first. The accent bar is the exception -- it travels, stretched between the row it left and
the row it is arriving at (`motion::Span`, the same two-edged follower the segmented control
draws its block with).

A heading is a label for a group of rows, and a rail has no groups to label: its gap and its
own height go with the pane's width, so the icons close up into one column as it collapses
(WinUI's compact pane does the same). And the rows start under the pane's button when there is
one, because the button is drawn over the pane's own top corner -- with no button, the top of
the pane is free and the rows go up into it.

The pane never touches the page. The only two moments it is involved in one are `onSelect`
and `onToggle`, and both are the page's own callbacks, running in the page's own code. A page
that keeps its scroll offset in a field -- the way `examples/gallery` does -- is therefore
still exactly where it was read after the pane has opened and closed over it; a page that
starts at the top when another one is chosen is the page's decision and not this control's.

Up and Down move the choice, which means the page follows the arrow keys. With
`followsFocus` off they move only the focus ring, and Enter or Space chooses -- for a window
whose pages are expensive to build. A pane open over the page is drawn in layer `z == 2`; see
the painting order in [Overview](README.md). `examples/nav` is this control with every switch
above on the page beside it.

Not here: a pane title, a back button, hierarchical rows, and a pane that scrolls its own
list when the rows do not fit -- that last one is a `ClipRect()` and a `ScrollBar` away if a
window ever needs it.

## TextBox

```cpp
TextBox();
```

| Member | Description |
|---|---|
| `std::wstring text` | Current text. Read it; set it with `SetText`. |
| `void SetText(const std::wstring &s)` | Replaces the text and puts the caret at the end. Does not call `onChange`. |
| `std::wstring placeholder` | Shown in the disabled text color while the field is empty. |
| `bool pathField` | For file system paths. Paste also removes surrounding quotes, as added by Explorer's Copy as path, and trailing spaces. |
| `std::function<void(const std::wstring &)> onChange` | Called after every edit. |
| `std::function<void(const std::wstring &)> onCommit` | Called on Enter and when the field loses focus. Save here. |

```cpp
auto *t = Add(new micula::TextBox());
t->SetText(folder);
t->pathField = true;
t->onChange = [this](const std::wstring &s) { folder = s; };
t->rect = micula::Rect(24, y, 320, micula::metric::kControlH);
```

Supported: caret and selection with mouse and keyboard, double-click to select a word
(separated by spaces, `\` and `/`), Shift+click, Left, Right, Home, End (with Shift to
select), Backspace, Delete, Ctrl+A, Ctrl+C, Ctrl+X, Ctrl+V, IME input. Paste keeps only
the first line.

Not supported: multiple lines, undo, context menu, drag and drop, right-to-left text.

## ProgressBar

```cpp
ProgressBar();
```

| Member | Description |
|---|---|
| `float value` | Progress from 0 to 1. |
| `bool indeterminate` | Show two crossing bars instead of `value`. Keeps the frame loop running while visible. |

The bar is 3 DIPs tall, centered in `rect`. Indeterminate is WinUI's own animation: two bars,
40% and 60% of the width, crossing on a two-second loop with the second one starting three
quarters of a second behind the first, so that for half of the cycle one is leaving at the
right while the other arrives at the left. The loop is read from `MonotonicSeconds()` rather
than counted per frame, so a control the page has rebuilt -- a resize, another setting
changing the shape of the page, a scroll on a page that lays itself out in response to one --
does not restart the sweep.

## ScrollBar

```cpp
explicit ScrollBar(std::function<void(float to, bool glide)> onScroll);
```

A vertical scroll bar that follows WinUI: hidden, a thin indicator while the page is
scrolled or the pointer is over it, and expanded with arrows when the pointer rests on
it. When Windows' "Always show scrollbars" setting is on, it stays expanded.

The page sets these in `Layout()`, and keeps `value` and `drawn` current whenever it
scrolls -- which is the only two of them that move on their own:

| Member | Description |
|---|---|
| `D2D1_RECT_F rect` | Its position. `ScrollBar::kSize` (12) DIPs wide. |
| `D2D1_RECT_F area` | The scrolling area. Pointer movement over it shows the indicator. |
| `float viewport` | Visible height of the scrolling area. |
| `float extent` | Full height of the content. |
| `float value` | Current scroll position, 0 to `extent - viewport`. |
| `float drawn` | The position the content is drawn at. Equal to `value` unless the page glides. |
| `bool visible` | Set false when there is nothing to scroll. |

| Member | Description |
|---|---|
| `onScroll(float to, bool glide)` | The bar asks the page to scroll to `to`. `glide` is false while the thumb is dragged, when the content must follow the pointer exactly. |
| `void Wake()` | Shows the indicator and restarts its 2-second timeout. Call it when the page scrolls. |
| `void Poll()` | Updates the bar's state and timers. Call it after `Wake()`. |

Make the bar once, set `persistent` on it, and reposition it in each `Layout()`; a scroll
touches only `value` and `drawn`. See [Scrolling](window.md#scrolling).
