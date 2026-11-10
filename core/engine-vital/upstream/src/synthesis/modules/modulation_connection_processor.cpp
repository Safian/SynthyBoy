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

#include "modulation_connection_processor.h"
#include "futils.h"

namespace sbvital {

  ModulationConnectionProcessor::ModulationConnectionProcessor(int index) :
      SynthModule(kNumInputs, kNumOutputs), index_(index), polyphonic_(true), current_value_(nullptr),
      bipolar_(nullptr), stereo_(nullptr) {
    setControlRate(true);

    modulation_amount_ = 0.0f;

    destination_scale_ = std::make_shared<mono_float>();
    *destination_scale_ = 0.0f;
    last_destination_scale_ = 0.0f;

    power_ = 0.0f;

    map_generator_ = std::make_shared<LineGenerator>();
    map_generator_->initLinear();
    slew_source_ = std::make_shared<Output>(kMaxBufferSize, kMaxOversample);   // GROUPB WP-08: made here, never on the audio thread
  }

  void ModulationConnectionProcessor::init() {
    std::string bipolar_name = "modulation_" + std::to_string(index_ + 1) + "_bipolar";
    bipolar_ = createBaseControl(bipolar_name);

    std::string stereo_name = "modulation_" + std::to_string(index_ + 1) + "_stereo";
    stereo_ = createBaseControl(stereo_name);

    std::string bypass_name = "modulation_" + std::to_string(index_ + 1) + "_bypass";
    bypass_ = createBaseControl(bypass_name);

    SynthModule::init();
  }

  poly_float ModulationConnectionProcessor::computeAuxFactor() const {
    const Extras& extras = *extras_;
    if (!extras.aux_active)
      return 1.0f;

    poly_float a = input(kAuxInput)->source->buffer[0];
    if (extras.aux_bipolar)
      a = a * 0.5f + 0.5f;
    a = utils::clamp(a, 0.0f, 1.0f);
    if (extras.aux_invert)
      a = poly_float(1.0f) - a;
    if (extras.aux_power != 0.0f)
      a = futils::powerScale(a, poly_float(-extras.aux_power));
    return a * extras.aux_output;
  }

  // The coefficients of the RISE / FALL slew for a step of `seconds_per_step`: a one pole towards the input, the time constant of the rise (input above
  // the value) or of the fall; a time is the span of 90 % of a step (ln 10 time constants). Per voice (the times are controls and may be modulated).
  bool ModulationConnectionProcessor::updateSlewRates(mono_float seconds_per_step) {
    static constexpr mono_float kLn10 = 2.302585093f;
    const poly_float rise = utils::clamp(input(kRise)->at(0), 0.0f, 5000.0f);
    const poly_float fall = utils::clamp(input(kFall)->at(0), 0.0f, 5000.0f);
    bool any = false;
    for (size_t i = 0; i < poly_float::kSize; ++i) {
      const mono_float r = rise[i] * 0.001f, f = fall[i] * 0.001f;
      rate_rise_.set(i, r > 0.0f ? 1.0f - std::exp(-seconds_per_step * kLn10 / r) : 1.0f);
      rate_fall_.set(i, f > 0.0f ? 1.0f - std::exp(-seconds_per_step * kLn10 / f) : 1.0f);
      any = any || r > 0.0f || f > 0.0f;
    }
    return any;
  }

  // One step of the RISE / FALL slew with the coefficients of updateSlewRates. A fresh voice starts on its value.
  poly_float ModulationConnectionProcessor::slewStep(poly_float value_in) {
    const poly_float rate = utils::maskLoad(rate_fall_, rate_rise_, poly_float::greaterThan(value_in, slew_value_));
    slew_value_ = slew_value_ + (value_in - slew_value_) * rate;
    return slew_value_;
  }

  // The DELAY of the slot: 0 for a voice that has not played for the delay yet, 1 after it (and always 1 with DELAY 0). The gate multiplies the amount of the
  // connection, so it is ramped over a block like every change of the amount.
  poly_float ModulationConnectionProcessor::computeDelayGate() {
    poly_float delay;
    if (extras_->delay_sync) {
      const mono_float bps = std::max(input(kBeatsPerSecond)->at(0)[0], 0.01f);
      delay = utils::clamp(input(kDelayBeats)->at(0), 0.0f, 8.0f) * (1.0f / bps);
    }
    else
      delay = utils::clamp(input(kDelay)->at(0), 0.0f, 4000.0f) * 0.001f;
    delay_elapsed_ = utils::maskLoad(delay_elapsed_, 0.0f, getResetMask(kReset));
    const poly_float gate = utils::maskLoad(poly_float(1.0f), poly_float(0.0f), poly_float::lessThan(delay_elapsed_, delay));
    delay_elapsed_ = utils::min(delay_elapsed_ + block_seconds_, 1000.0f);
    return gate;
  }

  void ModulationConnectionProcessor::process(int num_samples) {
    const Output* source = input(kModulationInput)->source;
    block_seconds_ = static_cast<mono_float>(num_samples) / static_cast<mono_float>(getSampleRate());
    aux_factor_ = computeAuxFactor() * computeDelayGate();
    poly_float modulation_input = source->trigger_value;
    output(kModulationSource)->buffer[0] = modulation_input;

    if (last_destination_scale_ != *destination_scale_)
      modulation_amount_ = 0.0f;
    last_destination_scale_ = *destination_scale_;

    if (isControlRate() || source->isControlRate())
      processControlRate(source);
    else {
      const bool slewing = updateSlewRates(1.0f / static_cast<mono_float>(getSampleRate()));
      if (slewing) {
        // GROUPB WP-08: the audio rate path reads a slewed copy of the source (a fresh voice starts on its value)
        const poly_float* in = source->buffer;
        poly_float* out = slew_source_->buffer;
        slew_value_ = utils::maskLoad(slew_value_, in[0], getResetMask(kReset));
        for (int i = 0; i < num_samples; ++i)
          out[i] = slewStep(in[i]);
        processAudioRate(num_samples, slew_source_.get());
      }
      else {
        slew_value_ = source->buffer[num_samples - 1];   // the slew starts from the source when RISE / FALL are switched on
        processAudioRate(num_samples, source);
      }
    }
  }

  void ModulationConnectionProcessor::processAudioRate(int num_samples, const Output* source) {
    if (bypass_->value()) {
      output(kModulationOutput)->clearBuffer();
      output(kModulationOutput)->trigger_value = 0.0f;
      return;
    }

    poly_float power = -input(kModulationPower)->at(0);
    bool using_power = (poly_float::notEqual(0.0f, power) | poly_float::notEqual(0.0f, power_)).anyMask();
    bool using_map = !map_generator_->linear();

    if (using_power && using_map)
      processAudioRateRemappedAndMorphed(num_samples, source, power);
    else if (using_power)
      processAudioRateMorphed(num_samples, source, power);
    else if (using_map)
      processAudioRateRemapped(num_samples, source);
    else
      processAudioRateLinear(num_samples, source);

    power_ = power;
  }

  void ModulationConnectionProcessor::processAudioRateLinear(int num_samples, const Output* source) {
    poly_float* dest = output(kModulationOutput)->buffer;
    const poly_float* modulation_source = source->buffer;

    poly_float bipolar_offset = -bipolar_->value() * 0.5f;
    poly_float current_amount = modulation_amount_;
    poly_float stereo_scale = poly_float(1.0f) - (constants::kRightOne * 2.0f * stereo_->value());
    poly_float modulation_amount = utils::clamp(input(kModulationAmount)->at(0), -1.0f, 1.0f) * stereo_scale * aux_factor_;
    modulation_amount_ = modulation_amount * (*destination_scale_);
    current_amount = utils::maskLoad(current_amount, modulation_amount_, getResetMask(kReset));
    poly_float delta_amount = (modulation_amount_ - current_amount) * (1.0f / num_samples);

    for (int i = 0; i < num_samples; ++i) {
      current_amount += delta_amount;
      poly_float modulation_value = modulation_source[i];
      dest[i] = (modulation_value + bipolar_offset) * current_amount;
    }

    output(kModulationPreScale)->buffer[0] = (modulation_source[0] + bipolar_offset) * modulation_amount;
    output(kModulationOutput)->trigger_value = dest[0];
  }

  void ModulationConnectionProcessor::processAudioRateMorphed(int num_samples, const Output* source,
                                                              poly_float power) {
    poly_float* dest = output(kModulationOutput)->buffer;
    const poly_float* modulation_source = source->buffer;
    poly_float bipolar_offset = -bipolar_->value() * 0.5f;

    poly_float bipolar = bipolar_->value();
    poly_float polarity_pre_scale = bipolar + 1.0f;
    poly_float polarity_post_scale = -bipolar * 0.5f + 1.0f;
    polarity_post_scale *= poly_float(1.0f) - (constants::kRightOne * 2.0f * stereo_->value());

    poly_float current_amount = modulation_amount_;
    poly_float current_power = power_;

    poly_float modulation_amount = utils::clamp(input(kModulationAmount)->at(0), -1.0f, 1.0f) * aux_factor_;
    modulation_amount_ = modulation_amount * (*destination_scale_);

    poly_mask reset_mask = getResetMask(kReset);
    current_amount = utils::maskLoad(current_amount, modulation_amount_, reset_mask);
    current_power = utils::maskLoad(current_power, power, reset_mask);

    float sample_inc = 1.0f / num_samples;
    poly_float delta_amount = (modulation_amount_ - current_amount) * sample_inc;
    poly_float delta_power = (power - current_power) * sample_inc;

    for (int i = 0; i < num_samples; ++i) {
      current_amount += delta_amount;
      current_power += delta_power;

      poly_float modulation_value = modulation_source[i];
      poly_float modulation_shift = modulation_value * polarity_pre_scale - bipolar;
      poly_float modulation_abs = poly_float::abs(modulation_shift);
      poly_mask sign_mask = poly_float::sign_mask(modulation_shift);

      poly_float shifted_modulation = futils::powerScale(modulation_abs, current_power);
      poly_float pre_modulation = current_amount * shifted_modulation;
      dest[i] = (pre_modulation ^ sign_mask) * polarity_post_scale;
    }

    output(kModulationPreScale)->buffer[0] = dest[0] * (1.0f / (*destination_scale_));
    output(kModulationOutput)->trigger_value = dest[0];
  }

  void ModulationConnectionProcessor::processAudioRateRemappedAndMorphed(int num_samples, const Output* source,
                                                                         poly_float power) {
    poly_float* dest = output(kModulationOutput)->buffer;
    const poly_float* modulation_source = source->buffer;
    poly_float bipolar_offset = -bipolar_->value() * 0.5f;

    poly_float bipolar = bipolar_->value();
    poly_float polarity_pre_scale = bipolar + 1.0f;
    poly_float polarity_post_scale = -bipolar * 0.5f + 1.0f;
    polarity_post_scale *= poly_float(1.0f) - (constants::kRightOne * 2.0f * stereo_->value());

    poly_float current_amount = modulation_amount_;
    poly_float current_power = power_;

    poly_float modulation_amount = utils::clamp(input(kModulationAmount)->at(0), -1.0f, 1.0f) * aux_factor_;
    modulation_amount_ = modulation_amount * (*destination_scale_);

    poly_mask reset_mask = getResetMask(kReset);
    current_amount = utils::maskLoad(current_amount, modulation_amount_, reset_mask);
    current_power = utils::maskLoad(current_power, power, reset_mask);

    float sample_inc = 1.0f / num_samples;
    poly_float delta_amount = (modulation_amount_ - current_amount) * sample_inc;
    poly_float delta_power = (power - current_power) * sample_inc;

    mono_float* buffer = map_generator_->getCubicInterpolationBuffer();
    for (int i = 0; i < num_samples; ++i) {
      current_amount += delta_amount;
      current_power += delta_power;
      poly_float modulation_value = modulation_source[i];

      mono_float resolution = map_generator_->resolution();
      poly_float boost = utils::clamp(modulation_value * resolution, 0.0f, resolution);
      poly_int indices = utils::clamp(utils::toInt(boost), 0, resolution - 1);
      poly_float t = boost - utils::toFloat(indices);

      matrix interpolation_matrix = utils::getCatmullInterpolationMatrix(t);
      matrix value_matrix = utils::getValueMatrix(buffer, indices);

      value_matrix.transpose();
      modulation_value = utils::clamp(interpolation_matrix.multiplyAndSumRows(value_matrix), -1.0f, 1.0f);

      poly_float modulation_shift = modulation_value * polarity_pre_scale - bipolar;
      poly_float modulation_abs = poly_float::abs(modulation_shift);
      poly_mask sign_mask = poly_float::sign_mask(modulation_shift);

      poly_float shifted_modulation = futils::powerScale(modulation_abs, current_power);
      poly_float pre_modulation = current_amount * shifted_modulation;
      dest[i] = (pre_modulation ^ sign_mask) * polarity_post_scale;
    }

    output(kModulationPreScale)->buffer[0] = dest[0] * (1.0f / (*destination_scale_));
    output(kModulationOutput)->trigger_value = dest[0];
  }

  void ModulationConnectionProcessor::processAudioRateRemapped(int num_samples, const Output* source) {
    poly_float* dest = output(kModulationOutput)->buffer;
    const poly_float* modulation_source = source->buffer;

    poly_float bipolar_offset = -bipolar_->value() * 0.5f;
    poly_float current_amount = modulation_amount_;
    poly_float stereo_scale = poly_float(1.0f) - (constants::kRightOne * 2.0f * stereo_->value());
    poly_float modulation_amount = utils::clamp(input(kModulationAmount)->at(0), -1.0f, 1.0f) * stereo_scale * aux_factor_;
    modulation_amount_ = modulation_amount * (*destination_scale_);
    current_amount = utils::maskLoad(current_amount, modulation_amount_, getResetMask(kReset));
    poly_float delta_amount = (modulation_amount_ - current_amount) * (1.0f / num_samples);

    mono_float* buffer = map_generator_->getCubicInterpolationBuffer();
    for (int i = 0; i < num_samples; ++i) {
      current_amount += delta_amount;
      poly_float modulation_value = modulation_source[i];

      mono_float resolution = map_generator_->resolution();
      poly_float boost = utils::clamp(modulation_value * resolution, 0.0f, resolution);
      poly_int indices = utils::clamp(utils::toInt(boost), 0, resolution - 1);
      poly_float t = boost - utils::toFloat(indices);

      matrix interpolation_matrix = utils::getCatmullInterpolationMatrix(t);
      matrix value_matrix = utils::getValueMatrix(buffer, indices);

      value_matrix.transpose();

      modulation_value = utils::clamp(interpolation_matrix.multiplyAndSumRows(value_matrix), -1.0f, 1.0f);
      dest[i] = (modulation_value + bipolar_offset) * current_amount;
    }

    output(kModulationPreScale)->buffer[0] = (modulation_source[0] + bipolar_offset) * modulation_amount;
    output(kModulationOutput)->trigger_value = dest[0];
  }

  void ModulationConnectionProcessor::processControlRate(const Output* source) {
    if (bypass_->value()) {
      output(kModulationOutput)->buffer[0] = 0.0f;
      output(kModulationOutput)->trigger_value = 0.0f;
      return;
    }

    poly_float modulation_input = utils::clamp(source->buffer[0], 0.0f, 1.0f);

    if (extras_->glide_active) {
      // velocity / note curve "legato" (Q17): on a legato note the source glides to its new value over the portamento time; a
      // fresh voice (reset) starts on its value
      mono_float rate = 1.0f - std::exp(-block_seconds_ / std::max(extras_->glide_seconds, 0.0001f));
      glide_value_ = utils::maskLoad(glide_value_ + (modulation_input - glide_value_) * rate, modulation_input, getResetMask(kReset));
      modulation_input = glide_value_;
    }

    if (updateSlewRates(block_seconds_)) {
      // GROUPB WP-08: RISE / FALL in front of the curve; a fresh voice (reset) starts on its value
      slew_value_ = utils::maskLoad(slew_value_, modulation_input, getResetMask(kReset));
      modulation_input = slewStep(modulation_input);
    }
    else
      slew_value_ = modulation_input;   // the slew starts from the source when RISE / FALL are switched on

    if (!map_generator_->linear()) {
      mono_float* buffer = map_generator_->getCubicInterpolationBuffer();
      mono_float resolution = map_generator_->resolution();
      poly_float boost = utils::clamp(modulation_input * resolution, 0.0f, resolution);
      poly_int indices = utils::clamp(utils::toInt(boost), 0, resolution - 1);
      poly_float t = boost - utils::toFloat(indices);

      matrix interpolation_matrix = utils::getCatmullInterpolationMatrix(t);
      matrix value_matrix = utils::getValueMatrix(buffer, indices);

      value_matrix.transpose();

      modulation_input = utils::clamp(interpolation_matrix.multiplyAndSumRows(value_matrix), -1.0f, 1.0f);
    }

    poly_float bipolar = bipolar_->value();
    poly_float polarity_pre_scale = bipolar + 1.0f;
    poly_float polarity_post_scale = -bipolar * 0.5f + 1.0f;
    polarity_post_scale *= poly_float(1.0f) - (constants::kRightOne * 2.0f * stereo_->value());

    poly_float modulation_shift = modulation_input * polarity_pre_scale - bipolar;
    poly_float modulation_abs = poly_float::abs(modulation_shift);
    poly_mask sign_mask = poly_float::sign_mask(modulation_shift);

    poly_float power = -input(kModulationPower)->at(0);
    poly_float shifted_modulation = futils::powerScale(modulation_abs, power);
    poly_float modulation_amount = utils::clamp(input(kModulationAmount)->at(0), -1.0f, 1.0f) * aux_factor_;
    poly_float pre_modulation = modulation_amount * shifted_modulation;
    poly_float raw_modulation = (pre_modulation ^ sign_mask) * polarity_post_scale;
    output(kModulationPreScale)->buffer[0] = raw_modulation;
    output(kModulationOutput)->buffer[0] = raw_modulation * (*destination_scale_);
    VITAL_ASSERT(utils::isFinite(output()->buffer[0]));
  }
} // namespace sbvital
