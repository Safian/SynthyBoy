#!/usr/bin/env python3
# SynthyBoy - the host-saved fixture that shell-mpc/tests/programs_test.cpp resolves in every program-list mode still carries the number and the name the test uses.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
import importlib.util
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("gen_mpc_xpl", HERE.parent / "gen-mpc-xpl.py")
gen_mpc_xpl = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gen_mpc_xpl)
FIXTURE = HERE / "data" / "synthyboy-host-saved-Space-Wobble-Bass.xpl"


class ProgramGroupsFixture(unittest.TestCase):
    def test_chunk_head(self):
        chunk = gen_mpc_xpl.parse_xpl(FIXTURE.read_text(encoding="utf-8"))["chunk_bytes"]
        self.assertTrue(chunk.startswith(b'{"mpcProgram":20,"mpcProgramName":"Space Wobble Bass",'))


if __name__ == "__main__":
    unittest.main()
