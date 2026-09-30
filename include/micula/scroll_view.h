#pragma once

// A box whose children do not fit is scrolled by the box.
//
//     auto *page = root->Add(new ScrollView());
//     page->Add(new Heading(L"Capture"));    // goes into the column, not into the viewport
//     page->Add(new Card(...));              // and as many more as the page has
//
// The whole of it is one idea: **scrolling is arranging the children somewhere else.** The column is
// laid out as tall as it comes out and moved up by however far the view is scrolled, so what changes
// when a wheel turns is a number, and everything downstream follows from the column being where it
// would be if the page were that much taller:
//
//   - the glide that already exists animates it, because a child whose rectangle has moved is a child
//     whose `drawn` rectangle is on its way there -- there is no second animation for scrolling and no
//     scroll offset for anything to remember;
//   - the hit test follows it for the same reason, since it reaches a widget where it *looks*;
//   - the clip is the view, and it is the only part of this that is about the container rather than
//     about what is in it.
//
// The tree underneath it is two children rather than one:
//
//     ScrollView            the viewport: clips, scrolls, and owns the bar
//       View                the column a page adds to, and where the page's margin lives
//       ScrollBar           the bar, drawn *over* the column
//
// The column is a child rather than the view itself for one reason: children are painted in order, so
// the bar has to be last, and it cannot be last if the thing a page keeps adding to is the view.

#include "scroll_bar.h"
#include "stack_layout.h"

namespace micula {

struct ScrollView;

// The viewport's own layout: the column, where it is scrolled to, and the bar down the right-hand
// edge. Not a StackLayout -- the column inside is one, and asking a stack to arrange a stack is how a
// page's margin ends up applied twice.
struct ScrollLayout : Layout {
    Want Measure(const Room &room) const override;
    void Arrange(const Room &room, const D2D1_RECT_F &box) override;
    D2D1_RECT_F ContentBox(const D2D1_RECT_F &box) const override { return box; }
};

struct ScrollView : View {
    ScrollView();

    // How far the column is scrolled, in DIPs from the top: the target, not where it is drawn. The
    // glide is what moves the column, and this is what it moves it toward.
    float scroll = 0.0f;
    // How tall the column came out. Written by the view's layout on every arrangement, read by the
    // wheel to know when to stop.
    float extent = 0.0f;

    // The column a page adds to. `Add` below is the whole of the interface to it.
    View *content = nullptr;
    // The bar. A child of the view rather than a thing the view draws, so the tree paints it,
    // hit-tests it and ticks it -- which is the work the old model handed to whoever owned one.
    ScrollBar *bar = nullptr;

    // A page adds to the column, not to the viewport. Written out rather than inherited because the
    // order of the view's children is the order they are painted in, and the bar has to stay behind
    // everything a page adds.
    template <typename T> T *Add(T *w) { return content->Add(w); }

    // A viewport to a client: how far down the page it is, how much of the page it is showing, and
    // whether there is anything to scroll at all. Both percentages are worked out here because this is
    // the only side that knows both halves of them -- the offset and what it is an offset *in*.
    bool AccessibleScroll(float &percent, float &view, bool &canScroll) const override {
        const float most = ScrollMax();
        canScroll = most > 0.0f;
        percent = canScroll ? std::clamp(scroll / most, 0.0f, 1.0f) : 0.0f;
        view = extent > 0.0f ? std::clamp(Height(rect) / extent, 0.0f, 1.0f) : 1.0f;
        return true;
    }
    // A region that holds things and scrolls them, which is the type UIA has for it: not a list, and
    // not the page -- the page is what a client walks into this to read.
    int AccessibleType() const override { return UIA_PaneControlTypeId; }
    bool AccessibleWritable() const override { return true; }
    // Where a client asks to be taken: a percentage of what there is to scroll, which is what the
    // scroll bar and a dragged thumb do too. Glided rather than snapped, for the same reason they are.
    bool AccessibleSetScroll(float percent) override {
        if (percent < 0.0f || percent > 1.0f) return false;
        ScrollTo(percent * ScrollMax(), true);
        return true;
    }

    // A view is a window onto its children: they are drawn where they are inside it and not
    // otherwise. See Widget::Clips.
    bool Clips() const override { return true; }

    // The viewport takes the room it is given rather than asking for the height of what is in it,
    // which is the difference between a viewport and a page.
    //
    // `shrink` is the other answer, and it is what a panel hanging off a control wants: **as big as
    // what is in it, and no bigger than the room it has.** A drop-down's list is four things, not a
    // page, and a panel that took the whole window for four rows would be a panel three quarters full
    // of nothing. The clamp needs the room, which a measurement does not always have: it arrives as
    // `Room::height`, which is zero while the parent does not know its own height yet -- and zero is
    // answered with the content's own height, which is the honest answer at that moment.
    //
    // Across, it is the wider of the two: a list of long words is a wider panel than the control it
    // hangs off, and the flyout hands it the control's width to start from.
    //
    // The bar needs nothing said about it: a list that fits has no overflow, and `ScrollBar` puts
    // itself away.
    bool shrink = false;
    micula::Want Measure(const Room &room) const override {
        if (!shrink || !content) return micula::Want(Axis::Fill(), Axis::Fill());
        const micula::Want want = content->Measure(room);
        const float w = (std::max)(want.w.how == Sizing::Fill ? 0.0f : want.w.size, room.width);
        const float h = want.h.how == Sizing::Fill ? room.height : want.h.size;
        return micula::Want(Axis::Content(w),
                            Axis::Content(room.height > 0.0f ? (std::min)(h, room.height) : h));
    }

    // How much of the column is out of sight. Zero when everything fits, which is the state a short
    // page is in and the answer to "is there anything to scroll".
    float ScrollMax() const { return (std::max)(0.0f, extent - Height(rect)); }

    // Go to `to`, inside what there is. `glide` is false for a dragged thumb only: a thumb under the
    // pointer has to stay under it rather than catch up with it, so that one move is not animated.
    void ScrollTo(float to, bool glide) {
        // Anything that moves the page brings the bar out, the way it does everywhere else in
        // Windows: the bar is the feedback for the move, not only for the pointer. Awake before the
        // clamp rather than after, so that pushing against the end of the page still shows where the
        // end is instead of looking like nothing happened.
        if (bar) { bar->Wake(); bar->Poll(); }
        const float at = std::clamp(to, 0.0f, ScrollMax());
        if (at == scroll) return;
        scroll = at;
        instant = !glide;
        InvalidateLayout();
    }

    // Move it, in DIPs. The arrangement follows on the next frame -- this marks the tree dirty and
    // the window's own pass does the rest, so ten wheel notches inside one frame are one arrangement
    // and one move.
    void ScrollBy(float dip) { ScrollTo(scroll + dip, true); }

    // A wheel turned over this, or over anything in it -- the window offers the notch to the widget
    // under the pointer and then to each thing it is inside of, which is what brings it here. One
    // notch is the system's own "lines per notch" times a control's height: a page whose rows are 32
    // DIP tall scrolls a row and a little for each, which is what the same setting means everywhere
    // else in Windows.
    bool OnWheel(float, float, float notches) override {
        if (notches == 0.0f || ScrollMax() <= 0.0f) return false;
        const float lines = SystemWheelLines();
        const float step = lines > 0.0f ? lines * layout->spec.controlH : Height(rect);
        ScrollBy(-notches * step);
        return true;
    }

    // Set for the one arrangement after a move that is not to be animated, and cleared by the layout
    // once the column has been placed at it.
    bool instant = false;
};

inline ScrollView::ScrollView() {
    // The viewport's own layout: the column where it is scrolled to, and the bar at the edge.
    SetLayout(new ScrollLayout());
    // Qualified, because this class hides `Add`: these two are the view's own children rather than
    // things a page is putting in the column.
    content = Widget::Add(new View());
    content->SetLayout(new StackLayout());
    bar = Widget::Add(new ScrollBar([this](float to, bool glide) { ScrollTo(to, glide); }));
    // Nothing to scroll yet, and a bar that has never been arranged would otherwise flash at the
    // right-hand edge before the first arrangement. See ScrollBar::Poll, which puts it away itself.
    bar->visible = false;
}

inline Want ScrollLayout::Measure(const Room &room) const {
    // The viewport's own answer, not the column's: how tall the page is, is the column's business and
    // the layout asks it directly in Arrange, where the answer is needed next to the box it is for.
    (void)room;
    return Want(Axis::Fill(), Axis::Fill());
}

inline void ScrollLayout::Arrange(const Room &room, const D2D1_RECT_F &box) {
    ScrollView *view = static_cast<ScrollView *>(host_);
    Room inner = room;
    inner.width = (std::max)(0.0f, box.right - box.left);

    const Want want = view->content->Measure(inner);
    const float viewport = box.bottom - box.top;
    // A column that asked to fill has no height of its own to report -- it is saying it is as tall as
    // its box, and for a scrolling page that means the page fits and there is nothing to scroll.
    const float tall = want.h.how == Sizing::Fill ? viewport : want.h.size;
    view->extent = tall;

    // A window that shrank, or a page that got shorter, leaves the view past the end of it. The
    // offset is pulled back rather than treated as a fault: the arrangement is the only place that
    // knows both halves of it.
    if (view->scroll > view->ScrollMax()) view->scroll = view->ScrollMax();
    if (view->scroll < 0.0f) view->scroll = 0.0f;

    // The column, in a box as tall as it is and moved up by the offset. This is the whole of
    // scrolling: from here down, nothing knows it happened.
    view->content->rect = { box.left, box.top - view->scroll,
                            box.right, box.top - view->scroll + tall };
    if (view->instant) {
        // A move that is not to be animated -- a dragged thumb -- is placed where it is going, so
        // that there is nothing left for the glide to do. Every other move is the glide's.
        view->content->drawn = view->content->rect;
        view->content->placed = true;
        for (auto &child : view->content->children) {
            child->drawn = child->rect;
            child->placed = true;
        }
        view->instant = false;
    }

    // Where the column is *drawn*, which is what the bar's thumb follows: the offset, minus however
    // much of the move the glide still owes. One child is enough to ask -- they all move together --
    // and none at all means there is nothing to have moved.
    float lag = 0.0f;
    for (const auto &child : view->content->children) {
        if (!child->visible || !child->placed) continue;
        lag = child->drawn.top - child->rect.top;
        break;
    }

    ScrollBar *bar = view->bar;
    // WinUI's bar overlays the page at its right-hand edge rather than taking a column of its own,
    // which is what a 12-DIP bar with a 2-DIP margin is.
    bar->rect = { box.right - 2.0f - ScrollBar::kSize, box.top, box.right - 2.0f, box.bottom };
    // The area the bar watches *outside itself*: the whole page, so that a pointer moving over the
    // page brings the bar out. Not a hit-test area -- a click out there is a click on the page.
    bar->area = { box.left - bar->rect.left, box.top - bar->rect.top,
                  box.right - bar->rect.left, box.bottom - bar->rect.top };
    bar->viewport = viewport;
    bar->extent = tall;
    bar->value = view->scroll;
    bar->drawn = view->scroll - lag;
    bar->visible = tall > viewport;
    // The arrangement is the only moment either of them changes, so it is where the bar is told to
    // look at them. It also has to be here rather than left to the bar's own clock: a bar that is
    // out is being tweened, and a bar that is not yet out is not animating yet -- this is what says
    // there is something to come out for.
    bar->Poll();
}

}  // namespace micula
