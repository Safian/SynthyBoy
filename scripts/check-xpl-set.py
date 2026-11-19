#!/usr/bin/env python3
"""Checker for Akai MPC .xpl preset files."""
import argparse
import glob
import importlib.util
import json
import os
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("gen_mpc_xpl", HERE / "gen-mpc-xpl.py")
gen_mpc_xpl = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gen_mpc_xpl)

BANK_HEADER_SIZE = gen_mpc_xpl.BANK_HEADER_SIZE
FILENAME_RE = re.compile(r"^(\d{3})-([^-]+)-(.+)\.xpl$")


def main():
    ap = argparse.ArgumentParser(description="Validate a set of .xpl preset files.")
    ap.add_argument("dir", help="directory containing .xpl files")
    ap.add_argument("--expect-count", type=int, default=None)
    ap.add_argument("--expect-programs", type=int, default=None)
    ap.add_argument("--plugin-path", default=None)
    args = ap.parse_args()

    dirpath = Path(args.dir)
    if not dirpath.is_dir():
        sys.stderr.write(f"set: directory not found: {args.dir}\n")
        sys.exit(2)

    xpl_files = sorted(glob.glob(os.path.join(args.dir, "*.xpl")))
    findings = []

    # --expect-count check
    if args.expect_count is not None:
        if len(xpl_files) != args.expect_count:
            findings.append(f"set: count is {len(xpl_files)}, expected {args.expect_count}")

    # Check for duplicate numbers
    all_numbers = []
    for fpath in xpl_files:
        fname = os.path.basename(fpath)
        m = FILENAME_RE.match(fname)
        if not m:
            continue
        all_numbers.append(m.group(1))

    dup_numbers = set()
    seen = set()
    for n in all_numbers:
        if n in seen:
            dup_numbers.add(n)
        seen.add(n)

    if dup_numbers:
        for dn in sorted(dup_numbers):
            findings.append(f"set: duplicate number {dn}")

    # Process each file
    for fpath in xpl_files:
        fname = os.path.basename(fpath)
        m = FILENAME_RE.match(fname)
        if not m:
            continue  # already reported

        number, category, preset_name = m.group(1), m.group(2), m.group(3)

        # Read raw bytes
        try:
            raw = Path(fpath).read_bytes()
        except Exception as e:
            findings.append(f"{fname}: cannot read file: {e}")
            continue

        # Check CRLF line endings
        has_bad_line_end = False
        for i in range(1, len(raw)):
            if raw[i] == 0x0A and raw[i - 1] != 0x0D:
                has_bad_line_end = True
                break
        if has_bad_line_end:
            findings.append(f"{fname}: line ends are not CRLF")

        # Decode as UTF-8
        try:
            text = raw.decode("utf-8")
        except Exception as e:
            findings.append(f"{fname}: cannot parse: {e}")
            continue

        # Parse with parse_xpl
        try:
            parsed = gen_mpc_xpl.parse_xpl(text)
        except Exception as e:
            findings.append(f"{fname}: cannot parse: {e}")
            continue

        # Check state length
        state_len = len(parsed["state_bytes"])
        expected_len = BANK_HEADER_SIZE + parsed["header"]["chunk_size"]
        if state_len != expected_len:
            findings.append(
                f"{fname}: state length {state_len} does not match the header ({expected_len})"
            )

        # Check header magic
        hdr = parsed["header"]
        if hdr["chunk_magic"] != b"CcnK" or hdr["fx_magic"] != b"FBCh":
            findings.append(f"{fname}: bad bank header")

        # Check chunk is valid UTF-8 JSON dict
        try:
            chunk_json = json.loads(parsed["chunk_bytes"].decode("utf-8"))
            if not isinstance(chunk_json, dict):
                findings.append(f"{fname}: chunk is not JSON")
                continue
        except Exception:
            findings.append(f"{fname}: chunk is not JSON")
            continue

        # Check preset name matches file name
        if parsed["preset"] != preset_name:
            findings.append(
                f"{fname}: file name does not end with the preset name '{parsed['preset']}' "
                f"(file has '{preset_name}')"
            )

        # --expect-programs check
        if args.expect_programs is not None:
            if hdr["num_programs"] != args.expect_programs:
                findings.append(
                    f"{fname}: numPrograms is {hdr['num_programs']}, expected {args.expect_programs}"
                )

        # --plugin-path check
        if args.plugin_path is not None:
            file_attr = parsed["plugin_attrs"].get("file", "")
            if file_attr != args.plugin_path:
                findings.append(
                    f"{fname}: file= is '{file_attr}', expected '{args.plugin_path}'"
                )

    # Output findings
    if findings:
        for f in findings:
            print(f)
        sys.exit(1)
    else:
        print(f"{len(xpl_files)} files OK")
        sys.exit(0)


if __name__ == "__main__":
    main()
