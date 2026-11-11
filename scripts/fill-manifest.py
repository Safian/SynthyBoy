#!/usr/bin/env python3
# SynthyBoy - fills plugin.json with the release assets (stdlib only): plugin.json -> plugin.json.release.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   fill-manifest.py --assets DIR (--repo OWNER/REPO --tag vX.Y.Z | --base-url URL | --relative | --file-urls)
#                    [--manifest plugin.json] [--out DIR/plugin.json.release] [--partial] [--sums FILE]
#
# DIR holds the files the release jobs produced: <id>-<version>-<platform>.zip for the platform builds
# (scripts/package-release.py) and <packid>-<packversion>.zip for the content packs (tools/contentpack/contentpack.py build).
# Nothing is taken on trust from the build jobs: size and SHA-256 are computed here from the files themselves. For every
# builds[*] and contentPacks[*] entry of the manifest the script
#   * opens the zip (integrity; a platform zip must have exactly one .clap at its root, LICENSE and README.txt; a pack zip
#     must verify completely: manifest, every file, no extra member, expected id and version),
#   * sets status "released", url, sha256, size (and entry_point = the root .clap for a build),
# then derives "assets" (the list Kick's PluginManifest reads) from the packs, writes the copy and checks it with
# scripts/check-manifest.py (--released with --assets for GitHub urls, --local for relative / file:// urls or --partial).
# The checked-in plugin.json is never touched.
#
# URL styles: --repo + --tag gives https://github.com/OWNER/REPO/releases/download/TAG/<file> (the release workflow);
# --base-url URL gives URL/<file>; --relative gives the bare file name (relative to the manifest), --file-urls absolute
# file:// urls (scripts/make-local-release.sh). "Relative" is relative to the folder of the written manifest.
# --partial: a platform or pack without a file in DIR is left out of the copy (Kick rejects a build without SHA-256) instead of
# being an error; at least one build must remain.
# --sums FILE writes SHA256SUMS ("<sha256>  <file>") of every file the manifest points to and of the manifest itself.

import argparse
import copy
import hashlib
import importlib.util
import json
import os
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class FillError(Exception):
    pass


def _load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_checker = _load("sb_check_manifest", "scripts/check-manifest.py")
_packtool = _load("sb_contentpack", "tools/contentpack/contentpack.py")


def sha256_and_size(path):
    h = hashlib.sha256()
    size = 0
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
            size += len(block)
    return h.hexdigest(), size


def check_sidecar(path, digest):
    """<zip>.sha256 written by package-release.py: if it is there it must agree with the file."""
    sidecar = path.with_name(path.name + ".sha256")
    if sidecar.exists():
        recorded = sidecar.read_text(encoding="utf-8").split()
        if not recorded or recorded[0] != digest:
            raise FillError("%s: SHA-256 %s differs from %s (%s): the file changed after packaging" % (path.name, digest, sidecar.name,
                                                                                                    recorded[0] if recorded else "empty"))


def inspect_platform_zip(path):
    """Returns the entry point (the one .clap at the zip root); raises FillError for anything that is not a plugin zip."""
    try:
        zf = zipfile.ZipFile(path)
    except (OSError, zipfile.BadZipFile) as e:
        raise FillError("%s: not a readable zip: %s" % (path.name, e))
    with zf:
        bad = zf.testzip()
        if bad:
            raise FillError("%s: CRC error in member %s" % (path.name, bad))
        names = [i.filename for i in zf.infolist()]
        for n in names:
            if n.startswith("/") or "\\" in n or ".." in n.split("/"):
                raise FillError("%s: unsafe member path %r" % (path.name, n))
        roots = sorted(n for n in names if "/" not in n.rstrip("/") and n.endswith(".clap"))
        if len(roots) != 1:
            raise FillError("%s: needs exactly one .clap at the zip root (the native shell Kick loads), found %r" % (path.name, roots))
        for required in ("LICENSE", "README.txt"):
            if required not in names:
                raise FillError("%s: %s is missing" % (path.name, required))
    return roots[0]


def make_url_function(args, manifest_dir):
    if args.file_urls:
        return lambda path: path.resolve().as_uri()
    if args.relative:
        return lambda path: Path(os.path.relpath(path.resolve(), Path(manifest_dir).resolve())).as_posix()
    if args.base_url:
        base = args.base_url.rstrip("/")
        return lambda path: "%s/%s" % (base, path.name)
    return lambda path: "https://github.com/%s/releases/download/%s/%s" % (args.repo, args.tag, path.name)


def fill(manifest, assets_dir, url_for, partial=False):
    """Returns (filled copy, [(kind, name, file, size, sha256)]). Raises FillError listing every problem."""
    assets_dir = Path(assets_dir)
    out = copy.deepcopy(manifest)
    problems = []
    report = []

    kept_builds = {}
    for platform, build in manifest["builds"].items():
        path = assets_dir / _checker.build_asset_name(manifest, platform)
        if not path.is_file():
            if not partial:
                problems.append("no asset for %s: %s is missing from %s" % (platform, path.name, assets_dir))
            continue
        try:
            entry_point = inspect_platform_zip(path)
            digest, size = sha256_and_size(path)
            check_sidecar(path, digest)
        except FillError as e:
            problems.append(str(e))
            continue
        kept_builds[platform] = {"status": "released", "url": url_for(path), "sha256": digest, "size": size, "entry_point": entry_point}
        report.append(("build", platform, path.name, size, digest))
    if not kept_builds and not problems:
        problems.append("no platform build found in %s" % assets_dir)
    out["builds"] = kept_builds
    if isinstance(out.get("gui"), dict):
        out["gui"]["builds"] = [name for name in out["gui"].get("builds", []) if name in kept_builds]  # the GUI is in the builds that exist
        if not out["gui"]["builds"]:
            del out["gui"]

    kept_packs = []
    for pack in manifest.get("contentPacks", []):
        path = assets_dir / _checker.pack_asset_name(pack)
        if not path.is_file():
            if not partial:
                problems.append("no asset for content pack %s: %s is missing from %s" % (pack["id"], path.name, assets_dir))
            continue
        errors = _packtool.verify_zip(path, pack["id"], pack["version"])
        if errors:
            problems.extend(errors)
            continue
        digest, size = sha256_and_size(path)
        check_sidecar(path, digest)
        filled = dict(pack, status="released", url=url_for(path), sha256=digest, size=size)
        kept_packs.append(filled)
        report.append(("pack", pack["id"], path.name, size, digest))
    out["contentPacks"] = kept_packs
    out["assets"] = _checker.kick_assets(out)

    if problems:
        raise FillError("\n  - ".join(["%d problem(s):" % len(problems)] + problems))
    return out, report


def main(argv=None):
    parser = argparse.ArgumentParser(description="Fills plugin.json with the release assets (see the header of this file)")
    parser.add_argument("--manifest", default=str(ROOT / "plugin.json"))
    parser.add_argument("--assets", required=True, help="directory with the platform zips and the pack zips")
    parser.add_argument("--out", help="default: <assets>/plugin.json.release")
    style = parser.add_mutually_exclusive_group(required=True)
    style.add_argument("--repo", help="OWNER/REPO of the GitHub release (needs --tag)")
    style.add_argument("--base-url")
    style.add_argument("--relative", action="store_true")
    style.add_argument("--file-urls", action="store_true")
    parser.add_argument("--tag", help="release tag (vX.Y.Z); it must equal v<version of plugin.json>")
    parser.add_argument("--partial", action="store_true")
    parser.add_argument("--sums", help="also write SHA256SUMS to this file")
    args = parser.parse_args(argv)
    if args.repo and not args.tag:
        parser.error("--repo needs --tag")

    try:
        manifest = json.loads(Path(args.manifest).read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        print("fill-manifest: cannot read %s: %s" % (args.manifest, e), file=sys.stderr)
        return 1
    if args.tag and args.tag != "v%s" % manifest.get("version"):
        print("fill-manifest: tag %s does not match the manifest version %s (expected v%s)" % (args.tag, manifest.get("version"), manifest.get("version")),
              file=sys.stderr)
        return 1

    assets_dir = Path(args.assets)
    out_path = Path(args.out) if args.out else assets_dir / "plugin.json.release"
    try:
        filled, report = fill(manifest, assets_dir, make_url_function(args, out_path.parent), args.partial)
    except FillError as e:
        print("fill-manifest: %s" % e, file=sys.stderr)
        return 1

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(filled, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    local = args.partial or args.relative or args.file_urls
    _, errors = _checker.check_manifest(out_path, "local" if local else "released", None if local else assets_dir)
    if errors:
        out_path.unlink()
        print("fill-manifest: the result does not pass scripts/check-manifest.py (%s):" % ("--local" if local else "--released"), file=sys.stderr)
        for e in errors:
            print("  - " + e, file=sys.stderr)
        return 1

    for kind, name, file, size, digest in report:
        print("%-5s %-45s %12d  %s  %s" % (kind, name, size, digest, file))
    print("wrote %s (%d builds, %d content packs)" % (out_path, len(filled["builds"]), len(filled["contentPacks"])))

    if args.sums:
        lines = ["%s  %s" % (digest, file) for _, _, file, _, digest in report]
        lines.append("%s  %s" % (sha256_and_size(out_path)[0], out_path.name))
        Path(args.sums).write_text("\n".join(lines) + "\n", encoding="utf-8")
        print("wrote %s" % args.sums)
    return 0


if __name__ == "__main__":
    sys.exit(main())
