// SynthyBoy - tests of the content pipeline of the JUCE shell (WP-01, docs/ui-design/13-new-keys.md 4.25): the user content library behind the engine,
// the wavetable editor / sample / Convolve flows through the UiHost, the Vital import, the Missing Files dialog and the library menus.
// Headless: a real processor with a temp user directory, audio through processBlock, the panels as components. Synthetic fixtures only.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "PluginProcessor.h"
#include "fixtures.h"
#include "serum_fixtures.h"
#include "synthyboy/library_items.h"
#include "synthyboy/user_content.h"
#include "synthyboy/vital_plugin_import.h"
#include "test_runner.h"
#include "ui/ContentIO.h"
#include "ui/ContentSheets.h"
#include "ui/Editor.h"
#include "ui/LibraryMenu.h"
#include "ui/panels/FxRackView.h"
#include "ui/panels/GlbVoiceControlPanel.h"
#include "ui/panels/LfoShapeEditor.h"
#include "ui/panels/NoisePanel.h"
#include "ui/panels/OscWaveView.h"
#include "ui/panels/WavetableEditorSheet.h"

using namespace synthyboy;
using namespace synthyboy::ui;
namespace fs = std::filesystem;

namespace
{
juce::ScopedJuceInitialiser_GUI* juceInitContent = new juce::ScopedJuceInitialiser_GUI();
constexpr double kPi = 3.14159265358979323846;

fs::path baseDir()
{
    static const fs::path dir = fs::temp_directory_path() / "sb_test_ui_content";
    return dir;
}

/** Sets SYNTHYBOY_USER_DIR before the processor of the member after it is built (members are built in order). */
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
        setenv ("SYNTHYBOY_USER_DIR", (dir / "user").string().c_str(), 1);
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
        // a patch that is easy to compare: one oscillator voice, no random phase, no filter
        setParameter (proc, "osc1.random_phase", 0.0f);
        setParameter (proc, "osc1.unison.voices", 1.0f);
        setParameter (proc, "filter1.on", 0.0f);
        setParameter (proc, "osc1.level", 100.0f);
        setParameter (proc, "env1.release", 20.0f);
    }
    ~Host() { proc.setFileRequestHandler ({}); }
    fs::path workDir() const { return dir / "files"; }
};

fs::path writeBytes (const fs::path& path, const std::vector<uint8_t>& bytes)
{
    fs::create_directories (path.parent_path());
    std::ofstream out (path, std::ios::binary);
    out.write (reinterpret_cast<const char*> (bytes.data()), static_cast<std::streamsize> (bytes.size()));
    return path;
}

std::vector<float> tone (double hz, double seconds, int sr = 48000)
{
    std::vector<float> v (static_cast<size_t> (seconds * sr));
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = 0.8f * static_cast<float> (std::sin (2.0 * kPi * hz * static_cast<double> (i) / sr));
    return v;
}

/** One held note (off at `offSeconds`, negative = held), `seconds` long, left channel. */
std::vector<float> render (SynthyBoyProcessor& proc, int key, double seconds, double offSeconds = -1.0, bool prepare = true)
{
    if (prepare)
        proc.prepareToPlay (48000.0, 256);
    const int blocks = static_cast<int> (seconds * 48000.0) / 256;
    const int offBlock = offSeconds < 0.0 ? -1 : static_cast<int> (offSeconds * 48000.0) / 256;
    std::vector<float> out;
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buffer (2, 256);
        buffer.clear();
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, key, 1.0f), 0);
        if (b == offBlock)
            midi.addEvent (juce::MidiMessage::noteOff (1, key), 0);
        proc.processBlock (buffer, midi);
        out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 256);
    }
    return out;
}

/** Silent blocks with a pause between them: the helper thread of the Convolve effect partitions a new impulse response in real time next to the rendering. */
void warmUp (SynthyBoyProcessor& proc)
{
    for (int round = 0; round < 2; ++round)
    {
        for (int b = 0; b < 16; ++b)
        {
            juce::AudioBuffer<float> buffer (2, 256);
            juce::MidiBuffer midi;
            proc.processBlock (buffer, midi);
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (500));
    }
}

double rms (const std::vector<float>& v, double t0, double t1)
{
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = static_cast<size_t> (t0 * 48000.0); i < static_cast<size_t> (t1 * 48000.0) && i < v.size(); ++i, ++n)
        sum += static_cast<double> (v[i]) * static_cast<double> (v[i]);
    return n > 0 ? std::sqrt (sum / static_cast<double> (n)) : 0.0;
}

/** rms (a - b) / rms (a) over a window. */
double relDiff (const std::vector<float>& a, const std::vector<float>& b, double t0, double t1)
{
    double d = 0.0, s = 0.0;
    for (size_t i = static_cast<size_t> (t0 * 48000.0); i < static_cast<size_t> (t1 * 48000.0) && i < a.size() && i < b.size(); ++i)
    {
        d += (static_cast<double> (a[i]) - static_cast<double> (b[i])) * (static_cast<double> (a[i]) - static_cast<double> (b[i]));
        s += static_cast<double> (a[i]) * static_cast<double> (a[i]);
    }
    return s > 0.0 ? std::sqrt (d / s) : 0.0;
}

double hzOf (const std::vector<float>& v, double t0, double t1)
{
    int crossings = 0;
    for (size_t i = static_cast<size_t> (t0 * 48000.0) + 1; i < static_cast<size_t> (t1 * 48000.0) && i < v.size(); ++i)
        if (v[i - 1] < 0.0f && v[i] >= 0.0f)
            ++crossings;
    return crossings / (t1 - t0);
}

wtedit::Wavetable testTable (const char* name, int harmonicB)
{
    wtedit::Wavetable t (name);
    t.clear();
    for (size_t f = 0; f < 2; ++f)
    {
        wtedit::Frame frame;
        for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
        {
            const double ph = 2.0 * kPi * static_cast<double> (i) / static_cast<double>(wtedit::kWaveformSize);
            frame[i] = 0.6f * static_cast<float> (f == 0 ? std::sin (ph) : std::sin (static_cast<double> (harmonicB) * ph));
        }
        if (f == 0)
            t.setFrame (0, frame);
        else
            t.insertFrame (1, frame);
    }
    return t;
}

fs::path junkPath (Host& h) { return writeBytes (h.workDir() / "junk.json", { 'x', 'y', 'z' }); }
fs::path junkPathIn (Host& h) { return writeBytes (h.workDir() / "junk.wav", { 1, 2, 3, 4, 5, 6 }); }

int presetIndex (const char* key) { return choiceIndexOf (key, "Preset"); }

bool writePng (const juce::Image& image, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    return out.openedOk() && png.writeImageToStream (image, out);
}

/** Pixels that differ between two snapshots. */
int pixelsDiffering (const juce::Image& a, const juce::Image& b)
{
    int n = 0;
    for (int y = 0; y < juce::jmin (a.getHeight(), b.getHeight()); ++y)
        for (int x = 0; x < juce::jmin (a.getWidth(), b.getWidth()); ++x)
            if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                ++n;
    return n;
}
} // namespace

TEST (content_saved_table_survives_a_state_reload_and_differs_from_the_saw_fallback)
{
    Host a ("table");
    setParameter (a.proc, "osc1.frame", findParamByKey ("osc1.frame")->max); // the last frame of the table: the one that differs between the two tables below
    const auto saw = render (a.proc, 57, 0.6);
    CHECK (rms (saw, 0.1, 0.5) > 0.01);

    // Save Table: a 16-bit WAV in the user library, content.wavetables[0], oscN.wavetable = Preset table
    const auto saved = saveWavetable (a.ctx, 1, testTable ("My Test Table", 5), false);
    CHECK (saved.ok);
    CHECK_EQ (static_cast<int> (readParameter (a.proc, "osc1.wavetable")), presetIndex ("osc1.wavetable"));
    const auto content = a.proc.content();
    CHECK_EQ (content.wavetables.size(), static_cast<size_t> (1));
    CHECK_EQ (content.wavetables[0].ref.pack, std::string (kPackWavetables));
    CHECK_EQ (content.wavetables[0].frames, 2);
    CHECK_EQ (content.wavetables[0].ref.sha256.size(), static_cast<size_t> (64));
    CHECK (a.proc.contentLibrary()->contains (content.wavetables[0].ref));
    CHECK_EQ (a.proc.contentLibrary()->list (ContentKind::wavetable).size(), static_cast<size_t> (1));

    const auto withTable = render (a.proc, 57, 0.6);
    CHECK (relDiff (saw, withTable, 0.1, 0.5) > 0.3); // the table is audible: not the saw fallback

    // the state keeps the reference; a second instance with the same user directory plays the same sound
    juce::MemoryBlock state;
    a.proc.getStateInformation (state);
    Host b ("table", false);
    b.proc.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (b.proc.content() == content);
    CHECK (b.proc.contentRevision() > 0);
    CHECK (relDiff (withTable, render (b.proc, 57, 0.6), 0.1, 0.5) < 1.0e-3);
    CHECK (MissingFilesModel (b.ctx).empty());

    // the same state in another user directory: the file is missing and the engine falls back (saw)
    Host c ("table_elsewhere");
    c.proc.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    MissingFilesModel missing (c.ctx);
    CHECK_EQ (missing.items().size(), static_cast<size_t> (1));
    CHECK (relDiff (withTable, render (c.proc, 57, 0.6), 0.1, 0.5) > 0.3);
    CHECK (relDiff (saw, render (c.proc, 57, 0.6), 0.1, 0.5) < 0.3);

    // Embed in Preset: the same file in the embedded pack (not part of the library list)
    const auto embedded = saveWavetable (a.ctx, 1, testTable ("Embedded", 3), true);
    CHECK (embedded.ok);
    CHECK_EQ (a.proc.content().wavetables[0].ref.pack, std::string (kPackEmbedded));
    CHECK_EQ (a.proc.contentLibrary()->list (ContentKind::wavetable).size(), static_cast<size_t> (1));
    CHECK (relDiff (withTable, render (a.proc, 57, 0.6), 0.1, 0.5) > 0.3);

    // the table opens again from the library
    wtedit::Wavetable stored;
    CHECK (loadWavetableOf (a.proc, 1, stored));
    CHECK_EQ (stored.numFrames(), static_cast<size_t> (2));
}

TEST (content_sample_load_plays_the_content_sample_at_the_key_pitch_through_the_shell)
{
    Host h ("sample");
    const fs::path wavFile = writeBytes (h.workDir() / "Tone A3.wav", encodeWav (tone (440.0, 1.0).data(), nullptr, 48000, 48000, 16));
    OscWaveView view (h.ctx, 1);
    view.setBounds (0, 0, 390, 220);
    CHECK (view.preview() == nullptr);

    const auto loaded = view.loadSampleFile (juce::File (wavFile.string()));
    CHECK (loaded.ok);
    CHECK (view.preview() != nullptr);
    CHECK (loaded.message.contains ("root note 69"));
    CHECK_EQ (static_cast<int> (readParameter (h.proc, "osc1.sample.root")), 69); // A3 in the file name
    CHECK_EQ (static_cast<int> (readParameter (h.proc, "osc1.type")), choiceIndexOf ("osc1.type", "Sample"));
    const auto content = h.proc.content();
    CHECK_EQ (content.samples.size(), static_cast<size_t> (1));
    CHECK_EQ (content.samples[0].ref.pack, std::string (kPackSamples));
    CHECK_EQ (content.samples[0].frames, 48000);
    setParameter (h.proc, "osc1.sample.loop_mode", 1.0f);

    const auto a4 = render (h.proc, 69, 1.0);
    CHECK (rms (a4, 0.3, 0.9) > 0.05);
    CHECK_NEAR (hzOf (a4, 0.3, 0.9), 440.0, 6.0); // the sine at its root key
    CHECK_NEAR (hzOf (render (h.proc, 81, 1.0), 0.3, 0.9), 880.0, 12.0); // key tracking: an octave up

    // the view draws the real file: another sample gives another picture, no sample another one again
    auto snapshot = [&] { return view.createComponentSnapshot (view.getLocalBounds()); };
    const auto sinePicture = snapshot();
    std::vector<float> clicks (48000, 0.0f);
    for (double at : { 0.0, 0.25, 0.5, 0.75 })
        for (size_t k = 0; k < 2400; ++k)
            clicks[static_cast<size_t> (at * 48000.0) + k] = 0.9f * static_cast<float> (std::sin (0.2 * static_cast<double> (k))) * (1.0f - static_cast<float> (k) / 2400.0f);
    const fs::path clickFile = writeBytes (h.workDir() / "Clicks.wav", encodeWav (clicks.data(), nullptr, clicks.size(), 48000, 16));
    CHECK (view.loadSampleFile (juce::File (clickFile.string())).ok);
    const auto clickPicture = snapshot();
    CHECK (pixelsDiffering (sinePicture, clickPicture) > 500);

    // Auto slices: the markers are the onsets the engine plays
    setParameter (h.proc, "osc1.sample.slice_mode", 1.0f);
    setParameter (h.proc, "osc1.sample.slice_threshold", 80.0f);
    const auto slices = view.autoSliceMarkers();
    CHECK (slices.size() >= 3);
    CHECK_NEAR (slices[0], 0.0, 1.0e-9);
    CHECK (view.autoSliceMarkers().size() >= (setParameter (h.proc, "osc1.sample.slice_threshold", 10.0f), view.autoSliceMarkers().size()));
    const auto autoPicture = snapshot();
    setParameter (h.proc, "osc1.sample.slice_mode", 0.0f);
    CHECK (pixelsDiffering (autoPicture, snapshot()) > 20); // the slice markers are drawn in Auto mode

    // Snap: Off / Zero / Beats / Loop on the marker drags
    view.loadSampleFile (juce::File (wavFile.string()));
    CHECK (view.snapMode() == SnapMode::off);
    CHECK_NEAR (view.snappedMarker (0.2503f, OscWaveView::SampleMarker::start), 0.2503, 1.0e-6);
    view.snapButton().onClick(); // Off -> Zero
    CHECK (view.snapMode() == SnapMode::zero);
    CHECK (view.snapButton().getButtonText().contains ("Zero"));
    const float zero = view.snappedMarker (0.2503f, OscWaveView::SampleMarker::start);
    CHECK (std::abs (view.preview()->valueAt (zero)) < 0.1f);
    view.snapButton().onClick();
    CHECK (view.snapMode() == SnapMode::beats);
    setParameter (h.proc, "osc1.sample.length_beats", 4.0f); // 4 beats over START..END = steps of 25 %
    CHECK_NEAR (view.snappedMarker (0.3f, OscWaveView::SampleMarker::end), 0.25, 1.0e-4);
    view.snapButton().onClick();
    CHECK (view.snapMode() == SnapMode::loop);
    setParameter (h.proc, "osc1.sample.loop_start", 40.0f);
    CHECK_NEAR (view.snappedMarker (0.41f, OscWaveView::SampleMarker::slice), 0.4, 1.0e-4);
    view.snapButton().onClick();
    CHECK (view.snapMode() == SnapMode::off);

    // Trim: the file is cut to START .. END, the markers start again at the ends
    setParameter (h.proc, "osc1.sample.start", 25.0f);
    setParameter (h.proc, "osc1.sample.end", 75.0f);
    const auto trimmed = view.trimSample();
    CHECK (trimmed.ok);
    CHECK_NEAR (h.proc.content().samples[0].frames, 24000, 4);
    CHECK_NEAR (readParameter (h.proc, "osc1.sample.start"), 0.0, 1.0e-6);
    CHECK_NEAR (readParameter (h.proc, "osc1.sample.end"), 100.0, 1.0e-6);
    CHECK (! view.trimSample().ok); // nothing left to trim

    // Load goes through the host: the file request is answered by the test, the Load button runs the flow
    h.proc.setFileRequestHandler ([&] (const FileRequest& request, std::function<void (const juce::File&)> done)
    {
        CHECK (request.wildcards.contains ("flac"));
        done (juce::File (wavFile.string()));
        return true;
    });
    view.loadButton().onClick();
    CHECK_EQ (h.proc.content().samples[0].frames, 48000);

    // a FLAC file is decoded with the JUCE audio formats; the root note F2 (53) comes from its name
    {
        const juce::File flacFile (juce::String ((h.workDir() / "Hit F2.flac").string()));
        flacFile.getParentDirectory().createDirectory();
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::OutputStream> flacStream = flacFile.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer = flac.createWriterFor (flacStream, juce::AudioFormatWriterOptions().withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (16));
        CHECK (writer != nullptr);
        if (writer)
        {
            juce::AudioBuffer<float> buffer (1, 24000);
            for (int i = 0; i < 24000; ++i)
                buffer.setSample (0, i, 0.5f * std::sin (static_cast<float> (2.0 * kPi * 220.0 * i / 48000.0)));
            writer->writeFromAudioSampleBuffer (buffer, 0, 24000);
            writer.reset();
        }
        const auto flacResult = view.loadSampleFile (flacFile);
        CHECK (flacResult.ok);
        CHECK_EQ (static_cast<int> (readParameter (h.proc, "osc1.sample.root")), 53);
        CHECK_EQ (h.proc.content().samples[0].frames, 24000);
        CHECK_EQ (h.proc.content().samples[0].ref.name, std::string ("Hit F2"));
        CHECK (h.proc.content().samples[0].ref.file.find (".wav") != std::string::npos); // stored as a WAV
    }

    // something that is not audio is refused with a reason and changes nothing
    const fs::path junk = writeBytes (h.workDir() / "junk.wav", { 1, 2, 3, 4, 5 });
    const auto before = h.proc.content();
    CHECK (! view.loadSampleFile (juce::File (junk.string())).ok);
    CHECK (h.proc.content() == before);

    CHECK (view.removeSample().ok);
    CHECK (h.proc.content().samples.empty());
    CHECK (view.preview() == nullptr);
    view.setMarkerAnimation (true);
    CHECK (view.markerAnimationOn());
    view.setMarkerAnimation (false);
}

TEST (content_convolve_impulse_response_makes_a_tail)
{
    Host h ("convolve");
    setParameter (h.proc, "convolve.on", 1.0f);
    setParameter (h.proc, "convolve.mix", 100.0f);
    setParameter (h.proc, "env1.release", 5.0f);
    setParameter (h.proc, "env1.sustain", 100.0f);

    // without an impulse response (convolve.ir = None) the note ends with its release: no tail
    warmUp (h.proc);
    const auto bare = render (h.proc, 60, 1.0, 0.15, false);
    CHECK (rms (bare, 0.15, 0.3) > 0.001);
    CHECK (rms (bare, 0.55, 0.9) < 1.0e-3);

    // a 0.6 s decaying noise burst as the impulse response
    std::vector<float> ir (static_cast<size_t> (0.6 * 48000));
    uint32_t seed = 12345;
    for (size_t i = 0; i < ir.size(); ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        ir[i] = (static_cast<float> (seed >> 8) / 8388608.0f - 1.0f) * std::exp (-4.0f * static_cast<float> (i) / static_cast<float> (ir.size()));
    }
    const fs::path file = writeBytes (h.workDir() / "Room.wav", encodeWav (ir.data(), nullptr, ir.size(), 48000, 16));
    const auto loaded = loadImpulseFile (h.ctx, juce::File (file.string()));
    CHECK (loaded.ok);
    CHECK_EQ (h.proc.content().impulses.size(), static_cast<size_t> (1));
    CHECK_EQ (h.proc.content().impulses[0].pack, std::string (kPackImpulses));
    CHECK_EQ (static_cast<int> (readParameter (h.proc, "convolve.ir")), choiceIndexOf ("convolve.ir", "Preset"));

    warmUp (h.proc);
    const auto tail = render (h.proc, 60, 1.0, 0.15, false);
    CHECK (rms (tail, 0.3, 0.6) > 0.005); // the reverb tail of the IR rings on after the key was released
    CHECK (rms (tail, 0.3, 0.6) > 10.0 * rms (bare, 0.3, 0.6) + 1.0e-4);

    // the preview of the panel reads the stored file
    std::vector<float> mono;
    double rate = 0.0;
    CHECK (loadImpulsePreview (h.proc, mono, rate));
    CHECK_EQ (mono.size(), ir.size());

    CHECK (removeImpulse (h.ctx).ok);
    CHECK (h.proc.content().impulses.empty());
    CHECK_EQ (static_cast<int> (readParameter (h.proc, "convolve.ir")), 0);
    warmUp (h.proc);
    CHECK (rms (render (h.proc, 60, 1.0, 0.15, false), 0.55, 0.9) < 1.0e-3);
}

TEST (content_vital_import_inside_the_plugin_with_the_report_sheet)
{
    Host h ("vital");
    std::unique_ptr<juce::AudioProcessorEditor> base (h.proc.createEditor());
    auto* editor = dynamic_cast<SynthyBoyEditor*> (base.get());
    CHECK (editor != nullptr);
    if (editor == nullptr)
        return;
    editor->setSize (390, 844);

    sbfx::Fixture f = sbfx::defaultPatch();
    f.name = "Fixture Pad";
    f.set ("filter_1_cutoff", 70.0);
    f.wavetables = "[" + sbfx::wavetable ("Fixture Table",
        { sbfx::component ("Wave Source", "\"interpolation\":0", { sbfx::waveKeyframe (0, sbfx::sineWave (1)), sbfx::waveKeyframe (255, sbfx::sineWave (5)) }) }) + "]";
    const std::string text = f.json();
    const fs::path file = writeBytes (h.workDir() / "Fixture Pad.vital", std::vector<uint8_t> (text.begin(), text.end()));

    const float cutoffBefore = readParameter (h.proc, "filter1.cutoff");
    const auto result = editor->importVitalFile (juce::File (file.string()));
    CHECK (result.flow.ok);
    CHECK (result.imported.kind == VitalFileKind::preset);
    CHECK (std::abs (readParameter (h.proc, "filter1.cutoff") - cutoffBefore) > 1.0f); // the patch was loaded
    CHECK (! h.proc.content().wavetables.empty());                                       // with its table
    CHECK (MissingFilesModel (h.ctx).empty());                                           // which the library resolves
    CHECK (rms (render (h.proc, 57, 0.5), 0.1, 0.4) > 0.001);
    auto* report = dynamic_cast<ImportReportSheet*> (editor->contentSheet());
    CHECK (report != nullptr);
    if (report != nullptr)
    {
        CHECK (report->text().contains ("settings imported"));
        CHECK (report->text().contains ("Fidelity"));
        report->closeButton().onClick();
        CHECK (editor->contentSheet() == nullptr);
    }

    // a stand-alone table becomes the table of the chosen oscillator
    const std::string table = sbfx::wavetable ("Alone", { sbfx::component ("Wave Source", "\"interpolation\":0", { sbfx::waveKeyframe (0, sbfx::sineWave (1)), sbfx::waveKeyframe (255, sbfx::sineWave (3)) }) });
    const fs::path tableFile = writeBytes (h.workDir() / "Alone.vitaltable", std::vector<uint8_t> (table.begin(), table.end()));
    const auto t = importVitalFile (h.ctx, juce::File (tableFile.string()), 2, 0);
    CHECK (t.flow.ok);
    CHECK_EQ (static_cast<int> (readParameter (h.proc, "osc2.wavetable")), presetIndex ("osc2.wavetable"));
    bool osc2 = false;
    for (const auto& w : h.proc.content().wavetables)
        osc2 = osc2 || (w.osc == 2 && w.frames > 1);
    CHECK (osc2);

    // a stand-alone LFO shape goes into the LFO slot (and the slot is switched to Custom)
    const std::string lfo = sbfx::lineObject ({ { 0.0, 1.0 }, { 0.5, 0.0 }, { 1.0, 1.0 } }, { 0.0, 0.0, 0.0 }, true);
    const fs::path lfoFile = writeBytes (h.workDir() / "Wave.vitallfo", std::vector<uint8_t> (lfo.begin(), lfo.end()));
    const auto l = importVitalFile (h.ctx, juce::File (lfoFile.string()), 0, 2);
    CHECK (l.flow.ok);
    bool lfo2 = false;
    for (const auto& s : h.proc.content().lfoShapes)
        lfo2 = lfo2 || (s.lfo == 2 && s.points.size() == 3);
    CHECK (lfo2);
    CHECK_EQ (static_cast<int> (readParameter (h.proc, "lfo2.shape")), choiceIndexOf ("lfo2.shape", "Custom"));
    CHECK (! importVitalFile (h.ctx, juce::File (lfoFile.string()), 0, 0).flow.ok); // no LFO chosen: nothing changes
    const fs::path junk = writeBytes (h.workDir() / "bad.vital", { 'n', 'o', 'p', 'e' });
    CHECK (! importVitalFile (h.ctx, juce::File (junk.string()), 1, 1).flow.ok);

    // the menu entry asks the host for the file
    bool asked = false;
    h.proc.setFileRequestHandler ([&] (const FileRequest& request, std::function<void (const juce::File&)>)
    {
        asked = request.wildcards.contains ("vital");
        return true;
    });
    editor->importVitalFromMenu();
    CHECK (asked);
}

TEST (content_serum_import_inside_the_plugin_with_the_report_sheet)
{
    Host h ("serum");
    std::unique_ptr<juce::AudioProcessorEditor> base (h.proc.createEditor());
    auto* editor = dynamic_cast<SynthyBoyEditor*> (base.get());
    CHECK (editor != nullptr);
    if (editor == nullptr)
        return;
    editor->setSize (390, 844);
    using namespace sbserum;

    // a preset: patch, table and filter come in, the report sheet shows the numbers
    std::vector<double> table (4096);
    for (size_t i = 0; i < table.size(); ++i)
        table[i] = std::sin (2.0 * kPi * static_cast<double> (i % 2048) / 2048.0 * (i < 2048 ? 1.0 : 3.0));
    Preset p;
    p.set ("Oscillator0", map ({ { "plainParams", plain ({ { "kParamVolume", 0.8 } }) },
                                 { "WTOsc0", map ({ { "embeddedWTData", nums (table) }, { "plainParams", plain ({ { "kParamTablePos", 128 } }) } }) } }));
    p.set ("VoiceFilter0", map ({ { "plainParams", plain ({ { "kParamEnable", 1 }, { "kParamFreq", 0.6 } }) } }));
    p.set ("Env0", map ({ { "plainParams", plain ({ { "kParamAttack", 0.002 }, { "kParamRelease", 0.3 } }) } }));
    const Bytes preset = p.file ("Serum Fixture");
    const fs::path file = writeBytes (h.workDir() / "Serum Fixture.SerumPreset", preset);
    const float cutoffBefore = readParameter (h.proc, "filter1.cutoff");
    const auto result = editor->importSerumFile (juce::File (file.string()));
    CHECK (result.flow.ok);
    CHECK (result.imported.kind == SerumFileKind::preset);
    CHECK (std::abs (readParameter (h.proc, "filter1.cutoff") - cutoffBefore) > 1.0f);
    CHECK (! h.proc.content().wavetables.empty());
    CHECK (MissingFilesModel (h.ctx).empty());
    CHECK (rms (render (h.proc, 57, 0.5), 0.1, 0.4) > 0.001);
    auto* report = dynamic_cast<ImportReportSheet*> (editor->contentSheet());
    CHECK (report != nullptr);
    if (report != nullptr)
    {
        CHECK (report->text().contains ("values imported"));
        CHECK (report->text().contains ("Fidelity"));
        report->closeButton().onClick();
        CHECK (editor->contentSheet() == nullptr);
    }

    // an effect chain is written onto the patch that is loaded now (only the parameters it holds)
    const Bytes fx = container ("SerumFX", map ({ { "FXPhaser", map ({ { "plainParams", plain ({ { "kParamRate", 1.5 }, { "kParamWet", 60 } }) } }) }, { "fxType", text ("FXPhaser") }, { "type", num (2) } }), "Phaser");
    const fs::path fxFile = writeBytes (h.workDir() / "Phaser.SerumFX", fx);
    const float cutoffAfter = readParameter (h.proc, "filter1.cutoff");
    const auto f = synthyboy::ui::importSerumFile (h.ctx, juce::File (fxFile.string()), 0);
    CHECK (f.flow.ok);
    CHECK (f.imported.kind == SerumFileKind::fxModule);
    CHECK_EQ (static_cast<int> (readParameter (h.proc, "fxi01.type")), kFxTypePhaser);
    CHECK (std::abs (readParameter (h.proc, "filter1.cutoff") - cutoffAfter) < 1e-3f); // the patch is otherwise untouched

    // a shape goes into the chosen LFO slot and switches it to Custom
    const Bytes shape = container ("XferShape", map ({ { "LFO", map ({ { "curveVals", nums ({ 0.5, 0.5, 0.5 }) }, { "numPoints", num (2) }, { "xVals", nums ({ 0, 0.5, 1 }) }, { "yVals", nums ({ 1, 0, 1 }) } }) } }), "Tri");
    const fs::path shapeFile = writeBytes (h.workDir() / "Tri.XferShape", shape);
    const auto s = synthyboy::ui::importSerumFile (h.ctx, juce::File (shapeFile.string()), 3);
    CHECK (s.flow.ok);
    bool lfo3 = false;
    for (const auto& sh : h.proc.content().lfoShapes)
        lfo3 = lfo3 || (sh.lfo == 3 && sh.points.size() == 3);
    CHECK (lfo3);
    CHECK_EQ (static_cast<int> (readParameter (h.proc, "lfo3.shape")), choiceIndexOf ("lfo3.shape", "Custom"));
    CHECK (! synthyboy::ui::importSerumFile (h.ctx, juce::File (shapeFile.string()), 0).flow.ok); // no LFO chosen
    const fs::path junk = writeBytes (h.workDir() / "bad.SerumPreset", { 'n', 'o', 'p', 'e' });
    CHECK (! synthyboy::ui::importSerumFile (h.ctx, juce::File (junk.string()), 1).flow.ok);
    // a family without an equivalent is reported, nothing changes
    const Bytes arp = container ("XferArp", map ({ { "ArpClip", map ({ { "name", text ("x") } }) } }), "Arp");
    const fs::path arpFile = writeBytes (h.workDir() / "Arp.XferArp", arp);
    const auto a = synthyboy::ui::importSerumFile (h.ctx, juce::File (arpFile.string()), 1);
    CHECK (a.flow.ok);
    CHECK (a.imported.report.numDropped >= 1);

    // the menu entry asks the host for the file
    bool asked = false;
    h.proc.setFileRequestHandler ([&] (const FileRequest& request, std::function<void (const juce::File&)>)
    {
        asked = request.wildcards.contains ("SerumPreset") && request.purpose == "serum";
        return true;
    });
    editor->importSerumFromMenu();
    CHECK (asked);
}

TEST (content_noise_sample_load_through_the_noise_panel_plays_survives_a_state_round_trip_and_falls_back_when_removed)
{
    Host h ("noise_sample");
    setParameter (h.proc, "osc1.on", 0.0f);
    setParameter (h.proc, "noise.on", 1.0f);
    setParameter (h.proc, "noise.level", 100.0f);
    setParameter (h.proc, "noise.destination", 3.0f); // effects only
    setParameter (h.proc, "noise.keytrack", 0.0f);
    NoisePanel panel (h.ctx);
    panel.build();
    panel.setBounds (0, 0, 390, 700);

    // the generator plays first
    const auto generated = render (h.proc, 57, 0.8);
    CHECK (rms (generated, 0.2, 0.7) > 0.01);
    CHECK (! panel.usesSample());
    CHECK (! panel.removeButton().isEnabled());

    // Load...: the WP-01 flow for the noise source (osc 4); noise.type switches to Sample, the name button shows the file
    const fs::path wavFile = writeBytes (h.workDir() / "Tone.wav", encodeWav (tone (440.0, 1.0).data(), nullptr, 48000, 48000, 16));
    h.proc.setFileRequestHandler ([&] (const FileRequest& request, std::function<void (const juce::File&)> done)
    {
        CHECK (request.purpose == "sample");
        done (juce::File (wavFile.string()));
        return true;
    });
    panel.loadButton().onClick();
    CHECK (panel.status().startsWith ("Loaded Tone"));
    CHECK (panel.usesSample());
    CHECK_EQ (static_cast<int> (readParameter (h.proc, "noise.type")), 1);
    const auto content = h.proc.content();
    CHECK_EQ (content.samples.size(), static_cast<size_t> (1));
    CHECK_EQ (content.samples[0].osc, kNoiseSampleOsc);
    CHECK_EQ (content.samples[0].ref.pack, std::string (kPackSamples));
    CHECK_EQ (panel.sampleNameButton().getButtonText(), juce::String ("Tone"));
    CHECK (panel.removeButton().isEnabled());
    CHECK_EQ (static_cast<int> (readParameter (h.proc, "osc1.type")), 0); // no oscillator was taken for it

    // it plays at its recorded pitch (no key tracking), also on another key; the generator is gone
    const auto sampled = render (h.proc, 57, 1.0);
    CHECK_NEAR (hzOf (sampled, 0.3, 0.9), 440.0, 8.0);
    CHECK_NEAR (hzOf (render (h.proc, 81, 1.0), 0.3, 0.9), 440.0, 8.0);
    CHECK (relDiff (generated, sampled, 0.2, 0.7) > 0.5);

    // the picture is the file: another sample gives another one
    const auto picture = panel.view().createComponentSnapshot (panel.view().getLocalBounds());
    CHECK (panel.view().showsContentSample());

    // a state round trip into another instance (its library lacks the file, the Missing Files flow is the WP-01 one; here the file is found by the sha)
    juce::MemoryBlock state;
    h.proc.getStateInformation (state);
    {
        Host b ("noise_sample_b"); // a new user directory: only the state travels
        b.proc.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        CHECK_EQ (static_cast<int> (readParameter (b.proc, "noise.type")), 1);
        CHECK_EQ (b.proc.content().samples.size(), static_cast<size_t> (1));
        MissingFilesModel model (b.ctx);
        CHECK_EQ (model.items().size(), static_cast<size_t> (1));
        CHECK (model.describe (0).startsWith ("Noise sample"));
        CHECK (model.locate (0, juce::File (wavFile.string())).ok);
        setParameter (b.proc, "osc1.on", 0.0f);
        setParameter (b.proc, "noise.keytrack", 0.0f);
        const auto wave = render (b.proc, 57, 1.0);
        CHECK_NEAR (hzOf (wave, 0.3, 0.9), 440.0, 8.0);
    }

    // stepping through the generators leaves the sample (the file stays in the patch)
    panel.sampleNextButton().onClick();
    CHECK (! panel.usesSample());
    CHECK_EQ (h.proc.content().samples.size(), static_cast<size_t> (1));
    CHECK (relDiff (generated, render (h.proc, 57, 0.8), 0.2, 0.7) < 5.0); // a generator again (not the tone)

    // Remove: the reference is gone; with noise.type = Sample the engine plays the generator (the documented fallback)
    setParameter (h.proc, "noise.type", 1.0f);
    setParameter (h.proc, "noise.sample", 0.0f);
    CHECK (panel.removeSample().ok);
    CHECK (h.proc.content().samples.empty());
    CHECK (! panel.view().showsContentSample());
    CHECK (rms (render (h.proc, 57, 0.8), 0.2, 0.7) > 0.01);
    CHECK (hzOf (render (h.proc, 57, 1.0), 0.3, 0.9) > 3000.0); // noise, not the tone
}

TEST (content_missing_files_dialog_logic_locate_remove_and_hash_check)
{
    Host a ("missing_a");
    CHECK (saveWavetable (a.ctx, 2, testTable ("Pad Table", 4), false).ok);
    const fs::path sampleFile = writeBytes (a.workDir() / "Hit C3.wav", encodeWav (tone (261.6, 0.5).data(), nullptr, 24000, 48000, 16));
    CHECK (loadSampleFile (a.ctx, 3, juce::File (sampleFile.string())).ok);
    CHECK (loadImpulseFile (a.ctx, juce::File (sampleFile.string())).ok);
    juce::MemoryBlock state;
    a.proc.getStateInformation (state);
    const auto original = a.proc.content();
    const fs::path tableOnDisk = fs::path (a.proc.contentLibrary()->rootDirectory()) / "wavetables" / original.wavetables[0].ref.file;

    // a new instance elsewhere: three references are missing; loading the state raises the revision and the editor opens the dialog
    Host b ("missing_b");
    std::unique_ptr<juce::AudioProcessorEditor> base (b.proc.createEditor());
    auto* editor = dynamic_cast<SynthyBoyEditor*> (base.get());
    editor->setSize (390, 844);
    const auto revision = b.proc.contentRevision();
    b.proc.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (b.proc.contentRevision() > revision);
    MissingFilesModel model (b.ctx);
    CHECK_EQ (model.items().size(), static_cast<size_t> (3));
    CHECK (model.describe (0).contains ("not found"));
    CHECK (editor->checkMissingContent());
    auto* sheet = dynamic_cast<MissingFilesSheet*> (editor->contentSheet());
    CHECK (sheet != nullptr);
    if (sheet == nullptr)
        return;
    CHECK_EQ (sheet->rowCount(), 3);

    // Locate with a file that is not the one of the preset: refused (the SHA-256 differs); the right one is taken
    const fs::path wrong = writeBytes (b.workDir() / "wrong.wav", encodeWav (tone (100.0, 0.5).data(), nullptr, 24000, 48000, 16));
    std::size_t sampleRow = 0;
    for (std::size_t i = 0; i < model.items().size(); ++i)
        if (model.items()[i].kind == ContentKind::sample)
            sampleRow = i;
    CHECK (! model.locate (sampleRow, juce::File (wrong.string())).ok);
    CHECK (model.locate (sampleRow, juce::File (sampleFile.string())).ok);
    CHECK_EQ (model.items().size(), static_cast<size_t> (2));
    CHECK (b.proc.content().samples[0].ref.sha256 == original.samples[0].ref.sha256);

    // the dialog buttons go through the host's file request
    sheet->rebuild();
    CHECK_EQ (sheet->rowCount(), 2);
    b.proc.setFileRequestHandler ([&] (const FileRequest&, std::function<void (const juce::File&)> done)
    {
        done (juce::File (tableOnDisk.string()));
        return true;
    });
    int tableRow = -1;
    for (int i = 0; i < sheet->rowCount(); ++i)
        if (sheet->model().items()[static_cast<size_t> (i)].kind == ContentKind::wavetable)
            tableRow = i;
    CHECK (tableRow >= 0);
    sheet->locateButton (tableRow).onClick();
    CHECK_EQ (sheet->rowCount(), 1);
    CHECK (b.proc.content().wavetables[0].ref.sha256 == original.wavetables[0].ref.sha256);

    // Remove drops the reference; the sound falls back and the dialog is done
    sheet->removeButton (0).onClick();
    CHECK (b.proc.content().impulses.empty());
    CHECK (MissingFilesModel (b.ctx).empty());
    CHECK (editor->contentSheet() == nullptr);

    // a changed file under a known name is a hash mismatch, not a missing one
    {
        std::fstream f (tableOnDisk, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp (100);
        f.put (static_cast<char> (0x77));
    }
    MissingFilesModel changed (a.ctx);
    CHECK_EQ (changed.items().size(), static_cast<size_t> (1));
    CHECK (changed.items()[0].reason == MissingReason::hashMismatch);
    CHECK (changed.describe (0).contains ("changed"));
    CHECK (changed.removeAll().ok);
    CHECK (a.proc.content().wavetables.empty());
}

TEST (content_user_library_service_lfo_shapes_formulas_and_fx_racks)
{
    Host h ("library");
    CHECK (h.proc.userLibrary() != nullptr);

    // LFO shapes: save the current shape, change it, load the saved one; favourites; a .lfoshape.json and a .vitallfo as files
    {
        LfoShapeEditor editor (h.ctx, 1);
        editor.setSize (400, 220);
        editor.setActive (true);
        const size_t points = editor.shape().points.size();
        CHECK (editor.addPointAt (0.3f, 0.8f));
        CHECK_EQ (editor.shape().points.size(), points + 1);
        LibraryMenu& menu = editor.libraryMenu();
        CHECK (menu.saveCurrent ("Wobble"));
        CHECK (menu.saveCurrent ("Other"));
        editor.applyPreset (0);
        CHECK (editor.shape().points.size() != points + 1);
        CHECK (menu.load ("Wobble"));
        CHECK_EQ (editor.shape().points.size(), points + 1);
        CHECK (menu.setFavourite ("Other", true));
        CHECK_EQ (menu.entries()[0].name, std::string ("Other"));
        CHECK (menu.remove ("Other"));
        CHECK_EQ (menu.entries().size(), static_cast<size_t> (1));
        CHECK (! menu.load ("Missing one"));

        LfoShapeFile file;
        file.name = "From file";
        file.points = { ContentPoint (0.0f, 0.0f), ContentPoint (0.5f, 1.0f), ContentPoint (1.0f, 0.0f) };
        const fs::path shapeFile = writeBytes (h.workDir() / "From file.lfoshape.json", [&] { const auto s = serializeLfoShapeFile (file); return std::vector<uint8_t> (s.begin(), s.end()); }());
        CHECK (editor.applyShapeFromFile (juce::File (shapeFile.string())));
        CHECK_EQ (editor.shape().points.size(), static_cast<size_t> (3));
        const std::string vital = sbfx::lineObject ({ { 0.0, 1.0 }, { 0.25, 0.0 }, { 0.5, 1.0 }, { 1.0, 0.0 } }, { 0.0, 0.0, 0.0, 0.0 }, false);
        const fs::path vitalFile = writeBytes (h.workDir() / "Pulse.vitallfo", std::vector<uint8_t> (vital.begin(), vital.end()));
        CHECK (editor.applyShapeFromFile (juce::File (vitalFile.string())));
        CHECK_EQ (editor.shape().points.size(), static_cast<size_t> (4));
        CHECK (! editor.applyShapeFromFile (juce::File (junkPath (h).string())));
    }

    // FX rack presets: the chain is saved, changed and restored
    {
        FxRackView view (h.ctx);
        view.setSize (400, 300);
        setParameter (h.proc, "reverb.on", 1.0f);
        setParameter (h.proc, "reverb.mix", 63.0f);
        setParameter (h.proc, "chorus.on", 1.0f);
        CHECK (view.rackPresets().saveCurrent ("Space"));
        CHECK (h.proc.userLibrary()->exists (kLibFxRacks, "Space"));
        setParameter (h.proc, "reverb.on", 0.0f);
        setParameter (h.proc, "reverb.mix", 10.0f);
        setParameter (h.proc, "chorus.on", 0.0f);
        const float volume = readParameter (h.proc, "master.volume");
        CHECK (view.rackPresets().load ("Space"));
        CHECK_NEAR (readParameter (h.proc, "reverb.on"), 1.0, 1.0e-6);
        CHECK_NEAR (readParameter (h.proc, "reverb.mix"), 63.0, 0.05);
        CHECK_NEAR (readParameter (h.proc, "chorus.on"), 1.0, 1.0e-6);
        CHECK_NEAR (readParameter (h.proc, "master.volume"), volume, 1.0e-6); // only the chain is part of a rack preset
        std::string json;
        CHECK (h.proc.userLibrary()->load (kLibFxRacks, "Space", json));
        CHECK (json.find ("synthyboy-fx-rack") != std::string::npos);
    }

    // formula presets and own formulas in the wavetable editor
    {
        WavetableEditorSheet sheet (h.ctx);
        sheet.setBounds (0, 0, 390, 844);
        CHECK (sheet.applyFormulaItem (serializeFormulaItem ("Saw", "1 - 2*w")));
        LibraryMenu* menu = sheet.formulaLibraryMenu();
        CHECK (menu != nullptr);
        if (menu == nullptr)
            return;
        CHECK (menu->saveCurrent ("My saw"));
        CHECK (sheet.applyFormulaItem (serializeFormulaItem ("Sine", "sin(2*pi*x)")));
        CHECK (menu->load ("My saw"));
        std::string name, expression;
        std::string json;
        CHECK (h.proc.userLibrary()->load (kLibFormulas, "My saw", json));
        CHECK (parseFormulaItem (json, name, expression));
        CHECK_EQ (expression, std::string ("1 - 2*w"));
        CHECK (! sheet.applyFormulaItem ("{\"format\":\"other\"}"));
        CHECK (menu->setFavourite ("My saw", true));
        CHECK (menu->entries()[0].favourite);
        // the menu is built from the same entries
        juce::PopupMenu popup;
        menu->addTo (popup, 1000);
        CHECK (popup.getNumItems() >= 3);
        CHECK (menu->remove ("My saw"));
    }

    // a library item of another host category survives: the voice control category exists for WP-07
    CHECK (h.proc.userLibrary()->save (kLibVoiceControl, "Steps", "{\"format\":\"synthyboy-voice-control\"}"));
    CHECK_EQ (h.proc.userLibrary()->list (kLibVoiceControl).size(), static_cast<size_t> (1));
}

TEST (content_wavetable_editor_imports_real_files_saves_and_exports)
{
    Host h ("editor");
    WavetableEditorSheet sheet (h.ctx);
    sheet.setBounds (0, 0, 390, 844);
    sheet.setTargetOsc (3);
    CHECK_EQ (sheet.targetOsc(), 3);

    // wavetable file: a WAV of 3 cycles of 2048 samples
    std::vector<float> frames (2048 * 3);
    for (size_t i = 0; i < frames.size(); ++i)
        frames[i] = 0.5f * static_cast<float> (std::sin (2.0 * kPi * static_cast<double> ((i / 2048) + 1) * static_cast<double> (i % 2048) / 2048.0));
    const fs::path tableFile = writeBytes (h.workDir() / "Three.wav", encodeWav (frames.data(), nullptr, frames.size(), 44100, 16));
    CHECK (sheet.importFile (juce::File (tableFile.string()), WavetableEditorSheet::ImportKind::wavetable));
    CHECK_EQ (sheet.wavetable().numFrames(), static_cast<size_t> (3));
    CHECK (sheet.modified());

    // image file: 40 columns = 40 frames
    juce::Image image (juce::Image::RGB, 40, 24, true);
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 40; ++x)
            image.setPixelAt (x, y, juce::Colour::fromRGB (static_cast<juce::uint8> (x * 6), static_cast<juce::uint8> (y * 10), 120));
    const juce::File imageFile (juce::String ((h.workDir() / "grad.png").string()));
    CHECK (writePng (image, imageFile));
    CHECK (sheet.importFile (imageFile, WavetableEditorSheet::ImportKind::image));
    CHECK_EQ (sheet.wavetable().numFrames(), static_cast<size_t> (40));

    // audio file (multi cycle import on the worker thread)
    const fs::path audioFile = writeBytes (h.workDir() / "Voice.wav", encodeWav (tone (220.0, 1.0).data(), nullptr, 48000, 48000, 16));
    sheet.chooseImportFile (juce::File (audioFile.string()), WavetableEditorSheet::ImportKind::audio);
    sheet.pressImport();
    CHECK (sheet.importRunning());
    for (int i = 0; i < 400 && ! sheet.importWorkerDone(); ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    CHECK (sheet.finishImport());
    CHECK (sheet.wavetable().numFrames() >= 1);
    const size_t frameCount = sheet.wavetable().numFrames();
    CHECK (! sheet.importFile (juce::File (junkPathIn (h).string()), WavetableEditorSheet::ImportKind::audio));
    CHECK (sheet.statusText().contains ("Import failed"));

    // Save Table writes the library file and attaches it to the target oscillator
    bool savedCalled = false;
    sheet.onSaved = [&] { savedCalled = true; };
    const auto saved = sheet.saveToLibrary (false);
    CHECK (saved.ok);
    CHECK (savedCalled);
    CHECK (! sheet.modified());
    CHECK_EQ (h.proc.content().wavetables.size(), static_cast<size_t> (1));
    CHECK_EQ (h.proc.content().wavetables[0].osc, 3);
    CHECK_EQ (h.proc.content().wavetables[0].frames, static_cast<int> (frameCount));
    CHECK_EQ (static_cast<int> (readParameter (h.proc, "osc3.wavetable")), presetIndex ("osc3.wavetable"));

    // the editor opens on the stored file; Embed in Preset moves the reference to the embedded pack
    std::unique_ptr<WavetableEditorSheet> holder;
    auto& reopened = WavetableEditorSheet::open (h.ctx, 3, *h.proc.createEditor(), holder, wtedit::Wavetable ("Built-in"), {}, {});
    CHECK_EQ (reopened.wavetable().numFrames(), frameCount);
    CHECK (reopened.saveToLibrary (true).ok);
    CHECK_EQ (h.proc.content().wavetables[0].ref.pack, std::string (kPackEmbedded));

    // Export asks the host for a file and writes the WAV (16-bit PCM, 32-bit float and single cycle)
    const juce::File out16 (juce::String ((h.workDir() / "out16.wav").string()));
    h.proc.setFileRequestHandler ([&] (const FileRequest& request, std::function<void (const juce::File&)> done)
    {
        CHECK (request.mode == FileRequest::Mode::save);
        done (out16);
        return true;
    });
    sheet.promptExport (wtedit::ExportFormat::Wav16Bit);
    CHECK (out16.existsAsFile());
    wtedit::Wavetable back;
    CHECK (wavetableFromWav (readFileBytes (out16), "out", back));
    CHECK_EQ (back.numFrames(), frameCount);
    CHECK_NEAR (back.getFrame (0)[300], sheet.wavetable().getFrame (0)[300], 2.0e-3);
    const juce::File outF32 (juce::String ((h.workDir() / "out32.wav").string()));
    CHECK (sheet.exportToFile (wtedit::ExportFormat::Wav32BitFloat, outF32));
    CHECK (sheet.exportToFile (wtedit::ExportFormat::SingleCycle, juce::File (juce::String ((h.workDir() / "cycle.wav").string()))));
    wtedit::Wavetable cycle;
    CHECK (wavetableFromWav (readFileBytes (juce::File (juce::String ((h.workDir() / "cycle.wav").string()))), "c", cycle));
    CHECK_EQ (cycle.numFrames(), static_cast<size_t> (1));
}

// GROUPA2 (WP-07): the voice control panel - the step grid, the sequence length, Init, the user library and a .SerumStyle file (phone and tablet)
TEST (content_voice_control_panel_steps_length_init_presets_and_style_file)
{
    struct Size { int w; bool twoLines; };
    for (const Size sz : { Size { 390, true }, Size { 1024, false } })
    {
        Host h ("voicectl");
        GlbVoiceControlPanel panel (h.ctx);
        panel.build();
        panel.setBounds (0, 0, sz.w, panel.preferredHeight (sz.w));
        CHECK_EQ (panel.controls().size(), size_t { 62 });
        CHECK_EQ (panel.keys().size(), size_t { 62 });
        const int touch = h.theme.touch();

        // one row of eight cells is shown, every target is a 44 point target and inside the panel; a phone folds the grid into two lines of four
        const auto check = [&] (int row)
        {
            for (int s = 0; s < 8; ++s)
                for (int r = 0; r < 6; ++r)
                    CHECK_EQ (panel.cell (s, r)->isVisible(), r == row);
            for (int s = 0; s < 8; ++s)
            {
                const auto* c = panel.cell (s, row);
                CHECK (c->getWidth() >= touch && c->getHeight() >= touch);
                CHECK (c->getRight() <= panel.getWidth() && c->getX() >= 0);
                CHECK (panel.numberButton (s).getWidth() >= touch && panel.numberButton (s).getHeight() >= touch);
                CHECK_EQ (panel.numberButton (s).getX(), c->getX());
            }
            CHECK_EQ (panel.cell (4, row)->getY() > panel.cell (0, row)->getY(), sz.twoLines);
        };
        check (0);
        for (int r = 0; r < 6; ++r)
        {
            CHECK (panel.rowButton (r).getWidth() >= touch && panel.rowButton (r).getHeight() >= touch);
            panel.rowButton (r).onClick();
            CHECK_EQ (panel.selectedRow(), r);
            CHECK (panel.rowButton (r).getToggleState());
            check (r);
        }
        CHECK (panel.initButtonRef().getHeight() >= touch && panel.presetsButtonRef().getHeight() >= touch);

        // a drag on a cell is a gesture on its key; a double tap would reset it
        panel.selectRow (1);
        CHECK_NEAR (readParameter (h.proc, "voicectl.s2.detune"), 0.0, 1.0e-4);
        panel.cell (1, 1)->nudgeNormalised (0.25f);
        CHECK (readParameter (h.proc, "voicectl.s2.detune") > 20.0f);
        panel.cell (3, 4)->nudgeNormalised (-0.2f);
        CHECK (readParameter (h.proc, "voicectl.s4.mod1") < -10.0f);

        // the numbers are the sequence length
        CHECK_EQ (panel.sequenceLength(), 8);
        panel.numberButton (2).onClick();
        CHECK_EQ (static_cast<int> (readParameter (h.proc, "voicectl.length") + 0.5f), 3);
        CHECK_EQ (panel.sequenceLength(), 3);
        CHECK (panel.numberButton (1).getToggleState() && ! panel.numberButton (5).getToggleState());
        setParameter (h.proc, "voicectl.length", 6.0f); // from outside (automation, a preset)
        CHECK_EQ (panel.sequenceLength(), 6);
        CHECK (panel.numberButton (5).getToggleState() && ! panel.numberButton (6).getToggleState());

        // the user library: save, change, load
        setParameter (h.proc, "voicectl.rand.pan", 40.0f);
        CHECK (panel.presets().saveCurrent ("Mine"));
        setParameter (h.proc, "voicectl.rand.pan", 0.0f);
        setParameter (h.proc, "voicectl.s1.pan", 77.0f);
        CHECK (panel.presets().load ("Mine"));
        CHECK_NEAR (readParameter (h.proc, "voicectl.rand.pan"), 40.0, 1.0e-3);
        CHECK_NEAR (readParameter (h.proc, "voicectl.s1.pan"), 0.0, 1.0e-3);         // a saved configuration is complete: the step it did not change is the default
        CHECK (readParameter (h.proc, "voicectl.s2.detune") > 20.0f);
        CHECK (! panel.presets().load ("Nothing"));
        juce::PopupMenu menu;
        panel.presets().addTo (menu, 1000);
        CHECK (menu.getNumItems() >= 3);

        // Init: every key is the default, the cells and the buttons follow
        panel.initialise();
        for (size_t i = 0; i < paramCount(); ++i)
            if (isVoiceControlKey (paramTable()[i].key))
                CHECK_NEAR (readParameter (h.proc, paramTable()[i].key), paramTable()[i].def, 1.0e-4);
        CHECK_EQ (panel.sequenceLength(), 8);
        CHECK (panel.numberButton (7).getToggleState());

        // a .SerumStyle file ("Presets" -> Load from file...): the whole panel is written
        const fs::path style = writeBytes (h.workDir() / "2 Voice Pan.SerumStyle",
            sbserum::container ("SerumStyle", sbserum::map ({ { "VoicePanel0", sbserum::map ({ { "plainParams",
                sbserum::plain ({ { "kParamVoiceCount", 2 }, { "kParamVoice1Pan", 50 }, { "kParamVoice2Pan", -50 } }) } }) } }), "2 Voice Pan"));
        const SerumFlowResult styled = importSerumFile (h.ctx, juce::File (style.string()), 0);
        CHECK (styled.flow.ok);
        CHECK_EQ (static_cast<int> (readParameter (h.proc, "voicectl.length") + 0.5f), 2);
        CHECK_NEAR (readParameter (h.proc, "voicectl.s1.pan"), 50.0, 1.0e-3);
        CHECK_NEAR (readParameter (h.proc, "voicectl.s2.pan"), -50.0, 1.0e-3);
        CHECK_EQ (panel.sequenceLength(), 2);
        CHECK (styled.imported.kind == SerumFileKind::style);
        // the patch around the panel was not touched
        CHECK_NEAR (readParameter (h.proc, "osc1.level"), 100.0, 1.0e-3);

        // More: the random amounts and the oscillator mask are controls, the layout keeps inside the panel
        panel.setMoreExpanded (true);
        panel.setBounds (0, 0, sz.w, panel.preferredHeight (sz.w));
        for (const auto& c : panel.controls())
            if (c->isVisible())
                CHECK (c->getRight() <= panel.getWidth() && c->getBottom() <= panel.getHeight());
        CHECK_EQ (panel.cell (0, panel.selectedRow())->isVisible(), true);
    }
}
