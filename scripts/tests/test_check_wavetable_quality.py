#!/usr/bin/env python3
# SynthyBoy - tests of the wavetable quality gate (scripts/check-wavetable-quality.py).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import importlib.util
import math
import os
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT_PATH = ROOT / "scripts" / "check-wavetable-quality.py"


def load_checker():
    spec = importlib.util.spec_from_file_location("check_wavetable_quality", SCRIPT_PATH)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


checker = load_checker()


def write_test_wav(path: str, samples: list, sample_rate: int = 44100, bits: int = 32, clm: bool = True):
    num_samples = len(samples)
    if bits == 32:
        audio_format = 3  # IEEE Float
        raw_data = struct.pack(f"<{num_samples}f", *samples)
    elif bits == 16:
        audio_format = 1  # PCM
        int_samples = [max(-32768, min(32767, int(s * 32767))) for s in samples]
        raw_data = struct.pack(f"<{num_samples}h", *int_samples)
    else:
        raise ValueError("Unsupported bits")

    num_channels = 1
    block_align = num_channels * (bits // 8)
    byte_rate = sample_rate * block_align

    fmt_chunk = b"fmt \x10\x00\x00\x00" + struct.pack(
        "<HHIIHH", audio_format, num_channels, sample_rate, byte_rate, block_align, bits
    )

    clm_chunk = b""
    if clm:
        clm_text = b"<!>2048 10000000 wavetable (test)\x00"
        clm_chunk = b"clm " + struct.pack("<I", len(clm_text)) + clm_text
        if len(clm_text) % 2 == 1:
            clm_chunk += b"\x00"

    data_chunk = b"data" + struct.pack("<I", len(raw_data)) + raw_data

    total_len = 4 + len(fmt_chunk) + len(clm_chunk) + len(data_chunk)
    riff_header = b"RIFF" + struct.pack("<I", total_len) + b"WAVE"

    with open(path, "wb") as f:
        f.write(riff_header + fmt_chunk + clm_chunk + data_chunk)


class TestWavetableQuality(unittest.TestCase):
    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.tmp_dir.cleanup()

    def test_clean_single_cycle_sine_passes(self):
        """A pure single-cycle sine wave of 2048 samples must pass all gates."""
        N = 2048
        samples = [math.sin(2.0 * math.pi * i / N) * 0.9 for i in range(N)]
        p = os.path.join(self.tmp_dir.name, "clean_sine.wav")
        write_test_wav(p, samples)

        res = checker.check_wavetable(p)
        self.assertTrue(res["passed"], f"Expected PASS, got errors: {res['errors']}")
        self.assertEqual(res["frames"], 1)
        self.assertAlmostEqual(res["peak"], 0.9, places=3)
        self.assertLess(res["max_dc_ratio"], 0.001)
        self.assertTrue(res["is_single_cycle"])
        self.assertTrue(res["sc_info"]["dir_consistent"])

    def test_invalid_frame_sample_count_fails(self):
        """Files not having an exact multiple of 2048 samples must fail."""
        samples = [0.0] * 2000
        p = os.path.join(self.tmp_dir.name, "bad_length.wav")
        write_test_wav(p, samples)

        res = checker.check_wavetable(p)
        self.assertFalse(res["passed"])
        self.assertTrue(any("Invalid frame size" in e for e in res["errors"]))

    def test_peak_exceeding_one_fails(self):
        """Peak sample > 1.0 must fail the quality gate."""
        N = 2048
        samples = [math.sin(2.0 * math.pi * i / N) * 1.05 for i in range(N)]
        p = os.path.join(self.tmp_dir.name, "peak_over.wav")
        write_test_wav(p, samples)

        res = checker.check_wavetable(p)
        self.assertFalse(res["passed"])
        self.assertTrue(any("Peak exceeds 1.0" in e for e in res["errors"]))

    def test_dc_offset_exceeding_one_percent_fails(self):
        """DC offset >= 1% of peak must fail."""
        N = 2048
        # Peak 0.8, DC offset +0.02 (2.5% of peak)
        samples = [math.sin(2.0 * math.pi * i / N) * 0.8 + 0.02 for i in range(N)]
        p = os.path.join(self.tmp_dir.name, "high_dc.wav")
        write_test_wav(p, samples)

        res = checker.check_wavetable(p)
        self.assertFalse(res["passed"])
        self.assertTrue(any("DC offset exceeds 1%" in e for e in res["errors"]))

    def test_single_cycle_boundary_offset_fails(self):
        """Single cycle table with |s[0]| >= 2% of peak must fail."""
        N = 2048
        # Start at 0.05 on peak 0.9 (~5.5%)
        samples = [math.cos(2.0 * math.pi * i / N) * 0.9 for i in range(N)]
        p = os.path.join(self.tmp_dir.name, "cosine.wav")
        write_test_wav(p, samples)

        res = checker.check_wavetable(p, single_cycle=True)
        self.assertFalse(res["passed"])
        self.assertTrue(any("first sample not near zero" in e for e in res["errors"]))

    def test_multi_frame_wavetable_quality(self):
        """Multi-frame wavetable (64 frames) checks each frame."""
        N = 2048
        frames = 4
        samples = []
        for f in range(frames):
            samples.extend([math.sin(2.0 * math.pi * (1 + f) * i / N) * 0.8 for i in range(N)])
        p = os.path.join(self.tmp_dir.name, "multi_frame.wav")
        write_test_wav(p, samples)

        res = checker.check_wavetable(p)
        self.assertTrue(res["passed"], f"Errors: {res['errors']}")
        self.assertEqual(res["frames"], 4)
        self.assertFalse(res["is_single_cycle"])

    def test_repo_wavetables_frame_count_and_peak(self):
        """All 32 repo wavetables in content/wavetables must have 2048 samples/frame, finite values, and peak <= 1.0."""
        wt_dir = ROOT / "content" / "wavetables"
        if not wt_dir.is_dir():
            self.skipTest("content/wavetables directory not found")

        wav_files = checker.find_wav_files(str(wt_dir))
        self.assertGreaterEqual(len(wav_files), 32)

        for path in wav_files:
            res = checker.check_wavetable(path)
            rel = os.path.relpath(path, wt_dir)
            self.assertEqual(res["samples_per_frame"], 2048, f"{rel}: wrong samples per frame")
            self.assertLessEqual(res["peak"], 1.0 + 1e-6, f"{rel}: peak exceeds 1.0")
            self.assertEqual(res["total_samples"] % 2048, 0, f"{rel}: remainder not 0")
            # If single-cycle (DnbWave.wav), verify boundary zero-crossing
            if res["frames"] == 1:
                self.assertLess(res["sc_info"]["s0_ratio"], 0.02, f"{rel}: s0 exceeds 2%")
                self.assertLess(res["sc_info"]["send_ratio"], 0.02, f"{rel}: send exceeds 2%")
                self.assertTrue(res["sc_info"]["dir_consistent"], f"{rel}: direction inconsistent")


if __name__ == "__main__":
    unittest.main()
