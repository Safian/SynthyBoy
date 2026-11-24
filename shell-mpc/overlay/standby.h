// SynthyBoy - MPC overlay: the host's standby dim. After about ten minutes without input the host repaints its own picture BLURRED (an 8 px box), and on the Key 61 (gen 1) also darkened to half and with the
// backlight dropped (/sys/class/backlight/<node>/brightness 227 -> 106 in one step); on the MPC XL (gen 2) the picture can blur without darkening even when the physical backlight drops. So the markers of our pages
// are no longer in the colours the page detector knows. This module lets our picture stay: it finds the backlight node, reads the blurred markers, runs the small state machine that holds the page across
// the signals that do not change in the same frame, and darkens our scene / canvas to the host's brightness. The numbers are measured: docs/MPC.md "Standby dim".
//
// Two independent kinds of evidence that the page is dimmed:
//  (a) backlight + picture (Key 61): the backlight is at or below kDropRatio of its awake value AND the held view's markers look dimmed (`markersLookDimmed`: the marker colour lines at the dim
//      factor, the blur's coverage unknown, 5 samples per marker).
//  (b) picture alone (XL, and any host that only blurs): `blurredPicture`. The host's background level near the markers gives the factor f (host background / known background; XL 1.00, Key 61 0.49;
//      accepted 0.35 .. 1.05, snapped to 1 within 0.04 and to kDimFactor within 0.03). Then the picture around ALL markers of the held view (the left marker, the instance picture, the right marker) must
//      be the sum of their colours blurred by the measured kernel: a box of kBlurWidth = 7.5 px centred kBlurShift = -0.25 px, i.e. pixel = f * (bg + sum_i cover_i(x) * cover_i(y) * (marker_i - bg)).
//      Measured on the XL (play page, 3 markers, 2 x 35 x 40 px around them): mean error 0.23 levels, largest 7 (with the 4 px blue tail, 0.4 over the tolerance below). 13 samples per marker (centre,
//      the four sides 3 px outside, the four inset corners, the four sides 6 px outside) must each match within kBlurTolerance + kBlurRelTolerance * |predicted contrast| per channel; so the plain
//      background (no contrast), a colour area (coverage 1 instead of 0.64), another view's colours (play1 vs filter1: 60 levels apart), a host pop-up and an unrelated screen are all refused, and the
//      three markers must agree on the same view. A marker whose predicted centre contrast is below kBlurMinContrast cannot be evidence (the view then has no blur look). The right marker may lie under
//      the host's Q-Link pop-up: then its samples must read the dimmed pop-up fill. Neighbour views differ by 7 levels in a channel (4.5 after the blur): the identity among them is the held view's.
// The Tracker holds the page while the markers are half-way between sharp and blurred (kHoldMs) so the host skin is never shown for a frame.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include "drm_interfaces.h"
#include "geometry.h"
#include "gpu/scene.h"

namespace synthyboy_mpc { namespace overlay { namespace standby {

/** The host's picture while dimmed is the awake picture times this (measured on the Key 61: background 0x1e1d1e -> 0x0f0e0f exactly, white 0xff -> a blurred 0x64 on a small icon). */
constexpr double kDimFactor = 0.5;
/** "Dimmed" = the backlight is at or below this share of the value seen while awake (Key 61: 106 / 227 = 0.47; another driver has other numbers, only the drop counts). */
constexpr double kDropRatio = 0.75;
/** ... and at least this many levels lower: on a panel with a tiny scale (the XL reads 3) a step of one level (4 -> 3) is the ratio 0.75 but no dimming; only a drop of this size counts as backlight evidence. */
constexpr int kMinDropLevels = 4;
/** How long the picture and the backlight may disagree (one already dim, the other not yet) before the page is taken for gone. Measured on the Key 61: the dim shows in both in the same 20 ms
    sample; on waking the host's picture is back first and the backlight 50 ms later (sampled every 20 ms). Three times the larger skew plus one detector tick of 100 ms. */
constexpr double kHoldMs = 300.0;
/** A backlight value that has not changed for this long while the page is on screen awake is the new awake reference (the user may change the brightness; the wake step must not become it). */
constexpr double kBaselineSettleMs = 1500.0;
constexpr int kLookTolerance = 3;   // levels per channel, plus a tenth of the marker's contrast
/** The blur the host paints over its picture while dimmed, fitted to the XL's two grabs (the box width and the shift of its centre in pixels), and the per-sample tolerance of the fit. */
constexpr double kBlurWidth = 7.5, kBlurShift = -0.25;
constexpr double kBlurTolerance = 3.5, kBlurRelTolerance = 0.15;   // levels per channel, plus a share of the predicted contrast
constexpr double kBlurMinContrast = 6.0;                           // levels: a marker that shows less than this at its centre cannot be evidence
constexpr double kMinFactor = 0.35, kMaxFactor = 1.05;             // the dim factors a host picture may have (XL 1.0, Key 61 0.5)
constexpr double kMinCoverage = 0.15, kMaxCoverage = 1.35;   // how much of the marker's contrast must be left after the blur (a 6 px square under an 8 px blur keeps about half in its middle)

/** The brightness of the panel: the first readable node under /sys/class/backlight/ (any node name; the XL has another one). The value is read at every call (a sysfs read of a few bytes). */
class Backlight
{
public:
    explicit Backlight (std::string dir = "/sys/class/backlight") : dir_ (std::move (dir)) {}

    /** true: `value` is the current brightness. false: no node (looked for again every 5 seconds). */
    bool read (double nowMs, int& value)
    {
        if (node_.empty())
        {
            if (nowMs < nextScanMs_)
                return false;
            nextScanMs_ = nowMs + 5000.0;
            scan();
            if (node_.empty())
                return false;
        }
        int v = 0;
        if (! readInt (dir_ + "/" + node_ + "/brightness", v))
        {
            node_.clear();   // the node went away: look again later
            return false;
        }
        value_ = v;
        value = v;
        return true;
    }
    const std::string& node() const noexcept { return node_; }
    int value() const noexcept { return value_; }

private:
    static bool readInt (const std::string& path, int& v)
    {
        const int fd = ::open (path.c_str(), O_RDONLY);
        if (fd < 0)
            return false;
        char buf[32];
        const ssize_t n = ::read (fd, buf, sizeof buf - 1);
        ::close (fd);
        if (n <= 0)
            return false;
        buf[n] = 0;
        char* end = nullptr;
        const long x = std::strtol (buf, &end, 10);
        if (end == buf)
            return false;
        v = static_cast<int> (x);
        return true;
    }
    void scan()
    {
        std::vector<std::string> names;
        if (DIR* d = ::opendir (dir_.c_str()))
        {
            while (const dirent* e = ::readdir (d))
                if (e->d_name[0] != '.')
                    names.push_back (e->d_name);
            ::closedir (d);
        }
        std::sort (names.begin(), names.end());
        for (const std::string& n : names)
        {
            int v = 0;
            if (readInt (dir_ + "/" + n + "/brightness", v))
            {
                node_ = n;
                return;
            }
        }
    }

    std::string dir_, node_;
    double nextScanMs_ = 0.0;
    int value_ = -1;
};

namespace detail
{
/** One sample: the pixel `v` against the marker colour `m` on the background `bg`, both at the dim factor. The marker's contrast with the background is scaled by the dim factor and by how much of it
    the blur left (coverage t); every channel must lie on that line within the tolerance, and t must be a clear share of the contrast on the channel that has the most. A plain dimmed background (t 0) and
    another colour are refused. */
inline bool sampleLooksDimmed (uint32_t v, uint32_t m, uint32_t bg, double f) noexcept
{
    double obs[3], ref[3], del[3];
    int k = 0;
    for (int c = 0; c < 3; ++c)
    {
        const int sh = 16 - 8 * c;
        obs[c] = static_cast<double> ((v >> sh) & 255u);
        ref[c] = f * static_cast<double> ((bg >> sh) & 255u);
        del[c] = f * (static_cast<double> ((m >> sh) & 255u) - static_cast<double> ((bg >> sh) & 255u));
        if (std::fabs (del[c]) > std::fabs (del[k]))
            k = c;
    }
    if (std::fabs (del[k]) < 4.0)
        return false;   // a marker that does not differ from the background cannot be told at half brightness
    const double t = (obs[k] - ref[k]) / del[k];
    if (t < kMinCoverage || t > kMaxCoverage)
        return false;
    for (int c = 0; c < 3; ++c)
        if (std::fabs (obs[c] - (ref[c] + t * del[c])) > kLookTolerance + 0.1 * std::fabs (del[c]))
            return false;
    return true;
}
} // namespace detail

/** Both markers of a view (`pair[0]` left, `pair[1]` right; rect + rgb) as the host's dimmed picture shows them. The right marker may lie under the host's Q-Link pop-up (`rightMayBeUnderPanel`): the
    panel is dimmed with the rest, its fill then reads as the dimmed background. */
template <class Marker>
bool markersLookDimmed (IFrameSource& src, const Marker* pair, int rotation, int w, int h, double f, bool rightMayBeUnderPanel, uint32_t background = 0x1e1d1eu)
{
    for (int m = 0; m < 2; ++m)
    {
        const Rect r { pair[m].rect.x, pair[m].rect.y, pair[m].rect.w, pair[m].rect.h };
        const int ix = r.w > 3 ? 1 : 0, iy = r.h > 3 ? 1 : 0;
        const int xs[5] = { r.x + ix, r.x + r.w - 1 - ix, r.x + ix, r.x + r.w - 1 - ix, r.x + r.w / 2 };
        const int ys[5] = { r.y + iy, r.y + iy, r.y + r.h - 1 - iy, r.y + r.h - 1 - iy, r.y + r.h / 2 };
        bool markerOk = true, panelOk = m == 1 && rightMayBeUnderPanel;
        for (int k = 0; k < 5; ++k)
        {
            int px, py;
            uint32_t v = 0;
            uiPointToPanel (rotation, xs[k], ys[k], w, h, px, py);
            if (! src.pixel (px, py, v))
                return false;
            if (! detail::sampleLooksDimmed (v, pair[m].rgb, background, f))
                markerOk = false;
            if (panelOk)
            {
                for (int c = 0; c < 3; ++c)
                {
                    const int sh = 16 - 8 * c;
                    const double want = f * static_cast<double> ((background >> sh) & 255u);
                    if (std::fabs (static_cast<double> ((v >> sh) & 255u) - want) > kLookTolerance)
                        panelOk = false;
                }
            }
        }
        if (! markerOk && ! panelOk)
            return false;
    }
    return true;
}

/** A marker or picture the blur test knows: its rectangle (landscape UI pixels), its awake colour, and whether that colour is known (the instance picture's is only when the held slot is). */
struct BlurSource
{
    int x = 0, y = 0, w = 0, h = 0;
    uint32_t rgb = 0;
    bool known = true;
};

namespace detail
{
/** The share of the 1-pixel cell `u` that the blur box (kBlurWidth wide, centre at the cell centre + kBlurShift) takes from the span [a0, a0 + len). One axis of the separable blur. */
inline double boxCover (int u, int a0, int len) noexcept
{
    const double l = u + 0.5 + kBlurShift - kBlurWidth / 2.0, h = l + kBlurWidth;
    const double lo = std::max (l, static_cast<double> (a0)), hi = std::min (h, static_cast<double> (a0 + len));
    return hi > lo ? (hi - lo) / kBlurWidth : 0.0;
}
/** True when pixel (x, y) is so near the span of a source of unknown colour that its blur reaches it. */
inline bool nearRect (int x, int y, const BlurSource& s) noexcept
{
    const double r = kBlurWidth / 2.0 + 1.0;
    return x > s.x - r && x < s.x + s.w + r && y > s.y - r && y < s.y + s.h + r;
}
inline double channelOf (uint32_t c, int ch) noexcept { return static_cast<double> ((c >> (16 - 8 * ch)) & 255u); }
} // namespace detail

/** The dim factor of the host's picture: its background level on the sides of the markers (9 px beyond the rectangles, outside the blur's reach) against the known background. -1: not measurable
    (the points disagree: something else is there). Snapped to 1 (the picture is not darkened) and to kDimFactor (the Key 61's half). */
inline double measureFactor (IFrameSource& src, const BlurSource* s, int n, int rotation, int w, int h, uint32_t background = 0x1e1d1eu)
{
    double f[32];
    int cnt = 0;
    double nom = 0.0;
    for (int c = 0; c < 3; ++c)
        nom += detail::channelOf (background, c);
    if (nom < 30.0)
        return -1.0;
    constexpr int kGap = 9;
    for (int i = 0; i < n && cnt < 28; ++i)
    {
        const BlurSource& r = s[i];
        const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        const int pts[4][2] = { { r.x - kGap, cy }, { r.x + r.w - 1 + kGap, cy }, { cx, r.y - kGap }, { cx, r.y + r.h - 1 + kGap } };
        for (const auto& p : pts)
        {
            bool inside = false;
            for (int j = 0; j < n; ++j)   // not near another source of the view
                if (j != i && p[0] > s[j].x - kGap + 3 && p[0] < s[j].x + s[j].w + kGap - 3 && p[1] > s[j].y - kGap + 3 && p[1] < s[j].y + s[j].h + kGap - 3)
                    inside = true;
            int px, py;
            uint32_t v = 0;
            if (inside || p[0] < 0 || p[1] < 0)
                continue;
            uiPointToPanel (rotation, p[0], p[1], w, h, px, py);
            if (! src.pixel (px, py, v))
                continue;
            f[cnt++] = (detail::channelOf (v, 0) + detail::channelOf (v, 1) + detail::channelOf (v, 2)) / nom;
        }
    }
    if (cnt < 4)
        return -1.0;
    std::sort (f, f + cnt);
    const double med = f[cnt / 2];
    int agree = 0;
    for (int i = 0; i < cnt; ++i)
        if (std::fabs (f[i] - med) <= 0.04)
            ++agree;
    if (agree * 3 < cnt * 2 || agree < 4 || med < kMinFactor || med > kMaxFactor)
        return -1.0;
    if (std::fabs (med - 1.0) < 0.04)
        return 1.0;
    if (std::fabs (med - kDimFactor) < 0.03)
        return kDimFactor;
    return med;
}

/** Native skins can have a gradient behind the markers. Read its colour on
    this scanline outside the blur footprints of all three identity squares. */
inline bool skinRowBackground (IFrameSource& src, const BlurSource* sources, int count, const BlurSource& marker,
                               int y, int rotation, int w, int h, uint32_t& rgb)
{
    for (int x : { marker.x - 9, marker.x + marker.w - 1 + 9 })
    {
        if (x < 0 || y < 0 || std::any_of (sources, sources + count, [=] (const BlurSource& s) { return detail::nearRect (x, y, s); }))
            continue;
        int px, py;
        uiPointToPanel (rotation, x, y, w, h, px, py);
        if (src.pixel (px, py, rgb))
            return true;
    }
    return false;
}

enum class BlurBackdrop { Plain, NativeSkinRow };

/** The blurred-picture test (rule (b) of the header): the held view's markers `s[0]` (left), `s[1]` (right), and `s[2]` (the instance picture) when `n` is 3, as the host's blurred picture at brightness
    `f` shows them. */
inline bool blurredPicture (IFrameSource& src, const BlurSource* s, int n, int rotation, int w, int h, double f, bool rightMayBeUnderPanel, BlurBackdrop backdrop = BlurBackdrop::Plain)
{
    if (n < 2)
        return false;
    auto predicted = [&] (int x, int y, uint32_t bg, double out[3])
    {
        for (int c = 0; c < 3; ++c)
            out[c] = detail::channelOf (bg, c);
        for (int i = 0; i < n; ++i)
        {
            if (! s[i].known)
                continue;
            const double k = detail::boxCover (x, s[i].x, s[i].w) * detail::boxCover (y, s[i].y, s[i].h);
            for (int c = 0; c < 3; ++c)
                out[c] += k * (detail::channelOf (s[i].rgb, c) - detail::channelOf (bg, c));
        }
        for (int c = 0; c < 3; ++c)
            out[c] *= f;
    };
    bool anyEvidence = false;
    for (int m = 0; m < n; ++m)
    {
        if (! s[m].known)
            continue;
        const BlurSource& r = s[m];
        const int cx = r.x + r.w / 2, cy = r.y + r.h / 2, x1 = r.x + r.w - 1, y1 = r.y + r.h - 1;
        const int ix = r.w > 3 ? 1 : 0, iy = r.h > 3 ? 1 : 0;
        const int pts[13][2] = { { cx, cy },
                                 { r.x - 3, cy }, { x1 + 3, cy }, { cx, r.y - 3 }, { cx, y1 + 3 },
                                 { r.x + ix, r.y + iy }, { x1 - ix, r.y + iy }, { r.x + ix, y1 - iy }, { x1 - ix, y1 - iy },
                                 { r.x - 6, cy }, { x1 + 6, cy }, { cx, r.y - 6 }, { cx, y1 + 6 } };
        double centre[3];
        uint32_t bg = 0x1e1d1eu;
        if (backdrop == BlurBackdrop::NativeSkinRow && ! skinRowBackground (src, s, n, r, cy, rotation, w, h, bg))
            return false;
        predicted (cx, cy, bg, centre);
        double strongest = 0.0;
        for (int c = 0; c < 3; ++c)
            strongest = std::max (strongest, std::fabs (centre[c] - f * detail::channelOf (bg, c)));
        if (strongest < kBlurMinContrast)
            return false;
        bool markerOk = true, panelOk = m == 1 && rightMayBeUnderPanel;
        for (const auto& p : pts)
        {
            bool skip = false;
            for (int j = 0; j < n; ++j)
                if (! s[j].known && detail::nearRect (p[0], p[1], s[j]))
                    skip = true;
            if (skip)
                continue;
            int px, py;
            uint32_t v = 0;
            uiPointToPanel (rotation, p[0], p[1], w, h, px, py);
            if (p[0] < 0 || p[1] < 0 || ! src.pixel (px, py, v))
                return false;
            double want[3];
            if (backdrop == BlurBackdrop::NativeSkinRow && ! skinRowBackground (src, s, n, r, p[1], rotation, w, h, bg))
                return false;
            predicted (p[0], p[1], bg, want);
            for (int c = 0; c < 3; ++c)
            {
                const double got = detail::channelOf (v, c);
                const double tol = kBlurTolerance + kBlurRelTolerance * std::fabs (want[c] - f * detail::channelOf (bg, c));
                if (std::fabs (got - want[c]) > tol)
                    markerOk = false;
                if (std::fabs (got - f * detail::channelOf (bg, c)) > kLookTolerance)
                    panelOk = false;
            }
        }
        if (! markerOk && ! panelOk)
            return false;
        if (markerOk)
            anyEvidence = true;
    }
    return anyEvidence;
}

/** What the controller does with a detection tick. */
struct Verdict
{
    bool hold = false;     // the page is still there although its awake markers are not: keep it
    bool dimmed = false;   // the host's picture is dimmed: draw darkened, take no touch
};

/** The state machine. Inputs of one detection tick: `awake` (the awake markers of a view are on screen), `dimLook` (the markers of the held page look dimmed), `havePage` (our picture is on screen),
    the backlight value if there is a node. The awake markers decide first: the picture is the host's and ours then. Without them the dimmed look decides and the backlight confirms it; one signal
    without the other is held for kHoldMs (they do not change in the same frame), then the page is taken for gone, as it is without this module. With no backlight node the dimmed look alone decides.
    `blur` is the picture-alone evidence (blurredPicture: the spatial profile of every marker of the held view matches the host's blur): it decides by itself, with or without a backlight drop (the XL does
    not change its backlight). The evidence used is reported by `evidence()`. */
class Tracker
{
public:
    Verdict update (double now, bool awake, bool dimLook, bool havePage, bool haveBl, int bl, bool blur = false)
    {
        haveBl_ = haveBl;
        if (haveBl)
        {
            if (! haveLast_ || bl != lastBl_)
            {
                lastBl_ = bl;
                blSince_ = now;
                haveLast_ = true;
            }
            if (awake)
            {
                if (baseline_ <= 0 || bl > baseline_ || now - blSince_ >= kBaselineSettleMs)
                    baseline_ = bl;
            }
        }
        low_ = haveBl ? (baseline_ > 0 && static_cast<double> (bl) <= static_cast<double> (baseline_) * kDropRatio && baseline_ - bl >= kMinDropLevels) : dimLook;
        Verdict v;
        busy_ = false;
        if (awake)
        {
            if (mode_ == Dimmed)
            {
                ++wakes_;
                mode_ = Awake;
            }
            skewSince_ = -1.0;
            lastGood_ = now;
            busy_ = haveBl && low_;   // the backlight is still low (the wake step comes 50 ms after the picture), or the picture has not been repainted yet: look again soon
            return v;
        }
        if (! havePage)
        {
            mode_ = Awake;
            skewSince_ = -1.0;
            return v;
        }
        if (dimLook || blur)
        {
            if (low_ || blur)
            {
                evidence_ = haveBl && low_ ? 1 : 2;
                if (mode_ == Awake)
                {
                    mode_ = Dimmed;
                    ++dims_;
                }
                skewSince_ = -1.0;
                lastGood_ = now;
                v.hold = v.dimmed = true;
                return v;
            }
            // the picture is dim and the backlight is not (or not any more): one signal only
            if (skewSince_ < 0.0)
                skewSince_ = now;
            busy_ = true;
            if (now - skewSince_ <= kHoldMs)
            {
                ++holdTicks_;
                v.hold = v.dimmed = true;
                return v;
            }
            ++hiddenTicks_;   // the bound is over: the page goes (a frame of ours missing in a transition)
            mode_ = Awake;
            return v;
        }
        skewSince_ = -1.0;
        if (mode_ == Dimmed && now - lastGood_ <= kHoldMs)
        {
            busy_ = true;
            ++holdTicks_;
            v.hold = v.dimmed = true;
            return v;
        }
        mode_ = Awake;   // neither the awake nor the dimmed markers: the host left the page
        return v;
    }

    bool dimmed() const noexcept { return mode_ == Dimmed; }
    /** A transition is open: the next check should come soon. */
    bool busy() const noexcept { return busy_; }
    unsigned long dims() const noexcept { return dims_; }
    unsigned long wakes() const noexcept { return wakes_; }
    unsigned long holdTicks() const noexcept { return holdTicks_; }
    /** Detection ticks at which the page was taken for gone because one of the two signals never followed the other (must stay 0 on the device). */
    unsigned long hiddenTicks() const noexcept { return hiddenTicks_; }
    /** What the held dim rests on: "backlight+picture" (the backlight dropped and the markers look dimmed), "picture" (the blurred markers alone: the XL, or no backlight node), "none" (not dimmed). */
    const char* evidence() const noexcept { return mode_ != Dimmed ? "none" : (evidence_ == 1 ? "backlight+picture" : "picture"); }
    int awakeBacklight() const noexcept { return baseline_; }
    bool backlightLow() const noexcept { return low_; }

private:
    enum Mode { Awake, Dimmed };
    Mode mode_ = Awake;
    int baseline_ = 0, lastBl_ = 0, evidence_ = 0;
    bool haveLast_ = false, haveBl_ = false, low_ = false, busy_ = false;
    double blSince_ = 0.0, skewSince_ = -1.0, lastGood_ = 0.0;
    unsigned long dims_ = 0, wakes_ = 0, holdTicks_ = 0, hiddenTicks_ = 0;
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Darkening: the awake picture times the factor.

/** The colour (straight 0xRRGGBBAA) with its RGB times f, alpha unchanged. */
inline uint32_t scaleRgba (uint32_t c, double f) noexcept
{
    auto ch = [f] (uint32_t v) { return static_cast<uint32_t> (std::min (255.0, std::floor (static_cast<double> (v) * f + 0.5))); };
    return (ch ((c >> 24) & 255u) << 24) | (ch ((c >> 16) & 255u) << 16) | (ch ((c >> 8) & 255u) << 8) | (c & 255u);
}

/** A display list drawn at the factor: every colour of every op scaled (source over is linear in the colours, so an opaque ground gives exactly the awake picture times f, and a transparent part stays
    transparent: the host shows through it, dimmed by the host itself). The hash of a cached cell is changed so the helper draws the cells again when the dim comes and goes. The tint of a bloom is a
    factor on layers that are already scaled: left alone. */
inline void dimScene (gpu::SceneData& s, double f) noexcept
{
    const uint32_t n = std::min (s.numOps, static_cast<uint32_t> (gpu::kMaxOps));
    for (uint32_t i = 0; i < n; ++i)
    {
        gpu::SceneOp& o = s.ops[i];
        switch (o.op)
        {
            case gpu::kOpNone: case gpu::kOpClip: case gpu::kOpCurve: case gpu::kOpStyle: case gpu::kOpFeedback: case gpu::kOpGlowBegin: case gpu::kOpGlowEnd: case gpu::kOpCellEnd:
                break;
            case gpu::kOpCellBegin:
                o.rgba ^= 0x5bd1e995u;
                break;
            case gpu::kOpFillGradient:
                o.rgba = scaleRgba (o.rgba, f);
                o.rgba2 = scaleRgba (o.rgba2, f);
                break;
            case gpu::kOpPaint:
                o.rgba = scaleRgba (o.rgba, f);
                o.rgba2 = scaleRgba (o.rgba2, f);
                o.f[0] = gpu::bitsFloat (scaleRgba (gpu::floatBits (o.f[0]), f));
                o.f[1] = gpu::bitsFloat (scaleRgba (gpu::floatBits (o.f[1]), f));
                break;
            default:   // Clear, Rect, VLine, Fill, Stroke, Disc, Text, Line, Arc, Frame, RRect, Band: rgba is the colour (rgba2 is a width or a tracking)
                o.rgba = scaleRgba (o.rgba, f);
                break;
        }
    }
}

/** A canvas of premultiplied ARGB8888 pixels times the factor (RGB only: the alpha stays, a transparent pixel stays transparent). */
inline void dimCanvas (uint32_t* px, size_t count, double f) noexcept
{
    const uint32_t k = static_cast<uint32_t> (f * 256.0 + 0.5);
    for (size_t i = 0; i < count; ++i)
    {
        const uint32_t p = px[i];
        const uint32_t r = (((p >> 16) & 255u) * k + 128u) >> 8, g = (((p >> 8) & 255u) * k + 128u) >> 8, b = ((p & 255u) * k + 128u) >> 8;
        px[i] = (p & 0xff000000u) | (std::min (r, 255u) << 16) | (std::min (g, 255u) << 8) | std::min (b, 255u);
    }
}

}}} // namespace synthyboy_mpc::overlay::standby
