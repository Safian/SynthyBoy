// SynthyBoy - UI: Filter models metadata, response math, input routing logic and Serum type catalog.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace synthyboy::ui
{

/** Total number of filter models in params.json. */
inline constexpr int kNumFilterModels = 27;

/**
 * Returns the contextual 4th "Var" parameter label for a given filter model index and optional style index.
 * Matches docs/ui-design/13-new-keys.md section 4.11, CATALOGUE.md and Serum manual p.135-139.
 */
inline const char* getVarLabelForModel (int modelIndex, int styleIndex = 0) noexcept
{
    switch (modelIndex)
    {
        case 8:  return "FREQ";    // Multi SVF (cutoff of second SVF)
        case 9:  return "FAT";     // Slope (resonance path saturation)
        case 10: return "DB";      // EQ 6 dB (pass-band gain)
        case 11: return styleIndex == 1 ? "SPREAD" : "";  // Ring Mod (carrier 2 spread on x2)
        case 12: return "";        // Sample Hold (no Var)
        case 13: return "DAMP";    // Smear (HF feedback damping)
        case 14: return "BOEUF";   // French LP (second resonance)
        case 15: return "THRU";    // Add Bass (phase-rotated dry)
        case 16: return "SCREAM";  // Scream (feedback loop cutoff)
        case 17: return "MORPH";   // New Wsp (LPF / Notch / HPF)
        case 18: return "";        // DJ Mixer (no Var)
        case 19: return "STAGES";  // Diffusor (all-pass stages)
        case 20: return "SMOOTH";  // EMS Ladder (cutoff slew rate)
        case 21: return "SMOOTH";  // PZ SVF (cutoff slew rate)
        case 22: return "FAT";     // Normal (12 styles: MG Low 12/24, Low 12/24, High 12/24, Band 12/24, Peak 12/24, Notch 12/24)
        case 23: return "MORPH";   // Multi Morph (LBH, LPH, LNH, BPN)
        case 24:                   // Flanges (9 styles)
            if (styleIndex < 3) return "LP FREQ";
            if (styleIndex < 6) return "HP FREQ";
            return "HL WID";
        case 25:                   // Misc (12 styles)
            if (styleIndex < 3) return "DB";       // Low/Band/High EQ 12
            if (styleIndex == 3) return "";        // German LP (no Var)
            if (styleIndex <= 6) return "FORMNT";  // Formant I, II, III
            if (styleIndex == 7) return "WIDTH";   // Bandreject
            if (styleIndex <= 11) return "COMBFRQ";// Dist.Comb 1/2 LP/BP
            return "";
        case 26:                   // New (6 styles)
            if (styleIndex < 2) return "SMOOTH";  // MG Ladder, Acid Ladder
            if (styleIndex == 2) return "PAIN";   // MG Dirty
            if (styleIndex == 3) return "FRQ2";   // Comb 2
            if (styleIndex == 4) return "MIX";    // Exp MM
            return "";                            // Exp BPF (no Var)
        default: return "";        // Models 0..7 use drive, comb_offset, formant.*
    }
}

/** Returns the model display name from index (0..26). */
inline const char* getFilterModelName (int modelIndex) noexcept
{
    static const char* const kNames[kNumFilterModels] = {
        "Analog", "Dirty", "Ladder", "Digital", "Diode", "Formant", "Comb", "Phaser",
        "Multi SVF", "Slope", "EQ 6 dB", "Ring Mod", "Sample Hold", "Smear",
        "French LP", "Add Bass", "Scream", "New Wsp", "DJ Mixer", "Diffusor",
        "EMS Ladder", "PZ SVF",
        "Normal", "Multi Morph", "Flanges", "Misc", "New"
    };
    if (modelIndex >= 0 && modelIndex < kNumFilterModels)
        return kNames[modelIndex];
    return "Unknown";
}

/** Returns the list of style names for a given model. */
inline std::vector<std::string> getStylesForModel (int modelIndex)
{
    switch (modelIndex)
    {
        case 0: // Analog
        case 1: // Dirty
        case 2: // Ladder
        case 3: // Digital
            return { "Style A (12 dB)", "Style B (24 dB)", "Style C (notch swap)", "Style D (dual notch)", "Style E (band peak notch)", "Style F (shelf)" };
        case 4: // Diode
            return { "Style A (12 dB)", "Style B (24 dB)" };
        case 5: // Formant
            return { "A-O-I-E", "A-I-U-O", "Vocal Tract" };
        case 6: // Comb
            return { "Comb", "+Flange", "-Flange", "Low-High Comb", "+Flange Spread", "-Flange Spread" };
        case 7: // Phaser
            return { "Positive Feedback", "Negative Feedback" };
        case 8: // Multi SVF
            return { "LH", "LB", "LP", "LN", "HB", "HP", "HN", "BP", "BN", "PP", "PN", "NN" };
        case 9: // Slope
            return { "MG Low 6", "MG Low 18", "Low 6", "Low 18", "High 6", "High 18" };
        case 10: // EQ 6 dB
            return { "Low EQ 6", "Band EQ 6", "High EQ 6" };
        case 11: // Ring Mod
            return { "Ring Mod", "Ring Mod x2" };
        case 12: // Sample Hold
            return { "SampHold", "SampHold-" };
        case 13: // Smear
            return { "Combs", "Allpasses", "Reverb" };
        case 14: // French LP
            return { "French LP" };
        case 15: // Add Bass
            return { "Add Bass" };
        case 16: // Scream
            return { "Scream LP", "Scream BP" };
        case 17: // New Wsp
            return { "New Wsp" };
        case 18: // DJ Mixer
            return { "DJ Mixer Xfer" };
        case 19: // Diffusor
            return { "Diffusor" };
        case 20: // EMS Ladder
            return { "EMS Ladder" };
        case 21: // PZ SVF
            return { "PZ SVF" };
        case 22: // Normal (12 styles)
            return { "MG Low 12", "MG Low 24", "Low 12", "Low 24", "High 12", "High 24",
                     "Band 12", "Band 24", "Peak 12", "Peak 24", "Notch 12", "Notch 24" };
        case 23: // Multi Morph (4 styles)
            return { "LBH", "LPH", "LNH", "BPN" };
        case 24: // Flanges (9 styles)
            return { "Cmb L", "Flg L", "Phs L", "Cmb H", "Flg H", "Phs H", "Cmb HL", "Flg HL", "Phs HL" };
        case 25: // Misc (12 styles)
            return { "Low EQ 12", "Band EQ 12", "High EQ 12", "German LP",
                     "Formant-I", "Formant-II", "Formant-III", "Bandreject",
                     "Dist.Comb 1 LP", "Dist.Comb 1 BP", "Dist.Comb 2 LP", "Dist.Comb 2 BP" };
        case 26: // New (6 styles)
            return { "MG Ladder", "Acid Ladder", "MG Dirty", "Comb 2", "Exp MM", "Exp BPF" };
        default:
            return { "Default" };
    }
}

/** Returns the blend slider label for the given model and style. */
inline const char* getBlendLabelForModel (int modelIndex, int styleIndex) noexcept
{
    if (modelIndex >= 0 && modelIndex <= 3) // Analog, Dirty, Ladder, Digital
    {
        switch (styleIndex)
        {
            case 0:
            case 1: return "LP — BP — HP";
            case 2: return "LP — Notch — HP";
            case 3: return "Dual Notch";
            case 4: return "Band — Peak — Notch";
            case 5: return "Low Shelf — High Shelf";
            default: return "LP — BP — HP";
        }
    }
    switch (modelIndex)
    {
        case 4:  return "LP — BP";
        case 5:  return "Vowel Transpose";
        case 6:  return "Comb Blend";
        case 7:  return "Notch Count";
        case 8:  return "Stage Balance";
        case 9:  return "Slope Blend";
        case 10: return "EQ Curve";
        case 11: return "Carrier Balance";
        case 13: return "Smear Spread";
        case 16: return "Feedback Balance";
        case 17: return "LP — Notch — HP";
        case 19: return "Diffusion Balance";
        case 21: return "Zero Placement";
        case 22: return "LP — BP — HP";
        case 23: return "LP — BP — HP";
        case 24: return "Comb / Flange Blend";
        case 25: return "EQ / Formant / Blend";
        case 26: return "Ladder / Exp Blend";
        default: return "Blend";
    }
}

// ---- Routing Logic (Section 3 Input Rule) ------------------------------------------------------------------------------------------

/**
 * Section 3: "Input rule (oscN.destination choices: Filter 1, Filter 2, Filters 1+2, Effects only, Direct out, Filter blend, None):
 * toggling 'on' for Filter 1 changes Filter 2 -> Filters 1+2, Effects only / Direct out -> Filter 1;
 * toggling 'off' for Filter 1 changes Filters 1+2 -> Filter 2, Filter 1 -> Effects only;
 * Filter 2 symmetrically."
 */
inline bool isFilter1Active (int destinationChoice) noexcept
{
    return destinationChoice == 0 || destinationChoice == 2; // Filter 1 or Filters 1+2
}

inline bool isFilter2Active (int destinationChoice) noexcept
{
    return destinationChoice == 1 || destinationChoice == 2; // Filter 2 or Filters 1+2
}

inline int toggleFilter1Input (int currentDestination) noexcept
{
    if (isFilter1Active (currentDestination))
    {
        // Turn OFF Filter 1
        if (currentDestination == 2)
            return 1; // Filter 2
        return 3;     // Effects only
    }
    // Turn ON Filter 1
    if (currentDestination == 1) // was Filter 2
        return 2;                // Filters 1+2
    return 0;                    // Filter 1
}

inline int toggleFilter2Input (int currentDestination) noexcept
{
    if (isFilter2Active (currentDestination))
    {
        // Turn OFF Filter 2
        if (currentDestination == 2)
            return 0; // Filter 1
        return 3;     // Effects only
    }
    // Turn ON Filter 2
    if (currentDestination == 0) // was Filter 1
        return 2;                // Filters 1+2
    return 1;                    // Filter 2
}

inline bool hasAnyInputForFilter1 (int d1, int d2, int d3, int dNoise) noexcept
{
    return isFilter1Active (d1) || isFilter1Active (d2) || isFilter1Active (d3) || isFilter1Active (dNoise);
}

inline bool hasAnyInputForFilter2 (int d1, int d2, int d3, int dNoise, int filterRoutingChoice) noexcept
{
    if (isFilter2Active (d1) || isFilter2Active (d2) || isFilter2Active (d3) || isFilter2Active (dNoise))
        return true;
    // Series 1 to 2 routing (choice 1) feeds audio from Filter 1 to Filter 2
    if (filterRoutingChoice == 1 && hasAnyInputForFilter1 (d1, d2, d3, dNoise))
        return true;
    return false;
}

// ---- Response Math (Frequency & Phase Response) ------------------------------------------------------------------------------------

struct FilterCurvePoint
{
    float normFreq; // 0..1 log scale (20 Hz .. 20 kHz)
    float db;       // -36 dB .. +18 dB
};

/** Evaluates filter frequency response |H(f)| in dB and phase in radians. */
struct FilterResponse
{
    float magnitudeDb = 0.0f;
    float phaseRad = 0.0f;
};

/** Log frequency from normalized 0..1 position across 20 Hz to 20 kHz. */
inline float normToFrequency (float norm) noexcept
{
    const float clamped = std::clamp (norm, 0.0f, 1.0f);
    return 20.0f * std::pow (1000.0f, clamped);
}

/** Normalized position 0..1 from frequency in 20 Hz to 20 kHz. */
inline float frequencyToNorm (float hz) noexcept
{
    const float clamped = std::clamp (hz, 20.0f, 20000.0f);
    return std::log10 (clamped / 20.0f) / 3.0f; // log10(1000) = 3
}

/** Evaluates frequency response at frequency f for given parameters. */
inline FilterResponse evaluateFilterResponse (float f, int model, int style, float cutoffHz,
                                              float resPercent, float blendPercent, float varPercent,
                                              float formantX = 50.0f, float formantY = 50.0f,
                                              float formantTranspose = 0.0f) noexcept
{
    FilterResponse resp;
    const float fc = std::clamp (cutoffHz, 20.0f, 20000.0f);
    const float r = std::clamp (resPercent, 0.0f, 100.0f) / 100.0f;
    const float blend = std::clamp (blendPercent, 0.0f, 100.0f) / 100.0f;
    const float var = std::clamp (varPercent, 0.0f, 100.0f) / 100.0f;

    // Quality factor Q with steep boost near self-oscillation (> 90%)
    float q = 0.5f + 9.5f * r * r;
    if (r > 0.9f)
        q += (r - 0.9f) * 40.0f;

    const float w = f / fc;
    const float w2 = w * w;

    // Helper for 2-pole SVF denominator |D|^2 and phase
    const auto svfDenom = [q] (float normW) {
        const float nw2 = normW * normW;
        const float re = 1.0f - nw2;
        const float im = normW / q;
        const float mag2 = std::max (1e-8f, re * re + im * im);
        const float phase = -std::atan2 (im, re);
        return std::make_pair (mag2, phase);
    };

    if (model == 5) // Formant
    {
        // 3 parallel resonant vowel peaks
        const float shift = std::pow (2.0f, formantTranspose / 12.0f);
        const float f1 = std::clamp ((300.0f + 500.0f * (formantY / 100.0f)) * shift, 100.0f, 18000.0f);
        const float f2 = std::clamp ((800.0f + 1400.0f * (formantX / 100.0f)) * shift, 200.0f, 19000.0f);
        const float f3 = std::clamp ((2200.0f + 800.0f * (formantY / 100.0f)) * shift, 500.0f, 20000.0f);
        const float qF = 4.0f + 16.0f * r;

        const auto bpPeak = [f, qF] (float center) {
            const float ratio = f / center;
            const float re = 1.0f - ratio * ratio;
            const float im = ratio / qF;
            return (ratio / qF) / std::sqrt (std::max (1e-8f, re * re + im * im));
        };

        const float mag = 0.9f * bpPeak (f1) + 0.7f * bpPeak (f2) + 0.5f * bpPeak (f3);
        resp.magnitudeDb = 20.0f * std::log10 (std::max (1e-4f, mag));
        resp.phaseRad = -std::atan2 (f / f2, 1.0f - (f / f2) * (f / f2));
        return resp;
    }

    if (model == 6) // Comb
    {
        // Delay period T = 1 / fc -> periodic harmonic peaks
        const float feedback = 0.88f * r;
        const float phaseTerm = 2.0f * 3.14159265f * (f / fc);
        const float mag2 = 1.0f + feedback * feedback + 2.0f * feedback * std::cos (phaseTerm);
        resp.magnitudeDb = 10.0f * std::log10 (std::max (1e-4f, mag2));
        resp.phaseRad = std::atan2 (feedback * std::sin (phaseTerm), 1.0f + feedback * std::cos (phaseTerm));
        return resp;
    }

    if (model == 8) // Multi SVF (Dual SVF)
    {
        // Primary SVF at fc, Secondary SVF at fc2 shifted by Var (-4 to +4 octaves)
        const float fc2 = std::clamp (fc * std::pow (2.0f, 8.0f * (var - 0.5f)), 20.0f, 20000.0f);
        const auto d1 = svfDenom (f / fc);
        const auto d2 = svfDenom (f / fc2);

        // Stages determined by style index: 0=LH, 1=LB, 2=LP, 3=LN, 4=HB, 5=HP, 6=HN, 7=BP, 8=BN, 9=PP, 10=PN, 11=NN
        float num1_2 = 1.0f, num2_2 = 1.0f;
        const float w1_2 = (f / fc) * (f / fc);
        const float w2_2 = (f / fc2) * (f / fc2);

        const int s = std::clamp (style, 0, 11);
        // First stage
        if (s < 4)       num1_2 = 1.0f;              // Low-Pass
        else if (s < 7)  num1_2 = w1_2 * w1_2;       // High-Pass
        else if (s < 9)  num1_2 = w1_2 / (q * q);    // Band-Pass
        else if (s < 11) num1_2 = 1.0f + 4.0f * w1_2 / (q * q); // Peak
        else             num1_2 = (1.0f - w1_2) * (1.0f - w1_2); // Notch

        // Second stage
        if (s == 0) num2_2 = w2_2 * w2_2;            // HP
        else if (s == 1 || s == 4) num2_2 = w2_2 / (q * q); // BP
        else if (s == 2 || s == 5 || s == 7 || s == 9) num2_2 = 1.0f + 4.0f * w2_2 / (q * q); // Peak
        else num2_2 = (1.0f - w2_2) * (1.0f - w2_2); // Notch

        const float mag2 = (num1_2 / d1.first) * (num2_2 / d2.first);
        resp.magnitudeDb = 10.0f * std::log10 (std::max (1e-4f, mag2));
        resp.phaseRad = d1.second + d2.second;
        return resp;
    }

    // Default SVF / Analog / Digital / Slope / Moog Ladder
    const auto d = svfDenom (w);
    float mag2 = 1.0f;
    float phase = d.second;

    if (style == 1 || model == 2 || model == 20) // 24 dB 4-pole
    {
        // 4-pole cascade
        const float lp = 1.0f / (d.first * d.first);
        const float hp = (w2 * w2 * w2 * w2) / (d.first * d.first);
        const float bp = (w2 * w2) / (d.first * d.first * q * q * q * q);

        if (blend <= 0.5f)
        {
            const float t = blend * 2.0f;
            mag2 = (1.0f - t) * lp + t * bp;
        }
        else
        {
            const float t = (blend - 0.5f) * 2.0f;
            mag2 = (1.0f - t) * bp + t * hp;
        }
        phase = d.second * 2.0f;
    }
    else if (style == 2) // Style C: Notch swap
    {
        const float notch = ((1.0f - w2) * (1.0f - w2)) / d.first;
        const float lp = 1.0f / d.first;
        const float hp = (w2 * w2) / d.first;
        if (blend <= 0.5f)
        {
            const float t = blend * 2.0f;
            mag2 = (1.0f - t) * lp + t * notch;
        }
        else
        {
            const float t = (blend - 0.5f) * 2.0f;
            mag2 = (1.0f - t) * notch + t * hp;
        }
    }
    else if (style == 3) // Style D: Dual notch
    {
        const float n1 = ((1.0f - w2) * (1.0f - w2)) / d.first;
        const float w2_shift = (w * 0.7f) * (w * 0.7f);
        const float n2 = ((1.0f - w2_shift) * (1.0f - w2_shift)) / svfDenom (w * 0.7f).first;
        mag2 = n1 * n2;
    }
    else if (style == 4) // Style E: Band peak notch
    {
        const float bp = (w2 / (q * q)) / d.first;
        const float peak = (1.0f + 4.0f * w2 / (q * q)) / d.first;
        const float notch = ((1.0f - w2) * (1.0f - w2)) / d.first;
        if (blend <= 0.5f)
        {
            const float t = blend * 2.0f;
            mag2 = (1.0f - t) * bp + t * peak;
        }
        else
        {
            const float t = (blend - 0.5f) * 2.0f;
            mag2 = (1.0f - t) * peak + t * notch;
        }
    }
    else if (style == 5) // Style F: Shelf
    {
        const float gain = std::pow (10.0f, (blend - 0.5f) * 24.0f / 20.0f);
        mag2 = (1.0f + gain * gain * w2) / (1.0f + w2);
    }
    else // Style A (12 dB): LP -> BP -> HP morph
    {
        const float lp = 1.0f / d.first;
        const float hp = (w2 * w2) / d.first;
        const float bp = (w2 / (q * q)) / d.first;

        if (blend <= 0.5f)
        {
            const float t = blend * 2.0f;
            mag2 = (1.0f - t) * lp + t * bp;
        }
        else
        {
            const float t = (blend - 0.5f) * 2.0f;
            mag2 = (1.0f - t) * bp + t * hp;
        }
    }

    resp.magnitudeDb = 10.0f * std::log10 (std::max (1e-4f, mag2));
    resp.phaseRad = phase;
    return resp;
}

// ---- Serum Type Hierarchical Catalog (Section 2.3) ---------------------------------------------------------------------------------

struct SerumFilterTypeEntry
{
    const char* name;
    const char* family; // "Normal", "Multi", "Flanges", "Misc", "New"
    int model;
    int style;
    float defaultBlend; // 0..100
};

inline const std::vector<SerumFilterTypeEntry>& getSerumFilterCatalog()
{
    static const std::vector<SerumFilterTypeEntry> catalog = {
        // Normal
        { "MG Low 6", "Normal", 9, 0, 0.0f },
        { "MG Low 12", "Normal", 22, 0, 0.0f },
        { "MG Low 18", "Normal", 9, 1, 0.0f },
        { "MG Low 24", "Normal", 22, 1, 0.0f },
        { "Low 6", "Normal", 9, 2, 0.0f },
        { "Low 12", "Normal", 22, 2, 0.0f },
        { "Low 18", "Normal", 9, 3, 0.0f },
        { "Low 24", "Normal", 22, 3, 0.0f },
        { "High 6", "Normal", 9, 4, 100.0f },
        { "High 12", "Normal", 22, 4, 100.0f },
        { "High 18", "Normal", 9, 5, 100.0f },
        { "High 24", "Normal", 22, 5, 100.0f },
        { "Band 12", "Normal", 22, 6, 50.0f },
        { "Band 24", "Normal", 22, 7, 50.0f },
        { "Peak 12", "Normal", 22, 8, 50.0f },
        { "Peak 24", "Normal", 22, 9, 50.0f },
        { "Notch 12", "Normal", 22, 10, 50.0f },
        { "Notch 24", "Normal", 22, 11, 50.0f },

        // Multi
        { "LH", "Multi", 8, 0, 0.0f },
        { "LB", "Multi", 8, 1, 0.0f },
        { "LP", "Multi", 8, 2, 0.0f },
        { "LN", "Multi", 8, 3, 0.0f },
        { "HB", "Multi", 8, 4, 0.0f },
        { "HP", "Multi", 8, 5, 0.0f },
        { "HN", "Multi", 8, 6, 0.0f },
        { "BP", "Multi", 8, 7, 0.0f },
        { "BN", "Multi", 8, 8, 0.0f },
        { "PP", "Multi", 8, 9, 0.0f },
        { "PN", "Multi", 8, 10, 0.0f },
        { "NN", "Multi", 8, 11, 0.0f },
        { "LBH", "Multi", 23, 0, 0.0f },
        { "LPH", "Multi", 23, 1, 0.0f },
        { "LNH", "Multi", 23, 2, 0.0f },
        { "BPN", "Multi", 23, 3, 50.0f },

        // Flanges
        { "Cmb L", "Flanges", 24, 0, 0.0f },
        { "Flg L", "Flanges", 24, 1, 0.0f },
        { "Phs L", "Flanges", 24, 2, 0.0f },
        { "Cmb H", "Flanges", 24, 3, 50.0f },
        { "Flg H", "Flanges", 24, 4, 50.0f },
        { "Phs H", "Flanges", 24, 5, 50.0f },
        { "Cmb HL", "Flanges", 24, 6, 100.0f },
        { "Flg HL", "Flanges", 24, 7, 100.0f },
        { "Phs HL", "Flanges", 24, 8, 100.0f },

        // Misc
        { "Low EQ 6", "Misc", 10, 0, 50.0f },
        { "Low EQ 12", "Misc", 25, 0, 20.0f },
        { "Band EQ 6", "Misc", 10, 1, 50.0f },
        { "Band EQ 12", "Misc", 25, 1, 50.0f },
        { "High EQ 6", "Misc", 10, 2, 50.0f },
        { "High EQ 12", "Misc", 25, 2, 80.0f },
        { "Ring Mod", "Misc", 11, 0, 50.0f },
        { "Ring Mod x2", "Misc", 11, 1, 50.0f },
        { "SampHold", "Misc", 12, 0, 50.0f },
        { "SampHold-", "Misc", 12, 1, 50.0f },
        { "Combs", "Misc", 13, 0, 50.0f },
        { "Allpasses", "Misc", 13, 1, 50.0f },
        { "Reverb", "Misc", 13, 2, 50.0f },
        { "French LP", "Misc", 14, 0, 0.0f },
        { "German LP", "Misc", 25, 3, 0.0f },
        { "Add Bass", "Misc", 15, 0, 0.0f },
        { "Formant-I", "Misc", 25, 4, 0.0f },
        { "Formant-II", "Misc", 25, 5, 0.0f },
        { "Formant-III", "Misc", 25, 6, 0.0f },
        { "Bandreject", "Misc", 25, 7, 50.0f },
        { "Dist.Comb 1 LP", "Misc", 25, 8, 20.0f },
        { "Dist.Comb 1 BP", "Misc", 25, 9, 50.0f },
        { "Dist.Comb 2 LP", "Misc", 25, 10, 20.0f },
        { "Dist.Comb 2 BP", "Misc", 25, 11, 50.0f },
        { "Scream LP", "Misc", 16, 0, 0.0f },
        { "Scream BP", "Misc", 16, 1, 50.0f },

        // New
        { "New Wsp", "New", 17, 0, 0.0f },
        { "DJ Mixer Xfer", "New", 18, 0, 50.0f },
        { "Diffusor", "New", 19, 0, 50.0f },
        { "MG Ladder", "New", 26, 0, 0.0f },
        { "Acid Ladder", "New", 26, 1, 0.0f },
        { "EMS Ladder", "New", 20, 0, 0.0f },
        { "MG Dirty", "New", 26, 2, 0.0f },
        { "PZ SVF", "New", 21, 0, 50.0f },
        { "Comb 2", "New", 26, 3, 42.0f },
        { "Exp MM", "New", 26, 4, 50.0f },
        { "Exp BPF", "New", 26, 5, 50.0f }
    };
    return catalog;
}

/** Returns the display name for a given (model, style) pair, using the Serum catalog name where applicable. */
inline std::string getFilterTypeNameForModelAndStyle (int model, int style)
{
    const auto& catalog = getSerumFilterCatalog();
    for (const auto& item : catalog)
    {
        if (item.model == model && item.style == style)
            return item.name;
    }
    const auto styles = getStylesForModel (model);
    if (style >= 0 && style < static_cast<int> (styles.size()))
        return styles[static_cast<size_t> (style)];
    return getFilterModelName (model);
}

} // namespace synthyboy::ui
