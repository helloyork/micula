// Micula / text_box.h

#pragma once

#include "window.h"
#include "scroll_bar.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <functional>
#include <string>
#include <vector>

namespace micula {


// A text field, drawn.
//
// This was a real child EDIT control, and it had to stop being one: the window carries
// `WS_EX_NOREDIRECTIONBITMAP` so that Mica is visible behind it (window.h says why),
// and a window with no redirection surface has nowhere for USER32 to draw a child
// control. The choice was Mica or the EDIT, and the EDIT lost.
//
// So this reimplements the part of an edit control a settings field actually uses: a
// caret, a selection made with the keyboard or the mouse (press, drag, Shift+click,
// double-click), the six navigation keys, the four clipboard commands, and IME
// input. One line or several -- see `TextMode` -- and a field with several has a
// `ScrollBar` of its own, laid over its right-hand edge the way WinUI's own bar lies
// over the page it belongs to.
//
// It does not reimplement undo, drag-and-drop, right-click, spell checking or rich
// text, and it should not grow them -- a dialog that needs those wants a redirected
// window with a real edit control, not a bigger version of this.
//
// The layout object is kept rather than rebuilt per paint. Both things this needs --
// where the caret goes for a character index, and which character index a click landed
// on -- are `IDWriteTextLayout` queries, and creating a layout per query turns a
// 60-character path into 60 layouts on every mouse move. A wrapped field has one more
// reason to keep it: the same text at another width is another set of lines, so it is
// remade when the width moves and at no other time.
// What a validator says about the text as it stands. **Three states and not two**, because a
// *prefix* of something valid matches nothing yet: `2001:` is not an address, and a rule that could
// only say yes or no could never be satisfied by somebody typing one.
//
// The field asks its validator on every change and paints an `Invalid` answer on the underline, in
// `Palette::bad`. It is not asked at all when no validator is set, which is the usual case and costs
// nothing.
enum class Validation { Acceptable, Intermediate, Invalid };

// What the field turned away, told to `TextBox::onRefused` so a page can say why rather than leaving
// somebody typing into a field that silently eats their keystrokes. One report per attempt, whatever
// length it was: a paste of a hundred refused characters is one thing that happened.
enum class Refusal {
    Character,   // the filter would not have it
    Length,      // there was no room left for it
    Lines,       // the field's own shape had no room for the line break in it
    Invalid,     // the text now stands, and the validator will not accept it
};

// How much text the field is for.
//
// `RichText` is declared and **not built**: it behaves as `Multiline` until somebody writes the runs,
// the editor and the toolbar that would make it mean something. A mode that quietly does the wrong
// thing is worse than one that says it is not there yet.
enum class TextMode {
    SingleLine,   // one line: Enter finishes the value, no wrapping
    Multiline,    // wraps, Enter is a line break, and the box has room for `lines` of them
    RichText,     // not built -- see above
};

struct TextBox;

// What arranges a field's own children, of which there is one: the bar. A field has no layout of a
// page's kind -- the page arranges *it* -- so this is only about the bar at the right-hand edge,
// which is laid over the text rather than taking a column of it. WinUI's bar does the same, and its
// reason applies here too: a field is as wide as the room it was given, and a bar that took a column
// would narrow the text on the frames it appeared.
struct FieldLayout : Layout {
    Want Measure(const Room &room) const override;
    void Arrange(const Room &room, const D2D1_RECT_F &box) override;
};

struct TextBox : Widget {
    std::wstring text;
    size_t caret  = 0;      // index into `text`
    size_t anchor = 0;      // the other end of the selection; equal to caret when none
    float  scroll = 0.0f;   // how far the text is scrolled left, in DIPs
    float  scrollY = 0.0f;  // and up, which a field with one line has no use for
    // The bar, a child of the field. Hidden unless there is something to scroll -- see `BarNumbers`
    // -- and laid out by `FieldLayout`.
    ScrollBar *bar = nullptr;
    std::wstring placeholder;
    // The field holds a file-system path. Paste then also drops the quotes that
    // Explorer's "Copy as path" puts round what it copies, and any trailing spaces --
    // neither is part of the path, and both are what somebody would have to delete by
    // hand. Off for ordinary text, where a quoted word is meant.
    bool pathField = false;

    // ---- what may go in, and what the field says when it will not ---------------------------------
    //
    // **One door for every way in.** A character typed, a character pasted, a value the page sets
    // and a value a client sets all go through `Admitted`, so a rule cannot be enforced on one of
    // them and forgotten on the rest -- which is exactly how a field ends up taking from the
    // clipboard what it would not take from the keyboard.
    //
    // Nothing here is a validator's job alone: the field owns the *shape* of what it holds (a
    // single-line field holds no line breaks) and the page owns what counts as a *value*.

    // Which characters may come in. Empty means all of them. A refused character is refused
    // everywhere, paste included.
    std::function<bool(wchar_t)> filter;
    // What the text is meant to be, asked on every change. See `Validation`.
    std::function<Validation(const std::wstring &)> validate;
    // How much text the field will hold, 0 for no limit at all -- which is the answer rather than a
    // very large number, because a limit nobody chose is a limit somebody will hit.
    //
    // Applied to the keyboard, the clipboard and `SetText` alike. WinUI's own `MaxLength` leaves
    // pasted text alone, and that is a hole this library does not have.
    size_t maxLength = 0;
    // How many lines the field will hold, 0 for no limit. Only a field that has more than one line
    // can have more than one line of them.
    size_t maxLines = 0;

    // What the field is for, and how tall it is. **Both are plain fields**: setting them marks the
    // field's own layout stale and nothing else, because a property change that rearranged the page
    // under the pointer would be a page reacting to a keystroke. Nothing is arranged until the frame
    // that was already coming. See `Widget::InvalidateLayout`.
    TextMode mode = TextMode::SingleLine;
    size_t lines = 3;
    // More than one line: wrapping, line breaks that stay, and a box that is `lines` of them tall.
    bool Wraps() const { return mode != TextMode::SingleLine; }
    // `RichText` is not built: see `TextMode`.

    // Told when something was turned away. See `Refusal`.
    std::function<void(Refusal, const std::wstring &)> onRefused;
    // The last thing `validate` said, and whether it still stands. **Lazy**: the answer is asked for
    // when it is needed -- by the underline that paints it, by `State`, and by a change -- rather
    // than by every way the text can move. `SetTextRaw` in particular only marks it stale, because
    // not paying for that call is the whole reason it exists. `Acceptable` while no validator is
    // set, so the ordinary field has one answer and no branch.
    mutable Validation validation = Validation::Acceptable;
    mutable bool validationStale = false;
    std::function<void(const std::wstring &)> onChange;
    // Fired when the field is finished with -- Enter, or focus leaving it -- and not
    // on every keystroke. See Widget::OnBlur for why a text field needs both.
    std::function<void(const std::wstring &)> onCommit;
    // What the field held when it was focused. Enter and a blur both mean "here is what it is now",
    // and what it is now is what it was: nothing has happened. Without this baseline a page that
    // writes a file, or marks itself dirty, on a commit does so for the act of clicking into a
    // field -- and a "saved" indicator that appears because somebody clicked somewhere is worse
    // than no indicator. It is the rule `onChange` has always kept; see Slider::SetFromX.
    std::wstring atFocus;

    // The layout is a cache of what the text, the format and the theme already say, so
    // it is mutable: the queries below are const and are called from places that have no
    // business rebuilding text layout, the IME among them. Built on demand, dropped by
    // Dirty().
    mutable IDWriteTextLayout *layout = nullptr;
    // The width the layout above was made at, or -1 when there is none. See `Ensure`.
    mutable float layoutW = -1.0f;

    ~TextBox() override { if (layout) layout->Release(); }
    // The field makes its own bar. See the end of this header for what a field's layout arranges.
    TextBox();

    bool Focusable() const override { return true; }
    bool TextCursor() const override { return true; }
    void OnFocus() override { atFocus = text; }
    void OnBlur() override { Commit(); }

    // A field is as wide as the room it is given -- it is the thing a page stretches. **One control
    // tall while it holds one line, and one line taller for each line after that**: the box is not
    // measured from the text. A field that grew as somebody typed into it would move everything
    // under it on every keystroke, which is a page that reacts rather than a field that scrolls.
    micula::Want Measure(const Room &room) const override {
        float h = room.spec->controlH;
        const size_t rows = Rows();
        if (rows > 1 && room.fonts) h += (float)(rows - 1) * RowH(*room.fonts);
        return micula::Want(Axis::Fill(), Axis::Fixed(h));
    }

    // Fires onCommit when there is something to commit, and not otherwise. The baseline is taken
    // again after the callback, because a page is allowed to put the value back into the field -- a
    // port of 0080 is 80 -- and what it put back is the value the field now stands for.
    void Commit() {
        if (text == atFocus) return;
        if (onCommit) onCommit(text);
        atFocus = text;
    }

    // A text field to a screen reader: the text in it, so somebody who cannot see the screen can
    // read back what was typed. Deliberately no name from the placeholder: a placeholder is a hint
    // about the format and not a label -- a client reading "C:\Path\to\folder" out as the name of
    // the field is worse than reading nothing -- so the page names the field with `accessibleName`,
    // which is what the words beside it are for. See the Accessibility section of docs/window.md.
    int AccessibleType() const override { return UIA_EditControlTypeId; }
    // **What typing into it is.** The text goes in through `onChange` and is committed, because a field
    // a client has just filled in is a field that has been filled in -- a page that only listens for a
    // commit would otherwise never hear about it -- and the caret goes to the end, as it does after a
    // paste. `SetText` is what a page uses to put a value in without anybody having typed it.
    bool AccessibleWritable() const override { return true; }
    bool AccessibleSetValue(const std::wstring &s) override {
        SetText(s);
        if (onChange) onChange(text);
        if (onCommit) onCommit(text);
        Invalidate();
        return true;
    }
    bool AccessibleValue(std::wstring &out) const override {
        if (text.empty()) return false;
        out = text;
        return true;
    }

    // Fluent's text field padding: 11 DIPs each side, and the border's own DIP is what the clip is
    // inset by. See `Paint`.
    float InnerLeft() const  { return rect.left + 11.0f; }
    float InnerWidth() const { return Width(rect) - 22.0f; }
    float InnerHeight() const { return Height(rect) - 2.0f; }

    // One line of the body font, which is what a field with more than one line is made of. Asked of
    // the fonts rather than of a layout, because `Measure` has no layout to ask -- and the two agree
    // because it is the same format either way.
    static float RowH(const Fonts &f) { return f.WrappedHeight(f.body, L"X", 1000.0f); }
    // Where the first line is drawn: centred in what one control's height would have been, which is
    // exactly where a single-line field puts its text. Every line after it is one row lower, so the
    // box grows downwards and the text does not move.
    float FirstLineTop(float rowH) const {
        const float box = Height(rect) - (float)(Rows() - 1) * rowH;
        return rect.top + (box - rowH) * 0.5f;
    }
    // At least one line, always: a field that holds nothing still holds a line to type it into.
    size_t Rows() const { return Wraps() && lines > 0 ? lines : 1; }

    // What of `in` the field will take, with `base` as the length the text already stands at once
    // whatever is selected has gone. One pass, and one report per rule rather than one per
    // character: what a page needs to know is that an attempt was refused, and roughly why.
    std::wstring Admitted(const std::wstring &in, size_t base, bool raw) const {
        if (raw) return in;
        std::wstring out;
        out.reserve(in.size());
        bool refusedChar = false, refusedLen = false, refusedLine = false;
        size_t breaks = maxLines ? BreaksIn(BaseText()) : 0;
        for (wchar_t ch : in) {
            if (filter && !filter(ch)) { refusedChar = true; continue; }
            if (maxLength && base + out.size() >= maxLength) { refusedLen = true; continue; }
            if (ch == L'\n' || ch == L'\r') {
                // **The field's own shape**, which is the one rule a page cannot talk it out of: a
                // single-line field has no room for a break at all, and a field with a `maxLines` no
                // room for another. What is left of the text is dropped, which is where a single-line
                // field's paste has always ended -- the *door* is what moved, not the rule.
                if (!Wraps() || (maxLines && breaks + 1 >= maxLines)) { refusedLine = true; break; }
                if (ch == L'\n') breaks++;
            }
            out.push_back(ch);
        }
        if (onRefused) {
            if (refusedChar) onRefused(Refusal::Character, in);
            else if (refusedLen) onRefused(Refusal::Length, in);
            else if (refusedLine) onRefused(Refusal::Lines, in);
        }
        return out;
    }
    // The text as it will stand once the selection has gone, which is what a limit is measured
    // against: a break being deleted makes room for one that is arriving, and a character being
    // replaced is not one the field has to find room for.
    std::wstring BaseText() const {
        if (!HasSelection()) return text;
        std::wstring out = text.substr(0, SelLo());
        out += text.substr(SelHi());
        return out;
    }
    static size_t BreaksIn(const std::wstring &s) {
        size_t n = 0;
        for (wchar_t ch : s)
            if (ch == L'\n') n++;
        return n;
    }
    // The text after the selection has gone: what a change is measured against.
    size_t Base() const { return text.size() - (HasSelection() ? SelHi() - SelLo() : 0); }
    // What the validator says now, at most once per change. `report` is for the two paths that took
    // the answer as the text changed: a page wants to say "that is not an address" once, and not
    // again on every frame the answer happens to be looked at while somebody types one.
    void Resolve(bool report) const {
        if (!validationStale) return;
        validationStale = false;
        const Validation before = validation;
        validation = validate ? validate(text) : Validation::Acceptable;
        if (report && validation == Validation::Invalid && before != Validation::Invalid && onRefused)
            onRefused(Refusal::Invalid, text);
    }
    // The answer, for a page that wants to look at it -- and the only way to hear about a
    // `SetTextRaw` that skipped the question. Asking is a page's own act, so the transition is
    // reported from here as well; what does *not* report is the underline, which resolves the same
    // answer while painting and has no business calling a page back from inside a paint.
    Validation State() const {
        Resolve(true);
        return validation;
    }

    // **The page's own value, subject to the same rules as anybody else's.** A field that took from
    // its page what it would not take from the keyboard is a field with two sets of rules. What
    // this cannot do is complain usefully -- there is nobody to complain to but `onRefused`, which
    // is called anyway.
    void SetText(const std::wstring &s) {
        text = Admitted(s, 0, false);
        caret = anchor = text.size();
        Dirty();
        validationStale = true;
        Resolve(true);
    }
    // The same, unchecked: for a page that has validated the value itself, or that is restoring a
    // value the field refused long ago and would refuse again. **Nothing else skips the rules** --
    // in particular the field's own shape is not a page's to break -- and the validator is not
    // called either, which is the other half of what this is for: the answer is marked stale and
    // asked for only if somebody looks.
    void SetTextRaw(const std::wstring &s) {
        text = s;
        caret = anchor = text.size();
        Dirty();
        validationStale = true;
    }
    // **A layout is only good for the width it was made at**, and a wrapped one is the whole reason:
    // the same text at another width is another set of lines. Remade when the width moves, and not
    // on every measure -- which is what a page animating its own size would otherwise pay per frame.
    void Dirty() const {
        if (layout) { layout->Release(); layout = nullptr; }
        layoutW = -1.0f;
    }
    void Ensure() const {
        Surface *w = surface();
        if (!w || !w->fonts.dw || !w->fonts.body) return;
        const float want = Wraps() ? InnerWidth() : 0.0f;
        if (layout && layoutW != want) Dirty();
        if (layout) return;
        w->fonts.dw->CreateTextLayout(text.c_str(), (UINT32)text.size(), w->fonts.body,
                                      Wraps() ? want : 100000.0f, 100000.0f, &layout);
        if (!layout) return;
        // The shared body format is vertically centred, because every other call site
        // hands DirectWrite a rectangle the size of its control and wants the text in
        // the middle of it. A *layout* is different: it centres within its own
        // `maxHeight` -- which is why that is a number nothing will reach -- so the
        // glyphs would land below the origin they are drawn at, outside a 32-DIP field,
        // and be clipped away. The field came up empty and the box around it did not,
        // which is a fault that looks like the text was never set.
        layout->SetWordWrapping(Wraps() ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        layoutW = want;
    }
    // Where the caret for an index sits, in the layout's own space: its own line and column, which is
    // the whole difference between a field that can hold more than one line and one that cannot.
    D2D1_POINT_2F CaretAt(size_t index) const {
        Ensure();
        if (!layout) return D2D1::Point2F(0.0f, 0.0f);
        FLOAT x = 0, y = 0;
        DWRITE_HIT_TEST_METRICS m = {};
        layout->HitTestTextPosition((UINT32)index, FALSE, &x, &y, &m);
        return D2D1::Point2F(x, y);
    }
    float XOf(size_t index) const { return CaretAt(index).x; }
    // A point in the field's own space, turned into a place in the text: the origin above has to come
    // off both halves of it, which is what a field with more than one line cannot avoid asking.
    size_t IndexAtPoint(float x, float y, size_t *under = nullptr) const {
        const D2D1_POINT_2F at = TextOrigin(RowHere());
        return IndexAt(x - at.x, y - at.y, under);
    }
    // The row height, asked of the surface's fonts -- the same place `Measure` takes it from, so the
    // box and the caret agree about where a line ends. Zero before there is a surface to ask.
    float RowHere() const {
        Surface *w = surface();
        return w ? RowH(w->fonts) : 0.0f;
    }
    // One line, which is what a caret is drawn as tall and what a scroll moves by. **Asked of the
    // fonts, the same way `Measure` asks**: the box and the caret then cannot disagree about how tall
    // a line is, which is the one thing a field with several of them has to get right.
    float LineH() const {
        Surface *w = surface();
        return w ? RowH(w->fonts) : 0.0f;
    }

    // --- the bar, and the two ways a field is scrolled -------------------------------------------

    // **The bar is told, not asked.** It is a child, so its rectangle is the layout's business (see
    // `FieldLayout`), but what it *shows* changes on every keystroke and on every notch, and asking
    // the tree to lay out again for each of those would be a page-wide arrangement per character.
    // So the field keeps the bar's numbers itself, and the layout never has to run for a scroll.
    void BarNumbers() {
        if (!bar) return;
        const float box = InnerHeight();
        const float all = TextHeight();
        bar->viewport = box;
        bar->extent = all;
        bar->value = scrollY;
        bar->drawn = scrollY;
        bar->visible = Wraps() && all > box;
        bar->Poll();
    }
    // Scroll the text, from a notch or from the bar being dragged. Clamped to the text: the last line
    // is readable to its end rather than scrolled past.
    void ScrollTo(float to) {
        const float most = (std::max)(0.0f, TextHeight() - InnerHeight());
        const float at = std::clamp(to, 0.0f, most);
        if (at == scrollY) return;
        scrollY = at;
        if (bar) bar->Wake();
        BarNumbers();
        Invalidate();
    }
    // **The wheel scrolls the text when the pointer is over it, and does not care where the focus is.**
    // That is the opposite of the drop-down's rule, and deliberately: there the wheel *changes a
    // value*, which only a control being worked on should do -- and here it *scrolls*, which anything
    // under the pointer may do. WinUI puts it the same way: scrolling is "automatically enabled when
    // needed" on a text box, and the scroll bar is conscious of the pointer rather than of the focus.
    //
    // A notch with nowhere to go is passed on, the way `ScrollView` passes one on: a field that has
    // scrolled to its end is not in the way of the page's own scroll.
    bool OnWheel(float, float, float notches) override {
        if (!Wraps() || notches == 0.0f) return false;
        if (TextHeight() - InnerHeight() <= 0.0f) return false;
        const float perNotch = SystemWheelLines();
        ScrollTo(scrollY - notches * (perNotch > 0.0f ? perNotch : 3.0f) * LineH());
        return true;
    }
    // How tall the text stands, lines and all: what a field with more than one line scrolls over.
    float TextHeight() const {
        Ensure();
        if (!layout) return 0.0f;
        DWRITE_TEXT_METRICS tm = {};
        layout->GetMetrics(&tm);
        return tm.height;
    }
    // The gap nearest a point, which is where a caret goes. `under`, if asked for, is
    // the character the point is actually over -- not the same thing in the right half
    // of a letter, and the one a double-click has to start from.
    //
    // `localY` is in the layout's own space, which is what a field with more than one line has to
    // hand over: a click on the second line is a click on the second line. A single-line field
    // passes zero and means it, because there is nowhere else for the point to be.
    size_t IndexAt(float localX, float localY, size_t *under = nullptr) const {
        Ensure();
        if (under) *under = 0;
        if (!layout) return 0;
        BOOL trailing = FALSE, inside = FALSE;
        DWRITE_HIT_TEST_METRICS m = {};
        layout->HitTestPoint(localX, localY, &trailing, &inside, &m);
        if (under) *under = (size_t)m.textPosition;
        return (size_t)m.textPosition + (trailing ? 1u : 0u);
    }
    // The layout's own origin as it is drawn, which is what a point has to be measured against. One
    // line and it is the corner: the layout is drawn where the text is centred and the hit test asks
    // about nothing but the column.
    D2D1_POINT_2F TextOrigin(float rowH) const {
        if (!Wraps()) return D2D1::Point2F(InnerLeft() - scroll, 0.0f);
        return D2D1::Point2F(InnerLeft() - scroll, FirstLineTop(rowH) - scrollY);
    }

    bool HasSelection() const { return caret != anchor; }
    size_t SelLo() const { return caret < anchor ? caret : anchor; }
    size_t SelHi() const { return caret < anchor ? anchor : caret; }
    std::wstring Selected() const {
        return HasSelection() ? text.substr(SelLo(), SelHi() - SelLo()) : std::wstring();
    }
    void DeleteSelection() {
        if (!HasSelection()) return;
        text.erase(SelLo(), SelHi() - SelLo());
        caret = anchor = SelLo();
        Dirty();
    }
    void Changed() {
        Dirty();
        validationStale = true;
        Resolve(true);
        if (onChange) onChange(text);
    }

    // Keep the caret in view. Without this a path longer than the field types itself
    // off the right-hand edge and the person is editing something they cannot see.
    void ScrollToCaret() {
        const D2D1_POINT_2F at = CaretAt(caret);
        if (at.x - scroll > InnerWidth() - 2) scroll = at.x - InnerWidth() + 2;
        if (at.x - scroll < 0)                scroll = at.x;
        const float full = CaretAt(text.size()).x;
        if (full - scroll < InnerWidth() && scroll > 0)
            scroll = full > InnerWidth() ? full - InnerWidth() : 0.0f;
        if (scroll < 0) scroll = 0;
        // **And down**, which is the half a field with one line has no use for. The caret's own line
        // is what has to stay visible, and the scroll is clamped to the text so that the last line is
        // readable to its end rather than scrolled past.
        if (!Wraps()) {
            scrollY = 0.0f;
            return;
        }
        const float row = LineH();
        const float box = InnerHeight();
        if (at.y - scrollY + row > box) scrollY = at.y + row - box;
        if (at.y - scrollY < 0)         scrollY = at.y;
        scrollY = std::clamp(scrollY, 0.0f, (std::max)(0.0f, TextHeight() - box));
        BarNumbers();
    }

    // --- the mouse ---------------------------------------------------------------
    //
    // The caret moves on the press, not on the release. It used to move in OnClick,
    // which is the release, and that left a drag nowhere to start from: the field could
    // not select anything with the mouse, and a user reported it as a text box that was
    // only a picture of one.
    bool  selecting = false;       // between a press and its release
    // The window class has no CS_DBLCLKS -- with it, the second click of a quick pair
    // arrives as WM_LBUTTONDBLCLK, and every button in the window would miss it -- so a
    // double-click reaches this as two presses and is recognised here.
    DWORD lastPressTime = 0;
    float lastPressX = -1.0e6f;

    bool TracksPointer() const override { return selecting; }
    bool Dragging() const override { return selecting; }

    void OnPress(float x, float y) override {
        size_t under = 0;
        const size_t at = IndexAtPoint(x, y, &under);
        const DWORD now = (DWORD)GetMessageTime();
        Surface *w = surface();
        const float slop = (float)GetSystemMetrics(SM_CXDOUBLECLK) / 2.0f /
                           (w ? w->scale() : 1.0f);
        const bool twice = lastPressTime != 0 && now - lastPressTime <= GetDoubleClickTime() &&
                           std::fabs(x - lastPressX) <= slop;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        lastPressX = x;
        if (twice && !shift) {
            SelectPieceAt(under);
            lastPressTime = 0;     // a third click is a fresh one, not a second pair
            selecting = false;
        } else {
            lastPressTime = now;
            caret = at;
            if (!shift) anchor = at;
            selecting = true;
        }
        ScrollToCaret();
    }
    void OnDrag(float x, float y) override {
        if (!selecting) return;
        caret = IndexAtPoint(x, y);
        ScrollToCaret();
    }
    void OnRelease() override { selecting = false; }

    // Nothing left to do: the press has placed the caret. This used to place it from
    // GetCursorPos, and Space reaches OnClick from the keyboard -- so every space typed
    // into `Program Files` moved the caret to wherever the pointer was resting and went
    // in there. OnKey now keeps Space from coming here at all; this is the other half.
    void OnClick() override {}

    // A double-click selects one piece of the text: the run between two separators,
    // where a separator is a backslash, a slash or a space. Spaces alone would select
    // `Files\Micula` out of `C:\Program Files\Micula`, and the piece somebody
    // double-clicks in a path to replace is one folder name.
    static bool Separator(wchar_t ch) {
        return ch == L'\\' || ch == L'/' || ch == L' ' || ch == L'\t';
    }
    void SelectPieceAt(size_t at) {
        if (text.empty()) { caret = anchor = 0; return; }
        if (at >= text.size()) at = text.size() - 1;
        if (Separator(text[at])) { anchor = at; caret = at + 1; return; }
        size_t lo = at, hi = at;
        while (lo > 0 && !Separator(text[lo - 1])) lo--;
        while (hi < text.size() && !Separator(text[hi])) hi++;
        anchor = lo;
        caret = hi;
    }

    // What of the clipboard this field keeps: **nothing that is the field's own shape to keep.** A
    // break in a single-line field, and a break past `maxLines` in one with more, are refused at the
    // door like anything else -- see `Admitted` -- so what is left here is the one transformation
    // that is about the *clipboard* rather than about the field.
    std::wstring Pasted(std::wstring in) const {
        if (pathField) {
            while (!in.empty() && in.back() == L' ') in.pop_back();
            if (in.size() >= 2 && in.front() == L'"' && in.back() == L'"')
                in = in.substr(1, in.size() - 2);
        }
        return in;
    }

    bool OnChar(wchar_t ch) override {
        if (!enabled) return false;
        // Asked before anything moves: what fits depends on the length the text will stand at, and
        // the selection is on its way out. Answered either way -- a character this field will not
        // take is still this field's to answer for, and returning false here would hand it to the
        // window as a mnemonic. See the note on VK_SPACE below.
        const std::wstring ok = Admitted(std::wstring(1, ch), Base(), false);
        if (ok.empty()) return true;
        DeleteSelection();
        text.insert(caret, ok);
        caret = anchor = caret + ok.size();
        Changed();
        ScrollToCaret();
        return true;
    }

    bool OnKey(WPARAM vk) override {
        if (!enabled) return false;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

        if (ctrl) {
            HWND hwnd = surface() ? surface()->hwnd : nullptr;
            switch (vk) {
            case 'A': anchor = 0; caret = text.size(); return true;
            // **The other way to finish a value**, and the one a field with more than one line
            // needs: Enter is a line break there, so leaving the field is what a commit would
            // otherwise have to wait for. See VK_RETURN below.
            case VK_RETURN: Commit(); return true;
            case 'C': if (HasSelection()) micula::SetClipboardText(hwnd, Selected());
                      return true;
            case 'X': if (HasSelection()) {
                          micula::SetClipboardText(hwnd, Selected());
                          DeleteSelection();
                          Changed();
                      }
                      return true;
            case 'V': {
                const std::wstring in = Admitted(Pasted(micula::ClipboardText(hwnd)), Base(), false);
                if (in.empty()) return true;
                DeleteSelection();
                text.insert(caret, in);
                caret = anchor = caret + in.size();
                Changed();
                ScrollToCaret();
                return true;
            }
            default: return false;
            }
        }

        switch (vk) {
        case VK_RETURN: {
            // Consumed rather than left to the window, which would otherwise read it
            // as the default button while the person was still in the field. **One line and it is the
            // end of the value**; more than one and it is a line break, which is what a field the
            // height of several lines is for -- the value is finished by leaving it, or with
            // Ctrl+Enter. It goes through the same door as everything else, so a `maxLines` can
            // refuse it.
            if (!Wraps()) {
                Commit();
                return true;
            }
            const std::wstring ok = Admitted(L"\r\n", Base(), false);
            if (ok.empty()) return true;
            DeleteSelection();
            text.insert(caret, ok);
            caret = anchor = caret + ok.size();
            Changed();
            ScrollToCaret();
            return true;
        }
        case VK_SPACE:
            // The space itself arrives as WM_CHAR. The key is consumed so the window does
            // not also read it as "operate the focused control" -- see OnClick.
            return true;
        case VK_LEFT:
            if (caret > 0) caret--;
            if (!shift) anchor = caret;
            break;
        case VK_RIGHT:
            if (caret < text.size()) caret++;
            if (!shift) anchor = caret;
            break;
        case VK_HOME:
            caret = 0;
            if (!shift) anchor = caret;
            break;
        case VK_END:
            caret = text.size();
            if (!shift) anchor = caret;
            break;
        case VK_BACK:
            if (HasSelection()) DeleteSelection();
            else if (caret > 0) { text.erase(caret - 1, 1); caret = anchor = caret - 1; }
            else return true;
            Changed();
            break;
        case VK_DELETE:
            if (HasSelection()) DeleteSelection();
            else if (caret < text.size()) text.erase(caret, 1);
            else return true;
            Changed();
            break;
        default:
            return false;
        }
        ScrollToCaret();
        return true;
    }

    // Where the IME should open: at the caret, in the field's own space.
    //
    // Measured now rather than read from a position cached by the last paint. This is
    // called from the message loop when a composition starts, which can arrive before
    // the WM_PAINT for a key that has just moved the caret, and the candidate window
    // then opened wherever the caret used to be. Nothing here depends on a paint having
    // happened: the layout is created on demand -- see Ensure.
    bool CaretPoint(D2D1_POINT_2F *out) const override {
        const D2D1_POINT_2F at = CaretAt(caret);
        // The bottom of the caret's own line, which for one line is the bottom of the field -- where
        // the candidate window has always opened.
        const float y = Wraps() ? (FirstLineTop(RowHere()) - scrollY + at.y + LineH()) : rect.bottom;
        if (out) *out = D2D1::Point2F(InnerLeft() + at.x - scroll, y);
        return true;
    }

    void Paint(const Painter &p) override {
        const Palette &c = *p.pal;
        const bool active = focus;

        // Three states crossing over: the resting fill, the hover fill, and the lighter
        // one Fluent gives a field with the caret in it (ControlFillColorInputActive,
        // which is opaque white in light and 70% #1E1E1E in dark). The focus fill wins
        // over hover, which is why it is the outer mix.
        p.FillRound(rect, metric::kRadiusControl,
                    !enabled ? c.controlBg
                             : Mix(Mix(c.controlBg, c.controlBgHover, hoverT),
                                   c.controlBgInput, focusT));
        p.StrokeRound(rect, metric::kRadiusControl, c.controlStroke);
        // The accent underline, **whole, the moment the field takes focus.**
        //
        // It used to grow out of the centre, which is Material Design's text field and
        // not Fluent's: WinUI changes `BorderThickness` to 0,0,0,2 in a visual-state
        // setter, and a setter has no duration. The fill behind it is the part that
        // crosses over, and it does that above.
        Resolve(false);
        p.Line(rect.left + metric::kRadiusControl, rect.bottom - 1,
               rect.right - metric::kRadiusControl, rect.bottom - 1,
               validation == Validation::Invalid
                   ? c.bad
                   : (active && enabled ? c.accent : c.controlStrokeBottom),
               active && enabled ? 2.0f : 1.0f);

        Ensure();

        const D2D1_RECT_F inner = { InnerLeft(), rect.top + 1, rect.right - 11, rect.bottom - 1 };
        p.rt->PushAxisAlignedClip(inner, D2D1_ANTIALIAS_MODE_ALIASED);

        // **Where the layout goes.** One line: where the text has always been centred, and the hit
        // test asks about the column alone. More than one: the first line where that same text would
        // have been, every line after it a row lower, and the whole thing raised by however much the
        // caret has needed to stay in view. `TextOrigin` is the same expression, so a click lands
        // where the glyphs are.
        const float row = p.font ? RowH(*p.font) : 0.0f;
        const float originY = Wraps() ? (FirstLineTop(row) - scrollY)
                                      : (rect.top + (Height(rect) - TextHeight()) / 2);
        const float originX = inner.left - scroll;

        if (text.empty() && !placeholder.empty()) {
            p.Text(placeholder, inner, p.font->body, c.textDisabled);
        } else if (layout) {
            // **The selection is the field's; the highlight is the focus's.** A selection survives a
            // blur -- clicking elsewhere must not throw away what was picked, and neither Windows' own
            // edit control nor WinUI clears it -- but the highlight does not. WinUI keeps two brushes
            // for exactly this pair, `SelectionHighlightColor` and
            // `SelectionHighlightColorWhenNotFocused`, and the second one's default is *transparent*:
            // a field that is not being worked on holds a selection that cannot be seen, and shows it
            // again the moment it is focused. What is drawn here is the first of the two.
            const D2D1_COLOR_F wash = D2D1::ColorF(c.accent.r, c.accent.g, c.accent.b, 0.35f);
            if (active && HasSelection()) {
                if (Wraps()) {
                    // **Per line, which is the whole difference**: a selection that runs over a break
                    // is not one rectangle. The range API answers with the boxes the text actually
                    // occupies -- the boxes the caret walks along -- and is asked twice, because the
                    // first answer is only how many there are.
                    UINT32 got = 0;
                    const UINT32 n = (UINT32)(SelHi() - SelLo());
                    layout->HitTestTextRange((UINT32)SelLo(), n, originX, originY, nullptr, 0, &got);
                    if (got) {
                        std::vector<DWRITE_HIT_TEST_METRICS> boxes(got);
                        if (SUCCEEDED(layout->HitTestTextRange((UINT32)SelLo(), n, originX, originY,
                                                               boxes.data(), got, &got)))
                            for (UINT32 i = 0; i < got; i++)
                                p.Fill({ boxes[i].left, boxes[i].top,
                                         boxes[i].left + boxes[i].width,
                                         boxes[i].top + boxes[i].height }, wash);
                    }
                } else {
                    const float a = XOf(SelLo()) - scroll, b = XOf(SelHi()) - scroll;
                    // The accent at a third, which is what Fluent's selection highlight
                    // is: the text stays its own colour and reads through it.
                    p.Fill({ inner.left + a, rect.top + 5, inner.left + b, rect.bottom - 5 }, wash);
                }
            }
            // Vertically centred by hand for one line: the layout is drawn from its origin and its own
            // height is the line height, not the box's. More than one and the origin above is what has
            // already worked that out.
            p.rt->DrawTextLayout(D2D1::Point2F(originX, originY), layout,
                                 p.Brush(enabled ? c.textPrimary : c.textDisabled),
                                 D2D1_DRAW_TEXT_OPTIONS_NONE);
        }

        if (active && enabled) {
            Surface *w = surface();
            if (w && w->caretOn) {
                const D2D1_POINT_2F at = CaretAt(caret);
                const float x = inner.left + at.x - scroll;
                // A caret is as tall as the line it is in, which a field with one line never had to
                // say: there, it is the box.
                const float y = Wraps() ? (originY + at.y) : (rect.top + 6);
                const float h = Wraps() ? LineH() : (rect.bottom - 6 - (rect.top + 6));
                p.Line(x, y, x, y + h, c.textPrimary, 1.0f);
            }
        }
        p.rt->PopAxisAlignedClip();
    }
};

// What a field's own layout arranges: the bar, over the right-hand edge rather than in a column of
// its own, the way WinUI's lies over the page it belongs to. Its numbers are the field's business and
// are kept up to date by `BarNumbers` -- a scroll must not be a reason to lay a page out again.
inline Want FieldLayout::Measure(const Room &room) const {
    (void)room;
    return Want(Axis::Fill(), Axis::Fill());
}

inline void FieldLayout::Arrange(const Room &room, const D2D1_RECT_F &box) {
    (void)room;
    TextBox *f = static_cast<TextBox *>(host_);
    if (!f || !f->bar) return;
    const float inset = 2.0f;
    f->bar->rect = { box.right - inset - ScrollBar::kSize, box.top + inset, box.right - inset,
                     box.bottom - inset };
    // The region the bar watches outside itself, and it is written in the space the pointer is handed
    // to it in: a widget's own `rect` and the points it is given share one origin, which for the bar is
    // the field's. So the field's own box, whole -- a pointer anywhere over the field brings the bar
    // out. Not a hit test region: a click out there is a click on the field, which is where the caret
    // goes.
    f->bar->area = box;
    f->BarNumbers();
}

inline TextBox::TextBox() {
    // The field's own layout, which arranges the bar and nothing else: the page arranges the field.
    SetLayout(new FieldLayout());
    // Qualified, because this class hides `Add`: the bar is the field's own child rather than
    // something a page is putting in it.
    bar = Widget::Add(new ScrollBar([this](float to, bool) { ScrollTo(to); }));
    // Nothing to scroll yet, and a bar that has never been arranged would otherwise flash at the
    // right-hand edge before the first arrangement. See ScrollBar::Poll, which puts it away itself.
    bar->visible = false;
}

}  // namespace micula
