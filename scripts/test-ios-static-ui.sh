#!/usr/bin/env bash
# SynthyBoy - builds the static UI library for the iOS SDK and proves it (docs/ARCHITECTURE.md 4.3, docs/BITMAP-GUI-EXTENSION.md section 14).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/test-ios-static-ui.sh [--sdk iphonesimulator|iphoneos] [--archs "arm64;x86_64"] [--build-dir DIR] [--juce7 DIR] [--no-run]
#
#   1. configure + build with -DCMAKE_SYSTEM_NAME=iOS -DSYNTHYBOY_STATIC_UI=ON -DSYNTHYBOY_WITH_VITAL=OFF: libsynthyboy_static_ui.a, the link list, and the two test programs
#   2. scripts/check-static-ui-symbols.py (the library exports synthyboy_clap_get_entry and nothing else; the parts before the partial link are checked for the JUCE namespace)
#      and scripts/check-no-vital.sh (the iOS rule) over the library and the archives it links
#   3. (simulator SDK, unless --no-run) the tests of the bitmap GUI run IN the iOS simulator against the linked library: the same 31 tests and the goldens of the own engine
#   4. (with --juce7 DIR, or when Kick's JUCE 7.0.12 cache is found) tests/ios-juce7: a program that links JUCE 7 whole and the library, runs both, and fails on "implemented in both"
#      of the Objective-C runtime
# --sdk iphoneos builds for the device (arm64) and runs steps 1, 2 and the link of step 4 (a device cannot run here).
# The simulator is the one of $SYNTHYBOY_SIMULATOR_UDID, or a device created for the run and deleted at its end. Network access: only what CMake's FetchContent already does (JUCE, CLAP headers).

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sdk="iphonesimulator"
archs="arm64"
build="$root/build-ios-ui-test"
juce7="${JUCE7_DIR:-$HOME/Documents/Projects/Safian/BehringerKick/.3rdPartySourceCache/JUCE-7.0.12}"
run=1
while [[ $# -gt 0 ]]; do
    case "$1" in
        --sdk) sdk="$2"; shift 2 ;;
        --archs) archs="$2"; shift 2 ;;
        --build-dir) build="$2"; shift 2 ;;
        --juce7) juce7="$2"; shift 2 ;;
        --no-run) run=0; shift ;;
        *) echo "usage: $0 [--sdk iphonesimulator|iphoneos] [--archs \"arm64;x86_64\"] [--build-dir DIR] [--juce7 DIR] [--no-run]" >&2; exit 2 ;;
    esac
done
[[ "$sdk" == "iphoneos" ]] && run=0
[[ "$build" == /* ]] || build="$PWD/$build"

step() { printf '\n== %s\n' "$*"; }
extra=()
[[ -n "${FETCHCONTENT_SOURCE_DIR_JUCE:-}" ]] && extra+=("-DFETCHCONTENT_SOURCE_DIR_JUCE=$FETCHCONTENT_SOURCE_DIR_JUCE")
[[ -n "${FETCHCONTENT_SOURCE_DIR_CLAP:-}" ]] && extra+=("-DFETCHCONTENT_SOURCE_DIR_CLAP=$FETCHCONTENT_SOURCE_DIR_CLAP")

step "1. configure and build for the $sdk SDK ($archs)"
cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$sdk" -DCMAKE_OSX_ARCHITECTURES="$archs" \
    -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=ON -DSYNTHYBOY_WITH_VITAL=OFF -DSYNTHYBOY_STATIC_UI=ON -DCMAKE_BUILD_TYPE=Release ${extra[@]+"${extra[@]}"}
cmake --build "$build" --target synthyboy_static_ui synthyboy_test_bitmap_gui synthyboy_test_bitmap_golden --parallel
lib="$build/shell-clap/libsynthyboy_static_ui.a"
parts="$build/shell-clap/libsynthyboy_static_ui_parts.a"
list="$build/shell-clap/synthyboy_static_ui.link.txt"
lipo -info "$lib"

step "2. symbols and the Vital rule"
python3 -I "$root/scripts/check-static-ui-symbols.py" --self-test
python3 -I "$root/scripts/check-static-ui-symbols.py" "$lib"
echo "-- before the partial link (the parts): the JUCE namespace is what separates the C++ names, the Objective-C classes carry its prefix"
nm -g -U -j "$parts" | grep -c '^_OBJC_CLASS_\$_synthyboy_juce8_' | sed 's/^/   prefixed Objective-C classes: /'
if nm -g -U -j "$parts" | grep -E '^_OBJC_CLASS_\$_(Juce|FileChooser|iOSAudio)' ; then echo "an Objective-C class of JUCE has no prefix" >&2; exit 1; fi
"$root/scripts/check-no-vital.sh" --self-test
archives=("$lib")
while IFS= read -r line; do
    [[ -z "$line" || "$line" == \#* || "$line" == -* ]] && continue
    archives+=("$line")
done < "$list"
"$root/scripts/check-no-vital.sh" "${archives[@]}"

if [[ "$run" == 1 ]]; then
    step "3. the tests of the bitmap GUI in the iOS simulator"
    udid="${SYNTHYBOY_SIMULATOR_UDID:-}"
    created=0
    if [[ -z "$udid" ]]; then
        read -r devtype runtime < <(xcrun simctl list -j runtimes | python3 -I -c '
import json, sys
best = None
for r in json.load(sys.stdin)["runtimes"]:
    if not r.get("isAvailable") or r.get("platform") != "iOS":
        continue
    phones = [t["identifier"] for t in r.get("supportedDeviceTypes", []) if t.get("productFamily") == "iPhone"]
    if phones:
        key = [int(x) for x in r["version"].split(".")]
        if best is None or key > best[0]:
            best = (key, phones[-1], r["identifier"])
print(best[1], best[2]) if best else sys.exit(1)') || { echo "no iOS simulator runtime with an iPhone is installed" >&2; exit 1; }
        udid="$(xcrun simctl create "synthyboy-ios-ui-test" "$devtype" "$runtime")"
        created=1
    fi
    cleanup() { if [[ "$created" == 1 ]]; then xcrun simctl shutdown "$udid" >/dev/null 2>&1 || true; xcrun simctl delete "$udid" >/dev/null 2>&1 || true; fi; }
    trap cleanup EXIT
    xcrun simctl boot "$udid" >/dev/null 2>&1 || true
    xcrun simctl bootstatus "$udid" >/dev/null
    work="$(mktemp -d)"
    for test in synthyboy_test_bitmap_gui synthyboy_test_bitmap_golden; do
        echo "-- $test"
        SIMCTL_CHILD_SYNTHYBOY_USER_DIR="$work/$test" \
            xcrun simctl spawn "$udid" "$build/tests/bitmap-gui/$test.app/$test" 2>&1 | tee "$work/$test.log" | tail -4
        grep -q " 0 failed case(s)" "$work/$test.log" || { echo "$test FAILED" >&2; exit 1; }
        if grep -q "implemented in both" "$work/$test.log"; then echo "Objective-C class implemented twice" >&2; exit 1; fi
    done
fi

if [[ -d "$juce7/modules/juce_core" ]]; then
    step "4. JUCE 7 and the static UI library in one binary ($juce7)"
    clap_include="$(sed -n 's/^CLAP_INCLUDE_DIR:[A-Z]*=//p' "$build/CMakeCache.txt")"
    j7build="$build/ios-juce7"
    # JUCE 7.0.12 builds its helper tool (juceaide) for the Mac that runs this script; against the macOS 15+ SDK it only compiles for an older deployment target (it calls an obsoleted function)
    MACOSX_DEPLOYMENT_TARGET=12.0 cmake -S "$root/tests/ios-juce7" -B "$j7build" -G Ninja \
        -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$sdk" -DCMAKE_OSX_ARCHITECTURES="$archs" -DCMAKE_BUILD_TYPE=Release \
        -DJUCE7_DIR="$juce7" -DSYNTHYBOY_SOURCE_DIR="$root" -DSYNTHYBOY_STATIC_UI_LIB="$lib" -DSYNTHYBOY_STATIC_UI_LINK_LIST="$list" -DCLAP_INCLUDE_DIR="$clap_include"
    cmake --build "$j7build" --target ios_static_ui_with_juce7 --parallel
    echo "-- linked: no duplicate symbol with JUCE 7 linked whole"
    if [[ "$run" == 1 ]]; then
        xcrun simctl spawn "$udid" "$j7build/ios_static_ui_with_juce7.app/ios_static_ui_with_juce7" > "$work/juce7.out" 2> "$work/juce7.err" || { cat "$work/juce7.out" "$work/juce7.err"; echo "the two JUCEs do not run side by side" >&2; exit 1; }
        cat "$work/juce7.out"
        if grep -qi "implemented in both" "$work/juce7.err"; then cat "$work/juce7.err"; echo "Objective-C class implemented twice" >&2; exit 1; fi
        echo "-- the Objective-C runtime reports no class implemented in both"
    fi
else
    echo "-- step 4 skipped: no JUCE 7 source at $juce7 (--juce7 DIR)"
fi
echo; echo "test-ios-static-ui: OK"
