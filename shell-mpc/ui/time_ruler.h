// SynthyBoy - MPC UI component library: time ruler, the round time marks under a graph whose x axis is time.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace synthyboy_mpc { namespace ui {

struct RulerTick {
    float x;
    double ms;
    char label[16];
};

struct TimeRuler {
    static constexpr int kMaxTicks = 32;
    int count = 0;
    double stepMs = 0.0;
    RulerTick ticks[kMaxTicks];
};

/** The next larger step of the 1-2-5 series (1 -> 2 -> 5 -> 10 ...). */
inline double rulerNextStep(double step) noexcept
{
    // step is always 1, 2, or 5 times a power of 10.
    // Return the next larger step in the 1-2-5 series.
    if (step <= 0.0) return 1.0;
    double m = step / std::pow(10.0, std::floor(std::log10(step)));
    // m should be 1, 2, or 5; use range checks to avoid floating-point equality issues
    if (m < 1.5) return step * 2.0;       // 1*10^n -> 2*10^n
    if (m < 3.5) return step * 2.5;       // 2*10^n -> 5*10^n
    return step * 2.0;                      // 5*10^n -> 10*10^n = 1*10^(n+1)
}

inline double rulerStepMs(double totalMs, float widthPx, float minSpacingPx) noexcept
{
    if (totalMs <= 0.0 || widthPx <= 0.0) return 0.0;
    if (minSpacingPx < 1.0f) minSpacingPx = 1.0f;

    // We need: widthPx * step / totalMs >= minSpacingPx
    // => step >= minSpacingPx * totalMs / widthPx
    double minStep = (double)minSpacingPx * totalMs / (double)widthPx;

    // Find the smallest 1-2-5 step >= minStep
    if (minStep <= 0.0) return 1.0;

    double step = std::pow(10.0, std::floor(std::log10(minStep)));
    // Now step is the power of 10 below minStep
    // Try 1x, 2x, 5x
    // an exact fit counts: the comparison allows for the rounding of pow / log10 and of the division above
    const double tol = 1.0 + 1e-6;
    if (step * tol >= minStep) return step;
    double step2 = step * 2.0;
    if (step2 * tol >= minStep) return step2;
    double step5 = step * 5.0;
    if (step5 * tol >= minStep) return step5;
    return step * 10.0;
}

inline void formatRulerTime(double ms, char* out, std::size_t size) noexcept
{
    if (size == 0) return;

    if (ms == 0.0) {
        if (size >= 2) {
            out[0] = '0';
            out[1] = '\0';
        } else {
            out[0] = '\0';   // room for the terminator only
        }
        return;
    }

    // Round to 2 decimal places first
    double rounded = std::round(ms * 100.0) / 100.0;

    if (rounded < 1000.0) {
        // Format in ms with up to 2 decimals
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", rounded);
        // Remove trailing zeros after decimal point
        char* dot = std::strchr(buf, '.');
        if (dot) {
            char* end = buf + std::strlen(buf) - 1;
            while (end > dot && *end == '0') {
                *end = '\0';
                --end;
            }
            // Remove trailing dot too
            if (*end == '.') {
                *end = '\0';
            }
        }
        // Append " ms" (suffix length 3)
        std::size_t len = std::strlen(buf);
        std::size_t suffixLen = 3;
        if (len + suffixLen <= size) {
            std::memcpy(out, buf, len);
            std::memcpy(out + len, " ms", suffixLen);
            out[len + suffixLen] = '\0';
        } else {
            // Truncate to fit: leave room for null terminator
            std::size_t maxContent = size - 1;
            if (len <= maxContent) {
                std::memcpy(out, buf, len);
                std::size_t avail = maxContent - len;
                if (avail >= suffixLen) {
                    std::memcpy(out + len, " ms", suffixLen);
                } else if (avail > 0) {
                    std::memcpy(out + len, " ms", avail);
                }
            } else {
                std::memcpy(out, buf, maxContent);
            }
            out[size - 1] = '\0';
        }
    } else {
        // Format in seconds
        double sec = rounded / 1000.0;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", sec);
        // Remove trailing zeros after decimal point
        char* dot = std::strchr(buf, '.');
        if (dot) {
            char* end = buf + std::strlen(buf) - 1;
            while (end > dot && *end == '0') {
                *end = '\0';
                --end;
            }
            if (*end == '.') {
                *end = '\0';
            }
        }
        // Append " s" (suffix length 2)
        std::size_t len = std::strlen(buf);
        std::size_t suffixLen = 2;
        if (len + suffixLen <= size) {
            std::memcpy(out, buf, len);
            std::memcpy(out + len, " s", suffixLen);
            out[len + suffixLen] = '\0';
        } else {
            // Truncate to fit: leave room for null terminator
            std::size_t maxContent = size - 1;
            if (len <= maxContent) {
                std::memcpy(out, buf, len);
                std::size_t avail = maxContent - len;
                if (avail >= suffixLen) {
                    std::memcpy(out + len, " s", suffixLen);
                } else if (avail > 0) {
                    std::memcpy(out + len, " s", avail);
                }
            } else {
                std::memcpy(out, buf, maxContent);
            }
            out[size - 1] = '\0';
        }
    }
}

inline TimeRuler makeTimeRuler(double totalMs, float x0, float widthPx, float minSpacingPx) noexcept
{
    TimeRuler result{};
    if (totalMs <= 0.0 || widthPx <= 0.0) return result;

    double step = rulerStepMs(totalMs, widthPx, minSpacingPx);
    if (step <= 0.0) return result;

    // Compute count analytically: marks at 0, step, 2*step, ...
    // while i*step <= totalMs * (1 + 1e-9)
    // count = floor(totalMs * (1+1e-9) / step) + 1
    const double limit = totalMs * (1.0 + 1e-9);

    // If too many, increase step until it fits (counted as a double: a huge ratio must not overflow an int)
    while (std::floor(limit / step) + 1.0 > (double)TimeRuler::kMaxTicks)
        step = rulerNextStep(step);
    const int count = (int)(std::floor(limit / step)) + 1;

    result.stepMs = step;
    result.count = count;

    for (int i = 0; i < count; ++i) {
        double ms = i * step;
        result.ticks[i].ms = ms;
        result.ticks[i].x = x0 + widthPx * (float)(ms / totalMs);
        formatRulerTime(ms, result.ticks[i].label, sizeof(result.ticks[i].label));
    }

    return result;
}

} } // namespace synthyboy_mpc::ui
