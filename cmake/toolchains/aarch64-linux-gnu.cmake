# SynthyBoy - toolchain file: Linux AArch64, cross compiled on an x86_64 Linux host.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Debian/Ubuntu: sudo apt-get install g++-aarch64-linux-gnu gcc-aarch64-linux-gnu qemu-user ninja-build
# cmake -S . -B build-aarch64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake \
#       -DSYNTHYBOY_BUILD_PLUGIN=OFF -DCMAKE_CROSSCOMPILING_EMULATOR="qemu-aarch64;-L;/usr/aarch64-linux-gnu"
# (CI builds AArch64 natively on an arm runner; this file is for local cross builds)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Conservative code generation for every 64-bit MPC (the MPC XL has Cortex-A55 and A76 cores; the MPC SDK builds with -march=armv8-a): no -mcpu=native, no
# newer architecture level. NEON (Advanced SIMD) is part of armv8-a.
set(SYNTHYBOY_ARM_FLAGS "-march=armv8-a")
set(CMAKE_C_FLAGS_INIT   "${SYNTHYBOY_ARM_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${SYNTHYBOY_ARM_FLAGS}")

set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
