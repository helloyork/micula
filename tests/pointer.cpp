#include <micula/side_nav.h>
#include <micula/drop_down.h>
#include <cstdio>
#include <cstdlib>

using namespace micula;
static void Require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

// No HWND or graphics device: use the real dispatch and animation state.
int main() {
    {
        int changes=0;
        DropDown dd({L"One",L"Two",L"Three"},1,[&](int){++changes;});
        ScrollView scroll;
        scroll.rect=Rect(0,0,200,100);scroll.extent=1000;
        dd.scroll=&scroll;
        DropDownList list(&dd);
        Require(list.OnWheel(0,0,-1) && dd.selected==2 && changes==1,
                "Windows popup wheel must advance the choice exactly once");
        Require(scroll.scroll==0, "Windows wheel must not become an independent list scroll");
        Require(list.OnWheel(0,0,-1) && dd.selected==2 && changes==1,
                "Windows popup must consume wheel at the last choice without wrapping");
        Require(list.OnWheel(0,0,1) && dd.selected==1 && changes==2,
                "Windows reverse wheel must restore the preceding choice");
        dd.enabled=false;scroll.ScrollTo(0,false);
        Require(list.OnWheel(0,0,-1) && scroll.scroll==0 && dd.selected==1 && changes==2,
                "disabled popup must consume without scrolling or changing selection");
        dd.scroll=nullptr;
    }
    for (auto hand : {Surface::Hand::Finger, Surface::Hand::Pen, Surface::Hand::Mouse}) {
        Surface host;
        host.content = std::make_unique<View>();
        host.content->host = &host;
        host.content->rect = Rect(0, 0, 500, 400);
        auto *pane = host.content->Add(new SideNav());
        pane->rect = Rect(0, 0, 184, 400);
        pane->style = PaneStyle::Toggle;
        pane->items = {{glyph::kHome, L"First"}, {glyph::kSettings, L"Second"}};
        pane->Tick(.05f);
        Widget::Item item;
        Require(pane->AccessibleItem(1, item), "second navigation row must exist");
        const float x = (item.box.left + item.box.right) / 2;
        const float y = (item.box.top + item.box.bottom) / 2;
        host.PressAt(x, y, hand);
        pane->Tick(.05f);
        Require(pane->pressed && pane->selected == 0, "press must precede selection");
        Require(pane->HotRow() == 1 && pane->hot[1] > 0 && pane->hot[0] == 0,
                "a navigation row must show its press without prior hover");
        host.ReleaseAt(hand);
        Require(!pane->pressed && pane->selected == 1, "release must select the pressed row");
        pane->Tick(.05f);
        pane->Tick(.05f);
        Require(pane->HotRow() == -1, "release must not leave a touch hover behind");

        // Hover and exit use the same retained tree as pointer input.
        host.pointerX = x; host.pointerY = y;
        host.HoverTo(pane);
        host.SendMove(host.content.get(), x, y, pane);
        pane->Tick(.05f);
        Require(pane->hover && pane->HotRow() == 1, "hover must highlight the row under the pointer");
        host.HoverTo(nullptr);
        pane->Tick(.05f); pane->Tick(.05f);
        Require(!pane->hover && pane->HotRow() == -1 && pane->hot[1] == 0,
                "hover exit must clear the row highlight");
    }
    std::puts("PASS: navigation press before selection and hover/exit tree state");
}
