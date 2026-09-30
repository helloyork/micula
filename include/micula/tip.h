// Micula / tip.h
//
// **A control's `tips`, shown when the pointer comes to rest on it.** `Widget::tips` is one string and
// it is both the tooltip and what a screen reader is told -- see `Widget::AccessibleName` -- so there
// is nothing for a page to declare twice and nothing to keep in step.
//
// The box is a `Popup`: a window of its own, as big as its words and no bigger. A tip has to be able
// to cross the edge of the window it belongs to -- a control on the last row of a window says what it
// is in the strip below it -- and it must not take the foreground or the click: `activates` is false
// and the window is `WS_EX_TRANSPARENT`, so a click where the tip is lands on what the tip is about.
//
// Two pieces. `Tip` is the box, and `Tips` decides when it is there -- attach one to a window and the
// whole of the tooltip behaviour is that line:
//
// ```cpp
// struct MyWindow : Window {
//     Tips tips{ *this };
//     ...
// };
// ```
//
// **A hand on the glass has no tip, and that is not a special case here.** A tip waits for the pointer
// to rest on a control, and a finger does not rest: `Surface::MoveTo` answers "nothing is hovered"
// while one is down and `RefreshHover` keeps answering it once the finger has lifted, because a touch
// does not put a cursor anywhere. So a tip cannot be triggered by a finger at all, which is what a
// touch interface wants -- there is nothing to point with, so there is nothing to hover.
//
// **The dwell is a timer rather than a count of frames**, and it has to be. The frame loop only runs
// while something is animating (see `App::Moving`), and a pointer resting on a control is the
// definition of a window that is not: a tip that waited for frames would be waiting for an animation
// that is never coming. A Windows timer is what wakes a sleeping loop, which is the same reason the
// pane's auto-hide is one.

#pragma once

#include "popup.h"

#include <cmath>

namespace micula {

// How long the pointer has to rest on a control before the tip appears, and it is the machine's own
// answer rather than a number of ours: `SPI_GETMOUSEHOVERTIME` is what Windows' own tooltips wait, 400
// ms out of the box, and it is a setting a person can change -- which is what makes a tip feel early or
// late to the person using it rather than to whoever wrote this.
inline UINT TipDwell() {
    UINT ms = 0;
    if (!SystemParametersInfoW(SPI_GETMOUSEHOVERTIME, 0, &ms, 0) || ms == 0) ms = 400;
    return ms;
}

// How often a tip that is on screen checks that the pointer is still on the control it is about. It is a
// quarter of a second, which is short enough that nobody sees a tip that should have gone and long enough
// that a resting pointer costs four wake-ups a second rather than a frame loop.
constexpr UINT kTipWatchMs = 250;

// What a tip hangs off, and **the control is the default**: a tip is a sentence about the thing the
// pointer is on, so it belongs to that thing rather than to the hand, and it stays where it is while the
// hand moves across the control. It also leaves the control itself readable, which a tip beside the
// pointer does not -- what is under the pointer is what the tip is about, and covering it is the one
// thing a tip must not do.
//
// `Pointer` is the other answer, for a page that wants what the system's own tooltips do: the box is put
// beside the hand and follows it, which a control with no room of its own around it may need -- one that
// fills the window, or one against the edge of the screen. It is one switch for the whole `Tips` rather
// than one per control, because it is how a page reads rather than what a control is, and a page that
// wants one of them beside the hand wants all of them there.
enum class TipFollow { Control, Pointer };

// The box: one line of the platform's caption text with the platform's control radius around it, and
// nothing else. Its size *is* its words -- a tip is not a panel with something in it -- which is why it
// measures itself rather than carrying a layout: laying one line out is a `Measure` call and a margin.
struct Tip : Popup {
    Tip() {
        // Neither of the two things a tip must not do: take the focus, or take the click.
        activates = false;
        extraStyle = WS_EX_TRANSPARENT;
    }

    std::wstring text;
    const wchar_t *ClassName() const override { return L"MiculaTip"; }
    // The box it was last put in, in the DIPs `Place` takes, and the words that put it there: being asked for
    // the same thing again is not work. See `For`.
    int atX = 0, atY = 0, atW = 0, atH = 0;

    // What the box is placed by, in DIPs. The padding is the room a line of text is given in an overlay
    // in this library's other panels -- a dialog's body, a flyout's list. `kOffset` is how far the box is
    // kept off the pointer itself, which is the one place a tip must not be: whatever is under the
    // pointer is what the tip is about. `kGap` is the much smaller distance it is kept off the control
    // it hangs under, which only has to be enough for the two to read as two things.
    static constexpr float kPadX = 12.0f;
    static constexpr float kPadY = 8.0f;
    static constexpr float kOffset = 12.0f;
    static constexpr float kGap = 8.0f;
    // **A sentence is what a tip is, and a long one wraps.** Without a limit the box would be as wide as
    // the sentence, which for a tip that is really a paragraph is a strip across the whole screen; 400 DIP
    // is the width a tip gives itself before it turns into two or three lines.
    static constexpr float kMaxTextW = 400.0f;
    // **And the window is bigger than the panel by the room its shadow needs.** A panel drawn in a window
    // the size of the panel and no bigger is a panel whose shadow has nowhere to go: the window's corners
    // are square, the panel's are round, and what shows in the four gaps is the shadow that was supposed
    // to be cast *onto the page* -- a dark wedge inside each corner, cut off along the window's edge. A
    // flyout or a dialog never meets this because it is drawn inside a window it does not size; a tip is
    // its own window, so it pays for its own shadow. See `Painter::Shadow` for the two numbers.
    static constexpr float kShadowReach = 8.0f;
    static constexpr float kShadowDrop = 2.0f;
    static constexpr float kMargin = kShadowReach + kShadowDrop;

    // Show `s` about the box `anchor`, with the hand at `at` -- both in **screen pixels**, the same thing
    // `ClientToScreen` answers with, and the only unambiguous points there are on a machine with two
    // screens. Which of the two the box is placed by is `follow`: under the box and centred on it, or
    // below and to the right of the hand. `beside` overrides the first of those for a tip about one row
    // of a column of them -- see `Widget::Tip`. And it **flips to the other side when the work area runs
    // out**, which is not a nicety -- a control in the bottom right corner of a screen is where the tip
    // would otherwise be half off it, and that corner is where controls really are -- and it is then kept
    // inside the work area whatever happens.
    void For(const std::wstring &s, const RECT &anchor, POINT at, TipFollow follow, bool beside = false) {
        Fonts &f = CurrentFonts();
        // The size is the words and the padding around them, and the height is *measured* rather than
        // assumed: a line box is not the point size, and `Fonts` keeps the answer for anything that
        // repeats. The interior is what has to be measured at, and it is the same number the drawing
        // below wraps at, so that the two cannot disagree about how many lines there are.
        const float inner = std::ceil((std::min)(f.Measure(f.caption, s), kMaxTextW));
        const float pw = inner + kPadX * 2.0f;
        const float ph = std::ceil(f.WrappedHeight(f.caption, s, inner)) + kPadY * 2.0f;

        // DIPs of the monitor the tip is landing on, which is asked of the monitor rather than of this
        // window: on a machine with two screens at two DPIs the point decides, and `Show` creates the
        // window in the same DIPs. See `dpiapi::ForPoint`.
        //
        // **Which point decides is the one the box hangs off.** A control is on the screen its own box
        // is on, so the middle of that box is what is asked: a tip about a control on the second screen
        // would otherwise be laid out for the first one's DIPs, and be the wrong size for its own text.
        const POINT ref = follow == TipFollow::Control
                              ? POINT{ (anchor.left + anchor.right) / 2, (anchor.top + anchor.bottom) / 2 }
                              : at;
        const UINT mon = dpiapi::ForPoint(ref);
        const float scale = (float)mon / 96.0f;
        RECT work = {};
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(MonitorFromPoint(ref, MONITOR_DEFAULTTONEAREST), &mi);
        work = mi.rcWork;
        // **And the work area it is kept in is the work area less the room its own shadow needs.** A tip
        // pushed against the edge of a screen would otherwise be a panel sitting on the last pixel --
        // rounding a DIP into a pixel is enough to put one of them outside -- with its shadow cut off
        // along the bezel, and a shadow must end by fading rather than by stopping. The margin the window
        // already carries is exactly that room, so the whole of the tip lands on the screen, shadow
        // included, and the gap left over is what a control at the very edge of a screen reads as.
        const float left = (float)work.left / scale + kMargin, top = (float)work.top / scale + kMargin;
        const float right = (float)work.right / scale - kMargin;
        const float bottom = (float)work.bottom / scale - kMargin;

        float x = 0.0f, y = 0.0f;
        if (follow == TipFollow::Pointer) {
            const float hx = (float)at.x / scale, hy = (float)at.y / scale;
            x = hx + kOffset;
            y = hy + kOffset;
            if (x + pw > right) x = hx - kOffset - pw;
            if (y + ph > bottom) y = hy - kOffset - ph;
        } else if (beside) {
            // **Beside the box rather than under it.** A tip about one row of a column -- a rail of icons
            // -- has the next row under every row, so a box hanging below the one being pointed at lands
            // on the next one and hides the thing the hand is about to go to. Centred on the row and off
            // to its right, and flipped to the left when the work area runs out, which is the same flip
            // the other two placements make and the side WinUI's own compact pane puts its tips on.
            const float aL = (float)anchor.left / scale, aT = (float)anchor.top / scale;
            const float aR = (float)anchor.right / scale, aB = (float)anchor.bottom / scale;
            y = (aT + aB - ph) * 0.5f;
            x = aR + kGap;
            if (x + pw > right) x = aL - kGap - pw;
        } else {
            const float aL = (float)anchor.left / scale, aT = (float)anchor.top / scale;
            const float aR = (float)anchor.right / scale, aB = (float)anchor.bottom / scale;
            x = (aL + aR - pw) * 0.5f;
            y = aB + kGap;
            if (y + ph > bottom) y = aT - kGap - ph;
        }
        // And inside it whatever happens: a control at the very edge of a screen has a tip that fits
        // nowhere, and a tip half off the screen is worse than one over the control it names. Both bounds
        // are the work area's already, less the shadow's margin -- see above.
        x = (std::max)(left, (std::min)(x, right - pw));
        y = (std::max)(top,  (std::min)(y, bottom - ph));

        const int wx = (int)std::lround(x - kMargin), wy = (int)std::lround(y - kMargin);
        const int ww = (int)std::lround(pw + kMargin * 2.0f);
        const int wh = (int)std::lround(ph + kMargin * 2.0f);
        // **Being asked for the same thing again is not work.** A page, a hover that flaps, or a caller that
        // asks every frame all arrive here with the same words and the box that came out of them, and
        // re-placing the window is the least of what would happen: the entrance below is a fade, and one
        // restarted on every ask is a tip that blinks. It is the words and the box that are compared, which is
        // everything the drawn tip is.
        if (hwnd && Shown() && s == text && wx == atX && wy == atY && ww == atW && wh == atH) return;
        text = s;
        atX = wx;
        atY = wy;
        atW = ww;
        atH = wh;
        // **And the entrance is for arriving.** A tip that is already up and is being put somewhere else --
        // the hand has drifted, the page has moved -- keeps the opacity it has rather than fading again from
        // nothing. Only a tip that is not on the screen is arriving.
        // See `Popup::Arrival`.
        if (!Shown()) Arrive();
        if (!hwnd) Show(wx, wy, ww, wh, mon);
        else {
            Place(wx, wy, ww, wh);
            if (!Shown()) ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        }
        Invalidate();
    }

    // The panel, drawn rather than laid out: a shadow, an opaque fill and a contour, which is what a
    // surface over the page is made of -- and a narrower shadow than a flyout's, because a tip is the
    // lowest thing that floats there. See `Flyout::Paint` for the panel a control hangs off.
    void PaintFurniture(const Painter &p) override {
        if (text.empty()) return;
        // **The whole tip, faded as one.** There is no tree here to leave an opacity on, so the fade is the
        // colours themselves -- the shadow's own `opacity` is the parameter it takes for exactly this ("a
        // surface that fades while its shadow stays is the one thing that gives a fade away"), and the fill,
        // the contour and the words are faded with `Fade`. See `Popup::Arrival`.
        const float t = Arrival();
        // The panel is the window less the room the shadow needs -- see `kMargin`. Drawn inset rather than
        // drawn whole, and that is the fix for the corners: the four gaps between a square window and a
        // round panel are the shadow's to fade into rather than a wedge of its own.
        const D2D1_RECT_F box = { kMargin, kMargin, ClientW() - kMargin, ClientH() - kMargin };
        p.Shadow(box, metric::kRadiusControl, t, kShadowReach, kShadowDrop, 14, 0.18f);
        p.FillRound(box, metric::kRadiusControl, Fade(p.pal->flyoutBg, t));
        p.StrokeRound(box, metric::kRadiusControl, Fade(p.pal->flyoutStroke, t));
        p.TextWrapped(text, { box.left + kPadX, box.top + kPadY,
                              box.right - kPadX, box.bottom - kPadY },
                     p.font->caption, Fade(p.pal->textPrimary, t));
    }
};

// What decides when the tip is there. Attached to a surface, and it is that surface's hover it watches:
// `Surface::onHoverMoved` is the one hook a tip needs, because a tip is not in the tree and so nothing
// in the tree can tell it anything.
//
// **The tip lives as long as this object, and it is one tip per `Tips`.** Showing the same box for a
// different control is a `For` call with different words, so the window is made once and moved; the
// dwell is a one-shot timer that is stopped the moment the pointer leaves, which is what makes the
// hand-off from one control to the next a hide rather than a race between two timers.
//
// **And while it is up, the tip checks that the pointer is still there.** A window is told when the
// pointer leaves only if it asked, and the asking is thrown away whenever *any* window appears under the
// pointer -- which is what showing a tip is. A move inside the window asks again; a pointer that has left
// the window sends nothing at all, so a tip that only listened would sit over a control nobody is
// pointing at until the next mouse move inside that window, which may never come. Asking the pointer
// where it is, four times a second while a tip is on screen, is the whole of the answer to that.
//
// **Where the box goes is `TipFollow`**, and the default is the control it is about rather than the hand.
struct Tips {
    explicit Tips(Surface &s, UINT dwellMs = 0)
        : surface(s), dwell(dwellMs ? dwellMs : TipDwell()) {
        s.onHoverMoved = [this](Widget *was, Widget *now) { Moved(was, now); };
    }
    ~Tips() {
        // **The hook is the surface's, and this object is the page's.** A window that outlives its own
        // members -- which is what a page's member of a window is, destroyed before the window it sits
        // in but after the loop has run -- would otherwise leave the surface calling into freed memory on
        // the next mouse move.
        surface.onHoverMoved = nullptr;
        timer.Stop();
        watch.Stop();
        tip.Hide();
    }
    Tips(const Tips &) = delete;
    Tips &operator=(const Tips &) = delete;

    // The box, for a page that wants to draw something else in it -- a subclass of `Tip` put here before
    // the first hover, which is when the window is made.
    Tip *Box() { return &tip; }
    // How long the pointer has to rest. Setting it does not restart a dwell that is already running.
    void Dwell(UINT ms) { dwell = ms; }
    // What the box hangs off -- see `TipFollow`. Set it before the first hover, like a `Tip` subclass: a
    // tip that is already up stays where it is.
    void Follow(TipFollow f) { follow = f; }

private:
    void Moved(Widget * /*was*/, Widget *now) {
        // Whatever the pointer left, a tip about it is wrong now: the box goes at once rather than after
        // a fade, because the next tip is a different sentence about a different control and the two
        // must not be on the screen together.
        timer.Stop();
        watch.Stop();
        waiting = nullptr;
        tip.Hide();
        if (!now || !surface.app) return;
        // **Nothing to say *here* is not the same as nothing to say.** A widget whose tip depends on the
        // point -- a rail of icons, whose words are a row's name -- says the same thing about itself
        // everywhere and something different about each row, and nothing at all about a row whose name is
        // already drawn beside it. This is the same hook that a row change comes through, see
        // `Surface::TipChanged`: the answer was different, so the question is asked again and the dwell
        // starts over, which is what makes moving from one icon to the next a fresh wait rather than the
        // tail of the last one.
        const D2D1_POINT_2F local = surface.LocalPoint(now, surface.pointerX, surface.pointerY);
        if (now->TipAt(local.x, local.y).text.empty()) return;
        // A control that is not on the screen -- scrolled out of the container that shows it, or inside
        // a panel that has not arrived -- says nothing about itself: `VisibleArea` is what every other
        // question about a widget asks, and this is one of them.
        const D2D1_RECT_F room = now->VisibleArea();
        if (room.right <= room.left || room.bottom <= room.top) return;
        waiting = now;
        timer.Start(&surface, dwell, [this] { Appear(); });
    }

    void Appear() {
        // **The dwell is over, and it was a one-shot.** A Windows timer repeats until it is stopped -- see
        // `Timer` -- so one left running calls this again every `dwell` milliseconds, which is a tip that is
        // re-placed, and re-*faded* once it has an entrance, four times a second: a box that blinks on a
        // regular beat while the pointer rests on the control it is about. It is the same call that the
        // watchdog below is started by, and the two are the pair the tip runs on from here: nothing asks
        // again whether it should appear, and the watch is what asks whether it should go.
        timer.Stop();
        // The pointer may have moved off while the timer was running... the timer is stopped on the way
        // out (see `Moved`), so this is the case where nothing told us -- a widget that went away with the
        // pointer still nominally on it.
        if (!waiting || !waiting->hover) { waiting = nullptr; return; }
        // Where the hand is, in screen pixels. The surface's own memory of the pointer rather than the
        // cursor's position, for the reason `Surface::pointerX` exists: on a touchscreen the two are not
        // the same place, and the hover this tip is about came from a pointer that is not the mouse.
        POINT p = { (LONG)(surface.pointerX * surface.scale()),
                    (LONG)(surface.pointerY * surface.scale()) };
        ClientToScreen(surface.hwnd, &p);
        // **What the tip says is asked of the point, not of the widget**, which is the same thing for a
        // control that is one thing and a different row for one that draws a set of them. The box the tip
        // is about comes back with it, in the space a pointer hook is handed, which is exactly what
        // `ScreenBox` takes -- so a tip about the fourth row of a rail hangs off the fourth row and
        // travels with it, placement and animation and all.
        const D2D1_POINT_2F local = surface.LocalPoint(waiting, surface.pointerX, surface.pointerY);
        const Widget::Tip what = waiting->TipAt(local.x, local.y);
        if (what.text.empty()) { waiting = nullptr; return; }
        anchor = surface.ScreenBox(waiting, what.box);
        tip.For(what.text, anchor, p, follow, what.beside);
        // **And the window the tip is has just taken the leave-tracking away.** Windows cancels a
        // window's WM_MOUSELEAVE ask when any window appears under the pointer, so a tip that shows
        // while the pointer is resting on a control is a tip that will never hear that the pointer has
        // gone. Asking again covers the moves that come next; the check below covers the case this one
        // cannot, which is a pointer that has already gone and will send nothing at all.
        surface.TrackLeave();
        // **And the tip needs a loop to be painted in.** A `Popup` is a window like any other, and one
        // nothing pumps is a window that never draws. Adding it is the same call a menu makes, and
        // `App::Add` is idempotent -- the tip is added on the first hover of a window and stays in the
        // list for as long as the window does, which is what makes the second hover cheap.
        if (!tip.app) surface.app->Add(tip);
        watch.Start(&surface, kTipWatchMs, [this] { StillThere(); });
    }

    // Is the pointer still on the control this tip is about? Asked on a timer while the tip is up, and
    // the answer being no means the same thing the hover hook would have said: the tip goes.
    void StillThere() {
        if (!waiting || !surface.hwnd) { watch.Stop(); return; }
        POINT p = {};
        if (!GetCursorPos(&p)) return;
        // **And the pointer has to be over this window, not only over the control's box.** A tip is
        // topmost, which is how it stays in front of the window it belongs to -- and topmost is also what
        // would keep it in front of a window that has taken the foreground away: alt-tabbing leaves the
        // pointer resting exactly where it was, on a control of a window nobody is looking at, and the box
        // would hang there over whatever came to the front. Which window the pointer is over is the same
        // question as whether that control is still worth a tip, and it is the one with an answer; a tip
        // the pointer is over is this one, so it is not an intruder.
        const HWND over = WindowFromPoint(p);
        if (over != surface.hwnd && over != tip.hwnd) { Moved(waiting, nullptr); return; }
        // **And still on the box this tip is about**, which for a control that is one thing is the whole of
        // it and for a rail is the one row the words name. A pane whose rows have scrolled under a resting
        // pointer is a tip about a row that is no longer there, and this is what notices.
        const RECT box = anchor;
        if (p.x < box.left || p.x >= box.right || p.y < box.top || p.y >= box.bottom) {
            Moved(waiting, nullptr);
            return;
        }
        // **And it is still about what it is about.** What a point answers with can change with nothing
        // moving at all: a pane peeking open under a resting pointer draws the labels the tip was standing
        // in for, and a box of words repeating the word now written beside the cursor is exactly what a tip
        // must not be. Asked here because this is the only thing running while the pointer rests -- the
        // pointer moving is `Moved`'s way in, and a wheel or a scroll under a still hand sends nothing.
        const D2D1_POINT_2F local = surface.LocalPoint(waiting, surface.pointerX, surface.pointerY);
        if (waiting->TipAt(local.x, local.y).text != tip.text) Moved(waiting, nullptr);
    }

    Surface &surface;
    UINT dwell;
    TipFollow follow = TipFollow::Control;
    Timer timer;
    Timer watch;   // while a tip is up: is the pointer still there?
    Tip tip;
    Widget *waiting = nullptr;
    // The box the tip on the screen is about, in screen pixels: what the watchdog checks the pointer is
    // still inside, and it is the box the words were placed against rather than the widget's own.
    RECT anchor = {};
};

}  // namespace micula
