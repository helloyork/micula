// Micula / dialog.h
//
// A question with buttons on it: `Layer`, in the clothes a dialog is recognised by -- a title, a
// body, a footer of buttons on a surface of its own, a border, a shadow and an answer.
//
// Everything `Layer` does still holds, and that is most of what a dialog is: it takes the input
// under it, it fades in and out as one group with its smoke, Esc closes it, Tab stays inside it
// while it is up, and the title bar above it stays live so the window can still be dragged with a
// question open. What is added here is the shape and the answer.
//
// ```cpp
// if (asking) {
//     auto *d = new Dialog(L"Delete this file?", L"It will not go to the Recycle Bin.");
//     d->AddButton(L"Cancel", 0);
//     d->AddButton(L"Delete", 1, ButtonStyle::Accent);
//     d->onResult = [this](int r) {
//         if (r == 1) Delete();
//         asking = false;
//     };
//     page->Add(d);
// }
// ```
//
// Two things worth knowing before writing one:
//
//   - **The buttons are the dialog's children and it places them itself.** A layer takes no place in
//     the layout of the widget it was added to -- see `ArrangeSubtree` -- so nothing else arranges a
//     dialog, and `DialogLayout` is free to put the footer where a footer goes. `AddButton` is the
//     whole of the interface to them, and the order is the order they are read in: the primary goes
//     last, at the right-hand end, which is where Windows puts it.
//
//     **Add the dialog to what it is to cover**, which is the page and not the card whose button
//     raised it: where a layer goes is the layer's own answer (`Layer::Cover`), and this one takes
//     the default, which is the whole of the widget it was added to -- so a question asked of a page
//     covers the page. Not the scrolling container the page is in either: a layer inside a container
//     that clips would be clipped by it and would travel with the page it is asking about, which is
//     the one thing a modal must not do.
//   - **`onResult` runs after the dialog has started leaving**, so laying the page out in it cannot
//     pull the layer out from under a panel that is still fading. That order is the whole reason
//     `Pick` exists.

#pragma once

#include "button.h"
#include "window.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace micula {

struct Dialog;

// Where a dialog's parts go, in the dialog's own space. The dialog works this out and paints it as
// it stands, and the layout places the buttons in the very same footer -- one answer rather than
// two, which is what stops a drawn panel and a hit-tested button from drifting apart.
struct DialogFrame {
    D2D1_RECT_F panel = {};    // the surface: its shadow, its fill and its contour
    D2D1_RECT_F footer = {};   // the button band, a shade down from the body and under a separator
    D2D1_RECT_F body = {};     // where the body text is drawn, and wrapped
};

// The dialog's own layout, and it arranges one thing: the row of buttons along the bottom. The rest
// of the geometry is the dialog's, because the frame of a dialog is what the dialog *is* rather than
// what its layout came up with -- `Paint` draws the same panel the hit test is asked about.
//
// A column would not do. The buttons are right-aligned in a band whose height is the panel's, and
// their widths are what the panel was sized to hold, so the two have to agree; a layout that put
// children one under another would be answering a question nobody asked.
struct DialogLayout : Layout {
    Want Measure(const Room &room) const override;
    void Arrange(const Room &room, const D2D1_RECT_F &box) override;
};

struct Dialog : Layer {
    // A button, and the number it answers with. The page's own numbers: a dialog says what was
    // pressed, not what it means.
    struct Action {
        std::wstring label;
        int          result = 0;
        ButtonStyle  style  = ButtonStyle::Standard;
    };

    std::wstring title, body;
    std::vector<Action> actions;
    // The same buttons as widgets, which is what the layout places and what a focus ring is made of.
    std::vector<Button *> buttons;

    // The answer, with the number the button carried.
    std::function<void(int)> onResult;

    // What Esc reports, and what the window deactivating reports. Give it the number of the button
    // that means cancel; a dialog with no way out of it turns `escape` off instead (see Layer).
    int cancelResult = 0;

    // Enter presses the accent button, which is what a confirm dialog wants. **Turn it off for a
    // dialog whose primary destroys something**: Enter arriving from nowhere is how people lose
    // files, and it is the reason Windows leaves a `ContentDialog` with no default button at all
    // unless a page names one.
    bool enterTakesPrimary = true;

    explicit Dialog(std::wstring heading, std::wstring text = {})
        : title(std::move(heading)), body(std::move(text)) {
        smoke = true;
        SetLayout(new DialogLayout());
        // Not light-dismissing, which is `Layer`'s default and stays it: a click on the dim is how a
        // flyout is abandoned, and a question is answered. Esc, the buttons and the close box are the
        // ways out, and `onDismiss` -- which is where Esc and a deactivation arrive -- is wired to
        // the cancel here so that a page only has to know about `onResult`.
        onDismiss = [this] { Pick(cancelResult); };
    }

    // Buttons are listed left to right, so the one that means "yes" is added last: Windows puts the
    // primary at the right-hand end with the cancel to its left. This is also the whole of what a
    // page has to do about them -- the button is the dialog's child, so it fades with it, is dropped
    // with it, and is what Tab walks while the question is up.
    Dialog &AddButton(std::wstring label, int result, ButtonStyle style = ButtonStyle::Standard) {
        actions.push_back(Action{ label, result, style });
        buttons.push_back(Add(new Button(std::move(label), style, [this, r = result] { Pick(r); })));
        return *this;
    }

    // Microsoft's ContentDialog numbers: 24 DIPs of padding, 320 to 548 wide, and the body a line of
    // its own under the title. The title box is a line's worth rather than the glyph height, for the
    // reason a clipped descender gives away -- the format centres and clips (see Painter::Text).
    static constexpr float kPad    = 24.0f;
    static constexpr float kMinW   = 320.0f;   // ContentDialogMinWidth
    static constexpr float kMaxW   = 548.0f;   // ContentDialogMaxWidth
    static constexpr float kTitleH = 32.0f;
    static constexpr float kGap    = 8.0f;     // title to body, when there are both
    static constexpr float kButtonGap = 8.0f;

    // How wide a button comes out: what it asks for, and never narrower than the 100 DIPs that keep a
    // row of buttons even.
    float ButtonWidth(const Fonts &fonts, const Button &b) const {
        return (std::max)(b.PreferredWidth(&fonts), metric::kButtonMinW);
    }

    // The panel's size: as wide as the widest thing that cannot wrap -- the title, each line of the
    // body as it was written, and the row of buttons -- inside ContentDialog's limits, and as tall
    // as what is on it. Asked by the layout when a measurement of the dialog is needed, and by
    // `FrameOf` to place it in the box.
    D2D1_SIZE_F PanelSize(const Fonts &fonts, const Spec &spec) const {
        float natural = 0.0f;
        if (!title.empty()) natural = (std::max)(natural, fonts.Measure(fonts.subtitle, title));
        for (size_t at = 0; at <= body.size();) {
            const size_t nl = body.find(L'\n', at);
            natural = (std::max)(natural, fonts.Measure(fonts.body, body.substr(at, nl - at)));
            if (nl == std::wstring::npos) break;
            at = nl + 1;
        }
        float row = 0.0f;
        for (size_t i = 0; i < buttons.size(); i++) {
            if (i > 0) row += kButtonGap;
            row += ButtonWidth(fonts, *buttons[i]);
        }
        natural = (std::max)(natural, row);

        const float w = std::clamp(natural + 2.0f * kPad, kMinW, kMaxW);
        const float titleH = title.empty() ? 0.0f : kTitleH;
        const float gap    = (titleH > 0.0f && !body.empty()) ? kGap : 0.0f;
        const float bodyH  = body.empty() ? 0.0f
            : fonts.WrappedHeight(fonts.body, body, w - 2.0f * kPad);
        const float footH  = buttons.empty() ? 0.0f : 2.0f * kPad + spec.controlH;
        return { w, 2.0f * kPad + titleH + gap + bodyH + footH };
    }

    // Where everything goes, in the dialog's own space and centred in the box it was given -- which
    // is the whole page, because covering the page is what a layer in it does.
    //
    // Kept as well as returned, because `Body()` has to answer a hit test with the panel and a hit
    // test has no fonts to measure with. The arrangement always runs before anything can be drawn or
    // clicked, so what was kept is what is on screen; `Paint` works it out again rather than trusting
    // it, which costs a few cached measurements and keeps the two from being able to disagree.
    DialogFrame FrameOf(const Fonts &fonts, const Spec &spec) {
        const D2D1_SIZE_F size = PanelSize(fonts, spec);
        const float cx = (rect.left + rect.right) / 2.0f;
        const float cy = (rect.top + rect.bottom) / 2.0f;
        frame.panel  = { cx - size.width / 2.0f, cy - size.height / 2.0f,
                         cx + size.width / 2.0f, cy + size.height / 2.0f };
        const float footH = buttons.empty() ? 0.0f : 2.0f * kPad + spec.controlH;
        frame.footer = { frame.panel.left, frame.panel.bottom - footH,
                         frame.panel.right, frame.panel.bottom };
        const float titleH = title.empty() ? 0.0f : kTitleH;
        const float gap    = (titleH > 0.0f && !body.empty()) ? kGap : 0.0f;
        const float bodyH  = body.empty() ? 0.0f
            : fonts.WrappedHeight(fonts.body, body, Width(frame.panel) - 2.0f * kPad);
        frame.body = { frame.panel.left + kPad, frame.panel.top + kPad + titleH + gap,
                       frame.panel.right - kPad, frame.panel.top + kPad + titleH + gap + bodyH };
        return frame;
    }
    DialogFrame frame;

    // Tab stays in the footer while the dialog is up, which is what makes it modal to the keyboard as
    // well as to the pointer.
    std::vector<Widget *> FocusRing() override { return { buttons.begin(), buttons.end() }; }

    Widget *DefaultButton() override {
        if (!enterTakesPrimary) return nullptr;
        for (size_t i = 0; i < buttons.size() && i < actions.size(); i++)
            if (actions[i].style == ButtonStyle::Accent) return buttons[i];
        return nullptr;
    }

    // The panel, so that a page that turns light-dismissing on gets a dismissable dialog: a click on
    // the dim is outside this, and a click on the question itself is inside it.
    D2D1_RECT_F Body() const override { return frame.panel; }

    // Answered: `Close` first, so the layer is already on its way out when the page hears about it.
    // A page that lays itself out in `onResult` -- which is what every page does -- would otherwise
    // take the layer out of the tree on the frame the button was pressed, and the fade would never
    // be seen. The dialog is not deleted here; the window drops the subtree when the fade is over.
    void Pick(int result) {
        Close();
        if (onResult) onResult(result);
    }

    // The default button takes the focus, so that the ring is on it and Enter and Space agree about
    // what is about to happen. This is the first frame the dialog has a window to be focused in:
    // there is no "I have been added" to hang it on -- a page builds a tree and a window comes later
    // -- and this is the first moment the question can be answered at all. A dialog with no primary
    // button leaves the focus where it is rather than clearing it, whether it is in a window or not,
    // because Tab inside the dialog is the page's business until something asks for it.
    void Tick(float dt) override {
        Layer::Tick(dt);
        if (tookFocus) return;
        Surface *w = surface();
        if (!w) return;              // not in one yet: this comes round again on the next frame
        tookFocus = true;
        if (Widget *first = DefaultButton()) w->SetFocusTo(first);
    }

    void Paint(const Painter &p) override {
        Layer::Paint(p);                       // the smoke, when there is one
        const DialogFrame f = FrameOf(*p.font, layout->spec);

        // A surface over the page: a shadow, an opaque fill so that the page does not read through
        // it, and a contour. The shadow is a *tighter* one than a flyout's, of all things -- see
        // Painter::Shadow for why Fluent's ambient is a blur of 8 -- and the contour is doing the
        // job Windows gives it, which is the job a sharp shadow does on other platforms: it is what
        // says where the panel ends. That is why it is `dialogStroke` and not the flyout's 5.78 per
        // cent, and why it reads across the dim instead of vanishing into it.
        p.Shadow(f.panel, metric::kRadiusCard, 1.0f, 12.0f, 2.0f, 14, 0.18f);
        p.FillRound(f.panel, metric::kRadiusCard, p.pal->flyoutBg);
        // The button area is a surface of its own, a shade down from the body of the dialog and under
        // a separator line. Windows' own prompts have looked like that for as long as they have had
        // buttons along the bottom, and the line is what stops the last row of buttons reading as one
        // more line of the text.
        if (f.footer.top > f.panel.top) {
            p.Panel(f.footer, Corners(0.0f, 0.0f, metric::kRadiusCard, metric::kRadiusCard),
                    Shade(p.pal->flyoutBg, -0.03f));
            p.Line(f.footer.left, f.footer.top, f.footer.right, f.footer.top, p.pal->flyoutStroke);
        }
        p.StrokeRound(f.panel, metric::kRadiusCard, p.pal->dialogStroke);

        if (!title.empty())
            p.Text(title, { f.panel.left + kPad, f.panel.top + kPad, f.panel.right - kPad,
                            f.panel.top + kPad + kTitleH },
                   p.font->subtitle, p.pal->textPrimary);
        if (!body.empty()) {
            // `clip` on: a body longer than the box it was measured into is cut rather than drawn
            // over the buttons, and a body that does not fit is a layout fault worth seeing.
            p.TextWrapped(body, f.body, p.font->body, p.pal->textPrimary, false, true);
        }
    }

    // A question is a group of things: what it asks, and how to answer it.
    const wchar_t *AccessibleName() const override { return title.empty() ? nullptr : title.c_str(); }
    int AccessibleType() const override { return UIA_GroupControlTypeId; }

private:
    bool tookFocus = false;
};

inline Want DialogLayout::Measure(const Room &room) const {
    const D2D1_SIZE_F size = static_cast<const Dialog *>(host_)->PanelSize(*room.fonts, spec);
    return Want(Axis::Content(size.width), Axis::Content(size.height));
}

inline void DialogLayout::Arrange(const Room &room, const D2D1_RECT_F &box) {
    Dialog *d = static_cast<Dialog *>(host_);
    const DialogFrame f = d->FrameOf(*room.fonts, spec);
    // The frame is in the dialog's own rectangle, which is the space it draws in; the buttons go in
    // the box the dialog was arranged in, and that is the same rectangle with the dialog's own origin
    // taken off -- one rectangle named from outside and from inside. A dialog covers what it was
    // added to and starts at that origin, so the shift is nothing; a layer with a rectangle of its
    // own would have one, and the buttons are not part of it.
    //
    // **The far end of the row is the panel's right edge**, in that same box. Written from the box
    // instead -- which is the whole of the *page* -- the buttons are placed from the page: on a
    // 700-DIP window the difference is 200 DIPs, which is a footer whose buttons have walked out of it
    // and off to the right, still answering clicks where they are drawn.
    const float footTop = f.footer.top - d->rect.top + box.top;
    const float right = f.panel.right - d->rect.left + box.left;
    // The buttons at the right-hand end of the footer, the one added last furthest right, each as
    // wide as it asked to be. The panel was sized to hold this row, so nothing here has to measure
    // it again to be sure it fits.
    float x = right - Dialog::kPad;
    for (size_t i = d->buttons.size(); i > 0; i--) {
        Button *b = d->buttons[i - 1];
        const float bw = d->ButtonWidth(*room.fonts, *b);
        b->rect = { x - bw, footTop + Dialog::kPad, x,
                    footTop + Dialog::kPad + spec.controlH };
        x -= bw + Dialog::kButtonGap;
    }
}

}  // namespace micula
