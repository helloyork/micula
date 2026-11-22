// Exercise the Windows mouse/touch/pen dispatch without creating a window.
#include <micula/toggle_switch.h>
#include <cstdio>
#include <cstdlib>

using namespace micula;

static void Require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

struct Gesture {
    Surface host;
    ToggleSwitch *toggle;
    Surface::Hand hand;
    int changes = 0;
    float x, y = 26;
    Gesture(Surface::Hand hand, bool on) : hand(hand) {
        host.content = std::make_unique<View>();
        host.content->host = &host;
        host.content->rect = Rect(0, 0, 100, 100);
        toggle = host.content->Add(new ToggleSwitch(L"", on, [this](bool) { ++changes; }));
        toggle->rect = Rect(10, 10, 50, 42);
        x = toggle->KnobX();
    }
    void Press() {
        host.PressAt(x, y, hand);
        Require(host.capture == toggle, "pointer must hit the switch");
    }
    void Move(float dx, float dy = 0) { host.MoveTo(x + dx, y + dy, hand, true); }
    void Release() { host.ReleaseAt(hand); }
};

int main() {
    for (auto hand : {Surface::Hand::Mouse, Surface::Hand::Finger, Surface::Hand::Pen}) {
        for (bool on : {false, true}) {
            for (float jitter : {0.0f, 0.5f, 3.0f}) {
                Gesture g(hand, on);
                g.Press();
                g.Move(jitter, jitter);
                g.Move(-jitter, -jitter);
                g.Release();
                Require(g.toggle->on != on && g.changes == 1,
                        "a stationary/jittery knob tap must toggle exactly once");
            }
            {
                Gesture g(hand, on);
                g.Press(); g.Release();
                Require(g.toggle->on != on && g.changes == 1, "a tap without move must toggle");
            }
            {
                Gesture g(hand, on);
                g.Press(); g.Move(on ? -6.0f : 6.0f); g.Release();
                Require(g.toggle->on == on && g.changes == 0,
                        "a drag below the state-flip threshold must not become a tap");
            }
            {
                Gesture g(hand, on);
                g.Press(); g.Move(on ? -16.0f : 16.0f); g.Release();
                Require(g.toggle->on != on && g.changes == 1,
                        "a completed drag must change state exactly once");
            }
            {
                Gesture g(hand, on);
                g.Press(); g.Move(on ? -16.0f : 16.0f); g.Move(0); g.Release();
                Require(g.toggle->on == on && g.changes == 2,
                        "returning a drag to its starting point must not add a click");
            }
            {
                Gesture g(hand, on);
                g.Press(); g.Move(0, 6); g.Release();
                Require(g.toggle->on == on && g.changes == 0,
                        "vertical travel beyond tap slop must not toggle");
            }
            {
                Gesture g(hand, on);
                g.x = on ? 20.0f : 40.0f;
                g.Press(); g.Move(0.5f, 0.5f); g.Release();
                Require(g.toggle->on != on && g.changes == 1,
                        "the empty end of the track must remain clickable");
            }
        }
    }
    std::puts("PASS: Windows mouse/finger/pen knob taps, jitter, partial/full/reversed drags and track clicks");
}
