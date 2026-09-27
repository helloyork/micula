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
//         Layout();
//     };
//     Add(d);                 // and nothing else: the dialog adds its own buttons
// }
// ```
//
// Two things worth knowing before writing one:
//
//   - **The dialog adds its own buttons**, in `OnAdded` -- see there for why it has to be the one
//     that does it. A page adds the dialog and stops.
//   - **`onResult` runs after the dialog has started leaving**, so laying the page out in it cannot
//     pull the layer out from under a panel that is still fading. That order is the whole reason
//     `Pick` exists.

#pragma once

#include "button.h"
#include "window.h"

#include <string>
#include <utility>
#include <vector>

namespace micula {

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
        // Not light-dismissing, which is `Layer`'s default and stays it: a click on the dim is how a
        // flyout is abandoned, and a question is answered. Esc, the buttons and the close box are the
        // ways out, and `onDismiss` -- which is where Esc and a deactivation arrive -- is wired to
        // the cancel here so that a page only has to know about `onResult`.
        onDismiss = [this] { Pick(cancelResult); };
    }

    // Buttons are listed left to right, so the one that means "yes" is added last: Windows puts the
    // primary at the right-hand end with the cancel to its left.
    Dialog &AddButton(std::wstring label, int result, ButtonStyle style = ButtonStyle::Standard) {
        actions.push_back(Action{ std::move(label), result, style });
        return *this;
    }

    // The controls on a layer are the layer's to add, and this is where: `Window::Add` calls it the
    // moment the dialog has joined the window, so the buttons land after it in the list and above it
    // in `z`, which is the order the pointer and the painter both need. A page adds the dialog and
    // nothing else.
    void OnAdded() override {
        if (!owner) return;
        rect = CoverPage();
        // A layer at z 0 is page content: it would be clipped to the scrolling strip and would move
        // with the page.
        if (z <= 0) z = 1;
        for (const Action &a : actions) {
            Button *b = owner->Add(new Button(a.label, a.style, [this, r = a.result] { Pick(r); }));
            b->z = z + 1;
            buttons.push_back(b);
            ring.push_back(b);
        }
        // The default button takes the focus, so that the ring is on it and Enter and Space agree
        // about what is about to happen.
        if (Widget *first = DefaultButton()) owner->SetFocusTo(first);
    }

    // Tab stays in the footer while the dialog is up, which is what makes it modal to the keyboard
    // as well as to the pointer.
    std::vector<Widget *> FocusRing() override { return ring; }

    Widget *DefaultButton() override {
        if (!enterTakesPrimary) return nullptr;
        for (size_t i = 0; i < buttons.size(); i++)
            if (actions[i].style == ButtonStyle::Accent) return buttons[i];
        return nullptr;
    }

    // What a click on the dim missed. Only reachable with `lightDismiss` turned on, which a dialog
    // does not do -- but a page that wants a dismissible one gets an answer rather than a close.
    D2D1_RECT_F Body() const override { return panel; }

    // Answered: `Close` first, so the layer is already on its way out when the page hears about it.
    // A page that lays itself out in `onResult` -- which is what every page does -- would otherwise
    // take the layer out of the list on the frame the button was pressed, and the fade would never
    // be seen. The dialog is not deleted here; the window drops it when the fade is over.
    void Pick(int result) {
        Close();
        if (onResult) onResult(result);
    }

    void Paint(const Painter &p) override {
        Place(p);
        Layer::Paint(p);                       // the smoke, when there is one

        // A surface over the page: a shadow, an opaque fill so that the page does not read through
        // it, and a contour. The shadow is a *tighter* one than a flyout's, of all things -- see
        // Painter::Shadow for why Fluent's ambient is a blur of 8 -- and the contour is doing the
        // job Windows gives it, which is the job a sharp shadow does on other platforms: it is what
        // says where the panel ends. That is why it is `dialogStroke` and not the flyout's 5.78 per
        // cent, and why it reads across the dim instead of vanishing into it.
        p.Shadow(panel, metric::kRadiusCard, 1.0f, 12.0f, 2.0f, 14, 0.18f);
        p.FillRound(panel, metric::kRadiusCard, p.pal->flyoutBg);
        // The button area is a surface of its own, a shade down from the body of the dialog and under
        // a separator line. Windows' own prompts have looked like that for as long as they have had
        // buttons along the bottom, and the line is what stops the last row of buttons reading as one
        // more line of the text.
        if (footer.top > panel.top) {
            p.Panel(footer, Corners(0.0f, 0.0f, metric::kRadiusCard, metric::kRadiusCard),
                    Shade(p.pal->flyoutBg, -0.03f));
            p.Line(footer.left, footer.top, footer.right, footer.top, p.pal->flyoutStroke);
        }
        p.StrokeRound(panel, metric::kRadiusCard, p.pal->dialogStroke);

        if (!title.empty())
            p.Text(title, { panel.left + kPad, panel.top + kPad, panel.right - kPad,
                            panel.top + kPad + kTitleH },
                   p.font->subtitle, p.pal->textPrimary);
        if (!body.empty()) {
            // `clip` on: a body longer than the box it was measured into is cut rather than drawn
            // over the buttons, and a body that does not fit is a layout fault worth seeing.
            p.TextWrapped(body, bodyBox, p.font->body, p.pal->textPrimary, false, true);
        }
    }

private:
    // Microsoft's ContentDialog numbers: 24 DIPs of padding, 320 to 548 wide, and the body a line of
    // its own under the title. The title box is a line's worth rather than the glyph height, for the
    // reason a clipped descender gives away -- the format centres and clips (see Painter::Text).
    static constexpr float kPad    = 24.0f;
    static constexpr float kMinW   = 320.0f;   // ContentDialogMinWidth
    static constexpr float kMaxW   = 548.0f;   // ContentDialogMaxWidth
    static constexpr float kTitleH = 32.0f;
    static constexpr float kGap    = 8.0f;     // title to body, when there are both
    static constexpr float kButtonGap = 8.0f;

    std::vector<Button *> buttons;
    // The same controls as `Widget *`, which is what a focus ring is made of -- and a second vector
    // only because `C++` will not let one be the other.
    std::vector<Widget *> ring;
    D2D1_RECT_F panel = {}, footer = {}, bodyBox = {};

    // Where everything goes, worked out from the text and the buttons on every paint rather than once
    // when the dialog was added. A page can be resized while a dialog is up, and the alternative --
    // a layout the page has to be told to call -- is one more thing for a page to forget. Measuring
    // is a lookup (`Fonts::Measure` keeps its answers), and this is also what puts the buttons where
    // they are drawn, which is where the hit test looks for them.
    void Place(const Painter &p) {
        if (!owner) return;
        // As wide as the content wants to be, up to the limit: what cannot wrap is a button row, the
        // title, and the body's own lines -- a body written as one paragraph wraps inside the width
        // those ask for.
        float natural = 0.0f;
        if (!title.empty()) natural = p.MeasureWidth(title, p.font->subtitle);
        for (size_t at = 0; at <= body.size();) {
            const size_t nl = body.find(L'\n', at);
            natural = (std::max)(natural, p.MeasureWidth(body.substr(at, nl - at), p.font->body));
            if (nl == std::wstring::npos) break;
            at = nl + 1;
        }
        float rowW = 0.0f;
        for (Button *b : buttons) {
            rowW += (std::max)(b->PreferredWidth(p), metric::kButtonMinW);
            if (b != buttons.front()) rowW += kButtonGap;
        }
        natural = (std::max)(natural, rowW);
        const float w = (std::min)(kMaxW, (std::max)(kMinW, natural + 2.0f * kPad));
        const float inner = w - 2.0f * kPad;

        const float titleH = title.empty() ? 0.0f : kTitleH;
        const float gap    = (titleH > 0.0f && !body.empty()) ? kGap : 0.0f;
        const float bodyH  = body.empty() ? 0.0f
            : p.TextWrapped(body, { 0.0f, 0.0f, inner, 1e5f }, p.font->body,
                            p.pal->textPrimary, true);
        const float footH  = buttons.empty() ? 0.0f : 2.0f * kPad + metric::kControlH;
        const float h = 2.0f * kPad + titleH + gap + bodyH + footH;

        // Centred in the part of the window the layer covers, which is the client under the caption
        // bar -- the same rectangle `CoverPage` gives, and the same place Windows puts one.
        const float cx = (rect.left + rect.right) / 2.0f;
        const float cy = (rect.top + rect.bottom) / 2.0f;
        panel  = { cx - w / 2.0f, cy - h / 2.0f, cx + w / 2.0f, cy + h / 2.0f };
        footer = { panel.left, panel.bottom - footH, panel.right, panel.bottom };
        bodyBox = { panel.left + kPad, panel.top + kPad + titleH + gap, panel.right - kPad,
                    panel.top + kPad + titleH + gap + bodyH };

        float x = panel.right - kPad;
        for (size_t i = buttons.size(); i > 0; i--) {
            Button *b = buttons[i - 1];
            const float bw = (std::max)(b->PreferredWidth(p), metric::kButtonMinW);
            b->rect = { x - bw, footer.top + kPad, x, footer.top + kPad + metric::kControlH };
            x -= bw + kButtonGap;
        }
    }
};

}  // namespace micula
