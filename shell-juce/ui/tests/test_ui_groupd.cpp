// SynthyBoy - headless UI tests of GROUPD (WP-14 / WP-15 / WP-16, docs/ui-design/13-new-keys.md 4.40): Switch to wavetable from the Sample, Granular and Spectral oscillators,
// Render OSC Warp, Resample, the last note as audio (save, share, drag-out), wavetable frame <-> LFO shape (menu, Alt-drag), state round trip. A real processor with a temp user
// directory, audio through processBlock, the panels as components. Synthetic fixtures only.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "PluginProcessor.h"
#include "groupd_util.h"
#include "synthyboy/preferences.h"
#include "synthyboy/user_content.h"
#include "synthyboy/wave_convert.h"
#include "test_runner.h"
#include "ui/ContentIO.h"
#include "ui/ContentSheets.h"
#include "EditorAdapter.h"
#include "ui/Editor.h"
#include "ui/panels/LfoShapeEditor.h"
#include "ui/panels/LfoTabsPanel.h"
#include "ui/panels/OscWaveView.h"

using namespace synthyboy;
using namespace synthyboy::ui;
using namespace groupd;
namespace fs = std::filesystem;

namespace
{
juce::ScopedJuceInitialiser_GUI* juceInitGroupD = new juce::ScopedJuceInitialiser_GUI();

fs::path baseDir()
{
    static const fs::path dir = fs::temp_directory_path() / "sb_test_ui_groupd";
    return dir;
}

struct UserDirScope
{
    UserDirScope (const char* name, bool wipe)
    {
        dir = baseDir() / name;
        if (wipe)
        {
            std::error_code ec;
            fs::remove_all (dir, ec);
        }
        fs::create_directories (dir);
        sbtest::setEnv ("SYNTHYBOY_USER_DIR", (dir / "user").string().c_str());
    }
    fs::path dir;
};

struct Host : private UserDirScope
{
    SynthyBoyProcessor proc;
    Theme theme = Theme::dark();
    ModAssignController assign { proc };
    UiContext ctx { proc, theme, assign };

    explicit Host (const char* name, bool wipe = true) : UserDirScope (name, wipe)
    {
        juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();
        proc.setPlayConfigDetails (0, 2, 48000.0, 256);
        proc.prepareToPlay (48000.0, 256);
        plain();
    }
    ~Host() { proc.setFileRequestHandler ({}); proc.setShareHandler ({}); }
    /** One oscillator voice, no random phase, no filter, a flat envelope. */
    void plain()
    {
        setParameter (proc, "osc1.random_phase", 0.0f);
        setParameter (proc, "osc1.phase", 0.0f);
        setParameter (proc, "osc1.unison.voices", 1.0f);
        setParameter (proc, "filter1.on", 0.0f);
        setParameter (proc, "osc1.level", 50.0f);
        setParameter (proc, "env1.attack", 0.0f);
        setParameter (proc, "env1.sustain", 100.0f);
        setParameter (proc, "env1.release", 20.0f);
        setParameter (proc, "master.volume", -6.0f);
    }
    fs::path workDir() const { return dir / "files"; }
};

fs::path writeBytes (const fs::path& path, const std::vector<uint8_t>& bytes)
{
    fs::create_directories (path.parent_path());
    std::ofstream out (path, std::ios::binary);
    out.write (reinterpret_cast<const char*> (bytes.data()), static_cast<std::streamsize> (bytes.size()));
    return path;
}

/** One held note, `seconds` long, left channel. */
std::vector<float> render (SynthyBoyProcessor& proc, int key, double seconds, bool prepare = true)
{
    if (prepare)
        proc.prepareToPlay (48000.0, 256);
    const int blocks = static_cast<int> (seconds * 48000.0) / 256;
    std::vector<float> out;
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buffer (2, 256);
        buffer.clear();
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, key, 1.0f), 0);
        proc.processBlock (buffer, midi);
        out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 256);
    }
    return out;
}

double peakOf (const std::vector<float>& v, size_t from = 0)
{
    float p = 0.0f;
    for (size_t i = from; i < v.size(); ++i)
        p = std::max (p, std::fabs (v[i]));
    return p;
}

/** Harmonics 1..count of a played key (dB relative to the first one) over the steady part of a render. */
std::vector<double> relativeHarmonics (const std::vector<float>& audio, int key, int count = 12, double from = 0.4, double length = 0.6)
{
    const double f0 = 440.0 * std::pow (2.0, (key - 69) / 12.0);
    auto h = harmonicDb (audio, static_cast<size_t> (from * 48000.0), static_cast<size_t> (length * 48000.0), f0, 48000.0, count);
    for (size_t i = h.size(); i-- > 0;)
        h[i] -= h[0];
    return h;
}

int choice (const char* key, const char* name) { return choiceIndexOf (key, name); }

/** 1 s of a 220 Hz tone: the first half a saw (1/h), the second half a square (odd harmonics 1/h). */
std::vector<float> halves (double seconds = 2.0)
{
    const size_t n = static_cast<size_t> (seconds * 48000.0);
    std::vector<float> v (n, 0.0f);
    for (size_t i = 0; i < n; ++i)
    {
        const bool saw = i < n / 2;
        double sum = 0.0;
        for (int h = 1; h <= 16; ++h)
            if (saw || (h % 2) == 1)
                sum += std::sin (2.0 * kPi * 220.0 * h * static_cast<double> (i) / 48000.0) / h;
        v[i] = static_cast<float> (sum * (saw ? 0.5 : 0.6));
    }
    return v;
}
} // namespace

TEST (groupd_switch_to_wavetable_from_sample_granular_and_spectral_oscillators)
{
    Host h ("switch");
    const auto source = halves();
    const fs::path wav = writeBytes (h.workDir() / "Halves A3.wav", encodeWav (source.data(), nullptr, source.size(), 48000, 16));
    OscWaveView view (h.ctx, 1);
    view.setBounds (0, 0, 390, 220);
    CHECK (view.loadSampleFile (juce::File (wav.string())).ok);
    const size_t half = source.size() / 2;
    const std::vector<float> sawHalf (source.begin(), source.begin() + static_cast<long> (half)), squareHalf (source.begin() + static_cast<long> (half), source.end());
    const auto sawHarmonics = [&] { auto x = harmonicDb (sawHalf, 24000, 48000, 220.0, 48000.0, 12); for (size_t i = x.size(); i-- > 0;) x[i] -= x[0]; return x; }();
    const auto squareHarmonics = [&] { auto x = harmonicDb (squareHalf, 24000, 48000, 220.0, 48000.0, 12); for (size_t i = x.size(); i-- > 0;) x[i] -= x[0]; return x; }();

    // a wavetable oscillator and an oscillator without a sample are refused with a reason
    {
        OscWaveView other (h.ctx, 2);
        const auto refused = other.switchToWavetable ({});
        CHECK (! refused.ok && refused.message.contains ("Sample, Granular or Spectral"));
        setParameter (h.proc, "osc2.type", static_cast<float> (kOscTypeSample));
        const auto noSample = other.switchToWavetable ({});
        CHECK (! noSample.ok && noSample.message.contains ("no sample"));
        setParameter (h.proc, "osc2.type", static_cast<float> (kOscTypeWavetable));
    }

    for (const int type : { kOscTypeSample, kOscTypeGranular, kOscTypeSpectral })
    {
        // the first half (START 0 .. END 50 %): the saw
        setParameter (h.proc, "osc1.type", static_cast<float> (type));
        setParameter (h.proc, "osc1.sample.start", 0.0f);
        setParameter (h.proc, "osc1.sample.end", 50.0f);
        CHECK_EQ (view.oscType(), type);
        CHECK (view.sampleMenuButton().isVisible() && view.sampleMenuButton().isEnabled());
        const auto switched = view.switchToWavetable ({});
        std::printf ("    type %d: %s\n", type, switched.message.toRawUTF8());
        CHECK (switched.ok);
        CHECK_EQ (view.oscType(), kOscTypeWavetable);
        CHECK_EQ (juce::roundToInt (readParameter (h.proc, "osc1.wavetable")), kWavetablePresetTable);
        const auto content = h.proc.content();
        const ContentWavetable* table = wavetableOf (content, 1);
        CHECK (table != nullptr && table->frames > 10);
        CHECK (sampleOf (content, 1) != nullptr);    // the sample stays attached: the type menu goes back to it
        const auto heard = relativeHarmonics (render (h.proc, 57, 1.5), 57);
        const double worst = worstHarmonicDifference (sawHarmonics, heard, 40.0);
        std::printf ("    type %d: saw region, harmonics 1..12 worst difference %.3f dB\n", type, worst);
        CHECK (worst < 0.5);
    }

    // the second half (50 .. 100 %): the square, a different table from the same sample
    setParameter (h.proc, "osc1.type", static_cast<float> (kOscTypeGranular));
    setParameter (h.proc, "osc1.sample.start", 50.0f);
    setParameter (h.proc, "osc1.sample.end", 100.0f);
    CHECK (view.switchToWavetable ({}).ok);
    const auto heard = relativeHarmonics (render (h.proc, 57, 1.5), 57);
    CHECK (worstHarmonicDifference (squareHarmonics, heard, 40.0) < 0.5);
    CHECK (heard[1] < -40.0);                      // no even harmonics: it is the square, not the saw

    // another import option (FFT 1024) still gives a sounding table; a region that is too short is refused
    setParameter (h.proc, "osc1.type", static_cast<float> (kOscTypeSpectral));
    wtedit::ImportOptions fft;
    fft.mode = wtedit::ImportMode::Fft1024;
    CHECK (view.switchToWavetable (fft).ok);
    CHECK (peakOf (render (h.proc, 57, 0.6)) > 0.01);
    setParameter (h.proc, "osc1.type", static_cast<float> (kOscTypeSample));
    setParameter (h.proc, "osc1.sample.start", 50.0f);
    setParameter (h.proc, "osc1.sample.end", 50.01f);
    const auto tooShort = view.switchToWavetable ({});
    CHECK (! tooShort.ok && tooShort.message.contains ("too short"));
    CHECK_EQ (view.oscType(), kOscTypeSample);

    // state round trip: the new table and the type are in the saved state; a fresh processor on the same user directory plays the same sound
    setParameter (h.proc, "osc1.sample.start", 0.0f);
    setParameter (h.proc, "osc1.sample.end", 50.0f);
    CHECK (view.switchToWavetable ({}).ok);
    const auto before = render (h.proc, 57, 1.2);
    juce::MemoryBlock state;
    h.proc.getStateInformation (state);
    Host again ("switch", false);
    again.proc.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK_EQ (juce::roundToInt (readParameter (again.proc, "osc1.type")), kOscTypeWavetable);
    CHECK (wavetableOf (again.proc.content(), 1) != nullptr);
    const auto after = render (again.proc, 57, 1.2);
    CHECK (worstHarmonicDifference (relativeHarmonics (before, 57), relativeHarmonics (after, 57), 40.0) < 0.01);
}

TEST (groupd_render_osc_warp_bakes_the_warp_and_resample_captures_the_patch)
{
    Host h ("render");
    OscWaveView view (h.ctx, 1);
    view.setBounds (0, 0, 390, 220);
    CHECK (view.waveMenuButton().isVisible());
    setParameter (h.proc, "osc1.warp.type", static_cast<float> (choice ("osc1.warp.type", "Sync")));
    setParameter (h.proc, "osc1.warp.amount", 40.0f);
    const auto reference = relativeHarmonics (render (h.proc, 57, 1.3), 57, 24);
    const float referenceLevel = static_cast<float> (peakOf (render (h.proc, 57, 1.0)));

    const auto warp = view.renderWarp();
    std::printf("    %s\n", warp.message.toRawUTF8());
    CHECK (warp.ok);
    CHECK_EQ (juce::roundToInt (readParameter (h.proc, "osc1.warp.type")), 0);
    CHECK_EQ (juce::roundToInt (readParameter (h.proc, "osc1.morph.type")), 0);
    CHECK_NEAR (readParameter (h.proc, "osc1.frame"), 40.0, 1.0e-3);
    CHECK_EQ (juce::roundToInt (readParameter (h.proc, "osc1.wavetable")), kWavetablePresetTable);
    CHECK_EQ (wavetableOf (h.proc.content(), 1)->frames, 256);
    const auto baked = render (h.proc, 57, 1.3);
    const double worst = worstHarmonicDifference (reference, relativeHarmonics (baked, 57, 24), 30.0);
    std::printf ("    Render OSC Warp through the shell: harmonics 1..24 worst difference %.3f dB, level %.4f against %.4f\n", worst, peakOf (render (h.proc, 57, 1.0)), referenceLevel);
    CHECK (worst < 0.1);
    CHECK_NEAR (peakOf (render (h.proc, 57, 1.0)) / referenceLevel, 1.0, 0.02);

    // a sample oscillator cannot be rendered
    OscWaveView second (h.ctx, 2);
    setParameter (h.proc, "osc2.type", static_cast<float> (kOscTypeSample));
    CHECK (! second.renderWarp().ok);
    setParameter (h.proc, "osc2.type", static_cast<float> (kOscTypeWavetable));

    // Resample: a filtered saw, played once, then captured as the table of osc 1 (the filter off afterwards: the table carries the sound)
    Host r ("resample");
    setParameter (r.proc, "filter1.on", 1.0f);
    setParameter (r.proc, "filter1.cutoff", 1800.0f);
    setParameter (r.proc, "filter1.resonance", 35.0f);
    setParameter (r.proc, "osc1.level", 100.0f);
    const auto filtered = render (r.proc, 60, 1.0);
    const auto filteredHarmonics = relativeHarmonics (filtered, 60, 10, 0.3, 0.5);
    const auto resampled = resampleToWavetable (r.ctx, 1);
    std::printf("    %s\n", resampled.message.toRawUTF8());
    CHECK (resampled.ok);
    CHECK_EQ (juce::roundToInt (readParameter (r.proc, "osc1.wavetable")), kWavetablePresetTable);
    setParameter (r.proc, "filter1.on", 0.0f);
    setParameter (r.proc, "osc1.frame", 50.0f);
    const auto playedBack = render (r.proc, 60, 1.2);
    const double resampleWorst = worstHarmonicDifference (filteredHarmonics, relativeHarmonics (playedBack, 60, 10, 0.4, 0.5), 30.0);
    std::printf ("    Resample through the shell: filtered saw harmonics 1..10, worst difference %.3f dB\n", resampleWorst);
    CHECK (resampleWorst < 1.0);
    // Osc A + Osc B
    const auto both = resampleToWavetable (r.ctx, 0);
    CHECK (both.ok);
    CHECK (wavetableOf (r.proc.content(), 1) != nullptr && wavetableOf (r.proc.content(), 2) != nullptr);
    CHECK (! resampleToWavetable (r.ctx, 7).ok);

    // the ultra preference is honoured by the renders (4x oversampling): the audio differs, each setting renders the same twice
    LastNoteAudio normalA, normalB, ultraA, ultraB;
    CHECK (renderLastNote (r.ctx, normalA).ok);
    CHECK (renderLastNote (r.ctx, normalB).ok);
    CHECK (normalA.audio.left == normalB.audio.left);
    synthyboy::Preferences prefs = r.proc.preferences();
    prefs.ultraRender = true;
    r.proc.setPreferences (prefs);
    CHECK (renderLastNote (r.ctx, ultraA).ok);
    CHECK (renderLastNote (r.ctx, ultraB).ok);
    CHECK (ultraA.audio.left == ultraB.audio.left);
    CHECK (ultraA.audio.left != normalA.audio.left);
}

TEST (groupd_last_note_as_audio_saves_shares_and_drags_out_without_disturbing_the_live_engine)
{
    Host h ("lastnote");
    // no note yet: refused
    LastNoteAudio none;
    const auto nothing = renderLastNote (h.ctx, none);
    CHECK (! nothing.ok && nothing.message.contains ("play a note"));

    // the live engine plays a note; a render of that note in the middle of it must not change one sample of the live output
    const auto livePlay = [] (Host& host, bool withRender)
    {
        host.proc.prepareToPlay (48000.0, 256);
        std::vector<float> out;
        for (int b = 0; b < 40; ++b)
        {
            juce::AudioBuffer<float> buffer (2, 256);
            buffer.clear();
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 52, 0.9f), 0);
            if (b == 12 && withRender)
            {
                LastNoteAudio note;
                CHECK (renderLastNote (host.ctx, note).ok);
                CHECK (note.audio.frames() > 12000);
            }
            host.proc.processBlock (buffer, midi);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 256);
            out.insert (out.end(), buffer.getReadPointer (1), buffer.getReadPointer (1) + 256);
        }
        return out;
    };
    Host quiet ("lastnote_a"), busy ("lastnote_b");
    CHECK (livePlay (quiet, false) == livePlay (busy, true));

    // the last note: key 64, held for 0.6 s, then released
    render (h.proc, 64, 0.6);
    {
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOff (1, 64), 0);
        h.proc.processBlock (buffer, midi);
    }
    const auto last = h.proc.lastNote();
    CHECK (last.valid && last.key == 64);
    CHECK_NEAR (last.heldSeconds, 0.6, 0.02);

    SynthyBoyEditor* editorRaw = nullptr;
    std::unique_ptr<juce::AudioProcessorEditor> base (h.proc.createEditor());
    editorRaw = innerEditor (base.get());
    CHECK (editorRaw != nullptr);
    if (editorRaw == nullptr)
        return;
    editorRaw->setSize (390, 844);
    auto* sheet = editorRaw->showLastNoteSheet();
    CHECK (sheet != nullptr && editorRaw->contentSheet() == sheet);
    CHECK (sheet->note().audio.frames() > static_cast<size_t> (0.6 * 48000.0));
    CHECK (sheet->note().wav.size() > 44u);
    CHECK (sheet->saveButton().isEnabled() && sheet->shareButton().isEnabled() && sheet->dragChip().isVisible());
    CHECK_NEAR (sheet->note().audio.sampleRate, 48000.0, 1.0e-6);

    // Save as WAV: the host's file request answers with a path
    const fs::path saved = h.workDir() / "exported" / "My note";
    h.proc.setFileRequestHandler ([&] (const FileRequest& request, std::function<void (const juce::File&)> done)
    {
        CHECK (request.mode == FileRequest::Mode::save && request.purpose == "export");
        done (juce::File (saved.string()));
        return true;
    });
    sheet->saveButton().onClick();
    CHECK (fs::exists (saved.string() + ".wav") && fs::file_size (saved.string() + ".wav") == sheet->note().wav.size());

    // Share: the share hook gets the temp file (the platform share sheet on iOS / Android)
    juce::File shared;
    h.proc.setShareHandler ([&shared] (const juce::File& file, const juce::String&) { shared = file; return true; });
    sheet->shareButton().onClick();
    CHECK (shared.existsAsFile() && shared.hasFileExtension ("wav"));
    CHECK_EQ (static_cast<size_t> (shared.getSize()), sheet->note().wav.size());

    // Drag-out: the same file is what the external drag carries (the test replaces the platform drag)
    juce::File dragged;
    sheet->dragHandler = [&dragged] (const juce::File& file) { dragged = file; return true; };
    CHECK (sheet->startDragOut());
    CHECK_EQ (dragged.getFullPathName(), shared.getFullPathName());

    // the main menu path: the editor opens the same sheet and the WAV decodes to the played pitch
    DecodedAudio decoded;
    CHECK (decodeAudioFile (shared, decoded));
    CHECK_NEAR (decoded.sampleRate, 48000.0, 1.0e-6);
    CHECK_NEAR (crossings (decoded.left, 4800, 24000, 48000.0), 440.0 * std::pow (2.0, (64 - 69) / 12.0), 3.0);

    // Render OSC Warp / Resample from the menu entries (a message sheet shows the outcome)
    editorRaw->closeContentSheet();
    const auto resampled = editorRaw->resampleFromMenu (1);
    CHECK (resampled.ok);
    CHECK (editorRaw->contentSheet() != nullptr);
    const auto warped = editorRaw->renderOscWarpFromMenu (1);
    CHECK (warped.ok);
    // Close hides the sheet and releases the render
    sheet = editorRaw->showLastNoteSheet();
    sheet->closeButton().onClick();
    CHECK (editorRaw->contentSheet() == nullptr);
}

TEST (groupd_wavetable_frame_and_lfo_shape_copy_both_ways)
{
    Host h ("lfo");
    // Wavetable A to LFO 2: the built-in saw's frame as the shape; the shape plays as a Custom LFO
    LfoShapeEditor editor (h.ctx, 2);
    editor.setBounds (0, 0, 390, 360);
    const auto copied = editor.copyFromWavetable (1);
    std::printf("    %s\n", copied.message.toRawUTF8());
    CHECK (copied.ok);
    CHECK_EQ (juce::roundToInt (readParameter (h.proc, "lfo2.shape")), kLfoShapeCustom);
    const auto content = h.proc.content();
    const auto it = std::find_if (content.lfoShapes.begin(), content.lfoShapes.end(), [] (const ContentLfoShape& s) { return s.lfo == 2; });
    CHECK (it != content.lfoShapes.end());
    if (it == content.lfoShapes.end())
        return;
    CHECK (it->points.size() >= 3 && it->points.size() <= static_cast<size_t> (kMaxLfoShapePoints));
    CHECK_EQ (it->points.front().x, 0.0f);
    CHECK_EQ (it->points.back().x, 1.0f);
    // the editor shows it and Undo takes it away again
    CHECK (editor.canUndo());
    CHECK_EQ (editor.shape().points.size(), it->points.size());

    // the frame the shape was made from is the engine's: frame -> LFO -> frame stays within the error bound of the point reduction
    wtedit::Wavetable raw;
    const auto values = std::make_unique<ParamValues> (readAllValues (h.proc));
    convert::WarpRenderOptions options;
    options.frames = 2;
    options.rawFrame = true;
    CHECK (convert::renderOscWarp (offlineRunner (h.ctx), values.get(), options, "frame", raw));
    wtedit::Frame back;
    convert::lfoShapeToFrame (*it, back);
    double worst = 0.0;
    for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
        worst = std::max (worst, static_cast<double> (std::fabs (raw.getFrame (0)[i] - back[i])));
    std::printf ("    frame -> LFO -> frame through the shell: max error %.5f\n", worst);
    CHECK (worst <= 2.0 * 0.002 + 1.0e-3);

    // LFO 2 to Wavetable B: osc 2 has a built-in table, so the shape becomes a one-frame table "LFO 2 shape"
    const auto written = editor.copyToWavetable (2);
    CHECK (written.ok);
    wtedit::Wavetable table;
    CHECK (loadWavetableOf (h.proc, 2, table));
    CHECK_EQ (table.numFrames(), static_cast<size_t> (1));
    CHECK_EQ (table.name(), std::string ("LFO 2 shape"));
    CHECK_EQ (juce::roundToInt (readParameter (h.proc, "osc2.wavetable")), kWavetablePresetTable);
    double tableError = 0.0;
    for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
        tableError = std::max (tableError, static_cast<double> (std::fabs (table.getFrame (0)[i] - back[i])));
    CHECK (tableError < 1.0e-3);   // 16-bit file: the quantisation of the stored table
    setParameter (h.proc, "osc2.on", 1.0f);
    CHECK (peakOf (render (h.proc, 57, 0.5)) > 0.01);

    // an own table: the shape replaces the current frame only (osc 3 has four frames, WT POS at the end)
    wtedit::Wavetable four ("Four");
    four.clear();
    for (size_t f = 0; f < 4; ++f)
    {
        wtedit::Frame frame;
        for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
            frame[i] = 0.5f * static_cast<float> (std::sin (2.0 * kPi * static_cast<double> (f + 1) * static_cast<double> (i) / wtedit::kWaveformSize));
        if (f == 0)
            four.setFrame (0, frame);
        else
            four.insertFrame (f, frame);
    }
    CHECK (saveWavetable (h.ctx, 3, four, false).ok);
    setParameter (h.proc, "osc3.frame", 100.0f);
    setParameter (h.proc, "lfo5.shape", 1.0f); // the built-in Triangle
    const auto replaced = copyLfoShapeToWavetable (h.ctx, 5, 3);
    std::printf("    %s\n", replaced.message.toRawUTF8());
    CHECK (replaced.ok);
    wtedit::Wavetable after;
    CHECK (loadWavetableOf (h.proc, 3, after));
    CHECK_EQ (after.numFrames(), static_cast<size_t> (4));
    wtedit::Frame triangle;
    convert::lfoShapeToFrame (convert::builtInLfoShape (5, 1), triangle);
    double replacedError = 0.0, untouched = 0.0;
    for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
    {
        replacedError = std::max (replacedError, static_cast<double> (std::fabs (after.getFrame (3)[i] - triangle[i])));
        untouched = std::max (untouched, static_cast<double> (std::fabs (after.getFrame (0)[i] - four.getFrame (0)[i])));
    }
    CHECK (replacedError < 1.0e-3);
    CHECK (untouched < 1.0e-3);

    // bad arguments
    CHECK (! copyLfoShapeToWavetable (h.ctx, 11, 1).ok);
    CHECK (! copyWavetableFrameToLfo (h.ctx, 4, 1).ok);
}

TEST (groupd_alt_drag_of_an_lfo_tab_onto_a_wavetable_display_copies_the_shape)
{
    Host h ("altdrag");
    juce::Component root;
    root.setBounds (0, 0, 420, 700);
    root.setVisible (true);
    LfoTabsPanel tabs (h.ctx);
    tabs.build();
    OscWaveView view (h.ctx, 1);
    root.addAndMakeVisible (tabs);
    root.addAndMakeVisible (view);
    tabs.setBounds (0, 0, 420, 120);
    view.setBounds (0, 200, 420, 240);
    // LFO 4 with the Saw Up preset as built-in shape
    setParameter (h.proc, "lfo4.shape", 2.0f);

    // dropped beside the display: nothing happens; on a random LFO chip or a sample oscillator: nothing
    CHECK (! tabs.dropOnWavetable (3, root.getScreenPosition() + juce::Point<int> (210, 600)));
    CHECK (! tabs.dropOnWavetable (11, root.getScreenPosition() + juce::Point<int> (210, 300)));
    CHECK (! tabs.dropOnWavetable (-1, root.getScreenPosition() + juce::Point<int> (210, 300)));
    CHECK (wavetableOf (h.proc.content(), 1) == nullptr);

    // dropped on the display of osc 1: the shape is the table
    CHECK (tabs.dropOnWavetable (3, root.getScreenPosition() + juce::Point<int> (210, 320)));
    wtedit::Wavetable table;
    CHECK (loadWavetableOf (h.proc, 1, table));
    CHECK_EQ (table.numFrames(), static_cast<size_t> (1));
    CHECK_EQ (juce::roundToInt (readParameter (h.proc, "osc1.wavetable")), kWavetablePresetTable);
    // the saw-up LFO: the frame rises from -1 to +1
    CHECK_NEAR (table.getFrame (0)[0], -1.0, 0.01);
    CHECK_NEAR (table.getFrame (0)[1024], 0.0, 0.01);
    CHECK_NEAR (table.getFrame (0)[2047], 1.0, 0.01);
    setParameter (h.proc, "osc1.type", static_cast<float> (kOscTypeSample));
    CHECK (! tabs.dropOnWavetable (3, root.getScreenPosition() + juce::Point<int> (210, 320))); // a sample display is no wavetable
}
