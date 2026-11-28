# Audio quality gates

What is current for the MPC product: the tier table (what each quality tier caps or bypasses) and the parameter effect audit. The measurements of earlier engines are not kept here; the DSP facts are in [dsp-knowledge](dsp-knowledge/README.md), the numbers in [PERFORMANCE.md](PERFORMANCE.md).

## Tiers and the modules

* `Quality::fxMask` has one bit per effect module (bits 9 .. 15: Hyper / Dimension, Utility, Bode, Convolve, the three splitters). **Tier low bypasses Hyper / Dimension, Bode and Convolve** (in addition to the reverb and the flanger); medium and high run all modules.
* Sample oscillator: the interpolation follows the tier (4 taps on low = oversampling 1, 8 taps on medium, 16 taps with mip blending on high), `maxUnison` caps the unison voices of a sample too. No other bypass.
* Serum filter models: four scalar filters per voice pair and filter slot; no bypass on any tier.
* Racks, buses and DIRECT: a silent bus without enabled effects costs one scan of the block.

### Tier table of the modules of state version 3

What each tier does with the new modules (`qualityForTier`, `core/include/synthyboy/quality.h`; the tier only caps or bypasses, it never changes stored values).
"bypassed" means the module is switched off by `Quality::fxMask` and costs nothing.

| Module | low (MPC class) | medium (phone) | high (desktop) | Test |
|--------|-----------------|----------------|----------------|------|
| Voices / unison per oscillator | 8 / 4 | 16 / 8 | 32 / 16 | `test_engine`, `test_own_engine` |
| Oversampling, wavetable interpolation | 1x, linear | 2x, linear | 2x, spectral | `test_own_alias_baseline` |
| Sample oscillator | 4-tap, oversampling 1 | 8-tap | 16-tap with mip blending | `synthyboy_test_sample_osc` |
| Serum filter models (37 types) | runs | runs | runs | `test_own_engine`, `synthyboy_test_filters_serum` |
| Buses, DIRECT, FX racks | runs | runs | runs | `test_own_engine` |
| Reverb, flanger | bypassed | runs | runs | `test_own_convolve_bypass`, `test_own_fx` |
| Hyper / Dimension | bypassed | runs | runs | `test_own_engine`, `synthyboy_test_fx_serum_a` |
| Bode (frequency shifter) | bypassed | runs | runs | `test_own_engine`, `synthyboy_test_fx_serum_b` |
| Convolve (partitioned FFT) | bypassed | runs | runs | `test_own_engine`, `synthyboy_test_fx_serum_b` |
| Utility, splitters | runs | runs | runs | `test_own_engine` |
| Tuning, key scale, MPE, curves, custom LFO shapes, oscillator mapping | no tier dependence | | | `test_engine` |

Measured behaviour of the tiers on the factory presets: identical between low, medium and high to within 2.2 dB in the peak of any preset (the factory presets use one oscillator, one filter and at most the unison voices the low tier allows, so the caps do not bite).

## Parameter effect audit (2026-11-11)

Does the sound at the output really change, and the way it should, when a value changes? `scripts/run-param-audit.sh` answers that for the whole parameter table of the
own engine (`tools/param-audit`):

* **Stage 1, the scan** (`synthyboy_param_audit`): every key the engine supports is put into its context (the first state of a ladder in which it is audible: effect on, oscillator
  type, routed LFO / envelope, filter model ...), rendered offline at every value (`--full`: every step of a stepped key, 65 points + default, both neighbours and the edges of a
  continuous one; `--quick`: nine) and measured (RMS, peak, centroid, 24 bands, balance, f0, envelope, zero crossings, click, level modulation). Modulatable keys are swept again
  through a routed source (macro 1 in a modulation slot). A key is ALIVE if the audio changes by more than the noise floor of the harness (-60 dB re peak, or 3 dB above the run to run
  noise of a random context), DEAD if not in any context of the ladder; DEAD keys need an entry with a reason in `tools/param-audit/exemptions.tsv` or the run fails.
* **Stage 2, the semantics** (`synthyboy_test_param_semantics`, ctest label `slow;audit`): pitch in cents, levels in dB, the pan law, filter cutoff / resonance, envelope times, LFO rate,
  mod matrix amount (all 64 slots), delay time, EQ gain, effect mix 0 = dry, velocity law, portamento time ... against the expected function, point by point over the sweep.
* **Baseline**: the baseline file (default `docs/ui-design/param-audit-baseline-own.json`, written by `--update-baseline`; none is committed yet) holds verdict + a compact metric curve (nine points) per key. A run reports REGRESSIONS (changed verdict or curve) with the old and
  new numbers and exits 1; intended changes are accepted with `--update-baseline` and show up in `git diff`. Keys whose context holds a random source (granular grains, the reverb, the Random LFO; `curve:` lines of `tools/param-audit/exemptions.tsv`) are compared by verdict only.
* **Mutation self test** (`--self-test`, ctest `synthyboy_param_audit_selftest`): seven faults are injected into the harness through `SYNTHYBOY_AUDIT_FAULT` (cutoff scaled, pan swapped, delay
  time x2, transpose 2 cents off, master volume in half the dB, attack halved, chorus controls unwired); every one must be caught.

**Baseline across platforms.** The baseline records the platform that made it (`platform`: `darwin-arm64`). On that platform the curves compare to one quantum (1 dB, a quarter octave, a
semitone); on another (for example Linux x86_64) to two quanta, the verdicts always exactly: the same code rounds differently on another CPU and compiler (an oscillator's `floorToInt`, a
pitch detector at a bin boundary). Points whose rms is below -100 dBFS compare as silence: the centroid, f0 and balance of rounding noise are not a property of the engine, and an rms
of -170 dB against -166 dB is a different amount of nothing (for example `osc1.pitch_coarse` at +100 % modulation, where the pitch leaves the audible
range and the output is silent, and `osc3.wavetable`). A baseline made on another platform is a `--update-baseline` away; two baselines are not needed.

```
scripts/run-param-audit.sh --quick                  # minutes: the quick subset (ctest `synthyboy_param_audit_scan`)
scripts/run-param-audit.sh --full --jobs 8          # every value of every key (hours)
scripts/run-param-audit.sh --group filter1. --full  # only a part;  --keys REGEX also works
scripts/run-param-audit.sh --keys '^filter1\.cutoff$' --keep-wavs /tmp/w   # listen to what a key does
scripts/run-param-audit.sh --quick --update-baseline                       # accept an intended change
```

Output: `build/param-audit/<UTC timestamp>/` (+ `latest`): `report.md`, `keys.csv`, `keys.json` (with the curves), `semantics.log`. Exit codes: 0 clean, 1 regression / unexplained DEAD /
failed semantic check, 2 usage, 3 build problem.
