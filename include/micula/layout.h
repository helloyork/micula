#pragma once

// The layout protocol: what a control says it wants, what a layout does with the answer, and the
// numbers a settings page is made of.
//
// A Layout belongs to the container it arranges (`Widget::layout`) and it is the only thing that
// decides where that container's children go. Nothing here runs inside a control's callback: a
// change marks the tree dirty and the window arranges once, at a defined point in the frame, before
// it paints. That is what makes a retained tree cheap -- ten changes in one message are one arrange
// -- and it is also why a callback may add, remove, hide or restyle anything it likes: the tree it
// is running in is never rearranged under it.

#include <d2d1.h>

#include <algorithm>
#include <cmath>

// Compile with -DMICULA_DEBUG_LAYOUT=1 and the window draws what the layouts worked out, over the
// page: the rectangle each widget was arranged into (red), the clip it is painted under (blue), a
// container's content box inside its own padding (orange), and the rectangle a widget is gliding
// toward while it is not there yet (magenta). None of it exists in a build without the flag -- not
// the code, not the colours -- and a build that has it can still turn it off:
//
//     micula::debug::layout = false;
//
// A page is mostly its layout, and a layout is arithmetic; this is the arithmetic drawn where it can
// be disagreed with. Nothing here is a replace for a probe that prints the rectangles, which is what
// a real fault wants -- it is a replace for taking a screenshot of a window to find out which box is
// wrong.
#ifndef MICULA_DEBUG_LAYOUT
#define MICULA_DEBUG_LAYOUT 0
#endif

namespace micula {

struct Fonts;
struct Spec;
struct Widget;

namespace debug {
#if MICULA_DEBUG_LAYOUT
inline bool layout = true;
#endif
}  // namespace debug

// How a control wants one axis of itself treated. Three answers, and they are the whole vocabulary:
//
//   Content -- as big as its content measures. `size` carries that measurement, and it is a
//              preference: a parent may give it more, or less.
//   Fill    -- as big as there is. `size` is ignored, and in a stack a Fill child shares what is
//              left over with the other Fill children, which is what makes a page reach the bottom
//              of its window.
//   Fixed   -- exactly `size`. A parent honours it: a square progress ring, a control that is one
//              switch wide.
enum class Sizing { Content, Fill, Fixed };

struct Axis {
    Sizing how = Sizing::Content;
    float size = 0.0f;

    static Axis Content(float measured) { return { Sizing::Content, measured }; }
    static Axis Fill() { return { Sizing::Fill, 0.0f }; }
    static Axis Fixed(float size) { return { Sizing::Fixed, size }; }
};

struct Want {
    Axis w, h;

    Want() = default;
    Want(Axis width, Axis height) : w(width), h(height) {}
    Want(float width, float height) : w(Axis::Content(width)), h(Axis::Content(height)) {}
    Want(Sizing width, Sizing height) : w{ width, 0.0f }, h{ height, 0.0f } {}
};

// The room on offer, and what is needed to answer with a measurement.
//
// `width` is what the parent can give on the axis a layout flows along: for a column of children
// that is the column's content width, and it is the width a wrapping label has to wrap at. `height`
// is 0 while the parent does not know its own yet -- measure runs from the leaves up, so a parent
// that fills its window measures its children before it has been told how tall it is.
struct Room {
    const Fonts *fonts = nullptr;
    const Spec *spec = nullptr;
    float width = 0.0f;
    float height = 0.0f;
};

// The numbers a settings page is made of, taken from Windows Settings itself: a page margin of 24,
// cards 64 DIP high and 4 apart, a control 32 high. They live here rather than in every page because
// they are the platform's design rather than the page's -- and a page that wants its own has one
// struct to say so with, which is a layout field away:
//
//     layout->spec.pagePad = 16.0f;
//
// A theme change (DPI, density) reloads the window's own, and the tree is arranged again.
struct Spec {
    float pagePad = 24.0f;      // left and right margin of a page
    float gap = 4.0f;           // between the rows of a stack
    float controlH = 32.0f;     // one control: a button, a field, a switch
    float rowGap = 8.0f;        // between the controls of one row
    float headingTop = 24.0f;   // above a heading -- the band a heading widget carries of its own
    float headingH = 30.0f;     // a heading's own height, below that
    float labelH = 20.0f;       // one line of body text
    float cardH = 64.0f;        // one settings card
    float cardPad = 16.0f;      // inside a card, left and right
    float cardGap = 16.0f;      // between a card's text and its control
};

// A layout: measure the host's children, then place them. It is asked for a measurement when the
// host's own parent needs a size for the host, and for an arrangement when the host has a box.
struct Layout {
    virtual ~Layout() {}

    // What the host's children want, as the host's own size. The default is "give me the room",
    // which is the honest answer for a layout that has not thought about it.
    virtual Want Measure(const Room &room) const {
        (void)room;
        return Want(Sizing::Fill, Sizing::Fill);
    }

    // Place every child inside `box`, in the host's own space. Padding is this layout's business,
    // not the caller's: `box` is the whole host. The room comes along because arranging is also
    // where a child is measured against the width it is really being given -- a wrapping line has
    // to wrap at the width it ends up with, not at the one it was offered.
    //
    // The caller follows this with `Glide(0)`, which is what places a child that has never been
    // placed: nothing glides on the first arrangement.
    virtual void Arrange(const Room &room, const D2D1_RECT_F &box) = 0;

    // The part of the host's box this layout places children in: its padding is off. The default is
    // the whole box, and a layout with padding says so here -- which is what lets the debug pass
    // draw the content box, and a hit test ask what the room really is, without every layout having
    // to answer the question twice.
    virtual D2D1_RECT_F ContentBox(const D2D1_RECT_F &box) const { return box; }

    // The animations this layout owns. Its default body glides each child from where it was drawn
    // toward the rectangle this layout arranged for it, which is what makes a re-arrangement move
    // rather than jump; see Widget::drawn. Defined in widget.h, beside the node it walks.
    virtual void Tick(float dt);

    // Ask for another arrangement. Cheap and idempotent: the window arranges once per frame, so a
    // callback that invalidates ten times arranges once.
    void Invalidate();

    Widget *host() const { return host_; }

    // The numbers. Edit in place before the first arrange, or replace the struct.
    Spec spec;

    // The glide's time constant and how close counts as arrived -- see motion::Follow, which is
    // what this is handed. A layout that wants everything to move at its own pace sets its own.
    float glideLag = 0.07f;
    float glideSnap = 0.5f;

    // One step of the glide, for every child, and whether anything is still moving.
    bool Glide(float dt);

protected:
    Widget *host_ = nullptr;   // set by Widget::SetLayout
    friend struct Widget;
};

}  // namespace micula
