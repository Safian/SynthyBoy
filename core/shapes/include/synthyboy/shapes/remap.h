// SynthyBoy - core/shapes: per-slot remap curve evaluator (Q20).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <array>

#include "synthyboy/shapes/shape.h"

namespace synthyboy::shapes
{

/** Evaluates modulation matrix per-slot remap curves (Q20). */
class RemapCurve
{
public:
    static constexpr size_t kDefaultTableSize = 256;

    void initLinear() noexcept;
    void setCustomShape (const ShapeData& shape) noexcept;
    void clearCustomShape() noexcept;
    bool hasCustomShape() const noexcept { return hasCustomShape_; }
    const ShapeData& shape() const noexcept { return shape_; }

    void setPower (float power) noexcept { power_ = power; }
    float power() const noexcept { return power_; }

    void setBipolar (bool bipolar) noexcept { bipolar_ = bipolar; }
    bool isBipolar() const noexcept { return bipolar_; }

    /** Evaluates remap for input.
        If bipolar: input is in [-1.0, 1.0], output in [-1.0, 1.0].
        If unipolar: input is in [0.0, 1.0], output in [0.0, 1.0]. */
    float evaluate (float input) const noexcept;

    /** Bakes remap curve to a lookup table for rapid modulation evaluation. */
    void bake (float* outTable, size_t size = kDefaultTableSize) const noexcept;

private:
    ShapeData shape_;
    float power_ = 0.0f;
    bool hasCustomShape_ = false;
    bool bipolar_ = false;
};

} // namespace synthyboy::shapes
