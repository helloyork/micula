# Window

`struct micula::Window`, in `micula/window.h`.

Subclass it, override `ClassName()`, `Title()` and `Layout()`, then call `Create()` and
`Run()`.

```cpp
struct Page : micula::Window {
    bool enabled = true;

    const wchar_t *ClassName() const override { return L"MyApp.Page"; }
    const wchar_t *Title() const override { return L"My App"; }

    void Layout() override {
        ClearWidgets();
        auto *sw = Add(new micula::ToggleSwitch(L"Enabled", enabled,
                                                [this](bool on) { enabled = on; }));
        sw->rect = micula::Rect(24, 56, 300, 32);
    }
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, wchar_t *, int) {
    // No EnablePerMonitorDpi and no CoInitializeEx: Create does both, and only when nobody else
    // has said anything. See Requirements in the README.
    Page page;
    return page.Create(360, 120, false, nullptr) ? page.Run() : 1;
}
```

## Creating and running

| Member | Description |
|---|---|
| `bool Create(int dipW, int dipH, bool canResize, HICON icon)` | Registers the window class, creates the window centered on its monitor with a client area of `dipW` x `dipH` DIPs, applies the theme, calls `Layout()` and shows it with `showCommand`. `canResize` adds the resize border and the maximize button. `icon` may be null. Returns false if the window could not be created. |
| `int Run()` | Runs the message loop until the window is destroyed. Returns the `WM_QUIT` exit code: 0, or the value passed to `PostQuitMessage`. Releases the window's Direct2D resources before returning. |
| `int showCommand` | How `Create` shows the window. Default `SW_SHOW`. With `SW_HIDE` the window stays hidden until the program shows it. |

Esc and the title bar's close button post `WM_CLOSE`. To ask before closing, handle
`WM_CLOSE` in `OnAppMessage` and return true to keep the window open.

Frames are run only while something asks for them, and only while the window is somewhere
it can be seen. A hidden, minimised or cloaked window gets none at all -- an animation still
going does not paint into a window nobody is looking at, and a minimised window is the
worst of it, because every one of those frames is drawn and then thrown away. A control the
page has scrolled out of `ClipRect()` does not ask for frames either: it is not drawn, so it
is not animated. Neither case loses the animation -- the frame clock is picked up again when
frames resume, so an ease carries on from where it was rather than jumping forward by
however long the window was away.

Occlusion is not part of that. Windows has no query for "another window is over this one",
and the approximations of one are wrong for a partly covered window and for a layered one,
so a window that is merely covered still paints. `Visible()` is the same test the frame loop
uses, for a page that wants to ask it of itself.

## The app

**A program is over when its main window is closed.** The first window to join the app's loop is the main one,
and a window opened beside it -- a log, a picker, a preview -- does not take the program with it when it goes.
Which window is the program's is the page's to say: `set_main_window(false)` on the first one means the program
has no main window and ends when the last of its windows has gone, and `set_main_window(true)` on a later one
hands that part to it. A `Popup` -- a menu, a tip -- is never a thing in its own right: it is over a window
rather than beside one, it can never end the program by closing, and one that has ever been shown stays in the
loop for as long as the page does. Which is why what ends a program is a *window* going and not the list of
surfaces emptying -- a program that waited for an empty list waited forever after the first menu anybody
opened, with its window gone and its screen empty and the process running on with nothing to do.

`Surface::Run()` runs that one surface -- a window, usually. A program with more than one surface, which
is what a menu or a tip is, makes an `App` and gives it the surfaces:

```cpp
int WINAPI wWinMain(HINSTANCE, HINSTANCE, wchar_t *, int) {
    App app;
    MainWindow main;
    if (!main.Create(1040, 700, true, nullptr)) return 1;
    app.Add(main);
    app.Add(menu);                 // a window, a menu, a tip: whatever draws
    return app.Run();
}
```

| Member | Description |
|---|---|
| `std::vector<Surface *> surfaces` | What it pumps, in the order they were added. |
| `void Add(Surface &s)` | Adds a surface to the loop. It may be added while the loop is running, which is what a menu a page opens should do. |
| `void Remove(Surface &s)` | Takes one out. The loop ends when the last one is gone, and a surface that is destroyed does this itself. |
| `int Run()` | Runs until there is nothing left to run. Returns the exit code of the `WM_QUIT` that ended it. |
| `void Quit(int code = 0)` | Ends the loop where it stands. |
| `bool Running() const` | Whether `Run` is on the stack. |

On `Surface`, two things go with it:

| Member | Description |
|---|---|
| `App *app` | The app this surface is in, or null. Set by `App::Add`. |
| `int Run()` | `App app; app.Add(*this); return app.Run();` -- the same loop, for a program with one surface. |

And one more on `Window`, which is a window's rather than a surface's:

| Member | Description |
|---|---|
| `virtual void OnClosed()` | The window has been destroyed, and this is the last thing it does about it. |

- **One loop, one thread, as many surfaces as you like.** Every surface in the app is ticked and
painted by the same loop, on the thread that made them, so a surface that did not start the loop
still animates -- and nothing in the library has to be synchronised for it. A menu opening over a
window therefore does not stop the window: the loop turns frames for both, and the window's
animations go on running underneath.
- **The App does not have to outlive the surfaces**, and it is usually a local in `wWinMain`, which
means it is destroyed *first*. A surface still in it clears its own pointer, and the App's destructor
clears whatever is left, so the order the two die in does not matter.
- **Owning a window a page made.** A window cannot be deleted inside its own message, so `OnClosed`
is where a page hears about it, and the usual shape is a `Post` to the window that made it -- see
`Window::OnClosed` and `Post`, and the `App` whose loop is what both windows are in.
- **A surface made while the loop is running** is brought up to it as it joins: its frame clock and
its caret timer start there, and `WM_DESTROY` hands back its device. The tail of
`Run()` does that only for the surfaces still standing when the loop ends, which is the other way out
-- a window a page opens and closes all day would otherwise take a D3D device with it every time.

## Popups

A menu, a tip, or a flyout that has to be able to leave the rectangle it came from. `Popup`
is a `Surface` with a window of its own -- a real `WS_POPUP` one, with no caption and no client area
under one -- rather than a `Layer`, because a layer is drawn inside its window and cannot cross its
edge... and because a menu has to be able to exist with no window behind it at all, which is what a
shell tray menu is.

It joins the app like anything else, so it is drawn and ticked by the same loop: the window under it
keeps animating while it is up, and a menu that animates itself is possible.

**And it arrives rather than appearing.** A popup fades in over `motion::kFast`, as one group -- the panel,
its shadow and everything on it together -- because a box that is simply *there* on the next frame reads as a
drawing fault rather than as something opening. `Popup::Arrival` is the ramp, and whoever shows the popup
starts it (`Popup::Arrive`); with the animation switch off it answers 1 from the first frame, so a machine
that asked for no animations gets a popup that is there at once rather than one that is invisible for a
sixth of a second. A subclass that wants its own entrance has both halves of the same idea to hand: the group
(the root's `Widget::opacity`, or the colours it draws) and, later, one per element.

```cpp
struct SettingsMenu : micula::Popup {
    const wchar_t *ClassName() const override { return L"MiculaSettingsMenu"; }
    bool OnMessage(UINT m, WPARAM wp, LPARAM lp) override {
        if (m == WM_KEYDOWN && wp == VK_ESCAPE) { Hide(); return true; }
        return false;
    }
};

SettingsMenu menu;
menu.Add(new micula::Label(L"Settings"));
menu.SetLayout(new micula::StackLayout());
menu.Show(x, y, 220, 120);        // screen DIPs
app.Add(menu);
```

| Member | Description |
|---|---|
| `virtual const wchar_t *ClassName() const` | The window class it is made with. Registered on demand, like a window's. |
| `bool Show(int x, int y, int dipW, int dipH, UINT dpiOf = 0)` | Shows it with its corner at `x, y` screen DIPs and a client area of `dipW` x `dipH`. Topmost. `dpiOf` says which monitor's DIPs those are -- pass it if you have worked it out (`dpiapi::ForPoint` takes pixels), and 0 asks the point. Returns false if the window could not be made. |
| `void Place(int x, int y)` | Moves it, in screen DIPs. |
| `void Place(int x, int y, int dipW, int dipH)` | Moves it and resizes it. The tree is arranged into the new box and `OnPlaced` runs. |
| `void Hide()` / `bool Shown()` | Takes it off the screen, and whether it is on it. The window and the tree stay where they are, so showing it again is one call. |
| `bool activates` | Whether it is allowed to take the foreground. Set it before `Show`: a tip or a menu over an active window wants false, or the caption of the window it belongs to blinks. |
| `DWORD extraStyle` | Extra extended styles for its window, on top of `WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP`. Set it before `Show`. A tip is `WS_EX_TRANSPARENT`, so that a click where the tip is lands on what the tip is about. |
| `virtual bool OnMessage(UINT, WPARAM, LPARAM)` | A message the popup answers itself -- the keys, the wheel, activation. The press, the move and the release are the surface's already. |
| `virtual void OnPlaced()` | Shown, moved or resized: where a menu puts its tree. |
| `View *EnsureContent()`, `T *Add(T *)`, `void SetLayout(Layout *)` | The tree, the same three a window has. |

Both halves of the mouse and the finger path are shared with `Window` -- `Surface::HandMessage` is the
one place a press is turned into a click -- so a menu answers a finger exactly as a window does.
What it does *not* have is a caption, the page hooks, a frame the size of the screen, or an automation
tree of its own: `UiaElement` is the window's.

## Tips

A control's `tips`, shown when the pointer comes to rest on it: `tip.h`. `Widget::tips` is one string and
it is both the tooltip and what a screen reader is told, so there is nothing for a page to declare twice.

`Tips` is the whole of the behaviour, and attaching it to a surface is the whole of using it:

```cpp
struct MyWindow : micula::Window {
    micula::Tips tips{ *this };
    ...
};
```

Every control in that window with `tips` set now has a tooltip. The box is a `Popup`, so it may cross the
edge of the window it belongs to; it is as big as its words and no bigger, and it takes neither the
foreground nor the click. **A finger triggers no tip**, because a tip waits for a pointer to rest and a
finger does not rest. It fades in as it arrives -- see [Popups](#popups) -- and only *arrives* once: being asked
for the same words in the same place again is not work, and being put somewhere else while it is up does not
make it fade a second time.

**It hangs off the control and not the pointer**: centred on the control and one small gap under it, so
that the control stays readable while its own sentence is up, and so that the tip stays put while the hand
moves across a wide one. A tip with no room left under the control flips above it, and one with no room
anywhere is kept inside the work area, less the room its own shadow needs -- so a tip against the edge of
a screen shows its shadow whole and a gap beside it rather than sitting on the last pixel, and a control on
the last row of a window is exactly the case a tip is a `Popup` for. `TipFollow::Pointer` puts the box
below and to the right of the hand instead, and follows it:

```cpp
micula::Tips tips{ *this };
tips.Follow(micula::TipFollow::Pointer);
```

**And a widget that draws a set of things says something different about each of them.** A `SideNav` in
its rail has a name for every icon and draws none of them -- hovering one is the only way to read it -- and
a row is not a widget that can carry a string. So a tip is asked of the *point*: `Widget::TipAt(x, y)`
answers with `Widget::Tip { std::wstring text; D2D1_RECT_F box; bool beside; }`, both the point and the box
in the space the widget's own `rect` is in, which is the space every pointer hook is handed. The pane's
answer is the row under the pointer, that row's own box, and `beside` -- a column of icons has the next one
under every row, and words that land there hide the icon the hand is going to -- which puts the box against
the pane's right edge, reading as a label for the row it names. Empty words mean nothing to say *here*: the
same pane says nothing at all once its labels are drawn, which is what it does in `Minimal` and in the two
styles that expand whenever they are open.

A rail is one widget from the first icon to the last, so the window sees no hover change when the hand goes
from one row to the next. The widget that knows says so -- `Surface::TipChanged` -- and the tip is put away
and its dwell starts over, which is the rule Windows' own hover tracking uses: a pointer that has not left
the row it was on has not asked a new question. The watchdog asks what the tip is about as well as where the
pointer is, so a pane that peeks open under a still hand stops standing in for the labels it has just drawn.

| Member | Description |
|---|---|
| `Tips(Surface &s, UINT dwellMs = 0)` | Attaches to a surface. The dwell is the machine's own `SPI_GETMOUSEHOVERTIME` -- 400 ms out of the box -- unless one is passed. |
| `void Dwell(UINT ms)` | How long the pointer has to rest before the tip appears. |
| `void Follow(TipFollow f)` | What the box hangs off: `TipFollow::Control`, the default, or `TipFollow::Pointer`. |
| `Tip *Box()` | The box, for a page that draws something else in it. |
| `Tip` | The `Popup` itself: `std::wstring text`, and `For(text, anchor, at, follow, beside = false)` to show it -- `anchor` is the box the words are about and `at` the hand, both in screen pixels, and `beside` puts the box beside that box rather than under it. Subclass it and override `PaintFurniture` to draw something else. |

## Menus

A list of things to pick, in a window of its own: `menu.h`. A right-click on a control that has one opens it
where the hand is, and so do the Menu key and Shift+F10 under the control the keyboard is on.

```cpp
struct MyWindow : micula::Window {
    micula::Menus menus{ *this };     // one line, as a `Tips` is
    ...
};

panel->contextMenu = [this](micula::Menu &m) {
    m.Item(L"Copy", micula::glyph::kCopy, L"Ctrl+C", [this] { Copy(); });
    m.Item(L"Paste", micula::glyph::kPaste, L"Ctrl+V", [this] { Paste(); }, CanPaste());
    m.Separator();
    m.Check(L"Show hidden", hidden, [this](bool on) { SetHidden(on); });
    m.Sub(L"Sort by", [](micula::Menu &s) {
        s.Item(L"Name", nullptr, L"", [this] { SortBy(Name); });
        s.Sub(L"More", [](micula::Menu &t) { t.Check(L"Folders first", foldersFirst, Set); });
    });
};
```

**The builder runs every time the menu opens**, so a row that is disabled *now*, a tick that is on *now* and a
list that changed under it are the page's own state read at the moment it matters: there is no second copy of
the truth to keep in step as the page changes.

A menu is a `Popup`, so it may cross the edge of the window it belongs to; its corner goes at the point it was
asked for and flips to the other side when the work area runs out, and it is kept inside the work area less the
room its own shadow needs -- as a tip is, and for the same reason. It fades in as it opens, and fades in again
every time it is opened -- see [Popups](#popups). It answers the arrows, Home/End, Enter and
Esc, a click on a row picks it, and a click anywhere else closes the menu **and stops there**: what the menu
was opened over is not also pressed by the click that put the menu away.

**It takes neither the activation nor the focus.** A menu that activated would dim the caption of the window it
hangs off for as long as somebody is merely using it, so the input the menu needs is the input that window was
sent, handed to the menu first while it is up -- see `Surface::onInput`. The window keeps the foreground, its
caption stays lit, and the same door is what closes the menu when the window goes away.

**A row that holds another menu is a way in rather than a choice.** `Sub` takes the builder of that menu, run
every time *it* opens exactly as the page's builder is run for this one, and the menu opens beside the row:
the row's own box on the screen, the panel's left edge landing **4 DIP inside the row's right edge** -- the two
overlap rather than stand apart, which is what WinUI does (`CascadingMenuHelper::OpenSubMenu`: `position.X +=
subItemWidth - m_subMenuOverlapPixels`, and `m_subMenuOverlapPixels` is 4) -- and its top on the row's own,
which is where WinUI aligns it too ("align top of owner with top of presenter"). Flipped to the left of the
menu it came from when the work area has no room to the right of it, the same four the other side. Three ways
in -- resting on the row for as long as the platform's own menus wait (`SPI_GETMENUSHOWDELAY`), a click on it,
or the right arrow on it -- and three ways back out, one level at a time: the left arrow, Esc, or a hand moving
onto another row of the menu it came from. **A click is a stronger way in than a rest**: what it opens stays open
while the hand travels to it -- across the rows of this menu in between, which is right and up for a submenu that
opened upwards -- until that row is clicked again, which puts it away rather than opening it a second time, or
another row that holds one is clicked, which switches to that one on the same terms. A submenu is a `Menu` like its parent, in a `Popup` of its own,
which is what lets one be nested inside another without limit and what gives each level the entrance every
popup has.

**A menu with more rows than room scrolls rather than being cut off at the bottom of the screen.** It is
measured against the work area it lands in, less the margin its shadow needs -- the same room a tip keeps
itself in -- and a menu that does not fit is that tall and no taller, with the rows past its view reachable
the three ways anything scrolling is. The wheel, where `SPI_GETWHEELSCROLLLINES` decides what a notch is as it
does everywhere else. A finger on the rows, which follow the hand rather than gliding after it -- and which are
*thrown* when the hand is lifted fast, because a hand being careful does not have to lift to be precise, so a
release at speed is a hand asking for distance: a slow one leaves the rows where they are, and a hand or a
wheel put back on them catches a throw that is still in flight. And the arrow keys, which scroll the row they
arrived on into view **and the row past it on the side they are walking towards** -- so the choice is never the
last row in sight and a long menu says which way there is more of it, while a hand coming to rest on a row is
not a direction and drags nothing along behind it. **Up and down are clamped at the ends and nowhere else**, so
the end of a menu is a row wholly in view rather than half of one, and the scroll itself is a glide through the
frame loop rather than a jump -- a menu that has arrived and is being scrolled is still animating, and says so.
The bar is the pane's own `ScrollBar`, drawing over the rows in the panel's padding 4 DIP in from the edge of
the menu and appearing when the pointer moves over the rows as well as when something scrolls them: a menu
whose rows fit has no bar at all. The rows are clipped to the view they have, which is the one line that keeps
a half-scrolled row from being drawn over the padding above or below it.

| Member | Description |
|---|---|
| `std::function<void(Menu &)> contextMenu` | A widget's menu, filled in when it is opened. Null for a widget with none, and then a right-click goes up to the nearest widget above it that has one -- a click on a card's label is a click on the card. |
| `Menus(Surface &s)` | The trigger, attached to a surface like `Tips`: a right-click, the Menu key, Shift+F10, and handing an open menu the surface's input. |
| `bool Menus::Open(Widget *w, POINT at)` | Opens the menu `w` (or the nearest widget above it) has, at `at` in screen pixels. For a menu somewhere the hand is not: a button's `onClick`, a shortcut of the page's own. |
| `Menu &Menus::Box()`, `bool Menus::Shown()`, `void Menus::Close()` | The box, and putting it away from the page. |
| `Row &Menu::Item(text, icon, shortcut, onPick, enabled)` | A row that does something. The shortcut is drawn at the far end of the row, and the icon in the column at the near one. |
| `Row &Menu::Check(text, checked, onCheck, icon)` | A row that is a switch. `onCheck` is handed what the state *becomes*, so the page writes `hidden = on` and nothing in the menu owns the state it is showing. |
| `Row &Menu::Sub(text, build)` | A row that holds another menu, drawn with the chevron that says there is more behind it. `build` fills that menu in every time it opens, and it may hold a `Sub` of its own. |
| `Row &Menu::Separator()` | A line between rows -- 9 DIP of room and 1 of ink. |
| `bool Menu::Scrolls()`, `float Menu::ScrollMax()` | Whether a menu has more rows than the room it landed in, and how far they can be scrolled. A menu that does not fit is capped to the work area less the room its shadow needs, the same room a tip keeps itself in, and scrolls inside that -- see below. |
| `Menu::onClose` | Told when the menu has closed itself, which is where a page hears that it went. |

## Accessibility

micula's controls are drawn by hand, so none of what Windows' own controls get for free comes with
them: a control is a rectangle a screen reader cannot see into unless the window says so. It says so
through UI Automation, and there is nothing to switch on -- a program that never runs beside a
screen reader does no UIA work at all, and one that does gets a tree of the page's controls.

What a control has to say is a handful of virtuals, and one string for the page's half of it:

|---|---|
| `std::wstring tips` | The tooltip text, and the same string is what a client reads as the help text. One string, because what a control says in a tooltip and what it says to somebody who cannot see it are the same thought. `Tips` (tip.h) is what shows it -- see [Tips](#tips). |
| `std::wstring accessibleName` | What the page calls the control, for the times the page knows a name the control does not -- the words beside a switch are page text and are not part of it, and a page cannot override a virtual on a control it did not write. Set, and it wins over `AccessibleName()` below. |
| `virtual const wchar_t *AccessibleName() const` | What the control is called. Null for a control whose whole content is a glyph, which is an honest answer and better than a name made up from the class. |
| `virtual int AccessibleType() const` | The UIA control type -- `UIA_ButtonControlTypeId` and the rest. `UIA_CustomControlTypeId` by default. |
| `virtual int AccessibleToggle() const` | -1 when the control is not a switch; otherwise 0 off, 1 on, 2 indeterminate. |
| `virtual bool AccessibleValue(std::wstring &out) const` | False when there is no value worth reading. True fills `out` with what a client should say -- `"40%"` rather than 0.4, because the formatting is the control's business, not the client's. |
| `virtual bool AccessibleActionable() const` | Whether `OnActivate` -- Space -- does something a client may ask for. Invoke and Toggle both land on it, so a control wired up for the keyboard is wired up for a screen reader by the same code. |
| `virtual int AccessibleItems() const`, `virtual bool AccessibleItem(int i, Widget::Item &out) const` | **The things a widget *is*, when it draws a set of them rather than one**: the rows of a list, the cells of a segmented control, the items of a pane. The children of its element are those items rather than its child widgets -- one element per row, which is what WinUI's own list reports, and no widget per row. An item carries its name, its kind, the page's own index for it, whether it is the chosen one, where it is drawn and whether it is on screen at all. See `Widget::Item`. |
| `virtual bool AccessibleRange(float &value, float &minimum, float &maximum, float &step) const` | The number a control's value *is*, for the controls whose value is one -- a slider, a progress bar -- and what UIA reads with `IRangeValueProvider`. False for a control with no range, which is not a control whose range is all zeros. |
| `virtual int AccessibleExpanded() const` | -1 when this is not something that opens; otherwise 0 collapsed, 1 expanded. A drop-down's list and a navigation pane are the two. |
| `virtual bool AccessibleScroll(float &percent, float &view, bool &canScroll) const` | For a container that scrolls: how far down it is (0 to 1 of what there is to scroll), how much of the content it is showing (0 to 1) and whether there is anything to scroll at all. The percentages are worked out by the container because it is the only side that knows both halves of them. |
| `virtual bool AccessibleWritable() const` | Whether the control can be changed from outside at all. False everywhere by default -- a control is read-only until it says otherwise -- and it is the one answer a client decides what to offer from, so the property `IsReadOnly` and the pattern's own `get_IsReadOnly` are both this. |
| `virtual bool AccessibleSetValue(const std::wstring &text)` | Write the text of a control whose value is one -- a field. True if it was taken. |
| `virtual bool AccessibleSetRange(float value)` | Write the number of a control whose value is one -- a slider. The control snaps it to its own step and range before anything sees it, so a client cannot put it where a hand could not. |
| `virtual bool AccessibleSelect(int i)` | Choose one of the things the control draws -- a row of a list, a cell of a segmented control, an item of a pane, by the page's own index. |
| `virtual bool AccessibleSetExpanded(bool open)` | Open or close a control that opens -- a drop-down's list, a navigation pane. |
| `virtual bool AccessibleSetScroll(float percent)` | Move a container that scrolls, 0 to 1 of what there is to scroll. |

**Every one of those is the page's own path and not a way around it.** `AccessibleSetValue` ends in
`SetText` and the same `onChange` a keystroke runs; `AccessibleSelect` in `Select` and the same
`onSelect` a click runs; `AccessibleSetExpanded` in `SetOpen`; `AccessibleSetRange` in the slider's
own step, `onChange` and `onCommit`. So a page cannot tell a screen reader from a hand, which is the
whole of what makes writing safe to have: there is no second kind of change that a page could be
right about and this wrong about. The writing is a dozen lines per control because the control
already had the path, and the only new thing is that something other than a gesture can ask for it.

- **What a control does not have to say.** Its rectangle (in screen pixels, cut by the containers
  that clip it), whether it is enabled, whether it takes the keyboard, whether it has the keyboard,
  and that it is there at all, all come from the widget. So does the tree: a widget's children are
  its items if it is made of them and its child widgets if it is not, and the window is the root.
- **Nothing that cannot be seen is answered for.** `IsOffscreen` is true for a control a scroll has
  taken out of the container showing it -- and for an item a list has carried out of its panel -- and
  its bounding rectangle is the part of it that is really there, which is nothing. `VisibleArea()` is
  where that answer comes from: every container above it that clips, intersected.
- **The things beside a control are what it is called.** `Card::Set` puts the card's own line on the
  control it is given, unless the control answers a name of its own: a switch with no label is named
  by the row it is on, and what a drop-down *is set to* is its value rather than its name.
- **Focus is announced.** Every move of the keyboard focus raises
`UIA_AutomationFocusChangedEventId`, which is the event a screen reader follows the Tab key by.
- **And so is a change, by comparison.** A client that has read a row has to hear about the next one,
and there is nowhere in a widget that knows it changed: a page changes a control through the control's
own path or by writing a field, and either way the window is the only place that can see both sides.
So once per frame it draws, it looks at the handful of things a change *is* -- what a control says,
what a slider says, whether something is open, where a page is scrolled to, which of a set is chosen --
and tells a client that is listening which of them differ from the frame before: `PropertyChanged` for
the four, and for the fifth `SelectionItem.IsSelected` on each row that changed, `ElementSelected` for
the row that arrived and `Selection.Invalidated` for the set it belongs to. The table it compares
against is kept whether or not anybody is connected, which is what keeps it honest: a change made
while nobody was listening is answered by the state rather than by silence, and the change that makes
a client attach in the first place is not the one thing it never hears. A page being scrolled is
announced in half-percent steps and not per frame, since a client that heard about every frame of it
would hear about nothing else.
- **And the tree, which is the half a value cannot carry.** A client does not hold values, it holds
*elements*, and the same frame that compares values also compares shape: who is a child of whom,
which children a control has and in what order, and how many rows or cells it says it is made of. An
element that arrived, one that left, and the same ones in another order are none of them about a
control -- no property says "the thing you are holding is not the thing that is there" -- so they go
out as `UiaRaiseStructureChangedEvent` instead: `ChildRemoved` before `ChildAdded`, so a client
following along never has two elements claiming the same place, `ChildrenReordered` when the set is
the same and the order is not, and `ChildrenInvalidated` when a list's options were replaced, where
every id the client holds for it is the wrong one and there is no id that can name the set that took
its place. Nothing is said about the first frame the table is filled on: a window whose whole tree has
just been written down has had nothing added to it.
- **A row is an element and has its own runtime id.** `GetRuntimeId` answers `{AppendRuntimeId, uid}`
for a control and `{AppendRuntimeId, uid, row}` for a row of one, because to a client the rows of a
list are elements in their own right -- it reads "30 days, 4 of 6, selected" and may hold that row
across frames. Two elements under one id is the confusion the id exists to prevent: with the row
sharing its control's id, every per-row announcement read as an announcement about the whole list.
- **Reading and writing, and a refusal is an answer.** A client reads what every control says and
writes the ones that say they can be written -- a slider's value, a field's text, the choice in a
list or a pane, whether something is open, where a page is scrolled to -- through
`IValueProvider`, `IRangeValueProvider`, `ISelectionItemProvider`, `IExpandCollapseProvider` and
`IScrollProvider`. A control that cannot do the thing refuses in the way a client expects
(`UIA_E_NOTSUPPORTED`, `UIA_E_INVALIDOPERATION`), which it can act on, rather than accepting the
call and doing nothing, which it cannot.
- **`UIAutomationCore` is not linked.** Its four functions are looked up at run time, so a program
built on micula takes on no new load-time dependency, and a machine without them is a machine whose
screen readers see the window as they did before -- the same reasoning as
`DCompositionWaitForCompositorClock`, which is resolved for a different reason: that one is Windows
11 only.
- **Not there yet.** The scroll bar itself, which is drawn rather than published as a range of its
own, and a change in the *shape* of a page: a control that arrives or leaves says nothing, so a
client holding a tree of it has to find out for itself.

## Page callbacks

All virtual. `ClassName()` and `Title()` must be overridden.

| Callback | Default | When |
|---|---|---|
| `const wchar_t *ClassName() const` | | Once, in `Create`. The window class name; unique per window type. |
| `const wchar_t *Title() const` | | Window title, also drawn in the title bar. |
| `void Layout()` | nothing | In `Create`, on resize and on DPI change. Call it yourself when the page changes shape -- and not when it scrolls: that is `ContentTransform()`. Start with `ClearWidgets()`. |
| `void PaintPage(const Painter &p)` | nothing | Every paint, before the controls. Draws what is not a control: headings, card backgrounds, labels. |
| `void OnDefaultAction()` | nothing | Enter, when no control has focus. |
| `void OnCancel()` | posts `WM_CLOSE` | Esc. |
| `void OnTick(float dt)` | nothing | Every animation frame. `dt` is seconds since the last frame, at most 0.1. |
| `bool AnimationWanted() const` | `false` | Before every frame. Return true to keep frames coming for an animation of the page's own. |
| `bool OnAppMessage(UINT m, WPARAM wp, LPARAM lp)` | `false` | Messages the window does not consume. Return true if handled. See [Messages](#messages). |
| `D2D1_RECT_F ClipRect() const` | empty | The scrolling part of the page. Controls with `scrolls` set are clipped to it and take input only inside it. A control that floats over the page -- one with a `z` -- is not: it is drawn whole, because its shadow is meant to reach outside its own rectangle, and it keeps itself inside whatever room it has. Empty means the page does not scroll. |
| `void ContentTransform(float *dy, float *opacity) const` | `0`, `1` | Vertical offset and opacity for the scrolling controls while painting. **A page's scroll lives here** (`dy = -drawn`); hit testing subtracts the same offset, and the points a control is given in its callbacks have had it taken off already. The opacity is for a page arriving or fading in. `PaintPage` is not transformed. |
| `void MinSize(int *w, int *h) const` | `0`, `0` | Smallest client size in DIPs the window can be resized to. 0 means no limit. |

## Controls

| Member | Description |
|---|---|
| `T *Add(T *widget)` | Takes ownership of `widget`, sets its `owner`, appends it and returns it. The order is paint order and Tab order. |
| `void ClearWidgets()` | Removes every control that is not `persistent`. |
| `std::vector<std::unique_ptr<Widget>> widgets` | The controls. |
| `Widget *focused` | The control with keyboard focus, or null. |
| `void SetFocusTo(Widget *w)` | Moves focus to `w` (or clears it with null). The previous control gets `OnBlur()` and the new one `OnFocus()`. |
| `void MoveFocus(int delta)` | Moves focus forward (1) or back (-1) through the focusable controls, as Tab does. |
| `Widget *capture` | The control the mouse button went down on, until it is released. |
| `bool showFocusRing` | Set when Tab moves focus, cleared by a click. Controls draw a focus ring only while it is set. |

## Layers

A `Layer` is a control that floats over the page: a dialog, a flyout, a menu, a tip. A widget with
a `z` already paints over the page and is reached by the pointer before it; a layer adds the things
that make it a layer rather than a control that happens to be raised.

| Member | Description |
|---|---|
| `bool modal` | Default true. The layer takes the input *under* it: its rectangle is the page (see `CoverPage()`), so a click that is not on something the layer itself put there lands on the layer and stops there. |
| `bool lightDismiss` | Default **false**. A click that misses `Body()`, or the window losing activation, calls `onDismiss`. **A flyout turns this on; a dialog does not** -- Windows draws the same line: `Popup` carries `IsLightDismissEnabled`, and `ContentDialog` has no property of the kind and can only be answered with a button. |
| `bool smoke` | Default false. Dims what is behind it, over black, by `smokeAlpha`. The same black the pane puts on a page it floats over. |
| `float smokeAlpha` | Default 0.14. The dim's own alpha: a dialog that wants to be the only thing on screen turns it up, a suggestion turns it down. |
| `bool escape` | Default true. Esc closes it -- the window offers Esc to the layer before the page's `OnCancel()`. A dialog with a job still running says false. |
| `std::function<void()> onDismiss` | What "closed" *means*, which is the page's: it clears its flag and lays out again. |
| `virtual D2D1_RECT_F Body() const` | The part of the layer that is the layer's own content. A click in `rect` outside it is a click on the dim. The default is the whole rectangle -- a layer with no dim, and so nothing to miss. |
| `virtual std::vector<Widget *> FocusRing()` | What Tab walks while the layer is open, in order. Empty leaves the page's own order alone, which is right for a layer that is only a picture. |
| `virtual Widget *DefaultButton()` | Enter, when nothing in the layer holds the focus. |
| `D2D1_RECT_F CoverPage() const` | What a layer covers: the page, and not the title bar above it. A page sets its layer's `rect` to this in `Layout()`. |
| `void Close()` | Asks the layer to go away, and the page's `onDismiss` runs **at once**: what the page keeps is its own state, and a page that has been told can lay itself out and put the focus somewhere sensible while the layer is still on its way out. The layer survives that layout and fades out; the window drops it when the fade is over. Calling it twice does nothing the second time. Ignored -- no fade at all -- with animations off. |
| `bool Leaving() const` | On its way out. The window takes no clicks while a layer is: what is being dismissed is not a place to be pressed again. |
| `bool HasLeft() const` | Leaving, and the fade has finished. The window sweeps these away after the frame's tick. |
| `float Arrival() const` | How far the layer has arrived: 0 as it comes up, 1 at rest, and on its way back down while it leaves. For a subclass that draws a panel of its own and wants it to come in on the same clock. |

**Add the layer first and what sits on it after it**, with a higher `z`: the hit test reaches the
last added of the raised controls while the painter orders by `z`, and this is the order that makes
the two agree.

```cpp
if (asking) {
    auto *box = new ConfirmLayer();              // your Layer subclass
    box->smoke = true;
    box->z = 1;
    box->onDismiss = [this] { asking = false; Layout(); };
    Layer *layer = Add(box);
    layer->rect = layer->CoverPage();

    // Close() first, then whatever the button is for: a page that lays itself out while the layer
    // is not yet leaving pulls the layer out before it can be seen to go.
    Button *ok = Add(new Button(L"OK", ButtonStyle::Accent, [this, box] { box->Close(); Apply(); }));
    ok->z = 2;                                   // over the layer, and after it in the list
    ok->rect = { ... };
    box->ring = { ok };                          // what its FocusRing() returns
    box->def  = ok;
}
```

Three things worth knowing before writing one:

- **A layer arrives and leaves by fading**, over `motion::kFast` (167 ms) in both directions, and
the dim fades with it. `kFast` rather than the `kNormal` a page or a flyout takes: those are
watched, and a panel is *read* -- it is legible well before the fade is over, and a quarter of a
second of dim creeping over the page is a quarter of a second of "not yet" for nothing. The curve
is `Decel`, so it is 0.9 of the way there about 90 ms in. It is one fade for the whole group: the
window paints everything over the page through a single opacity layer while it runs, because fading
each widget on its own would show the page through the gaps between them and come out darker where
two overlap. **A layer may also paint its own arrival**, and one does: `Arrival()` is what the layer's
own drawing reads as well as what the walk draws it through, so a shape of its own -- a `Flyout`'s
panel being uncovered from the control's row outward -- comes in and goes out on the same clock, and
there is one number behind both directions rather than a pair of animations to keep in step. With animations off the layer is drawn whole on the first frame that asks for it and
gone on the next layout, with no fade and no invisible frame in between. **The controls the page put
on it leave with it**: they keep their places while the fade runs, so a button does not vanish from
under a panel that is still on screen, and none of them answers the pointer, the Tab ring, Enter or
Esc until it is gone.
- **The surface has to be opaque, and wants a shadow.** Painting the panel over the page in
`pal.cardBg` is exactly what the note over `pal.flyoutBg` warns about: a card is part of the page and
is meant to let it show through, so the page's own text reads through the dialog -- and the library
has no blur to hide it behind. `pal.flyoutBg` and `pal.flyoutStroke` are the colours for a surface
over the page, and `Painter::Shadow` is what puts it off the page: a dialog's numbers are wider and
softer than a flyout's, `p.Shadow(panel, radius, 1.0f, 26.0f, 8.0f, 16, 0.011f)`.
- **The caption bar stays live**, on purpose: the layer covers the page and not the title bar, so
dragging the window, double-clicking the caption and the Windows 11 snap-layout flyout all keep
working with a modal open. Windows' own modal dialogs keep their title bar too, and the smoke of a
`ContentDialog` does not reach past the client area either.

A drop-down's flyout is a raised control rather than a layer: it light-dismisses, but the page under
it is still the page. A layer is what a page reaches for when the *page* should not be reachable.
`Dialog` is this, ready made -- a title, a body, a footer of buttons, and an answer -- with the
buttons it adds itself: see [Controls](controls.md).

## Window state

| Member | Description |
|---|---|
| `HWND hwnd` | The window. |
| `UINT dpi` | Current DPI of the window. |
| `float scale() const` | `dpi / 96`. |
| `float ClientW() const`, `float ClientH() const` | Client size in DIPs. |
| `bool Visible() const` | Whether any of the window is on a screen: shown, not minimised, not cloaked. Occlusion is not part of it, and cannot be. |
| `Palette pal` | Current colors. See [Drawing](drawing.md#palette). |
| `Fonts fonts` | Text formats. See [Drawing](drawing.md#fonts). |
| `bool micaActive` | True when DWM accepted the backdrop that was asked for. False on Windows 10 and Windows 11 before 22H2; the page then has an opaque background. |
| `DWORD backdrop` | Which system backdrop to ask DWM for: `kDwmBackdropAuto`, `kDwmBackdropNone`, `kDwmBackdropMainWindow` (Mica, the default), `kDwmBackdropAcrylic` or `kDwmBackdropTabbed` (Mica Alt). Set it before `Create`; a page that switches material at run time sets it and calls `ApplyThemeToFrame()`. |
| `bool resizable` | As passed to `Create`. |
| `bool active` | Whether the window is the active window. The title bar dims when it is not. |
| `bool animOn` | True while the frame loop is running. |
| `void Invalidate()` | Requests a repaint. |

## Theme

| Member | Description |
|---|---|
| `void ApplyThemeToFrame()` | Applies `pal.dark` to the frame: DWM dark mode, rounded corners, Mica. Call after replacing `pal`. |
| `void ReloadTheme()` | Rebuilds `pal` from `DarkTheme()` and the accent, applies it and repaints. Runs automatically when Windows says the colours changed. |
| `void Theme(ThemeMode)` | Which theme the window paints in: `Auto` (the default, following the machine), `Light` or `Dark`. Set it before `Create` and the window comes up in it; set it while one is up and call `ReloadTheme()`. |
| `ThemeMode ThemeSetting()` | Which of the three it is in. For a settings page's initial selection. |
| `bool DarkTheme()` | What that resolves to right now -- the machine's answer under `Auto`, and the program's otherwise. **`ReloadTheme()` goes through this**, so a program that has said Dark stays dark when Windows announces that the colours changed. |

A window can use its own theme:

```cpp
micula::Theme(micula::ThemeMode::Dark);
ReloadTheme();                             // rebuild the palette and repaint
```

Setting `pal` yourself and calling `ApplyThemeToFrame()` still works, and it is what a page with a
palette of its own -- one that is not `MakePalette` -- should do. What it does **not** survive is a
system theme change: `ReloadTheme()` runs on that message, and without a `ThemeMode` there is nothing
for it to know that this program had an opinion.

## Pictures

| Member | Description |
|---|---|
| `ID2D1Bitmap1 *Image(const std::wstring &path, UINT maxW)` | Decodes an image file through WIC, scaled down to at most `maxW` pixels wide, and caches it by path. The first call's `maxW` is the one used. Returns null if the file cannot be read, and caches that too. The window owns the bitmap. It is released when the Direct2D device is lost, so don't keep the pointer beyond one paint. Needs COM. |

## Messages

| Input | What the window does |
|---|---|
| Mouse move | Sets `hover` on the control under the pointer, and sends `OnPointerMove` to it and to any control whose `ExternalRegion` the pointer is over. The control holding capture gets `pressed` (while the pointer is over it) and `OnDrag`. |
| Left button down | Every other control gets `Dismiss()`. The control under the pointer takes capture, `pressed`, focus if it is focusable, and `OnPress`. A click on nothing clears focus. |
| Left button up | The captured control gets `OnRelease`, then `OnClick` if the pointer is still over it. |
| Capture lost | `WM_CAPTURECHANGED` or `WM_CANCELMODE` -- alt-tab, a system modal, another application taking the mouse: the drag in progress gets `OnRelease` and no `OnClick`, and the capture is cleared. A button's release goes to whoever holds the capture, so a drag that loses it is ended here rather than never ending. |
| Wheel | The control under the pointer gets `OnWheel`. If it returns false, a control floating over the page -- one with a `z` -- is offered it. If none takes it, the page gets `OnAppMessage(WM_MOUSEWHEEL, wp, lp)` with `lp` holding the pointer in client pixels, and only when the pointer is over the page: the clip's width, from the caption bar down. A wheel over the pane's rail or the caption bar is taken and scrolls nothing. |
| Key down | The focused control's `OnKey` first. If it returns false: Tab and Shift+Tab move focus, Space activates the focused control (`OnActivate()`), Enter activates it or calls `OnDefaultAction()`, Esc calls `OnCancel()`. |
| Characters | `WM_CHAR` and `WM_IME_CHAR` go to the focused control's `OnChar`. Control characters are dropped. |
| `WM_TIMER` | Offered to the timers this window is running, by id -- see `Timer` in [Widgets](widget.md#timers). An id none of them took is a page's own, and reaches `OnAppMessage`. |
| Deactivation | Every control gets `Dismiss()`, `hover` is cleared, and a drag in progress gets its `OnRelease`. |
| Resize, DPI change | `Layout()`. |
| Drag of the border or the caption | `WM_ENTERSIZEMOVE` and `WM_EXITSIZEMOVE`. Windows runs a modal loop of its own, in which the frame loop cannot run, so the window paints from a 16 ms timer for the duration: the resize is live and animations keep running. |
| Scroll | Nothing is laid out: the controls move through `ContentTransform()` and the frame loop repaints them. |
| Settings change | `ReloadTheme()` when the machine switched between light and dark, and the animation switch is re-read -- see [Animations](drawing.md#animations). |
| Touch, pen | `WM_POINTERDOWN`, `WM_POINTERUPDATE` and `WM_POINTERUP` -- handled rather than passed to `DefWindowProc`, which is what keeps Windows from also promoting each touch to a mouse button. What arrives is the same press, move and release a mouse gets, with the three differences a hand really has: a finger has no hover and no cursor, a second finger takes the first one's click away, and a pen hovers while it is in the air and presses when it is not. **Which of the movements was a tap and which was a drag is not judged here**: the window feeds the frames to `InteractionContext` -- the recognizer under the shell and under WinUI, reached through `NInput.dll` for the same reason UIAutomationCore is -- and takes its answer, so the slop is the system's and not a constant of ours. A machine with no recognizer falls back to eight DIPs of travel (`kTouchSlop`). |

Messages not consumed reach `OnAppMessage` and then `DefWindowProc`: `WM_CLOSE`,
`WM_COMMAND`, `WM_APP` messages, `WM_ACTIVATE`, right and middle mouse buttons, timers
and wheel notches no control took. Mouse and keyboard messages in the table above,
`WM_SETTINGCHANGE`, `WM_SIZE` and `WM_PAINT` are consumed.

**One path for three hands.** What a press, a move and a release *do* is the window's own -- which
widget is under the pointer, whether it takes the focus, what the widget's callbacks are -- and the
mouse messages and the pointer messages both end there, so a control that answers one of them answers
all of them. A gesture belongs to the hand that began it, which is why a mouse moving while a finger is
down neither un-presses the finger's tap nor drags what the finger is dragging.

**A drag no control is using belongs to whatever scrolls.** Once the recognizer says the movement is not
a tap, the control stops being asked about it: one that is moving its own value -- a slider, a switch,
a bar's thumb, the words of a field being selected -- says so with `Dragging` and keeps the gesture for
itself, and anything else lets it go. That is the difference between `Dragging` and `TracksPointer`, and
it is not a subtle one: a list's rows follow the pointer so the row under it lights up, and the list
still scrolls under them, so the two questions cannot be one. What is left goes to the first container
above that answers `Pans` -- `ScrollView`, today -- and it follows the hand rather than gliding toward
it, which is what makes the content the finger's; past the ends the page comes a third of the way and
never more than forty DIPs (`kPanRubber`, `kPanOver`) and springs back when the hand lets go. The
container is decided once per gesture, so a page inside a page does not feel like two.

**A throw is a distance and a duration, and both are Android's.** Letting go hands the container the
speed the hand had, and `ScrollView` turns it into the fling Android's own `Scroller` would: the same
`DECELERATION_RATE`, the same pair of spline tensions, and the friction behind
`ViewConfiguration.getScrollFriction`, so the page covers a distance that goes with the square of the
speed and stops when its duration is up. **The duration is the point** -- the obvious fling, keeping the
speed and multiplying it down by a friction every frame, has no horizon and leaves a page creeping for
seconds after the eye has stopped watching it. A speed under `fling::kSlowest` is not a throw at all,
and one over `kFastest` is not a thumb; the recognizer's own inertia is not used, because on the
machines that do not produce one there would be nothing there to fall back to.

## Scrolling

Scrolling is a transform, not a layout. Return the scrolling area from `ClipRect()`, set
`scrolls` on the controls inside it, lay them out in the **page's** coordinates -- as though
the page had never been scrolled -- and move them by returning an offset from
`ContentTransform()`, glided on the frame loop:

```cpp
struct List : micula::Window {
    static constexpr float kTop = 80, kRow = 44;
    bool checked[30] = {};
    float scroll = 0, drawn = 0, maxScroll = 0;   // where it is, where it is drawn, its end
    micula::ScrollBar *bar = nullptr;

    const wchar_t *ClassName() const override { return L"MyApp.List"; }
    const wchar_t *Title() const override { return L"List"; }
    D2D1_RECT_F ClipRect() const override { return { 0, kTop, ClientW(), ClientH() }; }
    void ContentTransform(float *dy, float *opacity) const override {
        *dy = -drawn;                            // scrolled down: drawn higher up
        *opacity = 1.0f;
    }
    // The glide is the page's own animation; this is how the loop knows to run frames.
    bool AnimationWanted() const override { return drawn != scroll; }
    void OnTick(float dt) override {
        if (drawn == scroll) return;
        drawn += (scroll - drawn) * (1.0f - std::exp(-dt / 0.07f));
        if (std::fabs(scroll - drawn) < 0.5f) drawn = scroll;
        if (bar) { bar->value = scroll; bar->drawn = drawn; }
    }
    // What the wheel, the bar and its arrows call: a target, not a rebuild.
    void ScrollTo(float to, bool glide = true) {
        scroll = std::clamp(to, 0.0f, maxScroll);
        if (!glide) drawn = scroll;               // the thumb has to stay under the pointer
        if (bar) { bar->value = scroll; bar->drawn = drawn; bar->Wake(); bar->Poll(); }
        if (drawn != scroll) micula::StartAnimation(this);
        Invalidate();
    }

    void Layout() override {
        ClearWidgets();                              // keeps `bar`: it is persistent
        for (int i = 0; i < 30; i++) {
            auto *cb = Add(new micula::CheckBox(L"Item", checked[i],
                                                [this, i](bool v) { checked[i] = v; }));
            cb->rect = micula::Rect(24, kTop + i * kRow, 300, 32);   // page coordinates
            cb->scrolls = true;
        }
        const float viewport = ClientH() - kTop, extent = 30 * kRow;
        maxScroll = extent > viewport ? extent - viewport : 0;
        if (!bar) {
            bar = Add(new micula::ScrollBar([this](float to, bool glide) { ScrollTo(to, glide); }));
            bar->persistent = true;
        }
        bar->rect = { ClientW() - micula::ScrollBar::kSize - 1, kTop, ClientW() - 1, ClientH() };
        bar->area = ClipRect();
        bar->viewport = viewport;
        bar->extent = extent;
        bar->value = scroll;
        bar->drawn = drawn;
        bar->visible = maxScroll > 0;
    }

    bool OnAppMessage(UINT m, WPARAM wp, LPARAM) override {
        if (m != WM_MOUSEWHEEL) return false;
        ScrollTo(scroll - GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA * 66);
        return true;
    }
};
```

Nothing above lays the page out while it is being scrolled. The controls' rectangles are
worked out when the shape of the page changes, and a wheel notch costs one transform and one
repaint -- so everything a control is holding survives a scroll, and is lost only to a
`Layout()`: an open drop-down, a selection mid-drag, an animation that has not finished.

`examples/gallery` is a complete scrolling page. Three things in the sample are on purpose:

- Coordinates. The controls, and whatever `PaintPage()` draws, are in page coordinates.
  `PaintPage()` is not transformed, so it takes the offset off its own drawing by hand
  (`y - drawn`).
- `Widget::VisibleArea()` is `ClipRect()` in that same space, for a control that has to know
  how much room it really has -- a drop-down deciding whether its list fits below it.
- The follower in `OnTick`, rather than `motion::Track` or `motion::Ramp`: a `Track` runs for
  a duration, and a wheel spun through five notches restarts it five times inside one frame;
  `Ramp` advances by a rate in value units and is for 0..1 fades, so it cannot carry a
  distance in DIPs.

## Free functions and constants

| Name | Description |
|---|---|
| `void EnablePerMonitorDpi()` | Sets per-monitor DPI awareness v2, or the best the system has. `Window::Create` does this itself for a process that has not said anything about DPI yet -- which is every launch with no manifest -- so this is for a program with its own opinion about it. Call before creating a window. |
| `std::wstring ClipboardText(HWND owner)` | The clipboard's Unicode text, or empty. |
| `void SetClipboardText(HWND owner, const std::wstring &s)` | Replaces the clipboard's contents with `s`. |
| `void StartAnimation(Window *w)` | Wakes the message loop so it checks for animation. Only needed when a control starts animating outside a message. |
| `double MonotonicSeconds()` | Seconds since the process started, monotonic. For an animation that is periodic and holds nothing else, so that a control rebuilt by a layout does not restart it. |
| `bool Post(Window *w, std::function<void()> fn)` | Runs `fn` on the thread that owns `w`, once the message in hand is finished. Safe to call from any thread; false if the window is already gone. See [Threads](#threads). |
| `struct PostSlot` | A place for a worker to post from, one call at a time: `slot.Post(w, fn)` drops the call when one is already pending. See [Threads](#threads). |
| `kCaptionH` | 32. Title bar height in DIPs. |
| `kCaptionBtnW` | 46. Width of each title bar button in DIPs. |
| `kResizeGrip` | 6. Width of the resize border in DIPs. |

## Threads

The window and everything in it belong to the thread that made it. Nothing in Micula
synchronises anything, so a control touched from another thread is a race and not a
slower way of doing it.

What a worker thread has instead is `Post`, which hands the window a callable to run in its
own turn:

```cpp
// On the worker. Nothing here is locked, and nothing has to be polled.
Post(&page, [&page, snapshot] { page.ShowState(snapshot); });
```

The callback runs between two messages, with the same rights a control's callback has: it
may lay the page out again, add controls, invalidate. What it may not do is block -- it is
holding the window's thread.

`PostSlot` is for a worker with more news than the window needs. One call is pending at a
time and the rest are dropped, so a thousand updates cost one turn:

```cpp
std::atomic<float> progress;   // the worker's, the page only ever reads it
PostSlot slot;

// On the worker, as often as it likes.
progress.store(done);
slot.Post(&page, [&page, &progress, &slot] {
    page.SetProgress(progress.load());   // reads the latest, does not carry a change
});
```

That is the whole contract: a call that is dropped is a change that is never reported, so
the callable reads the state rather than carrying a snapshot of it. The state stays on the
worker's side of the boundary and the page only ever sees what it asks for.

A call posted to a window that is destroyed before it runs never runs, and the window frees
it on the way out; `Post` answers false if the window is already gone.

## Internals

Public because `Window` is a struct, but not part of the interface: `Paint`, `Reaches`,
`Resize`, `Frame`, `Tick`, `Animating`, `RefreshHover`, `HitTest`, `CaptionHitTest`,
`PaintCaption`, `MeasureFrame`, `CreateDevice`, `ReleaseDevice`, `Proc`, the Direct2D
and DirectComposition pointers (`dw`, `d3d`, `dxgi`, `d2d`, `d2dDevice`, `dc`, `swap`,
`target`, `comp`, `compTarget`, `compVisual`, `brush`), and the `dpiapi` and `frameclock`
namespaces.
