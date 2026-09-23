// Micula gallery
//
// Every control in the library on one page, laid out the way a small tool's settings
// window would be: a fixed header, then cards that scroll under it.
//
// It is also the shortest complete answer to "how is a Micula page written", so the
// comments are about the pattern rather than about the controls:
//
//   * State lives in the page, not in the controls. Layout() throws every control away
//     and builds them again from the page's fields -- on resize, on a DPI change, when
//     the shape of the page changes -- so a control's callback writes the field it shows.
//   * Scrolling is not a shape change. The controls are laid out in the page's own
//     coordinates and drawn through ContentTransform(), so a wheel notch moves a number
//     and costs one transform per frame: no control is built, moved or thrown away, and
//     nothing one of them is holding -- focus, a half-typed field, a sweep half done --
//     is lost on the way.
//   * Layout() may run inside a control's own callback (the Start button does); the
//     window keeps the old controls alive until that message has returned.
//   * Anything that is not a control -- headings, card backgrounds, row text -- is drawn
//     in PaintPage from rectangles Layout() worked out.

#include <micula/micula.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>
#include <vector>

using namespace micula;

namespace {

constexpr float kPad     = 24.0f;    // page margin, left and right
constexpr float kHeaderH = 104.0f;   // caption, title and subtitle; does not scroll
constexpr float kRowH    = 64.0f;    // one settings card
constexpr float kRowGap  = 4.0f;     // between the cards of a group, as Windows Settings has

}  // namespace

struct Gallery : Window {
    // --- the page's state: everything a control shows is one of these ---------------
    bool  notify = true;          // CheckBox
    bool  background = false;     // ToggleSwitch
    int   quality = 0;            // Segmented
    int   interval = 5;           // DropDown
    float volume = 0.6f;          // Slider, and the determinate ProgressBar
    bool  busy = false;           // the Start button, and the indeterminate ProgressBar
    std::wstring name = L"Micula";
    std::wstring folder;

    // --- scrolling --------------------------------------------------------------------
    // Where the page is scrolled to, and where it is drawn, which trails it. Page DIPs,
    // both of them; see ContentTransform and OnTick.
    float scroll = 0.0f;
    float drawn = 0.0f;
    float maxScroll = 0.0f;
    // How long the drawing takes to catch up with the scroll, in seconds. A wheel notch
    // or two is a short settle; a spun wheel arrives as several retargetings of the one
    // follower.
    static constexpr float kGlide = 0.07f;
    // Made once and kept across layouts (Widget::persistent): a resize or a DPI change
    // does rebuild the page, and a thumb being dragged through one must survive it.
    ScrollBar *bar = nullptr;

    // --- what PaintPage draws, worked out by Layout --------------------------------
    struct Heading { float y; std::wstring text; };
    struct Card { D2D1_RECT_F r; std::wstring title, detail; float textRight; };
    std::vector<Heading> headings;
    std::vector<Card> cards;
    size_t volumeCard = 0;           // the card whose detail follows the slider
    ProgressBar *level = nullptr;    // rebuilt by every layout, so found again by every one

    const wchar_t *ClassName() const override { return L"MiculaGallery"; }
    const wchar_t *Title() const override { return L"Micula Gallery"; }
    void MinSize(int *w, int *h) const override { *w = 540; *h = 360; }
    // The scrolling half of the page. Controls marked `scrolls` are clipped to it.
    D2D1_RECT_F ClipRect() const override { return { 0, kHeaderH, ClientW(), ClientH() }; }
    // How the scrolling half is moved. The controls' rectangles are in page coordinates
    // and never change while the page is being scrolled; this is the only thing that does,
    // so a notch costs one transform per frame instead of a whole layout.
    void ContentTransform(float *dy, float *opacity) const override {
        *dy = -drawn;            // scrolled down: the page is drawn that much higher
        *opacity = 1.0f;
    }
    // The glide is the only thing this page animates on its own account.
    bool AnimationWanted() const override { return drawn != scroll; }
    void OnTick(float dt) override {
        if (drawn == scroll) return;
        // A follower rather than a curve with a duration, for the reason the segmented
        // control's block trails with one too: a wheel spun through six notches retargets
        // this six times inside a single frame, and a storyboard restarted each time would
        // stutter between them. Exponential, so the page sets off at a speed that depends
        // on how far it has to go and eases into place, which is what a glide is.
        drawn += (scroll - drawn) * (1.0f - std::exp(-dt / kGlide));
        if (std::fabs(scroll - drawn) < 0.5f) drawn = scroll;
        SyncBar();
    }

    // The bar is laid out with the page and moves with it: its two numbers are the page's
    // own two numbers.
    void SyncBar() {
        if (!bar) return;
        bar->value = scroll;
        bar->drawn = drawn;
    }
    // What the wheel, the bar and its arrows call. It sets a target and lets the frame
    // loop draw the page toward it -- nothing is laid out. `glide` is false for the thumb,
    // which has to stay under the pointer rather than settle toward it.
    void ScrollTo(float to, bool glide = true) {
        scroll = std::clamp(to, 0.0f, maxScroll);
        if (!glide) drawn = scroll;
        SyncBar();
        if (bar) { bar->Wake(); bar->Poll(); }
        if (drawn != scroll) StartAnimation(this);
        Invalidate();
    }
    void OnDefaultAction() override { ToggleBusy(); }   // Enter, with nothing focused

    void ToggleBusy() {
        busy = !busy;
        // Relabelling the button means rebuilding it -- from inside its own callback,
        // which is allowed. See Window::retired.
        Layout();
        Invalidate();
    }

    static std::wstring Percent(float v) {
        wchar_t b[16];
        swprintf(b, 16, L"%d%%", (int)(v * 100.0f + 0.5f));
        return b;
    }

    void Layout() override;
    void PaintPage(const Painter &p) override;
    bool OnAppMessage(UINT m, WPARAM wp, LPARAM lp) override;
};

void Gallery::Layout() {
    ClearWidgets();   // everything but `bar`, which is persistent
    headings.clear();
    cards.clear();
    level = nullptr;

    const float w = ClientW(), h = ClientH();
    const float left = kPad, right = w - kPad;
    Painter measure;   // measuring text needs the fonts and nothing else
    measure.font = &fonts;

    // `y` is in page coordinates, 0 at the top of the scrolling part, and so are the
    // rectangles worked out from it: the page's scroll is not applied here at all, it is a
    // transform the window puts on this half of the page when it paints.
    float y = 8.0f;
    auto at = [&](float py) { return kHeaderH + py; };
    auto place = [](Widget *wd, const D2D1_RECT_F &r) {
        wd->rect = r;
        wd->scrolls = true;
        return wd;
    };
    auto heading = [&](const wchar_t *text) {
        if (!cards.empty()) y += 24.0f - kRowGap;
        headings.push_back({ at(y), text });
        y += 30.0f;
    };
    // One settings card: a title and a line of detail on the left, and a slot for the
    // control on the right, which is what this returns.
    auto card = [&](std::wstring title, std::wstring detail, float controlW) {
        const D2D1_RECT_F r = { left, at(y), right, at(y) + kRowH };
        const float slotL = r.right - 16 - controlW;
        cards.push_back({ r, std::move(title), std::move(detail), slotL - 16 });
        y += kRowH + kRowGap;
        const float cy = (r.top + r.bottom) / 2;
        return D2D1_RECT_F{ slotL, cy - metric::kControlH / 2, r.right - 16,
                            cy + metric::kControlH / 2 };
    };

    heading(L"Buttons");
    {
        card(L"", L"", 0);
        const D2D1_RECT_F r = cards.back().r;
        const float cy = (r.top + r.bottom) / 2;
        float x = r.left + 16;
        auto row = [&](Button *b) {
            const float bw = b->PreferredWidth(measure);
            place(b, { x, cy - metric::kControlH / 2, x + bw, cy + metric::kControlH / 2 });
            x += bw + 8;
        };
        row(Add(new Button(busy ? L"Stop" : L"Start", ButtonStyle::Accent,
                           [this] { ToggleBusy(); })));
        row(Add(new Button(L"Standard", ButtonStyle::Standard, [] {})));
        Button *subtle = Add(new Button(L"Open folder", ButtonStyle::Subtle, [] {}));
        subtle->glyph = glyph::kFolder;
        row(subtle);
        row(Add(new Button(L"Learn more", ButtonStyle::Link, [] {})));
    }

    heading(L"Choices");
    {
        card(L"", L"", 0);
        const D2D1_RECT_F r = cards.back().r;
        const float cy = (r.top + r.bottom) / 2;
        CheckBox *cb = Add(new CheckBox(L"Show a notification when done", notify,
                                        [this](bool on) { notify = on; }));
        cb->detail = L"CheckBox - a choice collected now and applied later";
        place(cb, { r.left + 16, cy - 22, r.right - 16, cy + 22 });
    }
    place(Add(new ToggleSwitch(L"", background, [this](bool on) { background = on; })),
          card(L"Run in the background",
               L"ToggleSwitch - takes effect the moment it changes", 40));
    place(Add(new Segmented({ L"Auto", L"High", L"Low" }, quality,
                            [this](int i) { quality = i; })),
          card(L"Quality", L"Segmented - a few words, side by side", 180));
    place(Add(new DropDown({ L"Never", L"Every hour", L"Every 3 hours", L"Every 6 hours",
                             L"Every 12 hours", L"Daily", L"Every 3 days", L"Weekly",
                             L"Monthly" },
                           interval, [this](int i) { interval = i; })),
          card(L"Check for updates",
               L"DropDown - opens over the control, the chosen row on it", 180));

    heading(L"Values");
    {
        const D2D1_RECT_F slot = card(L"Volume", Percent(volume), 200);
        volumeCard = cards.size() - 1;
        // No Invalidate here: a slider reports its drag from inside its own paint, and
        // the window is already repainting for as long as the drag lasts.
        place(Add(new Slider(volume, 0.0f, 1.0f, 0.01f, [this](float v) {
                  volume = v;
                  if (volumeCard < cards.size()) cards[volumeCard].detail = Percent(v);
                  if (level) level->value = v;
              })),
              slot);
    }
    {
        TextBox *t = Add(new TextBox());
        t->SetText(name);
        t->onChange = [this](const std::wstring &s) { name = s; };
        place(t, card(L"Display name", L"TextBox - caret, selection, clipboard, IME", 220));
    }
    {
        TextBox *t = Add(new TextBox());
        t->SetText(folder);
        t->pathField = true;
        t->placeholder = L"C:\\Path\\to\\folder";
        t->onChange = [this](const std::wstring &s) { folder = s; };
        place(t, card(L"Folder", L"pathField - pasted quotes are dropped", 220));
    }

    heading(L"Progress");
    {
        level = Add(new ProgressBar());
        level->value = volume;
        place(level, card(L"Level", L"ProgressBar - follows the volume slider", 200));
    }
    {
        ProgressBar *spin = Add(new ProgressBar());
        spin->indeterminate = busy;
        place(spin, card(L"Working", busy ? L"Indeterminate - press Stop to end it"
                                          : L"Indeterminate - press Start to run it",
                         200));
    }

    const float extent = y - kRowGap + kPad;
    const float viewport = h - kHeaderH;
    maxScroll = (std::max)(0.0f, extent - viewport);
    // A window made taller, or content made shorter, can leave the page scrolled past
    // its own end. Laid out again from the end rather than drawn with a gap.
    if (scroll > maxScroll) {
        scroll = maxScroll;
        Layout();
        return;
    }
    // And the same for the drawing, which is what the page is really moved by.
    if (drawn > maxScroll || drawn < 0.0f) drawn = scroll;

    if (!bar) {
        bar = Add(new ScrollBar([this](float to, bool glide) { ScrollTo(to, glide); }));
        bar->persistent = true;
    }
    bar->rect = { w - ScrollBar::kSize - 1, kHeaderH, w - 1, h - 1 };
    bar->area = ClipRect();
    bar->viewport = viewport;
    bar->extent = extent;
    bar->visible = extent > viewport;
    SyncBar();
}

void Gallery::PaintPage(const Painter &p) {
    const Palette &c = *p.pal;
    const float w = ClientW();
    p.Text(L"Micula", { kPad, kCaptionH + 8, w - kPad, kCaptionH + 48 }, p.font->title,
           c.textPrimary);
    p.Text(L"Every control in the library, on one page.",
           { kPad, kCaptionH + 48, w - kPad, kCaptionH + 68 }, p.font->body, c.textSecondary);

    // The scrolling part, clipped to the same rectangle the window clips the scrolling
    // controls to, so a card and the control on it disappear under the header together.
    // The cards are in page coordinates like the controls, and PaintPage is not
    // transformed, so the page's offset comes off the drawing here by hand.
    p.rt->PushAxisAlignedClip(ClipRect(), D2D1_ANTIALIAS_MODE_ALIASED);
    const float off = -drawn;
    for (const Heading &hd : headings)
        p.Text(hd.text, { kPad, hd.y + off, w - kPad, hd.y + off + 30 }, p.font->bodyStrong,
               c.textPrimary);
    for (const Card &cd : cards) {
        const D2D1_RECT_F r = { cd.r.left, cd.r.top + off, cd.r.right, cd.r.bottom + off };
        p.FillRound(r, metric::kRadiusControl, c.cardBg);
        p.StrokeRound(r, metric::kRadiusControl, c.cardStroke);
        if (cd.title.empty()) continue;
        p.Text(cd.title, { r.left + 16, r.top + 12, cd.textRight, r.top + 32 },
               p.font->body, c.textPrimary);
        p.Text(cd.detail, { r.left + 16, r.top + 32, cd.textRight, r.top + 52 },
               p.font->caption, c.textSecondary);
    }
    p.rt->PopAxisAlignedClip();
}

// The wheel over anything that did not take it itself -- an open drop-down's list does.
bool Gallery::OnAppMessage(UINT m, WPARAM wp, LPARAM) {
    if (m != WM_MOUSEWHEEL) return false;
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    if (lines == WHEEL_PAGESCROLL) lines = 6;
    const float notches = (float)GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA;
    ScrollTo(scroll - notches * (float)lines * 22.0f);
    return true;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, wchar_t *, int) {
    EnablePerMonitorDpi();
    // WIC is COM, and the window goes through it for the caption icon and Window::Image.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    int code = 1;
    {
        Gallery g;
        // No icon. The caption draws one as a monochrome mask, which suits a white mark
        // and turns a colour icon into its silhouette -- see Window::EnsureIconBitmap.
        if (g.Create(560, 640, true, nullptr)) code = g.Run();
    }
    CoUninitialize();
    return code;
}
