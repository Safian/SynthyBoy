#!/usr/bin/env python3
# SynthyBoy - packages the built MPC plugin as architecture-specific archives using the production native-skin validator.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage:
#   scripts/package-mpc-release.py --plugin-dir "<the built folder>" --out <dir> --source-url <url> [--arch all|armv7|aarch64|combined] [--version X]
#
# Validates the built folder:
#   - plugin.json with file32 (for armv7) and/or file64 (for aarch64)
#   - VST/ binaries exist and are ELF 32-bit ARM / ELF 64-bit AArch64
#   - version.xml present
#   - Plugin Skins/ with TUI.json and Q-Links.json
#   - Content/presets/ with at least 100 *.sbpreset files
#
# Release packaging produces TWO separate archives by default (--arch all):
#   - SynthyBoy-MPC-<version>-armv7.zip (for 32-bit MPCs: Key 61, Live, One, X, etc.)
#   - SynthyBoy-MPC-<version>-aarch64.zip (for 64-bit MPCs: XL, Live III)
# Each archive contains exactly one top-level plugin folder holding the matching
# architecture binary, display helper (if present), skins, presets, INSTALL.txt, and LICENSE.
# Produces reproducible output (no macOS metadata, sorted entries, fixed timestamps).
# Also prints path, size, and sha256 for each archive, and writes <zip>.sha256.

import argparse
import hashlib
import json
import os
import re
import stat
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIXED_DATE = (1980, 1, 1, 0, 0, 0)

EM_ARM = 40
EM_AARCH64 = 183
ELFCLASS32 = 1
ELFCLASS64 = 2

ARCH_ARMV7 = "armv7"
ARCH_AARCH64 = "aarch64"
ARCH_ALL = "all"
ARCH_COMBINED = "combined"

ARCH_ALIASES = {
    "arm32": ARCH_ARMV7,
    "arm64": ARCH_AARCH64,
}

ARCH_SPECS = {
    ARCH_ARMV7: {
        "key": "file32",
        "bits": 32,
        "machine": EM_ARM,
        "name": "32-bit ARM",
        "helper": "synthyboy-overlay-gpu-arm32",
        "helper_name": "32-bit ARM GPU helper",
        "exclude_key": "file64",
        "exclude_bins": {"synthyboy-arm64.so", "synthyboy-overlay-gpu-arm64"},
        "suffix": "-armv7",
    },
    ARCH_AARCH64: {
        "key": "file64",
        "bits": 64,
        "machine": EM_AARCH64,
        "name": "64-bit AArch64",
        "helper": "synthyboy-overlay-gpu-arm64",
        "helper_name": "64-bit AArch64 GPU helper",
        "exclude_key": "file32",
        "exclude_bins": {"synthyboy-arm32.so", "synthyboy-overlay-gpu-arm32"},
        "suffix": "-aarch64",
    },
}

INSTALL_TEMPLATE = """SynthyBoy for Akai MPC
======================

Supported hardware:
{supported_hardware}

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
- The instrument appears in the instrument list of a plugin track as "SynthyBoy"
  (on MPC OS 3.10.1: Sounds > Instruments, last page).
  Create a Plugin program on an instrument track and select SynthyBoy.

How to remove:
- Delete the "{folder}" folder from "VST2 Plugins".

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

    if head[5] != 1:
        raise PackageError(
            f"{path}: wrong ELF endianness: expected little-endian for {arch_name}"
        )

    machine = struct.unpack_from("<H", head, 18)[0]
    if machine != expected_machine:
        raise PackageError(
            f"{path}: wrong ELF machine: expected {expected_machine} for {arch_name}, got {machine}"
        )


def validate_plugin_folder(plugin_dir: Path, arch: str = ARCH_ALL) -> dict:
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

    arch = ARCH_ALIASES.get(arch, arch)

    if arch in (ARCH_ALL, ARCH_COMBINED):
        arch_keys = [ARCH_ARMV7, ARCH_AARCH64]
    elif arch in ARCH_SPECS:
        arch_keys = [arch]
    else:
        raise PackageError(f"unknown architecture: {arch}")

    for a_key in arch_keys:
        spec = ARCH_SPECS[a_key]
        key = spec["key"]
        expected_bits = spec["bits"]
        expected_mach = spec["machine"]
        name = spec["name"]

        if key not in manifest or not isinstance(manifest[key], str) or not manifest[key].strip():
            if arch == ARCH_ALL:
                other_name = ARCH_ARMV7 if key == "file64" else ARCH_AARCH64
                raise PackageError(
                    f"plugin.json without {key} in {manifest_path} (--arch all requires both binaries; specify --arch {other_name} to package single architecture)"
                )
            raise PackageError(f"plugin.json without {key} in {manifest_path}")

        rel_str = manifest[key].strip()
        rel_path = Path(rel_str)
        if rel_path.is_absolute() or ".." in rel_path.parts:
            raise PackageError(
                f"plugin.json {key} must be a relative path inside plugin folder: {rel_str}"
            )
        bin_path = plugin_dir / rel_path
        if not bin_path.is_file():
            raise PackageError(f"missing binary: {name} binary not found at {bin_path}")
        check_elf_binary(bin_path, expected_bits, expected_mach, name)

        helper = plugin_dir / "VST" / spec["helper"]
        if helper.is_file():
            check_elf_binary(helper, expected_bits, expected_mach, spec["helper_name"])
            if not helper.stat().st_mode & 0o111:
                raise PackageError(f"{helper}: the GPU helper is not executable")

    version_xml = plugin_dir / "version.xml"
    if not version_xml.is_file():
        raise PackageError(f"version.xml missing from {plugin_dir}")

    skins_dir = plugin_dir / "Plugin Skins"
    if not skins_dir.is_dir():
        raise PackageError(f"Plugin Skins directory missing from {plugin_dir}")

    tui = skins_dir / "TUI.json"
    if not tui.is_file():
        raise PackageError(f"skin file TUI.json missing from {skins_dir}")

    qlinks = skins_dir / "Q-Links.json"
    if not qlinks.is_file():
        raise PackageError(f"skin file Q-Links.json missing from {skins_dir}")

    skin_check = subprocess.run([sys.executable, str(ROOT / "scripts/validate_synthyboy_skin.py"), str(plugin_dir)],
                                capture_output=True, text=True)
    if skin_check.returncode:
        raise PackageError(skin_check.stderr.strip())

    presets_dir = plugin_dir / "Content" / "presets"
    if not presets_dir.is_dir():
        raise PackageError(f"Content/presets directory missing from {plugin_dir}")

    presets = [
        p
        for p in presets_dir.rglob("*.sbpreset")
        if p.is_file()
        and not any(
            part.startswith("._") or part == ".DS_Store"
            for part in p.relative_to(plugin_dir).parts
        )
    ]
    if len(presets) < 100:
        raise PackageError(
            f"fewer than 100 presets: found {len(presets)} *.sbpreset under {presets_dir}"
        )

    return manifest


def generate_install_text(folder_name: str, source_url: str, arch: str = None) -> str:
    """Renders INSTALL.txt with the folder name, source URL, and architecture."""
    arch = ARCH_ALIASES.get(arch, arch)
    if arch == ARCH_ARMV7:
        hw = "- 32-bit: MPC X, X SE, Live, Live II, One, One+, Key 61, Key 37, Force"
    elif arch == ARCH_AARCH64:
        hw = "- 64-bit: MPC Live III, MPC XL"
    else:
        hw = (
            "- 32-bit: MPC X, X SE, Live, Live II, One, One+, Key 61, Key 37, Force\n"
            "- 64-bit: MPC Live III, MPC XL"
        )
    return (
        INSTALL_TEMPLATE.replace("{folder}", folder_name)
        .replace("<SOURCE_URL>", source_url)
        .replace("{supported_hardware}", hw)
    )


def package_single_arch_zip(
    plugin_dir: Path,
    out_dir: Path,
    source_url: str,
    version: str,
    folder_name: str,
    license_bytes: bytes,
    manifest: dict,
    target_arch: str,
) -> dict:
    """Creates a reproducible zip for a single target architecture (armv7, aarch64, or combined)."""
    spec = ARCH_SPECS.get(target_arch)
    if spec:
        suffix = spec["suffix"]
        exclude_bins = spec["exclude_bins"]
        clean_manifest = {k: v for k, v in manifest.items() if k != spec["exclude_key"]}
    else:
        suffix = ""
        exclude_bins = set()
        clean_manifest = manifest

    manifest_bytes = json.dumps(clean_manifest, indent=2, ensure_ascii=False).encode("utf-8") + b"\n"
    install_content = generate_install_text(folder_name=folder_name, source_url=source_url, arch=target_arch)
    install_bytes = install_content.encode("utf-8")

    entries = []

    # Collect files from plugin_dir, excluding macOS metadata files, excluded binaries, and root duplicates
    for dirpath, dirnames, filenames in os.walk(plugin_dir):
        dirnames[:] = [d for d in dirnames if not (d.startswith("._") or d == ".DS_Store")]
        dirnames.sort()
        names_to_process = sorted(
            filenames + [d for d in dirnames if (Path(dirpath) / d).is_symlink()]
        )
        for f in names_to_process:
            if f.startswith("._") or f == ".DS_Store":
                continue
            if Path(dirpath) == plugin_dir and f in ("INSTALL.txt", "LICENSE", "plugin.json"):
                continue
            if f in exclude_bins:
                continue
            full = Path(dirpath) / f
            rel = full.relative_to(plugin_dir).as_posix()
            arcname = f"{folder_name}/{rel}"
            is_link = full.is_symlink()
            st = full.lstat() if is_link else full.stat()
            mode = 0o777 if is_link else (0o755 if (st.st_mode & 0o111) else 0o644)
            entries.append((arcname, full, mode))

    entries.append((f"{folder_name}/plugin.json", manifest_bytes, 0o644))
    entries.append((f"{folder_name}/INSTALL.txt", install_bytes, 0o644))
    entries.append((f"{folder_name}/LICENSE", license_bytes, 0o644))

    # Sort entries deterministically
    entries.sort(key=lambda e: e[0])

    zip_filename = f"SynthyBoy-MPC-{version}{suffix}.zip"
    out_dir.mkdir(parents=True, exist_ok=True)
    target = out_dir / zip_filename
    tmp = target.with_name(zip_filename + ".part")

    try:
        with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, allowZip64=True) as zf:
            for arcname, item, mode in entries:
                info = zipfile.ZipInfo(arcname, date_time=FIXED_DATE)
                info.create_system = 3  # Unix
                info.compress_type = zipfile.ZIP_DEFLATED
                if isinstance(item, Path) and item.is_symlink():
                    info.external_attr = (stat.S_IFLNK | 0o777) << 16
                    zf.writestr(
                        info,
                        os.readlink(item),
                        compress_type=zipfile.ZIP_DEFLATED,
                        compresslevel=9,
                    )
                    continue
                info.external_attr = (stat.S_IFREG | mode) << 16
                if isinstance(item, bytes):
                    zf.writestr(info, item, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
                else:
                    with open(item, "rb") as sf:
                        zf.writestr(
                            info, sf.read(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9
                        )
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


def package_mpc(
    plugin_dir: Path,
    out_dir: Path,
    source_url: str,
    version: str = None,
    license_path: Path = None,
    arch: str = ARCH_ALL,
    repo_root: Path = ROOT,
) -> list:
    """Validates the plugin folder and creates the release zip archive(s) and sha256 checksum(s)."""
    plugin_dir = Path(str(plugin_dir).rstrip("/\\")).resolve()
    out_dir = Path(out_dir).resolve()

    if not plugin_dir.exists():
        raise PackageError(f"plugin directory not found: {plugin_dir}")

    try:
        out_dir.relative_to(plugin_dir)
        raise PackageError(f"--out directory ({out_dir}) cannot be inside --plugin-dir ({plugin_dir})")
    except ValueError:
        pass

    arch = ARCH_ALIASES.get(arch, arch)
    if arch not in (ARCH_ARMV7, ARCH_AARCH64, ARCH_ALL, ARCH_COMBINED):
        raise PackageError(f"invalid arch: {arch}; choices are armv7, aarch64, all, combined, arm32, arm64")

    manifest = validate_plugin_folder(plugin_dir, arch=arch)

    if not source_url or not source_url.strip():
        raise PackageError("--source-url is required and must not be empty")
    source_url = source_url.strip()

    if version is not None and not str(version).strip():
        version = None
    if not version:
        version_xml = plugin_dir / "version.xml"
        if version_xml.is_file():
            m = re.search(r"<version>\s*(\d+\.\d+\.\d+)\.\d+\s*</version>", version_xml.read_text(encoding="utf-8"))
            if m:
                version = m.group(1)
    if not version:
        raise PackageError("version could not be determined; specify --version")
    version = str(version).strip()
    if "/" in version or "\\" in version or "\0" in version:
        raise PackageError(f"invalid version string: {version}")

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
    if not folder_name or folder_name in (".", ".."):
        raise PackageError(f"invalid plugin directory name: {plugin_dir}")
    if folder_name.startswith("._") or folder_name == ".DS_Store":
        raise PackageError(f"plugin directory name cannot be macOS metadata: {folder_name}")

    if arch == ARCH_ALL:
        results = [
            package_single_arch_zip(
                plugin_dir=plugin_dir,
                out_dir=out_dir,
                source_url=source_url,
                version=version,
                folder_name=folder_name,
                license_bytes=license_bytes,
                manifest=manifest,
                target_arch=ARCH_ARMV7,
            ),
            package_single_arch_zip(
                plugin_dir=plugin_dir,
                out_dir=out_dir,
                source_url=source_url,
                version=version,
                folder_name=folder_name,
                license_bytes=license_bytes,
                manifest=manifest,
                target_arch=ARCH_AARCH64,
            ),
        ]
    else:
        results = [
            package_single_arch_zip(
                plugin_dir=plugin_dir,
                out_dir=out_dir,
                source_url=source_url,
                version=version,
                folder_name=folder_name,
                license_bytes=license_bytes,
                manifest=manifest,
                target_arch=arch,
            )
        ]

    return results


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Packages the built MPC plugin folder as architecture-specific release zip(s)"
    )
    parser.add_argument("--plugin-dir", required=True, help="the built MPC plugin folder")
    parser.add_argument("--out", required=True, help="output directory for the release zip(s)")
    parser.add_argument(
        "--source-url",
        required=True,
        help="source code repository URL for AGPL-3.0-or-later compliance",
    )
    parser.add_argument(
        "--arch",
        choices=["all", "armv7", "aarch64", "combined", "arm32", "arm64"],
        default="all",
        help="target architecture: all (default: two zips: armv7 and aarch64), armv7 (or arm32), aarch64 (or arm64), combined (single legacy zip)",
    )
    parser.add_argument("--version", help="release version (default: from version.xml)")
    parser.add_argument("--license", help="path to LICENSE file (default: from repo or plugin dir)")

    args = parser.parse_args(argv)

    try:
        results = package_mpc(
            plugin_dir=Path(args.plugin_dir),
            out_dir=Path(args.out),
            source_url=args.source_url,
            version=args.version,
            license_path=Path(args.license) if args.license else None,
            arch=args.arch,
        )
    except (PackageError, OSError, ValueError) as e:
        print(f"package-mpc-release: {e}", file=sys.stderr)
        return 1

    for res in results:
        print(f"{res['path']}  {res['size']} bytes  sha256 {res['sha256']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
