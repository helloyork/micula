#pragma once

// A widget that is only a box with children in it: what a page has its layout arrange, and what a
// `Card` puts its control in. It draws nothing of its own -- a page's backgrounds and its headings
// are widgets too -- so a page is a View with a layout and children rather than a rectangle with a
// paint call.
//
//     panel->SetLayout(new StackLayout());
//     panel->Add(new Heading(L"Choices"));

#include "widget.h"

namespace micula {

struct View : Widget {
    // A View takes the room it is given, which is what a box in a layout wants; a page's own
    // measurement comes from the layout inside it.
    void Paint(const Painter & /*p*/) override {}
};

}  // namespace micula
