# Grouped and expandable cards

`CardGroup` arranges existing `Card` widgets as one bounded group. `CardExpander` adds a card header
and reveals another group below it. Both are included by `<micula/micula.h>`.

```cpp
auto *page = root->Add(new micula::ScrollView());
auto *column = static_cast<micula::StackLayout *>(page->content->layout.get());
column->maxWidth = 920.0f; // application choice, in DIPs; 0 leaves the column unlimited

auto *group = page->Add(new micula::CardGroup());
group->Add(new micula::Card(L"General", L"The first row has the top corners."));
group->Add(new micula::Card(L"Notifications", L"The last row has the bottom corners."))
    ->Set(new micula::ToggleSwitch(L"", true, nullptr));

auto *details = page->Add(new micula::CardExpander(L"Advanced", L"Click to expand."));
details->header->Set(new micula::ToggleSwitch(L"Enabled", true, nullptr));
details->Add(new micula::Card(L"Path"))->Set(new micula::TextBox());
details->Add(new micula::Card(L"Action"))->Set(new micula::DropDown({ L"Block", L"Allow" }, 0, nullptr));
details->Add(new micula::Card(L"Notes"));
```

The root still needs a layout that gives the scroll view its space; see `examples/cards/cards.cpp`
for a complete program.

## Surfaces and layout

- `Card` now exposes `corners` (`Corners`, in top-left, top-right, bottom-right, bottom-left order)
  and `borderEdges` (`edge::kAll` by default). Its standalone appearance is unchanged.
- `CardGroup::Add` accepts cards and card subclasses, takes ownership, and returns the original
  type. Cards keep their existing `CardLayout`, controls, callbacks and automation IDs. Each card
  has an internal clipping slot, so its immediate parent is that slot. Use the group's card APIs
  rather than treating `children` as a vector of cards. Slots remain in the raw UIA tree as structural
  panels; UIA control/content views skip them.
- The group owns its cards' corner and edge settings. It recomputes them from the visible rows
  during arrangement: one row gets four corners, middle rows get none, and an empty group is zero
  high. `group->SetVisible(card, false)` starts a clipped exit; `SetVisible(card, true)` reveals it
  again. `IsVisible(card)` reads the requested state. Direct `card->visible` still suppresses the
  card's own painting immediately; use `SetVisible` for animated visibility changes.
- `group->Remove(card)` immediately removes a card from interaction/UIA and destroys it **after** its
  exit animation. Hide instead of remove when values and identity should survive. Do not keep using
  a pointer after requesting removal. Ordinary `Widget::Remove` keeps its existing immediate semantics.
- `GroupLayout().radius` defaults to the existing 4-DIP control radius. `spacing` defaults to 1 DIP
  of clear space, with each card painting its own borders. Zero spacing removes the gutter and
  suppresses duplicate separator edges. Change these fields and call `InvalidateLayout()`.
- `CardExpander::header` is a Card subclass. Its `Set`, `icon`, `text`, `detail` and `value` work as
  on any other card; the disclosure arrow reserves a separate 32-DIP trailing area. Operating a
  child control does not also toggle the header.
- `CardExpander::Add` adds to `items`, its retained `CardGroup`. `items->Remove(card)` removes a
  detail card. The header and the details form one outline, with the same small gutter between them.

The public sources have different spacing from the user's current Windows Settings measurement:
[WinUI Gallery SettingsPage](https://github.com/microsoft/WinUI-Gallery/blob/main/WinUIGallery/Pages/SettingsPage.xaml)
declares `SettingsCardSpacing=4`; the Community Toolkit's
[SettingsExpander item template](https://github.com/CommunityToolkit/Windows/blob/main/components/SettingsControls/src/SettingsExpander/SettingsExpander.xaml)
uses no extra stack gap and `BorderThickness=0,1,0,0`. Those are not the private SystemSettings
template. This control's 1-DIP gutter is an explicit choice informed by the user's approximate
2-pixel measurement at 150%, not a claimed WinUI default. Borders and rasterization also affect a
physical-pixel measurement. The existing `Spec::gap` and the example's page gap remain 4 DIPs.
Existing card heights/padding remain unchanged.

## Content column width

`StackLayout::maxWidth` limits the column **after** its horizontal padding and centres that column.
It applies during measurement and arrangement, so a wrapping label measures at the width it will
actually get. `ContentBox()` reports the same constrained box.

Set it on the scroll view's **content layout**, rather than on individual cards. Headings, standalone
cards and groups then share one axis. The viewport and scroll bar keep the window's full width.
This is opt-in; existing layouts remain unlimited. The example's 920 DIPs are an application choice,
not a universal WinUI resource value.

## Expansion, input and automation

`CardExpander(title, detail, expanded)` starts collapsed unless the third argument is true.
`Expanded()` reads its state; `SetExpanded(bool)` changes it. `onExpanded(bool)` runs once per change,
after the state is updated and layout is invalidated. Changes made before first arrangement have no
entrance animation.

The header supports click, Space/Enter and Left/Right to collapse/expand. The existing Tab walk visits
the header and its controls; open detail controls follow in normal tree order. Closing immediately
excludes details from pointer input, Tab and the UIA tree. Focus inside the details returns to the
header, capture is cancelled, and transient controls are dismissed. Children are retained, so edits,
values and automation identities survive reopening. Closing pixels may remain during the animation;
`Widget::AccessibleVisible()` distinguishes that painted tail from logical accessibility.

The header and group expose the existing UIA ExpandCollapse provider. Cached providers for hidden
descendants return `UIA_E_ELEMENTNOTAVAILABLE` until the same descendants become exposed again.

The parent layout's existing rectangle glide owns the reveal height and the movement of following
cards. There is no additional height storyboard chasing that glide. Detail cards stay at their natural
size behind a rounded clip that follows the **drawn** group size. The arrow follows the same reveal.
Reversing mid-flight continues from the current geometry. With animations disabled the change settles
on the next layout/tick. Like other moving widgets, the expander should be placed by a parent layout.

Group row changes coalesce at arrangement, rather than starting an animation in every Add/Remove/
SetVisible call. The group snapshots the current presentation once for the whole batch. Consecutive
entering/leaving rows retain their natural relative offsets and travel behind one shared window.
Painting, hit testing and UIA bounds all use the clipping slots, so a translucent neighbour cannot
show the departing content through its own background. Rows stop participating in input/UIA as soon
as their requested visibility changes, but their closing pixels stay until the exit completes.
Reversing or splitting a run starts from the current drawn positions and clipped windows. Overlapping
natural rectangles in an interrupted run receive disjoint paint windows. Net-zero-height batches use
one local clock; height-changing batches use the parent's geometry clock, including changes inside
an open expander. `Widget::OnGeometryChanged()` keeps these windows in sync after the parent's glide.

`Widget::ClipCorners()` is an optional rounded clip for containers; the default keeps the existing
rectangular clipping path. `Painter::PanelPath()` supplies the same geometry used for panel painting.

## Example and verification

Build the examples, then run `micula-cards.exe`. It demonstrates an ordinary group, a header with an
independent switch, three expandable cards, editable text, batch addition/removal and hiding rows.

```powershell
.\build-msvc\micula-cards.exe --check
.\build-msvc\micula-cards.exe --open
.\build-msvc\micula-cards.exe --render cards-light.png --open --light
.\build-msvc\micula-cards.exe --render cards-dark.png --dark --dpi 144
.\build-msvc\micula-cards.exe --render cards-narrow.png --open --width 500
.\build-msvc\micula-cards.exe --render cards-mid-animation.png --open --frames 2
.\build-msvc\micula-cards.exe --render cards-hiding.png --open --hide-middle --frames 2
.\build-msvc\micula-cards.exe --render cards-showing.png --open --show-middle --frames 2
.\build-msvc\micula-cards.exe --render cards-batch.png --open --batch-hide --frames 2
```

`--check` runs file-font loading, geometry, clipped row exits/entries, run reversals/splits, batch
destruction, net-zero-height updates, focus, hit-testing and real UIA provider checks without opening a
window. `--render` uses the library's Direct2D paint walk on an offscreen bitmap, with a solid page
background. It does not verify live Mica composition, physical mouse/keyboard input or Narrator.
