#!/usr/bin/env bash
# SynthyBoy - checks the COMPILED ARM code of the audio libraries for the instructions that would make a NEON form and its scalar form differ in the last bit:
#   fused multiply-add   AArch64: fmadd fmsub fnmadd fnmsub fmla fmls   32 bit ARM: vfma vfms vfnma vfnms (VFPv4; the MPC Key 61 build is armv7-a + NEON, which has none)
#   approximate reciprocal / reciprocal square root   AArch64: frecpe frecps frecpx frsqrte frsqrts   32 bit ARM: vrecpe vrecps vrsqrte vrsqrts
# and lists, for information, the non fused multiply-accumulate of 32 bit ARM (vmla / vmls .f32: the product is rounded before the add, the same bits as vmul + vadd).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/check-no-fma.sh [--arch arm32|arm64|all] [--build-dir DIR] [--no-build] [--jobs N] [--allow FILE] [--targets "t1 t2 ..."] [--objects FILE...] [--verbose] [--self-test]
#
#   --arch       arm32 (ARMv7 hard-float with NEON: the MPC Key 61) or arm64 (AArch64: the MPC XL) or all (default)
#   --build-dir  the cross build trees, <DIR>/<arch> (default: <temporary directory>/check-no-fma); an existing tree (configured with the toolchain files of cmake/toolchains) is reused
#   --no-build   do not configure or build: check the object files that are already in the trees
#   --targets    the CMake targets whose object files are checked (default: the audio libraries: synthyboy_core synthyboy_spectral synthyboy_granular synthyboy_sampleosc synthyboy_filters_serum)
#   --objects    check these object files (or static libraries) only, for the architecture of --arch (arm32 or arm64, not all): no build, no CMake tree
#   --allow      the list of documented exceptions (default: scripts/check-no-fma.allow): lines `<object path regex> <function regex> <class>  # reason`, class fma or approx
#   --verbose    print every function with a hit, also the allowed ones
#   --self-test  compile a three line source that has a fused multiply-add, a fused vector multiply-add and an estimate for both architectures and check that this script reports all three
#
# Exit status: 0 when every hit is an allowed one, 1 when there is a hit that is not (the list names the object, the function, the instruction and its number), 2 for a usage or build error.
# What it proves and what it does not: it reads the machine code of the build the shell packages (Release, the toolchain files of cmake/toolchains, GCC), per function. The NEON and the scalar
# forms of the engine are bit identical only while neither contains a fused operation or an estimate; the source relies on `#pragma GCC optimize ("fp-contract=off")` (dsp/simd.h, kernels.cpp,
# granular.cpp, sample_voice.cpp: no -ffp-contract flag is passed to the library targets), which a different compiler version may stop honouring. A new compiler or a new flag: run this again.
set -euo pipefail

me="check-no-fma.sh"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
scratch_root="/tmp/synthyboy-$(id -u)/$(printf '%s\n' "$repo_root" | cksum | cut -d' ' -f1)"
arch="all"
build_root="$scratch_root/check-no-fma"
build=1
allow_file="$repo_root/scripts/check-no-fma.allow"
targets="synthyboy_core synthyboy_spectral synthyboy_granular synthyboy_sampleosc synthyboy_filters_serum"
verbose=0
self_test=0
objects=()
jobs="$( (command -v nproc >/dev/null && nproc) || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch) arch="$2"; shift 2 ;;
        --build-dir) build_root="$2"; shift 2 ;;
        --no-build) build=0; shift ;;
        --jobs) jobs="$2"; shift 2 ;;
        --allow) allow_file="$2"; shift 2 ;;
        --targets) targets="$2"; shift 2 ;;
        --objects) shift; while [[ $# -gt 0 && "$1" != --* ]]; do objects+=("$1"); shift; done ;;
        --verbose) verbose=1; shift ;;
        --self-test) self_test=1; shift ;;
        -h|--help) sed -n '2,24p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "$me: unknown option $1 (see --help)" >&2; exit 2 ;;
    esac
done
case "$arch" in
    all) archs=(arm32 arm64) ;;
    arm32|arm64) archs=("$arch") ;;
    *) echo "$me: --arch must be arm32, arm64 or all" >&2; exit 2 ;;
esac
if [[ "$self_test" == 1 ]]; then
    tmp="$(mktemp -d)"
    cat > "$tmp/control.cpp" <<'EOS'
#include <arm_neon.h>
float mad (float a, float b, float c) { return a * b + c; }
float32x4_t vmad (float32x4_t a, float32x4_t b, float32x4_t c) { return vfmaq_f32 (c, a, b); }
float32x4_t vrec (float32x4_t a) { return vrecpeq_f32 (a); }
EOS
    ok=1
    for a in arm32 arm64; do
        if [[ "$a" == arm64 ]]; then cxx=aarch64-linux-gnu-g++; flags="-march=armv8-a"; else cxx=arm-linux-gnueabihf-g++; flags="-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard"; fi
        command -v "$cxx" >/dev/null 2>&1 || { echo "$me: $cxx not found" >&2; exit 2; }
        # shellcheck disable=SC2086
        "$cxx" -O2 $flags -c "$tmp/control.cpp" -o "$tmp/control-$a.o"
        n="$("$0" --arch "$a" --objects "$tmp/control-$a.o" --allow /dev/null | grep -c '^   FAIL' || true)"
        echo "self-test $a: $n of 3 planted instructions reported"
        [[ "$n" == 3 ]] || ok=0
    done
    rm -rf "$tmp"
    [[ "$ok" == 1 ]] && { echo "== self-test PASS"; exit 0; }
    echo "== self-test FAIL"
    exit 1
fi
if [[ ${#objects[@]} -gt 0 && "$arch" == all ]]; then
    echo "$me: --objects needs --arch arm32 or arm64" >&2
    exit 2
fi

toolchain_of() { case "$1" in arm32) echo "$repo_root/cmake/toolchains/armv7-linux-gnueabihf.cmake" ;; arm64) echo "$repo_root/cmake/toolchains/aarch64-linux-gnu.cmake" ;; esac; }
objdump_of() { case "$1" in arm32) echo "arm-linux-gnueabihf-objdump" ;; arm64) echo "aarch64-linux-gnu-objdump" ;; esac; }

# the mnemonic classes (extended regular expressions on the mnemonic field of objdump -d)
fma_re_arm64='^(fmadd|fmsub|fnmadd|fnmsub|fmla|fmls)$'
approx_re_arm64='^(frecpe|frecps|frecpx|frsqrte|frsqrts)$'
fma_re_arm32='^v(fma|fms|fnma|fnms)\.'
approx_re_arm32='^v(recpe|recps|rsqrte|rsqrts)\.'
info_re_arm32='^v(mla|mls|nmla|nmls)\.f32'
info_re_arm64='^$'   # (AArch64 has no non fused multiply-accumulate on floats)

hits_file="$(mktemp)"
trap 'rm -f "$hits_file"' EXIT
status=0
total_hits=0
total_allowed=0
for a in "${archs[@]}"; do
    od="$(objdump_of "$a")"
    command -v "$od" >/dev/null 2>&1 || { echo "$me: $od not found (apt-get install binutils-aarch64-linux-gnu binutils-arm-linux-gnueabihf)" >&2; exit 2; }
    files=()
    if [[ ${#objects[@]} -gt 0 ]]; then
        files=("${objects[@]}")
    else
        dir="$build_root/$a"
        if [[ "$build" == 1 ]]; then
            if [[ ! -f "$dir/CMakeCache.txt" ]]; then
                cmake -S "$repo_root" -B "$dir" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$(toolchain_of "$a")" \
                    -DSYNTHYBOY_BUILD_TESTS=OFF >/dev/null || { echo "$me: cmake configure of $a failed" >&2; exit 2; }
            fi
            # shellcheck disable=SC2086
            cmake --build "$dir" --target $targets --parallel "$jobs" >/dev/null || { echo "$me: build of $a failed" >&2; exit 2; }
        fi
        for t in $targets; do
            while IFS= read -r f; do files+=("$f"); done < <(find -L "$dir" -path "*/CMakeFiles/$t.dir/*" -name '*.o' | sort)
        done
        if [[ ${#files[@]} -eq 0 ]]; then
            echo "$me: no object files of [$targets] under $dir (build them, or point --build-dir at a tree that has them)" >&2
            exit 2
        fi
    fi
    fma_re="fma_re_$a"; approx_re="approx_re_$a"; info_re="info_re_$a"
    echo "== $a: $(( ${#files[@]} )) object file(s), $("$od" --version | head -1)"
    for f in "${files[@]}"; do
        # one line per function and class: <class> <function> <mnemonic> <count>; classes fma, approx, info
        "$od" -d --no-show-raw-insn -C "$f" 2>/dev/null | awk -F'\t' -v fma="${!fma_re}" -v approx="${!approx_re}" -v info="${!info_re}" '
            /^[0-9a-f]+ <.*>:$/ { fn = $0; sub(/^[0-9a-f]+ </, "", fn); sub(/>:$/, "", fn); next }
            NF >= 2 {
                m = $2; sub(/ .*$/, "", m)
                if (m ~ fma) c["fma\t" fn "\t" m]++
                else if (m ~ approx) c["approx\t" fn "\t" m]++
                else if (info != "^$" && m ~ info) c["info\t" fn "\t" m]++
            }
            END { for (k in c) print k "\t" c[k] }' | sort > "$hits_file" || true
        [[ -s "$hits_file" ]] || { [[ "$verbose" == 1 ]] && echo "   ok   $(basename "$f"): no fused or estimate instruction"; continue; }
        while IFS=$'\t' read -r cls fn mn cnt; do
            if [[ "$cls" == info ]]; then
                [[ "$verbose" == 1 ]] && echo "   info $(basename "$f") $fn: $cnt x $mn (not fused)"
                continue
            fi
            allowed=0
            if [[ -f "$allow_file" ]]; then
                while IFS= read -r line; do
                    line="${line%%#*}"
                    [[ -z "${line//[[:space:]]/}" ]] && continue
                    read -r oreg freg creg <<<"$line"
                    if [[ "$cls" == "$creg" && "$f" =~ $oreg && "$fn" =~ $freg ]]; then allowed=1; break; fi
                done < "$allow_file"
            fi
            total_hits=$((total_hits + 1))
            if [[ "$allowed" == 1 ]]; then
                total_allowed=$((total_allowed + 1))
                [[ "$verbose" == 1 ]] && echo "   allowed $cls $(basename "$f") $fn: $cnt x $mn"
            else
                status=1
                echo "   FAIL $cls $(basename "$f") $fn: $cnt x $mn"
            fi
        done < "$hits_file"
    done
done
echo "== $total_hits hit(s) in the checked functions, $total_allowed of them allowed (documented in $(basename "$allow_file")), $((total_hits - total_allowed)) not"
if [[ "$status" == 0 ]]; then
    echo "== PASS: no fused multiply-add and no reciprocal / reciprocal square root estimate outside the documented exceptions"
else
    echo "== FAIL: see the FAIL lines above (a function with a fused operation or an estimate is not the bit exact twin of its scalar / NEON form)"
fi
exit "$status"
