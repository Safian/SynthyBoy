#!/bin/sh
# xlshim.sh - deploy, control, and inspect the MPC XL atomic commit measurement shim.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Commands:
#   start            install shim to /tmp/xl-shim-bin/, add systemd drop-in, restart acvs
#   stop             remove drop-in and restart acvs normally (recovery path)
#   status           print MPC pid, check /proc/$pid/maps for shim, tail log
#   stage2 on|off    enable/disable Stage 2 TEST_ONLY probe via /tmp/xl-commit-shim-stage2
#   log [N]          tail /tmp/xl-commit-shim.log (default 30 lines)
# Options:
#   --dry-run        print remote commands without connecting or executing

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
DEV="root@192.168.0.126"
SSHO="-o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=8 -o PreferredAuthentications=publickey,none"
BIN="/tmp/xl-shim-bin"
DROP="/run/systemd/system/acvs.service.d"
SO_FILE="$HERE/xl-commit-shim.so"

DRY_RUN=0
CMD=""
SUB=""
EXTRA=""

for arg in "$@"; do
    case "$arg" in
        --dry-run)
            DRY_RUN=1
            ;;
        *)
            if [ -z "$CMD" ]; then
                CMD="$arg"
            elif [ -z "$SUB" ]; then
                SUB="$arg"
            elif [ -z "$EXTRA" ]; then
                EXTRA="$arg"
            fi
            ;;
    esac
done

dev() {
    if [ "$DRY_RUN" = "1" ]; then
        echo "[dry-run] ssh $SSHO $DEV \"$*\""
    else
        ssh $SSHO $DEV "$@"
    fi
}

case "$CMD" in
start)
    if [ ! -f "$SO_FILE" ] && [ "$DRY_RUN" = "0" ]; then
        echo "Error: $SO_FILE not found. Run tools/xl-commit-shim/build.sh first." >&2
        exit 1
    fi
    dev "mkdir -p $BIN"
    if [ "$DRY_RUN" = "1" ]; then
        echo "[dry-run] cat \"$SO_FILE\" | ssh $SSHO $DEV \"cat > $BIN/xl-commit-shim.so.new && mv $BIN/xl-commit-shim.so.new $BIN/xl-commit-shim.so\""
    else
        cat "$SO_FILE" | dev "cat > $BIN/xl-commit-shim.so.new && mv $BIN/xl-commit-shim.so.new $BIN/xl-commit-shim.so"
    fi
    dev "mkdir -p $DROP && printf '[Service]\nEnvironment=LD_PRELOAD=$BIN/xl-commit-shim.so\n' > $DROP/xlshim.conf && systemctl daemon-reload && systemctl restart acvs && echo restarted"
    ;;

stop)
    dev "rm -f $DROP/xlshim.conf; rmdir $DROP 2>/dev/null; systemctl daemon-reload && systemctl restart acvs && echo restarted-normal"
    ;;

status)
    dev 'pid=$(pidof MPC); echo "pid: $pid"; if [ -n "$pid" ]; then echo "maps:"; grep -E "xl-commit-shim" /proc/$pid/maps || echo "shim not mapped"; else echo "MPC not running"; fi; echo "log tail:"; tail -n 15 /tmp/xl-commit-shim.log 2>/dev/null || echo "no log file"'
    ;;

stage2)
    case "$SUB" in
    on)
        dev "touch /tmp/xl-commit-shim-stage2 && echo stage2 enabled"
        ;;
    off)
        dev "rm -f /tmp/xl-commit-shim-stage2 && echo stage2 disabled"
        ;;
    *)
        echo "Usage: xlshim.sh [--dry-run] stage2 on|off" >&2
        exit 2
        ;;
    esac
    ;;

log)
    lines="${SUB:-30}"
    dev "tail -n $lines /tmp/xl-commit-shim.log"
    ;;

*)
    sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
    ;;
esac
