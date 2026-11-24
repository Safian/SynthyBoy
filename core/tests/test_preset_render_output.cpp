// SynthyBoy - tests of the files synthyboy_preset_render writes for the build to build comparison (CPU review K6): a render that differs in one float below the 24 bit step is reported
// as different by the 32 bit float WAV and by the hash, and is invisible in the 24 bit WAV (which is why the comparisons must not use it).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "render_output.h"
#include "test_runner.h"

using namespace synthyboy::presetrender;
namespace fs = std::filesystem;

namespace
{
std::vector<uint8_t> slurp (const fs::path& p)
{
    std::ifstream in (p, std::ios::binary);
    return std::vector<uint8_t> (std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>());
}
std::string slurpText (const fs::path& p)
{
    const auto b = slurp (p);
    return std::string (b.begin(), b.end());
}
float nextUp (float v, int steps)
{
    uint32_t u;
    std::memcpy (&u, &v, 4);
    u += static_cast<uint32_t> (steps);
    std::memcpy (&v, &u, 4);
    return v;
}
} // namespace

TEST (render_difference_below_the_24_bit_step_is_reported)
{
    const fs::path dir = fs::temp_directory_path() / "sb_render_output_test";
    fs::remove_all (dir);
    const int n = 4801;
    std::vector<float> l (n), r (n);
    for (int i = 0; i < n; ++i)
    {
        l[static_cast<size_t> (i)] = 0.5f * std::sin (0.01f * static_cast<float> (i));
        r[static_cast<size_t> (i)] = 0.3f * std::cos (0.013f * static_cast<float> (i));
    }
    std::vector<float> l2 = l;
    l2[1234] = nextUp (l2[1234], 1); // one float, one ulp (6e-8 relative: far below half a 24 bit step, 6e-8 absolute, for a value of about 0.4: one ulp is 3e-8)
    CHECK (std::fabs (l2[1234] - l[1234]) < 3.0e-8f);
    // a difference of a few ulp that stays inside the same 24 bit rounding cell is also invisible in 24 bit
    for (bool f32 : { false, true })
    {
        OutputOptions o;
        o.float32 = f32;
        o.hash = true;
        CHECK (writeRenderFiles (dir / "a", "p", l, r, 48000, o));
        CHECK (writeRenderFiles (dir / "b", "p", l2, r, 48000, o));
        CHECK (writeRenderFiles (dir / "c", "p", l, r, 48000, o)); // the same render again
        const bool wavSame = slurp (dir / "a" / "p.wav") == slurp (dir / "b" / "p.wav");
        const bool hashSame = slurpText (dir / "a" / "p.hash") == slurpText (dir / "b" / "p.hash");
        CHECK (slurp (dir / "a" / "p.wav") == slurp (dir / "c" / "p.wav"));
        CHECK (slurpText (dir / "a" / "p.hash") == slurpText (dir / "c" / "p.hash"));
        CHECK (! hashSame); // the hash sees it in both modes
        if (f32)
            CHECK (! wavSame); // the float WAV sees it
        else
            CHECK (wavSame); // the 24 bit WAV does not: the reason for the option
        fs::remove_all (dir / "a"); fs::remove_all (dir / "b"); fs::remove_all (dir / "c");
    }
    fs::remove_all (dir);
}

TEST (render_float_wav_holds_every_bit_and_hash_covers_non_finite_renders)
{
    const fs::path dir = fs::temp_directory_path() / "sb_render_output_test2";
    fs::remove_all (dir);
    std::vector<float> l { 0.0f, -0.0f, 1.0e-39f, 0.25f }, r { 1.0f, 3.0e38f, -1.0e-30f, 0.5f };
    OutputOptions o;
    o.float32 = true;
    o.hash = true;
    CHECK (writeRenderFiles (dir, "x", l, r, 44100, o));
    const auto b = slurp (dir / "x.wav");
    CHECK (b.size() == 44 + 4 * 2 * l.size());
    float back[8];
    std::memcpy (back, b.data() + 44, sizeof back);
    for (size_t i = 0; i < l.size(); ++i)
        CHECK (std::memcmp (&back[2 * i], &l[i], 4) == 0 && std::memcmp (&back[2 * i + 1], &r[i], 4) == 0);
    // -0 against +0 and a subnormal against zero are different renders
    std::vector<float> l2 = l;
    l2[1] = 0.0f;
    CHECK (hashFloatBits (l.data(), l.size()) != hashFloatBits (l2.data(), l2.size()));
    // a render that is not finite writes no WAV but still its hash
    std::vector<float> bad = l;
    bad[2] = std::nanf ("");
    CHECK (writeRenderFiles (dir / "nan", "y", bad, r, 44100, o, false));
    CHECK (! fs::exists (dir / "nan" / "y.wav"));
    CHECK (fs::exists (dir / "nan" / "y.hash"));
    // without the options only the 24 bit WAV is written, as before
    OutputOptions plain;
    CHECK (writeRenderFiles (dir / "plain", "z", l, r, 44100, plain));
    CHECK (fs::exists (dir / "plain" / "z.wav") && ! fs::exists (dir / "plain" / "z.hash"));
    CHECK (slurp (dir / "plain" / "z.wav").size() == 44 + 3 * 2 * l.size());
    fs::remove_all (dir);
}
