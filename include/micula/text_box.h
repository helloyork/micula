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

// One run of the text, drawn differently from the field's own font.
//
// **The run is the primitive, not the line**: a line's look is whatever its runs add up to, so anything
// a page wants to say about a line -- a heading, a quoted block, the row of a log -- is said by giving
// that line's characters a run. Nothing here knows what a line is, and that is what leaves the wrapping,
// the caret, the hit test and the scroll alone -- they go on being the layout's own answers, and simply
// follow the runs. A colour or an underline moves nothing at all; a *format* can change how wide a word
// is, which is exactly what a page that asked for one asked for.
//
// Offsets are into `TextBox::text`, so a page that edits the text owns their upkeep -- which is what
// `TextBox::formatRuns` exists to avoid: an engine asked for the runs again whenever the text changes
// never has to know how it changed.
struct Run {
    size_t at = 0;                 // where in the text the run starts
    size_t len = 0;                // how much of it is drawn this way
    // **What is different about it, and every part of it means "the field's own" when unset.** The
    // format is one the page made, like a field's own `font`: one for a keyword, one for a string, the
    // same object used again for every run that wants it. Per-range formatting is expressed to DirectWrite
    // in pieces rather than as a format, and a page should not have to know that. **Held rather than
    // borrowed**: a run keeps its format alive, so an engine that rebuilds its formats when something
    // else changes -- a theme, a setting -- can hand a fresh set over and stop thinking about the old one.
    Font font;
    bool hasColor = false;
    D2D1_COLOR_F color = {};
    bool underline = false;
};

struct TextBox;
struct ClearButton;

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
    // **Where the caret is trying to be, while the keys moving it are vertical ones.** Down through a line
    // shorter than the column somebody was on clamps the caret to that line's end, and the run of presses
    // has to come back out to the column it started from rather than creep leftwards -- see `VK_UP`. In the
    // layout's own x, and -1 when there is no such run. An editor does this and the platform's own edit
    // control does not: that one takes the column it finds on the line it is leaving, so a run of presses
    // through a short line walks a caret leftwards and leaves it there.
    float preferredX = -1.0f;
    float  scroll = 0.0f;   // how far the text is scrolled left, in DIPs
    float  scrollY = 0.0f;  // and up, which a field with one line has no use for
    // The bar, a child of the field. Hidden unless there is something to scroll -- see `BarNumbers`
    // -- and laid out by `FieldLayout`.
    ScrollBar *bar = nullptr;
    // **The clear button, and it is the page's to ask for.** Off by default: a field is not always a
    // field whose value can be emptied by a click, and the ones that can say so.
    bool showClearButton = false;
    // What a screen reader is told it is -- the glyph says nothing out loud -- and a library has no
    // language, so the page puts its own word here.
    std::wstring clearLabel = L"Clear";
    // The button itself, a child of the field like the bar and arranged by `FieldLayout`.
    ClearButton *clearButton = nullptr;
    std::wstring placeholder;
    // **A field that shows rather than takes.** Typing, the clipboard's cut and paste, Backspace, Delete
    // and a new line are refused -- and *only* those. The caret still moves, a drag still selects,
    // Ctrl+A still selects all, Ctrl+C still copies and the wheel still scrolls, which is the whole
    // difference between this and `enabled = false`: a disabled field is greyed out and answers none of
    // it. Measured against the platform's own read-only edit control, which refuses exactly that set and
    // keeps exactly that one.
    //
    // **The page is not who is being refused.** `SetText`, `SetTextRaw`, `Clear` and `SetRuns` take a
    // value whatever this says, because a field showing a log is a field whose page is still putting text
    // into it -- and a log is what this is for: a field somebody reads and copies out of while it grows.
    // What it does say to the outside is that a *client* cannot type into it either; see
    // `AccessibleWritable`.
    bool readOnly = false;
    // Refusing a keystroke is the field's answer, not the window's: a read-only field that returned false
    // would be handing its keys to the window as mnemonics. See `OnChar` and `OnKey`.
    void SetReadOnly(bool on) {
        readOnly = on;
        ClearState();   // the clear button is not shown to a field that may not be emptied
        Invalidate();
    }
    // **The line along the field's bottom edge, and whether taking the focus lights it up.** WinUI gives
    // a focused text box an accent underline two DIPs thick and this draws the same one -- see `Paint` --
    // so a page that is *showing* something rather than taking something, a log somebody reads or a value
    // read back out of a device, can ask for the resting edge instead. What is left is the edge every
    // other control has; the fill and the caret still say where the keyboard is. An `Invalid` answer is
    // still painted, because that is a report and not an emphasis.
    bool showAccentUnderline = true;
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

    // **What the field is for, how tall it is, and how far apart its lines are.** These three are the
    // field's *shape*, and every one of them changes the box -- which is the page's business, not the
    // field's: it was the page's layout that decided how tall this control is.
    TextMode mode = TextMode::SingleLine;
    size_t lines = 3;
    // A multiple of one line: 1 is what the font calls a line and anything else multiplies it, which
    // is all a "line height" setting ever is. Nothing to a field with one line -- there is no spacing
    // between one line.
    float lineSpacing = 1.0f;
    // **The setters, for a page that changes one of the three after the field is on screen.**
    // `InvalidateLayout` writes `Surface::layoutDirty` and does nothing else where it is called; the
    // next frame turns it into one arrangement of the content, which is the same pass a resize runs and
    // the only one there is. Nothing else about the field is the page's business: its *text* is `Dirty`
    // plus `Invalidate`, and the box does not depend on the text. Changing the fields directly is
    // allowed -- they are fields -- and owes an `InvalidateLayout`; these are the way that cannot be
    // forgotten.
    void SetMode(TextMode m) { mode = m; InvalidateLayout(); }
    void SetLines(size_t n) { lines = n; InvalidateLayout(); }
    void SetLineSpacing(float multiple) { lineSpacing = multiple; InvalidateLayout(); }
    // **The font the field draws with, or empty for the library's body font.** A page that wants a
    // field in something other than the body font -- a monospace for a path, a log, a code sample --
    // brings its own, and it is the field's *shape* like the three above: the lines are measured with
    // it, so the box, the caret and the 5em the clear button follows all come from it. The field holds
    // a *share* of it, so a page may let go of its own as soon as it has handed it over -- see `Font`.
    Font font;
    IDWriteTextFormat *TextFont(const Fonts &f) const { return font.TextFormat(f); }
    // Both halves are needed: the DirectWrite layout was made with the old format (`Dirty`) and the box
    // was measured against it (`InvalidateLayout`).
    void SetFont(const Font &f) { font = f; Dirty(); InvalidateLayout(); }

    // **The composition an input method is holding, and where its own caret sits in it.** It is not part of
    // the text: the field does not own it, nothing about it is saved, selected or undone, and the layout it
    // is drawn in is thrown away with it. But **it is content while it lasts** -- see `EnsureComposed` --
    // which is what makes it wrap in a field that wraps and push the text along in one that does not.
    std::wstring preedit;
    int preeditCaret = 0;

    // Told by the window, which is where the composition string can be read from the input context at all.
    // Nothing is filtered, refused or measured here: a composition is not text yet, and a field that
    // stepped on it would be a field that argues with an input method.
    void OnComposition(const std::wstring &composing, int at) override {
        preedit = composing;
        preeditCaret = at < 0 ? 0 : at;
        if ((size_t)preeditCaret > preedit.size()) preeditCaret = (int)preedit.size();
        // Where the two scrolls want to be is a question for the layout, and the layout is asked again the
        // next time it is needed -- so all this does is send them there: a pre-edit that has outgrown the
        // row it started on has moved the caret onto the next one.
        ScrollToCaret();
        Invalidate();
    }

    // --- the composition, laid out as content ----------------------------------------------------------
    //
    // **While an input method is holding a pre-edit, the pre-edit is content.** It stands where the caret
    // is -- or where a selection is, which is the same thing a keystroke does -- and is laid out by the very
    // same rules the text is: same format, same width, same runs, same wrapping. Everything follows from
    // that one decision:
    //
    //   * In a field that wraps, it wraps with the text: a sentence that outgrows the row it started on
    //     moves onto the next one and takes what follows it down with it. The pre-edit itself breaks between
    //     any two of its characters (see `Broken`), because it is a run of letters being typed rather than a
    //     word, while the text's own words go on being carried down whole. There is no sideways scroll in
    //     such a field to take part in, and none happens.
    //   * In a field with one line there is nothing to wrap into, so it pushes what follows it along and
    //     out of the box, and the scroll follows it -- the same scroll that follows the caret when somebody
    //     types without an input method.
    //
    // It is deliberately *not* spliced into `text`: the field's indices, the hit test, the selection, the
    // runs and the lines a key walks are the real text's, and a pre-edit that could be selected, saved or
    // undone would be a field arguing with an input method about what it holds. This layout is drawn and
    // measured and asked where the composition's own caret is -- see `Drawn`, which is the one place that
    // decides which of the two layouts a question about the *drawing* is answered by.
    mutable std::wstring composedFor;
    mutable IDWriteTextLayout *composed = nullptr;
    // Where that splice was made and how much of the text it stands in for. See `EnsureComposed`.
    mutable size_t composedAt = 0;
    mutable size_t composedTook = 0;

    // **Where the composition stands, and what it stands in for.** A selection is a range that a keystroke
    // replaces -- that is what `OnChar` does with one, what Windows does, and what makes Ctrl+A followed by
    // a letter mean "start again" -- so a composition stands *where the selection is* rather than beside
    // it: the pre-edit is drawn over the selected text, and what is on the screen is what a commit will
    // produce. With nothing selected it stands at the insertion point, which is the only place it can.
    //
    // **The selection itself is left alone**, which is the half this does not do: nothing is deleted while
    // a composition is up and the anchor is not moved, so cancelling one -- Esc, or an input method that
    // decides it has nothing -- puts the picture back exactly as it was. Deleting the selection when a
    // composition started would spend it on a keystroke somebody may not have meant.
    //
    // `SpliceAt` is where it stands; `ReplacedLen` is how much of the text it stands in for.
    size_t SpliceAt() const { return HasSelection() ? SelLo() : (std::min)(caret, text.size()); }
    size_t ReplacedLen() const { return HasSelection() ? SelHi() - SelLo() : 0; }

    // **The pre-edit with somewhere to break after every character**, which is what makes it fill the line
    // it is on instead of being carried down whole. DirectWrite breaks lines between *words*, and pinyin has
    // no spaces in it: `ni'hao'shi'jie'de'yi'qian` is one word to the breaker, and a word that does not fit
    // in what is left of a line is moved down as a unit -- measured, the line above it ends at x=34 of a
    // 128-DIP box with 94 DIP of it empty, and the paragraph takes four lines where three would do. U+200B
    // is a zero-width space: a place a line may break, with no advance and nothing drawn (measured: the
    // composed string comes to 121.1 DIP whether the pre-edit is written this way or the whole layout is set
    // to `CHARACTER` -- and that mode would give every character of the *text* a break as well, re-wrapping
    // the field's own words while somebody composes, which is not what a field being typed into should look
    // like).
    //
    // Only the pre-edit is written this way, and the library still needs no idea of what a word is: a word
    // of the text that lands on a boundary goes on moving down whole.
    static std::wstring Broken(const std::wstring &chars) {
        std::wstring out;
        out.reserve(chars.size() * 2);
        for (wchar_t ch : chars) { out += ch; out += L'\u200B'; }
        return out;
    }
    // How long the composition is where it stands: every character of it, and a place to break after each.
    size_t ComposedLen() const { return preedit.size() * 2; }
    // The composition's own caret, in the composed layout's indices: where the pre-edit stands, plus how
    // much of it the input method has taken -- two indices per character, since each is followed by the
    // place it may be broken at.
    size_t ComposedCaret() const {
        return SpliceAt() + (std::min)((size_t)preeditCaret, preedit.size()) * 2;
    }

    void DropComposed() const {
        if (composed) { composed->Release(); composed = nullptr; }
        composedFor.clear();
        composedAt = 0;
        composedTook = 0;
    }
    // **Asked once per frame and answered out of what is already there**, which is why the splice is
    // compared rather than made again: making it is a DirectWrite layout, and a layout per keystroke is the
    // point while a layout per question is not.
    bool EnsureComposed() const {
        if (preedit.empty()) return false;
        // The *shape* of the content -- the width it is cut to and the spacing between its lines -- is the
        // text's, and a change to either drops both layouts where the text's is built. See `Ensure`.
        Ensure();
        Surface *w = surface();
        IDWriteTextFormat *typeface = w ? TextFont(w->fonts) : nullptr;
        if (!w || !w->fonts.dw || !typeface) return false;
        const float want = Wraps() ? InnerWidth() : 0.0f;
        const float spacing = Wraps() ? lineSpacing : 1.0f;
        const size_t at = SpliceAt();
        const size_t took = ReplacedLen();
        std::wstring spliced;
        spliced.reserve(text.size() + preedit.size());
        spliced.assign(text, 0, at);
        spliced += Broken(preedit);
        spliced.append(text, at + took, std::wstring::npos);
        // The three things the string alone cannot say: where the pre-edit began, how much it stands in
        // for, and -- the string does say this -- how long it is. Two different stretches of text can
        // splice into the same characters, and the runs are laid on one of them differently.
        if (composed && composedFor == spliced && composedAt == at && composedTook == took) return true;
        DropComposed();
        composedFor = spliced;
        composedAt = at;
        composedTook = took;
        if (FAILED(w->fonts.dw->CreateTextLayout(spliced.c_str(), (UINT32)spliced.size(), typeface,
                                                 Wraps() ? want : 100000.0f, 100000.0f, &composed)) ||
            !composed)
            return false;
        // Set by hand for the reason `Ensure` sets them: the shared format is vertically centred, and a
        // layout centres within its own height -- which here is a number nothing reaches -- so the glyphs
        // would land far below the origin they are drawn at and be clipped away.
        composed->SetWordWrapping(Wraps() ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        composed->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        SpaceLines(composed, spacing);
        ApplyRuns(composed, at, took, ComposedLen());
        return true;
    }

    // **The layout as it is drawn**, which is the composed one while an input method holds a pre-edit and
    // the text's own the rest of the time. The wrapping, the height, the scroll, the caret and what the
    // input method is told are all answered by this one; every *index* question -- the hit test, the
    // selection, the lines a key walks -- stays on `layout`, which is the real text's.
    IDWriteTextLayout *Drawn() const {
        Ensure();
        if (preedit.empty()) return layout;
        return EnsureComposed() ? composed : layout;
    }
    // Whether the composition is in the layout that would be drawn. False when it could not be built, and
    // false for a field that will not draw one at all -- see `Paint`.
    bool ComposedIsDrawn() const { return !preedit.empty() && Drawn() == composed && composed != nullptr; }
    // How much text that layout holds.
    size_t DrawnLen() const {
        return ComposedIsDrawn() ? text.size() + ComposedLen() - ReplacedLen() : text.size();
    }
    // **Where the caret is drawn**, which is the composition's own caret while one is up: that is where the
    // person looking at the field sees it, rather than the insertion point it stands in front of.
    D2D1_POINT_2F DrawnCaret() const {
        if (ComposedIsDrawn()) return CaretAtIn(composed, ComposedCaret());
        return CaretAt(caret);
    }

    // **The runs the text is drawn with, and the engine that answers them.** See `Run`.
    //
    // A syntax highlighter is the case this exists for, and it is not the only one: a search that lights
    // up its hits, a validator that marks the part it did not like, a log coloured by the level of each
    // row -- all of them are this one function. The field hands it the text and asks it to fill the runs,
    // once per change, and the drawing, the caret and the hit test are the field's again. It is asked with
    // a vector to fill rather than one to return, so an engine that keeps a scratch vector pays nothing
    // per call; and it is asked for *all* of the runs rather than for what changed, so an engine that
    // wants to be incremental can compare inside itself and one that does not is correct by construction.
    std::function<void(const std::wstring &text, std::vector<Run> &out)> formatRuns;
    // What it answered, or what a page set by hand with no engine at all. **Set by hand, a page owns
    // their upkeep** -- every edit moves the offsets after it, and the field does not move them -- so a
    // page that intends to edit the text wants `formatRuns`.
    std::vector<Run> runs;
    void SetRuns(std::vector<Run> s) {
        runs.swap(s);
        Dirty();
        Invalidate();
    }
    // Ask the engine again, for a page whose answer comes from something other than the text: a theme, a
    // setting, which of its own rules is switched on.
    void Reformat() {
        if (formatRuns) { runs.clear(); formatRuns(text, runs); }
        // The text is not what somebody was aiming at any more: whatever column a run of vertical moves
        // was holding, it was about the text as it was.
        preferredX = -1.0f;
        Dirty();
        Invalidate();
    }
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
    // And the line spacing it was made at, for the same reason: a wrapped layout is one set of lines
    // at one spacing, and these two are the whole of what it is good for.
    mutable float layoutSpacing = -1.0f;

    ~TextBox() override { if (layout) layout->Release(); DropComposed(); ReleaseBrushes(); }
    // The field makes its own bar. See the end of this header for what a field's layout arranges.
    TextBox();

    bool Focusable() const override { return true; }
    bool TextCursor() const override { return true; }
    void OnFocus() override { atFocus = text; ClearState(); preferredX = -1.0f; }
    void OnBlur() override { Commit(); ClearState(); preferredX = -1.0f; }

    // A field is as wide as the room it is given -- it is the thing a page stretches. **One control
    // tall while it holds one line, and one line taller for each line after that**: the box is not
    // measured from the text. A field that grew as somebody typed into it would move everything
    // under it on every keystroke, which is a page that reacts rather than a field that scrolls.
    micula::Want Measure(const Room &room) const override {
        float h = room.spec->controlH;
        const size_t rows = Rows();
        if (rows > 1 && room.fonts) h += (float)(rows - 1) * RowH(*room.fonts) * lineSpacing;
        return micula::Want(Axis::Fill(), Axis::Fixed(h));
    }

    // Fires onCommit when there is something to commit, and not otherwise. The baseline is taken
    // again after the callback, because a page is allowed to put the value back into the field -- a
    // port of 0080 is 80 -- and what it put back is the value the field now stands for.
    //
    // **A read-only field never commits**: there is no value of the user's to finish, and Enter on one is
    // a key that arrived at a field showing something rather than a field they are filling in. This is
    // where that lives rather than at the two keys, so that a blur cannot commit one either.
    void Commit() {
        if (readOnly) return;
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
    //
    // This is also what the window asks before it hands the IME to a control, so a read-only field gets
    // no composition and no candidate window: see `Surface::SetFocusTo`.
    bool AccessibleWritable() const override { return !readOnly; }
    bool AccessibleSetValue(const std::wstring &s) override {
        // A client filling the field in is somebody typing, not the page: a read-only field refuses it
        // the way it refuses a keystroke, and the answer reaches the client as "this control is read
        // only" rather than as a value that quietly did not change.
        if (readOnly) return false;
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
    // **The text stops short of the clear button when it is there**, which is the difference between
    // the button and the bar: the bar lies over the text, because a field is as wide as it was given
    // and one that took a column of it would narrow the text on the frames it appeared -- and a button
    // that lay over the text would be a button over somebody's last character. WinUI's own button
    // takes a column of its template, and so does this one.
    float InnerWidth() const { return Width(rect) - 22.0f - (ClearShown() ? ClearSize() : 0.0f); }
    float InnerHeight() const { return Height(rect) - 2.0f; }

    // --- the clear button -------------------------------------------------------------------------

    // A square as tall as the field's inside, which is the height WinUI keeps its own button at as
    // the field's height changes ("In order to maintain square button, set the width whenever height
    // is changed").
    float ClearSize() const { return (std::max)(0.0f, Height(rect) - 2.0f); }
    // **Where the button goes, taken from the box `Arrange` was given rather than from `rect`**: a
    // child's rectangle is measured in its parent's *own* space -- the box the field was handed -- and
    // the field's `rect` is measured in the space above that.
    static D2D1_RECT_F ClearBoxIn(const D2D1_RECT_F &box) {
        const float s = (std::max)(0.0f, Height(box) - 2.0f);
        return { box.right - 1.0f - s, box.top + 1.0f, box.right - 1.0f, box.bottom - 1.0f };
    }
    // **WinUI's own rule for its delete button, whole** -- `CTextBox::CanInvokeDeleteButton` and the
    // 5em part of its `ArrangeOverride`: the page asked for it, the field is enabled and may be typed
    // into, has the focus and holds something, and it is a *single line with no wrapping*. A field that
    // wraps is a field somebody is writing in, and a button that empties it is not what a hand is
    // looking for there -- so WinUI shows none, and neither does this.
    bool ClearShown() const {
        if (!showClearButton || !clearButton || !enabled || readOnly || !focus) return false;
        if (Wraps() || text.empty()) return false;
        Surface *w = surface();
        if (!w) return false;
        IDWriteTextFormat *typeface = TextFont(w->fonts);
        if (!typeface) return false;
        const FLOAT em = typeface->GetFontSize();
        if (em <= 0.0f) return false;
        // "Minimum width for TextBox with DeleteButton visible is 5em."
        return Width(rect) > 5.0f * (float)em;
    }
    // The button's own state, kept where the bar's numbers are kept and for the same reason: a
    // keystroke, a blur or a click must not each be a reason to lay a page out again. See
    // `BarNumbers`. Defined under the class, because the button it writes to is declared here and
    // defined at the end of this header.
    void ClearState();
    // **What the button does, and it goes through the one door**: what a click may leave in the field
    // is what the keyboard and the clipboard may, so a field with a rule cannot be emptied behind it.
    // `Changed` is what this is, too -- a click that empties a field is a change somebody made, and
    // the page hears about it the way it hears about a keystroke. The caret and the scroll go with
    // the text: a field with nothing in it has one place for the caret to be.
    void Clear() {
        text = Admitted(std::wstring(), 0, false);
        caret = anchor = text.size();
        scroll = 0.0f;
        Dirty();
        Changed();
        ScrollToCaret();
        ClearState();
        Invalidate();
    }

    // One line of the field's own font, which is what a field with more than one line is made of. Asked
    // of the fonts rather than of a layout, because `Measure` has no layout to ask -- and the two agree
    // because it is the same format either way, the page's if it brought one.
    float RowH(const Fonts &f) const { return f.WrappedHeight(TextFont(f), L"X", 1000.0f); }
    // Where the first line is drawn: centred in what one control's height would have been, which is
    // exactly where a single-line field puts its text. Every line after it is one row lower, so the
    // box grows downwards and the text does not move.
    float FirstLineTop(float rowH) const {
        const float box = Height(rect) - (float)(Rows() - 1) * rowH;
        return rect.top + (box - rowH) * 0.5f;
    }
    // At least one line, always: a field that holds nothing still holds a line to type it into.
    size_t Rows() const { return Wraps() && lines > 0 ? lines : 1; }

    // **How far down what it is clipped to the text begins.** A wrapped field centres its *first line*
    // in one control's height -- see `FirstLineTop` -- so the text does not start at the clip's top, and
    // every question about where the text *ends* has to add this. Leaving it out is what left the last
    // line of a scrolled field short, by the inset less the border: with the body font in a three-line
    // box, 5.7 DIP, which is the descenders. The pitch is `LineH` and not `RowH` because that is what
    // `Paint` centres the first line with, and the two have to be the same number.
    float TextTop() const { return Wraps() ? FirstLineTop(LineH()) - (rect.top + 1.0f) : 0.0f; }

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
        Reformat();
        ClearState();
        ClampScroll();
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
        Reformat();
        ClearState();
        ClampScroll();
    }
    // **A layout is only good for the width it was made at**, and a wrapped one is the whole reason:
    // the same text at another width is another set of lines. Remade when the width moves, and not
    // on every measure -- which is what a page animating its own size would otherwise pay per frame.
    void Dirty() const {
        if (layout) { layout->Release(); layout = nullptr; }
        layoutW = -1.0f;
        layoutSpacing = -1.0f;
        DropComposed();
    }
    void Ensure() const {
        Surface *w = surface();
        if (!w || !w->fonts.dw) return;
        IDWriteTextFormat *typeface = TextFont(w->fonts);
        if (!typeface) return;
        const float want = Wraps() ? InnerWidth() : 0.0f;
        // A single line has nothing to space, and its own line height is left alone: a spaced single
        // line would be a line lower in its box rather than a box that grew.
        const float spacing = Wraps() ? lineSpacing : 1.0f;
        // **A new device takes the runs with it.** The layout was handed brushes and the brushes were
        // the device's, so both go and both are built again -- which is what the surface's count is for.
        if (w->deviceGen != brushesFor) {
            ReleaseBrushes();
            brushesFor = w->deviceGen;
            Dirty();
        }
        if (layout && (layoutW != want || layoutSpacing != spacing)) Dirty();
        if (layout) return;
        w->fonts.dw->CreateTextLayout(text.c_str(), (UINT32)text.size(), typeface,
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
        SpaceLines(layout, spacing);
        layoutW = want;
        layoutSpacing = spacing;
        ApplyRuns(layout, 0, 0, 0);
    }

    // **The spacing between lines, taken from a layout's own first line.** DirectWrite's uniform method wants
    // a line height and a baseline, and the layout already knows both -- the height the line would have had
    // and where the baseline sits in it -- so they are read back and multiplied rather than worked out again
    // from font metrics, which is the other way the box and the caret could come to disagree about how tall
    // a line is. Shared by the text's layout and the composition's, so that a pre-edit in a spaced field
    // sits on the same lines as the text it is part of.
    void SpaceLines(IDWriteTextLayout *into, float spacing) const {
        if (!into || spacing == 1.0f) return;
        DWRITE_TEXT_METRICS tm = {};
        if (SUCCEEDED(into->GetMetrics(&tm)) && tm.lineCount > 0) {
            std::vector<DWRITE_LINE_METRICS> rows((size_t)tm.lineCount);
            UINT32 got = 0;
            if (SUCCEEDED(into->GetLineMetrics(rows.data(), tm.lineCount, &got)) && got > 0)
                into->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                                     rows[0].height * spacing, rows[0].baseline * spacing);
        }
    }

    // --- the runs, and the brushes they are drawn with ---------------------------------------------------

    // **A brush belongs to the device that made it**, and a layout that has been given one holds on to
    // it: the brushes cannot be the painter's own -- that one is recoloured per call, so every run would
    // end up the last colour set -- and they cannot be made per paint either. So they are made with the
    // layout, one per colour in use, kept beside it and released with it. See `Surface::deviceGen` for
    // what happens when the device goes away, which is `Ensure`'s business and not this.
    mutable std::vector<ID2D1SolidColorBrush *> brushes;
    mutable unsigned brushesFor = 0;

    void ReleaseBrushes() const {
        for (ID2D1SolidColorBrush *b : brushes)
            if (b) b->Release();
        brushes.clear();
    }
    ID2D1SolidColorBrush *BrushFor(const D2D1_COLOR_F &c) const {
        for (ID2D1SolidColorBrush *b : brushes) {
            if (!b) continue;
            const D2D1_COLOR_F got = b->GetColor();
            if (got.r == c.r && got.g == c.g && got.b == c.b && got.a == c.a) return b;
        }
        Surface *w = surface();
        if (!w || !w->dc) return nullptr;
        ID2D1SolidColorBrush *made = nullptr;
        if (FAILED(w->dc->CreateSolidColorBrush(c, &made))) return nullptr;
        brushes.push_back(made);
        return made;
    }
    // **A run's range as the drawn layout counts it.** The composition stands in for whatever was selected
    // when it started, so a run is: unchanged where it lies before the composition, carried along by
    // however much *longer* the pre-edit is than what it replaced where it lies after it, and gone where it
    // lies inside what the composition took -- text the pre-edit is standing over is not in the picture at
    // all, so there is nothing to lay a colour on. At most two pieces of a run are left, and with nothing
    // composing there is exactly the one it came in with.
    void MappedRanges(size_t at, size_t len, size_t cutAt, size_t cutLen, size_t insertLen,
                      DWRITE_TEXT_RANGE *out, size_t *count) const {
        *count = 0;
        const size_t cutEnd = cutAt + cutLen;
        if (at < cutAt) {
            out[*count] = { (UINT32)at, (UINT32)(std::min)(len, cutAt - at) };
            (*count)++;
        }
        if (at + len > cutEnd) {
            const size_t from = (std::max)(at, cutEnd);
            out[*count] = { (UINT32)(from + insertLen - cutLen), (UINT32)(at + len - from) };
            (*count)++;
        }
    }
    // **The runs, laid on a layout, once.** How much of the text is drawn how is this function's whole
    // business, and nothing else in the field has to know about runs at all: the wrapping, the caret, the
    // hit test and the scroll are still the layout's own answers, and follow the runs because the layout
    // does. It is asked for both layouts -- the text's, with nothing standing in for anything, and the
    // composition's.
    void ApplyRuns(IDWriteTextLayout *into, size_t cutAt, size_t cutLen, size_t insertLen) const {
        if (!into) return;
        // Out of range is not something an engine has to avoid: a run that runs past the end is treated as
        // ending there, and one that starts past the end is not drawn at all. An engine answering from a
        // text a keystroke out of date is a frame, not a crash.
        const DWRITE_TEXT_RANGE all = { 0, (UINT32)(text.size() + insertLen - cutLen) };
        into->SetDrawingEffect(nullptr, all);
        for (const Run &r : runs) {
            if (r.len == 0 || r.at >= text.size()) continue;
            DWRITE_TEXT_RANGE range[2];
            size_t count = 0;
            MappedRanges(r.at, (std::min)(r.len, text.size() - r.at), cutAt, cutLen, insertLen,
                         range, &count);
            for (size_t i = 0; i < count; i++) {
                if (!r.font.Empty()) {
                    IDWriteTextFormat *typeface = r.font.Get();
                    into->SetFontSize(typeface->GetFontSize(), range[i]);
                    into->SetFontWeight(typeface->GetFontWeight(), range[i]);
                    into->SetFontStyle(typeface->GetFontStyle(), range[i]);
                    const UINT32 n = typeface->GetFontFamilyNameLength();
                    if (n) {
                        std::wstring name((size_t)n + 1, L'\0');
                        if (SUCCEEDED(typeface->GetFontFamilyName(&name[0], n + 1))) {
                            name.resize(n);
                            into->SetFontFamilyName(name.c_str(), range[i]);
                        }
                    }
                }
                into->SetUnderline(r.underline ? TRUE : FALSE, range[i]);
                if (r.hasColor)
                    if (ID2D1SolidColorBrush *b = BrushFor(r.color)) into->SetDrawingEffect(b, range[i]);
            }
        }
    }
    // Where the caret for an index sits, in a given layout's own space: its own line and column, which is
    // the whole difference between a field that can hold more than one line and one that cannot. Split out
    // because there are two layouts to ask -- the text's and the composition's -- and the answer has to come
    // from whichever one is being drawn. See `Drawn`.
    D2D1_POINT_2F CaretAtIn(IDWriteTextLayout *into, size_t index) const {
        if (!into) return D2D1::Point2F(0.0f, 0.0f);
        FLOAT x = 0, y = 0;
        DWRITE_HIT_TEST_METRICS m = {};
        into->HitTestTextPosition((UINT32)index, FALSE, &x, &y, &m);
        return D2D1::Point2F(x, y);
    }
    D2D1_POINT_2F CaretAt(size_t index) const {
        Ensure();
        return CaretAtIn(layout, index);
    }
    float XOf(size_t index) const { return CaretAt(index).x; }
    float XOfIn(IDWriteTextLayout *into, size_t index) const { return CaretAtIn(into, index).x; }
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
    // fonts, the same way `Measure` asks**, and multiplied by the field's own spacing: the box, the
    // caret, a notch and a click then cannot disagree about how tall a line is, which is the one thing
    // a field with several of them has to get right.
    float LineH() const {
        Surface *w = surface();
        return w ? RowH(w->fonts) * (Wraps() ? lineSpacing : 1.0f) : 0.0f;
    }
    // The lines the *layout* makes of the text, as `{first, end}` for each, with any line break left out of
    // the end. **A field that wraps has lines its text does not have**, and every key below is about these:
    // the end of a line somebody is looking at is the end of a wrap, not of a paragraph.
    struct Line {
        size_t first = 0;
        size_t end   = 0;
    };
    std::vector<Line> Lines() const {
        std::vector<Line> out;
        Ensure();
        if (!layout) return out;
        DWRITE_TEXT_METRICS tm = {};
        if (FAILED(layout->GetMetrics(&tm)) || tm.lineCount == 0) return out;
        std::vector<DWRITE_LINE_METRICS> lm((size_t)tm.lineCount);
        UINT32 got = 0;
        if (FAILED(layout->GetLineMetrics(lm.data(), tm.lineCount, &got)) || got == 0) return out;
        size_t at = 0;
        for (UINT32 i = 0; i < got; i++) {
            const size_t len  = (size_t)lm[i].length;
            const size_t brk  = (size_t)lm[i].newlineLength;
            out.push_back({ at, at + len - (brk <= len ? brk : 0) });
            at += len;
        }
        return out;
    }
    // Which of them holds a position. **The end of a line belongs to it**: a caret at the end of a line is
    // on that line and not on the next one, which is the whole difference between End and Down.
    static size_t LineAt(const std::vector<Line> &lines, size_t index) {
        for (size_t i = 0; i < lines.size(); i++)
            if (index <= lines[i].end) return i;
        return lines.empty() ? 0 : lines.size() - 1;
    }
    // **A step over a line break is one step.** Both halves of a CRLF are one thing to whoever is pressing
    // the key, and a caret that could stop between them would be a caret in a place nothing can be typed.
    size_t PrevIndex(size_t i) const {
        if (i >= 2 && text[i - 1] == L'\n' && text[i - 2] == L'\r') return i - 2;
        return i > 0 ? i - 1 : 0;
    }
    size_t NextIndex(size_t i) const {
        if (i + 1 < text.size() && text[i] == L'\r' && text[i + 1] == L'\n') return i + 2;
        return i < text.size() ? i + 1 : text.size();
    }
    // **The one move Up, Down, PageUp and PageDown all make**: another of the layout's lines, keeping the
    // column. The target line is clamped to the ones that exist, and **a target that is the line the caret
    // is already on is not a move at all** -- measured against the platform's own edit control, both ends
    // leave the caret alone (an Up at the first line, a Down at the last), rather than flinging it to the
    // start or the end of the text; a page at the end still travels, because it lands on another line.
    void MoveLines(int by) {
        const std::vector<Line> rows = Lines();
        if (rows.empty()) return;
        const size_t at = LineAt(rows, caret);
        const float  pitch = LineH();
        const int    want = (std::min)((std::max)((int)at + by, 0), (int)rows.size() - 1);
        if (want == (int)at) return;
        if (preferredX < 0.0f) preferredX = CaretAt(caret).x;
        // The middle of the line, so that a point exactly on a boundary is not read as the line above it,
        // and clamped to the line afterwards for the same reason `preferredX` exists at all.
        caret = IndexAt(preferredX, (float)want * pitch + pitch * 0.5f);
        caret = (std::min)((std::max)(caret, rows[(size_t)want].first), rows[(size_t)want].end);
    }
    // How far a page moves: the whole lines on screen less one, so a line somebody has already read stays
    // in view. A choice rather than a measurement -- the edit control ignores the page keys outright in
    // every arrangement this was tried in, focused or not, so what it would have done is not knowable
    // from it. Keeping a line of the previous page visible is what makes a page down not lose the thread.
    int PageLines() const {
        const float pitch = LineH();
        if (pitch <= 0.0f) return 1;
        const int whole = (int)(InnerHeight() / pitch);
        return whole > 1 ? whole - 1 : 1;
    }

    // --- the bar, and the two ways a field is scrolled -------------------------------------------

    // **The bar is told, not asked.** It is a child, so its rectangle is the layout's business (see
    // `FieldLayout`), but what it *shows* changes on every keystroke and on every notch, and asking
    // the tree to lay out again for each of those would be a page-wide arrangement per character.
    // So the field keeps the bar's numbers itself, and the layout never has to run for a scroll.
    void BarNumbers() {
        if (!bar) return;
        const float box = InnerHeight();
        // **The text as the scroll sees it**: what begins at the clip's top is `TextTop` lower down, so
        // the bar's extent is the text plus that inset -- otherwise its own limit is smaller than the
        // field's and the thumb cannot be dragged to the end of it. See `MaxScrollY`.
        const float all = TextTop() + TextHeight();
        bar->viewport = box;
        bar->extent = all;
        bar->value = scrollY;
        bar->drawn = scrollY;
        bar->visible = Wraps() && MaxScrollY() > 0.0f;
        bar->Poll();
    }
    // **How far the text can be scrolled before its last line is at the bottom of the box.** This is the
    // clamp every scroll goes through, and the number a page needs in order to know whether the reader
    // is at the bottom: `scrollY >= MaxScrollY()` is "pinned", and following the text only while
    // somebody is watching the end of it is then the page's to do -- a field has no business deciding
    // that. See `ClampScroll`.
    //
    // **It is not `TextHeight - InnerHeight`**, which is the answer only if the text begins at the top
    // of what it is clipped to. It does not -- see `TextTop` -- so that form leaves the last line short
    // by the inset less the border, and the bottom of a line is the descent: the tail of a `g` and the
    // foot of a `p`, on the last line of a log.
    float MaxScrollY() const { return (std::max)(0.0f, TextTop() + TextHeight() - InnerHeight()); }
    // **Both scrolls are re-clamped when the page sets the text**, because they belong to the text that
    // was there: a value the page shortened would otherwise leave the field scrolled past its own last
    // line -- a field showing nothing at all -- until something else happened to scroll it back. No
    // caret is followed here: where the scroll *should* be when the text changes is the page's answer,
    // and this only makes sure the one it left is one the text can still reach.
    void ClampScroll() {
        if (scroll == 0.0f && scrollY == 0.0f) return;   // nothing to clamp, and no layout to build
        scrollY = std::clamp(scrollY, 0.0f, MaxScrollY());
        const float full = CaretAt(text.size()).x;
        scroll = std::clamp(scroll, 0.0f, (std::max)(0.0f, full - InnerWidth()));
        BarNumbers();
    }
    // Scroll the text, from a notch or from the bar being dragged. Clamped to the text: the last line
    // is readable to its end rather than scrolled past.
    void ScrollTo(float to) {
        const float at = std::clamp(to, 0.0f, MaxScrollY());
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
        if (MaxScrollY() <= 0.0f) return false;
        const float perNotch = SystemWheelLines();
        ScrollTo(scrollY - notches * (perNotch > 0.0f ? perNotch : 3.0f) * LineH());
        return true;
    }
    // How tall the text stands, lines and all: what a field with more than one line scrolls over.
    float TextHeight() const {
        Ensure();
        // **The composition counts as content here too**, which is what lets a caret that the pre-edit has
        // carried onto the next line be scrolled to: the field is taller by whatever the pre-edit added, and
        // a scroll clamped to the text without it would refuse to go that far. See `MaxScrollY`.
        IDWriteTextLayout *body = Drawn();
        if (!body) return 0.0f;
        DWRITE_TEXT_METRICS tm = {};
        body->GetMetrics(&tm);
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
        Reformat();
        ClearState();
        if (onChange) onChange(text);
    }

    // Keep the caret in view. Without this a path longer than the field types itself
    // off the right-hand edge and the person is editing something they cannot see.
    void ScrollToCaret() {
        // **The caret as it is drawn**, which is the composition's own caret while one is up: the pre-edit is
        // part of the content (see `EnsureComposed`), so the caret a person is looking at is inside it.
        const D2D1_POINT_2F at = DrawnCaret();
        // **Sideways, which is a field with one line and nothing else.** A wrapped field has no horizontal
        // scroll to move: its content is cut to the width it was given, so what does not fit on this row is
        // on the next one -- or is a word carried down whole -- and sliding it left would take the
        // paragraph's left margin out of the box to show the end of one row.
        if (Wraps()) {
            scroll = 0.0f;
        } else {
            const float x = at.x;
            if (x - scroll > InnerWidth() - 2) scroll = x - InnerWidth() + 2;
            if (x - scroll < 0)                scroll = x;
            const float full = XOfIn(Drawn(), DrawnLen());
            if (full - scroll < InnerWidth() && scroll > 0)
                scroll = full > InnerWidth() ? full - InnerWidth() : 0.0f;
            if (scroll < 0) scroll = 0;
        }
        // **And down**, which is the half a field with one line has no use for. The caret's own line
        // is what has to stay visible, and the scroll is clamped to the text so that the last line is
        // readable to its end rather than scrolled past.
        if (!Wraps()) {
            scrollY = 0.0f;
            return;
        }
        const float row = LineH();
        const float box = InnerHeight();
        const float top = TextTop();
        if (at.y + top - scrollY + row > box) scrollY = at.y + top + row - box;
        if (at.y + top - scrollY < 0)         scrollY = at.y + top;
        scrollY = std::clamp(scrollY, 0.0f, MaxScrollY());
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
        // **A read-only field still answers for the character**, with "no": handing it back would let
        // the window read it as a mnemonic, which is the one thing worse than ignoring it. See
        // `readOnly`. The IME's committed text arrives here too -- `WM_CHAR` and `WM_IME_CHAR` are both
        // this function -- so it is refused the same way.
        if (readOnly) return true;
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
        // **The column is kept across a run of vertical moves and nothing else.** A letter, a click, the
        // clipboard move the caret somewhere somebody chose, and a column remembered from before that is
        // not the column they are on. See `preferredX`.
        const bool vertical = (vk == VK_UP || vk == VK_DOWN || vk == VK_PRIOR || vk == VK_NEXT);
        if (!vertical) preferredX = -1.0f;

        if (ctrl) {
            HWND hwnd = surface() ? surface()->hwnd : nullptr;
            switch (vk) {
            case 'A': anchor = 0; caret = text.size(); return true;
            // **The whole text**, which is what Home and End used to be before a field could have more
            // than one line of them; the plain keys are the line's. See VK_HOME below.
            case VK_HOME: caret = 0; if (!shift) anchor = caret; return true;
            case VK_END:  caret = text.size(); if (!shift) anchor = caret; return true;
            // **The other way to finish a value**, and the one a field with more than one line
            // needs: Enter is a line break there, so leaving the field is what a commit would
            // otherwise have to wait for. See VK_RETURN below.
            case VK_RETURN: Commit(); return true;
            case 'C': if (HasSelection()) micula::SetClipboardText(hwnd, Selected());
                      return true;
            // **Cut and paste are the clipboard changing the text**, which a read-only field does not
            // do; copy is the field answering, which it does. See `readOnly`.
            case 'X': if (!readOnly && HasSelection()) {
                          micula::SetClipboardText(hwnd, Selected());
                          DeleteSelection();
                          Changed();
                      }
                      return true;
            case 'V': {
                if (readOnly) return true;
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
            // A line break is text arriving, which is what a read-only field is for refusing. The
            // commit above is not: what the field *stands for* is not changed by being asked about.
            if (readOnly) return true;
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
            caret = PrevIndex(caret);
            if (!shift) anchor = caret;
            break;
        case VK_RIGHT:
            caret = NextIndex(caret);
            if (!shift) anchor = caret;
            break;
        case VK_HOME:
        case VK_END: {
            // **The line the caret is on**, as the layout drew it: wrapping means the end of a line is the
            // end of a wrap rather than of a paragraph, and that is what somebody pressing End is looking
            // at. A field with one line has one of these, so this is what it has always done there.
            const std::vector<Line> rows = Lines();
            if (rows.empty()) caret = vk == VK_HOME ? 0 : text.size();
            else {
                const size_t at = LineAt(rows, caret);
                caret = vk == VK_HOME ? rows[at].first : rows[at].end;
            }
            if (!shift) anchor = caret;
            break;
        }
        case VK_UP:
        case VK_DOWN:
        case VK_PRIOR:
        case VK_NEXT: {
            // **A field with one line has nothing for these to do**, and a long line whose caret jumps to
            // its end because somebody brushed the key is an annoyance with no reason behind it: the key
            // goes on to whoever else wants it. (The window moves the focus on Tab and on nothing else, so
            // what it does there today is nothing at all.)
            if (!Wraps()) return false;
            // **The column comes along**, which is the whole of what the x remembered below is for: a
            // short line in the middle clamps the caret while it is there and gives the column back on the
            // far side, instead of the caret sliding leftwards for every line it passes.
            const bool up = (vk == VK_UP || vk == VK_PRIOR);
            const int  by = (vk == VK_PRIOR || vk == VK_NEXT) ? PageLines() : 1;
            // The line is clamped to the ones that exist and a target on the line the caret is already on
            // is not a move: that is what leaves the caret alone at both ends. See `MoveLines`.
            MoveLines(up ? -by : by);
            if (!shift) anchor = caret;
            break;
        }
        case VK_BACK:
            if (readOnly) return true;
            if (HasSelection()) DeleteSelection();
            else if (caret > 0) {
                // A line break in one press, both halves of it: half a CRLF left behind is a lone carriage
                // return, which is a line break with nothing to say it is one.
                const size_t n = (caret >= 2 && text[caret - 1] == L'\n' && text[caret - 2] == L'\r') ? 2 : 1;
                text.erase(caret - n, n);
                caret = anchor = caret - n;
            }
            else return true;
            Changed();
            break;
        case VK_DELETE:
            if (readOnly) return true;
            if (HasSelection()) DeleteSelection();
            else if (caret < text.size()) {
                const size_t n = (caret + 1 < text.size() && text[caret] == L'\r' &&
                                  text[caret + 1] == L'\n') ? 2 : 1;
                text.erase(caret, n);
            }
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
        // The drawn caret: while an input method is composing, that is inside the pre-edit, and the candidate
        // window belongs under the character being worked on rather than under the insertion point in front
        // of it. See `DrawnCaret`.
        const D2D1_POINT_2F at = DrawnCaret();
        // The bottom of the caret's own line, which for one line is the bottom of the field -- where
        // the candidate window has always opened.
        const float y = Wraps() ? (FirstLineTop(RowHere()) - scrollY + at.y + LineH()) : rect.bottom;
        if (out) *out = D2D1::Point2F(InnerLeft() + at.x - scroll, y);
        return true;
    }

    // **The face the caret's line is drawn with, and how tall that line is**, which is the rest of what
    // an input method needs to draw its composition string over this field. Both come from the field's
    // own font -- the one `SetFont` was given, or the library's body font -- and the height is the one
    // the caret itself is drawn at, so a log field in a monospace composes in that monospace and the
    // IME's window is the height of this field's line rather than of the stock font's. See
    // `Widget::CaretStyle`.
    bool CaretStyle(CaretLine *out) const override {
        Surface *w = surface();
        if (!w || !out) return false;
        IDWriteTextFormat *typeface = TextFont(w->fonts);
        if (!typeface) return false;
        out->format = typeface;
        // **The line the text is on, which is not the caret's box.** An input method is told two things:
        // where the *character* is -- its top-left -- and how tall the *line that contains it* is. For a
        // field with one line the layout is centred in the box by hand (see the paint below), so the line
        // starts at the middle of the box less half of itself, and it is `LineH()` tall rather than the
        // box's inner height. The numbers that used to be here -- the six-DIP inset and the inner box --
        // agreed with that to within a DIP at the body font, which is why they looked right, and are out
        // by several once the field is given a larger one.
        if (Wraps()) {
            const D2D1_POINT_2F at = DrawnCaret();
            out->top = FirstLineTop(RowHere()) - scrollY + at.y;
        } else {
            out->top = rect.top + ((rect.bottom - rect.top) - LineH()) * 0.5f;
        }
        out->height = LineH();
        // **And the baseline inside that line**, which is the one thing the IME's drawing and this
        // library's have in common. `Ensure` reads the same metric for the spacing it applies: the
        // distance from the top of a line to its baseline, which is the same for every line of a field
        // whose spacing is uniform. See `Window::ImeLineRect` for what the window does with it -- the two
        // rendering paths do not put the same number of pixels above the baseline for one and the same
        // font, and the composition was landing five pixels below the text because of it.
        //
        // **Room for every line, and not for one.** Asking a layout of two lines for a single line's
        // metrics fails -- measured, `GetLineMetrics` answers 0x8007007A, the buffer is too small, reports
        // the real count and leaves the baseline at zero -- and a baseline of zero is a line reported a
        // whole ascent too high. Measured where it matters: the line's top edge was handed over as 260 px
        // for a line that begins at 283, so an input method opening its candidate list one line-height
        // below that put it at 288 -- over the characters being typed rather than under them. A field of
        // one line never showed this, which is why it lasted: the same query on one line is answered.
        Ensure();
        out->baseline = out->top;
        IDWriteTextLayout *body = Drawn();
        if (body) {
            DWRITE_TEXT_METRICS tm = {};
            if (SUCCEEDED(body->GetMetrics(&tm)) && tm.lineCount > 0) {
                std::vector<DWRITE_LINE_METRICS> rows((size_t)tm.lineCount);
                UINT32 got = 0;
                if (SUCCEEDED(body->GetLineMetrics(rows.data(), tm.lineCount, &got)) && got > 0)
                    out->baseline = out->top + rows[0].baseline;
            }
        }
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
        //
        // **`showAccentUnderline` is off and there is nothing to light up**: the field keeps the edge it
        // has at rest, which is what a page showing a log rather than being typed into wants. The line is
        // still drawn either way -- that edge is the field's own bottom, and not drawing it would leave
        // the rounded stroke open -- and an `Invalid` answer is painted here whatever the flag says,
        // because a report is not an emphasis.
        // **What the window is, not only what the widget is.** A window somebody has alt-tabbed away from
        // still has a focused field, and the field is still where the keyboard would go -- but the keyboard
        // is not there, so nothing about it is lit: no accent along the bottom edge and no caret. The fill
        // is left alone, because that one is about the widget rather than about the window.
        const bool hasKeyboard = active && (!surface() || surface()->Active());
        Resolve(false);
        const bool bad = validation == Validation::Invalid;
        const bool lit = hasKeyboard && enabled && (bad || showAccentUnderline);
        p.Line(rect.left + metric::kRadiusControl, rect.bottom - 1,
               rect.right - metric::kRadiusControl, rect.bottom - 1,
               bad ? c.bad
                   : (hasKeyboard && enabled && showAccentUnderline ? c.accent : c.controlStrokeBottom),
               lit ? 2.0f : 1.0f);

        Ensure();

        const D2D1_RECT_F inner = { InnerLeft(), rect.top + 1, InnerLeft() + InnerWidth(),
                                    rect.bottom - 1 };
        p.rt->PushAxisAlignedClip(inner, D2D1_ANTIALIAS_MODE_ALIASED);

        // **Where the layout goes.** One line: where the text has always been centred, and the hit
        // test asks about the column alone. More than one: the first line where that same text would
        // have been, every line after it a row lower, and the whole thing raised by however much the
        // caret has needed to stay in view. `TextOrigin` is the same expression, so a click lands
        // where the glyphs are.
        const float row = LineH();
        const float originY = Wraps() ? (FirstLineTop(row) - scrollY)
                                      : (rect.top + (Height(rect) - TextHeight()) / 2);
        const float originX = inner.left - scroll;

        // **The composition is content, so the layout it is drawn from is the one it is in.** `Drawn` is the
        // text's own layout when there is nothing composing and `EnsureComposed`'s when there is -- and a
        // disabled field never has one to draw, because the window hands the input method to a writable
        // control. Declared out here because the dotted line and the caret are drawn past the layout's guard.
        IDWriteTextLayout *body = enabled ? Drawn() : layout;
        const bool composing = enabled && ComposedIsDrawn();

        if (text.empty() && !placeholder.empty() && !composing) {
            p.Text(placeholder, inner, TextFont(*p.font), c.textDisabled);
        } else if (body) {
            // **The selection is the field's; the highlight is the focus's.** A selection survives a
            // blur -- clicking elsewhere must not throw away what was picked, and neither Windows' own
            // edit control nor WinUI clears it -- but the highlight does not. WinUI keeps two brushes
            // for exactly this pair, `SelectionHighlightColor` and
            // `SelectionHighlightColorWhenNotFocused`, and the second one's default is *transparent*:
            // a field that is not being worked on holds a selection that cannot be seen, and shows it
            // again the moment it is focused. What is drawn here is the first of the two.
            const D2D1_COLOR_F wash = D2D1::ColorF(c.accent.r, c.accent.g, c.accent.b, 0.35f);
            // **Nothing is highlighted while a composition is up.** The composition stands in the
            // selection's place -- see `SpliceAt` -- so the text that was selected is not on the screen to
            // be washed: what marks the pre-edit is the dotted line under it, and a highlight over that
            // would be saying "this is text, and it is picked" about something that is neither yet. The
            // selection is still the field's, and it is drawn again the moment the composition ends.
            if (active && HasSelection() && !composing) {
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
            ID2D1Brush *ink = p.Brush(enabled ? c.textPrimary : c.textDisabled);
            // Vertically centred by hand for one line: the layout is drawn from its origin and its own
            // height is the line height, not the box's. More than one and the origin above is what has
            // already worked that out.
            //
            // **One draw, of the text with the composition in it.** There is nothing to splice into the
            // picture and nothing to clip around: what follows the caret has already been laid out around
            // the pre-edit, onto the next line in a field that wraps -- where a word that no longer fits is
            // carried down whole -- and out of the box in one that does not, where the scroll has followed
            // it. The pre-edit is in the field's own font and on the field's own line because it is in the
            // field's own layout; see `EnsureComposed`.
            p.rt->DrawTextLayout(D2D1::Point2F(originX, originY), body, ink, D2D1_DRAW_TEXT_OPTIONS_NONE);
        }

        // **The dotted line under the composition**, which is what Windows has always put under one. This is
        // the half of an input method that this library does itself: the IME is told not to draw its own
        // composition window (`WM_IME_SETCONTEXT`) and what it is composing is laid out in the field's own
        // layout, so what is left to say about it is that it is not text yet.
        if (composing) {
            // **Where the dots go is the layout's answer**, asked about the pre-edit's own characters: a
            // pre-edit that has wrapped is a run of dots per line rather than one long line running off the
            // edge. Drawn as short strokes rather than as a dash style, because a stroke style is a device
            // resource that has to be made with the device and released with it, and this is a dozen strokes.
            // **Room for every line here too, and not for one.** The same query one line short of the layout
            // fails -- see `CaretStyle`, where the number it drops is the one an input method places its
            // candidate list from, and where the same mistake was measured -- and what it costs here is the
            // dots: without an answer the line under the composition is its bottom edge, so they are drawn a
            // descent too low. The first line's baseline is every line's.
            float baseline = originY + LineH();
            DWRITE_TEXT_METRICS tm = {};
            if (SUCCEEDED(body->GetMetrics(&tm)) && tm.lineCount > 0) {
                std::vector<DWRITE_LINE_METRICS> rows((size_t)tm.lineCount);
                UINT32 got = 0;
                if (SUCCEEDED(body->GetLineMetrics(rows.data(), tm.lineCount, &got)) && got > 0)
                    baseline = originY + rows[0].baseline;
            }
            const UINT32 from = (UINT32)SpliceAt();
            const UINT32 n = (UINT32)ComposedLen();
            UINT32 got = 0;
            body->HitTestTextRange(from, n, originX, originY, nullptr, 0, &got);
            if (got) {
                std::vector<DWRITE_HIT_TEST_METRICS> boxes(got);
                if (SUCCEEDED(body->HitTestTextRange(from, n, originX, originY, boxes.data(), got, &got)))
                    for (UINT32 i = 0; i < got; i++) {
                        const float y = baseline + 2.0f;
                        const float end = boxes[i].left + boxes[i].width;
                        for (float x = boxes[i].left; x < end; x += 5.0f)
                            p.Line(x, y, (std::min)(x + 2.0f, end), y, c.textPrimary, 1.0f);
                    }
            }
        }

        if (hasKeyboard && enabled) {
            Surface *w = surface();
            if (w && w->caretOn) {
                // **The caret is the drawn one**, which is inside the composition while one is up. What the
                // input method's own cursor is past is what it has consumed, and that is where the committed
                // text will land; a caret left at the insertion point would point at the beginning of the
                // pre-edit instead. See `DrawnCaret`.
                const D2D1_POINT_2F at = DrawnCaret();
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

// The field's clear button: one glyph in a square, at the field's right-hand edge.
//
// **A widget of its own rather than a `Button`**, for the reason the bar has one: what it is worth is
// the field's metric -- a square as tall as the field -- and when it is there is the field's state, so
// there is nothing left for a page to set but the word a screen reader reads. Its click is `OnClick`,
// which is what a `Button` answers and what a client's own invoke reaches, so it is a button to
// everything that asks.
struct ClearButton : Widget {
    TextBox *field = nullptr;

    int AccessibleType() const override { return UIA_ButtonControlTypeId; }
    // The glyph says nothing out loud, so the field's `clearLabel` is what it is called.
    const wchar_t *AccessibleName() const override {
        return field && !field->clearLabel.empty() ? field->clearLabel.c_str() : L"Clear";
    }
    // **A glyph is not text and the pointer over it is over a button**: WinUI sets an arrow over its
    // own delete button rather than leaving the field's I-beam, and this is that.
    bool TextCursor() const override { return false; }
    void OnClick() override { if (field) field->Clear(); }

    void Paint(const Painter &p) override {
        const Palette &c = *p.pal;
        // Subtle: no box at all until the pointer is on it, which is what WinUI's button inside a
        // field is. A filled square in the corner of every field is the other thing this could be.
        if (enabled && hoverT > 0.0f)
            p.FillRound(rect, metric::kRadiusControl,
                        Fade(Mix(c.subtleHover, c.subtlePressed, pressT), hoverT));
        // Centred by measuring the glyph, because the icon format aligns leading like every other
        // format and a glyph is not a paragraph.
        const float gw = p.MeasureWidth(glyph::kClear, p.font->icon);
        const D2D1_RECT_F box = { rect.left + (Width(rect) - gw) * 0.5f, rect.top,
                                  rect.left + (Width(rect) + gw) * 0.5f, rect.bottom };
        p.Text(glyph::kClear, box, p.font->icon,
               enabled ? (hoverT > 0.0f ? c.textPrimary : c.textSecondary) : c.textDisabled);
    }
};

// The field's side of the button, which needs the button to be a complete type: where a click on it
// leaves the field is `TextBox::Clear`, and when it is there is `ClearShown`.
inline void TextBox::ClearState() {
    if (clearButton) clearButton->visible = ClearShown();
}

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
    // The clear button, in the same space. Its *state* is the field's -- see `ClearState` -- but a box
    // that has just changed size is where the 5em rule can change, so it is asked for again here.
    if (f->clearButton) f->clearButton->rect = TextBox::ClearBoxIn(box);
    f->ClearState();
}

inline TextBox::TextBox() {
    // The field's own layout, which arranges the bar and the button and nothing else: the page
    // arranges the field.
    SetLayout(new FieldLayout());
    // Qualified, because this class hides `Add`: the bar is the field's own child rather than
    // something a page is putting in it.
    bar = Widget::Add(new ScrollBar([this](float to, bool) { ScrollTo(to); }));
    // Nothing to scroll yet, and a bar that has never been arranged would otherwise flash at the
    // right-hand edge before the first arrangement. See ScrollBar::Poll, which puts it away itself.
    bar->visible = false;
    // The button is made whether or not the page ever asks for it, and hidden until it does: a control
    // that appeared later would be a control arriving in the tree after a layout, and there is nothing
    // saved by waiting.
    clearButton = Widget::Add(new ClearButton());
    clearButton->field = this;
    clearButton->visible = false;
}

}  // namespace micula
