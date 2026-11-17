#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests that docs/PARAMETERS.md is up to date with docs/params.json.
Fails when docs/PARAMETERS.md is stale relative to scripts/gen-params-doc.py.
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def test_params_doc_up_to_date():
    params_json = os.path.join(ROOT, "docs", "params.json")
    params_doc = os.path.join(ROOT, "docs", "PARAMETERS.md")
    gen_script = os.path.join(ROOT, "scripts", "gen-params-doc.py")

    assert os.path.exists(params_json), f"{params_json} missing"
    assert os.path.exists(params_doc), f"{params_doc} missing"
    assert os.path.exists(gen_script), f"{gen_script} missing"

    with open(params_json, "r", encoding="utf-8") as f:
        data = json.load(f)
    expected_count = data["count"]

    with open(params_doc, "r", encoding="utf-8") as f:
        doc_content = f.read()

    assert f"**{expected_count} parameters.**" in doc_content, (
        f"docs/PARAMETERS.md does not contain '**{expected_count} parameters.**'"
    )

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp_out = os.path.join(tmpdir, "PARAMETERS.md")
        res = subprocess.run(
            [sys.executable, "-I", gen_script, params_json, tmp_out],
            capture_output=True,
            text=True,
        )
        assert res.returncode == 0, f"gen-params-doc.py failed: {res.stderr}"

        with open(tmp_out, "r", encoding="utf-8") as f:
            generated_content = f.read()

        assert doc_content == generated_content, (
            "docs/PARAMETERS.md is stale relative to docs/params.json. "
            "Regenerate with: python3 -I scripts/gen-params-doc.py docs/params.json docs/PARAMETERS.md"
        )


class TestParamsDoc(unittest.TestCase):
    def test_up_to_date(self):
        test_params_doc_up_to_date()


if __name__ == "__main__":
    unittest.main()
