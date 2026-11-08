// SynthyBoy - wavetable-edit: Comprehensive unit tests.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/wavetable_edit.h"
#include "test_runner.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace synthyboy::wtedit;

//==============================================================================
TEST(fft_round_trip)
{
    std::array<float, kWaveformSize> input {};
    std::array<float, kWaveformSize> real {};
    std::array<float, kWaveformSize> imag {};

    // Synthesize a complex test signal with multiple sines and saw components
    for (size_t i = 0; i < kWaveformSize; ++i)
    {
        const double t = static_cast<double>(i) / static_cast<double>(kWaveformSize);
        input[i] = static_cast<float>(0.5 * std::sin(2.0 * 3.1415926535 * t * 3.0) +
                                      0.3 * std::cos(2.0 * 3.1415926535 * t * 7.0) +
                                      0.2 * (1.0 - 2.0 * t));
        real[i] = input[i];
        imag[i] = 0.0f;
    }

    // Forward FFT followed by Inverse FFT
    Radix2Fft::forward(real.data(), imag.data(), kWaveformSize);
    Radix2Fft::inverse(real.data(), imag.data(), kWaveformSize);

    float maxErr = 0.0f;
    for (size_t i = 0; i < kWaveformSize; ++i)
    {
        const float err = std::fabs(real[i] - input[i]);
        if (err > maxErr)
            maxErr = err;
        CHECK(std::fabs(imag[i]) < 1e-5f);
    }
    CHECK(maxErr < 1e-5f);

    // Test realToPolar -> polarToReal round trip
    std::array<float, kNumFftBins + 1> mags {};
    std::array<float, kNumFftBins + 1> phases {};
    std::array<float, kWaveformSize> reconstructed {};

    Radix2Fft::realToPolar(input.data(), mags.data(), phases.data(), kWaveformSize);
    Radix2Fft::polarToReal(mags.data(), phases.data(), reconstructed.data(), kWaveformSize);

    float maxPolarErr = 0.0f;
    for (size_t i = 0; i < kWaveformSize; ++i)
    {
        const float err = std::fabs(reconstructed[i] - input[i]);
        if (err > maxPolarErr)
            maxPolarErr = err;
    }
    CHECK(maxPolarErr < 1e-5f);
}

//==============================================================================
TEST(wavetable_model_invariants)
{
    Wavetable wt("TestWT");
    CHECK_EQ(wt.name(), "TestWT");
    CHECK_EQ(wt.numFrames(), 1u);
    CHECK_EQ(wt.getFrame(0).data.size(), kWaveformSize);

    // Metadata
    wt.setAuthor("Test Author");
    wt.setComment("Unit test wavetable");
    wt.setSampleRate(48000.0);
    CHECK_EQ(wt.author(), "Test Author");
    CHECK_EQ(wt.comment(), "Unit test wavetable");
    CHECK_NEAR(wt.sampleRate(), 48000.0, 1e-3);

    // Duplicate frame
    CHECK(wt.duplicateFrame(0));
    CHECK_EQ(wt.numFrames(), 2u);

    // Insert frame
    Frame customFrame;
    customFrame[0] = 0.75f;
    CHECK(wt.insertFrame(1, customFrame));
    CHECK_EQ(wt.numFrames(), 3u);
    CHECK_NEAR(wt.getFrame(1)[0], 0.75f, 1e-6);

    // Move frame
    CHECK(wt.moveFrame(1, 0));
    CHECK_NEAR(wt.getFrame(0)[0], 0.75f, 1e-6);

    // Remove frame
    CHECK(wt.removeFrame(0));
    CHECK_EQ(wt.numFrames(), 2u);

    // Cannot remove below 1 frame
    CHECK(wt.removeFrame(0));
    CHECK_EQ(wt.numFrames(), 1u);
    CHECK(!wt.removeFrame(0)); // Should fail, preserving 1 frame
    CHECK_EQ(wt.numFrames(), 1u);

    // Fill up to max capacity
    while (wt.numFrames() < kMaxFrames)
        wt.duplicateFrame(0);
    CHECK_EQ(wt.numFrames(), kMaxFrames);
    CHECK(!wt.duplicateFrame(0)); // Cannot exceed 256 frames
    CHECK(!wt.insertFrame(0, customFrame));
}

//==============================================================================
TEST(frame_ops_invariants)
{
    Frame f;
    for (size_t i = 0; i < kWaveformSize; ++i)
        f[i] = static_cast<float>(i) * 0.001f + 0.5f;

    // Normalisation invariant: peak <= 1.0
    ops::normalise(f);
    float maxVal = 0.0f;
    for (size_t i = 0; i < kWaveformSize; ++i)
    {
        const float a = std::fabs(f[i]);
        if (a > maxVal) maxVal = a;
    }
    CHECK_NEAR(maxVal, 1.0f, 1e-5);

    // Idempotence of normalise
    Frame fCopy = f;
    ops::normalise(f);
    for (size_t i = 0; i < kWaveformSize; ++i)
        CHECK_NEAR(f[i], fCopy[i], 1e-6);

    // DC remove invariant: mean == 0.0
    ops::removeDc(f);
    double sum = 0.0;
    for (size_t i = 0; i < kWaveformSize; ++i)
        sum += static_cast<double>(f[i]);
    CHECK_NEAR(sum / static_cast<double>(kWaveformSize), 0.0, 1e-5);

    // Idempotence of DC remove
    fCopy = f;
    ops::removeDc(f);
    for (size_t i = 0; i < kWaveformSize; ++i)
        CHECK_NEAR(f[i], fCopy[i], 1e-6);

    // Polarity invert: y = -y and double-invert restores
    fCopy = f;
    ops::flipVertical(f);
    for (size_t i = 0; i < kWaveformSize; ++i)
        CHECK_NEAR(f[i], -fCopy[i], 1e-6);
    ops::flipVertical(f);
    for (size_t i = 0; i < kWaveformSize; ++i)
        CHECK_NEAR(f[i], fCopy[i], 1e-6);

    // Reverse: y[i] = y[N - 1 - i] and double-reverse restores
    fCopy = f;
    ops::flipHorizontal(f);
    for (size_t i = 0; i < kWaveformSize; ++i)
        CHECK_NEAR(f[i], fCopy[kWaveformSize - 1 - i], 1e-6);
    ops::flipHorizontal(f);
    for (size_t i = 0; i < kWaveformSize; ++i)
        CHECK_NEAR(f[i], fCopy[i], 1e-6);

    // Fade edges: start and end samples attenuated
    ops::fadeEdges(f, 16);
    CHECK_NEAR(f[0], 0.0f, 1e-4);
    CHECK_NEAR(f[kWaveformSize - 1], 0.0f, 1e-4);

    // Crossfade edges (gridSize 16 -> win = 2048 / 16 = 128 samples)
    ops::crossfadeEdges(f, 16);
    CHECK_NEAR(f[0], f[kWaveformSize - 128], 1e-4);

    // Crossfade edges 16 samples (win = 16)
    ops::crossfadeEdges16Samples(f);
    CHECK_NEAR(f[0], f[kWaveformSize - 16], 1e-4);

    // Shift to zero crossing
    ops::shiftToZeroCrossing(f);
    CHECK(f[0] >= -0.01f);
}

//==============================================================================
TEST(frame_ops_all_and_ranges)
{
    Wavetable wt;
    wt.clear();
    for (size_t i = 0; i < 8; ++i)
    {
        Frame f;
        for (size_t j = 0; j < kWaveformSize; ++j)
            f[j] = static_cast<float>(i + 1) * 0.1f;
        wt.insertFrame(i, f);
    }
    // wt has 1 (clear) + 8 = 9 frames
    CHECK_EQ(wt.numFrames(), 9u);

    // normaliseEach
    ops::normaliseEach(wt);
    for (size_t f = 0; f < wt.numFrames(); ++f)
    {
        float mx = 0.0f;
        for (size_t j = 0; j < kWaveformSize; ++j)
            if (std::fabs(wt.getFrame(f)[j]) > mx) mx = std::fabs(wt.getFrame(f)[j]);
        if (mx > 0.0f)
            CHECK_NEAR(mx, 1.0f, 1e-5);
    }

    // normaliseSame
    wt.getFrame(0)[0] = 0.5f;
    wt.getFrame(1)[0] = 1.0f;
    ops::normaliseSame(wt);
    CHECK_NEAR(wt.getFrame(1)[0], 1.0f, 1e-5);
    CHECK_NEAR(wt.getFrame(0)[0], 0.5f, 1e-5);

    // removeDcAll, flipVerticalAll, flipHorizontalAll
    ops::removeDcAll(wt);
    ops::flipVerticalAll(wt);
    ops::flipHorizontalAll(wt);
    ops::fadeEdgesAll(wt, 16);
    ops::crossfadeEdgesAll(wt, 16);
    ops::crossfadeEdges16SamplesAll(wt);
    ops::shiftToZeroCrossingAll(wt);

    // removeRange
    CHECK(ops::removeRange(wt, 1, 3));
    CHECK_EQ(wt.numFrames(), 6u);

    // removeBeginningToSelected
    CHECK(ops::removeBeginningToSelected(wt, 1));
    CHECK_EQ(wt.numFrames(), 4u);

    // removeSelectedToEnd
    CHECK(ops::removeSelectedToEnd(wt, 2));
    CHECK_EQ(wt.numFrames(), 2u);

    // removeAllExceptSelected
    wt.duplicateFrame(0);
    wt.duplicateFrame(0);
    CHECK_EQ(wt.numFrames(), 4u);
    CHECK(ops::removeAllExceptSelected(wt, 1, 2));
    CHECK_EQ(wt.numFrames(), 2u);
}

//==============================================================================
TEST(table_resize_ops)
{
    Wavetable wt;
    wt.clear();
    for (size_t i = 0; i < 4; ++i)
    {
        Frame f;
        f[0] = static_cast<float>(i);
        wt.insertFrame(i, f);
    }
    CHECK_EQ(wt.numFrames(), 5u); // 1 initial + 4 inserted

    // Reduce to every 2nd frame
    ops::reduceTo(wt, 2);
    CHECK_EQ(wt.numFrames(), 3u);

    // Resize half: doubles total frames
    CHECK(ops::resizeTablesHalf(wt));
    CHECK_EQ(wt.numFrames(), 6u);

    // Resize double: halves total frames
    CHECK(ops::resizeTablesDouble(wt));
    CHECK_EQ(wt.numFrames(), 3u);

    // Create PWM from frame
    ops::createPwmFromFrame(wt, 0);
    CHECK_EQ(wt.numFrames(), kMaxFrames);
}

//==============================================================================
TEST(drawing_tools)
{
    Frame f;
    // Flat line
    applyDrawingTool(f, DrawingTool::FlatLine, 4, 0, 0.5f);
    for (size_t i = 0; i < 512; ++i)
        CHECK_NEAR(f[i], 0.5f, 1e-6);

    // Sine
    applyDrawingTool(f, DrawingTool::Sine, 4, 1, 0.0f);
    CHECK_NEAR(f[512], 0.0f, 1e-3);

    // Slope Up
    applyDrawingTool(f, DrawingTool::SlopeUp, 4, 2, 0.0f);
    CHECK_NEAR(f[1024], -1.0f, 1e-3);
    CHECK_NEAR(f[1535], 1.0f, 1e-3);

    // Slope Down
    applyDrawingTool(f, DrawingTool::SlopeDown, 4, 3, 0.0f);
    CHECK_NEAR(f[1536], 1.0f, 1e-3);
    CHECK_NEAR(f[2047], -1.0f, 1e-3);

    // Curve Up & Curve Down
    applyDrawingTool(f, DrawingTool::CurveUp, 8, 0, 0.0f);
    applyDrawingTool(f, DrawingTool::CurveDown, 8, 1, 0.0f);

    // HalfSinePeak & HalfSineValley
    applyDrawingTool(f, DrawingTool::HalfSinePeak, 8, 2, 0.0f);
    applyDrawingTool(f, DrawingTool::HalfSineValley, 8, 3, 0.0f);

    // InterpolateLinear & InterpolateCurved
    applyDrawingTool(f, DrawingTool::InterpolateLinear, 8, 4, 0.0f);
    applyDrawingTool(f, DrawingTool::InterpolateCurved, 8, 5, 0.0f);

    // Nudge & Noise
    applyDrawingTool(f, DrawingTool::Nudge, 8, 6, 0.1f);
    applyDrawingTool(f, DrawingTool::Noise, 8, 7, 0.05f);

    // Mirror
    applyDrawingTool(f, DrawingTool::Mirror, 4, 0, 0.0f);
    for (size_t i = 0; i < kWaveformSize / 2; ++i)
        CHECK_NEAR(f[kWaveformSize - 1 - i], f[i], 1e-6);
}

//==============================================================================
TEST(fft_spectral_ops)
{
    Frame f;
    ops::fftGenerateSaw(f);

    // Check harmonic amplitudes
    std::array<float, kNumFftBins + 1> mags {};
    std::array<float, kNumFftBins + 1> phases {};
    Radix2Fft::realToPolar(f.sampleData(), mags.data(), phases.data(), kWaveformSize);
    CHECK_NEAR(mags[1], 1.0f, 1e-3);
    CHECK_NEAR(mags[2], 0.5f, 1e-3);
    CHECK_NEAR(mags[4], 0.25f, 1e-3);

    // Clear HF
    ops::fftClearHf(f, 10);
    Radix2Fft::realToPolar(f.sampleData(), mags.data(), phases.data(), kWaveformSize);
    for (size_t k = 10; k <= kNumFftBins; ++k)
        CHECK_NEAR(mags[k], 0.0f, 1e-5);

    // Clear LF
    ops::fftGenerateSaw(f);
    ops::fftClearLf(f, 3);
    Radix2Fft::realToPolar(f.sampleData(), mags.data(), phases.data(), kWaveformSize);
    CHECK_NEAR(mags[1], 0.0f, 1e-5);
    CHECK_NEAR(mags[2], 0.0f, 1e-5);
    CHECK_NEAR(mags[3], 0.0f, 1e-5);
    CHECK(mags[4] > 0.1f);

    // Octave shift up
    ops::fftGenerateSaw(f);
    ops::fftShiftOctaveUp(f);
    Radix2Fft::realToPolar(f.sampleData(), mags.data(), phases.data(), kWaveformSize);
    CHECK_NEAR(mags[1], 0.0f, 1e-5); // Odd bin cleared
    CHECK_NEAR(mags[2], 1.0f, 1e-3); // Old bin 1 shifted to 2

    // Octave shift down
    ops::fftShiftOctaveDown(f);
    Radix2Fft::realToPolar(f.sampleData(), mags.data(), phases.data(), kWaveformSize);
    CHECK_NEAR(mags[1], 1.0f, 1e-3); // Restored to fundamental

    // Progressive fade & random ops
    ops::fftGenerateSaw(f);
    ops::fftProgressiveFade(f, 1.5f);
    ops::fftRandomizeLowBins(f, 8, true);
    ops::fftRandomizeAll(f);
    ops::fftCreateRandomSeriesGaps(f, 0.4f);
    ops::fftRepeatBinGroup(f, 4);
    ops::fftScaleFreqValuesByBinIndex(f);
    ops::fftSetHarmonic(f, 5, 0.9f, 0.0f);

    // Clear all
    ops::fftClearAll(f);
    for (size_t i = 0; i < kWaveformSize; ++i)
        CHECK_NEAR(f[i], 0.0f, 1e-7);
}

//==============================================================================
TEST(cross_frame_spectral_and_morph_ops)
{
    Wavetable wt1;
    wt1.clear();
    Frame f1, f2, f3;
    ops::fftGenerateSaw(f1);
    ops::fftGenerateSaw(f2);
    ops::fftShiftOctaveUp(f2);
    ops::fftGenerateSaw(f3);
    ops::fftClearHf(f3, 4);

    wt1.insertFrame(0, f1);
    wt1.insertFrame(1, f2);
    wt1.insertFrame(2, f3);

    // Spectra & phase copying across frames
    ops::setSpectraThisFrameToAll(wt1, 0);
    ops::setPhasesThisFrameToAll(wt1, 0);

    Wavetable wt2 = wt1;
    ops::fftGenerateSaw(wt2.getFrame(0));
    ops::setSpectraFromOther(wt1, wt2);
    ops::setPhasesFromOther(wt1, wt2);
    ops::subtractSpectraFromOther(wt1, wt2);

    // Blur operations
    ops::blurSpectraAdjacentBinsAll(wt1, 8);
    ops::blurPhasesAdjacentBinsAll(wt1, 8);
    ops::blurSpectraAdjacentFrames(wt1, 4);
    ops::blurPhasesAdjacentFrames(wt1, 4);

    // Time domain modulation & warping
    ops::smoothTimeDomain(wt1.getFrame(0), 4);
    ops::bendTimeDomain(wt1.getFrame(0), 0.5f);
    ops::fmBetweenFrames(wt1.getFrame(0), wt1.getFrame(1), 0.2f);
    ops::amBetweenFrames(wt1.getFrame(0), wt1.getFrame(1), 0.5f);

    // Interpolation modes
    Wavetable wtMorph;
    wtMorph.clear();
    while (wtMorph.numFrames() < 5)
        wtMorph.duplicateFrame(0);
    wtMorph.setFrame(0, f1);
    wtMorph.setFrame(4, f2);

    std::vector<size_t> kfs = { 0, 4 };
    ops::interpolateBetweenKeyframes(wtMorph, kfs, MorphMode::Crossfade);
    ops::interpolateBetweenKeyframes(wtMorph, kfs, MorphMode::Spectral);
    ops::interpolateBetweenKeyframes(wtMorph, kfs, MorphMode::SpectralZeroFundPhase);
    ops::interpolateBetweenKeyframes(wtMorph, kfs, MorphMode::SpectralZeroAllPhases);

    // Remove morph tables
    ops::removeMorphTables(wtMorph, kfs);
    CHECK_EQ(wtMorph.numFrames(), 2u);

    // Sorting
    ops::sortFrames(wt1, SortMode::PeakSpect);
    ops::sortFrames(wt1, SortMode::AverageSpect);
    ops::sortFrames(wt1, SortMode::PeakAmount);
    ops::sortFrames(wt1, SortMode::NumWithSpect);
    ops::sortFrames(wt1, SortMode::HighestWithSpect);
    ops::sortFrames(wt1, SortMode::FundamentalAmount);
    ops::sortFrames(wt1, SortMode::Randomize);
    ops::sortFrames(wt1, SortMode::Reverse);
}

//==============================================================================
TEST(vital_group_component_model)
{
    using namespace vital;

    Group group;
    CHECK_EQ(group.numComponents(), 1u);
    CHECK_EQ(group.getComponent(0)->type(), ComponentType::WaveSource);

    // Add PhaseModifier
    auto phaseMod = std::make_unique<Component>(ComponentType::PhaseModifier);
    Keyframe kf0(0.0f);
    kf0.param1 = 0.25f; // phase
    kf0.param2 = 1.0f;  // mix
    Keyframe kf1(255.0f);
    kf1.param1 = 0.75f;
    kf1.param2 = 1.0f;
    phaseMod->insertKeyframe(kf0);
    phaseMod->insertKeyframe(kf1);
    group.addComponent(std::move(phaseMod));

    // Add WaveWindowModifier, FrequencyFilterModifier, SlewLimitModifier, WaveFoldModifier, WaveWarpModifier
    group.addComponent(std::make_unique<Component>(ComponentType::WaveWindowModifier));
    group.addComponent(std::make_unique<Component>(ComponentType::FrequencyFilterModifier));
    group.addComponent(std::make_unique<Component>(ComponentType::SlewLimitModifier));
    group.addComponent(std::make_unique<Component>(ComponentType::WaveFoldModifier));
    group.addComponent(std::make_unique<Component>(ComponentType::WaveWarpModifier));
    CHECK_EQ(group.numComponents(), 7u);

    // Render single frame
    Frame outFrame;
    group.renderFrame(128.0f, outFrame);
    bool nonZero = false;
    for (size_t i = 0; i < kWaveformSize; ++i)
        if (std::fabs(outFrame[i]) > 1e-4f) nonZero = true;
    CHECK(nonZero);

    // Render entire wavetable
    Wavetable renderedWt;
    group.renderWavetable(renderedWt, 16);
    CHECK_EQ(renderedWt.numFrames(), 16u);

    // Synchronize group from Wavetable
    Group synced;
    synced.initFromWavetable(renderedWt);
    CHECK_EQ(synced.getComponent(0)->numKeyframes(), 16u);
}

//==============================================================================
TEST(undo_redo_bit_exact)
{
    UndoStack undoStack(10);
    CHECK(!undoStack.canUndo());
    CHECK(!undoStack.canRedo());

    Wavetable wt1("Table1");
    wt1.getFrame(0)[0] = 0.12345f;
    wt1.getFrame(0)[100] = -0.6789f;

    undoStack.push(wt1);
    CHECK(!undoStack.canUndo()); // 1 state = nowhere to undo

    Wavetable wt2 = wt1;
    wt2.getFrame(0)[0] = 0.9999f;
    wt2.setName("Table2");
    undoStack.push(wt2);
    CHECK(undoStack.canUndo());
    CHECK(!undoStack.canRedo());

    // Undo: must restore Table1 bit-exactly
    Wavetable restored;
    CHECK(undoStack.undo(restored));
    CHECK_EQ(restored.name(), "Table1");
    CHECK_EQ(restored.getFrame(0)[0], wt1.getFrame(0)[0]);
    CHECK_EQ(restored.getFrame(0)[100], wt1.getFrame(0)[100]);
    CHECK(std::memcmp(restored.getFrame(0).data.data(), wt1.getFrame(0).data.data(), sizeof(Frame)) == 0);

    // Redo: must restore Table2 bit-exactly
    CHECK(undoStack.canRedo());
    CHECK(undoStack.redo(restored));
    CHECK_EQ(restored.name(), "Table2");
    CHECK_EQ(restored.getFrame(0)[0], wt2.getFrame(0)[0]);
    CHECK(std::memcmp(restored.getFrame(0).data.data(), wt2.getFrame(0).data.data(), sizeof(Frame)) == 0);

    // Bounded memory test: push 15 states with maxDepth = 10
    for (int i = 0; i < 15; ++i)
    {
        Wavetable state("Step" + std::to_string(i));
        undoStack.push(state);
    }
    // Can undo at most 9 times (10 states in history)
    int undoCount = 0;
    while (undoStack.undo(restored))
        ++undoCount;
    CHECK_EQ(undoCount, 9);
}

//==============================================================================
TEST(wav_export_import_roundtrip)
{
    Wavetable original("ExportTest");
    original.clear();

    for (size_t f = 0; f < 3; ++f)
    {
        Frame frame;
        for (size_t i = 0; i < kWaveformSize; ++i)
        {
            const double t = static_cast<double>(i) / static_cast<double>(kWaveformSize);
            frame[i] = static_cast<float>(0.8 * std::sin(2.0 * 3.1415926535 * (f + 1) * t));
        }
        original.insertFrame(f, frame);
    }
    // original has 1 cleared + 3 inserted = 4 frames
    CHECK_EQ(original.numFrames(), 4u);

    // 1. Export 32-bit Float WAV
    ExportOptions opt32;
    opt32.format = ExportFormat::Wav32BitFloat;
    opt32.writeSerumClmChunk = true;

    std::vector<uint8_t> wav32Bytes;
    CHECK(exportWav(original, opt32, wav32Bytes));
    CHECK(wav32Bytes.size() > 44);

    // Verify 'clm ' chunk exists in bytes
    bool foundClm = false;
    for (size_t i = 0; i < wav32Bytes.size() - 4; ++i)
    {
        if (std::memcmp(wav32Bytes.data() + i, "clm ", 4) == 0)
        {
            foundClm = true;
            break;
        }
    }
    CHECK(foundClm);

    // Import 32-bit Float WAV back
    Wavetable imported32;
    ImportOptions importOpt;
    importOpt.mode = ImportMode::ByFrameSize;
    importOpt.frameSize = kWaveformSize;
    CHECK(importAudioWav(wav32Bytes.data(), wav32Bytes.size(), importOpt, imported32));
    CHECK_EQ(imported32.numFrames(), 4u);

    for (size_t f = 0; f < 4; ++f)
    {
        for (size_t i = 0; i < kWaveformSize; ++i)
            CHECK_NEAR(imported32.getFrame(f)[i], original.getFrame(f)[i], 1e-6);
    }

    // 2. Export 16-bit PCM WAV
    ExportOptions opt16;
    opt16.format = ExportFormat::Wav16Bit;
    opt16.writeSerumClmChunk = true;

    std::vector<uint8_t> wav16Bytes;
    CHECK(exportWav(original, opt16, wav16Bytes));

    Wavetable imported16;
    CHECK(importAudioWav(wav16Bytes.data(), wav16Bytes.size(), importOpt, imported16));
    CHECK_EQ(imported16.numFrames(), 4u);

    // 16-bit roundtrip tolerance: 1 / 32768 ~= 3e-5
    for (size_t f = 0; f < 4; ++f)
    {
        for (size_t i = 0; i < kWaveformSize; ++i)
            CHECK_NEAR(imported16.getFrame(f)[i], original.getFrame(f)[i], 1e-3);
    }

    // 3. Single-cycle export
    ExportOptions optSingle;
    optSingle.format = ExportFormat::SingleCycle;
    optSingle.singleFrameIndex = 1;

    std::vector<uint8_t> singleBytes;
    CHECK(exportWav(original, optSingle, singleBytes));

    Wavetable importedSingle;
    importOpt.mode = ImportMode::FixedSingleCycle;
    CHECK(importAudioWav(singleBytes.data(), singleBytes.size(), importOpt, importedSingle));
    CHECK_EQ(importedSingle.numFrames(), 1u);
    for (size_t i = 0; i < kWaveformSize; ++i)
        CHECK_NEAR(importedSingle.getFrame(0)[i], original.getFrame(1)[i], 1e-5);
}

//==============================================================================
TEST(audio_import_slicing_modes)
{
    // Generate synthetic audio signal of 8192 samples (approx 4 cycles of 2048)
    const size_t totalSamples = 8192;
    std::vector<float> audio(totalSamples);
    for (size_t i = 0; i < totalSamples; ++i)
    {
        const double t = static_cast<double>(i) / 2048.0;
        audio[i] = static_cast<float>(std::sin(2.0 * 3.1415926535 * t));
    }

    Wavetable wt;

    // FixedSingleCycle
    ImportOptions opt;
    opt.mode = ImportMode::FixedSingleCycle;
    CHECK(importAudioSamples(audio.data(), audio.size(), 44100.0, opt, wt));
    CHECK_EQ(wt.numFrames(), 1u);

    // ByFrameSize
    opt.mode = ImportMode::ByFrameSize;
    opt.frameSize = 1024;
    CHECK(importAudioSamples(audio.data(), audio.size(), 44100.0, opt, wt));
    CHECK_EQ(wt.numFrames(), 8u);

    // PitchDetectZeroSnap
    opt.mode = ImportMode::PitchDetectZeroSnap;
    CHECK(importAudioSamples(audio.data(), audio.size(), 44100.0, opt, wt));
    CHECK(wt.numFrames() >= 2u);

    // PitchDetectFollow
    opt.mode = ImportMode::PitchDetectFollow;
    CHECK(importAudioSamples(audio.data(), audio.size(), 44100.0, opt, wt));
    CHECK(wt.numFrames() >= 2u);

    // PitchAverage
    opt.mode = ImportMode::PitchAverage;
    CHECK(importAudioSamples(audio.data(), audio.size(), 44100.0, opt, wt));
    CHECK(wt.numFrames() >= 2u);

    // FFT modes
    opt.mode = ImportMode::Fft256;
    CHECK(importAudioSamples(audio.data(), audio.size(), 44100.0, opt, wt));
    CHECK(wt.numFrames() >= 4u);

    opt.mode = ImportMode::Fft512;
    CHECK(importAudioSamples(audio.data(), audio.size(), 44100.0, opt, wt));
    CHECK(wt.numFrames() >= 4u);

    opt.mode = ImportMode::Fft1024;
    CHECK(importAudioSamples(audio.data(), audio.size(), 44100.0, opt, wt));
    CHECK(wt.numFrames() >= 4u);

    opt.mode = ImportMode::Fft2048;
    CHECK(importAudioSamples(audio.data(), audio.size(), 44100.0, opt, wt));
    CHECK(wt.numFrames() >= 3u);

    // Resample test: arbitrary inLength to 2048
    std::vector<float> resampled(kWaveformSize);
    resampleBuffer(audio.data(), 500, resampled.data(), kWaveformSize);
    CHECK(std::fabs(resampled[0]) <= 1.0f);
}

//==============================================================================
TEST(malformed_wav_fuzz)
{
    Wavetable wt;
    ImportOptions opt;
    std::string err;

    // Empty data
    CHECK(!importAudioWav(nullptr, 0, opt, wt, &err));

    // Truncated headers
    uint8_t tiny[20] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E' };
    CHECK(!importAudioWav(tiny, sizeof(tiny), opt, wt, &err));

    // Garbage data
    std::vector<uint8_t> garbage(256, 0xAA);
    CHECK(!importAudioWav(garbage.data(), garbage.size(), opt, wt, &err));

    // Bad chunk size claiming 1GB
    uint8_t fakeRiff[44] = {
        'R', 'I', 'F', 'F', 0x00, 0x00, 0x00, 0x40, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0, 0x44, 0xAC, 0, 0,
        0x88, 0x58, 1, 0, 2, 0, 16, 0,
        'd', 'a', 't', 'a', 0xFF, 0xFF, 0xFF, 0x7F
    };
    CHECK(!importAudioWav(fakeRiff, sizeof(fakeRiff), opt, wt, &err));

    // Unsupported format tag (e.g. 7 = mu-law)
    fakeRiff[20] = 7;
    CHECK(!importAudioWav(fakeRiff, sizeof(fakeRiff), opt, wt, &err));
}

//==============================================================================
TEST(image_import_invariants)
{
    // 8x16 grayscale buffer (8 frames, 16 harmonics)
    const int W = 8;
    const int H = 16;
    std::vector<uint8_t> gray(static_cast<size_t>(W * H), 128); // mid gray

    Wavetable wtGray;
    CHECK(importImageGrayscale(gray.data(), W, H, wtGray));
    CHECK_EQ(wtGray.numFrames(), 8u);
    for (size_t f = 0; f < 8; ++f)
    {
        CHECK_EQ(wtGray.getFrame(f).data.size(), kWaveformSize);
        // Peak normalized <= 1.0
        float maxVal = 0.0f;
        for (size_t i = 0; i < kWaveformSize; ++i)
        {
            const float a = std::fabs(wtGray.getFrame(f)[i]);
            if (a > maxVal) maxVal = a;
        }
        CHECK(maxVal <= 1.0001f);
    }

    // RGBA test
    std::vector<uint8_t> rgba(static_cast<size_t>(W * H * 4), 200);
    Wavetable wtRgba;
    CHECK(importImageRgba(rgba.data(), W, H, wtRgba));
    CHECK_EQ(wtRgba.numFrames(), 8u);
}

//==============================================================================
TEST(realtime_player_invariants)
{
    Wavetable wt;
    wt.initDefault();

    WavetablePlayer player;

    // Test across sample rates (22050 to 192000)
    const double rates[] = { 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    for (double sr : rates)
    {
        player.prepare(sr, 4096);
        player.setWavetable(&wt);
        player.reset();

        PlayerParams params;
        params.frequency = 440.0f;
        params.gain = 0.8f;
        params.framePosition = 0.0f;

        // Test across block sizes (1 to 4096)
        const int blockSizes[] = { 1, 16, 64, 128, 512, 1024, 4096 };
        for (int bs : blockSizes)
        {
            std::vector<float> buffer(static_cast<size_t>(bs), 0.0f);
            player.process(params, buffer.data(), bs);

            for (int i = 0; i < bs; ++i)
            {
                CHECK(!std::isnan(buffer[static_cast<size_t>(i)]));
                CHECK(!std::isinf(buffer[static_cast<size_t>(i)]));
                CHECK(std::fabs(buffer[static_cast<size_t>(i)]) <= 1.5f);
            }
        }
    }

    // Denormal and NaN/inf safety test
    player.prepare(44100.0, 128);
    player.setWavetable(&wt);

    PlayerParams badParams;
    badParams.frequency = std::numeric_limits<float>::quiet_NaN();
    badParams.gain = std::numeric_limits<float>::infinity();
    badParams.framePosition = -10.0f;

    std::vector<float> badBuffer(128, 999.0f);
    player.process(badParams, badBuffer.data(), 128);

    for (int i = 0; i < 128; ++i)
    {
        CHECK(!std::isnan(badBuffer[static_cast<size_t>(i)]));
        CHECK(!std::isinf(badBuffer[static_cast<size_t>(i)]));
    }
}
