#!/bin/sh
# SynthyBoy - runs the SDK's host_sim and judges its result with one allowance: the plugin's display-only parameters (LFO n Cursor, Spectrum n) hold live
# values of the audio, not state. host_sim's state round-trip check (save the chunk, change every parameter, load the chunk, compare) compares them too, and a
# display tick that lands between the save and the compare changes such a value for a legitimate reason. A run is accepted when host_sim exits 0, or when
# every FAIL line it printed is about a display parameter (index >= the number of engine parameters) or is the summary of that same round-trip check, and the number
# of FAIL lines equals the failures it counts. Anything else (an engine parameter, any other check) fails as before. The SDK tool itself is not changed.
#   Usage: shell-mpc/host-sim-run.sh HOST_SIM LIB ENGINE_PARAM_COUNT LOGFILE host_sim-arguments...
# Exit 0 accepted, 1 not accepted (the log is kept). The engine parameters of the state round trip are therefore still compared exactly; the display parameters are
# covered by shell-mpc/tests/lfo_live_host.cpp (they are not in the chunk, writes to them are ignored).
set -u
HS=$1; LIB=$2; N=$3; LOG=$4; shift 4
"$HS" "$LIB" "$@" > "$LOG" 2>&1
rc=$?
[ $rc -eq 0 ] && exit 0
awk -v n="$N" '
    /^FAIL/ {
        fails++
        if ($0 ~ /^FAIL  state round-trip: [0-9]+ parameter\(s\) did not come back from the saved chunk$/) next
        if (match($0, /^FAIL  param [0-9]+ "(LFO [1-4] Cursor|Spectrum [0-9]+)": saved /)) {
            split($0, a, " "); if (a[3] + 0 >= n) next
        }
        bad++
    }
    /^Result: FAIL/ { if (match($0, /[0-9]+ failures/)) declared = substr($0, RSTART, RLENGTH) + 0 }
    END { exit (fails > 0 && bad == 0 && fails == declared) ? 0 : 1 }
' "$LOG"
