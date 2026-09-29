# Layout

**Status: design.** This file describes the retained object tree, which is being built on the branch
`layout-experimental` and is not in `master`. Everything below is a decision unless it is under
[Open questions](#open-questions) at the end, which is working notes for the branch.

## Why the page is no longer rebuilt

The library began with a page that rebuilds itself: `Layout()` calls `ClearWidgets()` and creates the
controls again from the window's own fields, and a control's callback writes those fields. For a
dialog with six controls that is small, honest and enough. It stops being enough at four places:

- **The geometry is the page's arithmetic.** Every page writes the same shapes again: a stack with
  gaps, a section heading, a settings card 64 DIP high with a title, a line of detail and a slot for
  the control on its right, a row of buttons as wide as they asked to be, and the content extent the
  scroll bar wants. `examples/gallery`, `examples/settings`, `examples/nav` and the applications
  built on the library each have their own copy, with their own numbers.
- **Nothing survives a rebuild, so scrolling needs a second model.** A wheel notch does not rebuild,
  so the page keeps a scroll offset, a drawn offset, a glide it writes itself, a `ScrollBar` it
  creates and wires up, `ClipRect()`, `ContentTransform()`, `scrolls` on every control inside, and
  the rule that `Layout()` must never run while scrolling. That is the library handing its own job
  to the page.
- **A rebuild during a callback is a trap.** `Layout()` is routinely called from inside a control's
  callback. Everything that is holding something at that moment has to be written to survive it, and
  the library is full of the workarounds: `persistent`, `retired`, the layer that outlives one
  layout, the promise that a control removed mid-message is destroyed after it returns. It works, but
  it is a cost paid by every control, for a page that could just... not rebuild.
- **A flat list has nothing to report.** UI Automation sees the window and then every control as its
  child, in paint order. A card is not a group, a heading is not a heading, and the rows of an open
  drop-down are not item elements, because nothing in the window knows what a card is. That is not a
  gap in the UIA code; it is a gap in the model.

## The tree

`Widget` becomes a node.

| Member | |
|---|---|
| `Widget *parent` | Who owns it. Null at the root. |
| `std::vector<std::unique_ptr<Widget>> children` | What it owns, in order. Order is paint order, hit-test order reversed, tab order, and the order a screen reader reads. |
| `bool visible` | Drawn, hit-tested and read only when true. A layout that switches between pages switches this and nothing else. |
| `T *Add(T *w)` | Takes ownership, appends, returns `w`. The same shape as `Window::Add` today. |
| `void Remove(Widget *w)` | Takes it out. Destruction is still deferred to after the message being handled, which is machinery that already exists. |

A control is no longer a rectangle a page placed. It is a node whose parent's layout chose its
position, and whose size it asked for or was given.

**A rectangle is in its parent's space.** The painter walks the tree with a stack of transforms and
clips -- the window's own, a scrolling container's offset, a card's padding, a position on its way
somewhere -- and hit testing unwinds the same stack. Scrolling is already a transform today, one
level up; this is the same idea with the level count freed. It is also what makes motion cheap: a
subtree that moves is a transform, and nothing has to be told.

A widget may opt out with `space = Space::Page`: its rectangle is then in the window's own space, no
ancestor transform touches it, and what the transforms were doing becomes its own job -- clip itself
to the room it has, keep out of the way of anything that scrolls around it, and be hit-tested exactly
where it draws. That is the honest answer for a widget that hosts a child window or draws a canvas of
its own, and taking it on should be a decision somebody makes on purpose, which is why it is a field
a widget sets rather than a mode the window is put into.

The rectangle a layout arranges is the *target*; what is drawn is a rectangle gliding toward it.
See [Motion](#motion).

## What a control says about itself

`Widget` gains one question:

```cpp
virtual Want Measure(const Room &room) const;
```

`Want` is what it would like to be, per axis, and there are three answers rather than a number:

| | |
|---|---|
| `Content` | As big as its content measures. `size` carries the measurement, and it is a preference: a parent may give more, or less. |
| `Fill` | As big as there is. In a column the `Fill` children share what the others left over. |
| `Fixed` | Exactly `size`, and a parent honours it: a square ring, a control that is one switch wide. |

`Room` is what the question is asked in: the fonts (with the measured-text cache behind them), the
`Spec` in force, the width on offer and the height when it is known. A button answers `Content` with
its preferred width and `Fixed` at the control height; a wrapping label answers `Content` with the
height it needs *at the width on offer*, which is why the width is part of the question; a widget
with no opinion answers `Fill`. A widget with children answers with what its own layout measured.

This is the one part of the protocol that cannot be changed cheaply later, because every control
implements it. **Decided: three states per axis.**

## The Layout

`Layout` is a class of its own, owned by the container it arranges:

```cpp
struct Widget {
    std::unique_ptr<Layout> layout;      // null: children keep the rectangles they were given
    void SetLayout(Layout *l);
};
```

```cpp
struct Layout {
    virtual Want Measure(const Room &room) const;
    virtual void Arrange(const Room &room, const D2D1_RECT_F &box) = 0;
    virtual void Tick(float dt);          // the animations this layout owns
    void Invalidate();                    // mark the subtree dirty
    bool Glide(float dt);                 // move the drawn rectangles, and say whether anything moved
    Spec spec;                            // the numbers, edit in place
};
```

Both passes take the room, because arranging is also where a child is measured against the width it
is *really* being given -- which is not the one it was offered, once a line wraps.

**Nothing arranges inside a callback.** A change marks the subtree dirty, and the window arranges
once, at one defined point in the frame, before it paints. That is what makes a retained tree cheap
(ten changes in one message are one arrange) and it is also the answer to re-entrancy: a callback may
add, remove, hide or restyle anything it likes, and the tree it is running in is never rearranged
under it. The library's habit of calling back into page code with lambdas gets safer, not riskier.

Built-in layouts, in the order they are needed:

| | |
|---|---|
| `StackLayout` | The default, and the one most pages want: children one after another, a gap between them, padding, headings that take their own band. |
| `RowLayout` | Children side by side, each as wide as it asked to be, or sharing the room. |
| `CardLayout` | The settings card, and the layout of the `Card` widget below. |
| `GridLayout` | Columns and rows, with a cell able to span. The one that needs a real measure pass, and the one to build last. |
| `CustomLayout` | A callback: `CustomLayout([](Widget *host, const D2D1_RECT_F &box) { ... })`. The escape hatch for a page that wants to do its own arithmetic, which is what today's `Layout()` is. |

## The spec is a thing

The numbers in today's pages are not arbitrary: a page margin of 24, cards 64 DIP high and 4 apart, a
control slot 16 DIP from the card's right edge, a heading 30 high with 24 above it, a control height
of 32. They are the platform's own settings-page numbers, the same in every Windows app, which is
exactly why they should not be typed into every page.

They become one struct the layouts read -- `Spec`, defaulting to those numbers, with the `theme.h`
metrics folding in and a way for a page to override a field or hand in its own. A layout places; the
spec says what "placed" looks like. A theme change (DPI, accent, density) reloads it and marks the
tree dirty, so a page never sees it.

## The user says the order, not the coordinates

```cpp
auto *page = nav->AddPage();
page->SetLayout(new StackLayout());
page->Add(new Heading(L"Choices"));
page->Add(new CheckBox(L"Show a notification when done", notify, onChange));

auto *card = page->Add(new Card(L"Quality", L"Segmented - a few words, side by side"));
card->Add(new Segmented({ L"Auto", L"High", L"Low" }, quality, onChange));
```

A `Heading`, a `Label` and a `Card` are widgets, so what a page used to draw in `PaintPage()` -- its
headings, its card backgrounds, its lines of detail -- is placed, measured, ordered and reported like
anything else, and `PaintPage()` has nothing left to do.

A `Card` is the platform's control template rather than a shape: an icon if it has one, a title line,
a description line, one control in its slot (a `Button`, a `ToggleSwitch`, a `TextBox`, a `Segmented`)
and the line that reads that control's value back. Windows Settings is built out of that one template,
so a library that looks like Windows Settings is built out of it too -- which is why it belongs in the
library as a widget instead of being a shape every page assembles, and why it is the widget a page
reaches for most.

## Scrolling belongs to the container

A container whose arranged children do not fit scrolls them: the children are clipped to it, it
keeps the offset, it takes the wheel, it holds a `ScrollBar` of its own and glides the offset on the
frame loop. That is what CSS calls `overflow: auto` and what a WinUI `ScrollViewer` is.

What disappears with it: `scrolls`, `ClipRect()`, `ContentTransform()`, `VisibleArea()`, the
`ScrollBar` a page creates and wires up, the `maxScroll` arithmetic, and the rule that a page must
not call `Layout()` while scrolling -- a rule that only exists because a rebuild would fight the
scroll. `overflow = Clip` and `overflow = Visible` cover the containers that mean those.

## Motion

A layout arranges a child into a target rectangle; the child is drawn at a rectangle that glides
toward it. The layout holds both, because it is the thing that knows what changed, and `Tick`
advances them, so a card steps down when something above it opens, a replaced page slides, and a
scrolled list glides -- by the same mechanism, in one place, off the tree's transforms.

With animations off the drawn rectangle is the target in the same frame. That is the rule the theme
already keeps for everything else: to the destination, not frozen halfway.

For v1: the glide, the page switch, and a re-arrange that tweens. Enter and exit transitions after
that.

## Debugging a layout

A page is mostly its layout, and a layout is arithmetic, so the fault is usually a box that is not
where somebody thought it was. There are two ways to look at it and they answer different questions.

**Print the rectangles.** Arrange the tree and write out what each widget came out as -- a test, or a
`--dump` switch on an example. This is what a fault in the arithmetic wants: no window, no GPU, no
screenshot, and a diffable answer. The layouts in this file were built against it.

**Draw the rectangles.** Built with `-DMICULA_DEBUG_LAYOUT=1`, the window draws over the page:

| | |
|---|---|
| red | the rectangle the layout arranged the widget into (`rect`) |
| blue | the clip the widget is painted under |
| orange | a container's content box: its own rectangle less its padding (`Layout::ContentBox`) |
| magenta | `rect` while the widget is drawn somewhere else -- a position in flight |

The whole pass is compiled out without the flag: no code, no colours. A build that has it can still
turn it off at run time with `micula::debug::layout = false`. It draws unclipped on purpose, because
a box that overflows its clip is the one thing worth seeing.

## Input, focus, and the tree

- Hit testing descends from the root, skipping invisible subtrees and anything outside its parent's
  clip, last child first. Capture and focus stay where they are, on the window.
- Tab order is document order, so a page no longer has to keep its control order in step with its
  creation order.
- A pointer move reaches the control under the pointer, and a control that needs to hear about moves
  that leave its own subtree -- today's `ExternalRegion` -- keeps a way to say so.
- A modal child is the old `Layer`: it takes the input under it, `CoverPage()` is the window's page,
  and `modal`, `lightDismiss`, `smoke` and `onDismiss` keep their meaning. A layer is the last child
  of the root, so it paints over everything and needs nothing like `z`.

## Accessibility

The tree *is* the automation tree. That is the point of having one: a `Card` is a group named by its
title, a `Heading` is a heading with a level, a `Page` in a `SideNav` is a page, and the rows of an
open drop-down can be item elements of the drop-down, because a control can have children now.

Most of the list under "Not there yet" in [Window](window.md) is a list of things a flat list cannot
express. The four questions a control answers about itself (`AccessibleName`, `AccessibleType`,
`AccessibleToggle`, `AccessibleValue`) do not change.

## What the window becomes

`Window` becomes the root of the tree -- it is a `Widget` -- and it holds the root widget the page
builds into: the client area, which is WinUI's `Window.Content` and the thing a page adds to. Its own
furniture is its other children: the title bar, its buttons, the pane. `kCaptionH` stops being a
constant and becomes the height of a widget, and the minimise, maximise and close buttons join the
automation tree, which they are missing from today.

The frame becomes:

1. messages, which change the tree and mark it dirty
2. arrange, if anything is dirty
3. tick the animations
4. paint

## What survives

- The control API: `rect`, `visible`, `enabled`, `hover`, `pressed`, the callbacks, and the lambda
  style. A control is now *told* its size instead of working it out from the page's arithmetic.
- The painter, the fonts, the measured-text cache, the theme, the glyphs, `MinSize`.
- The interaction rules that were paid for: capture, `pressed`, `Dismiss()`, the deferred
  destruction, focus and the focus ring, the keyboard behaviour of every control.

## What goes

| Gone | Replaced by |
|---|---|
| `Window::Layout()` and the rebuild | the tree; a change marks it dirty and the frame arranges |
| `ClearWidgets()`, `persistent` | the tree, and hiding |
| `rect` in page coordinates | `rect` in the parent's space |
| `scrolls`, `ClipRect()`, `ContentTransform()`, `VisibleArea()`, the page's `ScrollBar` | a container that overflows |
| `PaintPage()` | widgets: `Heading`, `Label`, `Card` |
| `z` | tree order |
| A control laying itself out by calling the page's `Layout()` | its own children, and its own layout |

## Migration

On the branch, in this order. Each phase is something a person can look at and disagree with:

0. **This file.** The design, agreed before code. *(you are here)*
1. **The core.** `Widget` as a node, the transform stack in the painter, hit test and focus over the
   tree, `Layout` with `Measure`/`Arrange`/`Invalidate`/`Tick`, `StackLayout`, `Spec`, and the
   widgets a page needs to say anything at all: `Label`, `Heading`. Prove it on a new small example
   rather than by porting one of the three, so the port is not confused with the core.
2. **Scrolling and the card.** Overflow in a container, the scroll bar it owns, the glide; `Card` and
   `CardLayout`. Port `examples/settings`, which is the smallest page that uses both.
3. **Pages and the grid.** `SideNav` as a root widget holding pages and switching by visibility;
   `Page` with its own layout; `GridLayout`; port `examples/gallery` and `examples/nav`.
4. **Accessibility and polish.** Groups, headings and item elements in the automation tree; the
   animation set beyond the glide; the docs rewritten (`window.md`, `controls.md`, `widget.md`,
   `README.md`) and `docs/layout.md` (this file) turned into the user-facing document.

While the branch lives, `master` keeps shipping: merge `master` into the branch whenever it moves.
The conflicts will be in `window.h`, and they will be real -- the branch replaces its middle. Nothing
large should be refactored in `window.h` on `master` while this is open.

Merging back is a breaking release. The migration note is short, because the new model is the smaller
one: a page stops computing rectangles and starts adding children, and a control that used to read
`rect` reads the rectangle its parent's layout gave it. `docs/layout.md` is the note.

## Open questions

1. **How much of the window is a widget in v1.** The title bar, the pane and the window buttons are
   children of the root in this design; whether they become widgets in the first pass or the second is
   a question about how much moves at once, not about the model.
2. **What the v1 animation set is.** The glide, the page switch and a re-arrange that tweens are in
   this file; enter and exit transitions are not.

Answered on 2026-09-29, and the answers are in the sections above: `Window` is the root and holds the
root widget; a rectangle is in the parent's space, with an opt-out for a widget that would rather do
all of it itself; `Want` has three states per axis; a container that overflows scrolls without being
asked. `CustomLayout` stays, and is written: it is the escape hatch for a page with one odd row, and
the shape `Window::Layout()` becomes for a page that keeps its own arithmetic.
