// Micula / slider.h

#pragma once

#include "window.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <functional>
#include <string>
#include <vector>

namespace micula {


struct Slider : Widget {
    float value = 0.0f, lo = -1.0f, hi = 1.0f, step = 0.01f;
    // Two callbacks, because a slider produces two different kinds of event and the
    // caller wants different things from them. `onChange` fires on every pixel of the
    // drag -- that is the readout following the knob. `onCommit` fires once, when the
    // gesture ends, and that is the one to write a file from: saving on every change
    // rewrites the file a hundred times across one drag.
    //
    // A page that listens only to the first and saves nothing ends up with a value that
    // is correct on screen, correct in memory and absent from the disk -- so it comes
    // back wrong the next time the page is built.
    std::function<void(float)> onChange;
    std::function<void(float)> onCommit;
    // Between a press and its release, wherever the pointer has got to since. The
    // gesture's own flag rather than `pressed`, which the window clears as soon as the
    // pointer leaves the rectangle -- a slider's drag outlives that by design.
    bool dragging = false;
    // Whether this gesture has moved the value at all. `onCommit` is the end of a gesture and not a
    // statement that the value changed, and those are two different things: a press on the knob that
    // leaves it exactly where it was ends a gesture and changed nothing.
    bool moved = false;
    // Where the knob is drawn, as a fraction, which trails a value that snaps from one step
    // to the next. A step is the one thing about a stepped slider that is visible, and
    // answering it by jumping the knob says the knob and the value are the same thing. They
    // are not: the value is what the page is told and it is exact, the knob is where the
    // result is drawn -- so the knob eases to each new step while the value is already there.
    motion::Track drawn;

    Slider(float v, float a, float b, float s, std::function<void(float)> f)
        : value(v), lo(a), hi(b), step(s), onChange(std::move(f)), drawn(Frac()) {}

    // A slider is the one control that is nothing but a line of travel, so it takes the whole width
    // it is offered and one control's height to sit in the middle of.
    micula::Want Measure(const Room &room) const override {
        return micula::Want(Axis::Fill(), Axis::Fixed(room.spec->controlH));
    }

    bool Focusable() const override { return true; }
    // While it is being dragged: the knob follows the pointer, and the pointer moving is
    // the only thing that happens. The window repaints on that account rather than on the
    // caller's -- a slider whose `onChange` does not happen to invalidate the window is
    // still a slider, and its knob still has to move. `dragging` rather than `pressed`,
    // which the window clears the moment the pointer leaves the rectangle.
    bool TracksPointer() const override { return dragging; }
    // A drag of a slider is mostly sideways, and a control row is 32 DIPs tall with a
    // 20-DIP thumb in the middle: six pixels of wander and the pointer is outside. The
    // thumb is the one thing on screen saying the gesture has not ended, so it stays held
    // for the whole of it.
    bool PressedVisual() const override { return pressed || dragging; }
    float Frac() const { return (value - lo) / (hi - lo); }

    // A slider to a screen reader: a slider, and what it is set to. The percentage is formed here
    // rather than by the client, because this is the only side that knows what the number means --
    // the same 0.4 is 40% on one control and a fourth of a scale on the next.
    int AccessibleType() const override { return UIA_SliderControlTypeId; }
    bool AccessibleValue(std::wstring &out) const override {
        if (hi <= lo) return false;
        out = std::to_wstring((int)(Frac() * 100.0f + 0.5f)) + L"%";
        return true;
    }
    // And the number behind the percentage: what it is worth, what it runs between, and the step one
    // press of an arrow key takes. A client needs the number rather than the words -- it is what says
    // "five to a hundred" out loud -- and the percentage is what a reader says after it.
    bool AccessibleRange(float &v, float &lo, float &hi, float &step) const override {
        if (this->hi <= this->lo) return false;
        v = value;
        lo = this->lo;
        hi = this->hi;
        step = this->step;
        return true;
    }
    bool AccessibleWritable() const override { return true; }
    // **The path a drag takes, not a way around it**: snapped to the step, reported through `onChange`
    // and committed, because a client setting a slider is somebody setting it and a page that saves on
    // a commit must not be able to tell the two apart. Out of range is refused rather than clamped: a
    // client that asked for 200 out of a 5..100 slider asked for something this control does not do.
    bool AccessibleSetRange(float v) override {
        if (this->hi <= this->lo || v < this->lo || v > this->hi) return false;
        const float raw = step > 0.0f ? std::round(v / step) * step : v;
        const float snapped = std::clamp(raw, this->lo, this->hi);
        if (snapped == value) return true;
        value = snapped;
        moved = true;
        if (onChange) onChange(value);
        if (onCommit) onCommit(value);
        Invalidate();
        return true;
    }

    bool Animating() const override { return Widget::Animating() || drawn.Wants(Frac()); }
    void Tick(float dt) override {
        Widget::Tick(dt);
        // A curve rather than a follower, because a step is a place rather than a direction:
        // the knob arrives at it and stops, instead of closing a gap that a hand could keep
        // opening. Retargeting means a fast drag restarts this once a step, which is a move
        // the knob can make continuously.
        drawn.To(Frac());
        drawn.Step(dt, motion::kFaster);
    }

    void SetFromX(float x) {
        const float t = std::clamp((x - rect.left - 8) / (std::max)(1.0f, Width(rect) - 16), 0.0f, 1.0f);
        // Snapped to the step, so a slider that writes 0.02 into a settings file
        // cannot land on 0.019999999. Files like that are read by people.
        const float raw = lo + t * (hi - lo);
        const float snapped = std::round(raw / step) * step;
        // A drag is a mouse message per pointer move, and a step is wider than a pixel,
        // so most of them land on the step the value is already on. Those have nothing
        // to report, and reporting them anyway would have onChange write the settings
        // file a hundred times across one gesture.
        if (snapped == value) return;
        value = snapped;
        moved = true;
        if (onChange) onChange(value);
    }
    // The press is already a value: clicking anywhere on the track puts the knob there,
    // which is what every slider does and what makes the track worth aiming at. It also
    // means a click over before the next frame is still a click.
    void OnPress(float x, float /*y*/) override { dragging = true; moved = false; SetFromX(x); }
    void OnDrag(float x, float /*y*/) override { SetFromX(x); }
    // Nothing: the press placed the knob and the drag moved it. A slider has no separate
    // click, and Space reaches here from the keyboard -- OnKey is where that belongs.
    void OnClick() override {}
    void OnRelease() override {
        dragging = false;
        // Only when the gesture moved something: see `moved`. A page that saves from a commit would
        // otherwise write the file for the act of touching the control.
        if (moved && onCommit) onCommit(value);
    }
    bool OnKey(WPARAM vk) override {
        const float was = value;
        if (vk == VK_LEFT || vk == VK_DOWN)  { value = (std::max)(lo, value - step); }
        else if (vk == VK_RIGHT || vk == VK_UP) { value = (std::min)(hi, value + step); }
        else if (vk == VK_HOME) value = lo;
        else if (vk == VK_END)  value = hi;
        else return false;
        // The key is consumed either way -- it was not the page's to use -- but a key pressed with
        // the value already against its end has not changed anything, and has nothing to report.
        if (value == was) return true;
        if (onChange) onChange(value);
        // A keypress is a whole gesture on its own -- there is no release to wait for.
        if (onCommit) onCommit(value);
        return true;
    }

    void Paint(const Painter &p) override {
        const Palette &c = *p.pal;
        // Draws the value and nothing else. It used to *set* it here, from the cursor,
        // on the grounds that the window already held capture and already set `pressed`
        // -- so a paint was the whole of the drag and no mouse-move plumbing was needed.
        // That was wrong three times over: `pressed` goes false the moment the pointer
        // leaves the 32-DIP row, which froze the knob mid-drag; a value read from the
        // physical cursor cannot be driven by a posted message, which put the control out
        // of reach of a message-posting harness; and a page long enough to scroll does not
        // paint a control the clip has left behind, which would stop the drag outright.
        // OnPress and OnDrag now carry it.
        const float cy = rect.top + Height(rect) / 2;
        const float x0 = rect.left + 8, x1 = rect.right - 8;
        const float at = x0 + std::clamp(drawn.value, 0.0f, 1.0f) * (x1 - x0);
        p.rt->FillRoundedRectangle(D2D1::RoundedRect({ x0, cy - 2, x1, cy + 2 }, 2, 2),
                                   p.Brush(c.controlStroke));
        p.rt->FillRoundedRectangle(D2D1::RoundedRect({ x0, cy - 2, at, cy + 2 }, 2, 2),
                                   p.Brush(enabled ? c.accent : c.textDisabled));
        // Fluent's thumb is an accent ring with a filled centre that shrinks when
        // grabbed. Drawn as three circles because that is exactly what it is.
        p.rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(at, cy), 10, 10), p.Brush(c.controlBg));
        p.rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(at, cy), 9.5f, 9.5f),
                          p.Brush(c.controlStroke), 1.0f);
        // 6 at rest, 7 under the pointer, 4 while it is being dragged -- Fluent's own
        // three sizes, now passed through rather than jumped between.
        const float r = 6.0f + hoverT - 3.0f * pressT;
        p.rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(at, cy), r, r),
                          p.Brush(enabled ? c.accent : c.textDisabled));
        if (ShowFocusRing())
            p.rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(at, cy), 12, 12),
                              p.Brush(c.textPrimary), 2.0f);
    }
};

}  // namespace micula
