#pragma once

// The icon slot: a `Glyph`, or something a page draws itself.
//
// **A glyph costs nothing.** `Icon` holds one pointer for the common case, exactly as the
// `const wchar_t *` slots did, so a page that fills the slot with `glyph::kSettings` is not
// paying for the abstraction. Copying one is copying a pointer and, when there is a shape behind
// it, a shared count -- which is what lets a `NavItem` live in a `std::vector` and be rebuilt
// every time a pane opens.
//
// **A page with an icon of its own subclasses `IconShape`, not `Icon`.** An `Icon` is kept *by
// value* -- `Card::icon`, `Button::glyph`, `NavItem::glyph`, a menu's row -- and a class with
// virtual functions that is copied by value drops the override without a word: measured, MSVC
// warns about nothing at `/W4`. `IconShape` sits behind a `shared_ptr` instead, so there is
// nothing to slice and nothing to keep alive by hand.
//
//     struct Badge : IconShape {
//         void Paint(const Painter &p, const D2D1_RECT_F &box, const D2D1_COLOR_F &colour,
//                    IDWriteTextFormat *) const override { ... }
//         float Width(const Fonts &, IDWriteTextFormat *) const override { return 16.0f; }
//     };
//     card->icon = std::make_shared<Badge>();
//
// **The format is a parameter, because the control picks the size**: a card draws at
// `Fonts::icon`, a scroll bar's arrow at `Fonts::iconTiny`, the caption buttons at
// `Fonts::iconSmall`. A glyph is drawn at whatever it is handed; a shape with its own idea of
// its size ignores it.
//
// **Why this is not one type.** The constants have to stay `inline constexpr`, and a
// `shared_ptr` is not a literal type: an `Icon` that held one could not be a constant, so all 75
// of them would be built at run time, in every translation unit that includes the header.
// `Glyph` is the part that stays a constant; this is the part a control holds.

#include "glyphs.h"   // `Glyph` and `glyph::Set`: the two things a slot can be filled with
#include "window.h"   // `Painter` and `Fonts`, which an icon is drawn and measured with

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace micula {

// An icon a page draws itself. Two questions, which are the two a control asks of an icon: how
// wide it wants to be, and how to put it in a box.
//
// Both are `const`, and both are asked while a frame is being drawn with a `Painter` that was
// made for it -- so a shape may cache what is expensive (a geometry, a path) and must not keep
// anything that belongs to the device.
struct IconShape {
    virtual ~IconShape() = default;
    virtual void Paint(const Painter &p, const D2D1_RECT_F &box, const D2D1_COLOR_F &colour,
                       IDWriteTextFormat *glyphFormat) const = 0;
    virtual float Width(const Fonts &fonts, IDWriteTextFormat *glyphFormat) const = 0;

    // **The three a shape adds if it has more than one face.** A still icon answers 0, refuses the
    // change, and never wants a frame -- which is why the forwarding in `Icon` costs nothing for
    // the icons that are one glyph and nothing else.
    virtual int Face() const { return 0; }
    virtual bool SetFace(int /*face*/) { return false; }
    // Whether the next frame would look different. A control's `Animating()` asks this, and it is
    // what keeps the frame loop turning while a face of a set is on its way from one to the next.
    virtual bool AnimationWanted() const { return false; }
};

// A `Set` with a face: what a slot holds when its icon is one of several -- the battery it is
// showing, the alignment it has chosen.
//
// **The index lives here, in the one allocation this slot made**, rather than in `Icon`: a glyph
// icon and an icon of the page's own stay 24 bytes, and only a slot that really is a set pays for
// being able to move. That is what "forwarded" means here -- `Icon::Face(n)` reaches the set, and
// nothing about it is carried by the 99% of icons that are one code point.
//
// **One per slot, never shared.** Two icons made from the same set are two objects with two
// indices; sharing one would make every battery in the window show the same level.
struct IconSet : IconShape {
    explicit IconSet(const glyph::Set &s) : set(s) {}
    glyph::Set set;
    int index = 0;

    Glyph Current() const { return set.at(index); }
    void Paint(const Painter &p, const D2D1_RECT_F &box, const D2D1_COLOR_F &colour,
               IDWriteTextFormat *glyphFormat) const override {
        p.Text(Current(), box, glyphFormat, colour);
    }
    float Width(const Fonts &fonts, IDWriteTextFormat *glyphFormat) const override {
        const Glyph g = Current();
        if (g.empty()) return 0.0f;
        return fonts.Measure(glyphFormat, std::wstring(g.Code()));
    }
    int Face() const override { return index; }
    bool SetFace(int face) override {
        index = set.count > 0 ? (face < 0 ? 0 : (face < set.count ? face : set.count - 1)) : 0;
        return true;
    }
};

class Icon {
public:
    Icon() = default;
    // A slot with nothing in it, said out loud: a menu row that wants a shortcut, a card with no
    // icon. `nullptr` can only mean "none" in this position -- it cannot be confused with words --
    // so it is allowed where a `const wchar_t *` no longer is.
    Icon(std::nullptr_t) {}
    // Implicit on purpose: a `Glyph` is already the type that cannot be mistaken for words, so an
    // `Icon(...)` at the call site would say nothing that the argument does not.
    Icon(Glyph g) : glyph_(g) {}
    Icon(std::shared_ptr<IconShape> shape) : shape_(std::move(shape)) {}
    // A set. Implicit for the same reason, and it is the one form that allocates -- once, for this
    // slot, which is what keeps every other icon free.
    Icon(const glyph::Set &s) : shape_(std::make_shared<IconSet>(s)) {}

    void Paint(const Painter &p, const D2D1_RECT_F &box, const D2D1_COLOR_F &colour,
               IDWriteTextFormat *glyphFormat) const {
        if (shape_) shape_->Paint(p, box, colour, glyphFormat);
        else if (!glyph_.empty()) p.Text(glyph_, box, glyphFormat, colour);
    }

    // How wide it wants to be: what a control asks before it knows where the words beside it go.
    // A glyph answers with its measured advance, which is the pair already kept in `Fonts`.
    float Width(const Fonts &fonts, IDWriteTextFormat *glyphFormat) const {
        if (shape_) return shape_->Width(fonts, glyphFormat);
        if (glyph_.empty()) return 0.0f;
        return fonts.Measure(glyphFormat, std::wstring(glyph_.Code()));
    }

    // Nothing to draw. What `Card::HasWording` asks, and what an empty slot answers.
    bool empty() const { return !shape_ && glyph_.empty(); }
    explicit operator bool() const { return !empty(); }

    // Which face of the set this slot is showing, and the way to move it. False from `Face(int)`
    // when the slot holds one glyph or an icon of the page's own -- there is nothing to move.
    //
    //     card->icon = glyph::battery::Levels;
    //     card->icon.Face(percent / 10);
    bool Face(int face) { return shape_ && shape_->SetFace(face); }
    int  Face() const { return shape_ ? shape_->Face() : 0; }

    // Ask the one a control forwards to its own `Animating()`: it is the seam a set that moves by
    // itself will use, and it answers false for everything that is drawn still.
    bool AnimationWanted() const { return shape_ && shape_->AnimationWanted(); }

private:
    Glyph glyph_{};
    std::shared_ptr<IconShape> shape_;
};

}  // namespace micula
