#!/bin/bash
# SynthyBoy - MPC overlay touch tests runner.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKTREE_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
cd "${WORKTREE_ROOT}"

TMP_BIN="/tmp/touch_host_test_$$"
trap 'rm -f "${TMP_BIN}" "${TMP_BIN}_asan"' EXIT

echo "=== Building and running touch_host (standard -std=c++17 -Wall -Wextra -Werror) ==="
c++ -std=c++17 -Wall -Wextra -Werror -I shell-mpc/overlay \
    -o "${TMP_BIN}" shell-mpc/overlay/tests/touch_host.cpp

"${TMP_BIN}"

echo "=== Building and running touch_host (-fsanitize=address,undefined) ==="
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -I shell-mpc/overlay \
    -o "${TMP_BIN}_asan" shell-mpc/overlay/tests/touch_host.cpp

"${TMP_BIN}_asan"

echo "=== ALL TOUCH TESTS PASSED ==="
