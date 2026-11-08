// SynthyBoy - core: parsing of the embedded content of a .vital file (docs/ui-design/11-vital-preset-import.md 1.1, 1.3).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "vital_content.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "number.h"
#include "sha256.h"
#include "synthyboy/vital_import.h"

namespace synthyboy
{
namespace vitalimport
{
using json::Doc;
using json::Type;

namespace
{
constexpr int kMaxGroups = 8, kMaxComponents = 32, kMaxKeyframes = 257, kMaxLinePoints = 100;

double clampd (double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool numberOf (const Doc& doc, int node, double& out)
{
    if (node < 0)
        return false;
    const json::Node& n = doc.nodes[static_cast<size_t> (node)];
    double v;
    switch (n.type)
    {
        case Type::number: v = n.number; break;
        case Type::boolean: v = n.boolean ? 1.0 : 0.0; break;
        case Type::string:
        {
            const std::string& s = doc.text (n.str);
            const char* next = nullptr;
            if (s.empty() || ! number::parseDecimal (s.data(), s.data() + s.size(), false, v, next) || next != s.data() + s.size())
                return false;
            break;
        }
        default: return false;
    }
    if (! std::isfinite (v))
        return false;
    out = v;
    return true;
}

/** Field lookup: the keyframe object first, then the component object (the design lists some fields without saying which). */
int fieldNode (const Doc& doc, int kf, int comp, const char* name)
{
    int n = kf >= 0 ? doc.find (kf, name) : -1;
    if (n < 0 && comp >= 0)
        n = doc.find (comp, name);
    return n;
}

double numField (const Doc& doc, int kf, int comp, const char* name, double def)
{
    double v;
    return numberOf (doc, fieldNode (doc, kf, comp, name), v) ? v : def;
}

bool boolField (const Doc& doc, int kf, int comp, const char* name, bool def)
{
    double v;
    return numberOf (doc, fieldNode (doc, kf, comp, name), v) ? v != 0.0 : def;
}

std::string stringOf (const Doc& doc, int node, size_t maxBytes)
{
    if (node < 0 || doc.nodes[static_cast<size_t> (node)].type != Type::string)
        return {};
    return cleanText (doc.text (doc.nodes[static_cast<size_t> (node)].str), maxBytes, true);
}

bool decodeField (const Doc& doc, int node, std::vector<uint8_t>& out, ContentNotes& notes)
{
    if (node < 0 || doc.nodes[static_cast<size_t> (node)].type != Type::string)
        return false;
    const std::string& s = doc.text (doc.nodes[static_cast<size_t> (node)].str);
    const size_t decoded = s.size() / 4 * 3 + 3;
    if (decoded > kMaxBase64Decoded + 3 || notes.decodedBytes + decoded > kMaxBase64PerFile)
    {
        notes.limitExceeded = true;
        return false;
    }
    if (! base64Decode (s, kMaxBase64Decoded, out))
    {
        ++notes.badFields;
        return false;
    }
    notes.decodedBytes += out.size();
    return true;
}
} // namespace

bool base64Decode (const std::string& in, size_t maxBytes, std::vector<uint8_t>& out)
{
    out.clear();
    size_t n = in.size();
    int pad = 0;
    while (n > 0 && in[n - 1] == '=' && pad < 2)
    {
        --n;
        ++pad;
    }
    if (n % 4 == 1)
        return false;
    if (n / 4 * 3 + 2 > maxBytes + 2)
        return false;
    out.reserve(n / 4 * 3 + 3);
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const char c = in[i];
        uint32_t v;
        if (c >= 'A' && c <= 'Z') v = static_cast<uint32_t> (c - 'A');
        else if (c >= 'a' && c <= 'z') v = static_cast<uint32_t> (c - 'a' + 26);
        else if (c >= '0' && c <= '9') v = static_cast<uint32_t> (c - '0' + 52);
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else return false;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back (static_cast<uint8_t> ((acc >> bits) & 0xFF));
            acc &= (1u << bits) - 1u;
        }
    }
    return out.size() <= maxBytes;
}

bool parseLfoLine (const Doc& doc, int node, ParsedLfo& out, ContentNotes& notes)
{
    if (node < 0 || doc.nodes[static_cast<size_t> (node)].type != Type::object)
        return false;
    out.name = stringOf (doc, doc.find (node, "name"), 255);

    const int pointsNode = doc.find (node, "points"), powersNode = doc.find (node, "powers");
    if (pointsNode < 0 || powersNode < 0 || doc.nodes[static_cast<size_t> (pointsNode)].type != Type::array
        || doc.nodes[static_cast<size_t> (powersNode)].type != Type::array)
        return false;
    double declared = 0.0;
    if (! numberOf (doc, doc.find (node, "num_points"), declared))
        return false;

    const size_t pointNumbers = doc.nodes[static_cast<size_t> (pointsNode)].count;
    const size_t powerCount = doc.nodes[static_cast<size_t> (powersNode)].count;
    // design 1.3.2: num_points clamped to [2, min (100, powers.size (), points.size () / 2)]; fewer than 2 usable points = invalid
    const double cap = static_cast<double> (std::min<size_t> ({ static_cast<size_t> (kMaxLinePoints), powerCount, pointNumbers / 2 }));
    if (declared > static_cast<double> (kMaxLinePoints))
        notes.limitExceeded = true;
    if (cap < 2.0)
        return false;
    const double n = std::min (std::max (declared, 2.0), cap);
    const size_t np = static_cast<size_t> (n);

    std::vector<double> nums;
    nums.reserve (np * 2);
    size_t taken = 0;
    for (int c = doc.nodes[static_cast<size_t> (pointsNode)].first; c >= 0 && taken < np * 2; c = doc.nodes[static_cast<size_t> (c)].next, ++taken)
    {
        double v = 0.0;
        numberOf (doc, c, v);
        nums.push_back (v);
    }
    std::vector<double> pw;
    taken = 0;
    for (int c = doc.nodes[static_cast<size_t> (powersNode)].first; c >= 0 && taken < np; c = doc.nodes[static_cast<size_t> (c)].next, ++taken)
    {
        double v = 0.0;
        numberOf (doc, c, v);
        pw.push_back (v);
    }
    wtrender::Line line;
    double lastX = 0.0;
    for (size_t i = 0; i < np; ++i)
    {
        const double x = std::max (clampd (nums[i * 2], 0.0, 1.0), lastX); // non-decreasing
        lastX = x;
        line.x.push_back (static_cast<float> (x));
        line.y.push_back (static_cast<float> (1.0 - clampd (nums[i * 2 + 1], 0.0, 1.0))); // SPEC-GAP: the file stores 1 - value
        line.power.push_back (static_cast<float> (clampd (pw[i], -50.0, 50.0)));
    }
    line.smooth = false;
    double sm = 0.0;
    if (numberOf (doc, doc.find (node, "smooth"), sm))
        line.smooth = sm != 0.0;
    out.line = std::move (line);
    return true;
}

bool parseWavetable (const Doc& doc, int node, ParsedWavetable& out, ContentNotes& notes)
{
    if (node < 0 || doc.nodes[static_cast<size_t> (node)].type != Type::object)
        return false;
    out.name = stringOf (doc, doc.find (node, "name"), 255);
    out.author = stringOf (doc, doc.find (node, "author"), 255);

    const int groupsNode = doc.find (node, "groups");
    if (groupsNode < 0 || doc.nodes[static_cast<size_t> (groupsNode)].type != Type::array)
    {
        // a bare LFO line object: the wavetable is built from that line
        ParsedLfo lfo;
        if (doc.find (node, "points") < 0 || ! parseLfoLine (doc, node, lfo, notes))
            return false;
        wtrender::Component c;
        c.type = wtrender::ComponentType::lineSource;
        wtrender::Keyframe k;
        k.position = 0;
        k.line = lfo.line;
        c.keyframes.push_back (std::move (k));
        out.desc.groups.emplace_back();
        out.desc.groups[0].components.push_back (std::move (c));
        return true;
    }

    out.desc.removeAllDc = boolField (doc, -1, node, "remove_all_dc", false);
    out.desc.fullNormalize = boolField (doc, -1, node, "full_normalize", false);

    int groupCount = 0;
    for (int g = doc.nodes[static_cast<size_t> (groupsNode)].first; g >= 0; g = doc.nodes[static_cast<size_t> (g)].next)
    {
        if (groupCount >= kMaxGroups)
        {
            notes.limitExceeded = true;
            break;
        }
        if (doc.nodes[static_cast<size_t> (g)].type != Type::object)
            continue;
        ++groupCount;
        out.desc.groups.emplace_back();
        wtrender::Group& group = out.desc.groups.back();
        const int compsNode = doc.find (g, "components");
        if (compsNode < 0 || doc.nodes[static_cast<size_t> (compsNode)].type != Type::array)
            continue;
        int compCount = 0;
        for (int cn = doc.nodes[static_cast<size_t> (compsNode)].first; cn >= 0; cn = doc.nodes[static_cast<size_t> (cn)].next)
        {
            if (compCount >= kMaxComponents)
            {
                notes.limitExceeded = true;
                break;
            }
            if (doc.nodes[static_cast<size_t> (cn)].type != Type::object)
                continue;
            ++compCount;
            wtrender::Component c;
            const int typeNode = doc.find (cn, "type");
            const std::string typeName = stringOf (doc, typeNode, 64);
            if (! wtrender::componentTypeFromName (typeName.c_str(), c.type))
            {
                notes.unsupportedTypes.push_back (typeName);
                continue;
            }
            double v = 0.0;
            c.interpolationStyle = numberOf (doc, doc.find (cn, "interpolation_style"), v)
                ? static_cast<wtrender::Interpolation> (static_cast<int> (clampd (std::floor (v + 0.5), 0.0, 2.0))) : wtrender::Interpolation::linear;
            c.frequencyInterpolation = c.type == wtrender::ComponentType::waveSource && boolField (doc, -1, cn, "interpolation", false);
            c.normalize = boolField (doc, -1, cn, "normalize", true);
            c.normalizeGain = boolField (doc, -1, cn, "normalize_gain", true);
            c.normalizeMult = static_cast<float> (clampd (numField (doc, -1, cn, "normalize_mult", 1.0), 0.0, 16.0));
            c.windowSize = static_cast<int> (clampd (numField (doc, -1, cn, "window_size", 2048.0), 2.0, 1048576.0));
            c.fadeStyle = static_cast<int> (clampd (std::floor (numField (doc, -1, cn, "fade_style", 0.0) + 0.5), 0.0, 3.0));
            c.phaseStyle = static_cast<int> (clampd (std::floor (numField (doc, -1, cn, "phase_style", 0.0) + 0.5), 0.0, 2.0));
            c.randomSeed = static_cast<uint32_t> (static_cast<int64_t> (clampd (numField (doc, -1, cn, "random_seed", 0.0), -2147483648.0, 4294967295.0)));
            c.audioSampleRate = static_cast<float> (clampd (numField (doc, -1, cn, "audio_sample_rate", 44100.0), 1.0, 1.0e7));

            if (c.type == wtrender::ComponentType::audioFileSource)
            {
                std::vector<uint8_t> bytes;
                if (decodeField (doc, doc.find (cn, "audio_file"), bytes, notes))
                {
                    c.audio.resize (bytes.size() / 2);
                    for (size_t i = 0; i < c.audio.size(); ++i) // little endian int16
                        c.audio[i] = static_cast<int16_t> (static_cast<uint16_t> (bytes[i * 2] | (bytes[i * 2 + 1] << 8)));
                }
            }

            const int kfsNode = doc.find (cn, "keyframes");
            int kfCount = 0;
            if (kfsNode >= 0 && doc.nodes[static_cast<size_t> (kfsNode)].type == Type::array)
                for (int kn = doc.nodes[static_cast<size_t> (kfsNode)].first; kn >= 0; kn = doc.nodes[static_cast<size_t> (kn)].next)
                {
                    if (kfCount >= kMaxKeyframes)
                    {
                        notes.limitExceeded = true;
                        break;
                    }
                    if (doc.nodes[static_cast<size_t> (kn)].type != Type::object)
                        continue;
                    ++kfCount;
                    wtrender::Keyframe k;
                    k.position = static_cast<int> (clampd (std::floor (numField (doc, kn, -1, "position", 0.0) + 0.5), 0.0, 256.0));
                    k.pullPower = static_cast<float> (clampd (numField (doc, kn, cn, "pull_power", 0.0), -50.0, 50.0));
                    k.startPosition = static_cast<float> (clampd (numField (doc, kn, cn, "start_position", 0.0), 0.0, 1.0e9));
                    k.windowFade = static_cast<float> (clampd (numField (doc, kn, cn, "window_fade", 1.0), 0.0, 1.0));
                    k.windowSize = static_cast<float> (clampd (numField (doc, kn, cn, "window_size", c.windowSize), 2.0, 1048576.0));
                    k.phase = static_cast<float> (clampd (numField (doc, kn, cn, "phase", 0.0), -1.0e6, 1.0e6));
                    k.mix = static_cast<float> (clampd (numField (doc, kn, cn, "mix", 1.0), 0.0, 1.0));
                    k.style = static_cast<int> (clampd (std::floor (numField (doc, kn, cn, "style", 0.0) + 0.5), 0.0, 16.0));
                    k.cutoff = static_cast<float> (clampd (numField (doc, kn, cn, "cutoff", 0.5), 0.0, 1.0));
                    k.shape = static_cast<float> (clampd (numField (doc, kn, cn, "shape", 0.5), 0.0, 1.0));
                    k.upRunRise = static_cast<float> (clampd (numField (doc, kn, cn, "up_run_rise", 0.0), 0.0, 1.0));
                    k.downRunRise = static_cast<float> (clampd (numField (doc, kn, cn, "down_run_rise", 0.0), 0.0, 1.0));
                    k.leftPosition = static_cast<float> (clampd (numField (doc, kn, cn, "left_position", 0.0), 0.0, 1.0));
                    k.rightPosition = static_cast<float> (clampd (numField (doc, kn, cn, "right_position", 1.0), 0.0, 1.0));
                    k.windowShape = static_cast<float> (clampd (numField (doc, kn, cn, "window_shape", 0.0), 0.0, 16.0));
                    k.foldBoost = static_cast<float> (clampd (numField (doc, kn, cn, "fold_boost", 0.0), 0.0, 4.0));
                    k.horizontalPower = static_cast<float> (clampd (numField (doc, kn, cn, "horizontal_power", 0.0), -50.0, 50.0));
                    k.verticalPower = static_cast<float> (clampd (numField (doc, kn, cn, "vertical_power", 0.0), -50.0, 50.0));
                    k.horizontalAsymmetric = boolField (doc, kn, cn, "horizontal_asymmetric", false);
                    k.verticalAsymmetric = boolField (doc, kn, cn, "vertical_asymmetric", false);

                    if (c.type == wtrender::ComponentType::waveSource)
                    {
                        std::vector<uint8_t> bytes;
                        if (! decodeField (doc, doc.find (kn, "wave_data"), bytes, notes))
                            continue; // a Wave Source keyframe without usable data is dropped alone
                        const size_t samples = std::min<size_t> (bytes.size() / 4, wtrender::kFrameSize);
                        k.wave.resize (samples);
                        for (size_t i = 0; i < samples; ++i) // little endian IEEE float32
                        {
                            const uint32_t u = static_cast<uint32_t> (bytes[i * 4]) | (static_cast<uint32_t> (bytes[i * 4 + 1]) << 8)
                                             | (static_cast<uint32_t> (bytes[i * 4 + 2]) << 16) | (static_cast<uint32_t> (bytes[i * 4 + 3]) << 24);
                            float f;
                            std::memcpy (&f, &u, 4);
                            k.wave[i] = std::isfinite (f) ? f : 0.0f;
                        }
                    }
                    else if (c.type == wtrender::ComponentType::lineSource)
                    {
                        ParsedLfo lfo;
                        if (! parseLfoLine (doc, doc.find (kn, "line"), lfo, notes))
                        {
                            ++notes.badFields;
                            continue; // an invalid line: this keyframe is dropped
                        }
                        k.line = std::move (lfo.line);
                    }
                    c.keyframes.push_back (std::move (k));
                }
            group.components.push_back (std::move (c));
        }
    }
    return true;
}

std::vector<uint8_t> createWav16 (const int16_t* pcmLeft, const int16_t* pcmRight, size_t numFrames, int sampleRate, int channels)
{
    std::vector<uint8_t> b;
    if (pcmLeft == nullptr || numFrames == 0 || channels < 1 || channels > 2)
        return b;

    const uint32_t dataSize = static_cast<uint32_t> (numFrames * static_cast<size_t> (channels) * sizeof (int16_t));
    const uint32_t totalSize = 36 + dataSize;
    b.reserve (44 + dataSize);

    const auto put32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back (static_cast<uint8_t> (v >> (8 * i))); };
    const auto put16 = [&] (uint16_t v) { b.push_back (static_cast<uint8_t> (v)); b.push_back (static_cast<uint8_t> (v >> 8)); };
    const auto tag = [&] (const char* t) { for (int i = 0; i < 4; ++i) b.push_back (static_cast<uint8_t> (t[i])); };

    tag ("RIFF");
    put32 (totalSize);
    tag ("WAVE");

    tag ("fmt ");
    put32 (16);
    put16 (1); // PCM
    put16 (static_cast<uint16_t> (channels));
    put32 (static_cast<uint32_t> (sampleRate));
    put32 (static_cast<uint32_t> (sampleRate * channels * 2));
    put16 (static_cast<uint16_t> (channels * 2)); // block align
    put16 (16); // bits per sample

    tag ("data");
    put32 (dataSize);

    for (size_t i = 0; i < numFrames; ++i)
    {
        put16 (static_cast<uint16_t> (pcmLeft[i]));
        if (channels == 2)
            put16 (static_cast<uint16_t> (pcmRight != nullptr ? pcmRight[i] : pcmLeft[i]));
    }
    return b;
}

bool parseVitalSample (const json::Doc& doc, int node, ImportedSample& out, ContentNotes& notes)
{
    if (node < 0 || doc.nodes[static_cast<size_t> (node)].type != Type::object)
        return false;

    const int nameNode = doc.find (node, "name");
    std::string sname = stringOf (doc, nameNode, 255);
    if (sname.empty())
        sname = "sample";

    const double rawLen = numField (doc, node, -1, "length", 0.0);
    const double rawSr = numField (doc, node, -1, "sample_rate", 44100.0);
    const int sampleRate = static_cast<int> (clampd (rawSr, 1000.0, 192000.0));

    const int sn = doc.find (node, "samples");
    if (sn < 0)
        return false;

    std::vector<int16_t> pcmLeft;
    const json::Node& snNode = doc.nodes[static_cast<size_t> (sn)];
    if (snNode.type == Type::string)
    {
        std::vector<uint8_t> rawBytes;
        if (! decodeField (doc, sn, rawBytes, notes))
            return false;

        const size_t targetLen = rawLen > 0.0 ? static_cast<size_t> (rawLen) : 0;
        if (targetLen > 0 && rawBytes.size() >= targetLen * 4)
        {
            // float32 little endian
            pcmLeft.resize (targetLen);
            const float* fptr = reinterpret_cast<const float*> (rawBytes.data());
            for (size_t i = 0; i < targetLen; ++i)
                pcmLeft[i] = static_cast<int16_t> (std::lround (std::max (-1.0f, std::min (1.0f, fptr[i])) * 32767.0f));
        }
        else
        {
            // int16 little endian
            const size_t frames = targetLen > 0 ? std::min (targetLen, rawBytes.size() / 2) : (rawBytes.size() / 2);
            if (frames == 0)
                return false;
            pcmLeft.resize (frames);
            for (size_t i = 0; i < frames; ++i)
                pcmLeft[i] = static_cast<int16_t> (static_cast<uint16_t> (rawBytes[i * 2] | (rawBytes[i * 2 + 1] << 8)));
        }
    }
    else if (snNode.type == Type::array)
    {
        for (int e = snNode.first; e >= 0; e = doc.nodes[static_cast<size_t> (e)].next)
        {
            double v;
            if (numberOf (doc, e, v))
                pcmLeft.push_back (static_cast<int16_t> (std::lround (std::max (-1.0, std::min (1.0, v)) * 32767.0)));
        }
    }
    else
    {
        return false;
    }

    if (pcmLeft.empty())
        return false;

    std::vector<int16_t> pcmRight;
    const int stn = doc.find (node, "samples_stereo");
    if (stn >= 0)
    {
        const json::Node& stNode = doc.nodes[static_cast<size_t> (stn)];
        if (stNode.type == Type::string)
        {
            std::vector<uint8_t> rawBytes;
            if (decodeField (doc, stn, rawBytes, notes))
            {
                if (rawBytes.size() >= pcmLeft.size() * 4)
                {
                    pcmRight.resize (pcmLeft.size());
                    const float* fptr = reinterpret_cast<const float*> (rawBytes.data());
                    for (size_t i = 0; i < pcmLeft.size(); ++i)
                        pcmRight[i] = static_cast<int16_t> (std::lround (std::max (-1.0f, std::min (1.0f, fptr[i])) * 32767.0f));
                }
                else if (rawBytes.size() >= pcmLeft.size() * 2)
                {
                    pcmRight.resize (pcmLeft.size());
                    for (size_t i = 0; i < pcmLeft.size(); ++i)
                        pcmRight[i] = static_cast<int16_t> (static_cast<uint16_t> (rawBytes[i * 2] | (rawBytes[i * 2 + 1] << 8)));
                }
            }
        }
        else if (stNode.type == Type::array)
        {
            for (int e = stNode.first; e >= 0; e = doc.nodes[static_cast<size_t> (e)].next)
            {
                double v;
                if (numberOf (doc, e, v))
                    pcmRight.push_back (static_cast<int16_t> (std::lround (std::max (-1.0, std::min (1.0, v)) * 32767.0)));
            }
        }
    }

    const int channels = (! pcmRight.empty() && pcmRight.size() == pcmLeft.size()) ? 2 : 1;
    out.name = std::move (sname);
    out.numFrames = static_cast<int> (pcmLeft.size());
    out.sampleRate = sampleRate;
    out.channels = channels;
    out.wavBytes = createWav16 (pcmLeft.data(), channels == 2 ? pcmRight.data() : nullptr, pcmLeft.size(), sampleRate, channels);
    out.sha256 = Sha256::hashBufferHex (out.wavBytes.data(), out.wavBytes.size());
    return true;
}
} // namespace vitalimport

UserWavetableStore::UserWavetableStore (const std::string& packDirectory)
{
    std::filesystem::path p (packDirectory);
    if (p.filename() == "user.imported-vital" || std::filesystem::exists (p / "contentpack.json"))
        packDir_ = p.string();
    else
        packDir_ = (p / "user.imported-vital").string();

    std::error_code ec;
    std::filesystem::create_directories (packDir_, ec);

    const std::filesystem::path manifest = std::filesystem::path (packDir_) / "contentpack.json";
    if (std::filesystem::exists (manifest, ec))
    {
        std::ifstream f (manifest.string(), std::ios::binary);
        if (f)
        {
            std::string content ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
            ContentPackInfo loaded;
            if (parseContentPackManifest (content.data(), content.size(), loaded))
                info_ = std::move (loaded);
        }
    }

    if (info_.id.empty())
    {
        info_.schemaVersion = 1;
        info_.id = "user.imported-vital";
        info_.name = "Imported Vital Wavetables";
        info_.version = "1.0.0";
        info_.license = "user-supplied";
        info_.attribution = "user-supplied";
        info_.kind = "wavetables";
        info_.requiresPlugin = ">=0.1.0";
        info_.description = "User-imported wavetables from Vital presets";
        writeContentPackManifest (packDir_, info_);
    }
}

bool UserWavetableStore::hasWavetable (const std::string& sha256)
{
    if (sha256.empty())
        return false;
    for (const auto& f : info_.files)
        if (f.sha256 == sha256)
            return true;
    return false;
}

bool UserWavetableStore::addWavetable (const ImportedWavetable& wt, std::string& packOut, std::string& fileOut)
{
    packOut = info_.id.empty() ? "user.imported-vital" : info_.id;

    for (const auto& f : info_.files)
    {
        if (f.sha256 == wt.sha256)
        {
            fileOut = f.path;
            return true;
        }
    }

    std::string safeName;
    for (char c : wt.name)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
            safeName += c;
        else if (c == ' ')
            safeName += '_';
    }
    if (safeName.empty())
        safeName = "table";

    const std::string shortHash = wt.sha256.size() >= 12 ? wt.sha256.substr (0, 12) : wt.sha256;
    const std::string relPath = "tables/" + safeName + "_" + shortHash + ".sbwt";

    std::filesystem::path full = std::filesystem::path (packDir_) / relPath;
    std::error_code ec;
    std::filesystem::create_directories (full.parent_path(), ec);

    std::ofstream out (full.string(), std::ios::binary);
    if (! out)
        return false;

    const size_t byteSize = wt.frames.size() * sizeof (int16_t);
    out.write (reinterpret_cast<const char*> (wt.frames.data()), static_cast<std::streamsize> (byteSize));
    out.close();
    if (! out.good())
        return false;

    ContentPackFile cpf;
    cpf.path = relPath;
    cpf.sha256 = wt.sha256;
    cpf.size = byteSize;
    info_.files.push_back (std::move (cpf));

    writeContentPackManifest (packDir_, info_);
    fileOut = relPath;
    return true;
}

bool UserWavetableStore::hasSample (const std::string& sha256)
{
    if (sha256.empty())
        return false;
    for (const auto& f : info_.files)
        if (f.sha256 == sha256)
            return true;
    return false;
}

bool UserWavetableStore::addSample (const ImportedSample& s, std::string& packOut, std::string& fileOut)
{
    packOut = info_.id.empty() ? "user.imported-vital" : info_.id;

    for (const auto& f : info_.files)
    {
        if (f.sha256 == s.sha256)
        {
            fileOut = f.path;
            return true;
        }
    }

    std::string safeName;
    for (char c : s.name)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
            safeName += c;
        else if (c == ' ')
            safeName += '_';
    }
    if (safeName.empty())
        safeName = "sample";

    const std::string shortHash = s.sha256.size() >= 12 ? s.sha256.substr (0, 12) : s.sha256;
    const std::string relPath = "samples/" + safeName + "_" + shortHash + ".wav";

    std::filesystem::path full = std::filesystem::path (packDir_) / relPath;
    std::error_code ec;
    std::filesystem::create_directories (full.parent_path(), ec);

    std::ofstream out (full.string(), std::ios::binary);
    if (! out)
        return false;

    out.write (reinterpret_cast<const char*> (s.wavBytes.data()), static_cast<std::streamsize> (s.wavBytes.size()));
    out.close();
    if (! out.good())
        return false;

    ContentPackFile cpf;
    cpf.path = relPath;
    cpf.sha256 = s.sha256;
    cpf.size = s.wavBytes.size();
    info_.files.push_back (std::move (cpf));

    writeContentPackManifest (packDir_, info_);
    fileOut = relPath;
    return true;
}
} // namespace synthyboy
