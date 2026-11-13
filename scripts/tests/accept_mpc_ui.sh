#!/bin/sh
# Acceptance of the MPC ui.json generator (default mode and --layout mode). Needs MPC_SDK: the SDK's gen_ui.py is run on the result.
cd "$(dirname "$0")/../.." || exit 1
: "${MPC_SDK:?MPC_SDK must point at mpc-plugin-builder-sdk}"
L=$(mktemp)
python3 scripts/tests/test_gen_mpc_ui_layout.py > "$L" 2>&1 || { grep -E "^(FAIL|ERROR|AssertionError)|Error:|^Ran|^FAILED" "$L" | head -40; rm -f "$L"; echo "ACCEPT FAIL: test_gen_mpc_ui_layout.py"; exit 1; }
python3 scripts/tests/test_gen_mpc_ui.py > "$L" 2>&1 || { grep -E "^(FAIL|ERROR|AssertionError)|Error:|^Ran|^FAILED" "$L" | head -40; rm -f "$L"; echo "ACCEPT FAIL: test_gen_mpc_ui.py"; exit 1; }
rm -f "$L"
echo "ACCEPT OK"
