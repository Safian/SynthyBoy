#!/bin/sh
# Builds and runs the event hand-over acceptance test (no CMake needed), also under ThreadSanitizer. Exit 0 = all checks pass.
set -e
cd "$(dirname "$0")/.."
out="${TMPDIR:-/tmp}/synthyboy_event_inbox_host.$$"
trap 'rm -f "$out"' EXIT INT TERM
c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow -Werror -I . tests/event_inbox_host.cpp -o "$out" -lpthread
"$out"
c++ -std=c++17 -O1 -g -fsanitize=thread -I . tests/event_inbox_host.cpp -o "$out" -lpthread
"$out"
