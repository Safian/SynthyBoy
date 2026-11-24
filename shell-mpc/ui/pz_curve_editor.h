// SynthyBoy - MPC editor for the normalized-frequency, dB-valued PZ SVF content curve.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "composite.h"
#include "synthyboy/content_state.h"

namespace synthyboy_mpc::ui
{
class PzCurveCanvas final : public Component
{
public:
    using Point = synthyboy::ContentFilterCurvePoint;
    static constexpr float kDbLimit = 60.0f;

    PzCurveCanvas() { points_.reserve (synthyboy::kMaxFilterCurvePoints); reset(); }
    Size minSize() const override { return { 360, 220 }; }
    const std::vector<Point>& points() const noexcept { return points_; }
    int selected() const noexcept { return selected_; }
    Rect plot() const noexcept { return { rect_.x + 66, rect_.y + 19, std::max (1, rect_.w - 100), std::max (1, rect_.h - 63) }; }

    void load (const std::vector<Point>* points)
    {
        if (points != nullptr && points->size() >= 2)
            points_ = *points;
        else
            reset();
        selected_ = held_ = -1;
    }

    void reset()
    {
        points_ = { { 0.0f, 0.0f }, { 0.5f, 0.0f }, { 1.0f, 0.0f } };
        selected_ = held_ = -1;
    }

    void removeSelected()
    {
        if (selected_ >= 0 && points_.size() > 2)
        {
            points_.erase (points_.begin() + selected_);
            selected_ = -1;
        }
    }

    int px (float x) const noexcept { const Rect r = plot(); return r.x + static_cast<int> (std::lround (x * (r.w - 1))); }
    int py (float db) const noexcept
    {
        const Rect r = plot();
        return r.y + static_cast<int> (std::lround ((kDbLimit - db) * (r.h - 1) / (2.0f * kDbLimit)));
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        const Rect r = plot();
        const auto& t = theme::L();
        Cell cell (sb, rect_);
        Pen pen (sb, enabled_);
        pen.block (r);
        static constexpr const char* gains[] = { "+60 dB", "+30 dB", "0 dB", "-30 dB", "-60 dB" };
        for (int i = 0; i < 5; ++i)
        {
            const auto y = static_cast<float> (py (kDbLimit - i * 30.0f));
            pen.line (r.x, y, r.right() - 1, y, t.hair, pen.c (i == 2 ? t.keyline : t.grid, 255));
            pen.textRight ({ r.x - 64, static_cast<int> (y) - 8, 56, 16 }, gains[i], t.fSmall, pen.c (t.textDim, 255), t.fMin);
        }
        static constexpr const char* frequencies[] = { "20 Hz", "200 Hz", "2 kHz", "20 kHz" };
        for (int i = 0; i < 4; ++i)
        {
            const int x = px (i / 3.0f);
            pen.line (x, r.y, x, r.bottom() - 1, t.hair, pen.c (t.grid, 255));
            const int tx = std::clamp (x - 38, rect_.x, rect_.right() - 76);
            pen.textCentered ({ tx, r.bottom() + 5, 76, 20 }, frequencies[i], t.fSmall, pen.c (t.textDim, 255), t.fMin);
        }
        float rows[256];
        for (int i = 0; i < 256; ++i)
            rows[i] = static_cast<float> (py (sample (i / 255.0f)));
        const uint32_t colour = pen.c (t.flow[0], 255);
        if (sb.curve (rows, 256, r.x, r.w - 1) >= 0)
            sb.stroke (1.0f, colour);
        pen.style (1.0f, true);
        for (int i = 0; i < static_cast<int> (points_.size()); ++i)
        {
            const int x = px (points_[i].x), y = py (points_[i].y);
            pen.disc (x, y, i == selected_ ? 9.0f : 7.0f, colour);
            pen.disc (x, y, 3.0f, pen.c (t.text, 255));
        }
        pen.plain();
    }

protected:
    void drawBody (Pen&) const override {}

    Change onDown (int x, int y, double now) override
    {
        tap_.down (x, y, now);
        held_ = -1;
        int limit = 24 * 24;
        for (int i = 0; i < static_cast<int> (points_.size()); ++i)
        {
            const int dx = px (points_[i].x) - x, dy = py (points_[i].y) - y;
            const int d = dx * dx + dy * dy;
            if (d < limit)
                held_ = i, limit = d;
        }
        if (held_ >= 0)
        {
            selected_ = held_;
            offX_ = px (points_[held_].x) - x;
            offY_ = py (points_[held_].y) - y;
            return Change::request (id_);
        }
        return Change::none();
    }

    Change onMove (int x, int y) override
    {
        if (! tap_.move (x, y) || held_ < 0)
            return Change::none();
        const float left = held_ == 0 ? 0.0f : points_[held_ - 1].x;
        const float right = held_ + 1 == static_cast<int> (points_.size()) ? 1.0f : points_[held_ + 1].x;
        Point p = point (x + offX_, y + offY_);
        p.x = std::clamp (p.x, left, right);
        points_[held_] = p;
        return Change::request (id_);
    }

    Change onUp (int x, int y) override
    {
        const bool add = held_ < 0 && tap_.up() && plot().contains (tap_.startX(), tap_.startY()) && plot().contains (x, y);
        held_ = -1;
        if (! add || points_.size() == synthyboy::kMaxFilterCurvePoints)
            return Change::none();
        const Point p = point (x, y);
        const auto at = std::upper_bound (points_.begin(), points_.end(), p.x, [] (float value, Point other) { return value < other.x; });
        selected_ = static_cast<int> (at - points_.begin());
        points_.insert (at, p);
        return Change::request (id_);
    }

private:
    Point point (int x, int y) const noexcept
    {
        const Rect r = plot();
        const float u = std::clamp (static_cast<float> (x - r.x) / std::max (1, r.w - 1), 0.0f, 1.0f);
        const float v = std::clamp (static_cast<float> (y - r.y) / std::max (1, r.h - 1), 0.0f, 1.0f);
        return { u, (1.0f - 2.0f * v) * kDbLimit };
    }

    float sample (float x) const noexcept
    {
        const auto right = std::upper_bound (points_.begin(), points_.end(), x, [] (float value, Point other) { return value < other.x; });
        if (right == points_.begin())
            return points_.front().y;
        if (right == points_.end())
            return points_.back().y;
        const auto left = right - 1;
        const float t = (x - left->x) / (right->x - left->x);
        return left->y + (right->y - left->y) * t * t * (3.0f - 2.0f * t);
    }

    std::vector<Point> points_;
    TapGesture tap_;
    int selected_ = -1, held_ = -1, offX_ = 0, offY_ = 0;
};

class PzCurveEditor final : public Composite
{
public:
    MenuButton back { "BACK", MenuButton::Icon::None };
    MenuButton reset { "RESET", MenuButton::Icon::None };
    MenuButton remove { "DELETE POINT", MenuButton::Icon::None };
    MenuButton apply { "APPLY", MenuButton::Icon::None };
    PzCurveCanvas canvas;

    PzCurveEditor()
    {
        Component* parts[] { &canvas, &back, &reset, &remove, &apply };
        for (Component* c : parts)
            addKid (*c);
    }
    Size minSize() const override { return { 720, 400 }; }
    void showFilter (int filter, const std::vector<PzCurveCanvas::Point>* points)
    {
        filter_ = filter;
        canvas.load (points);
        initial_ = canvas.points();
        refresh();
    }
    void refresh()
    {
        const auto& points = canvas.points();
        remove.setEnabled (canvas.selected() >= 0 && points.size() > 2);
        apply.setEnabled (! std::equal (points.begin(), points.end(), initial_.begin(), initial_.end(),
                                       [] (auto a, auto b) { return a.x == b.x && a.y == b.y; }));
    }
    void setNativeToolbar (bool native)
    {
        for (auto* button : { &back, &reset, &remove, &apply })
            show (*button, ! native);
    }
    std::array<Rect, 4> toolbarRects() const noexcept
    {
        return { back.rect(), reset.rect(), remove.rect(), apply.rect() };
    }

protected:
    void layout() override
    {
        back.place ({ rect_.x + 8, rect_.y + 12, 94, 56 });
        apply.place ({ rect_.right() - 106, rect_.y + 12, 98, 56 });
        remove.place ({ apply.rect().x - 168, rect_.y + 12, 160, 56 });
        reset.place ({ remove.rect().x - 102, rect_.y + 12, 94, 56 });
        canvas.place ({ rect_.x + 12, rect_.y + 76, rect_.w - 24, rect_.h - 88 });
    }
    void drawExtra (Pen& pen) const override
    {
        const auto& t = theme::L();
        pen.textLeft ({ rect_.x + 116, rect_.y + 8, 290, 31 }, filter_ == 2 ? "F2  PZ SVF CURVE" : "F1  PZ SVF CURVE",
                      t.fTitle, pen.c (t.text, 255), t.fMin);
        pen.textLeft ({ rect_.x + 116, rect_.y + 43, 310, 22 }, "Tap to add a point. Drag to move it.",
                      t.fSmall, pen.c (t.textDim, 255), t.fMin);
    }

private:
    int filter_ = 1;
    std::vector<PzCurveCanvas::Point> initial_;
};
} // namespace synthyboy_mpc::ui
