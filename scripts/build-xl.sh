#!/bin/bash
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

# Build, archive under .hw-local/mpc-builds/ and install for the MPC XL (aarch64); options: scripts/build-and-install.sh.
exec "$(dirname "$0")/build-and-install.sh" xl "$@"
