#!/usr/bin/env python3
# SynthyBoy - packages the built plugin of one platform as <id>-<version>-<platform>.zip (stdlib only).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   package-release.py --platform linux-armv7 --native build/shell-clap/synthyboy.clap --out dist
#                      [--juce-clap build/SynthyBoy_artefacts/Release/CLAP/SynthyBoy.clap]
#                      [--juce-vst3 build/SynthyBoy_artefacts/Release/VST3/SynthyBoy.vst3]
#                      [--manifest plugin.json]
#
# id and version come from plugin.json, so the asset name is the one scripts/check-manifest.py and scripts/fill-manifest.py expect.
# Layout of the zip (docs/RELEASE.md):
#   synthyboy.clap            the native shell (shell-clap, no JUCE): what Kick loads (builds[*].entry_point)
#   juce/SynthyBoy.clap, juce/SynthyBoy.vst3   the JUCE builds for DAWs (only where the platform has them)
#   LICENSE, NOTICE, THIRD_PARTY.md, README.txt
# The native file is checked for the platform it is packaged for (ELF class / machine / ARM hard-float ABI, Mach-O universal with
# arm64 and x86_64, PE x86-64): a binary of the wrong architecture is never shipped under a platform name. A <zip>.sha256 file
# ("<sha256>  <name>") is written next to the zip; fill-manifest.py cross-checks it.
#
# The archive is reproducible: sorted entries, fixed timestamps, no user / group information.

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
NATIVE_NAME = "synthyboy.clap"
PLATFORMS = ("linux-armv7", "linux-aarch64", "linux-x86_64", "macos-universal", "windows-x64")
EXTRA_FILES = ("LICENSE", "NOTICE", "THIRD_PARTY.md")

# Mach-O
FAT_MAGICS = {0xCAFEBABE: 20, 0xCAFEBABF: 32}   # fat header, fat64 header: size of one arch entry
CPU_TYPE_X86_64 = 0x01000007
CPU_TYPE_ARM64 = 0x0100000C


class PackageError(Exception):
    pass


def binary_description(path):
    """What the binary at path is: ('elf', bits, machine, flags), ('macho-fat', {cpu types}), ('macho', cpu type) or ('pe', machine)."""
    with open(path, "rb") as f:
        head = f.read(4096)
    if head[:4] == b"\x7fELF" and len(head) >= 52:
        bits = 32 if head[4] == 1 else 64
        endian = "<" if head[5] == 1 else ">"
        machine = struct.unpack_from(endian + "H", head, 18)[0]
        flags = struct.unpack_from(endian + "I", head, 36 if bits == 32 else 48)[0]
        return ("elf", bits, machine, flags)
    if len(head) >= 8 and struct.unpack_from(">I", head, 0)[0] in FAT_MAGICS:
        magic, count = struct.unpack_from(">II", head, 0)
        entry = FAT_MAGICS[magic]
        types = set()
        for i in range(min(count, 16)):
            offset = 8 + i * entry
            if offset + 4 <= len(head):
                types.add(struct.unpack_from(">I", head, offset)[0])
        return ("macho-fat", types)
    if len(head) >= 8 and struct.unpack_from("<I", head, 0)[0] in (0xFEEDFACF, 0xFEEDFACE):
        return ("macho", struct.unpack_from("<I", head, 4)[0])
    if head[:2] == b"MZ" and len(head) >= 64:
        pe = struct.unpack_from("<I", head, 60)[0]
        with open(path, "rb") as f:
            f.seek(pe)
            sig = f.read(6)
        if sig[:4] == b"PE\0\0":
            return ("pe", struct.unpack("<H", sig[4:6])[0])
    return ("unknown",)


def check_native_binary(path, platform):
    """Raises PackageError unless the file is a shared module of the platform's architecture."""
    d = binary_description(path)
    ok = False
    what = repr(d)
    if platform == "linux-armv7":
        # EM_ARM = 40, 32 bit, EABI version 5 (flags >> 24), hard-float ABI (EF_ARM_ABI_FLOAT_HARD = 0x400)
        ok = d[0] == "elf" and d[1] == 32 and d[2] == 40 and (d[3] >> 24) == 5 and bool(d[3] & 0x400)
        what = "needs an ELF32 ARM EABI5 hard-float binary, got %r" % (d,)
    elif platform == "linux-aarch64":
        ok = d[0] == "elf" and d[1] == 64 and d[2] == 183
        what = "needs an ELF64 AArch64 binary (EM_AARCH64 183), got %r" % (d,)
    elif platform == "linux-x86_64":
        ok = d[0] == "elf" and d[1] == 64 and d[2] == 62
        what = "needs an ELF64 x86-64 binary (EM_X86_64 62), got %r" % (d,)
    elif platform == "macos-universal":
        ok = d[0] == "macho-fat" and {CPU_TYPE_ARM64, CPU_TYPE_X86_64} <= d[1]
        what = "needs a universal Mach-O with arm64 and x86_64, got %r" % (d,)
    elif platform == "windows-x64":
        ok = d[0] == "pe" and d[1] == 0x8664
        what = "needs a PE binary for x86-64 (0x8664), got %r" % (d,)
    if not ok:
        raise PackageError("%s: %s" % (path, what))


def zip_mode(path, st):
    return 0o755 if (st.st_mode & 0o111) else 0o644


def collect(source, arcname, entries):
    """Adds a file, or every file / symlink below a directory, to entries as (arcname, source path)."""
    source = Path(source)
    if source.is_symlink() or source.is_file():
        entries.append((arcname, source))
    elif source.is_dir():
        for dirpath, dirnames, filenames in os.walk(source):
            dirnames.sort()
            for name in sorted(filenames + [d for d in dirnames if (Path(dirpath) / d).is_symlink()]):
                full = Path(dirpath) / name
                entries.append(("%s/%s" % (arcname, full.relative_to(source).as_posix()), full))
    else:
        raise PackageError("%s does not exist" % source)


def write_entry(zf, arcname, path):
    info = zipfile.ZipInfo(arcname, date_time=FIXED_DATE)
    info.create_system = 3
    if path.is_symlink():
        info.external_attr = (stat.S_IFLNK | 0o777) << 16
        zf.writestr(info, os.readlink(path))
        return
    st = path.stat()
    info.external_attr = (stat.S_IFREG | zip_mode(path, st)) << 16
    info.compress_type = zipfile.ZIP_DEFLATED
    with open(path, "rb") as src:
        zf.writestr(info, src.read(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def readme_text(manifest, platform, has_juce):
    lines = [
        "%s %s (%s)" % (manifest["name"], manifest["version"], platform),
        "",
        "%s" % manifest.get("description", ""),
        "",
        "Contents",
        "  %s    the native CLAP module (no JUCE, no GUI toolkit): the file the Kick plugin manager loads" % NATIVE_NAME,
    ]
    if has_juce:
        lines += ["  juce/SynthyBoy.clap, juce/SynthyBoy.vst3    CLAP and VST3 builds with the JUCE editor for DAWs"]
    lines += [
        "  LICENSE, NOTICE, THIRD_PARTY.md    licence texts (AGPL-3.0-or-later) and the third party notices",
        "",
        "Use",
        "  Kick: install through the plugin manager from the release's plugin.json.release (it verifies the SHA-256 of this archive);",
        "        or unpack and give Kick the path of %s." % NATIVE_NAME,
    ]
    if has_juce:
        lines += ["  DAW:  copy the files of juce/ to the plug-in folders of your system (e.g. ~/.clap, ~/.vst3 on Linux,",
                  "        ~/Library/Audio/Plug-Ins/{CLAP,VST3} on macOS, C:\\Program Files\\Common Files\\{CLAP,VST3} on Windows)."]
    lines += [
        "",
        "Source code (AGPL-3.0-or-later): %s (tag v%s)" % (manifest.get("homepage", ""), manifest["version"]),
        "Content packs (presets) are separate downloads, see plugin.json.release.",
        "",
    ]
    return "\n".join(lines)


def package(manifest, platform, native, out_dir, juce_clap=None, juce_vst3=None, repo_root=ROOT):
    if platform not in PLATFORMS:
        raise PackageError("unknown platform %r (one of %s)" % (platform, ", ".join(PLATFORMS)))
    native = Path(native)
    if not native.is_file():
        raise PackageError("the native shell %s is not a file" % native)
    check_native_binary(native, platform)

    entries = [(NATIVE_NAME, native)]
    if juce_clap:
        collect(juce_clap, "juce/SynthyBoy.clap", entries)
    if juce_vst3:
        collect(juce_vst3, "juce/SynthyBoy.vst3", entries)
    for name in EXTRA_FILES:
        src = Path(repo_root) / name
        if not src.is_file():
            raise PackageError("%s is missing from the repository root" % name)
        entries.append((name, src))

    name = "%s-%s-%s.zip" % (manifest["id"], manifest["version"], platform)
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    target = out_dir / name
    tmp = target.with_name(name + ".part")
    try:
        with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, allowZip64=True) as zf:
            for arcname, path in sorted(entries, key=lambda e: e[0]):
                write_entry(zf, arcname, path)
            info = zipfile.ZipInfo("README.txt", date_time=FIXED_DATE)
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | 0o644) << 16
            zf.writestr(info, readme_text(manifest, platform, bool(juce_clap or juce_vst3)), compress_type=zipfile.ZIP_DEFLATED)
        os.replace(tmp, target)
    finally:
        if tmp.exists():
            tmp.unlink()
    h = hashlib.sha256()
    with open(target, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    digest = h.hexdigest()
    (out_dir / (name + ".sha256")).write_text("%s  %s\n" % (digest, name), encoding="utf-8")
    return {"file": name, "size": target.stat().st_size, "sha256": digest}


def main(argv=None):
    parser = argparse.ArgumentParser(description="Packages one platform's plugin build as <id>-<version>-<platform>.zip")
    parser.add_argument("--platform", required=True, choices=PLATFORMS)
    parser.add_argument("--native", required=True, help="the native shell module (shell-clap synthyboy.clap)")
    parser.add_argument("--juce-clap", help="the JUCE CLAP build (file or macOS bundle directory)")
    parser.add_argument("--juce-vst3", help="the JUCE VST3 build (bundle directory)")
    parser.add_argument("--out", required=True)
    parser.add_argument("--manifest", default=str(ROOT / "plugin.json"))
    args = parser.parse_args(argv)
    try:
        manifest = json.loads(Path(args.manifest).read_text(encoding="utf-8"))
        result = package(manifest, args.platform, args.native, args.out, args.juce_clap, args.juce_vst3)
    except (PackageError, OSError, ValueError, KeyError) as e:
        print("package-release: %s" % e, file=sys.stderr)
        return 1
    print("%s  %d bytes  sha256 %s" % (result["file"], result["size"], result["sha256"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
