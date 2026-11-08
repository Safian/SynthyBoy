# core/fx-serum-b: Serum 2 FX Modules (Bode & Convolve)

Standalone real-time DSP library implementing the Serum 2-exclusive FX modules **Bode** (frequency shifter) and **Convolve** (partitioned FFT convolution reverb/IR processor) per `docs/ui-design/08-effects.md` and the Serum 2 manual (pages 161, 164–165).

## Modules

### 1. Bode Frequency Shifter (`synthyboy::fx::Bode`)
Hilbert-transform-based single-sideband frequency shifter with delay feedback loop and stereo sideband dispersion.
- **Controls:**
  - `monoInput`: Route mono input to the module.
  - `shift`: Pitch shift percentage (-1.0 to +1.0).
  - `range`: Shift range in Hertz (0.0 to 5000.0 Hz).
  - `dir`: Shift direction (-1.0 to +1.0). Center (0.0) sends channels in opposite directions (left up, right down).
  - `width`: Stereo sideband spread (0.0 to 1.0).
  - `delay`: Delay time in seconds (0.0 to 2.0 s).
  - `bpm`: Delay tempo sync flag.
  - `tempoBpm`: Host tempo in BPM.
  - `delayBeats`: Delay duration in whole beats (e.g. 0.25 = 1/16th note).
  - `feedback`: Delay feedback into shifter (-0.95 to +0.95).
  - `balance`: Delay input mix between down- and up-shifted signals (-1.0 to +1.0).
  - `blur`: Modulation for chorus and wow/flutter effects (0.0 to 1.0).
  - `mix`: Wet/dry mix (0.0 to 1.0).
  - `level`: Output trim in decibels.
  - `retrig()`: Restart carrier oscillator phase (Serum right-click Retrig function).
- **DSP Architecture:**
  - 129-tap Blackman-windowed FIR Hilbert transformer with constant linear phase.
  - Constant latency of 64 frames (`latency() == 64`), with internal phase alignment for the dry path.
  - Denormal flush-to-zero and feedback cubic soft-limiting.

### 2. Convolve Convolution Reverb (`synthyboy::fx::Convolve`)
Zero-latency uniform partitioned FFT convolution reverb and impulse response processor.
- **Controls:**
  - `loadIr(data, frames, channels, irRate)`: Non-real-time impulse response loader supporting mono or stereo float buffers.
  - `size`: Time stretch / contract factor (0.25 to 4.0).
  - `tone`: 1-pole spectral tilt EQ (-1.0 to +1.0).
  - `minimumPhase` ($\phi$ MIN): Cepstral minimum-phase conversion eliminating pre-ringing.
  - `preDelay`: Pre-delay time in seconds (0.0 to 2.0 s).
  - `bpm`: Pre-delay tempo sync flag.
  - `tempoBpm`: Host tempo in BPM.
  - `preDelayBeats`: Pre-delay duration in whole beats.
  - `attack`: Fade-in duration in seconds.
  - `decay`: Shortening / exponential decay duration in seconds.
  - `damp`: High-frequency acoustic damping over time (0.0 to 1.0).
  - `irGain`: Impulse gain in decibels (-24.0 to +24.0 dB).
  - `mix`: Wet/dry mix (0.0 to 1.0).
  - `level`: Output trim in decibels.
- **DSP Architecture:**
  - True zero latency (`latency() == 0`).
  - Uniform partitioned convolution:
    - Head partition: 128-sample direct time-domain FIR with contiguous array optimization.
    - Tail partitions: uniform 128-sample blocks transformed via 256-point Radix-2 FFT and frequency-domain multiply-accumulate with overlap-add.
  - IR Length Cap: `kMaxIrFrames = 524288` (~10.9 seconds at 48 kHz).
  - Sinc windowed resampler for sample rate conversion (`irRate` to `sampleRate`).
  - Bitwise block-size independence across block sizes 1, 64, and 512.

### 3. Radix-2 FFT (`synthyboy::fx::Radix2FFT`)
Standalone in-place complex and real Cooley-Tukey Radix-2 FFT with pre-computed bit-reversal and twiddle tables. Completely zero allocations during audio processing.

### 4. WAV Reader & Writer (`synthyboy::fx::readWav`, `synthyboy::fx::writeWav`)
Small lightweight RIFF WAV decoder and encoder supporting:
- 16-bit signed integer PCM
- 24-bit signed integer PCM
- 32-bit signed integer PCM
- 32-bit IEEE float (format tag 3 and extensible format)

## Integration
In `core/CMakeLists.txt`:
```cmake
add_subdirectory(fx-serum-b)
```
Links `synthyboy_fx_serum_b`.
