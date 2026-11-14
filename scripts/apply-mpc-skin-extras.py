#!/usr/bin/env python3
# SynthyBoy - applies the modules of scripts/mpc-skin-extras/ to a generated MPC "Plugin Skins" folder (stdlib only).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: python3 scripts/apply-mpc-skin-extras.py --skins <Plugin Skins dir> --params docs/params.json [--meta skin-meta.json] [--extras-dir DIR]
#
# Runs after the SDK gen_ui.py. Every DIR/*.py (name order) defines apply(tui, qlinks, ctx); see DIR/README.md.
# TUI.json and Q-Links.json are rewritten only if a module changed them. Exit 0 ok, 1 on any error (message on stderr).

import argparse
import importlib.util
import json
import sys
from pathlib import Path

DUP = "\x00"  # separator of the stored name of a repeated key: "version\x00 1" is written back as "version"
DEFAULT_EXTRAS = Path(__file__).resolve().parent / "mpc-skin-extras"


class ExtrasError(Exception):
    pass


def _pairs_to_dict(pairs):
    out = {}
    for key, value in pairs:
        stored, n = key, 0
        while stored in out:
            n += 1
            stored = "%s%s%d" % (key, DUP, n)
        out[stored] = value
    return out


def load_json(text):
    return json.loads(text, object_pairs_hook=_pairs_to_dict)


def dump_json(value, indent=0):
    """Same layout as the SDK's gen_ui.py dump_json (4 spaces, ensure_ascii off)."""
    pad, inner = " " * indent, " " * (indent + 4)
    if isinstance(value, dict):
        if not value:
            return "{}"
        parts = [inner + json.dumps(str(k).split(DUP)[0]) + ": " + dump_json(v, indent + 4) for k, v in value.items()]
        return "{\n" + ",\n".join(parts) + "\n" + pad + "}"
    if isinstance(value, (list, tuple)):
        if not value:
            return "[]"
        return "[\n" + ",\n".join(inner + dump_json(v, indent + 4) for v in value) + "\n" + pad + "]"
    if isinstance(value, bool):
        return "true" if value else "false"
    if value is None:
        return "null"
    if isinstance(value, float):
        return repr(value)
    if isinstance(value, int):
        return str(value)
    return json.dumps(value, ensure_ascii=False)


class Context:
    def __init__(self, tui, params, skins_dir, meta=None):
        self._tui = tui
        self._index = {p["key"]: i for i, p in enumerate(params["params"])}
        self._keys = [p["key"] for p in params["params"]]
        self.param_count = len(self._keys)   # entries of params.json; the plugin's own parameters (display, pad pressure) follow them
        self._extra = {}                     # index >= param_count -> key, registered by a module (add_param_key)
        self.skins_dir = Path(skins_dir)
        self.meta = meta   # skin-meta.json of gen-mpc-ui.py (names of the controls, tab structure) or None

    def param_key(self, index):
        if index in self._extra:
            return self._extra[index]
        if not 0 <= index < len(self._keys):
            raise ExtrasError("parameter index %r is not in params.json" % (index,))
        return self._keys[index]

    def add_param_key(self, index, key):
        """Names a parameter of the shell that params.json does not list (index >= param_count) so that the later modules (80_layout, 85_names) can treat a control bound to it like any other."""
        if index < len(self._keys) or key in self._index or key in self._extra.values() or index in self._extra:
            raise ExtrasError("cannot register parameter %r as %r: index inside params.json or already registered" % (index, key))
        self._extra[index] = key

    def param_index(self, key):
        if key not in self._index:
            raise ExtrasError("unknown parameter key %r (not in params.json)" % (key,))
        return self._index[key]

    def tab(self, title):
        tabs = self._tui["pageData"]["tabs"]
        found = [t for t in tabs if t.get("tabName") == title]
        if len(found) != 1:
            names = ", ".join(repr(t.get("tabName")) for t in tabs)
            raise ExtrasError("%s tab %r; tabs are: %s" % ("ambiguous" if found else "unknown", title, names))
        return found[0]

    def add_component_definition(self, name, obj):
        defs = self._tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
        if any(d.get("key") == name for d in defs):
            raise ExtrasError("component definition %r already exists" % (name,))
        defs.append({"key": name, "value": obj})


def load_module(path):
    spec = importlib.util.spec_from_file_location("mpc_skin_extra_" + path.stem, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if not callable(getattr(module, "apply", None)):
        raise ExtrasError("has no apply(tui, qlinks, ctx) function")
    return module


def run(skins_dir, params_path, extras_dir=DEFAULT_EXTRAS, meta_path=None):
    skins_dir, extras_dir = Path(skins_dir), Path(extras_dir)
    tui_path, ql_path = skins_dir / "TUI.json", skins_dir / "Q-Links.json"
    for p in (tui_path, ql_path, Path(params_path)):
        if not p.is_file():
            raise ExtrasError("missing file %s" % p)
    if not extras_dir.is_dir():
        raise ExtrasError("extras directory %s does not exist" % extras_dir)
    tui_text, ql_text = tui_path.read_text(encoding="utf-8"), ql_path.read_text(encoding="utf-8")
    tui, qlinks = load_json(tui_text), load_json(ql_text)
    params = json.loads(Path(params_path).read_text(encoding="utf-8"))
    meta = None
    if meta_path:
        if not Path(meta_path).is_file():
            raise ExtrasError("missing file %s" % meta_path)
        meta = json.loads(Path(meta_path).read_text(encoding="utf-8"))
    ctx = Context(tui, params, skins_dir, meta)
    for path in sorted(extras_dir.glob("*.py")):
        try:
            load_module(path).apply(tui, qlinks, ctx)
        except ExtrasError as e:
            raise ExtrasError("%s: %s" % (path.name, e)) from None
        except Exception as e:  # a module bug must stop the build with the module named
            raise ExtrasError("%s: %s: %s" % (path.name, type(e).__name__, e)) from None
    new_tui, new_ql = dump_json(tui) + "\n", dump_json(qlinks) + "\n"
    # Compare against the re-serialized original, so only a real change rewrites a file.
    for path, old, new in ((tui_path, tui_text, new_tui), (ql_path, ql_text, new_ql)):
        if new != old:
            path.write_text(new, encoding="utf-8")


def main(argv=None):
    ap = argparse.ArgumentParser(description="Apply scripts/mpc-skin-extras/*.py modules to a generated MPC Plugin Skins folder.")
    ap.add_argument("--skins", required=True, help="the generated 'Plugin Skins' folder")
    ap.add_argument("--params", required=True, help="docs/params.json")
    ap.add_argument("--meta", default=None, help="skin-meta.json written by gen-mpc-ui.py --layout (the modules that need it fail without)")
    ap.add_argument("--extras-dir", default=str(DEFAULT_EXTRAS), help="folder of modules (default scripts/mpc-skin-extras)")
    args = ap.parse_args(argv)
    try:
        run(args.skins, args.params, args.extras_dir, args.meta)
    except ExtrasError as e:
        print("apply-mpc-skin-extras.py: error: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
