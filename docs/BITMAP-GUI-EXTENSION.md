# CLAP extension `com.safian.bitmap-gui/1` - a plug-in GUI that is a pixel buffer

Status: implemented in SynthyBoy's native CLAP module (2026-11-11) and, for statically linked hosts (iOS), in the static library `synthyboy_static_ui` (2026-11-12, section 14); header `shell-clap/bitmap_gui_ext.h`, report `ui-design/BITMAPGUI-REPORT.md`, design record `ui-design/13-new-keys.md` section 4.45.
This document is meant to be enough for a host developer to implement the host side without reading anything else. The header is the normative layout; this text is the normative behaviour.

## 1. What it is for

Some hosts cannot (or must not) run a plug-in's own GUI toolkit inside their process: the Akai MPC Key 61 (Linux ARMv7, no X11, a software framebuffer, a 1024 x 600 touch screen and eight Q-Link
encoders), a mobile app that already links another version of the same toolkit, a host that draws everything itself. With this extension the **plug-in draws its own GUI into memory** and the **host only**

1. calls `create ()` with a size and a scale,
2. calls `tick ()` regularly and `render ()` when something changed,
3. copies the **dirty rectangles** of the returned frame to its screen,
4. forwards touch / mouse / wheel / key input and the answers to the few requests the plug-in makes (files, text input, ...).

Nothing of the plug-in's toolkit crosses the interface: pixels and plain C structs. The plug-in keeps its toolkit statically linked inside the module with hidden symbols, so it cannot clash with the host's own copy
(SynthyBoy ships JUCE 8.0.17 inside the `.clap`; Kick links JUCE 7.0.12; the module exports `clap_entry` and `synthyboy_clap_get_entry` and nothing else, `scripts/check-clap-exports.sh`.
Where the plug-in is not a module but a static library in the host's binary, iOS, hidden symbols are not enough and the toolkit lives in a namespace of its own: section 14).
The extension is generic: nothing in the header knows SynthyBoy, any plug-in can offer it.

Two ids, both versioned. An incompatible change is a new id (`/2`); a plug-in may answer both.

| Side | Id | Obtained with |
|---|---|---|
| plug-in | `com.safian.bitmap-gui/1` (`SAFIAN_EXT_BITMAP_GUI`) | `clap_plugin_t::get_extension ()` -> `const safian_plugin_bitmap_gui_t*` |
| host | `com.safian.bitmap-gui.host/1` (`SAFIAN_EXT_BITMAP_GUI_HOST`) | `clap_host_t::get_extension ()` -> `const safian_host_bitmap_gui_t*`; **every function is optional** (NULL = "this host cannot") |

## 2. Rules for every call

* **Main thread only, on both sides.** The plug-in calls the host extension only from inside one of its own functions that the host called, or - when the host runs the platform event loop on the main thread
  (section 5.3) - from a timer / async message of that loop: still the main thread, between `create ()` and `destroy ()`. The audio thread is never involved. Parameters change through ordinary CLAP parameter
  events (section 9).
* **Safe before `activate ()`** and after `deactivate ()`. The GUI does not depend on the audio engine running. Every function also tolerates "no GUI yet" (returns false / does nothing).
* **One GUI per plug-in instance.** `create ()` before everything else; `destroy ()` before `clap_plugin::destroy ()` (a forgotten GUI is destroyed by the plug-in). `create ()` twice without `destroy ()` fails.
* **Logical pixels** are the unit of sizes and input positions (origin top left, x right, y down). **Device pixels** are the unit of the frame and its dirty rectangles: `round (logical * scale)`.
* **Strings are UTF-8**, owned by the caller; the callee copies what it keeps.
* Struct layouts are frozen for `/1`.
* No function throws; failures are `false` / ignored events.

## 3. Life cycle and size

```
create (plugin, width, height, scale, flags)   // logical px; scale 1.0 / 2.0 / 1.5 ...; flags: CREATE_HOST_RUNS_EVENT_LOOP
get_info / get_preferred_size / can_resize / get_size_limits   // valid before create ()
get_size
set_size (w, h)     // clamped into the limits; get_size () tells the result; next frame is FRAME_FULL with new pixel dimensions
set_scale (s)       // the window moved to another display: same logical size, new pixel dimensions, FRAME_FULL
destroy ()
```

SynthyBoy: preferred 1024 x 768, limits 360 x 480 .. 1800 x 1400, any aspect ratio, scale 0.5 .. 4.0. The layout follows the width: below ~600 logical pixels it is the phone layout (one column, 390 x 844 is
the reference), 600 .. 999 the tablet layout, from 1000 the wide three-column layout (1024 x 768 and the MPC's 1024 x 600).

`request_resize ()` (host side) is how the plug-in asks for another size (the user chose "UI size" in its menu, a sheet needs room). `true` means the host will call `set_size ()` (maybe with a different size, maybe later).

## 4. Frames

```c
render (plugin, &frame)  ->  pixels, width, height, stride_bytes, pixel_format, frame_id,
                              dirty_count + dirty[] (device px), flags, next_frame_ms
```

* **Pixel format** `SAFIAN_BITMAP_GUI_PIXEL_BGRA8888_PREMUL`: 32 bits per pixel, one pixel = the little-endian word `0xAARRGGBB`, i.e. the bytes in memory are **B, G, R, A** (the layout of a Windows DIB, of Core Graphics'
  `kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst`, of Cairo `ARGB32` and of a Linux fbdev `ARGB8888`). Colour channels are **premultiplied** by alpha. SynthyBoy paints an opaque background, so alpha is 255 in every
  pixel and premultiplied and straight alpha are the same bytes: a host may ignore the alpha byte (`XRGB8888` framebuffers work unchanged). Row 0 is the top row. Only little-endian plug-ins exist.
* `stride_bytes >= width * 4`, a multiple of 4. The pointer is **read only and valid until the next `render ()`, `set_size ()`, `set_scale ()` or `destroy ()`** of this GUI.
* **`frame_id`** starts at 1 and increases by exactly one for every `render ()` that changed pixels. The same id again = unchanged; `dirty_count` is then 0 and the pixels are still the valid previous frame.
* **Dirty rectangles**: the rectangles that changed since the previous `render ()` of this GUI, in device pixels, disjoint, inside the frame, at most `info.max_dirty_rects` (SynthyBoy: 16; it merges). The host copies
  **only these** to its screen. `FRAME_FULL` = the whole frame changed (first frame, resize, scale change); `dirty` then lists exactly one rectangle, the whole frame.
* **`is_dirty ()`** is one flag read: nothing is run, nothing is drawn. An idle GUI costs one call per host frame. `tick ()` (section 5) runs what is due and returns the same answer afterwards.
* **`FRAME_NEEDS_ANOTHER` / `next_frame_ms`**: the GUI is animating or shows live data (meters, scope, a drag in progress); the host should `tick ()` again within `next_frame_ms` (SynthyBoy: 16). Without the flag the
  GUI is idle and a tick every 50 .. 100 ms is plenty (timers still have to run).

Measured on the build machine (Apple M-series, software renderer, Release): idle `tick () + is_dirty ()` ~1 microsecond, a whole 1024 x 768 frame ~7 ms, the typical dirty update (a knob turning) well below 1 ms. The numbers
for the ARMv7 module are in `ui-design/BITMAPGUI-REPORT.md`, clearly marked as emulated, not MPC.

## 5. Running the GUI: `tick ()`

The plug-in has no thread and no loop of its own. Its toolkit has timers (animations, long-press detection, meters, polling of the engine's live data) and asynchronous messages; `tick ()` runs them. The host calls it
at its UI cadence while the GUI is visible and **at least every 100 ms** (every `next_frame_ms` while the last frame asked for it). `render ()` calls `tick ()` itself. Input events do their work synchronously and
need no tick; what they queue (a deferred repaint, a long-press timer) runs at the next tick.

### 5.1 What `tick ()` does

1. runs the toolkit's pending timers and async messages (bounded: at most 256 messages per call, never waits),
2. takes the parameter changes the host / the audio thread made since the last tick (host automation, a state load, MIDI learn) into the GUI's controls,
3. checks whether the user moved to another panel (remote page, section 8),
4. returns `true` when `render ()` would produce a frame.

### 5.2 Which thread

The calling thread **becomes the toolkit's message thread** at every entry point. A host may call from different threads over time (a plug-in scan thread, then the UI thread), never from two at once. Several plug-in
instances share one toolkit instance (it is a singleton of the module), and several modules with different toolkit versions in one process do not see each other.

### 5.4 `SAFIAN_BITMAP_GUI_CREATE_NO_ONSCREEN_KEYBOARD`

The host has hardware keys of its own (an MPC, a keyboard controller): the plug-in omits its on-screen piano strip and gives the space to the rest of the GUI. Unknown flags are ignored, so a host may always pass it.

### 5.3 `SAFIAN_BITMAP_GUI_CREATE_HOST_RUNS_EVENT_LOOP`

* **Not set** (a framebuffer host, a test harness, a Linux host without a loop the toolkit can join): `tick ()` and `render ()` pump the toolkit's queue themselves (on Linux and Windows the toolkit's own message queue; on
  macOS the run loop of the calling thread is run once for ready sources - this is the only case where the plug-in touches a loop of the host process, so a host that has one should set the flag).
* **Set** (a macOS / Windows application whose native loop runs on the calling thread): the toolkit posts its timers and async messages into that loop and they are delivered by it; `tick ()` does not pump. The plug-in
  may then call host functions at any time on the main thread (section 2).

### 5.4 Several instances, a foreign toolkit

All state of the toolkit is private to the module (statically linked, hidden symbols, class and window names with random suffixes). Another copy of the same toolkit of another version in the host process is unaffected,
and the toolkit never opens a window, a native dialog, a display connection or a font service (section 10).

## 6. Input

Coordinates are logical pixels. Every function returns `true` when the GUI used the event (the host may then not pass it on).

### 6.1 Pointers (`pointer_event`)

`type`: `DOWN`, `MOVE` (while down), `UP`, `CANCEL`, `HOVER` (mouse / pen without a button), `LEAVE` (the pointer left the GUI area). `kind`: mouse, touch, pen. `pointer_id`: identifies one finger / pen / the mouse from
`DOWN` to `UP` / `CANCEL` (any 32 bit value chosen by the host, reusable after `UP`, 0 for the mouse). `buttons` are the buttons down **after** the event (a touch is the primary button, the plug-in adds it when a
touch event leaves it out), `modifiers` shift / ctrl / alt / meta, `pressure` 0..1 (0 = unknown).

* **One finger** acts as the primary mouse button: press, drag, release, with the usual slop and long-press behaviour of the GUI (a long press on a control opens its value sheet; 400 ms with no movement). After
  `UP` the pointer is gone: there is no hover state left behind on a touch screen.
  A drag of one finger that starts on something that does not use the drag itself (the background of a card, a title, a label, a gap) scrolls the panel area, with momentum; a drag that starts on a control (knob,
  slider, curve / envelope / LFO / remap / wavetable / filter / shaper editor, modulation chip or row handle, effect rack, key scale, a button or a text field) belongs to that control and never scrolls, and a tap on
  a control still acts (a press that travelled beyond the slop is no tap). A drag that starts on a control and ends on a background does not scroll either.
* **Two fingers** become the pinch and pan gestures of a desktop trackpad: when a second finger goes down the first stops acting as a mouse (its drag ends where it is) and for as long as two fingers are down every move
  of either is delivered as pinch (the spreading of the pair) or pan (the travel of its middle), whichever the fingers did more of. A third finger is tracked but unused. The gesture ends with the last finger; a finger that
  stays down afterwards does not start a drag. (The toolkit has independent touch input sources on iOS / Android / touch Windows only; this behaviour is the same on every platform, so it is the contract.)
* `CANCEL` (palm rejection, a system gesture took the pointer) releases the pointer like `UP`; the toolkit has no cancel, so a half done gesture may or may not be committed by the control (SynthyBoy commits what the
  drag already did: nothing is half-written, a drag is one undo step).
* Mouse and pen: `HOVER`, `DOWN`, `MOVE`, `UP` with `buttons`; right button = secondary (opens the value sheet like a long press), middle = middle.
* After a mouse event the plug-in calls the host's `set_cursor ()` when the shape under the pointer changed.

### 6.2 Wheel (`wheel_event`)

`delta_x`, `delta_y` in wheel notches (1.0 = one detent; trackpads send fractions with `WHEEL_PRECISE`); positive `delta_y` = wheel rolled away from the user = content moves down (reveals what is above). With
Ctrl / Cmd held a wheel is a zoom where the GUI zooms. A wheel over a knob turns it.

### 6.3 Keys (`key_event`, `text_input`, `set_focus`)

`key_event`: `DOWN` / `UP`, `key_code`, `modifiers`, `flags` (`REPEAT`), `text`. A printable key has the Unicode code point of the character it makes **without shift, lower case** as its code (`'a'`, `'1'`, `' '`);
the other keys have the stable codes `SAFIAN_BITMAP_GUI_KEY_*` (backspace, tab, enter, escape, delete, insert, home, end, page up / down, the arrows, F1 .. F12). `text` (UTF-8, only on `DOWN`) is what the key press
produces with the current modifiers and layout and is what a text field inserts. Hosts without a physical keyboard send only `text_input ()` (committed text from a virtual keyboard or an IME, UTF-8, inserted into the
focused text field) plus backspace / enter / escape / arrows as keys.

Shortcuts of the GUI: Ctrl / Cmd + Z = undo, + Shift + Z or Ctrl / Cmd + Y = redo, Esc / Enter close sheets.

`set_focus (true)` when the GUI (the window or the screen area it is in) gets the keyboard focus, `false` when it loses it. Losing it releases every pointer that is down and ends text editing. Exactly one GUI of the
module should have the focus; a GUI that gets key events without having it takes the focus back from the other (a convenience, not a contract).

## 7. Text input (virtual keyboard)

A text field of the GUI (preset name, exact value entry, search) got the keyboard focus: `request_text_input (show = true, field, kind, text)` - `field` is its rectangle in logical pixels (so the host can scroll or
shift the GUI above the keyboard), `kind` `TEXT` or `NUMBER` (digits and a few letters are enough: `1.5k`, `-12 dB`), `text` its current content. When it loses the focus: `request_text_input (false, NULL, ..., NULL)`.
The host shows / hides its keyboard and sends `text_input ()` and `key_event ()`. A host without a virtual keyboard leaves the function NULL: the GUI still works with a physical keyboard, and a text field can be left
with Esc / Enter.

## 8. Files, sharing, URLs

The plug-in opens no native file dialog and no native window of any kind. What leaves the GUI is a request to the host.

* `request_file (request_id, {mode open | save, title, wildcards "*.wav;*.flac", suggested_name, purpose})`: the host asks the user and later calls the plug-in's `request_done (request_id, result, path)` **exactly once**:
  `OK` with the chosen path (open: the file exists; save: the target), `CANCELLED`, or `FAILED`. Return `false` from the call when you cannot ask (then no answer comes). `purpose` ("sample", "wavetable", "impulse",
  "vital", "lfoshape", "export") lets the host remember the last folder per purpose. The answer may come any time later, from any main-thread context, even before `request_file ()` has returned.
* `share_file (request_id, path, title)`: hand an exported file (the rendered last note) to the platform's share / export function; `request_done (id, OK | FAILED, NULL)` follows.
* `open_url (url)`: open a link in the system browser (no answer).
* **Degrading**: a host that leaves these NULL gets a GUI that dims the entries that need them (the import / save menus, Share), says "This host has no share function" and otherwise works. Nothing blocks.

Request ids are chosen by the plug-in, unique per GUI. A late answer for a destroyed GUI is ignored.

## 9. Parameters, notes, state: through CLAP, not through this extension

* The GUI changes **its own parameters through the normal CLAP parameter events**: an edit becomes `CLAP_EVENT_PARAM_GESTURE_BEGIN`, `CLAP_EVENT_PARAM_VALUE` (flag `IS_LIVE`) and `CLAP_EVENT_PARAM_GESTURE_END` in the output
  events of the next `process ()` or `params.flush ()`, exactly like a hardware knob (the plug-in calls `clap_host_params::request_flush` after an edit so a host that does not process gets a flush). `params.get_value ()` returns
  the new value immediately. The audio thread applies the edit like host automation (same code path, no allocation, no lock). There is no `request_parameter_gesture` in this extension.
* Host automation and state loads reach the GUI the other way: the audio thread publishes the change lock-free and the next `tick ()` refreshes the controls (the changed knobs repaint, nothing else).
* The on-screen keyboard, the pitch / mod wheels and the sustain pedal of the GUI reach the engine as MIDI through the same lock-free channel (`CLAP_EVENT_MIDI` handling of the note port); a destroyed GUI sends
  note offs for held keys and "all notes off".
* Presets: the prev / next arrows step through the presets installed on disk (`<user dir>/presets`, `<user dir>/packs/*`) with the core's preset code; the host's own browser is unaffected.

## 10. Remote controls (Q-Link) follow the panel

The page list is the plug-in's `clap.remote-controls` pages (the Kick pages, one page per module, `page_id` = the index). When the user moves to another panel - taps a tab, or starts a gesture on a control that is on
another page - the plug-in calls `set_active_remote_page (page_id)` and the host's controller bank (the MPC's eight Q-Link encoders) switches to that page. The reverse, `show_remote_page (page_id)` (plug-in function):
the host's bank moved; the GUI shows the panel that holds the page's parameters (returns false when it has none). Neither direction echoes.

## 11. Host implementer's checklist

1. `get_extension (SAFIAN_EXT_BITMAP_GUI)`; if NULL the plug-in has no bitmap GUI.
2. Fill a `safian_host_bitmap_gui_t` for `clap_host::get_extension (SAFIAN_EXT_BITMAP_GUI_HOST)`; leave out what you cannot do (NULL functions are never called). Do it **before** `create ()`: the plug-in reads it there.
3. `create (w, h, scale, flags)`; `can_resize` / `get_size_limits` for the window.
4. Every UI frame: `tick ()`; `if (is_dirty ()) { render (&f); for each f.dirty: blit; }`. Keep the previous frame's pixels (your own surface), the plug-in only tells you what changed.
5. Forward input in logical pixels; call `set_focus ()`.
6. Answer `request_file` / `share_file` with `request_done ()`.
7. On window resize `set_size ()`; on a display with another scale `set_scale ()`; on close `destroy ()` before you destroy the plug-in.
8. Optional: show the virtual keyboard on `request_text_input`, move the Q-Link bank on `set_active_remote_page`, set the cursor on `set_cursor`.

A complete working host, without any toolkit, is `tests/bitmap-gui/bitmap_gui_harness.h` (about 450 lines): load, create, tick / render, input, audio thread, parameter events.

## 12. Guarantees of the SynthyBoy implementation (what the tests prove)

| Property | Test (`tests/bitmap-gui/`) |
|---|---|
| Works before `activate ()`, double `create` / `destroy`, calls without a GUI are harmless | `bg_everything_is_harmless_without_a_gui`, `bg_gui_works_before_activate_and_creates_once` |
| First frame is full, opaque, BGRA | `bg_first_frame_is_full_opaque_bgra` |
| An idle GUI produces no pixels and `tick () + is_dirty ()` costs ~1 microsecond | `bg_idle_gui_produces_no_pixels` |
| A drag on a knob changes the CLAP parameter (gesture + value events) and the audio | `bg_drag_on_a_knob_changes_the_clap_parameter_and_the_audio` |
| An external parameter change refreshes exactly the controls that show it | `bg_external_parameter_change_refreshes_the_pixels` |
| Golden images at 390 x 844, 1024 x 768, 1024 x 600 | `test_bitmap_golden.cpp` |
| Text input (double tap -> value box -> the host keyboard types), file request through the Load button (request, cancel, unknown id, import), open_url, remote page both ways, resize, scale, wheel, undo keys, two fingers, preset arrows, keyboard to audio, several instances, animation advances and settles, ChangeBroadcaster / async delivery inside render without a display, host without the host extension, host without file functions, the toolkit of another JUCE in the process, menus and choice lists open inside the frame | `test_bitmap_gui.cpp` |
| The `.clap` exports only `clap_entry` and `synthyboy_clap_get_entry` | `scripts/check-clap-exports.sh` (ctest `synthyboy_clap_exports`) |
| No leak after `destroy`, no data race between the GUI and the audio thread | ASan / UBSan and TSan builds of the same tests (`.github/workflows/build.yml`) |

## 13. Not in `/1`

OpenGL / GPU surfaces, native child windows, drag and drop of files into or out of the GUI, clipboard, IME composition (only committed text), accessibility, high dynamic range / wide gamut pixel formats. A plug-in that
needs them offers `/2`.

## 14. Statically linked hosts: the iOS library `synthyboy_static_ui`

The Kick app on iOS cannot load a `.clap` (no `dlopen` of plug-ins) and is a JUCE 7.0.12 application, so SynthyBoy is linked into its binary, and so is SynthyBoy's JUCE 8.0.17. Decision of the owner
(`ui-design/14-morning-questions.md` 110): the iOS app has **the same UI as every platform, drawn by the plug-in through this extension**, with the own engine (no Vital-derived code in any iOS build,
`ARCHITECTURE.md` 2.2, `scripts/check-no-vital.sh`). Design and proof: `ARCHITECTURE.md` 4.3. What is on offer, and what a host does with it:

### 14.1 What the library is

| | |
|---|---|
| Library | `libsynthyboy_static_ui.a` (CMake target `synthyboy_static_ui`), built by `-DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos (or iphonesimulator) -DSYNTHYBOY_STATIC_UI=ON -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_WITH_VITAL=OFF` (`scripts/test-ios-static-ui.sh` is the reference invocation). arm64 for the device, arm64 + x86_64 for the simulator, deployment target 15.0 (the lowest the current SDK's libc++ supports without a warning; the Kick app's 16.0 is above it). |
| Entry | `const clap_plugin_entry_t* synthyboy_clap_get_entry (void)` (`shell-clap/synthyboy_clap.h`): **the only symbol the archive exports.** No `clap_entry`. `clap_entry.init (path)` ignores the path, `deinit ()` shuts the toolkit down. |
| Extension | `com.safian.bitmap-gui/1` through `clap_plugin::get_extension ()` of the plug-in the factory creates, exactly as in sections 1 to 11. Nothing in this document changes for a static host. |
| Engine | the own engine (`core/engine-own`): 7 of the 2637 keys are live, the rest is drawn (dimmed when a capability bit is missing) and stored. `ui-design/OWN-ENGINE-GAP.md`. |
| Contents | the plug-in (`synthyboy_clap.cpp`, content service), the bitmap GUI module, the UI kit (`shell-juce/ui`), the view-model, the JUCE 8 modules that the UI needs (gui_basics, graphics, events, core, data_structures, audio_basics, audio_formats, audio_utils and what these pull in), the embedded Roboto. Software renderer only: the frame is a memory buffer, no OpenGL, no Metal view, no window. |
| Not in it | `synthyboy_core` and its packages (`synthyboy_filters_serum`, `_wtedit`, `_shapes`, `_sampleosc`, `_granular`, `_spectral`, `_multisample`: no JUCE, no Vital): they are separate archives, the same as for `synthyboy_static`; their list in link order and the frameworks JUCE needs are in `<build>/shell-clap/synthyboy_static_ui.link.txt` (generated). |
| Frameworks | Foundation, UIKit, CoreGraphics, CoreImage, CoreText, QuartzCore, CoreServices, Accelerate, AudioToolbox, AVFoundation, CoreAudio, CoreAudioKit, CoreMIDI, WebKit (declared by `juce_gui_extra`, which `juce_audio_utils` pulls in; no web view is created); weakly Metal, MetalKit, UniformTypeIdentifiers, UserNotifications. The file lists them exactly. |

### 14.2 Two JUCE versions in one binary: why it works

Three layers (`ARCHITECTURE.md` 4.3): JUCE 8 is compiled in the C++ namespace `synthyboy_juce8` (JUCE 8 has no namespace switch; the build renames `juce` with a compiler definition), its Objective-C classes carry the
prefix `synthyboy_juce8_`, and one partial link turns every symbol of the archive into a local symbol except `synthyboy_clap_get_entry`. Kick's JUCE 7 needs nothing: it stays what it is, unnamespaced. The fonts are a namespace of
their own (`synthyboy_embedded_fonts`), the toolkit's singletons (message manager, desktop, look and feel) are private to the namespace. Kick's `juce::Desktop`, message manager and look and feel are not
touched, and the toolkit of SynthyBoy never opens a window, a native dialog or an audio device.

### 14.3 Integration note for the Kick side

What Kick calls is **what it already calls for a `.clap` on the desktop**: `Kick/clap_host` has the generic client (`BitmapGuiClient`, `BitmapGuiComponent`, `PluginPanel`) of this extension; it needs the plug-in
entry from the static library instead of `dlopen` + `clap_entry`.

1. **Link.** Add `libsynthyboy_static_ui.a` (device or simulator build), the archives of `synthyboy_static_ui.link.txt` and the frameworks of that file to the iOS target. The library **replaces `synthyboy_static`** and also replaces the vendored
   copy of the plug-in sources that `Kick/clap_host/CMakeLists.txt` compiles into its own `synthyboy_core` (`builtin/synthyboy`, with `SYNTHYBOY_STATIC_ONLY=1`): both define `synthyboy_clap_get_entry` and the core
   symbols, so linking both is a duplicate symbol error. Either take the archives of this repository for iOS (recommended: one source of truth, the UI is in it), or keep the vendored sources for every other target and
   leave them out of the iOS target. The two archive sets must be built from the same revision (the library calls the core by name).
   For an Xcode project with prebuilt libraries: one archive per platform is enough, `libtool -static -o libsynthyboy_all.a libsynthyboy_static_ui.a <the archives of the list>`, then
   `xcodebuild -create-xcframework -library libsynthyboy_all.a -headers <shell-clap headers> -output SynthyBoy.xcframework` for the device and the simulator library.
2. **Entry.** `StaticPluginLoader` calls `synthyboy_clap_get_entry ()` as before (`plugin.json`: `static.entry`), `init`, `get_factory (CLAP_PLUGIN_FACTORY_ID)`, `create_plugin (..., "com.safian.synthyboy")`. The static library
   exports no `clap_entry`, so it cannot collide with another statically linked plug-in's.
3. **The extension.** `plugin->get_extension (plugin, SAFIAN_EXT_BITMAP_GUI)` is non-NULL; `BitmapGuiClient::supported (host)` is true. Fill the host table (`SAFIAN_EXT_BITMAP_GUI_HOST`) **before** `create ()`:
   `request_render` / `request_tick` (a `CADisplayLink` or the existing JUCE timer of `BitmapGuiComponent`), `request_text_input` (Kick's on-screen keyboard; on iOS the system keyboard works the same: send `text_input ()` and
   backspace / enter as keys), `request_file` (`juce::FileChooser` of Kick's JUCE 7, answer with `request_done ()`), `share_file` (the share sheet), `open_url`, `set_active_remote_page` (Q-Link pages are irrelevant on iOS: leave it NULL).
4. **`create ()` with `SAFIAN_BITMAP_GUI_CREATE_HOST_RUNS_EVENT_LOOP`** (section 5.3): the app's main run loop runs on the main thread, the toolkit posts its timers and asynchronous messages into it and `tick ()` does not
   pump. Without the flag `tick ()` pumps the run loop of the calling thread once per call (`CFRunLoopRunInMode`), which inside a UIKit callback is not what an app wants.
   Sizes: logical points, the scale is `UIScreen.scale` of the screen the component is on (`set_scale ()` when it changes); phone layout below 600 pt (390 x 844 is the reference), tablet 600 .. 999, wide from 1000 (section 3).
5. **Touch.** One `pointer_event` per touch with `kind = TOUCH` and `pointer_id` per finger, `CANCEL` for `touchesCancelled`; two fingers are pinch / pan (section 6.1). Hardware keyboard: `key_event`.
6. **Threads.** Main thread for every GUI call (section 2); the audio thread only runs the plug-in's `process ()` and changes travel through the CLAP parameter events as on every host (section 9).
7. **Data.** The user directory is `$SYNTHYBOY_USER_DIR`, else `$HOME/Library/Application Support/SynthyBoy`, which on iOS is inside the app's container (`Library/Application Support`, writable). Set `SYNTHYBOY_USER_DIR` with `setenv`
   before the first `create_plugin` to put it elsewhere (a shared container of a Kick extension, say). Presets and packs the app installs go to `<user dir>/presets` and `<user dir>/packs/*` (section 9).
8. **Destroy order.** `destroy ()` of the GUI before `clap_plugin::destroy ()`, `entry->deinit ()` when the last plug-in is gone (it shuts JUCE 8 down); Kick's own JUCE 7 shutdown is independent of it, in any order.
9. **What the user sees.** The same UI as on the desktop; with the own engine most knobs are inert (`ui-design/OWN-ENGINE-GAP.md`): that is a property of the engine, not of the integration.

### 14.4 What is verified, and what is not

| Claim | Evidence |
|---|---|
| The archive builds for the iOS SDK without a warning: device arm64, simulator arm64 and arm64 + x86_64 | `scripts/test-ios-static-ui.sh --sdk iphoneos` / `--archs "arm64;x86_64"`, CI job `ios-static-ui` |
| The only exported symbol is `synthyboy_clap_get_entry`; before the partial link every C++ name is in `synthyboy_juce8` / `synthyboy`, every JUCE Objective-C class is prefixed | `scripts/check-static-ui-symbols.py` (self-testing) |
| No Vital-derived object, symbol or string in the library and in every archive it links | `scripts/check-no-vital.sh` in the same script |
| The extension works through the linked library: the 31 tests (input, frames, parameters, state, files, menus, resize, scale, ...) and the golden pictures of the own engine at 390 x 844, 1024 x 768 and 1024 x 600 | the test programs of `tests/bitmap-gui` built for the simulator SDK and **run in the iOS simulator** (`xcrun simctl spawn`); the same programs run on macOS arm64 against the same static library (`-DSYNTHYBOY_STATIC_UI=ON` without `CMAKE_SYSTEM_NAME`) |
| Next to the real JUCE 7.0.12 (Kick's source tree, not renamed, linked whole with `-force_load`) in one binary: no duplicate symbol, both JUCEs run, no class "implemented in both" | `tests/ios-juce7` (run in the simulator). Without the partial link the same link has 336 duplicate symbols (the audio codecs' C names): the control of the test |
| Not verified | a run on a device (built and linked, not run: no signing identity and no device here); Kick's own iOS project and its link (a stand-in program, not the app); the app on the App Store review; the golden pictures on a device (the simulator renders them identical to macOS to 0.0000 % of the pixels, the device uses the same software renderer); Android (the target is Apple only) |
