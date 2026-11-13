#!/usr/bin/env python3
# SynthyBoy - packages the built MPC plugin as SynthyBoy-MPC-<version>.zip (stdlib only).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage:
#   scripts/package-mpc-release.py --plugin-dir "<the built folder>" --out <dir> --source-url <url> [--version X]
#
# Validates the built folder:
#   - plugin.json with file32 and file64
#   - VST/ binaries exist and are ELF 32-bit ARM / ELF 64-bit AArch64
#   - version.xml present
#   - Plugin Skins/ with TUI.json and Q-Links.json
#   - Content/presets/ with at least 100 *.sbpreset files
#
# Writes SynthyBoy-MPC-<version>.zip with exactly one top-level folder,
# containing the plugin files, INSTALL.txt, and LICENSE.
# Produces reproducible output (no macOS metadata, sorted entries, fixed timestamps).
# Also prints the path, size and sha256, and writes <zip>.sha256.

import argparse
import hashlib
import json
import os
import stat
import struct
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIXED_DATE = (1980, 1, 1, 0, 0, 0)

EM_ARM = 40
EM_AARCH64 = 183
ELFCLASS32 = 1
ELFCLASS64 = 2

INSTALL_TEMPLATE = """SynthyBoy for Akai MPC
======================

Supported hardware:
- 32-bit: MPC X, X SE, Live, Live II, One, One+, Key 61, Key 37, Force
- 64-bit: MPC Live III, MPC XL

Requirements:
- MPC OS 3.10.1 or later. Older MPC OS versions cannot load external plugins.
- On the MPC, open Preferences > Enhancements (Beta) and turn on both switches:
    1. Enable Enhancements
    2. External Plugin Support
  The plugin remains hidden until both switches are turned on.

Installation:
1. Copy the folder ("{folder}") into a folder named exactly "VST2 Plugins"
   in the root of the internal drive or an attached drive (USB drive or SD card).
   Example:
       <drive>/VST2 Plugins/{folder}/plugin.json
       <drive>/VST2 Plugins/{folder}/version.xml
       <drive>/VST2 Plugins/{folder}/VST/...
2. Restart the MPC (or reboot) so the host discovers the newly added plugin.

Where the instrument appears:
- The instrument appears in the plugin browser under "External Plugins".
  Create a Plugin program on an instrument track and select SynthyBoy.

How to remove:
- Delete the "{folder}" folder from "VST2 Plugins".
  Projects that used the plugin will open and load without it.

Important notes and stability:
- Third-party plugins are not supported by Akai and can crash the MPC.
  Save your work often, especially before loading or editing plugins.

License:
- Licensed under the GNU Affero General Public License v3 or later (AGPL-3.0-or-later).
  See the LICENSE file in this folder for the full license text.
- Source code is available at:
  <SOURCE_URL>

Disclaimer:
Not affiliated with or endorsed by Akai Professional / inMusic Brands.
"""


class PackageError(Exception):
    pass


def check_elf_binary(path: Path, expected_bits: int, expected_machine: int, arch_name: str):
    """Verifies that path is an ELF binary with matching class and machine."""
    if not path.is_file():
        raise PackageError(f"missing binary: {path}")
    with open(path, "rb") as f:
        head = f.read(64)
    if len(head) < 52 or head[:4] != b"\x7fELF":
        raise PackageError(f"{path}: not an ELF binary")

    ei_class = head[4]
    bits = 32 if ei_class == ELFCLASS32 else (64 if ei_class == ELFCLASS64 else 0)
    if bits != expected_bits:
        class_str = f"{bits}-bit" if bits else f"unknown ({ei_class})"
        raise PackageError(
            f"{path}: wrong ELF class: expected {expected_bits}-bit for {arch_name}, got {class_str}"
        )

    endian = "<" if head[5] == 1 else ">"
    machine = struct.unpack_from(endian + "H", head, 18)[0]
    if machine != expected_machine:
        raise PackageError(
            f"{path}: wrong ELF machine: expected {expected_machine} for {arch_name}, got {machine}"
        )


def validate_plugin_folder(plugin_dir: Path) -> dict:
    """Checks the plugin folder structure, binaries, metadata, skins and presets."""
    if not plugin_dir.is_dir():
        raise PackageError(f"plugin directory not found or not a directory: {plugin_dir}")

    manifest_path = plugin_dir / "plugin.json"
    if not manifest_path.is_file():
        raise PackageError(f"plugin.json missing from {plugin_dir}")

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except ValueError as e:
        raise PackageError(f"plugin.json in {plugin_dir} is not valid JSON: {e}")

    if not isinstance(manifest, dict):
        raise PackageError(f"plugin.json in {plugin_dir} is not a JSON object")

    if "file32" not in manifest or not manifest["file32"]:
        raise PackageError(f"plugin.json without file32 in {manifest_path}")

    if "file64" not in manifest or not manifest["file64"]:
        raise PackageError(f"plugin.json without file64 in {manifest_path}")

    bin32 = plugin_dir / manifest["file32"]
    if not bin32.is_file():
        raise PackageError(f"missing binary: 32-bit ARM binary not found at {bin32}")
    check_elf_binary(bin32, 32, EM_ARM, "32-bit ARM")

    bin64 = plugin_dir / manifest["file64"]
    if not bin64.is_file():
        raise PackageError(f"missing binary: 64-bit AArch64 binary not found at {bin64}")
    check_elf_binary(bin64, 64, EM_AARCH64, "64-bit AArch64")

    version_xml = plugin_dir / "version.xml"
    if not version_xml.is_file():
        raise PackageError(f"version.xml missing from {plugin_dir}")

    skins_dir = plugin_dir / "Plugin Skins"
    tui = skins_dir / "TUI.json"
    if not tui.is_file():
        raise PackageError(f"skin file TUI.json missing from {skins_dir}")

    qlinks = skins_dir / "Q-Links.json"
    if not qlinks.is_file():
        raise PackageError(f"skin file Q-Links.json missing from {skins_dir}")

    presets_dir = plugin_dir / "Content" / "presets"
    if not presets_dir.is_dir():
        raise PackageError(f"Content/presets directory missing from {plugin_dir}")

    presets = [
        p
        for p in presets_dir.rglob("*.sbpreset")
        if not (p.name.startswith("._") or p.name == ".DS_Store")
    ]
    if len(presets) < 100:
        raise PackageError(
            f"fewer than 100 presets: found {len(presets)} *.sbpreset under {presets_dir}"
        )

    return manifest


def generate_install_text(folder_name: str, source_url: str) -> str:
    """Renders INSTALL.txt with the folder name and source URL."""
    text = INSTALL_TEMPLATE.format(folder=folder_name)
    return text.replace("<SOURCE_URL>", source_url)


def package_mpc(
    plugin_dir: Path,
    out_dir: Path,
    source_url: str,
    version: str = None,
    license_path: Path = None,
    repo_root: Path = ROOT,
) -> dict:
    """Validates the plugin folder and creates the release zip archive and sha256 checksum."""
    plugin_dir = Path(str(plugin_dir).rstrip("/\\")).resolve()
    out_dir = Path(out_dir).resolve()

    manifest = validate_plugin_folder(plugin_dir)

    if not source_url or not source_url.strip():
        raise PackageError("--source-url is required and must not be empty")
    source_url = source_url.strip()

    if not version:
        version = manifest.get("version")
        if not version:
            root_manifest = repo_root / "plugin.json"
            if root_manifest.is_file():
                try:
                    version = json.loads(root_manifest.read_text(encoding="utf-8")).get("version")
                except Exception:
                    pass
    if not version:
        raise PackageError("version could not be determined; specify --version")
    version = str(version).strip()

    # Determine LICENSE
    if license_path:
        lic_file = Path(license_path).resolve()
    elif (plugin_dir / "LICENSE").is_file():
        lic_file = (plugin_dir / "LICENSE").resolve()
    elif (repo_root / "LICENSE").is_file():
        lic_file = (repo_root / "LICENSE").resolve()
    else:
        raise PackageError("LICENSE file not found in plugin dir or repo root")

    if not lic_file.is_file():
        raise PackageError(f"LICENSE file not found: {lic_file}")

    license_bytes = lic_file.read_bytes()

    folder_name = plugin_dir.name
    install_content = generate_install_text(folder_name=folder_name, source_url=source_url)
    install_bytes = install_content.encode("utf-8")

    entries = []

    # Collect files from plugin_dir, excluding macOS metadata files and duplicates of INSTALL/LICENSE
    for dirpath, dirnames, filenames in os.walk(plugin_dir):
        dirnames[:] = [d for d in dirnames if not (d.startswith("._") or d == ".DS_Store")]
        dirnames.sort()
        for f in sorted(filenames):
            if f.startswith("._") or f == ".DS_Store":
                continue
            if f in ("INSTALL.txt", "LICENSE"):
                continue
            full = Path(dirpath) / f
            rel = full.relative_to(plugin_dir).as_posix()
            arcname = f"{folder_name}/{rel}"
            mode = 0o755 if (full.stat().st_mode & 0o111) else 0o644
            entries.append((arcname, full, mode))

    entries.append((f"{folder_name}/INSTALL.txt", install_bytes, 0o644))
    entries.append((f"{folder_name}/LICENSE", license_bytes, 0o644))

    # Sort entries deterministically
    entries.sort(key=lambda e: e[0])

    zip_filename = f"SynthyBoy-MPC-{version}.zip"
    out_dir.mkdir(parents=True, exist_ok=True)
    target = out_dir / zip_filename
    tmp = target.with_name(zip_filename + ".part")

    try:
        with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, allowZip64=True) as zf:
            for arcname, item, mode in entries:
                info = zipfile.ZipInfo(arcname, date_time=FIXED_DATE)
                info.create_system = 3  # Unix
                if isinstance(item, Path) and item.is_symlink():
                    info.external_attr = (stat.S_IFLNK | 0o777) << 16
                    zf.writestr(info, os.readlink(item))
                    continue
                info.external_attr = (stat.S_IFREG | mode) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                if isinstance(item, bytes):
                    zf.writestr(info, item, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
                else:
                    with open(item, "rb") as sf:
                        zf.writestr(info, sf.read(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
        os.replace(tmp, target)
    finally:
        if tmp.exists():
            tmp.unlink()

    h = hashlib.sha256()
    with open(target, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    digest = h.hexdigest()

    sha_file = out_dir / f"{zip_filename}.sha256"
    sha_file.write_text(f"{digest}  {zip_filename}\n", encoding="utf-8")

    return {
        "file": zip_filename,
        "path": target,
        "size": target.stat().st_size,
        "sha256": digest,
        "sha_path": sha_file,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Packages the built MPC plugin folder as SynthyBoy-MPC-<version>.zip"
    )
    parser.add_argument("--plugin-dir", required=True, help="the built MPC plugin folder")
    parser.add_argument("--out", required=True, help="output directory for the release zip")
    parser.add_argument(
        "--source-url",
        required=True,
        help="source code repository URL for AGPL-3.0-or-later compliance",
    )
    parser.add_argument("--version", help="release version (default: from plugin.json)")
    parser.add_argument("--license", help="path to LICENSE file (default: from repo or plugin dir)")

    args = parser.parse_args(argv)

    try:
        result = package_mpc(
            plugin_dir=Path(args.plugin_dir),
            out_dir=Path(args.out),
            source_url=args.source_url,
            version=args.version,
            license_path=Path(args.license) if args.license else None,
        )
    except (PackageError, OSError, ValueError) as e:
        print(f"package-mpc-release: {e}", file=sys.stderr)
        return 1

    print(f"{result['path']}  {result['size']} bytes  sha256 {result['sha256']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
