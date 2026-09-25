// Settings window for a made-up folder cleanup tool, laid out like Windows 11's
// Settings app: navigation on the left, cards on the right. It is the window in the
// README screenshot. Browse opens the system folder picker; nothing is moved or deleted.

#include <micula/micula.h>

#include <shobjidl.h>

#include <cwchar>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")

using namespace micula;

namespace {

constexpr float kNavW    = 184.0f;
// The navigation rows: the first one's top, one row's height, and the distance between
// two of them. Named because the accent bar is drawn from them as well as the buttons.
constexpr float kNavTop   = kCaptionH + 12.0f;
constexpr float kNavRowH  = 36.0f;
constexpr float kNavPitch = 40.0f;
constexpr float kCardH   = 68.0f;
constexpr float kCardGap = 4.0f;
constexpr float kInset   = 18.0f;   // card padding, left and right

// Segoe Fluent Icons code points that theme.h does not name.
// The icons this window draws, from the library's own list: nothing here is a code point of its
// own -- see <micula/glyphs.h>, which is where a page's icons belong.
constexpr const wchar_t *kIconDelete   = glyph::kDelete;
constexpr const wchar_t *kIconRecent   = glyph::kRecent;
constexpr const wchar_t *kIconDrive    = glyph::kDrive;
constexpr const wchar_t *kIconColor    = glyph::kColor;
constexpr const wchar_t *kIconCalendar = glyph::kCalendar;
constexpr const wchar_t *kIconCamera   = glyph::kCamera;
constexpr const wchar_t *kIconAdd      = glyph::kAdd;
constexpr const wchar_t *kIconBattery  = glyph::kBolt;

struct NavItem { const wchar_t *icon, *label; };
const NavItem kNav[] = {
    { glyph::kSettings, L"General" },
    { kIconCalendar,    L"Schedule" },
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

}  // namespace

struct Settings : Window {
    int page = 0;

    bool  autoClean = true;
    int   olderThan = 3;
    float freeGb = 20.0f;
    std::wstring archive = L"D:\\Archive\\Downloads";
    int   theme = 0;                 // System, Light, Dark

    int   when = 1;
    int   hour = 1;
    bool  onBattery = false;

    bool  downloads = true, desktop = false, screenshots = true;

    // What PaintPage draws, worked out by Layout.
    struct Card {
        D2D1_RECT_F r;
        const wchar_t *icon;
        std::wstring title, detail;
        float slotLeft;              // the control's left edge; text stops short of it
        std::wstring aside;          // a value shown beside the control
        const bool *onOff;           // or, for a switch, "On" / "Off"
    };
    std::vector<Card> cards;
    std::vector<std::pair<float, std::wstring>> headings;
    size_t freeCard = 0;
    float footerY = -1.0f;

    // The selection indicator, which moves between rows rather than appearing under the new
    // one: motion::Span is the same thing the library's segmented control draws its block
    // with, and it is where the two edges and their timings are described.
    motion::Span nav;
    bool navSet = false;

    // The move is the only thing this page animates on its own account; the controls' own
    // fades are the window's business.
    bool AnimationWanted() const override { return nav.Wants((float)page); }
    void OnTick(float dt) override {
        nav.To((float)page);
        nav.Step(dt);
    }

    const wchar_t *ClassName() const override { return L"MiculaSettings"; }
    const wchar_t *Title() const override { return L"Folder Cleanup"; }
    void MinSize(int *w, int *h) const override { *w = 680; *h = 600; }

    void ApplyTheme() {
        const bool dark = theme == 0 ? SystemUsesDarkTheme() : theme == 2;
        pal = MakePalette(dark);
        ApplyThemeToFrame();
        Invalidate();
    }

    void Layout() override;
    void PaintPage(const Painter &p) override;
};

void Settings::Layout() {
    ClearWidgets();
    cards.clear();
    headings.clear();
    footerY = -1.0f;
    const float w = ClientW();
    Painter measure;
    measure.font = &fonts;

    for (int i = 0; i < 4; i++) {
        Button *b = Add(new Button(kNav[i].label, ButtonStyle::Subtle, [this, i] {
            page = i;
            Layout();
            Invalidate();
        }));
        b->glyph = kNav[i].icon;
        b->leftAlign = true;
        b->rect = { 8, kNavTop + kNavPitch * i,
                    kNavW - 8, kNavTop + kNavPitch * i + kNavRowH };
        // The first layout has nothing to animate from: the indicator begins on the row the
        // page begins on.
        if (!navSet) { nav.Set((float)i); navSet = true; }
    }

    const float left = kNavW + 12, right = w - 24;
    float y = kCaptionH + 60;
    auto heading = [&](const wchar_t *text) {
        y += 16;
        headings.push_back({ y, text });
        y += 32;
    };
    // Adds a card and returns the rectangle for its control, on the right of the first row.
    auto card = [&](const wchar_t *icon, std::wstring title, std::wstring detail,
                    float controlW, float height = kCardH) {
        const D2D1_RECT_F r = { left, y, right, y + height };
        const float slot = right - kInset - controlW;
        cards.push_back({ r, icon, std::move(title), std::move(detail), slot, L"", nullptr });
        y += height + kCardGap;
        const float cy = r.top + kCardH / 2;
        return D2D1_RECT_F{ slot, cy - metric::kControlH / 2, right - kInset,
                            cy + metric::kControlH / 2 };
    };
    auto toggle = [&](const wchar_t *icon, const wchar_t *title, const wchar_t *detail,
                      bool *state) {
        Add(new ToggleSwitch(L"", *state, [this, state](bool on) {
            *state = on;
            Invalidate();
        }))->rect = card(icon, title, detail, 40);
        cards.back().onOff = state;
    };

    switch (page) {
    case 0: {
        toggle(kIconDelete, L"Clean up automatically",
               L"Move files you haven't opened in a while", &autoClean);
        Add(new DropDown({ L"1 day", L"1 week", L"2 weeks", L"30 days", L"60 days", L"90 days" },
                         olderThan, [this](int i) { olderThan = i; }))
            ->rect = card(kIconRecent, L"Move files older than",
                          L"Counted from when a file was last opened", 140);
        Add(new Slider(freeGb, 5.0f, 100.0f, 5.0f, [this](float v) {
            freeGb = v;
            if (freeCard < cards.size()) cards[freeCard].aside = Gb(v);
        }))->rect = card(kIconDrive, L"Keep free space above",
                         L"Start early when space runs low", 150);
        freeCard = cards.size() - 1;
        cards.back().aside = Gb(freeGb);

        // Two rows: the path field and its Browse button sit under the title, as a pair.
        card(glyph::kFolder, L"Archive folder", L"Where moved files go", 0, 112);
        {
            const D2D1_RECT_F r = cards.back().r;
            const float row = r.top + kCardH - 4;
            Button *browse = Add(new Button(L"Browse", ButtonStyle::Standard, [this] {
                const std::wstring picked = PickFolder(hwnd, archive);
                if (picked.empty()) return;
                archive = picked;
                Layout();   // the field is rebuilt from `archive`
                Invalidate();
            }));
            const float bw = browse->PreferredWidth(measure);
            browse->rect = { r.right - kInset - bw, row, r.right - kInset, row + metric::kControlH };
            TextBox *t = Add(new TextBox());
            t->SetText(archive);
            t->pathField = true;
            t->onChange = [this](const std::wstring &s) { archive = s; };
            t->rect = { r.left + 50, row, browse->rect.left - 8, row + metric::kControlH };
        }

        heading(L"Appearance");
        Add(new Segmented({ L"System", L"Light", L"Dark" }, theme, [this](int i) {
            theme = i;
            ApplyTheme();
        }))->rect = card(kIconColor, L"Theme", L"Follow Windows or pick one", 210);

        y += 8;
        footerY = y;
        Button *run = Add(new Button(L"Clean up now", ButtonStyle::Accent, [] {}));
        Button *log = Add(new Button(L"View log", ButtonStyle::Standard, [] {}));
        const float rw = run->PreferredWidth(measure), lw = log->PreferredWidth(measure);
        run->rect = { right - rw, y, right, y + metric::kControlH };
        log->rect = { right - rw - 8 - lw, y, right - rw - 8, y + metric::kControlH };
        break;
    }
    case 1: {
        Add(new Segmented({ L"At sign-in", L"Daily", L"Weekly" }, when,
                          [this](int i) { when = i; }))
            ->rect = card(kIconCalendar, L"Run", L"When a cleanup starts", 250);
        DropDown *day =
            Add(new DropDown({ L"Midnight", L"2:00", L"4:00", L"6:00", L"Noon", L"18:00" }, hour,
                             [this](int i) { hour = i; }));
        // Times of day are a ring, and the wheel over the list says so: the step past 18:00
        // is after midnight. See DropDown::wrapAround.
        day->wrapAround = true;
        day->rect = card(kIconRecent, L"Time of day", L"For daily and weekly cleanups", 140);
        toggle(kIconBattery, L"Run on battery power", L"Off by default to save battery",
               &onBattery);
        break;
    }
    case 2: {
        toggle(glyph::kFolder, L"Downloads", L"Files saved by browsers and other apps",
               &downloads);
        toggle(glyph::kFolder, L"Desktop", L"Loose files on the desktop", &desktop);
        toggle(kIconCamera, L"Screenshots", L"Pictures\\Screenshots", &screenshots);
        Button *add = Add(new Button(L"Add folder", ButtonStyle::Standard, [] {}));
        add->rect = card(kIconAdd, L"More folders", L"Any folder can be cleaned up the same way",
                         add->PreferredWidth(measure));
        break;
    }
    case 3:
        card(glyph::kInfo, L"Folder Cleanup 1.0",
             L"An example program for Micula. It does not move or delete anything.", 0);
        card(glyph::kSettings, L"Built with Micula " MICULA_VERSION_STRING,
             L"Header-only Fluent controls for Win32", 0);
        break;
    }
}

void Settings::PaintPage(const Painter &p) {
    const Palette &c = *p.pal;
    const float w = ClientW();
    const float left = kNavW + 12;

    // Windows 11's navigation selection: the item's own fill, and a short accent bar that
    // moves between items rather than appearing under the new one. The fill is on the chosen
    // row already -- it is the bar that travels, its two ends a row apart while it does, so
    // it stretches across the gap and closes up on arrival.
    const D2D1_RECT_F sel = { 8.0f, kNavTop + kNavPitch * (float)page,
                              kNavW - 8, kNavTop + kNavPitch * (float)page + kNavRowH };
    p.FillRound(sel, metric::kRadiusControl, c.subtleHover);
    p.FillRound({ 8.0f, kNavTop + kNavPitch * nav.Lo() + 10.0f,
                  11.0f, kNavTop + kNavPitch * nav.Hi() + kNavRowH - 10.0f },
                1.5f, c.accent);

    p.Text(kNav[page].label, { left, kCaptionH + 8, w - 24, kCaptionH + 52 }, p.font->title,
           c.textPrimary);

    for (const auto &h : headings)
        p.Text(h.second, { left, h.first, w - 24, h.first + 32 }, p.font->bodyStrong,
               c.textPrimary);

    for (const Card &cd : cards) {
        const D2D1_RECT_F &r = cd.r;
        p.FillRound(r, metric::kRadiusControl, c.cardBg);
        p.StrokeRound(r, metric::kRadiusControl, c.cardStroke);
        p.Text(cd.icon, { r.left + kInset, r.top, r.left + kInset + 20, r.top + kCardH },
               p.font->icon, c.textPrimary);

        float textRight = cd.slotLeft - 16;
        const std::wstring aside = cd.onOff ? (*cd.onOff ? L"On" : L"Off") : cd.aside;
        if (!aside.empty()) {
            const float aw = p.MeasureWidth(aside, p.font->body);
            p.Text(aside, { cd.slotLeft - 12 - aw, r.top, cd.slotLeft - 11, r.top + kCardH },
                   p.font->body, c.textPrimary);
            textRight = cd.slotLeft - 12 - aw - 16;
        }
        p.Text(cd.title, { r.left + 50, r.top + 13, textRight, r.top + 33 }, p.font->body,
               c.textPrimary);
        p.Text(cd.detail, { r.left + 50, r.top + 33, textRight, r.top + 53 }, p.font->caption,
               c.textSecondary);
    }

    if (footerY >= 0.0f)
        p.Text(L"Last cleanup 2 hours ago, 1.4 GB moved",
               { left, footerY, w - 260, footerY + metric::kControlH }, p.font->caption,
               c.textSecondary);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, wchar_t *, int) {
    EnablePerMonitorDpi();
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    int code = 1;
    {
        Settings s;
        if (s.Create(700, 630, true, nullptr)) code = s.Run();
    }
    CoUninitialize();
    return code;
}
