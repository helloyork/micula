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

#include <memory>
#include <string>
#include <vector>

namespace micula {

struct Painter;
struct Window;
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
    // The pointer, in this widget's own space: the space its rectangle is in, which is the one its
    // input callbacks are handed points in. It is the *physical* pointer, so a harness that posts
    // mouse messages cannot drive a control that reads this -- take a drag's points from OnPress and
    // OnDrag, and read this only for what genuinely means "where is the pointer now".
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
    virtual D2D1_RECT_F Cover(const Room &room, const Widget &host) const {
        (void)host;
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

    // ---- the window --------------------------------------------------------------------------
    // Found by walking up. The root of a tree holds the window itself, which is how a subtree that
    // was built before it was added still answers: the walk reaches the root and the root knows. A
    // widget in no window at all answers null, which is the state a page builds in.
    virtual Window *window() const { return parent ? parent->window() : win; }
    // The window this node's root belongs to. Set on the root alone -- see Window::EnsureContent.
    Window *win = nullptr;
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

}  // namespace micula
