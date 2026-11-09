// SynthyBoy - core: conversions between samples, rendered audio, wavetables and LFO shapes (see wave_convert.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "synthyboy/wave_convert.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <queue>

#include "synthyboy/params.h"
#include "synthyboy/shapes/shape.h"
#include "synthyboy/state.h"
#include "synthyboy/user_content.h"

namespace synthyboy::convert
{
namespace
{
constexpr double kTwoPow = 1.0 / 12.0;

void say (std::string* error, const char* text)
{
    if (error != nullptr)
        *error = text;
}

double keyHz (int key) noexcept
{
    return 440.0 * std::pow (2.0, (static_cast<double> (key) - 69.0) * kTwoPow);
}

/** Catmull-Rom interpolation of x at the (fractional) position `pos`; samples outside the range count as 0. */
float cubicAt (const std::vector<float>& x, double pos) noexcept
{
    const auto at = [&x] (long i) { return i >= 0 && i < static_cast<long> (x.size()) ? x[static_cast<size_t> (i)] : 0.0f; };
    const long i = static_cast<long> (std::floor (pos));
    const float t = static_cast<float> (pos - static_cast<double> (i));
    const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    const float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c = -0.5f * y0 + 0.5f * y2;
    return ((a * t + b) * t + c) * t + y1;
}

/** The period (in samples) of a periodic signal near `expected`: the maximum of the normalised autocorrelation within +-10 %, refined parabolically. */
double measurePeriod (const std::vector<float>& x, size_t from, double expected)
{
    const size_t window = static_cast<size_t> (std::ceil (2.0 * expected));
    const long lo = static_cast<long> (std::floor (0.9 * expected)), hi = static_cast<long> (std::ceil (1.1 * expected));
    if (from + window + static_cast<size_t> (hi) + 2 > x.size() || lo < 2)
        return expected;
    std::vector<double> score (static_cast<size_t> (hi - lo + 1), 0.0);
    double energy = 0.0;
    for (size_t i = 0; i < window; ++i)
        energy += static_cast<double> (x[from + i]) * x[from + i];
    if (energy < 1.0e-12)
        return expected;
    for (long lag = lo; lag <= hi; ++lag)
    {
        double sum = 0.0, other = 0.0;
        for (size_t i = 0; i < window; ++i)
        {
            const double a = x[from + i], b = x[from + i + static_cast<size_t> (lag)];
            sum += a * b;
            other += b * b;
        }
        score[static_cast<size_t> (lag - lo)] = sum / std::sqrt (energy * other + 1.0e-30);
    }
    size_t best = 0;
    for (size_t i = 1; i < score.size(); ++i)
        if (score[i] > score[best])
            best = i;
    double offset = 0.0;
    if (best > 0 && best + 1 < score.size())
    {
        const double a = score[best - 1], b = score[best], c = score[best + 1];
        const double denom = a - 2.0 * b + c;
        if (std::fabs (denom) > 1.0e-12)
            offset = std::clamp (0.5 * (a - c) / denom, -1.0, 1.0);
    }
    return static_cast<double> (lo) + static_cast<double> (best) + offset;
}

void monoOf (const OfflineAudio& audio, std::vector<float>& mono, int channel, bool mix)
{
    mono.resize (audio.left.size());
    for (size_t i = 0; i < mono.size(); ++i)
        mono[i] = mix ? 0.5f * (audio.left[i] + audio.right[i]) : (channel == 1 ? audio.right[i] : audio.left[i]);
}
} // namespace

// ---- WP-14 -----------------------------------------------------------------------------------------------------------------------------------

const std::vector<ImportChoice>& importChoices()
{
    static const std::vector<ImportChoice> choices = {
        { "Constant framesize (Pitch Average)", wtedit::ImportMode::PitchAverage },
        { "Dynamic pitch - zero snap", wtedit::ImportMode::PitchDetectZeroSnap },
        { "Dynamic pitch - follow", wtedit::ImportMode::PitchDetectFollow },
        { "Frequency estimation", wtedit::ImportMode::ByFrameSize },
        { "FFT 256", wtedit::ImportMode::Fft256 },
        { "FFT 512", wtedit::ImportMode::Fft512 },
        { "FFT 1024", wtedit::ImportMode::Fft1024 },
        { "FFT 2048", wtedit::ImportMode::Fft2048 },
        { "Single cycle", wtedit::ImportMode::FixedSingleCycle },
    };
    return choices;
}

bool sampleRegionToWavetable (const float* mono, size_t frames, double sampleRate, float startNorm, float endNorm, const wtedit::ImportOptions& options,
                              const std::string& name, wtedit::Wavetable& out, std::string* error)
{
    if (mono == nullptr || frames == 0 || ! (sampleRate > 0.0))
    {
        say (error, "the sample is empty");
        return false;
    }
    const float s = std::clamp (startNorm, 0.0f, 1.0f), e = std::clamp (endNorm, 0.0f, 1.0f);
    const size_t first = static_cast<size_t> (std::floor (static_cast<double> (s) * static_cast<double> (frames)));
    const size_t last = std::min (frames, static_cast<size_t> (std::ceil (static_cast<double> (e) * static_cast<double> (frames))));
    if (last <= first || last - first < 32)
    {
        say (error, "the START .. END region is too short to make a wavetable");
        return false;
    }
    wtedit::Wavetable table (name.empty() ? std::string ("Sample") : name);
    table.setSampleRate (sampleRate);
    if (! wtedit::importAudioSamples (mono + first, last - first, sampleRate, options, table) || table.numFrames() == 0)
    {
        say (error, "the region gave no wavetable frame (try another import option)");
        return false;
    }
    table.setName (name.empty() ? std::string ("Sample") : name);
    out = std::move (table);
    return true;
}

// ---- WP-15 -----------------------------------------------------------------------------------------------------------------------------------

bool renderResample (const OfflineRunner& run, const ResampleOptions& options, OfflineAudio& audio)
{
    if (! run || options.bars < 1 || options.bars > 16 || ! (options.bpm >= 20.0 && options.bpm <= 400.0))
        return false;
    OfflineRequest request;
    request.sampleRate = options.sampleRate;
    request.ultra = options.ultra;
    request.bpm = options.bpm;
    request.tailSeconds = options.tailSeconds;
    OfflineStep step;
    step.seconds = static_cast<double> (options.bars) * 4.0 * 60.0 / options.bpm;
    OfflineNote note;
    note.key = options.key;
    note.velocity = options.velocity;
    note.startSeconds = 0.0;
    note.lengthSeconds = step.seconds;
    step.notes.push_back (note);
    request.steps.push_back (std::move (step));
    return run (request, audio) && ! audio.left.empty();
}

bool audioToWavetables (const OfflineAudio& audio, const wtedit::ImportOptions& options, const std::string& name, wtedit::Wavetable& left, wtedit::Wavetable* right,
                        std::string* error)
{
    if (audio.left.empty() || audio.left.size() != audio.right.size())
    {
        say (error, "nothing was rendered");
        return false;
    }
    float peak = 0.0f;
    for (size_t i = 0; i < audio.left.size(); ++i)
        peak = std::max ({ peak, std::fabs (audio.left[i]), std::fabs (audio.right[i]) });
    if (peak < 1.0e-6f)
    {
        say (error, "the patch is silent for this note");
        return false;
    }
    std::vector<float> mono;
    if (right != nullptr)
        monoOf (audio, mono, 0, false); // Osc A: the left channel
    else
        monoOf (audio, mono, options.channel, options.channel == 0); // one oscillator: the mono mix, or the right channel when asked
    wtedit::Wavetable first (name);
    if (! wtedit::importAudioSamples (mono.data(), mono.size(), audio.sampleRate, options, first) || first.numFrames() == 0)
    {
        say (error, "the render gave no wavetable frame");
        return false;
    }
    first.setName (name);
    if (right != nullptr)
    {
        monoOf (audio, mono, 1, false);
        wtedit::Wavetable second (name);
        if (! wtedit::importAudioSamples (mono.data(), mono.size(), audio.sampleRate, options, second) || second.numFrames() == 0)
        {
            say (error, "the right channel gave no wavetable frame");
            return false;
        }
        second.setName (name);
        *right = std::move (second);
    }
    left = std::move (first);
    return true;
}

bool renderOscWarp (const OfflineRunner& run, const ParamValues* values, const WarpRenderOptions& options, const std::string& name, wtedit::Wavetable& out, std::string* error,
                    float* levelScale)
{
    if (! run || values == nullptr)
    {
        say (error, "the engine cannot render offline");
        return false;
    }
    if (options.osc < 1 || options.osc > 3 || options.frames < 2 || options.frames > static_cast<int> (wtedit::kMaxFrames))
    {
        say (error, "no such oscillator");
        return false;
    }
    const std::string o = "osc" + std::to_string (options.osc) + ".";
    const auto value = [values] (const std::string& key) -> float
    {
        const int i = findParamIndex (hashKey (key.c_str()));
        return i >= 0 ? values->plain[i] : 0.0f;
    };
    if (static_cast<int> (std::lround (value (o + "type"))) != kOscTypeWavetable)
    {
        say (error, "Render OSC Warp needs a wavetable oscillator");
        return false;
    }

    // the oscillator alone, dry and unmodulated: only the warp moves
    OfflineRequest request;
    request.sampleRate = options.sampleRate;
    request.ultra = options.ultra;
    request.values = values;
    const auto set = [&request] (const std::string& key, float v)
    {
        if (const auto* info = findParamByKey (key.c_str()))
            request.overrides.push_back ({ info->id, v });
    };
    const auto* table = paramTable();
    for (size_t i = 0; i < paramCount(); ++i)
    {
        const std::string key = table[i].key;
        const bool isOn = key.size() > 3 && key.compare (key.size() - 3, 3, ".on") == 0;
        if (isOn && key != "master.mpe.on" && key.find (".remap.") == std::string::npos && key != o + "on")
            request.overrides.push_back ({ table[i].id, 0.0f });
        else if (key.size() == 12 && key.compare (0, 3, "mod") == 0 && key.compare (5, 7, ".amount") == 0)
            request.overrides.push_back ({ table[i].id, 0.0f });
    }
    set (o + "on", 1.0f);
    set (o + "level", 100.0f);
    set (o + "pan", 0.0f);
    set (o + "transpose", 0.0f);
    set (o + "tune", 0.0f);
    set (o + "unison.voices", 1.0f);
    set (o + "random_phase", 0.0f);
    set (o + "phase", 0.0f);
    set (o + "frame_spread", 0.0f);
    set (o + "warp2.type", 0.0f);
    set ("master.transpose", 0.0f);
    set ("master.tune", 0.0f);
    set ("env1.delay", 0.0f);
    set ("env1.attack", 0.0f);
    set ("env1.hold", 0.0f);
    set ("env1.decay", 0.0f);
    set ("env1.sustain", 100.0f);
    set ("env1.release", 0.0f);
    set ("voice.same_note", 0.0f);
    if (options.rawFrame)
    {
        set (o + "warp.type", 0.0f);
        set (o + "morph.type", 0.0f);
    }

    const double period0 = options.sampleRate / keyHz (options.key);
    const double stepSeconds = 6.5 * period0 / options.sampleRate;
    const std::string warpKey = o + "warp.amount";
    const auto* warpInfo = findParamByKey (warpKey.c_str());
    if (warpInfo == nullptr)
    {
        say (error, "no such oscillator");
        return false;
    }
    const auto oscWavetable = findParamByKey ((o + "wavetable").c_str());
    const auto oscWarpType = findParamByKey ((o + "warp.type").c_str());
    const auto oscFrame = findParamByKey ((o + "frame").c_str());
    const auto oscMorph = findParamByKey ((o + "morph.type").c_str());
    if (oscWavetable == nullptr || oscWarpType == nullptr || oscFrame == nullptr || oscMorph == nullptr)
    {
        say (error, "no such oscillator");
        return false;
    }
    // step 0 calibrates the level: the built-in sine (unit amplitude) through the same chain gives the gain of the chain (oscillator level, voice, master volume),
    // so that a table value of 1.0 plays back as the same level the render had
    for (int f = -1; f < options.frames; ++f)
    {
        OfflineStep step;
        step.seconds = stepSeconds;
        if (f < 0)
        {
            step.overrides.push_back ({ oscWavetable->id, static_cast<float> (kWavetableSine) });
            step.overrides.push_back ({ oscWarpType->id, 0.0f });
            step.overrides.push_back ({ oscMorph->id, 0.0f });
            step.overrides.push_back ({ oscFrame->id, 0.0f });
            step.overrides.push_back ({ warpInfo->id, 0.0f });
        }
        else
        {
            step.overrides.push_back ({ oscWavetable->id, value (o + "wavetable") });
            step.overrides.push_back ({ oscWarpType->id, options.rawFrame ? 0.0f : value (o + "warp.type") });
            step.overrides.push_back ({ oscMorph->id, options.rawFrame ? 0.0f : value (o + "morph.type") });
            step.overrides.push_back ({ oscFrame->id, value (o + "frame") });
            step.overrides.push_back ({ warpInfo->id, 100.0f * static_cast<float> (f) / static_cast<float> (options.frames - 1) });
        }
        OfflineNote note;
        note.key = options.key;
        note.velocity = 1.0f;
        note.lengthSeconds = stepSeconds;
        step.notes.push_back (note);
        request.steps.push_back (std::move (step));
    }
    OfflineAudio audio;
    if (! run (request, audio) || audio.stepStart.size() != request.steps.size())
    {
        say (error, "the engine could not render");
        return false;
    }

    // one cycle per step: the period is measured once (on the loudest step), then every step is read at the same phase
    std::vector<float> mono;
    monoOf (audio, mono, 0, true);
    const size_t stepLen = static_cast<size_t> (std::llround (stepSeconds * options.sampleRate));
    size_t loudest = 1;
    double loudestEnergy = -1.0;
    for (size_t s = 1; s < audio.stepStart.size(); ++s)
    {
        const size_t from = audio.stepStart[s] + static_cast<size_t> (2.0 * period0), to = std::min (mono.size(), audio.stepStart[s] + stepLen);
        double energy = 0.0;
        for (size_t i = from; i < to; ++i)
            energy += static_cast<double> (mono[i]) * mono[i];
        if (energy > loudestEnergy)
        {
            loudestEnergy = energy;
            loudest = s;
        }
    }
    if (loudestEnergy < 1.0e-9)
    {
        say (error, "the oscillator is silent");
        return false;
    }
    const double period = measurePeriod (mono, audio.stepStart[loudest] + static_cast<size_t> (1.5 * period0), period0);
    // the gain of the chain: RMS of three cycles of the calibration sine (RMS of a unit sine = 1 / sqrt 2)
    double sineEnergy = 0.0;
    const size_t sineFrom = audio.stepStart[0] + static_cast<size_t> (std::llround (2.0 * period)), sineLen = static_cast<size_t> (std::llround (3.0 * period));
    for (size_t i = sineFrom; i < sineFrom + sineLen && i < mono.size(); ++i)
        sineEnergy += static_cast<double> (mono[i]) * mono[i];
    const double chainGain = std::sqrt (2.0 * sineEnergy / static_cast<double> (sineLen));
    if (chainGain < 1.0e-6)
    {
        say (error, "the oscillator is silent");
        return false;
    }
    wtedit::Wavetable result (name.empty() ? std::string ("Warp") : name);
    result.reserve (static_cast<size_t> (options.frames));
    result.clear();
    for (int f = 0; f < options.frames; ++f)
    {
        const size_t start = audio.stepStart[static_cast<size_t> (f) + 1];
        const double t0 = static_cast<double> (start) + 2.0 * period;
        wtedit::Frame frame;
        for (size_t j = 0; j < wtedit::kWaveformSize; ++j)
            frame[j] = static_cast<float> (cubicAt (mono, t0 + static_cast<double> (j) * period / static_cast<double> (wtedit::kWaveformSize)) / chainGain);
        if (f == 0)
            result.setFrame (0, frame);
        else
            result.insertFrame (result.numFrames(), frame);
    }
    result.setSampleRate (options.sampleRate);
    // a value of 1.0 is full scale and a 16-bit table file cannot hold more: a peak above 1 (or the wish to normalise) scales the whole table by one gain
    float peak = 0.0f;
    for (size_t f = 0; f < result.numFrames(); ++f)
        for (size_t j = 0; j < wtedit::kWaveformSize; ++j)
            peak = std::max (peak, std::fabs (result.getFrame (f)[j]));
    float scale = 1.0f;
    if (peak > 0.0f && (options.normalise || peak > 1.0f))
        scale = 1.0f / peak;
    if (scale != 1.0f)
        for (size_t f = 0; f < result.numFrames(); ++f)
            for (size_t j = 0; j < wtedit::kWaveformSize; ++j)
                result.getFrame (f)[j] *= scale;
    if (levelScale != nullptr)
        *levelScale = scale;
    out = std::move (result);
    return true;
}

std::vector<std::pair<std::string, float>> warpBakeParameterChanges (int osc, float warpAmountPercent, float levelScale, float currentLevelPercent)
{
    const std::string o = "osc" + std::to_string (osc) + ".";
    std::vector<std::pair<std::string, float>> changes = { { o + "warp.type", 0.0f }, { o + "morph.type", 0.0f }, { o + "remap.on", 0.0f }, { o + "frame", std::clamp (warpAmountPercent, 0.0f, 100.0f) } };
    if (levelScale > 0.0f && levelScale < 0.999f) // the table had to be scaled down to fit: the oscillator level makes up for it as far as it goes
        changes.emplace_back (o + "level", std::min (100.0f, currentLevelPercent / levelScale));
    return changes;
}

bool renderNote (const OfflineRunner& run, const NoteRender& note, OfflineAudio& audio)
{
    if (! run)
        return false;
    OfflineRequest request;
    request.sampleRate = note.sampleRate;
    request.ultra = note.ultra;
    request.tailSeconds = note.tailSeconds;
    OfflineStep step;
    step.seconds = std::clamp (note.heldSeconds, 0.05, 60.0);
    OfflineNote n;
    n.key = note.key;
    n.velocity = note.velocity;
    n.lengthSeconds = step.seconds;
    step.notes.push_back (n);
    request.steps.push_back (std::move (step));
    return run (request, audio) && ! audio.left.empty();
}

std::vector<uint8_t> audioToWav (const OfflineAudio& audio, int bits)
{
    if (audio.left.empty() || audio.left.size() != audio.right.size() || audio.sampleRate <= 0.0)
        return {};
    float peak = 0.0f;
    for (size_t i = 0; i < audio.left.size(); ++i)
        peak = std::max ({ peak, std::fabs (audio.left[i]), std::fabs (audio.right[i]) });
    const float gain = peak > 1.0f ? 1.0f / peak : 1.0f;
    std::vector<float> l (audio.left), r (audio.right);
    if (gain != 1.0f)
        for (size_t i = 0; i < l.size(); ++i)
        {
            l[i] *= gain;
            r[i] *= gain;
        }
    return encodeWav (l.data(), r.data(), l.size(), static_cast<int> (std::lround (audio.sampleRate)), bits);
}

// ---- WP-16 -----------------------------------------------------------------------------------------------------------------------------------

wtedit::Frame interpolatedFrame (const wtedit::Wavetable& table, float position)
{
    if (table.numFrames() == 0)
        return {};
    const float p = std::clamp (position, 0.0f, 1.0f) * static_cast<float> (table.numFrames() - 1);
    const size_t i = static_cast<size_t> (std::floor (p));
    const float t = p - static_cast<float> (i);
    if (i + 1 >= table.numFrames() || t <= 0.0f)
        return table.getFrame (std::min (i, table.numFrames() - 1));
    wtedit::Frame out;
    const auto& a = table.getFrame (i);
    const auto& b = table.getFrame (i + 1);
    for (size_t j = 0; j < wtedit::kWaveformSize; ++j)
        out[j] = a[j] + t * (b[j] - a[j]);
    return out;
}

bool frameToLfoShape (const wtedit::Frame& frame, int lfo, ContentLfoShape& out, float tolerance, int maxPoints, float* error)
{
    if (maxPoints < 2)
        return false;
    maxPoints = std::min (maxPoints, kMaxLfoShapePoints);
    constexpr int n = static_cast<int> (wtedit::kWaveformSize);
    float peak = 1.0f;
    for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
        peak = std::max (peak, std::fabs (frame[i]));
    std::vector<float> y (static_cast<size_t> (n) + 1);
    for (int i = 0; i < n; ++i)
        y[static_cast<size_t> (i)] = std::isfinite (frame[static_cast<size_t> (i)]) ? 0.5f + 0.5f * frame[static_cast<size_t> (i)] / peak : 0.5f;
    y[static_cast<size_t> (n)] = y[0]; // the cycle closes on its first sample

    struct Segment
    {
        float err;
        int lo, hi, at;
        bool operator< (const Segment& other) const noexcept { return err < other.err; }
    };
    const auto worst = [&y] (int lo, int hi) -> Segment
    {
        Segment s { 0.0f, lo, hi, lo };
        for (int i = lo + 1; i < hi; ++i)
        {
            const float t = static_cast<float> (i - lo) / static_cast<float> (hi - lo);
            const float d = std::fabs (y[static_cast<size_t> (i)] - (y[static_cast<size_t> (lo)] + t * (y[static_cast<size_t> (hi)] - y[static_cast<size_t> (lo)])));
            if (d > s.err)
            {
                s.err = d;
                s.at = i;
            }
        }
        return s;
    };
    std::vector<int> keep { 0, n };
    std::priority_queue<Segment> queue;
    queue.push (worst (0, n));
    float reached = 0.0f;
    while (! queue.empty())
    {
        const Segment top = queue.top();
        queue.pop();
        if (top.err <= tolerance || static_cast<int> (keep.size()) >= maxPoints)
        {
            reached = std::max (reached, top.err);
            continue; // the segments left are all at or below this error (it was the largest)
        }
        keep.push_back (top.at);
        queue.push (worst (top.lo, top.at));
        queue.push (worst (top.at, top.hi));
    }
    while (! queue.empty()) // the rest that stayed above the budget
    {
        reached = std::max (reached, queue.top().err);
        queue.pop();
    }
    std::sort (keep.begin(), keep.end());
    ContentLfoShape shape;
    shape.lfo = lfo;
    shape.smooth = false;
    shape.loopPoint = -1;
    shape.name = "Wavetable frame";
    for (const int i : keep)
        shape.points.emplace_back (static_cast<float> (i) / static_cast<float> (n), y[static_cast<size_t> (i)], 0.0f);
    out = std::move(shape);
    if (error != nullptr)
        *error = reached;
    return true;
}

ContentLfoShape builtInLfoShape (int lfo, int builtInShape)
{
    constexpr float kTwoPi = 6.28318530717958647692f;
    ContentLfoShape s;
    s.lfo = lfo;
    s.name = "Custom";
    const auto add = [&s] (float x, float y, float power = 0.0f) { s.points.emplace_back (x, y, power); };
    const auto sampled = [&s] (int n, float (*f) (float))
    {
        for (int i = 0; i <= n; ++i)
        {
            const float x = static_cast<float> (i) / static_cast<float> (n);
            s.points.emplace_back (x, std::clamp (f (x), 0.0f, 1.0f), 0.0f);
        }
    };
    switch (builtInShape)
    {
        case 0:  sampled (32, [] (float x) { return 0.5f + 0.5f * std::sin (6.28318530717958647692f * x); }); break;
        case 1:  add (0.0f, 0.0f); add (0.5f, 1.0f); add (1.0f, 0.0f); break;
        case 2:  add (0.0f, 0.0f); add (1.0f, 1.0f); break;
        case 3:  add (0.0f, 1.0f); add (1.0f, 0.0f); break;
        case 4:  add (0.0f, 1.0f); add (0.5f, 1.0f); add (0.5f, 0.0f); add (1.0f, 0.0f); break;
        case 5:  add (0.0f, 1.0f); add (0.25f, 1.0f); add (0.25f, 0.0f); add (1.0f, 0.0f); break;
        case 6:
        case 7:
            for (int i = 0; i < 4; ++i)
            {
                const float level = static_cast<float> (builtInShape == 6 ? i : 3 - i) / 3.0f;
                add (static_cast<float> (i) / 4.0f, level);
                add (static_cast<float> (i + 1) / 4.0f, level);
            }
            break;
        case 8:
        {
            static constexpr float steps[8] = { 0.4f, -0.7f, 0.9f, -0.2f, -0.9f, 0.6f, 0.1f, -0.5f };
            for (int i = 0; i < 8; ++i)
            {
                add (static_cast<float> (i) / 8.0f, 0.5f + 0.5f * steps[i]);
                add (static_cast<float> (i + 1) / 8.0f, 0.5f + 0.5f * steps[i]);
            }
            break;
        }
        case 9:  add (0.0f, 0.0f, 5.0f); add (1.0f, 1.0f); break;   // power 5: (e^(5x) - 1) / (e^5 - 1), the built-in
        case 10: add (0.0f, 1.0f, -5.0f); add (1.0f, 0.0f); break;
        case 11: sampled (16, [] (float x) { return std::sin (3.14159265358979f * x); }); break;
        default: add (0.0f, 0.5f); add (1.0f, 0.5f); break;
    }
    (void) kTwoPi;
    return s;
}

void lfoShapeToFrame (const ContentLfoShape& shape, wtedit::Frame& out)
{
    shapes::ShapeData d;
    d.numPoints = std::min<size_t> (shape.points.size(), shapes::ShapeData::kMaxPoints);
    for (size_t i = 0; i < d.numPoints; ++i)
    {
        d.points[i].x = shape.points[i].x;
        d.points[i].y = shape.points[i].y;
        d.points[i].curve = shape.points[i].power;
        d.points[i].smooth = shape.smooth;
    }
    d.sanitize();
    for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
        out[i] = std::clamp (2.0f * d.evaluate (static_cast<float> (i) / static_cast<float> (wtedit::kWaveformSize)) - 1.0f, -1.0f, 1.0f);
}
} // namespace synthyboy::convert
