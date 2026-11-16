#!/bin/sh
# Acceptance for shell-mpc (native build + SDK host simulator). Needs MPC_SDK.
set -u
: "${MPC_SDK:?MPC_SDK must point at mpc-plugin-builder-sdk}"
cd "$(dirname "$0")/.." || exit 1
B=build-mpc
# -USYNTHYBOY_MPC_OVERLAY*: the overlay options are the one place the builds do NOT name (the default is the build without the module), so a cache entry an earlier overlay build left in the directory is dropped.
# Every build below names every option of the display and the statistics itself: a build directory keeps the options of earlier runs in its CMake cache (a tree that was
# configured when the default was 25 a second stayed at 25 and failed the 10 a second checks), so nothing may rest on a default or on what an earlier run left.
BASE_OPTS="-USYNTHYBOY_MPC_OVERLAY -USYNTHYBOY_MPC_OVERLAY_GPU -DSYNTHYBOY_MPC_LFO_LIVE=ON -DSYNTHYBOY_MPC_LFO_LIVE_HZ=10 -DSYNTHYBOY_MPC_LFO_LIVE_NOTIFY=ON -DSYNTHYBOY_MPC_LFO_LIVE_SELFTEST=OFF -DSYNTHYBOY_MPC_CALLBACK_STATS=OFF -DSYNTHYBOY_MPC_SPECTRUM=ON -DSYNTHYBOY_MPC_SPECTRUM_SELFTEST=OFF -DSYNTHYBOY_MPC_OUTPUT_GAIN_DB=18 -DSYNTHYBOY_MPC_OUTPUT_LIMITER=OFF -DSYNTHYBOY_MPC_PAD_PRESSURE=ON"
cmake -S . -B $B -G Ninja $BASE_OPTS -DCMAKE_BUILD_TYPE=Release -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_MPC_SHELL=ON -DMPC_SDK_DIR="$MPC_SDK" > $B.configure.log 2>&1 || { tail -40 $B.configure.log; echo "ACCEPT FAIL: cmake configure"; exit 1; }
cmake --build $B --target synthyboy_mpc dump_mpc_chunks > $B/build.log 2>&1 || { grep -E "error|Error|undefined|FAILED" -A6 $B/build.log | head -80; echo "ACCEPT FAIL: build"; exit 1; }
grep -n "synthyboy_mpc.cpp.*warning" $B/build.log | head -20 > $B/warn.log; [ -s $B/warn.log ] && { cat $B/warn.log; echo "ACCEPT FAIL: compiler warnings in shell-mpc"; exit 1; }
LIB=$(find $B -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)
[ -n "$LIB" ] || { echo "ACCEPT FAIL: built library not found under $B"; exit 1; }
[ -x $B/host_sim ] || c++ -std=c++17 -O2 -o $B/host_sim "$MPC_SDK/tools/host-sim/host_sim.cpp" || { echo "ACCEPT FAIL: host_sim build"; exit 1; }
ENGINE_N=$(python3 -c "import json;print(json.load(open('docs/params.json'))['count'])")
HSR="sh shell-mpc/host-sim-run.sh $B/host_sim"   # accepts a state round-trip failure about display parameters only (LFO n Cursor, Spectrum n: live values, not state)
$HSR "$LIB" $ENGINE_N $B/test.log test --seconds 3 --notes 48,52,55 --wav $B/test.wav; rc=$?
grep -E "^(FAIL|WARN)|Result|main pass" $B/test.log | head -40
[ $rc -eq 0 ] || { tail -25 $B/test.log; echo "ACCEPT FAIL: host_sim test exit $rc"; exit 1; }
grep -qE "produced silence|extremely quiet" $B/test.log && { echo "ACCEPT FAIL: silent output"; exit 1; }
$HSR "$LIB" $ENGINE_N $B/test32.log test --seconds 2 --block 32 --notes 60 || { grep -E "^(FAIL|WARN)|Result" $B/test32.log; echo "ACCEPT FAIL: host_sim test with --block 32"; exit 1; }
grep -qE "produced silence" $B/test32.log && { echo "ACCEPT FAIL: block 32 run"; exit 1; }
$B/host_sim "$LIB" dump > $B/dump.json 2> $B/dump.err || { tail -20 $B/dump.err; echo "ACCEPT FAIL: host_sim dump"; exit 1; }
python3 - $B/dump.json docs/params.json <<'PY' || { echo "ACCEPT FAIL: parameter table"; exit 1; }
import json, sys
d = json.load(open(sys.argv[1])); p = json.load(open(sys.argv[2]))
a = [x["name"] for x in d["params"]]; b = [x["name"] for x in p["params"]]
ok = True
extra = ["LFO %d Cursor" % n for n in (1, 2, 3, 4)] + ["Spectrum %d" % n for n in range(1, 33)] + ["Pad Pressure", "Pad Pressure Amount"]   # SYNTHYBOY_MPC_LFO_LIVE and SYNTHYBOY_MPC_SPECTRUM (default ON): display-only parameters after the engine's list, then the two SYNTHYBOY_MPC_PAD_PRESSURE parameters
if len(a) != p["count"] + len(extra) or a[p["count"]:] != extra: print(f"param count {len(a)} != {p['count']} + 38 extra parameters {a[p['count']:]}"); ok = False
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
cmake -S . -B $B-own -G Ninja $BASE_OPTS -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=OFF -DSYNTHYBOY_BUILD_MPC_SHELL=ON -DSYNTHYBOY_SHELL_ENGINE=own -DMPC_SDK_DIR="$MPC_SDK" > $B-own.configure.log 2>&1 || { tail -20 $B-own.configure.log; echo "ACCEPT FAIL: configure with SYNTHYBOY_SHELL_ENGINE=own"; exit 1; }
grep -q -- "-DSYNTHYBOY_MPC_ENGINE_VITAL=0" $B-own/build.ninja || { echo "ACCEPT FAIL: with -DSYNTHYBOY_SHELL_ENGINE=own (and Vital in the build) shell-mpc must be compiled with -DSYNTHYBOY_MPC_ENGINE_VITAL=0"; exit 1; }
grep -q "SYNTHYBOY_MPC_ENGINE_VITAL" shell-mpc/synthyboy_mpc.cpp || { echo "ACCEPT FAIL: the source does not use SYNTHYBOY_MPC_ENGINE_VITAL"; exit 1; }
# plugin folder / content path logic
c++ -std=c++17 -O1 -Wall -Wextra -o $B/paths_test shell-mpc/tests/paths_test.cpp && $B/paths_test > $B/paths.log 2>&1 || { cat $B/paths.log 2>/dev/null | tail -12; echo "ACCEPT FAIL: paths_test (shell-mpc/plugin_paths.h)"; exit 1; }
# MPC manifest: only binaries that exist, version.xml
python3 scripts/tests/test_gen_mpc_manifest.py > $B/manifest-test.log 2>&1 || { tail -25 $B/manifest-test.log; echo "ACCEPT FAIL: scripts/tests/test_gen_mpc_manifest.py"; exit 1; }
python3 scripts/tests/test_gen_mpc_package.py > $B/package-test.log 2>&1 || { tail -30 $B/package-test.log; echo "ACCEPT FAIL: scripts/tests/test_gen_mpc_package.py"; exit 1; }
# the .xpl generator (scripts/gen-mpc-xpl.py): files the device wrote decode and re-render byte for byte, generated files decode to the same structure; with dump_mpc_chunks built (above) all 112 presets are generated from the built plugin
python3 scripts/tests/test_gen_mpc_xpl.py > $B/xpl-test.log 2>&1 || { tail -30 $B/xpl-test.log; echo "ACCEPT FAIL: scripts/tests/test_gen_mpc_xpl.py"; exit 1; }
for f in scripts/build-mpc.sh scripts/build-armv7-mpc.sh scripts/mpc-container-build.sh; do [ -f $f ] && bash -n $f || { echo "ACCEPT FAIL: $f missing or not valid bash"; exit 1; }; done
# factory presets as programs: the list header, then the library inside a plugin folder (Content/presets) and outside one
INC="-I core/include -I $MPC_SDK/framework"
c++ -std=c++17 -O1 -Wall -Wextra $INC -o $B/programs_test shell-mpc/tests/programs_test.cpp $B/core/libsynthyboy_core.a > $B/programs-build.log 2>&1 || { grep -E "error" -A4 $B/programs-build.log | head -30; echo "ACCEPT FAIL: programs_test build (shell-mpc/program_list.h)"; exit 1; }
$B/programs_test content/presets > $B/programs.log 2>&1 || { grep -E "^FAIL" $B/programs.log; echo "ACCEPT FAIL: programs_test"; exit 1; }
PK="$B/pkg/Safian - VST - SynthyBoy"
rm -rf $B/pkg; mkdir -p "$PK/VST" "$PK/Content"; cp -R content/presets "$PK/Content/presets"; cp "$LIB" "$PK/VST/"; echo '{}' > "$PK/plugin.json"
PLIB="$PK/VST/$(basename "$LIB")"
c++ -std=c++17 -O1 -Wall -Wextra $INC -o $B/programs_host shell-mpc/tests/programs_host.cpp $B/core/libsynthyboy_core.a || { echo "ACCEPT FAIL: programs_host build"; exit 1; }
BARE=$(mktemp -d); cp "$LIB" "$BARE/"   # outside the repository: its root has a plugin.json (and content/ answers to Content/ on a case-insensitive disk)
$B/programs_host "$BARE/$(basename "$LIB")" none > $B/programs-none.log 2>&1; nrc=$?; rm -rf "$BARE"
[ $nrc -eq 0 ] || { grep -E "^FAIL|^      " $B/programs-none.log | head -12; echo "ACCEPT FAIL: programs_host (library outside a plugin folder)"; exit 1; }
# a plugin folder without Content/, and one whose Content is a file: the plugin must load and offer "Init" only
NC="$B/pkg-nocontent/Safian - VST - SynthyBoy"; rm -rf $B/pkg-nocontent; mkdir -p "$NC/VST"; cp "$LIB" "$NC/VST/"; echo '{}' > "$NC/plugin.json"
$B/programs_host "$NC/VST/$(basename "$LIB")" none > $B/programs-nocontent.log 2>&1 || { grep -E "^FAIL|^      " $B/programs-nocontent.log | head -12; echo "ACCEPT FAIL: programs_host (plugin folder without Content)"; exit 1; }
echo x > "$NC/Content"
$B/programs_host "$NC/VST/$(basename "$LIB")" none > $B/programs-badcontent.log 2>&1 || { grep -E "^FAIL|^      " $B/programs-badcontent.log | head -12; echo "ACCEPT FAIL: programs_host (plugin folder whose Content is a file)"; exit 1; }
grep -q "catch (...)" shell-mpc/synthyboy_mpc.cpp || { echo "ACCEPT FAIL: no exception barrier in the shell"; exit 1; }
# a second package with fewer presets (a later version whose list differs): chunks of the first are loaded into it
P2="$B/pkg2/Safian - VST - SynthyBoy"; rm -rf $B/pkg2; mkdir -p "$P2/VST" "$P2/Content"; cp -R content/presets "$P2/Content/presets"; rm -rf "$P2/Content/presets/Bass"; cp "$LIB" "$P2/VST/"; echo '{}' > "$P2/plugin.json"
$B/programs_host "$PLIB" content/presets "$P2/VST/$(basename "$LIB")" "$P2/Content/presets" > $B/programs-pkg.log 2>&1 || { grep -E "^FAIL|^      " $B/programs-pkg.log | head -20; echo "ACCEPT FAIL: programs_host (library in a plugin folder with Content/presets)"; exit 1; }
$B/host_sim "$PLIB" dump > $B/dump-pkg.json 2> $B/dump-pkg.err || { tail -20 $B/dump-pkg.err; echo "ACCEPT FAIL: host_sim dump of the packaged library"; exit 1; }
python3 - $B/dump-pkg.json <<'PY' || { echo "ACCEPT FAIL: program list in the host_sim dump"; exit 1; }
import json, sys
d = json.load(open(sys.argv[1])); pr = d.get("programs", [])
ok = len(pr) == 113 and pr[0] == "Init" and "808 Boom" in pr
if not ok: print("programs in the dump:", len(pr), pr[:5])
sys.exit(0 if ok else 1)
PY
$HSR "$PLIB" $ENGINE_N $B/test-pkg.log test --seconds 2 --notes 48,55 || { grep -E "^(FAIL|WARN)|Result" $B/test-pkg.log; echo "ACCEPT FAIL: host_sim test of the packaged library"; exit 1; }
grep -qE "produced silence" $B/test-pkg.log && { echo "ACCEPT FAIL: host_sim test of the packaged library"; exit 1; }
grep -q 'Content/presets' scripts/build-mpc.sh || { echo "ACCEPT FAIL: scripts/build-mpc.sh does not copy content/presets to Content/presets"; exit 1; }
# stub host: state round trip with content, re-prepare on rate change, two-thread parameter writes (plain build, then ThreadSanitizer)
INC="-I core/include -I $MPC_SDK/framework"
c++ -std=c++17 -O1 $INC -o $B/stub_host shell-mpc/tests/stub_host.cpp $B/core/libsynthyboy_core.a || { echo "ACCEPT FAIL: stub_host build"; exit 1; }
$B/stub_host "$LIB" > $B/stub.log 2>&1 || { tail -15 $B/stub.log; echo "ACCEPT FAIL: stub_host"; exit 1; }
T=build-mpc-tsan
cmake -S . -B $T -G Ninja $BASE_OPTS -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=OFF -DSYNTHYBOY_BUILD_MPC_SHELL=ON -DMPC_SDK_DIR="$MPC_SDK" "-DCMAKE_CXX_FLAGS=-fsanitize=thread -g" "-DCMAKE_C_FLAGS=-fsanitize=thread -g" "-DCMAKE_MODULE_LINKER_FLAGS=-fsanitize=thread" > $T.configure.log 2>&1 && cmake --build $T --target synthyboy_mpc > $T.build.log 2>&1 || { tail -30 $T.build.log; echo "ACCEPT FAIL: ThreadSanitizer build"; exit 1; }
TLIB=$(find $T -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)
c++ -std=c++17 -O1 -g -fsanitize=thread $INC -o $T/stub_host shell-mpc/tests/stub_host.cpp $T/core/libsynthyboy_core.a || { echo "ACCEPT FAIL: tsan stub_host build"; exit 1; }
TSAN_OPTIONS="halt_on_error=0 exitcode=0" $T/stub_host "$TLIB" > $T/stub.log 2>&1
if grep -q "WARNING: ThreadSanitizer" $T/stub.log; then
    echo "ThreadSanitizer reports: $(grep -c 'WARNING: ThreadSanitizer' $T/stub.log); innermost frames:"
    grep -A14 "WARNING: ThreadSanitizer" $T/stub.log | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20
    echo "ACCEPT FAIL: data races (full report: $T/stub.log)"; exit 1
fi
grep -q "stub_host: OK" $T/stub.log || { tail -15 $T/stub.log; echo "ACCEPT FAIL: tsan stub_host"; exit 1; }
TPK="$T/pkg/Safian - VST - SynthyBoy"
rm -rf $T/pkg; mkdir -p "$TPK/VST" "$TPK/Content"; cp -R content/presets "$TPK/Content/presets"; cp "$TLIB" "$TPK/VST/"; echo '{}' > "$TPK/plugin.json"
c++ -std=c++17 -O1 -g -fsanitize=thread $INC -o $T/programs_host shell-mpc/tests/programs_host.cpp $T/core/libsynthyboy_core.a || { echo "ACCEPT FAIL: tsan programs_host build"; exit 1; }
T2="$T/pkg2/Safian - VST - SynthyBoy"; rm -rf $T/pkg2; mkdir -p "$T2/VST" "$T2/Content"; cp -R content/presets "$T2/Content/presets"; rm -rf "$T2/Content/presets/Bass"; cp "$TLIB" "$T2/VST/"; echo '{}' > "$T2/plugin.json"
TSAN_OPTIONS="halt_on_error=0 exitcode=0" $T/programs_host "$TPK/VST/$(basename "$TLIB")" content/presets "$T2/VST/$(basename "$TLIB")" "$T2/Content/presets" > $T/programs.log 2>&1
if grep -q "WARNING: ThreadSanitizer" $T/programs.log; then
    echo "ThreadSanitizer reports in programs_host: $(grep -c 'WARNING: ThreadSanitizer' $T/programs.log); innermost frames:"
    grep -A14 "WARNING: ThreadSanitizer" $T/programs.log | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20
    echo "ACCEPT FAIL: data races while changing programs (full report: $T/programs.log)"; exit 1
fi
grep -q "programs_host: OK" $T/programs.log || { grep -E "^FAIL|^      " $T/programs.log | head; echo "ACCEPT FAIL: tsan programs_host"; exit 1; }
# Live LFO display (SYNTHYBOY_MPC_LFO_LIVE, default ON in this shell): the main build above has it. Four more builds with the own engine (the readout of the cursors
# comes from it): OFF (nothing after the engine's parameters), ON (cursors follow a held note), SELFTEST (cursors sweep without a note), SELFTEST + NOTIFY=OFF
# (no audioMasterAutomate). All four give the same state chunk. The ON build runs once more under ThreadSanitizer (the display thread reads the engine's atomics).
LIVEINC="-I core/include -I shell-mpc -I $MPC_SDK/framework"
live_build() { # name, cmake options...
    n=$1; shift
    cmake -S . -B build-mpc-live-$n -G Ninja $BASE_OPTS -DCMAKE_BUILD_TYPE=Release -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=OFF -DSYNTHYBOY_BUILD_MPC_SHELL=ON -DSYNTHYBOY_SHELL_ENGINE=own -DMPC_SDK_DIR="$MPC_SDK" "$@" > build-mpc-live-$n.configure.log 2>&1 && cmake --build build-mpc-live-$n --target synthyboy_mpc > build-mpc-live-$n.build.log 2>&1 || { tail -30 build-mpc-live-$n.build.log; echo "ACCEPT FAIL: build of the LFO display variant $n"; exit 1; }
}
live_build off -DSYNTHYBOY_MPC_LFO_LIVE=OFF -DSYNTHYBOY_MPC_SPECTRUM=OFF
live_build on
live_build self -DSYNTHYBOY_MPC_LFO_LIVE_SELFTEST=ON
live_build quiet -DSYNTHYBOY_MPC_LFO_LIVE_SELFTEST=ON -DSYNTHYBOY_MPC_LFO_LIVE_NOTIFY=OFF
live_build hz25 -DSYNTHYBOY_MPC_LFO_LIVE_HZ=25
live_build stats -DSYNTHYBOY_MPC_CALLBACK_STATS=ON
live_build specoff -DSYNTHYBOY_MPC_SPECTRUM=OFF
live_build specself -DSYNTHYBOY_MPC_SPECTRUM_SELFTEST=ON
live_build specself25 -DSYNTHYBOY_MPC_SPECTRUM_SELFTEST=ON -DSYNTHYBOY_MPC_LFO_LIVE_HZ=25
live_build gain0 -DSYNTHYBOY_MPC_OUTPUT_GAIN_DB=0
live_build limiteron -DSYNTHYBOY_MPC_OUTPUT_LIMITER=ON
live_build limitergain0 -DSYNTHYBOY_MPC_OUTPUT_LIMITER=ON -DSYNTHYBOY_MPC_OUTPUT_GAIN_DB=0
live_build ovlon -DSYNTHYBOY_MPC_OVERLAY=ON
live_build ovloff -DSYNTHYBOY_MPC_OVERLAY=OFF
# Overlay (SYNTHYBOY_MPC_OVERLAY, default OFF, shell-mpc/overlay/; BASE_OPTS does not name it on purpose: the builds above are the ones "without it", the ovloff variant names OFF): the switched-off build is byte for byte the build that does not name the option (and the same as the build of the commit
# before the module existed: checked by hand when the module was added); the switched-on build has the module, the other builds have no trace of it. The unit tests run on the Mac with a fake
# display backend and a fake framebuffer; the plugin built with it ON must still load, run and give the same state chunk (on the Mac the module finds no display and disables itself).
python3 scripts/gen-overlay-layout-header.py --check || { echo "ACCEPT FAIL: shell-mpc/overlay/overlay_layout.h is not what scripts/gen-overlay-layout-header.py makes of overlay_layout.json"; exit 1; }
OVLON="$(find build-mpc-live-ovlon -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)"
OVLOFF="$(find build-mpc-live-ovloff -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)"
OVLDEF="$(find build-mpc-live-on -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)"
cmp -s "$OVLOFF" "$OVLDEF" || { echo "ACCEPT FAIL: SYNTHYBOY_MPC_OVERLAY=OFF is not byte for byte the build that does not name the option"; exit 1; }
grep -q "synthyboy-overlay-status" "$OVLON" || { echo "ACCEPT FAIL: the overlay build has no overlay code"; exit 1; }
for f in "$OVLOFF" "$OVLDEF" "$LIB"; do
    grep -q "synthyboy-overlay" "$f" && { echo "ACCEPT FAIL: trace of the overlay in a build without SYNTHYBOY_MPC_OVERLAY"; exit 1; }
done
OVCORE=$(find $B/core -name '*.a' | tr '\n' ' ')
OVSRC=shell-mpc/overlay/tests
c++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -I shell-mpc/overlay -o $B/geometry_render_host $OVSRC/geometry_render_host.cpp || { echo "ACCEPT FAIL: geometry_render_host build"; exit 1; }
$B/geometry_render_host > $B/geometry-render.log 2>&1 || { grep -E "^FAIL|ERROR" $B/geometry-render.log | head; echo "ACCEPT FAIL: geometry_render_host (rotation mapping, renderer)"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o $B/curve_host $OVSRC/curve_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: curve_host build"; exit 1; }
$B/curve_host > $B/curve-host.log 2>&1 || { grep -E "^FAIL" $B/curve-host.log; echo "ACCEPT FAIL: curve_host (curve source)"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o $B/controller_host $OVSRC/controller_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: controller_host build"; exit 1; }
$B/controller_host > $B/controller-host.log 2>&1 || { grep -E "^FAIL" $B/controller-host.log; echo "ACCEPT FAIL: controller_host (state machine)"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o $B/qlink_panel_host $OVSRC/qlink_panel_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: qlink_panel_host build"; exit 1; }
$B/qlink_panel_host > $B/qlink-panel-host.log 2>&1 || { grep -E "^FAIL|^      " $B/qlink-panel-host.log; echo "ACCEPT FAIL: qlink_panel_host (the host's Q-Link pop-up replayed against the key)"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o $B/mouse_host $OVSRC/mouse_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: mouse_host build"; exit 1; }
$B/mouse_host > $B/mouse-host.log 2>&1 || { grep -E "^FAIL" $B/mouse-host.log; echo "ACCEPT FAIL: mouse_host (the mouse on the plugin-drawn pages)"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o $B/views_host $OVSRC/views_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: views_host build"; exit 1; }
$B/views_host > $B/views-host.log 2>&1 || { grep -E "^FAIL" $B/views-host.log; echo "ACCEPT FAIL: views_host (the views: LFO, oscillator, envelope, filter 2, several views in the controller)"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o $B/gpu_scene_host $OVSRC/gpu_scene_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: gpu_scene_host build"; exit 1; }
$B/gpu_scene_host > $B/gpu-scene-host.log 2>&1 || { grep -E "^FAIL" $B/gpu-scene-host.log; echo "ACCEPT FAIL: gpu_scene_host (GPU scene protocol: seqlock, every view type as a scene)"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o $B/gpu_backend_host $OVSRC/gpu_backend_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: gpu_backend_host build"; exit 1; }
$B/gpu_backend_host > $B/gpu-backend-host.log 2>&1 || { grep -E "^FAIL" $B/gpu-backend-host.log; echo "ACCEPT FAIL: gpu_backend_host (GPU helper health state machine and fallback with a fake helper)"; exit 1; }
# run-time discovery (discovery.h: the plane tables of BOTH devices, a busy plane, no free plane, the cores), the rotation found from the markers, the pace grid with a fake vblank clock, a plane another program takes
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o $B/display_host $OVSRC/display_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: display_host build"; exit 1; }
$B/display_host > $B/display-host.log 2>&1 || { grep -E "^FAIL" $B/display-host.log; echo "ACCEPT FAIL: display_host (discovery of the display and the cores, rotation search, pace grid, lost plane)"; exit 1; }
# the spectrum curve (shell-mpc/overlay/spectrum_curve.h: FFT of the overlay thread from the ring, 256 points, attack / release) and the spectrum view with a fake instance, the picture of both renderers, the controller
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I shell-mpc/overlay -o $B/spectrum_curve_host $OVSRC/spectrum_curve_host.cpp -lpthread || { echo "ACCEPT FAIL: spectrum_curve_host build"; exit 1; }
$B/spectrum_curve_host > $B/spectrum-curve-host.log 2>&1 || { grep -E "^FAIL" $B/spectrum-curve-host.log; echo "ACCEPT FAIL: spectrum_curve_host (the spectrum curve: sine, two sines, noise, silence, timing)"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o $B/spectrum_view_host $OVSRC/spectrum_view_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: spectrum_view_host build"; exit 1; }
$B/spectrum_view_host > $B/spectrum-view-host.log 2>&1 || { grep -E "^FAIL" $B/spectrum-view-host.log; echo "ACCEPT FAIL: spectrum_view_host (the spectrum view: scene = CPU picture, no redraw at rest, the controller)"; exit 1; }
# the page detector with real pictures of the Key 61 (the host's Q-Link panel over the right edge covers the right marker of the Play tab; the page must still be recognised)
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I shell-mpc/overlay -DQLINK_FIXTURE=\"shell-mpc/overlay/tests/fixtures/qlink_panel_fixtures.bin\" -o $B/page_detector_host $OVSRC/page_detector_host.cpp || { echo "ACCEPT FAIL: page_detector_host build"; exit 1; }
$B/page_detector_host > $B/page-detector-host.log 2>&1 || { grep -E "^FAIL" $B/page-detector-host.log; echo "ACCEPT FAIL: page_detector_host (the Play tab under the host's Q-Link panel)"; exit 1; }
# the key of the host's Q-Link panel (shell-mpc/overlay/panel_key.h): the reference of the strip, the band's left edge at the places of a slide, tolerance, popup, stale reference
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I shell-mpc/overlay -o $B/qlink_setting_host $OVSRC/qlink_setting_host.cpp || { echo "ACCEPT FAIL: qlink_setting_host build"; exit 1; }
$B/qlink_setting_host > $B/qlink-setting-host.log 2>&1 || { grep -E "^FAIL" $B/qlink-setting-host.log; echo "ACCEPT FAIL: qlink_setting_host (the host's Q-Link status option)"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I shell-mpc/overlay -o $B/stacking_host $OVSRC/stacking_host.cpp || { echo "ACCEPT FAIL: stacking_host build"; exit 1; }
$B/stacking_host > $B/stacking-host.log 2>&1 || { grep -E "^FAIL" $B/stacking-host.log; echo "ACCEPT FAIL: stacking_host (the zpos rule and the guard)"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I shell-mpc/overlay -o $B/panel_key_host $OVSRC/panel_key_host.cpp || { echo "ACCEPT FAIL: panel_key_host build"; exit 1; }
$B/panel_key_host > $B/panel-key-host.log 2>&1 || { grep -E "^FAIL" $B/panel-key-host.log; echo "ACCEPT FAIL: panel_key_host (the key of the host's Q-Link panel)"; exit 1; }
# the oscillator UI (shell-mpc/overlay/osc_ui.h: every control wired to its engine parameter, the pages, the Q group, the modulations; also under AddressSanitizer below)
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o $B/osc_ui_host $OVSRC/osc_ui_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: osc_ui_host build"; exit 1; }
$B/osc_ui_host > $B/osc-ui-host.log 2>&1 || { grep -E "^FAIL" $B/osc-ui-host.log; echo "ACCEPT FAIL: osc_ui_host (the oscillator UI: wiring, pages, Q group, modulations)"; exit 1; }
# the envelope UI (shell-mpc/overlay/env_ui.h: every control wired to its envN parameter in both units, the handles of the graph, the targets page, the Q groups, the running dots, the Play view's routing between the elements; also under AddressSanitizer below)
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o $B/env_ui_host $OVSRC/env_ui_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: env_ui_host build"; exit 1; }
$B/env_ui_host > $B/env-ui-host.log 2>&1 || { grep -E "^FAIL" $B/env-ui-host.log; echo "ACCEPT FAIL: env_ui_host (the envelope UI: wiring, graph, targets, Q groups)"; exit 1; }
# the LFO UI (shell-mpc/overlay/lfo_ui.h: the LFO and the random LFO wired to their parameters, the Q groups, the shape editor; also under AddressSanitizer below)
mkdir -p $B/ui-images/lfo
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o $B/lfo_ui_host $OVSRC/lfo_ui_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: lfo_ui_host build"; exit 1; }
LFO_IMAGES=$B/ui-images/lfo $B/lfo_ui_host > $B/lfo-ui-host.log 2>&1 || { grep -E "^FAIL" $B/lfo-ui-host.log; echo "ACCEPT FAIL: lfo_ui_host (the LFO UI: wiring, pages, Q groups, shape editor)"; exit 1; }
# the filter UI (shell-mpc/overlay/filter_ui.h: every control wired to its engine parameter, the pages, Q groups, Formant pad, dual curves, modulations; also under AddressSanitizer below)
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o $B/filter_ui_host $OVSRC/filter_ui_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: filter_ui_host build"; exit 1; }
$B/filter_ui_host > $B/filter-ui-host.log 2>&1 || { grep -E "^FAIL" $B/filter-ui-host.log; echo "ACCEPT FAIL: filter_ui_host (the filter UI: wiring, pages, Q groups, modulations)"; exit 1; }
# the mod matrix and macro UI (shell-mpc/overlay/mod_ui.h, macro_ui.h: matrix, slot details, macros, Q groups; also under AddressSanitizer below)
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o $B/mod_ui_host $OVSRC/mod_ui_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: mod_ui_host build"; exit 1; }
$B/mod_ui_host > $B/mod-ui-host.log 2>&1 || { grep -E "^FAIL" $B/mod-ui-host.log; echo "ACCEPT FAIL: mod_ui_host (the mod matrix and macro UI: wiring, matrix, detail, macros, Q groups)"; exit 1; }
# the mixer and FX UI (shell-mpc/overlay/mixer_ui.h, fx_ui.h: channels, faders, meters, destination popup, FX chain bar, slot editor, Q groups)
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -I shell-mpc/ui -o $B/mixfx_ui_host $OVSRC/mixfx_ui_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: mixfx_ui_host build"; exit 1; }
$B/mixfx_ui_host > $B/mixfx-ui-host.log 2>&1 || { grep -E "^FAIL" $B/mixfx-ui-host.log; echo "ACCEPT FAIL: mixfx_ui_host (the mixer and FX UI: layout, Q groups, parameters)"; exit 1; }
# the master and voice UI (shell-mpc/overlay/master_ui.h, voice_ui.h: output, pitch, voices, glide, scale, MPE; the step grid, the random, scaling and target blocks, Q groups)
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -I shell-mpc/ui -o $B/mastervoice_ui_host $OVSRC/mastervoice_ui_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: mastervoice_ui_host build"; exit 1; }
$B/mastervoice_ui_host > $B/mastervoice-ui-host.log 2>&1 || { grep -E "^FAIL" $B/mastervoice-ui-host.log; echo "ACCEPT FAIL: mastervoice_ui_host (the master and voice UI: wiring, grid, Q groups)"; exit 1; }
# the plugin-drawn pages (shell-mpc/overlay/page_view.h, pages.h: the real layout, the controller opens the element page of the tab, group changes, the blank instance picture, the readout targets)
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -I shell-mpc/ui -o $B/page_view_host $OVSRC/page_view_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: page_view_host build"; exit 1; }
$B/page_view_host > $B/page-view-host.log 2>&1 || { grep -E "^FAIL" $B/page-view-host.log; echo "ACCEPT FAIL: page_view_host (the plugin-drawn pages: opening, groups, readout targets)"; exit 1; }
# the Q slots the pages publish (shell-mpc/overlay/q_publish.h: sixteen Q proxies, the active group on Q 1 .. 4, the next groups of the page on 5 .. 16), the idle chips and the device rule (q_device.h)
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -I shell-mpc/ui -o $B/qslots_host $OVSRC/qslots_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: qslots_host build"; exit 1; }
$B/qslots_host > $B/qslots-host.log 2>&1 || { grep -E "^FAIL" $B/qslots-host.log; echo "ACCEPT FAIL: qslots_host (the Q slots of every page, the chips, the device rule)"; exit 1; }
# the pages of the OSC tab (shell-mpc/overlay/osc_page.h: the oscillator UI in its page mode with the oscillator's display: the oscillator the tab names, no CLOSE, Q groups, writes, detail pages)
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -I shell-mpc/ui -o $B/osc_page_host $OVSRC/osc_page_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: osc_page_host build"; exit 1; }
$B/osc_page_host > $B/osc-page-host.log 2>&1 || { grep -E "^FAIL" $B/osc-page-host.log; echo "ACCEPT FAIL: osc_page_host (the pages of the OSC tab)"; exit 1; }
# the demonstration images of the four elements the same way as the other elements (filter, mod + macros, mixer + fx, master + voice), into $B/ui-images/<element>
for d in filter mod mixfx mastervoice osc_page; do
    mkdir -p $B/ui-images/$d
    c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -I shell-mpc/ui -o $B/${d}_ui_demo $OVSRC/${d}_ui_demo.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: ${d}_ui_demo build"; exit 1; }
    $B/${d}_ui_demo $B/ui-images/$d > $B/$d-ui-demo.log 2>&1 || { grep -E "^FAIL" $B/$d-ui-demo.log; echo "ACCEPT FAIL: ${d}_ui_demo (the ${d} pages)"; exit 1; }
done
# the cached cells of the scene protocol (gpu/scene.h CellBegin, gpu/cell_plan.h): the hash, the plan of the layers, the damage between two pictures, the part to paint for the age of the back buffer, the CPU reference
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I shell-mpc/overlay -I shell-mpc -o $B/cell_plan_host $OVSRC/cell_plan_host.cpp || { echo "ACCEPT FAIL: cell_plan_host build"; exit 1; }
$B/cell_plan_host > $B/cell-plan-host.log 2>&1 || { grep -E "^FAIL" $B/cell-plan-host.log; echo "ACCEPT FAIL: cell_plan_host (cached cells: hash, plan, damage, paint boxes)"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I shell-mpc/overlay -o $B/cell_cache_host $OVSRC/cell_cache_host.cpp || { echo "ACCEPT FAIL: cell_cache_host build"; exit 1; }
$B/cell_cache_host > $B/cell-cache-host.log 2>&1 || { grep -E "^FAIL" $B/cell-cache-host.log; echo "ACCEPT FAIL: cell_cache_host (the layer cache and the partial picture: every buffer age)"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I shell-mpc/overlay -o $B/gpu_clock_host $OVSRC/gpu_clock_host.cpp || { echo "ACCEPT FAIL: gpu_clock_host build"; exit 1; }
$B/gpu_clock_host > $B/gpu-clock-host.log 2>&1 || { grep -E "^FAIL" $B/gpu-clock-host.log; echo "ACCEPT FAIL: gpu_clock_host (the GPU clock hint)"; exit 1; }
# the function tour table of the oscillator UI (the Key 61 script reads it): it must build and list every control
c++ -std=c++17 -O1 -Wall -Wextra -I core/include -I shell-mpc/overlay -o $B/tour_dump $OVSRC/tour_dump.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: tour_dump build"; exit 1; }
[ "$($B/tour_dump | wc -l)" -gt 800 ] || { echo "ACCEPT FAIL: tour_dump lists too few controls"; exit 1; }
# the controller, the views and the GPU backend once more under AddressSanitizer + UBSan (instances are freed while their page is on screen: no use after free)
for t in controller_host qlink_panel_host mouse_host views_host gpu_backend_host spectrum_view_host display_host osc_ui_host env_ui_host lfo_ui_host cell_plan_host cell_cache_host filter_ui_host mod_ui_host mixfx_ui_host mastervoice_ui_host page_view_host osc_page_host; do
    c++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -I core/include -I shell-mpc/overlay -I shell-mpc/ui -o $B/${t}_asan $OVSRC/$t.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: $t asan build"; exit 1; }
    $B/${t}_asan > $B/$t-asan.log 2>&1 || { grep -E "^FAIL|ERROR|runtime error" $B/$t-asan.log | head; echo "ACCEPT FAIL: $t under AddressSanitizer / UBSan"; exit 1; }
    grep -qE "ERROR: AddressSanitizer|runtime error" $B/$t-asan.log && { grep -E "ERROR: AddressSanitizer|runtime error" $B/$t-asan.log | head -5; echo "ACCEPT FAIL: $t under AddressSanitizer / UBSan"; exit 1; }
done
# the Q slots test under AddressSanitizer + UBSan too, with the libc++ container annotation check off: the FX part adds an effect through the static core library (not instrumented), which pushes to vectors the annotating libc++ then reports at the next reallocation (a false report of the annotation, the same code in mixfx_ui_host happens to grow its vectors differently)
c++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -I core/include -I shell-mpc/overlay -I shell-mpc/ui -o $B/qslots_host_asan $OVSRC/qslots_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: qslots_host asan build"; exit 1; }
ASAN_OPTIONS=detect_container_overflow=0 $B/qslots_host_asan > $B/qslots_host-asan.log 2>&1 || { grep -E "^FAIL|ERROR|runtime error" $B/qslots_host-asan.log | head; echo "ACCEPT FAIL: qslots_host under AddressSanitizer / UBSan"; exit 1; }
# the UI component library (shell-mpc/ui/, header only: the black box components, their interface tests and the demonstration program): the tests under AddressSanitizer + UBSan, the demo
# renders the oscillator pages and the detail pages into $B/ui-images (PNG, for a human; not compared)
mkdir -p $B/ui-images
for t in ui_base_host ui_composite_host ui_screen_host ui_list_host ui_display_host ui_env_host ui_lfo_host ui_time_ruler_host; do
    c++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -I core/include -o $B/${t}_asan shell-mpc/ui/tests/$t.cpp || { echo "ACCEPT FAIL: $t build"; exit 1; }
    UI_OUT=$B/ui-images $B/${t}_asan > $B/$t.log 2>&1 || { grep -E "^FAIL|ERROR|runtime error" $B/$t.log | head; echo "ACCEPT FAIL: $t (UI components: interface, drawing, Q groups)"; exit 1; }
    grep -qE "ERROR: AddressSanitizer|runtime error" $B/$t.log && { grep -E "ERROR: AddressSanitizer|runtime error" $B/$t.log | head -5; echo "ACCEPT FAIL: $t under AddressSanitizer / UBSan"; exit 1; }
done
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -o $B/ui_demo shell-mpc/ui/tests/ui_demo.cpp || { echo "ACCEPT FAIL: ui_demo build"; exit 1; }
$B/ui_demo $B/ui-images > $B/ui-demo.log 2>&1 || { grep -E "^FAIL" $B/ui-demo.log; echo "ACCEPT FAIL: ui_demo (the demonstration pages)"; exit 1; }
# the envelope element: the library parts (ui_env_demo) and the real pages driven by engine parameters (env_ui_demo), into $B/ui-images/env
mkdir -p $B/ui-images/env
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -o $B/ui_env_demo shell-mpc/ui/tests/ui_env_demo.cpp || { echo "ACCEPT FAIL: ui_env_demo build"; exit 1; }
$B/ui_env_demo $B/ui-images/env > $B/ui-env-demo.log 2>&1 || { grep -E "^FAIL" $B/ui-env-demo.log; echo "ACCEPT FAIL: ui_env_demo (the envelope parts)"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o $B/env_ui_demo $OVSRC/env_ui_demo.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: env_ui_demo build"; exit 1; }
$B/env_ui_demo $B/ui-images/env > $B/env-ui-demo.log 2>&1 || { grep -E "^FAIL" $B/env-ui-demo.log; echo "ACCEPT FAIL: env_ui_demo (the envelope pages)"; exit 1; }
$B/stub_host "$OVLON" > $B/stub-ovlon.log 2>&1 || { tail -15 $B/stub-ovlon.log; echo "ACCEPT FAIL: stub_host with the overlay built in"; exit 1; }
live_build padoff -DSYNTHYBOY_MPC_PAD_PRESSURE=OFF
# output level (shell-mpc/output_stage.h): the stage alone, then the plugin in both limiter states:
# 1. limiter OFF (default): make-up gain (18 dB) against gain 0 dB (intact dynamics, peaks over 0 dBFS)
# 2. limiter ON: make-up gain (18 dB) against gain 0 dB (peaks held at -1 dBFS ceiling)
sh shell-mpc/tests/run_output_stage_test.sh > $B/output-stage.log 2>&1 || { grep -E "^FAIL|error" $B/output-stage.log | head; echo "ACCEPT FAIL: output stage test"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Werror -I shell-mpc -I $MPC_SDK/framework -o $B/output_level_host shell-mpc/tests/output_level_host.cpp || { echo "ACCEPT FAIL: output_level_host build"; exit 1; }
$B/output_level_host "$(find build-mpc-live-on -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)" "$(find build-mpc-live-gain0 -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)" 18 off > $B/output-level-off.log 2>&1 || { grep -E "^FAIL|^quiet|^loud|^dynamics" $B/output-level-off.log; echo "ACCEPT FAIL: output_level_host (limiter OFF)"; exit 1; }
$B/output_level_host "$(find build-mpc-live-limiteron -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)" "$(find build-mpc-live-limitergain0 -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)" 18 on > $B/output-level-on.log 2>&1 || { grep -E "^FAIL|^quiet|^loud" $B/output-level-on.log; echo "ACCEPT FAIL: output_level_host (limiter ON)"; exit 1; }
cat $B/output-level-off.log $B/output-level-on.log > $B/output-level.log
grep -E "^quiet|^loud|^dynamics" $B/output-level.log
c++ -std=c++17 -O1 -Wall -Wextra $LIVEINC -o $B/lfo_live_host shell-mpc/tests/lfo_live_host.cpp $B/core/libsynthyboy_core.a -lpthread || { echo "ACCEPT FAIL: lfo_live_host build"; exit 1; }
# the refresh rate: the default is 10 a second (at most 21 calls per cursor in 2 s), SYNTHYBOY_MPC_LFO_LIVE_HZ=25 still changes it (the checks scale with HZ)
for v in off:off:10 on:on:10 self:selftest:10 quiet:quiet:10 hz25:on:25; do
    d=${v%%:*}; r=${v#*:}; m=${r%%:*}; hz=${r##*:}
    $B/lfo_live_host "$(find build-mpc-live-$d -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)" $m $hz > $B/live-$d.log 2>&1 || { grep -E "^FAIL" $B/live-$d.log; echo "ACCEPT FAIL: lfo_live_host ($m, $hz Hz)"; exit 1; }
done
# the plugin with the overlay built in (on the Mac the module finds no display): the same chunk and the same LFO display as the build without it
$B/lfo_live_host "$OVLON" on overlay > $B/live-ovlon.log 2>&1 || { grep -E "^FAIL" $B/live-ovlon.log; echo "ACCEPT FAIL: lfo_live_host with the overlay built in (one more display parameter, Overlay Instance)"; exit 1; }
# the display parameter "Overlay Instance" (only in the overlay build): slots of several instances, the debug file, 17 instances, instances created and closed on several threads. The other builds above
# were checked with exact parameter counts without it (the tests take the word "overlay" for the one build that has the extra parameter and are exact in both cases).
c++ -std=c++17 -O1 -Wall -Wextra -Werror -I core/include -I shell-mpc -I $MPC_SDK/framework -o $B/overlay_instance_host shell-mpc/tests/overlay_instance_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: overlay_instance_host build"; exit 1; }
$B/overlay_instance_host "$OVLON" > $B/overlay-instance.log 2>&1 || { grep -E "^FAIL" $B/overlay-instance.log; echo "ACCEPT FAIL: overlay_instance_host (Overlay Instance parameter)"; exit 1; }
# the Play page's parameters (shell-mpc/play_params.h, docs/MPC.md "Play page"): selectors, proxies, chunk, no feedback loop (the list, the order and the counts are checked there)
c++ -std=c++17 -O1 -Wall -Wextra -Werror -I core/include -I shell-mpc -I $MPC_SDK/framework -o $B/play_host shell-mpc/tests/play_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: play_host build"; exit 1; }
$B/play_host "$OVLON" > $B/play.log 2>&1 || { grep -E "^FAIL" $B/play.log; echo "ACCEPT FAIL: play_host (the Play page's parameters)"; exit 1; }
# the Play page's parameter table is built when the library is loaded, not by the first caller (the host may call getParameter / setParameter from the audio thread)
c++ -std=c++17 -O1 -Wall -Wextra -Werror -I core/include -I shell-mpc -o $B/play_params_rt_host shell-mpc/tests/play_params_rt_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: play_params_rt_host build"; exit 1; }
$B/play_params_rt_host > $B/play-params-rt.log 2>&1 || { grep -E "^FAIL" $B/play-params-rt.log; echo "ACCEPT FAIL: play_params_rt_host (the table of the Play proxies must exist before the first lookup, no allocation)"; exit 1; }
# saving and reloading a project with the Play page (docs/MPC.md "Project save and reload"): the load orders a host could use (chunk only, write-back of every parameter before / after the chunk, program re-selection, the order seen on the Key 61), the restore window, the debug log
c++ -std=c++17 -O1 -Wall -Wextra -Werror -I core/include -I shell-mpc -I $MPC_SDK/framework -o $B/play_restore_host shell-mpc/tests/play_restore_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: play_restore_host build"; exit 1; }
$B/play_restore_host "$OVLON" > $B/play-restore.log 2>&1 || { grep -E "^FAIL" $B/play-restore.log | cut -c1-400; echo "ACCEPT FAIL: play_restore_host (project save and reload of the Play page)"; exit 1; }
# instances created and closed on three threads while the display thread reports (docs/MPC.md "Closing an instance"): the host is never called for an instance whose effClose returned
c++ -std=c++17 -O1 -Wall -Wextra -Werror -I core/include -I shell-mpc -I $MPC_SDK/framework -o $B/close_stress_host shell-mpc/tests/close_stress_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: close_stress_host build"; exit 1; }
$B/close_stress_host "$OVLON" 120 > $B/close-stress.log 2>&1 || { grep -E "^FAIL" $B/close-stress.log; echo "ACCEPT FAIL: close_stress_host (host called after effClose)"; exit 1; }
# the overlay builds without the parameter must not exist: a build that is not the overlay one has no trace of it (the parameter's name is in the overlay build only)
for f in "$OVLOFF" "$OVLDEF" "$LIB"; do
    grep -q "Overlay Instance" "$f" && { echo "ACCEPT FAIL: Overlay Instance in a build without SYNTHYBOY_MPC_OVERLAY"; exit 1; }
done
grep -q "Overlay Instance" "$OVLON" || { echo "ACCEPT FAIL: the overlay build has no Overlay Instance parameter"; exit 1; }
[ "$(grep -h '^chunk ' $B/live-on.log)" = "$(grep -h '^chunk ' $B/live-ovlon.log)" ] || { echo "ACCEPT FAIL: the state chunk differs in the overlay build"; exit 1; }
# live spectrum (SYNTHYBOY_MPC_SPECTRUM, default ON): the band mapping and the analyser on known signals, then the plugin in four variants (off, on, the self-test wave at 10 and 25 a second).
# The switched-off build has no trace of it; the state chunk is the same in every variant.
c++ -std=c++17 -O1 -Wall -Wextra -Werror -I shell-mpc -o $B/spectrum_bands_host shell-mpc/tests/spectrum_bands_host.cpp || { echo "ACCEPT FAIL: spectrum_bands_host build"; exit 1; }
$B/spectrum_bands_host > $B/spectrum-bands.log 2>&1 || { grep -E "^FAIL" $B/spectrum-bands.log; echo "ACCEPT FAIL: spectrum_bands_host"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Werror -I shell-mpc -o $B/spectrum_host shell-mpc/tests/spectrum_host.cpp -lpthread || { echo "ACCEPT FAIL: spectrum_host build"; exit 1; }
$B/spectrum_host > $B/spectrum-host.log 2>&1 || { grep -E "^FAIL" $B/spectrum-host.log; echo "ACCEPT FAIL: spectrum_host"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra $LIVEINC -o $B/spectrum_live_host shell-mpc/tests/spectrum_live_host.cpp $B/core/libsynthyboy_core.a -lpthread || { echo "ACCEPT FAIL: spectrum_live_host build"; exit 1; }
for v in specoff:off:10 on:on:10 specself:selftest:10 specself25:selftest:25; do
    d=${v%%:*}; r=${v#*:}; m=${r%%:*}; hz=${r##*:}
    $B/spectrum_live_host "$(find build-mpc-live-$d -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)" $m $hz > $B/spectrum-live-$d.log 2>&1 || { grep -E "^FAIL" $B/spectrum-live-$d.log; echo "ACCEPT FAIL: spectrum_live_host ($m, $hz Hz)"; exit 1; }
done
for d in specoff specself specself25; do
    [ "$(grep -h '^chunk ' $B/spectrum-live-on.log)" = "$(grep -h '^chunk ' $B/spectrum-live-$d.log)" ] || { echo "ACCEPT FAIL: the state chunk differs in the spectrum variant $d"; exit 1; }
done
grep -q "Spectrum %d" "$(find build-mpc-live-on -name '*synthyboy_mpc*.so' -o -name '*synthyboy_mpc*.dylib' | head -1)" || { echo "ACCEPT FAIL: the spectrum build has no spectrum code"; exit 1; }
grep -q "Spectrum %d" "$(find build-mpc-live-specoff -name '*synthyboy_mpc*.so' -o -name '*synthyboy_mpc*.dylib' | head -1)" && { echo "ACCEPT FAIL: trace of the spectrum in the build with SYNTHYBOY_MPC_SPECTRUM=OFF"; exit 1; }
# pad pressure to Filter 1 cutoff (SYNTHYBOY_MPC_PAD_PRESSURE, default ON): pad_pressure.h and the plugin in the own engine (the pressure opens the filter), in the Vital engine
# (the two parameters exist, the audio ignores them), without the display parameters, and switched off (no trace of it: no names, no chunk member)
c++ -std=c++17 -O1 -Wall -Wextra $LIVEINC -o $B/pad_pressure_host shell-mpc/tests/pad_pressure_host.cpp $B/core/libsynthyboy_core.a -lpthread || { echo "ACCEPT FAIL: pad_pressure_host build"; exit 1; }
pp_lib() { find build-mpc-live-$1 -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1; }
$B/pad_pressure_host "$(pp_lib on)" own > $B/pad-pressure-own.log 2>&1 || { grep -E "^FAIL" $B/pad-pressure-own.log; echo "ACCEPT FAIL: pad_pressure_host (own engine)"; exit 1; }
grep -E "^INFO" $B/pad-pressure-own.log
$B/pad_pressure_host "$(pp_lib off)" own 0 > $B/pad-pressure-nodisplay.log 2>&1 || { grep -E "^FAIL" $B/pad-pressure-nodisplay.log; echo "ACCEPT FAIL: pad_pressure_host (no display parameters)"; exit 1; }
$B/pad_pressure_host "$LIB" vital > $B/pad-pressure-vital.log 2>&1 || { grep -E "^FAIL" $B/pad-pressure-vital.log; echo "ACCEPT FAIL: pad_pressure_host (Vital engine)"; exit 1; }
$B/pad_pressure_host "$(pp_lib padoff)" own off > $B/pad-pressure-off.log 2>&1 || { grep -E "^FAIL" $B/pad-pressure-off.log; echo "ACCEPT FAIL: pad_pressure_host (switched off)"; exit 1; }
$B/pad_pressure_host "$OVLON" own overlay > $B/pad-pressure-ovlon.log 2>&1 || { grep -E "^FAIL" $B/pad-pressure-ovlon.log; echo "ACCEPT FAIL: pad_pressure_host with the overlay built in (Overlay Instance after the pad pressure parameters)"; exit 1; }
grep -q "Pad Pressure Amount" "$(pp_lib on)" || { echo "ACCEPT FAIL: the pad pressure build has no pad pressure code"; exit 1; }
grep -q "Pad Pressure Amount" "$(pp_lib padoff)" && { echo "ACCEPT FAIL: trace of the pad pressure in the build with SYNTHYBOY_MPC_PAD_PRESSURE=OFF"; exit 1; }
# callback statistics (SYNTHYBOY_MPC_CALLBACK_STATS, diagnostics): off in the default build (no trace of it in the module), on in the stats variant
STATSLIB="$(find build-mpc-live-stats -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)"
grep -q "dispatcher_opcodes:" "$STATSLIB" || { echo "ACCEPT FAIL: the stats variant has no host statistics (format 2)"; exit 1; }
grep -q "synthyboy-callback-stats" "$STATSLIB" || { echo "ACCEPT FAIL: the stats variant has no statistics code"; exit 1; }
for f in "$LIB" "$(find build-mpc-live-on -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)"; do
    grep -q "synthyboy-callback-stats" "$f" && { echo "ACCEPT FAIL: statistics code in a build without SYNTHYBOY_MPC_CALLBACK_STATS"; exit 1; }
    grep -q "dispatcher_opcodes:" "$f" && { echo "ACCEPT FAIL: host statistics (format 2) in a build without SYNTHYBOY_MPC_CALLBACK_STATS"; exit 1; }
done
c++ -std=c++17 -O1 -Wall -Wextra -I $MPC_SDK/framework -I shell-mpc -o $B/callback_stats_host shell-mpc/tests/callback_stats_host.cpp -lpthread || { echo "ACCEPT FAIL: callback_stats_host build"; exit 1; }
$B/callback_stats_host "$STATSLIB" $B/callback-stats.txt > $B/callback-stats.log 2>&1 || { grep -E "^FAIL" $B/callback-stats.log; echo "ACCEPT FAIL: callback_stats_host"; exit 1; }
[ "$(grep -h '^chunk ' $B/live-off.log)" = "$(grep -h '^chunk ' $B/live-on.log)" ] && [ "$(grep -h '^chunk ' $B/live-off.log)" = "$(grep -h '^chunk ' $B/live-self.log)" ] || { echo "ACCEPT FAIL: the state chunk differs between the LFO display variants"; exit 1; }
TL=build-mpc-live-tsan
cmake -S . -B $TL -G Ninja $BASE_OPTS -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=OFF -DSYNTHYBOY_BUILD_MPC_SHELL=ON -DSYNTHYBOY_SHELL_ENGINE=own -DSYNTHYBOY_MPC_CALLBACK_STATS=ON -DSYNTHYBOY_MPC_OVERLAY=ON -DMPC_SDK_DIR="$MPC_SDK" "-DCMAKE_CXX_FLAGS=-fsanitize=thread -g" "-DCMAKE_C_FLAGS=-fsanitize=thread -g" "-DCMAKE_MODULE_LINKER_FLAGS=-fsanitize=thread" > $TL.configure.log 2>&1 && cmake --build $TL --target synthyboy_mpc > $TL.build.log 2>&1 || { tail -30 $TL.build.log; echo "ACCEPT FAIL: ThreadSanitizer build of the LFO display"; exit 1; }
TLLIB=$(find $TL -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)
c++ -std=c++17 -O1 -g -fsanitize=thread $LIVEINC -o $TL/lfo_live_host shell-mpc/tests/lfo_live_host.cpp $TL/core/libsynthyboy_core.a -lpthread || { echo "ACCEPT FAIL: tsan lfo_live_host build"; exit 1; }
TSAN_OPTIONS="halt_on_error=0 exitcode=0" $TL/lfo_live_host "$TLLIB" on overlay > $TL/live.log 2>&1
if grep -q "WARNING: ThreadSanitizer" $TL/live.log; then
    grep -A14 "WARNING: ThreadSanitizer" $TL/live.log | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20
    echo "ACCEPT FAIL: data races in the LFO display (full report: $TL/live.log)"; exit 1
fi
grep -q "^FAIL" $TL/live.log && { grep "^FAIL" $TL/live.log; echo "ACCEPT FAIL: tsan lfo_live_host"; exit 1; }
# the spectrum ring (audio thread writes, display thread reads) and the plugin with the spectrum under ThreadSanitizer
c++ -std=c++17 -O1 -g -fsanitize=thread -I shell-mpc -o $TL/spectrum_host shell-mpc/tests/spectrum_host.cpp -lpthread || { echo "ACCEPT FAIL: tsan spectrum_host build"; exit 1; }
TSAN_OPTIONS="halt_on_error=0 exitcode=0" $TL/spectrum_host > $TL/spectrum-host.log 2>&1
c++ -std=c++17 -O1 -g -fsanitize=thread $LIVEINC -o $TL/spectrum_live_host shell-mpc/tests/spectrum_live_host.cpp $TL/core/libsynthyboy_core.a -lpthread || { echo "ACCEPT FAIL: tsan spectrum_live_host build"; exit 1; }
TSAN_OPTIONS="halt_on_error=0 exitcode=0" $TL/spectrum_live_host "$TLLIB" on 10 overlay > $TL/spectrum-live.log 2>&1
for f in $TL/spectrum-host.log $TL/spectrum-live.log; do
    if grep -q "WARNING: ThreadSanitizer" $f; then
        grep -A14 "WARNING: ThreadSanitizer" $f | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20
        echo "ACCEPT FAIL: data races in the spectrum (full report: $f)"; exit 1
    fi
    grep -q "^FAIL" $f && { grep "^FAIL" $f; echo "ACCEPT FAIL: tsan spectrum test ($f)"; exit 1; }
done
# the spectrum curve with the audio thread pushing into the ring while the overlay thread analyses (ThreadSanitizer)
c++ -std=c++17 -O1 -g -fsanitize=thread -I shell-mpc/overlay -o $TL/spectrum_curve_host shell-mpc/overlay/tests/spectrum_curve_host.cpp -lpthread || { echo "ACCEPT FAIL: tsan spectrum_curve_host build"; exit 1; }
TSAN_OPTIONS="halt_on_error=0 exitcode=0" $TL/spectrum_curve_host > $TL/spectrum-curve.log 2>&1
if grep -q "WARNING: ThreadSanitizer" $TL/spectrum-curve.log; then
    grep -A14 "WARNING: ThreadSanitizer" $TL/spectrum-curve.log | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20
    echo "ACCEPT FAIL: data races in the spectrum curve (full report: $TL/spectrum-curve.log)"; exit 1
fi
grep -q "^FAIL" $TL/spectrum-curve.log && { grep "^FAIL" $TL/spectrum-curve.log; echo "ACCEPT FAIL: tsan spectrum_curve_host"; exit 1; }
# pad pressure under ThreadSanitizer (the settings are written by the host thread and read by the audio thread through the engine's atomic)
c++ -std=c++17 -O1 -g -fsanitize=thread $LIVEINC -o $TL/pad_pressure_host shell-mpc/tests/pad_pressure_host.cpp $TL/core/libsynthyboy_core.a -lpthread || { echo "ACCEPT FAIL: tsan pad_pressure_host build"; exit 1; }
TSAN_OPTIONS="halt_on_error=0 exitcode=0" $TL/pad_pressure_host "$TLLIB" own overlay > $TL/pad-pressure.log 2>&1
if grep -q "WARNING: ThreadSanitizer" $TL/pad-pressure.log; then
    grep -A14 "WARNING: ThreadSanitizer" $TL/pad-pressure.log | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20
    echo "ACCEPT FAIL: data races in the pad pressure (full report: $TL/pad-pressure.log)"; exit 1
fi
grep -q "^FAIL" $TL/pad-pressure.log && { grep "^FAIL" $TL/pad-pressure.log; echo "ACCEPT FAIL: tsan pad_pressure_host"; exit 1; }
# the same ThreadSanitizer build has the statistics switched on: the display thread and the writer thread read what the audio thread writes
c++ -std=c++17 -O1 -g -fsanitize=thread -I $MPC_SDK/framework -I shell-mpc -o $TL/callback_stats_host shell-mpc/tests/callback_stats_host.cpp -lpthread || { echo "ACCEPT FAIL: tsan callback_stats_host build"; exit 1; }
TSAN_OPTIONS="halt_on_error=0 exitcode=0" $TL/callback_stats_host "$TLLIB" $TL/callback-stats.txt > $TL/stats.log 2>&1
if grep -q "WARNING: ThreadSanitizer" $TL/stats.log; then
    grep -A14 "WARNING: ThreadSanitizer" $TL/stats.log | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20
    echo "ACCEPT FAIL: data races in the callback statistics (full report: $TL/stats.log)"; exit 1
fi
grep -q "^FAIL" $TL/stats.log && { grep "^FAIL" $TL/stats.log; echo "ACCEPT FAIL: tsan callback_stats_host"; exit 1; }
# the overlay thread (the tsan build above has the module in: it runs next to the display thread and the statistics writer) and the controller on a real thread against changing parameters, markers and instances
TSCORE=$(find $TL/core -name '*.a' | tr '\n' ' ')
for t in controller_host curve_host gpu_scene_host gpu_backend_host; do
    c++ -std=c++17 -O1 -g -fsanitize=thread -I core/include -I shell-mpc/overlay -o $TL/$t shell-mpc/overlay/tests/$t.cpp $TSCORE -lpthread || { echo "ACCEPT FAIL: tsan $t build"; exit 1; }
    TSAN_OPTIONS="halt_on_error=0 exitcode=0" $TL/$t > $TL/$t.log 2>&1
    if grep -q "WARNING: ThreadSanitizer" $TL/$t.log; then
        grep -A14 "WARNING: ThreadSanitizer" $TL/$t.log | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20
        echo "ACCEPT FAIL: data races in the overlay ($t; full report: $TL/$t.log)"; exit 1
    fi
    grep -q "^FAIL" $TL/$t.log && { grep "^FAIL" $TL/$t.log; echo "ACCEPT FAIL: tsan $t"; exit 1; }
done
TSAN_OPTIONS="halt_on_error=0 exitcode=0" $TL/lfo_live_host "$TLLIB" on overlay > $TL/live-ovl.log 2>&1
# instances created, closed and asked for their slot on several threads against the overlay thread, in the plugin under ThreadSanitizer
c++ -std=c++17 -O1 -g -fsanitize=thread -I core/include -I shell-mpc -I $MPC_SDK/framework -o $TL/overlay_instance_host shell-mpc/tests/overlay_instance_host.cpp $TSCORE -lpthread || { echo "ACCEPT FAIL: tsan overlay_instance_host build"; exit 1; }
TSAN_OPTIONS="halt_on_error=0 exitcode=0" $TL/overlay_instance_host "$TLLIB" > $TL/overlay-instance.log 2>&1
grep -q "WARNING: ThreadSanitizer" $TL/overlay-instance.log && { grep -A14 "WARNING: ThreadSanitizer" $TL/overlay-instance.log | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20; echo "ACCEPT FAIL: data races in Overlay Instance (full report: $TL/overlay-instance.log)"; exit 1; }
grep -q "^FAIL" $TL/overlay-instance.log && { grep "^FAIL" $TL/overlay-instance.log; echo "ACCEPT FAIL: tsan overlay_instance_host"; exit 1; }
# the Play page's selectors and proxies: host writes, the display thread's reports and the echoing host, under ThreadSanitizer
c++ -std=c++17 -O1 -g -fsanitize=thread -I core/include -I shell-mpc -I $MPC_SDK/framework -o $TL/play_host shell-mpc/tests/play_host.cpp $TSCORE -lpthread || { echo "ACCEPT FAIL: tsan play_host build"; exit 1; }
TSAN_OPTIONS="halt_on_error=0 exitcode=0" $TL/play_host "$TLLIB" > $TL/play.log 2>&1
grep -q "WARNING: ThreadSanitizer" $TL/play.log && { grep -A14 "WARNING: ThreadSanitizer" $TL/play.log | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20; echo "ACCEPT FAIL: data races in the Play page's parameters (full report: $TL/play.log)"; exit 1; }
grep -q "^FAIL" $TL/play.log && { grep "^FAIL" $TL/play.log; echo "ACCEPT FAIL: tsan play_host"; exit 1; }
grep -q "WARNING: ThreadSanitizer" $TL/live-ovl.log && { grep -A14 "WARNING: ThreadSanitizer" $TL/live-ovl.log | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20; echo "ACCEPT FAIL: data races with the overlay thread in the plugin (full report: $TL/live-ovl.log)"; exit 1; }
# the same stress test under ThreadSanitizer (the display thread, the overlay thread and the host's threads) and under AddressSanitizer + UndefinedBehaviorSanitizer (a member used after it was destroyed, a leaked or doubly freed instance)
c++ -std=c++17 -O1 -g -fsanitize=thread -I core/include -I shell-mpc -I $MPC_SDK/framework -o $TL/close_stress_host shell-mpc/tests/close_stress_host.cpp $TSCORE -lpthread || { echo "ACCEPT FAIL: tsan close_stress_host build"; exit 1; }
TSAN_OPTIONS="halt_on_error=0 exitcode=0" $TL/close_stress_host "$TLLIB" 40 > $TL/close-stress.log 2>&1
grep -q "WARNING: ThreadSanitizer" $TL/close-stress.log && { grep -A14 "WARNING: ThreadSanitizer" $TL/close-stress.log | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20; echo "ACCEPT FAIL: data races when instances are closed (full report: $TL/close-stress.log)"; exit 1; }
grep -q "^FAIL" $TL/close-stress.log && { grep "^FAIL" $TL/close-stress.log; echo "ACCEPT FAIL: tsan close_stress_host"; exit 1; }
grep -q "^OK" $TL/close-stress.log || { tail -5 $TL/close-stress.log; echo "ACCEPT FAIL: tsan close_stress_host did not finish"; exit 1; }
AL=build-mpc-live-asan
cmake -S . -B $AL -G Ninja $BASE_OPTS -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=OFF -DSYNTHYBOY_BUILD_MPC_SHELL=ON -DSYNTHYBOY_SHELL_ENGINE=own -DSYNTHYBOY_MPC_OVERLAY=ON -DMPC_SDK_DIR="$MPC_SDK" "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -g" "-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -g" "-DCMAKE_MODULE_LINKER_FLAGS=-fsanitize=address,undefined" > $AL.configure.log 2>&1 && cmake --build $AL --target synthyboy_mpc > $AL.build.log 2>&1 || { tail -30 $AL.build.log; echo "ACCEPT FAIL: AddressSanitizer build of the plugin"; exit 1; }
ALIB="$(find $AL -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)"
ASCORE=$(find $AL/core -name '*.a' | tr '\n' ' ')
c++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -I core/include -I shell-mpc -I $MPC_SDK/framework -o $AL/close_stress_host shell-mpc/tests/close_stress_host.cpp $ASCORE -lpthread || { echo "ACCEPT FAIL: asan close_stress_host build"; exit 1; }
ASAN_OPTIONS="detect_leaks=0:halt_on_error=1" UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" $AL/close_stress_host "$ALIB" 40 > $AL/close-stress.log 2>&1 || { tail -30 $AL/close-stress.log; echo "ACCEPT FAIL: asan close_stress_host (memory error when instances are closed)"; exit 1; }
grep -q "^OK" $AL/close-stress.log || { tail -5 $AL/close-stress.log; echo "ACCEPT FAIL: asan close_stress_host did not finish"; exit 1; }
# the Q proxies' router (shell-mpc/q_router.h): the logic alone (group, reads, writes, restore window, reports, no allocation), plainly and under ThreadSanitizer (audio, UI and display threads at once)
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I shell-mpc -o $B/q_router_host shell-mpc/tests/q_router_host.cpp -lpthread || { echo "ACCEPT FAIL: q_router_host build"; exit 1; }
$B/q_router_host > $B/q-router.log 2>&1 || { grep -E "^FAIL" $B/q-router.log; echo "ACCEPT FAIL: q_router_host (the Q proxies' router)"; exit 1; }
c++ -std=c++17 -O1 -g -fsanitize=thread -I shell-mpc -o $TL/q_router_host shell-mpc/tests/q_router_host.cpp -lpthread || { echo "ACCEPT FAIL: tsan q_router_host build"; exit 1; }
TSAN_OPTIONS="halt_on_error=0 exitcode=0" $TL/q_router_host > $TL/q-router.log 2>&1
grep -q "WARNING: ThreadSanitizer" $TL/q-router.log && { grep -A14 "WARNING: ThreadSanitizer" $TL/q-router.log | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20; echo "ACCEPT FAIL: data races in the Q router (full report: $TL/q-router.log)"; exit 1; }
grep -q "^FAIL" $TL/q-router.log && { grep "^FAIL" $TL/q-router.log; echo "ACCEPT FAIL: tsan q_router_host"; exit 1; }
# the touch decoder and the evdev reader (touch_decoder.h, touch_linux.h; fakes for the syscalls, a capture of the Key 61 as a fixture): plainly with -Werror and under AddressSanitizer + UBSan
bash shell-mpc/overlay/tests/run_touch_tests.sh > $B/touch.log 2>&1 || { grep -E "FAIL|error" $B/touch.log | head -20; echo "ACCEPT FAIL: touch_host"; exit 1; }
echo "ACCEPT OK"
