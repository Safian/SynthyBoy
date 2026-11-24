#!/bin/sh
# xlshim.sh - backwards-compatible wrapper for layershim.sh xl.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
exec "$HERE/layershim.sh" xl "$@"
