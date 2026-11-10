#!/usr/bin/env python3
# SynthyBoy - contentpack: builds, verifies, splits and describes ContentPacks (docs/ARCHITECTURE.md section 6), stdlib only.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   contentpack.py manifest --dir DIR [--id ID --version X.Y.Z --kind K --license L --attribution A ...] [--check]
#       writes DIR/contentpack.json: the metadata (from the arguments, else from an existing DIR/contentpack.json) and the
#       "files" list (path, sha256, size of every file below DIR, sorted). --check writes nothing and exits 1 if the file differs.
#   contentpack.py verify (--dir DIR | --zip FILE) [--expect-id ID] [--expect-version X.Y.Z]
#       validates contentpack.json against schema/contentpack.schema.json and checks every listed file (present, size, SHA-256);
#       a directory or zip that holds a file the manifest does not list is an error (so is an unsafe path in a zip).
#   contentpack.py build --dir DIR --out OUTDIR [--max-size BYTES] [--summary FILE]
#       verifies DIR and writes OUTDIR/<id>-<version>.zip (contentpack.json + the listed files at the zip root, byte for byte
#       reproducible: sorted entries, fixed timestamps), verifies the zip again and refuses a zip over --max-size (default
#       2e9 bytes, GitHub's limit per release asset is 2 GiB): split such a pack per instrument with `split`.
#   contentpack.py split --dir DIR --out OUTDIR
#       one pack per top level directory of DIR (an instrument per directory): OUTDIR/<slug>/ with its own contentpack.json
#       (id "<id>-<slug>", same version / licence / attribution), files hard-linked (copied where that is not possible).
#
# The pack format is the one core/src/content_pack.cpp loads (parseContentPackManifest / verifyContentPack).

import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
MANIFEST_NAME = "contentpack.json"
DEFAULT_MAX_BYTES = 2_000_000_000   # below GitHub's 2 GiB per release asset
FIXED_DATE = (1980, 1, 1, 0, 0, 0)  # zip timestamps: reproducible archives
STORED_SUFFIXES = {".flac", ".ogg", ".mp3", ".zip"}  # already compressed
IGNORED_NAMES = {".DS_Store", "Thumbs.db", "desktop.ini"}
CHUNK = 1 << 20

ID_RE = re.compile(r"^[a-z0-9]+(\.[a-z0-9-]+)+$")
VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
KEY_ORDER = ["schemaVersion", "id", "name", "version", "license", "attribution", "kind", "requires_plugin", "description", "files"]


class PackError(Exception):
    pass


def _load_checker():
    """scripts/check-manifest.py holds the JSON schema validator (loaded by path: its name has a hyphen)."""
    spec = importlib.util.spec_from_file_location("sb_check_manifest", ROOT / "scripts" / "check-manifest.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_checker = _load_checker()


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            block = f.read(CHUNK)
            if not block:
                break
            h.update(block)
    return h.hexdigest()


def schema_errors(manifest, label):
    schema = json.loads((ROOT / "schema" / "contentpack.schema.json").read_text(encoding="utf-8"))
    errors = []
    _checker.validate(manifest, schema, schema, label, errors)
    return errors


def safe_relative_path(path):
    """True for a plain relative path that stays inside the pack (no '..', no absolute path, no backslash, no drive)."""
    if not path or path.startswith("/") or "\\" in path or "\x00" in path or re.match(r"^[A-Za-z]:", path):
        return False
    parts = path.split("/")
    return all(p not in ("", ".", "..") for p in parts)


def list_pack_files(directory):
    """Relative posix paths of every regular file below directory except the manifest and OS noise files."""
    found = []
    for dirpath, dirnames, filenames in os.walk(directory):
        dirnames[:] = sorted(d for d in dirnames if d not in IGNORED_NAMES)
        for name in filenames:
            if name in IGNORED_NAMES or name == MANIFEST_NAME and Path(dirpath) == Path(directory):
                continue
            full = Path(dirpath) / name
            if full.is_symlink() or not full.is_file():
                raise PackError("%s: not a regular file (symbolic links and special files are not allowed in a pack)" % full)
            found.append(full.relative_to(directory).as_posix())
    return sorted(found)


def read_manifest(directory):
    path = Path(directory) / MANIFEST_NAME
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        raise PackError("cannot read %s: %s" % (path, e))


def check_manifest_content(manifest, label):
    """Schema + the rules the schema cannot express (unique, safe paths)."""
    errors = schema_errors(manifest, label)
    seen = set()
    for entry in manifest.get("files", []) if isinstance(manifest, dict) else []:
        if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
            continue
        p = entry["path"]
        if not safe_relative_path(p):
            errors.append("%s: unsafe file path %r" % (label, p))
        if p == MANIFEST_NAME:
            errors.append("%s: the manifest must not list itself" % label)
        if p in seen:
            errors.append("%s: file listed twice: %r" % (label, p))
        seen.add(p)
    return errors


def verify_directory(directory, manifest=None):
    """Returns a list of problems; empty means the directory is exactly the pack its contentpack.json describes."""
    directory = Path(directory)
    manifest = manifest if manifest is not None else read_manifest(directory)
    errors = check_manifest_content(manifest, "contentpack(%s)" % directory.name)
    if errors:
        return errors
    listed = {}
    for entry in manifest["files"]:
        listed[entry["path"]] = entry
        full = directory / entry["path"]
        if not full.is_file():
            errors.append("missing file: %s" % entry["path"])
            continue
        if "size" in entry and full.stat().st_size != entry["size"]:
            errors.append("size mismatch for %s: manifest %d, file %d" % (entry["path"], entry["size"], full.stat().st_size))
        actual = sha256_file(full)
        if actual != entry["sha256"]:
            errors.append("checksum mismatch for %s: manifest %s, file %s" % (entry["path"], entry["sha256"], actual))
    for path in list_pack_files(directory):
        if path not in listed:
            errors.append("file not listed in %s: %s" % (MANIFEST_NAME, path))
    return errors


def verify_zip(zip_path, expect_id=None, expect_version=None):
    """Returns a list of problems of a pack zip (same rules as verify_directory, read straight from the archive)."""
    errors = []
    try:
        zf = zipfile.ZipFile(zip_path)
    except (OSError, zipfile.BadZipFile) as e:
        return ["%s: not a readable zip: %s" % (zip_path, e)]
    with zf:
        bad = zf.testzip()
        if bad:
            errors.append("%s: CRC error in member %s" % (zip_path, bad))
        names = [i.filename for i in zf.infolist() if not i.is_dir()]
        if len(set(names)) != len(names):
            errors.append("%s: duplicate member names" % zip_path)
        for n in names:
            if not safe_relative_path(n):
                errors.append("%s: unsafe member path %r" % (zip_path, n))
        if MANIFEST_NAME not in names:
            return errors + ["%s: no %s at the zip root" % (zip_path, MANIFEST_NAME)]
        try:
            manifest = json.loads(zf.read(MANIFEST_NAME).decode("utf-8"))
        except ValueError as e:
            return errors + ["%s: %s is not valid JSON: %s" % (zip_path, MANIFEST_NAME, e)]
        errors += check_manifest_content(manifest, "contentpack(%s)" % Path(zip_path).name)
        if errors:
            return errors
        if expect_id is not None and manifest["id"] != expect_id:
            errors.append("%s: pack id %r, expected %r" % (zip_path, manifest["id"], expect_id))
        if expect_version is not None and manifest["version"] != expect_version:
            errors.append("%s: pack version %r, expected %r" % (zip_path, manifest["version"], expect_version))
        listed = {e["path"]: e for e in manifest["files"]}
        for path, entry in listed.items():
            if path not in names:
                errors.append("%s: missing file: %s" % (zip_path, path))
                continue
            h = hashlib.sha256()
            size = 0
            with zf.open(path) as f:
                while True:
                    block = f.read(CHUNK)
                    if not block:
                        break
                    h.update(block)
                    size += len(block)
            if "size" in entry and size != entry["size"]:
                errors.append("%s: size mismatch for %s: manifest %d, member %d" % (zip_path, path, entry["size"], size))
            if h.hexdigest() != entry["sha256"]:
                errors.append("%s: checksum mismatch for %s" % (zip_path, path))
        for n in names:
            if n != MANIFEST_NAME and n not in listed:
                errors.append("%s: member not listed in %s: %s" % (zip_path, MANIFEST_NAME, n))
    return errors


def _write_member(zf, name, source_path, size):
    info = zipfile.ZipInfo(name, date_time=FIXED_DATE)
    info.create_system = 3
    info.external_attr = 0o644 << 16
    info.compress_type = zipfile.ZIP_STORED if Path(name).suffix.lower() in STORED_SUFFIXES else zipfile.ZIP_DEFLATED
    info.file_size = size
    with open(source_path, "rb") as src, zf.open(info, "w", force_zip64=size >= (1 << 31) - 1) as dst:
        while True:
            block = src.read(CHUNK)
            if not block:
                break
            dst.write(block)


def zip_name(manifest):
    return "%s-%s.zip" % (manifest["id"], manifest["version"])


def build_pack(directory, out_dir, max_bytes=DEFAULT_MAX_BYTES):
    """Verifies the pack directory and writes its zip. Returns the summary dict (file, size, sha256, id, version, ...)."""
    directory = Path(directory)
    manifest = read_manifest(directory)
    errors = verify_directory(directory, manifest)
    if errors:
        raise PackError("%s is not a valid pack:\n  - %s" % (directory, "\n  - ".join(errors)))
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    target = out_dir / zip_name(manifest)
    tmp = target.with_name(target.name + ".part")
    try:
        with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=9, allowZip64=True) as zf:
            _write_member(zf, MANIFEST_NAME, directory / MANIFEST_NAME, (directory / MANIFEST_NAME).stat().st_size)
            for entry in sorted(manifest["files"], key=lambda e: e["path"]):
                full = directory / entry["path"]
                _write_member(zf, entry["path"], full, full.stat().st_size)
        size = tmp.stat().st_size
        if size > max_bytes:
            raise PackError("%s: the zip is %d bytes, over the limit of %d (GitHub allows 2 GiB per release asset): split the pack "
                            "per instrument (contentpack.py split) and list every part in plugin.json" % (target.name, size, max_bytes))
        problems = verify_zip(tmp, manifest["id"], manifest["version"])
        if problems:
            raise PackError("the zip just written does not verify:\n  - " + "\n  - ".join(problems))
        os.replace(tmp, target)
    finally:
        if tmp.exists():
            tmp.unlink()
    return {"id": manifest["id"], "version": manifest["version"], "kind": manifest["kind"], "license": manifest["license"],
            "file": target.name, "size": target.stat().st_size, "sha256": sha256_file(target),
            "files": len(manifest["files"]), "uncompressed": sum(e.get("size", 0) for e in manifest["files"])}


def slugify(name):
    return re.sub(r"-+", "-", re.sub(r"[^a-z0-9]+", "-", name.lower())).strip("-")


def split_pack(directory, out_dir):
    """One pack per top level directory. Returns a list of {id, name, path, files, bytes}."""
    directory = Path(directory)
    manifest = read_manifest(directory)
    errors = verify_directory(directory, manifest)
    if errors:
        raise PackError("%s is not a valid pack:\n  - %s" % (directory, "\n  - ".join(errors)))
    groups = {}
    for entry in manifest["files"]:
        head, sep, rest = entry["path"].partition("/")
        if not sep:
            raise PackError("%s: file %s is not inside a directory; split needs one directory per instrument" % (directory, entry["path"]))
        groups.setdefault(head, []).append(dict(entry, path=rest))
    slugs = {}
    for head in groups:
        slug = slugify(head)
        if not slug:
            raise PackError("%s: cannot make an id from the directory name %r" % (directory, head))
        if slug in slugs:
            raise PackError("%s: directories %r and %r give the same id part %r" % (directory, slugs[slug], head, slug))
        slugs[slug] = head
    out_dir = Path(out_dir)
    result = []
    for slug in sorted(slugs):
        head = slugs[slug]
        sub = out_dir / slug
        if sub.exists():
            shutil.rmtree(sub)
        for entry in groups[head]:
            dst = sub / entry["path"]
            dst.parent.mkdir(parents=True, exist_ok=True)
            src = directory / head / entry["path"]
            try:
                os.link(src, dst)
            except OSError:
                shutil.copy2(src, dst)
        sub_manifest = {k: manifest[k] for k in KEY_ORDER if k in manifest and k not in ("files", "name", "id", "description")}
        sub_manifest["id"] = "%s-%s" % (manifest["id"], slug)
        sub_manifest["name"] = "%s - %s" % (manifest.get("name", manifest["id"]), head)
        if "description" in manifest:
            sub_manifest["description"] = manifest["description"]
        sub_manifest["files"] = groups[head]
        write_manifest(sub, sub_manifest)
        problems = verify_directory(sub)
        if problems:
            raise PackError("split result %s does not verify:\n  - %s" % (sub, "\n  - ".join(problems)))
        result.append({"id": sub_manifest["id"], "name": sub_manifest["name"], "path": str(sub), "files": len(groups[head]),
                       "bytes": sum(e["size"] for e in groups[head] if "size" in e)})
    return result


def format_manifest(manifest):
    ordered = {k: manifest[k] for k in KEY_ORDER if k in manifest}
    ordered.update({k: v for k, v in manifest.items() if k not in ordered})
    return json.dumps(ordered, indent=2, ensure_ascii=False) + "\n"


def write_manifest(directory, manifest):
    (Path(directory) / MANIFEST_NAME).write_text(format_manifest(manifest), encoding="utf-8")


def make_manifest(directory, args):
    directory = Path(directory)
    meta = {}
    existing = directory / MANIFEST_NAME
    if existing.exists():
        meta = {k: v for k, v in read_manifest(directory).items() if k != "files"}
    meta.setdefault("schemaVersion", 1)
    for arg, key in (("id", "id"), ("name", "name"), ("version", "version"), ("kind", "kind"), ("license", "license"),
                     ("attribution", "attribution"), ("requires_plugin", "requires_plugin"), ("description", "description")):
        value = getattr(args, arg)
        if value is not None:
            meta[key] = value
    meta.setdefault("requires_plugin", ">=0.1.0")
    files = []
    for path in list_pack_files(directory):
        full = directory / path
        files.append({"path": path, "sha256": sha256_file(full), "size": full.stat().st_size})
    meta["files"] = files
    errors = check_manifest_content(meta, "contentpack(%s)" % directory.name)
    if errors:
        raise PackError("the manifest would be invalid (give the missing metadata as arguments):\n  - " + "\n  - ".join(errors))
    return meta


def cmd_manifest(args):
    meta = make_manifest(args.dir, args)
    text = format_manifest(meta)
    target = Path(args.dir) / MANIFEST_NAME
    if args.check:
        if not target.exists() or target.read_text(encoding="utf-8") != text:
            print("%s is not up to date (run without --check)" % target)
            return 1
        print("%s: up to date (%d files)" % (target, len(meta["files"])))
        return 0
    target.write_text(text, encoding="utf-8")
    print("wrote %s (%d files)" % (target, len(meta["files"])))
    return 0


def cmd_verify(args):
    if bool(args.dir) == bool(args.zip):
        raise PackError("give exactly one of --dir and --zip")
    errors = verify_directory(args.dir) if args.dir else verify_zip(args.zip, args.expect_id, args.expect_version)
    if errors:
        print("%d problem(s):" % len(errors))
        for e in errors:
            print("  - " + e)
        return 1
    print("OK: %s" % (args.dir or args.zip))
    return 0


def cmd_build(args):
    summary = build_pack(args.dir, args.out, args.max_size)
    if args.summary:
        Path(args.summary).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print("built %s: %d bytes, sha256 %s, %d files" % (summary["file"], summary["size"], summary["sha256"], summary["files"]))
    return 0


def cmd_split(args):
    for item in split_pack(args.dir, args.out):
        print("%s  %s  (%d files, %d bytes)" % (item["id"], item["path"], item["files"], item["bytes"]))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="SynthyBoy ContentPack tool")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("manifest", help="write / check contentpack.json of a pack directory")
    p.add_argument("--dir", required=True)
    for name in ("id", "name", "version", "kind", "license", "attribution", "requires-plugin", "description"):
        p.add_argument("--" + name, dest=name.replace("-", "_"))
    p.add_argument("--check", action="store_true")
    p.set_defaults(func=cmd_manifest)

    p = sub.add_parser("verify", help="verify a pack directory or zip")
    p.add_argument("--dir")
    p.add_argument("--zip")
    p.add_argument("--expect-id")
    p.add_argument("--expect-version")
    p.set_defaults(func=cmd_verify)

    p = sub.add_parser("build", help="verify a pack directory and write <id>-<version>.zip")
    p.add_argument("--dir", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--max-size", type=int, default=DEFAULT_MAX_BYTES)
    p.add_argument("--summary")
    p.set_defaults(func=cmd_build)

    p = sub.add_parser("split", help="one pack per top level directory")
    p.add_argument("--dir", required=True)
    p.add_argument("--out", required=True)
    p.set_defaults(func=cmd_split)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except PackError as e:
        print("contentpack: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
