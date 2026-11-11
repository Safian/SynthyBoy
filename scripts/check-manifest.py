#!/usr/bin/env python3
# SynthyBoy - validates plugin.json against schema/plugin-manifest.schema.json (stdlib only) and against CMakeLists.txt.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: python3 -I scripts/check-manifest.py [--released | --local] [--assets DIR] [path/to/plugin.json]
#
# The schema validator implements only the subset of JSON Schema (draft 2020-12) that the schema uses:
# type, required, enum, const, pattern, properties, additionalProperties, items, minimum, minLength, minItems and local
# "$ref" ("#/$defs/..."). A schema keyword outside this subset is an error (it must not be silently ignored).
# Cross checks: version == project(... VERSION x.y.z) and clap_id == CLAP_ID in CMakeLists.txt; a build / content pack marked
# "released" needs an url, sha256 and size and the file name of the release asset (docs/RELEASE.md), an "unbuilt" one must have the
# empty placeholders; every declared content pack is verified (contentpack.json against its schema, equal id / version / kind /
# licence, requires_plugin satisfied by this plugin version, every file present with its SHA-256, small enough for one release
# asset).
#
# Modes:
#   (default)    the checked-in plugin.json: placeholders for what no release job has built yet.
#   --released   the manifest the release workflow produced (plugin.json.release): all five builds and all content packs are
#                "released" with https urls, "assets" (the list Kick's PluginManifest reads) mirrors the content packs; with
#                --assets DIR every url's file name must exist in DIR with the SHA-256 and size of the entry.
#   --local      a manifest of scripts/make-local-release.sh: urls may be https, file:// or relative to the manifest's folder (the
#                files are then checked), only built platforms / packs are listed (no "unbuilt" entry: Kick rejects a build without a
#                SHA-256), at least one build is released.

import argparse
import hashlib
import importlib.util
import json
import re
import sys
from pathlib import Path
from urllib.parse import unquote, urlparse
from urllib.request import url2pathname

ROOT = Path(__file__).resolve().parent.parent

ANNOTATIONS = {"$schema", "$id", "$defs", "title", "description", "default", "examples", "$comment"}
SUPPORTED = {"type", "required", "enum", "const", "pattern", "properties", "additionalProperties", "items",
             "minimum", "minLength", "minItems", "$ref"}


def type_ok(value, name):
    if name == "object":
        return isinstance(value, dict)
    if name == "array":
        return isinstance(value, list)
    if name == "string":
        return isinstance(value, str)
    if name == "boolean":
        return isinstance(value, bool)
    if name == "null":
        return value is None
    if name == "integer":
        return isinstance(value, int) and not isinstance(value, bool) or (isinstance(value, float) and value.is_integer())
    if name == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    raise ValueError("unsupported type in schema: %r" % name)


def resolve(root, ref):
    if not ref.startswith("#/"):
        raise ValueError("only local $ref is supported: %r" % ref)
    node = root
    for part in ref[2:].split("/"):
        node = node[part.replace("~1", "/").replace("~0", "~")]
    return node


def validate(value, schema, root, path, errors):
    if isinstance(schema, bool):
        if not schema:
            errors.append("%s: not allowed" % path)
        return
    for key in schema:
        if key not in SUPPORTED and key not in ANNOTATIONS:
            raise ValueError("schema keyword not supported by check-manifest.py: %r" % key)

    if "$ref" in schema:
        validate(value, resolve(root, schema["$ref"]), root, path, errors)

    if "type" in schema:
        types = schema["type"] if isinstance(schema["type"], list) else [schema["type"]]
        if not any(type_ok(value, t) for t in types):
            errors.append("%s: expected type %s, got %s" % (path, "/".join(types), type(value).__name__))
            return

    if "enum" in schema and value not in schema["enum"]:
        errors.append("%s: %r is not one of %r" % (path, value, schema["enum"]))
    if "const" in schema and value != schema["const"]:
        errors.append("%s: must be %r" % (path, schema["const"]))

    if isinstance(value, str):
        if "pattern" in schema and re.search(schema["pattern"], value) is None:
            errors.append("%s: %r does not match %s" % (path, value, schema["pattern"]))
        if "minLength" in schema and len(value) < schema["minLength"]:
            errors.append("%s: shorter than %d" % (path, schema["minLength"]))

    if isinstance(value, (int, float)) and not isinstance(value, bool):
        if "minimum" in schema and value < schema["minimum"]:
            errors.append("%s: %r is below %r" % (path, value, schema["minimum"]))

    if isinstance(value, dict):
        for name in schema.get("required", []):
            if name not in value:
                errors.append("%s: missing required property %r" % (path, name))
        props = schema.get("properties", {})
        for name, sub in props.items():
            if name in value:
                validate(value[name], sub, root, "%s.%s" % (path, name), errors)
        extra = schema.get("additionalProperties", True)
        for name in value:
            if name not in props:
                if extra is False:
                    errors.append("%s: unexpected property %r" % (path, name))
                elif isinstance(extra, dict):
                    validate(value[name], extra, root, "%s.%s" % (path, name), errors)

    if isinstance(value, list):
        if "minItems" in schema and len(value) < schema["minItems"]:
            errors.append("%s: fewer than %d items" % (path, schema["minItems"]))
        if "items" in schema:
            for i, item in enumerate(value):
                validate(item, schema["items"], root, "%s[%d]" % (path, i), errors)


PLATFORMS = ["linux-armv7", "linux-aarch64", "linux-x86_64", "macos-universal", "windows-x64"]
MAX_ASSET_BYTES = 2_000_000_000  # GitHub allows 2 GiB (2147483648) per release asset; stay below
MODES = ("checked-in", "released", "local")


def build_asset_name(manifest, platform):
    """File name of the zip of a platform build (also written by scripts/package-release.py)."""
    return "%s-%s-%s.zip" % (manifest["id"], manifest["version"], platform)


def pack_asset_name(pack):
    """File name of the zip of a content pack (also written by tools/contentpack/contentpack.py)."""
    return "%s-%s.zip" % (pack["id"], pack["version"])


def kick_assets(manifest):
    """The \"assets\" list that Kick's PluginManifest reads, derived from the released content packs."""
    assets = []
    for cp in manifest.get("contentPacks", []):
        if cp.get("status") == "released":
            assets.append({"id": cp["id"], "name": cp["name"], "version": cp["version"], "url": cp["url"], "sha256": cp["sha256"],
                           "size": cp["size"], "required": cp["required"], "type": cp["kind"], "license": cp["license"]})
    return assets


def requires_ok(requirement, version):
    """\">=X.Y.Z\" is satisfied by that version or a newer one, a bare \"X.Y.Z\" only by exactly that version."""
    m = re.match(r"^(>=)?(\d+)\.(\d+)\.(\d+)$", requirement or "")
    if not m:
        return False
    need = tuple(int(x) for x in m.group(2, 3, 4))
    have = tuple(int(x) for x in version.split("."))
    return have >= need if m.group(1) else have == need


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def load_contentpack_tool():
    spec = importlib.util.spec_from_file_location("sb_contentpack", ROOT / "tools" / "contentpack" / "contentpack.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def url_kind(url):
    if url.startswith("https://"):
        return "https"
    if url.startswith("file://"):
        return "file"
    if re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:", url) or url.startswith("/") or "\\" in url:
        return "bad"
    return "relative"


def local_path_of(url, kind, manifest_dir):
    if kind == "file":
        return Path(url2pathname(urlparse(url).path))
    return manifest_dir / unquote(url)


def check_artifact(label, entry, expected_name, mode, manifest_dir, assets_dir, errors):
    """Checks url / sha256 / size of a released build or pack entry (the placeholders are checked by the caller)."""
    url = str(entry.get("url", ""))
    kind = url_kind(url)
    sha = entry.get("sha256", "")
    size = entry.get("size", 0)
    if mode == "local":
        if kind == "bad":
            errors.append("%s: url %r is neither https, file:// nor a relative path" % (label, url))
    elif kind != "https":
        errors.append("%s: 'released' entries need an https url" % label)
    if len(sha) != 64:
        errors.append("%s: 'released' entries need a sha256" % label)
    if not isinstance(size, int) or size <= 0:
        errors.append("%s: 'released' entries need a size" % label)
    elif size > MAX_ASSET_BYTES:
        errors.append("%s: %d bytes is over the limit of %d for one release asset" % (label, size, MAX_ASSET_BYTES))
    if kind != "bad" and url.rsplit("/", 1)[-1] != expected_name:
        errors.append("%s: the url must end with the asset name %s (got %r)" % (label, expected_name, url))
    if kind == "bad" or len(sha) != 64:
        return
    local = None
    if mode == "local" and kind in ("file", "relative"):
        local = local_path_of(url, kind, manifest_dir)
    elif assets_dir is not None:
        local = assets_dir / url.rsplit("/", 1)[-1]
    if local is not None:
        if not local.is_file():
            errors.append("%s: asset file %s does not exist" % (label, local))
        else:
            if local.stat().st_size != size:
                errors.append("%s: size %d != size of %s (%d)" % (label, size, local, local.stat().st_size))
            if sha256_file(local) != sha:
                errors.append("%s: sha256 does not match %s" % (label, local))


def check_placeholder(label, entry, errors):
    if entry.get("url") != "" or entry.get("sha256") != "" or entry.get("size") != 0:
        errors.append("%s: 'unbuilt' entries must keep the placeholders (url \"\", sha256 \"\", size 0)" % label)
    if "entry_point" in entry:
        errors.append("%s: 'unbuilt' entries have no entry_point" % label)


def check_content_pack(cp, manifest, errors):
    """The pack directory named by plugin.json's entry: valid manifest, same identity, loadable by this plugin version, intact."""
    pack_schema_path = ROOT / "schema" / "contentpack.schema.json"
    if not pack_schema_path.exists() or not isinstance(cp, dict) or "path" not in cp:
        return
    label = "content pack %s" % cp.get("id")
    pack_dir = ROOT / cp["path"]
    pack_manifest_file = pack_dir / "contentpack.json"
    if not pack_manifest_file.exists():
        errors.append("%s: manifest %s does not exist" % (label, pack_manifest_file))
        return
    try:
        pack_manifest = json.loads(pack_manifest_file.read_text(encoding="utf-8"))
    except ValueError as ex:
        errors.append("%s: failed to parse %s: %s" % (label, pack_manifest_file, ex))
        return
    try:
        tool = load_contentpack_tool()
        problems = tool.verify_directory(pack_dir, pack_manifest)
    except Exception as ex:  # noqa: BLE001 - report, do not crash the checker
        errors.append("%s: failed to verify %s: %s" % (label, pack_dir, ex))
        return
    errors.extend("%s: %s" % (label, p) for p in problems)
    if problems:
        return
    for key in ("id", "version", "kind", "license"):
        if cp.get(key) != pack_manifest.get(key):
            errors.append("%s: %s %r in plugin.json != %r in %s" % (label, key, cp.get(key), pack_manifest.get(key), pack_manifest_file))
    if "requires_plugin" in cp and cp["requires_plugin"] != pack_manifest.get("requires_plugin"):
        errors.append("%s: requires_plugin differs from the pack manifest" % label)
    if not requires_ok(pack_manifest.get("requires_plugin"), manifest.get("version", "")):
        errors.append("%s: requires_plugin %r is not satisfied by plugin version %s (the loader would ignore the pack)"
                      % (label, pack_manifest.get("requires_plugin"), manifest.get("version")))
    total = sum(f.get("size", 0) for f in pack_manifest.get("files", []))
    if total > MAX_ASSET_BYTES:
        errors.append("%s: %d bytes of files do not fit one release asset (limit %d): split the pack per instrument "
                      "(tools/contentpack/contentpack.py split) and declare every part" % (label, total, MAX_ASSET_BYTES))


def header_constant(path, pattern, what, errors):
    """The first capture group of `pattern` in the file, or None (and an error)."""
    try:
        text = (ROOT / path).read_text(encoding="utf-8")
    except OSError as e:
        errors.append("gui: cannot read %s: %s" % (path, e))
        return None
    m = re.search(pattern, text)
    if not m:
        errors.append("gui: %s not found in %s" % (what, path))
        return None
    return m.group(1)


def check_gui(manifest, errors):
    """The optional "gui" section must say what the module really offers: the extension ids of shell-clap/bitmap_gui_ext.h, the sizes of the editor and of the bitmap session, and only builds
    the manifest declares."""
    gui = manifest.get("gui")
    if not isinstance(gui, dict):
        return
    ext = header_constant("shell-clap/bitmap_gui_ext.h", r'#define\s+SAFIAN_EXT_BITMAP_GUI\s+"([^"]+)"', "SAFIAN_EXT_BITMAP_GUI", errors)
    host = header_constant("shell-clap/bitmap_gui_ext.h", r'#define\s+SAFIAN_EXT_BITMAP_GUI_HOST\s+"([^"]+)"', "SAFIAN_EXT_BITMAP_GUI_HOST", errors)
    if ext is not None and gui.get("extension") != ext:
        errors.append("gui.extension %r != SAFIAN_EXT_BITMAP_GUI %r (shell-clap/bitmap_gui_ext.h)" % (gui.get("extension"), ext))
    if host is not None and gui.get("host_extension", host) != host:
        errors.append("gui.host_extension %r != SAFIAN_EXT_BITMAP_GUI_HOST %r (shell-clap/bitmap_gui_ext.h)" % (gui.get("host_extension"), host))
    editor = "shell-juce/ui/Editor.h"
    for key, name in (("min_width", "kMinWidth"), ("min_height", "kMinHeight"), ("max_width", "kMaxWidth"), ("max_height", "kMaxHeight")):
        value = header_constant(editor, r"%s\s*=\s*(\d+)" % name, name, errors)
        if value is not None and key in gui and gui[key] != int(value):
            errors.append("gui.%s %r != %s %s (%s)" % (key, gui[key], name, value, editor))
    session = "shell-clap/bitmap/BitmapSession.h"
    for key, name in (("preferred_width", "kDefaultWidth"), ("preferred_height", "kDefaultHeight")):
        value = header_constant(session, r"%s\s*=\s*(\d+)" % name, name, errors)
        if value is not None and key in gui and gui[key] != int(value):
            errors.append("gui.%s %r != %s %s (%s)" % (key, gui[key], name, value, session))
    declared = set((manifest.get("builds") or {}).keys())
    for name in gui.get("builds", []):
        if name not in declared:
            errors.append("gui.builds: %r is not a build of this manifest" % name)


def check_manifest(manifest_path, mode="checked-in", assets_dir=None):
    """Returns (manifest or None, list of problems)."""
    manifest_path = Path(manifest_path)
    schema_path = ROOT / "schema" / "plugin-manifest.schema.json"
    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    errors = []

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        return None, ["cannot read %s: %s" % (manifest_path, e)]
    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    if mode == "local":
        schema["properties"]["builds"]["required"] = []  # only the built platforms are listed

    validate(manifest, schema, schema, "$", errors)
    if not isinstance(manifest, dict):
        return manifest, errors

    # --- cross checks against the build ---------------------------------------------------------
    m = re.search(r"project\(\s*\w+\s+VERSION\s+(\d+\.\d+\.\d+)", cmake_text)
    if not m:
        errors.append("CMakeLists.txt: no project(... VERSION x.y.z) found")
    elif manifest.get("version") != m.group(1):
        errors.append("version %r != CMake project VERSION %r" % (manifest.get("version"), m.group(1)))

    m = re.search(r'CLAP_ID\s+"([^"]+)"', cmake_text)
    if not m:
        errors.append("CMakeLists.txt: no CLAP_ID found")
    elif manifest.get("clap_id") != m.group(1):
        errors.append("clap_id %r != CLAP_ID in CMakeLists.txt %r" % (manifest.get("clap_id"), m.group(1)))

    check_gui(manifest, errors)

    manifest_dir = manifest_path.resolve().parent
    assets_dir = Path(assets_dir) if assets_dir else None
    builds = manifest.get("builds") or {}
    for name, build in builds.items():
        if not isinstance(build, dict):
            continue
        label = "builds.%s" % name
        if build.get("status") == "unbuilt":
            check_placeholder(label, build, errors)
            if mode == "released":
                errors.append("%s: a released manifest has no 'unbuilt' build" % label)
            if mode == "local":
                errors.append("%s: a local manifest lists only the built platforms (an 'unbuilt' entry has no SHA-256 and Kick rejects it)" % label)
        elif build.get("status") == "released":
            check_artifact(label, build, build_asset_name(manifest, name), mode, manifest_dir, assets_dir, errors)
            if mode != "checked-in" and not build.get("entry_point"):
                errors.append("%s: 'released' entries need the entry_point (the .clap inside the zip)" % label)
    if mode == "local" and not any(isinstance(b, dict) and b.get("status") == "released" for b in builds.values()):
        errors.append("builds: a local manifest needs at least one released build")

    # --- content packs ----------------------------------------------------------------------------
    seen = set()
    packs = [cp for cp in manifest.get("contentPacks", []) if isinstance(cp, dict)]
    for cp in packs:
        label = "contentPacks[%s]" % cp.get("id")
        if cp.get("id") in seen:
            errors.append("%s: declared twice" % label)
        seen.add(cp.get("id"))
        if cp.get("status") == "unbuilt":
            check_placeholder(label, cp, errors)
            if mode == "released":
                errors.append("%s: a released manifest has no 'unbuilt' content pack" % label)
            if mode == "local":
                errors.append("%s: a local manifest lists only the packs that were built" % label)
        elif cp.get("status") == "released":
            check_artifact(label, cp, pack_asset_name(cp), mode, manifest_dir, assets_dir, errors)
        check_content_pack(cp, manifest, errors)

    # --- the list Kick reads ------------------------------------------------------------------------
    if mode != "checked-in":
        if manifest.get("assets") != kick_assets(manifest):
            errors.append("assets: must mirror the released content packs (id, name, version, url, sha256, size, required, type, license)")
    elif "assets" in manifest:
        errors.append("assets: only a released / local manifest has it (the release job derives it from contentPacks)")

    return manifest, errors


def main(argv=None):
    parser = argparse.ArgumentParser(description="Validates plugin.json (see the header of this file)")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--released", action="store_true", help="check the release workflow's plugin.json.release")
    group.add_argument("--local", action="store_true", help="check the manifest of scripts/make-local-release.sh")
    parser.add_argument("--assets", help="released mode: directory of the release assets (their SHA-256 and size are compared)")
    parser.add_argument("path", nargs="?", default=str(ROOT / "plugin.json"))
    args = parser.parse_args(argv)
    mode = "released" if args.released else "local" if args.local else "checked-in"
    if args.assets and mode != "released":
        parser.error("--assets belongs to --released (a local manifest's files are found through its urls)")

    manifest_path = Path(args.path)
    manifest, errors = check_manifest(manifest_path, mode, args.assets)
    if errors:
        print("%s: %d problem(s)" % (manifest_path, len(errors)))
        for e in errors:
            print("  - " + e)
        return 1
    print("%s: OK (%s mode, schemaVersion %s, version %s, clap_id %s)"
          % (manifest_path.name, mode, manifest["schemaVersion"], manifest["version"], manifest["clap_id"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
