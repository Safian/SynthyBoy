# Akai MPC Plugin Builder SDK — Issue Drafts

This document contains draft issue reports for upstream submission to the Akai MPC Plugin Builder SDK repository, based on hardware measurements and empirical findings from our plugin on real MPC hardware.

The drafts are ranked in descending order by the amount of debugging time they would save another developer (from critical silent failures that cost days of troubleshooting down to documentation clarifications and capability confirmations).

---

## Issue 1: EnvelopeOverlay drops entire skin silently when built with documented minimal key set; requires factory DAHDSR key set

- **MPC Model & Firmware:** MPC Key 61, MPC 3.10.1.12
- **What the docs / validator say:**
  `docs/ui-reference.md` section 6.13 states: *"The key list is long and the set of stage handles present depends on `envelopeType`. The recommended, best-understood configuration is `ADSR`."* In the table, keys `forceHideShapeHandles`, `forceCurveShape`, `decayAndReleaseCurvesLinked`, and `curveType` are described as *"optional"*, and shape handles are described as: *"Shape (curve) handles for the corresponding stages; bind to \"Invalid\" when the plugin has no shape parameters."* `tools/validate.py` performs no schema validation on `Envelope` or `EnvelopeOverlay` objects.
- **What was observed:**
  On the MPC Key 61, an `EnvelopeOverlay` defined in `localComponentDefinitions` is loaded only when it contains the complete key set of the factory Sub Factory `DAHDSR` envelope: `attackShapeHandle`, `decayShapeHandle`, `releaseShapeHandle`, `forceHideShapeHandles`, `forceCurveShape`, `decayAndReleaseCurvesLinked`, and `curveType`. When built using the minimal key set documented in `ui-reference.md` (e.g. `ADSR`, `AD`, `AHDS`, `Pumper`), the host drops the entire skin, resulting in an empty plugin edit page with zero tabs and no error log line on device. This occurs even if no tab references the overlay definition.
- **Minimal reproduction:**

```json
{
  "version": 1,
  "envelopeType": "DAHDSR",
  "envelopeStyle": "WithHeader",
  "envelopeName": "ENV 1",
  "backgroundColour": "ff1e1d1e",
  "scaleColour": "0",
  "lineColour": "ffd31145",
  "fillColour": "40d31145",
  "highlightColour": "ffffffff",
  "lineWidth": 3,
  "handleWidth": 15,
  "handleHeight": 10,
  "handleShape": "Circle",
  "handleLineWidth": 2,
  "handleLineColour": "ffffffff",
  "handleFillColour": "ff1e1d1e",
  "handleTextColour": "ffffffff",
  "handleLineWidthOnPressed": 2,
  "handleLineColourOnPressed": "ffffffff",
  "handleFillColourOnPressed": "ffd31145",
  "handleTextColourOnPressed": "ffffffff",
  "handleArrowLineColourOnPressed": "ffffffff",
  "handleArrowFillColourOnPressed": "ffd31145",
  "delayHandle": "Delay",
  "attackHandle": "Attack",
  "holdHandle": "Hold",
  "decayHandle": "Decay",
  "sustainHandle": "Sustain",
  "releaseHandle": "Release",
  "attackShapeHandle": "AttackShape",
  "decayShapeHandle": "DecayShape",
  "releaseShapeHandle": "ReleaseShape",
  "forceHideShapeHandles": false,
  "forceCurveShape": 0.5,
  "decayAndReleaseCurvesLinked": false,
  "curveType": "Quadratic"
}
```

- **Suggested doc or validator change:**
  Update `docs/ui-reference.md` section 6.13 to clarify that `EnvelopeOverlay` strictly requires all shape-handle keys and curve configuration attributes on current firmware. Add recursive schema checking to `tools/validate.py` to flag missing required keys in `Envelope` and `EnvelopeOverlay` definitions.

---

## Issue 2: Label of type "Text" is not drawn unless Data handle is remapped to a valid parameter

- **MPC Model & Firmware:** MPC Key 61, MPC 3.10.1.12
- **What the docs / validator say:**
  `docs/ui-reference.md` section 6.3 (*"Label"*) states in its key table:
  *"`handleName` | handle | `\"Data\"` | Ignored for `Text`, but must still be present (use `\"Invalid\"` or `\"Data\"`)."*
- **What was observed:**
  On the MPC Key 61, a fixed text `Label` (`type: "Text"`) placed on a tab is drawn only if its `Data` handle is bound to a valid plugin parameter (e.g. `Parameter 0`). If the handle is `"Invalid"` or the entry's `handle remapping` has an empty map (`{"version": 1, "map": []}`), nothing is rendered on screen. Neither setting `showWhenDataModelInvalid: "Show"` nor changing font styles resolves this. Notably, the SDK generator `tools/gen_ui.py` emits radio button segment labels with an empty remap, causing them not to draw unless manually patched.
- **Minimal reproduction:**

```json
{
  "version": 2,
  "componentData": {
    "version": 1,
    "name": "fixed_label",
    "type": "Label",
    "data": {
      "version": 1,
      "textStyle": {
        "version": 1,
        "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": 22.0},
        "colour": "ff6a6a6c",
        "justification": "horizontallyCentred verticallyCentred",
        "case": "Upper Case"
      },
      "type": "Text",
      "handleName": "Data",
      "text": "FILTER CUTOFF"
    }
  },
  "handle remapping": {
    "version": 1,
    "map": [{"key": "Data", "value": "Parameter 0"}]
  },
  "bounds": {
    "version": 1,
    "acceptsHWFocus": "No",
    "showWhenDataModelInvalid": "Hide",
    "whenVisible": "Always",
    "boundsType": "Absolute",
    "bounds": "23 0 272 26"
  }
}
```

*(Note: Leaving `"map": []` causes the label to render nothing on hardware.)*

- **Suggested doc or validator change:**
  Update `docs/ui-reference.md` section 6.3 to state that `handleName` cannot be unbound or mapped to `"Invalid"` on hardware. Update `tools/gen_ui.py` to bind radio segment labels to a valid parameter handle, and add a check in `tools/validate.py` for unmapped `Text` labels.

---

## Issue 3: Host does not poll getParameter; UI controls redraw only upon audioMasterAutomate

- **MPC Model & Firmware:** MPC Key 61, MPC 3.10.1.12
- **What the docs / validator say:**
  `docs/ui-reference.md` section 6.15 (*"Meter"*) notes: *"the MPC has no audio-level feed from a VST plugin, so the only way to drive a `Meter` is from a **parameter** the plugin updates itself (write the level into a read-only parameter from the audio thread)."* `docs/host-contract.md` mentions parameter and UI handling under *"Parameters"* and *"UI"*, but the documentation does not mention that `audioMasterAutomate` is required for host display updates.
- **What was observed:**
  The MPC host never polls `getParameter` to refresh on-screen UI elements (knobs, faders, value text, film-strip images, or meters). A parameter updated by the plugin without calling `audioMasterAutomate` remains stuck at its previous on-screen display value indefinitely. On-screen controls redraw only when the plugin explicitly invokes `audioMasterAutomate(paramIndex, value)`.
- **Minimal reproduction:**

```json
{
  "version": 2,
  "componentData": {
    "version": 1,
    "name": "displayCursor",
    "type": "Image",
    "data": {
      "version": 2,
      "imageType": "FilmStrip",
      "colour": "0",
      "image": "cursor_strip.png",
      "numFrames": 33,
      "handleName": "Data"
    }
  },
  "handle remapping": {
    "version": 1,
    "map": [{"key": "Data", "value": "Parameter 100"}]
  },
  "bounds": {
    "version": 1,
    "acceptsHWFocus": "No",
    "showWhenDataModelInvalid": "Hide",
    "whenVisible": "Always",
    "boundsType": "Absolute",
    "bounds": "340 30 600 240"
  }
}
```

```cpp
// On plugin side: Updating internal state alone does not redraw the widget on screen:
setParameter(100, normalizedValue);

// Redraw occurs only when the host interface is notified:
audioMasterAutomate(100, normalizedValue);
```

- **Suggested doc or validator change:**
  Clarify in `docs/ui-reference.md` section 6.15 and `docs/host-contract.md` (under *"Parameters"* and *"UI"*) that the MPC UI refresh loop is event-driven via `audioMasterAutomate`. Advise developers to throttle UI update notifications (e.g. 10–25 Hz) from a dedicated worker thread to prevent audio callback stalls.

---

## Issue 4: Plotter widget bound to Parameter N displays nothing (remains empty grey box)

- **MPC Model & Firmware:** MPC Key 61, MPC 3.10.1.12
- **What the docs / validator say:**
  `docs/ui-reference.md` section 6.16 (*"Plotter"*) documents the widget as functional: *"Draws a value as a line with optional history (a scrolling trace)."* The table lists `dataHandle` as a handle, and the section provides an example binding to a parameter: `"dataHandle": "Parameter 12"`.
- **What was observed:**
  On the MPC Key 61, a `Plotter` widget bound to a parameter handle (`Parameter N`, with or without history, with or without `audioMasterAutomate`, or mapped via `Data`) remains an empty grey box. In factory MPC plugins, `Plotter` instances are fed via internal array data channels (`Info/Named/...`), which external VST2 plugins have no mechanism to supply.
- **Minimal reproduction:**

```json
{
  "version": 2,
  "componentData": {
    "version": 1,
    "name": "wavePlotter",
    "type": "Plotter",
    "data": {
      "version": 1,
      "backgroundColour": "ff1e1d1e",
      "lineColourStart": "ffd31145",
      "lineColourEnd": "ffd31145",
      "horizLineGradient": false,
      "lineWidth": 2.0,
      "fillColourStart": "40d31145",
      "fillColourEnd": "00d31145",
      "horizFillGradient": false,
      "fillZero": 0.0,
      "dataHandle": "Data"
    }
  },
  "handle remapping": {
    "version": 1,
    "map": [{"key": "Data", "value": "Parameter 0"}]
  },
  "bounds": {
    "version": 1,
    "acceptsHWFocus": "No",
    "showWhenDataModelInvalid": "Hide",
    "whenVisible": "Always",
    "boundsType": "Absolute",
    "bounds": "40 500 400 100"
  }
}
```

- **Suggested doc or validator change:**
  Update `docs/ui-reference.md` section 6.16 to document that `Plotter` cannot be driven by standard VST parameters in external plugins and stays blank. Add a warning in `tools/validate.py` when `Plotter` widgets are detected.

---

## Issue 5: tools/validate.py rejects skins with >32 total tabs although firmware loads 34+ sub-tabs across <=32 function keys

- **MPC Model & Firmware:** MPC Key 61, MPC 3.10.1.12
- **What the docs / validator say:**
  `docs/ui-reference.md` section 3.5 states: *"Limits: at most **32 tabs** in total and at most **8 sub-tabs** per function key. Tabs beyond those limits are not supported."* `tools/validate.py` (lines 35, 456–457) enforces `MAX_TABS = 32`:
  ```python
  if len(tabs) > MAX_TABS:
      rep.error("TUI.json: %d tabs; at most %d are supported" % (len(tabs), MAX_TABS))
  ```
- **What was observed:**
  On the MPC Key 61, the host successfully loads and displays a skin containing 34 tabs organized under 10 function keys using `fnKeyIndex` and `fnKeySubIndex`. Sub-tab navigation functions properly on hardware, and sub-tab dot indicators appear under the function keys (e.g. 6 under Osc, 4 under Env). The firmware limit of 32 applies to the number of distinct top-level function keys (`fnKeyIndex`), not the total number of sub-tabs.
- **Minimal reproduction:**

```json
{
  "pageData": {
    "version": 1,
    "tabs": [
      {
        "version": 1,
        "tabName": "Osc 1",
        "fnKeyIndex": 1,
        "fnKeySubIndex": 0,
        "componentDefinition": {"version": 4, "actions": [], "backgroundData": {"version": 1, "focussed": {"version": 1, "colour": "0", "image": ""}, "unfocussed": {"version": 1, "colour": "0", "image": ""}}, "ignoreMousePresses": false, "disableCoarseDataWheel": false, "repeats": 1, "hideQLinkBounds": false, "componentsData": []},
        "qlinkBoundsData": []
      },
      {
        "version": 1,
        "tabName": "Osc 1 Shp",
        "fnKeyIndex": 1,
        "fnKeySubIndex": 1,
        "componentDefinition": {"version": 4, "actions": [], "backgroundData": {"version": 1, "focussed": {"version": 1, "colour": "0", "image": ""}, "unfocussed": {"version": 1, "colour": "0", "image": ""}}, "ignoreMousePresses": false, "disableCoarseDataWheel": false, "repeats": 1, "hideQLinkBounds": false, "componentsData": []},
        "qlinkBoundsData": []
      }
    ]
  }
}
```

*(When 34 total tabs are provided across 10 `fnKeyIndex` values, `validate.py` fails with an error, but MPC OS loads the skin without issue.)*

- **Suggested doc or validator change:**
  Update `tools/validate.py` to verify that `len(set(t.get("fnKeyIndex") for t in tabs)) <= 32` and `t.get("fnKeySubIndex", 0) < 8`, instead of restricting `len(tabs) <= 32`. Clarify the sub-tab limit in `docs/ui-reference.md` section 3.5.

---

## Issue 6: Decoded film strips are unpacked into /var/tmp/filmstrips on internal root filesystem

- **MPC Model & Firmware:** MPC Key 61, MPC 3.10.1.12
- **What the docs / validator say:**
  `docs/ui-reference.md` section 1.6 describes film strip format (vertical PNGs) and section 6.4 covers `Knob` film strips. The documentation does not describe host image decoding behavior, caching directories, or disk consumption in temporary partitions.
- **What was observed:**
  On the MPC Key 61, the host unpacks every film strip PNG into uncompressed image files located in `/var/tmp/filmstrips/` on the internal root filesystem (decoded size counts: `width * height * frames * 4 bytes`). For our plugin's skin, 174 files totaling 422 MB were written to `/var/tmp/filmstrips/` (without the LFO pictures, 173 files totaling 417 MB). Identical film strip files shared across multiple controls (such as the four LFOs) are unpacked only once.
- **Minimal reproduction:**

```json
{
  "version": 2,
  "knobType": "FilmStrip",
  "filmStrip": "knob_128_frames.png",
  "numFrames": 128,
  "invert": false,
  "handleName": "Data"
}
```

- **Suggested doc or validator change:**
  Document in `docs/ui-reference.md` and `docs/troubleshooting.md` that the host caches unpacked film strips in `/var/tmp/filmstrips/` at decoded size (`width * height * frames * 4 bytes`). Recommend sharing film strip assets across controls and keeping total decoded asset footprint within safe limits.

---

## Issue 7: Native Meter widget bound to parameter is fully functional on hardware (arm32 and arm64)

- **MPC Model & Firmware:** MPC Key 61 and MPC XL, MPC 3.10.1.12
- **What the docs / validator say:**
  `docs/ui-reference.md` section 6.15 categorizes `Meter` under *"(less tested)"* and cautions: *"Note for third-party plugins: the MPC has no audio-level feed from a VST plugin, so the only way to drive a `Meter` is from a **parameter** the plugin updates itself (write the level into a read-only parameter from the audio thread). `Hold/` (section 7) can provide the peak hold."*
- **What was observed:**
  On both 32-bit (MPC Key 61) and 64-bit (MPC XL) hardware, native `Meter` widgets bound to plugin parameters work reliably with both horizontal (`direction: "Right"`) and vertical (`direction: "Up"`) orientations. When configured with the complete version 3 key set (`version: 3`, `rmsImage: ""`, `rmsHandle: "Invalid"`, `peakHoldSize: 0`), up to 32 simultaneous meters refreshed at 25 Hz operated smoothly on screen. Measurements with our plugin's callback statistics diagnostics confirmed that 32 live meters did not hold up the audio callback on either machine (all callback gaps remained in the bucket below 1.5× the nominal block time; audio callback averaged ~68–70 µs on Key 61 and ~14 µs on XL).
- **Minimal reproduction:**

```json
{
  "version": 2,
  "componentData": {
    "version": 1,
    "name": "specMeter1",
    "type": "Meter",
    "data": {
      "version": 3,
      "direction": "Up",
      "invert": false,
      "inactiveImage": "meter_off.png",
      "peakImage": "meter_on.png",
      "rmsImage": "",
      "peakHandle": "Data",
      "rmsHandle": "Invalid",
      "peakHoldHandle": "Hold/Data",
      "peakHoldSize": 0,
      "peakHoldColour": "0"
    }
  },
  "handle remapping": {
    "version": 1,
    "map": [{"key": "Data", "value": "Parameter 100"}]
  },
  "bounds": {
    "version": 1,
    "acceptsHWFocus": "No",
    "showWhenDataModelInvalid": "Hide",
    "whenVisible": "Always",
    "boundsType": "Absolute",
    "bounds": "36 480 30 136"
  }
}
```

- **Suggested doc or validator change:**
  Promote `Meter` in `docs/ui-reference.md` section 6.15 from *"less tested"* to supported/verified. Document the required version 3 key structure and note that driving meters via `audioMasterAutomate` from a background display thread is safe and performant on both arm32 and arm64.

---

## Not included (no measured statement found)

The following topics were investigated against the measured hardware facts in `docs/MPC.md`, but no hardware measurements were found in the repository documentation:

1. **`XYPad` works:**
   While `XYPad` is listed in `docs/ui-reference.md` section 6.14, no empirical tests or measurements of `XYPad` on MPC hardware are documented in `docs/MPC.md`.
2. **No pinch / two-finger / long-press gestures reach skin widgets:**
   Touch gesture tracking (pinch, pan, long-press) is implemented in our plugin's standalone desktop JUCE interface and described in `docs/BITMAP-GUI-EXTENSION.md`, but was not measured or documented for native MPC JSON skin widgets in `docs/MPC.md`.
3. **Mouse wheel / right click / hover do nothing on skin widgets:**
   Mouse wheel, secondary click, and hover event handling are specified for the desktop bitmap GUI protocol (`docs/BITMAP-GUI-EXTENSION.md`), but no measured statements regarding mouse interactions on standalone MPC hardware appear in `docs/MPC.md`.
4. **Pad MIDI velocity and polyphonic aftertouch behavior with FULL LEVEL on/off:**
   The hypothesis that with FULL LEVEL on, pads send velocity 127 and no poly aftertouch to an external plugin, and with it off they send velocity and poly aftertouch but never X/Y, was checked against `docs/MPC.md`. No pad MIDI or FULL LEVEL measurements are documented in `docs/MPC.md`.
5. **General component schema errors in `TUI.json` (untested widget types, malformed component definitions) silently dropping skin:**
   While `docs/MPC.md` lines 163–164 documents that an `EnvelopeOverlay` with a minimal key set causes the host to drop the entire skin without an error log message, general component schema corruption (unknown widget types, missing required keys in arbitrary components) was not systematically tested or documented as measured on hardware in `docs/MPC.md`.
6. **Bounds object version 2 requires explicit `"additionalInvalidatingHandles"` key:**
   While `docs/MPC.md` lines 174, 215, and 311 reference bounds version 2 and the usage of `additionalInvalidatingHandles` for conditional visibility (`IndexedEnabling`), no hardware measurement or test documenting that omitting this key fails the firmware parser or drops the skin appears in `docs/MPC.md`.
7. **Decoded film strip persistence until reboot:**
   While `docs/MPC.md` line 113 measures that the host unpacks film strips into `/var/tmp/filmstrips/` (174 files totaling 422 MB) and unpacks shared strips only once, `docs/MPC.md` does not document tests regarding whether these files persist across project or plugin unloads until reboot, nor partition exhaustion measurements.
