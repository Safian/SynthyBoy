// SynthyBoy - core: versioned state (deterministic JSON text) with a bounded hand-written parser.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/modulation.h"
#include "synthyboy/state.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>

#include "number.h"

namespace synthyboy
{
namespace
{
constexpr const char* kFormatName = "synthyboy-state";
constexpr int kMaxDepth = 8;
constexpr size_t kMaxKeyChars = 63;

/** What the parser collected; indexed like paramTable(). Migrations rewrite it before it is applied. */
struct Parsed
{
    double value[kMaxParams] = {};
    bool has[kMaxParams] = {};
    bool derived[kMaxParams] = {}; // set by a migration, not taken from the document: not counted as applied
    int version = 0;

    /** The content section (state version 3): its status, and its data when the caller asked for it (loadStateWithContent). */
    ContentStatus contentStatus = ContentStatus::absent;
    StateContent content;

    /** The three keys of format version 1 ("gain", "waveform", "cutoff"), collected whatever the version says (the key
        order of a document is free); only a version 1 document is migrated from them. */
    double legacyValue[3] = {};
    bool legacyHas[3] = {};
};

constexpr const char* kLegacyKeys[3] = { "gain", "waveform", "cutoff" };

int legacyIndex (const char* key) noexcept
{
    for (int i = 0; i < 3; ++i)
        if (std::strcmp (key, kLegacyKeys[i]) == 0)
            return i;
    return -1;
}

void setParsed (Parsed& p, uint32_t id, double v) noexcept
{
    const int i = findParamIndex (id);
    if (i >= 0)
    {
        p.value[i] = v;
        p.has[i] = true;
    }
}

/** Version 1 -> 2. The three parameters of version 1 became:
      gain     (dB, -60..6)        -> master.volume  (dB, -60..6): the same unit and range, the value is carried over unchanged
      waveform (0 Sine, 1 Saw)     -> osc1.wavetable (index 0 = Sine, 1 = Saw: the first two built-in wavetables, same indices)
      cutoff   (Hz, 20..20000)     -> filter1.cutoff (Hz, 20..20000, same value) and filter1.on = 1 (the old filter was always on) */
void migrateV1ToV2 (Parsed& p) noexcept
{
    if (p.legacyHas[0])
        setParsed (p, kParamMasterVolume, p.legacyValue[0]);
    if (p.legacyHas[1])
        setParsed (p, kParamOsc1Wavetable, p.legacyValue[1] < 0.5 ? 0.0 : 1.0); // v1 knew two values; anything else is the saw
    if (p.legacyHas[2])
    {
        setParsed (p, kParamFilter1Cutoff, p.legacyValue[2]);
        setParsed (p, kParamFilter1On, 1.0);
    }
}

/** Version 2 -> 3. Nothing is renamed or rescaled (the 604 keys of version 3 are appended, every choice list only grew); a document of
    version 2 loads with each new key at the value it already has in the caller's ParamValues, except the seven fx.order.10 .. 16 that
    complete the 16-effect permutation: they are written explicitly (identity: slot 10 = effect 9 ... slot 16 = effect 15), so a
    version 2 patch always runs its nine effects in the order it stored, whatever chain the previous patch had. They are derived
    values: not counted as applied. */
void migrateV2ToV3 (Parsed& p) noexcept
{
    char key[16];
    for (int slot = kNumLegacyFxSlots + 1; slot <= kNumFxSlots; ++slot)
    {
        std::snprintf (key, sizeof key, "fx.order.%d", slot);
        const int i = findParamIndex (hashKey (key));
        if (i >= 0)
        {
            p.value[i] = static_cast<double> (slot - 1);
            p.has[i] = true;
            p.derived[i] = true;
        }
    }
}

/** A migration upgrades `Parsed` from version `from` to `from + 1` (rename keys, rescale values, ...). Later versions
    append entries here. */
using MigrateFn = void (*) (Parsed&) noexcept;
struct Migration
{
    int from;
    MigrateFn apply;
};
constexpr std::array<Migration, 2> kMigrations { { { 1, migrateV1ToV2 }, { 2, migrateV2ToV3 } } };

void migrate (Parsed& parsed) noexcept
{
    for (int v = parsed.version; v < kStateVersion; ++v)
        for (const auto& m : kMigrations)
            if (m.from == v)
                m.apply (parsed);
}

/** REMAINDER item 1 (13-new-keys.md 4.41): the RISE / FALL of a modulation slot used to be stored in the slot's record of content.mod_curves ("rise" / "fall", ms,
    GROUPB WP-08). They are the keys modNN.rise / modNN.fall now. A document that still holds them gets the values written into the keys (unless the document states the
    key itself), the record loses them and is dropped when it holds no points: the sound is the one of the old document, a save writes the keys only. */
void migrateSlewRecords (Parsed& p) noexcept
{
    char key[24];
    auto& records = p.content.modCurves;
    for (auto it = records.begin(); it != records.end();)
    {
        if (it->slot >= 1 && it->slot <= kNumModSlots)
        {
            const float legacy[2] = { it->riseMs, it->fallMs };
            const char* const names[2] = { "rise", "fall" };
            for (int q = 0; q < 2; ++q)
            {
                std::snprintf (key, sizeof key, "mod%02d.%s", it->slot, names[q]);
                const int i = findParamIndex (hashKey (key));
                if (i >= 0 && legacy[q] > 0.0f && ! p.has[i])
                {
                    p.value[i] = static_cast<double> (legacy[q]);
                    p.has[i] = true;
                    p.derived[i] = true;
                }
            }
        }
        it->riseMs = 0.0f;
        it->fallMs = 0.0f;
        it = it->points.empty() ? records.erase (it) : it + 1;
    }
}

class Parser
{
public:
    Parser (const char* b, const char* e, bool takeContent = false) noexcept : p (b), end (e), wantContent (takeContent) {}

    bool parseDocument (Parsed& out, bool& formatOk, bool& versionSeen) noexcept
    {
        if (end - p >= 3 && static_cast<unsigned char> (p[0]) == 0xef && static_cast<unsigned char> (p[1]) == 0xbb
            && static_cast<unsigned char> (p[2]) == 0xbf)
            p += 3; // UTF-8 byte order mark

        skipWs();
        if (! consume ('{'))
            return false;

        skipWs();
        if (! consume ('}'))
        {
            for (;;)
            {
                char key[kMaxKeyChars + 1];
                bool truncated = false;
                skipWs();
                if (! parseString (key, sizeof key, truncated))
                    return false;
                skipWs();
                if (! consume (':'))
                    return false;
                skipWs();

                if (! truncated && std::strcmp (key, "format") == 0)
                {
                    char text[32];
                    bool tooLong = false;
                    if (p < end && *p == '"')
                    {
                        if (! parseString (text, sizeof text, tooLong))
                            return false;
                        formatOk = ! tooLong && std::strcmp (text, kFormatName) == 0;
                    }
                    else
                    {
                        formatOk = false;
                        if (! skipValue (1))
                            return false;
                    }
                }
                else if (! truncated && std::strcmp (key, "version") == 0)
                {
                    double v = 0.0;
                    if (! parseNumber (v) || v != std::floor (v) || v < 1.0 || v > 1.0e9)
                        return false;
                    out.version = static_cast<int> (v);
                    versionSeen = true;
                }
                else if (! truncated && std::strcmp (key, "params") == 0 && p < end && *p == '{')
                {
                    if (! parseParams (out))
                        return false;
                }
                else if (! truncated && std::strcmp (key, "content") == 0)
                {
                    // whether a version 1 / 2 document may carry it is decided in the loader (the version key can come later)
                    if (! parseContentSection (out))
                        return false;
                }
                else if (! skipValue (1))
                {
                    return false;
                }

                skipWs();
                if (consume (','))
                    continue;
                if (consume ('}'))
                    break;
                return false;
            }
        }

        skipWs();
        return p == end; // nothing but white space may follow
    }

private:
    const char* p;
    const char* end;
    bool wantContent;
    bool contentBad = false; // a value of the content section was outside what content_state.h allows

    bool consume (char c) noexcept
    {
        if (p < end && *p == c)
        {
            ++p;
            return true;
        }
        return false;
    }

    void skipWs() noexcept
    {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            ++p;
    }

    static int hexValue (char c) noexcept
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    /** Parses a JSON string at p (which must be '"'). put (char) receives the decoded bytes, putCode (int) a \u escape. */
    template <typename Put, typename PutCode>
    bool parseStringWith (Put&& put, PutCode&& putCode) noexcept(noexcept (put ('x')) && noexcept (putCode (0)))
    {
        if (! consume ('"'))
            return false;

        while (p < end)
        {
            const unsigned char c = static_cast<unsigned char> (*p++);
            if (c == '"')
                return true;
            if (c < 0x20)
                return false; // raw control characters are not allowed in JSON strings
            if (c != '\\')
            {
                put (static_cast<char> (c));
                continue;
            }

            if (p >= end)
                return false;
            const char e = *p++;
            switch (e)
            {
                case '"': put ('"'); break;
                case '\\': put ('\\'); break;
                case '/': put ('/'); break;
                case 'b': put ('\b'); break;
                case 'f': put ('\f'); break;
                case 'n': put ('\n'); break;
                case 'r': put ('\r'); break;
                case 't': put ('\t'); break;
                case 'u':
                {
                    if (end - p < 4)
                        return false;
                    int code = 0;
                    for (int i = 0; i < 4; ++i)
                    {
                        const int h = hexValue (p[i]);
                        if (h < 0)
                            return false;
                        code = code * 16 + h;
                    }
                    p += 4;
                    putCode (code);
                    break;
                }
                default: return false;
            }
        }
        return false; // unterminated
    }

    /** Copies at most cap - 1 decoded characters into out (out may be null to just validate); truncated is set if more were present. */
    bool parseString (char* out, size_t cap, bool& truncated) noexcept
    {
        truncated = false;
        size_t n = 0;
        auto put = [&] (char c) noexcept
        {
            if (out != nullptr && n + 1 < cap)
                out[n++] = c;
            else
                truncated = true;
        };
        const bool ok = parseStringWith (put, [&] (int code) noexcept { put (code > 0 && code < 0x80 ? static_cast<char> (code) : '\x01'); }); // keys are ASCII
        if (ok && out != nullptr && cap > 0)
            out[n] = '\0';
        return ok;
    }

    /** A string of the content section: UTF-8, at most maxBytes (longer: tooLong, the rest is dropped). Not noexcept (allocates). */
    bool parseStringInto (std::string& out, size_t maxBytes, bool& tooLong)
    {
        out.clear();
        tooLong = false;
        auto put = [&] (char c)
        {
            if (out.size() < maxBytes)
                out.push_back (c);
            else
                tooLong = true;
        };
        return parseStringWith (put, [&] (int code)
        {
            if (code >= 0xd800 && code <= 0xdfff)
                code = 0xfffd; // a lone surrogate half
            if (code == 0)
                code = 0xfffd;
            if (code < 0x80)
                put (static_cast<char> (code));
            else if (code < 0x800)
            {
                put (static_cast<char> (0xc0 | (code >> 6)));
                put (static_cast<char> (0x80 | (code & 0x3f)));
            }
            else
            {
                put (static_cast<char> (0xe0 | (code >> 12)));
                put (static_cast<char> (0x80 | ((code >> 6) & 0x3f)));
                put (static_cast<char> (0x80 | (code & 0x3f)));
            }
        });
    }

    bool parseNumber (double& v) noexcept
    {
        const char* next = nullptr;
        if (! number::parseDecimal (p, end, true, v, next))
            return false;
        p = next;
        return true;
    }

    bool skipLiteral (const char* word) noexcept
    {
        const size_t n = std::strlen (word);
        if (static_cast<size_t> (end - p) < n || std::memcmp (p, word, n) != 0)
            return false;
        p += n;
        return true;
    }

    /** Validates and skips one JSON value; depth is the nesting level of this value. */
    bool skipValue (int depth) noexcept
    {
        if (p >= end)
            return false;

        switch (*p)
        {
            case '{':
            {
                if (depth > kMaxDepth)
                    return false;
                ++p;
                skipWs();
                if (consume ('}'))
                    return true;
                for (;;)
                {
                    bool truncated = false;
                    skipWs();
                    if (! parseString (nullptr, 0, truncated))
                        return false;
                    skipWs();
                    if (! consume (':'))
                        return false;
                    skipWs();
                    if (! skipValue (depth + 1))
                        return false;
                    skipWs();
                    if (consume (','))
                        continue;
                    return consume ('}');
                }
            }
            case '[':
            {
                if (depth > kMaxDepth)
                    return false;
                ++p;
                skipWs();
                if (consume (']'))
                    return true;
                for (;;)
                {
                    skipWs();
                    if (! skipValue (depth + 1))
                        return false;
                    skipWs();
                    if (consume (','))
                        continue;
                    return consume (']');
                }
            }
            case '"':
            {
                bool truncated = false;
                return parseString (nullptr, 0, truncated);
            }
            case 't': return skipLiteral ("true");
            case 'f': return skipLiteral ("false");
            case 'n': return skipLiteral ("null");
            default:
            {
                double ignored = 0.0;
                return parseNumber (ignored);
            }
        }
    }

    // ---- the content section (typed reading; allocates, so only when the caller asked for the content) --------------------------
    // A value of the wrong type or outside the caps is skipped and marks the section bad (contentBad): the section is then dropped
    // as a whole but the document stays valid. Only malformed JSON invalidates the document. depth is the nesting level of the
    // value at p (the document's top level members are at 1, kMaxDepth bounds the recursion).

    bool readNumber (double& v, int depth)
    {
        if (p < end && (*p == '-' || (*p >= '0' && *p <= '9')))
        {
            if (! parseNumber (v))
                return false;
            if (! std::isfinite (v))
            {
                contentBad = true;
                v = 0.0;
            }
            return true;
        }
        contentBad = true;
        return skipValue (depth);
    }

    bool readInt (int& out, int depth)
    {
        double v = 0.0;
        if (! readNumber (v, depth))
            return false;
        if (v != std::floor (v) || v < -2147483648.0 || v > 2147483647.0)
        {
            contentBad = true;
            return true;
        }
        out = static_cast<int> (v);
        return true;
    }

    bool readFloat (float& out, int depth)
    {
        double v = 0.0;
        if (! readNumber (v, depth))
            return false;
        const float f = static_cast<float> (v);
        if (! std::isfinite (f))
        {
            contentBad = true;
            return true;
        }
        out = f;
        return true;
    }

    bool readBool (bool& out, int depth)
    {
        if (p < end && *p == 't')
        {
            out = true;
            return skipLiteral ("true");
        }
        if (p < end && *p == 'f')
        {
            out = false;
            return skipLiteral ("false");
        }
        contentBad = true;
        return skipValue (depth);
    }

    bool readText (std::string& out, size_t maxBytes, int depth)
    {
        if (p < end && *p == '"')
        {
            bool tooLong = false;
            if (! parseStringInto (out, maxBytes, tooLong))
                return false;
            if (tooLong)
                contentBad = true;
            return true;
        }
        contentBad = true;
        return skipValue (depth);
    }

    /** onMember (key, valueDepth, handled): reads the value of a key it knows (and sets handled), or leaves it to be skipped. */
    template <typename F>
    bool readObject (int depth, F&& onMember)
    {
        if (depth > kMaxDepth)
            return false;
        if (p >= end || *p != '{')
        {
            contentBad = true;
            return skipValue (depth);
        }
        ++p;
        skipWs();
        if (consume ('}'))
            return true;
        for (;;)
        {
            char key[kMaxKeyChars + 1];
            bool truncated = false;
            skipWs();
            if (! parseString (key, sizeof key, truncated))
                return false;
            skipWs();
            if (! consume (':'))
                return false;
            skipWs();

            bool handled = false;
            if (! truncated && ! onMember (static_cast<const char*> (key), depth + 1, handled))
                return false;
            if (! handled && ! skipValue (depth + 1))
                return false;

            skipWs();
            if (consume (','))
                continue;
            return consume ('}');
        }
    }

    /** onElement (index, elementDepth) must read the element. At most maxElements are read; further ones are skipped and mark the section bad. */
    template <typename F>
    bool readArray (int depth, size_t maxElements, F&& onElement)
    {
        if (depth > kMaxDepth)
            return false;
        if (p >= end || *p != '[')
        {
            contentBad = true;
            return skipValue (depth);
        }
        ++p;
        skipWs();
        if (consume (']'))
            return true;
        for (size_t i = 0;; ++i)
        {
            skipWs();
            if (i >= maxElements)
            {
                contentBad = true;
                if (! skipValue (depth + 1))
                    return false;
            }
            else if (! onElement (i, depth + 1))
            {
                return false;
            }
            skipWs();
            if (consume (','))
                continue;
            return consume (']');
        }
    }

    bool readFileRefMember (ContentFileRef& r, const char* key, int depth, bool& handled)
    {
        std::string* field = std::strcmp (key, "pack") == 0 ? &r.pack : std::strcmp (key, "file") == 0 ? &r.file
                           : std::strcmp (key, "sha256") == 0 ? &r.sha256 : std::strcmp (key, "name") == 0 ? &r.name : nullptr;
        if (field == nullptr)
            return true;
        handled = true;
        return readText (*field, kMaxContentStringBytes, depth);
    }

    bool readFileRef (ContentFileRef& r, int depth)
    {
        return readObject (depth, [&] (const char* key, int d, bool& handled) { return readFileRefMember (r, key, d, handled); });
    }

    bool readMod (ContentModRef& m, int depth)
    {
        m.present = true;
        return readObject (depth, [&] (const char* key, int d, bool& handled)
        {
            handled = true;
            if (std::strcmp (key, "source") == 0)
                return readText (m.source, kMaxContentStringBytes, d);
            if (std::strcmp (key, "amount") == 0)
                return readFloat (m.amount, d);
            handled = false;
            return true;
        });
    }

    bool readPoint (ContentPoint& pt, int depth, bool withMod)
    {
        return readObject (depth, [&] (const char* key, int d, bool& handled)
        {
            handled = true;
            if (std::strcmp (key, "x") == 0)
                return readFloat (pt.x, d);
            if (std::strcmp (key, "y") == 0)
                return readFloat (pt.y, d);
            if (std::strcmp (key, "power") == 0)
                return readFloat (pt.power, d);
            if (withMod && std::strcmp (key, "mod_x") == 0)
                return readMod (pt.modX, d);
            if (withMod && std::strcmp (key, "mod_y") == 0)
                return readMod (pt.modY, d);
            handled = false;
            return true;
        });
    }

    bool readPoints (std::vector<ContentPoint>& points, size_t maxPoints, int depth, bool withMod)
    {
        return readArray (depth, maxPoints, [&] (size_t, int d)
        {
            points.emplace_back();
            return readPoint (points.back(), d, withMod);
        });
    }

    /** {"points":[...]} of a velocity / note curve. */
    bool readCurve (std::vector<ContentPoint>& points, int depth)
    {
        return readObject (depth, [&] (const char* key, int d, bool& handled)
        {
            if (std::strcmp (key, "points") != 0)
                return true;
            handled = true;
            return readPoints (points, static_cast<size_t> (kMaxCurvePoints), d, false);
        });
    }

    bool readContent (StateContent& c)
    {
        return readObject (1, [&] (const char* key, int d, bool& handled)
        {
            handled = true;
            if (std::strcmp (key, "wavetables") == 0)
                return readArray (d, static_cast<size_t> (kMaxContentWavetables), [&] (size_t, int ed)
                {
                    c.wavetables.emplace_back();
                    auto& w = c.wavetables.back();
                    return readObject (ed, [&] (const char* k, int vd, bool& h)
                    {
                        if (std::strcmp (k, "osc") == 0) { h = true; return readInt (w.osc, vd); }
                        if (std::strcmp (k, "author") == 0) { h = true; return readText (w.author, kMaxContentStringBytes, vd); }
                        if (std::strcmp (k, "frames") == 0) { h = true; return readInt (w.frames, vd); }
                        return readFileRefMember (w.ref, k, vd, h);
                    });
                });
            if (std::strcmp (key, "samples") == 0)
                return readArray (d, static_cast<size_t> (kMaxContentSamples), [&] (size_t, int ed)
                {
                    c.samples.emplace_back();
                    auto& s = c.samples.back();
                    return readObject (ed, [&] (const char* k, int vd, bool& h)
                    {
                        if (std::strcmp (k, "osc") == 0) { h = true; return readInt (s.osc, vd); }
                        if (std::strcmp (k, "sample_rate") == 0) { h = true; return readInt (s.sampleRate, vd); }
                        if (std::strcmp (k, "frames") == 0) { h = true; return readInt (s.frames, vd); }
                        if (std::strcmp (k, "channels") == 0) { h = true; return readInt (s.channels, vd); }
                        if (std::strcmp (k, "slices") == 0)
                        {
                            h = true;
                            return readArray (vd, static_cast<size_t> (kMaxSampleSlices), [&] (size_t, int sd)
                            {
                                float v = 0.0f;
                                if (! readFloat (v, sd))
                                    return false;
                                s.slices.push_back (v);
                                return true;
                            });
                        }
                        return readFileRefMember (s.ref, k, vd, h);
                    });
                });
            if (std::strcmp (key, "lfo_shapes") == 0)
                return readArray (d, static_cast<size_t> (kMaxContentLfoShapes), [&] (size_t, int ed)
                {
                    c.lfoShapes.emplace_back();
                    auto& l = c.lfoShapes.back();
                    return readObject (ed, [&] (const char* k, int vd, bool& h)
                    {
                        h = true;
                        if (std::strcmp (k, "lfo") == 0) return readInt (l.lfo, vd);
                        if (std::strcmp (k, "name") == 0) return readText (l.name, kMaxContentStringBytes, vd);
                        if (std::strcmp (k, "smooth") == 0) return readBool (l.smooth, vd);
                        if (std::strcmp (k, "loop_point") == 0) return readInt (l.loopPoint, vd);
                        if (std::strcmp (k, "points") == 0) return readPoints (l.points, static_cast<size_t> (kMaxLfoShapePoints), vd, true);
                        h = false;
                        return true;
                    });
                });
            if (std::strcmp (key, "curves") == 0)
                return readObject (d, [&] (const char* k, int vd, bool& h)
                {
                    h = true;
                    if (std::strcmp (k, "velocity") == 0) return readCurve (c.velocityCurve, vd);
                    if (std::strcmp (k, "note") == 0) return readCurve (c.noteCurve, vd);
                    if (std::strcmp (k, "shaper_a") == 0) return readCurve (c.shaperA, vd);
                    if (std::strcmp (k, "shaper_b") == 0) return readCurve (c.shaperB, vd);
                    h = false;
                    return true;
                });
            if (std::strcmp (key, "tuning") == 0)
            {
                c.tuning.present = true;
                return readObject (d, [&] (const char* k, int vd, bool& h)
                {
                    h = true;
                    if (std::strcmp (k, "scl") == 0) return readText (c.tuning.scl, kMaxTuningSclBytes, vd);
                    if (std::strcmp (k, "kbm") == 0)
                    {
                        if (p < end && *p == 'n') // null: no keyboard mapping
                            return skipLiteral ("null");
                        c.tuning.hasKbm = true;
                        return readText (c.tuning.kbm, kMaxTuningKbmBytes, vd);
                    }
                    h = false;
                    return true;
                });
            }
            if (std::strcmp (key, "impulses") == 0)
                return readArray (d, static_cast<size_t> (kMaxContentImpulses), [&] (size_t, int ed)
                {
                    c.impulses.emplace_back();
                    return readFileRef (c.impulses.back(), ed);
                });
            if (std::strcmp (key, "filter_curves") == 0)
                return readArray (d, static_cast<size_t> (kMaxContentFilterCurves), [&] (size_t, int ed)
                {
                    c.filterCurves.emplace_back();
                    auto& f = c.filterCurves.back();
                    return readObject (ed, [&] (const char* k, int vd, bool& h)
                    {
                        h = true;
                        if (std::strcmp (k, "filter") == 0) return readText (f.filter, kMaxContentStringBytes, vd);
                        if (std::strcmp (k, "points") == 0)
                            return readArray (vd, static_cast<size_t> (kMaxFilterCurvePoints), [&] (size_t, int pd)
                            {
                                f.points.emplace_back();
                                auto& pt = f.points.back();
                                return readObject (pd, [&] (const char* pk, int xd, bool& ph)
                                {
                                    ph = true;
                                    if (std::strcmp (pk, "x") == 0) return readFloat (pt.x, xd);
                                    if (std::strcmp (pk, "y") == 0) return readFloat (pt.y, xd);
                                    ph = false;
                                    return true;
                                });
                            });
                        h = false;
                        return true;
                    });
                });
            if (std::strcmp (key, "remaps") == 0)
                return readArray (d, static_cast<size_t> (kMaxContentRemaps), [&] (size_t, int ed)
                {
                    c.remaps.emplace_back();
                    auto& r = c.remaps.back();
                    return readObject (ed, [&] (const char* k, int vd, bool& h)
                    {
                        h = true;
                        if (std::strcmp (k, "osc") == 0) return readInt (r.osc, vd);
                        if (std::strcmp (k, "slot") == 0) return readInt (r.slot, vd);
                        if (std::strcmp (k, "name") == 0) return readText (r.name, kMaxContentStringBytes, vd);
                        if (std::strcmp (k, "points") == 0) return readPoints (r.points, static_cast<size_t> (kMaxRemapPoints), vd, false);
                        h = false;
                        return true;
                    });
                });
            if (std::strcmp (key, "spectral_filters") == 0)
                return readArray (d, static_cast<size_t> (kMaxContentSpectralFilters), [&] (size_t, int ed)
                {
                    c.spectralFilters.emplace_back();
                    auto& r = c.spectralFilters.back();
                    r.slot = 1;
                    return readObject (ed, [&] (const char* k, int vd, bool& h)
                    {
                        h = true;
                        if (std::strcmp (k, "osc") == 0) return readInt (r.osc, vd);
                        if (std::strcmp (k, "name") == 0) return readText (r.name, kMaxContentStringBytes, vd);
                        if (std::strcmp (k, "points") == 0) return readPoints (r.points, static_cast<size_t> (kMaxRemapPoints), vd, false);
                        h = false;
                        return true;
                    });
                });
            if (std::strcmp (key, "instruments") == 0)
                return readArray (d, static_cast<size_t> (kMaxContentInstruments), [&] (size_t, int ed)
                {
                    c.instruments.emplace_back();
                    auto& in = c.instruments.back();
                    return readObject (ed, [&] (const char* k, int vd, bool& h)
                    {
                        if (std::strcmp (k, "osc") == 0) { h = true; return readInt (in.osc, vd); }
                        if (std::strcmp (k, "zones") == 0) { h = true; return readInt (in.zones, vd); }
                        return readFileRefMember (in.ref, k, vd, h);
                    });
                });
            if (std::strcmp (key, "mod_curves") == 0)
                return readArray (d, static_cast<size_t> (kMaxContentModCurves), [&] (size_t, int ed)
                {
                    c.modCurves.emplace_back();
                    auto& mc = c.modCurves.back();
                    return readObject (ed, [&] (const char* k, int vd, bool& h)
                    {
                        h = true;
                        if (std::strcmp (k, "slot") == 0) return readInt (mc.slot, vd);
                        if (std::strcmp (k, "points") == 0) return readPoints (mc.points, static_cast<size_t> (kMaxModCurvePoints), vd, false);
                        if (std::strcmp (k, "rise") == 0) return readFloat (mc.riseMs, vd);   // GROUPB WP-08
                        if (std::strcmp (k, "fall") == 0) return readFloat (mc.fallMs, vd);
                        h = false;
                        return true;
                    });
                });
            handled = false;
            return true;
        });
    }

    bool parseContentSection (Parsed& out)
    {
        if (! wantContent)
        {
            out.contentStatus = ContentStatus::ignored;
            return skipValue (1);
        }

        const char* start = p;
        contentBad = false;
        StateContent c;
        if (! readContent (c))
            return false; // malformed JSON
        if (contentBad || ! validateContent (c))
        {
            p = start;
            out.contentStatus = ContentStatus::rejected;
            out.content.clear();
            return skipValue (1);
        }
        out.contentStatus = ContentStatus::ok;
        out.content = std::move (c);
        return true;
    }

    /** p is at the '{' of the "params" object. Numbers for known keys are collected, everything else is skipped. */
    bool parseParams (Parsed& out) noexcept
    {
        consume ('{');
        skipWs();
        if (consume ('}'))
            return true;

        for (;;)
        {
            char key[kMaxKeyChars + 1];
            bool truncated = false;
            skipWs();
            if (! parseString (key, sizeof key, truncated))
                return false;
            skipWs();
            if (! consume (':'))
                return false;
            skipWs();

            int index = -1;
            int legacy = -1;
            if (! truncated)
            {
                if (const auto* info = findParamByKey (key))
                    index = findParamIndex (info->id);
                else
                    legacy = legacyIndex (key);
            }

            if ((index >= 0 || legacy >= 0) && p < end && (*p == '-' || (*p >= '0' && *p <= '9')))
            {
                double v = 0.0;
                if (! parseNumber (v))
                    return false;
                if (index >= 0)
                {
                    out.value[index] = v; // duplicate keys: the last one wins
                    out.has[index] = true;
                }
                else
                {
                    out.legacyValue[legacy] = v;
                    out.legacyHas[legacy] = true;
                }
            }
            else if (! skipValue (2))
            {
                return false;
            }

            skipWs();
            if (consume (','))
                continue;
            return consume ('}');
        }
    }
};
} // namespace

ParamValues ParamValues::defaults() noexcept
{
    ParamValues v;
    const auto* table = paramTable();
    for (size_t i = 0; i < paramCount(); ++i)
        v.plain[i] = table[i].def;
    return v;
}

namespace
{
void putJsonString (std::string& s, const std::string& text)
{
    s += '"';
    for (const char ch : text)
    {
        const auto u = static_cast<unsigned char> (ch);
        switch (ch)
        {
            case '"': s += "\\\""; break;
            case '\\': s += "\\\\"; break;
            case '\n': s += "\\n"; break;
            case '\r': s += "\\r"; break;
            case '\t': s += "\\t"; break;
            default:
                if (u < 0x20)
                {
                    char esc[8];
                    std::snprintf (esc, sizeof esc, "\\u%04x", static_cast<unsigned> (u));
                    s += esc;
                }
                else
                {
                    s += ch; // UTF-8 bytes pass through
                }
        }
    }
    s += '"';
}

void putJsonFloat (std::string& s, float v)
{
    char buf[32];
    number::formatG9 (v, buf, sizeof buf);
    s += buf;
}

void putJsonInt (std::string& s, int v)
{
    s += std::to_string (v);
}

void putKey (std::string& s, const char* key)
{
    s += '"';
    s += key;
    s += "\":";
}

void putFileRefFields (std::string& s, const ContentFileRef& r)
{
    putKey (s, "pack");
    putJsonString (s, r.pack);
    s += ',';
    putKey (s, "file");
    putJsonString (s, r.file);
    s += ',';
    putKey (s, "sha256");
    putJsonString (s, r.sha256);
    s += ',';
    putKey (s, "name");
    putJsonString (s, r.name);
}

void putMod (std::string& s, const char* key, const ContentModRef& m)
{
    if (! m.present)
        return;
    s += ',';
    putKey (s, key);
    s += '{';
    putKey (s, "source");
    putJsonString (s, m.source);
    s += ',';
    putKey (s, "amount");
    putJsonFloat (s, m.amount);
    s += '}';
}

void putPoints (std::string& s, const std::vector<ContentPoint>& points)
{
    s += '[';
    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto& p = points[i];
        if (i > 0)
            s += ',';
        s += '{';
        putKey (s, "x");
        putJsonFloat (s, p.x);
        s += ',';
        putKey (s, "y");
        putJsonFloat (s, p.y);
        s += ',';
        putKey (s, "power");
        putJsonFloat (s, p.power);
        putMod (s, "mod_x", p.modX);
        putMod (s, "mod_y", p.modY);
        s += '}';
    }
    s += ']';
}

/** The content section in its fixed order; only the non-empty parts. */
void putContent (std::string& s, const StateContent& c)
{
    s += ",\"content\":{";
    bool first = true;
    const auto section = [&] (const char* name)
    {
        if (! first)
            s += ',';
        first = false;
        putKey (s, name);
    };

    if (! c.wavetables.empty())
    {
        section ("wavetables");
        s += '[';
        for (size_t i = 0; i < c.wavetables.size(); ++i)
        {
            const auto& w = c.wavetables[i];
            s += i > 0 ? ",{" : "{";
            putKey (s, "osc");
            putJsonInt (s, w.osc);
            s += ',';
            putFileRefFields (s, w.ref);
            s += ',';
            putKey (s, "author");
            putJsonString (s, w.author);
            s += ',';
            putKey (s, "frames");
            putJsonInt (s, w.frames);
            s += '}';
        }
        s += ']';
    }
    if (! c.samples.empty())
    {
        section ("samples");
        s += '[';
        for (size_t i = 0; i < c.samples.size(); ++i)
        {
            const auto& m = c.samples[i];
            s += i > 0 ? ",{" : "{";
            putKey (s, "osc");
            putJsonInt (s, m.osc);
            s += ',';
            putFileRefFields (s, m.ref);
            s += ',';
            putKey (s, "sample_rate");
            putJsonInt (s, m.sampleRate);
            s += ',';
            putKey (s, "frames");
            putJsonInt (s, m.frames);
            s += ',';
            putKey (s, "channels");
            putJsonInt (s, m.channels);
            s += ',';
            putKey (s, "slices");
            s += '[';
            for (size_t k = 0; k < m.slices.size(); ++k)
            {
                if (k > 0)
                    s += ',';
                putJsonFloat (s, m.slices[k]);
            }
            s += "]}";
        }
        s += ']';
    }
    if (! c.lfoShapes.empty())
    {
        section ("lfo_shapes");
        s += '[';
        for (size_t i = 0; i < c.lfoShapes.size(); ++i)
        {
            const auto& l = c.lfoShapes[i];
            s += i > 0 ? ",{" : "{";
            putKey (s, "lfo");
            putJsonInt (s, l.lfo);
            s += ',';
            putKey (s, "name");
            putJsonString (s, l.name);
            s += ',';
            putKey (s, "smooth");
            s += l.smooth ? "true" : "false";
            if (l.loopPoint >= 0)
            {
                s += ',';
                putKey (s, "loop_point");
                putJsonInt (s, l.loopPoint);
            }
            s += ',';
            putKey (s, "points");
            putPoints (s, l.points);
            s += '}';
        }
        s += ']';
    }
    if (! c.velocityCurve.empty() || ! c.noteCurve.empty() || ! c.shaperA.empty() || ! c.shaperB.empty())
    {
        section ("curves");
        s += '{';
        bool inner = false;
        const auto curve = [&] (const char* name, const std::vector<ContentPoint>& pts)
        {
            if (pts.empty())
                return;
            if (inner)
                s += ',';
            inner = true;
            putKey (s, name);
            s += "{\"points\":";
            putPoints (s, pts);
            s += '}';
        };
        curve ("velocity", c.velocityCurve);
        curve ("note", c.noteCurve);
        curve ("shaper_a", c.shaperA);
        curve ("shaper_b", c.shaperB);
        s += '}';
    }
    if (c.tuning.present)
    {
        section ("tuning");
        s += '{';
        putKey (s, "scl");
        putJsonString (s, c.tuning.scl);
        s += ',';
        putKey (s, "kbm");
        if (c.tuning.hasKbm)
            putJsonString (s, c.tuning.kbm);
        else
            s += "null";
        s += '}';
    }
    if (! c.impulses.empty())
    {
        section ("impulses");
        s += '[';
        for (size_t i = 0; i < c.impulses.size(); ++i)
        {
            s += i > 0 ? ",{" : "{";
            putFileRefFields (s, c.impulses[i]);
            s += '}';
        }
        s += ']';
    }
    if (! c.filterCurves.empty())
    {
        section ("filter_curves");
        s += '[';
        for (size_t i = 0; i < c.filterCurves.size(); ++i)
        {
            const auto& f = c.filterCurves[i];
            s += i > 0 ? ",{" : "{";
            putKey (s, "filter");
            putJsonString (s, f.filter);
            s += ',';
            putKey (s, "points");
            s += '[';
            for (size_t k = 0; k < f.points.size(); ++k)
            {
                if (k > 0)
                    s += ',';
                s += '{';
                putKey (s, "x");
                putJsonFloat (s, f.points[k].x);
                s += ',';
                putKey (s, "y");
                putJsonFloat (s, f.points[k].y);
                s += '}';
            }
            s += "]}";
        }
        s += ']';
    }
    if (! c.remaps.empty())
    {
        section ("remaps");
        s += '[';
        for (size_t i = 0; i < c.remaps.size(); ++i)
        {
            const auto& r = c.remaps[i];
            s += i > 0 ? ",{" : "{";
            putKey (s, "osc");
            putJsonInt (s, r.osc);
            s += ',';
            putKey (s, "slot");
            putJsonInt (s, r.slot);
            s += ',';
            putKey (s, "name");
            putJsonString (s, r.name);
            s += ',';
            putKey (s, "points");
            putPoints (s, r.points);
            s += '}';
        }
        s += ']';
    }
    if (! c.spectralFilters.empty())
    {
        section ("spectral_filters");
        s += '[';
        for (size_t i = 0; i < c.spectralFilters.size(); ++i)
        {
            const auto& r = c.spectralFilters[i];
            s += i > 0 ? ",{" : "{";
            putKey (s, "osc");
            putJsonInt (s, r.osc);
            s += ',';
            putKey (s, "name");
            putJsonString (s, r.name);
            s += ',';
            putKey (s, "points");
            putPoints (s, r.points);
            s += '}';
        }
        s += ']';
    }
    if (! c.instruments.empty())
    {
        section ("instruments");
        s += '[';
        for (size_t i = 0; i < c.instruments.size(); ++i)
        {
            const auto& in = c.instruments[i];
            s += i > 0 ? ",{" : "{";
            putKey (s, "osc");
            putJsonInt (s, in.osc);
            s += ',';
            putFileRefFields (s, in.ref);
            s += ',';
            putKey (s, "zones");
            putJsonInt (s, in.zones);
            s += '}';
        }
        s += ']';
    }
    if (! c.modCurves.empty())
    {
        section ("mod_curves");
        s += '[';
        for (size_t i = 0; i < c.modCurves.size(); ++i)
        {
            const auto& mc = c.modCurves[i];
            s += i > 0 ? ",{" : "{";
            putKey (s, "slot");
            putJsonInt (s, mc.slot);
            s += ',';
            putKey (s, "points");
            putPoints (s, mc.points);
            s += '}';
        }
        s += ']';
    }
    s += '}';
}
} // namespace

std::string saveState (const ParamValues& values, const StateContent& content)
{
    std::string s;
    s.reserve (32 + paramCount() * 28);
    s += "{\"format\":\"";
    s += kFormatName;
    s += "\",\"version\":";
    s += std::to_string (kStateVersion);
    s += ",\"params\":{";

    const auto* table = paramTable();
    for (size_t i = 0; i < paramCount(); ++i)
    {
        if (i > 0)
            s += ',';
        s += '"';
        s += table[i].key;
        s += "\":";

        char buf[32];
        number::formatG9 (constrainValue (table[i], values.plain[i]), buf, sizeof buf);
        s += buf;
    }
    s += '}';
    if (! content.empty() && validateContent (content))
        putContent (s, content);
    s += '}';
    return s;
}

std::string saveState (const ParamValues& values)
{
    return saveState (values, StateContent {});
}

namespace
{
int applyParsed (const Parsed& parsed, ParamValues& inOut) noexcept
{
    int applied = 0;
    const auto* table = paramTable();
    for (size_t i = 0; i < paramCount(); ++i)
    {
        if (! parsed.has[i])
            continue;
        const auto& info = table[i];
        const double lo = static_cast<double> (info.min);
        const double hi = static_cast<double> (info.max);
        const double v = parsed.value[i];
        inOut.plain[i] = constrainValue (info, static_cast<float> (v < lo ? lo : (v > hi ? hi : v)));
        if (! parsed.derived[i])
            ++applied;
    }
    return applied;
}

/** The loader behind loadState () (contentOut == nullptr: nothing is allocated, a content section is validated as JSON and skipped)
    and loadStateWithContent (). */
LoadResult loadImpl (const void* data, size_t size, ParamValues& inOut, StateContent* contentOut) noexcept
{
    LoadResult result;
    if (size == 0)
    {
        result.status = LoadStatus::empty;
        return result;
    }
    if (data == nullptr || size > kMaxStateBytes)
        return result;

    const char* begin = static_cast<const char*> (data);
    try
    {
        Parsed parsed;
        bool formatOk = false;
        bool versionSeen = false;
        Parser parser (begin, begin + size, contentOut != nullptr);
        if (! parser.parseDocument (parsed, formatOk, versionSeen) || ! formatOk || ! versionSeen)
            return result;

        result.version = parsed.version;
        migrate (parsed);
        if (contentOut != nullptr && parsed.version >= 3 && parsed.contentStatus == ContentStatus::ok)
            migrateSlewRecords (parsed);
        result.applied = applyParsed (parsed, inOut);
        result.status = parsed.version > kStateVersion ? LoadStatus::newer_version : LoadStatus::ok;

        // a content section belongs to version 3 and later; an older document has none (whatever a stray key says)
        result.content = parsed.version >= 3 ? parsed.contentStatus : ContentStatus::absent;
        if (contentOut != nullptr)
        {
            if (result.content == ContentStatus::ok)
                *contentOut = std::move (parsed.content);
            else
                contentOut->clear();
        }
        return result;
    }
    catch (...) // std::bad_alloc while the content is read: nothing was applied yet
    {
        return LoadResult {};
    }
}
} // namespace

LoadResult loadState (const void* data, size_t size, ParamValues& inOut) noexcept
{
    return loadImpl (data, size, inOut, nullptr);
}

LoadResult loadStateWithContent (const void* data, size_t size, ParamValues& inOut, StateContent& contentInOut) noexcept
{
    return loadImpl (data, size, inOut, &contentInOut);
}

int migrateLegacyParam (const char* key, double value, ParamValues& inOut) noexcept
{
    if (key == nullptr || std::isnan (value))
        return 0;
    const int li = legacyIndex (key);
    if (li < 0)
        return 0;

    Parsed parsed;
    parsed.legacyValue[li] = value;
    parsed.legacyHas[li] = true;
    migrateV1ToV2 (parsed);
    return applyParsed (parsed, inOut);
}
} // namespace synthyboy
