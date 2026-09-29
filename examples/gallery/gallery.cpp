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
    void Build(Widget *root) {
        root->SetLayout(new StackLayout());

        root->Add(new Heading(L"捕获"));

        auto *capturer = root->Add(new Card(L"使用捕获器", L"scrcpy 的窗口会被截断。"));
        capturer->icon = glyph::kEthernet;
        capturer->Set(new ToggleSwitch(L"", capture, [this](bool v) { capture = v; }));
        // Read when the card is painted rather than pushed into it, so the two never have to be kept
        // in step -- and the switch's own animation is what asks for the frames that redraw it.
        capturer->value = [this] { return capture ? L"开" : L"关"; };

        auto *named = root->Add(new Card(L"设备名称", L"设备的显示名，用来区分多个连接。"));
        named->icon = glyph::kCellPhone;
        auto *field = named->Set(new TextBox());
        field->text = device;
        field->placeholder = L"Pixel";
        field->onChange = [this](const std::wstring &s) { device = s; };

        root->Add(new Heading(L"高级"));

        auto *notified = root->Add(new Card(L"完成时通知", L"转录结束后弹出一条通知。"));
        notified->icon = glyph::kInfo;
        notified->Set(new CheckBox(L"", notify, [this](bool v) { notify = v; }));

        // A card whose control takes the width the text did not: the slider asked to fill, and the
        // card gives it what is left. None of that arithmetic is in the page.
        auto *gain = root->Add(new Card(L"音量", L"从设备回放到这台电脑上的增益。"));
        gain->icon = glyph::kVolume;
        gain->Set(new Slider(this->volume, 0.0f, 1.0f, 0.05f, [this](float v) { this->volume = v; }));
        gain->value = [this] {
            return std::to_wstring((int)(this->volume * 100.0f + 0.5f)) + L"%";
        };

        auto *sharp = root->Add(new Card(L"画质"));
        sharp->icon = glyph::kView;
        sharp->Set(new Segmented({ L"流畅", L"标准", L"清晰" }, this->quality,
                                  [this](int i) { this->quality = i; }));

        // A row of its own for the two things a page *does* rather than sets: a card is a setting,
        // and these are not.
        auto *verbs = root->Add(new View());
        verbs->SetLayout(new RowLayout());
        verbs->Add(new Button(L"开始", ButtonStyle::Accent, [this] { notify = true; }));
        // A control reaches the window through the tree it is in, which is what lets this callback
        // close the window without the page holding a pointer to one.
        Button *quit = verbs->Add(new Button(L"关闭", ButtonStyle::Standard, nullptr));
        quit->onClick = [quit] {
            if (Window *w = quit->window()) PostMessageW(w->hwnd, WM_CLOSE, 0, 0);
        };

        root->Add(new Label(L"这一页没有一处坐标：控件报告它们想要什么，布局决定它们在哪里。",
                            TextRole::Caption))->secondary = true;
    }
};

// One widget a line: the number Add gave it, the rectangle it came out as, and what a screen reader
// would call it. Indented by depth, because the tree is the thing being read -- a rectangle is only
// wrong relative to the one it should be inside of.
void Print(const Widget *w, int depth, float ox, float oy) {
    const D2D1_RECT_F &r = w->rect;
    std::wprintf(L"%*s#%-3d %7.1f %7.1f %7.1f %7.1f", depth * 2, L"", w->uid,
                 ox + r.left, oy + r.top, ox + r.right, oy + r.bottom);
    if (const wchar_t *name = w->AccessibleLabel()) std::wprintf(L"  %ls", name);
    std::wprintf(L"\n");
    for (const auto &child : w->children) {
        if (!child->visible) continue;
        Print(child.get(), depth + 1, ox + r.left, oy + r.top);
    }
}

// The page, arranged into a window's worth of space and printed, with no window anywhere.
//
// The fonts are the only thing a window was really providing: every measurement a layout makes is a
// DirectWrite one, and that needs a factory and ten formats rather than a device.
int Dump(float w, float h) {
    // Wide rather than in the console's code page: the labels are Chinese, and a stream left alone
    // narrows every one of them through the CRT's default encoding on the way out.
    _setmode(_fileno(stdout), _O_U16TEXT);

    IDWriteFactory *dw = nullptr;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown **>(&dw))) || !dw) {
        std::fwprintf(stderr, L"DirectWrite is not available.\n");
        return 1;
    }
    Fonts fonts;
    if (!fonts.Create(dw)) {
        std::fwprintf(stderr, L"the font formats could not be made.\n");
        return 1;
    }

    Page page;
    View root;
    page.Build(&root);

    // The box the window gives its content: the client area below the caption.
    root.rect = { 0.0f, kCaptionH, w, h };
    ArrangeSubtree(&root, fonts);
    Print(&root, 0, 0.0f, 0.0f);

    fonts.Release();
    dw->Release();
    return 0;
}

}  // namespace

// The window, and the page inside it. Nothing here but the two things a window has always had to
// say -- what it is called, and that it is a window -- and the tree its page built.
struct Gallery : Window {
    Page *page;
    explicit Gallery(Page *p) : page(p) {}

    const wchar_t *ClassName() const override { return L"MiculaGallery"; }
    const wchar_t *Title() const override { return L"Micula"; }
};

int wmain(int argc, wchar_t **argv) {
    for (int i = 1; i < argc; i++) {
        if (std::wcscmp(argv[i], L"--dump") == 0) return Dump(700.0f, 620.0f);
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
        page.Build(gallery.EnsureContent());
        if (gallery.Create(700, 620, true, nullptr)) code = gallery.Run();
    }
    return code;
}
