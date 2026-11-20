// SynthyBoy - MPC overlay tests: GPU polyline stroke strip builder and software rasteriser.
// Tests the GPU stroke strip builder (bevel joins on sharp turns vs old miter clamping)
// using a software rasteriser evaluating the Mali fragment shader's lineCov() function.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "overlay/gpu/stroke_strip.h"

namespace
{

static int g_fails = 0;

void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++g_fails;
}

struct Vertex
{
    float x = 0.0f, y = 0.0f, d0 = 0.0f, d1 = 0.0f;
};

struct Pt
{
    float x = 0.0f, y = 0.0f;
};

// The old strip builder before the fix (from gpu-helper/main.cpp with miter clamping)
template <typename Point, typename Emit>
void buildStrokeStripOld (const Point* pts, size_t m, float half, float soft, Emit&& emit)
{
    if (pts == nullptr || m < 2 || ! std::isfinite (half) || half <= 0.0f)
        return;
    const float ext = half + soft * 0.5f + 1.0f;
    const int count = static_cast<int> (m);
    for (int i = 0; i < count; ++i)
    {
        float n1x = 0.0f, n1y = 0.0f, n2x = 0.0f, n2y = 0.0f;
        if (i > 0)
        {
            const float dx = pts[i].x - pts[i - 1].x, dy = pts[i].y - pts[i - 1].y;
            const float l = std::sqrt (dx * dx + dy * dy);
            if (l > 1e-6f) { n1x = -dy / l; n1y = dx / l; }
        }
        if (i < count - 1)
        {
            const float dx = pts[i + 1].x - pts[i].x, dy = pts[i + 1].y - pts[i].y;
            const float l = std::sqrt (dx * dx + dy * dy);
            if (l > 1e-6f) { n2x = -dy / l; n2y = dx / l; }
        }
        if (i == 0) { n1x = n2x; n1y = n2y; }
        if (i == count - 1) { n2x = n1x; n2y = n1y; }
        float mx = n1x + n2x, my = n1y + n2y, ml = std::sqrt (mx * mx + my * my);
        if (ml < 1e-4f) { mx = n1x; my = n1y; ml = 1.0f; }
        mx /= ml;
        my /= ml;
        float cosHalf = mx * n2x + my * n2y;
        if (cosHalf < 0.4f)
            cosHalf = 0.4f;     // miter limit 2.5
        const float k2 = ext / cosHalf;
        emit (pts[i].x + mx * k2, pts[i].y + my * k2, ext, 0.0f);
        emit (pts[i].x - mx * k2, pts[i].y - my * k2, -ext, 0.0f);
    }
}

// Shader coverage function: Mali fragment shader P_BASIC / lineCov (Look 1: half = 0.775, soft = 1.0)
inline float edgeCov (float x, float uSoft)
{
    const float c = std::clamp (0.5f + x / uSoft, 0.0f, 1.0f);
    return uSoft > 1.0f ? c * c * (3.0f - 2.0f * c) : c;
}

inline float lineCov (float d, float uHalf, float uSoft)
{
    const float c = edgeCov (uHalf - d, uSoft);
    return uSoft > 1.0f ? c : std::min (c, 2.0f * uHalf);
}

// Point-in-triangle test with barycentric interpolation of d0
inline bool pointInTri (float px, float py, const Vertex& A, const Vertex& B, const Vertex& C, float& d0Out)
{
    const float denom = (B.y - C.y) * (A.x - C.x) + (C.x - B.x) * (A.y - C.y);
    if (std::abs (denom) < 1e-9f)
        return false;
    const float inv = 1.0f / denom;
    const float l0 = ((B.y - C.y) * (px - C.x) + (C.x - B.x) * (py - C.y)) * inv;
    const float l1 = ((C.y - A.y) * (px - C.x) + (A.x - C.x) * (py - C.y)) * inv;
    const float l2 = 1.0f - l0 - l1;
    constexpr float eps = -1e-5f;
    if (l0 >= eps && l1 >= eps && l2 >= eps)
    {
        d0Out = l0 * A.d0 + l1 * B.d0 + l2 * C.d0;
        return true;
    }
    return false;
}

float pixelCoverage (float px, float py, const std::vector<Vertex>& verts, float uHalf, float uSoft)
{
    float maxCov = 0.0f;
    if (verts.size() < 3) return 0.0f;
    for (size_t i = 0; i + 2 < verts.size(); ++i)
    {
        float d0 = 0.0f;
        if (pointInTri (px, py, verts[i], verts[i + 1], verts[i + 2], d0))
        {
            const float c = lineCov (std::abs (d0), uHalf, uSoft);
            if (c > maxCov) maxCov = c;
        }
    }
    return maxCov;
}

struct RowResult
{
    float sum = 0.0f;
    float max = 0.0f;
    std::vector<float> values;
};

RowResult evalRow (int y, int xMin, int xMax, const std::vector<Vertex>& verts, float uHalf = 0.775f, float uSoft = 1.0f)
{
    RowResult res;
    const float py = static_cast<float> (y) + 0.5f;
    for (int x = xMin; x <= xMax; ++x)
    {
        const float px = static_cast<float> (x) + 0.5f;
        const float c = pixelCoverage (px, py, verts, uHalf, uSoft);
        res.values.push_back (c);
        res.sum += c;
        if (c > res.max) res.max = c;
    }
    return res;
}

std::vector<Pt> sampleArm (Pt a, Pt b, float spacing)
{
    std::vector<Pt> pts;
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::sqrt (dx * dx + dy * dy);
    const int steps = static_cast<int> (std::ceil (len / spacing));
    for (int i = 0; i < steps; ++i)
    {
        const float t = (static_cast<float> (i) * spacing) / len;
        if (t < 1.0f)
            pts.push_back ({ a.x + t * dx, a.y + t * dy });
    }
    return pts;
}

std::vector<Pt> makeVShape (float spacing)
{
    const Pt p0 = { 5.5f, 20.0f };
    const Pt p1 = { 10.5f, 300.0f };
    const Pt p2 = { 15.5f, 20.0f };
    if (spacing <= 0.0f)
        return { p0, p1, p2 };

    std::vector<Pt> pts = sampleArm (p0, p1, spacing);
    pts.push_back (p1);
    const float dx2 = p2.x - p1.x, dy2 = p2.y - p1.y;
    const float len2 = std::sqrt (dx2 * dx2 + dy2 * dy2);
    const int steps2 = static_cast<int> (std::ceil (len2 / spacing));
    for (int i = 1; i <= steps2; ++i)
    {
        const float t = std::min (1.0f, (static_cast<float> (i) * spacing) / len2);
        pts.push_back ({ p1.x + t * dx2, p1.y + t * dy2 });
    }
    return pts;
}

int countFailingRowsArm (const std::vector<Vertex>& armVerts, int xMin, int xMax, std::string& report, const char* label)
{
    int fails = 0, zeroRows = 0;
    int firstFailY = -1, lastFailY = -1;
    float minSum = 999.0f, minMax = 999.0f;
    for (int y = 24; y <= 296; ++y)
    {
        const RowResult r = evalRow (y, xMin, xMax, armVerts);
        if (r.sum == 0.0f) ++zeroRows;
        if (r.sum < 1.40f || r.sum > 1.75f || r.max < 0.75f)
        {
            ++fails;
            if (firstFailY < 0) firstFailY = y;
            lastFailY = y;
            if (r.sum < minSum) minSum = r.sum;
            if (r.max < minMax) minMax = r.max;
        }
    }
    char buf[512];
    if (fails > 0)
    {
        std::snprintf (buf, sizeof buf, "%s: %d / 273 rows failed in y=[24..296] (failing range y=[%d..%d], zero-coverage rows=%d, min sum=%.4f, min max=%.4f)",
                       label, fails, firstFailY, lastFailY, zeroRows, minSum, minMax);
    }
    else
    {
        std::snprintf (buf, sizeof buf, "%s: all 273 rows in y=[24..296] passed (sum in [1.40, 1.75], max >= 0.75)",
                       label);
    }
    report = buf;
    return fails;
}

void testVerticalSegment()
{
    const std::vector<Pt> pts = { { 10.5f, 0.0f }, { 10.5f, 100.0f } };
    std::vector<Vertex> verts;
    synthyboy_mpc::overlay::gpu::buildStrokeStrip (pts, 0.775f, 1.0f, [&] (float x, float y, float d0, float d1) {
        verts.push_back ({ x, y, d0, d1 });
    });

    const RowResult res = evalRow (50, 8, 12, verts, 0.775f, 1.0f);
    check (res.values.size() == 5, "vertical segment at x=10.5: exactly 5 pixels evaluated across row (x=8..12)");
    const bool v0 = std::abs (res.values[0] - 0.0f) < 1e-4f;
    const bool v1 = std::abs (res.values[1] - 0.275f) < 1e-4f;
    const bool v2 = std::abs (res.values[2] - 1.0f) < 1e-4f;
    const bool v3 = std::abs (res.values[3] - 0.275f) < 1e-4f;
    const bool v4 = std::abs (res.values[4] - 0.0f) < 1e-4f;
    check (v0 && v1 && v2 && v3 && v4, "vertical segment at x=10.5: row gives exactly [0, 0.275, 1.0, 0.275, 0]");
    check (std::abs (res.sum - 1.55f) < 1e-4f, "vertical segment at x=10.5: row sum is exactly 1.55");
}

void testVShapeFailureAndPass()
{
    const std::vector<Pt> v3 = makeVShape (0.0f);

    // Old builder on 3-point V: MUST fail on both arms near tip
    std::vector<Vertex> vertsOld;
    buildStrokeStripOld (v3.data(), v3.size(), 0.775f, 1.0f, [&] (float x, float y, float d0, float d1) {
        vertsOld.push_back ({ x, y, d0, d1 });
    });
    // In old builder, point 0 has 2 verts, point 1 (tip) has 2 verts, point 2 has 2 verts.
    const std::vector<Vertex> arm1Old (vertsOld.begin(), vertsOld.begin() + 4);
    std::string reportOld;
    const int failsOld = countFailingRowsArm (arm1Old, 0, 15, reportOld, "Old builder (Arm 1, 3-point V)");
    std::printf ("INFO  %s\n", reportOld.c_str());
    check (failsOld > 0, "V test fails on old builder code before fix (steep lines narrow near tip)");

    // New builder on 3-point V: Arm 1 and Arm 2 MUST pass
    std::vector<Vertex> vertsNew;
    synthyboy_mpc::overlay::gpu::buildStrokeStrip (v3, 0.775f, 1.0f, [&] (float x, float y, float d0, float d1) {
        vertsNew.push_back ({ x, y, d0, d1 });
    });
    // In new builder, point 0 has 2 verts, tip has 4 verts (2 for incoming, 2 for outgoing).
    const std::vector<Vertex> arm1New (vertsNew.begin(), vertsNew.begin() + 4);
    const std::vector<Vertex> arm2New (vertsNew.begin() + 4, vertsNew.end());
    std::string reportNew1, reportNew2;
    const int failsNew1 = countFailingRowsArm (arm1New, 0, 15, reportNew1, "New builder (Arm 1, 3-point V)");
    const int failsNew2 = countFailingRowsArm (arm2New, 7, 22, reportNew2, "New builder (Arm 2, 3-point V)");
    std::printf ("INFO  %s\n", reportNew1.c_str());
    std::printf ("INFO  %s\n", reportNew2.c_str());
    check (failsNew1 == 0 && failsNew2 == 0, "V test passes on new builder (bevel join maintains full width on both arms)");

    // New builder at 4.3 px sample spacing
    const std::vector<Pt> v43 = makeVShape (4.3f);
    std::vector<Vertex> verts43;
    synthyboy_mpc::overlay::gpu::buildStrokeStrip (v43, 0.775f, 1.0f, [&] (float x, float y, float d0, float d1) {
        verts43.push_back ({ x, y, d0, d1 });
    });
    // Tip point index is where (10.5, 300) is
    size_t tipIdx43 = 0;
    for (size_t i = 0; i < v43.size(); ++i)
        if (v43[i].x == 10.5f && v43[i].y == 300.0f) { tipIdx43 = i; break; }
    const std::vector<Vertex> arm1_43 (verts43.begin(), verts43.begin() + static_cast<ptrdiff_t> (tipIdx43 * 2 + 2));
    const std::vector<Vertex> arm2_43 (verts43.begin() + static_cast<ptrdiff_t> (tipIdx43 * 2 + 2), verts43.end());
    std::string report43_1, report43_2;
    const int fails43_1 = countFailingRowsArm (arm1_43, 0, 15, report43_1, "New builder (Arm 1, 4.3 px spacing)");
    const int fails43_2 = countFailingRowsArm (arm2_43, 7, 22, report43_2, "New builder (Arm 2, 4.3 px spacing)");
    std::printf ("INFO  %s\n", report43_1.c_str());
    std::printf ("INFO  %s\n", report43_2.c_str());
    check (fails43_1 == 0 && fails43_2 == 0, "V test at 4.3 px sample spacing passes (coverage sum in [1.40, 1.75], max >= 0.75)");

    // New builder at 8.6 px sample spacing
    const std::vector<Pt> v86 = makeVShape (8.6f);
    std::vector<Vertex> verts86;
    synthyboy_mpc::overlay::gpu::buildStrokeStrip (v86, 0.775f, 1.0f, [&] (float x, float y, float d0, float d1) {
        verts86.push_back ({ x, y, d0, d1 });
    });
    size_t tipIdx86 = 0;
    for (size_t i = 0; i < v86.size(); ++i)
        if (v86[i].x == 10.5f && v86[i].y == 300.0f) { tipIdx86 = i; break; }
    const std::vector<Vertex> arm1_86 (verts86.begin(), verts86.begin() + static_cast<ptrdiff_t> (tipIdx86 * 2 + 2));
    const std::vector<Vertex> arm2_86 (verts86.begin() + static_cast<ptrdiff_t> (tipIdx86 * 2 + 2), verts86.end());
    std::string report86_1, report86_2;
    const int fails86_1 = countFailingRowsArm (arm1_86, 0, 15, report86_1, "New builder (Arm 1, 8.6 px spacing)");
    const int fails86_2 = countFailingRowsArm (arm2_86, 7, 22, report86_2, "New builder (Arm 2, 8.6 px spacing)");
    std::printf ("INFO  %s\n", report86_1.c_str());
    std::printf ("INFO  %s\n", report86_2.c_str());
    check (fails86_1 == 0 && fails86_2 == 0, "V test at 8.6 px sample spacing passes (coverage sum in [1.40, 1.75], max >= 0.75)");
}

void testArmSlopes()
{
    // Slope 2.2: arm drops 110 px over 50 px dx (cosHalf = 1 / sqrt(1 + 2.2^2) = 0.4138 > 0.4 -> miter side)
    const std::vector<Pt> pts22 = { { 10.0f, 20.0f }, { 60.0f, 130.0f }, { 110.0f, 20.0f } };
    std::vector<Vertex> verts22;
    synthyboy_mpc::overlay::gpu::buildStrokeStrip (pts22, 0.775f, 1.0f, [&] (float x, float y, float d0, float d1) {
        verts22.push_back ({ x, y, d0, d1 });
    });
    const std::vector<Vertex> arm1_22 (verts22.begin(), verts22.begin() + 4);
    int fails22 = 0;
    for (int y = 24; y <= 126; ++y)
    {
        const RowResult r = evalRow (y, 5, 65, arm1_22);
        if (r.sum < 1.40f || r.sum > 1.75f || r.max < 0.75f)
            ++fails22;
    }
    check (fails22 == 0, "arm slope 2.2 (miter side of 0.4 limit) passes coverage checks");

    // Slope 2.4: arm drops 120 px over 50 px dx (cosHalf = 1 / sqrt(1 + 2.4^2) = 0.3846 < 0.4 -> bevel side)
    const std::vector<Pt> pts24 = { { 10.0f, 20.0f }, { 60.0f, 140.0f }, { 110.0f, 20.0f } };
    std::vector<Vertex> verts24;
    synthyboy_mpc::overlay::gpu::buildStrokeStrip (pts24, 0.775f, 1.0f, [&] (float x, float y, float d0, float d1) {
        verts24.push_back ({ x, y, d0, d1 });
    });
    const std::vector<Vertex> arm1_24 (verts24.begin(), verts24.begin() + 4);
    int fails24 = 0;
    for (int y = 24; y <= 136; ++y)
    {
        const RowResult r = evalRow (y, 5, 65, arm1_24);
        if (r.sum < 1.40f || r.sum > 1.75f || r.max < 0.75f)
            ++fails24;
    }
    check (fails24 == 0, "arm slope 2.4 (bevel side of 0.4 limit) passes coverage checks");
}

void testGentlePolylineBitIdentity()
{
    std::vector<Pt> gentlePts;
    for (int i = 0; i <= 20; ++i)
    {
        const float x = static_cast<float> (i) * 10.0f;
        const float y = 50.0f + 15.0f * std::sin (static_cast<float> (i) * 0.2f);
        gentlePts.push_back ({ x, y });
    }

    std::vector<Vertex> vertsOld, vertsNew;
    buildStrokeStripOld (gentlePts.data(), gentlePts.size(), 0.775f, 1.0f, [&] (float x, float y, float d0, float d1) {
        vertsOld.push_back ({ x, y, d0, d1 });
    });
    synthyboy_mpc::overlay::gpu::buildStrokeStrip (gentlePts, 0.775f, 1.0f, [&] (float x, float y, float d0, float d1) {
        vertsNew.push_back ({ x, y, d0, d1 });
    });

    check (vertsOld.size() == vertsNew.size(), "gentle polyline: vertex count identical between old and new builder");
    bool bitIdentical = true;
    for (size_t i = 0; i < vertsOld.size() && bitIdentical; ++i)
    {
        if (std::memcmp (&vertsOld[i], &vertsNew[i], sizeof (Vertex)) != 0)
            bitIdentical = false;
    }
    check (bitIdentical, "gentle polyline: all vertices bit-identical between old and new builder");
}

} // namespace

int main()
{
    std::printf ("=== stroke_strip_host tests ===\n");
    testVerticalSegment();
    testVShapeFailureAndPass();
    testArmSlopes();
    testGentlePolylineBitIdentity();
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
