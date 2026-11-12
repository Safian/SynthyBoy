# Questions from the implementer

The implementer (branch `engine-own-impl`) writes questions here. The spec author answers in the document concerned and marks the row.

| # | Document | Question | Status |
|---|----------|----------|--------|
| 1 | 02 | Which built-in tables with several frames (Saw to Square, Sine to Saw, Harmonic Series, Odd Harmonics, Formant Sweep, FM Sweep, PWM Sweep, Bell Partials) have which frame definitions in engine-vital? The own engine defines them itself (only the first frames agree in level). Please give the per-frame spectra or the construction. | open |
| 2 | 03 | Confirm: the Sine LFO shape starts at the bottom (0.5 - 0.5 cos), so `-cos` in the bipolar view. Measured black box, agrees with L1; needs only a yes. | open |
| 3 | 04 | M9 (`modNN.stereo` on `oscN.pan`): does a destination with one value per voice get two lanes (L = +c, R = -c) before the pan law? Exact order of lane split and pan law for the oscillator pan, noise pan, filter cutoff. | open |
| 4 | 05 | Section 1.4 and 2 refer to "7.0" for what `filterN.style` means per model and for how `blend` is used by the styles 2 (notch/pass swap), 3 (dual notch/band) and 4 (band/peak/notch) of the Digital, Analog, Dirty and Ladder models, but the document has no section 7.0. Please give the weights of LP / BP / HP (and the cutoff shifts of the dual notch) as a function of `blend` for each of these styles. The own engine implements the shelving style 5 exactly (A = 1: identity at 0 dB) and an approximation of 2 .. 4 until this is answered. | open |
| 5 | 05 | Formant style 2 (vocal tract): the document allows a real model or a pass-through. The own engine implements a tract style (vowels E U O A at the corners, narrower resonators, glottal tilt). Please confirm that this is wanted, or give the intended behaviour. | open |
| 4 | 07 | The sample voice's centre pan law costs a mono sample 3.0 dB; the granular type then measures -1.49 dB and the spectral type -2.90 dB (S4). In the own engine the granular library voice already lands at -1.52 dB without a factor, the spectral voice at -8.7 dB: it needed a factor of 1.95 (not 5.3703) to reach -2.90 dB. Is the 5.3703 relative to a raw level of the Vital-side library voice that differs from `core/spectral`? (Calibrated on S4, no action needed unless the level should be matched on other samples.) | open |
| 5 | 02 | Remap 1 .. 4 (warp entries 20 .. 23): the "drawn remap curve of the oscillator" - is it the curve of `oscN.remap.slot` (the one of the frame position remap) or a separate curve? The own engine uses the curve of the selected slot (identity when none is stored). | open |
| 6 | 07 | Choke groups (`off_by`) release the layers of other voices; the glue's scope is not stated (all voices of the engine, or of the same oscillator slot). The own engine releases within one voice only. | open |

