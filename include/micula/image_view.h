// Micula / image_view.h

#pragma once

#include "window.h"

#ifdef _WIN32
#include <algorithm>
#include <string>
#include <utility>

namespace micula {

// A static image from disk, centered and fitted inside its layout rectangle.
struct ImageView : Widget {
    ImageView(std::wstring path, float width, float height, UINT maxDecodeWidth = 0)
        : path_(std::move(path)), width_(width), height_(height),
          maxDecodeWidth_(maxDecodeWidth) {}

    void SetSource(std::wstring path) {
        path_ = std::move(path);
        Invalidate();
    }

    micula::Want Measure(const Room &) const override {
        return micula::Want(Axis::Fixed(width_), Axis::Fixed(height_));
    }

    void Paint(const Painter &p) override {
        const float width = rect.right - rect.left;
        const float height = rect.bottom - rect.top;
        auto *owner = surface();
        if (!owner || p.rt != owner->dc || width <= 0 || height <= 0) return;

        auto *bitmap = owner->Image(path_, maxDecodeWidth_);
        if (!bitmap) return;

        const auto size = bitmap->GetSize();
        const float scale = std::min(width / size.width, height / size.height);
        const float imageWidth = size.width * scale;
        const float imageHeight = size.height * scale;
        const float x = rect.left + (width - imageWidth) * 0.5f;
        const float y = rect.top + (height - imageHeight) * 0.5f;
        const auto dest = D2D1::RectF(x, y, x + imageWidth, y + imageHeight);
        p.rt->DrawBitmap(bitmap, &dest, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR);
    }

private:
    std::wstring path_;
    float width_;
    float height_;
    UINT maxDecodeWidth_;
};

}  // namespace micula
#endif
