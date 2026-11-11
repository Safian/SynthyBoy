// SynthyBoy (AGPL-3.0-or-later): the Serum modules of the effect chain, rendered by core/fx-serum-a and core/fx-serum-b.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "serum_fx_modules.h"
#include "compressor_module.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "synth_constants.h"
#include "synthyboy/fx/hyper_dimension.h"
#include "synthyboy/fx/splitter_lh.h"
#include "synthyboy/fx/splitter_lmh.h"
#include "synthyboy/fx/splitter_ms.h"
#include "synthyboy/fx/utility.h"
#include "bode.h"
#include "convolve.h"

namespace sbvital {
  namespace {
    constexpr int kBlock = kMaxBufferSize * kMaxOversample;

    // the windows of kDivisions 4 .. 12 (4 bars .. 1/64 note) in beats, and the sync factors of kFxSync (free, tempo, dotted, triplet)
    const double kDivisionBeats[9] = { 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625 };
    const double kSyncFactor[4] = { 0.0, 1.0, 1.5, 2.0 / 3.0 };

    /** A control of the module: its natural unit (Hz, %, ms, dB) is the value of the engine's control. */
    float value(const Value* v) { return v->value(); }
    float mod(const Output* o) { return o->buffer[0][0]; }

    void gather(const poly_float* in, float* l, float* r, int n) {
      for (int i = 0; i < n; ++i) {
        l[i] = in[i][0];
        r[i] = in[i][1];
      }
    }

    void scatter(const float* l, const float* r, poly_float* out, int n) {
      for (int i = 0; i < n; ++i)
        out[i] = poly_float(l[i], r[i], l[i], r[i]);
    }
  } // namespace

  // Common base: a stereo float DSP behind the poly_float interface of the chain (lanes 0 / 1 = left / right, copied to 2 / 3).
  class StereoFxModule : public SynthModule {
    public:
      StereoFxModule(const Output* beats_per_second, const int* note_counter) :
          SynthModule(0, 1), beats_per_second_(beats_per_second), note_counter_(note_counter), last_note_counter_(*note_counter) {
        l_.assign(kBlock, 0.0f);
        r_.assign(kBlock, 0.0f);
      }

      void process(int) override { }
      Processor* clone() const override { VITAL_ASSERT(false); return nullptr; }

      void processWithInput(const poly_float* audio_in, int num_samples) override {
        // runs the control processors of the module (the modulation sums of the mono mod controls): without it the modulation of the keys of
        // these modules never reached the DSP (found by the parameter audit, docs/ui-design/PARAM-AUDIT-REPORT.md 3.1); the Vital modules do the same
        SynthModule::process(num_samples);
        if (num_samples > kBlock) {
          utils::copyBuffer(output()->buffer, audio_in, num_samples);
          return;
        }
        gather(audio_in, l_.data(), r_.data(), num_samples);
        processStereo(l_.data(), r_.data(), num_samples);
        scatter(l_.data(), r_.data(), output()->buffer, num_samples);
      }

      void setSampleRate(int sample_rate) override {
        SynthModule::setSampleRate(sample_rate);
        prepareStereo(static_cast<double>(getSampleRate()));
      }

      void setOversampleAmount(int oversample) override {
        SynthModule::setOversampleAmount(oversample);
        prepareStereo(static_cast<double>(getSampleRate()));
      }

      void enable(bool on) override {
        if (on && !enabled())
          resetStereo();
        SynthModule::enable(on);
      }

      void hardReset() override { resetStereo(); }

    protected:
      virtual void prepareStereo(double sample_rate) = 0;
      virtual void resetStereo() = 0;
      virtual void processStereo(float* l, float* r, int num_samples) = 0;

      double bpm() const { return std::max(10.0, static_cast<double>(beats_per_second_->buffer[0][0]) * 60.0); }
      bool noteOnSinceLastBlock() {
        const bool fired = *note_counter_ != last_note_counter_;
        last_note_counter_ = *note_counter_;
        return fired;
      }

      const Output* beats_per_second_;
      const int* note_counter_;
      int last_note_counter_;
      std::vector<float> l_, r_;
  };

  namespace {
    // ---- Hyper / Dimension ---------------------------------------------------------------------------------------------------------
    class HyperDimensionModule : public StereoFxModule {
      public:
        HyperDimensionModule(const Output* bps, const int* counter) : StereoFxModule(bps, counter) {
          rate_ = createMonoModControl("hyperdim_rate");
          unison_ = createBaseControl("hyperdim_unison");
          detune_ = createMonoModControl("hyperdim_detune");
          retrig_ = createBaseControl("hyperdim_retrig");
          mix_ = createMonoModControl("hyperdim_mix");
          dim_size_ = createMonoModControl("hyperdim_dim_size");
          dim_mix_ = createMonoModControl("hyperdim_dim_mix");
          level_ = createMonoModControl("hyperdim_level");
        }

      protected:
        void prepareStereo(double sr) override { fx_.prepare(sr, kBlock); }
        void resetStereo() override { fx_.reset(); }
        void processStereo(float* l, float* r, int n) override {
          synthyboy::fx::HyperDimension::Params p;
          p.enabled = true;
          p.hyperRate = mod(rate_);
          p.hyperUnison = static_cast<int>(value(unison_) + 0.5f);
          p.hyperDetune = mod(detune_) * 0.01f;
          p.hyperRetrig = value(retrig_) > 0.5f;
          p.hyperMix = mod(mix_) * 0.01f;
          p.dimSize = mod(dim_size_) * 0.01f;
          p.dimMix = mod(dim_mix_) * 0.01f;
          p.levelDb = mod(level_);
          if (noteOnSinceLastBlock() && p.hyperRetrig)
            fx_.retrigger();
          fx_.setParams(p);
          fx_.process(l, r, n);
        }

      private:
        synthyboy::fx::HyperDimension fx_;
        Output *rate_, *detune_, *mix_, *dim_size_, *dim_mix_, *level_;
        Value *unison_, *retrig_;
    };

    // ---- Utility ---------------------------------------------------------------------------------------------------------------------
    class UtilityModule : public StereoFxModule {
      public:
        UtilityModule(const Output* bps, const int* counter) : StereoFxModule(bps, counter) {
          polarity_l_ = createBaseControl("utility_polarity_l");
          polarity_r_ = createBaseControl("utility_polarity_r");
          lpf_ = createMonoModControl("utility_lpf");
          hpf_ = createMonoModControl("utility_hpf");
          mono_bass_ = createBaseControl("utility_mono_bass");
          mono_freq_ = createMonoModControl("utility_mono_freq");
          width_ = createMonoModControl("utility_width");
          pan_ = createMonoModControl("utility_pan");
          mix_ = createMonoModControl("utility_mix");
          level_ = createMonoModControl("utility_level");
        }

      protected:
        void prepareStereo(double sr) override { fx_.prepare(sr, kBlock); }
        void resetStereo() override { fx_.reset(); }
        void processStereo(float* l, float* r, int n) override {
          synthyboy::fx::Utility::Params p;
          p.enabled = true;
          p.invertL = value(polarity_l_) > 0.5f;
          p.invertR = value(polarity_r_) > 0.5f;
          p.lpfFreq = mod(lpf_);
          p.lpfEnabled = p.lpfFreq < 19999.0f;   // A21: the extreme position bypasses the filter
          p.hpfFreq = mod(hpf_);
          p.hpfEnabled = p.hpfFreq > 20.5f;
          p.monoBassEnabled = value(mono_bass_) > 0.5f;
          p.monoBassFreq = mod(mono_freq_);
          p.width = mod(width_) * 0.01f;
          p.pan = mod(pan_) * 0.01f;
          p.mix = mod(mix_) * 0.01f;
          p.levelDb = mod(level_);
          fx_.setParams(p);
          fx_.process(l, r, n);
        }

      private:
        synthyboy::fx::Utility fx_;
        Value *polarity_l_, *polarity_r_, *mono_bass_;
        Output *lpf_, *hpf_, *mono_freq_, *width_, *pan_, *mix_, *level_;
    };

    // ---- Bode frequency shifter --------------------------------------------------------------------------------------------------------
    class BodeModule : public StereoFxModule {
      public:
        int latency() const { return fx_.latency(); }

        BodeModule(const Output* bps, const int* counter) : StereoFxModule(bps, counter) {
          mono_input_ = createBaseControl("bode_mono_input");
          shift_ = createMonoModControl("bode_shift");
          range_ = createMonoModControl("bode_range");
          dir_ = createMonoModControl("bode_dir");
          width_ = createMonoModControl("bode_width");
          delay_ = createMonoModControl("bode_delay");
          sync_ = createBaseControl("bode_sync");
          division_ = createBaseControl("bode_division");
          feed_ = createMonoModControl("bode_feed");
          balance_ = createMonoModControl("bode_balance");
          blur_ = createMonoModControl("bode_blur");
          retrig_ = createBaseControl("bode_retrig");
          mix_ = createMonoModControl("bode_mix");
          level_ = createMonoModControl("bode_level");
        }

      protected:
        void prepareStereo(double sr) override { fx_.prepare(sr, kBlock); }
        void resetStereo() override { fx_.reset(); }
        void processStereo(float* l, float* r, int n) override {
          synthyboy::fx::Bode::Params& p = fx_.params;
          p.monoInput = value(mono_input_) > 0.5f;
          p.shift = mod(shift_) * 0.01f;
          p.range = mod(range_);
          p.dir = mod(dir_) * 0.01f;
          p.width = mod(width_) * 0.01f;
          const int sync = std::max(0, std::min(3, static_cast<int>(value(sync_) + 0.5f)));
          p.bpm = sync != 0;
          p.tempoBpm = bpm();
          p.delayBeats = kDivisionBeats[std::max(0, std::min(8, static_cast<int>(value(division_) + 0.5f)))] * kSyncFactor[sync];
          p.delay = mod(delay_) * 0.001f;
          p.feedback = mod(feed_) * 0.0095f;   // 0 .. 100 % -> 0 .. 0.95
          p.balance = mod(balance_) * 0.01f;
          p.blur = mod(blur_) * 0.01f;
          p.mix = mod(mix_) * 0.01f;
          p.level = mod(level_);
          if (noteOnSinceLastBlock() && value(retrig_) > 0.5f)
            fx_.retrig();
          fx_.process(l, r, n);
        }

      private:
        synthyboy::fx::Bode fx_;
        Value *mono_input_, *sync_, *division_, *retrig_;
        Output *shift_, *range_, *dir_, *width_, *delay_, *feed_, *balance_, *blur_, *mix_, *level_;
    };

    // ---- Convolve ------------------------------------------------------------------------------------------------------------------------
    // The impulse is partitioned (allocating) whenever its size, minimum phase, attack, decay, damping or gain changes; that happens on a
    // builder thread that the audio thread only reads from through atomics: it posts the wanted settings, the builder prepares a new
    // synthyboy::fx::Convolve and publishes it, the audio thread takes it over at the start of a block and hands the old one back.
    class ConvolveModule : public StereoFxModule {
      public:
        ConvolveModule(const Output* bps, const int* counter) : StereoFxModule(bps, counter) {
          ir_ = createBaseControl("convolve_ir");
          size_ = createMonoModControl("convolve_size");
          tone_ = createMonoModControl("convolve_tone");
          phi_min_ = createBaseControl("convolve_phi_min");
          predelay_ = createMonoModControl("convolve_predelay");
          sync_ = createBaseControl("convolve_sync");
          division_ = createBaseControl("convolve_division");
          attack_ = createMonoModControl("convolve_attack");
          decay_ = createMonoModControl("convolve_decay");
          damp_ = createMonoModControl("convolve_damp");
          ir_gain_ = createMonoModControl("convolve_ir_gain");
          mix_ = createMonoModControl("convolve_mix");
          level_ = createMonoModControl("convolve_level");
          for (auto& slot : retired_)
            slot.store(nullptr);
          builder_ = std::thread([this] { build(); });
        }

        ~ConvolveModule() override {
          stop_.store(true);
          {
            std::lock_guard<std::mutex> lock(wake_mutex_);
          }
          wake_.notify_all();   // the builder sleeps 10 ms between two looks in live mode: do not wait for the end of that sleep
          builder_.join();
          delete pending_.exchange(nullptr);
          for (auto& slot : retired_)
            delete slot.exchange(nullptr);
          delete stash_;
          delete active_;   // the convolver in use (taken over from the builder): it was the one object nothing freed
        }

        // false (the default): deterministic handover. A block that posts a rebuild (or follows a new impulse / sample rate) waits for the
        // builder and takes the new convolver over in that same block, so the output never depends on thread timing (offline renders,
        // tests, bounces). true: live playback, the audio thread never waits and the take-over happens whenever the build is finished.
        void setRealtime(bool realtime) { realtime_.store(realtime); }

        void setImpulse(std::shared_ptr<const ImpulseData> impulse) {
          std::atomic_store(&impulse_, std::move(impulse));
          impulse_version_.fetch_add(1);
        }

      protected:
        void prepareStereo(double sr) override {
          rate_.store(static_cast<float>(sr));
          rate_version_.fetch_add(1);
        }
        void resetStereo() override {
          if (active_)
            active_->reset();
        }

        void processStereo(float* l, float* r, int n) override {
          // the settings that need a rebuild of the impulse are posted to the builder
          const bool use_ir = value(ir_) > 0.5f;
          const float size = mod(size_), attack = mod(attack_), decay = mod(decay_), damp = mod(damp_), gain = mod(ir_gain_);
          const bool phi = value(phi_min_) > 0.5f;
          if (!posted_ || use_ir != post_use_ || std::fabs(size - post_size_) > 0.05f || std::fabs(attack - post_attack_) > 0.5f ||
              std::fabs(decay - post_decay_) > 0.05f || std::fabs(damp - post_damp_) > 0.05f || std::fabs(gain - post_gain_) > 0.02f ||
              phi != post_phi_) {
            posted_ = true;
            post_use_ = use_ir; post_size_ = size; post_attack_ = attack; post_decay_ = decay; post_damp_ = damp; post_gain_ = gain; post_phi_ = phi;
            want_use_.store(use_ir);
            want_size_.store(size);
            want_attack_.store(attack);
            want_decay_.store(decay);
            want_damp_.store(damp);
            want_gain_.store(gain);
            want_phi_.store(phi);
            want_version_.fetch_add(1);
          }

          // deterministic mode: wait until the builder has caught up with every posted change (the audio thread waits, it does not allocate)
          if (!realtime_.load() && rate_.load() > 0.0f) {
            while (!stop_.load() && (done_want_.load() != want_version_.load() || done_ir_.load() != impulse_version_.load() ||
                                     done_rate_.load() != rate_version_.load()))
              std::this_thread::yield();
          }

          // the audio thread takes over a finished convolver and hands the old one back to the builder to free
          for (int attempt = 0; attempt < 1000; ++attempt) {
            if (stash_ != nullptr) {
              for (auto& slot : retired_) {
                Convolve* expected = nullptr;
                if (slot.compare_exchange_strong(expected, stash_)) {
                  stash_ = nullptr;
                  break;
                }
              }
            }
            if (stash_ == nullptr) {
              Convolve* next = pending_.exchange(nullptr);
              if (next != nullptr) {
                stash_ = active_;
                active_ = next;
              }
              break;
            }
            if (realtime_.load())
              break;
            std::this_thread::yield();   // the builder is freeing the retired convolvers (deterministic mode only)
          }

          if (active_ == nullptr)
            return;   // nothing built yet: the signal passes
          // the settings that do not (tone, pre-delay, mix, level)
          const int sync = std::max(0, std::min(3, static_cast<int>(value(sync_) + 0.5f)));
          synthyboy::fx::Convolve::Params& p = active_->params;
          p.tone = mod(tone_) * 0.01f;
          p.bpm = sync != 0;
          p.tempoBpm = bpm();
          p.preDelayBeats = kDivisionBeats[std::max(0, std::min(8, static_cast<int>(value(division_) + 0.5f)))] * kSyncFactor[sync];
          p.preDelay = mod(predelay_) * 0.001f;
          p.mix = mod(mix_) * 0.01f;
          p.level = mod(level_);
          active_->process(l, r, n);
        }

      private:
        void build() {
          uint32_t built_want = ~0u, built_ir = ~0u, built_rate = ~0u;
          while (!stop_.load()) {
            for (auto& slot : retired_)
              delete slot.exchange(nullptr);
            const uint32_t want = want_version_.load(), ir_version = impulse_version_.load(), rate_version = rate_version_.load();
            if ((want != built_want || ir_version != built_ir || rate_version != built_rate) && rate_.load() > 0.0f && want_version_.load() != 0) {
              // consistent read of the posted settings (the audio thread may post again while we read: then we build again)
              const bool use = want_use_.load(), phi = want_phi_.load();
              const float size = want_size_.load(), attack = want_attack_.load(), decay = want_decay_.load(), damp = want_damp_.load(), gain = want_gain_.load();
              auto impulse = std::atomic_load(&impulse_);
              auto fx = std::make_unique<synthyboy::fx::Convolve>();
              fx->prepare(static_cast<double>(rate_.load()), kBlock);
              fx->params.size = 0.25f * std::pow(16.0f, size * 0.01f);   // 50 % = the original length, 0 .. 100 % = 0.25x .. 4x
              fx->params.minimumPhase = phi;
              fx->params.attack = attack * 0.001f;
              fx->params.decay = (100.0f - decay) * 0.01f;
              fx->params.damp = damp * 0.01f;
              fx->params.irGain = gain;
              if (use && impulse != nullptr && impulse->frames > 0)
                fx->loadIr(impulse->samples.data(), impulse->frames, impulse->channels, impulse->sample_rate);
              built_want = want;
              built_ir = ir_version;
              built_rate = rate_version;
              Convolve* ready = fx.release();
              Convolve* old = pending_.exchange(ready);   // a build that was never taken over is replaced
              delete old;
              done_want_.store(want);   // published after the convolver: the audio thread waiting on these finds the pending pointer set
              done_ir_.store(ir_version);
              done_rate_.store(rate_version);
            }
            else if (rate_.load() > 0.0f && want_version_.load() == 0) {
              // nothing posted yet: nothing to wait for
              done_want_.store(0);
              done_ir_.store(ir_version);
              done_rate_.store(rate_version);
            }
            std::unique_lock<std::mutex> lock(wake_mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(realtime_.load() ? 10 : 1), [this] { return stop_.load(); });
          }
        }

        using Convolve = synthyboy::fx::Convolve;
        Value *ir_, *phi_min_, *sync_, *division_;
        Output *size_, *tone_, *predelay_, *attack_, *decay_, *damp_, *ir_gain_, *mix_, *level_;

        // audio thread
        Convolve* active_ = nullptr;
        Convolve* stash_ = nullptr;
        bool posted_ = false, post_use_ = false, post_phi_ = false;
        float post_size_ = 0, post_attack_ = 0, post_decay_ = 0, post_damp_ = 0, post_gain_ = 0;

        // between the threads
        std::atomic<Convolve*> pending_ { nullptr };
        std::atomic<Convolve*> retired_[4];
        std::atomic<uint32_t> want_version_ { 0 }, impulse_version_ { 0 }, rate_version_ { 0 };
        std::atomic<uint32_t> done_want_ { ~0u }, done_ir_ { ~0u }, done_rate_ { ~0u };   // versions of the last published build
        std::atomic<bool> realtime_ { false };
        std::atomic<bool> want_use_ { false }, want_phi_ { false }, stop_ { false };
        std::atomic<float> want_size_ { 50.0f }, want_attack_ { 0.0f }, want_decay_ { 100.0f }, want_damp_ { 0.0f }, want_gain_ { 0.0f }, rate_ { 0.0f };
        std::shared_ptr<const ImpulseData> impulse_;
        std::mutex wake_mutex_;
        std::condition_variable wake_;
        std::thread builder_;
    };

    // ---- splitters -------------------------------------------------------------------------------------------------------------------
    template<int kBands>
    class SplitterBase : public BandSplitterModule {
      public:
        SplitterBase() : BandSplitterModule(0, 1) {
          for (int b = 0; b < kBands; ++b) {
            band_[b].assign(kBlock, poly_float(0.0f));
            bl_[b].assign(kBlock, 0.0f);
            br_[b].assign(kBlock, 0.0f);
          }
          l_.assign(kBlock, 0.0f);
          r_.assign(kBlock, 0.0f);
        }
        void process(int) override { }
        Processor* clone() const override { VITAL_ASSERT(false); return nullptr; }
        int numBands() const override { return kBands; }
        const poly_float* bandBuffer(int band) const override { return band_[band].data(); }
        void setSampleRate(int sample_rate) override { SynthModule::setSampleRate(sample_rate); prepareSplitter(static_cast<double>(getSampleRate())); }
        void setOversampleAmount(int oversample) override { SynthModule::setOversampleAmount(oversample); prepareSplitter(static_cast<double>(getSampleRate())); }
        void enable(bool on) override { if (on && !enabled()) resetSplitter(); SynthModule::enable(on); }
        void hardReset() override { resetSplitter(); }

        void processWithInput(const poly_float* audio_in, int num_samples) override {
          utils::copyBuffer(output()->buffer, audio_in, num_samples);   // used when something calls the module like an effect: pass
        }

      protected:
        virtual void prepareSplitter(double sample_rate) = 0;
        virtual void resetSplitter() = 0;
        void scatterBands(int n) {
          for (int b = 0; b < kBands; ++b)
            scatter(bl_[b].data(), br_[b].data(), band_[b].data(), n);
        }
        void gatherProcessed(const poly_float* const* processed, int n) {
          for (int b = 0; b < kBands; ++b)
            gather(processed[b], bl_[b].data(), br_[b].data(), n);
        }

        std::vector<poly_float> band_[kBands];
        std::vector<float> bl_[kBands], br_[kBands], l_, r_;
    };

    class SplitterLhModule : public SplitterBase<2> {
      public:
        SplitterLhModule() {
          freq_ = createMonoModControl("splitter_lh_freq");
          low_bypass_ = createBaseControl("splitter_lh_low_bypass");
          high_bypass_ = createBaseControl("splitter_lh_high_bypass");
          level_ = createMonoModControl("splitter_lh_level");
        }
        int bandRack(int band) const override { return ReorderableEffectChain::kLhLow + band; }
        void split(const poly_float* in, int n) override {
          SynthModule::process(n);   // the control processors (modulation sums), as for the effect modules above
          update();
          gather(in, l_.data(), r_.data(), n);
          fx_.split(l_.data(), r_.data(), bl_[0].data(), br_[0].data(), bl_[1].data(), br_[1].data(), n);
          scatterBands(n);
        }
        void merge(const poly_float* const* processed, int n) override {
          gatherProcessed(processed, n);
          fx_.merge(bl_[0].data(), br_[0].data(), bl_[1].data(), br_[1].data(), l_.data(), r_.data(), n);
          scatter(l_.data(), r_.data(), output()->buffer, n);
        }
      protected:
        void prepareSplitter(double sr) override { fx_.prepare(sr, kBlock); }
        void resetSplitter() override { fx_.reset(); }
      private:
        void update() {
          synthyboy::fx::SplitterLH::Params p;
          p.splitFreq = mod(freq_);
          p.lowBypass = value(low_bypass_) > 0.5f;
          p.highBypass = value(high_bypass_) > 0.5f;
          p.levelDb = mod(level_);
          fx_.setParams(p);
        }
        synthyboy::fx::SplitterLH fx_;
        Output *freq_, *level_;
        Value *low_bypass_, *high_bypass_;
    };

    class SplitterLmhModule : public SplitterBase<3> {
      public:
        SplitterLmhModule() {
          freq_low_ = createMonoModControl("splitter_lmh_freq_low");
          freq_high_ = createMonoModControl("splitter_lmh_freq_high");
          low_bypass_ = createBaseControl("splitter_lmh_low_bypass");
          mid_bypass_ = createBaseControl("splitter_lmh_mid_bypass");
          high_bypass_ = createBaseControl("splitter_lmh_high_bypass");
          level_ = createMonoModControl("splitter_lmh_level");
        }
        int bandRack(int band) const override { return ReorderableEffectChain::kLmhLow + band; }
        void split(const poly_float* in, int n) override {
          SynthModule::process(n);   // the control processors (modulation sums), as for the effect modules above
          update();
          gather(in, l_.data(), r_.data(), n);
          fx_.split(l_.data(), r_.data(), bl_[0].data(), br_[0].data(), bl_[1].data(), br_[1].data(), bl_[2].data(), br_[2].data(), n);
          scatterBands(n);
        }
        void merge(const poly_float* const* processed, int n) override {
          gatherProcessed(processed, n);
          fx_.merge(bl_[0].data(), br_[0].data(), bl_[1].data(), br_[1].data(), bl_[2].data(), br_[2].data(), l_.data(), r_.data(), n);
          scatter(l_.data(), r_.data(), output()->buffer, n);
        }
      protected:
        void prepareSplitter(double sr) override { fx_.prepare(sr, kBlock); }
        void resetSplitter() override { fx_.reset(); }
      private:
        void update() {
          synthyboy::fx::SplitterLMH::Params p;
          const float a = mod(freq_low_), b = mod(freq_high_);
          p.splitFreq1 = std::min(a, b);
          p.splitFreq2 = std::max(a, b);
          p.lowBypass = value(low_bypass_) > 0.5f;
          p.midBypass = value(mid_bypass_) > 0.5f;
          p.highBypass = value(high_bypass_) > 0.5f;
          p.levelDb = mod(level_);
          fx_.setParams(p);
        }
        synthyboy::fx::SplitterLMH fx_;
        Output *freq_low_, *freq_high_, *level_;
        Value *low_bypass_, *mid_bypass_, *high_bypass_;
    };

    class SplitterMsModule : public SplitterBase<2> {
      public:
        SplitterMsModule() {
          mid_bypass_ = createBaseControl("splitter_ms_mid_bypass");
          side_bypass_ = createBaseControl("splitter_ms_side_bypass");
          level_ = createMonoModControl("splitter_ms_level");
        }
        int bandRack(int band) const override { return ReorderableEffectChain::kMsMid + band; }
        void split(const poly_float* in, int n) override {
          SynthModule::process(n);   // the control processors (modulation sums), as for the effect modules above
          update();
          gather(in, l_.data(), r_.data(), n);
          fx_.split(l_.data(), r_.data(), bl_[0].data(), br_[0].data(), bl_[1].data(), br_[1].data(), n);
          scatterBands(n);
        }
        void merge(const poly_float* const* processed, int n) override {
          gatherProcessed(processed, n);
          fx_.merge(bl_[0].data(), br_[0].data(), bl_[1].data(), br_[1].data(), l_.data(), r_.data(), n);
          scatter(l_.data(), r_.data(), output()->buffer, n);
        }
      protected:
        void prepareSplitter(double sr) override { fx_.prepare(sr, kBlock); }
        void resetSplitter() override { fx_.reset(); }
      private:
        void update() {
          synthyboy::fx::SplitterMS::Params p;
          p.midBypass = value(mid_bypass_) > 0.5f;
          p.sideBypass = value(side_bypass_) > 0.5f;
          p.levelDb = mod(level_);
          fx_.setParams(p);
        }
        synthyboy::fx::SplitterMS fx_;
        Output* level_;
        Value *mid_bypass_, *side_bypass_;
    };
  } // namespace

  SynthModule* createSerumModule(int index, const Output* beats_per_second, const int* note_counter) {
    switch (index) {
      case ReorderableEffectChain::kHyperDimension: return new HyperDimensionModule(beats_per_second, note_counter);
      case ReorderableEffectChain::kUtility: return new UtilityModule(beats_per_second, note_counter);
      case ReorderableEffectChain::kBode: return new BodeModule(beats_per_second, note_counter);
      case ReorderableEffectChain::kConvolve: return new ConvolveModule(beats_per_second, note_counter);
      case ReorderableEffectChain::kSplitterLh: return new SplitterLhModule();
      case ReorderableEffectChain::kSplitterLmh: return new SplitterLmhModule();
      case ReorderableEffectChain::kSplitterMs: return new SplitterMsModule();
      default: return nullptr;
    }
  }

  int serumModuleLatency(const SynthModule* module) {
    if (const auto* bode = dynamic_cast<const BodeModule*>(module))
      return bode->latency();
    if (const auto* compressor = dynamic_cast<const CompressorModule*>(module))
      return compressor->latency();
    return 0;   // Hyper / Dimension, Utility, Convolve (zero latency partitioned convolution) and the crossovers (LR4, all-pass compensated)
  }

  void setConvolveRealtime(SynthModule* convolve, bool realtime) {
    if (auto* module = dynamic_cast<ConvolveModule*>(convolve))
      module->setRealtime(realtime);
  }

  void setConvolveImpulse(SynthModule* convolve, std::shared_ptr<const ImpulseData> impulse) {
    if (auto* module = dynamic_cast<ConvolveModule*>(convolve))
      module->setImpulse(std::move(impulse));
  }
} // namespace sbvital
