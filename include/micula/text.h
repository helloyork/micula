#pragma once

// The page's own words, as widgets.
//
// Headings and labels used to be drawn by the page, from rectangles its own layout had worked out:
// a page kept an array of "where the headings are" and painted them after everything else, which
// meant the text could not be arranged, could not scroll on its own, and was invisible to a screen
// reader. A heading is a thing on the page like a button is, and it measures and paints like one.

#include "window.h"   // `Painter`, which a line of text is drawn through

namespace micula {

// Which of the window's formats a line is drawn in. Not every one of the ten: these are the four a
// page has words in, and a page that wants the title size says so by being the page title.
enum class TextRole { Body, Strong, Caption, Subtitle };

inline IDWriteTextFormat *FormatOf(const Fonts &f, TextRole role) {
    switch (role) {
    case TextRole::Strong:   return f.bodyStrong;
    case TextRole::Caption:  return f.caption;
    case TextRole::Subtitle: return f.subtitle;
    default:                 return f.body;
    }
}

// One line of text, or one paragraph of it.
//
//     page->Add(new Label(L"Nothing here yet.", TextRole::Caption));
//     page->Add(new Label(explanation, TextRole::Body))->wrap = true;
struct Label : Widget {
    std::wstring text;
    TextRole role = TextRole::Body;
    bool wrap = false;
    // The second colour: what a page says under a control, or a line of explanation. Set rather than
    // a role, because a caption can be primary-coloured and a body line can be secondary.
    bool secondary = false;

    Label(std::wstring s, TextRole r = TextRole::Body)
        : text(std::move(s)), role(r) {}

    micula::Want Measure(const Room &room) const override {
        const Fonts *f = room.fonts;
        IDWriteTextFormat *fmt = FormatOf(*f, role);
        if (wrap)
            return micula::Want(Axis::Fill(),
                                Axis::Content(f->WrappedHeight(fmt, text, room.width)));
        return micula::Want(Axis::Content(f->Measure(fmt, text)), Axis::Fixed(room.spec->labelH));
    }

    void Paint(const Painter &p) override {
        const D2D1_COLOR_F fg = secondary ? p.pal->textSecondary : p.pal->textPrimary;
        IDWriteTextFormat *fmt = FormatOf(*p.font, role);
        if (wrap) p.TextWrapped(text, rect, fmt, fg, false, true);
        else      p.Text(text, rect, fmt, fg);
    }

    const wchar_t *AccessibleName() const override { return text.empty() ? nullptr : text.c_str(); }
    int AccessibleType() const override { return UIA_TextControlTypeId; }

    // **Words are not something the pointer can act on.** A line of text is a widget because the
    // arrangement and the screen reader both have to reach it, and the pointer is the one thing that
    // does not: a click on a label is a click on the page, so it is the page that takes the capture and
    // the page that the focus rule hears about. What is behind it is not covered up either -- see
    // Widget::Covers -- so a label over something is not in that thing's way.
    bool Covers(float, float) const override { return false; }
};

// A section heading: the line a group of cards is under.
//
// It carries the band above itself as part of its own height, which is what makes a page read like
// Windows Settings without the page knowing how far a heading sits below what came before it:
//
//     page->Add(new Card(...));               // rows, gap 4 apart
//     page->Add(new Heading(L"Advanced"));    // 24 above it, because a heading says so
//     page->Add(new Card(...));
//
// Nothing is drawn in the band: a heading is its own margin. The two numbers come from the spec and
// are fields because this is the one widget whose spacing belongs to it rather than to the layout
// around it -- a page that wants its headings tighter says so on the heading.
struct Heading : Widget {
    std::wstring text;
    float band = Spec{}.headingTop;   // above the line, empty
    float line = Spec{}.headingH;     // the line's own height

    explicit Heading(std::wstring s) : text(std::move(s)) {}

    micula::Want Measure(const Room &) const override {
        return micula::Want(Axis::Fill(), Axis::Content(band + line));
    }

    void Paint(const Painter &p) override {
        // The strong body format, not the subtitle: a heading names the group under it rather than
        // being a line of its own, and at the subtitle size it outweighed the card titles it was
        // grouping -- twice the measured weight of those words reads as the heading of a *page*,
        // which is what `PageTitle` is.
        p.Text(text, { rect.left, rect.top + band, rect.right, rect.top + band + line },
               p.font->bodyStrong, p.pal->textPrimary);
    }

    // A heading is its own margin and a line of words: the pointer goes past it the same way it goes
    // past a label. See Label::Covers.
    bool Covers(float, float) const override { return false; }

    const wchar_t *AccessibleName() const override { return text.empty() ? nullptr : text.c_str(); }
    int AccessibleType() const override { return UIA_TextControlTypeId; }
};

// The page's own title: the one line at the top of a page, in the window's title size.
//
// `Heading` groups what is under it; this is what everything on the page is under. They are two
// widgets rather than one with a size on it because they are the two ends of a page's hierarchy and
// each is wrong for the other's job: a page title is a 44 DIP line with 8 above it, a heading is 30
// with 24 above it.
//
//     page->Add(new PageTitle(kPages[0].title));
//     page->Add(new Card(...));                  // rows, gap 4 apart
//     page->Add(new Heading(L"Advanced"));
struct PageTitle : Widget {
    std::wstring text;
    float band = 8.0f;    // above the line, empty
    float line = 44.0f;   // the line's own height

    explicit PageTitle(std::wstring s) : text(std::move(s)) {}

    micula::Want Measure(const Room &) const override {
        return micula::Want(Axis::Fill(), Axis::Content(band + line));
    }

    void Paint(const Painter &p) override {
        p.Text(text, { rect.left, rect.top + band, rect.right, rect.top + band + line }, p.font->title,
               p.pal->textPrimary);
    }

    // Its own margin and a line of words, like a heading. See Label::Covers.
    bool Covers(float, float) const override { return false; }

    const wchar_t *AccessibleName() const override { return text.empty() ? nullptr : text.c_str(); }
    int AccessibleType() const override { return UIA_TextControlTypeId; }
};

}  // namespace micula
