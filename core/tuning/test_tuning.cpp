// SynthyBoy - core/tuning: unit tests for microtonal tuning, parsers, and MTS-ESP seam.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include <string>
#include <vector>

#include "synthyboy/tuning.h"
#include "test_runner.h"

using namespace synthyboy::tuning;

namespace
{
// Helper to compute standard 12-TET frequency analytically
double standard12TET (int midiKey, double a4Hz = 440.0) noexcept
{
    return a4Hz * std::pow (2.0, (static_cast<double> (midiKey) - 69.0) / 12.0);
}

// 19-EDO Scala string
const char* k19EdoScl = R"(! 19-edo.scl
19 equal divisions of octave
19
!
63.15789
126.31579
189.47368
252.63158
315.78947
378.94737
442.10526
505.26316
568.42105
631.57895
694.73684
757.89474
821.05263
884.21053
947.36842
1010.52632
1073.68421
1136.84211
2/1
)";

// 12-tone 5-limit Just Intonation Scala string
const char* kJustIntonationScl = R"(! ji_12.scl
12-tone 5-limit Just Intonation
12
!
16/15
9/8
6/5
5/4
4/3
45/32
3/2
8/5
5/3
9/5
15/8
2/1
)";

// Sample KBM string with A4 = 432 Hz reference and middle C = 60
const char* kSampleKbm = R"(! sample_432.kbm
0
0
127
60
69
432.000000
9
)";

// Sample KBM with white-keys only mapping (black keys unmapped = 'x')
const char* kWhiteKeysKbm = R"(! white_keys.kbm
12
0
127
60
60
261.625565
0
! Mapping entries (C=0, D=1, E=2, F=3, G=4, A=5, B=6):
0
x
1
x
2
3
x
4
x
5
x
6
)";

// Sample AnaMark TUN string
const char* kSampleTun = R"(; sample.tun
[Information]
Name = Test AnaMark Tuning
[Tuning]
BaseFreq = 8.1757989156437
note 0 = 0.0
note 60 = 6000.0
note 69 = 6900.0
note 127 = 12700.0
[Exact Tuning]
note 60 = 6000.000000
note 69 = 6900.000000
)";

// Mock dynamic tuning source (MTS-ESP seam)
class MockDynamicTuningSource : public ITuningSource
{
public:
    explicit MockDynamicTuningSource (bool connected) : connected_ (connected)
    {
        for (int k = 0; k < kNumKeys; ++k)
            freqs_[k] = standard12TET (k, 432.0);
    }

    bool isConnected() const noexcept override { return connected_; }

    double frequencyForKey (int midiKey) const noexcept override
    {
        if (! connected_ || midiKey < 0 || midiKey >= kNumKeys)
            return 0.0;
        return freqs_[midiKey];
    }

    bool query128Keys (std::array<double, kNumKeys>& outFrequencies) const noexcept override
    {
        if (! connected_)
            return false;
        outFrequencies = freqs_;
        return true;
    }

    const char* name() const noexcept override { return "MockMTS"; }

private:
    bool connected_ = false;
    std::array<double, kNumKeys> freqs_ {};
};

} // namespace

TEST (Tuning_12TET_Precision)
{
    Tuning t;
    CHECK (t.isDefault());
    CHECK_EQ (t.scaleSize(), 12);

    // Prompt requirement: 12-TET equals 440*2^((k-69)/12) within 1e-9 for all 128 keys
    for (int k = 0; k < kNumKeys; ++k)
    {
        const double expected = standard12TET (k, 440.0);
        const double freq = t.frequencyForKey (k, 440.0);
        const double tableFreq = t.table()[k];

        CHECK_NEAR (freq, expected, 1e-9);
        CHECK_NEAR (tableFreq, expected, 1e-9);
    }

    // Benchmark notes: A4 (69) = 440, A3 (57) = 220, A5 (81) = 880
    CHECK_NEAR (t.frequencyForKey (69, 440.0), 440.0, 1e-9);
    CHECK_NEAR (t.frequencyForKey (57, 440.0), 220.0, 1e-9);
    CHECK_NEAR (t.frequencyForKey (81, 440.0), 880.0, 1e-9);

    // Scaled concert pitch (e.g. 442.0 Hz, 432.0 Hz)
    CHECK_NEAR (t.frequencyForKey (69, 442.0), 442.0, 1e-9);
    CHECK_NEAR (t.frequencyForKey (69, 432.0), 432.0, 1e-9);
    CHECK_NEAR (t.frequencyForKey (57, 442.0), 221.0, 1e-9);

    // computeTable test
    const auto table442 = t.computeTable (442.0);
    for (int k = 0; k < kNumKeys; ++k)
        CHECK_NEAR (table442[k], standard12TET (k, 442.0), 1e-9);

    // Out-of-bounds keys
    CHECK_EQ (t.frequencyForKey (-1, 440.0), 0.0);
    CHECK_EQ (t.frequencyForKey (128, 440.0), 0.0);
    CHECK_EQ (t.frequencyForKey (999, 440.0), 0.0);
}

TEST (Tuning_KnownScl_19EDO)
{
    Tuning t;
    const ParseResult res = t.loadScl (k19EdoScl);
    CHECK (res.ok());
    CHECK (! t.isDefault());
    CHECK_EQ (t.scaleSize(), 19);

    const double freqC4 = t.frequencyForKey (60, 440.0);
    CHECK (freqC4 > 0.0);

    // In 19-EDO, 19 scale steps = exactly 1 octave (2.0x frequency)
    const double freqC5 = t.frequencyForKey (60 + 19, 440.0);
    CHECK_NEAR (freqC5, freqC4 * 2.0, 1e-5);

    const double freqC3 = t.frequencyForKey (60 - 19, 440.0);
    CHECK_NEAR (freqC3, freqC4 * 0.5, 1e-5);

    const double freqC6 = t.frequencyForKey (60 + 38, 440.0);
    CHECK_NEAR (freqC6, freqC4 * 4.0, 1e-5);

    // Each step i has ratio ~ 2^(i / 19)
    for (int i = 0; i <= 19; ++i)
    {
        const double f = t.frequencyForKey (60 + i, 440.0);
        const double expectedRatio = std::pow (2.0, static_cast<double> (i) / 19.0);
        CHECK_NEAR (f / freqC4, expectedRatio, 1e-4);
    }
}

TEST (Tuning_KnownScl_JustIntonation)
{
    Tuning t;
    const ParseResult res = t.loadScl (kJustIntonationScl);
    CHECK (res.ok());
    CHECK (! t.isDefault());
    CHECK_EQ (t.scaleSize(), 12);

    const double fTonic = t.frequencyForKey (60, 440.0); // degree 0
    CHECK (fTonic > 0.0);

    // 16/15 (minor second)
    CHECK_NEAR (t.frequencyForKey (61, 440.0) / fTonic, 16.0 / 15.0, 1e-9);

    // 9/8 (major second)
    CHECK_NEAR (t.frequencyForKey (62, 440.0) / fTonic, 9.0 / 8.0, 1e-9);

    // 6/5 (minor third)
    CHECK_NEAR (t.frequencyForKey (63, 440.0) / fTonic, 6.0 / 5.0, 1e-9);

    // 5/4 (major third)
    CHECK_NEAR (t.frequencyForKey (64, 440.0) / fTonic, 5.0 / 4.0, 1e-9);

    // 4/3 (perfect fourth)
    CHECK_NEAR (t.frequencyForKey (65, 440.0) / fTonic, 4.0 / 3.0, 1e-9);

    // 45/32 (augmented fourth)
    CHECK_NEAR (t.frequencyForKey (66, 440.0) / fTonic, 45.0 / 32.0, 1e-9);

    // 3/2 (perfect fifth)
    CHECK_NEAR (t.frequencyForKey (67, 440.0) / fTonic, 3.0 / 2.0, 1e-9);

    // 8/5 (minor sixth)
    CHECK_NEAR (t.frequencyForKey (68, 440.0) / fTonic, 8.0 / 5.0, 1e-9);

    // 5/3 (major sixth)
    CHECK_NEAR (t.frequencyForKey (69, 440.0) / fTonic, 5.0 / 3.0, 1e-9);

    // 9/5 (minor seventh)
    CHECK_NEAR (t.frequencyForKey (70, 440.0) / fTonic, 9.0 / 5.0, 1e-9);

    // 15/8 (major seventh)
    CHECK_NEAR (t.frequencyForKey (71, 440.0) / fTonic, 15.0 / 8.0, 1e-9);

    // 2/1 (octave)
    CHECK_NEAR (t.frequencyForKey (72, 440.0) / fTonic, 2.0, 1e-9);
}

TEST (Tuning_Kbm_RemappingAndReferenceNote)
{
    // 1. Reference note retuning: A4 (69) at 432 Hz
    {
        Tuning t;
        const ParseResult res = t.loadKbm (kSampleKbm);
        CHECK (res.ok());
        CHECK_NEAR (t.frequencyForKey (69, 440.0), 432.0, 1e-9);
        CHECK_NEAR (t.frequencyForKey (57, 440.0), 216.0, 1e-9);
        CHECK_NEAR (t.frequencyForKey (81, 440.0), 864.0, 1e-9);
    }

    // 2. White-keys only remapping
    {
        Tuning t;
        const ParseResult res = t.loadKbm (kWhiteKeysKbm);
        CHECK (res.ok());

        // Black keys are unmapped -> frequency 0.0
        CHECK_EQ (t.frequencyForKey (61, 440.0), 0.0); // C#
        CHECK_EQ (t.frequencyForKey (63, 440.0), 0.0); // D#
        CHECK_EQ (t.frequencyForKey (66, 440.0), 0.0); // F#
        CHECK_EQ (t.frequencyForKey (68, 440.0), 0.0); // G#
        CHECK_EQ (t.frequencyForKey (70, 440.0), 0.0); // A#

        // White keys have non-zero frequencies
        const double fC4 = t.frequencyForKey (60, 440.0);
        const double fD4 = t.frequencyForKey (62, 440.0);
        const double fE4 = t.frequencyForKey (64, 440.0);
        const double fF4 = t.frequencyForKey (65, 440.0);
        const double fG4 = t.frequencyForKey (67, 440.0);
        const double fA4 = t.frequencyForKey (69, 440.0);
        const double fB4 = t.frequencyForKey (71, 440.0);
        const double fC5 = t.frequencyForKey (72, 440.0);

        CHECK (fC4 > 0.0);
        CHECK (fD4 > fC4);
        CHECK (fE4 > fD4);
        CHECK (fF4 > fE4);
        CHECK (fG4 > fF4);
        CHECK (fA4 > fG4);
        CHECK (fB4 > fA4);
        CHECK_NEAR (fC5, fC4 * 2.0, 1e-9);
    }
}

TEST (Tuning_TunFile_Parsing)
{
    Tuning t;
    const ParseResult res = t.loadTun (kSampleTun);
    CHECK (res.ok());

    // Check key 69 is 440 Hz
    CHECK_NEAR (t.frequencyForKey (69, 440.0), 440.0, 1e-9);

    // Check key 60 is standard C4 ~ 261.6255653 Hz
    const double expectedC4 = standard12TET (60, 440.0);
    CHECK_NEAR (t.frequencyForKey (60, 440.0), expectedC4, 1e-9);

    // Scaled A4
    CHECK_NEAR (t.frequencyForKey (69, 442.0), 442.0, 1e-9);
}

TEST (Tuning_MalformedInputs_Robustness)
{
    // Test 30+ malformed and adversarial inputs: none must crash or throw!
    Tuning t;
    const double defaultA4Freq = t.frequencyForKey (69, 440.0);

    // 1. Null pointer
    CHECK (! t.loadScl (nullptr, 0).ok());
    CHECK (! t.loadKbm (nullptr, 0).ok());
    CHECK (! t.loadTun (nullptr, 0).ok());

    // 2. Empty string
    CHECK (! t.loadScl ("").ok());
    CHECK (! t.loadKbm ("").ok());
    CHECK (! t.loadTun ("").ok());

    // 3. Size limit exceeded (> 1MB)
    const std::string hugeData (kMaxTuningFileSize + 10, '!');
    CHECK_EQ (t.loadScl (hugeData).error, ParseError::tooLarge);
    CHECK_EQ (t.loadKbm (hugeData).error, ParseError::tooLarge);
    CHECK_EQ (t.loadTun (hugeData).error, ParseError::tooLarge);

    // 4. SCL: comments only
    CHECK (! t.loadScl ("! line1\n! line2\n! line3\n").ok());

    // 5. SCL: description only, no scale size
    CHECK (! t.loadScl ("! comment\nDescription line\n").ok());

    // 6. SCL: scale size non-numeric
    CHECK (! t.loadScl ("Description\nabc\n").ok());

    // 7. SCL: scale size negative
    CHECK (! t.loadScl ("Description\n-5\n").ok());

    // 8. SCL: scale size zero
    CHECK (! t.loadScl ("Description\n0\n").ok());

    // 9. SCL: scale size exceeds limit
    CHECK (! t.loadScl ("Description\n9999999\n").ok());

    // 10. SCL: denominator 0 in ratio
    CHECK (! t.loadScl ("Description\n1\n3/0\n").ok());

    // 11. SCL: negative numerator in ratio
    CHECK (! t.loadScl ("Description\n1\n-3/2\n").ok());

    // 12. SCL: negative denominator in ratio
    CHECK (! t.loadScl ("Description\n1\n3/-2\n").ok());

    // 13. SCL: multiple slashes in ratio
    CHECK (! t.loadScl ("Description\n1\n3/2/1\n").ok());

    // 14. SCL: non-numeric garbage in pitch
    CHECK (! t.loadScl ("Description\n1\nfoo/bar\n").ok());

    // 15. SCL: invalid cents decimal format
    CHECK (! t.loadScl ("Description\n1\n12.34.56\n").ok());

    // 16. SCL: cents NaN
    CHECK (! t.loadScl ("Description\n1\nnan\n").ok());

    // 17. SCL: cents Inf
    CHECK (! t.loadScl ("Description\n1\ninf\n").ok());

    // 18. SCL: missing pitch lines (declared 5, provided 2)
    CHECK (! t.loadScl ("Description\n5\n100.0\n200.0\n").ok());

    // 19. KBM: incomplete header (less than 7 lines)
    CHECK (! t.loadKbm ("0\n0\n127\n60\n").ok());

    // 20. KBM: map size negative
    CHECK (! t.loadKbm ("-1\n0\n127\n60\n69\n440.0\n9\n").ok());

    // 21. KBM: map size exceeds limit
    CHECK (! t.loadKbm ("999999\n0\n127\n60\n69\n440.0\n9\n").ok());

    // 22. KBM: firstMidi > lastMidi
    CHECK (! t.loadKbm ("0\n100\n50\n60\n69\n440.0\n9\n").ok());

    // 23. KBM: firstMidi out of range
    CHECK (! t.loadKbm ("0\n-5\n127\n60\n69\n440.0\n9\n").ok());

    // 24. KBM: lastMidi out of range
    CHECK (! t.loadKbm ("0\n0\n150\n60\n69\n440.0\n9\n").ok());

    // 25. KBM: middleNote out of range
    CHECK (! t.loadKbm ("0\n0\n127\n200\n69\n440.0\n9\n").ok());

    // 26. KBM: referenceNote out of range
    CHECK (! t.loadKbm ("0\n0\n127\n60\n250\n440.0\n9\n").ok());

    // 27. KBM: referenceFrequency negative
    CHECK (! t.loadKbm ("0\n0\n127\n60\n69\n-440.0\n9\n").ok());

    // 28. KBM: referenceFrequency zero
    CHECK (! t.loadKbm ("0\n0\n127\n60\n69\n0.0\n9\n").ok());

    // 29. KBM: missing mapping lines (declared 4, provided 1)
    CHECK (! t.loadKbm ("4\n0\n127\n60\n69\n440.0\n9\n0\n").ok());

    // 30. KBM: garbage in mapping lines
    CHECK (! t.loadKbm ("1\n0\n127\n60\n69\n440.0\n9\nnot_a_number\n").ok());

    // 31. TUN: no sections
    CHECK (! t.loadTun ("random garbage string without brackets").ok());

    // 32. TUN: missing '=' in note line
    CHECK (! t.loadTun ("[Tuning]\nBaseFreq = 8.175\nnote 60 6000\n").ok());

    // 33. TUN: invalid note index
    CHECK (! t.loadTun ("[Tuning]\nBaseFreq = 8.175\nnote -5 = 100\n").ok());

    // 34. TUN: note index out of range
    CHECK (! t.loadTun ("[Tuning]\nBaseFreq = 8.175\nnote 200 = 100\n").ok());

    // 35. TUN: invalid BaseFreq
    CHECK (! t.loadTun ("[Tuning]\nBaseFreq = -5.0\nnote 60 = 6000\n").ok());

    // 36. TUN: non-numeric cents
    CHECK (! t.loadTun ("[Tuning]\nBaseFreq = 8.175\nnote 60 = invalid\n").ok());

    // After all malformed attempts, tuning must still be in a valid working state!
    CHECK_NEAR (t.frequencyForKey (69, 440.0), defaultA4Freq, 1e-9);
}

TEST (Tuning_RoundTrips)
{
    // 1. SCL round trip with 19-EDO
    {
        Tuning t1;
        CHECK (t1.loadScl (k19EdoScl).ok());
        const std::string serializedScl = t1.toScl();

        Tuning t2;
        CHECK (t2.loadScl (serializedScl).ok());
        CHECK_EQ (t1.scaleSize(), t2.scaleSize());

        for (int k = 0; k < kNumKeys; ++k)
            CHECK_NEAR (t1.frequencyForKey (k, 440.0), t2.frequencyForKey (k, 440.0), 1e-9);
    }

    // 2. SCL round trip with Just Intonation
    {
        Tuning t1;
        CHECK (t1.loadScl (kJustIntonationScl).ok());
        const std::string serializedScl = t1.toScl();

        Tuning t2;
        CHECK (t2.loadScl (serializedScl).ok());
        CHECK_EQ (t1.scaleSize(), t2.scaleSize());

        for (int k = 0; k < kNumKeys; ++k)
            CHECK_NEAR (t1.frequencyForKey (k, 440.0), t2.frequencyForKey (k, 440.0), 1e-9);
    }

    // 3. KBM round trip
    {
        Tuning t1;
        CHECK (t1.loadKbm (kWhiteKeysKbm).ok());
        const std::string serializedKbm = t1.toKbm();

        Tuning t2;
        CHECK (t2.loadKbm (serializedKbm).ok());

        for (int k = 0; k < kNumKeys; ++k)
            CHECK_NEAR (t1.frequencyForKey (k, 440.0), t2.frequencyForKey (k, 440.0), 1e-9);
    }

    // 4. TUN round trip
    {
        Tuning t1;
        CHECK (t1.loadTun (kSampleTun).ok());
        const std::string serializedTun = t1.toTun();

        Tuning t2;
        CHECK (t2.loadTun (serializedTun).ok());

        for (int k = 0; k < kNumKeys; ++k)
            CHECK_NEAR (t1.frequencyForKey (k, 440.0), t2.frequencyForKey (k, 440.0), 1e-9);
    }

    // 5. Default 12-TET round trip through TUN
    {
        Tuning t1; // default 12-TET
        const std::string serializedTun = t1.toTun();

        Tuning t2;
        CHECK (t2.loadTun (serializedTun).ok());

        for (int k = 0; k < kNumKeys; ++k)
            CHECK_NEAR (t2.frequencyForKey (k, 440.0), standard12TET (k, 440.0), 1e-9);
    }
}

TEST (Tuning_DynamicSource_Seam)
{
    // Test seam with disconnected source
    MockDynamicTuningSource disconnected (false);
    CHECK (! disconnected.isConnected());
    CHECK_EQ (disconnected.frequencyForKey (69), 0.0);
    std::array<double, kNumKeys> arr {};
    CHECK (! disconnected.query128Keys (arr));

    // Test seam with connected source
    MockDynamicTuningSource connected (true);
    CHECK (connected.isConnected());
    CHECK_EQ (std::string (connected.name()), "MockMTS");
    CHECK_NEAR (connected.frequencyForKey (69), 432.0, 1e-9);

    std::array<double, kNumKeys> queriedTable {};
    CHECK (connected.query128Keys (queriedTable));
    CHECK_NEAR (queriedTable[69], 432.0, 1e-9);
    CHECK_NEAR (queriedTable[57], 216.0, 1e-9);
}
