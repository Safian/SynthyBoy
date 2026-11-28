# Audio quality gates

What is current for the MPC product: the tier table (what each quality tier caps or bypasses). The measurements of earlier engines are not kept here; the DSP facts are in [dsp-knowledge](dsp-knowledge/README.md), the numbers in [PERFORMANCE.md](PERFORMANCE.md).

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
