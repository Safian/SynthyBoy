#!/usr/bin/env bash
# SynthyBoy - fails if a .clap module exports anything but clap_entry and synthyboy_clap_get_entry (the bitmap GUI links its own JUCE statically: nothing of it may be visible
# to the host or to another JUCE in the same process, docs/BITMAP-GUI-EXTENSION.md section 1).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/check-clap-exports.sh <synthyboy.clap>     (a macOS .clap bundle directory is accepted)
#        scripts/check-clap-exports.sh --self-test
#
# The exported (global, defined) symbols of the module are listed with nm (Mach-O: nm -gU, ELF: nm -D --defined-only) and compared with the two allowed names. Linker bookkeeping symbols that
# every ELF / Mach-O shared object may carry (_init, _fini, __bss_start, _edata, _end, __mh_bundle_header, __mh_dylib_header) are ignored. Exit status: 0 only the allowed ones, 1 anything else
# (listed) or none of the two, 2 usage.
#
# Environment: NM (default nm), CC (compiler for --self-test, default cc).

set -euo pipefail

NM="${NM:-nm}"
allowed_re='^(clap_entry|synthyboy_clap_get_entry)$'
bookkeeping_re='^(_init|_fini|__bss_start|_edata|_end|__mh_bundle_header|__mh_dylib_header|_mh_bundle_header|_mh_dylib_header)$'

exported_symbols()
{
    local file="$1" os
    os="$(uname -s)"
    if [[ -d "$file" ]]; then
        file="$file/Contents/MacOS/$(basename "${file%.clap}")"
    fi
    if [[ "$os" == "Darwin" ]]; then
        "$NM" -gU "$file" | awk 'NF >= 2 { print $NF }' | sed 's/^_//'
    else
        "$NM" -D --defined-only "$file" | awk 'NF >= 2 { print $NF }' | sed 's/@.*$//'
    fi
}

check_module()
{
    local file="$1" symbols extra count seen=0
    if [[ ! -e "$file" ]]; then
        echo "check-clap-exports: $file: no such file" >&2
        return 2
    fi
    symbols="$(exported_symbols "$file" | grep -Ev "$bookkeeping_re" | sort -u)"
    extra="$(grep -Ev "$allowed_re" <<< "$symbols" | grep -v '^$' || true)"
    if grep -qx 'clap_entry' <<< "$symbols"; then seen=$((seen + 1)); fi
    if grep -qx 'synthyboy_clap_get_entry' <<< "$symbols"; then seen=$((seen + 1)); fi
    if [[ -n "$extra" ]]; then
        echo "check-clap-exports: $file exports more than clap_entry and synthyboy_clap_get_entry:" >&2
        sed -n '1,20p' <<< "$extra" | sed 's/^/    /' >&2
        count="$(wc -l <<< "$extra" | tr -d ' ')"
        if [[ "$count" -gt 20 ]]; then echo "    ... ($count symbols)" >&2; fi
        return 1
    fi
    if [[ "$seen" -ne 2 ]]; then
        echo "check-clap-exports: $file does not export both clap_entry and synthyboy_clap_get_entry" >&2
        return 1
    fi
    echo "check-clap-exports: OK $file exports clap_entry and synthyboy_clap_get_entry only"
    return 0
}

self_test()
{
    local cc="${CC:-cc}" dir os flags
    dir="$(mktemp -d)"
    trap 'rm -rf "$dir"' RETURN
    os="$(uname -s)"
    if [[ "$os" == "Darwin" ]]; then flags="-dynamiclib"; else flags="-shared -fPIC"; fi
    printf 'const int clap_entry = 1;\nint synthyboy_clap_get_entry(void){return 2;}\n' > "$dir/good.c"
    printf 'const int clap_entry = 1;\nint synthyboy_clap_get_entry(void){return 2;}\nint juce_something(void){return 3;}\n' > "$dir/bad.c"
    printf 'int unrelated(void){return 4;}\n' > "$dir/missing.c"
    for name in good bad missing; do
        # shellcheck disable=SC2086
        "$cc" $flags -o "$dir/$name.so" "$dir/$name.c"
    done
    check_module "$dir/good.so" > /dev/null || { echo "check-clap-exports: self-test failed: a module with exactly the two symbols was rejected" >&2; return 1; }
    if check_module "$dir/bad.so" > /dev/null 2>&1; then echo "check-clap-exports: self-test failed: an extra export was accepted" >&2; return 1; fi
    if check_module "$dir/missing.so" > /dev/null 2>&1; then echo "check-clap-exports: self-test failed: a module without clap_entry was accepted" >&2; return 1; fi
    echo "check-clap-exports: self-test OK"
}

case "${1:-}" in
    --self-test) self_test ;;
    "") echo "usage: check-clap-exports.sh <synthyboy.clap> | --self-test" >&2; exit 2 ;;
    *) check_module "$1" ;;
esac
