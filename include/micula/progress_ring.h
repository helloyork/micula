// Micula / progress_ring.h

#pragma once

#include "window.h"

#include <algorithm>
#include <cmath>

namespace micula {


// Which brush the moving part is drawn in. Accent is the Fluent default -- a ring is a control
// saying that something is happening, and that is what the accent is for. Subtle is grey, for a ring
// that is a detail of somebody else's panel rather than a control of its own, which is what a page
// waiting on a background job wants and a blue spinner in the middle of a picture is not.
enum class RingStyle {
    Accent,
    Subtle,
};

struct ProgressRing : Widget {
    float value = 0.0f;      // 0..1
    bool  indeterminate = false;
    RingStyle style = RingStyle::Accent;
    // The ring is square and it says so: a ring whose width and height differ is an ellipse, and
    // nobody draws one of those on purpose. A page places it by saying how big it is.
    float size = 32.0f;

    micula::Want Measure(const Room &) const override {
        return micula::Want(Axis::Fixed(size), Axis::Fixed(size));
    }

    // WinUI's ProgressRing, to the numbers, and taken from the visual it actually draws rather than
    // from a reading of how it looks. The source is LottieGen's ProgressRingIndeterminate: an 80
    // unit box, a radius of 35, a stroke of 7.5, round ends, 60 fps, 120 frames, two seconds a
    // cycle. The three ratios below are that box's, so the ring is the same ring at any size.
    //
    // One cycle is:
    //
    //   - the whole shape turns a steady 450 degrees a second -- two and a half turns a cycle, and
    //     no easing at all: the curve the source turns on is cubic-bezier(0.167, 0.167, 0.833,
    //     0.833), whose control points all lie on the diagonal, which makes it the identity;
    //   - and an arc of that circle, capped round, grows from a dot to half the circle over the
    //     first second -- its head running away from a fixed tail -- then shrinks back to a dot over
    //     the second second, its tail catching up with a head that has stopped.
    //
    // The source does that with two arcs and an opacity cross-fade between them, so that a trimmed
    // path's end never has to move; what an eye sees is one arc with one moving end, which is what
    // is drawn here.
    static constexpr float kStrokeRatio = 0.09375f;   // 7.5 of 80
    static constexpr float kRadiusRatio = 0.4375f;    // 35 of 80 -- where the stroke's centre runs
    static constexpr float kCycleS      = 2.0f;
    static constexpr float kTurnDeg     = 900.0f;     // per cycle, i.e. 450 a second
    static constexpr float kHalfDeg     = 180.0f;     // as long as the arc ever gets
    static constexpr float kLeastDeg    = 0.036f;     // ...and as short: 0.0001 of a turn, a dot

    // The arc at `phase` of the cycle: where it starts and how much of the circle it covers, in
    // degrees clockwise from three o'clock. Both halves turn at the same rate; what tells them
    // apart is which end of the arc is moving -- the first half takes the head round, the second
    // pulls the tail up to a head that has stopped.
    static void ArcAt(float phase, float *start, float *sweep) {
        const bool closing = phase >= 0.5f;
        const float u = closing ? phase * 2.0f - 1.0f : phase * 2.0f;  // through this half
        const float half = kTurnDeg * 0.5f;
        const float turn = closing ? half * (1.0f + u) : half * u;
        const float arc = kHalfDeg * u;
        if (closing) { *start = turn + arc; *sweep = kHalfDeg - arc; }
        else         { *start = turn;       *sweep = arc; }
        // A round cap on a zero-length arc is a dot, which is what a cycle ends and begins as -- and
        // never *exactly* zero, because the source trims to 0.0001 of the circle rather than to
        // nothing, and the dot is the one frame of this animation that has to be seen.
        if (*sweep < kLeastDeg) *sweep = kLeastDeg;
    }

    // Self-driving, like the indeterminate bar and for the same reason: a ring that has to be told
    // to advance is a ring that sits still wherever a page forgets.
    bool Animating() const override { return Widget::Animating() || indeterminate; }

    // Where in the cycle *now* is, in seconds -- read from the clock rather than counted across
    // frames. See ProgressBar::Cycle: a control is rebuilt for all sorts of reasons, and a phase
    // accumulated per frame starts the animation over on each of them.
    static float Cycle() {
        return (float)std::fmod(micula::MonotonicSeconds(), (double)kCycleS);
    }

    // A progress ring to a screen reader is a progress bar: the same fact, the same control type.
    // The percentage too, unless it is the indeterminate one, which has no number to report and 0%
    // is a value that was never there.
    int AccessibleType() const override { return UIA_ProgressBarControlTypeId; }
    bool AccessibleValue(std::wstring &out) const override {
        if (indeterminate) return false;
        out = std::to_wstring((int)(std::clamp(value, 0.0f, 1.0f) * 100.0f + 0.5f)) + L"%";
        return true;
    }

    void Paint(const Painter &p) override {
        const Palette &c = *p.pal;
        // Square, and as big as the rectangle allows: a ring whose width and height differ is an
        // ellipse, and nobody draws one of those on purpose.
        const float d = (std::min)(Width(rect), Height(rect));
        if (d <= 0.0f) return;
        const float cx = (rect.left + rect.right) * 0.5f;
        const float cy = (rect.top + rect.bottom) * 0.5f;
        const float r = d * kRadiusRatio;
        const float w = d * kStrokeRatio;
        const D2D1_COLOR_F col = style == RingStyle::Accent ? c.accent : c.textSecondary;

        float start = 0.0f, sweep = 0.0f;
        if (indeterminate) {
            ArcAt(Cycle() / kCycleS, &start, &sweep);
        } else {
            const float v = std::clamp(value, 0.0f, 1.0f);
            // From the top, clockwise, as much of the circle as there is progress: the Fluent ring
            // fills the way a clock does, and an arc that starts anywhere else reads as a decoration.
            start = -90.0f;
            sweep = 360.0f * v;

            // The track, drawn here and not for the indeterminate one: a track is "this much of the
            // whole", and a ring that has no whole has no part of it to be. Drawn whatever the value
            // is, nought included -- an empty track is what a ring at the start of its value looks
            // like, and the alternative is a control that has gone missing.
            p.rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r),
                              p.Brush(c.controlStroke), w);
        }

        // A full circle is not an arc: its two end points are the same point, and Direct2D draws a
        // figure from a point to itself as nothing at all. A determinate ring at 100% is a circle.
        if (sweep >= 360.0f) {
            p.rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), p.Brush(col), w, Stroke(p));
            return;
        }
        if (sweep <= 0.0f) return;

        ID2D1Factory *factory = nullptr;
        p.rt->GetFactory(&factory);
        if (!factory) return;
        ID2D1PathGeometry *geo = nullptr;
        if (FAILED(factory->CreatePathGeometry(&geo)) || !geo) return;

        const auto point = [&](float degrees) {
            const float a = degrees * 3.14159265f / 180.0f;
            return D2D1::Point2F(cx + r * std::cos(a), cy + r * std::sin(a));
        };

        // Made per frame rather than kept: a geometry is immutable once its sink is closed, and this
        // one is a different arc every frame. It is two points.
        ID2D1GeometrySink *sink = nullptr;
        if (SUCCEEDED(geo->Open(&sink)) && sink) {
            sink->BeginFigure(point(start), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc(D2D1::ArcSegment(point(start + sweep), D2D1::SizeF(r, r), 0.0f,
                                          D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                          sweep > kHalfDeg ? D2D1_ARC_SIZE_LARGE
                                                           : D2D1_ARC_SIZE_SMALL));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            sink->Release();
            p.rt->DrawGeometry(geo, p.Brush(col), w, Stroke(p));
        }
        geo->Release();
    }

    // Round ends, made once: an arc without them has sawn-off ends and reads as a broken ring.
    ~ProgressRing() override {
        if (m_stroke) m_stroke->Release();
    }

private:
    ID2D1StrokeStyle *Stroke(const Painter &p) const {
        if (m_stroke) return m_stroke;
        ID2D1Factory *factory = nullptr;
        p.rt->GetFactory(&factory);
        if (!factory) return nullptr;
        const D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
            D2D1_LINE_JOIN_ROUND, 10.0f, D2D1_DASH_STYLE_SOLID, 0.0f);
        factory->CreateStrokeStyle(props, nullptr, 0, &m_stroke);
        return m_stroke;
    }

    // Mutable because it is made on the first paint and read from then on, and a widget's paint is
    // const.
    mutable ID2D1StrokeStyle *m_stroke = nullptr;
};

}  // namespace micula
