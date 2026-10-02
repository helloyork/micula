// Settings window for a made-up folder cleanup tool, laid out like Windows 11's Settings app:
// navigation on the left, cards on the right. It is the window in the README screenshot. Browse opens
// the system folder picker; nothing is moved or deleted.
//
// The shell is `NavigationView` -- a row and the page it shows, added in one call -- so what is left
// for this file is the state, four pages, and the picker. `--dump` prints the rectangles the shell
// came out as, with no window anywhere: the same mode the other two examples have.

#include <micula/micula.h>

#include <shobjidl.h>

#include <cstdio>
#include <cwchar>
#include <fcntl.h>
#include <io.h>
#include <string>

#pragma comment(lib, "shell32.lib")

using namespace micula;

namespace {

// --- the state: file scope, and deliberately not saved ------------------------------------------
int   page      = 0;               // the row the window opens on
bool  autoClean = true;
int   olderThan = 3;               // which line of the list under it, not days
float freeGb    = 20.0f;
std::wstring archive = L"D:\\Archive\\Downloads";

int   when      = 1;               // when a cleanup starts: at sign-in, daily, weekly
int   hour      = 1;
bool  onBattery = false;

bool  downloads = true, desktop = false, screenshots = true;

const wchar_t *const kOlder[] = { L"1 day", L"1 week", L"2 weeks", L"30 days", L"60 days",
                                  L"90 days" };
const wchar_t *const kTimes[] = { L"Midnight", L"2:00", L"4:00", L"6:00", L"Noon", L"18:00" };
const wchar_t *const kWhen[]  = { L"At sign-in", L"Daily", L"Weekly" };

// The four rows, as an icon and a name each: which is all a row needs to be one. A row and the page it
// shows are one thing said twice, so the page is added with its row rather than named again here.
const NavItem kNav[] = {
    { glyph::kSettings, L"General" },
    { glyph::kCalendar, L"Schedule" },
    { glyph::kFolder,   L"Folders" },
    { glyph::kInfo,     L"About" },
};

std::wstring Gb(float v) {
    wchar_t b[16];
    swprintf(b, 16, L"%d GB", (int)(v + 0.5f));
    return b;
}

// The system folder picker, opened at `start` if it exists. Empty if cancelled.
std::wstring PickFolder(HWND owner, const std::wstring &start) {
    std::wstring out;
    IFileOpenDialog *dlg = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))))
        return out;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    IShellItem *at = nullptr;
    if (!start.empty() &&
        SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&at)))) {
        dlg->SetFolder(at);
        at->Release();
    }
    IShellItem *item = nullptr;
    if (SUCCEEDED(dlg->Show(owner)) && SUCCEEDED(dlg->GetResult(&item))) {
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            out = path;
            CoTaskMemFree(path);
        }
        item->Release();
    }
    dlg->Release();
    return out;
}

// --- the pages -----------------------------------------------------------------------------------

// One card: an icon, a title, the line under it, and -- added by the page -- the control on the right.
// `sheet` rather than `page`, which is the row the window opens on.
Card *Setting(ScrollView *sheet, const wchar_t *icon, const wchar_t *title, const wchar_t *detail) {
    Card *card = sheet->Add(new Card(title, detail));
    card->icon = icon;
    return card;
}

// A card whose control is a switch, and whose own value says whether it is on. Two lines, four times
// over: the state is what the card reads rather than a copy of it, so a switch that changes something
// else on the page needs no other wiring.
void ToggleCard(ScrollView *sheet, const wchar_t *icon, const wchar_t *title, const wchar_t *detail,
                bool *state) {
    Card *card = Setting(sheet, icon, title, detail);
    card->Set(new ToggleSwitch(L"", *state, [state](bool v) { *state = v; }));
    card->value = [state] { return *state ? L"On" : L"Off"; };
}

void GeneralPage(ScrollView *sheet) {
    ToggleCard(sheet, glyph::kDelete, L"Clean up automatically",
               L"Move files you haven't opened in a while", &autoClean);

    Card *older = Setting(sheet, glyph::kRecent, L"Move files older than",
                          L"Counted from when a file was last opened");
    older->Set(new DropDown({ kOlder[0], kOlder[1], kOlder[2], kOlder[3], kOlder[4], kOlder[5] },
                            olderThan, [](int i) { olderThan = i; }));

    Card *free = Setting(sheet, glyph::kDrive, L"Keep free space above",
                         L"Start early when space runs low");
    free->Set(new Slider(freeGb, 5.0f, 100.0f, 5.0f, [](float v) { freeGb = v; }));
    // The value beside a slider is the state read when the card is painted, in the page's own words:
    // the slider holds a number, and a card's value is the string that goes next to it.
    free->value = [] { return Gb(freeGb); };

    // The one card with two controls on it: the path, which is a field, and the button that replaces
    // it. The field takes what the button leaves, which is what a `RowLayout` is for -- a child that
    // asked to fill takes what the others left.
    Card *where = Setting(sheet, glyph::kFolder, L"Archive folder", L"Where moved files go");
    auto *line = new View();
    line->SetLayout(new RowLayout());
    TextBox *path = line->Add(new TextBox());
    path->SetText(archive);
    path->pathField = true;
    path->onChange = [](const std::wstring &s) { archive = s; };
    line->Add(new Button(L"Browse", ButtonStyle::Standard, [path] {
        const std::wstring picked = PickFolder(nullptr, archive);
        if (picked.empty()) return;
        archive = picked;
        // The field is the state's to write and not the other way round, which is why the picker's
        // answer goes through it rather than around it.
        path->SetText(picked);
    }))->tips = L"Pick the folder with the shell's own dialog";
    where->Set(line);

    sheet->Add(new Heading(L"Appearance"));
    Card *theme = Setting(sheet, glyph::kColor, L"Theme", L"Follow Windows or pick one");
    theme->Set(new Segmented({ L"System", L"Light", L"Dark" }, (int)ThemeSetting(), [sheet](int i) {
        Theme((ThemeMode)i);
        // The palette a window paints from was built when it was made, so the mode alone changes
        // nothing until the window is told to build it again.
        if (Surface *w = sheet->surface()) w->ReloadTheme();
    }));

    // The footer: what happened last, and the two things a person can do about it.
    sheet->Add(new Label(L"Last cleanup 2 hours ago, 1.4 GB moved", TextRole::Caption))->secondary =
        true;
    auto *actions = new View();
    actions->SetLayout(new RowLayout());
    actions->Add(new Button(L"Clean up now", ButtonStyle::Accent, [] {}))
        ->tips = L"Move everything the rules above match, and say so afterwards";
    actions->Add(new Button(L"View log", ButtonStyle::Standard, [] {}))->tips =
        L"What the last cleanup did, file by file";
    sheet->Add(actions);
}

void SchedulePage(ScrollView *sheet) {
    Card *run = Setting(sheet, glyph::kCalendar, L"Run", L"When a cleanup starts");
    run->Set(new Segmented({ kWhen[0], kWhen[1], kWhen[2] }, when, [](int i) { when = i; }));

    Card *time = Setting(sheet, glyph::kRecent, L"Time of day", L"For daily and weekly cleanups");
    DropDown *day = new DropDown({ kTimes[0], kTimes[1], kTimes[2], kTimes[3], kTimes[4],
                                   kTimes[5] }, hour, [](int i) { hour = i; });
    // Times of day are a ring, and the wheel over the list says so: the step past 18:00 is after
    // midnight. See `DropDown::wrapAround`.
    day->wrapAround = true;
    time->Set(day);

    ToggleCard(sheet, glyph::kBolt, L"Run on battery power", L"Off by default to save battery",
               &onBattery);
}

void FoldersPage(ScrollView *sheet) {
    ToggleCard(sheet, glyph::kFolder, L"Downloads", L"Files saved by browsers and other apps",
               &downloads);
    ToggleCard(sheet, glyph::kFolder, L"Desktop", L"Loose files on the desktop", &desktop);
    ToggleCard(sheet, glyph::kCamera, L"Screenshots", L"Pictures\\Screenshots", &screenshots);

    Card *more = Setting(sheet, glyph::kAdd, L"More folders",
                         L"Any folder can be cleaned up the same way");
    more->Set(new Button(L"Add folder", ButtonStyle::Standard, [] {}));
}

void AboutPage(ScrollView *sheet) {
    Setting(sheet, glyph::kInfo, L"Folder Cleanup 1.0",
            L"An example program for Micula. It does not move or delete anything.");
    Setting(sheet, glyph::kSettings, L"Built with Micula " MICULA_VERSION_STRING,
            L"Header-only Fluent controls for Win32");
}

// The tree: the shell, the four rows, and the pages under them. Built into whatever root it is given
// -- a window's client area, or a plain View in `--dump`, which is what makes the two the same code.
NavigationView *BuildTree(Widget *root) {
    // The root is a stack with no margin of its own, so its one child is the whole client area.
    auto *whole = new StackLayout();
    whole->padX = 0.0f;
    root->SetLayout(whole);

    NavigationView *nav = root->Add(new NavigationView());
    // A rail that is always out: this window has no room for the page to be wider and nothing that
    // put the pane away. Its own button goes with it -- a button whose only press closes a pane that
    // cannot close is a control that does nothing. See `SideNav` for the styles.
    nav->pane->style = PaneStyle::Fixed;
    nav->pane->ownToggle = false;
    nav->pane->openW = 184.0f;

    // Every page starts with its own name: the row says which list you are in, the heading says where
    // in it, which is what Windows 11's own settings pages do. One place rather than four, because it
    // is the row's own label either way.
    ScrollView *sheets[4] = {
        nav->AddPage(kNav[0], new ScrollView()),
        nav->AddPage(kNav[1], new ScrollView()),
        nav->AddPage(kNav[2], new ScrollView()),
        nav->AddPage(kNav[3], new ScrollView()),
    };
    for (int i = 0; i < 4; i++) sheets[i]->Add(new PageTitle(kNav[i].label));

    GeneralPage(sheets[0]);
    SchedulePage(sheets[1]);
    FoldersPage(sheets[2]);
    AboutPage(sheets[3]);

    // The row the window opens on, taken through the same path a click takes.
    nav->Select(page);
    return nav;
}

// --- the state on the command line ---------------------------------------------------------------
// `micula-settings page=2 theme=2`, so that a page worth looking at does not have to be clicked to.
// Nothing is remembered either way.
void ReadState(const wchar_t *cmd) {
    if (!cmd) return;
    auto number = [&](const wchar_t *key, int fallback) {
        const wchar_t *at = wcsstr(cmd, key);
        return at ? (int)wcstol(at + wcslen(key), nullptr, 10) : fallback;
    };
    page = (std::min)((std::max)(number(L"page=", page), 0), 3);
    // 0 follows Windows, 1 light, 2 dark. Set before `Create` the first palette the window builds is
    // already this one; set while one is up, the window has to be told again -- which is what the
    // switch on the General page does.
    Theme((ThemeMode)(std::min)((std::max)(number(L"theme=", (int)ThemeSetting()), 0), 2));
}

// --- the window -----------------------------------------------------------------------------------
struct SettingsWindow : Window {
    NavigationView *shell = nullptr;
    // Every control on these pages with a `tips` says it when the pointer rests on it; see tip.h.
    Tips tips{ *this };

    const wchar_t *ClassName() const override { return L"MiculaSettings"; }
    const wchar_t *Title() const override { return L"Folder Cleanup"; }
    void MinSize(int *w, int *h) const override { *w = 680; *h = 600; }

    // The tree, and the box a window of this size would give it.
    NavigationView *Build(float w, float h) {
        shell = BuildTree(EnsureContent());
        content->rect = { 0.0f, kCaptionH, w, h };
        return shell;
    }
};

// The window the example opens at, and the box `--dump` lays the tree out in. Wider than the old hand
// placed version was: a card gives its control the whole slot beside the text, and a control that asks
// for the room it is in takes all of it -- so the text needs the room the old fixed-width controls left
// behind them.
constexpr float kWinW = 780.0f;
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

// One widget a line, with where it came out. A page's own geometry is what its layout works out from
// the room it was given, so what these lines are for is telling a page that came out wrong from a
// layout that worked it out wrong.
void Print(const Widget *w, int depth, float ox, float oy) {
    const D2D1_RECT_F &r = w->rect;
    std::wprintf(L"%*s%s %7.1f %7.1f %7.1f %7.1f\n", depth * 2, L"", w->visible ? L"   " : L"hid",
                 ox + r.left, oy + r.top, ox + r.right, oy + r.bottom);
    for (const auto &child : w->children) Print(child.get(), depth + 1, ox + r.left, oy + r.top);
}

// The tree, arranged into a window's worth of space and printed, with no window anywhere.
void Dump(Fonts &fonts) {
    SettingsWindow win;
    NavigationView *shell = win.Build(kWinW, kWinH);
    ArrangeSubtree(win.content.get(), fonts);
    std::wprintf(L"window   %.0f x %.0f, caption %.0f\n", kWinW, kWinH, kCaptionH);
    std::wprintf(L"shell    pane %.1f wide, the row chosen is %d of %d\n",
                 shell->pane->Reserved(), shell->Selected(), shell->pane->Selectable());
    std::wprintf(L"page     %.1f,%.1f %.1f x %.1f\n", shell->content->rect.left,
                 shell->content->rect.top, Width(shell->content->rect),
                 Height(shell->content->rect));
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
    // The boxes a layout worked out, which a debug build draws over every widget: off unless this run
    // asked for it, because a page that is being looked at is not a page that is being measured.
#if MICULA_DEBUG_LAYOUT
    debug::layout = overlay;
#else
    (void)overlay;
#endif
    if (dump) {
        // Wide rather than in the console's code page, like the other two examples.
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
        SettingsWindow win;
        win.Build(kWinW, kWinH);
        if (win.Create((int)kWinW, (int)kWinH, true, nullptr)) code = win.Run();
    }
    return code;
}
