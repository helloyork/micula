#pragma once

// Cards paint themselves. The group assigns their outline and keeps each card in a clipping slot.
// Consecutive entering/leaving cards travel as one block behind a shared window, so translucent
// surfaces never reveal a neighbouring row through them.

#include "card.h"
#include "view.h"

#include <type_traits>

namespace micula {

struct CardGroup;

struct CardGroupSlot : View {
    Card *card;
    bool shown = true, retiring = false, hadPixels = false, fromShown = false, targetShown = false;
    float height = 0, fromTop = 0, toTop = 0;
    float fromFirst = 0, fromLast = 0, toFirst = 0, toLast = 0;
    float first = 0, last = 0; // the shared window, in the group's space
    float paintFirst = 0, paintLast = 0;

    explicit CardGroupSlot(Card *value) : card(Widget::Add(value)) {}
    bool Clips() const override { return true; }
    D2D1_RECT_F ClipBox() const override { return { rect.left, paintFirst, rect.right, paintLast }; }
    bool Covers(float x, float y) const override {
        return Inside(rect, x, y) && Inside(ClipBox(), x, y);
    }
    bool AccessibleVisible() const override { return visible && shown && card->visible; }
    bool AccessibleControlElement() const override { return false; }
};

struct CardGroupLayout : Layout {
    float radius = metric::kRadiusControl;
    // An explicit Settings-style choice: a 1-DIP clear gutter, not a doubled border.
    float spacing = 1.0f;
    bool topConnected = false;
    float arrangedWidth = -1, arrangedRadius = -1, arrangedSpacing = -1;
    bool arrangedConnected = false;
    Want Measure(const Room &room) const override;
    void Arrange(const Room &room, const D2D1_RECT_F &box) override;
    void Tick(float dt) override;
};

struct CardGroup : View {
    CardGroup() { SetLayout(new CardGroupLayout()); }
    CardGroupLayout &GroupLayout() { return *static_cast<CardGroupLayout *>(layout.get()); }
    const CardGroupLayout &GroupLayout() const {
        return *static_cast<const CardGroupLayout *>(layout.get());
    }
    // An expander supplies its reveal height; an ordinary group follows its own drawn height.
    std::function<float()> presentationHeight;

    template <typename T> T *Add(T *card) {
        static_assert(std::is_base_of<Card, T>::value, "CardGroup accepts Cards and Card subclasses");
        BeginChange();
        Widget::Add(new CardGroupSlot(card));
        return card;
    }
    CardGroupSlot *Slot(const Card *card) const {
        for (const auto &child : children) {
            auto *slot = static_cast<CardGroupSlot *>(child.get());
            if (slot->card == card) return slot;
        }
        return nullptr;
    }
    bool IsVisible(const Card *card) const {
        const auto *slot = Slot(card);
        return slot && slot->shown && slot->card->visible;
    }
    void SetVisible(Card *card, bool show) {
        auto *slot = Slot(card);
        if (!slot || slot->retiring || slot->shown == show) return;
        BeginChange();
        slot->shown = show;
        if (show && !slot->visible) UnplaceSubtree(card);
        slot->enabled = show && card->visible;
        if (!show) Withdraw(slot);
        InvalidateLayout();
    }
    // Retains the card until its clipped exit completes. Repeated removals coalesce at arrangement.
    void Remove(Card *card) {
        auto *slot = Slot(card);
        if (!slot || slot->retiring) return;
        SetVisible(card, false);
        slot->retiring = true;
        InvalidateLayout();
    }
    bool Clips() const override { return true; }
    D2D1_RECT_F ClipBox() const override {
        const auto &at = placed ? drawn : rect;
        return { rect.left, rect.top, rect.left + Width(at), rect.top + Height(at) };
    }
    Corners ClipCorners() const override {
        const float r = GroupLayout().radius;
        return { GroupLayout().topConnected ? 0.0f : r,
                 GroupLayout().topConnected ? 0.0f : r, r, r };
    }
    int AccessibleType() const override { return UIA_GroupControlTypeId; }
    bool Animating() const override {
        if (moving_) return true;
        for (const auto &child : children)
            if (static_cast<const CardGroupSlot *>(child.get())->retiring) return true;
        return Widget::Animating();
    }
    void OnGeometryChanged() override { Present(); }
    void Tick(float dt) override {
        Widget::Tick(dt);
        Present();
        // No child-vector mutation during Widget::Tick's walk, and no callback is destroyed while
        // it is running: Widget::Remove retains its normal message-dispatch deferral.
        for (size_t i = children.size(); i-- > 0;) {
            auto *slot = static_cast<CardGroupSlot *>(children[i].get());
            if (slot->retiring && !slot->visible) Widget::Remove(slot);
        }
    }

private:
    bool ready_ = false, dirty_ = false, moving_ = false;
    float fromHeight_ = 0, toHeight_ = 0, phase_ = 1, travel_ = 1;

    float CurrentHeight() const {
        return presentationHeight ? presentationHeight() : Height(placed ? drawn : rect);
    }
    static float Blend(float a, float b, float t) { return a + (b - a) * t; }
    void BeginChange() {
        if (dirty_) return; // the first snapshot belongs to the whole batch, not its last mutation
        Present();
        fromHeight_ = CurrentHeight();
        for (const auto &child : children) {
            auto *slot = static_cast<CardGroupSlot *>(child.get());
            slot->hadPixels = slot->visible && slot->last > slot->first;
            slot->fromShown = slot->shown && slot->card->visible;
            slot->fromTop = slot->drawn.top;
            slot->fromFirst = slot->paintFirst;
            slot->fromLast = slot->paintLast;
        }
        dirty_ = true;
    }
    void Withdraw(CardGroupSlot *slot) {
        if (Surface *s = surface()) {
            s->DismissIn(slot);
            if (slot->Holds(s->capture)) s->CancelCapture();
            if (slot->Holds(s->focused)) {
                std::vector<Widget *> tab;
                s->CollectTab(this, tab);
                s->SetFocusTo(tab.empty() ? nullptr : tab.front());
            }
        }
    }
    void Present() {
        if (!ready_ || dirty_) return;
        float t = phase_;
        // A height-changing batch uses exactly the parent's geometry clock. This keeps the run's
        // viewport and the following page card together, without another follower chasing it.
        if (moving_ && parent && parent->layout && std::fabs(Height(rect) - toHeight_) < 0.02f &&
            std::fabs(toHeight_ - fromHeight_) > 0.5f)
            t = std::clamp((CurrentHeight() - fromHeight_) / (toHeight_ - fromHeight_), 0.0f, 1.0f);
        if (!Animations()) t = 1;
        if (t >= 1) { moving_ = false; phase_ = 1; }
        float previousBottom = -1.0e20f;
        for (const auto &child : children) {
            auto *slot = static_cast<CardGroupSlot *>(child.get());
            const float y = Blend(slot->fromTop, slot->toTop, t);
            slot->first = Blend(slot->fromFirst, slot->toFirst, t);
            slot->last = Blend(slot->fromLast, slot->toLast, t);
            slot->paintFirst = slot->first;
            slot->paintLast = slot->last;
            slot->rect = slot->drawn = { 0, y, Width(rect), y + slot->height };
            slot->placed = true;
            slot->visible = (slot->shown && slot->card->visible) || (t < 1 && slot->hadPixels);
            // A new entering run needs to paint as soon as its window opens.
            if (slot->shown && slot->card->visible) slot->visible = true;
            slot->enabled = slot->shown && slot->card->visible;
            // Splitting an in-flight run can make its natural rectangles overlap temporarily.
            // Partition the windows at the preceding row's painted edge; neither translucent
            // card ever paints on the other's pixels. A normal intact run needs no adjustment.
            if (slot->visible) {
                float top = (std::max)(slot->rect.top, slot->paintFirst);
                const float bottom = (std::min)(slot->rect.bottom, slot->paintLast);
                if (top < bottom) {
                    if (top < previousBottom) {
                        slot->paintFirst = (std::max)(slot->paintFirst, previousBottom);
                        slot->paintLast = (std::max)(slot->paintFirst, slot->paintLast);
                        top = previousBottom;
                    }
                    if (top < bottom) previousBottom = bottom;
                }
            }
        }
    }
    friend struct CardGroupLayout;
};

inline Want CardGroupLayout::Measure(const Room &room) const {
    float height = 0;
    int count = 0;
    for (const auto &child : host_->children) {
        const auto *slot = static_cast<const CardGroupSlot *>(child.get());
        if (!slot->shown || !slot->card->visible) continue;
        if (count++) height += spacing;
        height += slot->card->Measure(room).h.size;
    }
    return Want(Axis::Fill(), Axis::Content(height));
}

inline void CardGroupLayout::Arrange(const Room &room, const D2D1_RECT_F &box) {
    auto *group = static_cast<CardGroup *>(host_);
    Room inner = room;
    inner.width = Width(box);
    if (inner.width != arrangedWidth || radius != arrangedRadius || spacing != arrangedSpacing ||
        topConnected != arrangedConnected) group->BeginChange();
    for (const auto &child : host_->children) {
        auto *slot = static_cast<CardGroupSlot *>(child.get());
        if (slot->height != slot->card->Measure(inner).h.size ||
            slot->targetShown != (slot->shown && slot->card->visible)) group->BeginChange();
    }
    if (!group->dirty_) { group->Present(); return; }
    float y = 0;
    int count = 0;
    CardGroupSlot *first = nullptr, *last = nullptr;
    for (const auto &child : host_->children) {
        auto *slot = static_cast<CardGroupSlot *>(child.get());
        slot->height = slot->card->Measure(inner).h.size;
        slot->targetShown = slot->shown && slot->card->visible;
        if (!slot->shown || !slot->card->visible) continue;
        if (count++) y += spacing;
        if (!first) first = slot;
        last = slot;
        slot->toTop = slot->toFirst = y;
        slot->toLast = y + slot->height;
        y += slot->height;
    }
    group->toHeight_ = y;
    // Adjacent entering / leaving rows share one moving window and keep their natural offsets.
    // There is no per-operation animation to overwrite when a callback changes several cards.
    for (size_t i = 0; i < host_->children.size();) {
        auto *slot = static_cast<CardGroupSlot *>(host_->children[i].get());
        const bool show = slot->shown && slot->card->visible;
        const bool entering = show && (!slot->hadPixels || !slot->fromShown);
        if (show && !entering) { ++i; continue; }
        size_t end = i;
        float total = 0;
        while (end < host_->children.size()) {
            auto *next = static_cast<CardGroupSlot *>(host_->children[end].get());
            const bool nextShow = next->shown && next->card->visible;
            if (nextShow != show || (entering && next->hadPixels && next->fromShown)) break;
            if (end != i) total += spacing;
            total += next->height;
            ++end;
        }
        float anchor = 0;
        if (entering) {
            for (size_t p = 0; p < i; ++p) {
                auto *before = static_cast<CardGroupSlot *>(host_->children[p].get());
                if (before->hadPixels)
                    anchor = (std::max)(anchor, (std::min)(before->fromLast,
                        before->fromTop + before->height) + spacing);
            }
        } else {
            for (size_t p = 0; p < i; ++p) {
                auto *before = static_cast<CardGroupSlot *>(host_->children[p].get());
                if (before->shown && before->card->visible) anchor = before->toTop + before->height + spacing;
            }
        }
        float offset = 0;
        const float targetFirst = slot->toTop;
        for (size_t p = i; p < end; ++p) {
            auto *part = static_cast<CardGroupSlot *>(host_->children[p].get());
            if (entering) {
                if (!part->hadPixels) {
                    part->fromTop = anchor - total + offset;
                    part->fromFirst = part->fromLast = anchor;
                }
                part->toFirst = targetFirst;
                part->toLast = targetFirst + total;
            } else {
                part->toTop = anchor - total + offset;
                part->toFirst = part->toLast = anchor;
                // Fresh, fully shown runs can widen their clips to the whole block. On reversal,
                // preserve the current window exactly rather than revealing previously cut pixels.
                if (!group->moving_) {
                    auto *tail = static_cast<CardGroupSlot *>(host_->children[end - 1].get());
                    part->fromFirst = slot->fromTop;
                    part->fromLast = tail->fromTop + tail->height;
                }
            }
            offset += part->height + spacing;
        }
        i = end;
    }
    group->travel_ = std::fabs(group->toHeight_ - group->fromHeight_);
    for (const auto &child : host_->children) {
        auto *slot = static_cast<CardGroupSlot *>(child.get());
        if (slot->shown || slot->hadPixels) {
            group->travel_ = (std::max)(group->travel_, std::fabs(slot->toTop - slot->fromTop));
            group->travel_ = (std::max)(group->travel_, std::fabs(slot->toFirst - slot->fromFirst));
            group->travel_ = (std::max)(group->travel_, std::fabs(slot->toLast - slot->fromLast));
        }
        if (slot->shown && slot->card->visible) {
            const float top = slot == first && !topConnected ? radius : 0;
            const float bottom = slot == last ? radius : 0;
            slot->card->corners = { top, top, bottom, bottom };
            slot->card->borderEdges = edge::kAll;
            if ((spacing == 0 && slot != first) || (slot == first && topConnected && spacing == 0))
                slot->card->borderEdges &= ~edge::kTop;
        }
        slot->card->rect = slot->card->drawn = { 0, 0, inner.width, slot->height };
        slot->card->placed = true;
    }
    group->moving_ = group->ready_ && Animations() && group->travel_ > 0.02f;
    group->travel_ = (std::max)(1.0f, group->travel_);
    group->phase_ = group->moving_ ? 0.0f : 1.0f;
    group->ready_ = true;
    group->dirty_ = false;
    arrangedWidth = inner.width;
    arrangedRadius = radius;
    arrangedSpacing = spacing;
    arrangedConnected = topConnected;
    group->Present();
}

inline void CardGroupLayout::Tick(float dt) {
    auto *group = static_cast<CardGroup *>(host_);
    if (!Animations()) group->phase_ = 1;
    else if (group->moving_)
        motion::Follow(group->phase_, 1.0f, dt, glideLag, glideSnap / group->travel_);
    group->Present();
}

struct CardExpander;

struct CardExpanderHeader : Card {
    CardExpander *owner;
    CardExpanderHeader(CardExpander *group, std::wstring title, std::wstring detail)
        : Card(std::move(title), std::move(detail)), owner(group) { trailingSpace = 32.0f; }

    bool Focusable() const override { return true; }
    void OnClick() override;
    bool OnKey(WPARAM key) override;
    int AccessibleType() const override { return UIA_ButtonControlTypeId; }
    bool AccessibleActionable() const override { return true; }
    int AccessibleExpanded() const override;
    bool AccessibleWritable() const override { return true; }
    bool AccessibleSetExpanded(bool open) override;
    D2D1_COLOR_F Background(const Palette &c) const override {
        return Mix(Mix(c.cardBg, c.controlBgHover, hoverT), c.controlBgPressed, pressT);
    }
    void Paint(const Painter &p) override;
};

struct CardExpanderItems : CardGroup {
    bool exposed = false;
    bool AccessibleVisible() const override { return visible && exposed; }
};

struct CardExpanderLayout : Layout {
    Want Measure(const Room &room) const override;
    void Arrange(const Room &room, const D2D1_RECT_F &box) override;
};

struct CardExpander : View {
    CardExpanderHeader *header;
    CardExpanderItems *items;
    std::function<void(bool)> onExpanded;

    explicit CardExpander(std::wstring title, std::wstring detail = {}, bool expanded = false)
        : open_(expanded) {
        SetLayout(new CardExpanderLayout());
        header = Widget::Add(new CardExpanderHeader(this, std::move(title), std::move(detail)));
        items = Widget::Add(new CardExpanderItems());
        items->GroupLayout().topConnected = true;
        items->presentationHeight = [this] {
            return Expanded() ? (std::max)(0.0f, Reveal() - BodyGap()) : Height(items->rect);
        };
        items->visible = items->enabled = items->exposed = expanded;
    }

    template <typename T> T *Add(T *card) { return items->Add(card); }
    bool Expanded() const { return open_; }
    void SetExpanded(bool open) {
        if (open_ == open) return;
        open_ = open;
        if (!open) {
            if (Surface *s = surface()) {
                s->DismissIn(items);
                if (items->Holds(s->capture)) s->CancelCapture();
                if (items->Holds(s->focused)) s->SetFocusTo(header);
            }
        } else {
            // Cards stay alive, but a hidden subtree must be measured at its current width again.
            if (!items->visible) UnplaceSubtree(items);
            items->visible = true;
        }
        items->enabled = items->exposed = open;
        InvalidateLayout();
        if (onExpanded) onExpanded(open);
    }

    float HeaderHeight() const { return Height(header->rect); }
    float BodyGap() const { return Height(items->rect) > 0 ? items->GroupLayout().spacing : 0; }
    float Reveal() const {
        return (std::max)(0.0f, Height(placed ? drawn : rect) - HeaderHeight());
    }
    bool Clips() const override { return true; }
    void OnGeometryChanged() override { items->OnGeometryChanged(); }
    D2D1_RECT_F ClipBox() const override {
        // PaintTree already translates the origin. Only the drawn size goes on here.
        const auto &at = placed ? drawn : rect;
        return { rect.left, rect.top, rect.left + Width(at), rect.top + Height(at) };
    }
    Corners ClipCorners() const override {
        const float r = items->GroupLayout().radius;
        return { r, r, r, r };
    }
    const wchar_t *AccessibleName() const override { return header->AccessibleLabel(); }
    int AccessibleType() const override { return UIA_GroupControlTypeId; }
    int AccessibleExpanded() const override { return open_ ? 1 : 0; }
    bool AccessibleWritable() const override { return true; }
    bool AccessibleSetExpanded(bool open) override { SetExpanded(open); return true; }

    bool Animating() const override {
        // One last frame retires the painted body after the parent's glide reaches the header.
        return (!open_ && items->visible && Reveal() <= 0.5f) || Widget::Animating();
    }
    void Tick(float dt) override {
        Widget::Tick(dt);
        if (!open_ && items->visible && Reveal() <= 0.5f) items->visible = false;
        const float r = items->GroupLayout().radius;
        const bool body = items->visible && Height(items->rect) > 0.0f;
        header->corners = { r, r, body ? 0.0f : r, body ? 0.0f : r };
    }

private:
    bool open_ = false;
};

inline Want CardExpanderLayout::Measure(const Room &room) const {
    const auto *group = static_cast<const CardExpander *>(host_);
    const float header = group->header->Measure(room).h.size;
    const float body = group->Expanded() ? group->items->Measure(room).h.size : 0.0f;
    return Want(Axis::Fill(), Axis::Content(header + body + (body > 0 ? group->items->GroupLayout().spacing : 0)));
}

inline void CardExpanderLayout::Arrange(const Room &room, const D2D1_RECT_F &box) {
    auto *group = static_cast<CardExpander *>(host_);
    Room inner = room;
    inner.width = (std::max)(0.0f, box.right - box.left);
    const float header = group->header->Measure(inner).h.size;
    const float body = group->items->Measure(inner).h.size;
    group->header->rect = { box.left, box.top, box.right, box.top + header };
    const float top = box.top + header + (body > 0 ? group->items->GroupLayout().spacing : 0);
    group->items->rect = { box.left, top, box.right, top + body };
    // The parent's existing glide owns the reveal and every following sibling's movement. The
    // natural body stays put behind that window; gliding it too would introduce a second lag.
    group->header->drawn = group->header->rect;
    group->items->drawn = group->items->rect;
    group->header->placed = group->items->placed = true;
    const float r = group->items->GroupLayout().radius;
    const bool shown = body > 0.0f && (group->Expanded() || group->Reveal() > 0.5f);
    group->header->corners = { r, r, shown ? 0.0f : r, shown ? 0.0f : r };
}

inline void CardExpanderHeader::OnClick() { if (enabled) owner->SetExpanded(!owner->Expanded()); }
inline bool CardExpanderHeader::OnKey(WPARAM key) {
    if (!enabled || (key != VK_LEFT && key != VK_RIGHT)) return false;
    owner->SetExpanded(key == VK_RIGHT);
    return true;
}
inline int CardExpanderHeader::AccessibleExpanded() const { return owner->Expanded() ? 1 : 0; }
inline bool CardExpanderHeader::AccessibleSetExpanded(bool open) {
    owner->SetExpanded(open);
    return true;
}
inline void CardExpanderHeader::Paint(const Painter &p) {
    Card::Paint(p);
    const float radius = owner->items->GroupLayout().radius;
    const float amount = Height(owner->items->rect) > 0.0f
        ? std::clamp(owner->Reveal() / (Height(owner->items->rect) + owner->BodyGap()), 0.0f, 1.0f) : 0.0f;
    const float x = rect.right - layout->spec.cardPad - 12.0f;
    const float y = (rect.top + rect.bottom) * 0.5f;
    D2D1::Matrix3x2F before;
    p.rt->GetTransform(&before);
    p.rt->SetTransform(D2D1::Matrix3x2F::Rotation(180.0f * amount, D2D1::Point2F(x, y)) * before);
    p.Text(glyph::kChevronDown, { x - 8.0f, y - 10.0f, x + 8.0f, y + 10.0f }, p.font->icon,
           enabled ? p.pal->textPrimary : p.pal->textDisabled);
    p.rt->SetTransform(before);
    if (ShowFocusRing())
        p.Panel({ rect.left + 2, rect.top + 2, rect.right - 2, rect.bottom - 2 },
                { radius, radius, radius, radius }, D2D1::ColorF(0, 0.0f), p.pal->textPrimary,
                edge::kAll, 2.0f);
}

}  // namespace micula
