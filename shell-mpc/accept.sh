#!/bin/sh
# Acceptance for shell-mpc (native build + SDK host simulator). Needs MPC_SDK.
set -u
: "${MPC_SDK:?MPC_SDK must point at mpc-plugin-builder-sdk}"
cd "$(dirname "$0")/.." || exit 1
B=build-mpc
cmake -S . -B $B -G Ninja -DCMAKE_BUILD_TYPE=Release -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_MPC_SHELL=ON -DMPC_SDK_DIR="$MPC_SDK" > $B.configure.log 2>&1 || { tail -40 $B.configure.log; echo "ACCEPT FAIL: cmake configure"; exit 1; }
cmake --build $B --target synthyboy_mpc > $B/build.log 2>&1 || { grep -E "error|Error|undefined|FAILED" -A6 $B/build.log | head -80; echo "ACCEPT FAIL: build"; exit 1; }
grep -n "synthyboy_mpc.cpp.*warning" $B/build.log | head -20 > $B/warn.log; [ -s $B/warn.log ] && { cat $B/warn.log; echo "ACCEPT FAIL: compiler warnings in shell-mpc"; exit 1; }
LIB=$(find $B -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)
[ -n "$LIB" ] || { echo "ACCEPT FAIL: built library not found under $B"; exit 1; }
[ -x $B/host_sim ] || c++ -std=c++17 -O2 -o $B/host_sim "$MPC_SDK/tools/host-sim/host_sim.cpp" || { echo "ACCEPT FAIL: host_sim build"; exit 1; }
$B/host_sim "$LIB" test --seconds 3 --notes 48,52,55 --wav $B/test.wav > $B/test.log 2>&1; rc=$?
grep -E "^(FAIL|WARN)|Result|main pass" $B/test.log | head -40
[ $rc -eq 0 ] || { tail -25 $B/test.log; echo "ACCEPT FAIL: host_sim test exit $rc"; exit 1; }
grep -q "^FAIL" $B/test.log && { echo "ACCEPT FAIL: FAIL lines"; exit 1; }
grep -qE "produced silence|extremely quiet" $B/test.log && { echo "ACCEPT FAIL: silent output"; exit 1; }
$B/host_sim "$LIB" test --seconds 2 --block 32 --notes 60 > $B/test32.log 2>&1 || { grep -E "^(FAIL|WARN)|Result" $B/test32.log; echo "ACCEPT FAIL: host_sim test with --block 32"; exit 1; }
grep -qE "^FAIL|produced silence" $B/test32.log && { echo "ACCEPT FAIL: block 32 run"; exit 1; }
$B/host_sim "$LIB" dump > $B/dump.json 2> $B/dump.err || { tail -20 $B/dump.err; echo "ACCEPT FAIL: host_sim dump"; exit 1; }
python3 - $B/dump.json docs/params.json <<'PY' || { echo "ACCEPT FAIL: parameter table"; exit 1; }
import json, sys
d = json.load(open(sys.argv[1])); p = json.load(open(sys.argv[2]))
a = [x["name"] for x in d["params"]]; b = [x["name"] for x in p["params"]]
ok = True
if len(a) != p["count"]: print(f"param count {len(a)} != {p['count']}"); ok = False
for i, (x, y) in enumerate(zip(a, b)):
    if x != y: print(f"param {i}: plugin '{x}' != docs '{y}'"); ok = False; break
for k, v in (("name", "SynthyBoy"), ("vendor", "Safian"), ("uid", "SfSB"), ("isInstrument", True), ("numInputs", 0), ("numOutputs", 2)):
    if d.get(k) != v: print(f"{k}: {d.get(k)!r} != {v!r}"); ok = False
if "Init" not in d.get("programs", []): print("no program named Init:", d.get("programs")); ok = False
sys.exit(0 if ok else 1)
PY
EXP=$(nm -gU "$LIB" | awk '{print $NF}' | sort | tr '\n' ' ')
[ "$EXP" = "_VSTPluginMain " ] || { echo "exports: $EXP" | cut -c1-600; echo "ACCEPT FAIL: exported symbols must be only _VSTPluginMain"; exit 1; }
# engine selection follows SYNTHYBOY_SHELL_ENGINE (configure-only checks of the compile definition)
grep -q -- "-DSYNTHYBOY_MPC_ENGINE_VITAL=1" $B/build.ninja || { echo "ACCEPT FAIL: default build (SYNTHYBOY_SHELL_ENGINE=vital) must compile shell-mpc with -DSYNTHYBOY_MPC_ENGINE_VITAL=1"; exit 1; }
cmake -S . -B $B-own -G Ninja -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=OFF -DSYNTHYBOY_BUILD_MPC_SHELL=ON -DSYNTHYBOY_SHELL_ENGINE=own -DMPC_SDK_DIR="$MPC_SDK" > $B-own.configure.log 2>&1 || { tail -20 $B-own.configure.log; echo "ACCEPT FAIL: configure with SYNTHYBOY_SHELL_ENGINE=own"; exit 1; }
grep -q -- "-DSYNTHYBOY_MPC_ENGINE_VITAL=0" $B-own/build.ninja || { echo "ACCEPT FAIL: with -DSYNTHYBOY_SHELL_ENGINE=own (and Vital in the build) shell-mpc must be compiled with -DSYNTHYBOY_MPC_ENGINE_VITAL=0"; exit 1; }
grep -q "SYNTHYBOY_MPC_ENGINE_VITAL" shell-mpc/synthyboy_mpc.cpp || { echo "ACCEPT FAIL: the source does not use SYNTHYBOY_MPC_ENGINE_VITAL"; exit 1; }
# plugin folder / content path logic
c++ -std=c++17 -O1 -Wall -Wextra -o $B/paths_test shell-mpc/tests/paths_test.cpp && $B/paths_test > $B/paths.log 2>&1 || { cat $B/paths.log 2>/dev/null | tail -12; echo "ACCEPT FAIL: paths_test (shell-mpc/plugin_paths.h)"; exit 1; }
# MPC manifest: only binaries that exist, version.xml
python3 scripts/tests/test_gen_mpc_manifest.py > $B/manifest-test.log 2>&1 || { tail -25 $B/manifest-test.log; echo "ACCEPT FAIL: scripts/tests/test_gen_mpc_manifest.py"; exit 1; }
python3 scripts/tests/test_gen_mpc_package.py > $B/package-test.log 2>&1 || { tail -30 $B/package-test.log; echo "ACCEPT FAIL: scripts/tests/test_gen_mpc_package.py"; exit 1; }
[ -f scripts/build-armv7-mpc.sh ] && bash -n scripts/build-armv7-mpc.sh || { echo "ACCEPT FAIL: scripts/build-armv7-mpc.sh missing or not valid bash"; exit 1; }
# stub host: state round trip with content, re-prepare on rate change, two-thread parameter writes (plain build, then ThreadSanitizer)
INC="-I core/include -I $MPC_SDK/framework"
c++ -std=c++17 -O1 $INC -o $B/stub_host shell-mpc/tests/stub_host.cpp $B/core/libsynthyboy_core.a || { echo "ACCEPT FAIL: stub_host build"; exit 1; }
$B/stub_host "$LIB" > $B/stub.log 2>&1 || { tail -15 $B/stub.log; echo "ACCEPT FAIL: stub_host"; exit 1; }
T=build-mpc-tsan
cmake -S . -B $T -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=OFF -DSYNTHYBOY_BUILD_MPC_SHELL=ON -DMPC_SDK_DIR="$MPC_SDK" "-DCMAKE_CXX_FLAGS=-fsanitize=thread -g" "-DCMAKE_C_FLAGS=-fsanitize=thread -g" "-DCMAKE_MODULE_LINKER_FLAGS=-fsanitize=thread" > $T.configure.log 2>&1 && cmake --build $T --target synthyboy_mpc > $T.build.log 2>&1 || { tail -30 $T.build.log; echo "ACCEPT FAIL: ThreadSanitizer build"; exit 1; }
TLIB=$(find $T -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)
c++ -std=c++17 -O1 -g -fsanitize=thread $INC -o $T/stub_host shell-mpc/tests/stub_host.cpp $T/core/libsynthyboy_core.a || { echo "ACCEPT FAIL: tsan stub_host build"; exit 1; }
TSAN_OPTIONS="halt_on_error=0 exitcode=0" $T/stub_host "$TLIB" > $T/stub.log 2>&1
if grep -q "WARNING: ThreadSanitizer" $T/stub.log; then
    echo "ThreadSanitizer reports: $(grep -c 'WARNING: ThreadSanitizer' $T/stub.log); innermost frames:"
    grep -A14 "WARNING: ThreadSanitizer" $T/stub.log | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20
    echo "ACCEPT FAIL: data races (full report: $T/stub.log)"; exit 1
fi
grep -q "stub_host: OK" $T/stub.log || { tail -15 $T/stub.log; echo "ACCEPT FAIL: tsan stub_host"; exit 1; }
echo "ACCEPT OK"
