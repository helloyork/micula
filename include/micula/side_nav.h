// Micula / side_nav.h

#pragma once

#include "window.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <functional>
#include <string>
#include <vector>

namespace micula {


// One row of a navigation pane: an icon and a label, or -- with `header` -- the name of a
// group, which is drawn quieter and is not something to choose.
//
// An aggregate, so that a pane's contents are a list of literals:
//
//     pane->items = { { glyph::kSettings, L"General" }, NavItem::Heading(L"Pages"),
//                     { kIconCalendar, L"Schedule" } };
struct NavItem {
    const wchar_t *glyph = nullptr;
    std::wstring label;
    bool header = false;

    static NavItem Heading(std::wstring text) {
        NavItem h;
        h.label = std::move(text);
        h.header = true;
        return h;
    }
};

// When the pane expands. Four shapes, and they differ in nothing but that: the drawing is
// the same one in all of them.
//
// `Fixed`   part of the layout, and it never moves
// `Toggle`  the button opens and closes it, and it stays where it was put
// `Peek`    resting the pointer on the rail opens the pane for as long as it is there, and the
//           button pins it open instead -- a pinned pane is a `Toggle` and the page makes room
// `Minimal` the rail alone: it never expands, so there is nothing to open and no button
//
// `Peek` is not WinUI: NavigationView has no such property, and Task Manager -- which is
// NavigationView -- does not open on hover either. It is Visual Studio's auto-hide tool
// window, and it is here because a rail that only ever shows icons is a rail whose labels
// are unreachable without deciding to. Off by default, like everything else here.
enum class PaneStyle { Fixed, Toggle, Peek, Minimal };

// The list of pages down the left of a window, the way Windows 11's own settings window has
// it. WinUI calls the whole thing NavigationView and what is here is its pane: the content
// stays the page's business, and switching to another one is what the callback is for.
//
// A control rather than something the window provides, because what a window does with it
// is the window's business -- a page of cards and a page of text want different things from
// the same pane, and the four styles above are four answers to one question rather than four
// controls.
//
// **Make it persistent.** A pane is the one control a page really is laid out *while it is
// being operated*: choosing a page rebuilds the page, and a rebuild would take the open
// state, the width half way through its animation, the hover and the accent bar's travel
// with it. The page makes it once, marks it persistent, and hands it a fresh `rect` in every
// Layout() -- the pane owns its own right edge and grows from the left one it is given.
// Whether the page makes room for the pane is not a setting: it follows from what the pane
// *is*, and from who asked for it. A pane that stays -- `Fixed`, `Toggle`, and a `Peek` the
// button pinned -- is part of the layout, and a page laid out around it is the whole point of
// it; a pane that comes and goes covers what it is over, and pushing the page around under a
// pointer that is only passing through would be worse than covering it. That is the whole of
// what a peek is: the same pane, with the room only for the shape a person asked for.
//
// WinUI's own pane is `Toggle` over a rail, which is `LeftCompact`, and it was here as a third
// axis for a while. It came out: a pane the user asked to open and which then covers the
// content is a pane nobody asked for, and WinUI earns that shape by choosing it *for you* in a
// window too narrow to give up the room. This library has no such mode, so there is nothing to
// choose between.
struct SideNav : Widget {
    // --- what it shows -----------------------------------------------------------
    std::vector<NavItem> items;    // headings and rows, top to bottom
    std::vector<NavItem> footer;   // rows pinned to the bottom, which is where Settings lives
    // Which row is chosen. Counts the rows that can be chosen and not the headings, and
    // carries on through the footer, so a heading may be added to `items` without the page's
    // own indices moving.
    int selected = 0;
    std::function<void(int)> onSelect;
    // The pane opened or closed, by its own button or by SetOpen. A page in Push mode lays
    // itself out again here; a page in Overlay mode has nothing to do.
    std::function<void(bool)> onToggle;

    // --- how it behaves ----------------------------------------------------------
    PaneStyle style = PaneStyle::Toggle;
    // Whether the width slides. Off, it arrives in one frame, which is what a pane that is
    // part of the layout wants and what Windows' own task manager does.
    bool animate = true;
    // Draw the button that opens and closes the pane, at the top of the rail. Off, the page
    // puts a button of its own somewhere and calls SetOpen.
    bool ownToggle = true;
    // Darken the page while an overlay pane is open, so that the pane reads as being over it.
    bool scrim = false;
    // Whether Up and Down move the choice or only the focus ring. WinUI has the same switch
    // under the same name (NavigationView.SelectionFollowsFocus) and both answers have their
    // users: a pane whose pages are expensive to build does not want to build one on the way
    // past, and a pane whose pages are cheap wants the arrow keys to be the whole of it.
    bool followsFocus = true;

    // WinUI's CompactPaneLength and OpenPaneLength, and its row: 36 DIPs with 4 between rows,
    // which is what Windows 11's own panes are laid out on.
    float compactW = 48.0f;
    float openW    = 320.0f;
    float rowH     = 36.0f;
    static constexpr float kRowGap    = 4.0f;
    static constexpr float kGroupGap  = 16.0f;   // above a heading
    static constexpr float kToggle    = 36.0f;   // the button's square
    static constexpr float kTogglePad = 6.0f;    // and its inset from the pane's own edge
    static constexpr float kBottomPad = 8.0f;
    // Peek: how long the pointer has to *rest* on the rail before the pane opens. Crossing a
    // rail on the way to somewhere else is not asking for a pane, and without this every
    // crossing costs a 272-DIP animation. Closing has no such delay -- a pane that lingers
    // over the page after the pointer has left is in the way, and the animation is already
    // the time it takes to leave.
    float peekIn = 0.20f;

    // --- state -------------------------------------------------------------------
    // What the button and the page have asked for.
    bool open = false;
    // Peek's second reason to be open, which the pointer owns and a click does not.
    bool peeking = false;
    // Peek: the pointer has arrived and is resting, and the delay above has been counted
    // from then.
    ULONGLONG arrivedAt = 0;
    bool wasHover = false;
    // Peek, and the pane was closed by a click: the pointer is still on the rail, and opening
    // again at once is what makes the button look broken. It has to leave first.
    bool peekHeld = false;
    // Peek, and the page is still making room for the pane: it was pinned a moment ago and is on
    // its way back to the rail. Without it `Pushes()` follows `open`, which the click has already
    // cleared -- and the retraction then happens over a page that has closed up behind it, with a
    // flyout's fill and its shadow and a raised z, like a pane arriving rather than one leaving.
    // Set by the toggle and cleared in the frame the width reaches the rail, so the next hover is
    // a peek again: the pointer covers the page, the button does not. A style switched to Peek
    // while the pane is already open is the case that wants this to be written where the pane is
    // *closed* rather than where it is opened.
    bool peekPushes = false;
    // 0 is the rail, 1 is the pane at its full width, and what is between them is a pane
    // still arriving. A track rather than a follower: this is one move per decision, and the
    // two directions are different shapes -- a pane arriving settles, a pane leaving gets out
    // of the way. Same pair as the drop-down's lid, for the same reason.
    motion::Track wide;
    // The accent bar, which travels between rows rather than appearing under the new one. The
    // two edges a row apart while it moves is what makes it stretch across the gap and close
    // up on arrival, which is what examples/settings used to do by hand.
    motion::Span bar;
    // The first frame has nothing to animate *from*: a pane that comes up open is open, not
    // a pane growing out of the rail. Both the width and the bar are set rather than moved
    // on that frame, which is what this remembers.
    bool primed = false;
    // One hover fade per row that can be chosen, so that moving down a pane cross-fades
    // instead of swapping, and the button's own two.
    std::vector<float> hot;
    float toggleT = 0.0f, togglePressT = 0.0f;
    // The row the keyboard is on, when `followsFocus` is off: the ring moves on its own and
    // the choice waits for Enter.
    int ringRow = 0;

    // --- geometry ------------------------------------------------------------------
    struct Row {
        const NavItem *item = nullptr;
        float top = 0.0f;
        int index = -1;      // its place among the rows that can be chosen; -1 for a heading
    };
    // Worked out on demand rather than kept: it is a dozen float adds, and both the item list
    // and the rectangle change under it -- a pane that survives a layout is handed a new
    // `rect` by every one of them.
    mutable std::vector<Row> rows;
    void Build() const {
        rows.clear();
        // Where the rows start: under the button when there is one, because the button is drawn
        // over the pane's own top corner and the first row would be under it. With no button --
        // `Fixed`, `Minimal`, or a page that draws its own -- the top of the pane is free, and
        // the rows go up into it.
        const float frac = wide.value;
        float y = rect.top + (ShowsToggle() ? kTogglePad + kToggle + kTogglePad : kTogglePad);
        int index = 0;
        for (const NavItem &it : items) {
            // A heading is a label for a group of rows, and a rail of icons has no groups to
            // label: its gap and its own height go with the pane's width, so the icons close up
            // into one column as it collapses. WinUI's compact pane does the same, and the
            // alternative is the empty band this used to leave behind -- which reads as a bug in
            // the layout rather than as a group that is not being labelled.
            if (it.header) y += kGroupGap * frac;
            rows.push_back({ &it, y, it.header ? -1 : index++ });
            y += it.header ? (rowH + kRowGap) * frac : (rowH + kRowGap);
        }
        // The footer is stacked up from the bottom, so that the two lists cannot push each
        // other around as one of them grows.
        float fy = rect.bottom - kBottomPad - (float)footer.size() * (rowH + kRowGap);
        for (const NavItem &it : footer) {
            rows.push_back({ &it, fy, it.header ? -1 : index++ });
            fy += rowH + kRowGap;
        }
    }

    // --- what the window and the page ask it ---------------------------------------
    // The width the rail rests at: the closed pane, in every style. A closed pane is a rail of
    // icons first and a button second, and the style whose button the page owns is no
    // exception -- there the rail is all there is, which is what makes it worth having.
    float RailW() const { return compactW; }
    // Whether the pane is expanded, which is the state the width animates towards. `Fixed` is
    // always and `Minimal` is never -- neither has anything to ask -- and the two that open are
    // asked by `open`, with a peek counting only in the style that has one: anything left behind
    // in `peeking`, from a pointer that has since left or a style that has since changed, is not
    // a reason for a pane with no hover to be open.
    bool Expanded() const {
        if (style == PaneStyle::Fixed) return true;
        if (style == PaneStyle::Minimal) return false;
        return open || (style == PaneStyle::Peek && peeking);
    }
    // Whether the page makes room for it, or the pane goes over it. See the note above the
    // struct: this is what the style is, and not a second question -- except that a Peek is
    // both, and which one it is comes down to who asked. The hover is the pointer passing over
    // the rail and covers the page; the button is a person asking, and a pane a person asked to
    // be open is a pane the page is laid out around, or it is in the way of the page it was
    // asked to show.
    bool Pushes() const {
        if (style == PaneStyle::Fixed || style == PaneStyle::Toggle) return true;
        // Pinned, or on its way back from having been pinned: a peek the pointer asked for goes
        // over the page, and one a person asked for is a pane the page is laid out around -- for
        // as long as it is wider than the rail, and not only while `open` says so.
        return style == PaneStyle::Peek && (open || peekPushes);
    }
    // The width the pane is drawn at right now.
    float Width() const { return RailW() + (openW - RailW()) * wide.value; }
    // The room the button needs when the rail is narrower than it, which a page that sets
    // `compactW` small will have made it: a button drawn at the pane's corner is over the page,
    // so the page has to be told about it or the first card is under it.
    float ButtonRoom() const {
        return ShowsToggle() ? 2 * kTogglePad + kToggle : 0.0f;
    }
    // The width a page has to leave for it. A pane that pushes: the width of the state being
    // asked for, so that the window lays itself out again on the toggle and the pane then slides
    // into the room it has made rather than over the page. A pane that covers: the rail, always
    // -- the page does not move when the pane opens over it.
    float Reserved() const {
        if (Pushes() && Expanded()) return openW;
        return (std::max)(RailW(), ButtonRoom());
    }
    // How many rows can be chosen: the items that are not headings, then the footer.
    int Selectable() const {
        int n = 0;
        for (const NavItem &it : items) if (!it.header) n++;
        for (const NavItem &it : footer) if (!it.header) n++;
        return n;
    }
    // Whether there is a button to draw. A Fixed pane has nothing to open or close, and Minimal
    // is the one style whose button is the page's: the pane rests as a rail and the app puts a
    // button in its own header, which is what the style is for -- on a window too narrow for a
    // pane that pushes, the button belongs to the page rather than to the pane. `ownToggle` is
    // not asked.
    bool ShowsToggle() const {
        return ownToggle && style != PaneStyle::Fixed && style != PaneStyle::Minimal;
    }
    D2D1_RECT_F ToggleBox() const {
        // Centred in the rail, and at the pane's own corner when a page has made the rail
        // narrower than the button: `Reserved()` leaves the wider of the two, so the corner is
        // inside the room the page gave up either way.
        const float x = RailW() >= kToggle + 2 * kTogglePad
                            ? rect.left + (RailW() - kToggle) / 2
                            : rect.left + kTogglePad;
        return { x, rect.top + kTogglePad, x + kToggle, rect.top + kTogglePad + kToggle };
    }

    // The top of the row that is `at`-th among the rows that can be chosen, as a real number,
    // because the accent bar sits between two of them while it travels. The rows are not
    // evenly spaced -- a heading is a gap, and so is the jump down to the footer -- so this
    // interpolates between the two rows it is between rather than doing arithmetic on the
    // index.
    float TopOf(float at) const {
        Build();
        const int i = (int)std::floor(at);
        const float f = at - (float)i;
        float a = 0.0f, b = 0.0f;
        bool gotA = false, gotB = false;
        for (const Row &r : rows) {
            if (r.index < 0) continue;
            if (r.index == i)     { a = r.top; gotA = true; }
            if (r.index == i + 1) { b = r.top; gotB = true; }
        }
        if (!gotA) return rect.top;
        return gotB ? a + (b - a) * f : a;
    }
    // The row that can be chosen at this point, or -1. Boxed in on both sides, like the
    // drop-down's: a pane is a column, and the height alone would answer for a pointer
    // resting beside it.
    int RowAt(float x, float y) const {
        Build();
        if (x < rect.left || x >= rect.left + Width()) return -1;
        // The button's box and not the field: whether there *is* a button is `ShowsToggle`'s
        // question, and asking `ownToggle` here took the first row's place away in the two styles
        // that draw no button -- `Fixed` and `Minimal` -- where the rows start at the top of the
        // pane and the first of them sits exactly where a button would have been. A click on it
        // went nowhere at all: not a row, and not a button either.
        if (ShowsToggle() && Inside(ToggleBox(), x, y)) return -1;
        for (const Row &r : rows)
            if (r.index >= 0 && y >= r.top && y < r.top + rowH) return r.index;
        return -1;
    }
    // The row under the pointer, or -1 when the pointer is elsewhere -- on the button, on a
    // heading, or off the pane.
    int HotRow() const {
        if (!hover || !enabled) return -1;
        const D2D1_POINT_2F at = Cursor();
        return RowAt(at.x, at.y);
    }
    // The pane's own rectangle, and not `rect`: the page hands it a fresh one in every Layout
    // with the right edge at zero, because that edge is the pane's to derive -- and deriving it
    // again is a frame's work, which a layout with no animation after it does not get. A hit
    // test that read `rect` therefore saw a pane no wider than nothing, drawn and unclickable
    // both, until something else in the window happened to animate.
    bool Covers(float x, float y) const override {
        const float right = rect.left + (std::max)(Width(), ButtonRoom());
        return x >= rect.left && x < right && y >= rect.top && y < rect.bottom;
    }

    // --- what the page calls -------------------------------------------------------
    bool Select(int i) {
        const int n = Selectable();
        if (n <= 0) return false;
        i = (std::min)((std::max)(i, 0), n - 1);
        if (i == selected) return false;
        selected = i;
        ringRow = i;
        if (onSelect) onSelect(selected);
        if (owner) owner->Invalidate();
        return true;
    }
    // Nothing at all in `Minimal`, which never expands: a page can wire a button to this, or to
    // `Toggle`, whatever style the pane is in without asking which one that is.
    void SetOpen(bool o) {
        if (style == PaneStyle::Minimal || open == o) return;
        // A peek a person touched is a pane the page makes room for, in both directions and from
        // here: opening is the state, and closing is the retraction that has to be pushed too.
        if (style == PaneStyle::Peek) peekPushes = true;
        open = o;
        // Closed while the pointer is still resting on the rail: the peek is held back until it
        // leaves and comes back, or the pane opens again from under the button that closed it and
        // that button looks like it did nothing. Held here rather than in the pane's own click,
        // because who closed it is not the question -- the page's button closes it too, and the
        // pane is in the same place either way.
        if (!o && style == PaneStyle::Peek && hover) peekHeld = true;
        if (onToggle) onToggle(open);
        if (owner) owner->Invalidate();
    }
    void Toggle() { SetOpen(!open); }

    // --- the pointer and the keyboard ----------------------------------------------
    bool Focusable() const override { return true; }
    // The rows' hover and the bar follow the pointer *inside* this control, which is the one
    // thing the window cannot see: the same widget is hovered from the first row to the last.
    bool TracksPointer() const override { return true; }

    void OnClick() override {
        const D2D1_POINT_2F at = Cursor();
        if (ShowsToggle() && Inside(ToggleBox(), at.x, at.y)) {
            // The peek is the pointer's and the button has just overruled it, so it is settled
            // here rather than in the next Tick: pinned, the pane is a `Toggle` and nothing about
            // it is a peek, and closed it is not to peek back out from under the button. The hold
            // that stops the second one is `SetOpen`'s -- the pointer is on the rail by
            // definition when this runs.
            peeking = false;
            Toggle();
            return;
        }
        const int i = RowAt(at.x, at.y);
        if (i >= 0) Select(i);
    }
    // Space and Enter. With `followsFocus` the choice has already caught up with the ring, so
    // this is what the arrow keys have arrived at; without it, this is what picks it up.
    void OnActivate() override { Select(followsFocus ? selected : ringRow); }
    bool OnKey(WPARAM vk) override {
        if (vk != VK_UP && vk != VK_DOWN) return false;
        const int n = Selectable();
        if (n <= 0) return false;
        const int dir = vk == VK_DOWN ? 1 : -1;
        if (followsFocus) { Select(selected + dir); return true; }
        ringRow = (std::min)((std::max)(ringRow + dir, 0), n - 1);
        if (owner) owner->Invalidate();
        return true;
    }
    // A press somewhere else puts away the peek it was showing, and nothing else: a pinned pane
    // is a decision and stays.
    void Dismiss() override {
        if (peeking) { peeking = false; peekHeld = true; }
    }

    // --- animation ------------------------------------------------------------------
    bool Animating() const override {
        if (Widget::Animating()) return true;
        if (wide.Wants(Expanded() ? 1.0f : 0.0f) || bar.Wants((float)selected)) return true;
        // The peek's delay is a wait, and a wait nobody animates is a wait that never ends:
        // the frame loop has to keep running while the pointer rests on the rail.
        if (style == PaneStyle::Peek && hover && !Expanded() && !peekHeld) return true;
        // And a peek the pointer has already left is a frame's work to clear. Nothing else will
        // ask for that frame -- the width is where the pane wants it, and the pointer has stopped
        // moving -- so without this the pane stays open over a page nobody is pointing at, and
        // stays that way until something else in the window animates.
        if (style == PaneStyle::Peek && peeking && (!hover || peekHeld)) return true;
        const int want = HotRow();
        for (int i = 0; i < (int)hot.size(); i++)
            if (hot[i] != Want(i == want)) return true;
        if (ShowsToggle() && (toggleT != Want(hover && OverToggle()) ||
                              togglePressT != Want(pressed && OverToggle()))) return true;
        return false;
    }
    bool OverToggle() const {
        const D2D1_POINT_2F at = Cursor();
        return Inside(ToggleBox(), at.x, at.y);
    }
    void Tick(float dt) override {
        Widget::Tick(dt);

        // Peek first, since it is the only thing here that can change the target.
        if (style == PaneStyle::Peek) {
            const ULONGLONG now = GetTickCount64();
            if (!hover || peekHeld) {
                peeking = false;
                if (!hover) { peekHeld = false; wasHover = false; }
            } else {
                if (!wasHover) { wasHover = true; arrivedAt = now; }
                if (!open && now - arrivedAt >= (ULONGLONG)(peekIn * 1000.0f)) {
                    // The pointer has taken the pane over from whatever a person asked for -- a
                    // close from a page's own button leaves the pointer free to rest on the rail
                    // while the pane is still on its way out. From here it is a peek, and a peek
                    // covers the page, retraction or not.
                    peekPushes = false;
                    peeking = true;
                }
            }
        }

        const float target = Expanded() ? 1.0f : 0.0f;
        wide.To(target);
        if (!primed) { wide.Set(target); primed = true; }
        else if (!animate || style == PaneStyle::Fixed) wide.Set(target);
        else if (target > 0.0f) wide.Step(dt, motion::kFast, motion::Decel);
        else                    wide.Step(dt, motion::kFast, motion::Accel);
        // The retraction is over when the width reaches the rail, and the page stops making room
        // in the same frame -- this runs while the width is still moving, so the frame that
        // settles is also the frame that clears it, and nothing is left behind for the next
        // hover to be mistaken for.
        if (peekPushes && !open && style == PaneStyle::Peek && wide.value <= 0.0f) peekPushes = false;

        bar.To((float)selected);
        bar.Step(dt);

        const int want = HotRow();
        if ((int)hot.size() != Selectable()) hot.resize((size_t)Selectable(), 0.0f);
        for (int i = 0; i < (int)hot.size(); i++)
            motion::Ramp(&hot[i], Want(i == want), dt, motion::kFaster);

        const bool button = ShowsToggle() && OverToggle();
        motion::Ramp(&toggleT, Want(hover && button), dt, motion::kFaster);
        motion::Ramp(&togglePressT, Want(pressed && button), dt, motion::kFaster);

        // The pane owns its right edge, so that the pointer reaches exactly as far as the pane
        // is drawn: a rectangle left at the open width would swallow the clicks that belong to
        // the page beside a closed rail.
        rect.right = rect.left + (std::max)(Width(), ButtonRoom());
        if (owner) rect.right = (std::min)(rect.right, owner->ClientW());
        // And it is raised over the page exactly while it is wider than the room the page left
        // for it, which is the part of the animation where it is over the page at all -- and a
        // pane that pushes is never over the page, however wide it is on the way past. A peek
        // retracting to the rail is what makes that worth saying: for the whole animation it is
        // wider than the room the page was given, and it is still the page's neighbour.
        z = !Pushes() && Width() > (std::max)(RailW(), Reserved()) + 0.5f ? 2 : 0;
    }

    void Paint(const Painter &p) override {
        const Palette &c = *p.pal;
        Build();
        const float w    = Width();
        const float frac = wide.value;
        const D2D1_RECT_F box = { rect.left, rect.top, rect.left + w, rect.bottom };
        // How far the pane has grown past the room the page left for it, which is exactly how
        // far it is over the page -- and *only a pane that covers the page is over it at all*:
        // one that pushes is part of the layout, and the page's room is the width it asked for.
        //
        // Asking the difference rather than the style was what made opening and closing look
        // like two different controls. `Reserved()` reports the state being asked for, so on the
        // way out it is already the rail while the pane is still most of its width, and the closing
        // half of the animation was painted with a flyout's fill and a shadow around it: a pane
        // that pushes appearing as an overlay for as long as it takes to leave. None of it is
        // drawn for one that pushes, so both directions are the same pane on the same backdrop.
        const float overlap = Pushes() ? 0.0f : w - Reserved();
        if (overlap > 0.5f) {
            const float solid = (std::min)(1.0f, overlap / 24.0f);
            // The page behind it goes down, which is what makes the pane read as being over it
            // rather than beside it. Drawn from the pane's edge to the window's, and outside
            // this control's rectangle on purpose: a click on it lands on the page, which is one
            // of the things that Dismisses the pane.
            if (scrim && owner && owner->ClientW() > box.right)
                p.Fill({ box.right, rect.top, owner->ClientW(), rect.bottom },
                       Fade(Rgb(0x000000, 0.14f), (std::min)(1.0f, overlap / 48.0f)));
            // A pane is a straight edge over the page, and what makes it read as a panel rather
            // than as a stripe of paint is the falloff beside it: the same stacked-rectangles
            // shadow the flyout has, on the one edge that needs it.
            constexpr int kShadowLayers = 8;
            constexpr float kShadowReach = 12.0f;
            for (int i = kShadowLayers; i >= 1; i--) {
                const float e = kShadowReach * (float)i / (float)kShadowLayers;
                p.Fill({ box.left - e, box.top, box.right + e, box.bottom },
                       Fade(Rgb(0x000000, 0.012f), solid));
            }
            p.Fill(box, Fade(c.flyoutBg, solid));
        }
        // The button is outside the clip below: it is the one thing that is always whole.
        if (ShowsToggle()) PaintToggle(p);

        p.rt->PushAxisAlignedClip({ rect.left, rect.top, rect.left + w, rect.bottom },
                                  D2D1_ANTIALIAS_MODE_ALIASED);
        const float iconX  = rect.left + (std::max)(12.0f, (RailW() - 20.0f) / 2);
        // Close to the icon, and not a second column: Windows' own panes put the label eight to
        // twelve DIPs past the icon and leave it there whether the pane is open or not.
        const float labelX = iconX + 32.0f;
        for (const Row &r : rows) {
            const float top = r.top;
            const D2D1_COLOR_F label = Fade(r.item->header ? c.textSecondary
                                                           : (enabled ? c.textPrimary
                                                                      : c.textDisabled),
                                            r.item->header ? frac : 1.0f);
            if (r.index < 0) {
                // The heading's own rectangle collapses with the pane, the way its place in the
                // layout does: a label left at full height would be lying across the rows that
                // have moved up into the room it gave back.
                p.Text(r.item->label, { rect.left + 12, top, box.right - 8, top + rowH * frac },
                       p.font->bodyStrong, label);
                continue;
            }
            // The pill grows out of the icon's box, and it is the pane's own width that is the
            // second number: the shape that is a square around the icon at rest is the whole
            // row once the pane has arrived, and every frame in between is the pane's arrival
            // itself. One number, and not a second animation to keep in step with it.
            const float hoverNow = r.index < (int)hot.size() ? hot[r.index] : 0.0f;
            const float fill = (std::max)(r.index == selected ? 1.0f : 0.0f, hoverNow);
            if (fill > 0.0f)
                p.FillRound({ rect.left + 4, top + 2, box.right - 4, top + rowH - 2 },
                            metric::kRadiusControl,
                            Fade(pressed && hoverNow > 0.0f ? c.controlBgPressed : c.subtleHover,
                                 fill));
            if (r.item->glyph) {
                // Centred in the icon's box by measuring, the way the button at the top of the
                // rail is. The icon format is left-aligned like every other text format here,
                // and the glyphs do not fill their advance, so drawn from the box's own edge
                // they sit a couple of DIPs to the left of the button above them -- which is
                // the one thing in a rail of icons there is to line up with.
                const float gw = p.MeasureWidth(r.item->glyph, p.font->icon);
                p.Text(r.item->glyph,
                       { iconX + (20.0f - gw) / 2, top, iconX + (20.0f + gw) / 2, top + rowH },
                       p.font->icon, label);
            }
            p.Text(r.item->label,
                   { labelX, top - metric::kTextLift, box.right - 8,
                     top + rowH - metric::kTextLift },
                   p.font->body, Fade(label, frac));
            // The focus ring goes around the row the keyboard is on, which is the choice when
            // it follows the focus and the ring itself when it does not.
            if (focus && owner && owner->showFocusRing &&
                r.index == (followsFocus ? selected : ringRow))
                p.StrokeRound({ rect.left + 4, top + 2, box.right - 4, top + rowH - 2 },
                              metric::kRadiusControl, c.textPrimary, 2.0f);
        }
        // The bar is drawn over the rows it is passing: it stretches from the row it left to
        // the row it is arriving at, and on the way it crosses the one between them.
        if (Selectable() > 0) {
            const float bx = rect.left + 1.0f;
            p.FillRound({ bx, TopOf(bar.Lo()) + 8.0f, bx + 3.0f,
                          TopOf(bar.Hi()) + rowH - 8.0f },
                        1.5f, Fade(c.accent, enabled ? 1.0f : 0.4f));
        }
        p.rt->PopAxisAlignedClip();
    }

private:
    void PaintToggle(const Painter &p) const {
        const Palette &c = *p.pal;
        const D2D1_RECT_F b = ToggleBox();
        const float f = (std::max)(toggleT, togglePressT);
        if (enabled && f > 0.0f)
            p.FillRound(b, metric::kRadiusControl,
                        Fade(Mix(Mix(c.controlBg, c.controlBgHover, toggleT),
                                 c.controlBgPressed, togglePressT), f));
        // Centred by measuring, because the icon format is left-aligned like every other text
        // format here -- the same thing the caption's three buttons do.
        const float gw = p.MeasureWidth(glyph::kMenu, p.font->icon);
        p.Text(glyph::kMenu, { (b.left + b.right) / 2 - gw / 2, b.top, b.right, b.bottom },
               p.font->icon, enabled ? c.textPrimary : c.textDisabled);
    }
};

}  // namespace micula
