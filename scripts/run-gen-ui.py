#!/usr/bin/env python3
# SynthyBoy - runs the SDK's tools/gen_ui.py (all its options) and lifts its limit of 32 pages when ui.json has more.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: python3 scripts/run-gen-ui.py [--sdk DIR] <gen_ui.py options>        (--sdk defaults to $MPC_SDK)
#
# gen_ui.py (MAX_PAGES = 32) makes one tab of every page of ui.json. The layout variant with 34 tabs (scripts/mpc-ui-pages-v34.json, "tabLimit" 34) is a test
# of the device: whether the sub-tabs count in the limit of 32 tabs of the SDK (ui-reference 3.5) is not known. For up to 32 pages this script is exactly
# gen_ui.py; with more it sets gen_ui.MAX_PAGES to the number of pages in the file given with --ui and says so. The SDK itself is not changed.

import argparse
import importlib.util
import json
import os
import sys
from pathlib import Path


def main(argv=None):
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("--sdk", default=os.environ.get("MPC_SDK"))
    ap.add_argument("--ui", default=None)
    known, rest = ap.parse_known_args(argv)
    if not known.sdk:
        print("run-gen-ui.py: --sdk or MPC_SDK is needed", file=sys.stderr)
        return 2
    tools = Path(known.sdk) / "tools"
    path = tools / "gen_ui.py"
    if not path.is_file():
        print("run-gen-ui.py: %s not found" % path, file=sys.stderr)
        return 2
    sys.path.insert(0, str(tools))   # gen_ui.py imports make_ui_kit from its own folder
    spec = importlib.util.spec_from_file_location("sdk_gen_ui", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    args = list(rest)
    if known.ui:
        args += ["--ui", known.ui]
        pages = len(json.loads(Path(known.ui).read_text(encoding="utf-8")).get("pages", []))
        if pages > module.MAX_PAGES:
            print("run-gen-ui.py: NOTE: %d pages; the page limit of gen_ui.py (%d) is lifted to %d (not an SDK-conformant skin)"
                  % (pages, module.MAX_PAGES, pages))
            module.MAX_PAGES = pages
    return module.main(args)


if __name__ == "__main__":
    sys.exit(main())
