#!/bin/sh
# layershim.sh - deploy, control, and inspect the MPC main-layer atomic takeover shim (XL & Key 61).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: layershim.sh [--dry-run] <xl|k61> <cmd> [args...]
# Devices:
#   xl               MPC XL (root@192.168.0.126, aarch64)
#   k61              MPC Key 61 (root@192.168.0.30, armv7)
# Commands:
#   start            install shim to /tmp/layer-shim-bin/, write drop-in (combining hwctl LD_PRELOAD if active), restart acvs
#   stop             remove our drop-in (preserving hwctl.conf if present) and restart acvs
#   status           print MPC pid, LD_PRELOAD, maps, drop-ins, stage/takeover flags, layer block, last summary, tail log
#   guard|takeover on|off enable/disable Page Guard mode via /tmp/xl-commit-shim-takeover
#   stage2 on|off    enable/disable Stage 2 TEST_ONLY probe via /tmp/xl-commit-shim-stage2
#   stage3 on|off    enable/disable Stage 3 test rectangle merge via /tmp/xl-commit-shim-stage3
#   recover          remove all shim flag files, remove our drop-in, restart acvs normally
#   log [N]          tail /tmp/xl-commit-shim.log (default 30 lines)
# Options:
#   --dry-run        print remote commands without connecting or executing

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
SSHO="-o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=8 -o PreferredAuthentications=publickey,none"
BIN="/tmp/layer-shim-bin"
DROP="/run/systemd/system/acvs.service.d"
SHIM_SO="$BIN/xl-commit-shim.so"

DRY_RUN=0
TARGET=""
CMD=""
SUB=""
EXTRA=""

for arg in "$@"; do
    case "$arg" in
        --dry-run)
            DRY_RUN=1
            ;;
        *)
            if [ -z "$TARGET" ]; then
                TARGET="$arg"
            elif [ -z "$CMD" ]; then
                CMD="$arg"
            elif [ -z "$SUB" ]; then
                SUB="$arg"
            elif [ -z "$EXTRA" ]; then
                EXTRA="$arg"
            fi
            ;;
    esac
done

case "$TARGET" in
    xl)
        DEV="root@192.168.0.126"
        ARCH="aarch64"
        SO_FILE="$HERE/xl-commit-shim-aarch64.so"
        ;;
    k61)
        DEV="root@192.168.0.30"
        ARCH="armv7"
        SO_FILE="$HERE/xl-commit-shim-armv7.so"
        ;;
    *)
        sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//' >&2
        exit 2
        ;;
esac

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
        echo "Error: $SO_FILE not found. Run tools/xl-commit-shim/build.sh $ARCH first." >&2
        exit 1
    fi
    dev "mkdir -p $BIN"
    if [ "$DRY_RUN" = "1" ]; then
        echo "[dry-run] cat \"$SO_FILE\" | ssh $SSHO $DEV \"cat > $SHIM_SO.new && mv $SHIM_SO.new $SHIM_SO\""
    else
        cat "$SO_FILE" | dev "cat > $SHIM_SO.new && mv $SHIM_SO.new $SHIM_SO"
    fi
    # Safely inspect existing systemd drop-ins and combine LD_PRELOAD (e.g. hwctl.conf) into one Environment=LD_PRELOAD=a:b line.
    START_SCRIPT="mkdir -p $DROP && rm -f $DROP/xlshim.conf && extra_preload='' && for f in $DROP/*.conf; do [ -e \"\$f\" ] || continue; b=\$(basename \"\$f\"); [ \"\$b\" = \"zz-layershim.conf\" ] && continue; if grep -qE '^[[:space:]]*EnvironmentFile=' \"\$f\"; then echo \"Refusing: \$f uses EnvironmentFile=, cannot combine LD_PRELOAD safely\" >&2; exit 1; fi; lines=\$(grep -E '^[[:space:]]*Environment=.*LD_PRELOAD' \"\$f\" || true); if [ -n \"\$lines\" ]; then cnt=\$(printf '%s\\n' \"\$lines\" | wc -l); if [ \"\$cnt\" -gt 1 ]; then echo \"Refusing: \$f has multiple LD_PRELOAD lines\" >&2; exit 1; fi; case \"\$lines\" in *\" \"*|*\"'\"*|*'\"'*) echo \"Refusing: \$f has quoted/multi-var Environment=LD_PRELOAD line (\$lines)\" >&2; exit 1 ;; Environment=LD_PRELOAD=*) val=\${lines#Environment=LD_PRELOAD=}; if [ -z \"\$val\" ]; then echo \"Refusing: empty LD_PRELOAD in \$f\" >&2; exit 1; fi; if [ -n \"\$extra_preload\" ]; then echo \"Refusing: multiple drop-ins set LD_PRELOAD (\$f)\" >&2; exit 1; fi; extra_preload=\"\$val\" ;; *) echo \"Refusing: unrecognised LD_PRELOAD line in \$f (\$lines)\" >&2; exit 1 ;; esac; fi; done && if [ -n \"\$extra_preload\" ]; then combined=\"$SHIM_SO:\$extra_preload\"; else combined=\"$SHIM_SO\"; fi && printf '[Service]\\nEnvironment=LD_PRELOAD=%s\\n' \"\$combined\" > $DROP/zz-layershim.conf && systemctl daemon-reload && systemctl restart acvs && echo \"restarted ($TARGET: LD_PRELOAD=\$combined)\""
    dev "$START_SCRIPT"
    ;;

stop)
    dev "rm -f $DROP/zz-layershim.conf $DROP/xlshim.conf; rmdir $DROP 2>/dev/null || true; systemctl daemon-reload && systemctl restart acvs && echo restarted-normal"
    ;;

status)
    dev 'pid=$(pidof MPC); echo "pid: $pid"; if [ -n "$pid" ]; then echo "LD_PRELOAD:"; tr "\0" "\n" < /proc/$pid/environ | grep "^LD_PRELOAD=" || echo "  (none)"; echo "maps:"; grep -E "xl-commit-shim|hwshim" /proc/$pid/maps || echo "  shim not mapped"; else echo "MPC not running"; fi; echo "drop-ins:"; ls -la /run/systemd/system/acvs.service.d 2>/dev/null || echo "  (none)"; echo "flags:"; [ -f /tmp/xl-commit-shim-takeover ] && echo "  guard: ON" || echo "  guard: OFF"; [ -f /tmp/xl-commit-shim-stage2 ] && echo "  stage2: ON" || echo "  stage2: OFF"; [ -f /tmp/xl-commit-shim-stage3 ] && echo "  stage3: ON" || echo "  stage3: OFF"; echo "layer block:"; ls -la /tmp/synthyboy-layer-block 2>/dev/null || echo "  (none)"; echo "last summary:"; grep "\[SUMMARY 5s\]" /tmp/xl-commit-shim.log 2>/dev/null | tail -n 1 || echo "  no summary yet"; echo "log tail:"; tail -n 15 /tmp/xl-commit-shim.log 2>/dev/null || echo "  no log file"'
    ;;

takeover|guard)
    case "$SUB" in
    on)
        dev "rm -f /tmp/xl-commit-shim-stage3 && touch /tmp/xl-commit-shim-takeover && echo guard enabled"
        ;;
    off)
        dev "rm -f /tmp/xl-commit-shim-takeover && echo guard disabled"
        ;;
    *)
        echo "Usage: layershim.sh [--dry-run] <xl|k61> guard|takeover on|off" >&2
        exit 2
        ;;
    esac
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
        echo "Usage: layershim.sh [--dry-run] <xl|k61> stage2 on|off" >&2
        exit 2
        ;;
    esac
    ;;

stage3)
    case "$SUB" in
    on)
        dev "rm -f /tmp/xl-commit-shim-takeover && touch /tmp/xl-commit-shim-stage3 && echo stage3 enabled"
        ;;
    off)
        dev "rm -f /tmp/xl-commit-shim-stage3 && echo 'stage3 disabled; waiting up to 5s for disable commit...' && for i in \$(seq 1 10); do if tail -n 20 /tmp/xl-commit-shim.log 2>/dev/null | grep -q 'disabled'; then echo 'plane disable observed in log'; break; fi; sleep 0.5; done; echo 'log tail:'; tail -n 15 /tmp/xl-commit-shim.log 2>/dev/null || true"
        ;;
    *)
        echo "Usage: layershim.sh [--dry-run] <xl|k61> stage3 on|off" >&2
        exit 2
        ;;
    esac
    ;;

recover)
    dev "rm -f /tmp/xl-commit-shim-takeover /tmp/xl-commit-shim-stage2 /tmp/xl-commit-shim-stage3 && rm -f $DROP/zz-layershim.conf $DROP/xlshim.conf; rmdir $DROP 2>/dev/null || true; systemctl daemon-reload && systemctl restart acvs && echo recovered-normal"
    ;;

log)
    lines="${SUB:-30}"
    dev "tail -n $lines /tmp/xl-commit-shim.log"
    ;;

*)
    sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
    ;;
esac
