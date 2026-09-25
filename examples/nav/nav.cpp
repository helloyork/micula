// The navigation pane with every one of its switches on the page beside it. A playground
// rather than a program: the pane's style, where the page goes, whether the width animates,
// whether the page is dimmed and whether the arrow keys carry the choice are each a control
// away from here.
//
// The state is file-scope globals and nothing is saved. Change something, open and close the
// pane, and look at it -- that is the whole of what this window is for. Switching between
// the styles at run time is one assignment: they are fields rather than four controls.

#include <micula/micula.h>

#include <cwchar>
#include <string>
#include <vector>

using namespace micula;

namespace {

constexpr float kHeaderH = 104.0f;   // caption, title and subtitle; does not scroll
constexpr float kPad     = 24.0f;
constexpr float kRowH    = 64.0f;
constexpr float kRowGap  = 4.0f;
constexpr float kInset   = 18.0f;    // card padding, left and right
constexpr float kGlide   = 0.07f;    // seconds for the page's drawing to catch up

// The icons this window draws, from the library's own list: nothing here is a code point of its
// own, and a page that needs one the library does not name adds it to <micula/glyphs.h> rather
// than starting a list like this one. The names are what the cards below call them by.
constexpr const wchar_t *kIconRecent   = glyph::kRecent;
constexpr const wchar_t *kIconHome     = glyph::kHome;
constexpr const wchar_t *kIconCalendar = glyph::kCalendar;
constexpr const wchar_t *kIconColor    = glyph::kColor;
constexpr const wchar_t *kIconMotion   = glyph::kBusy;
constexpr const wchar_t *kIconLight    = glyph::kBrightness;
constexpr const wchar_t *kIconWidth    = glyph::kAdd;
constexpr const wchar_t *kIconBattery  = glyph::kBolt;
constexpr const wchar_t *kIconCamera   = glyph::kCamera;
constexpr const wchar_t *kIconDebug    = glyph::kBug;

// --- the state: deliberately not saved ------------------------------------------------
int   page   = 0;
float scroll = 0.0f, drawn = 0.0f, maxScroll = 0.0f;

int   paneStyle  = 1;              // PaneStyle: Fixed, Toggle, Peek, Minimal
int   navRows    = 0;              // extra pane rows: `nav=24` for a pane taller than the window
bool  paneSlides = true;           // whether the width animates
bool  paneScrim  = false;          // whether an overlay pane dims the page
bool  paneFollow = true;           // whether the arrow keys carry the choice
bool  paneOwn    = true;           // whether the pane draws its own button
float paneOpenW  = 260.0f;
bool  paneOpen   = true;
int   windowBackdrop = 2;          // DWM_SYSTEMBACKDROP_TYPE: 2 Mica, 3 Acrylic, 4 Mica Alt


// State for the other pages, so that their controls have something of their own to hold.
bool demoBattery = false, demoDownloads = true, demoDesktop = false;
bool demoScreens = true, demoSkip = true;

const wchar_t *const kStyles[] = { L"Fixed", L"Toggle", L"Peek", L"Minimal" };

// The debug page's two lists: how many rows past the pages the pane is built with, and the DWM
// backdrops by their `backdrop=` values. Both of them are restarts, because both are settled
// when the window is made -- the pane's rows as its items, the backdrop as an attribute of a
// window that is already up -- and it is exactly those two that a page has no way to reach.
const int kRowChoices[] = { 0, 8, 24, 40 };
const wchar_t *const kBackdrops[] = { L"Auto", L"None", L"Mica", L"Acrylic", L"Mica Alt" };

template <size_t N>
std::vector<std::wstring> Names(const wchar_t *const (&names)[N]) {
    return { names, names + N };
}

struct PageInfo { const wchar_t *title, *detail; };
const PageInfo kPages[] = {
    { L"The pane",    L"Every switch it has, on the page it is looking at" },
    { L"A long page", L"Scroll it, then open and close the pane" },
    { L"Schedule",    L"One of a group, with a heading above it" },
    { L"Folders",     L"...and the other one, so the group has two" },
    { L"Settings",    L"A footer row: the last one sits at the bottom" },
};

// The debug page is not one of those: it is the first of the footer's rows, so it is described
// here and drawn by its own branch of the layout. It is the command line this window was started
// with, as controls, which is the point of it -- a state that can only be asked for when the
// window is made is one that only the command line can ask for, and half of what this window can
// do now has no control at all.
const PageInfo kDebugPage = { L"Debug", L"The command line this window was started with" };

std::wstring Dip(float v) {
    wchar_t b[16];
    swprintf(b, 16, L"%d", (int)(v + 0.5f));
    return b;
}

// The system backdrop the window asks DWM for. Mica is the library's default; the others are
// here so that the difference can be looked at side by side, which is what `backdrop=` is for.
const wchar_t *BackdropName() {
    switch (windowBackdrop) {
    case 0:  return L"Auto (0) - DWM decides";
    case 1:  return L"None (1) - the window paints its own background";
    case 3:  return L"Acrylic (3) - DWMSBT_TRANSIENTWINDOW";
    case 4:  return L"Mica Alt (4) - DWMSBT_TABBEDWINDOW";
    default: return L"Mica (2) - DWMSBT_MAINWINDOW";
    }
}

// The state can be asked for on the command line -- `micula-nav.exe style=2 open=0` -- so that
// a state worth looking at does not have to be clicked to. Nothing is remembered either way.
void ReadState(const wchar_t *cmd) {
    if (!cmd) return;
    auto number = [&](const wchar_t *key, int fallback) {
        const wchar_t *at = wcsstr(cmd, key);
        return at ? (int)wcstol(at + wcslen(key), nullptr, 10) : fallback;
    };
    paneStyle  = (std::min)((std::max)(number(L"style=", paneStyle), 0), 3);
    // More rows in the pane than the window can show, which is what the pane's own scrolling is
    // for: `nav=24` puts twenty-four of them above the footer.
    navRows = (std::min)((std::max)(number(L"nav=", navRows), 0), 40);
    // Up to the last row the pane will have: the debug page passes its own place in the pane back
    // in when it restarts, and that place is `navRows` rows below the pages.
    page       = (std::min)((std::max)(number(L"page=", page), 0), 6 + navRows);
    paneOpen   = number(L"open=", paneOpen ? 1 : 0) != 0;
    paneSlides = number(L"animate=", paneSlides ? 1 : 0) != 0;
    paneScrim  = number(L"scrim=", paneScrim ? 1 : 0) != 0;
    paneFollow = number(L"follow=", paneFollow ? 1 : 0) != 0;
    paneOwn    = number(L"own=", paneOwn ? 1 : 0) != 0;
    paneOpenW  = (float)number(L"width=", (int)paneOpenW);
    windowBackdrop = (std::min)((std::max)(number(L"backdrop=", windowBackdrop), 0), 4);
}

}  // namespace

struct NavDemo : Window {
    // What PaintPage draws, worked out by Layout.
    struct Card {
        D2D1_RECT_F r;
        const wchar_t *icon;
        std::wstring title, detail;
        float textRight;             // the control's left edge; the text stops short of it
        std::wstring aside;          // a value shown beside the control
    };
    std::vector<Card> cards;
    std::vector<std::pair<float, std::wstring>> headings;
    // The pane, and the page's scroll bar: both persistent, because both are operated while
    // the page is laid out again -- the pane by its own button, the bar by a drag.
    SideNav   *pane = nullptr;
    ScrollBar *bar  = nullptr;
    // The page's two places that name the style, kept so that changing it does not have to rebuild
    // the page: what the page says about the style is a value and an enabled flag, not a different
    // page. Set by Layout, which is the only thing that makes them.
    Button *drive = nullptr;
    int styleCard = -1, openCard = -1;
    // The row the debug page is, which is the first of the footer's. Worked out by Layout, which
    // is the only thing that knows how many rows the pane was built with.
    int debugRow = -1;

    const wchar_t *ClassName() const override { return L"MiculaNavDemo"; }
    const wchar_t *Title() const override { return L"Micula - navigation pane"; }
    void MinSize(int *w, int *h) const override { *w = 760; *h = 520; }

    // The width the pane has made its own. A pane that pushes takes the room it is *drawn* at, so
    // that the page follows it while it moves -- see FollowPane. A pane that covers takes the rail
    // and no more: the page does not move when the pane opens over it.
    float Inset() const {
        if (!pane) return 0.0f;
        return pane->Pushes() ? pane->Width() : pane->Reserved();
    }
    // The inset the cards and the scrolled controls were laid out with, so that FollowPane can
    // tell how far they are behind.
    float laidOutAt = 0.0f;
    // The page follows the pane by being *moved*, not rebuilt. A rebuild is the wrong tool twice
    // over: it throws away whatever the page is in the middle of -- an open drop-down, a drag --
    // and it is only the room that changed, not what is in it; and a rebuild triggered by the
    // frame that is on its way out can read a width the pane has not finished arriving at, after
    // which nothing runs to correct it. Comparing what is drawn against what was laid out, on
    // every frame, has neither problem.
    //
    // One edge, because one edge is all a layout would have moved: everything in a row is placed
    // from the card's *right* edge -- its control is right-aligned in it, and so is the value
    // beside that -- so a wider room is a wider card and nothing else. Moving the whole rectangle
    // moved the controls with it, which put the drop-down somewhere a layout never would.
    void FollowPane() {
        const float dx = Inset() - laidOutAt;
        if (std::fabs(dx) < 0.05f) return;
        laidOutAt = Inset();
        for (Card &cd : cards) cd.r.left += dx;
        Invalidate();
    }
    // The header is not part of the scrolling area, so the clip starts below it.
    D2D1_RECT_F ClipRect() const override { return { Inset(), kHeaderH, ClientW(), ClientH() }; }
    void ContentTransform(float *dy, float *opacity) const override {
        *dy = -drawn;
        *opacity = 1.0f;
    }
    // The page's own answer to "keep the timer alive": the scroll is still gliding towards where
    // the wheel left it.
    bool AnimationWanted() const override { return drawn != scroll; }
    void OnTick(float dt) override {
        // A pane that pushes is part of the layout, so the page follows it while it moves -- and
        // it is moved rather than laid out again. See FollowPane.
        if (pane && pane->Pushes()) FollowPane();
        if (drawn == scroll) return;
        drawn += (scroll - drawn) * (1.0f - std::exp(-dt / kGlide));
        if (std::fabs(scroll - drawn) < 0.5f) drawn = scroll;
        SyncBar();
    }
    void SyncBar() {
        if (!bar) return;
        bar->value = scroll;
        bar->drawn = drawn;
    }
    void ScrollTo(float to, bool glide = true) {
        scroll = (std::min)((std::max)(to, 0.0f), maxScroll);
        if (!glide) drawn = scroll;
        SyncBar();
        if (bar) { bar->Wake(); bar->Poll(); }
        if (drawn != scroll) StartAnimation(this);
        Invalidate();
    }
    // The wheel over anything that did not take it itself.
    bool OnAppMessage(UINT m, WPARAM wp, LPARAM) override {
        if (m != WM_MOUSEWHEEL) return false;
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        if (lines == WHEEL_PAGESCROLL) lines = 6;
        const float notches = (float)GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA;
        ScrollTo(scroll - notches * (float)lines * 22.0f);
        return true;
    }

    // The pane's switches, from the globals above and onto it. Called by Layout, which every
    // one of the callbacks below runs -- so a control that changes one of them changes the
    // pane as well, in the same breath.
    void Dress() {
        pane->selected = page;
        pane->style = (PaneStyle)paneStyle;
        pane->animate = paneSlides;
        pane->scrim = paneScrim;
        pane->followsFocus = paneFollow;
        pane->ownToggle = paneOwn;
        pane->openW = paneOpenW;
        pane->open = paneOpen;
        // The left edge, the top and the bottom: the pane owns its own right edge, so that a
        // closed rail takes exactly the clicks that belong to it.
        pane->rect = { 0, kCaptionH, 0, ClientH() };
    }
    void Changed() {
        Layout();
        Invalidate();
    }
    // The pane is told the new style, and the page's own two mentions of it are rewritten in place.
    // See `toggle` for why this is not a rebuild.
    void Restyle() {
        Dress();
        if (styleCard >= 0 && styleCard < (int)cards.size())
            cards[styleCard].detail = kStyles[paneStyle];
        const bool minimal = (PaneStyle)paneStyle == PaneStyle::Minimal;
        if (drive) drive->enabled = !minimal;
        if (openCard >= 0 && openCard < (int)cards.size())
            cards[openCard].detail = minimal ? L"Nothing to open: Minimal never expands"
                                             : L"The pane's state, driven from the page";
        Invalidate();
    }

    // What this window was started as, as arguments, which is what a restart passes on. The
    // window's own geometry is not in it: a debug page is about the state a person can look at,
    // and the one thing that is not in the state is where the window happens to be.
    std::wstring CommandLine() const {
        wchar_t b[256];
        swprintf(b, 256,
                 L"style=%d nav=%d page=%d open=%d animate=%d scrim=%d follow=%d own=%d "
                 L"width=%d backdrop=%d",
                 paneStyle, navRows, page, paneOpen ? 1 : 0, paneSlides ? 1 : 0,
                 paneScrim ? 1 : 0, paneFollow ? 1 : 0, paneOwn ? 1 : 0, (int)paneOpenW,
                 windowBackdrop);
        return b;
    }
    // Starts this window again with the state it is showing. A restart rather than a rebuild,
    // because not everything here is rebuildable: the pane's rows are its items and are built
    // once, and the DWM backdrop belongs to a window that already exists. Both of them are things
    // a debug page has to be able to reach, and both are reachable only from the command line --
    // so the page hands the command line back to itself. Nothing is saved either way, so this is
    // also how a person finds out whether the arguments do what they say.
    void Relaunch() {
        wchar_t exe[MAX_PATH];
        if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return;
        std::wstring line = L"\"" + std::wstring(exe) + L"\" " + CommandLine();
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(nullptr, &line[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si,
                            &pi))
            return;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        if (hwnd) PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    // Which of the debug page's row counts is the one the pane was built with.
    int RowsChoice() const {
        for (int i = 0; i < (int)(sizeof(kRowChoices) / sizeof(kRowChoices[0])); i++)
            if (kRowChoices[i] == navRows) return i;
        return 0;
    }

    void Layout() override;
    void PaintPage(const Painter &p) override;
};

void NavDemo::Layout() {
    ClearWidgets();   // everything but `pane` and `bar`
    cards.clear();
    headings.clear();
    drive = nullptr;
    styleCard = openCard = -1;

    Painter measure;
    measure.font = &fonts;

    // The pane is made once and lives across every layout, which it has to: its open state, a
    // width half way through its animation and the accent bar's travel all belong to it, and
    // choosing a page is exactly what lays the page out again. Everything else here is thrown
    // away and made from the globals, which is the rule for the rest of the window too.
    if (!pane) {
        pane = Add(new SideNav());
        pane->persistent = true;
        pane->items = {
            NavItem::Heading(L"Pages"),
            { kIconHome,     L"The pane" },
            { kIconRecent,   L"A long page" },
            NavItem::Heading(L"More"),
            { kIconCalendar, L"Schedule" },
            { glyph::kFolder, L"Folders" },
        };
        pane->footer = {
            { kIconDebug, L"Debug" },
            { glyph::kSettings, L"Settings" },
        };
        for (int i = 0; i < navRows; i++) {
            wchar_t name[32];
            swprintf(name, 32, L"Row %d", i + 1);
            pane->items.push_back({ kIconRecent, name });
        }
        pane->onSelect = [this](int i) {
            page = i;
            // A page starts at the top: this is the one thing here that does reset the scroll,
            // and it is the page's own doing rather than the pane's. Opening and closing the
            // pane never comes through here, so the scroll stays where it was.
            scroll = drawn = 0.0f;
            Changed();
        };
        pane->onToggle = [this](bool open) {
            // The pane's own button. In Push the page has to lay itself out again; in Overlay
            // this is a layout that changes nothing, which is what makes `Reserved()` the one
            // thing a page has to ask.
            paneOpen = open;
            Changed();
        };
    }
    // The rows the pane has above its footer, which is where the footer's own numbering starts and
    // where the debug page is. It is the first of the footer's rows rather than the last so that
    // adding it moves nothing: `page` is a row's place in the pane, and a row added at the end
    // leaves every other place exactly where it was.
    debugRow = 0;
    for (const NavItem &it : pane->items) if (!it.header) debugRow++;
    Dress();

    const float left = Inset() + 12.0f, right = ClientW() - 24.0f;
    // What the cards and the controls below are placed against, for FollowPane: from here until
    // the next Layout, they are that many DIPs behind the pane.
    laidOutAt = Inset();
    float y = 8.0f;
    auto at = [&](float py) { return kHeaderH + py; };
    auto heading = [&](const wchar_t *text) {
        if (!cards.empty()) y += 24.0f - kRowGap;
        headings.push_back({ at(y), text });
        y += 30.0f;
    };
    // Adds a card and returns the rectangle for its control, on the right of the first row.
    auto card = [&](const wchar_t *icon, std::wstring title, std::wstring detail,
                    float controlW, float height = kRowH) {
        const D2D1_RECT_F r = { left, at(y), right, at(y) + height };
        const float slot = r.right - kInset - controlW;
        cards.push_back({ r, icon, std::move(title), std::move(detail), slot - 16.0f, L"" });
        y += height + kRowGap;
        const float cy = r.top + kRowH / 2;
        return D2D1_RECT_F{ slot, cy - metric::kControlH / 2, r.right - kInset,
                            cy + metric::kControlH / 2 };
    };
    // Everything the page lays out scrolls: `rect` is in the page's own coordinates and the
    // window moves it with ContentTransform() rather than with another Layout().
    auto place = [](Widget *wd, const D2D1_RECT_F &r) {
        wd->rect = r;
        wd->scrolls = true;
        return wd;
    };
    auto toggle = [&](const wchar_t *icon, const wchar_t *title, const wchar_t *detail,
                      bool *state, bool paneSetting) {
        // A switch that only tells the *pane* something does not rebuild the page, because the page
        // has not changed. It matters more than it sounds: a rebuild makes a new switch at the new
        // state, and the one that was animating the knob across has just been thrown away -- so the
        // switch snaps to the far end instead of moving. A page whose own content depends on a
        // switch is the case that has to rebuild, and it pays for it exactly this way.
        place(Add(new ToggleSwitch(L"", *state, [this, state, paneSetting](bool on) {
                  *state = on;
                  if (paneSetting) { Dress(); Invalidate(); }
              })),
              card(icon, title, detail, 40));
    };

    if (page == debugRow) {
        // The command line, as controls. The two lists below are restarts by nature -- what they
        // ask for is settled before a page exists -- and the button is there for the state that
        // has a control somewhere else: this is the one place that starts over from scratch.
        heading(L"The command line");
        place(Add(new DropDown({ L"No extra rows", L"8 rows", L"24 rows", L"40 rows" },
                               RowsChoice(), [this](int i) {
                                   navRows = kRowChoices[i];
                                   Relaunch();
                               })),
              card(kIconRecent, L"nav=",
                   L"Rows in the pane past the ones the pages take up", 180));
        place(Add(new DropDown(Names(kBackdrops), windowBackdrop, [this](int i) {
                      windowBackdrop = i;
                      Relaunch();
                  })),
              card(kIconLight, L"backdrop=",
                   L"The DWM backdrop, which a window is given as it is made", 180));
        card(L"", L"Started with", CommandLine(), 0);
        {
            Button *again =
                Add(new Button(L"Restart", ButtonStyle::Standard, [this] { Relaunch(); }));
            place(again, card(kIconMotion, L"Again",
                              L"Close this window and start the same one with these arguments",
                              again->PreferredWidth(measure)));
        }
    } else
    switch (page) {
    case 0:
        heading(L"The pane");
        styleCard = (int)cards.size();
        place(Add(new DropDown(Names(kStyles), paneStyle, [this](int i) {
                  paneStyle = i;
                  Restyle();
              })),
              card(kIconColor, L"Style", kStyles[paneStyle], 180));
        toggle(kIconMotion, L"The width slides", L"Off, it arrives in one frame", &paneSlides,
               true);
        toggle(kIconLight, L"Dim the page", L"An overlay pane is over something", &paneScrim,
               true);
        toggle(kIconBattery, L"Arrow keys choose",
               L"Off: they move the ring and Enter chooses", &paneFollow, true);
        toggle(glyph::kMenu, L"The pane's own button",
               L"Off: a button on the page. Minimal never draws one", &paneOwn, true);
        {
            const D2D1_RECT_F slot = card(kIconWidth, L"Open width",
                                          L"Taken off the page in Push, over it in Overlay", 150);
            Slider *w = Add(new Slider(paneOpenW, 180.0f, 380.0f, 10.0f, [this](float v) {
                paneOpenW = v;
                if (pane) pane->openW = v;
                if (!cards.empty()) cards.back().aside = Dip(v);
                // No layout from here: this runs from inside the slider's own paint, and a
                // rebuild would end the drag it is in the middle of.
            }));
            // A Push page is laid out again from the width, so that happens when the finger
            // comes up rather than on every step of it.
            w->onCommit = [this](float) { Changed(); };            place(w, slot);
            cards.back().aside = Dip(paneOpenW);
        }
        {
            drive = Add(new Button(paneOpen ? L"Close the pane" : L"Open the pane",
                                   ButtonStyle::Standard,
                                   [this] { if (pane) pane->Toggle(); }));
            // Minimal never expands, so there is nothing for this to do while it is the style.
            // The pane does nothing rather than the page having to ask which style it is in.
            drive->enabled = (PaneStyle)paneStyle != PaneStyle::Minimal;
            openCard = (int)cards.size();
            place(drive, card(glyph::kMenu, L"Open",
                              (PaneStyle)paneStyle == PaneStyle::Minimal
                                  ? L"Nothing to open: Minimal never expands"
                                  : L"The pane's state, driven from the page",
                              drive->PreferredWidth(measure)));
        }
        break;
    case 1:
        // A list longer than the room it opens into, with the choice at its end: opening it
        // needs the clamp at the top *and* the bar, and the two of them together is the case
        // worth poking at. Scrolled down a little, this control comes up near the header.
        {
            std::vector<std::wstring> many;
            for (int i = 1; i <= 40; i++) {
                wchar_t name[32];
                swprintf(name, 32, L"Option %d", i);
                many.push_back(name);
            }
            DropDown *longList = Add(new DropDown(many, 39, [](int) {}));
            place(longList, card(kIconRecent, L"Forty options",
                                 L"Longer than the room: the clamp and the bar together",
                                 180));
        }
        // Enough rows that the page scrolls, which is the point of this page: open and close
        // the pane and the scroll stays exactly where it was, because it is the page's own
        // state and a layout does not touch it.
        for (int i = 1; i <= 24; i++) {
            wchar_t title[32];
            swprintf(title, 32, L"Row %d", i);
            card(kIconRecent, title,
                 L"Open the pane, move it, close it: this page is still where you left it", 0);
        }
        break;
    case 2: {
        heading(L"Run");
        place(Add(new Segmented({ L"At sign-in", L"Daily", L"Weekly" }, 1, [](int) {})),
              card(kIconCalendar, L"When", L"A Segmented control on a page of its own", 250));
        DropDown *day =
            Add(new DropDown({ L"Midnight", L"2:00", L"4:00", L"6:00", L"Noon", L"18:00" },
                             1, [](int) {}));
        // Times of day are a ring, so the step past 18:00 is after midnight.
        day->wrapAround = true;
        place(day, card(kIconRecent, L"Time of day", L"A DropDown that wraps", 140));
        toggle(kIconBattery, L"Run on battery power", L"Off by default to save battery",
               &demoBattery, false);
        break;
    }
    case 3:
        toggle(glyph::kFolder, L"Downloads", L"Files saved by browsers and other apps",
               &demoDownloads, false);
        toggle(glyph::kFolder, L"Desktop", L"Loose files on the desktop", &demoDesktop, false);
        toggle(kIconCamera, L"Screenshots", L"Pictures\\Screenshots", &demoScreens, false);
        toggle(kIconLight, L"Skip hidden files", L"Nothing here is saved anyway", &demoSkip, false);
        break;
    default:
        heading(L"About");
        card(glyph::kInfo, L"Micula " MICULA_VERSION_STRING,
             L"Header-only Fluent controls for Win32", 0);
        card(glyph::kSettings, L"examples/nav",
             L"The navigation pane, and the switches that shape it", 0);
        card(kIconLight, L"Backdrop", BackdropName(), 0);
        heading(L"Notes");
        card(L"", L"Nothing here is saved",
             L"Close the window and every setting goes back to the one in the source", 0);
        card(L"", L"The scroll is the page's, the pane never touches it",
             L"Which is what keeps opening the pane from sending a page back to the top", 0);
        break;
    }

    const float extent = y - kRowGap + kPad;
    const float viewport = ClientH() - kHeaderH;
    maxScroll = (std::max)(0.0f, extent - viewport);
    // Laid out again from the end rather than drawn with a gap, but never from the top: a page
    // whose content got shorter is a page that was scrolled near its end, and the top of it is
    // the one place nobody asked to be.
    if (scroll > maxScroll) scroll = maxScroll;
    if (drawn > maxScroll || drawn < 0.0f) drawn = scroll;

    if (!bar) {
        bar = Add(new ScrollBar([this](float to, bool glide) { ScrollTo(to, glide); }));
        bar->persistent = true;
    }
    bar->rect = { ClientW() - ScrollBar::kSize - 1, kHeaderH, ClientW() - 1, ClientH() - 1 };
    bar->area = ClipRect();
    bar->viewport = viewport;
    bar->extent = extent;
    bar->visible = extent > viewport;
    SyncBar();
}

void NavDemo::PaintPage(const Painter &p) {
    const Palette &c = *p.pal;
    const float left = Inset() + 12.0f, right = ClientW() - 24.0f;
    // The debug page's row is the footer's first and has a heading of its own, because it is not
    // one of the pages in `kPages` at all. The row after it is Settings, which is the last of the
    // About pages: it used to be number four and is one further down now, so it is named here
    // rather than counted, and selecting it goes on showing the About page it always did. A
    // `Row N` page is the About one, as it always was.
    const PageInfo &info = page == debugRow       ? kDebugPage
                         : page == debugRow + 1   ? kPages[4]
                         : kPages[page >= 0 && page < 4 ? page : 0];
    // The page is a surface rather than the window: WinUI's NavigationView content is a layer
    // over the backdrop with its top-left corner rounded and its other three square, tucked
    // under the title bar and flush with the other two edges. The pane pushes it -- which is the
    // whole of what Inset() is for -- and a pane opening over it covers it.
    const D2D1_RECT_F frame = { Inset(), (float)kCaptionH, ClientW(), ClientH() };
    // WinUI's content layer: one rounded corner and three square, filled with the layer colour
    // and bordered inside its edge with the card's, along the two edges that face the rest of
    // the window. The right and bottom edges are the window's own, where a line would be
    // doubling a boundary that is already there -- and where a translucent layer's border shows
    // up darkest, because the layer ends and there is nothing behind the half-covered pixel.
    p.Panel(frame, Corners(metric::kRadiusCard, 0.0f, 0.0f, 0.0f), c.layerBg, c.cardStroke,
            edge::kTop | edge::kLeft);

    // The header does not scroll, and starts where the page does -- so a pane pushing the page
    // pushes this with it.
    p.Text(info.title, { left, kCaptionH + 8, right, kCaptionH + 52 }, p.font->title,
           c.textPrimary);
    p.Text(info.detail, { left, kCaptionH + 52, right, kCaptionH + 72 }, p.font->body,
           c.textSecondary);

    // The scrolling part, clipped to the same rectangle the window clips the scrolling controls
    // to, so that a card and the control on it go under the header together. The cards are in
    // page coordinates like the controls, and PaintPage is not transformed, so the page's
    // offset comes off the drawing here by hand.
    p.rt->PushAxisAlignedClip(ClipRect(), D2D1_ANTIALIAS_MODE_ALIASED);
    const float off = -drawn;
    for (const auto &h : headings)
        p.Text(h.second, { left, h.first + off, right, h.first + off + 30 }, p.font->bodyStrong,
               c.textPrimary);
    for (const Card &cd : cards) {
        const D2D1_RECT_F r = { cd.r.left, cd.r.top + off, cd.r.right, cd.r.bottom + off };
        p.FillRound(r, metric::kRadiusControl, c.cardBg);
        p.StrokeRound(r, metric::kRadiusControl, c.cardStroke);
        p.Text(cd.icon, { r.left + kInset, r.top, r.left + kInset + 20, r.top + kRowH },
               p.font->icon, c.textPrimary);

        float textRight = cd.textRight;
        if (!cd.aside.empty()) {
            // Right-aligned against the *control*, not the card: the control is the last thing on
            // the row and its left edge is `textRight` plus the gap the card left it. Hung off the
            // card's own edge instead, the value sat on top of the slider.
            const float aw = p.MeasureWidth(cd.aside, p.font->body);
            const float controlLeft = cd.textRight + 16.0f;
            const float asideRight = controlLeft - 12.0f;
            p.Text(cd.aside, { asideRight - aw, r.top, asideRight, r.top + kRowH }, p.font->body,
                   c.textSecondary);
            textRight = (std::min)(textRight, asideRight - aw - 16.0f);
        }
        p.Text(cd.title, { r.left + 50, r.top + 13, textRight, r.top + 33 }, p.font->body,
               c.textPrimary);
        p.Text(cd.detail, { r.left + 50, r.top + 33, textRight, r.top + 53 }, p.font->caption,
               c.textSecondary);
    }
    p.rt->PopAxisAlignedClip();
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, wchar_t *cmd, int) {
    ReadState(cmd);
    EnablePerMonitorDpi();
    // WIC is COM, and the window goes through it for the caption icon and Window::Image.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    int code = 1;
    {
        NavDemo d;
        d.backdrop = (DWORD)windowBackdrop;
        if (d.Create(1040, 700, true, nullptr)) code = d.Run();
    }
    CoUninitialize();
    return code;
}
