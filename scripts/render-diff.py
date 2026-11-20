#!/usr/bin/env python3
"""render-diff.py - how big is the difference between two folders of renders (docs/plans/reverb-convolve.md task T07).

scripts/compare-renders.sh can prove two renders identical but cannot say how big a difference is. This reads the 32-bit float WAVs of both folders
(as `synthyboy_preset_render --float32` writes them), pairs them by file name and prints, per file:

    name max_abs_diff peak ratio=<max_abs_diff/peak> rms_diff_db=<20 log10 (rms (a - b) / rms (a))>

and a verdict line. Exit code 0 if every ratio is <= --tol, 1 if not, 2 on bad input.

    python3 -I scripts/render-diff.py DIR_A DIR_B [--filter TEXT] [--tol 1e-4]
    python3 -I scripts/render-diff.py --selftest

Standard library only.
"""

import argparse
import array
import math
import os
import struct
import sys
import tempfile


def read_float_wav(path):
    """The samples (all channels, interleaved) of a 32-bit float WAV as an array('f')."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 12 or data[0:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("%s: not a RIFF/WAVE file" % path)
    pos = 12
    fmt = None
    samples = None
    while pos + 8 <= len(data):
        tag = data[pos:pos + 4]
        size = struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if tag == b"fmt ":
            if len(body) < 16:
                raise ValueError("%s: short fmt chunk" % path)
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif tag == b"data":
            samples = body
        pos += 8 + size + (size & 1)
    if fmt is None or samples is None:
        raise ValueError("%s: no fmt or data chunk" % path)
    tag_code, _channels, _rate, _byte_rate, _align, bits = fmt
    if bits != 32 or tag_code not in (3, 0xFFFE):
        raise ValueError("%s: not a 32 bit float WAV (format %d, %d bits)" % (path, tag_code, bits))
    out = array.array("f")
    out.frombytes(samples[: len(samples) - (len(samples) % 4)])
    if sys.byteorder == "big":
        out.byteswap()
    return out


def write_float_wav(path, values, rate=44100, channels=1):
    body = array.array("f", values)
    if sys.byteorder == "big":
        body.byteswap()
    payload = body.tobytes()
    header = b"RIFF" + struct.pack("<I", 36 + len(payload)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 3, channels, rate, rate * channels * 4, channels * 4, 32)
    header += b"data" + struct.pack("<I", len(payload))
    with open(path, "wb") as f:
        f.write(header + payload)


def compare(a, b):
    """(max_abs_diff, peak, ratio, rms_diff_db) of two sample arrays of the same length."""
    n = min(len(a), len(b))
    max_diff = 0.0
    peak = 0.0
    sum_d2 = 0.0
    sum_a2 = 0.0
    for i in range(n):
        x = a[i]
        d = abs(x - b[i])
        if d > max_diff:
            max_diff = d
        ax = abs(x)
        if ax > peak:
            peak = ax
        sum_d2 += d * d
        sum_a2 += x * x
    ratio = max_diff / peak if peak > 0.0 else (0.0 if max_diff == 0.0 else math.inf)
    if sum_d2 == 0.0:
        db = -math.inf
    elif sum_a2 == 0.0:
        db = math.inf
    else:
        db = 20.0 * math.log10(math.sqrt(sum_d2 / n) / math.sqrt(sum_a2 / n))
    return max_diff, peak, ratio, db


def run(dir_a, dir_b, text, tol, out=sys.stdout):
    if not os.path.isdir(dir_a) or not os.path.isdir(dir_b):
        print("render-diff: both arguments must be folders", file=sys.stderr)
        return 2
    names = sorted(n for n in os.listdir(dir_a) if n.lower().endswith(".wav") and (not text or text in n) and os.path.isfile(os.path.join(dir_b, n)))
    if not names:
        print("render-diff: no file name is in both folders", file=sys.stderr)
        return 2
    worst = 0.0
    bad = 0
    for name in names:
        try:
            a = read_float_wav(os.path.join(dir_a, name))
            b = read_float_wav(os.path.join(dir_b, name))
        except (OSError, ValueError, struct.error) as e:
            print("render-diff: %s" % e, file=sys.stderr)
            return 2
        if len(a) != len(b):
            print("%s length-differs a=%d b=%d ratio=inf rms_diff_db=inf" % (name, len(a), len(b)), file=out)
            bad += 1
            worst = math.inf
            continue
        max_diff, peak, ratio, db = compare(a, b)
        print("%s %.9g %.9g ratio=%.6g rms_diff_db=%s" % (name, max_diff, peak, ratio, "-inf" if db == -math.inf else ("%.2f" % db)), file=out)
        worst = max(worst, ratio)
        if ratio > tol:
            bad += 1
    verdict = "OK" if bad == 0 else "DIFFERENT"
    print("render-diff: %s files=%d over_tolerance=%d worst_ratio=%.6g tol=%g" % (verdict, len(names), bad, worst, tol), file=out)
    return 0 if bad == 0 else 1


def selftest():
    with tempfile.TemporaryDirectory() as tmp:
        da, db_, dc = (os.path.join(tmp, n) for n in ("a", "b", "c"))
        for d in (da, db_, dc):
            os.mkdir(d)
        base = [0.0] * 1000
        base[10] = 1.0
        base[500] = 0.25
        changed = list(base)
        changed[500] = 0.25 + 2.0 ** -10  # exact in float32
        write_float_wav(os.path.join(da, "x.wav"), base)
        write_float_wav(os.path.join(db_, "x.wav"), base)
        write_float_wav(os.path.join(dc, "x.wav"), changed)
        a = read_float_wav(os.path.join(da, "x.wav"))
        b = read_float_wav(os.path.join(db_, "x.wav"))
        c = read_float_wav(os.path.join(dc, "x.wav"))
        md, peak, ratio, db = compare(a, b)
        assert md == 0.0 and ratio == 0.0 and db == -math.inf, (md, ratio, db)
        md, peak, ratio, db = compare(a, c)
        assert peak == 1.0 and md == 2.0 ** -10 and ratio == 2.0 ** -10, (md, peak, ratio)
        assert run(da, db_, "", 1e-4, out=open(os.devnull, "w")) == 0
        assert run(da, dc, "", 1e-4, out=open(os.devnull, "w")) == 1
        assert run(da, dc, "", 1e-2, out=open(os.devnull, "w")) == 0
    print("render-diff selftest ok")
    return 0


def main(argv):
    p = argparse.ArgumentParser(description="Size of the difference between two folders of 32 bit float renders.")
    p.add_argument("dir_a", nargs="?")
    p.add_argument("dir_b", nargs="?")
    p.add_argument("--filter", default="", help="only the files whose name contains TEXT")
    p.add_argument("--tol", type=float, default=1e-4, help="largest accepted max_abs_diff / peak (default 1e-4)")
    p.add_argument("--selftest", action="store_true")
    args = p.parse_args(argv)
    if args.selftest:
        return selftest()
    if not args.dir_a or not args.dir_b:
        p.print_usage(sys.stderr)
        return 2
    return run(args.dir_a, args.dir_b, args.filter, args.tol)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
