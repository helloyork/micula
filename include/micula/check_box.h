// Micula / check_box.h

#pragma once

#include "window.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <functional>
#include <string>
#include <vector>

namespace micula {


struct CheckBox : Widget {
    std::wstring label;
    std::wstring detail;             // second line, secondary colour, optional
    bool  checked = false;
    bool  readOnly = false;          // shows a state rather than taking one
    std::function<void(bool)> onChange;
    // The box filling and the tick arriving, rather than both appearing between two
    // frames. WinUI's checkbox animates its glyph in; this is that, one storyboard at
    // ControlFastAnimationDuration.
    motion::Track checkT;

    CheckBox(std::wstring text, bool on, std::function<void(bool)> f)
        : label(std::move(text)), checked(on), onChange(std::move(f)),
          checkT(on ? 1.0f : 0.0f) {}

    // **The state is the page's to set, and nobody else's.** A read-only box takes no click, no
    // Space and no Enter -- all three land in `OnClick` -- and a screen reader's `Toggle` is
    // refused rather than answered and quietly ignored. The difference from `enabled = false` is
    // in `Paint`: the *label* stays at full strength, so a page showing a state somebody else owns
    // -- a log level, a value read back from a device -- is not left with a greyed row. `checked`
    // is still the page's field, the way `SetText` is a read-only field's.
    void SetReadOnly(bool on) {
        readOnly = on;
        Invalidate();
    }

    // The box, the gap after it, and whichever of the two lines is wider.
    micula::Want Measure(const Room &room) const override {
        const Fonts *f = room.fonts;
        const float text = (std::max)(f->Measure(f->body, label), f->Measure(f->caption, detail));
        return micula::Want(Axis::Content(20.0f + 12.0f + text),
                            Axis::Fixed(room.spec->controlH));
    }

    bool Focusable() const override { return true; }
    void OnClick() override {
        if (!enabled || readOnly) return;
        checked = !checked;
        if (onChange) onChange(checked);
    }

    // What it is to a screen reader: a labelled check box, and the state it is showing. `checked`
    // rather than the animated fill, so a client is told what was decided and not what is still
    // crossing -- see the UIA section of window.h.
    const wchar_t *AccessibleName() const override { return label.c_str(); }
    int AccessibleType() const override { return UIA_CheckBoxControlTypeId; }
    int AccessibleToggle() const override { return checked ? 1 : 0; }
    // Whether a client may press it, which is the one question the Toggle pattern asks before it
    // acts: a read-only box is refused rather than told yes, called, and left where it was.
    bool AccessibleWritable() const override { return !readOnly; }
    bool Animating() const override {
        return Widget::Animating() || checkT.Wants(checked ? 1.0f : 0.0f);
    }
    void Tick(float dt) override {
        Widget::Tick(dt);
        checkT.To(checked ? 1.0f : 0.0f);
        checkT.Step(dt, motion::kFast);
    }

    void Paint(const Painter &p) override {
        const Palette &c = *p.pal;
        const float side = 20.0f;
        const float y = rect.top + (Height(rect) - side) / 2;
        const D2D1_RECT_F box = { rect.left, y, rect.left + side, y + side };

        // **A read-only box answers nobody**, so the hover and press washes are mixed in for a box
        // that takes a press and for nothing else: what a read-only one draws is the state, and
        // nothing about where the pointer is. Checked, it takes Fluent's own pair for a disabled
        // accent -- the fill a disabled accent button has, and the text that reads on it -- which
        // is a box showing a decision without offering to take one.
        const bool live = enabled && !readOnly;
        const D2D1_COLOR_F off  = live ? Mix(c.controlBg, c.controlBgHover, hoverT) : c.controlBg;
        const D2D1_COLOR_F on   = live
            ? Mix(Mix(c.accent, c.accentHover, hoverT), c.accentPressed, pressT) : c.accentDisabled;
        const D2D1_COLOR_F tick = !enabled ? c.textDisabled
                                           : (readOnly ? c.accentTextDisabled : c.accentText);
        const float k = checkT.value;
        p.FillRound(box, 3.0f, !enabled ? c.controlBg : Mix(off, on, k));
        // The empty box's border goes as the accent comes up behind it, or the two are
        // both visible halfway through and the box reads as having gained a second edge.
        if (k < 1.0f)
            p.StrokeRound(box, 3.0f, Fade(enabled ? c.controlStroke : c.controlBg, 1.0f - k));
        if (k > 0.0f)
            p.Text(glyph::kCheck, { box.left + 2, box.top, box.right, box.bottom },
                   p.font->icon, Fade(tick, k));
        if (ShowFocusRing()) {
            const D2D1_RECT_F o = { rect.left - 2, rect.top - 1, rect.right + 2, rect.bottom + 1 };
            p.StrokeRound(o, 5.0f, c.textPrimary, 2.0f);
        }

        const D2D1_COLOR_F fg = enabled ? c.textPrimary : c.textDisabled;
        const float tx = rect.left + side + 12;
        if (detail.empty()) {
            p.Text(label, { tx, rect.top, rect.right, rect.bottom }, p.font->body, fg);
        } else {
            const float half = Height(rect) / 2;
            p.Text(label, { tx, rect.top, rect.right, rect.top + half }, p.font->body, fg);
            p.Text(detail, { tx, rect.top + half, rect.right, rect.bottom },
                   p.font->caption, c.textSecondary);
        }
    }
};

}  // namespace micula
