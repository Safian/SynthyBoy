// SynthyBoy - core: validation of the `content` section (see content_state.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/content_state.h"

#include <cmath>
#include <cstring>
#include <set>

#include "synthyboy/modulation.h"

namespace synthyboy
{
bool operator== (const ContentModRef& a, const ContentModRef& b) noexcept
{
    return a.present == b.present && a.source == b.source && a.amount == b.amount;
}

bool operator== (const ContentPoint& a, const ContentPoint& b) noexcept
{
    return a.x == b.x && a.y == b.y && a.power == b.power && a.modX == b.modX && a.modY == b.modY;
}

namespace
{
bool same (const ContentFileRef& a, const ContentFileRef& b) { return a.tie() == b.tie(); }

template <typename T, typename F>
bool sameVector (const std::vector<T>& a, const std::vector<T>& b, F eq)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (! eq (a[i], b[i]))
            return false;
    return true;
}

bool inRange (double v, double lo, double hi) noexcept
{
    return std::isfinite (v) && v >= lo && v <= hi;
}

bool plainString (const std::string& s, size_t maxBytes) noexcept
{
    if (s.size() > maxBytes)
        return false;
    for (const char c : s)
        if (static_cast<unsigned char> (c) < 0x20)
            return false;
    return true;
}

/** A relative path inside a content pack: no drive, no absolute path, no back slash, no ".." segment. */
bool safePath (const std::string& s) noexcept
{
    if (! plainString (s, kMaxContentStringBytes))
        return false;
    if (s.empty())
        return true;
    if (s[0] == '/' || s.find ('\\') != std::string::npos || s.find (':') != std::string::npos)
        return false;
    size_t pos = 0;
    while (pos <= s.size())
    {
        const size_t next = s.find ('/', pos);
        const std::string segment = s.substr (pos, next == std::string::npos ? std::string::npos : next - pos);
        if (segment == "..")
            return false;
        if (next == std::string::npos)
            break;
        pos = next + 1;
    }
    return true;
}

bool sha256Ok (const std::string& s) noexcept
{
    if (s.empty())
        return true;
    if (s.size() != 64)
        return false;
    for (const char c : s)
        if (! ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}

bool refOk (const ContentFileRef& r) noexcept
{
    return safePath (r.pack) && safePath (r.file) && sha256Ok (r.sha256) && plainString (r.name, kMaxContentStringBytes);
}

bool knownSource (const std::string& key) noexcept
{
    const ModSourceInfo* table = modSourceTable();
    for (size_t i = 0; i < modSourceCount(); ++i)
        if (key == table[i].key)
            return true;
    return false;
}

bool modOk (const ContentModRef& m) noexcept
{
    return ! m.present || (knownSource (m.source) && inRange (m.amount, -kMaxPointModAmount, kMaxPointModAmount));
}

/** points with ascending x in 0..1, y in [yLo, yHi], |power| <= kMaxPointPower; 0 points (identity) or 2 .. maxPoints. */
bool pointsOk (const std::vector<ContentPoint>& pts, int maxPoints, bool allowMod, bool required) noexcept
{
    if (pts.empty())
        return ! required;
    if (pts.size() < 2 || pts.size() > static_cast<size_t> (maxPoints))
        return false;
    float previousX = 0.0f;
    for (const auto& p : pts)
    {
        if (! inRange (p.x, 0.0, 1.0) || ! inRange (p.y, 0.0, 1.0) || ! inRange (p.power, -kMaxPointPower, kMaxPointPower) || p.x < previousX)
            return false;
        if (! allowMod && (p.modX.present || p.modY.present))
            return false;
        if (! modOk (p.modX) || ! modOk (p.modY))
            return false;
        previousX = p.x;
    }
    return true;
}
} // namespace

bool operator== (const StateContent& a, const StateContent& b) noexcept
{
    return sameVector (a.wavetables, b.wavetables, [] (const ContentWavetable& x, const ContentWavetable& y)
                       { return x.osc == y.osc && same (x.ref, y.ref) && x.author == y.author && x.frames == y.frames; })
        && sameVector (a.samples, b.samples, [] (const ContentSample& x, const ContentSample& y)
                       { return x.osc == y.osc && same (x.ref, y.ref) && x.sampleRate == y.sampleRate && x.frames == y.frames
                             && x.channels == y.channels && x.slices == y.slices; })
        && sameVector (a.lfoShapes, b.lfoShapes, [] (const ContentLfoShape& x, const ContentLfoShape& y)
                       { return x.lfo == y.lfo && x.name == y.name && x.smooth == y.smooth && x.loopPoint == y.loopPoint
                             && sameVector (x.points, y.points, [] (const ContentPoint& p, const ContentPoint& q) { return p == q; }); })
        && sameVector (a.velocityCurve, b.velocityCurve, [] (const ContentPoint& p, const ContentPoint& q) { return p == q; })
        && sameVector (a.noteCurve, b.noteCurve, [] (const ContentPoint& p, const ContentPoint& q) { return p == q; })
        && a.tuning.present == b.tuning.present && a.tuning.scl == b.tuning.scl && a.tuning.hasKbm == b.tuning.hasKbm && a.tuning.kbm == b.tuning.kbm
        && sameVector (a.impulses, b.impulses, [] (const ContentFileRef& x, const ContentFileRef& y) { return same (x, y); })
        && sameVector (a.filterCurves, b.filterCurves, [] (const ContentFilterCurve& x, const ContentFilterCurve& y)
                       { return x.filter == y.filter
                             && sameVector (x.points, y.points, [] (const ContentFilterCurvePoint& p, const ContentFilterCurvePoint& q)
                                            { return p.x == q.x && p.y == q.y; }); });
}

bool validateContent (const StateContent& c) noexcept
{
    try
    {
        if (c.wavetables.size() > static_cast<size_t> (kMaxContentWavetables))
            return false;
        std::set<int> oscs;
        for (const auto& w : c.wavetables)
            if (w.osc < 1 || w.osc > 3 || ! oscs.insert (w.osc).second || ! refOk (w.ref) || ! plainString (w.author, kMaxContentStringBytes)
                || w.frames < 0 || w.frames > 4096)
                return false;

        if (c.samples.size() > static_cast<size_t> (kMaxContentSamples))
            return false;
        oscs.clear();
        for (const auto& s : c.samples)
        {
            if (s.osc < 1 || s.osc > 3 || ! oscs.insert (s.osc).second || ! refOk (s.ref) || s.sampleRate < 0 || s.sampleRate > 768000
                || s.frames < 0 || s.frames > (1 << 30) || s.channels < 0 || s.channels > 2 || s.slices.size() > static_cast<size_t> (kMaxSampleSlices))
                return false;
            float previous = 0.0f;
            for (const float v : s.slices)
            {
                if (! inRange (v, 0.0, 1.0) || v < previous)
                    return false;
                previous = v;
            }
        }

        if (c.lfoShapes.size() > static_cast<size_t> (kMaxContentLfoShapes))
            return false;
        std::set<int> lfos;
        for (const auto& l : c.lfoShapes)
            if (l.lfo < 1 || l.lfo > kMaxContentLfoShapes || ! lfos.insert (l.lfo).second || ! plainString (l.name, kMaxContentStringBytes)
                || ! pointsOk (l.points, kMaxLfoShapePoints, true, true) || l.loopPoint < -1 || l.loopPoint >= static_cast<int> (l.points.size()))
                return false;

        if (! pointsOk (c.velocityCurve, kMaxCurvePoints, false, false) || ! pointsOk (c.noteCurve, kMaxCurvePoints, false, false))
            return false;

        if (c.tuning.present)
        {
            const auto text = [] (const std::string& s, size_t maxBytes)
            {
                if (s.size() > maxBytes)
                    return false;
                for (const char ch : s)
                {
                    const auto u = static_cast<unsigned char> (ch);
                    if (u < 0x20 && ch != '\n' && ch != '\r' && ch != '\t')
                        return false;
                }
                return true;
            };
            if (! text (c.tuning.scl, kMaxTuningSclBytes) || ! text (c.tuning.kbm, kMaxTuningKbmBytes) || (! c.tuning.hasKbm && ! c.tuning.kbm.empty()))
                return false;
        }
        else if (c.tuning.hasKbm || ! c.tuning.scl.empty() || ! c.tuning.kbm.empty())
        {
            return false;
        }

        if (c.impulses.size() > static_cast<size_t> (kMaxContentImpulses))
            return false;
        for (const auto& r : c.impulses)
            if (! refOk (r))
                return false;

        if (c.filterCurves.size() > static_cast<size_t> (kMaxContentFilterCurves))
            return false;
        std::set<std::string> filters;
        for (const auto& f : c.filterCurves)
        {
            if ((f.filter != "filter1" && f.filter != "filter2" && f.filter != "fxfilter") || ! filters.insert (f.filter).second || f.points.size() < 2
                || f.points.size() > static_cast<size_t> (kMaxFilterCurvePoints))
                return false;
            float previous = 0.0f;
            for (const auto& p : f.points)
            {
                if (! inRange (p.x, 0.0, 1.0) || ! inRange (p.y, -60.0, 60.0) || p.x < previous)
                    return false;
                previous = p.x;
            }
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}
} // namespace synthyboy
