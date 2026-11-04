# SynthyBoy - toolchain file: Linux ARMv7 hard-float (e.g. Akai MPC Key 61), cross compiled on an x86_64 Linux host.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Debian/Ubuntu: sudo apt-get install g++-arm-linux-gnueabihf gcc-arm-linux-gnueabihf qemu-user ninja-build
# cmake -S . -B build-armv7 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/armv7-linux-gnueabihf.cmake \
#       -DSYNTHYBOY_BUILD_PLUGIN=OFF -DCMAKE_CROSSCOMPILING_EMULATOR="qemu-arm;-L;/usr/arm-linux-gnueabihf"
# (the exact CPU of the target is not decided yet, see docs/ARCHITECTURE.md 3.1: generic armv7-a + NEON here)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

set(SYNTHYBOY_ARM_FLAGS "-march=armv7-a -mfpu=neon -mfloat-abi=hard")
set(CMAKE_C_FLAGS_INIT   "${SYNTHYBOY_ARM_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${SYNTHYBOY_ARM_FLAGS}")

set(CMAKE_FIND_ROOT_PATH /usr/arm-linux-gnueabihf)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
