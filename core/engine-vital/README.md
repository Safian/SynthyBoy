# engine-vital

Vital-derived DSP behind `synthyboy::IEngine`. Everything that contains Vital-derived code lives in this directory and
nowhere else (docs/ARCHITECTURE.md 2.2); it is built only with `-DSYNTHYBOY_WITH_VITAL=ON` (default; forced OFF for iOS,
where `scripts/check-no-vital.sh` checks the static library).

```
upstream/   the imported Vital sources (GPL-3.0-or-later, namespace sbvital), every change listed in docs/UPSTREAM.md
compat/     vital_compat.h: what JuceHeader.h used to provide (std headers, no-op leak detector, base64)
adapter/    our code (AGPL-3.0-or-later): vital_engine.{h,cpp}, the IEngine implementation (compiled into synthyboy_core when
            SYNTHYBOY_WITH_VITAL=ON; the imported DSP is the static library synthyboy_engine_vital, which depends on nothing of ours)
tests/      adapter tests (feature "golden" measurements, FFT) and the ported Vital unit tests (tests/synthesis/**)
```

`createEngine (EngineKind::vital)` (core/include/synthyboy/engine.h) returns the engine, or `nullptr` plus an error text if
the build has no Vital. The JUCE shell uses the own engine unless configured with `-DSYNTHYBOY_SHELL_ENGINE=vital`.

## What the adapter does

`prepare()` (the only call that allocates) builds a complete `sbvital::SoundEngine`, the wavetable of oscillator 1,
a default (12-tone equal temperament) `Tuning`, sets the sample rate and 120 BPM, keeps Vital's default oversampling (2x),
applies the init patch and the parameter mapping, and renders two short notes so that lazily built tables exist before
the audio thread runs. `process()` cuts the host block at every event frame and into pieces of at most 128 frames
(`sbvital::kMaxBufferSize`), applies the events between the pieces and converts Vital's interleaved `poly_float` output
(lanes 0/1 = left/right) to left/right. It sets flush-to-zero around the call and restores the caller's mode.

### Parameters (ParamInfo stays our own table)

| ParamInfo | Vital control | Mapping |
|-----------|---------------|---------|
| `gain` (dB, -60..6) | `volume` | Vital shows `sqrt(value) - 80` dB, so `value = (dB + 80)^2`; at or below -60 dB the control goes to -80 dB (mute, as in the own engine) |
| `cutoff` (Hz, 20..20000) | `filter_1_cutoff` | MIDI note scale: `12 * log2(hz / 8.1757989)` (clamped to Vital's 8..136; Hz is limited to 0.45 x sample rate first) |
| `waveform` (Sine, Saw) | `osc_1_wave_frame` | oscillator 1 has a two-frame wavetable built in code from the wavetable model (`WaveSource` with Vital's predefined sine and saw shapes, no file, no preset): frame 0 = sine, frame 1 = saw |
| `Quality::maxVoices` | `polyphony` | capped to 1..32 |

Events: `NoteOn` (velocity 0..1; a still-ringing note of the same key is released first, as in the own engine), `NoteOff`,
`PitchBend` (per channel, +-2 semitones: `setPitchWheel (v * 2 / 48, channel)`, Vital's per-channel bend has a range of 48),
`AllNotesOff` (all channels, or one channel).

### Init patch (everything the three parameters do not touch)

Oscillator 1 on at Vital's default level (0.7071, squared = amplitude 0.5), one unison voice, centred, phase randomisation
off (so every note starts the same way and renders are reproducible), distortion and spectral morph off, destination
filter 1. Filter 1 on, model Digital (state variable, 12 dB), low-pass, resonance 0.2, no drive, no key tracking. Oscillators 2
and 3, the sample, filter 2, the filter effect and all effects off. Envelope 1 (amplitude) at Vital's defaults
(attack 0.5 ms, decay 1 s, sustain 1, release about 90 ms), no modulation connections, no LFOs routed, portamento minimal,
velocity tracking 100 %. The Digital filter model is the transparent one: Vital's Analog, Ladder and Diode models saturate
and add odd harmonics to a sine.

## Behaviour that differs from the own engine (measured, see core/tests)

* Level: at the same gain the Vital engine is about 2.4x (7.4 dB) louder (its signal chain is calibrated differently).
* Velocity curve: amplitude ~ velocity squared (Vital's `velocity_track` interpolation); the own engine is linear.
* Release about 90 ms (own: 250 ms). Both end in exact zero and 0 voices.
* Voices: `activeVoices()` counts released voices that still fade out and stolen voices during their 50 ms kill fade, so it can
  exceed `Quality::maxVoices` for a moment when notes arrive faster than they fade (at most 34 seen with 32 allowed).
  The number of *sustaining* voices never exceeds the limit.
* Block size: without pitch bends the output does not depend on the host block size (relative RMS difference below 3e-6,
  because the block is cut at every event); a bend that arrives while a note sounds moves Vital's control-rate pitch
  over the processed piece, so its shape depends on how the host splits the stream (relative RMS difference up to 10 %
  for a sine between 1-sample and 4096-sample blocks).
* `reset()` renders 100 silent pieces (12800 frames) so that Vital's smoothers sit on the current parameter values; the
  next render equals the first one up to about 1e-6 relative RMS (voice filters keep tiny residues), not bit for bit.
  Two freshly prepared instances render bit-identical output.
* Note offsets inside a piece are not sample accurate in Vital (the `sample` argument of `noteOn` does not delay the
  audio), which is why the adapter cuts the block at every event frame instead.
* `prepare()` takes about 40 ms (builds the whole engine).

## Real-time rules

`process()` does not allocate (tested with a counting `operator new`, all tiers), takes no locks (the imported code has none)
and throws nothing. Parameter changes are atomics, copied into Vital's controls at the start of a block on the audio thread.
The two global singletons of the imported code (`PredefinedWaveFrames`, `FFT<bits>`) and the first-use tables are
initialised in `prepare()`.
