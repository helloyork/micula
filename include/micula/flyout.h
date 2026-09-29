// Micula / flyout.h
//
// A panel that drops out of a control: what a drop-down's list, a menu or a suggestion box is put
// in. Three things make it a layer rather than a box beside the control, and the first two are the
// same thing:
//
//   - **It covers the page, and that is what makes it behave.** The page behind a flyout cannot be
//     scrolled by a wheel and cannot be clicked: the flyout is a sibling of the page laid over it, so
//     the pointer under it is the flyout's and the wheel offered to the flyout's subtree is never
//     offered to the page's. Nothing had to be taught either one -- a layer that covers the page is a
//     layer the page is not reached through.
//   - **It light-dismisses**: a click that misses the panel, or the window losing activation, closes
//     it. That is what `Layer` is for, and it needs the covering: the click that misses the panel has
//     to land on the layer for the layer to hear about it.
//   - **It is anchored**: the panel is placed under the control that opened it, or over it when the
//     room below is not enough, and it points at that control with an edge that touches it.
//
// ```cpp
// auto *f = new Flyout(control->rect);   // what to hang it off, in the page's own space
// f->Add(new DropDownList(...));         // the panel: the flyout's one child, and it places it
// f->onClose = [this] { opened = false; };
// page->Add(f);                          // the page, so it covers it -- see Layer::Cover
// ```
//
// What it is not is a rectangle of its own: `Cover` is left alone, so a flyout is always the whole of
// what it was added to. A tip is the other way round -- a box the size of its own words -- which is
// why `Cover` is a question the tree asks rather than a rule about layers.

#pragma once

#include "window.h"

#include <algorithm>
#include <functional>

namespace micula {

struct Flyout;

// Places the flyout's one child: under the anchor, or over it when the room below is not enough, at
// least as wide as the anchor and never past the room the flyout has.
struct FlyoutLayout : Layout {
    // The control the panel hangs off, in the space the flyout is placed in. Set by the flyout.
    D2D1_RECT_F anchor = {};
    // The gap between the panel and the anchor, and the margin between the panel and the edge of the
    // page: the same 4 DIPs WinUI leaves around a drop-down's list.
    static constexpr float kGap = 4.0f;
    float gap = kGap;

    Want Measure(const Room &room) const override;
    void Arrange(const Room &room, const D2D1_RECT_F &box) override;
};

struct Flyout : Layer {
    // What the panel hangs off, in the page's space -- the control's own rectangle, which is the same
    // space the page's children are placed in.
    D2D1_RECT_F anchor = {};
    // The panel, which is the flyout's one child. Everything else the flyout has is the page under it.
    Widget *panel = nullptr;
    // Told when it has been dismissed, before the fade runs. A control that opened a flyout is what
    // knows whether it is still open, and this is where it hears.
    std::function<void()> onClose;
    // Where the panel goes when "under the anchor" is not what is wanted: the panel's own
    // `panelLine` is put on the page's `anchorLine`. A drop-down asks for the centre of its chosen row
    // on the centre of the control, so that the panel covers the control with the choice on it -- what
    // Windows 11's combo box does, and what makes a choice read as a swap rather than as a menu. Both
    // are y values, and the room has the last word either way.
    bool  linedUp = false;
    float panelLine = 0.0f;
    float anchorLine = 0.0f;
    // **The marker**: the one child a flyout may have that is not the panel, and it is placed at the
    // panel's own line rather than at the panel. A drop-down puts its accent mark on it -- the row the
    // panel was placed so that it covers, which is the chosen one.
    //
    // It has to be here rather than inside the panel, and that is the whole of what a choice looks
    // like. A list with room to spare moves the *panel* a row to bring the choice to the control; a
    // mark inside the panel would move with it and the choice would be a thing that slid past. Placed
    // from the panel's settled rectangle instead, the mark is already where the panel is going, and
    // the options travel under it -- which is a dial rather than a menu, and what Windows does.
    Widget *marker = nullptr;
    float   markerH = 0.0f;

    explicit Flyout(D2D1_RECT_F at) : anchor(at) {
        lightDismiss = true;
        onDismiss = [this] {
            if (onClose) onClose();
            Close();
        };
        auto *place = new FlyoutLayout();
        // The panel slides from one row to the next, and Fluent's drop-down slides it on a follower of
        // about a twentieth of a second rather than on a curve with a duration: a wheel spun through a
        // list retargets it several times inside one frame, and a storyboard restarted that often would
        // stutter between the notches. The glide is that follower -- see Layout::glideLag.
        place->glideLag = 0.05f;
        place->glideSnap = 0.004f;
        SetLayout(place);
    }

    // What the layer's own content is, which for a flyout is the panel: a click that misses it belongs
    // to the page behind -- and this is what a click outside a list is *for*. `Layer`'s default is its
    // whole rectangle, which a flyout covers the page with, so without this nothing would ever be a
    // click outside it.
    D2D1_RECT_F Body() const override { return panel ? panel->rect : Layer::Body(); }

    // The wheel belongs to the flyout rather than to the page behind: an open list is a list to choose
    // from, and a wheel that scrolled the page out from under it -- or left it pointing at a place the
    // page no longer has -- is the one thing an open list must not do. Which *part* of the flyout acts
    // on the notch is the panel's business: a list that can scroll does, and a list that cannot leaves
    // it here to be swallowed.
    bool OnWheel(float, float, float) override { return true; }

    // The panel is the only thing a flyout is given, so adding one is adding the panel. Written out
    // rather than inherited for the same reason `ScrollView::Add` is: children are painted in order,
    // and there is nothing here for a second child to be.
    template <typename T> T *Add(T *w) {
        panel = w;
        return Widget::Add(w);
    }

    // Where the panel may go, in the flyout's own box: the room under the anchor, and the room over
    // it. The layout picks between them; a control that wants to say "under, and only under" overrides
    // this. Here rather than in the layout because the anchor is the flyout's.
    D2D1_RECT_F RoomBelow(const D2D1_RECT_F &box) const {
        return { box.left, anchor.bottom, box.right, box.bottom };
    }
    D2D1_RECT_F RoomAbove(const D2D1_RECT_F &box) const {
        return { box.left, box.top, box.right, anchor.top };
    }

    // The panel's own surface: a flyout is over the page rather than part of it, so it gets an opaque
    // fill of its own, a contour and a shadow -- a narrower shadow than a dialog's, because it hangs
    // off a control rather than sitting in the middle of the page.
    //
    // Drawn by the flyout rather than by the panel, and that is what lets the panel be a picture of
    // nothing: a `ScrollView` with rows in it, a `View` with a list in it. The children are painted
    // after this, so the surface is under them, and the panel's own rectangle is what the surface is.
    void Paint(const Painter &p) override {
        Layer::Paint(p);                       // the smoke, when there is one
        if (!panel) return;
        // **The panel where it is drawn**, not where it was arranged. The surface is what the rows are
        // read through, and one that jumped to where the panel is going while the rows slid there would
        // be a frame arriving before its contents -- which is exactly what it looked like. PaintTree
        // draws a widget's children at its `drawn` for the same reason; this is the widget that draws
        // its child's surface, so it has to read it the same way.
        const D2D1_RECT_F box = panel->placed ? panel->drawn : panel->rect;
        p.Shadow(box, 8.0f, 1.0f, 14.0f, 4.0f);
        p.FillRound(box, 8.0f, p.pal->flyoutBg);
        p.StrokeRound(box, 8.0f, p.pal->flyoutStroke);
    }
};

inline Want FlyoutLayout::Measure(const Room &room) const {
    // The panel's own answer, since a flyout is measured before it is placed and the anchor has no
    // say yet. A flyout is a layer, so nobody arranges it from this -- see ArrangeSubtree.
    const Flyout *f = static_cast<const Flyout *>(host_);
    return f->panel ? f->panel->Measure(room) : Want(Sizing::Fill, Sizing::Fill);
}

inline void FlyoutLayout::Arrange(const Room &room, const D2D1_RECT_F &box) {
    Flyout *f = static_cast<Flyout *>(host_);
    if (!f->panel) return;

    // What the panel would come to where each way is open, and the taller of the two is the one that
    // shows more of it. Measured against the width it would really get, because a list of words wraps
    // or does not wrap at exactly that width.
    const float w = (std::max)(Width(f->anchor), 0.0f);
    Room inner = room;
    inner.width = w;

    const D2D1_RECT_F below = f->RoomBelow(box);
    const D2D1_RECT_F above = f->RoomAbove(box);
    const float fitBelow = (std::max)(0.0f, Height(below) - 2.0f * gap);
    const float fitAbove = (std::max)(0.0f, Height(above) - 2.0f * gap);
    const float fitAll = (std::max)(0.0f, Height(box) - 2.0f * gap);

    // **Measured in the room the panel will really be given.** One side of the anchor for a panel that
    // hangs off it, and the whole page for one that is lined up with it -- a combo box's list reaches
    // above and below the control it covers. Measured in the wrong one, a panel is clamped to a room it
    // never has: a thirty-option list came out the height of the space *below* the control, which on a
    // control halfway down a page is a hundred and ten DIPs of a six-hundred-DIP list.
    Room measured = inner;
    measured.height = f->linedUp ? fitAll : fitBelow;
    const Want want = f->panel->Measure(measured);
    const float wantH = want.h.how == Sizing::Fill ? measured.height : want.h.size;
    // Under it first: a list that fits below is a list that hangs off the control the way a list does.
    // It goes over only when the room below cannot hold it and the room above can hold more -- the
    // answer WinUI's own popup gives, and the reason the panel's height is decided here rather than by
    // what is in it.
    //
    // A panel that asked to be lined up is not going either way, so its room is the whole page rather
    // than one side of the anchor: a combo box's list reaches above and below the control it covers,
    // and the only thing that decides which is where the chosen row has to be.
    const bool up = !f->linedUp && fitBelow < wantH && fitAbove > fitBelow;
    const float height = (std::min)(wantH, f->linedUp ? fitAll : (up ? fitAbove : fitBelow));

    const float panelW = (std::max)(w, want.w.how == Sizing::Fill ? w : want.w.size);
    float left = f->anchor.left;
    if (left + panelW > box.right - gap) left = box.right - gap - panelW;
    if (left < box.left + gap) left = box.left + gap;

    float top;
    if (f->linedUp) {
        const float most = (std::max)(box.top + gap, box.bottom - gap - height);
        top = std::clamp(f->anchorLine - f->panelLine, box.top + gap, most);
    } else {
        top = up ? f->anchor.top - gap - height : f->anchor.bottom + gap;
    }
    f->panel->rect = { left, top, left + panelW, top + height };

    // The marker, at the panel's own line as it has *settled*. See Flyout::marker: a mark inside the
    // panel would ride the panel's slide, and a choice would be something that slid past rather than
    // something that changed.
    if (f->marker) {
        const float mid = top + (f->linedUp ? f->panelLine : 0.0f);
        f->marker->rect = { left + 1.0f, mid - f->markerH / 2.0f, left + 4.0f,
                            mid + f->markerH / 2.0f };
    }
}

}  // namespace micula
