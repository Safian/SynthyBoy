#!/usr/bin/env bash
# SynthyBoy - standalone ARM32 / AArch64 denormal-protection verification runner.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage:
#   shell-mpc/tests/run_arm_denormal_check.sh [--plugin <path-to-synthyboy_mpc.so|.dylib>] [--cross-check]
#
# Runs the standalone target-side/host-side check (shell-mpc/tests/arm_denormal_check.cpp)
# natively with -O2 -Wall -Wextra -Wpedantic -Wshadow -Werror.
# When --plugin <lib> and MPC_SDK are provided, also runs shell-mpc/tests/denormal_host.cpp
# against the compiled MPC VST2 plugin binary.
# When --cross-check is passed, cross-compiles static ARM32 (armv7) and AArch64 (arm64)
# binaries if the GNU cross-compilers are installed, or prints the exact reproducible commands.

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
shell_dir="$(cd "${script_dir}/.." && pwd)"
repo_root="$(cd "${shell_dir}/.." && pwd)"

plugin_path=""
cross_check=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --plugin)
            [[ $# -ge 2 ]] || { echo "run_arm_denormal_check.sh: --plugin needs a path" >&2; exit 2; }
            plugin_path="$2"
            shift 2
            ;;
        --cross-check)
            cross_check=1
            shift
            ;;
        *)
            echo "run_arm_denormal_check.sh: unknown option: $1" >&2
            exit 2
            ;;
    esac
done

tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/sb-denormal-check.XXXXXX")"
trap 'rm -rf "${tmp_dir}"' EXIT

neon_defs=()
case "$(uname -m)" in
    arm64|aarch64)
        neon_defs+=("-DSB_TEST_NEON=1")
        ;;
esac

echo "=== Building and running arm_denormal_check (native $(uname -m)) ==="
c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow -Werror \
    ${neon_defs[@]+"${neon_defs[@]}"} \
    -I "${shell_dir}" \
    -I "${repo_root}/core/include" \
    -I "${repo_root}/core/engine-own" \
    "${script_dir}/arm_denormal_check.cpp" \
    -o "${tmp_dir}/arm_denormal_check"

"${tmp_dir}/arm_denormal_check"

if [[ -n "${plugin_path}" ]]; then
    : "${MPC_SDK:?MPC_SDK must point at mpc-plugin-builder-sdk when --plugin is used}"
    echo "=== Building and running denormal_host against ${plugin_path} ==="
    c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow -Werror \
        -I "${shell_dir}" \
        -I "${MPC_SDK}/framework" \
        "${script_dir}/denormal_host.cpp" \
        -ldl \
        -o "${tmp_dir}/denormal_host"
    "${tmp_dir}/denormal_host" "${plugin_path}"
fi

if [[ "${cross_check}" == 1 ]]; then
    echo "=== Cross-target reproducible checks ==="
    if command -v arm-linux-gnueabihf-g++ >/dev/null 2>&1; then
        arm-linux-gnueabihf-g++ -std=c++17 -O2 -march=armv7-a -mfpu=neon -mfloat-abi=hard -static \
            -Wall -Wextra -Wpedantic -Wshadow -Werror \
            -I "${shell_dir}" -I "${repo_root}/core/include" -I "${repo_root}/core/engine-own" \
            "${script_dir}/arm_denormal_check.cpp" -o "${tmp_dir}/arm_denormal_check_arm32"
        echo "Built static ARM32 binary: ${tmp_dir}/arm_denormal_check_arm32"
    else
        echo "arm-linux-gnueabihf-g++ not installed; reproducible Key 61 (ARM32) static build command:"
        echo "  arm-linux-gnueabihf-g++ -std=c++17 -O2 -march=armv7-a -mfpu=neon -mfloat-abi=hard -static -Wall -Wextra -Wpedantic -Werror -I shell-mpc -I core/include -I core/engine-own shell-mpc/tests/arm_denormal_check.cpp -o arm_denormal_check_arm32"
    fi
    if command -v aarch64-linux-gnu-g++ >/dev/null 2>&1; then
        aarch64-linux-gnu-g++ -std=c++17 -O2 -march=armv8-a -static \
            -Wall -Wextra -Wpedantic -Wshadow -Werror \
            -I "${shell_dir}" -I "${repo_root}/core/include" -I "${repo_root}/core/engine-own" \
            "${script_dir}/arm_denormal_check.cpp" -o "${tmp_dir}/arm_denormal_check_arm64"
        echo "Built static AArch64 binary: ${tmp_dir}/arm_denormal_check_arm64"
    else
        echo "aarch64-linux-gnu-g++ not installed; reproducible MPC XL (AArch64) static build command:"
        echo "  aarch64-linux-gnu-g++ -std=c++17 -O2 -march=armv8-a -static -Wall -Wextra -Wpedantic -Werror -I shell-mpc -I core/include -I core/engine-own shell-mpc/tests/arm_denormal_check.cpp -o arm_denormal_check_arm64"
    fi
fi
