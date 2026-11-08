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

#include "filters_module.h"
#include "filter_module.h"

namespace sbvital {

  FiltersModule::FiltersModule() : SynthModule(kNumInputs, kNumOutputs), filter_1_(nullptr), filter_2_(nullptr),
                                   filter_1_filter_input_(nullptr), filter_2_filter_input_(nullptr) {
    filter_1_input_ = std::make_shared<Output>();
    filter_2_input_ = std::make_shared<Output>();
  }

  void FiltersModule::init() {
    filter_1_filter_input_ = createBaseControl("filter_1_filter_input");
    filter_1_ = new FilterModule("filter_1");
    addSubmodule(filter_1_);
    addProcessor(filter_1_);

    filter_1_->plug(filter_1_input_.get(), FilterModule::kAudio);
    filter_1_->useInput(input(kReset), FilterModule::kReset);
    filter_1_->useInput(input(kKeytrack), FilterModule::kKeytrack);
    filter_1_->useInput(input(kMidi), FilterModule::kMidi);

    filter_2_filter_input_ = createBaseControl("filter_2_filter_input");
    filter_2_ = new FilterModule("filter_2");
    addSubmodule(filter_2_);
    addProcessor(filter_2_);

    filter_2_->plug(filter_2_input_.get(), FilterModule::kAudio);
    filter_2_->useInput(input(kReset), FilterModule::kReset);
    filter_2_->useInput(input(kKeytrack), FilterModule::kKeytrack);
    filter_2_->useInput(input(kMidi), FilterModule::kMidi);

    SynthModule::init();
  }

  void FiltersModule::processParallel(int num_samples) {
    filter_1_input_->buffer = input(kFilter1Input)->source->buffer;
    filter_2_input_->buffer = input(kFilter2Input)->source->buffer;

    getLocalProcessor(filter_1_)->process(num_samples);
    getLocalProcessor(filter_2_)->process(num_samples);

    poly_float* output_buffer = output()->buffer;
    const poly_float* filter_1_buffer = filter_1_->output()->buffer;
    const poly_float* filter_2_buffer = filter_2_->output()->buffer;

    for (int i = 0; i < num_samples; ++i)
      output_buffer[i] = filter_1_buffer[i] + filter_2_buffer[i];
  }

  void FiltersModule::processSerialForward(int num_samples) {
    filter_1_input_->buffer = input(kFilter1Input)->source->buffer;
    filter_2_input_->buffer = filter_2_input_->owned_buffer.get();

    getLocalProcessor(filter_1_)->process(num_samples);

    poly_float* filter_2_input_buffer = filter_2_input_->buffer;
    const poly_float* filter_1_output_buffer = filter_1_->output()->buffer;
    const poly_float* filter_2_straight_input = input(kFilter2Input)->source->buffer;

    for (int i = 0; i < num_samples; ++i)
      filter_2_input_buffer[i] = filter_1_output_buffer[i] + filter_2_straight_input[i];

    getLocalProcessor(filter_2_)->process(num_samples);
    utils::copyBuffer(output()->buffer, filter_2_->output()->buffer, num_samples);
  }

  void FiltersModule::processSerialBackward(int num_samples) {
    filter_1_input_->buffer = filter_1_input_->owned_buffer.get();
    filter_2_input_->buffer = input(kFilter2Input)->source->buffer;

    getLocalProcessor(filter_2_)->process(num_samples);

    poly_float* filter_1_input_buffer = filter_1_input_->buffer;
    const poly_float* filter_2_output_buffer = filter_2_->output()->buffer;
    const poly_float* filter_1_straight_input = input(kFilter1Input)->source->buffer;

    for (int i = 0; i < num_samples; ++i)
      filter_1_input_buffer[i] = filter_2_output_buffer[i] + filter_1_straight_input[i];

    getLocalProcessor(filter_1_)->process(num_samples);
    utils::copyBuffer(output()->buffer, filter_1_->output()->buffer, num_samples);
  }

  // SynthyBoy: destination of each filter (effects only, direct out, none) and the bus sends. In series the filter that feeds the other
  // one has no destination of its own (its output goes into the other filter); a send always taps the filter's own output.
  void FiltersModule::route(int num_samples, int mode) {
    const int destination1 = static_cast<int>(filter_1_->getDestinationValue()->value());
    const int destination2 = static_cast<int>(filter_2_->getDestinationValue()->value());
    poly_float* direct = output(kDirect)->buffer;
    utils::zeroBuffer(direct, num_samples);

    if (destination1 != 0 || destination2 != 0) {
      poly_float* main_out = output(kOutput)->buffer;
      const poly_float* f1 = filter_1_->output()->buffer;
      const poly_float* f2 = filter_2_->output()->buffer;
      utils::zeroBuffer(main_out, num_samples);
      const bool use1 = mode != 1;  // mode: 0 parallel, 1 series 1 -> 2, 2 series 2 -> 1
      const bool use2 = mode != 2;
      for (int i = 0; i < num_samples; ++i) {
        if (use1) {
          if (destination1 == 0)
            main_out[i] += f1[i];
          else if (destination1 == 1)
            direct[i] += f1[i];
        }
        if (use2) {
          if (destination2 == 0)
            main_out[i] += f2[i];
          else if (destination2 == 1)
            direct[i] += f2[i];
        }
      }
    }

    poly_float* bus1 = output(kBus1)->buffer;
    poly_float* bus2 = output(kBus2)->buffer;
    const poly_float* a1 = filter_1_->output(FilterModule::kBus1Send)->buffer;
    const poly_float* a2 = filter_1_->output(FilterModule::kBus2Send)->buffer;
    const poly_float* b1 = filter_2_->output(FilterModule::kBus1Send)->buffer;
    const poly_float* b2 = filter_2_->output(FilterModule::kBus2Send)->buffer;
    for (int i = 0; i < num_samples; ++i) {
      bus1[i] = a1[i] + b1[i];
      bus2[i] = a2[i] + b2[i];
    }
  }

  void FiltersModule::process(int num_samples) {
    int mode = 0;
    if (filter_1_filter_input_->value() && filter_1_->getOnValue()->value()) {
      processSerialBackward(num_samples);
      mode = 2;
    }
    else if (filter_2_filter_input_->value() && filter_2_->getOnValue()->value()) {
      processSerialForward(num_samples);
      mode = 1;
    }
    else
      processParallel(num_samples);
    route(num_samples, mode);
  }
} // namespace sbvital
