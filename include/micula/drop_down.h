// Micula / drop_down.h
//
// A list of options with one of them showing: Windows' combo box. The control is a row with a label
// and a chevron on it, and opening it drops a **flyout** -- see flyout.h -- which covers the page.
//
// Everything the old one did for itself is one of those two things now. The popup that grew the
// control's own rectangle until it covered the page is a layer, and the bar it kept and placed and
// woke is the scroll view's: the list inside the flyout is a `ScrollView` that is only as big as its
// list (`shrink`), so a list longer than the room scrolls under the platform's own bar and a list that
// fits is a panel the height of its rows.
//
// What is left here is what a list *of things to choose* is, and the wheel is the one that is easy to
// get wrong: a notch steps the choice rather than scrolling the view, because the gesture is a walk
// down a list rather than a movement of one. Shift asks for the view instead. The list hands the wheel
// to the control to be that; see DropDown::Wheel.
//
// ```cpp
// auto *dd = new DropDown({ L"Low", L"Medium", L"High" }, quality, [this](int i) { quality = i; });
// card->Set(dd);
// ```

#pragma once

#include "flyout.h"
#include "glyphs.h"
#include "scroll_view.h"
#include "stack_layout.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <functional>
#include <string>
#include <vector>

namespace micula {

struct DropDown;
struct DropDownList;

// The accent mark on the chosen row, and the two shapes it makes when a gesture has nowhere to go.
//
// It is a child of the flyout -- see Flyout::marker -- and not something the list draws, because the
// list is *inside* the panel and anything it drew would ride the panel's slide. Placed where the
// choice has settled, the mark is already where a sliding panel is going, and the options travel under
// it: a choice is a thing that changed rather than a thing that slid past.
//
// Where the room has pinned the panel the rows cannot move at all, and then the mark is the only thing
// left that can show the change. There it travels down them, and the glide is what makes that a travel
// rather than a step.
//
// The two shapes are not motion, so the glide does not stand in for them:
//
//   - A gesture that had nowhere to go -- the wheel, or Up and Down, at the end of the list -- is
//     answered by the mark giving way the way it was pressed and coming back. The edge the gesture
//     goes towards twitches quickly and a short way and holds there; the other edge follows, more
//     slowly and further, so the mark is *shorter* for as long as either of them is out. Then both
//     spring back. A lean and a spring rather than a move.
//   - A letter that found nothing shrinks the mark for about a fifth of a second. Full at the instant
//     of the refusal and decaying from there, which is the shape that needs no state beyond the number
//     itself.
struct DropMark : Widget {
    float refuse = 0.0f;
    static constexpr float kRefuseLag = 0.07f;    // seconds
    float knock = 0.0f;                           // the fast edge's impulse, 0..1
    float knockLag = 0.0f;                        // and the edge that follows it, which goes further
    int knockDir = 0;                             // -1 up a list, +1 down it
    bool knockHeld = false;                       // still in the rise, which is where the two differ
    static constexpr float kKnockRise = 0.03f;    // seconds for the fast edge to arrive
    static constexpr float kKnockFollow = 0.03f;
    static constexpr float kKnockLag = 0.12f;     // and for both to spring back
    static constexpr float kKnockTip = 2.0f;      // DIPs the fast edge moves
    static constexpr float kKnockShove = 6.0f;    // and the edge that follows it

    // `dir` is +1 for down a list, -1 for up it: the direction the gesture was going.
    void Knock(int dir) {
        knock = 0.0f;
        knockLag = 0.0f;
        knockHeld = true;
        knockDir = dir;
    }
    void Refuse() { refuse = 1.0f; }

    // A mark is a picture: three DIPs of the panel's own margin would otherwise be a piece of a row
    // that cannot be clicked.
    bool Covers(float, float) const override { return false; }

    bool Animating() const override {
        return Widget::Animating() || refuse > 0.0f || knockHeld || knock > 0.0f || knockLag > 0.0f;
    }
    void Tick(float dt) override {
        Widget::Tick(dt);
        // A refusal nobody can see move is not worth showing, and neither is a lean: with animations
        // off both are off, and the mark is a mark.
        if (!Animations()) {
            refuse = knock = knockLag = 0.0f;
            knockHeld = false;
            knockDir = 0;
            return;
        }
        if (refuse > 0.0f) {
            refuse *= std::exp(-dt / kRefuseLag);
            if (refuse < 0.002f) refuse = 0.0f;
        }
        if (knockHeld) {
            // Both edges set off on the same frame. The edge behind goes three times as far, so a ramp
            // for it -- three times the distance in three times the time -- would run at exactly the
            // fast edge's rate, and the two would move as one for the first thirtieth of a second while
            // the mark slid bodily down and did not shorten at all. An approach is fastest in its first
            // instant, so the mark begins losing length at once, and the fast edge still arrives first
            // because its distance is the short one.
            knock = (std::min)(knock + dt / kKnockRise, 1.0f);
            knockLag = 1.0f - (1.0f - knockLag) * std::exp(-dt / kKnockFollow);
            if (knock >= 1.0f && knockLag >= 0.95f) knockHeld = false;
        } else if (knock > 0.0f || knockLag > 0.0f) {
            knock *= std::exp(-dt / kKnockLag);
            knockLag *= std::exp(-dt / kKnockLag);
            if (knock < 0.002f && knockLag < 0.002f) { knock = knockLag = 0.0f; knockDir = 0; }
        }
    }

    void Paint(const Painter &p) override {
        // At rest the bar is the row's own height less sixteen: eight DIPs in from either end, and the
        // refusal's shrink is the first 4.8 of those eight.
        const float inset = 8.0f + 4.8f * refuse;
        // The knock, in DIPs of offset per edge. The fast edge is capped at what the edge behind it has
        // already given: the mark may be shorter than it is at rest and never longer, so what the two
        // of them do together reads as length rather than as travel.
        const float tip = (std::min)(kKnockTip * knock, kKnockShove * knockLag);
        const float shove = kKnockShove * knockLag;
        const float top = rect.top + inset + (knockDir > 0 ? shove : -tip);
        const float bot = rect.bottom - inset + (knockDir > 0 ? tip : -shove);
        p.FillRound({ rect.left, top, rect.right, bot }, 1.5f, p.pal->accent);
    }
};

struct DropDown : Widget {
    std::vector<std::wstring> options;
    int selected = 0;
    std::function<void(int)> onChange;
    // Whether the choice is a ring. Off, the list has two ends, and a step past one of them has nowhere
    // to go: it does nothing rather than wrapping to the other end, and the mark says so -- see Knock.
    bool wrapAround = false;
    // Whether the list is up. The flyout comes and goes with it, and the pointers are null while it is
    // down -- so "is there a list" and "is it open" are the same answer.
    bool open = false;
    Flyout *flyout = nullptr;
    ScrollView *scroll = nullptr;
    // **The scroll the panel's placement was made for**, in DIPs -- what the placement *asks* for, which
    // is not always what a `ScrollTo` can give at the moment it asks. Two things read it. A list since
    // aimed somewhere else is a list moving under a mark that is no longer on the row it says it is, and
    // `Tick` compares this against where the list is to ask that; and while the ask is still owed (see
    // `placedOwed`) the mark is the placement's own line rather than the row's.
    float placedView = 0.0f;
    // Whether the scroll above is one to glide to: false opens a list (it is *placed* where the choice
    // is) and true follows a choice (a change to be seen). `ScrollTo` again after it has taken is a
    // no-op, which is what makes re-sending it safe. See `Press`.
    bool placedGlides = false;
    // **Whether that scroll has been sent into a panel that could take it.** A placement is made for a
    // height the panel does not have yet -- `SetOpen` presses before the flyout has ever been arranged,
    // and a choice near an end of the list makes the panel shorter than it was -- so the scroll it asks
    // for is clamped against a viewport that is about to be somebody else's. The number is the same one;
    // what is new is the panel, so it is sent once more on the first frame the panel has a box, and that
    // is the whole of the second pass. See `Tick`.
    bool placedOwed = false;
    DropDownList *list = nullptr;
    DropMark *mark = nullptr;

    // Typing to find an option. The letters go into a prefix, the option that starts with it is chosen,
    // and the prefix is forgotten after a second of quiet and whenever the list opens or closes: it is
    // a way of pointing at one option, not a query that stays.
    std::wstring typed;
    ULONGLONG typedAt = 0;
    static constexpr ULONGLONG kTypeWindow = 1000;

    // The panel's margin, which the list draws and this measures against.
    static constexpr float kPad = 4.0f;

    DropDown(std::vector<std::wstring> opts, int sel, std::function<void(int)> f)
        : options(std::move(opts)), selected(sel), onChange(std::move(f)) {}

    // A control's own row is one control tall, so that the panel which opens over it lines a row up
    // with it exactly.
    float RowH() const { return metric::kControlH; }

    // The list is the control's while it is up: it is a layer of the page's, not a child of this, and a
    // page that has dropped the control -- a rebuilt page, a window closing -- has to drop the list
    // with it. The node is handed to the window rather than destroyed here, so a flyout in the middle
    // of a message is still a live object until that message returns.
    ~DropDown() override {
        if (flyout && flyout->parent) flyout->parent->Remove(flyout);
    }

    bool Focusable() const override { return true; }
    int AccessibleType() const override { return UIA_ComboBoxControlTypeId; }
    bool AccessibleActionable() const override { return true; }
    // **What is chosen is the control's value, not its name**: a combo box is named by the words beside
    // it -- which is the card it is on, and what `Card::Set` puts on it -- and read out as "Move files
    // older than, 1 week": the label, then the choice. The choice as the *name* reads as a control
    // called "1 week", and leaves nothing to say what it is 1 week of.
    bool AccessibleValue(std::wstring &out) const override {
        if (selected < 0 || selected >= (int)options.size()) return false;
        out = options[selected];
        return true;
    }
    // Whether the list is out, which is what says whether the rows are the page or a thing to open.
    int AccessibleExpanded() const override { return open ? 1 : 0; }
    bool AccessibleWritable() const override { return true; }
    // Opening and closing, which is what a click on the control does.
    bool AccessibleSetExpanded(bool o) override {
        SetOpen(o);
        return true;
    }
    // **Typing an option name into a combo box**, which is what `IValueProvider::SetValue` means on one:
    // not free text but the name of one of the options. An exact match, so that a client spelling an
    // option out is believed and one saying something the list has never heard of is told so.
    bool AccessibleSetValue(const std::wstring &text) override {
        for (size_t i = 0; i < options.size(); i++)
            if (options[i] == text) {
                Choose((int)i);
                return true;
            }
        return false;
    }

    // **As wide as the room it is given**, like every other control in a card: a field, a slider and a
    // drop-down all fill the slot the card keeps for them. Written from the label instead -- the chosen
    // option, which is what "content" would mean -- the control's *width* would change when the choice
    // did: the card would lay itself out again under the pointer that had just chosen something, and
    // the control would jump narrower while the list it came from was still closing.
    //
    // The longest option is what the *panel* is sized from, in `DropDownList::Measure`, and that is
    // where a list of long words gets the room it needs.
    micula::Want Measure(const Room &room) const override {
        (void)room;
        return micula::Want(Axis::Fill(), Axis::Fixed(RowH()));
    }

    void Paint(const Painter &p) override {
        const Palette &c = *p.pal;
        p.FillRound(rect, metric::kRadiusControl,
                    !enabled ? c.controlBg
                             : Mix(c.controlBg, c.controlBgHover, open ? 1.0f : hoverT));
        p.StrokeRound(rect, metric::kRadiusControl, c.controlStroke);
        if (selected >= 0 && selected < (int)options.size())
            p.Text(options[selected], { rect.left + 11, rect.top, rect.right - 32, rect.bottom },
                   p.font->body, enabled ? c.textPrimary : c.textDisabled);
        p.Text(glyph::kChevronDown, { rect.right - 28, rect.top, rect.right, rect.bottom },
               p.font->icon, c.textSecondary);
        if (ShowFocusRing()) {
            const D2D1_RECT_F o = { rect.left - 2, rect.top - 2, rect.right + 2, rect.bottom + 2 };
            p.StrokeRound(o, metric::kRadiusControl + 2, c.textPrimary, 2.0f);
        }
    }

    // --- the list ----------------------------------------------------------------------------------
    //
    // Opening builds the flyout and then stops: the panel's place, the view and the mark all follow
    // from the choice, and there is nothing to keep in step afterwards. Defined below the list, which
    // is the thing it builds.

    void SetOpen(bool o);
    // **The panel, placed from the choice**, which is the whole of what a drop-down's geometry is: the
    // chosen row lands on the control's own line, the panel is made of the whole rows that fit around
    // that line, and the view shows the list from the row the panel's first one is on.
    //
    // `glide` is the whole of the difference between the two callers -- opening *places* the list where
    // the choice is, a step glides it, because a panel that slid in from wherever it was is an opening
    // nobody asked for and a choice that changes is a change to be seen -- and there is one piece of
    // arithmetic because there is one alignment. A placement worked out any other way is a panel the
    // room clamps, and a clamp takes the chosen row off the control by exactly what it clamped.
    void Press(bool glide = false);
    // The choice moved and the panel follows it: the same placement, glided. One function rather than
    // two because a second opinion about where the panel goes is what puts the chosen row on a row of
    // the panel the room did not leave it: the view then has to move, the panel moves with it, and the
    // clamp ends the walk with the choice off the control's line. See `Press`.
    void Follow() { Press(true); }

    // The choice, from a click on a row or from the keyboard, and whether it moved. A step that ran off
    // an end of the list is the option it started from, and the callers read that as nowhere to go.
    bool Choose(int i) {
        const int n = (int)options.size();
        if (n <= 0) return false;
        const int at = std::clamp(i, 0, n - 1);
        if (at == selected) return false;
        selected = at;
        if (onChange) onChange(selected);
        if (open) Follow();
        Invalidate();
        return true;
    }
    bool Step(int dir) {
        const int n = (int)options.size();
        if (n <= 0) return false;
        const int want = selected + dir;
        return Choose(wrapAround ? ((want % n) + n) % n : want);
    }

    // The wheel, wherever it lands on an open list: the list hands it here -- see
    // DropDownList::OnWheel -- and so does the panel around it.
    //
    // One row a notch, and the row that arrives at the control's own row is the choice. A notch is a
    // step through the items and not a distance: a wheel that moved the list 66 DIPs, as a page's does,
    // would leave the chosen row somewhere other than under the control it was chosen from. Taken even
    // at either end, where it is the mark that answers -- and if it were passed on instead, it would
    // scroll the page out from under the open list.
    bool Wheel(float notches) {
        if (options.empty()) return true;
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
            ScrollBy(notches);
            return true;
        }
        const int dir = notches > 0.0f ? -1 : 1;
        // A ring has no end to run into, so there is nothing for the mark to give way to: the step
        // either moves the choice or has come all the way round to where it was.
        if (!Step(dir) && !wrapAround && mark) mark->Knock(dir);
        return true;
    }

    // Shift asks for the list to be *scrolled* rather than chosen from: the same wheel over the same
    // list, aimed at the panel's view instead of at the choice. A list that fits has nothing to scroll,
    // and there the gesture does nothing at all -- which is better than a wheel that quietly took an
    // option because Shift was not understood. The choice does not move, so neither does the panel: the
    // mark travels with its row instead, over the frames the view is moving. See `Tick`.
    void ScrollBy(float notches) {
        if (!scroll || scroll->ScrollMax() <= 0.0f) return;
        const float lines = SystemWheelLines();
        const float step = lines > 0.0f ? lines : 6.0f;
        scroll->ScrollTo(scroll->scroll - notches * step * RowH(), true);
    }

    // The mark follows the row it is on while the list under it is scrolling: the view moved the chosen
    // row without moving the choice, and a mark left behind is a mark on somebody else's row. The panel
    // is not placed from this -- it stays where the choice put it, which is what makes a scroll a scroll
    // and a choice a swap -- and the work is the frames of a scroll, not every frame the window is awake
    // for: a list that is where the panel was placed for it is left alone. See `Tick`.
    //
    // Placed here rather than through an arrangement, which is the whole of why it is exact: an
    // arrangement runs *before* the tick that moved the list, so a mark left to one is drawn at the
    // scroll of the frame before, which reads as an indicator trailing the list it belongs to.
    void Tick(float dt) override {
        Widget::Tick(dt);
        if (!open || !flyout || !scroll || !scroll->content) return;
        // **The scroll the placement asked for, sent again into a panel that can take it.** `ScrollTo`
        // clamps against the viewport the panel has at that moment, and a placement is made for a height
        // the panel does not have yet: one opened before the flyout was ever arranged, and one near an
        // end of the list, where the panel is made *shorter* than it was and the view asked for is one
        // the old height had no room for. The number is the same one -- it is the panel that is new --
        // and one more send, on the first frame the panel has a box, is the whole of it.
        if (placedOwed && flyout->panel && Height(flyout->panel->rect) > 0.0f) {
            placedOwed = false;
            scroll->ScrollTo(placedView, placedGlides);
            // **Exactly**, not on its way: the list is travelling to where the placement put it, the
            // mark is already there, and this is the frame a list opens on -- a mark gliding from
            // wherever the last one left it is a first frame showing something else.
            flyout->PlaceMarker(true);
            return;
        }
        // **The mark is on its row unless the panel was placed for the row.** A placement is only true
        // of the list it was made with: once the view has been aimed elsewhere -- a dragged thumb, Shift
        // and the wheel -- the chosen row is somewhere else on screen and the mark goes with it. Asked of
        // the list rather than of the gesture: a flag set by the wheel's own scroll is a flag a dragged
        // scroll bar never raises, and the mark then stands still until the next wheel happens to put it
        // right. See `placedView`.
        if (scroll->scroll == placedView) return;
        // **From where the list is *drawn*, not from where it is going.** A scroll is a glide: `scroll`
        // is the target the view was given, and the column travels toward it over the frames after that.
        // A mark placed from the target therefore jumps the whole distance on the first frame and waits
        // there while the rows slide up to it, which reads as the indicator having gone on ahead. The
        // column's own drawn rectangle is the same number once it has arrived, and the one the rows are
        // painted at while it has not.
        const D2D1_RECT_F &column =
            scroll->content->placed ? scroll->content->drawn : scroll->content->rect;
        // Less the panel's own lag, for the same reason from the other end: the rows are painted from
        // where the panel is *drawn*, and `PlaceMarker` writes the mark from where it was arranged.
        const float lag = flyout->panel && flyout->panel->placed
                              ? flyout->panel->drawn.top - flyout->panel->rect.top
                              : 0.0f;
        flyout->markerLine = kPad + RowH() * (float)selected + column.top + RowH() / 2.0f - lag;
        flyout->PlaceMarker(true);
    }

    // The mark is the panel's own line again: called wherever the choice has placed the panel, so that
    // a mark left following the list -- a dragged thumb -- is back on the row the panel is placed for.
    void Settle() {
        if (flyout) flyout->markerLine = flyout->panelLine;
    }

    // --- input -------------------------------------------------------------------------------------

    void OnClick() override { if (enabled && !open) SetOpen(true); }
    // Space and Enter. Closed they open the list, which is what a control with a label on it does; open
    // they take the row the mark is on and put the list away. That is how a list is walked with the
    // keys -- Up and Down move the mark, and Space or Enter says "that one" -- and it is why a choice
    // made this way is *not* the pointer's choice: there the row under the pointer is the one named,
    // and here there is no pointer to read.
    void OnActivate() override {
        if (!enabled) return;
        if (!open) { SetOpen(true); return; }
        Choose(selected);
        SetOpen(false);
    }
    void Dismiss() override { SetOpen(false); }
    void OnBlur() override { if (open) SetOpen(false); }

    bool OnKey(WPARAM vk) override {
        if (vk == VK_ESCAPE && open) { SetOpen(false); return true; }
        // **A control that is off answers no key but Escape.** The arrows reach the same steps the wheel
        // does, and the wheel is the one place this was already right: a page turns the picker off while
        // it waits for what the choice it has just taken means, the list is a child of the flyout rather
        // than of the control, and the keys still went to the control -- so a choice could be moved under
        // a picker that had been told not to take one. Escape stays: what is open is still the control's
        // to close, whatever it has been told about taking a choice.
        if (!enabled) return false;
        if (vk != VK_UP && vk != VK_DOWN) return false;
        if (options.empty()) return false;
        const int dir = vk == VK_DOWN ? 1 : -1;
        // Open, a step goes through the same path the wheel's does, so the rows travel under the mark
        // as the choice moves. Closed, the label is the only thing that moves and there is no mark on
        // screen to give way, so the ends are silent.
        if (open) {
            if (!Step(dir) && !wrapAround && mark) mark->Knock(dir);
        } else {
            Choose(selected + dir);
        }
        return true;
    }

    // A printable character, from the keyboard or the IME, and always the control's whether or not it
    // found anything: a letter that matched nothing and was passed on to the window would be answered
    // with a beep.
    //
    // A first letter is a step, like a notch of the wheel, and a step goes forward -- the next option
    // that starts with it, after the one chosen now. Every letter after it only refines an answer that
    // has already been given, so it starts *at* the chosen option and the answer stays put for as long
    // as the option under it still starts with what has been typed. The same letter again steps to the
    // next option that starts with it rather than looking for the prefix it is already on, which is the
    // only way to reach the second "Monthly" from the keyboard.
    bool OnChar(wchar_t ch) override {
        const int n = (int)options.size();
        if (!enabled || n <= 0) return false;
        // Only while the list is open: a closed drop-down is a button with a label on it, and it has
        // nothing to search in.
        if (!open) return true;
        const ULONGLONG now = GetTickCount64();
        const wchar_t lower = (wchar_t)std::towlower(ch);
        const bool again = typed.size() == 1 && typed[0] == lower;
        if (again || now - typedAt > kTypeWindow) typed.clear();
        typedAt = now;
        typed.push_back(lower);
        const int from = typed.size() > 1 ? selected : selected + 1;
        for (int step = 0; step < n; step++) {
            const int i = ((from + step) % n + n) % n;
            if (StartsWith(options[i], typed)) { Choose(i); return true; }
        }
        // Nothing starts with it. Answered rather than ignored: see DropMark::Refuse.
        if (mark) mark->Refuse();
        return true;
    }

    // Closed, a wheel steps the choice -- **but only while the control has the focus**. That is what
    // WinUI 3 does and it is the whole of why it is right: its own `ComboBox::OnPointerWheelChanged`
    // asks `HasFocus()` before it touches the selection, and swallows the notch when it takes it. A
    // wheel that changed the value of whatever it happened to pass over is a wheel that changes a
    // setting on the way down a page -- the pointer crosses a control nobody was aiming at, and the
    // value moves with it. The click that focuses the control is the gesture that says *this* one is
    // being worked on; after that the wheel is a way of stepping it without a trip to the keyboard,
    // and an unfocused control is not in the way of the page's scroll.
    //
    // Open, the wheel is not this control's at all: it goes to the panel under the pointer, which is a
    // scroll view, so the list scrolls and the choices stay where they are. A list longer than the
    // panel can then be read to its end with the wheel, which the old one could not do -- there the
    // wheel stepped the choice and Shift was the only way to scroll.
    bool OnWheel(float, float, float notches) override {
        // **Off is off, whether or not anybody left it focused.** A control can be focused and then
        // turned off -- a page waiting for what the choice it has just taken means -- and the wheel was
        // the one hand that went on stepping it. Let past rather than swallowed: a control nobody can
        // work is not in the way of the page's scroll either, which is the same reason the focus rule
        // above exists.
        if (open || options.empty() || !focus || !enabled) return false;
        Step(notches > 0.0f ? -1 : 1);
        return true;
    }

    // --- geometry ----------------------------------------------------------------------------------

    // This control's own rectangle in the page's space: a widget's `rect` is in its parent's, so the
    // chain of parents is the difference, and the page is where a flyout's anchor has to be. The page's
    // own rectangle is left out -- it is the box the page's children are placed in, which is the space
    // the flyout is placed in too.
    D2D1_RECT_F InPage() const {
        D2D1_RECT_F r = rect;
        for (const Widget *p = parent; p && p->parent; p = p->parent) {
            r.left += p->rect.left;
            r.top += p->rect.top;
            r.right += p->rect.left;
            r.bottom += p->rect.top;
        }
        return r;
    }

    static bool StartsWith(const std::wstring &s, const std::wstring &prefix) {
        if (prefix.size() > s.size()) return false;
        for (size_t i = 0; i < prefix.size(); i++)
            if ((wchar_t)std::towlower(s[i]) != prefix[i]) return false;
        return true;
    }
};

// The rows, drawn by one widget: a list of forty countries is forty rows and not forty widgets, and a
// row of options is a line of text with a hover on it. Its height is the whole list's -- the scroll
// view around it is what shows a window of it -- and its width is the longest option's.
//
// The panel's own margin is drawn here rather than given to it: the rows are inset by it, and the mark
// sits in the first four DIPs of it.
struct DropDownList : Widget {
    explicit DropDownList(DropDown *owner) : dd(owner) {}
    DropDown *dd = nullptr;
    // **The row the pointer is over as the list is drawn**, asked of the pointer itself rather than
    // remembered from the last move. The pointer is not the only thing that moves: a list sliding to a
    // new choice takes its rows past a stationary pointer, and the row that lights up has to be the one
    // under it -- which is the row it is *drawn* under, since a widget's own space follows where it is
    // drawn and not where it was arranged. Nothing else could tell the list: no mouse message arrives
    // while the pointer is still, and a remembered answer waits for the next one.
    //
    // A press needs nothing of its own: the window has the capture, sets `pressed` on it, and clears it
    // when the pointer leaves the rectangle -- so the row under the pointer is the pressed one.
    // **What the hand is over, whether or not there is a pointer over it.** `hover` is the mouse's, and
    // a finger does not hover -- so a list that asked it would find nothing under the finger at all, and
    // a tap would choose nothing. What a click asks is this; what lights up asks `Hot`, and nothing
    // lights up under a finger.
    int Hot() const { return hover ? UnderHand() : -1; }
    int UnderHand() const {
        const D2D1_POINT_2F at = Cursor();
        if (!Inside(rect, at.x, at.y)) return -1;
        return RowAt(at.y);
    }

    // The text's own inset inside a row, and what the label leaves at the end of one.
    static constexpr float kTextPad = 7.0f;

    int Count() const { return (int)dd->options.size(); }
    float RowH() const { return dd->RowH(); }
    float RowTop(int i) const { return rect.top + DropDown::kPad + RowH() * (float)i; }

    bool TracksPointer() const override { return true; }

    // The whole list, so that the scroll view has something to scroll. **Its own height and no less**: a
    // list whose measured height were the rows that fit would be a list exactly as tall as the panel over
    // it, and a scroll view with nothing to scroll -- which is a list that cannot be moved at all. Whole
    // rows are the panel's business: see `Flyout::rowH`.
    micula::Want Measure(const Room &room) const override {
        float text = 0.0f;
        if (room.fonts)
            for (const std::wstring &o : dd->options)
                text = (std::max)(text, room.fonts->Measure(room.fonts->body, o));
        const float w = 2 * DropDown::kPad + 2 * kTextPad + text + ScrollBar::kSize + 2;
        return micula::Want(Axis::Content(w),
                            Axis::Content(2 * DropDown::kPad + RowH() * (float)Count()));
    }

    // The row a point is over, in this widget's own space; -1 for anything outside the list.
    int RowAt(float y) const {
        const int i = (int)std::floor((y - rect.top - DropDown::kPad) / RowH());
        return (i >= 0 && i < Count()) ? i : -1;
    }

    void OnClick() override {
        // Asked again rather than remembered: the press and the release are two messages apart, and
        // between them the list may have slid under the pointer -- a notch of the wheel, a step of the
        // keyboard. The old one read the cursor here for the same reason.
        if (!dd->enabled) return;
        const int i = UnderHand();
        if (i < 0) return;
        dd->Choose(i);
        dd->SetOpen(false);
    }

    // The wheel is the choice's here rather than the view's, and the control is what decides -- see
    // DropDown::Wheel. A list of things to choose is not a page: a notch that scrolled it would leave
    // the chosen row somewhere other than under the control it was chosen from.
    //
    // **A list is only as enabled as the control that opened it**, and the control can be turned off
    // while the list is up -- a page waiting for what the choice it has just taken means. It still
    // takes the notch, because an open list covers the page and a wheel let past it would scroll what
    // is behind: what a disabled list does is nothing, out loud.
    bool OnWheel(float, float, float notches) override {
        if (!dd->enabled) return true;
        return dd->Wheel(notches);
    }

    void Paint(const Painter &p) override {
        const Palette &c = *p.pal;
        const int lit = Hot();
        for (int i = 0; i < Count(); i++) {
            const float top = RowTop(i);
            const D2D1_RECT_F row = { rect.left + DropDown::kPad, top,
                                      rect.right - DropDown::kPad, top + RowH() };
            // A row of a list is filled the way WinUI's combo box item is -- `SubtleFillColorSecondary`
            // under the pointer, `SubtleFillColorTertiary` while it is pressed. See `Palette::subtlePressed`.
            // A control that has been turned off while its list is up takes the rows with it: see OnWheel.
            if (i == lit && enabled && dd->enabled)
                p.FillRound(row, metric::kRadiusControl,
                            pressed && enabled ? c.subtlePressed : c.subtleHover);
            p.Text(dd->options[i], { row.left + kTextPad, row.top, row.right, row.bottom },
                   p.font->body, enabled && dd->enabled ? c.textPrimary : c.textDisabled);
        }
    }

    int AccessibleType() const override { return UIA_ListControlTypeId; }

    // **The rows, one element each.** A list of forty options is one widget and forty things a client
    // has to be able to read one at a time -- "Row 14, 15 of 40, selected" -- so the children of this
    // element are the rows rather than anything the tree happens to hold. See Widget::AccessibleItems.
    int AccessibleItems() const override { return Count(); }
    bool AccessibleItem(int i, Item &out) const override {
        if (i < 0 || i >= Count()) return false;
        out.name = dd->options[i].c_str();
        out.type = UIA_ListItemControlTypeId;
        out.index = i;                        // the page's own number for the option
        out.selected = (i == dd->selected);
        const float top = RowTop(i);
        out.box = { rect.left + DropDown::kPad, top, rect.right - DropDown::kPad, top + RowH() };
        // A row the panel has scrolled past is one a client is told about and cannot reach.
        const D2D1_RECT_F seen = VisibleArea();
        out.onscreen = out.box.bottom > seen.top && out.box.top < seen.bottom;
        return true;
    }
    bool AccessibleWritable() const override { return true; }
    // **Choosing a row, which is what a click on it is**: the choice is made -- and a keyboard walk of
    // the list is what that leads to -- and the list goes away. Not a change to `selected` behind the
    // control's back: `Choose` is the one path in, and what the page's callback is run from.
    bool AccessibleSelect(int index) override {
        if (index < 0 || index >= Count()) return false;
        dd->Choose(index);
        dd->SetOpen(false);
        return true;
    }
};

inline void DropDown::SetOpen(bool o) {
    if (o == open || options.empty()) return;
    typed.clear();
    if (!o) {
        if (flyout) flyout->Close();
        return;
    }

    // **The page**: the widget with no parent, which is what a layer that covers the page has to be
    // added to. Not the card this control is on -- a flyout in a card would be clipped to it and would
    // travel with it -- and not the view the page scrolls in, either.
    Widget *page = this;
    while (page->parent) page = page->parent;

    open = true;
    flyout = new Flyout(InPage());
    flyout->linedUp = true;
    flyout->onClose = [this] {
        open = false;
        flyout = nullptr;
        scroll = nullptr;
        list = nullptr;
        mark = nullptr;
    };
    scroll = flyout->Add(new ScrollView());
    scroll->shrink = true;
    auto *column = new StackLayout();
    column->padX = 0.0f;      // the panel's margin is the list's own, not the page's
    scroll->content->SetLayout(column);
    list = scroll->Add(new DropDownList(this));
    // The mark is the flyout's second child, added after the panel so that it is painted over the rows,
    // and placed by the flyout rather than by anything here: see Flyout::marker.
    mark = flyout->Widget::Add(new DropMark());
    flyout->marker = mark;
    flyout->markerH = RowH();
    flyout->rowH = RowH();
    flyout->rowPad = kPad;

    Press();
    page->Add(flyout);
}

inline void DropDown::Press(bool glide) {
    if (!scroll || !flyout) return;
    const float rowH = RowH();
    const int n = (int)options.size();
    const D2D1_RECT_F here = InPage();
    const float anchor = (here.top + here.bottom) / 2.0f;
    // **The room, worked out the way the flyout will get it**: the page's own box, in the page's own
    // space, which is the box a layer is covered with -- the same rectangle `FlyoutLayout::Arrange` is
    // handed. Everything here is in that space already (`InPage`), so the two agree to the DIP, and a
    // panel that fits this room is a panel the layout has no reason to move: a room that is even 10 DIP
    // too generous is a panel clamped up against the box's bottom edge, which takes the chosen row off
    // the control by exactly that much.
    const Widget *root = this;
    while (root->parent) root = root->parent;
    const float roomTop = FlyoutLayout::kGap + kPad;
    const float roomBottom = Height(root->rect) - FlyoutLayout::kGap - kPad;
    const float line = anchor - rowH / 2.0f;             // where the chosen row's own top edge has to be
    const float fitAbove = (std::floor)((line - roomTop) / rowH);
    // **The panel is as tall as the room allows, in whole rows, and that is not a question about the
    // choice.** A panel that was as tall as the rows the choice happened to leave above and below it came
    // out three rows tall whenever the choice was near either end -- there was nothing under it, so there
    // was nothing for the panel to be but the three rows that were left. What the choice decides is where
    // the list *sits* in the panel, never how much of the panel there is. One height for the whole list is
    // also what keeps every notch of a wheel landing on a row: the same panel, whole rows, one row a step.
    const float rowsFit = (std::floor)((roomBottom - roomTop) / rowH);
    const float rows = std::clamp(rowsFit, 1.0f, (float)n);
    // **And the choice still comes out on the control.** As far down the panel as it may sit is what the
    // room over the control allows -- past that the panel's top edge leaves the page -- and the view may
    // not begin before the list's first row, which is the other side of the same line. Both are whole
    // rows, so the chosen row lands on the control's own line every time the list opens.
    float above = (std::min)((std::min)(fitAbove, rows - 1.0f), (float)selected);
    const float earliest = (float)selected - ((float)n - rows);
    if (above < earliest) above = earliest;
    const float below = rows - 1.0f - above;
    // **The view is the placement, not a second opinion about it.** The chosen row's own row inside the
    // panel is the one the rows above it leave it, so the view shows the list from exactly there: the
    // panel is placed once and does not move while the choice walks it, and every step is the rows
    // travelling under a mark that stays on the control -- which is the whole of what the alignment
    // buys. A view that moved only when it had to would leave the chosen row on some other row of the
    // panel, and the panel would have to move to put it back on the control: the popup then slides a row
    // at a time until the room clamps it, and the choice is off the control by what it was clamped.
    const float viewRows = (float)selected - above;
    // Placed rather than glided when the list opens, so that the frame it opens on is a list already
    // showing the chosen row -- and **before** the scroll, not after: the column's rectangle is what the
    // scroll writes, and `PlaceSubtree` copies a rectangle into `drawn`, so placing after it is placing
    // the column back where it was.
    if (!glide) micula::PlaceSubtree(scroll->content);
    scroll->ScrollTo(viewRows * rowH, glide);
    placedView = viewRows * rowH;
    placedGlides = glide;
    placedOwed = true;
    flyout->rowsAbove = above;
    flyout->rowsBelow = below;
    flyout->panelLine = kPad + rowH * above + rowH / 2.0f;
    Settle();
    flyout->anchorLine = anchor;
    flyout->InvalidateLayout();
}

}  // namespace micula
