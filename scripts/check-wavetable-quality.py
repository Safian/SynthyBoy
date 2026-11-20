#!/usr/bin/env python3
# SynthyBoy - checks wavetable quality against product rules (2048 samples/frame, finite, peak <= 1.0, DC < 1%, zero crossings).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
Quality gate per wavetable:
- Every frame has 2048 samples (no remainder)
- Finite values (no NaN, no Inf)
- Peak at most 1.0 (|x| <= 1.0)
- DC offset per frame (mean) below 0.01 (1%) of the peak
- For single-cycle tables (frames == 1 or marked):
    - First sample |s[0]| < 0.02 of peak
    - Last sample |s[-1]| < 0.02 of peak
    - Zero crossing direction consistent (slope entering and leaving boundary has matching sign)
"""

import argparse
import math
import os
import struct
import sys
from typing import Dict, List, Optional, Tuple


def read_wav_samples(path: str) -> Tuple[List[float], Dict]:
    with open(path, "rb") as f:
        data = f.read()

    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a valid RIFF WAVE file")

    pos = 12
    fmt = {}
    data_bytes = b""
    clm_chunk = None

    while pos + 8 <= len(data):
        chunk_id = data[pos:pos + 4]
        chunk_sz = struct.unpack("<I", data[pos + 4:pos + 8])[0]
        chunk_data = data[pos + 8:pos + 8 + chunk_sz]
        if chunk_id == b"fmt ":
            audio_fmt, num_ch, sr, byte_rate, block_align, bits = struct.unpack("<HHIIHH", chunk_data[:16])
            fmt = {
                "audio_format": audio_fmt,
                "channels": num_ch,
                "sample_rate": sr,
                "byte_rate": byte_rate,
                "block_align": block_align,
                "bits": bits,
            }
        elif chunk_id == b"clm ":
            clm_chunk = chunk_data
        elif chunk_id == b"data":
            data_bytes = chunk_data
        pos += 8 + chunk_sz
        if chunk_sz % 2 == 1:
            pos += 1

    if not fmt:
        raise ValueError(f"{path}: missing fmt chunk")

    audio_fmt = fmt["audio_format"]
    bits = fmt["bits"]
    num_ch = fmt["channels"]

    if audio_fmt == 3 and bits == 32:  # IEEE Float
        num_samples = len(data_bytes) // 4
        raw = struct.unpack(f"<{num_samples}f", data_bytes[:num_samples * 4])
        samples = list(raw)
    elif audio_fmt == 1 and bits == 16:  # 16-bit PCM
        num_samples = len(data_bytes) // 2
        raw = struct.unpack(f"<{num_samples}h", data_bytes[:num_samples * 2])
        samples = [s / 32768.0 for s in raw]
    elif audio_fmt == 1 and bits == 24:  # 24-bit PCM
        samples = []
        for i in range(0, len(data_bytes) - 2, 3):
            val = int.from_bytes(data_bytes[i:i + 3], byteorder="little", signed=True)
            samples.append(val / 8388608.0)
    else:
        raise ValueError(f"{path}: unsupported format (format={audio_fmt}, bits={bits})")

    if num_ch > 1:
        # Mix down or extract channel 0
        samples = samples[::num_ch]

    fmt["clm"] = clm_chunk.decode("utf-8", "replace") if clm_chunk else None
    return samples, fmt


def check_wavetable(path: str, single_cycle: Optional[bool] = None) -> Dict:
    samples, fmt = read_wav_samples(path)
    samples_per_frame = 2048
    total_samples = len(samples)
    frames = total_samples // samples_per_frame
    rem = total_samples % samples_per_frame

    errors = []

    # 1. Frame sample count
    if rem != 0 or total_samples == 0:
        errors.append(f"Invalid frame size: {total_samples} samples is not a multiple of {samples_per_frame} (rem={rem})")

    # 2. Finite values
    non_finite_count = sum(1 for s in samples if not math.isfinite(s))
    if non_finite_count > 0:
        errors.append(f"Non-finite values: {non_finite_count} samples are NaN or Inf")

    # 3. Peak at most 1.0
    peak = max((abs(s) for s in samples), default=0.0)
    if peak > 1.0 + 1e-6:
        errors.append(f"Peak exceeds 1.0: peak={peak:.6f}")

    # 4. DC offset per frame below 0.01 (1%) of peak
    dc_failed_frames = []
    max_dc = 0.0
    max_dc_ratio = 0.0

    for fi in range(frames):
        frame = samples[fi * samples_per_frame:(fi + 1) * samples_per_frame]
        mean_val = sum(frame) / samples_per_frame
        abs_dc = abs(mean_val)
        ratio = (abs_dc / peak) if peak > 0 else 0.0
        if abs_dc > max_dc:
            max_dc = abs_dc
            max_dc_ratio = ratio
        if ratio >= 0.01:
            dc_failed_frames.append((fi, abs_dc, ratio))

    if dc_failed_frames:
        errors.append(
            f"DC offset exceeds 1% of peak in {len(dc_failed_frames)}/{frames} frames "
            f"(max DC={max_dc:.6f}, {max_dc_ratio * 100:.2f}% of peak)"
        )

    # 5. Single-cycle zero crossing check
    is_sc = single_cycle if single_cycle is not None else (frames == 1)
    sc_info = {}
    if is_sc and frames >= 1:
        frame0 = samples[:samples_per_frame]
        s0 = frame0[0]
        send = frame0[-1]
        s0_ratio = (abs(s0) / peak) if peak > 0 else 0.0
        send_ratio = (abs(send) / peak) if peak > 0 else 0.0

        dir_start = 1 if frame0[1] > frame0[0] else (-1 if frame0[1] < frame0[0] else 0)
        dir_end = 1 if frame0[-1] > frame0[-2] else (-1 if frame0[-1] < frame0[-2] else 0)
        dir_consistent = (dir_start == dir_end) and (dir_start != 0)

        sc_info = {
            "s0": s0,
            "send": send,
            "s0_ratio": s0_ratio,
            "send_ratio": send_ratio,
            "dir_start": dir_start,
            "dir_end": dir_end,
            "dir_consistent": dir_consistent,
        }

        if s0_ratio >= 0.02:
            errors.append(f"Single-cycle first sample not near zero: |s[0]|={abs(s0):.6f} ({s0_ratio * 100:.2f}% of peak >= 2%)")
        if send_ratio >= 0.02:
            errors.append(f"Single-cycle last sample not near zero: |s[-1]|={abs(send):.6f} ({send_ratio * 100:.2f}% of peak >= 2%)")
        if not dir_consistent:
            errors.append(f"Single-cycle zero-crossing direction inconsistent: start={dir_start}, end={dir_end}")

    return {
        "path": path,
        "frames": frames,
        "samples_per_frame": samples_per_frame,
        "total_samples": total_samples,
        "sample_rate": fmt.get("sample_rate", 44100),
        "clm": fmt.get("clm"),
        "peak": peak,
        "max_dc": max_dc,
        "max_dc_ratio": max_dc_ratio,
        "dc_failed_frames": dc_failed_frames,
        "is_single_cycle": is_sc,
        "sc_info": sc_info,
        "errors": errors,
        "passed": len(errors) == 0,
    }


def find_wav_files(directory: str) -> List[str]:
    wavs = []
    for root, _, files in sorted(os.walk(directory)):
        for f in sorted(files):
            if f.endswith(".wav") and not f.startswith("."):
                wavs.append(os.path.join(root, f))
    return wavs


def main():
    parser = argparse.ArgumentParser(description="Check wavetable audio quality against product gates.")
    parser.add_argument("paths", nargs="*", help="WAV file(s) or director(y/ies) to check.")
    parser.add_argument("--dir", help="Directory containing wavetables (default: content/wavetables).")
    parser.add_argument("--single-cycle", action="store_true", help="Force single-cycle zero-crossing checks.")
    parser.add_argument("--report-only", action="store_true", help="Always exit 0, only report results.")
    args = parser.parse_args()

    files = []
    if args.paths:
        for p in args.paths:
            if os.path.isdir(p):
                files.extend(find_wav_files(p))
            elif os.path.isfile(p):
                files.append(p)
            else:
                print(f"Warning: path not found: {p}", file=sys.stderr)
    elif args.dir:
        files.extend(find_wav_files(args.dir))
    else:
        default_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "content", "wavetables")
        if os.path.isdir(default_dir):
            files.extend(find_wav_files(default_dir))
        else:
            parser.error("No input files or directory specified, and content/wavetables not found.")

    if not files:
        print("No WAV files found to check.")
        return 0

    print(f"Checking quality for {len(files)} wavetable file(s):")
    print("-" * 100)
    print(f"{'Path':40} | {'Frames':6} | {'Peak':7} | {'Max DC %':8} | {'Status'}")
    print("-" * 100)

    failing = []
    passing = []

    for path in sorted(files):
        res = check_wavetable(path, single_cycle=True if args.single_cycle else None)
        rel = os.path.relpath(path)
        status_str = "PASS" if res["passed"] else "FAIL"
        print(f"{rel:40} | {res['frames']:6} | {res['peak']:7.4f} | {res['max_dc_ratio'] * 100:7.2f}% | {status_str}")
        if res["passed"]:
            passing.append(res)
        else:
            failing.append(res)

    print("-" * 100)
    print(f"Summary: {len(passing)} passed, {len(failing)} failed out of {len(files)} table(s).")

    if failing:
        print("\nFailing table details:")
        for res in failing:
            rel = os.path.relpath(res["path"])
            print(f"\n  {rel}:")
            print(f"    Frames: {res['frames']}, Peak: {res['peak']:.6f}, Max DC: {res['max_dc']:.6f} ({res['max_dc_ratio'] * 100:.2f}% of peak)")
            if res["is_single_cycle"] and res["sc_info"]:
                sc = res["sc_info"]
                print(f"    Single-cycle endpoints: s[0]={sc['s0']:.6f} ({sc['s0_ratio'] * 100:.2f}%), s[-1]={sc['send']:.6f} ({sc['send_ratio'] * 100:.2f}%), dir_consistent={sc['dir_consistent']}")
            for err in res["errors"]:
                print(f"    - {err}")

    if failing and not args.report_only:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
