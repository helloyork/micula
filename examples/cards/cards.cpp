// Grouped settings cards: joined surfaces, a retained expander, and a centred content column.
// --check exercises geometry, input and the real UIA providers without opening a window.
// --render file.png paints the same tree offscreen through Direct2D, for visual inspection.
#include <micula/micula.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <cwchar>
#include <stdexcept>

using namespace micula;
using Microsoft::WRL::ComPtr;

namespace {

struct Cards : Window {
    ScrollView *page = nullptr;
    CardGroup *joined = nullptr;
    CardExpander *expander = nullptr;
    Card *first = nullptr, *middle = nullptr, *last = nullptr, *after = nullptr;
    TextBox *field = nullptr;
    ToggleSwitch *headerSwitch = nullptr;
    std::vector<Card *> extra;
    bool blocking = true, logs = false;
    Tips tips{ *this };

    const wchar_t *ClassName() const override { return L"MiculaCards"; }
    const wchar_t *Title() const override { return L"Micula - Grouped cards"; }

    void Build(float width = 1200, float height = 820) {
        Widget *root = EnsureContent();
        auto *stack = new StackLayout();
        stack->padX = 0;
        root->SetLayout(stack);
        page = root->Add(new ScrollView());
        // A demo choice, not a universal WinUI size token. Zero restores an unlimited column.
        auto *column = static_cast<StackLayout *>(page->content->layout.get());
        column->maxWidth = 920;
        page->Add(new PageTitle(L"分组卡片"));
        page->Add(new Label(L"拖宽窗口查看限宽居中；点击标题或使用键盘展开详情。"))->secondary = true;

        page->Add(new Heading(L"普通分组"));
        joined = page->Add(new CardGroup());
        first = joined->Add(new Card(L"启用规则", L"第一项只保留顶部圆角。"));
        first->Set(new ToggleSwitch(L"", blocking, [this](bool on) { blocking = on; }));
        middle = joined->Add(new Card(L"记录被阻止的启动", L"中间项没有圆角，相邻卡片间留有 1 DIP 间距。"));
        middle->Set(new CheckBox(L"", logs, [this](bool on) { logs = on; }));
        last = joined->Add(new Card(L"查看配置", L"最后一项只保留底部圆角。"));
        last->Set(new Button(L"显示 / 隐藏中间项", ButtonStyle::Standard, [this] {
            joined->SetVisible(middle, !joined->IsVisible(middle));
        }));

        page->Add(new Heading(L"可展开分组"));
        expander = page->Add(new CardExpander(L"程序规则", L"标题仍是一张 Card，右侧控件可独立操作。"));
        headerSwitch = expander->header->Set(new ToggleSwitch(L"启用", true, nullptr));
        auto *path = expander->Add(new Card(L"目标路径", L"收起再展开，已输入的文字和控件对象都保留。"));
        field = path->Set(new TextBox());
        field->text = L"C:\\Vendor\\popup.exe";
        auto *action = expander->Add(new Card(L"处理方式", L"沿用已有卡片和控件的布局能力。"));
        action->Set(new DropDown({ L"禁止启动", L"临时停用" }, 0, nullptr));
        auto *note = expander->Add(new Card(L"备注", L"第三张子卡片封住整个分组的底边和圆角。"));
        note->Set(new Button(L"批量添加 / 删除", ButtonStyle::Standard, [this] {
            if (!extra.empty()) {
                for (auto *card : extra) expander->items->Remove(card);
                extra.clear();
            } else for (int i = 0; i < 3; ++i)
                extra.push_back(expander->Add(new Card(L"动态卡片 " + std::to_wstring(i + 1),
                    L"连续操作合并为一次布局，三张卡片作为一块进入或退出。")));
        }));
        after = page->Add(new Card(L"分组之后", L"展开和收起时，它与分组底边始终保持正常的卡片间距。"));
        auto *buttons = page->Add(new View());
        auto *row = new RowLayout();
        buttons->SetLayout(row);
        buttons->Add(new Button(L"展开 / 收起", ButtonStyle::Standard,
                               [this] { expander->SetExpanded(!expander->Expanded()); }));
        buttons->Add(new Button(L"切换动画", ButtonStyle::Standard, [this] {
            Animations(!Animations());
            Invalidate();
        }));
        buttons->Add(new Button(L"批量显示 / 隐藏前两项", ButtonStyle::Standard, [this] {
            const bool show = !joined->IsVisible(first);
            joined->SetVisible(first, show);
            joined->SetVisible(middle, show);
        }));
        root->rect = { 0, kCaptionH, width, height };
    }
};

void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
bool Near(float a, float b) { return std::fabs(a - b) < 0.02f; }

void NoOverlap(CardGroup *group) {
    float bottom = -10000;
    for (const auto &child : group->children) {
        const auto *slot = static_cast<CardGroupSlot *>(child.get());
        if (!slot->visible) continue;
        const auto clip = slot->ClipBox();
        const float top = (std::max)(slot->drawn.top, clip.top);
        const float end = (std::min)(slot->drawn.bottom, clip.bottom);
        if (top >= end) continue;
        Require(top >= bottom - 0.02f, "painted rows must not overlap through translucent surfaces");
        bottom = end;
    }
}

void Settle(Cards &w) {
    for (int i = 0; i < 240 && w.content->Animating(); ++i) w.content->Tick(1.0f / 60.0f);
    Require(!w.content->Animating(), "animation must stop instead of keeping the frame loop awake");
}

void Check(Fonts &fonts) {
    wchar_t windows[MAX_PATH] = {};
    Require(GetWindowsDirectoryW(windows, MAX_PATH) != 0, "Windows font directory");
    const Font custom = Font::FromFile(std::wstring(windows) + L"\\Fonts\\segoeui.ttf", 14);
    Require(!custom.Empty(), "DirectWrite file-font creation must work with this SDK");
    Require(Font::FromFile(L"Z:\\micula-missing-font-file.ttf", 14).Empty(), "unreadable font returns empty");
    Animations(true);
    Cards w;
    w.Build();
    ArrangeSubtree(w.content.get(), fonts);
    PlaceSubtree(w.content.get());
    Require(Near(Width(w.joined->rect), 920), "wide page must cap the column");
    Require(Near(w.joined->rect.left, 140), "capped column must be centred");
    Require(w.first->corners.r[0] == 4 && w.first->corners.r[2] == 0, "first row corners");
    Require(w.middle->corners.r[0] == 0 && w.middle->corners.r[2] == 0, "middle row corners");
    Require(w.last->corners.r[0] == 0 && w.last->corners.r[2] == 4, "last row corners");
    Require(Near(w.joined->Slot(w.middle)->rect.top - w.joined->Slot(w.first)->rect.bottom, 1),
            "group gutter is a clear 1 DIP, independent of borders");
    w.joined->drawn.top += 12;
    w.joined->drawn.bottom += 12;
    Require(Near(w.last->VisibleArea().bottom, 64), "UIA/visibility clips follow the moving ancestor");
    w.joined->drawn = w.joined->rect;
    w.joined->GroupLayout().spacing = 0;
    w.joined->InvalidateLayout();
    ArrangeSubtree(w.content.get(), fonts);
    Settle(w);
    Require(!(w.middle->borderEdges & edge::kTop), "zero-gap mode suppresses duplicate separators");
    w.joined->GroupLayout().spacing = 1;
    w.joined->InvalidateLayout();
    ArrangeSubtree(w.content.get(), fonts);
    Settle(w);
    w.joined->SetVisible(w.middle, false);
    Require(!w.UiaFind(w.middle->uid), "a leaving row immediately leaves UIA");
    ArrangeSubtree(w.content.get(), fonts);
    auto *middleSlot = w.joined->Slot(w.middle);
    Require(middleSlot->visible, "a leaving row paints until its clipped exit completes");
    for (int i = 0; i < 4; ++i) { w.content->Tick(1.0f / 60); NoOverlap(w.joined); }
    const float reversing = middleSlot->drawn.top;
    const auto reversingClip = middleSlot->ClipBox();
    w.joined->SetVisible(w.middle, true);
    ArrangeSubtree(w.content.get(), fonts);
    Require(Near(middleSlot->drawn.top, reversing) && Near(middleSlot->first, reversingClip.top),
            "row reversal preserves geometry and clipped pixels");
    Settle(w);
    w.joined->SetVisible(w.first, false);
    w.joined->SetVisible(w.middle, false);
    ArrangeSubtree(w.content.get(), fonts);
    for (int i = 0; i < 6; ++i) {
        w.content->Tick(1.0f / 60);
        auto *a = w.joined->Slot(w.first);
        auto *b = w.joined->Slot(w.middle);
        Require(Near(b->drawn.top - a->drawn.top, a->height + 1), "batch exit travels as one block");
        Require(Near(a->first, b->first) && Near(a->last, b->last), "batch exit shares one viewport");
        NoOverlap(w.joined);
    }
    Settle(w);
    Require(!w.joined->Slot(w.first)->visible && !middleSlot->visible, "all batch rows retire together");
    Require(w.last->corners.r[0] == 4 && w.last->corners.r[2] == 4, "singleton has four corners");
    w.joined->SetVisible(w.first, true);
    w.joined->SetVisible(w.middle, true);
    ArrangeSubtree(w.content.get(), fonts);
    Require(Near(w.joined->Slot(w.first)->first, w.joined->Slot(w.first)->last),
            "entering rows begin behind a zero-height clip");
    for (int i = 0; i < 6; ++i) { w.content->Tick(1.0f / 60); NoOverlap(w.joined); }
    Settle(w);

    // A net-zero-height batch still needs its own shared clock.
    w.joined->SetVisible(w.middle, false);
    ArrangeSubtree(w.content.get(), fonts);
    Settle(w);
    const float sameHeight = Height(w.joined->drawn);
    w.joined->SetVisible(w.first, false);
    w.joined->SetVisible(w.middle, true);
    ArrangeSubtree(w.content.get(), fonts);
    for (int i = 0; i < 6; ++i) {
        w.content->Tick(1.0f / 60);
        Require(Near(Height(w.joined->drawn), sameHeight), "equal-height changes keep page geometry stable");
        NoOverlap(w.joined);
    }
    Settle(w);
    w.joined->SetVisible(w.first, true);
    ArrangeSubtree(w.content.get(), fonts);
    Settle(w);
    // Split/recombine runs while moving, including all-hidden and net-zero-height transitions.
    for (int step = 0; step < 24; ++step) {
        const unsigned mask = static_cast<unsigned>((step * 5) & 7);
        w.joined->SetVisible(w.first, (mask & 1) != 0);
        w.joined->SetVisible(w.middle, (mask & 2) != 0);
        w.joined->SetVisible(w.last, (mask & 4) != 0);
        ArrangeSubtree(w.content.get(), fonts);
        for (int frame = 0; frame < 3; ++frame) {
            w.content->Tick(1.0f / 60);
            NoOverlap(w.joined);
        }
    }
    w.joined->SetVisible(w.first, true);
    w.joined->SetVisible(w.middle, true);
    w.joined->SetVisible(w.last, true);
    ArrangeSubtree(w.content.get(), fonts);
    Settle(w);

    const int uid = w.field->uid;
    w.field->text = L"retained draft";
    ComPtr<IRawElementProviderSimple> header;
    header.Attach(new UiaElement(&w, w.expander->header->uid));
    ComPtr<IUnknown> pattern;
    Require(SUCCEEDED(header->GetPatternProvider(UIA_ExpandCollapsePatternId, &pattern)) && pattern,
            "header must expose the existing UIA ExpandCollapse provider");
    ComPtr<IExpandCollapseProvider> expand;
    Require(SUCCEEDED(pattern.As(&expand)), "ExpandCollapse interface");
    Require(SUCCEEDED(expand->Expand()), "UIA expand");
    ArrangeSubtree(w.content.get(), fonts);
    for (int frame = 0; frame < 6; ++frame) {
        w.content->Tick(1.0f / 60.0f);
        Require(Near(w.after->drawn.top - w.expander->drawn.bottom, 4),
                "reveal and following cards must share one geometry animation");
        Require(Near(Height(w.expander->ClipBox()), Height(w.expander->drawn)), "clip follows drawn height");
    }
    // Reverse while travelling: no restart from either endpoint.
    const float before = Height(w.expander->drawn);
    Require(SUCCEEDED(expand->Collapse()), "UIA collapse");
    ArrangeSubtree(w.content.get(), fonts);
    Require(Near(Height(w.expander->drawn), before), "reversal must be continuous");
    Require(!w.UiaFind(uid), "closing content leaves UIA immediately");
    Settle(w);
    Require(!w.expander->items->visible, "closed body stops painting and ticking");
    w.expander->header->OnKey(VK_RIGHT);
    ArrangeSubtree(w.content.get(), fonts);
    Settle(w);
    Require(w.field->uid == uid && w.field->text == L"retained draft", "collapse preserves object and edit");
    Require(w.UiaFind(uid) == w.field, "expanded body returns with the same automation identity");
    auto *oldLast = static_cast<CardGroupSlot *>(w.expander->items->children.back().get())->card;
    auto *added = w.expander->Add(new Card(L"Added at runtime"));
    ArrangeSubtree(w.content.get(), fonts);
    Settle(w);
    Require(oldLast->corners.r[2] == 0 && added->corners.r[2] == 4,
            "adding a card transfers the bottom corners");
    w.expander->items->Remove(added);
    ArrangeSubtree(w.content.get(), fonts);
    Settle(w);
    Require(oldLast->corners.r[2] == 4 && w.field->uid == uid,
            "removing a card restores the outline without rebuilding siblings");
    std::vector<Card *> batch;
    for (int i = 0; i < 3; ++i) batch.push_back(w.expander->Add(new Card(L"Batch")));
    ArrangeSubtree(w.content.get(), fonts);
    Settle(w);
    const size_t oldCount = w.expander->items->children.size();
    for (auto *card : batch) w.expander->items->Remove(card);
    ArrangeSubtree(w.content.get(), fonts);
    Require(w.expander->items->children.size() == oldCount, "batch removal keeps all painting tails");
    for (int i = 0; i < 6; ++i) {
        w.content->Tick(1.0f / 60);
        NoOverlap(w.expander->items);
        auto *a = w.expander->items->Slot(batch[0]);
        auto *b = w.expander->items->Slot(batch[1]);
        Require(a && b && Near(b->drawn.top - a->drawn.top, a->height + 1),
                "batch destruction uses one block rather than the last removal's animation");
    }
    Settle(w);
    Require(w.expander->items->children.size() == oldCount - 3, "batch is destroyed only after its exit");
    {
        View fixed;
        fixed.SetLayout(new RowLayout()); // stretch allocates more height than the group's content
        fixed.rect = { 0, 0, 400, 300 };
        auto *group = fixed.Add(new CardGroup());
        auto *row = group->Add(new Card(L"Fixed allocation"));
        group->Add(new Card(L"Still here"));
        ArrangeSubtree(&fixed, fonts);
        PlaceSubtree(&fixed);
        group->SetVisible(row, false);
        ArrangeSubtree(&fixed, fonts);
        for (int i = 0; i < 240 && fixed.Animating(); ++i) fixed.Tick(1.0f / 60);
        Require(!fixed.Animating() && !group->Slot(row)->visible,
                "a stretched group must finish using the local clock");
    }
    ComPtr<IValueProvider> cachedField;
    {
        ComPtr<IRawElementProviderSimple> element;
        element.Attach(new UiaElement(&w, uid));
        ComPtr<IUnknown> value;
        Require(SUCCEEDED(element->GetPatternProvider(UIA_ValuePatternId, &value)) && value,
                "retained field exposes Value through the real provider");
        Require(SUCCEEDED(value.As(&cachedField)), "Value interface");
    }

    std::vector<Widget *> tab;
    w.CollectTab(w.content.get(), tab);
    Require(std::find(tab.begin(), tab.end(), w.field) != tab.end(), "open body participates in Tab");
    const auto origin = w.OriginOf(w.headerSwitch);
    const auto r = w.headerSwitch->drawn;
    Require(w.HitTest(origin.x + (r.left + r.right) / 2, origin.y + (r.top + r.bottom) / 2)
            == w.headerSwitch, "header child receives its own pointer input");
    w.SetFocusTo(w.field);
    w.expander->SetExpanded(false);
    Require(w.focused == w.expander->header, "collapsing returns focus from content to header");
    BSTR hiddenText = nullptr;
    Require(cachedField->get_Value(&hiddenText) == UIA_E_ELEMENTNOTAVAILABLE,
            "a cached provider cannot read a logically collapsed descendant");
    SysFreeString(hiddenText);
    tab.clear();
    w.CollectTab(w.content.get(), tab);
    Require(std::find(tab.begin(), tab.end(), w.field) == tab.end(), "closing content leaves Tab immediately");
    ArrangeSubtree(w.content.get(), fonts);
    Settle(w);

    Animations(false);
    w.expander->SetExpanded(true);
    w.content->rect.right = 500;
    ArrangeSubtree(w.content.get(), fonts);
    w.content->Tick(0);
    Require(Near(Width(w.joined->rect), 452) && Near(w.joined->rect.left, 24), "narrow column fills its margin");
    Require(Near(Height(w.expander->drawn), Height(w.expander->rect)), "disabled motion settles immediately");
    w.expander->SetExpanded(false);
    ArrangeSubtree(w.content.get(), fonts);
    w.content->Tick(0);
    Require(!w.expander->items->visible && !w.content->Animating(), "disabled closing settles and sleeps");
    AnimationsAuto();
    std::puts("PASS: file fonts, corners, gutters, clipped reversible rows, batch exit/removal, equal-height batch, width cap, input, retained edit, UIA, reduced motion");
}

// A real Direct2D target, not a second implementation of the controls for screenshots.
HRESULT Render(Cards &w, Fonts &fonts, const wchar_t *path, float width, float height, float dpi) {
    const Graphics &g = Device();
    if (!g.d2dDevice) return E_FAIL;
    ComPtr<ID2D1DeviceContext> dc;
    HRESULT hr = g.d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc);
    if (FAILED(hr)) return hr;
    const UINT pw = (UINT)std::ceil(width * dpi / 96), ph = (UINT)std::ceil(height * dpi / 96);
    const auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
    ComPtr<ID2D1Bitmap1> target, cpu;
    if (FAILED(hr = dc->CreateBitmap(D2D1::SizeU(pw, ph), nullptr, 0, props, &target))) return hr;
    dc->SetDpi(dpi, dpi);
    dc->SetTarget(target.Get());
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(hr = dc->CreateSolidColorBrush(Rgb(0), &brush))) return hr;
    Palette palette = MakePalette(DarkTheme());
    Painter painter;
    painter.rt = dc.Get(); painter.br = brush.Get(); painter.font = &fonts; painter.pal = &palette;
    dc->BeginDraw();
    dc->Clear(palette.windowBg);
    PaintTree(painter, w.content.get(), 0, 0);
    if (FAILED(hr = dc->EndDraw())) return hr;
    dc->SetTarget(nullptr);
    auto read = props;
    read.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    if (FAILED(hr = dc->CreateBitmap(D2D1::SizeU(pw, ph), nullptr, 0, read, &cpu))) return hr;
    if (FAILED(hr = cpu->CopyFromBitmap(nullptr, target.Get(), nullptr))) return hr;
    D2D1_MAPPED_RECT mapped = {};
    if (FAILED(hr = cpu->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return hr;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    if (SUCCEEDED(hr)) hr = wic->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path, GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(pw, ph);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr) && format != GUID_WICPixelFormat32bppBGRA) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = frame->WritePixels(ph, mapped.pitch, mapped.pitch * ph, mapped.bits);
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    cpu->Unmap();
    return hr;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    bool check = false, open = false, hideMiddle = false, showMiddle = false;
    bool batchHide = false, batchShow = false;
    const wchar_t *render = nullptr;
    float width = 1200, height = 820, dpi = 96;
    int frames = -1;
    for (int i = 1; i < argc; ++i) {
        if (!std::wcscmp(argv[i], L"--check")) check = true;
        if (!std::wcscmp(argv[i], L"--open")) open = true;
        if (!std::wcscmp(argv[i], L"--hide-middle")) hideMiddle = true;
        if (!std::wcscmp(argv[i], L"--show-middle")) showMiddle = true;
        if (!std::wcscmp(argv[i], L"--batch-hide")) batchHide = true;
        if (!std::wcscmp(argv[i], L"--batch-show")) batchShow = true;
        if (!std::wcscmp(argv[i], L"--dark")) Theme(ThemeMode::Dark);
        if (!std::wcscmp(argv[i], L"--light")) Theme(ThemeMode::Light);
        if (!std::wcscmp(argv[i], L"--render") && i + 1 < argc) render = argv[++i];
        if (!std::wcscmp(argv[i], L"--width") && i + 1 < argc) width = (float)_wtof(argv[++i]);
        if (!std::wcscmp(argv[i], L"--dpi") && i + 1 < argc) dpi = (float)_wtof(argv[++i]);
        if (!std::wcscmp(argv[i], L"--frames") && i + 1 < argc) frames = _wtoi(argv[++i]);
    }
#if MICULA_DEBUG_LAYOUT
    debug::layout = false;
#endif
    if (!check && !render) {
        DWORD owners = 0;
        if (GetConsoleProcessList(&owners, 1) == 1) FreeConsole();
        Cards w;
        w.Build(width, height);
        w.expander->SetExpanded(open);
        return w.Create((int)width, (int)height, true, nullptr) ? w.Run() : 1;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int code = 0;
    {
        ComPtr<IDWriteFactory> dw;
        Fonts fonts;
        HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                        reinterpret_cast<IUnknown **>(dw.GetAddressOf()));
        if (FAILED(hr) || !fonts.Create(dw.Get())) code = 1;
        else {
            try {
                if (check) Check(fonts);
                if (render) {
                    Cards w;
                    w.Build(width, height);
                    ArrangeSubtree(w.content.get(), fonts);
                    PlaceSubtree(w.content.get());
                    w.expander->SetExpanded(open);
                    ArrangeSubtree(w.content.get(), fonts);
                    if (hideMiddle || showMiddle || batchHide || batchShow) {
                        Settle(w);
                        if (showMiddle || batchShow) {
                            w.joined->SetVisible(w.middle, false);
                            if (batchShow) w.joined->SetVisible(w.first, false);
                            ArrangeSubtree(w.content.get(), fonts);
                            Settle(w);
                        }
                        w.joined->SetVisible(w.middle, showMiddle || batchShow);
                        if (batchHide || batchShow) w.joined->SetVisible(w.first, batchShow);
                        ArrangeSubtree(w.content.get(), fonts);
                    }
                    if (frames < 0) Settle(w);
                    else for (int i = 0; i < frames; ++i) w.content->Tick(1.0f / 60.0f);
                    hr = Render(w, fonts, render, width, height, dpi);
                    if (FAILED(hr)) { std::printf("render failed: 0x%08lx\n", (unsigned long)hr); code = 1; }
                }
            } catch (const std::exception &e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); code = 1; }
        }
        fonts.Release();
    }
    CoUninitialize();
    return code;
}
