#!/usr/bin/env python3
# SynthyBoy - Akai MPC .xpl plugin binary path rewrite tool
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
Rewrites the file= attribute of the <PLUGIN ...> element in every .xpl file
within a specified folder to a target plugin binary path.

Preserves the remainder of each file byte-for-byte, including CRLF line endings,
XML formatting, attribute ordering, preset names, and JUCE base64 state data.

Usage:
    python3 scripts/mpc-fix-xpl-paths.py --dir <directory> --path <plugin-path> [--dry-run] [--quiet]
"""

import argparse
import re
import sys
from pathlib import Path
from typing import Optional, Tuple

PLUGIN_FILE_RE = re.compile(r'(<PLUGIN\b[^>]*?\bfile=")([^"]*)(")', re.DOTALL)


def rewrite_xpl_content(content: str, new_path: str) -> Tuple[str, bool]:
    """Rewrite the file= attribute inside <PLUGIN ...> to new_path.

    Returns (new_content, changed). If file= is not found, raises ValueError.
    """
    match = PLUGIN_FILE_RE.search(content)
    if not match:
        raise ValueError('No <PLUGIN ... file="..."> element found in .xpl content')

    old_path = match.group(2)
    if old_path == new_path:
        return content, False

    def repl(m: re.Match) -> str:
        return m.group(1) + new_path + m.group(3)

    new_content, count = PLUGIN_FILE_RE.subn(repl, content, count=1)
    return new_content, (count > 0 and old_path != new_path)


def fix_xpl_file(file_path: Path, new_path: str, dry_run: bool = False) -> bool:
    """Fix the plugin path in a single .xpl file.

    Returns True if the file was modified (or would be modified in dry-run).
    """
    raw_bytes = file_path.read_bytes()
    content = raw_bytes.decode("utf-8")
    new_content, changed = rewrite_xpl_content(content, new_path)
    if changed and not dry_run:
        file_path.write_bytes(new_content.encode("utf-8"))
    return changed


def fix_xpl_directory(
    dir_path: Path,
    new_path: str,
    dry_run: bool = False,
    recursive: bool = True,
) -> Tuple[int, int]:
    """Rewrite file= attribute in all .xpl files in dir_path.

    Returns (total_files_scanned, modified_files_count).
    """
    if not dir_path.is_dir():
        raise NotADirectoryError(f"Directory does not exist: {dir_path}")

    files = sorted(dir_path.rglob("*.xpl") if recursive else dir_path.glob("*.xpl"))
    modified_count = 0
    for f in files:
        if fix_xpl_file(f, new_path, dry_run=dry_run):
            modified_count += 1
    return len(files), modified_count


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--dir",
        type=Path,
        required=True,
        help="Directory containing .xpl files to update",
    )
    parser.add_argument(
        "--path",
        type=str,
        required=True,
        help="New plugin binary path to write into <PLUGIN file=...>",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Simulate the changes without modifying files on disk",
    )
    parser.add_argument(
        "--no-recursive",
        action="store_true",
        help="Only search the specified directory, not subdirectories",
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Suppress normal output",
    )

    args = parser.parse_args(argv)

    if not args.dir.is_dir():
        sys.stderr.write(f"mpc-fix-xpl-paths: error: not a directory: {args.dir}\n")
        return 1

    try:
        total, modified = fix_xpl_directory(
            args.dir,
            args.path,
            dry_run=args.dry_run,
            recursive=not args.no_recursive,
        )
    except Exception as e:
        sys.stderr.write(f"mpc-fix-xpl-paths: error: {e}\n")
        return 1

    if not args.quiet:
        dry_str = " (dry run)" if args.dry_run else ""
        print(f"mpc-fix-xpl-paths: updated {modified}/{total} .xpl files in {args.dir}{dry_str} -> {args.path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
