#pragma once

// A box whose children do not fit is scrolled by the box.
//
//     auto *page = root->Add(new ScrollView());
//     page->Add(new Heading(L"Capture"));
//     page->Add(new Card(...));      // and as many more as the page has
//
// The whole of it is one idea: **scrolling is arranging the children somewhere else.** They are laid
// out in a box as tall as they come out and moved up by however far the view is scrolled, so what
// changes when a wheel turns is a number, and everything downstream follows from the children being
// where they would be if the page were that much taller:
//
//   - the glide that already exists animates it, because a child whose rectangle has moved is a
//     child whose `drawn` rectangle is on its way there -- there is no second animation for scrolling
//     and no scroll offset for anything to remember;
//   - the hit test follows it for the same reason, since it reaches a widget where it *looks*;
//   - the clip is this box, and only the clip is about the container rather than the children.
//
// What is *not* here yet: a scroll bar. `ScrollBar` is one of the four controls still written against
// the flat widget list, and the bar this wants is that control ported, drawn by the view itself at
// its right-hand edge. The wheel and the keyboard are the whole of how it moves until then.

#include "stack_layout.h"

namespace micula {

struct ScrollView;

// The column inside it: a StackLayout whose box is not the host's, which is the whole of what makes
// it scroll. Everything else about it -- the margin, the gaps, the alignment -- is the stack's, and
// deliberately: a scrolling page and a page that fits are the same page.
struct ScrollLayout : StackLayout {
    void Arrange(const Room &room, const D2D1_RECT_F &box) override;
    // The box a page is scrolled through is the host itself; the box its *children* are arranged in
    // is the one `Arrange` works out.
    D2D1_RECT_F ContentBox(const D2D1_RECT_F &box) const override { return box; }
};

struct ScrollView : View {
    ScrollView() { SetLayout(new ScrollLayout()); }

    // How far the content is scrolled, in DIPs from the top. The target, not where it is drawn: the
    // glide is what moves the children, and this is what it moves them toward.
    float scroll = 0.0f;
    // How tall the children came out. Written by the layout on every arrangement, read by the wheel
    // to know when to stop.
    float extent = 0.0f;

    // A view is a window onto its children: they are drawn where they are inside it and not
    // otherwise. See Widget::Clips.
    bool Clips() const override { return true; }

    // It takes the room it is given rather than asking for the height of what is in it, which is the
    // difference between a viewport and a page.
    micula::Want Measure(const Room &) const override {
        return micula::Want(Axis::Fill(), Axis::Fill());
    }

    // How much of the content is out of sight. Zero when everything fits, which is the state a
    // viewer of a short page is in and the answer to "is there anything to scroll".
    float ScrollMax() const { return (std::max)(0.0f, extent - Height(rect)); }

    // Move it, in DIPs, and stay inside what there is. The arrangement follows on the next frame --
    // this marks the tree dirty and the window's own pass does the rest, so ten wheel notches inside
    // one frame are one arrangement and one move.
    void ScrollBy(float dip) {
        const float to = std::clamp(scroll + dip, 0.0f, ScrollMax());
        if (to == scroll) return;
        scroll = to;
        InvalidateLayout();
    }

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
};

inline void ScrollLayout::Arrange(const Room &room, const D2D1_RECT_F &box) {
    ScrollView *view = static_cast<ScrollView *>(host_);
    const float pad = PadX();
    Room inner = room;
    inner.width = (std::max)(0.0f, box.right - box.left - 2 * pad);

    // How tall the column comes out. Summed here rather than asked of StackLayout::Measure, which
    // answers the same question for a box of a *given* height: this one has no bottom, and a child
    // that asked to fill the way down has nothing to fill -- its presence cannot make the page
    // shorter, so it contributes nothing to it.
    float tall = 2 * padY;
    bool first = true;
    for (const auto &child : host_->children) {
        if (!child->visible) continue;
        if (gaps && !first) tall += spec.gap;
        first = false;
        const Want want = child->Measure(inner);
        if (want.h.how != Sizing::Fill) tall += want.h.size;
    }
    view->extent = tall;

    // A window that shrank, or content that got shorter, leaves the view past the end of it. The
    // offset is pulled back rather than treated as a fault: this is the same number the wheel clamps,
    // and the arrangement is the only place that knows both halves of it.
    const float most = (std::max)(0.0f, tall - (box.bottom - box.top));
    if (view->scroll > most) view->scroll = most;
    if (view->scroll < 0.0f) view->scroll = 0.0f;

    // And the column, arranged in a box as tall as it is and moved up by the offset. This is the
    // whole of scrolling: from here down, nothing knows it happened.
    const float top = box.top - view->scroll;
    StackLayout::Arrange(room, { box.left, top, box.right, top + tall });
}

}  // namespace micula
