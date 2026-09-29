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

const wchar_t *const kStyles[] = { L"Fixed", L"Toggle", L"Peek", L"Minimal" };
const wchar_t *const kTransitions[] = { L"None", L"Fade", L"Entrance" };

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

    Setting(sheet, glyph::kView, L"Style",
            L"Room for the pane, or the pane over the page")
        ->Set(new Segmented({ kStyles[0], kStyles[1], kStyles[2], kStyles[3] }, paneStyle,
                            [](int i) { paneStyle = i; Apply(); }));

    Setting(sheet, glyph::kMenu, L"Open",
            L"The pane's own state, driven from the page -- the same thing its button does")
        ->Set(new ToggleSwitch(L"", paneOpen, [](bool v) {
            paneOpen = v;
            if (nav) nav->pane->SetOpen(v);
        }));

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
    }));
    // The value the card shows is the state, read when it is painted: the slider's own `value` is the
    // number it holds, and a card's is a string to draw beside the control.
    width->value = [] { return Dip(paneOpenW); };
}

// A page of cards: what a page in a nav window is, and what the pane's scrolling and the page's have
// in common -- both are the same gesture, one over the other.
void FillerPage(ScrollView *sheet, const PageInfo &info, int cards) {
    sheet->Add(new Heading(info.title));
    sheet->Add(new Label(info.detail, TextRole::Caption))->secondary = true;
    for (int i = 1; i <= cards; i++) {
        Setting(sheet, glyph::kView, (L"Card " + std::to_wstring(i)).c_str(),
                L"Enough of these and the page scrolls, which is the pane's own wheel and the "
                L"page's side by side");
    }
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
            if (Window *w = sheet->window()) w->ReloadTheme();
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

    FillerPage(nav->AddPage({ glyph::kRecent, kPages[1].title }, new ScrollView()), kPages[1], 14);

    // A group: a heading in the pane, and the two rows under it.
    nav->AddRow(NavItem::Heading(L"Group"));
    FillerPage(nav->AddPage({ glyph::kCalendar, kPages[2].title }, new ScrollView()), kPages[2], 3);
    FillerPage(nav->AddPage({ glyph::kHome, kPages[3].title }, new ScrollView()), kPages[3], 2);

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

