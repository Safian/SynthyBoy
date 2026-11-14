#!/bin/sh
# Builds and runs the output stage acceptance test (no CMake needed). Exit 0 = all checks pass, no compiler warnings.
set -e
cd "$(dirname "$0")/.."
out="${TMPDIR:-/tmp}/synthyboy_output_stage_host.$$"
c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror -I . tests/output_stage_host.cpp -o "$out"
"$out"; rc=$?
rm -f "$out"
exit $rc
