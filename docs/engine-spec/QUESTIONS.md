# Questions from the implementer

The implementer (branch `engine-own-impl`) writes questions here. The spec author answers in the document concerned and marks the row.

| # | Document | Question | Status |
|---|----------|----------|--------|
| 1 | 02 | Which built-in tables with several frames (Saw to Square, Sine to Saw, Harmonic Series, Odd Harmonics, Formant Sweep, FM Sweep, PWM Sweep, Bell Partials) have which frame definitions in engine-vital? The own engine defines them itself (only the first frames agree in level). Please give the per-frame spectra or the construction. | open |
| 2 | 03 | Confirm: the Sine LFO shape starts at the bottom (0.5 - 0.5 cos), so `-cos` in the bipolar view. Measured black box, agrees with L1; needs only a yes. | open |
| 3 | 04 | M9 (`modNN.stereo` on `oscN.pan`): does a destination with one value per voice get two lanes (L = +c, R = -c) before the pan law? Exact order of lane split and pan law for the oscillator pan, noise pan, filter cutoff. | open |
