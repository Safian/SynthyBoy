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

**Not opened yet (to do):** Dattorro (Effect Design Part 1), Brandt (Hard Sync Without Aliasing), Parker/Zavalishin/Le Bivic
(DAFx-16), Vicanek (Anti-aliased distortion), Zavalishin (The Art of VA Filter Design 2.1.2), MPE 1.0 text (RP-053),
Esqueda/Välimäki/Bilbao (Rounding Corners with BLAMP: core.ac.uk returned 403), Bencina (Real-time audio programming 101:
connection refused), ARM Cortex-A17 TRM (no public VDIV timing table found by search).
