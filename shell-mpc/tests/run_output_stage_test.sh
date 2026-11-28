#!/bin/sh
# Builds and runs the output stage acceptance test (the stage: core/include/synthyboy/output_stage.h; the factory presets through it: core/tests/test_output_stage_presets.cpp) (no CMake needed). Exit 0 = all checks pass, no compiler warnings.
set -e
cd "$(dirname "$0")/.."
out="${TMPDIR:-/tmp}/synthyboy_output_stage_host.$$"
trap 'rm -f "$out"' EXIT INT TERM
c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror -I . -I ../core/include tests/output_stage_host.cpp -o "$out"
"$out"
# the NEON form against the scalar loops, bit for bit (needs an ARM host: the NEON intrinsics; AArch64 also runs it with flush-to-zero set)
case "$(uname -m)" in
  arm64|aarch64)
    c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror -ffp-contract=off -DSB_TEST_NEON=1 -I . -I ../core/include tests/output_stage_lanes_host.cpp -o "$out"
    "$out"
    ;;
esac
