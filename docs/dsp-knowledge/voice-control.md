# Voice Control execution

## Facts

- The8-step sequence advances on engine note starts/recalls; each voice retains its sequence number. Length is1…8. Legato advances the step but retains that voice's random draws. All-sounds-off resets the counter. `source: core/engine-own/own_engine.cpp (startNote/reset), voice/voice.cpp (start/legatoTo/evaluateVoiceControl)`.
- RANDOM PAN is a unipolar0…100% variation depth. Sequence PAN is bipolar−100…100% position offset. Random DETUNE is0…100cents;10× multiplies only its random depth, not the sequence value. All selected sources, including generated/sampleNoise, add the same masked tuning offset; keyboard tracking controls only the keyboard-derived offset. `test: ownv_voice_control_noise_detune_and_random_tenfold`, including KEYTRACK on/off, targetOFF identity, zero-depth identity and3c×10=30c; generated noise is also waveform-identical to the equivalent explicit generator pitch.
- ENV scaling multiplies delay/attack/hold/decay/release time, not sustain/curve values. Per-voice ENV variation uses4^(step/100+random*depth/100), then global time percentage. `source: voice.cpp (evaluateVoiceControl/envSettings)`; `test: ownv_voice_control_envelope_scaling_random_and_sources`, `voice_legato_note_takes_the_envelope_scaling_of_its_step_without_modulation`.
- LFO Percent mode scales duration (inverse frequency); Rate mode scales frequency directly. Shared Free-run/mono LFO and globalRND clocks apply the factor in OwnEngine::controlTick. Local/poly and HOST/KEY voice clocks apply it in Voice, exactly once at their owning clock. `test: ownv_voice_control_scales_shared_and_local_lfo_clocks`:36comparisons across Hz/BPM/KEY, Percent/Rate, local/shared/mono/RND/HOST against explicit equivalent rate/division/transpose settings.
- TARGETS selects PAN/DETUNE applicability to A/B/C/Sub/Noise. CUTOFF and sequence/randomENV variation remain per-voice shared-filter/envelope controls, MOD1/MOD2 remain matrix sources, and globalENV/LFO scaling remains global. Turning every targetOFF does not disable those controls. `test: ownv_voice_control_targets_are_pan_detune_only` proves cutoff andENV still affect output with all targetsOFF.

## Evidence limits

- Tests are Mac offline source/audio checks, not MPC performance/device measurement.
- Exact Serum seed, steal/reset, nonlinear scaling and10× equivalence is not established by public documentation. Vital's official product material does not establish a direct matching Voice Control panel. See [the public-documentation review](../review/VOICE-CONTROL-REVIEW-2026-11-24.md).
- Dedicated all-envelope release/beats, random-lane independence and voice-steal audio regressions remain outside the focused fixes above; existing sequence/ENV-cache behavior is preserved.
