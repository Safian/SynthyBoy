#!/usr/bin/env python3
# SynthyBoy - generator for DnbWave factory wavetables.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
Generates Serum-compatible 2048 samples/frame RIFF WAV wavetables with 'clm ' metadata chunk:
1. content/wavetables/DnbWave.wav: Single-cycle descending kick chirp (ICanHasKick style)
2. content/wavetables/DnbWave_sweep.wav: 64-frame morph sweep from pure sine to dense chirp
3. content/wavetables/DnbWave_drive.wav: 64-frame morph sweep with analog tanh saturation
"""

import math
import os
import struct

def make_wavetable(filename: str, num_frames: int, func):
    os.makedirs(os.path.dirname(filename), exist_ok=True)
    samples_per_frame = 2048
    sample_rate = 44100
    num_channels = 1
    bits_per_sample = 32
    audio_format = 3  # IEEE Float
    block_align = num_channels * (bits_per_sample // 8)
    byte_rate = sample_rate * block_align

    raw_data = bytearray()
    for f in range(num_frames):
        y = f / (num_frames - 1) if num_frames > 1 else 0.0
        for s in range(samples_per_frame):
            w = s / (samples_per_frame - 1)
            val = func(w, y)
            val = max(-1.0, min(1.0, val))
            raw_data += struct.pack("<f", val)

    clm_text = b"<!>2048 10000000 wavetable (created in SynthyBoy)\x00"
    clm_chunk = b"clm " + struct.pack("<I", len(clm_text)) + clm_text
    if len(clm_text) % 2 == 1:
        clm_chunk += b"\x00"

    data_chunk = b"data" + struct.pack("<I", len(raw_data)) + raw_data
    fmt_chunk = b"fmt \x10\x00\x00\x00" + struct.pack("<HHIIHH", audio_format, num_channels, sample_rate, byte_rate, block_align, bits_per_sample)

    total_len = 4 + len(fmt_chunk) + len(clm_chunk) + len(data_chunk)
    riff_header = b"RIFF" + struct.pack("<I", total_len) + b"WAVE"

    with open(filename, "wb") as out:
        out.write(riff_header + fmt_chunk + clm_chunk + data_chunk)
    print(f"Generated {filename}: {num_frames} frames, {len(raw_data)} sample bytes")

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    target_dir = os.path.join(root, "content", "wavetables")
    digital_dir = os.path.join(target_dir, "Digital")
    analog_dir = os.path.join(target_dir, "Analog")
    spectral_dir = os.path.join(target_dir, "Spectral")
    vowel_dir = os.path.join(target_dir, "Vowel")

    # --- Digital & Bass ---
    # 1. DnbWave (Single frame kick chirp)
    for d in (target_dir, digital_dir):
        make_wavetable(os.path.join(d, "DnbWave.wav"), 1,
                       lambda w, y: math.sin(2.0 * math.pi * (15.0 * w - 7.0 * (w**2))))
        make_wavetable(os.path.join(d, "DnbWave_sweep.wav"), 64,
                       lambda w, y: math.sin(2.0 * math.pi * ((1.0 + 14.0 * y) * w - 7.0 * y * (w**2))))
        make_wavetable(os.path.join(d, "DnbWave_drive.wav"), 64,
                       lambda w, y: math.tanh(2.5 * math.sin(2.0 * math.pi * ((1.0 + 14.0 * y) * w - 7.0 * y * (w**2)))))
        make_wavetable(os.path.join(d, "Monster_growl.wav"), 64,
                       lambda w, y: math.tanh(2.5 * math.sin(2.0 * math.pi * w + (2.0 + 4.0 * y) * math.sin(math.pi * w) * math.sin(2.0 * math.pi * w * (2.0 + 3.0 * y)))))
        make_wavetable(os.path.join(d, "Acid_303.wav"), 64,
                       lambda w, y: math.tanh((1.0 - 2.0 * w) + 1.2 * math.exp(-4.0 * w) * math.sin(2.0 * math.pi * w * (2.0 + 14.0 * y))))

    make_wavetable(os.path.join(digital_dir, "Hard_sync.wav"), 64,
                   lambda w, y: 1.0 - 2.0 * ((w * (1.0 + 7.0 * y)) % 1.0))
    make_wavetable(os.path.join(digital_dir, "FM_sweep.wav"), 64,
                   lambda w, y: math.sin(2.0 * math.pi * w + 4.0 * y * math.sin(4.0 * math.pi * w)))
    make_wavetable(os.path.join(digital_dir, "Bitcrush_sweep.wav"), 64,
                   lambda w, y: round(math.sin(2.0 * math.pi * w) * (2.0 + 14.0 * (1.0 - y))) / (2.0 + 14.0 * (1.0 - y)))
    make_wavetable(os.path.join(digital_dir, "Modern_talking.wav"), 64,
                   lambda w, y: math.tanh(1.8 * (math.sin(2.0 * math.pi * w) * 0.35 + math.sin(math.pi * w) * math.sin(2.0 * math.pi * w * (3.0 + 6.0 * y)) * math.exp(-2.0 * w) * 1.4)))
    make_wavetable(os.path.join(digital_dir, "Reese_detune.wav"), 64,
                   lambda w, y: 0.5 * ((1.0 - 2.0 * w) + (1.0 - 2.0 * ((w * (1.0 + 0.04 * (1.0 + 3.0 * y))) % 1.0))))
    make_wavetable(os.path.join(digital_dir, "Sub_808_drop.wav"), 64,
                   lambda w, y: math.sin(2.0 * math.pi * (w + (2.0 + 6.0 * y) * w * ((1.0 - w) ** 2))))
    make_wavetable(os.path.join(digital_dir, "Donk_bass.wav"), 64,
                   lambda w, y: math.sin(2.0 * math.pi * (w + (4.0 + 10.0 * y) * w * ((1.0 - w) ** 3))))
    make_wavetable(os.path.join(digital_dir, "Dirty_sync.wav"), 64,
                   lambda w, y: (1.0 - 2.0 * ((w * (1.0 + 7.0 * y)) % 1.0)) * (1.0 - 0.4 * math.sin(math.pi * w)))
    make_wavetable(os.path.join(digital_dir, "Neuro_squelch.wav"), 64,
                   lambda w, y: math.tanh(2.5 * math.sin(2.0 * math.pi * w * (1.0 + 4.0 * y)) * math.exp(-3.0 * w) + 0.5 * math.sin(2.0 * math.pi * w)))

    # --- Analog & Shapes ---
    for d in (target_dir, analog_dir):
        make_wavetable(os.path.join(d, "PWM_sweep.wav"), 64,
                       lambda w, y: 1.0 if w < (0.05 + 0.9 * y) else -1.0)

    # Basic shapes (4 frames: Sine, Triangle, Saw, Square)
    def basic_shape(w, y):
        # 4 frames: frame 0 = sine, 1 = triangle, 2 = saw, 3 = square
        if y < 0.25:
            # Sine to triangle
            t = y / 0.25
            sine = math.sin(2.0 * math.pi * w)
            tri = (1.0 - 4.0 * abs(w - 0.5)) if w < 0.75 else (4.0 * (w - 0.75) - 1.0)
            return (1.0 - t) * sine + t * tri
        elif y < 0.5:
            # Triangle to saw
            t = (y - 0.25) / 0.25
            tri = (1.0 - 4.0 * abs(w - 0.5)) if w < 0.75 else (4.0 * (w - 0.75) - 1.0)
            saw = 1.0 - 2.0 * w
            return (1.0 - t) * tri + t * saw
        else:
            # Saw to square
            t = (y - 0.5) / 0.5
            saw = 1.0 - 2.0 * w
            sq = 1.0 if w < 0.5 else -1.0
            return (1.0 - t) * saw + t * sq

    make_wavetable(os.path.join(analog_dir, "Basic_shapes.wav"), 64, basic_shape)
    make_wavetable(os.path.join(analog_dir, "Supersaw_stack.wav"), 64,
                   lambda w, y: 0.34 * ((1.0 - 2.0 * w) + (1.0 - 2.0 * ((w * (1.02 + 0.06 * y)) % 1.0)) + (1.0 - 2.0 * ((w * (0.98 - 0.06 * y) + 1.0) % 1.0))))
    make_wavetable(os.path.join(analog_dir, "Comb_filter_sweep.wav"), 64,
                   lambda w, y: 0.5 * ((1.0 - 2.0 * w) + (1.0 - 2.0 * ((w + 0.05 + 0.3 * y) % 1.0))))
    make_wavetable(os.path.join(analog_dir, "Tape_saturate.wav"), 64,
                   lambda w, y: math.tanh(2.0 * math.sin(2.0 * math.pi * (w + 0.03 * y * math.sin(2.0 * math.pi * w * 3.0)))))
    make_wavetable(os.path.join(analog_dir, "Casio_CZ_res.wav"), 64,
                   lambda w, y: math.sin(2.0 * math.pi * w * (2.0 + 10.0 * y)) * 0.5 * (1.0 - math.cos(2.0 * math.pi * w)))
    make_wavetable(os.path.join(analog_dir, "Hypersaw_5x.wav"), 64,
                   lambda w, y: 0.2 * ((1.0 - 2.0 * w) + (1.0 - 2.0 * ((w * (1.01 + 0.03 * y)) % 1.0)) + (1.0 - 2.0 * ((w * (0.99 - 0.03 * y) + 1.0) % 1.0)) + (1.0 - 2.0 * ((w * (1.02 + 0.06 * y)) % 1.0)) + (1.0 - 2.0 * ((w * (0.98 - 0.06 * y) + 1.0) % 1.0))))

    # --- Spectral & Harmonics ---
    for d in (target_dir, spectral_dir):
        make_wavetable(os.path.join(d, "Metallic_comb.wav"), 64,
                       lambda w, y: math.tanh(math.sin(2.0 * math.pi * w) + 0.6 * math.sin(math.pi * w) * math.sin(2.0 * math.pi * w * 2.73) + 0.4 * math.sin(math.pi * w) * math.sin(2.0 * math.pi * w * 5.41 * (1.0 + y))))

    make_wavetable(os.path.join(spectral_dir, "Organ_drawbars.wav"), 64,
                   lambda w, y: (math.sin(2.0 * math.pi * w) + 0.8 * math.sin(4.0 * math.pi * w) + 0.6 * math.sin(6.0 * math.pi * w) + 0.4 * math.sin(8.0 * math.pi * w)) * 0.4)
    make_wavetable(os.path.join(spectral_dir, "Buchla_wavefold.wav"), 64,
                   lambda w, y: math.tanh(1.5 * math.sin(2.0 * math.pi * w + (1.0 + 3.0 * y) * math.sin(4.0 * math.pi * w))))
    make_wavetable(os.path.join(spectral_dir, "Acoustic_pluck.wav"), 64,
                   lambda w, y: math.sin(2.0 * math.pi * w) + (0.6 * math.sin(4.0 * math.pi * w) + 0.4 * math.sin(6.0 * math.pi * w) + 0.3 * math.sin(8.0 * math.pi * w) + 0.2 * math.sin(10.0 * math.pi * w)) * (1.0 - 0.85 * y))
    make_wavetable(os.path.join(spectral_dir, "Shepard_riser.wav"), 64,
                   lambda w, y: 0.5 * math.sin(2.0 * math.pi * w * (2.0 ** y)) + 0.7 * math.sin(2.0 * math.pi * w * (2.0 ** (1.0 + y))) + 0.5 * math.sin(2.0 * math.pi * w * (2.0 ** (2.0 + y))) + 0.3 * math.sin(2.0 * math.pi * w * (2.0 ** (3.0 + y))))
    make_wavetable(os.path.join(spectral_dir, "Chime_bells.wav"), 64,
                   lambda w, y: math.sin(2.0 * math.pi * w) + math.sin(math.pi * w) * (0.65 * math.sin(2.0 * math.pi * w * 2.76) + 0.4 * math.sin(2.0 * math.pi * w * 5.4) + 0.2 * math.sin(2.0 * math.pi * w * 8.9)))
    make_wavetable(os.path.join(spectral_dir, "Gamelan_gong.wav"), 64,
                   lambda w, y: math.sin(2.0 * math.pi * w) + math.sin(math.pi * w) * (0.8 * math.sin(2.0 * math.pi * w * 1.52) + 0.6 * math.sin(2.0 * math.pi * w * 2.76) + 0.4 * math.sin(2.0 * math.pi * w * (4.2 + 2.0 * y))))
    make_wavetable(os.path.join(spectral_dir, "Harmonic_ladder.wav"), 64,
                   lambda w, y: math.sin(2.0 * math.pi * w) + 0.5 * math.sin(4.0 * math.pi * w) * y + 0.33 * math.sin(6.0 * math.pi * w) * (y ** 2) + 0.25 * math.sin(8.0 * math.pi * w) * (y ** 3) + 0.2 * math.sin(10.0 * math.pi * w) * (y ** 4))
    make_wavetable(os.path.join(spectral_dir, "Chebyshev_fold.wav"), 64,
                   lambda w, y: math.sin(2.0 * math.pi * w) - 0.5 * y * math.sin(6.0 * math.pi * w) + 0.3 * y * math.sin(10.0 * math.pi * w))

    # --- Vowel & Formants ---
    for d in (target_dir, vowel_dir):
        make_wavetable(os.path.join(d, "Vowel_formant.wav"), 64,
                       lambda w, y: math.sin(math.pi * w) * (math.sin(2.0 * math.pi * w * (2.0 + 5.0 * y)) + 0.7 * math.sin(2.0 * math.pi * w * (5.0 + 9.0 * y))))
    make_wavetable(os.path.join(vowel_dir, "Vowel_talkbox.wav"), 64,
                   lambda w, y: math.sin(math.pi * w) * (math.sin(2.0 * math.pi * w * (1.0 + 4.0 * y)) + 0.8 * math.sin(2.0 * math.pi * w * (3.0 + 8.0 * y)) + 0.5 * math.sin(2.0 * math.pi * w * (6.0 + 10.0 * y))))

if __name__ == "__main__":
    main()
