// Micula / drop_down.h

#pragma once

#include "scroll_bar.h"
#include "window.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace micula {


struct DropDown : Widget {
    std::vector<std::wstring> options;
    int selected = 0;
    std::function<void(int)> onChange;
    bool open = false;
    float rowH = 32.0f;
    // Whether the choice is a ring. Off, the list has two ends, and a step past one of them
    // has nowhere to go -- which is what the knock is for. On, the step past the last option
    // arrives at the first: the wheel's, Up and Down's, and either of them while the list is
    // closed as well as open, because a control whose keys and wheel disagree about its ends
    // is a control with two answers. The list's own scroll bar is not part of this: a bar has
    // two ends by definition, and so does the room the popup is shown through.
    bool wrapAround = false;
    // How far the lid is open: 0 is the control's own row and nothing else, 1 is the whole
    // popup. Fluent expands a flyout out of the control it belongs to rather than blinking
    // it on, and on the way out it collapses back into it rather than vanishing -- which is
    // why the control stays raised while this runs down.
    //
    // Two curves, because Fluent has two: in on "Fast Out, Slow In", out on "Slow Out,
    // Fast In". A flyout arriving settles; a flyout leaving gets out of the way.
    motion::Track openT;

    DropDown(std::vector<std::wstring> opts, int sel, std::function<void(int)> f)
        : options(std::move(opts)), selected(sel), onChange(std::move(f)) {}
    // A list thrown away while open -- the page laid out again under it -- must not leave
    // its bar's timers firing at the window with nobody to answer them.
    ~DropDown() override {
        // The list's own scroll bar goes with it, and its timers go with the bar: see Timer.
    }

    // The control's own row. While the list is open `rect` grows to cover the popup, so
    // the head cannot be derived from `rect` any more.
    D2D1_RECT_F head = {};
    // The rows' margin inside the popup, top and bottom. WinUI's
    // ComboBoxDropdownContentMargin, of which the 4 DIPs that show are the part that
    // matters here.
    static constexpr float kPad = 4.0f;
    // Where the popup comes to rest: worked out when it opens, from the chosen row, and
    // then left alone -- the popup does not move while it is being scrolled through, the
    // *rows* do. See Place().
    D2D1_RECT_F frame = {};
    // Where the panel is, in rows: 0 has the first option on the control's own row, 3 has
    // the fourth there. It trails `selected` so the list slides to a new choice rather than
    // jumping, and it is what the panel -- and every row in it -- is drawn from. The list
    // moves past the control as a whole; nothing scrolls inside the panel, and no marker
    // travels down a still one.
    float slid = 0.0f;
    // How long the slide takes to close most of its gap, in seconds. A follower rather than
    // a curve with a duration, like the page's scroll and the segmented indicator's block:
    // a spun wheel retargets this several times inside one frame, and a storyboard restarted
    // that often would stutter between the notches.
    static constexpr float kSlideLag = 0.05f;
    // How far the mark has drifted off the control, in rows, followed at the same lag. The
    // clamp is a step -- it engages the frame the panel would leave the strip -- and a mark
    // that took that step in one frame reads as the bar having moved to another option rather
    // than as the panel having moved under it. Everything else about the mark's place is a
    // step as well, by design; this is the one quantity that gets to travel.
    float drift = 0.0f;
    // Its scroll bar, the page's own control: WinUI's drop-down is a ScrollViewer, and
    // the bar in it is the one every other ScrollViewer has. Made the first time a list
    // needs one, not for every drop-down on every layout.
    std::unique_ptr<ScrollBar> bar;
    // The press went down on the bar, so letting go is not choosing a row.
    bool barGrab = false;

    // Typing to find an option. The control is a list of words, so the keyboard can be a
    // search: the letters go into a prefix, the option that starts with it is chosen, and an
    // open list slides to it. Windows' own combo boxes do this, and it needs no field, no
    // layout and nothing drawn -- the chosen option's label is already on the control.
    //
    // The prefix is forgotten after a second of quiet, and whenever the list opens or closes:
    // it is a way of pointing at one option, not a query that stays.
    std::wstring typed;
    ULONGLONG typedAt = 0;
    static constexpr ULONGLONG kTypeWindow = 1000;   // ms

    // What a letter that found nothing does. A control that swallows a key in silence looks
    // broken, and the window's answer to a key nobody took is a beep -- a complaint from the
    // operating system rather than from the thing that did not move. So the mark is the
    // answer: it shrinks for a moment and springs back. Heard, and nowhere to go with it.
    //
    // Full at the instant of the refusal and decaying from there, which is the shape that
    // needs no state beyond the number itself: a curve with a duration would have to be
    // turned round at the end of itself to come back.
    float refuse = 0.0f;
    static constexpr float kRefuseLag = 0.07f;       // seconds

    // And what a gesture that had nowhere to go does -- the wheel, or Up and Down, at the end
    // of the list. A different shape, because it is a different thing to say: not "no", but
    // "that way, and no further". The edge the gesture is going towards twitches quickly and a
    // short way and holds there; the other edge follows it, more slowly and further, so the
    // mark is *shorter* for as long as either of them is out. Then both spring back.
    float knock = 0.0f;        // the fast edge's impulse, 0..1
    float knockLag = 0.0f;     // and the edge that follows it, which goes further
    int knockDir = 0;          // -1 up a list, +1 down it
    bool knockHeld = false;    // still in the rise, which is where the two differ
    static constexpr float kKnockRise = 0.03f;    // seconds for the fast edge to arrive
    // And the time constant for the edge behind it, which is an approach rather than a ramp
    // and so is *fastest* in its first instant: three of these is 95 % of the way, which is
    // the tenth of a second a ramp over the same distance used to take.
    static constexpr float kKnockFollow = 0.03f;
    static constexpr float kKnockLag = 0.12f;     // and for both to spring back
    static constexpr float kKnockTip = 2.0f;      // DIPs the fast edge moves
    static constexpr float kKnockShove = 6.0f;    // and the edge that follows it

    static bool StartsWith(const std::wstring &s, const std::wstring &prefix) {
        if (prefix.size() > s.size()) return false;
        for (size_t i = 0; i < prefix.size(); i++)
            if ((wchar_t)std::towlower(s[i]) != prefix[i]) return false;
        return true;
    }

    bool Focusable() const override { return true; }
    bool TracksPointer() const override { return open; }
    D2D1_RECT_F Head() const { return open ? head : rect; }

    // The room a list may take: the page's visible strip, or on a window whose page does
    // not scroll, the window under its caption -- which is painted last, over everything.
    D2D1_RECT_F Bounds() const {
        if (!owner || !owner->hwnd) return D2D1_RECT_F{ 0, 0, 0, 0 };
        // In this control's own space rather than the window's, which on a scrolling page
        // are not the same thing: the strip the page shows moves with the scroll while the
        // control's rectangle stays where the layout put it. Measuring against the
        // window's rectangle instead made a control halfway down the page think it had the
        // room the top of the page had, and open downward off the bottom of it.
        const D2D1_RECT_F clip = VisibleArea();
        if (clip.bottom > clip.top) return clip;
        // No scrolling area: the window under its caption, in this control's space too,
        // because the page may still be drawn through a transform.
        float dy = 0.0f, op = 1.0f;
        if (scrolls) owner->ContentTransform(&dy, &op);
        return D2D1_RECT_F{ 0, kCaptionH - dy, owner->ClientW(), owner->ClientH() - dy };
    }
    float FullHeight() const { return rowH * (float)options.size() + 2 * kPad; }

    // The y a row has to be at to be over the control's own row: the two boxes centred on
    // each other, which for a row and a control of the same height is the two boxes on top
    // of each other -- and is why the popup reads as a lid closing over the control.
    float RowLine() const { return (Head().top + Head().bottom) / 2 - rowH / 2; }
    // `slid`, kept inside the strip the page shows the panel through.
    //
    // Laying the list out so the chosen row is on the control's own row is the whole point
    // of opening over the control -- but it is not worth rows the pointer cannot reach. A
    // control near the top or the foot of a scrolling page puts part of the panel outside
    // `ClipRect`, and outside it is not merely out of sight: the page cuts it mid-row, and
    // `Window::HitTest` refuses a scrolling widget outside the clip, so a click on the half
    // that is drawn falls through to whatever is behind and dismisses the popup. A row that
    // is visible and dead is worse than a row that is gone.
    //
    // So the panel slides back inside, and the chosen row drifts off the control by however
    // much that took. WinUI clamps for the same reason. This control did not, deliberately
    // -- see the note on FrameAt -- on the grounds that what hangs off is the far end of the
    // list and that is the end to lose. That holds when the thing doing the cutting is the
    // edge of the screen. It does not when it is the top of a page with a header above it.
    //
    // Two bounds, in rows, one from each edge. A list taller than the strip is the same pair
    // with the interval the other way round: then the panel has to *cover* the strip rather
    // than fit inside it, and the scroll bar is what reaches the rest.
    float PlacedAt(float k) const {
        const D2D1_RECT_F b = Bounds();
        if (b.bottom <= b.top) return k;
        const float fit  = (RowLine() - kPad - b.top) / rowH;
        const float full = (RowLine() - kPad + FullHeight() - b.bottom) / rowH;
        return std::clamp(k, (std::min)(fit, full), (std::max)(fit, full));
    }
    // Where the panel actually is. Everything that draws it or hit-tests it reads this and
    // not `slid`, because the two disagreeing is the defect above.
    float Placed() const { return PlacedAt(slid); }

    // Where a row is drawn. The panel's place in rows is `Placed()`, so every row moves with
    // it, which is the whole of the difference between this and a list that scrolls.
    float RowTop(int i) const { return RowLine() + rowH * ((float)i - Placed()); }

    // The panel: the whole list, laid out so that the chosen row is on the control's own
    // row, and moved as a whole when the choice moves.
    //
    // **This is what a Windows 11 combo box does**, and it is not "open below and flip when
    // short of room": the popup covers the control with the chosen item on it, so the two
    // names are in the same place and the choice reads as a swap rather than as a menu. The
    // rule is one line of ComboBox::GetNonPannablePopupLayout -- the chosen item is laid out
    // at `cbY + cbHeight/2 - itemHeight/2 - margin.Top` -- applied again after every step,
    // which is what makes the wheel feel like a dial under the control rather than a menu
    // being dragged about.
    //
    // Pure geometry: where the panel would be for a place of `k` rows, with nothing said
    // about whether that is somewhere the page can show. `Placed()` is what decides that,
    // and every caller here goes through it.
    //
    // It used to be called with `slid` and `selected` raw, on the grounds that a panel
    // hanging off the strip loses only the far end of the list. `PlacedAt` says why that
    // was wrong when the strip is a page rather than a screen.
    D2D1_RECT_F FrameAt(float k) const {
        const float top = RowLine() - kPad - rowH * k;
        return { Head().left, top, Head().right, top + FullHeight() };
    }
    D2D1_RECT_F Frame() const { return FrameAt(Placed()); }                   // as drawn
    D2D1_RECT_F Rest() const { return FrameAt(PlacedAt((float)selected)); }   // where it belongs
    // Everything that follows from the chosen row: the area the mouse can reach and the bar.
    // The rows themselves need no telling -- RowTop reads `slid`.
    //
    // Only while the list is open. Closed there is no popup to reach into, and `head` is
    // whatever the last one left behind -- or nothing at all, if there has never been one --
    // so a choice made from the keyboard while closed would have rewritten the control's own
    // rectangle out of a popup that does not exist. SetOpen puts all of this right before the
    // list is seen again.
    void Sync() {
        if (!open) return;
        const D2D1_RECT_F f = Rest();
        rect = { head.left, (std::min)(head.top, f.top),
                 head.right, (std::max)(head.bottom, f.bottom) };
        SyncBar();
    }
    // The popup as it is drawn *now*: it grows out of the control's own row, each edge
    // that has somewhere to go travelling from that row to where it ends up. Up and down
    // both when the list reaches both ways, which is the ordinary case -- the chosen row
    // sits over the control and its neighbours arrive from under it.
    //
    // WinUI does this with SplitOpenThemeAnimation, whose ClosedLength, OpenedLength and
    // OffsetFromCenter are the three numbers ComboBoxTemplateSettings hands it for exactly
    // this purpose.
    D2D1_RECT_F Shown() const {
        const D2D1_RECT_F f = Frame();
        const float k = openT.value;
        const float top = f.top < head.top ? head.top + (f.top - head.top) * k : head.top;
        const float bot = f.bottom > head.bottom
                              ? head.bottom + (f.bottom - head.bottom) * k
                              : head.bottom;
        return { head.left, top, head.right, bot };
    }
    // Taller than the room it is seen through, which is the only thing the bar is for.
    bool Overflows() const { return FullHeight() > Height(Bounds()) + 0.5f; }

    // The chosen row, by the wheel, the keys, a click or the bar. The panel's place and the
    // bar come out of it -- see Sync -- so there is nothing here to keep in step by hand.
    //
    // Returns whether the choice actually moved, which is what tells a gesture that had
    // nowhere to go from one that had: see Knock.
    bool Select(int i) {
        const int n = (int)options.size();
        if (n <= 0) return false;
        i = std::clamp(i, 0, n - 1);
        const bool moved = (i != selected);
        if (moved) {
            selected = i;
            Sync();
            if (onChange) onChange(selected);
        }
        if (BarShown()) { bar->Wake(); bar->Poll(); }
        if (owner) owner->Invalidate();
        return moved;
    }
    // One step of the choice, which is what the wheel, Up and Down and the bar's arrows all
    // amount to. Past either end it wraps when `wrapAround` is set and otherwise goes
    // nowhere -- Select clamps, so a step that ran off an end is the option it started from
    // and reports that nothing moved, which is what the callers read as nowhere to go.
    bool Step(int dir) {
        const int n = (int)options.size();
        if (n <= 0) return false;
        const int want = selected + dir;
        if (!wrapAround) return Select(want);
        return Select(((want % n) + n) % n);
    }
    // A gesture with nowhere to go -- the wheel or Up and Down at the end of the list -- is
    // answered by the mark giving way the way it was pressed and coming back. `dir` is +1 for
    // down a list, -1 for up it, which is the direction the gesture was going.
    void Knock(int dir) {
        knock = 0.0f;
        knockLag = 0.0f;
        knockHeld = true;
        knockDir = dir;
    }
    // The bar, which works in DIPs while the list works in rows. A step smaller than a row
    // still moves a row: an arrow that did nothing would be an arrow that is broken.
    void ScrollTo(float to, bool /*glide*/) {
        if (!open) return;
        const float at = Placed() * rowH;
        Select(to < at ? (int)std::floor(to / rowH) : (int)std::ceil(to / rowH));
    }
    // The bar's geometry. It belongs to the list, so it travels with the panel -- but its
    // track is only drawn where the page can show it, and what it reports is the chosen
    // row's place in the list, which is the only thing about this list that moves.
    void SyncBar() {
        if (!bar) return;
        const D2D1_RECT_F f = Frame();
        const D2D1_RECT_F b = Bounds();
        bar->rect = { f.right - 2 - ScrollBar::kSize, (std::max)(f.top, b.top) + 1,
                      f.right - 2, (std::min)(f.bottom, b.bottom) - 1 };
        bar->area = f;
        bar->viewport = Height(b);
        bar->extent = FullHeight();
        bar->value = (std::min)((std::max)(0.0f, Placed() * rowH),
                                (std::max)(0.0f, FullHeight() - Height(b)));
        bar->drawn = bar->value;
    }
    bool BarShown() const { return open && bar && bar->visible; }

    // Which option is at this point. -1 for anything outside the panel as it is drawn right
    // now -- a row the panel has not slid over yet is not there -- and for the bar.
    //
    // Boxed in on all four sides, and it has to be: this is the box the pointer is in when a
    // row is lit, and it is not the window. The sides matter because the popup is narrow --
    // the control's own width -- and the height alone lit a row for a pointer resting beside
    // it, on the card's title a hand's width away. `Bounds` matters for the same reason one
    // step out: the lid reaches over the title bar whenever the chosen row is low enough in
    // the list, and a pointer dragging the window sits exactly there. Rows the page has cut
    // away are not lit either, because they are not drawn -- see Bounds for the strip.
    int RowAt(float x, float y) const {
        const D2D1_RECT_F s = Shown();
        if (x < s.left || x >= s.right) return -1;
        if (y < s.top || y >= s.bottom) return -1;
        if (!Inside(Bounds(), x, y)) return -1;
        if (BarShown() && Inside(bar->rect, x, y)) return -1;
        const int i = (int)std::floor((y - RowLine()) / rowH + Placed());
        return (i >= 0 && i < (int)options.size()) ? i : -1;
    }

    void SetOpen(bool o) {
        barGrab = false;
        // A list being opened or closed is a fresh gesture at the keyboard.
        typed.clear();
        if (o) {
            head = { rect.left, rect.top, rect.right, rect.top + metric::kControlH };
            open = true;
            z = 1;
            // Opened with the chosen row already on the control: nothing to slide.
            slid = (float)selected;
            // And the mark starts out where it belongs rather than travelling there on the
            // way in: the panel is arriving, and one thing arriving at a time is enough.
            drift = (float)selected - PlacedAt((float)selected);
            const bool cut = Overflows();
            if (cut) {
                if (!bar) {
                    bar = std::make_unique<ScrollBar>(
                        [this](float to, bool glide) { ScrollTo(to, glide); });
                }
                bar->owner = owner;
                bar->visible = true;
            }
            // The whole of it takes the mouse, or a click on an option falls through to
            // whatever is behind the list.
            Sync();
            if (cut) {
                // The line shows at once: the pointer is on the control, not over the
                // list, so nothing else would wake it.
                bar->Wake();
                bar->Poll();
            }
        } else {
            open = false;
            // Stays raised while the lid closes, and Tick drops it back to 0 when that is
            // done. Nothing is stolen by that: the rect has gone back to the control's own
            // row, so the hit test cannot reach the rows even though they are still being
            // drawn.
            z = 1;
            if (head.right > head.left) rect = head;
            // Its timers go with it: the bar's own, which stop with the bar. See Timer.
            if (bar) {
                bar->OnRelease();
                bar->hover = false;
                bar->visible = false;
                bar->Poll();
            }
        }
    }
    void Dismiss() override { if (open) SetOpen(false); }
    bool Animating() const override {
        return Widget::Animating() || openT.Wants(open ? 1.0f : 0.0f) ||
               (open && slid != (float)selected) ||
               (open && drift != (float)selected - PlacedAt((float)selected)) ||
               refuse > 0.0f || knockHeld ||
               knock > 0.0f || knockLag > 0.0f ||
               (BarShown() && bar->Animating());
    }
    void Tick(float dt) override {
        Widget::Tick(dt);
        openT.To(open ? 1.0f : 0.0f);
        if (open) {
            openT.Step(dt, motion::kFast, motion::Decel);
            // The list slides while the popup is open, and only then. A click on an option
            // closes the popup on the release, and a panel that then slid its way to the
            // option it had just chosen would be moving the list *while the lid closed over
            // it* -- two motions where there is room for one, and the slide is the one that
            // loses, because it cannot finish. Closed, the panel is left where it was;
            // SetOpen puts it on the chosen row before it is seen again.
            const float want = (float)selected;
            if (slid != want) {
                slid += (want - slid) * (1.0f - std::exp(-dt / kSlideLag));
                if (std::fabs(want - slid) < 0.004f) slid = want;
                SyncBar();
            }
            // The mark's own correction: the clamp moved, the panel did not travel on its
            // account, and so the mark is the only thing that has to keep up with where the
            // chosen row came to rest. Same lag as the slide, because it is one motion -- a
            // correction that took longer than the list's own move would read as the mark
            // lagging behind the row it is on.
            const float driftWant = (float)selected - PlacedAt((float)selected);
            if (drift != driftWant) {
                drift += (driftWant - drift) * (1.0f - std::exp(-dt / kSlideLag));
                if (std::fabs(driftWant - drift) < 0.004f) drift = driftWant;
            }
        } else if (!openT.Step(dt, motion::kFast, motion::Accel)) {
            z = 0;          // the lid has finished closing; stop keeping it raised
        }
        // The refusal, if there is one: an impulse that fades, so a letter with nowhere to go
        // is answered for about a fifth of a second and then is not.
        if (refuse > 0.0f) {
            refuse *= std::exp(-dt / kRefuseLag);
            if (refuse < 0.002f) refuse = 0.0f;
        }
        // And the knock at the end of the list: the fast edge out in a thirtieth of a second
        // and held, the one behind it following in three times that, and both springing back
        // in about a fifth. A lean and a spring rather than a move.
        if (knockHeld) {
            // Both edges set off on this frame, and everything the gesture shows comes of
            // that. The edge behind goes three times as far, so a ramp for it -- three times
            // the distance in three times the time -- would run at exactly the fast edge's
            // rate, and the two of them would move as one for the first thirtieth of a second
            // while the mark slid bodily down and did not shorten at all; which is what this
            // was, and what made it read as the far edge starting late. An approach is fastest
            // in its first instant, so the mark begins losing length at once, and the fast
            // edge still arrives first because its distance is the short one.
            knock = (std::min)(knock + dt / kKnockRise, 1.0f);
            knockLag = 1.0f - (1.0f - knockLag) * std::exp(-dt / kKnockFollow);
            if (knock >= 1.0f && knockLag >= 0.95f) knockHeld = false;
        } else if (knock > 0.0f || knockLag > 0.0f) {
            knock *= std::exp(-dt / kKnockLag);
            knockLag *= std::exp(-dt / kKnockLag);
            if (knock < 0.002f && knockLag < 0.002f) { knock = knockLag = 0.0f; knockDir = 0; }
        }
        if (BarShown()) {
            SyncBar();
            // The bar is not in the window's list, so nothing else tells it the pointer
            // has left: this control's own hover going is what runs this frame.
            if (!barGrab) {
                const D2D1_POINT_2F at = Cursor();
                bar->hover = hover && Inside(bar->rect, at.x, at.y);
            }
            bar->Tick(dt);
        }
    }

    void OnClick() override { Pick(true); }
    void OnActivate() override { Pick(false); }
    // One path for both, because everything but the choice itself is the same: opening the
    // list, the bar's press, and the close. `byPointer` is the whole difference -- a click is
    // the pointer naming a row, and Space or Enter is naming nothing, which is the row the
    // accent mark has been left on. Reading the pointer for those chose whichever row happened
    // to be under a mouse that was resting somewhere else on the screen entirely.
    void Pick(bool byPointer) {
        if (!enabled) return;
        // A press on the list's bar scrolls it. It does not choose, and it does not close.
        if (barGrab) { barGrab = false; return; }
        if (!open) { SetOpen(true); return; }
        // Through the same function the drawing uses, so the row that was lit and the row
        // that is chosen cannot come apart.
        const D2D1_POINT_2F at = Cursor();
        const int i = byPointer ? RowAt(at.x, at.y) : selected;
        if (i >= 0) Select(i);
        SetOpen(false);
    }
    bool OnKey(WPARAM vk) override {
        // A bar drag that ended outside the control never reached OnClick; the key that
        // follows is not its release.
        barGrab = false;
        if (vk == VK_ESCAPE && open) { SetOpen(false); return true; }
        if (vk != VK_UP && vk != VK_DOWN) return false;
        const int n = (int)options.size();
        if (n <= 0) return false;
        // Open, a step goes through Select like the wheel's does, so the rows slide under
        // the control as the choice moves -- and a step that runs off an end is a gesture with
        // nowhere to go, which is the knock's. Closed, the label is the only thing that moves
        // and there is no mark on screen to give way, so the ends are silent.
        if (open) {
            const int dir = vk == VK_DOWN ? 1 : -1;
            if (!Step(dir) && !wrapAround) Knock(dir);
            return true;
        }
        Step(vk == VK_DOWN ? 1 : -1);
        return true;
    }
    // A printable character, from the keyboard or the IME. Always the control's, whether or
    // not it found anything: a letter that matched nothing and was passed on to the window
    // would be answered with a beep.
    //
    // **Only while the list is open.** A closed drop-down is a button with a label on it: it
    // has nothing to search in, and the mark that would show what the search found is not on
    // screen. Space opens it, and the search is there.
    bool OnChar(wchar_t ch) override {
        const int n = (int)options.size();
        if (!enabled || n <= 0) return false;
        if (!open) return true;      // consumed, so the window does not beep at it
        const ULONGLONG now = GetTickCount64();
        const wchar_t lower = (wchar_t)std::towlower(ch);
        // The same letter again steps to the next option that starts with it rather than
        // looking for the prefix it is already on. That is what Windows does, and it is the
        // only way to reach the second "Monthly" from the keyboard.
        const bool again = typed.size() == 1 && typed[0] == lower;
        if (again || now - typedAt > kTypeWindow) typed.clear();
        typedAt = now;
        typed.push_back(lower);
        // Where the search starts, and this is the whole of it: a first letter is a step, like a
        // notch of the wheel, and a step goes forward -- the next option that starts with it,
        // after the one chosen now. Every letter after it only refines an answer that has
        // already been given, so it starts *at* the chosen option, and the answer stays put for
        // as long as the option under it still starts with what has been typed. It moves on only
        // when that option has been typed out of the running.
        //
        // Starting after the chosen option every time is what this was, and it made every letter
        // a fresh errand: with five "Every ..." options in a row, E V E R Y walked through all
        // five of them, a letter each, so the name that was typed out was never the name that
        // ended up chosen. A search that walks a list while a name is typed into it is not what
        // Explorer does, and Explorer is the behaviour people arrive with.
        const int from = typed.size() > 1 ? selected : selected + 1;
        for (int step = 0; step < n; step++) {
            const int i = (from + step) % n;
            if (StartsWith(options[i], typed)) { Select(i); return true; }
        }
        // Nothing starts with it. Answered rather than ignored: see `refuse`.
        refuse = 1.0f;
        if (owner) owner->Invalidate();
        return true;
    }

    // The list's bar, driven from here: it is not one of the window's controls, because
    // the window's list is rebuilt on every layout and this bar lives as long as the list.
    // No offset to take off anywhere: the popup is drawn where it was placed, and it is
    // the rows that move inside it rather than it moving over them.
    void OnPress(float x, float y) override {
        barGrab = false;
        if (!BarShown()) return;
        SyncBar();
        if (!Inside(bar->rect, x, y)) return;
        barGrab = true;
        bar->hover = true;
        bar->OnPress(x, y);
    }
    void OnDrag(float x, float y) override {
        if (barGrab && BarShown()) bar->OnDrag(x, y);
    }
    void OnRelease() override {
        if (barGrab && BarShown()) bar->OnRelease();
    }
    void OnPointerMove(float x, float y) override {
        if (!BarShown()) return;
        SyncBar();
        if (!barGrab) bar->hover = hover && Inside(bar->rect, x, y);
        bar->OnPointerMove(x, y);
    }
    bool OnWheel(float x, float y, float notches) override {
        if (!open || !Inside(Shown(), x, y)) return false;
        // One row a notch, and the row that arrives at the control's own row is the choice.
        // A notch is a step through the items here and not a distance: this is a list of
        // things to choose, and a wheel that moved it 66 DIPs, as the page's does, would
        // leave the chosen row somewhere other than under the control it was chosen from.
        //
        // Taken even at either end. Passed on, it would scroll the page out from under an
        // open list -- and on a page that lays itself out in response to a scroll, take the
        // list away with it.
        const int step = (int)std::lround(-notches);
        const int dir = step != 0 ? step : (notches > 0.0f ? -1 : 1);
        // A ring has no end to run into, so there is nothing for the mark to give way to:
        // the step either moves the choice or has come all the way round to where it was.
        if (!Step(dir) && !wrapAround) Knock(dir > 0 ? 1 : -1);
        if (BarShown()) { bar->Wake(); bar->Poll(); }
        return true;
    }

    // The mark on the chosen row: the accent bar, which shrinks for a letter that found
    // nothing and gives way for a gesture that had nowhere to go. Drawn by the popup, which is
    // the only place it is: a closed drop-down has no mark, and nothing to search in either.
    //
    // Where the chosen row comes to *rest*, which is the control's own row whenever the panel
    // did not have to be moved to stay inside the page -- and then this is `RowLine()` to the
    // float, so nothing about the ordinary case changes. Resting position rather than current:
    // the mark holds still and the list travels into it, which is the motion this control has.
    // Following `RowTop(selected)` instead would carry the mark along with the list.
    //
    // It used to be `RowLine()` outright. That was the same number until PlacedAt began
    // clamping the panel into the strip, and then it was the wrong row -- the bar stayed on
    // the control while the choice it marks had moved, so it pointed at whichever option
    // happened to land there. Caught in a screenshot of the real page, not by the geometry
    // test, which had not thought to ask where the mark was.
    //
    // `drift` is that same resting place, followed instead of stepped: the clamp engages and
    // gives way as the choice crosses it, and a mark that jumped with it moved in a frame by
    // as much as a row. Following it keeps the panel still -- which is right, the clamp is a
    // decision about where the panel may be, not a motion -- and leaves the mark to travel.
    void PaintMark(const Painter &p, float alpha) {
        const D2D1_RECT_F h = Head();
        const float line = RowLine() + rowH * drift;
        const float inset = 8.0f + 4.8f * refuse;    // the refusal's own shrink
        // The knock, in DIPs of offset per edge. The fast edge is capped at what the edge
        // behind it has already given: the mark may be shorter than it is at rest and never
        // longer, so what the two of them do together reads as length rather than as travel.
        const float tip = (std::min)(kKnockTip * knock, kKnockShove * knockLag);
        const float shove = kKnockShove * knockLag;
        const float top = line + inset + (knockDir > 0 ? shove : -tip);
        const float bot = line + rowH - inset + (knockDir > 0 ? tip : -shove);
        p.rt->FillRoundedRectangle(
            D2D1::RoundedRect({ h.left + 1, top, h.left + 4, bot }, 1.5f, 1.5f),
            p.Brush(Fade(p.pal->accent, alpha)));
    }

    void Paint(const Painter &p) override {
        const Palette &c = *p.pal;
        const D2D1_RECT_F h = Head();
        p.FillRound(h, metric::kRadiusControl,
                    !enabled ? c.controlBg
                             : Mix(c.controlBg, c.controlBgHover,
                                   open ? 1.0f : hoverT));
        p.StrokeRound(h, metric::kRadiusControl, c.controlStroke);
        const D2D1_COLOR_F fg = enabled ? c.textPrimary : c.textDisabled;
        if (selected >= 0 && selected < (int)options.size())
            p.Text(options[selected], { h.left + 11, h.top, h.right - 32, h.bottom },
                   p.font->body, fg);
        p.Text(glyph::kChevron, { h.right - 28, h.top, h.right, h.bottom }, p.font->icon,
               c.textSecondary);
        if (focus && owner && owner->showFocusRing) {
            const D2D1_RECT_F o = { h.left - 2, h.top - 2, h.right + 2, h.bottom + 2 };
            p.StrokeRound(o, metric::kRadiusControl + 2, c.textPrimary, 2.0f);
        }
        const float openF = openT.value;
        if (openF <= 0.0f) return;

        // Where the lid is *now*, and the rows where they rest. The two are separate, and
        // that separation is the whole of the animation: the frame grows out of the
        // control's own row while the rows stay exactly where the layout put them, so the
        // chosen row is over the control from the first frame to the last, and what opens
        // is a window onto a list rather than a list that slides into place.
        const D2D1_RECT_F s = Shown();
        if (s.bottom - s.top < 2.0f) return;

        // A flyout is not a card: it is over the page rather than part of it, so it gets
        // an opaque surface of its own and a shadow.
        //
        // The shadow is Fluent's, which is a blur, and there is no cheap real blur in
        // Direct2D without an effect and a layer per frame -- so this is twelve rounded
        // rectangles at 0.9 per cent, the largest and faintest outermost, which stack into
        // a falloff smooth enough to read as one. It was five at four per cent, and five at
        // four per cent is a black band with an edge on it: at the size of a flyout the
        // steps show as bands, and the four per cent at the outermost layer is not faint
        // enough to disappear. Four DIPs down and none up, because the light is above the
        // popup and the popup hangs off the control it belongs to.
        constexpr int kShadowLayers = 12;
        constexpr float kShadowReach = 14.0f;
        constexpr float kShadowDrop = 4.0f;
        for (int i = kShadowLayers; i >= 1; i--) {
            const float e = kShadowReach * (float)i / (float)kShadowLayers;
            p.FillRound({ s.left - e, s.top - e + kShadowDrop,
                          s.right + e, s.bottom + e + kShadowDrop },
                        8.0f + e, Fade(Rgb(0x000000, 0.009f), openF));
        }
        p.FillRound(s, 8.0f, Fade(c.flyoutBg, openF));
        p.StrokeRound(s, 8.0f, Fade(c.flyoutStroke, openF));

        // The rows, clipped to the lid: what it has not grown over yet is not drawn, and a
        // row the leading edge has reached half way through is cut there.
        p.rt->PushAxisAlignedClip({ s.left, s.top + 1, s.right, s.bottom - 1 },
                                  D2D1_ANTIALIAS_MODE_ALIASED);
        const D2D1_POINT_2F at = open ? Cursor() : D2D1::Point2F();
        const int hot = open ? RowAt(at.x, at.y) : -1;
        for (size_t i = 0; i < options.size(); i++) {
            const float top = RowTop((int)i);
            const D2D1_RECT_F row = { s.left + 4, top, s.right - 4, top + rowH };
            if (row.bottom <= s.top || row.top >= s.bottom) continue;
            const bool over = (int)i == hot;
            // The row under the pointer is filled, and filled harder while the button is
            // down: the press has to land somewhere, and the release takes the list away.
            if (over)
                p.FillRound(row, metric::kRadiusControl,
                            Fade(pressed && enabled ? c.controlBgPressed : c.subtleHover, openF));
            p.Text(options[i], { row.left + 7, row.top, row.right, row.bottom },
                   p.font->body, Fade(c.textPrimary, openF));
        }
        // The mark on the chosen row is drawn on the control's own row instead, and does not
        // travel with the list. The two movements cancel: the choice moves one row while the
        // panel moves one row the other way, so the mark it is put on is the mark of the
        // choice, and what a notch of the wheel changes is which option is under it. Nothing
        // stretches, because nothing has anywhere to go -- see motion::Span for the rule this
        // is the degenerate case of.
        PaintMark(p, openF);
        // The bar once the list has arrived: its line is a setter, and at full strength
        // over a list still growing it would arrive first.
        if (BarShown() && openF >= 1.0f) {
            SyncBar();
            bar->Paint(p);
        }
        p.rt->PopAxisAlignedClip();
    }
};

}  // namespace micula
