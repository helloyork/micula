// Micula / menu.h
//
// **A list of things to pick, hanging over whatever asked for it.** The box is a `Popup` -- a window of
// its own, so it may cross the edge of the window it came from, and so a shell tray menu can exist with no
// window behind it at all -- and it is made of *rows* rather than of widgets: a menu of thirty commands is
// one thing with thirty rows in it, which is also what a screen reader is told (see
// `MenuPanel::AccessibleItems`).
//
// Two pieces, as a tip has. `Menu` is the box; `Menus` is the trigger, attached to a window:
//
// ```cpp
// struct MyWindow : Window {
//     Menus menus{ *this };
//     ...
// };
//
// panel->contextMenu = [this](micula::Menu &m) {
//     m.Item(L"Copy", micula::glyph::kCopy, L"Ctrl+C", [this] { Copy(); });
//     m.Item(L"Paste", micula::glyph::kPaste, L"Ctrl+V", [this] { Paste(); }, CanPaste());
//     m.Separator();
//     m.Check(L"Show hidden", hidden, [this](bool on) { SetHidden(on); });
// };
// ```
//
// A right-click on that panel opens it where the hand is, so do the Menu key and Shift+F10 under the
// focused control, and the menu itself answers the arrows, Home/End, Enter, Esc and a click on a row.
//
// **A submenu is a menu of its own, in a window of its own.** A row that is a container holds a builder
// (see `Row::onSub`), and resting on that row -- for as long as the platform's own menus wait -- clicking
// it, or the right arrow on it opens a second `Menu` beside it: see `Menu::OpenSub`. It is a `Popup` and
// not one more panel in this window for the same reason a menu is one and not a `Layer` -- it may have to
// cross the edge of the window it came from -- and for two more that pay for themselves here: where it goes
// is decided by the placement every menu already uses (the row's own box, flipped to the other side of it
// when the work area runs out, clamped into what is left), and it *arrives* with the same fade every popup
// arrives with, on its own. The cost is three small things, each of them a line: the parent owns the child
// and closes it with itself, the child is shown without activating like its parent, and the keys a menu
// takes are handed down to the deepest menu first.
//
// **It is filled every time it opens rather than kept in step.** The builder is the page's and it runs on
// each open, so a row that is disabled now, a tick that is on now, and a list that changed under it are
// all the page's own state read at the moment it matters: there is no second copy of the truth to get
// wrong, and nothing to keep in step as the page changes.
//
// **And it takes the keyboard without taking the activation.** A menu that activated -- which is what a
// `Popup` does by default -- would dim the caption of the window it belongs to for as long as somebody is
// merely using a menu that hangs off it, and would tell that window it had lost its activation when nothing
// of the sort had happened. So the menu is shown without activating, and the input it needs is the input the
// window it hangs over is sent: `Menus` hands every message to `Menu::TakeInput` first -- see
// `Surface::onInput` -- and what the menu takes is the menu's.
//
// What that buys, and what it costs. The caption stays lit, the window keeps the focus, and there is one
// place that answers "what is the input doing right now" instead of two. It costs one thing: a *touch* tips
// outside the menu goes to this window rather than to a window of the menu's own, which is why the press that
// dismisses a menu is read here rather than in the menu's own window.

#pragma once

#include "glyphs.h"
#include "popup.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace micula {

// How long the pointer rests on a row before the menu under it opens. **A delay and not nothing**, because
// a hand crossing a menu on its way somewhere passes over every container it has: a submenu that opened
// under the hand on the way past would flash open and shut, which is the flicker the same rule exists to
// prevent in the platform's own menus. Like the tip's dwell it is the machine's answer rather than a number
// of ours -- `SPI_GETMENUSHOWDELAY`, 400 ms out of the box, and a setting a person can change.
inline UINT MenuDwell() {
    UINT ms = 0;
    if (!SystemParametersInfoW(SPI_GETMENUSHOWDELAY, 0, &ms, 0) || ms == 0) ms = 400;
    return ms;
}

struct Menu;

// **The menu's one widget: the rows.** Not one widget per row -- a menu is a thing with rows in it, and
// the rows are `Menu::rows` -- so this is the widget that draws them, hit-tests them, and answers for
// them to a client. Its box is the panel, which is what `Menu::OnPlaced` gives it.
struct MenuPanel : Widget {
    Menu *menu = nullptr;

    void Paint(const Painter &p) override;
    void OnPointerMove(float x, float y) override;
    void OnPress(float x, float y) override;
    void OnRelease() override;
    // The rows are what this widget *is*, rather than widgets it has: see `Widget::Item`, which is the
    // same answer a list of countries gives. One element per row, and no widget per row.
    int AccessibleType() const override { return UIA_MenuControlTypeId; }
    int AccessibleItems() const override;
    bool AccessibleItem(int i, Item &out) const override;
};

// A menu. One class, `MenuPanel` for its one widget, and the rows are its own: what a row is, where it
// went, and what picking it does.
struct Menu : Popup {
    // One row. `kind` is what it is and the rest is what it has -- a separator has neither words nor a
    // callback, and a submenu's row holds another menu rather than something to do.
    struct Row {
        enum class Kind { Item, Separator, Check, Sub };
        std::wstring text;
        std::wstring shortcut;          // the keys that do the same thing, at the far end of the row
        const wchar_t *icon = nullptr;  // a glyph, in the icon column -- or the tick, for a `Check`
        Kind kind = Kind::Item;
        bool checked = false;
        bool enabled = true;
        std::function<void()> onPick;
        std::function<void(bool)> onCheck;
        // **What is under this row**, for a row that holds a submenu: the builder of the menu that opens
        // beside it, run every time that menu opens, exactly as the page's own builder is run for this one.
        // See `Sub` and `OpenSub`.
        std::function<void(Menu &)> onSub;
        // Where the row was placed, in the panel's own space, and how tall it is. Filled by `Measure`.
        float top = 0.0f, h = 0.0f;
    };

    // The numbers a menu is laid out by, in DIPs. **A row is the platform's control height** -- a menu
    // row is a control you pick, and Windows draws one 32 tall -- and the padding is the same 4 the
    // flyout leaves around a panel, so that the rows of a menu and the rows of a list agree about where
    // their words start.
    static constexpr float kRowH = metric::kControlH;
    static constexpr float kPad = 4.0f;        // the panel's own margin, around the rows
    static constexpr float kRowPad = 12.0f;    // a row's margin, before its icon and after its shortcut
    static constexpr float kIconW = 16.0f;     // the icon column, and the glyph drawn in it
    static constexpr float kIconGap = 12.0f;   // between the column and the words
    static constexpr float kShortcutGap = 24.0f;
    static constexpr float kSepH = 9.0f;       // a line, and the air above and below it
    static constexpr float kRadius = metric::kRadiusCard;
    static constexpr float kRowRadius = metric::kRadiusControl;
    static constexpr float kMinW = 128.0f;
    // **How far a submenu's panel is *inside* the row it belongs to.** The number and what it is measured from
    // are WinUI's own: `CascadingMenuHelper` places a submenu at
    //
    //     subMenuPosition.X += subItemWidth - m_subMenuOverlapPixels;
    //
    // with `static constexpr UINT m_subMenuOverlapPixels = 4` (dxaml/xcp/dxaml/lib/CascadingMenuHelper.{h,cpp}),
    // commented there as "the overlapped menu pixels between the main menu presenter and the sub presenter". So
    // the child's left edge lands 4 DIP inside the *item's* right edge -- which is a row of this menu, inset
    // from the panel by `kPad` -- and the two surfaces read as one thing with a fold in it rather than as two
    // boxes with a gutter between them. Flipped, the same four on the other side.
    static constexpr float kSubOverlap = 4.0f;
    // The room the window keeps around the panel for its shadow: the flyout's own falloff, which is what
    // a menu over a page wants -- see `Painter::Shadow` for why it is not a blur, and `Tip` for the
    // narrower one a tip carries. It is also how far a menu is kept inside the work area, so that a menu
    // against the edge of a screen shows its shadow rather than having it cut off by the bezel.
    static constexpr float kShadowReach = 14.0f;
    static constexpr float kShadowDrop = 4.0f;
    static constexpr float kMargin = kShadowReach + kShadowDrop;

    Menu() {
        // **Not activated, and that is the whole of how a menu behaves here.** See the note at the top of
        // this file: the window keeps the foreground and the menu takes the focus.
        activates = false;
        panel = new MenuPanel();
        panel->menu = this;
        Add(panel);
    }
    // Declared here and defined below the struct: a menu owns the submenu it has open, and a `unique_ptr`
    // to its own type needs that type complete where it is destroyed.
    ~Menu();

    const wchar_t *ClassName() const override { return L"MiculaMenu"; }

    // --- what a menu is made of ---------------------------------------------------------------------
    // Each of these answers the row it made, so that a page can set what the parameters do not cover: an
    // `enabled` of its own, an icon on a check row, an `onSub` it fills in later.
    Row &Item(const std::wstring &text, const wchar_t *icon = nullptr,
              const std::wstring &shortcut = std::wstring(), std::function<void()> onPick = nullptr,
              bool enabled = true) {
        Row r;
        r.text = text;
        r.icon = icon;
        r.shortcut = shortcut;
        r.onPick = std::move(onPick);
        r.enabled = enabled;
        return Push(std::move(r));
    }
    // A row that is a switch. `onCheck` is handed what the state *becomes* rather than what it was, so
    // that the page writes `hidden = on` and nothing in here owns the state it is showing.
    Row &Check(const std::wstring &text, bool checked, std::function<void(bool)> onCheck,
               const wchar_t *icon = nullptr) {
        Row r;
        r.text = text;
        r.icon = icon;
        r.kind = Row::Kind::Check;
        r.checked = checked;
        r.onCheck = std::move(onCheck);
        return Push(std::move(r));
    }
    Row &Sub(const std::wstring &text, std::function<void(Menu &)> build) {
        Row r;
        r.text = text;
        r.kind = Row::Kind::Sub;
        r.onSub = std::move(build);
        return Push(std::move(r));
    }
    Row &Separator() {
        Row r;
        r.kind = Row::Kind::Separator;
        return Push(std::move(r));
    }

    // --- showing it -------------------------------------------------------------------------------
    // **Fill it from the page's builder and put it at `at`**, which is in screen pixels -- the same thing
    // `ClientToScreen` answers with. Nothing is shown when the builder fills in nothing: a menu with no
    // rows in it is not a menu, and an empty box beside the hand is worse than no box.
    void Open(const std::function<void(Menu &)> &build, POINT at) {
        Anchor a;
        a.at = at;
        a.flip = at;
        OpenAt(build, a);
    }

    // **Where a menu puts itself**, which is the whole of the difference between one opened by a hand and one
    // opened beside a row. `at` is the screen point the panel's own corner goes at; `flip` is the point its
    // *right* edge goes at when the work area has no room for it where it wanted to be. The two are the same
    // point for a menu opened by a hand, which is what puts it on the left of the pointer near the right edge
    // of a screen; for a submenu they are the right and the left edge of the row it belongs to, which is what
    // puts it on the left of the menu it came from rather than over that menu. Up and down is `at` either way:
    // a menu with no room below it grows upwards from where it wanted to start.
    struct Anchor {
        POINT at = {};
        POINT flip = {};
    };
    void OpenAt(const std::function<void(Menu &)> &build, const Anchor &a) {
        Close();
        rows.clear();
        hot = armed = -1;
        if (build) build(*this);
        if (rows.empty()) return;
        Measure();

        // DIPs of the monitor it is landing on, asked of the monitor rather than of this window -- a menu
        // opens where the pointer is, and the pointer can be on a screen other than the one the window it
        // was opened from is on. See `dpiapi::ForPoint`.
        const UINT mon = dpiapi::ForPoint(a.at);
        const float scale = (float)mon / 96.0f;
        RECT work = {};
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(MonitorFromPoint(a.at, MONITOR_DEFAULTTONEAREST), &mi);
        work = mi.rcWork;
        // The work area less the room the shadow needs, as a tip keeps itself in: a menu against the edge
        // of a screen shows its shadow whole and a gap beside it rather than sitting on the last pixel.
        const float left = (float)work.left / scale + kMargin, top = (float)work.top / scale + kMargin;
        const float right = (float)work.right / scale - kMargin;
        const float bottom = (float)work.bottom / scale - kMargin;

        const float px = (float)a.at.x / scale, py = (float)a.at.y / scale;
        // **The corner goes at the point, and to the other side of it when the work area runs out** --
        // a menu off the right edge of a screen is a menu half read. A menu is opened by a hand, so the
        // point is where the hand is rather than the middle of a control: the row under the hand is the
        // row the hand is on.
        float x = px, y = py;
        if (x + panelW > right) x = (float)a.flip.x / scale - panelW;
        if (y + panelH > bottom) y = py - panelH;
        x = (std::max)(left, (std::min)(x, right - panelW));
        y = (std::max)(top, (std::min)(y, bottom - panelH));

        const int wx = (int)std::lround(x - kMargin), wy = (int)std::lround(y - kMargin);
        const int ww = (int)std::lround(panelW + kMargin * 2.0f);
        const int wh = (int)std::lround(panelH + kMargin * 2.0f);
        // **And it arrives.** A menu that is simply *there* on the next frame is the one thing about it that
        // reads as a fault: the whole surface fades in over `kFast` -- the panel, its shadow and every row as
        // one group, through the root's own opacity, because fading each of them on its own would show the
        // page through the gaps between them. See `Popup::Arrival`.
        Arrive();
        if (!hwnd) Show(wx, wy, ww, wh, mon);
        else {
            Place(wx, wy, ww, wh);
            if (!Shown()) ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        }
        // The row the hand is over, if it is over the panel: a menu opens *at* the pointer, so a row is
        // under it from the first frame. Asked here rather than left to the first move, because a window
        // that is not animating runs no frames and a resting hand sends no move at all.
        UnderCursor();
        Invalidate();
    }

    // Close it: what Esc, a picked row and the focus going away all do. Not `Hide` -- this is the menu
    // saying that it is over, and a page that wants to hear it (or to have the focus back) does.
    void Close() {
        // **A menu takes what it opened with it.** A child left standing over a page whose menu has gone is
        // a box nobody can put away, and closing from the inside out is also what lets each level answer
        // before the one above it has gone.
        CloseSub();
        if (!Shown()) return;
        Shut();
        hot = armed = -1;
        if (onClose) onClose();
    }

    // Put the chain away without telling the page, which is what a *destructor* needs: see `Menus::~Menus`,
    // where `Close` would run a page callback on its way out of that page's own destruction.
    void Shut() {
        CloseSub();
        Popup::Hide();
    }

    // --- the submenu --------------------------------------------------------------------------------
    // **The menu a row holds, opened beside that row.** Where it goes is the same placement a menu opened
    // by a hand uses -- see `OpenAt` -- with the row's own box for the point: the panel's corner at the
    // panel's right edge plus `kSubGap`, its top at the row's top, and flipped to the left of the row when
    // the work area has no room to the right of it.
    //
    // **The row's box is asked of the panel rather than worked out.** `ScreenBox` is the same call a tip
    // makes to learn where the control it names is, and it is the one place that knows what a widget's own
    // space is and how it travels through the window: a box this computed by hand from the two windows'
    // rectangles would be a second copy of that arithmetic, in the part of this library that has been wrong
    // before.
    void OpenSub(int i) {
        if (i < 0 || i >= (int)rows.size()) return;
        Row &r = rows[i];
        if (r.kind != Row::Kind::Sub || !r.onSub) return;
        // The delay is over whichever way this was called: a submenu opened by a resting hand is opened by
        // the timer that *was* the delay, and one opened by a click or an arrow arrives here first.
        open.Stop();
        if (subRow == i && sub && sub->Shown()) return;
        CloseSub();
        if (!app) return;   // a menu nothing pumps is a window that never draws: see `Menus::Open`
        if (!panel) return;
        if (!sub) sub = std::make_unique<Menu>();
        // **The row's box, on the screen.** `ScreenBox` takes a box in the space the panel's `rect` is
        // measured in -- the client area's, which is where an item's box and a tip's box are written too (see
        // `Widget::Item`) -- while `Row::top` is measured from the panel's own top edge, so the panel's own
        // origin goes back on here. Two spaces, one line apart: this is the arithmetic that was wrong in
        // `RowAt` before, and it is written out rather than folded into `ScreenBox` so that the space
        // `ScreenBox` wants stays the same one everything else uses.
        const D2D1_RECT_F rowBox = { panel->rect.left + kPad, panel->rect.top + r.top,
                                     panel->rect.left + panelW - kPad, panel->rect.top + r.top + r.h };
        const RECT box = ScreenBox(panel, rowBox);
        // **The row's own box is what a submenu hangs off**, and the child's left edge lands `kSubOverlap`
        // inside its right edge -- the same arithmetic WinUI's `CascadingMenuHelper::OpenSubMenu` does, written
        // the same way round: `position.X += subItemWidth - m_subMenuOverlapPixels`. Flipped, the same four the
        // other side of the row: the child's right edge lands that far inside the row's left edge.
        const LONG over = (LONG)std::lround(kSubOverlap * scale());
        Anchor a;
        a.at = { box.right - over, box.top };
        a.flip = { box.left + over, box.top };
        subRow = i;
        sub->owner = this;
        sub->OpenAt(r.onSub, a);
        if (!sub->Shown()) {
            subRow = -1;
            return;
        }
        if (!sub->app) app->Add(*sub);
        // The row that owns an open submenu is the row that has to keep looking lit, and a keyboard move can
        // have brought us here with nothing repainted yet.
        if (panel) panel->Invalidate();
    }

    void CloseSub() {
        open.Stop();
        if (sub && sub->Shown()) sub->Close();
        subRow = -1;
    }
    bool SubShown() const { return subRow >= 0 && sub && sub->Shown(); }

    // Told when the menu has closed itself. This is where a page puts the focus back, and where a trigger
    // learns that the menu it opened is gone.
    std::function<void()> onClose;
    std::vector<Row> rows;
    int hot = -1;     // the row the hand or the keys are on
    int armed = -1;   // the row the button went down on, which is what a release picks
    // **The menu this one has open, and the row of this one it belongs to.** Kept rather than made and
    // thrown away with every open, the same way `Menus` keeps the menu it opens: `Open` fills it again from
    // its builder, and a window that already exists is a window that does not have to be made to be put
    // beside a row for the second time. `subRow` is what says whether a submenu is up at all, because the
    // child's `Shown` is the child's business -- and it is also the row that has to keep looking lit.
    std::unique_ptr<Menu> sub;
    int subRow = -1;
    // **And the menu this one came from**, which is what a chain is: a row that is picked is a menu that is
    // over, all of it, and the level that was picked does not know the levels above it without this. See
    // `Dismiss`.
    Menu *owner = nullptr;
    // The delay between resting on a row and the menu under it opening: see `MenuDwell`, and
    // `MoveHighlight`, which starts it.
    Timer open;
    // Where the pointer was last seen, in the space the panel's `rect` is measured in, and whether it has
    // been seen at all: a move to the same place as the last one is not the pointer choosing anything.
    // See `MenuPanel::OnPointerMove`.
    bool moved = false;
    float moveX = 0.0f, moveY = 0.0f;
    float panelW = 0.0f, panelH = 0.0f;
    // Whether the menu has an icon column at all -- one is needed by any row that has an icon, a tick or a
    // chevron. Reserved for the whole menu rather than sized per row, which is what keeps the words of a
    // menu in one column when only some of its rows have a glyph.
    bool iconColumn = false;

    void OnPlaced() override {
        // The one widget, placed by hand: a menu *is* its rows, and there is nothing to lay out -- the size
        // came from the rows in the first place. Placed rather than glided (`drawn = rect`, no glide): a
        // menu does not slide into place inside itself.
        if (!panel) return;
        panel->rect = { kMargin, kMargin, kMargin + panelW, kMargin + panelH };
        panel->drawn = panel->rect;
        panel->placed = true;
    }

    bool OnMessage(UINT m, WPARAM wp, LPARAM /*lp*/) override {
        switch (m) {
        case WM_KEYDOWN:
            switch (wp) {
            case VK_DOWN:   MoveHighlight(NextPickable(hot < 0 ? -1 : hot, 1)); return true;
            case VK_UP:     MoveHighlight(NextPickable(hot < 0 ? (int)rows.size() : hot, -1)); return true;
            case VK_HOME:   MoveHighlight(NextPickable(-1, 1)); return true;
            case VK_END:    MoveHighlight(NextPickable((int)rows.size(), -1)); return true;
            // **Right is "into the row" and left is "back out of it"**, which is what the two arrows mean
            // inside a menu: a row that holds a submenu opens it, and one that does not has nothing to the
            // right of it. The keys do not wait for the dwell -- that delay is for a hand that is on its way
            // somewhere, and an arrow key is already a statement about where somebody wants to be.
            case VK_RIGHT:
                if (hot < 0 || hot >= (int)rows.size() || rows[hot].kind != Row::Kind::Sub) return false;
                OpenSub(hot);
                return true;
            case VK_LEFT:
                if (!SubShown()) return false;
                CloseSub();
                return true;
            case VK_RETURN:
                if (hot >= 0 && hot < (int)rows.size() && rows[hot].kind == Row::Kind::Sub) {
                    OpenSub(hot);
                    return true;
                }
                Pick(hot);
                return true;
            case VK_ESCAPE:
                // One level at a time: the menu in front goes first, and the one it came from is still there
                // for the next Esc. A menu with no submenu open closes itself, as it always did.
                if (SubShown()) {
                    CloseSub();
                    return true;
                }
                Close();
                return true;
            default:        return false;
            }
        case WM_KILLFOCUS:
            // In the tray case, where the menu has no window behind it to take the keyboard for it, the
            // focus going away is what a click outside looks like. Where a window *did* open it, the menu
            // never takes the focus at all and every click outside comes through `TakeInput`.
            Close();
            return false;
        }
        return false;
    }

    // **The input of the window this menu hangs over, while it is up.** `Menus` hands every message that
    // window is sent to here first (see `Surface::onInput`), because the menu must not take the activation
    // and so cannot be given the input the ordinary way.
    //
    // What it takes it eats, and what it only needs to know about it leaves: an activation is read -- the
    // window going away is the menu going with it -- and then handed on, because the window has work of its
    // own to do about it.
    bool TakeInput(UINT m, WPARAM wp, LPARAM lp) {
        if (!Shown()) return false;
        switch (m) {
        case WM_KEYDOWN:
            // **The deepest menu answers the keys.** A menu with a submenu open is not the menu the keyboard
            // is on: the arrows walk the rows of the one in front, Left steps back out of it and Esc closes
            // it -- and a key the menu in front has no use for is not this one's either, which is why the
            // answer is handed straight back rather than tried here as well.
            return (sub && sub->Shown()) ? sub->TakeInput(m, wp, lp) : OnMessage(m, wp, lp);
        case WM_MOUSEMOVE:
        case WM_NCMOUSEMOVE:
        case WM_MOUSEWHEEL:
            // Eaten so that nothing under the menu lights up or scrolls behind it: the pointer is over
            // the menu, and a control waking up under its shadow is a control that is doing something
            // nobody asked it to.
            return true;
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_XBUTTONDOWN:
        case WM_POINTERDOWN:
            // **A press that arrives here is a press that missed the menu.** A press *on* the menu is
            // delivered to the menu's own window -- that is what its panel is there for -- so anything coming
            // through this door came from somewhere else, whatever coordinates it carries: the ones in this
            // message are the *window's*, and asking them about the menu's panel would be reading one space
            // as another. The menu goes, and the press stops here, which is what the first click outside an
            // open Windows menu does: whatever the menu was opened over is not also pressed by the click that
            // put the menu away.
            Close();
            return true;
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
        case WM_XBUTTONUP:
        case WM_POINTERUP:
            // And the release of a press that missed it, for the same reason: a control under the menu must
            // not see a release without the press that goes with it.
            return true;
        case WM_ACTIVATE:
            // The window went away -- alt-tab, another program taking the front. Read rather than taken:
            // there is a gesture to cancel and a pane to put away in there, and those are the window's.
            if (LOWORD(wp) == WA_INACTIVE) Close();
            return false;
        }
        return false;
    }

    // **The window is bigger than the menu by the room the shadow takes, and a click in that room is not a
    // click on the menu.** Answered as `HTTRANSPARENT` so that the click reaches whatever is under it --
    // and that is also what closes the menu, because a click on another window takes the focus away. A
    // click that landed on the menu's own shadow instead would do nothing at all, and would leave the menu
    // up over a page somebody is trying to use.
    bool OnHitTest(POINT screen, LRESULT &out) override {
        POINT at = screen;
        ScreenToClient(hwnd, &at);
        const float s = scale();
        const float x = (float)at.x / s, y = (float)at.y / s;
        out = (x >= kMargin && x < kMargin + panelW && y >= kMargin && y < kMargin + panelH) ? HTCLIENT
                                                                                            : HTTRANSPARENT;
        return true;
    }

    // The row at a point **in the space the panel's `rect` is measured in**, or -1 for the padding and the
    // separators -- a line is not a thing a hand can be on.
    //
    // **That space and not the panel's own**, which is the one thing about a menu that is easy to get wrong
    // twice: `Row::top` is measured from the panel's top edge, while a pointer hook is handed the point in
    // the space the widget's rectangle lives in -- see `Widget::Paint` and `SendMove` for the same rule about
    // the same two spaces. `OnHitTest` and `UnderCursor` already subtract the margin to get from the client
    // area to a row; this used to answer in the panel's own space, so every point the panel was handed was
    // read as one `kMargin` further up the menu than it was -- a shift of more than half a row, which means
    // the row under the hand was the row *above* it, and a row below a separator was the disabled one after
    // the line (or nothing at all). The panel's origin comes off here, once, so that no caller can be told a
    // row and no caller can forget it.
    int RowAt(float x, float y) const {
        const float left = panel ? panel->rect.left : 0.0f, top = panel ? panel->rect.top : 0.0f;
        x -= left;
        y -= top;
        if (x < kPad || x >= panelW - kPad) return -1;
        for (size_t i = 0; i < rows.size(); i++) {
            const Row &r = rows[i];
            if (r.kind == Row::Kind::Separator) continue;
            if (y >= r.top && y < r.top + r.h) return (int)i;
        }
        return -1;
    }

    // The next row that can be picked, walking from `from` in `step`: a separator and a disabled row are
    // both things the arrows pass over. -1 when there is none that way.
    int NextPickable(int from, int step) const {
        for (int i = from + step; i >= 0 && i < (int)rows.size(); i += step)
            if (rows[i].kind != Row::Kind::Separator && rows[i].enabled) return i;
        return -1;
    }

    // **A menu does not wrap**, which is what Windows does and what keeps the ends of a menu readable:
    // nothing happens when there is nowhere left to go. The highlight is the only thing that changes, so
    // only the panel is repainted.
    //
    // **And the highlight is what a submenu follows.** Leaving the row that opened one closes it, and a hand
    // coming to rest on a row that holds one opens it after the delay -- which is why what moved matters
    // here: the arrows close a submenu and do not open one, because an arrow onto a container says "this row",
    // not "whatever is under it". See `OpenSub` for the other way in.
    void MoveHighlight(int to, bool byHover = false) {
        if (to < 0 || to == hot) return;
        CloseSub();
        hot = to;
        if (byHover && rows[to].kind == Row::Kind::Sub && rows[to].onSub && panel)
            open.Start(this, MenuDwell(), [this] { OpenSub(hot); });
        if (panel) panel->Invalidate();
    }

    // **The whole chain goes, not the level that was picked.** A row that was picked is a menu that is over,
    // and a child left standing over a page whose menu went away is a box nobody can put away; the level that
    // was picked is usually the deepest one and knows nothing above it, so the root is what closes. See
    // `owner`.
    void Dismiss() {
        Menu *root = this;
        while (root->owner) root = root->owner;
        root->Close();
    }

    // Pick a row: the menu goes first, and then the page's callback runs. **In that order**, so that a page
    // that opens a window or a dialog from a row does it with no menu over it, and so that a callback which
    // re-arranges the page is not doing it under a box that is on its way out. A check row is handed what
    // its state becomes rather than what it was.
    void Pick(int i) {
        if (i < 0 || i >= (int)rows.size()) return;
        const Row &r = rows[i];
        if (!r.enabled || r.kind == Row::Kind::Separator) return;
        // **A submenu's row is a way in rather than a choice**: picking it opens what it holds and leaves
        // everything standing, this menu included -- it is the row's own menu that is now in front.
        if (r.kind == Row::Kind::Sub) {
            OpenSub(i);
            return;
        }
        std::function<void()> pick = r.onPick;
        std::function<void(bool)> check = r.onCheck;
        const bool was = r.checked;
        Dismiss();
        if (pick) pick();
        else if (check) check(!was);
    }

    MenuPanel *panel = nullptr;

private:
    // Appended rather than named `Add`, which every `Popup` already is: a menu's rows are not widgets, and
    // the two overloads would hide one another.
    Row &Push(Row &&r) {
        rows.push_back(std::move(r));
        return rows.back();
    }

    // Size the panel to its rows: the widest row plus the two paddings, the rows stacked, and the column
    // the icons and ticks share.
    void Measure() {
        Fonts &f = CurrentFonts();
        iconColumn = false;
        for (const Row &r : rows)
            if (r.icon || r.kind == Row::Kind::Check || r.kind == Row::Kind::Sub) iconColumn = true;
        float w = kMinW;
        for (Row &r : rows) {
            if (r.kind == Row::Kind::Separator) { r.h = kSepH; continue; }
            r.h = kRowH;
            float need = kPad * 2.0f + kRowPad * 2.0f + f.Measure(f.body, r.text);
            if (iconColumn) need += kIconW + kIconGap;
            if (r.kind == Row::Kind::Sub) need += kIconGap + kIconW;   // the chevron's own column
            if (!r.shortcut.empty()) need += kShortcutGap + f.Measure(f.caption, r.shortcut);
            if (need > w) w = need;
        }
        float y = kPad;
        for (Row &r : rows) {
            r.top = y;
            y += r.h;
        }
        panelW = std::ceil(w);
        panelH = y + kPad;
    }

    // The row under the cursor, asked when the menu opens, because the menu opens at the hand and the hand
    // does not move for it. In the space `RowAt` takes, which is the client area's.
    void UnderCursor() {
        POINT p = {};
        if (!GetCursorPos(&p)) return;
        POINT origin = { 0, 0 };
        ClientToScreen(hwnd, &origin);
        const float s = scale();
        hot = RowAt(((float)p.x - (float)origin.x) / s, ((float)p.y - (float)origin.y) / s);
    }
};

// What turns a right-click and the Menu key into a menu. Attached to a surface, like `Tips`, because the
// surface is what hears the hand -- and the menu is the page's object rather than the app's, so it is
// destroyed with the page it belongs to.
//
// ```cpp
// struct MyWindow : Window {
//     Menus menus{ *this };
//     ...
// };
// ```
struct Menus {
    explicit Menus(Surface &s) : surface(s) {
        s.onContextMenu = [this](Widget *w, POINT at) { return Open(w, at); };
        // **And the input of that surface while a menu is up.** The menu is a window of its own and it must
        // not take the activation -- see the note at the top of this file -- so the keys, the clicks and the
        // wheel it needs are the ones the surface it hangs over would have been sent.
        s.onInput = [this](UINT m, WPARAM wp, LPARAM lp) {
            return menu.Shown() && menu.TakeInput(m, wp, lp);
        };
    }
    ~Menus() {
        // The hooks are the surface's and this object is the page's: see the destructor of `Tips` for what
        // that ordering is worth. And the chain goes with it, without telling the page: a submenu left behind
        // would be a window nothing owns, and `Close` here would run a page callback out of the page's own
        // destruction.
        surface.onContextMenu = nullptr;
        surface.onInput = nullptr;
        menu.Shut();
    }
    Menus(const Menus &) = delete;
    Menus &operator=(const Menus &) = delete;

    // The box, for a page that wants to dress it -- `Menu::onClose` is how it hears that it went.
    Menu &Box() { return menu; }
    bool Shown() const { return menu.Shown(); }

    // **Open the menu `w` has, or the one the nearest widget above it has** -- a right-click on a card's
    // label is a right-click on the card. False when there is none, which is what a right-click on a page
    // that has no menus is. Public because a page may want one somewhere the hand is not: a button's
    // `onClick`, a shortcut of its own.
    bool Open(Widget *w, POINT at) {
        Widget *target = w ? w->MenuTarget() : nullptr;
        if (!target || !surface.app) return false;
        menu.Open(target->contextMenu, at);
        if (!menu.Shown()) return false;
        // **A popup nothing pumps is a window that never draws.** The same call a tip makes, and for the
        // same reason: `App::Add` is idempotent, so the menu joins the loop on its first open and stays
        // joined for as long as the page does -- and the menu *is* animated by that loop for as long as it
        // is up, which is the whole point of a menu being a popup rather than a modal loop of its own.
        if (!menu.app) surface.app->Add(menu);
        return true;
    }
    void Close() { menu.Close(); }

private:
    Surface &surface;
    Menu menu;
};

// The submenu is owned, so the destructor has to be written where `Menu` is complete: see the note on its
// declaration. Nothing else about the destruction of a chain needs saying -- the child's own destructor
// takes the grandchild's window down with it, and a popup hides itself when it goes.
inline Menu::~Menu() = default;

// ---------------------------------------------------------------- the panel

inline void MenuPanel::Paint(const Painter &p) {
    if (!menu) return;
    // **The panel is drawn at `rect`, which is this widget's box in its *parent's* space.** A widget paints
    // where its rectangle is measured -- the transform `PaintTree` has already set is the parent's, and the
    // glide is in it -- so drawing at `0, 0` would put the panel in the window's top-left corner and the
    // shadow's left and top would be cut off by the window's edge: the menu's own version of the wedge a tip
    // had. What the widget draws *around* itself is not clipped (see `PaintTree`), which is how the shadow
    // gets out into the margin the window keeps for it.
    const D2D1_RECT_F box = rect;
    p.Shadow(box, Menu::kRadius, 1.0f, Menu::kShadowReach, Menu::kShadowDrop);
    p.FillRound(box, Menu::kRadius, p.pal->flyoutBg);
    p.StrokeRound(box, Menu::kRadius, p.pal->flyoutStroke);

    for (size_t i = 0; i < menu->rows.size(); i++) {
        const Menu::Row &r = menu->rows[i];
        // The rows are placed from the panel: `Row::top` is measured from the panel's own top edge.
        const D2D1_RECT_F row = { box.left + Menu::kPad, box.top + r.top, box.right - Menu::kPad,
                                  box.top + r.top + r.h };
        if (r.kind == Menu::Row::Kind::Separator) {
            // The line is inset from the rows rather than drawn edge to edge: it divides the *rows*, and a
            // line that reaches the panel's corner is a line that cuts the panel in two.
            const float inset = Menu::kPad + 4.0f;
            const float y = (row.top + row.bottom) * 0.5f;
            p.Line(box.left + inset, y, box.right - inset, y, p.pal->flyoutStroke);
            continue;
        }
        if ((int)i == menu->hot && r.enabled) p.FillRound(row, Menu::kRowRadius, p.pal->subtleHover);
        const D2D1_COLOR_F fg = r.enabled ? p.pal->textPrimary : p.pal->textDisabled;
        float left = row.left + Menu::kRowPad;
        float right = row.right - Menu::kRowPad;
        if (menu->iconColumn) {
            // A tick is the icon of a check row, in the icon column: that column is what makes a menu of
            // switches line up with a menu of commands.
            const wchar_t *ic = r.kind == Menu::Row::Kind::Check
                                    ? (r.checked ? glyph::kCheck : nullptr) : r.icon;
            if (ic && *ic) {
                const float gw = p.MeasureWidth(ic, p.font->icon);
                p.Text(ic, { left + (Menu::kIconW - gw) * 0.5f, row.top, left + (Menu::kIconW + gw) * 0.5f,
                             row.bottom },
                       p.font->icon, fg);
            }
            left += Menu::kIconW + Menu::kIconGap;
        }
        if (r.kind == Menu::Row::Kind::Sub) {
            // The chevron, and the column it owns at the far end of the row.
            const float gw = p.MeasureWidth(glyph::kChevronRight, p.font->icon);
            p.Text(glyph::kChevronRight, { right - gw, row.top, right, row.bottom }, p.font->icon, fg);
            right -= gw + Menu::kIconGap;
        }
        if (!r.shortcut.empty()) {
            const float sw = p.MeasureWidth(r.shortcut, p.font->caption);
            p.Text(r.shortcut, { right - sw, row.top, right, row.bottom }, p.font->caption,
                   p.pal->textSecondary);
            right -= sw + Menu::kShortcutGap;
        }
        // Last, so that a long label is what gets clipped rather than a shortcut or a chevron: the words
        // are the row and the two columns beside them are read at a glance.
        //
        // **A row's own box and its own colours, worked out one row at a time**, which is what a per-element
        // arrival would grow into: each row with an offset to slide from and an opacity of its own, rather
        // than the whole menu arriving as one group. Nothing is wired up for that yet -- it is a change in
        // this loop and nothing else.
        p.Text(r.text, { left, row.top, right, row.bottom }, p.font->body, fg);
    }
}

inline void MenuPanel::OnPointerMove(float x, float y) {
    if (!menu) return;
    // **A pointer that has not moved is not a pointer choosing anything.** Windows sends a move to a window
    // that has just appeared under a still pointer -- and a menu opens under the pointer every time -- so a
    // menu that took every move at face value would put the highlight back on the row under the hand on the
    // frame after the keyboard moved it. A move to the same place as the last one is therefore dropped, and
    // the first one is kept: the row under the hand when the menu opens is a choice already made.
    const bool same = menu->moved && x == menu->moveX && y == menu->moveY;
    menu->moved = true;
    menu->moveX = x;
    menu->moveY = y;
    if (same) return;
    const int i = menu->RowAt(x, y);
    // `byHover`: a hand resting on a row that holds a submenu opens it, after the delay a hand is given; an
    // arrow onto the same row does not. See `Menu::MoveHighlight`.
    menu->MoveHighlight(i, true);
}

inline void MenuPanel::OnPress(float x, float y) {
    if (menu) menu->armed = menu->RowAt(x, y);
}

inline void MenuPanel::OnRelease() {
    if (!menu) return;
    const int armed = menu->armed;
    menu->armed = -1;
    // Press and release on the same row, which is what every other control here means by a click -- and a
    // release that has moved off the row it started on picks nothing, which is how a hand takes a menu back.
    if (armed >= 0 && armed == menu->hot) menu->Pick(armed);
}

inline int MenuPanel::AccessibleItems() const { return menu ? (int)menu->rows.size() : 0; }

inline bool MenuPanel::AccessibleItem(int i, Item &out) const {
    if (!menu || i < 0 || i >= (int)menu->rows.size()) return false;
    const Menu::Row &r = menu->rows[i];
    out.name = r.text.c_str();
    out.type = r.kind == Menu::Row::Kind::Separator ? UIA_SeparatorControlTypeId : UIA_MenuItemControlTypeId;
    out.index = i;
    out.selected = r.checked;
    // The row's box in the space the panel's own `rect` is in, which is the space a widget's items are
    // written in and what the element adds the accumulated origin of the widget's ancestors to. The panel's
    // own origin is part of that, so it is here -- a row reported a margin up the menu is a highlight on the
    // wrong row for anybody reading the screen rather than looking at it.
    out.box = { rect.left + Menu::kPad, rect.top + r.top, rect.left + menu->panelW - Menu::kPad,
                rect.top + r.top + r.h };
    // A menu is on screen when it is up, and every row of it is: a menu is as tall as its rows, so there is
    // no row the scroll could have carried out of it.
    out.onscreen = true;
    return true;
}

}  // namespace micula
