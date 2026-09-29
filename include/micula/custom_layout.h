#pragma once

// A layout whose arrangement is a function: the escape hatch for a page with one odd row, and what
// today's `Window::Layout()` becomes for a page that would rather keep doing its own arithmetic. The
// difference is when it runs -- when the window arranges, with the room the host really has, and
// never while a message is being handled, which is the part of the old model that hurt.
//
//     page->SetLayout(new CustomLayout([](Widget *host, const Room &room, const D2D1_RECT_F &box) {
//         float y = box.top;
//         for (auto &child : host->children) {
//             child->rect = { box.left, y, box.right, y + room.spec->controlH };
//             y += room.spec->controlH + room.spec->gap;
//         }
//     }));
//
// A custom host asks to fill unless it says otherwise: what a function is going to arrange cannot be
// known from the outside, so there is nothing honest to measure.

#include "widget.h"

#include <functional>
#include <utility>

namespace micula {

struct CustomLayout : Layout {
    using ArrangeFn = std::function<void(Widget *host, const Room &room, const D2D1_RECT_F &box)>;

    explicit CustomLayout(ArrangeFn fn) : arrange(std::move(fn)) {}

    void Arrange(const Room &room, const D2D1_RECT_F &box) override {
        if (arrange) arrange(host_, room, box);
    }

    // What the host wants, for a page that would rather not fill: set it in the same breath as the
    // function, since only the page knows.
    Want measure = { Sizing::Fill, Sizing::Fill };
    Want Measure(const Room & /*room*/) const override { return measure; }

    ArrangeFn arrange;
};

}  // namespace micula
