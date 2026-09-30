// The navigation pane with every one of its switches a control away from here: a playground rather
// than a program. The pane's style, whether the width animates, whether the page is dimmed, whether
// the arrow keys carry the choice and whether the pane draws its own button are each a control on the
// page the window opens on -- change one, open and close the pane, and look at it.
//
// The state can be asked for on the command line, so that a state worth looking at does not have to be
// clicked into, and nothing is remembered between runs:
//
//     micula-nav style=2 open=0 scrim=1 nav=24 page=1 surface=0 transition=0
//
// `--dump` prints the rectangles the shell came out as, with no window anywhere: the same mode
// examples/gallery has, and the one to reach for when the pane and the page disagree about where the
// pane's edge is.
//
// The shell is the library's own (`NavigationView`), so what this file is left with is the pages and
// the switches: a row and its page are added together, and switching a page is that page's `visible`.
//
// The pages are also what a navigation window has in it -- a long list, a page of real controls, four
// switches -- because a page that is only a place for the shell to put something is not a page: what
// the pane does to the page it is looking at is only visible when the page has something on it.

#include <micula/micula.h>

#include <cstdio>
#include <cwchar>
#include <fcntl.h>
#include <io.h>
#include <string>

using namespace micula;

namespace {

// --- the state: file scope, and deliberately not saved ------------------------------------------
int   page       = 0;              // the row the window opens on
int   paneStyle  = 1;              // PaneStyle: Fixed, Toggle, Peek, Minimal
int   pageSwitch = 1;              // NavigationView::Transition: None, Entrance
int   navRows    = 0;              // extra rows: `nav=24` for a pane taller than the window
bool  paneSlides = true;           // whether the width animates
bool  paneScrim  = false;          // whether an overlay pane dims the page
bool  paneFollow = true;           // whether the arrow keys carry the choice
bool  paneOwn    = true;           // whether the pane draws its own button
bool  pageSurface = true;          // whether the page area is drawn as a surface of its own
float paneOpenW  = 260.0f;
bool  paneOpen   = true;
int   themeMode  = 0;              // ThemeMode: 0 follows Windows, 1 light, 2 dark
int   windowBackdrop = 2;          // DWM_SYSTEMBACKDROP_TYPE: 2 Mica, 3 Acrylic, 4 Mica Alt

// The pages' own state, which is a page's business rather than the pane's -- and so is the same
// whatever the pane is doing.
int   listAt     = 3;              // the line the long list is on, not the text
int   when       = 1;              // when a cleanup starts: at sign-in, daily, weekly
int   hour       = 1;              // the line of the times, not the hour
bool  onBattery  = false;
bool  downloads = true, desktop = false, screenshots = true, hidden = false;

const wchar_t *const kStyles[] = { L"Fixed", L"Toggle", L"Peek", L"Minimal" };
const wchar_t *const kTransitions[] = { L"None", L"Fade", L"Entrance" };
const wchar_t *const kWhen[] = { L"At sign-in", L"Daily", L"Weekly" };
const wchar_t *const kTimes[] = { L"Midnight", L"2:00", L"4:00", L"6:00", L"Noon", L"18:00" };
const wchar_t *const kBackdrops[] = { L"Mica", L"Acrylic", L"Mica Alt" };

struct PageInfo { const wchar_t *title, *detail; };
const PageInfo kPages[] = {
    { L"The pane",    L"Every switch it has, on the page it is looking at" },
    { L"A long page", L"Scroll it, then open and close the pane" },
    { L"Schedule",    L"One of a group, with a heading above it" },
    { L"Folders",     L"...and the other one, so the group has two" },
    { L"Settings",    L"A footer row: the last one sits at the bottom" },
};

// The shell the switches reach. `Apply` is what the pane is told by, so a control changes the pane in
// the same breath as the variable behind it.
NavigationView *nav = nullptr;

void Apply() {
    if (!nav) return;
    nav->pane->style = (PaneStyle)paneStyle;
    nav->pane->animate = paneSlides;
    nav->pane->scrim = paneScrim;
    nav->pane->followsFocus = paneFollow;
    nav->pane->ownToggle = paneOwn;
    nav->pane->openW = paneOpenW;
    nav->transition = (NavigationView::Transition)pageSwitch;
    // The room the pane asks a page for has moved with the style and the width, so the shell is
    // arranged again -- and the page arrives at its new place rather than being built again there.
    nav->InvalidateLayout();
}

std::wstring Dip(float v) {
    wchar_t b[16];
    swprintf(b, 16, L"%d", (int)(v + 0.5f));
    return b;
}

// The state can be asked for on the command line -- `micula-nav.exe style=2 open=0` -- so that a state
// worth looking at does not have to be clicked to. Nothing is remembered either way.
void ReadState(const wchar_t *cmd) {
    if (!cmd) return;
    auto number = [&](const wchar_t *key, int fallback) {
        const wchar_t *at = wcsstr(cmd, key);
        return at ? (int)wcstol(at + wcslen(key), nullptr, 10) : fallback;
    };
    paneStyle  = (std::min)((std::max)(number(L"style=", paneStyle), 0), 3);
    // More rows in the pane than the window can show, which is what the pane's own scrolling is for:
    // `nav=24` puts twenty-four of them above the footer.
    navRows = (std::min)((std::max)(number(L"nav=", navRows), 0), 40);
    page       = (std::min)((std::max)(number(L"page=", page), 0), 4 + 40);
    paneOpen   = number(L"open=", paneOpen ? 1 : 0) != 0;
    paneSlides = number(L"animate=", paneSlides ? 1 : 0) != 0;
    paneScrim  = number(L"scrim=", paneScrim ? 1 : 0) != 0;
    paneFollow = number(L"follow=", paneFollow ? 1 : 0) != 0;
    paneOwn    = number(L"own=", paneOwn ? 1 : 0) != 0;
    // Off is the same shell on a flat background -- which is what a window that paints its own has
    // nothing for a translucent layer to be over.
    pageSurface = number(L"surface=", pageSurface ? 1 : 0) != 0;
    pageSwitch = (std::min)((std::max)(number(L"transition=", pageSwitch), 0), 2);
    paneOpenW  = (float)number(L"width=", (int)paneOpenW);
    windowBackdrop = (std::min)((std::max)(number(L"backdrop=", windowBackdrop), 0), 4);
    // The library's animation switch, which is not the pane's: `animate=` above is the pane's own.
    // -1, or absent, follows Windows; 0 is off; 1 is on.
    switch ((std::min)((std::max)(number(L"anim=", -1), -1), 1)) {
    case 0: Animations(false); break;
    case 1: Animations(true); break;
    default: AnimationsAuto(); break;
    }
    // 0 follows Windows, 1 light, 2 dark -- the library's own three. Set before `Create` the first
    // palette a window builds is already this one; set while one is up, the window has to be told
    // again, which is what the switch on the Settings page does.
    themeMode = (std::min)((std::max)(number(L"theme=", (int)ThemeSetting()), 0), 2);
    Theme((ThemeMode)themeMode);
}

// --- the pages -----------------------------------------------------------------------------------

// One card, which is what a page here is made of: an icon, a title, and the line under it. `sheet`
// rather than `page`, which is the row the window opens on.
Card *Setting(ScrollView *sheet, const wchar_t *icon, const wchar_t *title, const wchar_t *detail) {
    Card *card = sheet->Add(new Card(title, detail));
    card->icon = icon;
    return card;
}

// The page the window opens on: the pane's own switches, which is what makes this window a playground
// rather than a demo.
void PanePage(ScrollView *sheet) {
    sheet->Add(new Heading(kPages[0].title));

    Card *style = Setting(sheet, glyph::kView, L"Style",
                          L"Room for the pane, or the pane over the page");
    style->Set(new Segmented({ kStyles[0], kStyles[1], kStyles[2], kStyles[3] }, paneStyle,
                             [](int i) { paneStyle = i; Apply(); }))
        ->tips = L"Fixed keeps the pane in the layout; Peek slides it over the page";

    // **A menu on the card**, which is a right-click anywhere on it -- on its label as much as on the
    // switch, because a click on a card's label is a click on the card. The rows are the page's state read
    // when the menu opens, which is why the tick is always the truth at the moment it is looked at, and
    // why there is nothing to keep in step as the segmented control beside it changes.
    style->contextMenu = [](micula::Menu &m) {
        for (int i = 0; i < 4; i++)
            m.Check(kStyles[i], paneStyle == i, [i](bool on) {
                if (on) {
                    paneStyle = i;
                    Apply();
                }
            });
        m.Separator();
        m.Item(L"Show the pane", nullptr, L"", [] {
            paneOpen = true;
            Apply();
        });
        m.Item(L"Hide the pane", nullptr, L"", [] {
            paneOpen = false;
            Apply();
        }, paneOpen);
        // The container a submenu will be, drawn with the chevron that says there is more behind it.
        m.Sub(L"Room for the pane", [](micula::Menu &s) {
            s.Item(L"Narrow", nullptr, L"", nullptr, false);
        });
    };

    Setting(sheet, glyph::kMenu, L"Open",
            L"The pane's own state, driven from the page -- the same thing its button does")
        ->Set(new ToggleSwitch(L"", paneOpen, [](bool v) {
            paneOpen = v;
            if (nav) nav->pane->SetOpen(v);
        }))
        ->tips = L"What the pane's own button does, asked for from the page instead";

    Setting(sheet, glyph::kBusy, L"Animate the width",
            L"Off, the pane arrives in one frame -- which is what a pane that is part of the layout "
            L"wants")
        ->Set(new ToggleSwitch(L"", paneSlides, [](bool v) { paneSlides = v; Apply(); }));

    Setting(sheet, glyph::kBrightness, L"Dim the page",
            L"An overlay pane is over something, and a scrim is what says so")
        ->Set(new ToggleSwitch(L"", paneScrim, [](bool v) { paneScrim = v; Apply(); }));

    Setting(sheet, glyph::kInfo, L"Where the page goes",
            L"Read off the pane: one that pushes is part of the layout, one that covers is over it")
        ->value = [] { return nav && nav->pane->Pushes() ? L"pushes the page" : L"over the page"; };

    Setting(sheet, glyph::kBusy, L"How a page arrives",
            L"Fade comes up from nothing, Entrance rises the last 24 DIP into place while it does")
        ->Set(new Segmented({ kTransitions[0], kTransitions[1], kTransitions[2] }, pageSwitch,
                            [](int i) { pageSwitch = i; Apply(); }));

    sheet->Add(new Heading(L"Keys and the button"));

    Setting(sheet, glyph::kRecent, L"The arrow keys carry the choice",
            L"Off, they move the focus ring on their own and Enter is what chooses")
        ->Set(new ToggleSwitch(L"", paneFollow, [](bool v) { paneFollow = v; Apply(); }));

    Setting(sheet, glyph::kHome, L"The pane's own button",
            L"Off, a page puts a button of its own somewhere and calls SetOpen")
        ->Set(new ToggleSwitch(L"", paneOwn, [](bool v) { paneOwn = v; Apply(); }));

    Card *width = Setting(sheet, glyph::kAdd, L"Open width",
                          L"WinUI's OpenPaneLength: how much room the pane takes when the page makes "
                          L"it");
    width->Set(new Slider(paneOpenW, 160.0f, 400.0f, 20.0f, [](float v) {
        paneOpenW = v;
        Apply();
    }))->tips = L"WinUI's OpenPaneLength: the room the page keeps for the pane";
    // The value the card shows is the state, read when it is painted: the slider's own `value` is the
    // number it holds, and a card's is a string to draw beside the control.
    width->value = [] { return Dip(paneOpenW); };

    // **And a menu on the page itself**, for the room around the cards: the walk from whatever the hand is
    // over ends here, so a page that answers for itself answers for all of it.
    sheet->contextMenu = [](micula::Menu &m) {
        m.Item(L"Open the pane", nullptr, L"", [] {
            paneOpen = true;
            Apply();
        }, !paneOpen);
        m.Item(L"Close the pane", nullptr, L"", [] {
            paneOpen = false;
            Apply();
        }, paneOpen);
        m.Separator();
        m.Item(L"Nothing behind this one", nullptr, L"", nullptr, false);
    };
}

// A card whose control is a switch, with On or Off beside it: two lines, five times over, and the
// state is what the card reads when it is painted rather than a copy of it.
void ToggleCard(ScrollView *sheet, const wchar_t *icon, const wchar_t *title, const wchar_t *detail,
                bool *state) {
    Card *card = Setting(sheet, icon, title, detail);
    card->Set(new ToggleSwitch(L"", *state, [state](bool v) { *state = v; }));
    card->value = [state] { return *state ? L"On" : L"Off"; };
}

// A page with forty lines on it. What it is for is the two scrolls side by side -- the pane's and the
// page's -- so the list is taller than the window on purpose, and past either end of it the wheel
// belongs to the page. See `DropDown` for the wheel and the search that go with a closed one.
void LongListPage(ScrollView *sheet) {
    sheet->Add(new Heading(kPages[1].title));
    sheet->Add(new Label(kPages[1].detail, TextRole::Caption))->secondary = true;

    std::vector<std::wstring> lines;
    for (int i = 1; i <= 40; i++) lines.push_back(L"Row " + std::to_wstring(i));
    Card *list = Setting(sheet, glyph::kMenu, L"A long list",
                         L"Forty lines: the wheel over the list is the list's, and past either end "
                         L"of it is the page's");
    list->Set(new DropDown(lines, listAt, [](int i) { listAt = i; }));
    list->value = [] { return L"Row " + std::to_wstring(listAt + 1); };

    for (int i = 1; i <= 14; i++) {
        Setting(sheet, glyph::kView, (L"Card " + std::to_wstring(i)).c_str(),
                L"Enough of these and the page scrolls, which is the pane's own wheel and the "
                L"page's side by side");
    }
}

// A page of real controls rather than a list of cards, which is what a navigation window has in it:
// the pane says which page, and the page says what is on it. A list of times of day is a ring -- the
// step past 18:00 is after midnight -- which is `DropDown::wrapAround` and the wheel over the list.
void SchedulePage(ScrollView *sheet) {
    sheet->Add(new Heading(kPages[2].title));
    sheet->Add(new Label(kPages[2].detail, TextRole::Caption))->secondary = true;

    Setting(sheet, glyph::kCalendar, L"Run", L"When a cleanup starts")
        ->Set(new Segmented({ kWhen[0], kWhen[1], kWhen[2] }, when, [](int i) { when = i; }));

    Card *time = Setting(sheet, glyph::kRecent, L"Time of day", L"For daily and weekly cleanups");
    DropDown *day = new DropDown({ kTimes[0], kTimes[1], kTimes[2], kTimes[3], kTimes[4],
                                   kTimes[5] }, hour, [](int i) { hour = i; });
    day->wrapAround = true;
    time->Set(day);

    ToggleCard(sheet, glyph::kBolt, L"Run on battery power", L"Off by default to save battery",
               &onBattery);
}

// And the same shape four times, which is what most of a settings page is.
void FoldersPage(ScrollView *sheet) {
    sheet->Add(new Heading(kPages[3].title));
    sheet->Add(new Label(kPages[3].detail, TextRole::Caption))->secondary = true;

    ToggleCard(sheet, glyph::kFolder, L"Downloads", L"Files saved by browsers and other apps",
               &downloads);
    ToggleCard(sheet, glyph::kFolder, L"Desktop", L"Loose files on the desktop", &desktop);
    ToggleCard(sheet, glyph::kCamera, L"Screenshots", L"Pictures\\Screenshots", &screenshots);
    ToggleCard(sheet, glyph::kView, L"Skip hidden files", L"Dot files and files with the hidden "
                                                                   L"attribute",
               &hidden);
}

// The footer's page: a footer row is a row like any other, and the page under it is a page like any
// other -- which is what makes this the place for the window's own theme. Switching it is two calls
// rather than one: the mode is the library's, and the palette a window paints from was built when
// the window was made, so the window it is on has to be told to build it again.
void SettingsPage(ScrollView *sheet) {
    sheet->Add(new Heading(kPages[4].title));
    sheet->Add(new Label(kPages[4].detail, TextRole::Caption))->secondary = true;
    Setting(sheet, glyph::kBrightness, L"Theme",
            L"Follow Windows or pick one: every colour on every page comes from it")
        ->Set(new Segmented({ L"System", L"Light", L"Dark" }, themeMode, [sheet](int i) {
            themeMode = i;
            Theme((ThemeMode)i);
            if (Surface *w = sheet->surface()) w->ReloadTheme();
        }));

    // The material is read when the window is made, like the theme, so asking for another one is the
    // same two calls: the state, and the window that has to act on it.
    Setting(sheet, glyph::kColor, L"Backdrop",
            L"What the page area is a layer over: the material the window asks DWM for")
        ->Set(new Segmented({ kBackdrops[0], kBackdrops[1], kBackdrops[2] }, windowBackdrop - 2,
                            [sheet](int i) {
            windowBackdrop = 2 + i;
            Window *w = sheet->surface() ? sheet->surface()->AsWindow() : nullptr;
            if (w) {
                w->backdrop = (DWORD)windowBackdrop;
                w->ApplyThemeToFrame();
            }
        }));
}

// The tree: the shell, the rows, and the pages. Built into whatever root it is given -- a window's
// client area, or a plain View in `--dump`, which is what makes the two the same code.
NavigationView *BuildTree(Widget *root) {
    // The root is a stack with no margin of its own, so its one child is the whole client area.
    auto *whole = new StackLayout();
    whole->padX = 0.0f;
    root->SetLayout(whole);

    nav = root->Add(new NavigationView());
    nav->pageSurface = pageSurface;
    Apply();
    nav->pane->open = paneOpen;

    PanePage(nav->AddPage({ glyph::kSettings, kPages[0].title }, new ScrollView()));

    LongListPage(nav->AddPage({ glyph::kRecent, kPages[1].title }, new ScrollView()));

    // A group: a heading in the pane, and the two rows under it.
    nav->AddRow(NavItem::Heading(L"Group"));
    SchedulePage(nav->AddPage({ glyph::kCalendar, kPages[2].title }, new ScrollView()));
    FoldersPage(nav->AddPage({ glyph::kHome, kPages[3].title }, new ScrollView()));

    // `nav=` long, and each one a page of its own: a row that leads nowhere is a row with nothing to
    // show, so what these are for is the pane having more rows than the window has room for.
    if (navRows > 0) {
        nav->AddRow(NavItem::Heading(L"More rows"));
        for (int i = 1; i <= navRows; i++) {
            const std::wstring name = L"Row " + std::to_wstring(i);
            ScrollView *sheet = nav->AddPage({ glyph::kHome, name }, new ScrollView());
            sheet->Add(new Heading(name));
            Setting(sheet, glyph::kHome, L"One of the extra rows",
                    L"`nav=` puts these here: the pane scrolls, and the bar down its edge is what "
                    L"says so");
        }
    }

    // The footer, where Settings lives -- the last row whatever order the pages were added in.
    SettingsPage(nav->AddFooter({ glyph::kSettings, kPages[4].title }, new ScrollView()));

    // The row the window opens on, taken through the same path a click takes: the pane is told, and
    // the pages follow it.
    nav->Select(page);
    return nav;
}

// --- the window -----------------------------------------------------------------------------------
struct NavWindow : Window {
    NavigationView *shell = nullptr;
    // Every control on these pages with a `tips` says it when the pointer rests on it. One line is the
    // whole of what a window has to do about it -- see tip.h.
    Tips tips{ *this };
    // And one line for the menus: a right-click on a control with a `contextMenu`, the Menu key or
    // Shift+F10 under the focus, and Esc or a click outside to put it away. See menu.h.
    Menus menus{ *this };

    const wchar_t *ClassName() const override { return L"MiculaNav"; }
    const wchar_t *Title() const override { return L"Micula - navigation"; }
    void MinSize(int *w, int *h) const override { *w = 720; *h = 480; }

    // The tree, and the box a window of this size would give it. The shell comes back because that is
    // what `--dump` prints.
    NavigationView *Build(float w, float h) {
        shell = BuildTree(EnsureContent());
        content->rect = { 0.0f, kCaptionH, w, h };
        return shell;
    }
};

// The window the example opens at, and the box `--dump` lays the tree out in.
constexpr float kWinW = 1000.0f;
constexpr float kWinH = 640.0f;

// The fonts, which is the one thing a layout needs that a window was providing.
int WithFonts(const std::function<void(Fonts &)> &body) {
    IDWriteFactory *dw = nullptr;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown **>(&dw))) || !dw) {
        std::fwprintf(stderr, L"DirectWrite is not available.\n");
        return 1;
    }
    Fonts fonts;
    if (!fonts.Create(dw)) {
        std::fwprintf(stderr, L"the font formats could not be made.\n");
        fonts.Release();
        dw->Release();
        return 1;
    }
    body(fonts);
    fonts.Release();
    dw->Release();
    return 0;
}

// One widget a line, with where it came out. A pane's own geometry is what it works out from its
// rectangle, and its rows are not widgets at all -- so the interesting lines here are the pane's, the
// page area's and the bar's, and the pages under them.
//
// Every child is printed, visible or not: the pages of a nav window are all in the tree at once, and
// the one that is showing is marked.
void Print(const Widget *w, int depth, float ox, float oy) {
    const D2D1_RECT_F &r = w->rect;
    std::wprintf(L"%*s%s %7.1f %7.1f %7.1f %7.1f\n", depth * 2, L"", w->visible ? L"   " : L"hid",
                 ox + r.left, oy + r.top, ox + r.right, oy + r.bottom);
    for (const auto &child : w->children) Print(child.get(), depth + 1, ox + r.left, oy + r.top);
}

// The tree, arranged into a window's worth of space and printed, with no window anywhere.
void Dump(Fonts &fonts) {
    NavWindow win;
    NavigationView *shell = win.Build(kWinW, kWinH);
    ArrangeSubtree(win.content.get(), fonts);
    std::wprintf(L"window   %.0f x %.0f, caption %.0f\n", kWinW, kWinH, kCaptionH);
    std::wprintf(L"shell    pane %.1f x %.1f, asks for %.1f, %ls\n", Width(shell->pane->rect),
                 Height(shell->pane->rect), shell->pane->Reserved(),
                 shell->pane->Pushes() ? L"pushes the page" : L"over the page");
    std::wprintf(L"page     %.1f,%.1f %.1f x %.1f\n", shell->content->rect.left,
                 shell->content->rect.top, Width(shell->content->rect),
                 Height(shell->content->rect));
    std::wprintf(L"rows     %d in the pane, %d of them can be chosen, showing %d\n",
                 (int)shell->pane->rows.size(), shell->pane->Selectable(), shell->Selected());
    std::wprintf(L"bar      %.1f,%.1f %.1f x %.1f  %ls\n", shell->pane->scrollBar->rect.left,
                 shell->pane->scrollBar->rect.top, Width(shell->pane->scrollBar->rect),
                 Height(shell->pane->scrollBar->rect),
                 shell->pane->scrollBar->visible ? L"showing" : L"put away");
    Print(win.content.get(), 0, 0.0f, 0.0f);
}

}  // namespace

int wmain(int argc, wchar_t **argv) {
    bool dump = false, overlay = false;
    for (int i = 1; i < argc; i++) {
        if (std::wcscmp(argv[i], L"--dump") == 0) dump = true;
        if (std::wcscmp(argv[i], L"--keys") == 0) overlay = true;
        if (std::wcscmp(argv[i], L"--layout") == 0) overlay = true;
    }
    ReadState(GetCommandLineW());
    // The layout overlay: off unless this run asked for it. It exists at all only in a build that has
    // it (`MICULA_DEBUG_LAYOUT`), and a box around every widget is what watching an animation has to
    // look past.
#if MICULA_DEBUG_LAYOUT
    debug::layout = overlay;
#else
    (void)overlay;
#endif
    if (dump) {
        // Wide rather than in the console's code page: the labels are Chinese, and a stream left alone
        // narrows every one of them through the CRT's default encoding on the way out.
        _setmode(_fileno(stdout), _O_U16TEXT);
        return WithFonts(Dump);
    }

    // A console program so that --dump has somewhere to print. Launched from Explorer that console is
    // one nobody asked for, so it is dropped when it is ours and left alone when it is a terminal's.
    DWORD owners = 0;
    if (GetConsoleProcessList(&owners, 1) == 1) FreeConsole();

    // Neither EnablePerMonitorDpi nor CoInitializeEx: Window::Create does both, and only when nobody
    // else has said anything. See the top of window.h.
    int code = 1;
    {
        NavWindow win;
        win.backdrop = (DWORD)windowBackdrop;
        win.Build(kWinW, kWinH);
        if (win.Create((int)kWinW, (int)kWinH, true, nullptr)) code = win.Run();
    }
    return code;
}

