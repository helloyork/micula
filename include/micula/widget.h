#pragma once

// The node. A widget owns the widgets under it, its parent's layout decides where it is, and
// `visible` decides whether it is anywhere at all -- which is how a page that used to be torn down
// and rebuilt to change what is on screen changes it instead.
//
// Nothing a control says about itself is a coordinate. A control reports what it wants (`Measure`),
// draws itself inside the rectangle it was given (`Paint`), and answers input in its own space; the
// tree is what moves things, with transforms, and the window is what runs the arrangement.

#include "layout.h"
#include "theme.h"                  // Fonts, and motion for the glide

#include <uiautomationclient.h>     // the UIA_* control type ids

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace micula {

struct Painter;
struct Window;
// The thing a tree is drawn into and handed to: a window, a menu, a tip. See window.h.
struct Surface;
struct Menu;
struct Layer;

// Where a widget's rectangle is measured from.
enum class Space {
    // The parent's space. This is what makes nesting, scrolling and motion one mechanism: every
    // transform in the tree is inherited, so a subtree on its way somewhere is a transform and
    // nothing inside it has to know it is moving.
    Parent,
    // The window's own space, with no ancestor transform on it. For a widget that is a world of its
    // own -- a hosted child window, a canvas. Taking this on means doing by hand what the transforms
    // were doing: clipping to the room it has, staying out of the way of whatever scrolls around
    // it, and being hit-tested exactly where it is drawn.
    Page,
};

// An arranged rectangle, and where it is being drawn. A widget is only ever in one of these states,
// which is what lets `Animating` answer with a comparison.
inline bool SameRect(const D2D1_RECT_F &a, const D2D1_RECT_F &b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

// The four geometry helpers everything reaches for. They live here rather than beside the painter
// because a widget is the thing that has a rectangle.
inline D2D1_RECT_F Rect(float x, float y, float w, float h) { return { x, y, x + w, y + h }; }
inline float Width(const D2D1_RECT_F &r)  { return r.right - r.left; }
inline float Height(const D2D1_RECT_F &r) { return r.bottom - r.top; }
inline bool Inside(const D2D1_RECT_F &r, float x, float y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

struct Widget {
    Widget() = default;
    virtual ~Widget() {}

    // ---- the tree ----------------------------------------------------------------------------
    Widget *parent = nullptr;
    std::vector<std::unique_ptr<Widget>> children;

    // Takes ownership, appends, returns. The order is paint order, hit-test order reversed, Tab
    // order, and the order a screen reader reads.
    template <typename T> T *Add(T *w) {
        w->parent = this;
        w->uid = NextUid();
        children.emplace_back(w);
        InvalidateLayout();
        return w;
    }

    // Takes a child out. The node is not destroyed here: it is handed to the window, which destroys
    // it after the message being handled has finished, so a control may remove itself from its own
    // callback -- which is a thing controls do.
    void Remove(Widget *w);

    // What arranges the children of this node. Null, and they keep the rectangles they were given,
    // which is what a page with one control in it wants.
    std::unique_ptr<Layout> layout;
    void SetLayout(Layout *l) {
        layout.reset(l);
        if (layout) layout->host_ = this;
        InvalidateLayout();
    }

    // ---- what it is --------------------------------------------------------------------------
    bool visible = true;
    // How solid this widget and everything under it is drawn, 0 to 1. The paint walk draws anything
    // below 1 as **one group at that opacity** rather than each widget in it at that opacity, because
    // a subtree faded widget by widget shows what is behind it through the gaps between them, and
    // comes out darker where two of them overlap. It is what a page arriving is drawn with -- see
    // `NavigationView::transition` -- and a widget at 1, which is every widget for all but a few
    // frames, costs nothing.
    float opacity = 1.0f;
    bool enabled = true;
    bool hover = false;
    bool pressed = false;
    bool focus = false;
    // The animated shadows of the three flags above: 0 is off, 1 is on, anything between is a brush
    // crossing over. Only the background follows these -- a WinUI control under the pointer moves
    // one property, `<ContentPresenter.BackgroundTransition>`, and leaves its border, its text and
    // its focus ring to change between two frames.
    float hoverT = 0.0f, pressT = 0.0f, focusT = 0.0f;

    // ---- where it is -------------------------------------------------------------------------
    D2D1_RECT_F rect = {};      // what the parent's layout arranged, in the parent's space
    D2D1_RECT_F drawn = {};     // where it is drawn and hit-tested: `rect`, gliding toward it
    bool placed = false;        // false until the first arrangement, and the first one never glides
    Space space = Space::Parent;

    // ---- what it wants -----------------------------------------------------------------------
    // A container answers with what its layout measured; a control answers from its own content; one
    // with no opinion at all takes the room it is given. `micula::Want` is spelled out because this
    // class has a member called `Want` as well -- the value a pointer state is crossing over to.
    virtual micula::Want Measure(const Room &room) const {
        if (layout) return layout->Measure(room);
        return micula::Want(Sizing::Fill, Sizing::Fill);
    }

    // ---- what it draws and what it answers ---------------------------------------------------
    virtual void Paint(const Painter &p) = 0;

    // Only widgets that can be operated from the keyboard join the Tab order.
    virtual bool Focusable() const { return false; }
    virtual void OnClick() {}
    // The mouse went down on this widget, at this point in the widget's own space. Taken from the
    // message rather than read from the cursor: the pointer can have moved between the click being
    // queued and this running, and a control that reads the cursor can disagree with the hit test
    // that chose it.
    virtual void OnPress(float /*x*/, float /*y*/) {}
    // The mouse moved while this widget holds capture, in the same space as OnPress. This and
    // OnPress are the whole of a drag; a control must not update itself out of Paint instead.
    virtual void OnDrag(float /*x*/, float /*y*/) {}
    // The pointer moved over this widget, or over the region it watches outside itself
    // (ExternalRegion), in the widget's own space.
    virtual void OnPointerMove(float /*x*/, float /*y*/) {}
    // The area outside `rect` -- in the widget's own space -- where it wants OnPointerMove as well.
    // Empty for a widget that only answers to itself. Moves only: a press here is a press on
    // whatever is behind, so this widens what a widget sees, not what it takes.
    virtual D2D1_RECT_F ExternalRegion() const { return {}; }
    // A wheel turned over this widget, in its own space; `notches` is positive away from the user.
    // Return true to keep it from the container, which would otherwise scroll.
    virtual bool OnWheel(float /*x*/, float /*y*/, float /*notches*/) { return false; }
    // The keyboard's way of working this widget: Space, and Enter on one that has a job. Distinct
    // from OnClick, which is the pointer doing something: a drop-down chooses the row the pointer is
    // over, and there is no pointer to read when the choice came from the keyboard.
    virtual void OnActivate() { OnClick(); }
    // Something happened that should put away anything transient this widget is showing: a press
    // somewhere else, the window being deactivated.
    virtual void Dismiss() {}
    virtual bool OnKey(WPARAM /*vk*/) { return false; }
    virtual bool OnChar(wchar_t /*c*/) { return false; }
    virtual void OnFocus() {}
    virtual void OnBlur() {}
    // The mouse went up on a widget that had capture, wherever the pointer ended up. OnClick is not
    // the same event and cannot stand in for it: it fires only when the release lands back inside
    // the widget, which is exactly what a drag does not do.
    virtual void OnRelease() {}
    // Where the IME should put its composition window, in this widget's own space. Only meaningful
    // for one that takes text; ignored otherwise.
    virtual bool CaretPoint(D2D1_POINT_2F * /*out*/) const { return false; }
    virtual bool HandCursor() const { return false; }
    virtual bool TextCursor() const { return false; }
    // This widget draws something that follows the pointer *inside* itself: an open flyout's hovered
    // row, a segmented control's hovered cell, a slider being dragged. The window repaints on hover
    // *changes*, and moving from one row of a list to the next is not one -- the same widget is
    // hovered throughout -- so without this the highlight stays where it was and only catches up
    // when something else happens to repaint.
    virtual bool TracksPointer() const { return false; }
    // **Whether this widget is the one using the drag.** Not the same question as `TracksPointer`, which
    // says the widget wants the pointer so its own drawing can follow it: an open list's rows follow a
    // finger and the list still scrolls under it, so a list answers that and not this. A control that is
    // moving its own value with the hand -- a slider, a switch, the text of a field being selected, the
    // thumb of a bar -- answers true, and keeps the gesture that would otherwise, past the slop, be
    // handed to a container that scrolls.
    virtual bool Dragging() const { return false; }
    // **A drag that no control took, answered for by a container above it.** A finger on a page is not
    // a finger on a control: once it has wandered past the slop, a control that is not holding a click
    // has nothing left to say about it, and what is left is the gesture itself. The window -- the only
    // side that sees a whole gesture -- asks the widget the press landed on and then each of its
    // ancestors for the first one that says it pans, and gives it the rest of the movement. Asked once
    // per gesture: a page that begins scrolling does not hand the same hand to the next container up
    // when it reaches its end, which is what a page inside a page would otherwise feel like.
    //
    // `PanMove` is handed the movement of the hand since the last call, in client DIPs, and is expected
    // to follow it rather than glide toward it: under a finger, the content is the finger's. `PanRelease`
    // ends the gesture with the speed the hand had, in DIPs per second, which is what a fling is worth.
    // See `ScrollView` and `SideNav`, the two containers that answer this today.
    virtual bool Pans() const { return false; }
    virtual void PanMove(float /*dx*/, float /*dy*/) {}
    virtual void PanRelease(float /*vx*/, float /*vy*/) {}
    // Whether the press shadow should be showing. The window clears `pressed` the moment the pointer
    // leaves the rectangle, which is what makes a button cancellable by dragging off it; a control
    // whose gesture outlives its own rectangle -- a slider dragged out of its track -- says so here.
    virtual bool PressedVisual() const { return pressed; }
    // Whether the pointer is on this widget, in the space its `rect` is in: what the window's hit
    // test asks. Virtual because a control whose box is not simply `rect` -- one that derives it
    // from its own state -- answers with that box.
    virtual bool Covers(float x, float y) const { return Inside(rect, x, y); }
    // The animated shadows of the three flags: 0 is off, 1 is on, anything between is a brush
    // crossing over. Only the background follows them -- a WinUI control under the pointer moves one
    // property, `<ContentPresenter.BackgroundTransition>`, and leaves its border, its text and its
    // focus ring to change between two frames.
    float Want(bool on) const { return on && enabled ? 1.0f : 0.0f; }
    // **Where the pointer last was**, in this widget's own space: the space its rectangle is in, which
    // is the one its input callbacks are handed points in. Asked by a control whose highlight follows
    // the pointer rather than its own last callback -- an open list's row, a pane's -- and it is the
    // window that answers, with the last place *any* hand went. A finger does not move the mouse, so a
    // control reading the cursor itself would be pointing at a place nobody is pointing at: which is
    // exactly what a pane's rows and a list's rows did, and why neither could be clicked with a finger.
    D2D1_POINT_2F Cursor() const;
    // How much room this widget really has: the page's box in its own space, and a container's clip
    // once containers clip. For a control deciding whether something it would show fits.
    D2D1_RECT_F VisibleArea() const;
    // Whether to draw the ring around `rect`. The window's decision and not the control's -- Windows
    // only shows one once the keyboard has been used -- and a control in no window shows none.
    bool ShowFocusRing() const;
    // The layer this widget is, when it is one. Asked by the window, which routes Esc, Enter and the
    // Tab ring through the top layer before the page sees them.
    virtual Layer *AsLayer() { return nullptr; }
    // Where this widget goes when it is one of those: **the tree asks the layer**, and the answer
    // below is the one most layers give -- the whole of the widget it was added to, in that widget's
    // own space. A dialog is added to the page and says nothing here, because covering the page is
    // covering the widget a page is.
    //
    // A layer that wants a rectangle of its own overrides this, because not every layer is a cover:
    // a tip beside the pointer is a box the size of its own words, and it would be as wrong at the
    // host's size as it would be in the wrong place anywhere else. A drop-down's flyout is *not* one
    // of these -- it covers the page, and has to: it takes the wheel so the page behind it does not
    // scroll, and a click outside the list closes it, which means the click has to land on the layer.
    // The list itself, under the field that opened it, is a child of the flyout, and a child's
    // rectangle is its host layout's business.
    //
    // The room comes along because sizing itself is measuring, and measuring needs the fonts; `spec`
    // is the host's own, so a layer that wants to know what a control is tall has the numbers the
    // host is arranged with.
    //
    // It is on the node rather than on `Layer` because the walk is what asks it (see
    // ArrangeSubtree), and the walk is here, where `Layer` is not yet a complete type. A widget that
    // is not a layer is never asked, which is why the answer below is a whole box rather than an
    // error.
    virtual D2D1_RECT_F Cover(const Room &room, const Widget &hostLayout) const {
        (void)hostLayout;
        return { 0.0f, 0.0f, room.width, room.height };
    }
    // Whether this widget is a **window onto its children** rather than a box they fit in: they are
    // drawn only where they are inside it, which is what makes a page too long for the room it has
    // readable. The paint walk pushes it as a clip, and VisibleArea answers with it -- so a control
    // asking how much room it has is told about the container it is scrolling in rather than about
    // the page behind it. Nothing else changes: what is outside the box is drawn, and simply not
    // seen, and the hit test never reaches it because a click outside the container is not a click
    // on the container either. See ScrollView.
    virtual bool Clips() const { return false; }
    // The box a container that clips cuts its children to, in the space its own `rect` is in. The
    // widget's own rectangle unless the container is showing only a part of itself, and written where
    // that part is *drawn* when the widget itself is on its way somewhere -- a clip is a box the eye
    // sees, so it is the drawn box that has to hold. See `Clips`, `VisibleArea`, and `Flyout::Reveal`,
    // which is a panel arriving: the window onto the rows is what grows, and the rows stand still.
    virtual D2D1_RECT_F ClipBox() const { return rect; }
    // ---- animation --------------------------------------------------------------------------
    // Whether this widget is animating **what it draws**: the three pointer states it paints, and
    // whatever its own Tick advances.
    //
    // **Not where it is.** A widget that is being carried somewhere by its container is not animating
    // anything: that is the container's animation, and it is the container's *layout* that says so
    // (see Layout::Gliding). Keeping the two apart is what makes a page scrolling under a stationary
    // pointer cost nothing at all -- the container is animating and the forty controls in it are not
    // -- and it is what lets a control the pointer happens to be over light up on the way past while
    // the ones beside it stay quiet.
    //
    // The walk down is a big OR and is written as one: a layout that is gliding has already answered
    // for everything under it, so there is nothing to find by descending.
    virtual bool Animating() const {
        if (hoverT != Want(hover) || pressT != Want(PressedVisual()) || focusT != Want(focus))
            return true;
        if (layout && layout->Gliding()) return true;
        for (const auto &c : children) {
            if (c->visible && c->Animating()) return true;
        }
        return false;
    }
    // One frame for the subtree: this widget's own pointer states, then its children, then the layout
    // that owns them -- which is what glides whatever it arranged somewhere new.
    virtual void Tick(float dt) {
        motion::Ramp(&hoverT, Want(hover), dt, motion::kFaster);
        motion::Ramp(&pressT, Want(PressedVisual()), dt, motion::kFaster);
        motion::Ramp(&focusT, Want(focus), dt, motion::kFaster);
        for (auto &c : children) {
            if (c->visible) c->Tick(dt);
        }
        if (layout) layout->Tick(dt);
    }

    // ---- the surface ---------------------------------------------------------------------------
    // Found by walking up. The root of a tree holds the surface itself, which is how a subtree that
    // was built before it was added still answers: the walk reaches the root and the root knows. A
    // widget in no tree at all answers null, which is the state a page builds in.
    //
    // **A surface rather than a window**, because what a widget needs is the tree it is in and the
    // hand that is on it -- and a menu's tree is one of those too.
    virtual Surface *surface() const { return parent ? parent->surface() : host; }
    // The surface this node's root belongs to. Set on the root alone -- see Window::EnsureContent.
    Surface *host = nullptr;
    // Ask for another arrangement, and for a repaint.
    void InvalidateLayout();
    void Invalidate();
    // Whether `w` is this widget or under it, which is what the window asks before it takes a
    // subtree out of the way -- a modal layer's own contents have to stay reachable.
    bool Holds(const Widget *w) const {
        for (const Widget *at = w; at; at = at->parent)
            if (at == this) return true;
        return false;
    }

    // ---- what a screen reader is told --------------------------------------------------------
    // Four questions and one string. `tips` is the tooltip text as well, and one string is the
    // point: what a control says in a tooltip and what it says to somebody who cannot see it are
    // the same thought.
    std::wstring tips;
    // **What a right-click asks for**: a menu, filled by this function when it is opened. Null for a
    // widget that has no menu of its own, and then the right-click goes up to the widget that holds
    // one -- a click on a card's label is a click on the card. See `Menu` and `Menus` in menu.h, and
    // `Menus` is what opens it.
    std::function<void(Menu &)> contextMenu;
    // The widget a context menu belongs to: this one, or the first one above it that has one. Null
    // when nothing in the walk has one, which is the honest answer for a control in a page with no
    // menus at all.
    Widget *MenuTarget() {
        for (Widget *at = this; at; at = at->parent)
            if (at->contextMenu) return at;
        return nullptr;
    }
    // What the page calls this widget, for the times the page knows a name the widget does not. The
    // words beside a switch are page text and are not part of it. Set, and it wins over the virtual.
    std::wstring accessibleName;
    virtual const wchar_t *AccessibleName() const { return nullptr; }
    const wchar_t *AccessibleLabel() const {
        return accessibleName.empty() ? AccessibleName() : accessibleName.c_str();
    }
    // Custom rather than Pane: a widget that has not said what it is has not said it is a container
    // either, and a client guessing from Custom guesses less wrong.
    virtual int AccessibleType() const { return UIA_CustomControlTypeId; }
    // -1 when this is not a switch; otherwise 0 off, 1 on, 2 indeterminate.
    virtual int AccessibleToggle() const { return -1; }
    // False when there is no value worth reading. True fills `out` with what a screen reader should
    // say -- "40%", the text of a field -- so the formatting stays the widget's business.
    virtual bool AccessibleValue(std::wstring & /*out*/) const { return false; }
    // Whether OnActivate does something a client may ask for on this widget's behalf. False by
    // default, focusable or not: a field is focusable and activating it does nothing.
    virtual bool AccessibleActionable() const { return false; }

    // **A widget that draws a set of things, and what one of them is.** A list of forty countries is
    // one widget and forty things a client has to be able to read one at a time -- the rows of an open
    // drop-down, the cells of a segmented control, the items of a navigation pane. So a widget answers
    // how many it is made of, and a client visits them as the children of that one widget: one
    // `ListItem` element per row, which is what WinUI's own list reports, and no widget per row.
    //
    // A thing like this is not a widget, so a uid is not enough to name one: an element for an item is
    // the widget's uid and the item's *place* in the sequence. `index` is the page's own number for it,
    // which is what a client asks the control to act on -- a pane counts its headings and a list counts
    // from zero, and neither of those is the place.
    struct Item {
        const wchar_t *name = nullptr;   // what a screen reader reads out
        int type = UIA_ListItemControlTypeId;
        int index = -1;                  // the page's own number for it, when it has one
        bool selected = false;
        // Where it is drawn **in the space this widget's `rect` is measured in** -- the space a pointer hook
        // is handed and the space `Paint` draws in, *not* a box measured from its own top-left corner (see
        // `TipAt` for the same rule about a tip) -- and whether it is on screen at all: a row the scroll has
        // carried out of its panel is an item a client is told about and cannot reach. The element adds the
        // accumulated origin of the widget's ancestors to this; the widget's own origin is already in it, and
        // a widget that got that wrong reports a row somewhere the row is not.
        D2D1_RECT_F box = {};
        bool onscreen = false;
    };
    // How many things this widget is made of. Zero -- the default -- is a widget that is one thing, and
    // then a client's children of it are its child widgets, which is the tree the page built.
    virtual int AccessibleItems() const { return 0; }
    // What item `i` is. False for a widget that has no such item, which is the honest answer for an
    // element a client held on to while the list under it changed.
    virtual bool AccessibleItem(int /*i*/, Item & /*out*/) const { return false; }

    // ---- and the tip, for the same reason ------------------------------------------------------
    // **What a tip is about, for the point the pointer is on.** A control that is one thing says the same
    // words wherever it is touched, and that is `tips`; a widget that draws a *set* of things has one tip
    // per row -- a rail of icons is a column of names that are not drawn, and hovering one is how a person
    // reads it -- and a row is not a widget that can carry the string. So the widget answers for the point:
    // the words, the box they are about, and whether they go beside that box rather than under it.
    //
    // Both the point and the box are in the space the `rect` is measured in, which is the space every
    // pointer hook is handed. Empty words mean there is nothing to say *here*, which is what a widget whose
    // labels are already drawn answers: a tip that repeats the word beside the cursor is noise.
    struct Tip {
        std::wstring text;
        D2D1_RECT_F box = {};
        // **Beside rather than under**, for a tip about one thing in a column of them: a rail has the next
        // icon under every row, and a box of words that lands on it hides the thing next to the one it is
        // about. WinUI's own compact navigation pane asks for its tooltips on the right.
        bool beside = false;
    };
    virtual Tip TipAt(float /*x*/, float /*y*/) const { return { tips, rect, false }; }
    // A number a client can compare two values of, for a control whose value is one: a slider, a
    // progress bar. False for a control that has no range, which is not the same as one whose value is
    // zero. See `IValueProvider` and `IRangeValueProvider` in the UIA section of window.h: the first is
    // for a value that is words -- a field's text -- and the second for one that is a number.
    virtual bool AccessibleRange(float & /*value*/, float & /*minimum*/, float & /*maximum*/,
                                 float & /*step*/) const {
        return false;
    }
    // Whether this widget can be opened and closed: a drop-down's list, a navigation pane. -1 for one
    // that cannot, otherwise 0 collapsed and 1 expanded -- the two states `ExpandCollapseState` has a
    // word for, and the two this has.
    virtual int AccessibleExpanded() const { return -1; }
    // Whether this widget is the thing a client can scroll, and how far it has: see `IScrollProvider`.
    virtual bool AccessibleScroll(float & /*percent*/, float & /*view*/, bool & /*canScroll*/) const {
        return false;
    }

    // **Whether a client may change this control, and how.** The write half of each pattern is answered
    // by these, and every one of them goes through the page's own path -- the callback a gesture would
    // have run -- so that a screen reader's "set this to 40" and a hand on the slider arrive at the page
    // the same way. That is the whole of what makes a written value safe: the page cannot tell which of
    // the two happened, so there is no second kind of change for it to get wrong.
    //
    // A widget that answers none of them is read-only, which is the default, and each pattern says so
    // rather than staying quiet: `IsReadOnly` true, and the write refused. A client handles a refusal;
    // a pattern that is not there at all it can only guess about.
    virtual bool AccessibleWritable() const { return false; }
    // Set the number, for a control whose value is one. False for a number this control does not take,
    // which is an answer and not a fault: a client that gets nothing back can only try something else.
    virtual bool AccessibleSetRange(float /*value*/) { return false; }
    // Set the words, for a control whose value is text -- and for a drop-down, whose value is the name
    // of one of its options.
    virtual bool AccessibleSetValue(const std::wstring & /*text*/) { return false; }
    // Choose the item with this index -- the page's own number for it, which is what `Item::index` is
    // for, and not the item's place in the sequence.
    virtual bool AccessibleSelect(int /*index*/) { return false; }
    // Open or close, for the controls that have those two states.
    virtual bool AccessibleSetExpanded(bool /*open*/) { return false; }
    // Scroll to `percent` of what there is to scroll: 0 to 1, and not the 0 to 100 a client speaks in.
    virtual bool AccessibleSetScroll(float /*percent*/) { return false; }
    // The number Add gave this widget. What a UIA element holds instead of a pointer, which is what
    // makes an element that has outlived its widget harmless.
    int uid = 0;

private:
    static int NextUid() {
        static int next = 0;
        return ++next;
    }
};

// ---- the parts of the protocol that need the node they walk ------------------------------------
//
// Defined here rather than in layout.h, which knows a Widget only by name: the glide moves a
// child's drawn rectangle, and only this header knows what one is.

inline bool Layout::Gliding() const {
    if (!host_) return false;
    for (const auto &child : host_->children) {
        if (!child->visible) continue;
        if (!child->placed || !SameRect(child->drawn, child->rect)) return true;
    }
    return false;
}

inline void Layout::Tick(float dt) { Glide(dt); }

inline void Layout::Invalidate() {
    if (host_) host_->InvalidateLayout();
}

inline bool Layout::Glide(float dt) {
    if (!host_) return false;
    bool moving = false;
    for (auto &child : host_->children) {
        Widget *w = child.get();
        if (!w->visible) continue;
        if (!w->placed) {
            // The first arrangement places without moving: a page that has just been built is where
            // it is, and sliding it in from wherever the empty rectangle happens to be is not a
            // transition anybody asked for.
            w->drawn = w->rect;
            w->placed = true;
            continue;
        }
        if (!Animations()) {
            w->drawn = w->rect;
            continue;
        }
        // All four edges, every frame: Follow is also what puts a value on its target when
        // animations are off, so it is called rather than skipped.
        bool m = motion::Follow(w->drawn.left, w->rect.left, dt, glideLag, glideSnap);
        m = motion::Follow(w->drawn.top, w->rect.top, dt, glideLag, glideSnap) || m;
        m = motion::Follow(w->drawn.right, w->rect.right, dt, glideLag, glideSnap) || m;
        m = motion::Follow(w->drawn.bottom, w->rect.bottom, dt, glideLag, glideSnap) || m;
        moving = moving || m;
    }
    return moving;
}

// One node and everything under it, placed: a widget's box is what its parent's layout made it, and
// its own layout then arranges its children inside that box. The window runs exactly this walk,
// from the root, once per frame if anything asked for it -- and a program that wants the rectangles
// without a window to draw them in runs it too, which is what a `--dump` mode is.
//
// Nothing here needs a window: a layout's numbers come from the room, and its children answer from
// their own content.
inline void ArrangeSubtree(Widget *w, const Fonts &fonts) {
    // A widget that has never been placed is placed without moving; the ones that have been keep the
    // rectangle they are drawn in until the glide catches up with the new one.
    if (!w->placed) { w->drawn = w->rect; w->placed = true; }
    // The room the host is being arranged in, built before the layout because a layer is measured in
    // it too -- a layer that sizes itself to its own content has to measure something. `spec` is the
    // host's, and a host with no layout of its own is measured against the platform's, since a layer
    // is still a control and still wants to know what one is tall.
    Room room;
    room.fonts = &fonts;
    room.spec = w->layout ? &w->layout->spec : &PlatformSpec();
    room.width = Width(w->rect);
    room.height = Height(w->rect);
    if (w->layout) w->layout->Arrange(room, { 0.0f, 0.0f, room.width, room.height });
    // A layer takes no place in the layout of the host, so the host's layout does not place it -- one
    // that did would have given it a slot in the column and pushed everything below down by the
    // height of a page. Where it goes is the layer's own answer instead (Layer::Cover), and the
    // default answer -- the whole of the host -- is what a dialog's is: it was added to the page, so
    // it covers the page. Before the glide, because the glide's target is this rectangle.
    for (auto &child : w->children) {
        if (!child->visible) continue;
        if (child->AsLayer()) child->rect = child->Cover(room, *w);
    }
    if (w->layout) w->layout->Glide(0.0f);
    for (const auto &child : w->children)
        if (child->visible) ArrangeSubtree(child.get(), fonts);
}

// Everything under `w`, placed where it was arranged rather than left to glide there.
//
// A layout change is animated by the glide, and that is what makes a card step down when something
// above it opens. A subtree *tracking* something that is moving is not a layout change: the glide
// closes a fraction of the gap per frame, so a target that keeps moving leaves it permanently behind
// -- the faster the target, the further behind -- and the catch-up at the end of it is a jump. Two
// things are that: a window being dragged by its border, which is `ArrangeTree`, and a navigation
// pane animating its width, which is `NavigationView::Tick`.
inline void PlaceSubtree(Widget *w) {
    w->drawn = w->rect;
    w->placed = true;
    for (auto &child : w->children)
        if (child->visible) PlaceSubtree(child.get());
}

// Everything under `w`, as if it had never been arranged: the next arrangement *places* it where it
// goes rather than gliding it there.
//
// This is what a subtree coming back on screen wants. A hidden subtree is skipped by the arrangement,
// so it is still laid out for the room it had last time -- and the room it has now is usually not that
// one. **A glide cannot carry a size**: what glides is a translation, so a card whose right edge has to
// move is the whole card sliding, leaving the room it should have filled empty behind it until it
// catches up. Unplaced, it is placed exactly, which is what a widget being added to the tree gets.
//
// Every child, visible or not: an invisible one is skipped by the arrangement either way, and the day
// it is shown it wants to be born again too.
inline void UnplaceSubtree(Widget *w) {
    w->placed = false;
    for (auto &child : w->children) UnplaceSubtree(child.get());
}

// ---- the fling, in Android's numbers ---------------------------------------------------------------
//
// A throw has a distance and a duration, and both come off one curve whose constants belong to Android
// -- `Scroller` and `OverScroller` in the AOSP, over `ViewConfiguration.getScrollFriction`. Borrowed
// rather than invented for the same reason the recognizer was: the shape of a fling is pure feel, there
// is nothing about it a library can be right about on its own, and these are the numbers a great many
// thumbs have already been trained on.
//
// **The duration is the point of it.** The obvious fling -- keep the speed, multiply it down by a
// friction every frame -- has no horizon: at a tenth a second, a page thrown at 1000 DIPs a second is
// still creeping three and a half seconds later, and an eye can see a sixtieth of that. Android's curve
// is over when its duration is up, and puts the tail in the tension at the end rather than in an
// asymptote. Nothing owns one of these: a container that pans keeps three or four numbers of its own and
// asks these for the rest -- see `ScrollView` and `SideNav`.
namespace fling {
// The deceleration exponent -- Android's `DECELERATION_RATE`.
constexpr float kRate = 2.3582f;      // log(0.78) / log(0.9)
// Where tension and deceleration cross, and how much tension at each end. The two control coefficients
// are that cubic, from Android's own pair of tensions.
constexpr float kInflexion = 0.35f;
constexpr float kTensionIn = 0.5f;
constexpr float kTensionOut = 1.0f;
constexpr float kP1 = kTensionIn * kInflexion;
constexpr float kP2 = 1.0f - kTensionOut * (1.0f - kInflexion);
// Android's default coefficient of friction, and the physical coefficient it is weighed against: gravity
// on a kilogram, the inches in a metre, and 160 pixels to the inch -- which in a world measured in DIPs
// is one, so these are the numbers a 1x screen gets. The last 0.84 is Android's own tuning constant for
// the second half of it, and is not the friction.
constexpr float kFriction = 0.015f;
constexpr float kPhysical = 9.80665f * 39.37f * 160.0f * 0.84f;
constexpr float kDecel = kFriction * kPhysical;
// **The two ends of a throw nobody can make**: under this nothing is thrown at all, and over it is not a
// thumb. Android's own bounds, in DIPs a second.
constexpr float kSlowest = 50.0f;
constexpr float kFastest = 8000.0f;
// How far a speed is worth, in DIPs. Android's spline function, with the logarithm taken once.
inline float Distance(float speed) {
    const float l = std::log(kInflexion * speed / kDecel);
    return kDecel * std::exp(kRate / (kRate - 1.0f) * l);
}
// And how long the throw takes, in seconds.
inline float Duration(float speed) {
    const float l = std::log(kInflexion * speed / kDecel);
    return std::exp(l / (kRate - 1.0f));
}
// Where in that distance the throw is, `t` being how much of the duration has gone by. Android builds a
// hundred-sample table of the curve and walks it; this builds the same table once, with the same
// three-way search, and reads it the same way.
inline float Position(float t) {
    constexpr int kN = 100;
    static float curve[kN + 1];
    static bool built = false;
    if (!built) {
        float lo = 0.0f;
        for (int i = 0; i < kN; i++) {
            const float alpha = (float)i / kN;
            float hi = 1.0f, x = 0.0f, coef = 0.0f;
            for (int step = 0; step < 32; step++) {
                x = lo + (hi - lo) / 2.0f;
                coef = 3.0f * x * (1.0f - x);
                const float at = coef * ((1.0f - x) * kP1 + x * kP2) + x * x * x;
                if (std::fabs(at - alpha) < 1e-5f) break;
                if (at > alpha) hi = x;
                else lo = x;
            }
            curve[i] = coef * ((1.0f - x) * kTensionIn + x) + x * x * x;
        }
        curve[kN] = 1.0f;
        built = true;
    }
    const float u = std::clamp(t, 0.0f, 1.0f) * (float)kN;
    const int i = (int)u;
    if (i >= kN) return 1.0f;
    return curve[i] + (u - (float)i) * (curve[i + 1] - curve[i]);
}
}  // namespace fling

}  // namespace micula
