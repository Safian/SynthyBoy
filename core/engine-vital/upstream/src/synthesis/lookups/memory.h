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

#pragma once

#include "common.h"

#include <algorithm>
#include <cmath>

#include "poly_utils.h"

namespace sbvital {

  template<size_t kChannels>
  class MemoryTemplate {
    public:
      static constexpr mono_float kMinPeriod = 2.0f;
      static constexpr int kExtraInterpolationValues = 3;

      MemoryTemplate(int size) : offset_(0) {
        size_ = utils::nextPowerOfTwo(size);
        bitmask_ = size_ - 1;
        for (int i = 0; i < kChannels; ++i) {
          memories_[i] = std::make_unique<mono_float[]>(2 * size_);
          buffers_[i] = memories_[i].get();
        }
      }

      MemoryTemplate(const MemoryTemplate& other) {
        for (int i = 0; i < poly_float::kSize; ++i) {
          memories_[i] = std::make_unique<mono_float[]>(2 * other.size_);
          buffers_[i] = memories_[i].get();
        }

        size_ = other.size_;
        bitmask_ = other.bitmask_;
        offset_ = other.offset_;
      }

      virtual ~MemoryTemplate() { }

      void push(poly_float sample) {
        offset_ = (offset_ + 1) & bitmask_;
        for (int i = 0; i < kChannels; ++i) {
          mono_float val = sample[i];
          buffers_[i][offset_] = val;
          buffers_[i][offset_ + size_] = val;
        }

        VITAL_ASSERT(utils::isFinite(sample));
      }

      /** SynthyBoy (13-new-keys.md 4.17, delay.hq): the value `past` samples ago (3 .. size - 5), read with an 8-point Lagrange interpolator
          instead of the 4-point Catmull-Rom of get (): flat up to near Nyquist for fractional and modulated delay times. One lane per channel. */
      force_inline poly_float getHighQuality(poly_float past) const {
        constexpr int kPoints = 8;
        float result[poly_float::kSize] = {};
        for (size_t c = 0; c < kChannels; ++c) {
          const float delay = std::min(std::max(past[c], 3.0f), static_cast<float>(size_) - 5.0f);
          const int whole = static_cast<int>(delay);
          const float t = delay - static_cast<float>(whole);
          // nodes at the delays whole + 4 - k, k = 0 .. 7, i.e. offsets 4 .. -3 from `whole`
          float factors[kPoints];
          for (int k = 0; k < kPoints; ++k) {
            float numerator = 1.0f, denominator = 1.0f;
            const float node = static_cast<float>(4 - k);
            for (int j = 0; j < kPoints; ++j) {
              if (j == k)
                continue;
              const float other = static_cast<float>(4 - j);
              numerator *= t - other;
              denominator *= node - other;
            }
            factors[k] = numerator / denominator;
          }
          const unsigned base = (offset_ - static_cast<unsigned>(whole) - 4u) & bitmask_;
          const mono_float* buffer = buffers_[c];
          float sum = 0.0f;
          for (int k = 0; k < kPoints; ++k)
            sum += factors[k] * buffer[base + static_cast<unsigned>(k)];
          result[c] = sum;
        }
        return utils::toPolyFloatFromUnaligned(result);
      }

      void clearMemory(int num, poly_mask clear_mask) {
        int start = (offset_ - (num + kExtraInterpolationValues)) & bitmask_;
        int end = (offset_ + kExtraInterpolationValues) & bitmask_;

        for (int p = 0; p < kChannels; ++p) {
          if (clear_mask[p]) {
            mono_float* buffer = buffers_[p];
            for (int i = start; i != end; i = (i + 1) & bitmask_)
              buffer[i] = 0.0f;
            buffer[end] = 0.0f;

            for (int i = 0; i < kExtraInterpolationValues; ++i)
              buffer[size_ + i] = 0.0f;
          }
        }
      }

      void clearAll() {
        for (int c = 0; c < kChannels; ++c)
          memset(buffers_[c], 0, 2 * size_ * sizeof(mono_float));
      }

      void readSamples(mono_float* output, int num_samples, int offset, int channel) const {
        mono_float* buffer = buffers_[channel];
        int bitmask = bitmask_;
        int start_index = (offset_ - num_samples - offset) & bitmask;
        for (int i = 0; i < num_samples; ++i)
          output[i] = buffer[(i + start_index) & bitmask];
      }

      unsigned int getOffset() const { return offset_; }

      void setOffset(int offset) { offset_ = offset; }

      int getSize() const {
        return size_;
      }

      int getMaxPeriod() const {
        return size_ - kExtraInterpolationValues;
      }

    protected:
      std::unique_ptr<mono_float[]> memories_[poly_float::kSize];
      mono_float* buffers_[poly_float::kSize];
      unsigned int size_;
      unsigned int bitmask_;
      unsigned int offset_;
  };

  class Memory : public MemoryTemplate<poly_float::kSize> {
    public:
      Memory(int size) : MemoryTemplate(size) { }
      Memory(Memory& other) : MemoryTemplate(other) { }

      force_inline poly_float get(poly_float past) const {
        VITAL_ASSERT(poly_float::lessThan(past, kMinPeriod).sum() == 0);
        VITAL_ASSERT(poly_float::greaterThan(past, getMaxPeriod()).sum() == 0);
        poly_int past_index = utils::toInt(past);
        poly_float t = utils::toFloat(past_index) - past + 1.0f;
        matrix interpolation_matrix = utils::getCatmullInterpolationMatrix(t);

        poly_int indices = (poly_int(offset_) - past_index - 2) & poly_int(bitmask_);
        matrix value_matrix = utils::getValueMatrix(buffers_, indices);
        value_matrix.transpose();
        return interpolation_matrix.multiplyAndSumRows(value_matrix);
      }
  };

  class StereoMemory : public MemoryTemplate<2> {
    public:
      StereoMemory(int size) : MemoryTemplate(size) { }
      StereoMemory(StereoMemory& other) : MemoryTemplate(other) { }

      force_inline poly_float get(poly_float past) const {
        VITAL_ASSERT(poly_float::lessThan(past, 2.0f).anyMask() == 0);
        VITAL_ASSERT(poly_float::greaterThan(past, getMaxPeriod()).anyMask() == 0);
        poly_int past_index = utils::toInt(past);
        poly_float t = utils::toFloat(past_index) - past + 1.0f;
        matrix interpolation_matrix = utils::getCatmullInterpolationMatrix(t);

        poly_int indices = (poly_int(offset_) - past_index - 2) & poly_int(bitmask_);
        matrix value_matrix(utils::toPolyFloatFromUnaligned(buffers_[0] + indices[0]),
                            utils::toPolyFloatFromUnaligned(buffers_[1] + indices[1]), 0.0f, 0.0f);
        value_matrix.transpose();
        return interpolation_matrix.multiplyAndSumRows(value_matrix);
      }
  };
} // namespace sbvital

