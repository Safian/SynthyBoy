# Opened sources

Only sources that were opened and read go here (title, where, what part, date read, what it is
used for). Sources named from memory in earlier research (Bristow-Johnson, Brandt, Dattorro,
Bencina, Zavalishin, the MPE and SFZ specifications, ARM Cortex-A17 documentation) are NOT here
until opened.

| Source | Part | Read | Used for |
|---|---|---|---|
| Audio EQ Cookbook, W3C Working Group Note 08 June 2021, ed. Raymond Toy, adapted from R. Bristow-Johnson's cookbook — https://www.w3.org/TR/audio-eq-cookbook/ | whole page (sec. 1-2) | 2026-11-21 | biquad EQ / filter coefficients |
| "Practical Considerations for Antiderivative Anti-Aliasing" (CCRMA notebook, author not named on the page) — https://ccrma.stanford.edu/~jatin/Notebooks/adaa.html | up to 2nd-order ADAA (page cut off after) | 2026-11-21 | first-order ADAA and its ill-conditioning fallback |
| "MPE: MIDI Polyphonic Expression" overview, MIDI Association (M1-100-UM v1.1) — https://midi.org/midi-polyphonic-expression-mpe | whole page | 2026-11-21 | identity of the spec: v1.1 replaces RP-053 v1.0 with no technical change |
| Arm Architecture Reference Manual for A-profile architecture (ARM DDI 0487) — developer.arm.com (FPCR.FZ bit [24] and MSR FPCR self-synchronization) | FPCR bit [24] (FZ) and self-synchronizing register access rules | 2026-11-23 | AArch64 `FPCR.FZ` flush-to-zero semantics (`FPCR.AH == 0`) and synchronization rules |
| ARM Architecture Reference Manual ARMv7-A and ARMv7-R edition (ARM DDI 0406) — developer.arm.com (FPSCR.FZ bit [24], VMSR/VMRS serialization, NEON FTZ) | FPSCR bit [24] (FZ), VMSR/VMRS exception/serialization barrier, Advanced SIMD always-FTZ | 2026-11-23 | ARM32 VFP `FPSCR.FZ` vs Advanced SIMD (NEON) flush-to-zero behavior and `VMSR` serialization |

## Facts from the opened sources

**Audio EQ Cookbook (W3C).** Biquad `H(z) = (b0 + b1 z^-1 + b2 z^-2) / (a0 + a1 z^-1 + a2 z^-2)`, normalised by a0;
`A = 10^(dBgain/40)`, `w0 = 2 pi f0 / Fs`, `alpha = sin(w0) / (2Q)`. LPF: `b0 = b2 = (1 - cos w0)/2`, `b1 = 1 - cos w0`,
`a0 = 1 + alpha`, `a1 = -2 cos w0`, `a2 = 1 - alpha`. Peaking: `b0 = 1 + alpha A`, `b2 = 1 - alpha A`, `a0 = 1 + alpha / A`,
`a2 = 1 - alpha / A`, `b1 = a1 = -2 cos w0` (a boost and an equal cut cancel exactly). Shelves use `2 sqrt(A) alpha`.
Coefficients come from the bilinear transform with prewarping; BW to Q: `1/Q = 2 sinh((ln2 / 2) BW w0 / sin w0)` (sec. 2).

**ADAA notebook (CCRMA).** First order: `y[n] = (F1(x[n]) - F1(x[n-1])) / (x[n] - x[n-1])`, F1 the antiderivative of the
nonlinearity. When `|x[n] - x[n-1]|` is below a tolerance (the page's code: 1e-5) use `f((x[n] + x[n-1]) / 2)`. Hard clip:
`F1(x) = x^2 / 2` for `|x| <= 1`, `x sgn(x) - 1/2` outside. First order alone leaves significant aliasing; the page
suggests combining it with modest (2x) oversampling — consistent with our measured 5.91 dB on Zero-Square
([antialiasing.md](antialiasing.md)) and the owner's D1 (2x stage on XL/desktop).


## Read from the PDFs (2026-11-21; text extracted with macOS PDFKit, Dattorro Fig. 1 / Table 2 checked as images)

## Hard Sync Without Aliasing (Eli Brandt, 2001)
- URL: https://www.cs.cmu.edu/~eli/papers/icmc01-hardsync.pdf
- Read: 2026-11-21
- File: (4 pages, Carnegie Mellon University; the paper is dated 2001 by its references, ICMC 2001 per the URL)
- Part read: all 4 pages.
- Facts:
  1. Naive hard sync (reset the slave phase when the master wraps, then sample) aliases like a naive sawtooth (Sec. 2). The slave's output has fundamental f0 (master), formants at multiples of f1 (slave).
  2. Stilson-Smith route (Sec. 3-5): the derivative of the hard-sync signal is an impulse pattern: n01 = floor(T0/T1) full impulses spaced T1 apart, one impulse of height r01 = T0/T1 - n01, minus DC offset d = f1. Replace the impulses with bandlimited ones and integrate. The integrator must be leaky: a first-order leaky integrator (-0.5 dB at 20 Hz) has 60 dB DC gain, which turns the ~1e-6 offset into a persistent -60 dB. A 2nd-order leaky integrator (leaky integrator cascaded with a one-pole highpass, c = 0.9992 at 44.1 kHz) has zero DC gain (Sec. 5).
  3. Windowed-sinc table (Sec. 4.2): oversample the table by factor Omega, step through it Omega places at a time per output sample (for a bandlimit of Nyquist), start at place Omega(1-alpha) for a pulse centred alpha samples past the last sample, linear interpolation is enough if Omega is large. Figure example: Omega = 64, Nz = 16 zero crossings, Blackman window. The window sets the stopband (alias) level, Nz sets the transition width.
  4. BLEP (Sec. 6.1): pre-integrate the windowed sinc into a bandlimited step with final value 1 and place steps instead of impulses; this removes the integrator and its DC problem. A running sum of the table is slightly inexact at high frequencies only (0.25 dB at -2 octaves, negligible if the table is oversampled several octaves more).
  5. MinBLEP (Sec. 6.2-6.3): turn the symmetric windowed sinc into a minimum-phase filter with the same magnitude (cepstral method, "zero the upper half of the cepstrum", MATLAB rceps), then integrate. The step has no lookahead (the symmetric version needs half an impulse length of prediction) and needs no integration stage. To use it: do not jump to the new level, mix in a MinBLEP instead.
  6. Validity (Sec. 6.3): steps replaced by MinBLEPs are correct only if the first and higher derivatives are essentially continuous across the discontinuity (C1): square, saw, hard-synced square and saw qualify. Hard-synced triangle needs ramps (C2) as well; hard-synced sine can only be approximated (aliasing falls 6 dB/oct per derivative made bandlimited). Cost is roughly proportional to f1, because that is how many impulses per sample are needed (Sec. 7). Soft sync idea: examine the slave phase at the end of each master cycle (Sec. 8).

## Effect Design Part 1: Reverberator and Other Filters (Jon Dattorro, 1997)
- URL: https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf
- Read: 2026-11-21
- File: (25 pages; J. Audio Eng. Soc., Vol. 45, No. 9, September 1997, pp. 660-684; AES, CCRMA Stanford)
- Part read: PDF pp. 1-7 (Sec. 0 introduction and Sec. 1 Reverberation, including Fig. 1, Tables 1 and 2), plus a skim of the Sec. 2-3 headings (musical filtering, Chamberlin topology). Part 2 Sec. 5 (all-pass interpolation formula) is NOT in this PDF; Part 1 only refers to it.
- Facts:
  1. Topology (Fig. 1, PDF p.3): stereo input summed and halved, then predelay, then a one-pole lowpass ("bandwidth"), then four input allpass diffusers (lattice) with delays 142, 107 (coefficient "input diffusion 1") and 379, 277 ("input diffusion 2"). The result feeds a figure-eight tank of two halves. Left half: modulated allpass 672+EXCURSION (decay diffusion 1, negative coefficients) -> delay 4453 -> damping one-pole -> x decay -> allpass 1800 (decay diffusion 2) -> delay 3720 -> x decay -> into the right half's input sum. Right half: modulated allpass 908+EXCURSION -> delay 4217 -> damping -> x decay -> allpass 2656 -> delay 3163 -> x decay -> into the left half's input sum. The delay lengths are in samples at Fs = 29761 Hz. Only the delay taps at nodes 24 and 48 (the two modulated allpasses) are modulated.
  2. Parameter table (Table 1, p.663), defaults at Fs = 29761 Hz:

     | Parameter | Default | Note from the paper |
     |---|---|---|
     | EXCURSION | 16 | max peak sample excursion of delay modulation (footnote 14: about 8 samples at ~29.8 kHz, rate on the order of 1 Hz) |
     | decay | 0.50 | rate of decay |
     | decay diffusion 1 | 0.70 | density of the tail |
     | decay diffusion 2 | 0.50 | rule: = decay + 0.15, floor 0.25, ceiling 0.50 |
     | input diffusion 1 | 0.750 | decorrelates the incoming signal |
     | input diffusion 2 | 0.625 | decorrelates the incoming signal |
     | bandwidth | 0.9995 | input lowpass; full bandwidth = 0.9999999 |
     | damping | 0.0005 | HF damping in the tank; none = 0.0 |

     The one-pole form is: bandwidth is the feed-forward gain and (1 - bandwidth) the feedback gain; for damping, the damping coefficient is the feedback gain and (1 - damping) the input gain (Fig. 1). Lattice coefficient range 0.0 to 0.9999999; above 1.0 is unstable; the two multipliers in a lattice must be identical; "decay diffusion 1" lattices use negative coefficients (changes the character of the impulse response but stays all-pass) (Sec. 1.3.3).
  3. Output taps (Table 2, p.665): the output is all wet, each tap scaled 0.6. YL = +266 and +2974 of nodes 48-54 (the right-half first delay), -1913 of nodes 55-59, +1996 of nodes 59-63, -1990 of nodes 24-30, -187 of nodes 31-33, -1066 of nodes 33-39. YR = +353 and +3627 of nodes 24-30, -1228 of nodes 31-33, +2673 of nodes 33-39, -2111 of nodes 48-54, -335 of nodes 55-59, -121 of nodes 59-63 (signs: + add, - subtract; the index is the tap position in samples inside that delay line). The input is mono-summed, so the stereo image is synthetic (Sec. 1.3.6).
  4. Modulation (Sec. 1.3.7): slowly modulate the nominal tap point of the two delay lines at nodes 24 and 48 (about 1 Hz, peak excursion about 8 samples at 29.8 kHz) with linear or, better, all-pass interpolation; linear interpolation adds time-varying lowpass damping to the tank, all-pass interpolation does not. It raises effective mode density ("picket fence" otherwise), helps drums, may sound like vibrato on piano. The same rate and depth are used for the pair, but a quadrature (sine/cosine) oscillator drives them to decorrelate. The LFO must run at the audio sample rate (footnote 15).
  5. Reverb sizing and implementation notes: the network is only about 22K words of memory at 30 kHz; Schroeder's 3 eigentones/Hz rule predicts about 88K words, and echo density matters as much as eigentone density (Sec. 1.1). Diffusers are lattice allpasses; lattices clip prematurely at internal nodes (do not feed full scale) and produce zero-input limit cycles, which magnitude truncation (truncate toward zero) removes (12-24 dB lower noise floor after input stops, up to 6 dB more THD+N) (Sec. 1.3.3-1.3.4). The three one-pole lowpasses (input bandwidth, two damping) in direct form I do not clip and only limit-cycle at DC (Sec. 1.3.5). Typical use: mix the wet YL/YR with the dry input.
  6. Part 1 also contains (not summarised in depth): musical filter/lattice topologies (Sec. 2, Regalia-Mitra) and Chamberlin state-variable filter analysis incl. truncation noise and signal-path width estimates (Sec. 3, Table 5).

## Note on Alias Suppression in Digital Distortion (Martin Vicanek, 2023, revised 2024)
- URL: https://www.vicanek.de/articles/AADistortion.pdf
- Read: 2026-11-21
- File: (5 pages; dated 4 Nov 2023, revised 19 Sep 2024)
- Part read: all 5 pages.
- Facts:
  1. Starting point: the first-order ADAA of Parker et al. y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]), F the antiderivative of the shaper f (eq. 2). It has 1/2-sample latency and, for f(x)=x, gives (x[n]+x[n-1])/2, which is zero at Nyquist: the "transparent" shaper loses the top of the spectrum (Sec. 2, eq. 3).
  2. Main result (Sec. 2): delay the rectangular kernel by half a sample: y[n] = (F(m1) - F(x[n-1])) / (x[n] - x[n-1]) + (F(x[n-1]) - F(m2)) / (x[n-1] - x[n-2]), with m1 = F-argument (x[n]+x[n-1])/2 and m2 = (x[n-1]+x[n-2])/2 (eq. 4, written with F1/2, F1, F3/2). Same aliasing reduction as eq. 2, one sample delay, and for f(x)=x it becomes (x[n] + 6 x[n-1] + x[n-2]) / 8 (eq. 5): only 6 dB loss at Nyquist. Its zeros are z1 = -1/(3+sqrt 8) and z2 = 1/z1.
  3. Ill-conditioning fix: when consecutive samples are nearly equal, replace the offending fraction in eq. 4 by (1/2) f(x[n-1]). Threshold rule of thumb: sqrt(eps), eps = machine precision (Sec. 2).
  4. Making it exactly transparent: compensate with a one-pole IIR y[n] = b x[n] - a y[n-1], a = 1/(3+sqrt 8), b = 1 + a (eq. 6), used twice in series, one before the shaper and one after, which also cancels the ~4 dB sinc loss at Nyquist of the shaped signal; the scheme is then perfectly transparent in the linear regime.
  5. Cheap shaper: f(x) = x / sqrt(1 + x^2), F(x) = sqrt(1 + x^2). Eq. 2 becomes (x[n] + x[n-1]) / (sqrt(1+x[n]^2) + sqrt(1+x[n-1]^2)) and eq. 4 becomes (1/4) * [(x[n] + 3 x[n-1]) / (F1/2 + F1) + (3 x[n-1] + x[n-2]) / (F1 + F3/2)] (eq. 9-10), both numerically stable for all inputs, with no 0/0 case. It resembles tanh with no significant difference in harmonic intensities, and is cheaper (Sec. 3).
  6. ADAA vs oversampling (Sec. 4): ADAA is best for low-frequency aliases (the sinc zero at multiples of fs folds to DC); the sinc is shallow so it is weaker at high frequencies. For a 1/f spectrum (hard-driven saturator) 2x oversampling gives only 6 dB, 4x gives 12 dB. Together they are best: ADAA steepens the falloff, then oversampling works; ADAA plus 4x oversampling makes aliasing "practically a non-issue even for a hard clipper". Higher-order ADAA can add zeros in the Nyquist band and needs more oversampling; the author uses first order plus oversampling.

## Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution (Julian D. Parker, Vadim Zavalishin, Efflam Le Bivic, 2016)
- URL: https://dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf
- Read: 2026-11-21
- File: (8 pages; Proc. 19th Int. Conf. on Digital Audio Effects, DAFx-16, Brno, 5-9 Sep 2016; Native Instruments)
- Part read: pp. 1-6 in full (Sec. 1-6.3, tables, Moog example), skimmed the rest.
- Facts:
  1. Core formula (Sec. 2): linearly interpolate the input between samples, apply f in continuous time, convolve with a unit-width rectangular kernel, sample: y[n] = (F0(x[n]) - F0(x[n-1])) / (x[n] - x[n-1]) (eq. 9), F0 the antiderivative of f. Examples: tanh -> F0 = log(cosh x) (eq. 20-21); hard clipper -> F0 = x sgn x - x^2/2 inside [-1,1], 1/2 outside, with F1 (antiderivative of x f(x)) = x^3/3 inside, (x^2/2 - 1/6) sgn x outside (eq. 25-26).
  2. Ill-conditioning (Sec. 2.3): eq. 9 is 0/0-like when x[n] ~ x[n-1]. Choose the integration constant so F0(0) = 0 (then precision loss is only in the denominator). Fallback: y ~ f((x[n]+x[n-1])/2) + O((x[n]-x[n-1])^2) (eq. 10). For the triangular kernel (Sec. 3) the two terms of eq. 14 fall back to (1/2) f applied at a point two thirds toward x[n-1] (eq. 15-16), needs F0 and F1, one evaluation of each per sample; ill-conditioning grows with kernel order.
  3. Half-sample delay (Sec. 4): at small signal level f(x) ~ x, eq. 9 becomes y[n] = (x[n] + x[n-1]) / 2 (eq. 17), a half-sample fractional delay by linear interpolation (group delay 0.5 samples, low-pass with zero at Nyquist); the triangular kernel gives a three-tap low-signal response (eq. 18) with a group delay of 1 sample. The extra lowpass is audible at 44.1 kHz.
  4. Feedback use (Sec. 6): the half-sample delay is a problem in delayless (zero-delay) loops. Because eq. 17 equals the FIR part of a direct-form-I trapezoidal integrator, drop that FIR part from the integrator that follows the antialiased nonlinearity (Fig. 5-7) and the delay is absorbed; works when a nonlinearity and an integrator are interleaved in the loop. If there is a summation node in between, put the same FIR/delay part in the other summation path (Fig. 9), which turns out equal to a naive 1-pole with adjusted cutoff (Fig. 10). If a nonlinearity has no integrator beside it, insert a very-high-cutoff (near Nyquist) one-pole after it (care: alters the small-signal response).
  5. Newton-Raphson for the antialiased implicit equation (Sec. 6.2): needs dPhi/dx[n] = (f(x[n]) (x[n]-x[n-1]) - (F0(x[n]) - F0(x[n-1]))) / (x[n]-x[n-1])^2 (eq. 28); ill-conditioned fallback (1/2) f'((x[n]+x[n-1])/2) (eq. 29). Moog-ladder-like test (Sec. 6.3, k = 8, 5 kHz sine, cutoff swept 1 Hz-21 kHz): iterate until convergence reaches -60 dB or better; antialiasing removes "steppy" aliasing-locked resonance.
  6. Efficiency data (Table 1, hard clipper, input gain 10, similar aliasing): no ADAA needs Fs = 529.2 kHz (12x); rectangular kernel 176.4 kHz (4x); triangular 132.3 kHz (3x); SNR about 46.5 dB in all three.

## The Art of VA Filter Design, rev. 2.1.2 (Vadim Zavalishin, 14 February 2020)
- URL: https://www.discodsp.net/VAFilterDesign_2.1.2.pdf
- Read: 2026-11-21
- File: (520 pages; Native Instruments)
- Part read: table of contents (via text index), Sec. 3.8 (prewarping, partly), 3.9 Zero-delay feedback and 3.10 Implementations (PDF pp. 85-89, book pp. 73-77), 5.10 Diode ladder (PDF pp. 176-184, book pp. 164-172), 6.4 Nonlinear zero-delay feedback equation and 6.5 Iterative methods (PDF pp. 195-203, book pp. 183-191), 6.13 Antialiasing of waveshaping (PDF pp. 232-242, book pp. 220-230, read to Fig. 6.72). Everything else unread.
- Facts:
  1. ZDF/TPT (Sec. 3.9-3.10, book pp. 73-77): replacing integrators with trapezoidal ones creates delay-free loops; adding a z^-1 to break them destroys the response near Nyquist, so solve the loop instead. A trapezoidal integrator's output is linear in its input at each tick: y = g x + s ("instantaneous response"), g = wc T/2, s = the stored state. The 1-pole lowpass loop y = g (x - y) + s gives y = (g x + s)/(1+g) (eq. 3.28); code: G = g/(1+g); v = (x - s) G; y = v + s; new state u = y + v (eq. 3.30-3.32). The cutoff is prewarped with tan: wa = (2/T) tan(wd T/2) (eq. 3.10), so g = tan(wc T/2) puts the -3 dB point at the wanted cutoff (Sec. 3.8).
  2. Diode ladder structure (Sec. 5.10, book pp. 164-171): four 1-pole lowpasses with neighbour coupling, y1' = wc((x+y2) - y1), y2' = wc((y1+y3)/2 - y2), y3' = wc((y2+y4)/2 - y3), y4' = wc(y3/2 - y4) (eq. 5.18), 1/2 gains give the averaging and unit DC gain; feedback k from y4 to the input (Fig. 5.49). Transfer function delta(s) = 1/(8(1+s)^4 - 8(1+s)^2 + 1), the Chebyshev polynomial T4(1+s) (eq. 5.27); poles real, in (-2, 0): -1 +- 1/2 +- 1/(2 sqrt 2). With feedback H = 1/(8(1+s)^4 - 8(1+s)^2 + 1 + k); self-oscillation at k = 17 at w = wc/sqrt 2 (eq. 5.29). (The paper's variant 5.19 gives the same transfer function.)
  3. Diode ladder TPT (Sec. 5.10): in nested instantaneous-response form with g in (0, 1/2) for the halved-input lowpasses, solve the loops from the innermost out: g23 = g/(1 - g^2) etc.; the whole ladder's instantaneous gain g04 = g01 g12 g23 g34 lies in (0, 1), and the feedback loop gain is -k g04, so the loop is not instantaneously unstable for k >= -1.
  4. Nonlinear ZDF equation (Sec. 6.4, book p.183): with a tanh in the feedback of a ladder in instantaneous form: u = x - k (G tanh u + S) (eq. 6.12). It is transcendental; for k >= 0 there is exactly one solution (decreasing line vs non-decreasing curve); for -1 < kG < 0 also one; for kG < -1 (instant instability) three.
  5. Iterative methods (Sec. 6.5, book pp. 183-191): fixed point u(n+1) = x - k(G tanh u(n) + S) converges only if |kG| < 1 (worse at high cutoff or resonance), bound the iteration count in real time; a 1-pole smoother in the loop improves high kG but slows good cases (at k = 0 exponential instead of immediate). Newton-Raphson: u(n+1) = u(n) - (u(n) + k(G tanh u(n) + S) - x) / (1 + k G (1 - tanh^2 u(n))) (eq. 6.16-6.17); converges well in near-linear regions, worse when strongly nonlinear or at large |kG|; initial guess: the previous sample's u (good when the new solution is close, but poor if it jumps between the two tanh saturations, then u = 0 may be better); often used to refine another method's result; choosing u (not v = kG tanh u) as the unknown avoids division by zero at k = 0 and atanh domain problems. Bisection always converges (bracket between 0 and kG sgn u) but needs about 10 iterations for -60 dB: probably too costly in real time.
  6. Antialiasing of waveshaping (Sec. 6.13, book pp. 220-230): any waveshaper that is non-polynomial or piecewise with a derivative discontinuity expands the spectrum infinitely (f = x^n expands it n times; a discontinuity in the N-th derivative gives 1/n^(N+1) harmonic rolloff), so sampling it aliases; the only reliable fix is heavy oversampling, and inside a feedback loop the whole loop must be oversampled. First-order ADAA: y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]) (eq. 6.38); for f(x) ~ x it gives (x[n] + x[n-1]) / 2, a half-sample delay (and a lowpass at half Nyquist, not very noticeable at 88.2 kHz and above); 44.1 kHz is "usually insufficient" even with it (88.2 kHz or higher). Inside ZDF loops: when the waveshaper directly precedes an integrator, use a direct form I trapezoidal integrator and drop its first (1+z^-1)/2 half, because the waveshaper already supplies it (Fig. 6.63; the wc T gain then sits mid-integrator, which matters only if the cutoff is modulated, fix by eq. 6.41-6.42); if followed by a full 1-pole, inject the shaped signal after that half (Fig. 6.69-6.72) and solve the resulting delay-free loop as v = g/(1+g/2) (u - s). Section 6.13 continues past Fig. 6.72; later pages were not read.

## MIDI Polyphonic Expression (MPE) Specification, Version 1.0 (MIDI Manufacturers Association / AMEI, 12 March 2018)
- URL: https://midimpe.neocities.org/rp53spec.pdf
- Read: 2026-11-21
- File: (20 pages; title page "MIDI Polyphonic Expression, Version 1.0, March-12-2018, Published By: The MIDI Manufacturers Association")
- Part read: full text (pp. 1-20) except the sample code on pp. 19-20. Note: this version calls the zone-level channel the "Master Channel" (the brief says "manager channel", the newer MIDI 2.0-era term). Same concept.
- Facts:
  1. Zones (Sec. 2.1, pp. 7-8): the channel space is split into a Lower Zone (Master Channel 1, Member Channels allocated upwards from channel 2) and an Upper Zone (Master Channel 16, Member Channels allocated downwards from channel 15). Unassigned channels stay available for conventional use; a channel can belong to only one zone (the most recent MCM wins); an unused zone's Master Channel may serve as a Member Channel of the other, so one zone can have up to 15 Member Channels. Receivers must stop notes and reset controls on any channel entering or leaving MPE (Sec. 2.1.4). Notes are spread one per Member Channel; when more notes than free channels, notes share a channel (Sec. 2.2.1).
  2. MCM (MPE Configuration Message, Sec. 2.1.1, p. 7): RPN 00 06: bytes [Bn 64 06] [Bn 65 00] [Bn 06 mm], n = channel nibble (0 = Lower Zone Master, F = Upper Zone Master, anything else ignored), mm = number of Member Channels (0 = zone off / MPE off for that zone, 1 to F = number of channels). Data entry LSB unused. Example: 7 Member Channels for the Lower Zone = channels 2-8.
  3. Pitch bend (Sec. 2.4, pp. 12-13): defaults are +/-48 semitones for Member Channels (per-note) and +/-2 semitones for the Master Channel (zone-wide); changed with RPN 0 (accepted range +/-1 to +/-96 semitones, whole semitones recommended, LSB transmitted as zero or omitted). Member Channels in a zone must all share one sensitivity: the last RPN 0 received on any Member Channel applies to all (send it to every Member Channel). Master and Member bend are combined (summed). Master bend also affects notes in their release phase (Sec. 3.3.3).
  4. Pressure (Sec. 2.5): Channel Pressure at the note level (Member Channel) and the zone level (Master Channel), combined by the receiver. Polyphonic Key Pressure must not be sent on Member Channels (reserved), only allowed on Master Channel notes. Controllers should send Channel Pressure = 0 immediately before Note On and Note Off where appropriate (Sec. 3.3.4).
  5. Third dimension (Sec. 2.6, 3.3.5): CC #74 (timbre), at the note and zone level, combined separately. "Initial-position": the value at Note On carries the strike position; "Initial-64": CC 74 starts at 0x40 (64) so movement can go either way.
  6. Other rules: zone-wide messages (e.g. damper pedal, Table 1) go only on the Master Channel, and must be ignored on Member Channels; Pitch Bend, Channel Pressure and CC 74 are both zone and note messages (Sec. 2.3); values on each Member Channel must be remembered even with no note playing, as the initial state of the next note (Sec. 3.3.2-3.3.3); Note On with velocity 0 should be treated as Note Off velocity 64.

**Not opened (to do):** Esqueda/Välimäki/Bilbao "Rounding Corners with BLAMP" (core.ac.uk returned 403), Bencina "Real-time audio programming 101" (connection refused), Dattorro "Effect Design Part 2" (all-pass interpolation), ARM Cortex-A17 VFP/NEON instruction timing (no public table found by search).

## Factory text-entry adapter (opened 2026-11-24)

- [JUCE TextEditor::InputFilter](https://docs.juce.com/master/classjuce_1_1TextEditor_1_1InputFilter.html):
  `filterNewText` restricts incoming editor text; it does not commit an application
  name. Used with the actual XL host RTTI to distinguish `NameSetterDataAdapter`
  from the submitted-text path. Exact binary identity and offsets are in
  [native-text-entry.md](native-text-entry.md).
