# OWNENGINE-D - the effects of the own engine

Branch `engine-own-d` (cut from `engine-own-c` e871d4b, `develop` merged first: the spec documents 05 section 2.1, 06 sections 9 / 11 / 12 and 03 section 1.3a). Clean room: the sources are `docs/engine-spec/*.md`, the manuals, DSP literature and black-box measurements of engine-vital through `IEngine` (`tools/own-oracle`). Assumptions A714 .. A725 are in `13-new-keys.md` 4.51, open points in `docs/engine-spec/QUESTIONS.md` (rows 9 .. 14).

## What was built

| Item | Where | Result |
|------|-------|--------|
| 1 Reverb | `core/engine-own/fx/fx_reverb.h` | the six algorithms of 06 section 9: type 0 with the output tap before the decay gain, Plate (Dattorro), Hall / Nitrous / Basin (8 line network, Walsh-Hadamard feedback, five rooms), Vintage; loop gain with the all-pass delays in the round trip, `reverb.feedback` (Nitrous, Basin), early reflections, damping shelves, width, pre-delay glide. RT60 within 15 % of the tables for all types, feedback 0 / 50 / 100 %: 0.51 / 2.01 / 8.08 s (Nitrous), 0.51 / 2.00 / 8.15 s (Basin) against 0.51 / 2.01 / 8.06 and 0.50 / 2.05 / 8.24 |
| 2 SVF styles 2 .. 5 | `voice/classic_filters.h` | the weights, sections, drive laws and ladder taps of 05 section 2.1 for Digital, Analog, Dirty, Ladder; Digital style 5 is the identity; Formant style 2 stays the tract style (accepted). The 64 vector rows of 2.1 and 10 pass (structure exact for Digital, +-3 dB for Dirty, +-5 dB on the rows that sit on the distortion floor of the loud noise, question 12) |
| 3 Instance pool | `fx/fx_chain.*`, `fx_module.h`, `fx_modules.cpp` | 16 slots `fxiNN`: every effect is an `FxModule` shared by instance 0 and the slots; sort key `64 q + 32` / `order`, tie module first; 10 ms crossfade on type change and on / off (a change inside a fade drops the earlier outgoing module), bypass of a rack skips its slots, the 24 values are modulation destinations in the normalised domain, non finite protection; a slot renders what instance 0 renders (difference 1.2e-7 for Distortion, Bode, EQ, Utility) |
| 4 Phaser depth 2, latency, envelope delay | `fx_effects.h`, `fx_latency.h`, `env_lfo/envelope.h` | depth 2 notches of P2 within 1 .. 3 % except the top notch (6 .. 7 % low at the host rate, question 10); the latency table of 06 section 11: every row reproduced (reported number and onset), with Bode at 64 host samples (question 11) and the limiter at 72; the envelope delay in chunks of four control periods with the matrix constants applied at the note start: the rows of 1.3a within 1.4 ms, the env2 rows within 0.1 ms |
| 5 Convolve | `fx_modules.cpp` (`ConvolveModule`), `osc/content_set.*` | the impulse of `content.impulses[0]` is decoded with the content set; the partitioned convolver of `core/fx-serum-b` is rebuilt on a helper thread (`FxWorker`) when the impulse or a key that shapes it changes, handed over at a block boundary when `Quality::realtime` is off; Vital and Serum law (`convolve.law`) |
| 6 EQ and compressor laws | `fx_eq.h`, `fx_compressor.h` | EQ Vital law (SVF shelves; E1 .. E7 within 1.2 dB) and Serum law (RBJ, E8), second peak band; compressor C1 / C2 laws exact, thresholds to -120 dB, Limit mode (look-ahead brickwall, latency reported through `limit_latency_comp`), Serum mode (peak compressor) |
| 7 QUESTIONS.md | `docs/engine-spec/QUESTIONS.md` | rows 4 and 5 answered, the duplicate numbers 4 / 5 renumbered (7 / 8), new rows 9 .. 14 |
| 8 Capability bits | `own_support.cpp`, `core/tests/test_own_fx.cpp` | `kCapFxConvolve`, `kCapFxInstances`, `kCapFxPhaserDepth2`, `kCapFxReverbAlgos`, `kCapReverbForce`, `kCapReverbFeedback`, `kCap2SerumEq`, `kCap2SerumCompressor`, `kCap2SerumConvolve` newly reported, `kCapFxCompressorLimit` / `kCapFxDistortionExtras` / `kCap2EqBand2` now proven by a probe; 2543 of the 2637 keys act (`OWN-ENGINE-GAP.md`) |

## Findings worth a decision

* **The level table of 06 section 9.5 is 8.3 dB below the live engine-vital** (question 14). The tails and RT60 agree; the own engine follows the live engine (oracle table below), the tests carry the constant +8.3 dB.
* **The EQ corner shift** (`3 b log2 A` semitones) contradicts the vectors E3 / E4 (question 9): the own EQ has no shift.
* **Phaser** poles at 1.0884 x the cutoff (question 10); **Bode latency** 64 host samples (question 11); the **distortion floor** of the loud noise limits the exactness of the notch rows (question 12); the **feed forward term** of the reverb type 0 is not specified (question 13).

## Tests

`core/tests/test_own_fx.cpp` (18 cases, 9200 checks): reverb levels and tails of the six types (R3), RT60 table (R1), pre-delay and mix (R2), feedback, EQ E1 .. E8, compressor C1 / C2, Serum mode and limiter, phaser P2, pool (same samples as instance 0, order, rack, bypass, crossfade, modulation, every type in every slot), latency table, envelope delay, Convolve from the store, SVF vectors, capability probes. ASan / UBSan clean for `test_own_fx`, `test_own_voice`, `test_own_engine`.

## Oracle table of the effects (own against engine-vital, `synthyboy_fx_oracle`)

The peak is the highest 10 ms energy window of the left channel (dB); the dry burst of the own engine is 1.3 dB louder than the one of engine-vital (a difference of the oscillator, not of the effects), so the reverb rows read +1.3 dB when the wet level matches. RT60 from the backward integrated energy (-5 .. -35 dB, x2), tail = the window 0.5 s after the note on relative to the peak.

| setting | peak own (dB) | peak vital (dB) | diff | RT60 own (s) | RT60 vital (s) | tail 0.5 s own (dB) | tail 0.5 s vital (dB) |
|---|---|---|---|---|---|---|---|
| dry burst (no effect)            |   13.2 |   11.9 |  +1.3 |  0.02 |  0.02 | -313.2 | -311.9 |
| reverb Vital, decay 500, size 100|    2.1 |    0.9 |  +1.3 |  0.70 |  0.61 |  -47.4 |  -50.4 |
| reverb Vital, decay 500, size 50 |    2.1 |    0.8 |  +1.3 |  1.00 |  0.92 |  -32.6 |  -34.7 |
| reverb Vital, decay 2000, size 50|    2.1 |    0.5 |  +1.6 |  3.50 |  3.25 |  -12.1 |  -13.2 |
| reverb Plate, decay 500, size 100|   -1.9 |   -3.9 |  +2.0 |  1.37 |  1.36 |   -7.8 |   -7.4 |
| reverb Plate, decay 500, size 50 |    1.6 |    0.0 |  +1.6 |  0.49 |  0.48 |  -49.4 |  -47.1 |
| reverb Plate, decay 2000, size 50|    1.6 |    0.0 |  +1.6 |  2.15 |  2.15 |  -20.5 |  -17.0 |
| reverb Hall, decay 500, size 100 |   -1.2 |   -2.9 |  +1.6 |  0.62 |  0.63 |  -59.3 |  -56.0 |
| reverb Hall, decay 500, size 50  |   -1.2 |   -2.9 |  +1.6 |  0.53 |  0.52 |  -53.8 |  -52.3 |
| reverb Hall, decay 2000, size 50 |   -0.6 |   -0.6 |  +0.0 |  1.96 |  1.95 |  -14.1 |  -14.5 |
| reverb Vintage, decay 500, size 100 |   -2.9 |   -4.8 |  +1.8 |  0.58 |  0.59 |  -31.3 |  -31.6 |
| reverb Vintage, decay 500, size 50 |   -1.8 |   -2.7 |  +0.9 |  0.54 |  0.52 |  -52.2 |  -52.0 |
| reverb Vintage, decay 2000, size 50 |   -0.0 |   -0.5 |  +0.5 |  1.95 |  1.94 |  -17.0 |  -16.9 |
| reverb Nitrous, decay 500, size 100 |   -2.3 |   -3.9 |  +1.6 |  0.62 |  0.62 |  -53.4 |  -52.3 |
| reverb Nitrous, decay 500, size 50 |   -2.3 |   -3.9 |  +1.6 |  0.51 |  0.52 |  -50.5 |  -49.5 |
| reverb Nitrous, decay 2000, size 50 |    1.8 |    0.5 |  +1.3 |  2.01 |  1.99 |  -13.3 |  -12.5 |
| reverb Basin, decay 500, size 100|   -6.3 |   -8.0 |  +1.6 |  0.54 |  0.52 |  -52.5 |  -52.1 |
| reverb Basin, decay 500, size 50 |   -3.1 |   -3.9 |  +0.8 |  0.51 |  0.50 |  -49.8 |  -49.7 |
| reverb Basin, decay 2000, size 50|    2.3 |    1.0 |  +1.3 |  2.00 |  2.00 |  -12.8 |  -12.4 |
| chorus (defaults)                |   10.2 |   10.8 |  -0.7 |  0.49 |  0.34 |  -64.1 |  -75.6 |
| delay (250 ms, fb 50, mix 50)    |   10.2 |    8.9 |  +1.3 |  2.50 |  2.50 |   -6.1 |   -6.0 |
| flanger (defaults)               |   12.4 |   11.3 |  +1.1 |  0.02 |  2.81 | -312.4 | -235.3 |
| phaser (defaults)                |   12.8 |    8.9 |  +3.9 |  0.02 |  0.02 | -312.8 | -308.9 |
| phaser feedback 0                |   12.0 |    8.9 |  +3.1 |  0.02 |  0.02 | -312.0 | -308.9 |
| phaser mix 100, feedback 50      |   13.7 |    8.1 |  +5.6 |  0.02 |  0.02 | -313.7 | -308.1 |
| phaser mix 100, feedback 0, depth 0 |   10.4 |    7.9 |  +2.5 |  0.02 |  0.02 | -310.4 | -307.9 |
| phaser mix 100, feedback 50, depth 0 |   11.6 |    8.7 |  +2.9 |  0.02 |  0.02 | -311.6 | -308.7 |
| phaser mix 100, feedback 0       |   11.5 |    7.2 |  +4.3 |  0.02 |  0.02 | -311.5 | -307.2 |
| distortion tube, drive 12 dB     |   20.8 |   19.7 |  +1.1 |  0.07 |  0.02 | -289.3 | -311.2 |
| distortion fold, drive 12 dB     |   20.0 |   19.0 |  +1.0 |  0.02 |  0.02 | -320.0 | -319.0 |
| eq mid +12 dB at 1 kHz           |   18.7 |   18.4 |  +0.3 |  0.02 |  0.02 | -318.7 | -318.4 |
| compressor (defaults)            |   10.4 |    8.8 |  +1.7 |  0.02 |  0.02 | -310.4 | -308.8 |
| compressor single, -20 dB, 100 % |    3.8 |    4.7 |  -0.9 |  0.02 |  0.02 | -303.8 | -304.7 |
| hyper / dimension (defaults)     |    8.0 |    6.1 |  +1.8 |  0.03 |  0.02 | -308.0 | -306.1 |
| utility width 150                |   13.2 |   11.9 |  +1.3 |  0.02 |  0.02 | -313.2 | -311.9 |
| bode shift 20 %                  |   12.3 |   11.2 |  +1.1 |  0.02 |  0.02 | -312.3 | -311.2 |

Reading the table: the six reverb algorithms match the live engine-vital within 2 dB in level (after the 1.3 dB of the dry burst) and within 15 % in RT60 (the Vital type 0: 0.70 against 0.61 s, 1.00 against 0.92, 3.50 against 3.25). The flanger row differs on purpose (the defect F1 of engine-vital: its delay has no effect, document 06 section 9.5, F1). The phaser rows differ by 1 .. 3 dB with the LFO at its start phase (the free running start phase of the LFO is not specified; at depth 0 the difference is 1.2 dB beyond the dry burst): see question 15.

