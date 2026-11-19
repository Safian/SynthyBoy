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

    # 1. Single frame DnbWave (descending kick chirp)
    make_wavetable(os.path.join(target_dir, "DnbWave.wav"), 1,
                   lambda w, y: math.sin(2.0 * math.pi * (15.0 * w - 7.0 * (w**2))))

    # 2. Multi-frame DnbWave sweep (64 frames)
    make_wavetable(os.path.join(target_dir, "DnbWave_sweep.wav"), 64,
                   lambda w, y: math.sin(2.0 * math.pi * ((1.0 + 14.0 * y) * w - 7.0 * y * (w**2))))

    # 3. Multi-frame DnbWave drive (64 frames)
    make_wavetable(os.path.join(target_dir, "DnbWave_drive.wav"), 64,
                   lambda w, y: math.tanh(2.5 * math.sin(2.0 * math.pi * ((1.0 + 14.0 * y) * w - 7.0 * y * (w**2)))))

if __name__ == "__main__":
    main()
