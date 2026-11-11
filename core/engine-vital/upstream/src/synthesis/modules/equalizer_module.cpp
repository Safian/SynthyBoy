/* Copyright 2013-2019 Matt Tytel
 *
 * vital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vital.  If not, see <http://www.gnu.org/licenses/>.
 */
// Modified for SynthyBoy (AGPL-3.0-or-later combined work), 2026

#include "equalizer_module.h"

#include "digital_svf.h"
#include "futils.h"
#include "value.h"

#include <algorithm>
#include <cmath>

namespace sbvital {

  // ---- the Serum 2.1.5 law: RBJ biquads (Audio EQ Cookbook), the Q laws measured against the real plug-in -------------------------------------------------------------
  mono_float SerumEqStage::qualityOf(Kind kind, mono_float resonance) {
    const mono_float x = std::min(1.0f, std::max(0.0f, resonance));
    if (kind == kLowShelf || kind == kHighShelf)
      return 0.4f + 1.7417f * x * x * x;
    return 0.1f + 18.9053f * x * x * x * x;
  }

  SerumEqStage::Coefficients SerumEqStage::design(Kind kind, mono_float frequency, mono_float resonance, mono_float decibels, mono_float sample_rate) {
    Coefficients out;
    if (kind == kOff)
      return out;
    const double nyquist_limit = 0.499 * sample_rate;
    const double f = std::min<double>(std::max<double>(frequency, 10.0), nyquist_limit);
    const double w0 = 2.0 * 3.14159265358979323846 * f / sample_rate;
    const double q = qualityOf(kind, resonance);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double c = std::cos(w0);
    const double a_gain = std::pow(10.0, std::min(24.0, std::max(-24.0, static_cast<double>(decibels))) / 40.0);
    double b0, b1, b2, a0, a1, a2;
    switch (kind) {
      case kPeak:
        b0 = 1.0 + alpha * a_gain; b1 = -2.0 * c; b2 = 1.0 - alpha * a_gain;
        a0 = 1.0 + alpha / a_gain; a1 = -2.0 * c; a2 = 1.0 - alpha / a_gain;
        break;
      case kLowShelf: {
        const double s = 2.0 * std::sqrt(a_gain) * alpha;
        b0 = a_gain * ((a_gain + 1.0) - (a_gain - 1.0) * c + s);
        b1 = 2.0 * a_gain * ((a_gain - 1.0) - (a_gain + 1.0) * c);
        b2 = a_gain * ((a_gain + 1.0) - (a_gain - 1.0) * c - s);
        a0 = (a_gain + 1.0) + (a_gain - 1.0) * c + s;
        a1 = -2.0 * ((a_gain - 1.0) + (a_gain + 1.0) * c);
        a2 = (a_gain + 1.0) + (a_gain - 1.0) * c - s;
        break;
      }
      case kHighShelf: {
        const double s = 2.0 * std::sqrt(a_gain) * alpha;
        b0 = a_gain * ((a_gain + 1.0) + (a_gain - 1.0) * c + s);
        b1 = -2.0 * a_gain * ((a_gain - 1.0) + (a_gain + 1.0) * c);
        b2 = a_gain * ((a_gain + 1.0) + (a_gain - 1.0) * c - s);
        a0 = (a_gain + 1.0) - (a_gain - 1.0) * c + s;
        a1 = 2.0 * ((a_gain - 1.0) - (a_gain + 1.0) * c);
        a2 = (a_gain + 1.0) - (a_gain - 1.0) * c - s;
        break;
      }
      case kHighPass:
        b0 = (1.0 + c) * 0.5; b1 = -(1.0 + c); b2 = (1.0 + c) * 0.5;
        a0 = 1.0 + alpha; a1 = -2.0 * c; a2 = 1.0 - alpha;
        break;
      case kLowPass:
        b0 = (1.0 - c) * 0.5; b1 = 1.0 - c; b2 = (1.0 - c) * 0.5;
        a0 = 1.0 + alpha; a1 = -2.0 * c; a2 = 1.0 - alpha;
        break;
      case kNotch:
      default:
        b0 = 1.0; b1 = -2.0 * c; b2 = 1.0;
        a0 = 1.0 + alpha; a1 = -2.0 * c; a2 = 1.0 - alpha;
        break;
    }
    const double inv = 1.0 / a0;
    out.b0 = static_cast<mono_float>(b0 * inv);
    out.b1 = static_cast<mono_float>(b1 * inv);
    out.b2 = static_cast<mono_float>(b2 * inv);
    out.a1 = static_cast<mono_float>(a1 * inv);
    out.a2 = static_cast<mono_float>(a2 * inv);
    return out;
  }

  void SerumEqStage::process(const poly_float* in, poly_float* out, int num_samples, const Coefficients& target) {
    if (!initialised) {
      current = target;
      initialised = true;
    }
    const mono_float inc = 1.0f / num_samples;
    const mono_float d0 = (target.b0 - current.b0) * inc, d1 = (target.b1 - current.b1) * inc, d2 = (target.b2 - current.b2) * inc;
    const mono_float e1 = (target.a1 - current.a1) * inc, e2 = (target.a2 - current.a2) * inc;
    Coefficients c = current;
    poly_float s1 = z1, s2 = z2;
    for (int i = 0; i < num_samples; ++i) {
      c.b0 += d0; c.b1 += d1; c.b2 += d2; c.a1 += e1; c.a2 += e2;
      const poly_float x = in[i];
      const poly_float y = x * c.b0 + s1;               // transposed direct form II
      s1 = x * c.b1 - y * c.a1 + s2;
      s2 = x * c.b2 - y * c.a2;
      out[i] = y;
    }
    z1 = s1;
    z2 = s2;
    current = target;
  }

  EqualizerModule::EqualizerModule() :
      SynthModule(0, 1),
      low_mode_(nullptr), band_mode_(nullptr), high_mode_(nullptr),
      high_pass_(nullptr), low_shelf_(nullptr),
      notch_(nullptr), band_shelf_(nullptr),
      low_pass_(nullptr), high_shelf_(nullptr) {
    audio_memory_ = std::make_shared<sbvital::StereoMemory>(sbvital::kAudioMemorySamples);
  }

  void EqualizerModule::init() {
    static const cr::Value kPass(DigitalSvf::k12Db);
    static const cr::Value kNotch(DigitalSvf::kNotchPassSwap);
    static const cr::Value kShelving(DigitalSvf::kShelving);

    high_pass_ = new DigitalSvf();
    low_shelf_ = new DigitalSvf();
    band_shelf_ = new DigitalSvf();
    notch_ = new DigitalSvf();
    low_pass_ = new DigitalSvf();
    high_shelf_ = new DigitalSvf();

    high_pass_->setDriveCompensation(false);
    high_pass_->setBasic(true);
    notch_->setDriveCompensation(false);
    notch_->setBasic(true);
    low_pass_->setDriveCompensation(false);
    low_pass_->setBasic(true);

    addIdleProcessor(high_pass_);
    addIdleProcessor(low_shelf_);
    addIdleProcessor(notch_);
    addIdleProcessor(band_shelf_);
    addIdleProcessor(low_pass_);
    addIdleProcessor(high_shelf_);
    low_pass_->useOutput(output());
    high_shelf_->useOutput(output());

    // block B34: the second peak / notch band (Off by default) and the law switch
    band2_shelf_ = new DigitalSvf();
    band2_notch_ = new DigitalSvf();
    band2_notch_->setDriveCompensation(false);
    band2_notch_->setBasic(true);
    addIdleProcessor(band2_shelf_);
    addIdleProcessor(band2_notch_);

    low_mode_ = createBaseControl("eq_low_mode");
    band_mode_ = createBaseControl("eq_band_mode");
    high_mode_ = createBaseControl("eq_high_mode");

    Output* low_cutoff_midi = createMonoModControl("eq_low_cutoff", true, true);
    Output* band_cutoff_midi = createMonoModControl("eq_band_cutoff", true, true);
    Output* high_cutoff_midi = createMonoModControl("eq_high_cutoff", true, true);
    Output* band2_cutoff_midi = createMonoModControl("eq_band2_cutoff", true, true);

    Output* low_resonance = createMonoModControl("eq_low_resonance");
    Output* band_resonance = createMonoModControl("eq_band_resonance");
    Output* high_resonance = createMonoModControl("eq_high_resonance");
    Output* band2_resonance = createMonoModControl("eq_band2_resonance");

    Output* low_decibels = createMonoModControl("eq_low_gain");
    Output* band_decibels = createMonoModControl("eq_band_gain");
    Output* high_decibels = createMonoModControl("eq_high_gain");
    Output* band2_decibels = createMonoModControl("eq_band2_gain");

    band2_mode_ = createBaseControl("eq_band2_mode");
    law_ = createBaseControl("eq_law");
    low_cutoff_ = low_cutoff_midi;
    band_cutoff_ = band_cutoff_midi;
    band2_cutoff_ = band2_cutoff_midi;
    high_cutoff_ = high_cutoff_midi;
    low_resonance_ = low_resonance;
    band_resonance_ = band_resonance;
    band2_resonance_ = band2_resonance;
    high_resonance_ = high_resonance;
    low_decibels_ = low_decibels;
    band_decibels_ = band_decibels;
    band2_decibels_ = band2_decibels;
    high_decibels_ = high_decibels;

    band2_shelf_->plug(&kShelving, DigitalSvf::kStyle);
    band2_shelf_->plug(&constants::kValueOne, DigitalSvf::kPassBlend);
    band2_shelf_->plug(band2_cutoff_midi, DigitalSvf::kMidiCutoff);
    band2_shelf_->plug(band2_resonance, DigitalSvf::kResonance);
    band2_shelf_->plug(band2_decibels, DigitalSvf::kGain);

    band2_notch_->plug(&kNotch, DigitalSvf::kStyle);
    band2_notch_->plug(&constants::kValueOne, DigitalSvf::kPassBlend);
    band2_notch_->plug(band2_cutoff_midi, DigitalSvf::kMidiCutoff);
    band2_notch_->plug(band2_resonance, DigitalSvf::kResonance);

    high_pass_->plug(&kPass, DigitalSvf::kStyle);
    high_pass_->plug(&constants::kValueTwo, DigitalSvf::kPassBlend);
    high_pass_->plug(low_cutoff_midi, DigitalSvf::kMidiCutoff);
    high_pass_->plug(low_resonance, DigitalSvf::kResonance);

    low_shelf_->plug(&kShelving, DigitalSvf::kStyle);
    low_shelf_->plug(&constants::kValueZero, DigitalSvf::kPassBlend);
    low_shelf_->plug(low_cutoff_midi, DigitalSvf::kMidiCutoff);
    low_shelf_->plug(low_resonance, DigitalSvf::kResonance);
    low_shelf_->plug(low_decibels, DigitalSvf::kGain);

    band_shelf_->plug(&kShelving, DigitalSvf::kStyle);
    band_shelf_->plug(&constants::kValueOne, DigitalSvf::kPassBlend);
    band_shelf_->plug(band_cutoff_midi, DigitalSvf::kMidiCutoff);
    band_shelf_->plug(band_resonance, DigitalSvf::kResonance);
    band_shelf_->plug(band_decibels, DigitalSvf::kGain);

    notch_->plug(&kNotch, DigitalSvf::kStyle);
    notch_->plug(&constants::kValueOne, DigitalSvf::kPassBlend);
    notch_->plug(band_cutoff_midi, DigitalSvf::kMidiCutoff);
    notch_->plug(band_resonance, DigitalSvf::kResonance);

    low_pass_->plug(&kPass, DigitalSvf::kStyle);
    low_pass_->plug(&constants::kValueZero, DigitalSvf::kPassBlend);
    low_pass_->plug(high_cutoff_midi, DigitalSvf::kMidiCutoff);
    low_pass_->plug(high_resonance, DigitalSvf::kResonance);

    high_shelf_->plug(&kShelving, DigitalSvf::kStyle);
    high_shelf_->plug(&constants::kValueTwo, DigitalSvf::kPassBlend);
    high_shelf_->plug(high_cutoff_midi, DigitalSvf::kMidiCutoff);
    high_shelf_->plug(high_resonance, DigitalSvf::kResonance);
    high_shelf_->plug(high_decibels, DigitalSvf::kGain);
    
    SynthModule::init();
  }

  void EqualizerModule::hardReset() {
    band2_shelf_->reset(constants::kFullMask);
    band2_notch_->reset(constants::kFullMask);
    serum_low_.reset();
    serum_band_.reset();
    serum_band2_.reset();
    serum_high_.reset();
    high_pass_->reset(constants::kFullMask);
    low_shelf_->reset(constants::kFullMask);
    band_shelf_->reset(constants::kFullMask);
    notch_->reset(constants::kFullMask);
    low_pass_->reset(constants::kFullMask);
    high_shelf_->reset(constants::kFullMask);
    band2_shelf_->reset(constants::kFullMask);
    band2_notch_->reset(constants::kFullMask);
    serum_low_.reset();
    serum_band_.reset();
    serum_band2_.reset();
    serum_high_.reset();
  }

  void EqualizerModule::enable(bool enable) {
    SynthModule::enable(enable);
    process(1);
    if (enable) {
      high_pass_->reset(constants::kFullMask);
      low_shelf_->reset(constants::kFullMask);
      band_shelf_->reset(constants::kFullMask);
      notch_->reset(constants::kFullMask);
      low_pass_->reset(constants::kFullMask);
      high_shelf_->reset(constants::kFullMask);
      band2_shelf_->reset(constants::kFullMask);
      band2_notch_->reset(constants::kFullMask);
      serum_low_.reset();
      serum_band_.reset();
      serum_band2_.reset();
      serum_high_.reset();
    }
  }

  void EqualizerModule::setSampleRate(int sample_rate) {
    Processor::setSampleRate(sample_rate);   // SynthyBoy: the module's own rate (the RBJ stages of the Serum law are designed for it); the original left it at its default
    high_pass_->setSampleRate(sample_rate);
    low_shelf_->setSampleRate(sample_rate);
    band_shelf_->setSampleRate(sample_rate);
    notch_->setSampleRate(sample_rate);
    low_pass_->setSampleRate(sample_rate);
    high_shelf_->setSampleRate(sample_rate);
    band2_shelf_->setSampleRate(sample_rate);
    band2_notch_->setSampleRate(sample_rate);
  }

  void EqualizerModule::processWithInput(const poly_float* audio_in, int num_samples) {
    SynthModule::process(num_samples);

    if (law_->value() > 0.5f) {
      processSerumLaw(audio_in, num_samples);
      return;
    }
    if (was_serum_) {
      was_serum_ = false;
      hardReset();
    }

    Processor* low_processor = low_mode_->value() ? high_pass_ : low_shelf_;
    Processor* band_processor = band_mode_->value() ? notch_ : band_shelf_;
    Processor* high_processor = high_mode_->value() ? low_pass_ : high_shelf_;

    low_processor->processWithInput(audio_in, num_samples);
    band_processor->processWithInput(low_processor->output()->buffer, num_samples);
    const poly_float* band_out = band_processor->output()->buffer;
    // block B34: the second band sits between the first band and the high band; Off leaves the chain bit exactly as it was
    const int band2_mode = static_cast<int>(band2_mode_->value());
    if (band2_mode > 0) {
      Processor* band2_processor = band2_mode == 2 ? static_cast<Processor*>(band2_notch_) : static_cast<Processor*>(band2_shelf_);
      band2_processor->processWithInput(band_out, num_samples);
      band_out = band2_processor->output()->buffer;
    }
    high_processor->processWithInput(band_out, num_samples);

    const poly_float* output_buffer = high_processor->output()->buffer;
    for (int i = 0; i < num_samples; ++i)
      audio_memory_->push(output_buffer[i]);
  }

  void EqualizerModule::processSerumLaw(const poly_float* audio_in, int num_samples) {
    if (!was_serum_) {
      was_serum_ = true;
      serum_low_.reset();
      serum_band_.reset();
      serum_band2_.reset();
      serum_high_.reset();
    }
    const mono_float sample_rate = static_cast<mono_float>(getSampleRate());
    const auto hz = [&](Output* cutoff) { return utils::midiNoteToFrequency(cutoff->buffer[num_samples - 1][0]); };
    const auto resonance = [&](Output* o) { return o->buffer[0][0]; };
    const auto decibels = [&](Output* o) { return o->buffer[0][0]; };

    const SerumEqStage::Kind low_kind = low_mode_->value() > 0.5f ? SerumEqStage::kHighPass : SerumEqStage::kLowShelf;
    const SerumEqStage::Kind band_kind = band_mode_->value() > 0.5f ? SerumEqStage::kNotch : SerumEqStage::kPeak;
    const int band2_mode = static_cast<int>(band2_mode_->value());
    const SerumEqStage::Kind band2_kind = band2_mode == 2 ? SerumEqStage::kNotch : band2_mode == 1 ? SerumEqStage::kPeak : SerumEqStage::kOff;
    const SerumEqStage::Kind high_kind = high_mode_->value() > 0.5f ? SerumEqStage::kLowPass : SerumEqStage::kHighShelf;

    poly_float* out = output()->buffer;
    serum_low_.process(audio_in, out, num_samples,
                       SerumEqStage::design(low_kind, hz(low_cutoff_), resonance(low_resonance_), decibels(low_decibels_), sample_rate));
    serum_band_.process(out, out, num_samples,
                        SerumEqStage::design(band_kind, hz(band_cutoff_), resonance(band_resonance_), decibels(band_decibels_), sample_rate));
    serum_band2_.process(out, out, num_samples,
                         SerumEqStage::design(band2_kind, hz(band2_cutoff_), resonance(band2_resonance_), decibels(band2_decibels_), sample_rate));
    serum_high_.process(out, out, num_samples,
                        SerumEqStage::design(high_kind, hz(high_cutoff_), resonance(high_resonance_), decibels(high_decibels_), sample_rate));
    for (int i = 0; i < num_samples; ++i)
      audio_memory_->push(out[i]);
  }
} // namespace sbvital
