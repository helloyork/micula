// Micula gallery
//
// Every control the library has, on one page, laid out the way a small tool's settings window is: a
// stack of cards, with a heading over each group.
//
// It is also the shortest complete answer to "how is a Micula page written", so the comments are
// about the pattern rather than about the controls:
//
//   * The page is a **tree, built once**. State lives in the page, and a control's callback writes
//     the field it shows; what changes about the page is what the layout does with the tree on the
//     next frame. Nothing is torn down and built again, so a half-typed field, the button that was
//     focused and a slider mid-drag all survive whatever else happened.
//   * **Nothing says where anything goes.** The page says "this, then this, then this" and the
//     platform supplies the spacing; a card says "these words, and this control on the right" and
//     the card works out where each of them sits. There is no coordinate in this file.
//   * Cards are the platform's control template rather than a picture the page draws: a row is an
//     icon if it has one, a line of text, a line under it if it has one, a value, and a control.
//   * Anything a page draws that is not a control -- a heading, a line of text -- is a widget too,
//     which is what puts it in the tree that the arrangement and the screen reader both read.
//
// `--dump` prints the rectangles all of that came out as, without opening a window. It is how a
// layout fault is found and checked, and it is what a screenshot is not.

#include <micula/micula.h>

#include <cstdio>
#include <cwchar>
#include <fcntl.h>
#include <io.h>
#include <string>

using namespace micula;

namespace {

// The page: its state, and the tree built out of it.
//
// A separate object from the window rather than the window itself, because the two are different
// things and only one of them needs a window to exist: `--dump` builds this one into a plain View.
struct Page {
    // --- the state: everything a control shows is one of these ----------------------
    bool  capture = true;
    bool  notify = false;
    int   quality = 1;                  // the middle one of the three
    float volume = 0.6f;
    std::wstring device = L"Pixel 8";

    // --- the tree -------------------------------------------------------------------
    // The page, in a window. Two nodes: the root the window gave the page, and the scroll view
    // everything else is in -- so the page margin belongs to the view rather than to the window, and
    // a window shorter than the page scrolls instead of cutting it off.
    ScrollView *Build(Widget *root) {
        // The root is a stack with no margin of its own, so its one child is the whole client area
        // below the caption.
        auto *whole = new StackLayout();
        whole->padX = 0.0f;
        root->SetLayout(whole);

        ScrollView *page = root->Add(new ScrollView());

        page->Add(new Heading(L"捕获"));

        auto *capturer = page->Add(new Card(L"使用捕获器", L"打开后 scrcpy 的窗口会被裁掉，录制时只保留设备画面本身。"));
        capturer->icon = glyph::kEthernet;
        capturer->Set(new ToggleSwitch(L"", capture, [this](bool v) { capture = v; }));
        // Read when the card is painted rather than pushed into it, so the two never have to be kept
        // in step -- and the switch's own animation is what asks for the frames that redraw it.
        capturer->value = [this] { return capture ? L"开" : L"关"; };

        auto *named = page->Add(new Card(L"设备名称", L"只影响本机显示，用来区分同时连接的多台设备。"));
        named->icon = glyph::kCellPhone;
        auto *field = named->Set(new TextBox());
        field->text = device;
        field->placeholder = L"Pixel";
        field->onChange = [this](const std::wstring &s) { device = s; };

        page->Add(new Heading(L"高级"));

        auto *notified = page->Add(new Card(L"完成时通知", L"转录或搬运结束时弹出一条通知，窗口在后台也能看到。"));
        notified->icon = glyph::kInfo;
        notified->Set(new CheckBox(L"", notify, [this](bool v) { notify = v; }));

        // A card whose control takes the width the text did not: the slider asked to fill, and the
        // card gives it what is left. None of that arithmetic is in the page.
        auto *gain = page->Add(new Card(L"音量", L"只改这台电脑回放时的增益，不会改动设备自己的音量。"));
        gain->icon = glyph::kVolume;
        gain->Set(new Slider(this->volume, 0.0f, 1.0f, 0.05f, [this](float v) { this->volume = v; }));
        gain->value = [this] {
            return std::to_wstring((int)(this->volume * 100.0f + 0.5f)) + L"%";
        };

        auto *sharp = page->Add(new Card(L"画质", L"流畅省电，清晰更接近原图，标准是两者的折中。"));
        sharp->icon = glyph::kView;
        sharp->Set(new Segmented({ L"流畅", L"标准", L"清晰" }, this->quality,
                                  [this](int i) { this->quality = i; }));

        // A row of its own for the two things a page *does* rather than sets: a card is a setting,
        // and these are not.
        auto *verbs = page->Add(new View());
        verbs->SetLayout(new RowLayout());
        verbs->Add(new Button(L"开始", ButtonStyle::Accent, [this] { notify = true; }));
        // A control reaches the window through the tree it is in, which is what lets this callback
        // close the window without the page holding a pointer to one.
        Button *quit = verbs->Add(new Button(L"关闭", ButtonStyle::Standard, nullptr));
        quit->onClick = [quit] {
            if (Window *w = quit->window()) PostMessageW(w->hwnd, WM_CLOSE, 0, 0);
        };

        page->Add(new Label(L"这一页没有一处坐标：控件报告它们想要什么，布局决定它们在哪里。",
                            TextRole::Caption))->secondary = true;
        return page;
    }
};

// One widget a line: the number Add gave it, the rectangle it came out as, and what a screen reader
// would call it. Indented by depth, because the tree is the thing being read -- a rectangle is only
// wrong relative to the one it should be inside of.
//
// A card gets three more lines under it, because its words are not widgets and their rectangles are
// the only geometry on the page that the tree cannot answer for.
void Print(const Fonts &fonts, const Widget *w, int depth, float ox, float oy) {
    const D2D1_RECT_F &r = w->rect;
    std::wprintf(L"%*s#%-3d %7.1f %7.1f %7.1f %7.1f", depth * 2, L"", w->uid,
                 ox + r.left, oy + r.top, ox + r.right, oy + r.bottom);
    if (const wchar_t *name = w->AccessibleLabel()) std::wprintf(L"  %ls", name);
    std::wprintf(L"\n");

    if (const Card *card = dynamic_cast<const Card *>(w)) {
        const Card::Text t = card->Wording(fonts, card->layout->spec);
        const D2D1_RECT_F boxes[3] = { t.title, t.under, t.value };
        const wchar_t *names[3] = { L"title", L"under", L"value" };
        const bool shown[3] = { true, t.hasUnder, t.hasValue };
        for (int i = 0; i < 3; i++) {
            if (!shown[i]) continue;
            const D2D1_RECT_F &b = boxes[i];
            // The card's own rectangles already are where they are drawn -- in the space the card's
            // `rect` is measured in, which is what `ox, oy` name -- so nothing is added to them.
            std::wprintf(L"%*s %-5ls %7.1f %7.1f %7.1f %7.1f\n", (depth + 1) * 2, L"", names[i],
                         ox + b.left, oy + b.top, ox + b.right, oy + b.bottom);
        }
    }

    for (const auto &child : w->children) {
        if (!child->visible) continue;
        Print(fonts, child.get(), depth + 1, ox + r.left, oy + r.top);
    }
}

// The page in a window that is never created: what `--dump`, `--hit` and `--scroll` all need, since
// the fonts and the tree are the whole of what a layout is measured against and a window is the one
// thing none of them has.
//
// The window object is still the right thing to build in. It is what owns the root widget, it is
// what the hit test is a method of, and it is what a page adds to -- so a program that lays its page
// out without showing it runs the same code the shown one does, which is the point.
struct Gallery : Window {
    Page *page;
    explicit Gallery(Page *p) : page(p) {}

    const wchar_t *ClassName() const override { return L"MiculaGallery"; }
    const wchar_t *Title() const override { return L"Micula"; }

    // The tree, and the box a window of this size would give it. The view the page is in comes back
    // because that is what a wheel turns and what `--scroll` turns by hand.
    ScrollView *Build(float w, float h) {
        ScrollView *view = page->Build(EnsureContent());
        content->rect = { 0.0f, kCaptionH, w, h };
        return view;
    }
};

// The fonts, and the three things that need them. Every measurement a layout makes is a DirectWrite
// one, so a factory and ten formats are the whole of what a window was providing.
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

// The window the example opens at, and the box the three window-free modes lay the page out in.
//
// Shorter than the page: 480 DIP of client area under a page that comes out about 512, which is what
// makes this the example's scroll view rather than a stack of cards that happens to end above the
// bottom. A page that fits says nothing about a container that overflows.
constexpr float kWinW = 700.0f;
constexpr float kWinH = 480.0f;

// The page, arranged into a window's worth of space and printed, with no window anywhere.
void Dump(Fonts &fonts) {
    Gallery gallery(nullptr);
    Page page;
    gallery.page = &page;
    ScrollView *view = gallery.Build(kWinW, kWinH);
    ArrangeSubtree(gallery.content.get(), fonts);
    std::wprintf(L"scroll   scroll=%.1f extent=%.1f most=%.1f\n",
                 view->scroll, view->extent, view->ScrollMax());
    Print(fonts, gallery.content.get(), 0, 0.0f, 0.0f);
}

// Every widget's own centre, asked of the hit test. What should come back at a widget's centre is
// that widget, or something inside it -- the middle of a card is over the control on the card, not
// over the card. Anything else is a click that would land on the wrong control, and it is the one
// fault in this area that looking at the window cannot find: a page whose controls are all in the
// right place and none of which can be pressed looks exactly like a page.
void Hit(Fonts &fonts, Widget *w, int depth, float ox, float oy) {
    if (!w->visible) return;
    const D2D1_RECT_F r = w->rect;
    const float cx = ox + (r.left + r.right) / 2;
    const float cy = oy + (r.top + r.bottom) / 2;
    Widget *found = w->window()->HitTest(cx, cy);
    // A widget scrolled out of the container it lives in is not reachable and is not meant to be: its
    // own centre is outside the box it is seen through. That is a different answer from a click
    // landing on something else, which is the fault this is looking for, so the print says which.
    const D2D1_RECT_F seen = w->VisibleArea();
    const bool clipped = r.bottom <= seen.top || r.top >= seen.bottom ||
                         r.right <= seen.left || r.left >= seen.right;
    const wchar_t *verdict = L"other";
    if (clipped) verdict = L"scrolled out";
    else if (!found) verdict = L"nothing";
    else if (found == w) verdict = L"itself";
    else if (w->Holds(found)) verdict = L"inside";

    std::wprintf(L"%*s#%-3d centre %7.1f %7.1f -> #%-3d %ls", depth * 2, L"", w->uid, cx, cy,
                 found ? found->uid : 0, verdict);
    if (const wchar_t *name = w->AccessibleLabel()) std::wprintf(L"  %ls", name);
    std::wprintf(L"\n");

    for (const auto &child : w->children) Hit(fonts, child.get(), depth + 1, ox + r.left, oy + r.top);
}

void Hits(Fonts &fonts) {
    Gallery gallery(nullptr);
    Page page;
    gallery.page = &page;
    gallery.Build(kWinW, kWinH);
    ArrangeSubtree(gallery.content.get(), fonts);
    Hit(fonts, gallery.content.get(), 0, 0.0f, 0.0f);
}

// The wheel, turned by hand: the same number `OnWheel` would move, through the same call. What it
// checks is the arithmetic nobody can see -- that the children moved by exactly the notch, that the
// offset stopped at the end of the content rather than past it, and that a notch's worth of them is
// the system's own "lines" times a row.
void Scroll(Fonts &fonts, float dip) {
    Gallery gallery(nullptr);
    Page page;
    gallery.page = &page;
    ScrollView *view = gallery.Build(kWinW, kWinH);
    ArrangeSubtree(gallery.content.get(), fonts);
    std::wprintf(L"before   scroll=%.1f extent=%.1f most=%.1f\n",
                 view->scroll, view->extent, view->ScrollMax());
    view->ScrollBy(dip);
    ArrangeSubtree(gallery.content.get(), fonts);
    std::wprintf(L"after    scroll=%.1f  (asked for %+.1f)\n", view->scroll, dip);

    // What the tree says about itself while the container is carrying it. The view is animating,
    // because its layout is gliding; the first card in it is not animating at all -- it is being
    // moved, and being moved is not something it draws. That split is the point, and it is also what
    // keeps a scrolled page from turning frames for the rest of the window's life: when the glide
    // lands everything says no, and the loop goes back to blocking in GetMessage.
    Widget *first = view->children.empty() ? nullptr : view->children.front().get();
    std::wprintf(L"moving   view=%ls (its layout is gliding)  first card=%ls (its own)\n",
                 view->Animating() ? L"yes" : L"no",
                 (first && first->Animating()) ? L"yes" : L"no");
    int frames = 0;
    while (view->Animating() && frames < 1000) {
        view->Tick(1.0f / 60.0f);
        frames++;
    }
    std::wprintf(L"glide    landed after %d frames, animating=%ls, first card drawn=%.1f rect=%.1f\n",
                 frames, view->Animating() ? L"yes" : L"no",
                 first ? first->drawn.top : 0.0f, first ? first->rect.top : 0.0f);
    Print(fonts, gallery.content.get(), 0, 0.0f, 0.0f);
}

}  // namespace

int wmain(int argc, wchar_t **argv) {
    bool dump = false, hit = false;
    float scroll = 0.0f;
    for (int i = 1; i < argc; i++) {
        if (std::wcscmp(argv[i], L"--dump") == 0) dump = true;
        if (std::wcscmp(argv[i], L"--hit") == 0) hit = true;
        if (std::wcscmp(argv[i], L"--scroll") == 0 && i + 1 < argc) scroll = (float)_wtof(argv[++i]);
    }
    if (dump || hit || scroll != 0.0f) {
        // Wide rather than in the console's code page: the labels are Chinese, and a stream left
        // alone narrows every one of them through the CRT's default encoding on the way out.
        _setmode(_fileno(stdout), _O_U16TEXT);
        if (dump) return WithFonts(Dump);
        if (hit) return WithFonts(Hits);
        // A closure rather than the function pointer the other two use, since this one has a number
        // of its own to carry.
        return WithFonts([scroll](Fonts &f) { Scroll(f, scroll); });
    }

    // A console program so that --dump has somewhere to print. Launched from Explorer that console
    // is one nobody asked for, so it is dropped when it is ours and left alone when it is a
    // terminal's.
    DWORD owners = 0;
    if (GetConsoleProcessList(&owners, 1) == 1) FreeConsole();

    // Neither EnablePerMonitorDpi nor CoInitializeEx: Window::Create does both, and only when
    // nobody else has said anything. See the top of window.h.
    int code = 1;
    {
        Page page;
        Gallery gallery(&page);
        gallery.Build(kWinW, kWinH);
        if (gallery.Create((int)kWinW, (int)kWinH, true, nullptr)) code = gallery.Run();
    }
    return code;
}

