#pragma once

// The card: the platform's control template.
//
// A settings row is not a control, it is a *shape* -- an optional icon, a line of text, an optional
// line under it, sometimes a value, and the control itself on the right -- and every settings page in
// Windows is that shape repeated. It used to be the page's business: a page kept an array of cards,
// worked out four rectangles for each of them and painted them all in PaintPage, which is why two
// pages could disagree about how far a card's text sits from its left edge. It is a widget now:
//
//     auto *card = new Card(L"\u4f7f\u7528\u6355\u83b7\u5668", L"scrcpy \u7684\u7a97\u53e3\u4f1a\u88ab\u6212\u65ad\u3002");
//     card->icon = glyph::kEthernet;
//     auto *t = card->Set(new ToggleSwitch(on));
//     card->value = [this] { return on ? i18n::tr(L"on") : i18n::tr(L"off"); };
//     page->Add(card);
//
// The four shapes a card comes in are all one shape with pieces missing: the icon may be absent, the
// detail may be absent -- and then the text is centred instead of sitting up -- and the control may
// be absent, which is a heading-like row of text. Nothing here is a special case for any of them.

#include "widget.h"

#include <algorithm>
#include <functional>
#include <string>

namespace micula {

struct Card;

// The card's own layout: the children along the right-hand side, right-aligned, each as wide as it
// asked to be, and a child that asked to fill taking what is left of the row.
//
// It has to know how much room the card's own text costs, because the children are placed around it
// and no measurement of the children can answer that. That question is `Reserved` below, and it is
// the only reason this layout knows what it is arranging.
struct CardLayout : Layout {
    Want Measure(const Room &room) const override;
    void Arrange(const Room &room, const D2D1_RECT_F &box) override;
    D2D1_RECT_F ContentBox(const D2D1_RECT_F &box) const override {
        return { box.left + spec.cardPad, box.top + spec.cardPad,
                 box.right - spec.cardPad, box.bottom - spec.cardPad };
    }
    // What the card's text and icon take off the left of the row, gap included.
    float Reserved(const Room &room) const;
};

struct Card : Widget {
    std::wstring icon;         // optional Segoe Fluent Icons code point, drawn at the left
    std::wstring text;         // the card's own line
    std::wstring detail;       // optional, secondary colour, under the text
    Widget *content = nullptr; // the control the card is built around, on the right
    // What the control reads back as -- "On", "80%", a file name -- drawn to its left. A function
    // rather than a string because it is the control's state that changes, and the page that owns
    // both is the one that knows how to say it. Nothing is drawn when it answers empty.
    std::function<std::wstring()> value;

    Card(std::wstring line, std::wstring under = std::wstring())
        : text(std::move(line)), detail(std::move(under)) {
        SetLayout(new CardLayout());
    }

    // Takes the control, adds it, and hands it back:
    //     auto *t = card->Set(new ToggleSwitch(on));
    template <typename T> T *Set(T *w) {
        content = w;
        return Add(w);
    }

    void Paint(const Painter &p) override {
        const Palette &c = *p.pal;
        const Spec &sp = layout->spec;
        p.FillRound(rect, metric::kRadiusControl, c.cardBg);
        p.StrokeRound(rect, metric::kRadiusControl, c.cardStroke);

        const float line = sp.labelH;
        const float mid = (rect.top + rect.bottom) / 2.0f;
        float left = rect.left + sp.cardPad;
        if (!icon.empty()) {
            p.Text(icon, { left, rect.top, left + 20, rect.bottom }, p.font->icon, c.textPrimary);
            left += 32;
        }

        // Where the text has to stop: the control, and the value beside it. Both come off the right,
        // and both are only known once the arrangement has happened -- which is what Paint runs after.
        float right = content ? content->rect.left - sp.cardGap : rect.right - sp.cardPad;
        if (value) {
            const std::wstring v = value();
            if (!v.empty()) {
                const float vw = p.MeasureWidth(v, p.font->body);
                p.Text(v, { right - vw, rect.top, right, rect.bottom }, p.font->body,
                       c.textPrimary);
                right -= vw + sp.cardGap;
            }
        }
        if (right < left) right = left;

        if (detail.empty()) {
            // Nothing under the text, so it is centred in the card rather than sitting up.
            p.Text(text, { left, rect.top, right, rect.bottom }, p.font->body, c.textPrimary);
        } else {
            p.Text(text, { left, mid - line, right, mid }, p.font->body, c.textPrimary);
            p.Text(detail, { left, mid, right, mid + line }, p.font->caption, c.textSecondary);
        }
    }

    // A row of a page is a group of things: the words, the control, and what the control is set to.
    const wchar_t *AccessibleName() const override { return text.empty() ? nullptr : text.c_str(); }
    int AccessibleType() const override { return UIA_GroupControlTypeId; }
};

inline float CardLayout::Reserved(const Room &room) const {
    const Card *card = static_cast<const Card *>(host_);
    const Fonts *f = room.fonts;
    float w = card->icon.empty() ? 0.0f : 32.0f;
    // The wider of the two lines, not their sum: they are one under the other.
    w += (std::max)(f->Measure(f->body, card->text), f->Measure(f->caption, card->detail));
    return w + spec.cardGap;
}

inline Want CardLayout::Measure(const Room &room) const {
    float tall = 0.0f;
    for (const auto &child : host_->children) {
        if (!child->visible) continue;
        // A child that fills on the way down has no opinion about how tall the card is: it is the
        // card's height that is the question, and that child is along for it.
        const Want want = child->Measure(room);
        if (want.h.how != Sizing::Fill) tall = (std::max)(tall, want.h.size);
    }
    // A card is never narrower than the platform's row, and grows when what is in it is taller than
    // one control -- which is the whole of what a card's height means.
    return Want(Axis::Fill(), Axis::Content((std::max)(spec.cardH, tall + 2 * spec.cardPad)));
}

inline void CardLayout::Arrange(const Room &room, const D2D1_RECT_F &box) {
    const float left = box.left + spec.cardPad + Reserved(room);
    const float right = box.right - spec.cardPad;
    const float mid = (box.top + box.bottom) / 2.0f;

    // Two passes, as everywhere else: what the children that are not filling want, then the room the
    // filling ones share. A field in a card is one of those, and it is the reason the loop is here --
    // and the reason the share is capped at the spec's slot: a control is as wide as the column, not
    // as wide as the card, which is what puts every control on a page in one column down the right.
    float used = 0.0f;
    float filling = 0.0f;
    int count = 0;
    for (const auto &child : host_->children) {
        if (!child->visible) continue;
        if (count++ > 0) used += spec.rowGap;
        const Want want = child->Measure(room);
        if (want.w.how == Sizing::Fill) filling += 1.0f;
        else used += want.w.size;
    }
    float share = 0.0f;
    if (filling > 0.0f) {
        share = (std::max)(0.0f, right - left - used) / filling;
        if (share > spec.cardSlotW) share = spec.cardSlotW;
    }

    float x = right - used - share * filling;
    for (const auto &child : host_->children) {
        Widget *w = child.get();
        if (!w->visible) continue;
        const Want want = w->Measure(room);
        const float cw = want.w.how == Sizing::Fill ? share : want.w.size;
        const float ch = want.h.how == Sizing::Fill ? box.bottom - box.top : want.h.size;
        w->rect = { x, mid - ch / 2, x + cw, mid + ch / 2 };
        x += cw + spec.rowGap;
    }
}

}  // namespace micula
