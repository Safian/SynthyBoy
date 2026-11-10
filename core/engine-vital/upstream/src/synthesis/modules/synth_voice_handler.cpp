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

#include "synth_voice_handler.h"

#include "envelope.h"
#include "envelope_module.h"
#include "filters_module.h"
#include "line_map.h"
#include "operators.h"
#include "portamento_slope.h"
#include "random_lfo_module.h"
#include "source_tap.h"
#include "synth_constants.h"
#include "lfo_module.h"
#include "trigger_random.h"
#include "value_switch.h"
#include "modulation_connection_processor.h"

namespace sbvital {
  namespace {
    /** The voice killer: the amplitude envelope path, and while a channel with its ENV 1 button off sounds also the enabled envelopes 2 to 4
        (Serum 2 manual p.146: such a voice ends after the longest release of any of the envelopes). With every ENV 1 button on the output is the
        amplitude, bit for bit, as before. */
    class VoiceKeepAlive : public Processor {
      public:
        static constexpr int kNumEnvelopeInputs = 3;
        enum { kAmplitude, kFreeAudio, kEnvelope1, kNumInputs = kEnvelope1 + kNumEnvelopeInputs };

        VoiceKeepAlive() : Processor(kNumInputs, 1) { }
        Processor* clone() const override { return new VoiceKeepAlive(*this); }

        void process(int num_samples) override {
          const poly_float* amplitude = input(kAmplitude)->source->buffer;
          const poly_float* free_audio = input(kFreeAudio)->source->buffer;
          poly_float* out = output()->buffer;
          poly_float free_peak = 0.0f;
          for (int i = 0; i < num_samples; ++i)
            free_peak = utils::max(free_peak, poly_float::abs(free_audio[i]));
          poly_float envelope = 0.0f;
          for (int e = 0; e < kNumEnvelopeInputs; ++e) {
            const Output* source = input(kEnvelope1 + e)->source;
            if (source != nullptr && source->owner != nullptr && source->owner->enabled())
              envelope = utils::max(envelope, poly_float::abs(source->buffer[0]));
          }
          const poly_float hold = utils::maskLoad(poly_float(0.0f), envelope, poly_float::greaterThan(free_peak, 1.0e-6f));
          for (int i = 0; i < num_samples; ++i)
            out[i] = utils::max(amplitude[i], hold);
        }
    };
  }

  SynthVoiceHandler::SynthVoiceHandler(Output* beats_per_second) :
      VoiceHandler(0, kMaxPolyphony), producers_(nullptr), beats_per_second_(beats_per_second),
      note_from_reference_(nullptr), midi_offset_output_(nullptr),
      bent_midi_(nullptr), current_midi_note_(nullptr), amplitude_envelope_(nullptr), amplitude_(nullptr),
      pitch_wheel_(nullptr), filters_module_(nullptr), lfos_(), envelopes_(), lfo_sources_(), random_(nullptr),
      random_lfos_(), note_mapping_(nullptr), velocity_mapping_(nullptr), aftertouch_mapping_(nullptr),
      slide_mapping_(nullptr), lift_mapping_(nullptr), mod_wheel_mapping_(nullptr),
      pitch_wheel_mapping_(nullptr), stereo_(nullptr), lowest_active_note_(nullptr), note_percentage_(nullptr), last_active_voice_mask_(0) {
    output_ = new Multiply();
    main_final_ = new Add();
    registerOutput(main_final_->output());

    direct_output_ = new Multiply();
    direct_final_ = new Add();
    registerOutput(direct_final_->output());

    bus1_output_ = new Multiply();
    registerOutput(bus1_output_->output());
    bus2_output_ = new Multiply();
    registerOutput(bus2_output_->output());

    note_from_reference_ = new cr::Add();
    midi_offset_output_ = registerControlRateOutput(note_from_reference_->output(), true);

    enabled_modulation_processors_.ensureCapacity(kMaxModulationConnections);
  }

  void SynthVoiceHandler::init() {
    createNoteArticulation();
    createProducers();
    createModulators();
    createFilters(note_from_reference_->output());
    createSourceTaps();
    createVoiceOutput();

    Add* voice_sum = new Add();
    voice_sum->plug(filters_module_, 0);
    voice_sum->plug(producers_->output(ProducersModule::kRawOut), 1);
    output_->plug(voice_sum, 0);
    output_->plug(amplitude_, 1);
    // SynthyBoy B21 (13-new-keys.md 4.22): a Main / Direct channel with its ENV 1 button off is multiplied by the velocity and voice amplitude
    // only (amplitude twice, as the squared amplitude of the enveloped path) and added behind the envelope
    Add* main_free = new Add();
    main_free->plug(producers_->output(ProducersModule::kRawFree), 0);
    main_free->plug(filters_module_->output(FiltersModule::kMainFree), 1);
    Add* direct_free = new Add();
    direct_free->plug(producers_->output(ProducersModule::kDirectFree), 0);
    direct_free->plug(filters_module_->output(FiltersModule::kDirectFree), 1);
    Add* free_sum = new Add();
    free_sum->plug(main_free, 0);
    free_sum->plug(direct_free, 1);
    Processor* free_gain_main[2];
    Processor* free_gain_direct[2];
    for (int stage = 0; stage < 2; ++stage) {
      free_gain_main[stage] = new SmoothMultiply();
      free_gain_main[stage]->plug(stage == 0 ? static_cast<Processor*>(main_free) : free_gain_main[0], SmoothMultiply::kAudioRate);
      free_gain_main[stage]->plug(control_amplitude_, SmoothMultiply::kControlRate);
      free_gain_main[stage]->plug(reset(), SmoothMultiply::kReset);
      free_gain_direct[stage] = new SmoothMultiply();
      free_gain_direct[stage]->plug(stage == 0 ? static_cast<Processor*>(direct_free) : free_gain_direct[0], SmoothMultiply::kAudioRate);
      free_gain_direct[stage]->plug(control_amplitude_, SmoothMultiply::kControlRate);
      free_gain_direct[stage]->plug(reset(), SmoothMultiply::kReset);
    }
    main_final_->plug(output_, 0);
    main_final_->plug(free_gain_main[1], 1);
    direct_final_->plug(direct_output_, 0);
    direct_final_->plug(free_gain_direct[1], 1);
    // the voice lives as long as the longest enabled envelope while a free channel sounds (manual p.146)
    keep_alive_ = new VoiceKeepAlive();
    keep_alive_->plug(amplitude_, VoiceKeepAlive::kAmplitude);
    keep_alive_->plug(free_sum, VoiceKeepAlive::kFreeAudio);
    for (int i = 1; i < kNumEnvelopes && i <= VoiceKeepAlive::kNumEnvelopeInputs; ++i)
      keep_alive_->plug(envelopes_[i]->output(EnvelopeModule::kValue), VoiceKeepAlive::kEnvelope1 + i - 1);
    addProcessor(main_free);
    addProcessor(direct_free);
    addProcessor(free_sum);
    for (int stage = 0; stage < 2; ++stage) {
      addProcessor(free_gain_main[stage]);
      addProcessor(free_gain_direct[stage]);
    }
    addProcessor(keep_alive_);
    addProcessor(main_final_);
    addProcessor(direct_final_);
    // SynthyBoy: the direct output also takes the filters whose destination is Direct out; the buses sum the sends of the sources and filters
    Add* direct_sum = new Add();
    direct_sum->plug(producers_->output(ProducersModule::kDirectOut), 0);
    direct_sum->plug(filters_module_->output(FiltersModule::kDirect), 1);
    direct_output_->plug(direct_sum, 0);
    direct_output_->plug(amplitude_, 1);
    Add* bus1_sum = new Add();
    bus1_sum->plug(producers_->output(ProducersModule::kBus1Out), 0);
    bus1_sum->plug(filters_module_->output(FiltersModule::kBus1), 1);
    bus1_output_->plug(bus1_sum, 0);
    bus1_output_->plug(amplitude_, 1);
    Add* bus2_sum = new Add();
    bus2_sum->plug(producers_->output(ProducersModule::kBus2Out), 0);
    bus2_sum->plug(filters_module_->output(FiltersModule::kBus2), 1);
    bus2_output_->plug(bus2_sum, 0);
    bus2_output_->plug(amplitude_, 1);

    addProcessor(voice_sum);
    addProcessor(output_);
    addProcessor(direct_sum);
    addProcessor(direct_output_);
    addProcessor(bus1_sum);
    addProcessor(bus1_output_);
    addProcessor(bus2_sum);
    addProcessor(bus2_output_);

    Output* macros[kNumMacros];
    for (int i = 0; i < kNumMacros; ++i)
      macros[i] = createMonoModControl("macro_control_" + std::to_string(i + 1));

    setVoiceKiller(keep_alive_->output());

    for (int i = 0; i < sbvital::kMaxModulationConnections; ++i) {
      ModulationConnectionProcessor* processor = modulation_bank_.atIndex(i)->modulation_processor.get();

      processor->plug(reset(), ModulationConnectionProcessor::kReset);

      std::string number = std::to_string(i + 1);
      std::string amount_name = "modulation_" + number + "_amount";
      Output* modulation_amount = createPolyModControl(amount_name);
      processor->plug(modulation_amount, ModulationConnectionProcessor::kModulationAmount);
      processor->initializeBaseValue(data_->controls[amount_name]);

      Output* modulation_power = createPolyModControl("modulation_" + number + "_power");
      processor->plug(modulation_power, ModulationConnectionProcessor::kModulationPower);

      // REMAINDER item 1 (13-new-keys.md 4.41): RISE / FALL / DELAY of the slot, controls in the unit of the key (ms, beats) that other slots may modulate
      processor->plug(createPolyModControl("modulation_" + number + "_rise"), ModulationConnectionProcessor::kRise);
      processor->plug(createPolyModControl("modulation_" + number + "_fall"), ModulationConnectionProcessor::kFall);
      processor->plug(createPolyModControl("modulation_" + number + "_delay"), ModulationConnectionProcessor::kDelay);
      processor->plug(createPolyModControl("modulation_" + number + "_delay_beats"), ModulationConnectionProcessor::kDelayBeats);
      processor->plug(beats_per_second_, ModulationConnectionProcessor::kBeatsPerSecond);

      addProcessor(processor);
      addSubmodule(processor);
      processor->enable(false);
    }

    VoiceHandler::init();
    producers_->setFilter1On(filters_module_->getFilter1OnValue());
    producers_->setFilter2On(filters_module_->getFilter2OnValue());
    setupPolyModulationReadouts();

    for (int i = 0; i < kNumMacros; ++i) {
      std::string name = "macro_control_" + std::to_string(i + 1);
      data_->mod_sources[name] = macros[i];
      createStatusOutput(name, macros[i]);
    }

    for (int i = 0; i < kNumRandomLfos; ++i) {
      std::string name = "random_" + std::to_string(i + 1);
      data_->mod_sources[name] = random_lfos_[i]->output();
      createStatusOutput(name, random_lfos_[i]->output());
    }

    data_->mod_sources["random"] = random_->output();
    data_->mod_sources["stereo"] = stereo_->output();

    createStatusOutput("random", random_->output());
    createStatusOutput("stereo", stereo_->output());
    createStatusOutput("sample_phase", producers_->samplePhaseOutput());
    createStatusOutput("num_voices", &num_voices_);

    std::string modulation_source_prefix = "modulation_source_";
    std::string modulation_amount_prefix = "modulation_amount_";
    for (int i = 0; i < sbvital::kMaxModulationConnections; ++i) {
      ModulationConnectionProcessor* processor = modulation_bank_.atIndex(i)->modulation_processor.get();
      std::string number = std::to_string(i + 1);
      Output* source_output = processor->output(ModulationConnectionProcessor::kModulationSource);
      Output* pre_scale_output = processor->output(ModulationConnectionProcessor::kModulationPreScale);
      createStatusOutput(modulation_source_prefix + number, source_output);
      createStatusOutput(modulation_amount_prefix + number, pre_scale_output);
    }
  }

  void SynthVoiceHandler::prepareDestroy() {
    for (int i = 0; i < sbvital::kMaxModulationConnections; ++i) {
      ModulationConnectionProcessor* processor = modulation_bank_.atIndex(i)->modulation_processor.get();
      removeProcessor(processor);
    }
  }

  void SynthVoiceHandler::createProducers() {
    lowest_active_note_ = new cr::Value(0.0f);
    producers_ = new ProducersModule();
    producers_->plug(reset(), ProducersModule::kReset);
    producers_->plug(retrigger(), ProducersModule::kRetrigger);
    producers_->plug(bent_midi_, ProducersModule::kMidi);
    producers_->plug(note_count(), ProducersModule::kNoteCount);
    producers_->plug(active_mask(), ProducersModule::kActiveVoices);
    producers_->plug(lowest_active_note_, ProducersModule::kLowestNote);
    producers_->plug(note_pressed(), ProducersModule::kNotePressed);
    producers_->plug(velocity(), ProducersModule::kVelocity); // GROUPC WP-13
    // SynthyBoy Block B14:
    producers_->plug(current_midi_note_, ProducersModule::kRawMidi);
    // SynthyBoy (GROUPA2, WP-07): the voice control (pan of every source, detune of the noise and the sub; the oscillators take the detune with the fold)
    for (int i = 0; i < kVoiceControlTargets; ++i)
      producers_->plug(voice_pan(i), ProducersModule::kVoicePan1 + i);
    producers_->plug(voice_detune(3), ProducersModule::kVoiceDetuneNoise);
    producers_->plug(voice_detune(4), ProducersModule::kVoiceDetuneSub);
    Value* voice_phase_legato = createBaseControl("voice_phase_legato");
    producers_->plug(voice_phase_legato, ProducersModule::kVoicePhaseLegato);
    for (int i = 0; i < 3; ++i) {
      producers_->plug(osc_mute(i), ProducersModule::kMute1 + i);
      producers_->plug(osc_fold(i), ProducersModule::kFold1 + i);
    }
    addSubmodule(producers_);
    addProcessor(producers_);
  }

  void SynthVoiceHandler::setNoteTable(const mono_float* table) {
    VoiceHandler::setNoteTable(table);
    if (producers_)
      producers_->setNoteTable(table);
  }

  void SynthVoiceHandler::createModulators() {
    lfo_rate_scale_ = new cr::Value(1.0f);   // SynthyBoy (GROUPA2, WP-07): the LFO scaling of the voice control, set by the adapter
    addIdleMonoProcessor(lfo_rate_scale_);
    for (int i = 0; i < kNumLfos; ++i) {
      lfo_sources_[i].setLoop(false);
      lfo_sources_[i].initTriangle();
      std::string prefix = std::string("lfo_") + std::to_string(i + 1);
      LfoModule* lfo = new LfoModule(prefix, &lfo_sources_[i], beats_per_second_);
      addSubmodule(lfo);
      addProcessor(lfo);
      lfos_[i] = lfo;
      lfo->plug(retrigger(), LfoModule::kNoteTrigger);
      lfo->plug(note_count(), LfoModule::kNoteCount);
      lfo->plug(bent_midi_, LfoModule::kMidi);
      lfo->plug(lfo_rate_scale_, LfoModule::kRateScale);

      data_->mod_sources[prefix] = lfo->output(LfoModule::kValue);
      createStatusOutput(prefix, lfo->output(LfoModule::kValue));
      createStatusOutput(prefix + "_phase", lfo->output(LfoModule::kOscPhase));
      createStatusOutput(prefix + "_frequency", lfo->output(LfoModule::kOscFrequency));
    }

    Value* poly_legato = createBaseControl("poly_legato");
    for (int i = 0; i < kNumEnvelopes; ++i) {
      std::string prefix = std::string("env_") + std::to_string(i + 1);
      EnvelopeModule* envelope = new EnvelopeModule(prefix, i == 0);
      envelope->setLegatoBus(&legato_buses_[i]);
      envelope->plug(poly_legato, EnvelopeModule::kPolyLegato);
      envelope->plug(retrigger(), EnvelopeModule::kTrigger);
      envelope->plug(retrigger_always(), EnvelopeModule::kTriggerAlways);
      envelope->plug(voice_env_time(), EnvelopeModule::kTimeScale);
      addSubmodule(envelope);
      addProcessor(envelope);
      envelopes_[i] = envelope;

      data_->mod_sources[prefix] = envelope->output();
      createStatusOutput(prefix, envelope->output(EnvelopeModule::kValue));
      createStatusOutput(prefix + "_phase", envelope->output(EnvelopeModule::kPhase));
    }

    random_ = new TriggerRandom();
    random_->plug(retrigger());
    addProcessor(random_);

    for (int i = 0; i < kNumRandomLfos; ++i) {
      std::string name = "random_" + std::to_string(i + 1);
      random_lfos_[i] = new RandomLfoModule(name, beats_per_second_);
      random_lfos_[i]->plug(retrigger(), RandomLfoModule::kNoteTrigger);
      random_lfos_[i]->plug(bent_midi_, RandomLfoModule::kMidi);
      random_lfos_[i]->plug(lfo_rate_scale_, RandomLfoModule::kRateScale);
      addSubmodule(random_lfos_[i]);
      addProcessor(random_lfos_[i]);
    }

    stereo_ = new cr::Value(constants::kLeftOne);
    addIdleMonoProcessor(stereo_);

    // SynthyBoy (GROUPA2, WP-07): the per voice values of the voice control as modulation sources
    data_->mod_sources["voice_index"] = voice_index();
    data_->mod_sources["voice_mod1"] = voice_mod(0);
    data_->mod_sources["voice_mod2"] = voice_mod(1);
    createStatusOutput("voice_index", voice_index());
    createStatusOutput("voice_mod1", voice_mod(0));
    createStatusOutput("voice_mod2", voice_mod(1));

    data_->mod_sources["note"] = note_percentage_->output();
    data_->mod_sources["note_in_octave"] = note_in_octave();
    data_->mod_sources["aftertouch"] = aftertouch();
    data_->mod_sources["velocity"] = velocity();
    data_->mod_sources["slide"] = slide();
    data_->mod_sources["poly_aftertouch"] = poly_aftertouch();
    data_->mod_sources["lift"] = lift();
    data_->mod_sources["mod_wheel"] = mod_wheel();
    data_->mod_sources["pitch_wheel"] = pitch_wheel_percent();

    createStatusOutput("note", note_percentage_->output());
    createStatusOutput("note_in_octave", note_in_octave());
    createStatusOutput("aftertouch" ,aftertouch());
    createStatusOutput("velocity", velocity());
    createStatusOutput("slide", slide());
    createStatusOutput("poly_aftertouch", poly_aftertouch());
    createStatusOutput("lift", lift());
    createStatusOutput("mod_wheel", mod_wheel());
    createStatusOutput("pitch_wheel", pitch_wheel_percent());
  }

  // GROUPB WP-06 (13-new-keys.md 4.34): the oscillators, the noise, the sub and the filters as matrix sources, and the sources that are not a signal
  void SynthVoiceHandler::createSourceTaps() {
    struct Tap { const char* name; Output* input; };
    const Tap taps[] = {
      { "osc_1_out", producers_->output(ProducersModule::kTapOsc1) },   { "osc_2_out", producers_->output(ProducersModule::kTapOsc2) },
      { "osc_3_out", producers_->output(ProducersModule::kTapOsc3) },   { "noise_out", producers_->output(ProducersModule::kTapNoise) },
      { "sub_out", producers_->output(ProducersModule::kTapSub) },      { "filter_1_out", filters_module_->output(FiltersModule::kTapFilter1) },
      { "filter_2_out", filters_module_->output(FiltersModule::kTapFilter2) },
    };
    for (const Tap& tap : taps) {
      SourceTap* processor = new SourceTap();
      processor->plug(tap.input);
      addProcessor(processor);
      data_->mod_sources[tap.name] = processor->output();
      createStatusOutput(tap.name, processor->output());
    }
    // the warps from a filter read the filter one block late (the filters run behind the oscillators): a Feedback node per filter
    getPolyRouter()->connectAudioFeedback(producers_, filters_module_->output(FiltersModule::kFeedFilter1), ProducersModule::kFilter1Tap);
    getPolyRouter()->connectAudioFeedback(producers_, filters_module_->output(FiltersModule::kFeedFilter2), ProducersModule::kFilter2Tap);

    active_voices_ = new cr::Value(0.0f);
    addIdleMonoProcessor(active_voices_);
    fixed_ = new cr::Value(1.0f);
    addIdleMonoProcessor(fixed_);
    data_->mod_sources["active_voices"] = active_voices_->output();
    data_->mod_sources["fixed"] = fixed_->output();
    createStatusOutput("active_voices", active_voices_->output());
    createStatusOutput("fixed", fixed_->output());
    data_->mod_sources["note_on_alt1"] = note_on_alt(0);
    data_->mod_sources["note_on_alt2"] = note_on_alt(1);
    data_->mod_sources["note_on_rand2"] = note_on_rand2();
    data_->mod_sources["release_velocity"] = lift();   // the note-off velocity: the Vital source Lift is Serum's Release Velocity
    data_->mod_sources["expression_x"] = expression_x();
    createStatusOutput("note_on_alt1", note_on_alt(0));
    createStatusOutput("note_on_alt2", note_on_alt(1));
    createStatusOutput("note_on_rand2", note_on_rand2());
    createStatusOutput("expression_x", expression_x());
    for (int k = 0; k < kNumDiscreteRandoms; ++k) {
      const std::string name = "note_on_discrete_" + std::to_string(k + 1);
      data_->mod_sources[name] = note_on_discrete(k);
    }
    createStatusOutput("note_on_discrete_1", note_on_discrete(0));
  }

  FilterCurveShared* SynthVoiceHandler::getFilterCurve(int filter) {
    return filters_module_->curveShared(filter);
  }

  void SynthVoiceHandler::createFilters(Output* keytrack) {
    filters_module_ = new FiltersModule();
    addSubmodule(filters_module_);
    addProcessor(filters_module_);

    filters_module_->plug(producers_->output(ProducersModule::kToFilter1), FiltersModule::kFilter1Input);
    filters_module_->plug(producers_->output(ProducersModule::kToFilter2), FiltersModule::kFilter2Input);
    filters_module_->plug(reset(), FiltersModule::kReset);
    filters_module_->plug(keytrack, FiltersModule::kKeytrack);
    filters_module_->plug(bent_midi_, FiltersModule::kMidi);
    filters_module_->plug(voice_cutoff(), FiltersModule::kVoiceCutoff);   // SynthyBoy (GROUPA2, WP-07)
  }

  void SynthVoiceHandler::createNoteArticulation() {
    Output* portamento = createPolyModControl("portamento_time");
    Output* portamento_slope = createPolyModControl("portamento_slope");
    Value* portamento_force = createBaseControl("portamento_force");
    Value* portamento_scale = createBaseControl("portamento_scale");

    current_midi_note_ = new PortamentoSlope();
    current_midi_note_->plug(last_note(), PortamentoSlope::kSource);
    current_midi_note_->plug(note(), PortamentoSlope::kTarget);
    current_midi_note_->plug(portamento_force, PortamentoSlope::kPortamentoForce);
    current_midi_note_->plug(portamento_scale, PortamentoSlope::kPortamentoScale);
    current_midi_note_->plug(portamento, PortamentoSlope::kRunSeconds);
    current_midi_note_->plug(portamento_slope, PortamentoSlope::kSlopePower);
    current_midi_note_->plug(voice_event(), PortamentoSlope::kReset);
    current_midi_note_->plug(note_pressed(), PortamentoSlope::kNumNotesPressed);
    setVoiceMidi(current_midi_note_->output());
    addProcessor(current_midi_note_);

    Output* pitch_bend_range = createPolyModControl("pitch_bend_range");
    Output* voice_tune = createPolyModControl("voice_tune");
    Output* voice_transpose = createPolyModControl("voice_transpose");
    cr::Multiply* pitch_bend = new cr::Multiply();
    pitch_bend->plug(pitch_wheel(), 0);
    pitch_bend->plug(pitch_bend_range, 1);
    bent_midi_ = new cr::VariableAdd();
    bent_midi_->plugNext(current_midi_note_);
    bent_midi_->plugNext(pitch_bend);
    bent_midi_->plugNext(local_pitch_bend());
    bent_midi_->plugNext(voice_tune);
    bent_midi_->plugNext(voice_transpose);

    static const cr::Value max_midi_invert(1.0f / (kMidiSize - 1));
    note_percentage_ = new cr::Multiply();
    note_percentage_->plug(&max_midi_invert, 0);
    note_percentage_->plug(bent_midi_, 1);
    addProcessor(note_percentage_);

    static const cr::Value reference_adjust(-kMidiTrackCenter);
    note_from_reference_->plug(&reference_adjust, 0);
    note_from_reference_->plug(bent_midi_, 1);
    addProcessor(note_from_reference_);

    addProcessor(pitch_bend);
    addProcessor(bent_midi_);
  }

  void SynthVoiceHandler::createVoiceOutput() {
    Output* velocity_track_amount = createPolyModControl("velocity_track");
    cr::Interpolate* velocity_track_mult = new cr::Interpolate();
    velocity_track_mult->plug(&constants::kValueOne, Interpolate::kFrom);
    velocity_track_mult->plug(velocity(), Interpolate::kTo);
    velocity_track_mult->plug(velocity_track_amount, Interpolate::kFractional);
    addProcessor(velocity_track_mult);

    Output* voice_amplitude = createPolyModControl("voice_amplitude");
    cr::Multiply* amplitude = new cr::Multiply();
    amplitude->plug(velocity_track_mult, 0);
    amplitude->plug(voice_amplitude, 1);
    addProcessor(amplitude);

    amplitude_envelope_ = envelopes_[0];
    amplitude_envelope_->setControlRate(false);

    Processor* control_amplitude = new SmoothMultiply();
    control_amplitude->plug(amplitude_envelope_->output(Envelope::kValue), SmoothMultiply::kAudioRate);
    control_amplitude->plug(amplitude, SmoothMultiply::kControlRate);
    control_amplitude->plug(reset(), SmoothMultiply::kReset);

    amplitude_ = new Square();
    amplitude_->plug(control_amplitude);

    addProcessor(control_amplitude);
    addProcessor(amplitude_);
    control_amplitude_ = amplitude;
  }

  void SynthVoiceHandler::process(int num_samples) {
    for (PolyLegatoBus& bus : legato_buses_)
      bus.beginBlock();

    poly_mask reset_mask = reset()->trigger_mask;
    if (reset_mask.anyMask())
      resetFeedbacks(reset_mask);

    if (lowest_active_note_)
      lowest_active_note_->set(getLowestActiveNote());

    VoiceHandler::process(num_samples);
    int num_voices = getNumActiveVoices();
    num_voices_.buffer[0] = num_voices;
    if (active_voices_)   // GROUPB WP-06: the active voices over the polyphony limit
      active_voices_->set(std::min(1.0f, static_cast<mono_float>(num_voices) / static_cast<mono_float>(std::max(1, polyphony()))));
    note_retriggered_.clearTrigger();

    if (num_voices == 0) {
      for (auto& status_source : data_->status_outputs)
        status_source.second->clear();
    }
    else {
      last_active_voice_mask_ = getCurrentVoiceMask();
      for (auto& status_source : data_->status_outputs)
        status_source.second->update(last_active_voice_mask_);

      for (ModulationConnectionProcessor* processor : enabled_modulation_processors_) {
        poly_float* buffer = processor->output()->buffer;
        if (processor->isControlRate() || processor->isPolyphonicModulation()) {
          poly_float masked_value = buffer[0] & last_active_voice_mask_;
          buffer[0] = masked_value + utils::swapVoices(masked_value);
        }
        else {
          for (int i = 0; i < num_samples; ++i) {
            poly_float masked_value = buffer[i] & last_active_voice_mask_;
            buffer[i] = masked_value + utils::swapVoices(masked_value);
          }
        }
      }
    }
  }

  void SynthVoiceHandler::noteOn(int note, mono_float velocity, int sample, int channel) {
    if (getNumPressedNotes() < polyphony() || !legato())
      note_retriggered_.trigger(constants::kFullMask, note, sample);
    VoiceHandler::noteOn(note, velocity, sample, channel);
  }

  void SynthVoiceHandler::noteOff(int note, mono_float lift, int sample, int channel) {
    if (getNumPressedNotes() > polyphony() && isNotePlaying(note) && !legato())
      note_retriggered_.trigger(constants::kFullMask, note, sample);

    VoiceHandler::noteOff(note, lift, sample, channel);
  }

  bool SynthVoiceHandler::shouldAccumulate(Output* output) {
    if (output->owner == amplitude_envelope_)
      return false;

    return VoiceHandler::shouldAccumulate(output);
  }

  SynthLfo* SynthVoiceHandler::getLfo(int index) {
    return lfos_[index]->lfo();
  }

  void SynthVoiceHandler::correctLfoToTime(int index, double seconds) {
    if (index >= 0 && index < kNumLfos)
      lfos_[index]->correctToTime(seconds);
  }

  void SynthVoiceHandler::correctToTime(double seconds) {
    for (int i = 0; i < kNumLfos; ++i)
      lfos_[i]->correctToTime(seconds);

    for (int i = 0; i < kNumRandomLfos; ++i)
      random_lfos_[i]->correctToTime(seconds);
  }

  void SynthVoiceHandler::disableUnnecessaryModSources() {
    for (int i = 0; i < kNumLfos; ++i)
      lfos_[i]->enable(false);

    for (int i = 1; i < kNumEnvelopes; ++i)
      envelopes_[i]->enable(false);

    for (int i = 0; i < kNumRandomLfos; ++i)
      random_lfos_[i]->enable(false);

    random_->enable(false);
  }

  void SynthVoiceHandler::disableModSource(const std::string& source) {
    if (source != "env_1")
      getModulationSource(source)->owner->enable(false);
  }

  void SynthVoiceHandler::enableModulationConnection(ModulationConnectionProcessor* processor) {
    enabled_modulation_processors_.push_back(processor);
  }

  void SynthVoiceHandler::disableModulationConnection(ModulationConnectionProcessor* processor) {
    enabled_modulation_processors_.remove(processor);
  }

  void SynthVoiceHandler::setupPolyModulationReadouts() {
    output_map& poly_mods = SynthModule::getPolyModulations();

    for (auto& mod : poly_mods)
      poly_readouts_[mod.first] = registerControlRateOutput(mod.second, mod.second->owner->enabled());
  }

  output_map& SynthVoiceHandler::getPolyModulations() {
    return poly_readouts_;
  }
} // namespace sbvital
