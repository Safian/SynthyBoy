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

#include "sample_source.h"
#include "futils.h"
#include "synth_constants.h"

#include <thread>

namespace sbvital {

  namespace {
    const std::string kDefaultName = "White Noise";
    const mono_float kUpsampleCoefficients[SampleSource::kNumUpsampleTaps] = {
      -0.000159813115702086552469274316479186382f,
      0.000225405365781280835058009159865832771f,
      -0.000378616814007205900686342525673921955f,
      0.000594907533596884547516525643118256994f,
      -0.000890530515941817101682742574553230952f,
      0.001284040046393844676508866342601322685f,
      -0.001796543223638378920792302295694753411f,
      0.002451862103068884121692683208948437823f,
      -0.003276873018553504678107568537370752892f,
      0.004302012661141991003987961050825106213f,
      -0.005561976429934398571952591794342879439f,
      0.007097105459677621741576558633823879063f,
      -0.008955232561651555595050311353588767815f,
      0.011195057708851860467369476737076183781f,
      -0.013890548104646217864033275191104621626f,
      0.017139719620821350365424962092220084742f,
      -0.021077036318492142763503238711564335972f,
      0.025897497908177177783350941808748757467f,
      -0.03189749744607761616776997470878995955f,
      0.039555400754278852160084056777122896165f,
      -0.049699764879031965714162311087420675904f,
      0.063901297378209126476278356676630210131f,
      -0.08553732517833501081128133591846562922f,
      0.123410206086688845061871688812971115112f,
      -0.209837893291539345774765479291090741754f,
      0.63582677174146173815216798175242729485f,
      0.63582677174146173815216798175242729485f,
      -0.209837893291539345774765479291090741754f,
      0.123410206086688845061871688812971115112f,
      -0.08553732517833501081128133591846562922f,
      0.063901297378209126476278356676630210131f,
      -0.049699764879031965714162311087420675904f,
      0.039555400754278852160084056777122896165f,
      -0.03189749744607761616776997470878995955f,
      0.025897497908177177783350941808748757467f,
      -0.021077036318492142763503238711564335972f,
      0.017139719620821350365424962092220084742f,
      -0.013890548104646217864033275191104621626f,
      0.011195057708851860467369476737076183781f,
      -0.008955232561651555595050311353588767815f,
      0.007097105459677621741576558633823879063f,
      -0.005561976429934398571952591794342879439f,
      0.004302012661141991003987961050825106213f,
      -0.003276873018553504678107568537370752892f,
      0.002451862103068884121692683208948437823f,
      -0.001796543223638378920792302295694753411f,
      0.001284040046393844676508866342601322685f,
      -0.000890530515941817101682742574553230952f,
      0.000594907533596884547516525643118256994f,
      -0.000378616814007205900686342525673921955f,
      0.000225405365781280835058009159865832771f,
      -0.000159813115702086552469274316479186382f
    };

    const mono_float kDownsampleCoefficients[SampleSource::kNumDownsampleTaps] = {
      -0.0013796309221920304f,
      -0.0008322130675804714f,
      0.0030100376204235577f,
      0.00666031332700994f,
      0.0040620073330527315f,
      -0.003019073425031439f,
      -0.004450269579432283f,
      0.0030526281279541555f,
      0.007614361286489334f,
      -0.000546514301955849f,
      -0.010099270019478761f,
      -0.003465846383906444f,
      0.011760981765402261f,
      0.009402148654924303f,
      -0.011429260748035207f,
      -0.016935843679984037f,
      0.008026778073943279f,
      0.025557280950428782f,
      -0.0002093220301655805f,
      -0.03448379812688787f,
      -0.013983156365753766f,
      0.04279770831566429f,
      0.03889228625534586f,
      -0.049566024787935245f,
      -0.09025827224454164f,
      0.05398926693924448f,
      0.31285587793730246f,
      0.4444714418837066f,
      0.31285587793730246f,
      0.05398926693924448f,
      -0.09025827224454164f,
      -0.049566024787935245f,
      0.03889228625534586f,
      0.04279770831566429f,
      -0.013983156365753766f,
      -0.03448379812688787f,
      -0.0002093220301655805f,
      0.025557280950428782f,
      0.008026778073943279f,
      -0.016935843679984037f,
      -0.011429260748035207f,
      0.009402148654924303f,
      0.011760981765402261f,
      -0.003465846383906444f,
      -0.010099270019478761f,
      -0.000546514301955849f,
      0.007614361286489334f,
      0.0030526281279541555f,
      -0.004450269579432283f,
      -0.003019073425031439f,
      0.0040620073330527315f,
      0.00666031332700994f,
      0.0030100376204235577f,
      -0.0008322130675804714f,
      -0.0013796309221920304f
    };

    force_inline mono_float getFilteredSample(const mono_float* buffer, int index, int size) {
      int radius = SampleSource::kNumDownsampleTaps / 2;
      int start = std::max(0, index - radius);
      int end = std::min(size - 1, index + radius);
      mono_float total = 0.0f;
      for (int i = start; i <= end; ++i) {
        mono_float coefficient = kDownsampleCoefficients[i - index + radius];
        total += coefficient *  buffer[i];
      }
      return total;
    }

    force_inline mono_float getFilteredLoopSample(const mono_float* buffer, int index, int size) {
      int radius = SampleSource::kNumDownsampleTaps / 2;
      int start = index - radius;
      int end = index + radius;
      mono_float total = 0.0f;
      for (int i = start; i <= end; ++i) {
        int buffer_index = (i + size * radius) % size;
        mono_float coefficient = kDownsampleCoefficients[i - index + radius];
        total += coefficient * buffer[buffer_index];
      }
      return total;
    }


    force_inline mono_float getInterpolatedSample(const mono_float* buffer, int index, int size) {
      int radius = SampleSource::kNumUpsampleTaps / 2;
      int start = std::max(0, index - radius + 1);
      int end = std::min(size - 1, index + radius);
      mono_float total = 0.0f;
      for (int i = start; i <= end; ++i) {
        int coefficient_index = i - index + radius - 1;
        VITAL_ASSERT(coefficient_index >= 0 && coefficient_index < SampleSource::kNumUpsampleTaps);
        mono_float coefficient = kUpsampleCoefficients[coefficient_index];
        total += coefficient * buffer[i];
      }
      return total;
    }

    void upsample(const mono_float* original, mono_float* dest, int original_size, int dest_size) {
      for (int i = 0; i < original_size; ++i) {
        float value1 = original[i];
        float value2 = getInterpolatedSample(original, i, original_size);
        dest[2 * i] = value1;
        dest[2 * i + 1] = value2;
      }
    }

    void downsample(const mono_float* original, mono_float* dest, int original_size, int dest_size) {
      for (int i = 0; i < dest_size; ++i)
        dest[i] = getFilteredSample(original, 2 * i, original_size);
    }

    void downsampleLoop(const mono_float* original, mono_float* dest, int original_size, int dest_size) {
      for (int i = 0; i < dest_size; ++i)
        dest[i] = getFilteredLoopSample(original, 2 * i, original_size);
    }

    void createBandLimitedBuffers(std::vector<std::unique_ptr<mono_float[]>>& destination,
                                  std::vector<std::unique_ptr<mono_float[]>>& loop_destination,
                                  const mono_float* buffer, int size) {
      destination.push_back(std::make_unique<mono_float[]>(size + 2 * Sample::kBufferSamples));
      loop_destination.push_back(std::make_unique<mono_float[]>(size + 2 * Sample::kBufferSamples));

      mono_float* play_buffer = destination.back().get();
      mono_float* loop_buffer = loop_destination.back().get();
      memcpy(play_buffer + Sample::kBufferSamples, buffer, size * sizeof(mono_float));
      memcpy(loop_buffer + Sample::kBufferSamples, buffer, size * sizeof(mono_float));

      for (int i = 0; i < Sample::kBufferSamples; ++i) {
        play_buffer[i] = 0.0f;
        play_buffer[size + Sample::kBufferSamples + i] = 0.0f;

        loop_buffer[i] = loop_buffer[size + i];
        loop_buffer[size + Sample::kBufferSamples + i] = loop_buffer[Sample::kBufferSamples + i];
      }

      int current_size = size;
      for (int i = 0; i < Sample::kUpsampleTimes; ++i) {
        int upsampled_size = current_size * 2;
        int num_samples = upsampled_size + 2 * Sample::kBufferSamples;
        destination.insert(destination.begin(), std::make_unique<mono_float[]>(num_samples));
        loop_destination.insert(loop_destination.begin(), std::make_unique<mono_float[]>(num_samples));

        mono_float* next_buffer = destination.front().get();
        mono_float* next_loop_buffer = loop_destination.front().get();

        upsample(play_buffer + Sample::kBufferSamples, next_buffer + Sample::kBufferSamples,
                 current_size, upsampled_size);
        memcpy(next_loop_buffer, next_buffer, num_samples * sizeof(mono_float));
        current_size = upsampled_size;
      }

      play_buffer = destination.back().get();
      loop_buffer = loop_destination.back().get();
      current_size = size;

      while (current_size >= Sample::kMinSize) {
        int next_size = (current_size + 1) / 2;
        destination.push_back(std::make_unique<mono_float[]>(next_size + 2 * Sample::kBufferSamples));
        loop_destination.push_back(std::make_unique<mono_float[]>(next_size + 2 * Sample::kBufferSamples));

        mono_float* next_buffer = destination.back().get();
        mono_float* next_loop_buffer = loop_destination.back().get();

        downsample(play_buffer + Sample::kBufferSamples, next_buffer + Sample::kBufferSamples,
                   current_size, next_size);
        downsampleLoop(loop_buffer + Sample::kBufferSamples, next_loop_buffer + Sample::kBufferSamples,
                       current_size, next_size);

        for (int i = 0; i < Sample::kBufferSamples; ++i) {
          next_buffer[i] = 0.0f;
          next_buffer[next_size + Sample::kBufferSamples + i] = 0.0f;

          next_loop_buffer[i] = loop_buffer[next_size + i];
          next_loop_buffer[next_size + Sample::kBufferSamples + i] = next_loop_buffer[Sample::kBufferSamples + i];
        }

        play_buffer = next_buffer;
        loop_buffer = next_loop_buffer;
        current_size = next_size;
      }
    }
  }

  Sample::Sample() : name_(kDefaultName), current_data_(nullptr), active_audio_data_(nullptr) {
    init();
  }

  void Sample::loadSample(const mono_float* buffer, int size, int sample_rate) {
    static constexpr int kMaxSize = 1764000;

    VITAL_ASSERT(active_audio_data_.is_lock_free());

    size = std::min(size, kMaxSize);
    std::unique_ptr<SampleData> old_data = std::move(data_);
    data_ = std::make_unique<SampleData>(size, sample_rate, false);
    createBandLimitedBuffers(data_->left_buffers, data_->left_loop_buffers, buffer, size);

    current_data_ = data_.get();
    while (active_audio_data_.load())
      std::this_thread::yield(); // Wait for audio thread to finish using old_data.
  }

  void Sample::loadSample(const mono_float* left_buffer, const mono_float* right_buffer, int size, int sample_rate) {
    std::unique_ptr<SampleData> old_data = std::move(data_);
    data_ = std::make_unique<SampleData>(size, sample_rate, true);
    createBandLimitedBuffers(data_->left_buffers, data_->left_loop_buffers, left_buffer, size);
    createBandLimitedBuffers(data_->right_buffers, data_->right_loop_buffers, right_buffer, size);

    current_data_ = data_.get();
    while (active_audio_data_.load())
      std::this_thread::yield(); // Wait for audio thread to finish using old_data.
  }

  namespace {
  void generateNoiseBuffer(int type, mono_float* buffer, int length, uint32_t salt = 0) {
    uint32_t seed = 0x12345678u + static_cast<uint32_t>(type) * 0x9e3779b9u + salt;
    auto rnd = [&seed]() -> float {
      seed = seed * 1664525u + 1013904223u;
      return static_cast<float>(static_cast<int32_t>(seed)) * (1.0f / 2147483648.0f);
    };

    switch (type) {
      case Sample::kWhite: {
        for (int i = 0; i < length; ++i)
          buffer[i] = rnd() * 0.9f;
        break;
      }
      case Sample::kPink: {
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f, b4 = 0.0f, b5 = 0.0f, b6 = 0.0f;
        for (int i = 0; i < length; ++i) {
          float white = rnd();
          b0 = 0.99886f * b0 + white * 0.0555179f;
          b1 = 0.99332f * b1 + white * 0.0750759f;
          b2 = 0.96900f * b2 + white * 0.1538520f;
          b3 = 0.86650f * b3 + white * 0.3104856f;
          b4 = 0.55000f * b4 + white * 0.5329522f;
          b5 = -0.7616f * b5 - white * 0.0168980f;
          buffer[i] = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f) * 0.18f;
          b6 = white * 0.115926f;
        }
        break;
      }
      case Sample::kBrown: {
        float brown = 0.0f;
        for (int i = 0; i < length; ++i) {
          float white = rnd();
          brown = (brown + 0.02f * white) * 0.98f;
          buffer[i] = brown * 3.5f;
        }
        break;
      }
      case Sample::kGeiger: {
        float click = 0.0f;
        for (int i = 0; i < length; ++i) {
          if (std::fabs(rnd()) < 0.0015f)
            click += (rnd() > 0.0f ? 0.9f : -0.9f);
          buffer[i] = click;
          click *= 0.92f;
        }
        break;
      }
      case Sample::kTapeHiss: {
        float lp = 0.0f;
        for (int i = 0; i < length; ++i) {
          float white = rnd();
          lp += 0.35f * (white - lp);
          buffer[i] = std::tanh(lp * 1.5f) * 0.8f;
        }
        break;
      }
      case Sample::kVinylClicks: {
        float rumble = 0.0f, pop = 0.0f;
        for (int i = 0; i < length; ++i) {
          float white = rnd();
          rumble += 0.04f * (white - rumble);
          if (std::fabs(rnd()) < 0.0008f)
            pop = (rnd() > 0.0f ? 0.85f : -0.85f);
          buffer[i] = rumble * 0.3f + pop;
          pop *= 0.85f;
        }
        break;
      }
      case Sample::kDigitalHash: {
        float held = 0.0f;
        int step = 0;
        for (int i = 0; i < length; ++i) {
          if ((step++ % 6) == 0) {
            float raw = rnd();
            held = std::floor(raw * 8.0f + 0.5f) * (1.0f / 8.0f);
          }
          buffer[i] = held * 0.9f;
        }
        break;
      }
      case Sample::kVelvetNoise: {
        int grid = 16;
        for (int i = 0; i < length; i += grid) {
          int offset = static_cast<int>(std::fabs(rnd()) * (grid - 1));
          float sign = (rnd() > 0.0f ? 0.9f : -0.9f);
          for (int j = 0; j < grid && (i + j) < length; ++j)
            buffer[i + j] = (j == offset) ? sign : 0.0f;
        }
        break;
      }
      case Sample::kAtmosphere: {
        float s1 = 0.0f, s2 = 0.0f;
        float f1 = 0.08f;
        for (int i = 0; i < length; ++i) {
          float white = rnd();
          s1 += f1 * (white - s1);
          s2 += f1 * (s1 - s2);
          buffer[i] = (s1 - s2) * 2.8f;
        }
        break;
      }
      default: {
        for (int i = 0; i < length; ++i)
          buffer[i] = rnd() * 0.9f;
        break;
      }
    }

    // Small circular crossfade at boundary to ensure zero click on loop
    constexpr int kXfade = 128;
    for (int i = 0; i < kXfade && i < length; ++i) {
      float t = static_cast<float>(i) / static_cast<float>(kXfade);
      buffer[i] = buffer[i] * t + buffer[length - kXfade + i] * (1.0f - t);
    }
  }
  } // namespace

  void Sample::init() {
    name_ = kDefaultName;
    mono_float buffer[kDefaultSampleLength];
    mono_float other[kDefaultSampleLength];

    // White Noise (index 0): identical to upstream Vital
    utils::RandomGenerator random_generator(-0.9f, 0.9f);
    for (int i = 0; i < kDefaultSampleLength; ++i)
      buffer[i] = random_generator.next();
    // GROUPA1 (noise.stereo): the colour samples carry a second, uncorrelated channel (an own generator: the engine's random seed counter
    // must not move, it decides the random LFOs). With STEREO at 0 the right channel is the left one again, bit for bit.
    generateNoiseBuffer(kWhite, other, kDefaultSampleLength, 0x51ED2701u);
    loadSample(buffer, other, kDefaultSampleLength, kDefaultSampleRate);
    data_->colour = true;
    noise_data_[kWhite] = nullptr;

    // Remaining 8 procedural noise sets (1..8)
    for (int t = 1; t < kNumNoiseTypes; ++t) {
      generateNoiseBuffer(t, buffer, kDefaultSampleLength);
      const bool colour = t <= kGeiger;
      noise_data_[t] = std::make_unique<SampleData>(kDefaultSampleLength, kDefaultSampleRate, colour);
      noise_data_[t]->colour = colour;
      createBandLimitedBuffers(noise_data_[t]->left_buffers, noise_data_[t]->left_loop_buffers,
                               buffer, kDefaultSampleLength);
      if (colour) {
        generateNoiseBuffer(t, other, kDefaultSampleLength, 0x51ED2701u);
        createBandLimitedBuffers(noise_data_[t]->right_buffers, noise_data_[t]->right_loop_buffers,
                                 other, kDefaultSampleLength);
      }
    }

    current_data_ = data_.get();
    markUsed();
  }

  std::unique_ptr<Sample::SampleData> Sample::makeUserData(const mono_float* left, const mono_float* right, int size, int sample_rate) {
    static constexpr int kMaxSize = 1764000;
    size = std::max(kMinSize, std::min(size, kMaxSize));
    auto data = std::make_unique<SampleData>(size, sample_rate, right != nullptr);
    createBandLimitedBuffers(data->left_buffers, data->left_loop_buffers, left, size);
    if (right != nullptr)
      createBandLimitedBuffers(data->right_buffers, data->right_loop_buffers, right, size);
    return data;
  }

  void Sample::useUserData(SampleData* data) {
    if (data != nullptr) {
      current_data_ = data;
      markUsed();
    }
    else
      setNoiseType(noise_type_);
  }

  void Sample::setNoiseType(int type) {
    noise_type_ = type;
    if (type == kWhite) {
      current_data_ = data_.get();
      markUsed();
    } else if (type > 0 && type < kNumNoiseTypes && noise_data_[type]) {
      current_data_ = noise_data_[type].get();
      markUsed();
    }
  }

  json Sample::stateToJson() {
    json data;
    data["name"] = name_;
    data["length"] = data_->length;
    data["sample_rate"] = data_->sample_rate;
    std::unique_ptr<int16_t[]> pcm_data = std::make_unique<int16_t[]>(data_->length);
    utils::floatToPcmData(pcm_data.get(), data_->left_buffers[kUpsampleTimes].get(), data_->length);
    std::string encoded = sbcompat::toBase64(pcm_data.get(), sizeof(int16_t) * data_->length);
    data["samples"] = encoded;
    if (data_->stereo) {
      utils::floatToPcmData(pcm_data.get(), data_->right_buffers[kUpsampleTimes].get(), data_->length);
      std::string encoded_stereo = sbcompat::toBase64(pcm_data.get(), sizeof(int16_t) * data_->length);
      data["samples_stereo"] = encoded_stereo;
    }
    return data;
  }

  void Sample::jsonToState(json data) {
    name_ = "";
    if (data.count("name"))
      name_ = data["name"].get<std::string>();

    int length = data["length"];
    int sample_rate = data["sample_rate"];

    std::vector<unsigned char> decoded;
    std::string wave_data = data["samples"];
    sbcompat::fromBase64(wave_data, decoded);
    decoded.resize(length * sizeof(int16_t));
    std::unique_ptr<int16_t[]> pcm_data = std::make_unique<int16_t[]>(length);
    memcpy(pcm_data.get(), decoded.data(), length * sizeof(int16_t));
    std::unique_ptr<mono_float[]> buffer = std::make_unique<mono_float[]>(length);
    utils::pcmToFloatData(buffer.get(), pcm_data.get(), length);

    if (data.count("samples_stereo")) {
      std::vector<unsigned char> decoded_stereo;
      std::string wave_data_stereo = data["samples_stereo"];
      sbcompat::fromBase64(wave_data_stereo, decoded_stereo);
      decoded_stereo.resize(length * sizeof(int16_t));
      std::unique_ptr<int16_t[]> pcm_data_stereo = std::make_unique<int16_t[]>(length);
      memcpy(pcm_data_stereo.get(), decoded_stereo.data(), length * sizeof(int16_t));

      std::unique_ptr<mono_float[]> buffer_stereo = std::make_unique<mono_float[]>(length);
      utils::pcmToFloatData(buffer_stereo.get(), pcm_data_stereo.get(), length);
      loadSample(buffer.get(), buffer_stereo.get(), length, sample_rate);
    }
    else
      loadSample(buffer.get(), length, sample_rate);
  }

  SampleSource::SampleSource() : Processor(kNumInputs, kNumOutputs), pan_amplitude_(0.0f), phase_inc_(0.0f),
                                 random_generator_(0.0f, 1.0f) {
    transpose_quantize_ = 0;
    last_quantized_transpose_ = 0.0f;
    sample_index_ = 0;
    sample_fraction_ = 0.0f;

    sample_ = std::make_shared<Sample>();
    phase_output_ = std::make_shared<cr::Output>();
  }

  void SampleSource::process(int num_samples) {
    sample_->markUsed();

    poly_float current_pan_amplitude = pan_amplitude_;
    poly_float input_pan = utils::clamp(input(kPan)->at(0), -1.0f, 1.0f);
    pan_amplitude_ = futils::panAmplitude(input_pan);

    poly_float input_midi = 0.0f;
    if (input(kKeytrack)->at(0)[0])
      input_midi = input(kMidi)->at(0) - kMidiTrackCenter;

    int transpose_quantize = static_cast<int>(input(kTransposeQuantize)->at(0)[0]);
    poly_float transpose = snapTranspose(input_midi, input(kTranspose)->at(0), transpose_quantize);
    transpose = utils::clamp(transpose + input(kTune)->at(0), kMinTranspose, kMaxTranspose);

    mono_float sample_rate_ratio = (1.0f * sample_->activeSampleRate()) / getSampleRate();
    poly_float current_phase_inc = phase_inc_;
    phase_inc_ = utils::centsToRatio(transpose * kCentsPerNote) * sample_rate_ratio * (1 << Sample::kUpsampleTimes);

    int audio_length = sample_->activeLength();
    poly_mask reset_mask = getResetMask(kReset);
    poly_float reset_offset = utils::toFloat(input(kReset)->source->trigger_offset);
    current_pan_amplitude = utils::maskLoad(current_pan_amplitude, pan_amplitude_, reset_mask);
    current_phase_inc = utils::maskLoad(current_phase_inc, phase_inc_, reset_mask);
    bounce_mask_ = bounce_mask_ & ~reset_mask;
    reset_offset *= current_phase_inc;
    
    poly_float reset_value = -reset_offset;
    if (input(kPhase)->source) {
      poly_float start_phase = utils::clamp(input(kPhase)->at(0), 0.0f, 1.0f);
      if (poly_float::greaterThan (start_phase, 0.0f).anyMask())
        reset_value += start_phase * audio_length;
    }
    if (input(kRandomPhase)->at(0)[0]) {
      reset_value = random_generator_.next() * audio_length;
      reset_value = utils::maskLoad(reset_value, random_generator_.next() * audio_length, constants::kFirstMask);
      reset_value -= reset_offset;
    }
    
    sample_index_ = utils::maskLoad(sample_index_, utils::floor(reset_value), reset_mask);
    sample_fraction_ = utils::maskLoad(sample_fraction_, reset_value - sample_index_, reset_mask);

    bool loop = input(kLoop)->at(0)[0] != 0.0f;
    poly_mask loop_enabled_mask = 0;
    if (loop)
      loop_enabled_mask = constants::kFullMask;

    bool bounce = input(kBounce)->at(0)[0] != 0.0f;
    poly_mask bounce_enabled_mask = 0;
    if (bounce)
      bounce_enabled_mask = constants::kFullMask;
    else
      bounce_mask_ = 0;

    const mono_float* audio_buffers[poly_float::kSize];
    poly_float phase_mult = 1.0f;
    for (int i = 0; i < poly_float::kSize; ++i) {
      int index = sample_->getActiveIndex(phase_inc_[i]);
      if (loop && !bounce) {
        if (i % 2)
          audio_buffers[i] = sample_->getActiveRightLoopBuffer(index);
        else
          audio_buffers[i] = sample_->getActiveLeftLoopBuffer(index);
      }
      else {
        if (i % 2)
          audio_buffers[i] = sample_->getActiveRightBuffer(index);
        else
          audio_buffers[i] = sample_->getActiveLeftBuffer(index);
      }

      phase_mult.set(i, 1.0f / (1 << index));
    }

    mono_float sample_inc = 1.0f / num_samples;
    poly_float delta_pan_amplitude = (pan_amplitude_ - current_pan_amplitude) * sample_inc;
    poly_float delta_phase_inc = (phase_inc_ - current_phase_inc) * sample_inc;

    poly_float* raw_output = output(kRaw)->buffer;
    poly_float current_fraction = sample_fraction_;
    poly_float current_index = utils::min(sample_index_, audio_length);

    poly_mask current_bounce = bounce_mask_;
    poly_int length = audio_length;
    for (int i = 0; i < num_samples; ++i) {
      current_phase_inc += delta_phase_inc;

      poly_float adjusted = utils::maskLoad(current_index, poly_float(audio_length) - current_index, current_bounce);
      poly_float index_phase = utils::max(adjusted, 0.0f) * phase_mult;
      poly_float fraction_phase = current_fraction * phase_mult;
      
      poly_int start_indices = utils::floorToInt(index_phase);
      poly_float rounded_down_phase = utils::toFloat(start_indices);
      poly_float t = index_phase - rounded_down_phase + fraction_phase;

      t = utils::maskLoad(t, poly_float(1.0f) - t, current_bounce);

      VITAL_ASSERT(poly_float::lessThan(utils::toFloat(start_indices), 0.0f).anyMask() == 0);
      VITAL_ASSERT(poly_float::greaterThan(utils::toFloat(start_indices), audio_length).anyMask() == 0);

      matrix interpolation_matrix = utils::getCatmullInterpolationMatrix(t);
      matrix value_matrix = utils::getValueMatrix(audio_buffers, start_indices);
      value_matrix.transpose();
      raw_output[i] = interpolation_matrix.multiplyAndSumRows(value_matrix);
      VITAL_ASSERT(utils::isContained(raw_output[i]));
      // SynthyBoy (noise.loop off): a one-shot sample that has reached its end is silent, it does not hold its last value
      raw_output[i] = utils::maskLoad(raw_output[i], poly_float(0.0f),
                                      poly_float::greaterThanOrEqual(current_index, audio_length) & ~loop_enabled_mask & ~bounce_enabled_mask);

      current_fraction += current_phase_inc;
      poly_float increment = utils::floor(current_fraction);
      current_fraction -= increment;

      current_index += increment;
      poly_mask done_mask = poly_float::greaterThanOrEqual(current_index, audio_length);
      poly_mask bounced_mask = done_mask & ~current_bounce & bounce_enabled_mask;
      poly_mask loop_over_mask = done_mask & (current_bounce | ~bounce_enabled_mask) & loop_enabled_mask;
      current_bounce = (bounced_mask | current_bounce) & ~loop_over_mask;

      current_index = utils::maskLoad(current_index, current_index - audio_length, bounced_mask | loop_over_mask);
      current_index = utils::min(audio_length, current_index);
      current_fraction = current_fraction & ~done_mask;
    }

    bounce_mask_ = current_bounce;
    colourStage(raw_output, num_samples, reset_mask);
    if (reset_mask.anyMask())
      clearOutputBufferForReset(reset_mask, kReset, kRaw);

    const poly_float* level_input = input(kLevel)->source->buffer;
    poly_float* levelled_output = output(kLevelled)->buffer;
    poly_float zero = 0.0f;
    poly_float max = kMaxAmplitude;
    for (int i = 0; i < num_samples; ++i) {
      current_pan_amplitude += delta_pan_amplitude;
      poly_float level = utils::clamp(level_input[i], zero, max);
      levelled_output[i] = current_pan_amplitude * level * level * raw_output[i];
    }

    sample_index_ = current_index;
    sample_fraction_ = current_fraction;
    poly_float phase = utils::maskLoad(sample_index_, poly_float(audio_length) - sample_index_, bounce_mask_);
    phase = phase * (1.0f / audio_length);
    phase_output_->buffer[0] = utils::encodePhaseAndVoice(phase, input(kNoteCount)->at(0));

    sample_->markUnused();
  }

  // GROUPA1 (13-new-keys.md 4.27): STEREO and FILTER of the noise colours (Serum 2 manual p.129). Only the generated colour samples (White, Pink, Brown,
  // Geiger) pass through it; every other sample (the other generators, a user sample) is untouched, as is a colour at STEREO 0 and FILTER 0 (the right
  // channel is then the left one, exactly as before the colour samples had two channels).
  void SampleSource::colourStage(poly_float* raw, int num_samples, poly_mask reset_mask) {
    if (!sample_->activeIsColour()) {
      filter_active_ = false;
      return;
    }

    const poly_float correlation = poly_float(1.0f) - utils::clamp(input(kStereo)->at(0), 0.0f, 1.0f);
    const poly_float uncorrelated = utils::sqrt(poly_float(1.0f) - correlation * correlation);   // keeps the power of the right channel
    for (int i = 0; i < num_samples; ++i) {
      poly_float mixed = correlation * utils::swapStereo(raw[i]) + uncorrelated * raw[i];
      raw[i] = utils::maskLoad(mixed, raw[i], constants::kLeftMask);
    }

    const poly_float amount = utils::clamp(input(kFilter)->at(0), -1.0f, 1.0f);
    const poly_mask active = poly_float::greaterThan(poly_float::abs(amount), 1.0e-4f);
    if (!active.anyMask()) {
      if (filter_active_ && num_samples > 0) {
        filter_lp1_ = filter_lp2_ = raw[num_samples - 1];   // the poles follow the signal while the filter is off: no click when it comes in
      }
      filter_active_ = false;
      return;
    }

    // a two pole low pass (negative: 40 Hz .. 20 kHz) or its complement (positive: 20 Hz .. 16 kHz), log spaced over the knob
    const poly_float magnitude = poly_float::abs(amount);
    const poly_float low_pass_hz = futils::exp(magnitude * -6.2146081f) * 20000.0f;           // 20000 * 0.002^m
    const poly_float high_pass_hz = futils::exp(magnitude * 6.6846117f) * 20.0f;              // 20 * 800^m
    const poly_mask is_low_pass = poly_float::lessThan(amount, 0.0f);
    poly_float cutoff = utils::maskLoad(high_pass_hz, low_pass_hz, is_low_pass);
    cutoff = utils::min(cutoff, 0.45f * getSampleRate());
    poly_float coefficient = poly_float(1.0f) - futils::exp(cutoff * (-6.28318531f / getSampleRate()));
    coefficient = utils::min(coefficient, 0.99f);
    coefficient = utils::maskLoad(poly_float(1.0f), coefficient, active);                     // an idle lane just follows

    poly_float lp1 = filter_lp1_, lp2 = filter_lp2_;
    if (!filter_active_ || reset_mask.anyMask()) {
      poly_float seed = num_samples > 0 ? raw[0] : poly_float(0.0f);
      lp1 = filter_active_ ? utils::maskLoad(lp1, seed, reset_mask) : seed;
      lp2 = filter_active_ ? utils::maskLoad(lp2, seed, reset_mask) : seed;
    }
    for (int i = 0; i < num_samples; ++i) {
      const poly_float x = raw[i];
      lp1 += coefficient * (x - lp1);
      lp2 += coefficient * (lp1 - lp2);
      const poly_float filtered = utils::maskLoad(x - lp2, lp2, is_low_pass);
      raw[i] = utils::maskLoad(x, filtered, active);
    }
    filter_lp1_ = lp1;
    filter_lp2_ = lp2;
    filter_active_ = true;
  }

  force_inline poly_float SampleSource::snapTranspose(poly_float input_midi, poly_float transpose, int quantize) {
    if (quantize == 0)
      return input_midi + transpose;

    bool global_transpose = utils::isTransposeQuantizeGlobal(quantize);
    poly_float pre_add = 0.0f;
    poly_float post_add = input_midi;
    if (global_transpose) {
      pre_add = input_midi;
      post_add = 0.0f;
    }

    poly_float snapped = utils::snapTranspose(pre_add + transpose, quantize);

    if (transpose_quantize_)
      phase_inc_ *= utils::noteOffsetToRatio(snapped - last_quantized_transpose_);

    last_quantized_transpose_ = snapped;
    transpose_quantize_ = quantize;
    return post_add + snapped;
  }
} // namespace sbvital
