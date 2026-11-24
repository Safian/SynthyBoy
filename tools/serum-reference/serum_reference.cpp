// SynthyBoy - serum-reference: renders a .SerumPreset through the real Serum 2 plug-in, offline and deterministic.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   synthyboy_serum_reference --preset FILE --out FILE.wav [--id ID] [--plugin PATH] [--notes SPEC] [--length SEC] [--route R] [--timeout SEC]
//   synthyboy_serum_reference --probe [--preset FILE] [--dump-state FILE] [--plugin PATH]      (what the plug-in offers: used to find the loading route)
//
// The host is a JUCE AudioPluginFormat host without any window: it loads Serum2.vst3 (the AU as a fallback), puts the preset into the plug-in (route below),
// renders offline (setNonRealtime, 48 kHz, 32-bit float stereo, blocks of 512, no realtime clock anywhere), removes the reported plug-in latency from the front
// and writes the WAV and a JSON sidecar (preset id, sha256 of the preset file, note script, latency, plug-in version, render time).
// Notes SPEC: "note:velocity:startSec:holdSec" separated by commas, default "60:100:0:2.5"; the render is --length seconds long (default 4.0 = hold + 1.5 s tail).
// The plug-in is licensed software: if it cannot run headless (a dialog, an authorization) the host reports that and exits with 4; nothing here touches licence files.
// Exit codes: 0 ok, 2 usage, 3 plug-in could not be loaded, 4 preset could not be loaded / plug-in needs interaction, 5 timeout, 6 render failed.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_cryptography/juce_cryptography.h>

#include "serum_internal.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <unistd.h>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;

struct Note
{
    int note = 60, velocity = 100;
    double start = 0.0, hold = 2.5;
};

struct Options
{
    juce::String plugin, preset, out, id, notes = "60:100:0:2.5", route = "wrapped", dumpState, setParams;
    double length = 4.0;
    int timeout = 120;
    bool probe = false;
    bool listParams = false;
    bool allowUnchanged = false;   // a preset that equals the plug-in's own init state does not change it (experiments)
};

void fail (int code, const juce::String& what)
{
    std::fprintf (stderr, "serum-reference: %s\n", what.toRawUTF8());
    std::fflush (stderr);
    std::_Exit (code);
}

std::vector<Note> parseNotes (const juce::String& spec)
{
    std::vector<Note> out;
    for (auto& item : juce::StringArray::fromTokens (spec, ",", ""))
    {
        auto f = juce::StringArray::fromTokens (item, ":", "");
        if (f.size() != 4) fail (2, "bad note spec: " + item);
        out.push_back ({ f[0].getIntValue(), f[1].getIntValue(), f[2].getDoubleValue(), f[3].getDoubleValue() });
    }
    return out;
}

juce::String jsonStr (const juce::String& s)
{
    return juce::JSON::toString (juce::var (s), true);
}

juce::MemoryBlock readFile (const juce::File& f)
{
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb)) fail (2, "cannot read " + f.getFullPathName());
    return mb;
}

void pump (int ms)
{
    auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (ms);
    while (juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
}

std::unique_ptr<juce::AudioPluginInstance> loadPlugin (const juce::String& path, juce::String& formatName)
{
    juce::AudioPluginFormatManager fm;
    fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
    fm.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
    std::vector<juce::String> candidates;
    if (path.isNotEmpty()) candidates.push_back (path);
    else
    {
        candidates.push_back ("/Library/Audio/Plug-Ins/VST3/Serum2.vst3");
        candidates.push_back ("/Library/Audio/Plug-Ins/Components/Serum2.component");
    }
    juce::String lastError;
    for (auto& c : candidates)
    {
        for (auto* fmt : fm.getFormats())
        {
            if (! fmt->fileMightContainThisPluginType (c)) continue;
            juce::OwnedArray<juce::PluginDescription> found;
            fmt->findAllTypesForFile (found, c);
            for (auto* d : found)
            {
                juce::String err;
                auto inst = fm.createPluginInstance (*d, kSampleRate, kBlock, err);
                if (inst != nullptr) { formatName = fmt->getName(); return inst; }
                lastError = err;
            }
        }
    }
    fail (3, "plug-in could not be loaded: " + lastError);
    return nullptr;
}

juce::String sha256Of (const juce::MemoryBlock& mb) { return juce::SHA256 (mb.getData(), mb.getSize()).toHexString(); }


// The route that works (docs/ui-design/REFCOMPARE-REPORT.md, "Loading a preset into Serum 2"): the plug-in state is a JUCE VST3 wrapper ("VC2!", the size, an XML text
// with the base64 of the IComponent and IEditController state), and the IComponent state is again an XferJson container whose body is the same CBOR map as a preset's,
// restricted to the members of the processor (the preset file also carries the interface members: Osc, WTOsc, Filter, SerumGUI, preset name ...; the plug-in refuses a
// processor state that has them). So the preset body is rebuilt from the members the plug-in's own processor state has (the value of the preset where it has one, the plug-in's
// own value otherwise), packed as a zstd frame (raw blocks: no compressor needed, any decoder reads it), the header is the plug-in's own with the new MD5, and the container is put
// into the wrapper of the state the plug-in itself reports.
using Bytes = std::vector<juce::uint8>;

struct Cbor
{
    // end of the data item starting at `i` (definite lengths only, the only kind the files use); -1 on malformed input
    static long skip (const Bytes& b, size_t i)
    {
        if (i >= b.size()) return -1;
        const int mt = b[i] >> 5, ai = b[i] & 31;
        ++i;
        juce::uint64 arg = static_cast<juce::uint64> (ai);
        if (ai >= 24)
        {
            if (ai > 27) return -1;
            const size_t n = static_cast<size_t> (1) << (ai - 24);
            if (i + n > b.size()) return -1;
            arg = 0;
            for (size_t k = 0; k < n; ++k) arg = (arg << 8) | b[i + k];
            i += n;
        }
        switch (mt)
        {
            case 0: case 1: case 7: return static_cast<long> (i);
            case 2: case 3: return i + arg > b.size() ? -1 : static_cast<long> (i + arg);
            case 6: return skip (b, i);
            case 4: case 5:
            {
                const juce::uint64 items = mt == 4 ? arg : arg * 2;
                for (juce::uint64 k = 0; k < items; ++k) { const long e = skip (b, i); if (e < 0) return -1; i = static_cast<size_t> (e); }
                return static_cast<long> (i);
            }
            default: return -1;
        }
    }
    struct Member { std::string key; size_t begin, end; }; // the key item and the value item of a pair: [begin, end) covers both
    // the pairs of a top level map
    static bool members (const Bytes& b, std::vector<Member>& out, size_t& firstPair)
    {
        if (b.empty() || (b[0] >> 5) != 5) return false;
        const int ai = b[0] & 31;
        size_t i = 1;
        juce::uint64 n = static_cast<juce::uint64> (ai);
        if (ai >= 24)
        {
            if (ai > 27) return false;
            const size_t w = static_cast<size_t> (1) << (ai - 24);
            n = 0;
            for (size_t k = 0; k < w; ++k) n = (n << 8) | b[i + k];
            i += w;
        }
        firstPair = i;
        for (juce::uint64 k = 0; k < n; ++k)
        {
            const size_t begin = i;
            const long ke = skip (b, i);
            if (ke < 0 || (b[i] >> 5) != 3) return false;
            // key text
            size_t j = i + 1;
            juce::uint64 len = b[i] & 31;
            if (len >= 24) { const size_t w = static_cast<size_t> (1) << (len - 24); len = 0; for (size_t q = 0; q < w; ++q) len = (len << 8) | b[j + q]; j += w; }
            std::string key (reinterpret_cast<const char*> (&b[j]), static_cast<size_t> (len));
            const long ve = skip (b, static_cast<size_t> (ke));
            if (ve < 0) return false;
            out.push_back ({ key, begin, static_cast<size_t> (ve) });
            i = static_cast<size_t> (ve);
        }
        return i == b.size();
    }
};

struct Container
{
    juce::var header;
    Bytes body; // decoded CBOR
};

bool openXfer (const juce::uint8* d, size_t n, Container& c, std::string& err)
{
    if (n < 25 || std::memcmp (d, "XferJson", 9) != 0) { err = "not an XferJson container"; return false; }
    juce::uint64 hl = 0;
    for (int i = 7; i >= 0; --i) hl = (hl << 8) | d[9 + i];
    if (17 + hl + 8 > n) { err = "container header out of range"; return false; }
    c.header = juce::JSON::parse (juce::String::fromUTF8 (reinterpret_cast<const char*> (d + 17), static_cast<int> (hl)));
    if (! c.header.isObject()) { err = "container header is not JSON"; return false; }
    const juce::uint8* r = d + 17 + hl;
    juce::uint32 format = 0;
    for (int i = 3; i >= 0; --i) format = (format << 8) | r[4 + i];
    if (format != 2) { err = "body format " + std::to_string (format) + " (zlib + UBJSON) is not handled"; return false; }
    std::vector<juce::uint8> body;
    if (synthyboy::serumimport::zstdDecompress (r + 8, n - 17 - hl - 8, 512u << 20, body) != synthyboy::serumimport::ZstdStatus::ok) { err = "zstd body does not decode"; return false; }
    c.body = std::move (body);
    return true;
}

/** A zstd frame that holds `raw` in raw blocks (RFC 8878: single segment, content size field, no checksum). */
Bytes zstdRawFrame (const Bytes& raw)
{
    Bytes f { 0x28, 0xB5, 0x2F, 0xFD };
    const juce::uint64 n = raw.size();
    if (n <= 255) { f.push_back (0x20); f.push_back (static_cast<juce::uint8> (n)); }                        // FCS flag 0, single segment
    else if (n < 65536 + 256) { f.push_back (0x60); const auto v = n - 256; f.push_back (static_cast<juce::uint8> (v)); f.push_back (static_cast<juce::uint8> (v >> 8)); }
    else { f.push_back (0xA0); for (int i = 0; i < 4; ++i) f.push_back (static_cast<juce::uint8> (n >> (8 * i))); }
    size_t pos = 0;
    do
    {
        const size_t len = std::min<size_t> (raw.size() - pos, 128 * 1024);
        const bool last = pos + len == raw.size();
        const juce::uint32 bh = (static_cast<juce::uint32> (len) << 3) | (last ? 1u : 0u); // type 0 = raw
        for (int i = 0; i < 3; ++i) f.push_back (static_cast<juce::uint8> (bh >> (8 * i)));
        f.insert (f.end(), raw.begin() + static_cast<long> (pos), raw.begin() + static_cast<long> (pos + len));
        pos += len;
    } while (pos < raw.size());
    return f;
}

juce::MemoryBlock wrapPresetAsState (const juce::MemoryBlock& file, const juce::MemoryBlock& currentState, juce::String& error)
{
    std::string err;
    Container pre;
    if (! openXfer (static_cast<const juce::uint8*> (file.getData()), file.getSize(), pre, err)) { error = err; return {}; }

    auto st = juce::String::fromUTF8 (static_cast<const char*> (currentState.getData()) + 8, static_cast<int> (currentState.getSize()) - 9);
    auto cb = st.indexOf ("<IComponent>"), ce = st.indexOf ("</IComponent>");
    juce::MemoryBlock curBlock;
    if (cb < 0 || ce < cb || ! curBlock.fromBase64Encoding (st.substring (cb + 12, ce))) { error = "unexpected plug-in state layout"; return {}; }
    Container own;
    if (! openXfer (static_cast<const juce::uint8*> (curBlock.getData()), curBlock.getSize(), own, err)) { error = "plug-in state: " + err; return {}; }

    std::vector<Cbor::Member> pm, om;
    size_t pf = 0, of = 0;
    if (! Cbor::members (pre.body, pm, pf) || ! Cbor::members (own.body, om, of)) { error = "body is not a CBOR map"; return {}; }
    Bytes body;
    const size_t count = om.size();
    if (count < 24) body.push_back (static_cast<juce::uint8> (0xA0 | count));
    else { body.push_back (0xB8); body.push_back (static_cast<juce::uint8> (count)); }
    for (const auto& m : om)
    {
        const Cbor::Member* src = &m;
        const Bytes* from = &own.body;
        for (const auto& q : pm)
            if (q.key == m.key) { src = &q; from = &pre.body; break; }
        body.insert (body.end(), from->begin() + static_cast<long> (src->begin), from->begin() + static_cast<long> (src->end));
    }
    const Bytes comp = zstdRawFrame (body);
    const auto hash = juce::MD5 (comp.data(), comp.size()).toHexString();

    juce::String nh = "{\"component\":\"processor\",\"hash\":\"" + hash + "\",\"product\":\"Serum2\",\"productVersion\":" + juce::JSON::toString (own.header["productVersion"], true)
                      + ",\"url\":\"https://xferrecords.com/\",\"vendor\":\"Xfer Records\",\"version\":" + juce::JSON::toString (own.header["version"], true) + "}";
    juce::MemoryBlock container;
    container.append ("XferJson", 9);
    const juce::uint64 hl = static_cast<juce::uint64> (nh.getNumBytesAsUTF8());
    for (int i = 0; i < 8; ++i) { auto b = static_cast<juce::uint8> ((hl >> (8 * i)) & 0xff); container.append (&b, 1); }
    container.append (nh.toRawUTF8(), static_cast<size_t> (hl));
    for (int i = 0; i < 4; ++i) { auto b = static_cast<juce::uint8> ((static_cast<juce::uint64> (body.size()) >> (8 * i)) & 0xff); container.append (&b, 1); }
    const juce::uint8 fmt[4] = { 2, 0, 0, 0 };
    container.append (fmt, 4);
    container.append (comp.data(), comp.size());

    auto newSt = st.substring (0, cb + 12) + container.toBase64Encoding() + st.substring (ce);
    juce::MemoryBlock out;
    out.append ("VC2!", 4);
    const auto sz = static_cast<juce::uint32> (newSt.getNumBytesAsUTF8());
    for (int i = 0; i < 4; ++i) { auto b = static_cast<juce::uint8> ((sz >> (8 * i)) & 0xff); out.append (&b, 1); }
    out.append (newSt.toRawUTF8(), sz + 1); // with the terminating NUL, like the plug-in's own state
    return out;
}

Options parseArgs (int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i)
    {
        juce::String a (argv[i]);
        auto next = [&]() -> juce::String { if (i + 1 >= argc) fail (2, "missing value for " + a); return juce::String (argv[++i]); };
        if (a == "--preset") o.preset = next();
        else if (a == "--out") o.out = next();
        else if (a == "--id") o.id = next();
        else if (a == "--plugin") o.plugin = next();
        else if (a == "--notes") o.notes = next();
        else if (a == "--length") o.length = next().getDoubleValue();
        else if (a == "--route") o.route = next();
        else if (a == "--timeout") o.timeout = next().getIntValue();
        else if (a == "--dump-state") o.dumpState = next();
        else if (a == "--set-params") o.setParams = next();
        else if (a == "--probe") o.probe = true;
        else if (a == "--list-params") o.listParams = true;
        else if (a == "--allow-unchanged") o.allowUnchanged = true;
        else fail (2, "unknown option " + a);
    }
    return o;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Options opt = parseArgs (argc, argv);

    // the watchdog: a plug-in that blocks (a modal dialog, an authorization) must not hang the batch
    std::atomic<bool> done { false };
    const int timeoutSeconds = opt.timeout;
    std::thread watchdog ([&done, timeoutSeconds] {
        for (int s = 0; s < timeoutSeconds * 10 && ! done; ++s) std::this_thread::sleep_for (std::chrono::milliseconds (100));
        if (! done) fail (5, "timeout after " + juce::String (timeoutSeconds) + " s (the plug-in blocks: a dialog or an authorization?)");
    });
    watchdog.detach();

    juce::String formatName;
    auto plugin = loadPlugin (opt.plugin, formatName);
    pump (300);

    juce::MemoryBlock presetBytes;
    if (opt.preset.isNotEmpty()) presetBytes = readFile (juce::File (opt.preset));

    juce::MemoryBlock initialState;
    plugin->getStateInformation (initialState);

    // ---- load the preset into the plug-in ----
    auto loadPreset = [&]() -> bool {
        if (presetBytes.getSize() == 0) return true;
        if (opt.route == "raw")
            plugin->setStateInformation (presetBytes.getData(), static_cast<int> (presetBytes.getSize()));
        else
        {
            juce::String err;
            auto st = wrapPresetAsState (presetBytes, initialState, err);
            if (st.getSize() == 0) fail (4, "preset could not be wrapped: " + err);
            if (opt.dumpState.isNotEmpty()) juce::File (opt.dumpState + ".wrapped").replaceWithData (st.getData(), st.getSize());
            plugin->setStateInformation (st.getData(), static_cast<int> (st.getSize()));
        }
        pump (300);
        juce::MemoryBlock after;
        plugin->getStateInformation (after);
        if (opt.dumpState.isNotEmpty()) juce::File (opt.dumpState + ".afterset").replaceWithData (after.getData(), after.getSize());
        return after != initialState;
    };

    if (opt.probe || opt.listParams)
    {
        std::printf ("plugin: %s (%s) version %s\n", plugin->getName().toRawUTF8(), formatName.toRawUTF8(), plugin->getPluginDescription().version.toRawUTF8());
        std::printf ("inputs %d outputs %d params %d programs %d current program %d latency %d tail %.3f\n", plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels(),
                     static_cast<int> (plugin->getParameters().size()), plugin->getNumPrograms(), plugin->getCurrentProgram(), plugin->getLatencySamples(), plugin->getTailLengthSeconds());
        std::printf ("state: %zu bytes sha256 %s\n", initialState.getSize(), sha256Of (initialState).toRawUTF8());
        if (opt.dumpState.isNotEmpty()) juce::File (opt.dumpState + ".initial").replaceWithData (initialState.getData(), initialState.getSize());
        if (presetBytes.getSize() > 0) std::printf ("state changed by the preset: %s\n", loadPreset() ? "yes" : "no");
        if (opt.listParams)
            for (auto* p : plugin->getParameters())
                std::printf ("param %d name=\"%s\" value=%.6f text=\"%s\"\n", p->getParameterIndex(), p->getName (64).toRawUTF8(), static_cast<double> (p->getValue()),
                             p->getText (p->getValue(), 32).toRawUTF8());
        done = true;
        std::fflush (stdout);
        std::_Exit (0);
    }

    if (opt.out.isEmpty()) fail (2, "--out is required for a render");
    const bool changed = loadPreset();
    if (! changed && presetBytes.getSize() > 0 && ! opt.allowUnchanged) fail (4, "the plug-in state did not change: the preset was not accepted (route " + opt.route + ")");

    // ---- offline render ----
    plugin->setNonRealtime (true);
    plugin->setPlayConfigDetails (0, 2, kSampleRate, kBlock);
    plugin->prepareToPlay (kSampleRate, kBlock);
    const int latency = plugin->getLatencySamples();
    const auto notes = parseNotes (opt.notes);
    const auto t0 = std::chrono::steady_clock::now();

    const int total = static_cast<int> (std::llround (opt.length * kSampleRate));
    const int warmBlocks = 8;
    juce::AudioBuffer<float> block (2, kBlock);
    juce::AudioBuffer<float> out (2, total);
    juce::MidiBuffer midi;
    auto sampleOf = [](double sec) { return static_cast<int> (std::llround (sec * kSampleRate)); };
    // block -warmBlocks .. -1: silence (the plug-in settles after the state load), the controllers are set in the first one
    for (int b = -warmBlocks; b * kBlock < total + latency; ++b)
    {
        midi.clear();
        const int base = b * kBlock;
        if (b == -warmBlocks)
        {
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, 0), 0);          // mod wheel 0
            midi.addEvent (juce::MidiMessage::pitchWheel (1, 8192), 0);               // pitch bend centre
        }
        for (const auto& n : notes)
        {
            const int on = sampleOf (n.start), off = sampleOf (n.start + n.hold);
            if (on >= base && on < base + kBlock) midi.addEvent (juce::MidiMessage::noteOn (1, n.note, static_cast<juce::uint8> (n.velocity)), on - base);
            if (off >= base && off < base + kBlock) midi.addEvent (juce::MidiMessage::noteOff (1, n.note), off - base);
        }
        block.clear();
        plugin->processBlock (block, midi);
        if (b >= 0)
        {
            // output sample i of the block is rendered time (base + i - latency)
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlock; ++i)
                {
                    const int t = base + i - latency;
                    if (t >= 0 && t < total) out.setSample (ch, t, block.getSample (ch, i));
                }
        }
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now() - t0).count();
    plugin->releaseResources();

    float peak = 0.0f;
    bool finite = true;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < total; ++i)
        {
            const float v = out.getSample (ch, i);
            if (! std::isfinite (v)) finite = false;
            peak = std::max (peak, std::abs (v));
        }

    juce::File wav (opt.out);
    wav.getParentDirectory().createDirectory();
    wav.deleteFile();
    {
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::OutputStream> fos (wav.createOutputStream());
        if (fos == nullptr) fail (6, "cannot write " + opt.out);
        auto w = fmt.createWriterFor (fos, juce::AudioFormatWriterOptions().withSampleRate (kSampleRate).withNumChannels (2).withBitsPerSample (32));
        if (w == nullptr) fail (6, "cannot create WAV writer");
        w->writeFromAudioSampleBuffer (out, 0, total);
    }

    juce::String notesJson;
    for (const auto& n : notes)
        notesJson += juce::String (notesJson.isEmpty() ? "" : ",") + "{\"note\":" + juce::String (n.note) + ",\"velocity\":" + juce::String (n.velocity) + ",\"start\":" + juce::String (n.start)
                     + ",\"hold\":" + juce::String (n.hold) + "}";
    juce::String json = "{\n  \"schema\": \"synthyboy-serum-reference\",\n  \"version\": 1,\n  \"presetId\": " + jsonStr (opt.id) + ",\n  \"presetSha256\": \"" + sha256Of (presetBytes)
                        + "\",\n  \"wav\": " + jsonStr (wav.getFileName()) + ",\n  \"sampleRate\": 48000,\n  \"block\": 512,\n  \"frames\": " + juce::String (total)
                        + ",\n  \"channels\": 2,\n  \"noteScript\": [" + notesJson + "],\n  \"length\": " + juce::String (opt.length) + ",\n  \"latencySamples\": " + juce::String (latency)
                        + ",\n  \"latencyCompensated\": true,\n  \"warmupBlocks\": " + juce::String (warmBlocks) + ",\n  \"modWheel\": 0,\n  \"pitchBend\": \"centre\",\n  \"plugin\": "
                        + jsonStr (plugin->getName() + " " + formatName + " " + plugin->getPluginDescription().version) + ",\n  \"route\": " + jsonStr (opt.route)
                        + ",\n  \"renderMs\": " + juce::String (static_cast<juce::int64> (ms)) + ",\n  \"peak\": " + juce::String (static_cast<double> (peak), 6)
                        + ",\n  \"finite\": " + (finite ? "true" : "false") + "\n}\n";
    wav.withFileExtension ("json").replaceWithText (json);
    done = true;
    std::fflush (stdout);
    std::fflush (stderr);
    std::_Exit (finite ? 0 : 6);   // no teardown: the plug-in is not unloaded (nothing to save), and the detached watchdog cannot race the exit
}
