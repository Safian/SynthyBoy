# BITMAPGUI - the editor inside the native CLAP module, through a generic bitmap extension

Branch `bitmap-gui` (cut from develop 7e20682, rebased onto da9d553). Human spec of the extension: `docs/BITMAP-GUI-EXTENSION.md`; header `shell-clap/bitmap_gui_ext.h`; spec section 4.45 with
assumptions A473 .. A487 in `13-new-keys.md`; questions 108 .. 111 in `14-morning-questions.md`.

## What was built

| Part | Where |
|---|---|
| The extension: ids `com.safian.bitmap-gui/1` (plug-in) and `com.safian.bitmap-gui.host/1` (host, every function optional); life cycle, size, frames (BGRA8888 premultiplied little endian, dirty rectangles, frame id, needs-another hint), pointers (multi pointer), wheel, keys + text, focus, request_render / tick / resize / cursor / text input / file / share / url / remote page, async answers | `shell-clap/bitmap_gui_ext.h`, `docs/BITMAP-GUI-EXTENSION.md` |
| Instance state of the native shell in a header, edits of the GUI handled like host automation (UiLink: lock free mirror, one slot per parameter, gesture bits, MIDI ring, live displays) | `shell-clap/synthyboy_plugin.{h,cpp}`, `shell-clap/synthyboy_ui_link.h`, `shell-clap/synthyboy_clap.cpp` |
| The GUI: `BitmapPeer` (a `juce::ComponentPeer` that is a memory buffer), `BitmapSession` (editor, software image, dirty rectangles, pointer / finger / key translation, text input, remote page), `NativeUiHost` (the `UiHost` over one `SynthyBoyClap`), `PresetBrowser` (prev / next over the presets on disk), `JuceRuntime` (lazy toolkit, message thread, pump, embedded Roboto, virtual display), the extension vtable | `shell-clap/bitmap/` |
| Visibility: hidden symbols + version script (Linux) / exported symbols list (macOS) / `dllexport` (Windows); `scripts/check-clap-exports.sh` (ctest `synthyboy_clap_exports`, self test) | `shell-clap/bitmap/CMakeLists.txt`, `clap_exports.map`, `clap_exports_macos.txt` |
| Build: option `SYNTHYBOY_BITMAP_GUI` (ON with the plug-in, OFF otherwise; the ARMv7 script asks for it; never in `synthyboy_static`), JUCE fetched even without the plug-in, fonts embedded by a CMake script (no helper tool), static FreeType, `JUCE_USE_FONTCONFIG=0 JUCE_ALSA=0 JUCE_JACK=0` | `CMakeLists.txt`, `shell-clap/CMakeLists.txt`, `shell-clap/bitmap/CMakeLists.txt`, `embed_fonts.cmake` |
| Manifest: `plugin.json` section `gui` (+ schema, `check-manifest.py` compares it with the header and the sizes, `fill-manifest.py` keeps only the built builds), RELEASE.md, release.yml | `plugin.json`, `schema/`, `scripts/`, `docs/RELEASE.md`, `.github/workflows/release.yml` |
| Tests: a CLAP host without JUCE (`bitmap_gui_harness.h`, ~450 lines), 30 test cases, golden images, bench | `tests/bitmap-gui/` |
| CI: ui-sanitizers also builds and runs the bitmap tests, job `bitmap-gui-tsan`, job `bitmap-gui-armv7` (module + tests under qemu) | `.github/workflows/build.yml` |

Changes in the shared UI library (the plug-in's behaviour is unchanged, its tests are green): `UiHost.h` includes `juce_gui_basics` (it names `juce::Component`), new `UiHost::openUrl / canOpenUrl /
aboutUrl` (defaults: cannot; the About line of the Preferences becomes a link when the host can), `StereoMeter` repaints only when its bars would look different, `LiveBus::changed ()` and the four displays bound
to the bus repaint only when the engine's read-out brought something new (both ran at 30 Hz without anything new: an idle GUI produced 17 frames in 30).

## Measured and verified

Verified = run, with the result shown. Everything below was run on this Mac (Apple M-series, macOS arm64) or in Docker Desktop's Linux aarch64 (native speed) / ARMv7 (qemu-user) containers.

| Item | Result |
|---|---|
| macOS arm64: `synthyboy_test_bitmap_gui` (30 cases, 293 checks), `synthyboy_test_bitmap_golden` (5 cases), `synthyboy_clap_exports` | green. Two tests need a pop-up menu / call-out to stay open, which JUCE ties to the process being the active application: a console process on this machine cannot become it (no window server session), they print `SKIPPED` and pass (see not verified) |
| Linux aarch64 (Debian 12, GCC 12, **no X display**, libX11 installed): the same programs | 30 cases / 317 checks and 5 golden cases green (menus and call-outs included) |
| Pixels across platforms | macOS arm64 vs Linux aarch64: the first frames at 390 x 844, 1024 x 768 and 1024 x 600 differ by at most 1 step of one channel; the filter page (curve from floating point maths) has 0.06 % of its pixels more than 8 steps apart (max 86). Tolerance of the golden test: 9 steps of a channel on, 0.2 % of the pixels, mean 0.15 / 255 |
| Exports | macOS and Linux (aarch64 and ARMv7): exactly `clap_entry` and `synthyboy_clap_get_entry` (`nm`), self test of the script green |
| Linux aarch64 `readelf -d` | NEEDED: libstdc++, libm, libgcc_s, libc, the loader. No FreeType (static), fontconfig, ALSA, X11 |
| ARMv7 hard-float module (Debian 12 cross toolchain, `scripts/build-armv7-clap.sh --run-tests`) | ELF32 ARM EABI5, `Tag_CPU_arch v7, VFPv3, NEONv1`, hard-float; NEEDED: libstdc++.so.6, libm.so.6, libgcc_s.so.1, libc.so.6, ld-linux-armhf.so.3; exports only the two entries; size 13,870,068 bytes (10,855,340 stripped). The build fails if anything else is NEEDED |
| ARMv7 under qemu-user in a container with only the ARMv7 C / C++ runtime (no X11 / FreeType / ALSA, like the MPC's Buildroot) | see the table at the end of this report |
| ASan + UBSan | macOS arm64: 28 cases clean (leak detection is not available on macOS arm64). Linux aarch64 (GCC, LSan on): clean up to the point where it found a real null dereference (below), fixed |
| TSan | macOS arm64: 28 cases, no report |
| `cmake --build build -j8` (macOS, plug-in + bitmap GUI + all tests) | zero warnings |
| `ctest -j8` (macOS, full) | all green (94 tests before this work's late additions; the bitmap tests were run separately after) |
| `build-novital` (`SYNTHYBOY_BUILD_PLUGIN=OFF SYNTHYBOY_WITH_VITAL=OFF`) rebuilt, `ctest` 44 / 44, `scripts/check-no-vital.sh` on `libsynthyboy_core.a` and `libsynthyboy_static.a` clean (and its self test); `libsynthyboy_static.a` holds no JUCE / bitmap symbol | verified |
| `scripts/gen-factory-presets.py --check` | factory presets up to date (112 presets) |
| Message delivery without a display, inside `render ()` (Linux, no DISPLAY) | `bg_change_messages_and_async_calls_are_delivered_inside_render_without_a_display`: MOD, a source chip, a drag on a knob; the ChangeBroadcaster message of the assignment is delivered by the ticks and the knob repaints; also the `callAsync` of the value box (`bg_double_tap_opens_the_value_box...`) and the import worker (`bg_the_load_button_asks_the_host_for_a_file...`) |
| Coexistence with another JUCE in the process | macOS: the JUCE-wrapped `SynthyBoy.clap` (own JUCE copy, symbols visible) is loaded and running next to the module while its GUI is used (`bg_coexists_with_another_juce_in_the_same_process`), no runtime complaint |

## Findings on the way (fixed)

1. **Idle cost**: the meter and every `LiveBus` display repainted 30 times a second with nothing new (17 of 30 idle frames changed). Fixed in the library, tested by `bg_idle_gui_produces_no_pixels`.
2. **JUCE without a display** (the MPC): `PopupMenu` dereferences a null `Display` (found by UBSan in the Linux container: `juce_PopupMenu.cpp:909`); `Process::isForegroundProcess ()` is false without an X window, which closes every menu and call-out at once. The module gives JUCE a virtual 4096 x 4096 display and marks the process active on Linux (one data symbol of its own JUCE copy). On Windows a peer without a native handle counts as embedded in the foreground process. On macOS the host decides.
3. **JUCE has no touch input sources on macOS and Linux** (`MouseInputSourceList::canUseTouch` is false there): fingers are the primary button of the mouse source, two fingers become pinch / pan (A477, question 108).
4. A pop-up menu only takes an entry after it saw the pointer move: a press that arrives where the pointer was not is preceded by a move.
5. The `juce_audio_utils` / `juce_audio_devices` modules compile ALSA code that made the module need libasound: compiled out (`JUCE_ALSA=0`).
6. `.clap` Windows exports: `SYNTHYBOY_CLAP_API` was empty on MSVC (nothing exported); the module now defines `SYNTHYBOY_CLAP_EXPORT` there.
7. A header edit while a long build was running gave a stale vtable (a build artefact, not a bug): sanitizer runs were repeated on a consistent tree.

## Not verified (stated plainly)

* **Windows**: built by CI only; nothing of this was run on Windows (the tests use `dlopen`). MSVC rules were followed (no init-capture of `this` in nested lambdas, no GNU-only code in the sources; the test host is POSIX only and not built there).
* **macOS menus and call-outs**: the two tests that need the process to be the active application are skipped on this machine (no window server session); they ran on Linux. A GitHub macOS runner has a session; not run here.
* **Taking an entry of a pop-up menu** through the extension (the menu polls the pointer and button with a timer): the lists and menus open inside the frame and stay open (tested on Linux), the tap on an entry through the harness did not close the list in my attempts; the same list was used by the plug-in's own tests. Needs a look with a real host. Therefore "Import Vital file..." through the menu and the share flow of the "Last note as audio" sheet were not driven end to end; the file request path is verified through the oscillator's Load button (request, cancel, unknown id ignored, answer imports the sample into the library) and the share path is not tested.
* **TSan on Linux** and **ASan on the final tree on Linux**: the ASan run on Linux was done on the tree before the last test additions; TSan was run on macOS only. The CI jobs `ui-sanitizers` and `bitmap-gui-tsan` are written for them and not run here.
* **MPC speed**: not measured. The ARMv7 numbers are under qemu-user on an Apple CPU: correctness only.
* A segmentation fault of qemu-user inside the armhf libX11 when the container has the armhf X11 libraries but there is no X server (the build container): not reproduced on a native CPU (Linux aarch64 with libX11 and no server is green) and not with only the C / C++ runtime (the MPC case); the test stage of the script therefore runs in a second container without them.
* **iOS**: the static library is untouched and checked; whether the UI goes into the iOS binary next to Kick's JUCE 7 is not decided (question 110): it needs `JUCE_NAMESPACE`.
* The deviation from "link only gui_basics, graphics, events, core, audio_basics": the UI library itself needs `juce_audio_formats` and `juce_audio_utils` (`MidiKeyboardComponent`), so the module also compiles audio_processors / audio_devices / gui_extra of JUCE (question 109).

## Frame times (software renderer, Release)

macOS arm64 (this Mac), idle = `tick () + is_dirty ()`, knob drag = a drag on a knob (one render per move), wavetable animation = `osc1.frame` moved by automation every frame:

| Size | create | first full frame | idle tick + is_dirty | knob drag | wavetable animation |
|---|---:|---:|---:|---|---|
| 1024 x 600 | 277 ms (first one: toolkit start, font parse) | 18.4 ms | 1.3 us | (knob not on this layout) | 2.9 ms mean, 10.5 ms worst, 70,144 px^2 |
| 1024 x 768 | 63 ms | 21.8 ms | 1.3 us | 0.94 ms mean, 7.1 ms worst, 17,685 px^2 | 3.1 ms mean, 20.7 ms worst |
| 390 x 844 | 64 ms | 7.4 ms | 1.5 us | - | - |

ARMv7 under qemu-user (an emulated CPU, NOT an MPC): see the bench output at the end of this report.
