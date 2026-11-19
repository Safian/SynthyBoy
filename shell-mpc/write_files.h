// SynthyBoy - MPC shell: the one compile switch for the files the plugin and its GPU helper write for the developer (status, report and log files in /tmp).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// SYNTHYBOY_MPC_WRITE_FILES = 1 (the default when nothing defines it, and the value of today's package builds): the files are written as before. 0 (CMake: -DSYNTHYBOY_MPC_DEV_LOG=OFF, scripts/build-mpc.sh --release):
// the periodic status file is written only while the switch file /tmp/synthyboy-overlay-status-on exists, and the one-off report files (/tmp/synthyboy-mpc-profile.txt, the helper's
// /tmp/synthyboy-overlay-gpu-egl.txt), the error report of PollGuard and the helper's log file are not written at all (the helper's output goes to /dev/null). Files that carry state the
// plugin needs (the GPU clock's original setting, the plane's rest value) and the switch files the plugin READS are not affected. docs/MPC.md "Logging in the plugin".
#pragma once

#ifndef SYNTHYBOY_MPC_WRITE_FILES
#define SYNTHYBOY_MPC_WRITE_FILES 1
#endif
