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
    const wchar_t *AccessibleName() const override {
        return selected >= 0 && selected < (int)options.size() ? options[selected].c_str() : L"";
    }
    int AccessibleType() const override { return UIA_ComboBoxControlTypeId; }
    bool AccessibleActionable() const override { return true; }

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
    // Where the panel goes and what the view shows, from the choice. Called when the list opens and
    // whenever the choice moves, and that is the whole of what either of them is.
    void Press();
    // The view moved as little as will do to keep the chosen row inside the panel, and the panel with
    // it where it still can. Snapping the choice to the panel's first row -- what opening does -- is
    // right while a list is opening, where the panel covers the control and the two names are meant to
    // be in the same place. It is wrong afterwards: a keyboard walk down a list should not throw away
    // wherever the reader had got to.
    void Follow();

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
            // Shift asks for the list to be *scrolled* rather than chosen from: the same wheel over the
            // same list, aimed at the panel instead of at the choice. A list that fits has nothing to
            // scroll, and there the gesture does nothing at all -- which is better than a wheel that
            // quietly takes an option because Shift was not understood.
            if (scroll && scroll->ScrollMax() > 0.0f) {
                const float lines = SystemWheelLines();
                const float step = lines > 0.0f ? lines : 6.0f;
                scroll->ScrollTo(scroll->scroll - notches * step * RowH(), true);
            }
            return true;
        }
        const int dir = notches > 0.0f ? -1 : 1;
        // A ring has no end to run into, so there is nothing for the mark to give way to: the step
        // either moves the choice or has come all the way round to where it was.
        if (!Step(dir) && !wrapAround && mark) mark->Knock(dir);
        return true;
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
        if (open || options.empty() || !focus) return false;
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
    int Hot() const {
        if (!hover) return -1;
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

    // The whole list, so that the scroll view has something to scroll.
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
        const int i = Hot();
        if (i < 0) return;
        dd->Choose(i);
        dd->SetOpen(false);
    }

    // The wheel is the choice's here rather than the view's, and the control is what decides -- see
    // DropDown::Wheel. A list of things to choose is not a page: a notch that scrolled it would leave
    // the chosen row somewhere other than under the control it was chosen from.
    bool OnWheel(float, float, float notches) override { return dd->Wheel(notches); }

    void Paint(const Painter &p) override {
        const Palette &c = *p.pal;
        const int lit = Hot();
        for (int i = 0; i < Count(); i++) {
            const float top = RowTop(i);
            const D2D1_RECT_F row = { rect.left + DropDown::kPad, top,
                                      rect.right - DropDown::kPad, top + RowH() };
            if (i == lit)
                p.FillRound(row, metric::kRadiusControl,
                            pressed && enabled ? c.controlBgPressed : c.subtleHover);
            p.Text(dd->options[i], { row.left + kTextPad, row.top, row.right, row.bottom },
                   p.font->body, enabled ? c.textPrimary : c.textDisabled);
        }
    }

    int AccessibleType() const override { return UIA_ListItemControlTypeId; }
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

    Press();
    page->Add(flyout);
}

inline void DropDown::Press() {
    if (!scroll || !flyout) return;
    // The room the page shows this through, and the panel's height in it: the list's own height, no
    // taller than that. The panel's place and the view's offset come out of the two of them together,
    // which is why the arithmetic is in one place.
    const float room = (std::max)(0.0f, Height(VisibleArea()) - 2.0f * FlyoutLayout::kGap);
    const float full = 2 * kPad + RowH() * (float)options.size();
    const float panel = (std::min)(full, room);
    const float most = (std::max)(0.0f, full - panel);

    // The chosen row at the panel's own first row, as far as the list's ends allow: a choice within a
    // panel's height of either end stops there, and the chosen row comes to rest as the panel's first
    // or last row rather than over the control.
    const float view = std::clamp(RowH() * (float)selected, 0.0f, most);
    scroll->ScrollTo(view, false);
    const D2D1_RECT_F here = InPage();
    flyout->panelLine = kPad + RowH() * (float)selected - view + RowH() / 2;
    flyout->anchorLine = (here.top + here.bottom) / 2;
    flyout->InvalidateLayout();
}

inline void DropDown::Follow() {
    if (!scroll || !flyout) return;
    const float rowTop = kPad + RowH() * (float)selected;
    const float h = Height(scroll->rect);
    const float lo = (std::min)(rowTop + RowH() - h, rowTop);
    scroll->ScrollTo(std::clamp(scroll->scroll, lo, rowTop), true);
    // The panel's own line, which is where the mark goes: when the panel can still bring the chosen
    // row to the control this is the same line as before, the panel travels a row, and the mark is
    // already where it is going -- which is the whole of what a choice looks like.
    flyout->panelLine = kPad + RowH() * (float)selected - scroll->scroll + RowH() / 2;
    flyout->InvalidateLayout();
}

}  // namespace micula
