# SynthyBoy - MPC skin extra: the Q-Link frames (qlinkBoundsData) of every tab, in one place.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Four equal column frames on every tab: x = 16 + 320 * c, width 288 (c = 0 .. 3): 16 px from the two edges of the screen, 32 px between the frames.
# The frame encloses a whole column; no control, label or picture may stick out of it horizontally (scripts/tests/test_mpc_skin_frames.py).
# The orange "Q" badge of the host sits in the top left corner of a frame (about 38 x 38 px): nothing is placed in x .. x + 40, y .. y + 40 of the frame.
# This module sets x and width only; the y and the height of a frame come from the tab: 0 and 624 on the plain tabs (the generator's value),
# 301 and 323 on the tabs with a picture in the upper half (Env 1-4, Osc 1-3: 10_env.py and 20_osc.py), where the frame covers the lower two rows only.
# It runs last (name order) so that it is the only place that knows the frame x and width.

FRAME_X0 = 16
FRAME_PITCH = 320
FRAME_W = 288
COLUMNS = 4


def frame_x(column):
    return FRAME_X0 + FRAME_PITCH * column


def apply(tui, qlinks, ctx):
    for tab in tui["pageData"]["tabs"]:
        old = tab["qlinkBoundsData"]
        if len(old) != COLUMNS:
            raise ValueError("tab %r has %d Q-Link frames, expected %d" % (tab["tabName"], len(old), COLUMNS))
        tab["qlinkBoundsData"] = ["%d %s %d %s" % (frame_x(c), b.split()[1], FRAME_W, b.split()[3]) for c, b in enumerate(old)]
