#pragma once

// The shell a navigation window is: a pane down the left, and one page at a time beside it.
//
// WinUI calls this NavigationView, and the pane in it is `SideNav` -- which stays a control: it is a
// column that selects, and it says which row was chosen. What this adds is the other half of that
// question, which is what a row *is*:
//
//     auto *nav = root->Add(new NavigationView());
//     nav->pane->style = PaneStyle::Peek;
//
//     auto *home = nav->AddPage({ glyph::kHome, L"Home" }, new ScrollView());
//     home->Add(new Heading(L"Home"));
//
// **A row and a page are one thing said twice**, so `AddPage` adds both in one call: a page nobody
// can reach is a page that is not there, and a row that leads nowhere is a row with nothing to show.
// A row that is not a page -- a heading, or a name that is only a name -- is `AddRow`.
//
// The tree is three nodes, in this order:
//
//     NavigationView     the shell: what the pane takes, and what is left for the page
//       View             the page area, which is what a page adds to
//       SideNav          the pane, last because a pane that covers is painted over the page
//
// Switching a page is **visibility and nothing else**. Every page is in the tree and arranged into
// the same box, and the one being shown is the visible one -- so what a page was in the middle of (an
// open drop-down, half a field, an animation in flight) is still there when its row comes back.
// Nothing is rebuilt, which is the whole reason a page is not.
//
// **A page arriving is drawn arriving.** The page area is one group at one opacity, so the page that
// has just been chosen comes up from nothing over `motion::kNormal` -- the whole page at once rather
// than each control on it, which is what a fade of a subtree has to be to show nothing through the
// gaps between its widgets. It is the *page area* that fades rather than the page, which is why the
// opacity is `content`'s: the surface the shell draws under it stays where it is, because the page
// comes and goes and the page area does not. What was there is gone by the time the new one is drawn,
// so there is nothing to cross-fade with and nothing to slide past -- and the first page of a
// window's life arrives in one frame, because a page being put up for the first time has nothing to
// arrive *from*. See `transition`.
//
// **And it arrives from below.** The page starts `kPageRise` DIP under where it was arranged and rises
// into place while it fades, which is the shape this animation has wherever it is used: a page comes
// *up* out of the page area, rather than in from the side, which would be saying the pages are laid
// out in a line. The two parts are one track -- the same 0..1 is the distance and the opacity -- so a
// page is never half up and fully solid, and there is no second animation to keep in step.
//
// The pane's own two questions -- does it push the page or cover it, and how wide is it -- are the
// pane's, and this is the one place that acts on the answer: a pane that pushes is given the room it
// is *drawn* at, so the page follows it while it moves; a pane that covers is given the rail, and the
// page stays where it was. See `SideNav::Reserved` and `SideNav::Pushes`.

#include "side_nav.h"
#include "stack_layout.h"
#include "view.h"

#include <functional>
#include <string>
#include <vector>

namespace micula {

struct NavigationView;

// How far below its place a page starts when it is arriving, in DIP. A page arriving rises *into* the
// page area, so the strip that is not covered yet is the top one -- and the page area's bottom edge is
// the window's own, so what the rise pushes off the other end is off the window rather than over
// something else. That is why the entrance needs no clip: one would have to be the rounded shape of
// the surface to be worth having, and the corner it would cut square is the corner the surface exists
// for. The distance is the one this animation is used at, and a rise of nothing is `Transition::Fade`.
constexpr float kPageRise = 24.0f;

// The shell's own layout: the page's box, and the pane's beside it.
//
// It has one thing to work out, and it is a question only the pane can answer -- how much room the
// pane is taking -- so the arrangement is a method of the shell's rather than arithmetic of the
// layout's: the layout is what asks for it, and `NavigationView::Tick` asks for the same placement
// once the pane has moved. See `Place`.
struct NavigationLayout : Layout {
    Want Measure(const Room &room) const override;
    void Arrange(const Room &room, const D2D1_RECT_F &box) override;
};

struct NavigationView : View {
    NavigationView();

    // The pane, and the rows in it. Its style, its width, its scrim and the rest are its own fields:
    // a shell that repeated them would be a second place to look for the same answer.
    SideNav *pane = nullptr;
    // The page area. The pages are its children, which is what makes switching one a `visible` flag
    // and no more than that.
    View *content = nullptr;

    // A page puts its widgets in the page area. The shell's own children are its two, and the pane of
    // it is last for a reason that has nothing to do with the order a page builds in.
    template <typename T> T *Add(T *w) { return content->Add(w); }

    // A row and the page it shows, added together and hidden until it is the chosen one.
    template <typename T> T *AddPage(NavItem item, T *page) {
        item.header = false;
        rows.push_back({ std::move(item), page, false });
        SyncRows();
        content->Add(page);
        Show(pane->selected);
        return page;
    }
    // A row that shows nothing: a heading, or a name that is a name. It is a row of the pane like any
    // other -- it can be hovered, and it takes its place in the list -- and choosing it shows no page,
    // which is what a row with no page is.
    void AddRow(NavItem item) {
        rows.push_back({ std::move(item), nullptr, false });
        SyncRows();
    }
    // A row pinned to the bottom of the pane, where Settings lives. It is the last row that can be
    // chosen whatever order the pages were added in, which is the pane's own rule and the reason the
    // page is found by walking the rows rather than counted. See PageAt.
    void AddFooter(NavItem item) {
        rows.push_back({ std::move(item), nullptr, true });
        SyncRows();
    }
    template <typename T> T *AddFooter(NavItem item, T *page) {
        item.header = false;
        rows.push_back({ std::move(item), page, true });
        SyncRows();
        content->Add(page);
        Show(pane->selected);
        return page;
    }

    // Which page is showing. The pane's callback is what calls this, so a row chosen by the pointer,
    // the keyboard or the wheel all arrive here; `AddPage` calls it too, to put the first page up.
    void Show(int i);
    // The row that is chosen, and hence the page that is showing. The pane's, read here.
    int Selected() const { return pane->selected; }
    // Choose from outside -- a page's own button, or a state asked for on the command line -- and the
    // pages follow: the pane is told first, and shows its mark moving to the row it was given.
    bool Select(int i) { return pane->Select(i); }

    // The pane's rectangle and the page's, from the box the shell was arranged into. Called by the
    // arrangement, and again by `Tick` -- see there for why the second one is needed.
    void Place(const D2D1_RECT_F &box);
    void Tick(float dt) override;
    // The page arriving is an animation the shell keeps for itself, which the tree's own walk knows
    // nothing about: without this the frame loop stops on the frame the page was chosen on, and what is
    // left on screen is the page at whatever the opacity of that frame was -- nothing, for a page that
    // has just been put up. See `SideNav::Animating` for the same shape one control down.
    bool Animating() const override { return Widget::Animating() || arrival.Wants(1.0f); }

    // How a page arrives, which is the two parts of the same thing:
    //
    //   `None`     --- in one frame, for a page change that is not worth a quarter of a second;
    //   `Fade`     --- the page area coming up from nothing;
    //   `Entrance` --- and the page rising into place while it does.
    //
    // All of them are put aside when animations are off, like every other track in the library --
    // `Track::Step` is where that is decided, not here.
    enum class Transition { None, Fade, Entrance };
    Transition transition = Transition::Entrance;

    // **The page area is a surface rather than part of the window** -- WinUI's NavigationView content
    // is a layer over the backdrop: its top-left corner rounded and its other three square, tucked
    // under the title bar and flush with the other edges. The layer colour and a border inside the two
    // edges that face the rest of the window are what make it read as a surface, and the right and
    // bottom edges are the window's own -- where a line would double a boundary that is already there,
    // and where a translucent layer's border shows up darkest, the layer ending with nothing behind
    // the half-covered pixel. Off is the same shell on a flat background, which is what a window that
    // paints its own background has nothing for a translucent layer to be over.
    bool pageSurface = true;
    void Paint(const Painter &p) override;

    // A row was chosen. The pages have already followed it; this is the page's own, for a row that is
    // something other than a page, or for a page that wants to know it is back.
    std::function<void(int)> onSelect;

private:
    // A row, and the page it shows. The pane's own two lists are built from these, so a row and its
    // page cannot come apart -- including the case that makes that worth saying: a footer row counts
    // *after* the rows above it, so a page's index is not the index it was added at.
    struct Row {
        NavItem item;
        Widget *page = nullptr;
        bool footer = false;
    };
    std::vector<Row> rows;
    void SyncRows();
    // The page the `i`-th row that can be chosen shows, or null.
    Widget *PageAt(int i) const;
    // The page that is up, which is what tells a page being chosen from a page being put up for the
    // first time -- only the first of those is arriving from somewhere.
    Widget *up = nullptr;
    // How far the page area has come up, 0 to 1. See `transition`.
    motion::Track arrival{ 1.0f };
};

inline NavigationView::NavigationView() {
    SetLayout(new NavigationLayout());
    content = Widget::Add(new View());
    auto *stack = new StackLayout();
    // No margin of the shell's own: the margin of a page is the page's. A `ScrollView` brings the
    // platform's with its column, and a page that wants none says so.
    stack->padX = 0.0f;
    content->SetLayout(stack);
    pane = Widget::Add(new SideNav());
    // The pane's answer to a row being chosen, wherever the choice came from. The pages follow the
    // row here, and the page is told after: `AddPage` calls `Show` instead, because a page arriving
    // is not a row being chosen.
    pane->onSelect = [this](int i) {
        Show(i);
        if (onSelect) onSelect(i);
    };
}

inline void NavigationView::Paint(const Painter &p) {
    if (!pageSurface || !content) return;
    // Behind the page and not around it: the layer *is* the page area's box, so what a page draws on it
    // starts at the frame's own edge. This node is painted before its children -- see PaintTree -- and
    // the pane is the last of them, so a pane over the page is over this too.
    p.Panel(content->rect, Corners(metric::kRadiusCard, 0.0f, 0.0f, 0.0f), p.pal->layerBg,
            p.pal->cardStroke, edge::kTop | edge::kLeft);
}

inline void NavigationView::Show(int i) {
    Widget *page = PageAt(i);
    // A page arriving is not the same as a page being put up: a window's first page is not arriving
    // from anywhere, and neither is any `Show` that finds the page it was already showing.
    const bool arriving = up != nullptr && page != up;
    up = page;
    for (auto &child : content->children) {
        const bool shown = (child.get() == page);
        // **A page coming back is a page being born.** It is still laid out for the room it had when it
        // was last on screen, and a size is not something a glide can carry -- so left to the glide,
        // the page arrives with its new width drawn where the old one was, and the card whose right
        // edge has to move slides into the room it should already be filling. Unplaced, the arrangement
        // places it exactly, which is what a widget added to the tree gets. See UnplaceSubtree.
        if (shown && !child->visible) micula::UnplaceSubtree(child.get());
        child->visible = shown;
    }
    // And the tree is arranged again, because a hidden subtree is skipped by the arrangement: the page
    // that has just become visible has the rectangles it had when it was last on screen -- or none at
    // all, if it has never been shown, in which case it is a page of zero-sized widgets and nothing of
    // it can be seen. Asked for here rather than left to whoever happens to lay the tree out next.
    //
    // The page arrives as it is put up rather than on the frame after: `Set` before `To`, so that the
    // paint this `Invalidate` asks for draws the page area at nothing -- the value of the track is what
    // it was left at, and carrying it over from the last switch would be a page that starts a quarter
    // of a second late and half drawn.
    if (arriving && transition != Transition::None && Animations()) {
        arrival.Set(0.0f);
        arrival.To(1.0f);
    } else {
        arrival.Set(1.0f);
    }
    content->opacity = arrival.value;
    InvalidateLayout();
    Invalidate();
}

inline Widget *NavigationView::PageAt(int i) const {
    if (i < 0) return nullptr;
    int at = 0;
    for (const Row &r : rows) {
        if (r.item.header) continue;   // a heading is not something that can be chosen
        if (at == i) return r.page;
        at++;
    }
    return nullptr;
}

inline void NavigationView::SyncRows() {
    pane->items.clear();
    pane->footer.clear();
    for (const Row &r : rows) (r.footer ? pane->footer : pane->items).push_back(r.item);
}

inline void NavigationView::Place(const D2D1_RECT_F &box) {
    // What the pane takes is the pane's own answer -- `Reserved`, which is also what its layout answers
    // `Measure` with -- so the room is not consulted, and a shell in the middle of a frame can ask it
    // without having one.
    const float paneW = pane->Reserved();
    // The page's box is what the pane *takes*, which is not always what it draws. A pane that pushes
    // takes the room it is drawn at, so the page follows it while it moves -- the pane is what is
    // moving, and a page gliding toward where the pane is going would be a page seen getting there
    // first. A pane that covers takes the rail and hangs over the page, so the page does not move
    // when a pane opens over it -- a page pushed around by a pointer that is only passing through
    // would be worse than one covered.
    //
    // A pane that has not ticked yet has not started moving either: it comes up at the width its state
    // asks for, so that is what the page is laid out around -- and a window's first arrangement runs
    // before its first tick, and `--dump` has no ticks at all.
    const bool pushes = pane->Pushes();
    const float taken = pushes ? (pane->primed ? pane->Width() : paneW) : paneW;
    content->rect = { box.left + taken, box.top, box.right, box.bottom };
    pane->rect = { box.left, box.top, box.left + paneW, box.bottom };
    // A pushing pane's page follows it exactly, frame by frame, and so does the pane's own rectangle:
    // the width the pane animates is *inside* its box, and a box gliding toward the width it will
    // have is a page and a pane that agree a frame late. With the pane covering, the page's box does
    // not depend on the animation at all, and the glide is left to carry whatever else moved.
    if (pushes) {
        content->drawn = content->rect;
        content->placed = true;
    }
    pane->drawn = pane->rect;
    pane->placed = true;
}

inline void NavigationView::Tick(float dt) {
    // The pane's own tick is what moves its width, and this frame's arrangement has already run by the
    // time this does. Two things follow, and they are the same reason:
    //
    //   - the two boxes are placed here, because a box a frame behind the pane's edge is a box the pane
    //     is drawn over -- and a pane that pushes has no opaque surface of its own to hide behind;
    //   - the page area's contents are arranged again here with them. **The page's box moving is not
    //     the page's contents moving**: a card that fills the room it is in is a card whose width
    //     changed with the room, and a layout is arranged in the arrangement. Left to the frame's own,
    //     the page is laid out for the width the pane had a frame ago -- a margin that breathes while
    //     the pane moves, and contents that never catch up at all when nothing else marks the tree
    //     dirty: after the pane has retracted, a page still laid out for the room it had while it was
    //     open, which is a row of cards stopping two hundred DIPs short of the page's corner until
    //     something happens to scroll.
    //
    // It is the shell that does this rather than the pane, because the shell is the one that knows its
    // content follows another widget's animation. The pane goes on being a control that animates
    // itself, so a page that arranges one by hand gets a pane that works without knowing any of this.
    Widget::Tick(dt);
    const D2D1_RECT_F was = content->rect;
    Place({ 0.0f, 0.0f, micula::Width(rect), Height(rect) });
    // Only where the box moved: this runs every frame the shell ticks, which is every frame of any
    // animation anywhere in the window, and a page standing still is arranged once.
    if (!SameRect(was, content->rect)) {
        if (Surface *w = surface()) {
            micula::ArrangeSubtree(content, w->fonts);
            // **And placed, not glided.** A widget whose rectangle has moved is drawn on its way there,
            // which is what makes a card step down when something above it opens -- but a page
            // following a *moving* pane is not that. The glide closes a fraction of the gap per frame,
            // so a target that keeps moving leaves the page permanently several frames behind it, and
            // the page catching up at the end of the animation is a bounce rather than a move. The
            // page tracks the pane exactly, which is what it did before there was a tree at all: the
            // old page moved its cards by hand, one frame at a time, and glided none of them.
            micula::PlaceSubtree(content);
        }
    }
    // And the page arriving, which is the page area at one opacity -- the area and not the page, so
    // that the surface under it stays where it is. See `transition`.
    if (arrival.Moving()) arrival.Step(dt, motion::kNormal);
    content->opacity = arrival.value;
    // And the page on its way up, which only `Entrance` asks for -- the distance is the transition, and
    // the fade is the same track either way. `drawn` is the one field both parts want, so nothing else
    // may be gliding the page while this runs -- which is why a page whose area is moving is *placed*
    // rather than glided above: the two would be writing this line against each other, and the glide
    // losing that is a page settling into place a few frames after its own arrival has finished.
    if (up) {
        up->drawn = up->rect;
        if (transition == Transition::Entrance && arrival.value < 1.0f)
            up->drawn.top += (1.0f - arrival.value) * kPageRise;
        // `placed` as well, because a page that is not placed is drawn where it was *arranged* and
        // would rise by nothing at all.
        up->placed = true;
    }
}

inline Want NavigationLayout::Measure(const Room &room) const {
    // The shell is as big as the window it is in. What the pane wants is the pane's own business, and
    // it is the arrangement below that acts on the answer.
    (void)room;
    return Want(Axis::Fill(), Axis::Fill());
}

inline void NavigationLayout::Arrange(const Room &room, const D2D1_RECT_F &box) {
    (void)room;
    static_cast<NavigationView *>(host_)->Place(box);
}

}  // namespace micula
