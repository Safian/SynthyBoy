# Granular Oscillator Loop Crossfade & Test Analysis

**Summary:**
1. **What is really wrong on `develop`:** Forward loop crossfade (`granular.cpp:478-490, 732-744`) unconditionally reads the partner at `g.pos - loopLen` (`[lg0 - xfade, lg0)`), which works when pre-roll exists (`lg0 >= xfade + 1`) but reads out-of-bounds `0.0f` when `lg0 < xfade` (fading the loop end to silence before jumping to `sample[lg0]`), and reverse grains (`step < 0`) have no loop crossfade at all.
2. **Is fix attempt 2 right:** Fix attempt 2 (`25a37f8c` on `agy/fix-batch-1-build`) is continuous across the wrap for its four primary cases **F1, F2, R1, R2** (and bit-identical to `develop` in **F1**), except at the exact full-file boundaries `lg0 < 1` (F1/R2) or `lg1 > fileFrames - 2` (F2/R1) where cubic Hermite tap `i - 1` or `i + 2` touches index `-1` or `fileFrames`; its fallback cases **F3/R3** (`lg0 < xfade` and `lg1 + xfade > fileFrames - 1`) silently shorten the effective crossfade to `min(xfade, lg0)` or `min(xfade, fileFrames - 1 - lg1)` and disable crossfading completely (`LoopXFade::none`) when the loop spans `[0, fileFrames - 1]`.
3. **Why the chirp test (`9a7bd43f`) was fooled and what test decides it:** In the chirp test (`phase = 2*pi*(200*t + 300*t^2)`), every tested loop boundary (`t in {0.0, 0.5, 1.0, 1.5, 2.0} s`) landed on an exact integer number of cycles (`0, 175, 500, 975, 1600`) where `sin(phase) == 0.0f` with positive slope—making both a fade-to-silence jump into `sample[0] == 0.0f` and an uncrossfaded reverse wrap from `sample[lg0] == 0.0f` to `sample[lg1 - 1]` indistinguishable from a normal chirp step; a decisive test must use a non-periodic signal with large, opposite-signed values and slopes at every loop boundary and measure only a narrow window around the exact wrap index.

---

## Q1 — Out-of-Bounds Reads in `readSample` (`hermite`) and `GrainRead::read` (`hermiteFast`)

### Code on `develop` (`core/granular/src/granular.cpp:80-130, 161-166`)

```cpp
// core/granular/src/granular.cpp:80-106
/** 4 point cubic Hermite read of one channel of a mip level; zero outside the data. */
float hermite (const float* d, int64_t n, double pos) noexcept
{
    const int64_t i = static_cast<int64_t> (std::floor (pos));
    const float t = static_cast<float> (pos - static_cast<double> (i));
    const auto at = [&] (int64_t k) { return k >= 0 && k < n ? d[k] : 0.0f; };
    const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

void readSample (const SampleView& s, double pos, double rate, float& l, float& r) noexcept
{
    int m = 0;
    if (rate > 1.0 && s.numMipLevels > 1)
        m = std::min (s.numMipLevels - 1, static_cast<int> (std::floor (std::log2 (rate))));
    if (m > 0 && s.mipLevels != nullptr)
    {
        const auto& lvl = s.mipLevels[m];
        const double p = pos / static_cast<double> (1 << m);
        l = hermite (lvl.channels[0], lvl.numFrames, p);
        r = s.numChannels > 1 ? hermite (lvl.channels[1], lvl.numFrames, p) : l;
        return;
    }
    l = hermite (s.channelData[0], s.numFrames, pos);
    r = s.numChannels > 1 ? hermite (s.channelData[1], s.numFrames, pos) : l;
}
```

```cpp
// core/granular/src/granular.cpp:112-130, 161-166
inline float hermiteFast (const float* d, int n, double pos) noexcept
{
    int i = static_cast<int> (pos); // 32 bit integers: a 64 bit conversion of a double is a library call on 32 bit ARM
    if (static_cast<double> (i) > pos)
        --i; // floor () of a negative position
    const float t = static_cast<float> (pos - static_cast<double> (i));
    float y0, y1, y2, y3;
    if (i >= 1 && i + 2 < n)
    {
        y0 = d[i - 1]; y1 = d[i]; y2 = d[i + 1]; y3 = d[i + 2];
    }
    else
    {
        const auto at = [&] (int k) { return k >= 0 && k < n ? d[k] : 0.0f; };
        y0 = at (i - 1); y1 = at (i); y2 = at (i + 1); y3 = at (i + 2);
    }
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}
// ...
    inline void read (double pos, float& l, float& r) const noexcept
    {
        const double p = scaled ? pos * invScale : pos;
        l = hermiteFast (ch[0], n, p);
        r = stereo ? hermiteFast (ch[1], n, p) : l;
    }
```

### Behavior for negative positions and positions beyond the last frame (`n - 1`)

Let `p` be the mip-scaled read coordinate (`p = pos / 2^m`), `i = floor(p)`, and `t = float(p - i) in [0, 1)`. Both `hermite` and `hermiteFast` fetch 4 taps `(y0, y1, y2, y3) = (at(i - 1), at(i), at(i + 1), at(i + 2))` using the zero-padding guard:
$$\operatorname{at}(k) = \begin{cases} d[k], & 0 \le k < n \\ 0.0\text{f}, & k < 0 \text{ or } k \ge n \end{cases}$$
They **neither clamp to the first/last sample nor wrap**; they **zero-pad** outside `[0, n - 1]`:

1. **Negative `p` (`p < 0`):**
   - For **`p <= -2.0`** (`i <= -2`), all four tap indices `i - 1, i, i + 1, i + 2 <= -1 < 0`, so `y0 = y1 = y2 = y3 = 0.0f` and the return value is identically **`0.0f`**.
   - At exact integer **`p = -1.0`** (`i = -1, t = 0.0f`), the cubic polynomial evaluates to `y1 = at(-1) = 0.0f` (even though `y2 = d[0]` and `y1` is returned directly because `t = 0`).
   - For fractional **`p in (-2.0, -1.0)`** (`i = -2, t in (0, 1)`), `y0 = y1 = y2 = 0.0f` and `y3 = d[0]`, returning `-0.5f * d[0] * t^2 * (1.0f - t)`.
   - For fractional **`p in (-1.0, 0.0)`** (`i = -1, t in (0, 1)`), `y0 = y1 = 0.0f`, `y2 = d[0]`, `y3 = d[1]`, returning `(((-1.5f * d[0] + 0.5f * d[1]) * t + (2.0f * d[0] - 0.5f * d[1])) * t + 0.5f * d[0]) * t`.
   -Note also: even at `p in [0.0, 1.0)` (`i = 0`), tap `y0 = at(-1) = 0.0f` is zero-padded.

2. **Positions beyond the last frame (`p > n - 1`):**
   - At `p in [n - 2.0, n - 1.0)` (`i = n - 2`), tap `y3 = at(n) = 0.0f` is zero-padded.
   - At exact integer `p = n - 1.0` (`i = n - 1, t = 0.0f`), the return value is `y1 = d[n - 1]`.
   - For fractional **`p in (n - 1.0, n)`** (`i = n - 1, t in (0, 1)`), `y0 = d[n - 2]`, `y1 = d[n - 1]`, `y2 = y3 = 0.0f`, smoothly decaying from `d[n - 1]` toward `0.0f`.
   - At exact integer **`p = n`** (`i = n, t = 0.0f`), `y1 = at(n) = 0.0f`, returning **`0.0f`**.
   - For fractional **`p in (n, n + 1.0)`** (`i = n, t in (0, 1)`), `y0 = d[n - 1]`, `y1 = y2 = y3 = 0.0f`, returning `((-0.5f * d[n - 1] * t + d[n - 1]) * t - 0.5f * d[n - 1]) * t`.
   - For **`p >= n + 1.0`** (`i >= n + 1`), all four taps `i - 1 >= n` are out of bounds (`0.0f`), returning identically **`0.0f`**.

3. **Are the two readers identical in this respect?**
   - **Yes**, for all `p` within 32-bit signed integer range (`-2147483648.0 < p < 2147483648.0`) when `rateMod == nullptr`: `int i = static_cast<int>(pos); if (static_cast<double>(i) > pos) --i;` computes the exact same `floor(pos)` as `static_cast<int64_t>(std::floor(pos))`, and the `else` branch in `hermiteFast` (`granular.cpp:125-126`) uses the identical `at(k)` zero-padding lambda and polynomial evaluation as `hermite` (`granular.cpp:85-88`). (The 32-bit `int` range is enforced by `GranularVoice::process` at `granular.cpp:842`, which routes `sample_->numFrames > (int64_t{1} << 29)` to `renderGrainsReference`.)

---

## Q2 — Forward Loop Crossfade on `develop` (`0.0..0.5` vs. `0.25..0.75`)

### Code on `develop` (`core/granular/src/granular.cpp:439-443, 471-490` and `605-609, 725-744`)

```cpp
// core/granular/src/granular.cpp:439-443 (identical at 605-609)
const double fileFrames = static_cast<double> (sample_->numFrames);
const double lg0 = std::min (p.loopLo, p.loopHi) * fileFrames, lg1 = std::max (p.loopLo, p.loopHi) * fileFrames;
const double loopLen = lg1 - lg0;
const bool wrap = p.loopGrains && loopLen > 8.0;
const double xfade = wrap ? std::min (0.5, static_cast<double> (clamp01 (p.loopCrossfade))) * loopLen : 0.0;
```

```cpp
// core/granular/src/granular.cpp:725-744 (identical in renderGrainsReference at 471-490)
if (wrap)
{
    while (g.pos >= lg1) g.pos -= loopLen;
    while (g.pos < lg0) g.pos += loopLen;
}
float sl, sr;
read.read (g.pos, sl, sr);
if (wrap && xfade > 1.0)
{
    const double toEnd = lg1 - g.pos;
    if (toEnd < xfade && step > 0.0)
    {
        const float t = static_cast<float> (1.0 - toEnd / xfade);
        float al, ar;
        read.read (g.pos - loopLen, al, ar);
        const float a = std::cos (t * 1.5707963f), b = std::sin (t * 1.5707963f);
        sl = sl * a + al * b;
        sr = sr * a + ar * b;
    }
}
```

### Shared Parameters
- 2 s sample at `44.1 kHz`: `fileFrames = N = 88200`, `rate = 1.0` (`mip = 0`), `step = +1.0` (unit pitch forward).
- `loopCrossfade = 0.1`, `loopLen = 0.5 * 88200 = 44100`, `xfade = 0.1 * 44100 = 4410.0`.
- Let $d[k]$ denote the sample value at frame $k$ (or $\operatorname{hermite}(d, N, p)$ for non-integer $p$).

### Case 1: Loop `0.0..0.5` (`lg0 = 0.0`, `lg1 = 44100.0`, no pre-roll)
- Crossfade region is `g.pos in (39690.0, 44100.0)` where `toEnd = 44100.0 - g.pos in (0.0, 4410.0)`.
- The partner read position is:
  $$p_{\text{partner}} = g.\text{pos} - \text{loopLen} = g.\text{pos} - 44100.0 = -\text{toEnd} \in (-4410.0, 0.0)$$
- For all `g.pos <= 44098.0` (`p_partner <= -2.0`) and at `g.pos = 44099.0` (`p_partner = -1.0`, `t_hermite = 0`), Q1 proves `read(p_partner) = 0.0f`.
- **Just before wrap** (last integer step before wrap, `g.pos = 44099.0`, `toEnd = 1.0`, `t = 1 - 1/4410 = 4409/4410 = 0.99977324f`):
  $$s_{\text{before}} = d[44099] \cdot \cos\!\left(\frac{4409}{4410}\cdot \frac{\pi}{2}\right) + \underbrace{\operatorname{hermite}(d, 88200, -1.0)}_{=\,0.0\text{f}} \cdot \sin\!\left(\frac{4409}{4410}\cdot \frac{\pi}{2}\right) = 0.00035623\,\cdot d[44099] \approx 0$$
- **Just after wrap** (next sample: `g.pos` advances to `44100.0`, wraps via `g.pos -= 44100.0` to `g.pos = 0.0`, where `toEnd = 44100.0 >= xfade = 4410.0`, so the crossfade `if` is **not** entered):
  $$s_{\text{after}} = d[0]$$
- **Continuity for arbitrary material:** **NOT continuous** (at unit integer step, the output fades to `~0` over 4410 samples and then jumps by $\Delta = d[0] - 0.00035623\,d[44099] \approx d[0]$ at the wrap; even for an infinitesimal fractional step $\epsilon \to 0^+$, although $\lim_{\epsilon \to 0^+} \operatorname{hermite}(d, N, -\epsilon) = d[0]$ makes the limit across $g.\text{pos} = 44100^-$ to $0^+$ approach $d[0]$, the partner signal across $g.\text{pos} \in [39690, 44098]$ is identically $0.0\text{f}$ and then abruptly turns on inside $g.\text{pos} \in (44098, 44100)$ over just 2 frames!).

### Case 2: Loop `0.25..0.75` (`lg0 = 22050.0`, `lg1 = 66150.0`, pre-roll `[17640, 22050)` exists)
- Crossfade region is `g.pos in (61740.0, 66150.0)` where `toEnd = 66150.0 - g.pos in (0.0, 4410.0)`.
- The partner read position is:
  $$p_{\text{partner}} = g.\text{pos} - 44100.0 = 22050.0 - \text{toEnd} \in (17640.0, 22050.0)$$
  Since $17640 \ge 1$ and $22050 + 2 < 88200$, all 4 cubic Hermite taps of the partner read lie strictly inside valid pre-roll sample data `[17639, 22051]`.
- **Just before wrap** (`g.pos = 66149.0`, `toEnd = 1.0`, `t = 4409/4410`):
  $$s_{\text{before}} = d[66149] \cdot \cos\!\left(\frac{4409}{4410}\cdot \frac{\pi}{2}\right) + d[22049] \cdot \sin\!\left(\frac{4409}{4410}\cdot \frac{\pi}{2}\right) = 0.00035623\,d[66149] + 0.99999994\,d[22049] \approx d[22049]$$
  (More generally, for step $\Delta p > 0$ at $g.\text{pos} = \text{lg1} - \epsilon$, $s_{\text{before}}(\epsilon) = d(\text{lg1} - \epsilon)\cos(\frac{\pi}{2}(1 - \frac{\epsilon}{\text{xfade}})) + d(\text{lg0} - \epsilon)\sin(\frac{\pi}{2}(1 - \frac{\epsilon}{\text{xfade}})) \to d(\text{lg0}^-)$ as $\epsilon \to 0^+$.)
- **Just after wrap** (`g.pos` advances to `66150.0`, wraps to `g.pos = 22050.0`, `toEnd = 44100.0 >= 4410.0`):
  $$s_{\text{after}} = d[22050]$$
- **Continuity for arbitrary material:** **Continuous** across the entire crossfade and across the wrap boundary: the jump at the wrap is $|s_{\text{after}} - s_{\text{before}}| = |d[22050] - (0.99999994\,d[22049] + 0.00035623\,d[66149])| \approx |d[22050] - d[22049]|$, which is just the natural sample-to-sample step of the waveform across frame `22049 -> 22050`.

### Verdict on the Audit's "Reads Silence" Claim
- The audit's claim is **PARTLY TRUE**:
  - **True** whenever `lg0 < xfade` (and completely true for the default `loopLo = 0.0f`, where `lg0 = 0` so `g.pos - loopLen = -toEnd <= -1` at every integer step in `[lg1 - xfade, lg1)`, reading `0.0f` silence across the crossfade and causing a full-scale step to `d[0]` at the wrap).
  - **False** whenever `lg0 >= xfade + 1.0`, where `g.pos - loopLen = lg0 - toEnd >= 1.0` reads the valid pre-roll `[lg0 - xfade, lg0)` immediately preceding `lg0` and produces a $C^0$-continuous equal-power crossfade into `d[lg0]`.

---

## Q3 — Reverse Grains (`step < 0`) on `develop`

### Code on `develop` (`core/granular/src/granular.cpp:471-500` and `725-755`)

```cpp
// core/granular/src/granular.cpp:471-490 (identical at 725-744)
if (wrap)
{
    while (g.pos >= lg1) g.pos -= loopLen;
    while (g.pos < lg0) g.pos += loopLen;
}
float sl, sr;
readSample (*sample_, g.pos, rate, sl, sr);
if (wrap && xfade > 1.0)
{
    const double toEnd = lg1 - g.pos;
    if (toEnd < xfade && step > 0.0)
    {
        // ... ONLY runs when step > 0.0 ...
    }
}
```

### What happens at the loop start (`lg0`) for reverse grains (`step < 0`)
1. **Is there any crossfade?**
   - **No.** Line `481` (`renderGrainsReference`) and line `735` (`renderGrains`) gate the crossfade behind `if (toEnd < xfade && step > 0.0)`. There is no `step < 0.0` branch anywhere on `develop` (and even when a reverse grain is near `lg1` with `toEnd < xfade`, `step > 0.0` is false so nothing runs there either).
2. **Where does the position go?**
   - While `g.pos >= lg0`, the grain steps backward by `g.pos += step` (`step < 0`).
   - On the last sample before the wrap (at unit reverse pitch `step = -1.0` starting from an integer frame), `g.pos = lg0`, reading `d[lg0]`.
   - At the end of that sample (`granular.cpp:500, 754`), `g.pos += step` moves `g.pos` to `lg0 + step < lg0` (e.g., `lg0 - 1.0`).
   - At the top of the next sample (`granular.cpp:474, 728`), `while (g.pos < lg0) g.pos += loopLen;` wraps `g.pos` to:
     $$g.\text{pos}_{\text{wrapped}} = g.\text{pos} + (\text{lg1} - \text{lg0}) = \text{lg1} - (\text{lg0} - g.\text{pos}) \in [\text{lg1} + \text{step},\, \text{lg1})$$
     At `step = -1.0` from `g.pos = lg0`, the position jumps from `lg0` to `lg1 - 1.0`.
3. **Is the output continuous?**
   - **No.** Just before the wrap (`g.pos = lg0`), the output is $s_{\text{before}} = d[\text{lg0}]$. Just after the wrap (`g.pos = lg1 - 1`), the output is $s_{\text{after}} = d[\text{lg1} - 1]$. For arbitrary material where $d[\text{lg1} - 1] \ne d[\text{lg0}]$, this produces a hard discontinuity (click) of magnitude $|d[\text{lg1} - 1] - d[\text{lg0}]|$ on every loop wrap.

---

## Q4 — Exact Source of the ~15x Jump in the FIRST Test (440.25 Hz Cosine)

### Parameters of the First Test (`cd5d184a`, `core/granular/tests/test_granular.cpp:93-156`)
- `sampleRate = 44100.0`, `hz = 440.25`, `totalFrames = 88200`:
  $$d[k] = 0.5 \cos\!\left(2\pi \cdot \frac{440.25\,k}{44100.0}\right) = 0.5 \cos\!\left(2\pi \cdot \frac{1761\,k}{176400}\right)$$
- **Maximum step of the cosine sample (`maxSineJump`):**
  The angular step per frame is $\Delta\theta = 2\pi \cdot \frac{440.25}{44100} = 0.0627249435\text{ rad}$, so:
  $$\text{maxSineJump} = \max_{k} |d[k] - d[k-1]| \approx 2 \cdot 0.5 \sin\!\left(\frac{\Delta\theta}{2}\right) = \sin(0.03136247\text{ rad}) = 0.03135733\text{f}$$
- **Grain & voice setup:**
  - `loopLo = 0.0f` (`lg0 = 0`), `loopHi = 0.5f` (`lg1 = 44100`, `loopLen = 44100`), `loopGrains = true`, `loopCrossfade = 0.1f` (`xfade = 4410.0`).
  - `densityHz = 1.0f` (`period = 44100.0`), `maxGrains = 1`, `lengthSec = 1.0f` (`g.length = 44100`), `gain = 1.0f`, `windowAmount = 0.0f`, `manual = true`, `scanSpeed = 0.0f`.
  - In `noteOn(..., jumpStart = true)` (`granular.cpp:269-277`): `want = min(1, ceil(1.0 * 44100 / 44100)) = 1`, spawning 1 grain at `delay = 0, age = 0`, and setting `spawnTimer_ = 44100.0 * 1.0 = 44100.0`.
  - In `spawn()` (`granular.cpp:316-320`): `overlap = max(1.0, 1.0 * 1.0) = 1.0`, `norm = 1.0f`, `pan = 0.0f`, `angle = 0.78539816f` ($\pi/4$), so:
    $$g.\text{gl} = g.\text{gr} = \cos(\pi/4) \cdot 1.41421356\text{f} \cdot 1.0\text{f} \cdot 1.0\text{f} = 1.0\text{f}$$
  - Only `8192` frames are rendered (`i = 0..8191`), and the test loop checks `for (size_t i = 100; i + 100 < 8192; ++i)` (`i = 100..8091`).
  - **Window value across `i = 100..8091`:** Since `g.length = 44100`, `x = age / 44100.0`. The `1/64` declick edge ramp (`granular.cpp:190, 526`: `ramp = min(1.0f, min(64.0f * x, 64.0f * (1.0f - x)))`) reaches `1.0f` at `age = ceil(44100 / 64) = 690` (`i = 690`) and stays `1.0f` until `age = 44100 - 690 = 43410` (well past `8192`). Even during `i = 100..689`, `ramp` changes by only `64 / 44100 = 0.001451` per sample (`< 0.05 * maxSineJump`). And no new grain ever spawns during `0..8191` because `g.length = 44100` and `spawnTimer_ = 44100`.
  - Therefore, **neither grain start, grain end, rectangular window ramp, nor a new grain spawn causes the jump; in both directions, the ~15x jump comes from the loop wrap itself.**

### 1. Forward Case (`reverseGrains = false`, `position = 0.45f`)
- At `i = 0` (`age = 0`), `g.pos = 0.45 * 88200 = 39690.0` (which is `lg1 - xfade = 44100 - 4410 = 39690.0`), and `step = +1.0`.
- At output sample **`i = 4409`** (`age = 4409`, `w = 1.0f`):
  - `g.pos = 39690.0 + 4409.0 = 44099.0` (last frame before `lg1 = 44100.0`).
  - `toEnd = 44100.0 - 44099.0 = 1.0 < 4410.0`.
  - `t = float(1.0 - 1.0 / 4410.0) = 4409.0f / 4410.0f = 0.99977324f`.
  - Primary read: `sl = d[44099]`. What is phase at `k = 44099`?
    $$\frac{440.25 \cdot 44099}{44100} = 440.25 - \frac{440.25}{44100} = 440.24001701\text{ cycles} \equiv +0.24001701\text{ cycles}$$
    $$d[44099] = 0.5 \cos(2\pi \cdot 0.24001701) = +0.0313421\text{f}$$
  - Partner read at `g.pos - loopLen = 44099.0 - 44100.0 = -1.0`: as shown in Q1, `hermiteFast(d, 88200, -1.0) = 0.0f` (`al = 0.0f`).
  - Crossfaded value at **`i = 4409`**:
    $$\text{outFwd}[4409] = d[44099] \cdot \cos(0.99977324 \cdot 1.5707963\text{f}) + 0.0\text{f} = 0.0313421\text{f} \cdot 0.00035623\text{f} = +0.00001116\text{f}$$
- At output sample **`i = 4410`** (`age = 4410`, `w = 1.0f`):
  - `g.pos` enters the loop at `44100.0`, wraps via `while (g.pos >= lg1) g.pos -= loopLen;` to **`g.pos = 0.0`**.
  - `toEnd = 44100.0 - 0.0 = 44100.0 >= xfade`, so no crossfade is applied.
  - Output at **`i = 4410`**:
    $$\text{outFwd}[4410] = d[0] = 0.5 \cos(0) = +0.50000000\text{f}$$
- **Exact jump at `i = 4410` (between `outFwd[4409]` and `outFwd[4410]`):**
  $$|\text{outFwd}[4410] - \text{outFwd}[4409]| = |0.50000000\text{f} - 0.00001116\text{f}| = 0.49998884\text{f} = \mathbf{15.945 \times \text{maxSineJump}}$$

### 2. Reverse Case (`reverseGrains = true`, `position = 0.05f`)
- At `i = 0` (`age = 0`), `g.pos = 0.05 * 88200 = 4410.0`, and `step = -1.0`.
- At output sample **`i = 4410`** (`age = 4410`, `w = 1.0f`):
  - `g.pos = 4410.0 - 4410.0 = 0.0` (`== lg0`). No wrap and no crossfade (`step < 0`).
  - Output at **`i = 4410`**:
    $$\text{outRev}[4410] = d[0] = 0.5 \cos(0) = +0.50000000\text{f}$$
- At output sample **`i = 4411`** (`age = 4411`, `w = 1.0f`):
  - At the end of sample `4410`, `g.pos` stepped to `-1.0 < lg0 = 0.0`.
  - At the start of sample `4411`, `while (g.pos < lg0) g.pos += loopLen;` wraps `g.pos` to `-1.0 + 44100.0 = 44099.0`.
  - No crossfade runs because `step < 0.0`.
  - Because `hz = 440.25 Hz` completes $440.25 \times 1.0\text{ s} = 440.25$ cycles (a **quarter-cycle** offset!) across the `44100`-frame loop `[0, 44100)`, $d[44099] = 0.5 \cos(2\pi \cdot 0.24001701) = +0.0313421\text{f}$.
  - Output at **`i = 4411`**:
    $$\text{outRev}[4411] = d[44099] = +0.0313421\text{f}$$
- **Exact jump at `i = 4411` (between `outRev[4410]` and `outRev[4411]`):**
  $$|\text{outRev}[4411] - \text{outRev}[4410]| = |0.0313421\text{f} - 0.50000000\text{f}| = 0.4686579\text{f} = \mathbf{14.946 \times \text{maxSineJump}}$$

---

## Q5 — Why the SECOND Test (Chirp, `9a7bd43f`) Passes on Unfixed `develop`

### Does the grain cross the loop boundary in the 6 cases?
**YES — in every one of the 6 cases, the single grain crosses the loop boundary TWICE during the 60,000 rendered frames.** Here is the exact trajectory from the test's parameters (`core/granular/tests/test_granular.cpp:175-305` in commit `9a7bd43f`):
- Sample: `sampleRate = 44100.0`, `totalFrames = N = 88200` (2.0 s).
- Parameters: `loopGrains = true`, `loopCrossfade = 0.1f`, `densityHz = 1.0f`, `maxGrains = 1`, `lengthSec = 2.0f`, `gain = 1.0f`, `windowAmount = 0.0f`, `manual = true`, `scanSpeed = 0.0f`, `regionLo = 0.0f` (default), `regionHi = 1.0f` (default).
- How `position` maps to a start frame:
  1. In `noteOn` (`granular.cpp:259, 266-267`): `lo = 0.0f, hi = 1.0f`, and `if (p.manual) head_ = lo + (hi - lo) * clamp01(p.position) = p.position`.
  2. With `jumpStart = true`, `period = 44100.0 / 1.0 = 44100.0`, `want = max(1, min(min(1, 48), ceil(2.0 * 44100 / 44100))) = 1`, so `spawn(p, 0, 0, head_)` spawns **one** grain with `g.length = int(2.0 * 44100) = 88200`, `g.age = 0`, `g.delay = 0`, `fraction = head_ = p.position`, `g.inc = +1.0` (forward) or `-1.0` (reverse), and:
     $$g.\text{pos}(0) = p.\text{position} \times 88200.0$$
  3. In `noteOn` (`granular.cpp:277`), `spawnTimer_ = period * 1.0 = 44100.0`. When `spawnTimer_` reaches `0` at sample `44099`, the grain is still active (`g.age = 44099 < 88200`), so `spawn()` sees `running = 1 >= limit = 1` (`granular.cpp:295`) and **skips the spawn**. Thus the single grain plays uninterrupted across all `60000` rendered frames (`age = 0..59999`).
- Across all three cases A (`0.0..0.5`), B (`0.25..0.75`), and C (`0.5..1.0`):
  - `loopLen = 0.5 * 88200 = 44100.0` frames, `xfade = 0.1 * 44100 = 4410.0` frames.
  - **Forward (`p.position = tc.loopHi - 0.05f`):**
    $$g.\text{pos}(0) = (\text{loopHi} - 0.05) \times 88200 = \text{lg1} - 4410.0 = \text{lg1} - \text{xfade}$$
    With `step = +1.0`, `g.pos` reaches `lg1 - 1` at **`i = 4409`** and wraps to `lg0` at **`i = 4410`** (crossing #1), then traverses the 44,100-frame loop and wraps from `lg1 - 1` at **`i = 48509`** to `lg0` at **`i = 48510`** (crossing #2), ending at `g.pos = lg0 + 11489` at `i = 59999`.
  - **Reverse (`p.position = tc.loopLo + 0.05f`):**
    $$g.\text{pos}(0) = (\text{loopLo} + 0.05) \times 88200 = \text{lg0} + 4410.0 = \text{lg0} + \text{xfade}$$
    With `step = -1.0`, `g.pos` reaches `lg0` at **`i = 4410`** and wraps to `lg1 - 1` at **`i = 4411`** (crossing #1), then traverses the loop backward and wraps from `lg0` at **`i = 48510`** to `lg1 - 1` at **`i = 48511`** (crossing #2), ending at `g.pos = lg1 - 11490` at `i = 59999`.

| Case | `loopLo..loopHi` | `lg0..lg1` (frames) | Forward `g.pos(0)` | Forward crossings (output index `i`) | Reverse `g.pos(0)` | Reverse crossings (output index `i`) |
|---|---|---|---|---|---|---|
| **A** | `0.00 .. 0.50` | `0 .. 44100` | `39690` | `4409 -> 4410`, `48509 -> 48510` | `4410` | `4410 -> 4411`, `48510 -> 48511` |
| **B** | `0.25 .. 0.75` | `22050 .. 66150` | `61740` | `4409 -> 4410`, `48509 -> 48510` | `26460` | `4410 -> 4411`, `48510 -> 48511` |
| **C** | `0.50 .. 1.00` | `44100 .. 88200` | `83790` | `4409 -> 4410`, `48509 -> 48510` | `48510` | `4410 -> 4411`, `48510 -> 48511` |

### Why all 6 cases still pass on unfixed `develop` (and why the ratios are `0.375x / 0.404x`, `0.581x / 0.555x`, `0.786x / 0.707x`)

Three arithmetic accidents in commit `9a7bd43f` conspired to hide the wrap discontinuity completely:

1. **Every single loop boundary (`0.0, 0.25, 0.5, 0.75, 1.0`) falls on an exact integer cycle of the chirp where `sampleData == 0.0f` with positive slope:**
   Commit `9a7bd43f` defines the chirp (`test_granular.cpp:186-188`) for $t = i / 44100.0 \in [0, 2]$ as:
   $$\phi(t) = 2\pi \left(200.0\,t + 0.5 \cdot 600.0\,t^2\right) = 2\pi \underbrace{\left(200\,t + 300\,t^2\right)}_{N_{\text{cycles}}(t)}, \qquad d[i] = 0.5 \sin(\phi(t))$$
   Evaluating the cycle count $N_{\text{cycles}}(t) = 200\,t + 300\,t^2$ at the five loop marker times $t \in \{0.0, 0.5, 1.0, 1.5, 2.0\}\text{ s}$:
   - $t = 0.0\text{ s}$ (`i = 0`, `loopLo = 0.0`): $N_{\text{cycles}}(0.0) = 0 + 0 = \mathbf{0.0\text{ cycles}} \implies d[0] = 0.5 \sin(0) = \mathbf{0.0\text{f}}$
   - $t = 0.5\text{ s}$ (`i = 22050`, `0.25`): $N_{\text{cycles}}(0.5) = 100 + 75 = \mathbf{175.0\text{ cycles}} \implies d[22050] = 0.5 \sin(2\pi \cdot 175) = \mathbf{0.0\text{f}}$
   - $t = 1.0\text{ s}$ (`i = 44100`, `0.50`): $N_{\text{cycles}}(1.0) = 200 + 300 = \mathbf{500.0\text{ cycles}} \implies d[44100] = 0.5 \sin(2\pi \cdot 500) = \mathbf{0.0\text{f}}$
   - $t = 1.5\text{ s}$ (`i = 66150`, `0.75`): $N_{\text{cycles}}(1.5) = 300 + 675 = \mathbf{975.0\text{ cycles}} \implies d[66150] = 0.5 \sin(2\pi \cdot 975) = \mathbf{0.0\text{f}}$
   - $t = 2.0\text{ s}$ (`i = 88200`, `1.00`): $N_{\text{cycles}}(2.0) = 400 + 1200 = \mathbf{1600.0\text{ cycles}} \implies d(88200) = 0.5 \sin(2\pi \cdot 1600) = \mathbf{0.0\text{f}}$

   Because $d[\text{lg0}] = 0.0\text{f}$ and $d(\text{lg1}) = 0.0\text{f}$ at **all** tested markers:
   - In **Case A Forward (`0.0..0.5`)**, `develop` fades the loop end `d[44099]` down to `0.0f` (because the partner read at `g.pos - loopLen < 0` returns `0.0f`) and then jumps at `i = 4410` to `d[0]`. Because **`d[0] == 0.0f`**, jumping from faded-out silence (`~0.0f`) to `d[0] = 0.0f` produces **zero jump** (`|0.0f - 0.0f| = 0`)!
   - In **Cases A, B, C Reverse**, `develop` has no reverse crossfade and jumps at `i = 4411` (and `48511`) from `d[lg0]` to `d[lg1 - 1]`. Because **`d[lg0] == 0.0f`** and **`d(lg1) == 0.0f`**, jumping from `d[lg0] = 0.0f` to `d[lg1 - 1]` has magnitude $|d[\text{lg1} - 1] - 0.0\text{f}| = |d[\text{lg1} - 1] - d(\text{lg1})|$, which equals **one normal sample-to-sample step of the chirp at `lg1`**!

2. **`lengthSec = 2.0f` with `densityHz = 1.0f` scales all grain output by `1 / sqrt(2) = 0.7071f`:**
   In `spawn()` (`granular.cpp:316-320`), `overlap = max(1.0, lengthSec * densityHz) = max(1.0, 2.0 * 1.0) = 2.0`, so `norm = 1.0f / sqrt(2.0) = 0.70710678f`, giving `g.gl = g.gr = 0.70710678f`. Meanwhile, `maxChirpJump` is measured on the unattenuated `sampleData` array (`gain = 1.0`).

3. **`maxChirpJump` is dominated by the 1400 Hz end of the chirp (`t = 2.0 s`), whereas Cases A and B only reach 800 Hz and 1100 Hz:**
   The instantaneous chirp frequency is $f(t) = 200 + 600\,t\text{ Hz}$, so the maximum sample-to-sample step on `[t_0, t_1]` is $\Delta_{\max}(t_1) = 2 \cdot 0.5 \sin(\pi f(t_1) / 44100) \approx \pi f(t_1) / 44100$, with global $\text{maxChirpJump} = \sin(\pi \cdot 1400 / 44100) = 0.099565\text{f}$ at $t = 2.0\text{ s}$:
   - **Reverse ratios on `develop`:** Since there is no crossfade in reverse and the wrap jump at `lg0 -> lg1 - 1` is just one step at $f(\text{loopHi})$, the max jump in the reverse render is simply the scaled max chirp step inside `[loopLo, loopHi]`:
     $$\frac{\text{maxJumpRev}}{\text{maxChirpJump}} = \frac{1}{\sqrt{2}} \cdot \frac{\sin(\pi f(\text{loopHi}) / 44100)}{\sin(\pi \cdot 1400 / 44100)} = \begin{cases} 0.7071 \times \frac{800}{1400} = \mathbf{0.404\text{x}} & \text{Case A } (f = 800\text{ Hz}) \\ 0.7071 \times \frac{1100}{1400} = \mathbf{0.555\text{x}} & \text{Case B } (f = 1100\text{ Hz}) \\ 0.7071 \times \frac{1400}{1400} = \mathbf{0.707\text{x}} & \text{Case C } (f = 1400\text{ Hz}) \end{cases}$$
   - **Forward ratios on `develop`:**
     - In **Case A Forward**, the crossfade attenuates the `[740 Hz, 800 Hz]` tail (`g.pos in [39690, 44100)`) by $\cos(t \pi / 2)$, so the peak step occurs just before the crossfade at $g.\text{pos} = 39690$ ($t = 0.9\text{ s}, f = 740\text{ Hz}$): $0.7071 \times \frac{740}{1400} = \mathbf{0.374\text{x}}$ (`0.375x` measured).
     - In **Cases B and C Forward**, pre-roll exists (`lg0 >= 22050 > 4410`), so the equal-power weights $a = \cos(t\pi/2)$ and $b = \sin(t\pi/2)$ sum two chirp segments of different frequencies; whenever their slopes align in sign, the step reaches slightly above $0.7071 \times f(\text{loopHi})/1400$, yielding **`0.581x`** in Case B and **`0.786x`** in Case C—all well below the test's `1.25x` threshold.

---

## Q6 — Precise Statement of Defects on `develop` & Verification of Fix Attempt 2 (`25a37f8c`)

### Part 1: Precise Statement of What Is Defective on `develop`

| # | Defect on `develop` (`core/granular/src/granular.cpp:478-490, 732-744`) | Exact Trigger Condition | Effect on Output | Audible? / How Large? |
|---|---|---|---|---|
| **D1** | **Forward loop crossfade reads negative sample indices (`g.pos - loopLen < 0`)** | `p.loopGrains && loopLen > 8.0 && xfade > 1.0 && step > 0.0` with **`lg0 < xfade`** (including the default `loopLo = 0.0f`, where `lg0 = 0`) | For the portion of the crossfade where `toEnd > lg0` (`g.pos in [lg1 - xfade, lg1 - lg0)`), `g.pos - loopLen < 0`. When `lg0 = 0`, the partner read is `0.0f` across `[lg1 - xfade, lg1 - 1]`: the grain fades out by $\cos(t\pi/2) \to 0$ over `xfade` frames and then jumps abruptly to $d[\text{lg0}]$ at the wrap (plus a 2-frame cubic spline overshoot across $p \in (-2, 0)$ for fractional steps). When $1 \le \text{lg0} < \text{xfade}$, the first $\text{xfade} - \text{lg0}$ frames of the crossfade read `0.0f` before hitting $d[0]$ at $g.\text{pos} = \text{lg1} - \text{lg0}$. | **Yes — highly audible:** produces a periodic volume drop (up to `xfade` frames long, up to 50% of the loop) followed by a full-scale click of up to $\|d[\text{lg0}]\| \le 1.0$ (or up to $15.95\times$ the step of a 440 Hz sine) on every loop wrap whenever $d[\text{lg0}] \ne 0$. |
| **D2** | **Reverse looped grains have no loop crossfade at all** | `p.loopGrains && loopLen > 8.0 && xfade > 1.0 && step < 0.0` (`p.reverseGrains` or `p.randDir > 0`, or negative `rateMod`) | The condition `if (toEnd < xfade && step > 0.0)` never triggers for `step < 0.0`. Every reverse grain steps unattenuated to `lg0` and wraps directly to `lg1 - |step|` with zero crossfading. | **Yes — highly audible:** produces a hard click of magnitude $\|d(\text{lg1} - \|\text{step}\|) - d(\text{lg0})\|$ (up to $2.0$ peak-to-peak on normalized audio) on every reverse loop wrap. |

### Part 2: Verification of Fix Attempt 2 (`agy/fix-batch-1-build`, commit `25a37f8c`)

Let's examine the mode selection and per-sample crossfade code in `git show agy/fix-batch-1-build:core/granular/src/granular.cpp` (`lines 447-496, 531-600` in `renderGrainsReference` and identical `lines 708-757, 881-950` in `renderGrains`):

```cpp
// agy/fix-batch-1-build:core/granular/src/granular.cpp:447-496 (and 708-757)
LoopXFade fwdMode = LoopXFade::none;
double fwdXf = 0.0;
LoopXFade revMode = LoopXFade::none;
double revXf = 0.0;
if (wrap && xfade > 1.0)
{
    // Forward (step > 0): F1 pre-roll available, F2 post-roll available, F3 fallback
    if (lg0 - xfade >= 0.0)
    {
        fwdMode = LoopXFade::beforeWrap;
        fwdXf = xfade;
    }
    else if (lg1 + xfade <= fileFrames - 1.0)
    {
        fwdMode = LoopXFade::afterWrap;
        fwdXf = xfade;
    }
    else if (lg0 >= 1.0)
    {
        fwdMode = LoopXFade::beforeWrap;
        fwdXf = std::min (xfade, lg0);
    }
    else if (fileFrames - 1.0 - lg1 >= 1.0)
    {
        fwdMode = LoopXFade::afterWrap;
        fwdXf = std::min (xfade, fileFrames - 1.0 - lg1);
    }

    // Reverse (step < 0): R1 post-roll available, R2 pre-roll available, R3 fallback
    if (lg1 + xfade <= fileFrames - 1.0)
    {
        revMode = LoopXFade::beforeWrap;
        revXf = xfade;
    }
    else if (lg0 - xfade >= 0.0)
    {
        revMode = LoopXFade::afterWrap;
        revXf = xfade;
    }
    else if (fileFrames - 1.0 - lg1 >= 1.0)
    {
        revMode = LoopXFade::beforeWrap;
        revXf = std::min (xfade, fileFrames - 1.0 - lg1);
    }
    else if (lg0 >= 1.0)
    {
        revMode = LoopXFade::afterWrap;
        revXf = std::min (xfade, lg0);
    }
}
```

And the per-sample loop (`agy/fix-batch-1-build:core/granular/src/granular.cpp:531-600` and `881-950`):
```cpp
if (step > 0.0)
{
    if (fwdMode == LoopXFade::beforeWrap)
    {
        const double toEnd = lg1 - g.pos;
        if (toEnd < fwdXf)
        {
            const float t = static_cast<float> (1.0 - toEnd / fwdXf);
            float al, ar;
            read.read (g.pos - loopLen, al, ar);
            const float a = std::cos (t * 1.5707963f), b = std::sin (t * 1.5707963f);
            sl = sl * a + al * b;
            sr = sr * a + ar * b;
        }
    }
    else if (fwdMode == LoopXFade::afterWrap)
    {
        const double fromStart = g.pos - lg0;
        if (fromStart < fwdXf)
        {
            const float u = static_cast<float> (fromStart / fwdXf);
            float al, ar;
            read.read (g.pos + loopLen, al, ar);
            const float a = std::cos (u * 1.5707963f), b = std::sin (u * 1.5707963f);
            sl = al * a + sl * b;
            sr = ar * a + sr * b;
        }
    }
}
else if (step < 0.0)
{
    if (revMode == LoopXFade::beforeWrap)
    {
        const double toStart = g.pos - lg0;
        if (toStart < revXf)
        {
            const float t = static_cast<float> (1.0 - toStart / revXf);
            float al, ar;
            read.read (g.pos + loopLen, al, ar);
            const float a = std::cos (t * 1.5707963f), b = std::sin (t * 1.5707963f);
            sl = sl * a + al * b;
            sr = sr * a + ar * b;
        }
    }
    else if (revMode == LoopXFade::afterWrap)
    {
        const double toEnd = lg1 - g.pos;
        if (toEnd < revXf)
        {
            const float u = static_cast<float> (toEnd / revXf);
            float al, ar;
            read.read (g.pos - loopLen, al, ar);
            const float a = std::cos (u * 1.5707963f), b = std::sin (u * 1.5707963f);
            sl = al * a + sl * b;
            sr = ar * a + sr * b;
        }
    }
}
```

#### Case-by-Case Arithmetic & Continuity Check of Fix Attempt 2

1. **Case F1 (`step > 0`, pre-roll available: `lg0 - xfade >= 0.0` $\implies$ `fwdMode = beforeWrap, fwdXf = xfade`):**
   - **Active zone:** `g.pos in (lg1 - xfade, lg1)`, `toEnd = lg1 - g.pos in (0, xfade)`, `t = 1 - toEnd / xfade in (0, 1)`.
   - **Partner index:** `g.pos - loopLen = lg0 - toEnd in (lg0 - xfade, lg0) subset (0, lg0)`.
   - **Continuity:**
     - Entering crossfade at `g.pos = lg1 - xfade` (`toEnd = xfade, t = 0`): `a = cos(0) = 1, b = sin(0) = 0`, so `sl_out = d(lg1 - xfade)`, continuous with `g.pos <= lg1 - xfade`.
     - Crossing wrap (`g.pos = lg1 - eps` $\to$ `g.pos = lg0`): as $\epsilon \to 0^+$, $t \to 1^-$, $a \to 0, b \to 1$, so $s_{\text{before}}(\text{lg1} - \epsilon) = d(\text{lg1} - \epsilon)\cos(t\pi/2) + d(\text{lg0} - \epsilon)\sin(t\pi/2) \to d(\text{lg0}^-)$, and after wrapping to $g.\text{pos} = \text{lg0}$ (`toEnd = loopLen >= xfade`), $s_{\text{after}}(\text{lg0}) = d(\text{lg0})$.
   - **Bit-identity with `develop`:** Because `fwdXf == xfade` and the expressions for `toEnd`, `t`, `g.pos - loopLen`, `a`, `b`, and `sl * a + al * b` are identical to `develop`, **F1 is bit-identical to `develop`**.
   - **Minor Hermite tap note:** When $0 \le \text{lg0} - \text{xfade} < 1.0$, for $g.\text{pos} \in (\text{lg1} - \text{xfade}, \text{lg1} - \text{xfade} + 1)$, the partner position $p \in [0, 1)$ has `i = 0`, so cubic Hermite tap `y0 = at(-1) = 0.0f` is zero-padded (the other three taps `y1 = d[0], y2 = d[1], y3 = d[2]` are in bounds).

2. **Case F2 (`step > 0`, no full pre-roll, post-roll available: `lg0 - xfade < 0.0 && lg1 + xfade <= fileFrames - 1.0` $\implies$ `fwdMode = afterWrap, fwdXf = xfade`):**
   - **Active zone:** `g.pos in [lg0, lg0 + xfade)`, `fromStart = g.pos - lg0 in [0, xfade)`, `u = fromStart / xfade in [0, 1)`.
   - **Partner index:** `g.pos + loopLen = lg1 + fromStart in [lg1, lg1 + xfade) subset [lg1, fileFrames - 1.0)`.
   - **Continuity:**
     - Just before wrap (`g.pos = lg1 - eps`, where `fromStart = loopLen - eps >= xfade` since `xfade <= 0.5 * loopLen`): no crossfade is active, so $s_{\text{before}}(\text{lg1} - \epsilon) = d(\text{lg1} - \epsilon)$.
     - Just after wrap (`g.pos = lg0`, `fromStart = 0, u = 0`): `a = cos(0) = 1, b = sin(0) = 0`, partner reads `lg0 + loopLen = lg1`, so:
       $$s_{\text{after}}(\text{lg0}) = \text{al} \cdot 1 + \text{sl} \cdot 0 = d(\text{lg1})$$
       Thus the wrap transition from `g.pos = lg1 - step` to `g.pos = lg0` reads $d(\text{lg1} - \text{step}) \to d(\text{lg1})$, which is the uninterrupted continuation of the waveform past `lg1`!
     - Exiting crossfade (`g.pos -> (lg0 + xfade)^-`, `u -> 1^-`): $a \to 0, b \to 1$, so $s(\text{lg0} + \text{xfade} - \epsilon) \to d((\text{lg0} + \text{xfade})^-)$, matching $d(\text{lg0} + \text{xfade})$ outside the crossfade.
   - **Minor Hermite tap note:** At the upper limit `lg1 + xfade == fileFrames - 1.0`, for `p in [fileFrames - 2.0, fileFrames - 1.0)`, `i = fileFrames - 2`, so cubic Hermite tap `y3 = at(fileFrames) = 0.0f` is zero-padded (`lg1 + xfade <= fileFrames - 2.0` keeps all 4 taps in bounds).

3. **Case F3 (`step > 0`, fallback when NEITHER full pre-roll NOR full post-roll exists: `lg0 < xfade && lg1 + xfade > fileFrames - 1.0`):**
   - This occurs when the loop spans most of the file and `xfade` is larger than both margins `lg0` and `fileFrames - 1 - lg1`.
   - Fix attempt 2 checks:
     - `else if (lg0 >= 1.0)`: uses `beforeWrap` with **clamped crossfade length** `fwdXf = min(xfade, lg0) = lg0`.
     - `else if (fileFrames - 1.0 - lg1 >= 1.0)`: uses `afterWrap` with **clamped crossfade length** `fwdXf = min(xfade, fileFrames - 1.0 - lg1)`.
     - **Else (`lg0 < 1.0 && fileFrames - 1.0 - lg1 < 1.0`, i.e., full-file loop `loopLo = 0.0, loopHi = 1.0`):** `fwdMode` remains **`LoopXFade::none`**!
   - **Assessment of F3:**
     - When `lg0 >= 1.0` or `fileFrames - 1 - lg1 >= 1.0`, clamping `fwdXf` to the available margin keeps `g.pos - loopLen >= 0` (or `g.pos + loopLen <= fileFrames - 1`) and preserves continuity across the wrap, **but** if the margin is small (e.g. `lg0 = 2` frames while `xfade = 4410` frames), `fwdXf` shrinks to `2` frames (essentially an uncrossfaded 2-sample splice even if the other side had 4000 frames of post-roll, because `else if (lg0 >= 1.0)` takes precedence over `fileFrames - 1.0 - lg1 >= 1.0` without picking `max(lg0, fileFrames - 1.0 - lg1)`!).
     - Even more importantly: for the **default full-file loop (`loopLo = 0.0f, loopHi = 1.0f`)**, `lg0 = 0.0 < 1.0` and `fileFrames - 1.0 - lg1 = -1.0 < 1.0`, so **neither `if` matches and `fwdMode = LoopXFade::none` — no crossfade happens at all when looping the full sample `0.0..1.0`!**

4. **Case R1 (`step < 0`, post-roll available: `lg1 + xfade <= fileFrames - 1.0` $\implies$ `revMode = beforeWrap, revXf = xfade`):**
   - **Active zone:** `g.pos in [lg0, lg0 + xfade)`, `toStart = g.pos - lg0 in [0, xfade)`, `t = 1 - toStart / xfade in (0, 1]`.
   - **Partner index:** `g.pos + loopLen = lg1 + toStart in [lg1, lg1 + xfade) subset [lg1, fileFrames - 1.0)`.
   - **Continuity:**
     - Entering crossfade from above at `g.pos = (lg0 + xfade)^-` (`toStart -> xfade^-, t -> 0^+`): `a -> 1, b -> 0`, so output matches `d(lg0 + xfade)`.
     - At `g.pos = lg0` (`toStart = 0, t = 1.0f`): `a = cos(1.5707963f) = 0.0000000437f ≈ 0, b = sin(1.5707963f) = 1.0f`, partner reads `lg0 + loopLen = lg1`, so:
       $$s_{\text{before}}(\text{lg0}) = d(\text{lg0}) \cdot a + d(\text{lg1}) \cdot 1 = d(\text{lg1})$$
     - On the next reverse step (`g.pos` steps to `lg0 + step < lg0` and wraps to `lg1 + step`, e.g., `lg1 - 1`), `toStart = (lg1 - 1) - lg0 = loopLen - 1 >= xfade`, so no crossfade runs and:
       $$s_{\text{after}}(\text{lg1} - 1) = d(\text{lg1} - 1)$$
       Thus the reverse wrap from `g.pos = lg0` to `g.pos = lg1 - 1` transitions smoothly from $d(\text{lg1})$ to $d(\text{lg1} - 1)$!

5. **Case R2 (`step < 0`, no full post-roll, pre-roll available: `lg1 + xfade > fileFrames - 1.0 && lg0 - xfade >= 0.0` $\implies$ `revMode = afterWrap, revXf = xfade`):**
   - **Active zone:** `g.pos in (lg1 - xfade, lg1)`, `toEnd = lg1 - g.pos in (0, xfade)`, `u = toEnd / xfade in (0, 1)`.
   - **Partner index:** `g.pos - loopLen = lg0 - toEnd in (lg0 - xfade, lg0) subset (0, lg0)`.
   - **Continuity:**
     - Just before wrap (`g.pos = lg0`, where `toEnd = loopLen >= xfade`): no crossfade is active, so $s_{\text{before}}(\text{lg0}) = d(\text{lg0})$.
     - Just after wrap (`g.pos = lg1 - eps`, `toEnd = eps, u = eps / xfade -> 0^+`): $a = \cos(u\pi/2) \to 1, b = \sin(u\pi/2) \to 0$, partner reads `g.pos - loopLen = lg0 - eps`, so:
       $$s_{\text{after}}(\text{lg1} - \epsilon) = d(\text{lg0} - \epsilon) \cdot \cos(u\pi/2) + d(\text{lg1} - \epsilon) \cdot \sin(u\pi/2) \to d(\text{lg0}^-)$$
       Thus at unit reverse step (`g.pos = lg0` $\to$ `g.pos = lg1 - 1`), the output transitions from $d[\text{lg0}]$ to $\approx d[\text{lg0} - 1]$, continuing the pre-roll backward across `lg0` and fading into $d(\text{lg1} - \text{xfade})$ as $u \to 1^-$!

6. **Case R3 (`step < 0`, fallback when NEITHER full post-roll NOR full pre-roll exists: `lg1 + xfade > fileFrames - 1.0 && lg0 - xfade < 0.0`):**
   - Same two defects as F3:
     1. `else if (fileFrames - 1.0 - lg1 >= 1.0)` takes unconditional precedence over `else if (lg0 >= 1.0)` instead of picking whichever margin (`fileFrames - 1.0 - lg1` vs. `lg0`) is larger.
     2. When `lg0 < 1.0 && fileFrames - 1.0 - lg1 < 1.0` (e.g. default full-file loop `loopLo = 0.0f, loopHi = 1.0f`), `revMode` stays `LoopXFade::none` and no crossfade is performed at all.

---

## Q7 — Specification of a Decisive Test That Cannot Be Fooled

### 1. The Sample (Non-Periodic, Non-Zero and Non-Matching at All Loop Boundaries)
- **Length & Rate:** `sampleRate = 44100.0`, `totalFrames = N = 88200` (2.0 seconds).
- **Waveform formula:** A non-periodic frequency-modulated cosine plus a slow linear DC ramp:
  $$d[i] = 0.35\,\cos\!\left(2\pi\left(173.3\,t + 241.7\,t^2\right)\right) + 0.45\,\left(1.0 - 1.5\,t\right), \qquad t = \frac{i}{44100.0} \in [0, 2)$$
- **Why this sample cannot be fooled:**
  Let us compute $d[i]$ at the 5 boundary frames $i \in \{0, 22050, 44100, 66150, 88199\}$ ($t \in \{0.0, 0.5, 1.0, 1.5, 1.9999773\}\text{ s}$):
  - At $i = 0$ ($t = 0.0\text{ s}$): phase $= 0.000\text{ cycles} \implies d[0] = 0.35(1.0) + 0.45(1.0) = \mathbf{+0.80000\text{f}}$.
  - At $i = 22050$ ($t = 0.5\text{ s}$): phase $= 86.65 + 60.425 = 147.075\text{ cycles} \implies d[22050] = 0.35\cos(2\pi \cdot 0.075) + 0.45(0.25) = 0.31185 + 0.11250 = \mathbf{+0.42435\text{f}}$.
  - At $i = 44100$ ($t = 1.0\text{ s}$): phase $= 173.3 + 241.7 = 415.000\text{ cycles} \implies d[44100] = 0.35(1.0) + 0.45(-0.5) = \mathbf{+0.12500\text{f}}$.
  - At $i = 66150$ ($t = 1.5\text{ s}$): phase $= 259.95 + 543.825 = 803.775\text{ cycles} \implies d[66150] = 0.35\cos(2\pi \cdot 0.775) + 0.45(-1.25) = 0.05475 - 0.56250 = \mathbf{-0.50775\text{f}}$.
  - At $i = 88199$ ($t \approx 2.0\text{ s}$): phase $\approx 346.6 + 966.8 = 1313.400\text{ cycles} \implies d[88199] \approx 0.35\cos(2\pi \cdot 0.400) + 0.45(-2.0) = -0.28316 - 0.90000 = \mathbf{-1.18316\text{f}}$ (or divide $d[i]$ by $1.5$ to keep $|d[i]| \le 0.80\text{f}$; using $d[i] = 0.25\cos(2\pi(173.3\,t + 241.7\,t^2)) + 0.30(1.0 - 1.5\,t)$ gives $|d[i]| \le 0.85\text{f}$ and:
    - $d[0] = \mathbf{+0.55000\text{f}}$
    - $d[22050] = 0.25\cos(2\pi \cdot 0.075) + 0.075 = \mathbf{+0.29775\text{f}}$
    - $d[44100] = 0.25(1.0) - 0.15 = \mathbf{+0.10000\text{f}}$
    - $d[66150] = 0.25\cos(2\pi \cdot 0.775) - 0.375 = \mathbf{-0.33589\text{f}}$
    - $d[88199] \approx 0.25\cos(2\pi \cdot 0.400) - 0.600 = \mathbf{-0.80225\text{f}}$).
  Every boundary value is bounded away from zero ($|d| \ge 0.10\text{f}$), and every pair of loop endpoints $(d[\text{lg0}], d[\text{lg1}])$ differs by at least $\mathbf{0.436\text{f}}$ (`>= 12.7x` the sample's own maximum step).
  - **Maximum sample-to-sample step of $d[i]$ (`maxSampleStep`):**
    At $t = 2.0\text{ s}$, instantaneous frequency is $f_{\max} = 173.3 + 2(241.7)(2.0) = 1140.1\text{ Hz}$, so:
    $$\text{maxSampleStep} = \max_{1 \le i < N} |d[i] - d[i - 1]| \approx 0.25 \cdot \frac{2\pi \cdot 1140.1}{44100.0} + \frac{0.45}{44100.0} = \mathbf{0.04062\text{f}}$$

### 2. Exact Grain Parameters, Known Crossing Index, and Window Choice
- **Common parameters:**
  - `regionLo = 0.0f`, `regionHi = 1.0f`, `loopGrains = true`, `loopCrossfade = 0.1f`
  - `manual = true`, `scanSpeed = 0.0f`, `pitchRatio = 1.0f`, `gain = 1.0f`
  - `densityHz = 0.5f`, `lengthSec = 1.0f`, `maxGrains = 1`
    - *Why `densityHz = 0.5f` and `lengthSec = 1.0f`?* Because in `spawn()` (`granular.cpp:316`), `overlap = max(1.0, lengthSec * densityHz) = max(1.0, 0.5) = 1.0`, so **`norm = 1.0f` and `g.gl = g.gr = 1.0f`** (no $1/\sqrt{2}$ gain dilution!), `g.length = 44100` frames, and `period = 44100 / 0.5 = 88200` frames (no second grain can even attempt to spawn within `44100` frames).
  - **Window choice:** `windowAmount = 0.0f` (`shape = WindowShape::hann`, `windowSkew = 0.0f`).
    - With `g.length = 44100` and `windowAmount = 0.0f`, `windowValue` / `windowKernel` equals `1.0f` everywhere except the `1/64` edge ramp on `age in [0, 689]` and `age in [43411, 44099]`.
  - **Rendered length:** `numFrames = 12000` (well inside the flat `w == 1.0f` plateau `[690, 43410]`).
- **Exact Crossing Output Sample Index $i_{\text{wrap}}$:**
  - Place the grain start $\Delta_{\text{start}} = 0.05 \times N = 4410$ frames ($= \text{xfade}$) before the target boundary:
    - **Forward (`reverseGrains = false`, `step = +1.0`):** `p.position = tc.loopHi - 0.05f`, so $g.\text{pos}(0) = \text{lg1} - 4410$.
      At output sample $i$, before wrapping, $g.\text{pos}(i) = \text{lg1} - 4410 + i$.
      The last pre-wrap sample is $i = 4409$ ($g.\text{pos} = \text{lg1} - 1$), and the wrap to $g.\text{pos} = \text{lg0}$ occurs at:
      $$i_{\text{wrap,fwd}} = \text{ lg1} - g.\text{pos}(0) = 0.05 \times 88200 = \mathbf{4410}$$
    - **Reverse (`reverseGrains = true`, `step = -1.0`):** `p.position = tc.loopLo + 0.05f`, so $g.\text{pos}(0) = \text{lg0} + 4410$.
      At output sample $i$, before wrapping, $g.\text{pos}(i) = \text{lg0} + 4410 - i$.
      The grain reaches $g.\text{pos} = \text{lg0}$ at $i = 4410$, and wraps to $g.\text{pos} = \text{lg1} - 1$ at:
      $$i_{\text{wrap,rev}} = \left(g.\text{pos}(0) - \text{lg0}\right) + 1 = 0.05 \times 88200 + 1 = \mathbf{4411}$$

### 3. Measured Region, Measure, and Threshold
- **Measured region:** Measure both:
  1. **Tight wrap window:** $i \in [i_{\text{wrap}} - 8,\, i_{\text{wrap}} + 8]$ (i.e. $i \in [4402, 4418]$ for forward and $i \in [4403, 4419]$ for reverse) — isolating the wrap boundary itself.
  2. **Full crossfade + wrap window:** $i \in [1000, 9000]$ — covering the entry into the 4410-sample crossfade (at $i = 4410$ or $i = 0$), the entire crossfade, the wrap at $i = 4410 / 4411$, and the exit from a post-wrap crossfade (at $i = 8820$), while staying strictly inside the flat window plateau $w(i) \equiv 1.0\text{f}$ ($i \in [690, 43410]$).
- **Measure:**
  $$\Delta_{\text{wrap}} = \max_{i \in [i_{\text{wrap}} - 8,\, i_{\text{wrap}} + 8]} |\text{out}[i] - \text{out}[i - 1]|, \qquad \Delta_{\text{region}} = \max_{i \in [1000,\, 9000]} |\text{out}[i] - \text{out}[i - 1]|$$
  normalized against the local maximum step $\Delta_{\text{local}}$ of $d[k]$ inside `[lg0 - xfade, lg1 + xfade]`, or against global $\text{maxSampleStep} = 0.04062\text{f}$.
- **Threshold:**
  $$\Delta_{\text{wrap}} \le 1.15 \times \text{maxSampleStep} = \mathbf{0.04671\text{f}}, \qquad \Delta_{\text{region}} \le 1.25 \times \text{maxSampleStep} = \mathbf{0.05078\text{f}}$$

### 4. Expected Values on `develop` vs. Fix Attempt 2 (`25a37f8c`) Across All Cases

| Case | Loop Bounds (`loopLo..loopHi`) | Direction & Mode in Fix 2 | Expected $\Delta_{\text{wrap}}$ on `develop` | `develop` Result | Expected $\Delta_{\text{wrap}}$ on Fix Attempt 2 | Fix 2 Result |
|---|---|---|---|---|---|---|
| **A-Fwd** | `0.00 .. 0.50` (`lg0=0, lg1=44100`) | Forward (**F2** `afterWrap`) | At $i = 4409 \to 4410$: fades to $0.000356 \cdot d[44099] \approx 0.00004\text{f}$, then jumps to $d[0] = +0.55000\text{f}$: $\Delta_{\text{wrap}} = \mathbf{0.54996\text{f}}$ (**`13.54x`** `maxSampleStep`) | **FAILS** (`13.54x > 1.15x`) | At $i = 4409 \to 4410$: transitions from $d[44099]$ to $d[44100]$: $\Delta_{\text{wrap}} = |d[44100] - d[44099]| \approx \mathbf{0.00035\text{f}}$ (**`0.009x`**; region peak $\le 0.65\text{x}$) | **PASSES** |
| **A-Rev** | `0.00 .. 0.50` (`lg0=0, lg1=44100`) | Reverse (**R1** `beforeWrap`) | At $i = 4410 \to 4411$: jumps from $d[0] = +0.55000\text{f}$ to $d[44099] \approx +0.10000\text{f}$: $\Delta_{\text{wrap}} = \mathbf{0.45000\text{f}}$ (**`11.08x`** `maxSampleStep`) | **FAILS** (`11.08x > 1.15x`) | At $i = 4410 \to 4411$: transitions from $d[44100]$ to $d[44099]$: $\Delta_{\text{wrap}} = |d[44099] - d[44100]| \approx \mathbf{0.00035\text{f}}$ (**`0.009x`**; region peak $\le 0.65\text{x}$) | **PASSES** |
| **B-Fwd** | `0.25 .. 0.75` (`lg0=22050, lg1=66150`) | Forward (**F1** `beforeWrap`, **bit-identical**) | At $i = 4409 \to 4410$: pre-roll exists; transitions from $\approx d[22049]$ to $d[22050]$: $\Delta_{\text{wrap}} \approx \mathbf{0.0155\text{f}}$ (**`0.38x`**; region peak $\le 0.90\text{x}$) | **PASSES** (pre-roll exists) | Identical to `develop`: $\Delta_{\text{wrap}} \approx \mathbf{0.0155\text{f}}$ (**`0.38x`**; region peak $\le 0.90\text{x}$) | **PASSES** |
| **B-Rev** | `0.25 .. 0.75` (`lg0=22050, lg1=66150`) | Reverse (**R1** `beforeWrap`) | At $i = 4410 \to 4411$: jumps from $d[22050] = +0.29775\text{f}$ to $d[66149] \approx -0.33589\text{f}$: $\Delta_{\text{wrap}} = \mathbf{0.63364\text{f}}$ (**`15.60x`** `maxSampleStep`) | **FAILS** (`15.60x > 1.15x`) | At $i = 4410 \to 4411$: transitions from $d[66150]$ to $d[66149]$: $\Delta_{\text{wrap}} \approx \mathbf{0.0319\text{f}}$ (**`0.79x`**; region peak $\le 0.95\text{x}$) | **PASSES** |
| **C-Fwd** | `0.50 .. 0.9999` (`lg0=44100, lg1=88190`) | Forward (**F1** `beforeWrap`) | Pre-roll `[39690, 44100)` exists: $\Delta_{\text{wrap}} = |d[44100] - d[44099]| \approx \mathbf{0.00035\text{f}}$ (**`0.009x`**; region peak $\le 1.08\text{x}$) | **PASSES** (pre-roll exists) | Identical to `develop`: $\Delta_{\text{wrap}} \approx \mathbf{0.00035\text{f}}$ (**`0.009x`**; region peak $\le 1.08\text{x}$) | **PASSES** |
| **C-Rev** | `0.50 .. 0.9999` (`lg0=44100, lg1=88190`) | Reverse (**R2** `afterWrap`) | At $i = 4410 \to 4411$: jumps from $d[44100] = +0.10000\text{f}$ to $d[88189] \approx -0.733\text{f}$: $\Delta_{\text{wrap}} \approx \mathbf{0.833\text{f}}$ (**`20.51x`** `maxSampleStep`) | **FAILS** (`20.51x > 1.15x`) | At $i = 4410 \to 4411$: transitions from $d[44100]$ to $\approx d[44099]$: $\Delta_{\text{wrap}} \approx \mathbf{0.00035\text{f}}$ (**`0.009x`**; region peak $\le 1.08\text{x}$) | **PASSES** |

### 5. Bit-Identity Requirement Between `develop` and Fix Attempt 2 (Case B-Fwd, F1)
- **Case:** **B-Fwd** (`loopLo = 0.25f`, `loopHi = 0.75f`, `reverseGrains = false`, `position = 0.70f`, `loopCrossfade = 0.1f`, `numFrames = 12000`), where `lg0 - xfade = 22050.0 - 4410.0 = 17640.0 >= 0.0` selects **F1** (`fwdMode = LoopXFade::beforeWrap, fwdXf = xfade`).
- **Which output samples to compare:** **All `12000` rendered output samples `outFwd[0 .. 11999]` (both Left and Right channels, and across both `renderGrains` and `renderGrainsReference`)** — specifically covering:
  - Pre-wrap crossfade samples **`i = 0 .. 4409`** (`g.pos = 61740 .. 66149`, where the F1 crossfade arithmetic runs on every sample), and
  - Post-wrap samples **`i = 4410 .. 11999`** (`g.pos = 22050 .. 29639`),
  must be **bit-for-bit identical (`std::memcmp == 0`)** between `develop` and fix attempt 2.

---

Nothing under the forbidden paths (`core/engine-vital/`, `core/vital-import/`, or `/Users/szabi/Documents/Projects/Safian/.hw-local/synth-knowledge/`) was opened, read, grepped, or listed.
