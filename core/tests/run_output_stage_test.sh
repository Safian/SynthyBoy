#!/bin/sh
# Builds and runs the output stage unit tests (core/tests/test_output_stage.cpp).
# Exit 0 = all checks pass, no compiler warnings.
set -e
cd "$(dirname "$0")/../.."
out="${TMPDIR:-/tmp}/synthyboy_test_output_stage.$$"
trap 'rm -f "$out"' EXIT INT TERM
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror -I core/include -I core/tests -I shell-mpc core/tests/test_output_stage.cpp core/tests/test_main.cpp -o "$out"
"$out"
