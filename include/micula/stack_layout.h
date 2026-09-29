#pragma once

// A column: the children one after another, down the page. It is the shape a settings page is made
// of, and the reason a page can say "this, then this, then this" and get the platform's spacing.
//
//     page->SetLayout(new StackLayout());
//     page->Add(new Heading(L"Choices"));
//     page->Add(new CheckBox(L"Show a notification when done", notify, onChange));
//     page->Add(new Card(...));
//
// Every number it uses is the spec's, so a page that says nothing about spacing gets the spacing
// Windows Settings has.

#include "widget.h"

namespace micula {

struct StackLayout : Layout {
    // Where a child sits when it is narrower than the column.
    enum class Align { Start, Center, Stretch };

    float pad = 0.0f;               // inside the host, all four sides
    Align align = Align::Stretch;   // the cross axis: a card and a field want the whole width
    bool gaps = true;               // put spec.gap between the children

    Want Measure(const Room &room) const override;
    void Arrange(const Room &room, const D2D1_RECT_F &box) override;
    D2D1_RECT_F ContentBox(const D2D1_RECT_F &box) const override {
        return { box.left + pad, box.top + pad, box.right - pad, box.bottom - pad };
    }
};

// The children as they asked to be, stacked. A child that asked to fill on the way down contributes
// its gap and nothing else: it lives on what the host has left over, which is only known when the
// host is arranged, and its presence is what makes the column itself ask to fill.
inline Want StackLayout::Measure(const Room &room) const {
    Room inner = room;
    inner.width = (std::max)(0.0f, room.width - 2 * pad);

    float width = 0.0f, height = 2 * pad;
    bool fillW = false, fillH = false, first = true;
    for (const auto &child : host_->children) {
        if (!child->visible) continue;
        const Want want = child->Measure(inner);
        if (gaps && !first) height += spec.gap;
        first = false;
        if (want.h.how == Sizing::Fill) fillH = true;
        else height += want.h.size;
        if (want.w.how == Sizing::Fill) fillW = true;
        else width = (std::max)(width, want.w.size);
    }

    Want out;
    out.w = fillW ? Axis::Fill() : Axis::Content(width + 2 * pad);
    out.h = fillH ? Axis::Fill() : Axis::Content(height);
    return out;
}

inline void StackLayout::Arrange(const Room &room, const D2D1_RECT_F &box) {
    Room inner = room;
    inner.width = (std::max)(0.0f, box.right - box.left - 2 * pad);
    inner.height = (std::max)(0.0f, box.bottom - box.top - 2 * pad);

    // Two passes, because the children that fill need to know what the others left. Measuring is
    // cheap -- a label's text is a cached measurement -- and it is what lets a wrapping line size
    // itself against the width it is really being given rather than the one it was offered.
    float used = 0.0f;
    float filling = 0.0f;
    int count = 0;
    for (const auto &child : host_->children) {
        if (!child->visible) continue;
        if (gaps && count > 0) used += spec.gap;
        count++;
        const Want want = child->Measure(inner);
        if (want.h.how == Sizing::Fill) filling += 1.0f;
        else used += want.h.size;
    }
    const float share = filling > 0.0f ? (std::max)(0.0f, inner.height - used) / filling : 0.0f;

    const float left = box.left + pad;
    float y = box.top + pad;
    int i = 0;
    for (auto &child : host_->children) {
        Widget *w = child.get();
        if (!w->visible) continue;
        if (gaps && i > 0) y += spec.gap;
        i++;

        const Want want = w->Measure(inner);
        float h = want.h.how == Sizing::Fill ? share : want.h.size;
        float width = want.w.size;
        float x = left;
        if (align == Align::Stretch || want.w.how == Sizing::Fill) {
            width = inner.width;
            x = left;
        }
        else if (align == Align::Center) {
            x = left + (inner.width - width) * 0.5f;
        }
        w->rect = { x, y, x + width, y + h };
        y += h;
    }
}

}  // namespace micula
