#!/usr/bin/env python3
# SynthyBoy - validates plugin.json against schema/plugin-manifest.schema.json (stdlib only) and against CMakeLists.txt.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: python3 -I scripts/check-manifest.py [path/to/plugin.json]
#
# The schema validator implements only the subset of JSON Schema (draft 2020-12) that the schema uses:
# type, required, enum, const, pattern, properties, additionalProperties, items, minimum, minLength, minItems and local
# "$ref" ("#/$defs/..."). A schema keyword outside this subset is an error (it must not be silently ignored).
# Cross checks: version == project(... VERSION x.y.z) and clap_id == CLAP_ID in CMakeLists.txt; a build marked
# "released" needs url / sha256 / size, an "unbuilt" one must have the empty placeholders.

import json
import re
import sys
from pathlib import Path

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


def main():
    manifest_path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "plugin.json"
    schema_path = ROOT / "schema" / "plugin-manifest.schema.json"
    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    errors = []

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        print("cannot read %s: %s" % (manifest_path, e))
        return 1
    schema = json.loads(schema_path.read_text(encoding="utf-8"))

    validate(manifest, schema, schema, "$", errors)

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

    for name, build in (manifest.get("builds") or {}).items():
        if not isinstance(build, dict):
            continue
        if build.get("status") == "unbuilt":
            if build.get("url") != "" or build.get("sha256") != "" or build.get("size") != 0:
                errors.append("builds.%s: 'unbuilt' entries must keep the placeholders (url \"\", sha256 \"\", size 0)" % name)
        elif build.get("status") == "released":
            if not str(build.get("url", "")).startswith("https://") or len(build.get("sha256", "")) != 64 or not build.get("size"):
                errors.append("builds.%s: 'released' entries need an https url, a sha256 and a size" % name)

    if errors:
        print("%s: %d problem(s)" % (manifest_path, len(errors)))
        for e in errors:
            print("  - " + e)
        return 1
    print("%s: OK (schemaVersion %s, version %s, clap_id %s)" % (manifest_path.name, manifest["schemaVersion"], manifest["version"], manifest["clap_id"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
