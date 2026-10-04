#pragma once

// A row: the children side by side, each as wide as it asked to be, sharing what is left if it asked
// for that. A row of buttons, a control and the line that reads its value, anything where the order
// matters and the widths do not.
//
//     slot->SetLayout(new RowLayout());
//     slot->Add(Add(new Button(L"Cancel", ...)));
//     slot->Add(Add(new Button(L"OK", ButtonStyle::Accent, ...)));

#include "widget.h"

namespace micula {

struct RowLayout : Layout {
    // Where a child sits when it is shorter than the row.
    enum class Align { Top, Middle, Bottom, Stretch };

    float pad = 0.0f;
    Align align = Align::Middle;   // a row of controls is centred: it is what a card's slot is
    bool gaps = true;              // spec.rowGap between the children
    // **A width of its own, for a row that is not to be measured from what is in it.** Zero is the
    // honest answer -- as wide as its children make it, or the room it is given when one of them
    // asked to fill -- and it is what every row wants; `Measure` asks the children and answers.
    // Anything else pins the row, and so whatever it holds, to that width, measured against the
    // box rather than against the children.
    //
    // It is for a control whose width a layout would otherwise decide: a drop-down asks to fill,
    // because in a settings row the card's slot is what a control's width is, and a card with no
    // words on it has no slot to be measured against -- there the whole card is what fill means,
    // and a page that wants the platform's control width at the card's right edge puts the control
    // in a `View` with a row like this one inside it.
    float width = 0.0f;

    Want Measure(const Room &room) const override;
    void Arrange(const Room &room, const D2D1_RECT_F &box) override;
    D2D1_RECT_F ContentBox(const D2D1_RECT_F &box) const override {
        return { box.left + pad, box.top + pad, box.right - pad, box.bottom - pad };
    }
};

// The children side by side. A child that asked to fill takes what the others left, and the row
// itself asks to fill across when one of them does -- so a row of two buttons hugs them, and a row
// with a filling field in it reaches the edge. A row with a `width` of its own is that width and
// answers nothing else: what is in it is measured against the box instead.
inline Want RowLayout::Measure(const Room &room) const {
    const float pinned = width;
    Room inner = room;
    inner.height = (std::max)(0.0f, room.height - 2 * pad);
    if (pinned > 0.0f) inner.width = (std::max)(0.0f, pinned - 2 * pad);

    float wanted = 2 * pad, height = 0.0f;
    bool fillW = false, first = true;
    for (const auto &child : host_->children) {
        if (!child->visible || child->AsLayer()) continue;   // a layer is the tree's, not the row's
        const Want want = child->Measure(inner);
        if (gaps && !first) wanted += spec.rowGap;
        first = false;
        if (want.w.how == Sizing::Fill) fillW = true;
        else wanted += want.w.size;
        height = (std::max)(height, want.h.size);
    }

    Want out;
    out.w = pinned > 0.0f ? Axis::Fixed(pinned)
                          : (fillW ? Axis::Fill() : Axis::Content(wanted));
    out.h = Axis::Content(height + 2 * pad);
    return out;
}

inline void RowLayout::Arrange(const Room &room, const D2D1_RECT_F &box) {
    Room inner = room;
    inner.width = (std::max)(0.0f, box.right - box.left - 2 * pad);
    inner.height = (std::max)(0.0f, box.bottom - box.top - 2 * pad);

    // Two passes for the same reason the column has them: what a filling child gets depends on what
    // every other child asked for, and on nothing else.
    float used = 0.0f;
    float filling = 0.0f;
    int count = 0;
    for (const auto &child : host_->children) {
        if (!child->visible || child->AsLayer()) continue;
        if (gaps && count > 0) used += spec.rowGap;
        count++;
        const Want want = child->Measure(inner);
        if (want.w.how == Sizing::Fill) filling += 1.0f;
        else used += want.w.size;
    }
    const float share = filling > 0.0f ? (std::max)(0.0f, inner.width - used) / filling : 0.0f;

    const float top = box.top + pad;
    float x = box.left + pad;
    int i = 0;
    for (auto &child : host_->children) {
        Widget *w = child.get();
        if (!w->visible || w->AsLayer()) continue;
        if (gaps && i > 0) x += spec.rowGap;
        i++;

        const Want want = w->Measure(inner);
        // Not `width`: the row has one of its own now, and a local by that name hides it -- see the
        // member above. This is the child's.
        const float childW = want.w.how == Sizing::Fill ? share : want.w.size;
        float height = inner.height;
        float y = top;
        if (align != Align::Stretch && want.h.how != Sizing::Fill) {
            height = want.h.size;
            if (align == Align::Middle) y = top + (inner.height - height) * 0.5f;
            else if (align == Align::Bottom) y = top + inner.height - height;
        }
        w->rect = { x, y, x + childW, y + height };
        x += childW;
    }
}

}  // namespace micula
