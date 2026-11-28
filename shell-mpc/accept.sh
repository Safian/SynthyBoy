#!/bin/sh
# Acceptance for shell-mpc (native build + SDK host simulator). Needs MPC_SDK.
set -u
: "${MPC_SDK:?MPC_SDK must point at mpc-plugin-builder-sdk}"
cd "$(dirname "$0")/.." || exit 1
build_root="/tmp/synthyboy-$(id -u)/$(pwd -P | cksum | cut -d' ' -f1)/accept"
mkdir -p "$build_root" || exit 1
B=$build_root/build-mpc
# The build options of the shipped plugin (what the package has: live LFO and spectrum display, pad pressure; the developer log is ON here, the release proof below builds it OFF).
# Every option is named so that a build directory that keeps the entries of earlier runs in its CMake cache cannot change the configuration.
BASE_OPTS="-DSYNTHYBOY_MPC_LFO_LIVE=ON -DSYNTHYBOY_MPC_SPECTRUM=ON -DSYNTHYBOY_MPC_PAD_PRESSURE=ON -DSYNTHYBOY_MPC_DEV_LOG=ON"
cmake -S . -B $B -G Ninja $BASE_OPTS -DCMAKE_BUILD_TYPE=Release -DSYNTHYBOY_BUILD_MPC_SHELL=ON -DMPC_SDK_DIR="$MPC_SDK" > $B.configure.log 2>&1 || { tail -40 $B.configure.log; echo "ACCEPT FAIL: cmake configure"; exit 1; }
cmake --build $B --target synthyboy_mpc dump_mpc_chunks synthyboy_sound_lab > $B/build.log 2>&1 || { grep -E "error|Error|undefined|FAILED" -A6 $B/build.log | head -80; echo "ACCEPT FAIL: build"; exit 1; }
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
# the names that do not fit the 31 characters of the host's buffer are shortened by the plugin (host_strings.h): the docs names go through the same function
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I shell-mpc -o $B/short_name_filter shell-mpc/tests/short_name_filter.cpp || { echo "ACCEPT FAIL: short_name_filter build"; exit 1; }
python3 - $B/dump.json docs/params.json $B/short_name_filter <<'PY' || { echo "ACCEPT FAIL: parameter table"; exit 1; }
import json, subprocess, sys
d = json.load(open(sys.argv[1])); p = json.load(open(sys.argv[2]))
a = [x["name"] for x in d["params"]]; b = [x["name"] for x in p["params"]]
b = subprocess.run([sys.argv[3]], input="\n".join(b) + "\n", capture_output=True, text=True, check=True).stdout.split("\n")[:-1]   # the names as the host gets them
ok = True
extra = ["LFO %d Cursor" % n for n in (1, 2, 3, 4)] + ["Spectrum %d" % n for n in range(1, 33)] + ["Pad Pressure", "Pad Pressure Amount"]   # SYNTHYBOY_MPC_LFO_LIVE and SYNTHYBOY_MPC_SPECTRUM (default ON): display-only parameters after the engine's list, then the two SYNTHYBOY_MPC_PAD_PRESSURE parameters
if len(a) < p["count"] + len(extra) or a[p["count"]:p["count"] + len(extra)] != extra: print(f"param count {len(a)}: the {len(extra)} display parameters do not follow the {p['count']} engine parameters: {a[p['count']:]}"); ok = False   # (the overlay parameters come after them)
for i, (x, y) in enumerate(zip(a, b)):
    if x != y: print(f"param {i}: plugin '{x}' != docs '{y}'"); ok = False; break
for k, v in (("name", "SynthyBoy"), ("vendor", "Safian"), ("uid", "SfSB"), ("isInstrument", True), ("numInputs", 0), ("numOutputs", 2)):
    if d.get(k) != v: print(f"{k}: {d.get(k)!r} != {v!r}"); ok = False
if "Init" not in d.get("programs", []): print("no program named Init:", d.get("programs")); ok = False
sys.exit(0 if ok else 1)
PY
EXP=$(nm -gU "$LIB" | awk '{print $NF}' | sort | tr '\n' ' ')
[ "$EXP" = "_VSTPluginMain " ] || { echo "exports: $EXP" | cut -c1-600; echo "ACCEPT FAIL: exported symbols must be only _VSTPluginMain"; exit 1; }
# the shell creates the own engine
grep -q "EngineKind::own" shell-mpc/synthyboy_mpc.cpp || { echo "ACCEPT FAIL: the source does not create the own engine"; exit 1; }
# plugin folder / content path logic
c++ -std=c++17 -O1 -Wall -Wextra -o $B/paths_test shell-mpc/tests/paths_test.cpp && $B/paths_test > $B/paths.log 2>&1 || { cat $B/paths.log 2>/dev/null | tail -12; echo "ACCEPT FAIL: paths_test (shell-mpc/plugin_paths.h)"; exit 1; }
for f in scripts/build-mpc.sh scripts/mpc-container-build.sh; do [ -f $f ] && bash -n $f || { echo "ACCEPT FAIL: $f missing or not valid bash"; exit 1; }; done
# factory presets as programs: the list header, then the library inside a plugin folder (Content/presets) and outside one
INC="-I core/include -I core/filters-serum/include -I core/shapes/include -I $MPC_SDK/framework"
c++ -std=c++17 -O1 -Wall -Wextra $INC -o $B/programs_test shell-mpc/tests/programs_test.cpp $B/core/libsynthyboy_core.a $B/core/shapes/libsynthyboy_shapes.a > $B/programs-build.log 2>&1 || { grep -E "error" -A4 $B/programs-build.log | head -30; echo "ACCEPT FAIL: programs_test build (shell-mpc/program_list.h)"; exit 1; }
$B/programs_test content/presets > $B/programs.log 2>&1 || { grep -E "^FAIL" $B/programs.log; echo "ACCEPT FAIL: programs_test"; exit 1; }
PK="$B/pkg/Safian - VST - SynthyBoy"
rm -rf $B/pkg; mkdir -p "$PK/VST" "$PK/Content"; cp -R content/presets "$PK/Content/presets"; cp "$LIB" "$PK/VST/"; echo '{}' > "$PK/plugin.json"
PLIB="$PK/VST/$(basename "$LIB")"
c++ -std=c++17 -O1 -Wall -Wextra $INC -o $B/programs_host shell-mpc/tests/programs_host.cpp $B/core/libsynthyboy_core.a $B/core/shapes/libsynthyboy_shapes.a || { echo "ACCEPT FAIL: programs_host build"; exit 1; }
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
python3 - $B/dump-pkg.json content/presets <<'PY' || { echo "ACCEPT FAIL: program list in the host_sim dump"; exit 1; }
import json, sys, pathlib
d = json.load(open(sys.argv[1])); pr = d.get("programs", [])
# the dump lists every factory preset of content/presets plus the built-in Init program: the count follows the folder, not a constant (it was 161 once and went stale)
expected = len(list(pathlib.Path(sys.argv[2]).rglob("*.sbpreset"))) + 1
ok = len(pr) == expected and pr[0] == "Init" and "808 Boom" in pr
if not ok: print("programs in the dump:", len(pr), "expected", expected, pr[:5])
sys.exit(0 if ok else 1)
PY
$HSR "$PLIB" $ENGINE_N $B/test-pkg.log test --seconds 2 --notes 48,55 || { grep -E "^(FAIL|WARN)|Result" $B/test-pkg.log; echo "ACCEPT FAIL: host_sim test of the packaged library"; exit 1; }
grep -qE "produced silence" $B/test-pkg.log && { echo "ACCEPT FAIL: host_sim test of the packaged library"; exit 1; }
grep -q 'Content/presets' scripts/build-mpc.sh || { echo "ACCEPT FAIL: scripts/build-mpc.sh does not copy content/presets to Content/presets"; exit 1; }
# stub host: state round trip with content, re-prepare on rate change, two-thread parameter writes (plain build, then ThreadSanitizer)
INC="-I core/include -I core/filters-serum/include -I core/shapes/include -I $MPC_SDK/framework"
c++ -std=c++17 -O1 $INC -o $B/stub_host shell-mpc/tests/stub_host.cpp $B/core/libsynthyboy_core.a $B/core/shapes/libsynthyboy_shapes.a || { echo "ACCEPT FAIL: stub_host build"; exit 1; }
$B/stub_host "$LIB" > $B/stub.log 2>&1 || { tail -15 $B/stub.log; echo "ACCEPT FAIL: stub_host"; exit 1; }
T=${B}-tsan
cmake -S . -B $T -G Ninja $BASE_OPTS -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSYNTHYBOY_BUILD_TESTS=OFF -DSYNTHYBOY_BUILD_MPC_SHELL=ON -DMPC_SDK_DIR="$MPC_SDK" "-DCMAKE_CXX_FLAGS=-fsanitize=thread -g" "-DCMAKE_C_FLAGS=-fsanitize=thread -g" "-DCMAKE_MODULE_LINKER_FLAGS=-fsanitize=thread" > $T.configure.log 2>&1 && cmake --build $T --target synthyboy_mpc > $T.build.log 2>&1 || { tail -30 $T.build.log; echo "ACCEPT FAIL: ThreadSanitizer build"; exit 1; }
TLIB=$(find $T -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)
c++ -std=c++17 -O1 -g -fsanitize=thread $INC -o $T/stub_host shell-mpc/tests/stub_host.cpp $T/core/libsynthyboy_core.a $T/core/shapes/libsynthyboy_shapes.a || { echo "ACCEPT FAIL: tsan stub_host build"; exit 1; }
run_tsan() {
    tsan_log=$1
    shift
    TSAN_OPTIONS="halt_on_error=0 exitcode=66" "$@" > "$tsan_log" 2>&1 || {
        tsan_rc=$?
        tail -40 "$tsan_log" >&2
        echo "ACCEPT FAIL: $1 exited $tsan_rc (see $tsan_log)" >&2
        exit 1
    }
}
run_tsan "$T/stub.log" $T/stub_host "$TLIB"
if grep -q "WARNING: ThreadSanitizer" $T/stub.log; then
    echo "ThreadSanitizer reports: $(grep -c 'WARNING: ThreadSanitizer' $T/stub.log); innermost frames:"
    grep -A14 "WARNING: ThreadSanitizer" $T/stub.log | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20
    echo "ACCEPT FAIL: data races (full report: $T/stub.log)"; exit 1
fi
grep -q "stub_host: OK" $T/stub.log || { tail -15 $T/stub.log; echo "ACCEPT FAIL: tsan stub_host"; exit 1; }
TPK="$T/pkg/Safian - VST - SynthyBoy"
rm -rf $T/pkg; mkdir -p "$TPK/VST" "$TPK/Content"; cp -R content/presets "$TPK/Content/presets"; cp "$TLIB" "$TPK/VST/"; echo '{}' > "$TPK/plugin.json"
c++ -std=c++17 -O1 -g -fsanitize=thread $INC -o $T/programs_host shell-mpc/tests/programs_host.cpp $T/core/libsynthyboy_core.a $T/core/shapes/libsynthyboy_shapes.a || { echo "ACCEPT FAIL: tsan programs_host build"; exit 1; }
T2="$T/pkg2/Safian - VST - SynthyBoy"; rm -rf $T/pkg2; mkdir -p "$T2/VST" "$T2/Content"; cp -R content/presets "$T2/Content/presets"; rm -rf "$T2/Content/presets/Bass"; cp "$TLIB" "$T2/VST/"; echo '{}' > "$T2/plugin.json"
run_tsan "$T/programs.log" $T/programs_host "$TPK/VST/$(basename "$TLIB")" content/presets "$T2/VST/$(basename "$TLIB")" "$T2/Content/presets"
if grep -q "WARNING: ThreadSanitizer" $T/programs.log; then
    echo "ThreadSanitizer reports in programs_host: $(grep -c 'WARNING: ThreadSanitizer' $T/programs.log); innermost frames:"
    grep -A14 "WARNING: ThreadSanitizer" $T/programs.log | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20
    echo "ACCEPT FAIL: data races while changing programs (full report: $T/programs.log)"; exit 1
fi
grep -q "programs_host: OK" $T/programs.log || { grep -E "^FAIL|^      " $T/programs.log | head; echo "ACCEPT FAIL: tsan programs_host"; exit 1; }
# The shipped configuration is the main build above (BASE_OPTS). The only extra build is the release one (DEV_LOG=OFF), for the release proof at the end.
LIVEINC="-I core/include -I core/filters-serum/include -I core/shapes/include -I shell-mpc -I $MPC_SDK/framework"
live_build() { # name, cmake options...
    n=$1; shift
    cmake -S . -B ${B}-live-$n -G Ninja $BASE_OPTS -DCMAKE_BUILD_TYPE=Release -DSYNTHYBOY_BUILD_TESTS=OFF -DSYNTHYBOY_BUILD_MPC_SHELL=ON -DMPC_SDK_DIR="$MPC_SDK" "$@" > ${B}-live-$n.configure.log 2>&1 && cmake --build ${B}-live-$n --target synthyboy_mpc > ${B}-live-$n.build.log 2>&1 || { tail -30 ${B}-live-$n.build.log; echo "ACCEPT FAIL: build of the LFO display variant $n"; exit 1; }
}
# nolog: the release mode (SYNTHYBOY_MPC_DEV_LOG=OFF, what build-mpc.sh --release passes): it must compile, and its binary holds no log strings (docs/ui-components/LOGGING.md, "Release proof")
live_build nolog -DSYNTHYBOY_MPC_DEV_LOG=OFF
grep -q "synthyboy-overlay-status" "$LIB" || { echo "ACCEPT FAIL: the build has no overlay code"; exit 1; }
OVCORE=$(find $B/core -name '*.a' | tr '\n' ' ')
OVSRC=shell-mpc/overlay/tests
c++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -I core/include -I core/filters-serum/include -I core/shapes/include -I shell-mpc/overlay -o $B/geometry_render_host $OVSRC/geometry_render_host.cpp || { echo "ACCEPT FAIL: geometry_render_host build"; exit 1; }
$B/geometry_render_host > $B/geometry-render.log 2>&1 || { grep -E "^FAIL|ERROR" $B/geometry-render.log | head; echo "ACCEPT FAIL: geometry_render_host (rotation mapping, renderer)"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I core/filters-serum/include -I core/shapes/include -I shell-mpc/overlay -o $B/curve_host $OVSRC/curve_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: curve_host build"; exit 1; }
$B/curve_host > $B/curve-host.log 2>&1 || { grep -E "^FAIL" $B/curve-host.log; echo "ACCEPT FAIL: curve_host (curve source)"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I core/filters-serum/include -I core/shapes/include -I shell-mpc/overlay -o $B/qlink_panel_host $OVSRC/qlink_panel_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: qlink_panel_host build"; exit 1; }
$B/qlink_panel_host > $B/qlink-panel-host.log 2>&1 || { grep -E "^FAIL|^      " $B/qlink-panel-host.log; echo "ACCEPT FAIL: qlink_panel_host (the host's Q-Link pop-up replayed against the key)"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I core/filters-serum/include -I core/shapes/include -I shell-mpc/overlay -o $B/views_host $OVSRC/views_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: views_host build"; exit 1; }
$B/views_host > $B/views-host.log 2>&1 || { grep -E "^FAIL" $B/views-host.log; echo "ACCEPT FAIL: views_host (the views: LFO, oscillator, envelope, filter 2, several views in the controller)"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I core/filters-serum/include -I core/shapes/include -I shell-mpc/overlay -o $B/gpu_backend_host $OVSRC/gpu_backend_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: gpu_backend_host build"; exit 1; }
$B/gpu_backend_host > $B/gpu-backend-host.log 2>&1 || { grep -E "^FAIL" $B/gpu-backend-host.log; echo "ACCEPT FAIL: gpu_backend_host (GPU helper health state machine and fallback with a fake helper)"; exit 1; }
# run-time discovery (discovery.h: the plane tables of BOTH devices, a busy plane, no free plane, the cores), the rotation found from the markers, the pace grid with a fake vblank clock, a plane another program takes
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I core/filters-serum/include -I core/shapes/include -I shell-mpc/overlay -o $B/display_host $OVSRC/display_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: display_host build"; exit 1; }
$B/display_host > $B/display-host.log 2>&1 || { grep -E "^FAIL" $B/display-host.log; echo "ACCEPT FAIL: display_host (discovery of the display and the cores, rotation search, pace grid, lost plane)"; exit 1; }
# the spectrum curve (shell-mpc/overlay/spectrum_curve.h: FFT of the overlay thread from the ring, 256 points, attack / release) and the spectrum view with a fake instance, the picture of both renderers, the controller
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I shell-mpc/overlay -o $B/spectrum_curve_host $OVSRC/spectrum_curve_host.cpp -lpthread || { echo "ACCEPT FAIL: spectrum_curve_host build"; exit 1; }
$B/spectrum_curve_host > $B/spectrum-curve-host.log 2>&1 || { grep -E "^FAIL" $B/spectrum-curve-host.log; echo "ACCEPT FAIL: spectrum_curve_host (the spectrum curve: sine, two sines, noise, silence, timing)"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I core/filters-serum/include -I core/shapes/include -I shell-mpc/overlay -o $B/spectrum_view_host $OVSRC/spectrum_view_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: spectrum_view_host build"; exit 1; }
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
# the cached cells of the scene protocol (gpu/scene.h CellBegin, gpu/cell_plan.h): the hash, the plan of the layers, the damage between two pictures, the part to paint for the age of the back buffer, the CPU reference
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I shell-mpc/overlay -I shell-mpc -o $B/cell_plan_host $OVSRC/cell_plan_host.cpp || { echo "ACCEPT FAIL: cell_plan_host build"; exit 1; }
$B/cell_plan_host > $B/cell-plan-host.log 2>&1 || { grep -E "^FAIL" $B/cell-plan-host.log; echo "ACCEPT FAIL: cell_plan_host (cached cells: hash, plan, damage, paint boxes)"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I shell-mpc/overlay -o $B/cell_cache_host $OVSRC/cell_cache_host.cpp || { echo "ACCEPT FAIL: cell_cache_host build"; exit 1; }
$B/cell_cache_host > $B/cell-cache-host.log 2>&1 || { grep -E "^FAIL" $B/cell-cache-host.log; echo "ACCEPT FAIL: cell_cache_host (the layer cache and the partial picture: every buffer age)"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I shell-mpc/overlay -o $B/gpu_clock_host $OVSRC/gpu_clock_host.cpp || { echo "ACCEPT FAIL: gpu_clock_host build"; exit 1; }
$B/gpu_clock_host > $B/gpu-clock-host.log 2>&1 || { grep -E "^FAIL" $B/gpu-clock-host.log; echo "ACCEPT FAIL: gpu_clock_host (the GPU clock hint)"; exit 1; }
# the controller, the views and the GPU backend once more under AddressSanitizer + UBSan (instances are freed while their page is on screen: no use after free)
A=${B}-asan
cmake -S . -B "$A" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSYNTHYBOY_BUILD_TESTS=OFF "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined" "-DCMAKE_C_FLAGS=-fsanitize=address,undefined" > "$A.configure.log" 2>&1 &&
    cmake --build "$A" --target synthyboy_core > "$A.build.log" 2>&1 || { echo "ACCEPT FAIL: sanitized core build (see $A.configure.log and $A.build.log)"; exit 1; }
ASAN_CORE=$(find "$A/core" -name '*.a' | tr '\n' ' ')
for t in qlink_panel_host views_host gpu_backend_host spectrum_view_host display_host cell_plan_host cell_cache_host controller_review_host; do
    c++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -I core/include -I core/filters-serum/include -I core/shapes/include -I shell-mpc/overlay -I shell-mpc/ui -o $B/${t}_asan $OVSRC/$t.cpp $ASAN_CORE -lpthread || { echo "ACCEPT FAIL: $t asan build"; exit 1; }
    $B/${t}_asan > $B/$t-asan.log 2>&1 || { grep -E "^FAIL|ERROR|runtime error" $B/$t-asan.log | head; echo "ACCEPT FAIL: $t under AddressSanitizer / UBSan"; exit 1; }
    grep -qE "ERROR: AddressSanitizer|runtime error" $B/$t-asan.log && { grep -E "ERROR: AddressSanitizer|runtime error" $B/$t-asan.log | head -5; echo "ACCEPT FAIL: $t under AddressSanitizer / UBSan"; exit 1; }
done
# the four host tests of the pointer / pop-up work that had no sanitizer run (review of 2026-11-16): the setting file, the plane order, the key of the pop-up, the detector on the captured pictures
for t in qlink_setting_host stacking_host panel_key_host; do
    c++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -I shell-mpc/overlay -o $B/${t}_asan $OVSRC/$t.cpp || { echo "ACCEPT FAIL: $t asan build"; exit 1; }
    $B/${t}_asan > $B/$t-asan.log 2>&1 || { grep -E "^FAIL|ERROR|runtime error" $B/$t-asan.log | head; echo "ACCEPT FAIL: $t under AddressSanitizer / UBSan"; exit 1; }
done
c++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -I shell-mpc/overlay -DQLINK_FIXTURE=\"shell-mpc/overlay/tests/fixtures/qlink_panel_fixtures.bin\" -o $B/page_detector_host_asan $OVSRC/page_detector_host.cpp || { echo "ACCEPT FAIL: page_detector_host asan build"; exit 1; }
$B/page_detector_host_asan > $B/page-detector-asan.log 2>&1 || { grep -E "^FAIL|ERROR|runtime error" $B/page-detector-asan.log | head; echo "ACCEPT FAIL: page_detector_host under AddressSanitizer / UBSan"; exit 1; }
# Exercise the Q slots with the same sanitized core as the other overlay hosts.
# the plugin-drawn layer (shell-mpc/ui/, header only): its tests under AddressSanitizer + UBSan; the tests write PNGs
# into $B/ui-images (for a human; not compared)
mkdir -p $B/ui-images
for t in ui_time_ruler_host; do   # ui_cov_findings_host is NOT here on purpose: it demonstrates findings (its own header says it is not an acceptance test)
    c++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -I core/include -I core/filters-serum/include -I core/shapes/include -o $B/${t}_asan shell-mpc/ui/tests/$t.cpp || { echo "ACCEPT FAIL: $t build"; exit 1; }
    UI_OUT=$B/ui-images $B/${t}_asan > $B/$t.log 2>&1 || { grep -E "^FAIL|ERROR|runtime error" $B/$t.log | head; echo "ACCEPT FAIL: $t (UI components: interface, drawing, Q groups)"; exit 1; }
    grep -qE "ERROR: AddressSanitizer|runtime error" $B/$t.log && { grep -E "ERROR: AddressSanitizer|runtime error" $B/$t.log | head -5; echo "ACCEPT FAIL: $t under AddressSanitizer / UBSan"; exit 1; }
done
sh shell-mpc/tests/run_event_inbox_test.sh > $B/event-inbox.log 2>&1 || { grep -E "^FAIL|error" $B/event-inbox.log | head; echo "ACCEPT FAIL: event inbox test"; exit 1; }
sh shell-mpc/tests/run_output_stage_test.sh > $B/output-stage.log 2>&1 || { grep -E "^FAIL|error" $B/output-stage.log | head; echo "ACCEPT FAIL: output stage test"; exit 1; }
# Build the native Play, Mixer and FX host checks together.
cmake --build "$B" --target mixer_binding_host > "$B/native-ui-build.log" 2>&1 || { tail -40 "$B/native-ui-build.log"; exit 1; }
for t in mixer_binding_host; do
    ctest --test-dir "$B" -R "^${t}$" --output-on-failure --no-tests=error || exit 1
done
# the Play page's parameter table is built when the library is loaded, not by the first caller (the host may call getParameter / setParameter from the audio thread)
c++ -std=c++17 -O1 -Wall -Wextra -Werror -I core/include -I core/filters-serum/include -I core/shapes/include -I shell-mpc -o $B/play_params_rt_host shell-mpc/tests/play_params_rt_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: play_params_rt_host build"; exit 1; }
$B/play_params_rt_host > $B/play-params-rt.log 2>&1 || { grep -E "^FAIL" $B/play-params-rt.log; echo "ACCEPT FAIL: play_params_rt_host (the table of the Play proxies must exist before the first lookup, no allocation)"; exit 1; }
# saving and reloading a project with the Play page (docs/MPC.md "Project save and reload"): the load orders a host could use (chunk only, write-back of every parameter before / after the chunk, program re-selection, the order seen on the Key 61), the restore window, the debug log
c++ -std=c++17 -O1 -Wall -Wextra -Werror -I core/include -I core/filters-serum/include -I core/shapes/include -I shell-mpc -I $MPC_SDK/framework -o $B/play_restore_host shell-mpc/tests/play_restore_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: play_restore_host build"; exit 1; }
$B/play_restore_host "$LIB" > $B/play-restore.log 2>&1 || { grep -E "^FAIL" $B/play-restore.log | cut -c1-400; echo "ACCEPT FAIL: play_restore_host (project save and reload of the Play page)"; exit 1; }
# instances created and closed on three threads while the display thread reports (docs/MPC.md "Closing an instance"): the host is never called for an instance whose effClose returned
c++ -std=c++17 -O1 -Wall -Wextra -Werror -I core/include -I core/filters-serum/include -I core/shapes/include -I shell-mpc -I $MPC_SDK/framework -o $B/close_stress_host shell-mpc/tests/close_stress_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: close_stress_host build"; exit 1; }
$B/close_stress_host "$LIB" 120 > $B/close-stress.log 2>&1 || { grep -E "^FAIL" $B/close-stress.log; echo "ACCEPT FAIL: close_stress_host (host called after effClose)"; exit 1; }
grep -q "Overlay Instance" "$LIB" || { echo "ACCEPT FAIL: the build has no Overlay Instance parameter"; exit 1; }
# live spectrum (SYNTHYBOY_MPC_SPECTRUM): the band mapping and the analyser on known signals
c++ -std=c++17 -O1 -Wall -Wextra -Werror -I shell-mpc -o $B/spectrum_bands_host shell-mpc/tests/spectrum_bands_host.cpp || { echo "ACCEPT FAIL: spectrum_bands_host build"; exit 1; }
$B/spectrum_bands_host > $B/spectrum-bands.log 2>&1 || { grep -E "^FAIL" $B/spectrum-bands.log; echo "ACCEPT FAIL: spectrum_bands_host"; exit 1; }
c++ -std=c++17 -O1 -Wall -Wextra -Werror -I shell-mpc -o $B/spectrum_host shell-mpc/tests/spectrum_host.cpp -lpthread || { echo "ACCEPT FAIL: spectrum_host build"; exit 1; }
$B/spectrum_host > $B/spectrum-host.log 2>&1 || { grep -E "^FAIL" $B/spectrum-host.log; echo "ACCEPT FAIL: spectrum_host"; exit 1; }
grep -q "Spectrum %d" "$LIB" || { echo "ACCEPT FAIL: the build has no spectrum code"; exit 1; }
# pad pressure to Filter 1 cutoff (SYNTHYBOY_MPC_PAD_PRESSURE): pad_pressure.h and the plugin in the own engine (the pressure opens the filter)
c++ -std=c++17 -O1 -Wall -Wextra $LIVEINC -o $B/pad_pressure_host shell-mpc/tests/pad_pressure_host.cpp $B/core/libsynthyboy_core.a $B/core/shapes/libsynthyboy_shapes.a -lpthread || { echo "ACCEPT FAIL: pad_pressure_host build"; exit 1; }
$B/pad_pressure_host "$LIB" own overlay > $B/pad-pressure-own.log 2>&1 || { grep -E "^FAIL" $B/pad-pressure-own.log; echo "ACCEPT FAIL: pad_pressure_host (own engine, Overlay Instance after the pad pressure parameters)"; exit 1; }
grep -E "^INFO" $B/pad-pressure-own.log
# (the names are asked of the built plugin: the compiler may fold the literal into the copy, so no string of the binary says it)
$B/host_sim "$LIB" dump 2>/dev/null | grep -q "Pad Pressure Amount" || { echo "ACCEPT FAIL: the build has no pad pressure code"; exit 1; }
TL=${B}-live-tsan
cmake -S . -B $TL -G Ninja $BASE_OPTS -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSYNTHYBOY_BUILD_TESTS=OFF -DSYNTHYBOY_BUILD_MPC_SHELL=ON -DMPC_SDK_DIR="$MPC_SDK" "-DCMAKE_CXX_FLAGS=-fsanitize=thread -g" "-DCMAKE_C_FLAGS=-fsanitize=thread -g" "-DCMAKE_MODULE_LINKER_FLAGS=-fsanitize=thread" > $TL.configure.log 2>&1 && cmake --build $TL --target synthyboy_mpc > $TL.build.log 2>&1 || { tail -30 $TL.build.log; echo "ACCEPT FAIL: ThreadSanitizer build of the LFO display"; exit 1; }
TLLIB=$(find $TL -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)
# the spectrum ring (audio thread writes, display thread reads) and the plugin with the spectrum under ThreadSanitizer
c++ -std=c++17 -O1 -g -fsanitize=thread -I shell-mpc -o $TL/spectrum_host shell-mpc/tests/spectrum_host.cpp -lpthread || { echo "ACCEPT FAIL: tsan spectrum_host build"; exit 1; }
run_tsan "$TL/spectrum-host.log" $TL/spectrum_host
for f in $TL/spectrum-host.log; do
    if grep -q "WARNING: ThreadSanitizer" $f; then
        grep -A14 "WARNING: ThreadSanitizer" $f | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20
        echo "ACCEPT FAIL: data races in the spectrum (full report: $f)"; exit 1
    fi
    grep -q "^FAIL" $f && { grep "^FAIL" $f; echo "ACCEPT FAIL: tsan spectrum test ($f)"; exit 1; }
done
# the spectrum curve with the audio thread pushing into the ring while the overlay thread analyses (ThreadSanitizer)
c++ -std=c++17 -O1 -g -fsanitize=thread -I shell-mpc/overlay -o $TL/spectrum_curve_host shell-mpc/overlay/tests/spectrum_curve_host.cpp -lpthread || { echo "ACCEPT FAIL: tsan spectrum_curve_host build"; exit 1; }
run_tsan "$TL/spectrum-curve.log" $TL/spectrum_curve_host
if grep -q "WARNING: ThreadSanitizer" $TL/spectrum-curve.log; then
    grep -A14 "WARNING: ThreadSanitizer" $TL/spectrum-curve.log | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20
    echo "ACCEPT FAIL: data races in the spectrum curve (full report: $TL/spectrum-curve.log)"; exit 1
fi
grep -q "^FAIL" $TL/spectrum-curve.log && { grep "^FAIL" $TL/spectrum-curve.log; echo "ACCEPT FAIL: tsan spectrum_curve_host"; exit 1; }
# pad pressure under ThreadSanitizer (the settings are written by the host thread and read by the audio thread through the engine's atomic)
c++ -std=c++17 -O1 -g -fsanitize=thread $LIVEINC -o $TL/pad_pressure_host shell-mpc/tests/pad_pressure_host.cpp $TL/core/libsynthyboy_core.a $TL/core/shapes/libsynthyboy_shapes.a -lpthread || { echo "ACCEPT FAIL: tsan pad_pressure_host build"; exit 1; }
run_tsan "$TL/pad-pressure.log" $TL/pad_pressure_host "$TLLIB" own overlay
if grep -q "WARNING: ThreadSanitizer" $TL/pad-pressure.log; then
    grep -A14 "WARNING: ThreadSanitizer" $TL/pad-pressure.log | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20
    echo "ACCEPT FAIL: data races in the pad pressure (full report: $TL/pad-pressure.log)"; exit 1
fi
grep -q "^FAIL" $TL/pad-pressure.log && { grep "^FAIL" $TL/pad-pressure.log; echo "ACCEPT FAIL: tsan pad_pressure_host"; exit 1; }
# the overlay thread (the tsan build above has the module in: it runs next to the display thread) and the controller on a real thread against changing parameters, markers and instances
TSCORE=$(find $TL/core -name '*.a' | tr '\n' ' ')
for t in curve_host gpu_backend_host; do
    c++ -std=c++17 -O1 -g -fsanitize=thread -I core/include -I core/filters-serum/include -I core/shapes/include -I shell-mpc/overlay -o $TL/$t shell-mpc/overlay/tests/$t.cpp $TSCORE -lpthread || { echo "ACCEPT FAIL: tsan $t build"; exit 1; }
    run_tsan "$TL/$t.log" $TL/$t
    if grep -q "WARNING: ThreadSanitizer" $TL/$t.log; then
        grep -A14 "WARNING: ThreadSanitizer" $TL/$t.log | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20
        echo "ACCEPT FAIL: data races in the overlay ($t; full report: $TL/$t.log)"; exit 1
    fi
    grep -q "^FAIL" $TL/$t.log && { grep "^FAIL" $TL/$t.log; echo "ACCEPT FAIL: tsan $t"; exit 1; }
done
# the same stress test under ThreadSanitizer (the display thread, the overlay thread and the host's threads) and under AddressSanitizer + UndefinedBehaviorSanitizer (a member used after it was destroyed, a leaked or doubly freed instance)
c++ -std=c++17 -O1 -g -fsanitize=thread -I core/include -I core/filters-serum/include -I core/shapes/include -I shell-mpc -I $MPC_SDK/framework -o $TL/close_stress_host shell-mpc/tests/close_stress_host.cpp $TSCORE -lpthread || { echo "ACCEPT FAIL: tsan close_stress_host build"; exit 1; }
run_tsan "$TL/close-stress.log" $TL/close_stress_host "$TLLIB" 40
grep -q "WARNING: ThreadSanitizer" $TL/close-stress.log && { grep -A14 "WARNING: ThreadSanitizer" $TL/close-stress.log | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20; echo "ACCEPT FAIL: data races when instances are closed (full report: $TL/close-stress.log)"; exit 1; }
grep -q "^FAIL" $TL/close-stress.log && { grep "^FAIL" $TL/close-stress.log; echo "ACCEPT FAIL: tsan close_stress_host"; exit 1; }
grep -q "^OK" $TL/close-stress.log || { tail -5 $TL/close-stress.log; echo "ACCEPT FAIL: tsan close_stress_host did not finish"; exit 1; }
AL=${B}-live-asan
cmake -S . -B $AL -G Ninja $BASE_OPTS -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSYNTHYBOY_BUILD_TESTS=OFF -DSYNTHYBOY_BUILD_MPC_SHELL=ON -DMPC_SDK_DIR="$MPC_SDK" "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -g" "-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -g" "-DCMAKE_MODULE_LINKER_FLAGS=-fsanitize=address,undefined" > $AL.configure.log 2>&1 && cmake --build $AL --target synthyboy_mpc > $AL.build.log 2>&1 || { tail -30 $AL.build.log; echo "ACCEPT FAIL: AddressSanitizer build of the plugin"; exit 1; }
ALIB="$(find $AL -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)"
ASCORE=$(find $AL/core -name '*.a' | tr '\n' ' ')
c++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -I core/include -I core/filters-serum/include -I core/shapes/include -I shell-mpc -I $MPC_SDK/framework -o $AL/close_stress_host shell-mpc/tests/close_stress_host.cpp $ASCORE -lpthread || { echo "ACCEPT FAIL: asan close_stress_host build"; exit 1; }
ASAN_OPTIONS="detect_leaks=0:halt_on_error=1" UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" $AL/close_stress_host "$ALIB" 40 > $AL/close-stress.log 2>&1 || { tail -30 $AL/close-stress.log; echo "ACCEPT FAIL: asan close_stress_host (memory error when instances are closed)"; exit 1; }
grep -q "^OK" $AL/close-stress.log || { tail -5 $AL/close-stress.log; echo "ACCEPT FAIL: asan close_stress_host did not finish"; exit 1; }
# the Q proxies' router (shell-mpc/q_router.h): the logic alone (group, reads, writes, restore window, reports, no allocation), plainly and under ThreadSanitizer (audio, UI and display threads at once)
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I shell-mpc -o $B/q_router_host shell-mpc/tests/q_router_host.cpp -lpthread || { echo "ACCEPT FAIL: q_router_host build"; exit 1; }
$B/q_router_host > $B/q-router.log 2>&1 || { grep -E "^FAIL" $B/q-router.log; echo "ACCEPT FAIL: q_router_host (the Q proxies' router)"; exit 1; }
c++ -std=c++17 -O1 -g -fsanitize=thread -I shell-mpc -o $TL/q_router_host shell-mpc/tests/q_router_host.cpp -lpthread || { echo "ACCEPT FAIL: tsan q_router_host build"; exit 1; }
run_tsan "$TL/q-router.log" $TL/q_router_host
grep -q "WARNING: ThreadSanitizer" $TL/q-router.log && { grep -A14 "WARNING: ThreadSanitizer" $TL/q-router.log | grep -E "#[0-1] " | sed 's/ (.*//' | sort | uniq -c | sort -rn | head -20; echo "ACCEPT FAIL: data races in the Q router (full report: $TL/q-router.log)"; exit 1; }
grep -q "^FAIL" $TL/q-router.log && { grep "^FAIL" $TL/q-router.log; echo "ACCEPT FAIL: tsan q_router_host"; exit 1; }
# which parameters the Q proxies drive relatively (q_step_rule.h): the stepped ones and the integer steppers of up to 32 values; the quantised amounts that stay absolute are pinned
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I core/filters-serum/include -I core/shapes/include -I shell-mpc -o $B/q_step_rule_host shell-mpc/tests/q_step_rule_host.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: q_step_rule_host build"; exit 1; }
$B/q_step_rule_host > $B/q-step-rule.log 2>&1 || { grep -E "^FAIL" $B/q-step-rule.log; echo "ACCEPT FAIL: q_step_rule_host (which parameters step relatively)"; exit 1; }
# the robustness review of the router (docs/MPC.md "Stepped targets"): a group change must not leave the old step baseline (R1), and a write must not mix the index of one group with the view of another (R2); -O2, AddressSanitizer + UBSan, ThreadSanitizer
c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -I shell-mpc -o $B/q_router_review_host shell-mpc/tests/q_router_review_host.cpp -lpthread || { echo "ACCEPT FAIL: q_router_review_host build"; exit 1; }
$B/q_router_review_host > $B/q-router-review.log 2>&1 || { grep -E "^FAIL" $B/q-router-review.log; echo "ACCEPT FAIL: q_router_review_host (O2)"; exit 1; }
c++ -std=c++17 -O1 -g -fsanitize=address,undefined -I shell-mpc -o $B/q_router_review_asan shell-mpc/tests/q_router_review_host.cpp -lpthread || { echo "ACCEPT FAIL: q_router_review_host asan build"; exit 1; }
$B/q_router_review_asan > $B/q-router-review-asan.log 2>&1 || { grep -E "^FAIL|ERROR|runtime error" $B/q-router-review-asan.log | head; echo "ACCEPT FAIL: q_router_review_host under AddressSanitizer / UBSan"; exit 1; }
c++ -std=c++17 -O1 -g -fsanitize=thread -I shell-mpc -o $TL/q_router_review_host shell-mpc/tests/q_router_review_host.cpp -lpthread || { echo "ACCEPT FAIL: tsan q_router_review_host build"; exit 1; }
run_tsan "$TL/q-router-review.log" $TL/q_router_review_host
grep -qE "^FAIL|WARNING: ThreadSanitizer" $TL/q-router-review.log && { grep -E "^FAIL|WARNING" $TL/q-router-review.log | head; echo "ACCEPT FAIL: tsan q_router_review_host"; exit 1; }
# the layout lint of every plugin-drawn page (Init and every factory preset) against its baseline, and its own self test
# the robustness review (docs: .hw-local/mpc-ui-plans/robustness-review.md): the logic alone, plainly and under AddressSanitizer + UBSan (and ThreadSanitizer where threads are the point)
RT=shell-mpc/tests
for t in safe_file_host poll_guard_host display_thread_host startup_timing_host log_host; do
    c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I shell-mpc -o $B/$t $RT/$t.cpp -lpthread || { echo "ACCEPT FAIL: $t build"; exit 1; }
    $B/$t > $B/$t.log 2>&1 || { grep -E "^FAIL" $B/$t.log; echo "ACCEPT FAIL: $t (robustness review)"; exit 1; }
    c++ -std=c++17 -O1 -g -fsanitize=address,undefined -I shell-mpc -o $B/${t}_asan $RT/$t.cpp -lpthread || { echo "ACCEPT FAIL: $t asan build"; exit 1; }
    $B/${t}_asan > $B/$t-asan.log 2>&1 || { grep -E "^FAIL|ERROR|runtime error" $B/$t-asan.log | head; echo "ACCEPT FAIL: $t under AddressSanitizer / UBSan"; exit 1; }
    c++ -std=c++17 -O1 -g -fsanitize=thread -I shell-mpc -o $TL/$t $RT/$t.cpp -lpthread || { echo "ACCEPT FAIL: tsan $t build"; exit 1; }
    run_tsan "$TL/$t.log" $TL/$t
    grep -qE "^FAIL|WARNING: ThreadSanitizer" $TL/$t.log && { grep -E "^FAIL|WARNING" $TL/$t.log | head; echo "ACCEPT FAIL: tsan $t"; exit 1; }
done
for t in host_strings_host chunk_review_host; do
    c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I core/filters-serum/include -I core/shapes/include -I shell-mpc -o $B/$t $RT/$t.cpp $OVCORE -lpthread || { echo "ACCEPT FAIL: $t build"; exit 1; }
    $B/$t > $B/$t.log 2>&1 || { grep -E "^FAIL" $B/$t.log; echo "ACCEPT FAIL: $t (robustness review)"; exit 1; }
done
# the plugin as the host loads it: a blocked host callback (effClose must not hang the host, no host call before the first dispatcher call)
c++ -std=c++17 -O1 -g -fsanitize=address,undefined -Wall -Wextra -I shell-mpc -I $MPC_SDK/framework -o $AL/close_hang_host $RT/close_hang_host.cpp -lpthread -ldl || { echo "ACCEPT FAIL: close_hang_host build"; exit 1; }
for v in "$ALIB" "$LIB"; do
    ASAN_OPTIONS="detect_leaks=0:halt_on_error=1" UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" $AL/close_hang_host "$v" > $AL/close-hang.log 2>&1 || { grep -E "^FAIL|ERROR" $AL/close-hang.log | head; echo "ACCEPT FAIL: close_hang_host ($v)"; exit 1; }
done
# the quality profile per device (shell-mpc/device_profile.h, docs/MPC.md "Quality profile per device"): the texts, the device table, the overrides; plainly and under AddressSanitizer + UBSan
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I core/filters-serum/include -I core/shapes/include -o $B/device_profile_host shell-mpc/tests/device_profile_host.cpp || { echo "ACCEPT FAIL: device_profile_host build"; exit 1; }
$B/device_profile_host > $B/device-profile-host.log 2>&1 || { grep -E "^FAIL" $B/device-profile-host.log; echo "ACCEPT FAIL: device_profile_host (quality profile per device: texts, table, overrides)"; exit 1; }
c++ -std=c++17 -O1 -g -fsanitize=address,undefined -I core/include -I core/filters-serum/include -I core/shapes/include -o $B/device_profile_host_asan shell-mpc/tests/device_profile_host.cpp || { echo "ACCEPT FAIL: device_profile_host asan build"; exit 1; }
$B/device_profile_host_asan > $B/device-profile-host-asan.log 2>&1 || { grep -E "^FAIL|ERROR|runtime error" $B/device-profile-host-asan.log | head; echo "ACCEPT FAIL: device_profile_host under AddressSanitizer / UBSan"; exit 1; }
# the editors of the FX pages (ui/fx_slot_editor.h, fx_layout.h: the tables of the 16 effects, the splitters), plainly and under AddressSanitizer + UBSan
# the data of the FX tables is consistent (ui/fx_layout.h against the engine's parameter table), plainly and under AddressSanitizer + UBSan
# the tool of the Sounds-browser files loads a program or a chunk into the built plugin (the dumper of the Sounds-browser set): it must compile and the program of the first factory preset must load
c++ -std=c++17 -O1 -Wall -Wextra -Werror -I core/include -I core/filters-serum/include -I core/shapes/include -I $MPC_SDK/framework -I shell-mpc -o $B/chunk_load_host shell-mpc/tests/chunk_load_host.cpp -ldl || { echo "ACCEPT FAIL: chunk_load_host build"; exit 1; }
# the headers of the oscillator pages stand alone (osc_spec_page.h: the spec tables of the pages; osc_marks.h: the marks of the oscillator displays)
for h in osc_spec_page osc_marks; do
    echo "#include \"$h.h\"" > $B/hdr_$h.cpp
    c++ -std=c++17 -fsyntax-only -Wall -Wextra -Wpedantic -Werror -I core/include -I core/filters-serum/include -I core/shapes/include -I shell-mpc/overlay -I shell-mpc/ui $B/hdr_$h.cpp || { echo "ACCEPT FAIL: shell-mpc/overlay/$h.h does not compile on its own"; exit 1; }
done
# the documents of the release (docs/release/*: form, cited commits and lines, numbers) and of the MPC (docs/MPC.md: structure, measured numbers only under the device observations)
python3 -m unittest scripts/tests/test_docs_release.py scripts/tests/test_docs_mpc.py > $B/docs-test.log 2>&1 || { tail -25 $B/docs-test.log; echo "ACCEPT FAIL: the tests of docs/release and docs/MPC.md"; exit 1; }
# the release proof: no developer log strings in the DEV_LOG=OFF binary (and the same patterns are found in the default overlay build, so the check can see them)
NOLOG="$(find ${B}-live-nolog -name '*synthyboy_mpc*.dylib' -o -name '*synthyboy_mpc*.so' | head -1)"
LOGPAT="-dev\.log|-log\.conf|mpckit|engine created at|scan factory presets|VSTPluginMain returns"
strings -a "$NOLOG" | grep -E -e "$LOGPAT" | grep -v -E "^N[0-9]+mpckit[0-9]" > $B/nolog-strings.txt &&   # (the RTTI type names of the kit classes, N6mpckit9IInstanceE, are not log strings: the kit holds classes now, not only the log)
 { head -5 $B/nolog-strings.txt; echo "ACCEPT FAIL: log strings in the release (DEV_LOG=OFF) build"; exit 1; }
strings -a "$LIB" | grep -qE -e "$LOGPAT" || { echo "ACCEPT FAIL: the developer log is not in the default overlay build (the release proof would see nothing)"; exit 1; }
# the lock free hand-over of the events between the threads (event_inbox.h): plainly and under ThreadSanitizer
sh shell-mpc/tests/run_event_inbox_test.sh > $B/event-inbox.log 2>&1 || { tail -15 $B/event-inbox.log; echo "ACCEPT FAIL: event_inbox_host"; exit 1; }
# the touch decoder and the evdev reader (touch_decoder.h, touch_linux.h; fakes for the syscalls, a capture of the Key 61 as a fixture): plainly with -Werror and under AddressSanitizer + UBSan
bash shell-mpc/overlay/tests/run_touch_tests.sh > $B/touch.log 2>&1 || { grep -E "FAIL|error" $B/touch.log | head -20; echo "ACCEPT FAIL: touch_host"; exit 1; }
echo "ACCEPT OK"
