// SynthyBoy - UI: the content pipeline flows (ContentIO.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ContentIO.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "synthyboy/params.h"
#include "synthyboy/sampleosc/wav_reader.h"

namespace synthyboy::ui
{
namespace
{
std::string utf8 (const juce::String& s) { return std::string (s.toRawUTF8()); }

FlowResult fail (const juce::String& message) { return { false, message }; }
FlowResult done (const juce::String& message) { return { true, message }; }

bool isWavBytes (const std::vector<uint8_t>& b)
{
    return b.size() > 12 && std::memcmp (b.data(), "RIFF", 4) == 0 && std::memcmp (b.data() + 8, "WAVE", 4) == 0;
}

bool commit (UiContext& ctx, const StateContent& content)
{
    return ctx.host.setContent (content);
}
} // namespace

juce::String audioFileWildcard()
{
    return "*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.caf";
}

bool decodeAudioFile (const juce::File& file, DecodedAudio& out, juce::String* error)
{
    const auto setError = [&] (const char* text) { if (error != nullptr) *error = text; return false; };
    if (! file.existsAsFile())
        return setError ("the file does not exist");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return setError ("not an audio file this build can read");
    if (reader->lengthInSamples <= 0 || reader->lengthInSamples > 64 * 1024 * 1024 || reader->sampleRate < 8000.0 || reader->sampleRate > 384000.0)
        return setError ("the audio is empty or too long");
    const int channels = static_cast<int> (std::min<unsigned int> (reader->numChannels, 2u));
    const int n = static_cast<int> (reader->lengthInSamples);
    juce::AudioBuffer<float> buffer (channels, n);
    if (! reader->read (&buffer, 0, n, 0, true, channels > 1))
        return setError ("the audio could not be decoded");
    out.left.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + n);
    out.right.clear();
    if (channels > 1)
        out.right.assign (buffer.getReadPointer (1), buffer.getReadPointer (1) + n);
    out.sampleRate = reader->sampleRate;
    out.name = file.getFileNameWithoutExtension();
    return true;
}

std::vector<uint8_t> readFileBytes (const juce::File& file)
{
    if (! file.existsAsFile() || static_cast<juce::int64> (file.getSize()) > static_cast<juce::int64> (kMaxContentFileBytes))
        return {};
    juce::MemoryBlock block;
    if (! file.loadFileAsData (block))
        return {};
    const auto* p = static_cast<const uint8_t*> (block.getData());
    return std::vector<uint8_t> (p, p + block.getSize());
}

bool writeFileBytes (const juce::File& file, const std::vector<uint8_t>& bytes)
{
    if (bytes.empty())
        return false;
    file.getParentDirectory().createDirectory();
    return file.replaceWithData (bytes.data(), bytes.size());
}

bool setParameter (UiHost& host, const char* key, float plain)
{
    auto* parameter = host.parameters().getParameter (key);
    if (parameter == nullptr)
        return false;
    const float normalised = parameter->convertTo0to1 (plain);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (normalised);
    parameter->endChangeGesture();
    return true;
}

float readParameter (UiHost& host, const char* key, float fallback)
{
    if (auto* raw = host.parameters().getRawParameterValue (key))
        return raw->load();
    return fallback;
}

int choiceIndexOf (const char* key, const char* name)
{
    const ParamInfo* info = findParamByKey (key);
    if (info == nullptr || info->choices == nullptr)
        return -1;
    const juce::String wanted (name);
    for (int i = 0; i < static_cast<int> (info->numChoices); ++i)
        if (juce::String (info->choices[i]).startsWithIgnoreCase (wanted))
            return i;
    return -1;
}

const ContentSample* sampleOf (const StateContent& content, int osc) noexcept
{
    for (const auto& s : content.samples)
        if (s.osc == osc)
            return &s;
    return nullptr;
}

const ContentWavetable* wavetableOf (const StateContent& content, int osc) noexcept
{
    for (const auto& w : content.wavetables)
        if (w.osc == osc)
            return &w;
    return nullptr;
}

// ---- samples ---------------------------------------------------------------------------------------------------------------------------

FlowResult loadSampleFile (UiContext& ctx, int osc, const juce::File& file)
{
    auto* library = ctx.host.contentLibrary();
    if (library == nullptr)
        return fail ("this host keeps no content library");
    if (osc < 1 || osc > kMaxContentSamples)
        return fail ("no such oscillator");

    std::vector<uint8_t> wav = readFileBytes (file);
    SampleFacts probe;
    if (! isWavBytes (wav) || ! probeSampleWav (wav, utf8 (file.getFileName()), probe))
    {
        // any other format (or a WAV the engine cannot read directly): decoded and stored as a 24-bit WAV
        DecodedAudio audio;
        juce::String error;
        if (! decodeAudioFile (file, audio, &error))
            return fail (error);
        wav = encodeWav (audio.left.data(), audio.right.empty() ? nullptr : audio.right.data(), audio.left.size(), juce::roundToInt (audio.sampleRate), 24);
        if (wav.empty() || ! probeSampleWav (wav, utf8 (file.getFileName()), probe))
            return fail ("the audio could not be converted");
    }

    ContentFileRef ref;
    if (! library->addFile (ContentKind::sample, utf8 (file.getFileName()), wav, ref))
        return fail ("the sample could not be stored in the library");

    auto content = ctx.host.content();
    ParamValues rootOnly = ParamValues::defaults(); // only the root note is taken from it
    if (! attachStoredSample (content, &rootOnly, osc, ref, *library))
        return fail ("the stored sample cannot be read back");
    if (! commit (ctx, content))
        return fail ("the content was refused");
    juce::String msg = "Loaded " + file.getFileName();
    if (osc == kNoiseSampleOsc)
    {
        // the noise source (GROUPA1, WP-04): it plays the file at its recorded pitch (no root note, no region keys); the choice noise.type = Sample selects it
        const int sampleType = choiceIndexOf ("noise.type", "Sample");
        if (sampleType >= 0)
            setParameter (ctx.host, "noise.type", static_cast<float> (sampleType));
    }
    else
    {
        const std::string o = "osc" + std::to_string (osc);
        if (probe.rootNote >= 0)
            setParameter (ctx.host, (o + ".sample.root").c_str(), static_cast<float> (probe.rootNote));
        const int sampleType = choiceIndexOf ((o + ".type").c_str(), "Sample");
        if (sampleType >= 0)
            setParameter (ctx.host, (o + ".type").c_str(), static_cast<float> (sampleType));
        setParameter (ctx.host, (o + ".sample.start").c_str(), 0.0f);
        setParameter (ctx.host, (o + ".sample.end").c_str(), 100.0f);
        if (probe.rootNote >= 0)
            msg << ", root note " << probe.rootNote << (probe.rootFromName ? " (from the file name)" : " (from the file)");
    }
    contentCache (ctx).clear();
    return done (msg);
}

std::vector<uint8_t> storedSampleBytes (UiHost& host, int osc)
{
    std::vector<uint8_t> bytes;
    const auto content = host.content();
    if (const auto* s = sampleOf (content, osc))
        if (auto* library = host.contentLibrary())
            library->readFile (s->ref, bytes);
    return bytes;
}

FlowResult trimSample (UiContext& ctx, int osc)
{
    auto* library = ctx.host.contentLibrary();
    auto content = ctx.host.content();
    const ContentSample* sample = sampleOf (content, osc);
    if (library == nullptr || sample == nullptr)
        return fail ("no sample is loaded in this oscillator");
    std::vector<uint8_t> bytes;
    if (! library->readFile (sample->ref, bytes))
        return fail ("the sample file is missing");

    const std::string o = "osc" + std::to_string (osc);
    const float start = readParameter (ctx.host, (o + ".sample.start").c_str(), 0.0f) * 0.01f;
    const float end = readParameter (ctx.host, (o + ".sample.end").c_str(), 100.0f) * 0.01f;
    if (end - start < 0.0005f || (start <= 0.0f && end >= 1.0f))
        return fail ("START and END already cover the whole sample");
    const std::vector<uint8_t> trimmed = trimSampleWav (bytes, start, end);
    if (trimmed.empty())
        return fail ("the region is too short to keep");

    ContentFileRef ref;
    if (! library->addFile (ContentKind::sample, sample->ref.name + " trimmed", trimmed, ref))
        return fail ("the trimmed sample could not be stored");
    // the slices inside the region move with it, the others are dropped
    std::vector<float> slices;
    for (float s : sample->slices)
        if (s >= start && s < end)
            slices.push_back ((s - start) / (end - start));
    const auto old = *sample;
    detachSample (content, osc);
    if (! attachStoredSample (content, nullptr, osc, ref, *library))
        return fail ("the trimmed sample cannot be read back");
    for (auto& s : content.samples)
        if (s.osc == osc)
            s.slices = slices;
    if (! commit (ctx, content))
        return fail ("the content was refused");
    // START / END / the loop markers are fractions of the file: they restart at the ends, the loop keeps its position inside the region
    const auto rescale = [&] (const char* key)
    {
        const float v = readParameter (ctx.host, (o + key).c_str(), 0.0f) * 0.01f;
        setParameter (ctx.host, (o + key).c_str(), 100.0f * juce::jlimit (0.0f, 1.0f, (v - start) / (end - start)));
    };
    rescale (".sample.loop_start");
    rescale (".sample.loop_end");
    setParameter (ctx.host, (o + ".sample.start").c_str(), 0.0f);
    setParameter (ctx.host, (o + ".sample.end").c_str(), 100.0f);
    contentCache (ctx).clear();
    (void) old;
    return done ("Trimmed to " + juce::String (juce::roundToInt ((end - start) * static_cast<float> (sample->frames))) + " frames");
}

FlowResult removeSample (UiContext& ctx, int osc)
{
    auto content = ctx.host.content();
    if (sampleOf (content, osc) == nullptr)
        return fail ("no sample is loaded in this oscillator");
    detachSample (content, osc);
    if (! commit (ctx, content))
        return fail ("the content was refused");
    contentCache (ctx).clear();
    return done ("Sample removed");
}

// ---- wavetables ------------------------------------------------------------------------------------------------------------------------

FlowResult saveWavetable (UiContext& ctx, int osc, const wtedit::Wavetable& table, bool embed)
{
    auto* library = ctx.host.contentLibrary();
    if (library == nullptr)
        return fail ("this host keeps no content library");
    if (osc < 1 || osc > kMaxContentWavetables)
        return fail ("no such oscillator");
    const std::vector<uint8_t> wav = wavetableToWav (table);
    if (wav.empty())
        return fail ("the table has no frames");
    ContentFileRef ref;
    const std::string name = table.name().empty() ? std::string ("Table") : table.name();
    if (! library->addFile (ContentKind::wavetable, name, wav, ref, embed))
        return fail ("the table could not be stored in the library");
    auto content = ctx.host.content();
    attachWavetable (content, osc, ref, table.author(), static_cast<int> (table.numFrames()));
    if (! commit (ctx, content))
        return fail ("the content was refused");
    const std::string key = "osc" + std::to_string (osc) + ".wavetable";
    const int preset = choiceIndexOf (key.c_str(), "Preset");
    if (preset >= 0)
        setParameter (ctx.host, key.c_str(), static_cast<float> (preset));
    contentCache (ctx).clear();
    return done (embed ? "Embedded in the preset: " + juce::String (name) : "Saved: " + juce::String (name));
}

bool loadWavetableOf (UiHost& host, int osc, wtedit::Wavetable& out)
{
    const auto content = host.content();
    const ContentWavetable* w = wavetableOf (content, osc);
    auto* library = host.contentLibrary();
    std::vector<uint8_t> bytes;
    if (w == nullptr || library == nullptr || ! library->readFile (w->ref, bytes))
        return false;
    return wavetableFromWav (bytes, w->ref.name, out);
}

// ---- impulse response ------------------------------------------------------------------------------------------------------------------

FlowResult loadImpulseFile (UiContext& ctx, const juce::File& file)
{
    auto* library = ctx.host.contentLibrary();
    if (library == nullptr)
        return fail ("this host keeps no content library");
    std::vector<uint8_t> wav = readFileBytes (file);
    SampleFacts probe;
    if (! isWavBytes (wav) || ! probeSampleWav (wav, utf8 (file.getFileName()), probe))
    {
        DecodedAudio audio;
        juce::String error;
        if (! decodeAudioFile (file, audio, &error))
            return fail (error);
        wav = encodeWav (audio.left.data(), audio.right.empty() ? nullptr : audio.right.data(), audio.left.size(), juce::roundToInt (audio.sampleRate), 24);
        if (wav.empty())
            return fail ("the audio could not be converted");
    }
    ContentFileRef ref;
    if (! library->addFile (ContentKind::impulse, utf8 (file.getFileName()), wav, ref))
        return fail ("the impulse response could not be stored in the library");
    auto content = ctx.host.content();
    attachImpulse (content, ref);
    if (! commit (ctx, content))
        return fail ("the content was refused");
    const int preset = choiceIndexOf ("convolve.ir", "Preset");
    if (preset >= 0)
        setParameter (ctx.host, "convolve.ir", static_cast<float> (preset));
    contentCache (ctx).clear();
    return done ("Loaded " + file.getFileName());
}

FlowResult removeImpulse (UiContext& ctx)
{
    auto content = ctx.host.content();
    if (content.impulses.empty())
        return fail ("no impulse response is loaded");
    detachImpulse (content);
    if (! commit (ctx, content))
        return fail ("the content was refused");
    setParameter (ctx.host, "convolve.ir", 0.0f);
    contentCache (ctx).clear();
    return done ("Impulse response removed");
}

bool loadImpulsePreview (UiHost& host, std::vector<float>& mono, double& sampleRate)
{
    const auto content = host.content();
    auto* library = host.contentLibrary();
    std::vector<uint8_t> bytes;
    if (content.impulses.empty() || library == nullptr || ! library->readFile (content.impulses[0], bytes))
        return false;
    std::vector<float> left, right;
    sampleosc::WavInfo info;
    if (! sampleosc::readWav (bytes.data(), bytes.size(), left, right, info) || left.empty())
        return false;
    mono.resize (left.size());
    for (size_t i = 0; i < left.size(); ++i)
        mono[i] = info.numChannels >= 2 ? 0.5f * (left[i] + right[i]) : left[i];
    sampleRate = info.sampleRate;
    return true;
}

// ---- Vital -----------------------------------------------------------------------------------------------------------------------------

VitalFlowResult importVitalFile (UiContext& ctx, const juce::File& file, int osc, int lfo)
{
    VitalFlowResult result;
    auto* library = ctx.host.contentLibrary();
    if (library == nullptr)
    {
        result.flow = fail ("this host keeps no content library");
        return result;
    }
    const std::vector<uint8_t> bytes = readFileBytes (file);
    if (bytes.empty())
    {
        result.flow = fail ("the file cannot be read");
        return result;
    }
    result.imported = importVitalFileForPlugin (utf8 (file.getFileName()), bytes, *library, ctx.host.userDirectory(), ctx.host.engineCaps());
    const VitalPluginImport& in = result.imported;
    if (! in.ok)
    {
        result.flow = fail ("Not imported: " + juce::String (in.error));
        return result;
    }
    if (in.bank)
    {
        result.flow = done ("Bank " + juce::String (in.name) + ": " + juce::String (in.summary.written) + " presets written to " + juce::String (in.presetDirectory)
                            + ", " + juce::String (in.summary.skippedSame) + " already there, " + juce::String (in.summary.failed) + " failed");
        return result;
    }
    if (in.kind == VitalFileKind::preset)
    {
        if (! ctx.host.loadPatch (in.values, in.content))
        {
            result.flow = fail ("this host cannot load a patch");
            return result;
        }
        result.flow = done ("Imported " + juce::String (in.name) + ": " + juce::String (in.report.numImported) + " settings, " + juce::String (in.report.numApproximated)
                            + " approximated, " + juce::String (in.report.numDropped) + " dropped");
        return result;
    }
    if (in.kind == VitalFileKind::wavetable)
    {
        auto content = ctx.host.content();
        if (osc >= 1 && osc <= kMaxContentWavetables && attachStoredWavetable (content, osc, in.tableRef, *library, in.tableAuthor) && commit (ctx, content))
        {
            const std::string key = "osc" + std::to_string (osc) + ".wavetable";
            setParameter (ctx.host, key.c_str(), static_cast<float> (std::max (0, choiceIndexOf (key.c_str(), "Preset"))));
            result.flow = done ("Table " + juce::String (in.name) + " (" + juce::String (in.tableFrames) + " frames) is now the table of oscillator " + juce::String (osc));
        }
        else
            result.flow = done ("Table " + juce::String (in.name) + " stored in the library");
        contentCache (ctx).clear();
        return result;
    }
    // a shape: it goes to the LFO slot
    auto content = ctx.host.content();
    if (lfo >= 1 && lfo <= kMaxContentLfoShapes)
    {
        content.lfoShapes.erase (std::remove_if (content.lfoShapes.begin(), content.lfoShapes.end(), [lfo] (const ContentLfoShape& s) { return s.lfo == lfo; }),
                                 content.lfoShapes.end());
        ContentLfoShape shape;
        shape.lfo = lfo;
        shape.name = in.lfoShape.name;
        shape.smooth = in.lfoShape.smooth;
        shape.points = in.lfoShape.points;
        content.lfoShapes.push_back (std::move (shape));
        std::sort (content.lfoShapes.begin(), content.lfoShapes.end(), [] (const ContentLfoShape& a, const ContentLfoShape& b) { return a.lfo < b.lfo; });
        if (commit (ctx, content))
        {
            const std::string key = "lfo" + std::to_string (lfo) + ".shape";
            setParameter (ctx.host, key.c_str(), static_cast<float> (std::max (0, choiceIndexOf (key.c_str(), "Custom"))));
            result.flow = done ("Shape " + juce::String (in.name) + " is now the shape of LFO " + juce::String (lfo));
            return result;
        }
    }
    result.flow = fail ("Choose an LFO to receive the shape");
    return result;
}

// ---- Serum 2 ---------------------------------------------------------------------------------------------------------------------------

SerumFlowResult importSerumFile (UiContext& ctx, const juce::File& file, int lfo)
{
    SerumFlowResult result;
    auto* library = ctx.host.contentLibrary();
    if (library == nullptr)
    {
        result.flow = fail ("this host keeps no content library");
        return result;
    }
    const std::vector<uint8_t> bytes = readFileBytes (file);
    if (bytes.empty())
    {
        result.flow = fail ("the file cannot be read");
        return result;
    }
    const std::string root = findSerumContentRoot (utf8 (file.getFullPathName()));
    SerumFolderResolver resolver (root);
    // the folder below Presets/Factory (Bass, Pad, ...) is the category of a preset
    std::string hint;
    for (auto dir = file.getParentDirectory(); dir.exists() && dir != dir.getParentDirectory(); dir = dir.getParentDirectory())
        if (dir.getParentDirectory().getFileName() == "Factory" && dir.getParentDirectory().getParentDirectory().getFileName() == "Presets")
        {
            hint = utf8 (dir.getFileName());
            break;
        }
    result.imported = importSerumFileForPlugin (utf8 (file.getFileName()), bytes, *library, ctx.host.engineCaps(), root.empty() ? nullptr : &resolver, hint);
    const SerumPluginImport& in = result.imported;
    if (! in.ok)
    {
        result.flow = fail ("Not imported: " + juce::String (in.error));
        return result;
    }
    const juce::String counts = juce::String (in.report.numImported) + " settings, " + juce::String (in.report.numApproximated) + " approximated, "
                                + juce::String (in.report.numDropped) + " dropped";
    if (in.kind == SerumFileKind::preset)
    {
        if (! ctx.host.loadPatch (in.values, in.content))
        {
            result.flow = fail ("this host cannot load a patch");
            return result;
        }
        contentCache (ctx).clear();
        result.flow = done ("Imported " + juce::String (in.name) + ": " + counts + (in.missingFiles > 0 ? ", " + juce::String (in.missingFiles) + " files not found" : juce::String()));
        return result;
    }
    if (in.kind == SerumFileKind::fxModule || in.kind == SerumFileKind::fxRack)
    {
        // the chain goes onto the patch that is loaded now: only the parameters it holds are written
        for (const int index : in.touched)
            setParameter (ctx.host, paramTable()[static_cast<size_t> (index)].key, in.values.plain[static_cast<size_t> (index)]);
        if (! in.content.impulses.empty())
        {
            auto content = ctx.host.content();
            content.impulses = in.content.impulses;
            commit (ctx, content);
        }
        contentCache (ctx).clear();
        result.flow = done ("Effect chain " + juce::String (in.name) + ": " + counts);
        return result;
    }
    if (in.kind == SerumFileKind::shape)
    {
        auto content = ctx.host.content();
        if (lfo >= 1 && lfo <= kMaxContentLfoShapes)
        {
            content.lfoShapes.erase (std::remove_if (content.lfoShapes.begin(), content.lfoShapes.end(), [lfo] (const ContentLfoShape& s) { return s.lfo == lfo; }),
                                     content.lfoShapes.end());
            ContentLfoShape shape;
            shape.lfo = lfo;
            shape.name = in.shape.name;
            shape.smooth = in.shape.smooth;
            shape.points = in.shape.points;
            content.lfoShapes.push_back (std::move (shape));
            std::sort (content.lfoShapes.begin(), content.lfoShapes.end(), [] (const ContentLfoShape& a, const ContentLfoShape& b) { return a.lfo < b.lfo; });
            if (commit (ctx, content))
            {
                const std::string key = "lfo" + std::to_string (lfo) + ".shape";
                setParameter (ctx.host, key.c_str(), static_cast<float> (std::max (0, choiceIndexOf (key.c_str(), "Custom"))));
                result.flow = done ("Shape " + juce::String (in.name) + " is now the shape of LFO " + juce::String (lfo));
                return result;
            }
        }
        result.flow = fail ("Choose an LFO to receive the shape");
        return result;
    }
    result.flow = done (juce::String (in.name) + ": this kind of file has no equivalent here (see the list)");
    return result;
}

// ---- cache -----------------------------------------------------------------------------------------------------------------------------

std::shared_ptr<const SamplePreview> ContentCache::samplePreview (UiHost& host, const ContentFileRef& ref)
{
    const std::string key = ref.pack + "/" + ref.file + "#" + ref.sha256;
    if (const auto it = samples.find (key); it != samples.end())
        return it->second;
    std::shared_ptr<const SamplePreview> preview;
    std::vector<uint8_t> bytes;
    if (auto* library = host.contentLibrary(); library != nullptr && library->readFile (ref, bytes))
        preview = SamplePreview::create (bytes, ref.file);
    samples[key] = preview;
    return preview;
}

std::shared_ptr<const wtedit::Wavetable> ContentCache::table (UiHost& host, const ContentFileRef& ref)
{
    const std::string key = ref.pack + "/" + ref.file + "#" + ref.sha256;
    if (const auto it = tables.find (key); it != tables.end())
        return it->second;
    std::shared_ptr<const wtedit::Wavetable> result;
    std::vector<uint8_t> bytes;
    wtedit::Wavetable table;
    if (auto* library = host.contentLibrary(); library != nullptr && library->readFile (ref, bytes) && wavetableFromWav (bytes, ref.name, table))
        result = std::make_shared<const wtedit::Wavetable> (std::move (table));
    tables[key] = result;
    return result;
}

void ContentCache::clear()
{
    samples.clear();
    tables.clear();
}

ContentCache& contentCache (UiContext& ctx)
{
    if (ctx.view->contentCache == nullptr)
        ctx.view->contentCache = std::make_shared<ContentCache>();
    return *ctx.view->contentCache;
}
} // namespace synthyboy::ui

namespace synthyboy::ui
{
void MissingFilesModel::refresh()
{
    missing.clear();
    if (auto* library = ctx.host.contentLibrary())
        missing = findMissingContent (ctx.host.content(), *library);
}

juce::String MissingFilesModel::describe (size_t row) const
{
    if (row >= missing.size())
        return {};
    const MissingContent& m = missing[row];
    const char* kind = m.kind == ContentKind::wavetable ? "wavetable" : (m.kind == ContentKind::sample ? "sample" : "impulse response");
    juce::String text = m.kind == ContentKind::impulse ? juce::String ("Convolve ") + kind
                      : (m.kind == ContentKind::sample && m.osc == kNoiseSampleOsc ? juce::String ("Noise ") + kind : "Osc " + juce::String (m.osc) + " " + kind);
    text << ": " << (m.ref.name.empty() ? juce::String (m.ref.file) : juce::String (m.ref.name));
    text << (m.reason == MissingReason::hashMismatch ? " (the file changed since the preset was saved)" : " (not found)");
    return text;
}

FlowResult MissingFilesModel::locate (size_t row, const juce::File& file, bool acceptDifferent)
{
    auto* library = ctx.host.contentLibrary();
    if (library == nullptr || row >= missing.size())
        return fail ("nothing to locate");
    const std::vector<uint8_t> bytes = readFileBytes (file);
    if (bytes.empty())
        return fail ("the file cannot be read");
    std::vector<uint8_t> stored = bytes;
    if (missing[row].kind != ContentKind::wavetable && ! isWavBytes (bytes)) // the library keeps WAV; another format is converted
    {
        DecodedAudio audio;
        juce::String error;
        if (! decodeAudioFile (file, audio, &error))
            return fail (error);
        stored = encodeWav (audio.left.data(), audio.right.empty() ? nullptr : audio.right.data(), audio.left.size(), juce::roundToInt (audio.sampleRate), 24);
    }
    auto content = ctx.host.content();
    if (! relinkMissing (content, missing[row], stored, utf8 (file.getFileName()), *library, acceptDifferent))
        return fail (missing[row].ref.sha256.empty() ? juce::String ("the file could not be stored") : juce::String ("this is not the file the preset was saved with"));
    if (! commit (ctx, content))
        return fail ("the content was refused");
    ctx.host.reloadContent(); // the reference may be the same as before: the engine has not seen the file yet
    contentCache (ctx).clear();
    refresh();
    return done ("Located " + file.getFileName());
}

FlowResult MissingFilesModel::remove (size_t row)
{
    if (row >= missing.size())
        return fail ("nothing to remove");
    auto content = ctx.host.content();
    dropMissing (content, missing[row]);
    if (! commit (ctx, content))
        return fail ("the content was refused");
    contentCache (ctx).clear();
    refresh();
    return done ("Reference removed");
}

FlowResult MissingFilesModel::removeAll()
{
    auto content = ctx.host.content();
    for (const auto& m : missing)
        dropMissing (content, m);
    if (! commit (ctx, content))
        return fail ("the content was refused");
    contentCache (ctx).clear();
    refresh();
    return done ("References removed");
}
} // namespace synthyboy::ui
