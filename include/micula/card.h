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
    //
    // **The card's own line is what the control is called.** A card has the words and the control does
    // not -- "Keep free space above" above a slider -- and a screen reader that read the slider as an
    // unnamed number would be reading the half of the row that means nothing on its own. Put on the
    // control unless it answers a name of its own: a button's label is its name, and the line beside it
    // is not.
    template <typename T> T *Set(T *w) {
        content = w;
        // An empty label is no name at all, which is what a bare switch has: `ToggleSwitch(L"", on)`.
        const wchar_t *named = w->AccessibleLabel();
        if (!named || !*named) w->accessibleName = text;
        return Add(w);
    }

    // Where this card's own words go, in the card's own space.
    //
    // Paint draws into these and a program can ask for them, from the fonts alone -- the width of a
    // string is a measurement, not something that needs a device context. That matters because a
    // card's title, the line under it and the value beside the control are the only geometry on a
    // page with no widget behind it: everything else can be read out of the tree, and these three
    // could only be worked out by reading Paint.
    struct Text {
        D2D1_RECT_F title = {};    // the card's own line, and the room it may use
        D2D1_RECT_F under = {};    // the line below it, when there is one
        D2D1_RECT_F value = {};    // the value, when there is one
        std::wstring said;         // what `value` answered, kept so it is asked once
        bool hasUnder = false;
        bool hasValue = false;
    };

    Text Wording(const Fonts &fonts, const Spec &spec) const {
        Text t;
        const float line = spec.labelH;
        const float mid = (rect.top + rect.bottom) / 2.0f;
        const float left = rect.left + spec.cardPad + (icon.empty() ? 0.0f : 32.0f);

        // Everything the words may not run into comes off the right: the value first, then the
        // control. Both are known only after the arrangement, which is why this is not part of the
        // layout.
        //
        // **The control's rectangle is in the card's space and this is in the card's parent's**, so
        // the card's own origin goes on before the two are compared. Leaving it off is not a small
        // error: `cardGap` was 16 and measured 40, because the difference is the card's own left
        // edge -- 24 DIP at a page margin of 24 -- and nothing else on the page is drawn from a
        // control's rectangle this way.
        float right = content ? rect.left + content->rect.left - spec.cardGap
                              : rect.right - spec.cardPad;
        if (value) {
            t.said = value();
            if (!t.said.empty()) {
                const float vw = fonts.Measure(fonts.body, t.said);
                t.value = { right - vw, rect.top, right, rect.bottom };
                t.hasValue = true;
                right -= vw + spec.cardGap;
            }
        }
        if (right < left) right = left;

        t.hasUnder = !detail.empty();
        if (t.hasUnder) {
            t.title = { left, mid - line, right, mid };
            t.under = { left, mid, right, mid + line };
        } else {
            // Nothing under the title, so it is centred in the card rather than sitting up.
            t.title = { left, rect.top, right, rect.bottom };
        }
        return t;
    }

    void Paint(const Painter &p) override {
        const Palette &c = *p.pal;
        const Spec &sp = layout->spec;
        p.FillRound(rect, metric::kRadiusControl, c.cardBg);
        p.StrokeRound(rect, metric::kRadiusControl, c.cardStroke);

        if (!icon.empty())
            p.Text(icon, { rect.left + sp.cardPad, rect.top, rect.left + sp.cardPad + 20,
                           rect.bottom }, p.font->icon, c.textPrimary);

        const Text t = Wording(*p.font, sp);
        if (t.hasValue) {
            // The second step of the ramp rather than the first. What the control is set to is worth
            // reading, but it is not what the row is *about* -- the title is -- and at the top
            // weight it competes with the title for the eye, which is the one thing a settings row
            // must not do.
            p.Text(t.said, t.value, p.font->body, c.textSecondary);
        }
        p.Text(text, t.title, p.font->body, c.textPrimary);
        if (t.hasUnder)
            p.Text(detail, t.under, p.font->caption, c.textSecondary);
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
