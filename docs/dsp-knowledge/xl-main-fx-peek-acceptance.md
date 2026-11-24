# XL Acceptance Procedure: MAIN FX PEEK and Physical Q-Link Targets (Breath of Machines ↔ Acid Saw Reso)
Copyright (C) 2026 Safian Szabolcs
SPDX-License-Identifier: AGPL-3.0-or-later

## 1. Goal and Scope

This document specifies the source-backed MPC XL acceptance procedure for the **Mixer MAIN FX PEEK panel** and its **physical Q-Link targets** across open preset changes between:
- **Preset A**: `Breath of Machines` (`content/presets/FX/breath_of_machines.sbpreset`)
- **Preset B**: `Acid Saw Reso` (`content/presets/Bass/acid_saw_reso.sbpreset`)

The procedure verifies that:
1. Opening the MAIN FX PEEK panel establishes the active effect cards and editor for the selected preset.
2. Changing presets with the PEEK panel held open (`A → B → A2`) updates the chain cards, selects the appropriate initial card, updates the editor canvas and controls, and dynamically rebinds the 16 physical Q-Link encoders.
3. Every open state matches its reopened control pixel-for-pixel (`delta == 0` for `A/A-reopen`, `B/B-reopen`, `A2/A2-reopen`, and `A/A2`).
4. Physical Q-Link encoder turns affect **only** their targeted parameter, leaving all other 2,636 engine parameters unchanged in full state comparisons.
5. All hardware values not yet captured on the physical XL unit are explicitly flagged as `UNMEASURED ON HARDWARE`.

---

## 2. Root Cause Analysis: Previous Failure on Build `97318619`

On XL build `97318619`, `docs/dsp-knowledge/mpc-display.md` recorded:
> "MAIN FX PEEK differed from its first reopened control by 138000 pixels. ... Raw captures and logs are in `grouped-xl-ui-v6/`; the comparison table and helper timings are in `integrated-xl-ui-v6/README.md`."

Inspection of the test script `grouped-xl-ui-v6/curves-extra.py` and git history reveals two compounding issues:

1. **Test Sequencing Artifact in `curves-extra.py`:**
   ```python
   # grouped-xl-ui-v6/curves-extra.py line 9:
   for name, x, a, b, roi in [
       ('peek-f1', 108, 'Solid Sub', 'Breakbeat Slice Keys', (45, 245, 1190, 469)),
       ('peek-f2', 285, 'Breath of Machines', 'Ring Mod Gamelan', (45, 245, 1190, 469)),
       ('peek-mainfx', 461, 'Acid Saw Reso', 'Wah Pressure Clav', (65, 310, 1215, 430))
   ]:
       pics = {}; tap(x, 137)  # <-- TAPS MAIN FX BEFORE SETTING PROGRAM A ('Acid Saw Reso')!
       for stage, program in [('A', a), ('B', b), ('A2', a)]:
           prefix = name + '-' + stage
           named(program, prefix)
   ```
   Before entering the loop for `peek-mainfx`, the engine was running the previous preset (`Ring Mod Gamelan`). The script tapped `x=461` (MAIN FX) **before** issuing `named('Acid Saw Reso')`. Shot `peek-mainfx-A` was captured after a preset change on an already-open panel from an arbitrary unaligned state, whereas `peek-mainfx-A-reopen` was captured by closing and reopening freshly on `Acid Saw Reso`.

2. **Runtime Stale-Selection Defect Prior to Commit `ef9ff7d6`:**
   Prior to commit `ef9ff7d6` ("Retain matching FX selection across preset loads"), `FxUi::update` on a state generation increment (`inst.stateGeneration()`) did not cleanly reset selection when the previous module became disabled in the new preset. Commit `ef9ff7d6` fixed this:
   ```cpp
   // shell-mpc/overlay/fx_ui.h lines 166-184:
   const uint64_t generation = inst.stateGeneration();
   if (generation != stateGeneration_)
   {
       stateGeneration_ = generation;
       const bool preserve = previousSelectedOn &&
                             synthyboy::fxItemOn (plain_.data(), previousSelected) &&
                             kindOfItem (previousSelected) == previousSelectedKind;
       if (! preserve)
           selectedItem_ = { false, -1 };
       selectedSlot_ = 0;
       selectionChanged_ = true;
       dropGesture();
       qSlots_.forget();
   }
   ```
   When switching between presets where active FX in Rack 0 differ, `preserve` evaluates to `false`, `selectedItem_` resets to `-1`, `rebuildChain()` repopulates `cardItems_` from `racks.rack[0]`, selects slot 0 of the new preset, and pulls the fresh editor and Q-Link targets.

---

## 3. Preset Data Inspection and Bindings

### 3.1 Preset A: `Breath of Machines`
- **File**: `content/presets/FX/breath_of_machines.sbpreset`
- **Category**: `FX`, Author: `Safian Szabolcs`, `tierTrimDb`: `-0.6 dB`
- **Fixed FX in Rack 0 (MAIN)**:
  - `eq.on = 1`, `eq.rack = 0`: **EQ** (fixed module index 4).
    - `eq.low.cutoff = 90.0 Hz`, `eq.low.gain = -6.0 dB`, `eq.low.mode = 0`, `eq.low.resonance = 30.0 %`
    - `eq.mid.cutoff = 1800.0 Hz`, `eq.mid.gain = 2.0 dB`, `eq.mid.mode = 0`, `eq.mid.resonance = 30.0 %`
    - `eq.high.cutoff = 7000.0 Hz`, `eq.high.gain = -3.0 dB`, `eq.high.mode = 0`, `eq.high.resonance = 30.0 %`
    - `eq.level = 0.0 dB`, `eq.law = 0`, `eq.mid2.mode = 0` (Off)
  - `delay.on = 1`, `delay.rack = 0`, `delay.mix = 16.0 %`: **Delay** (fixed module index 2).
  - `reverb.on = 1`, `reverb.rack = 0`, `reverb.mix = 26.0 %`: **Reverb** (fixed module index 8).
  - All other fixed modules in Rack 0 (`chorus`, `compressor`, `distortion`, `flanger`, `phaser`, etc.) have `on = 0`.
  - No pool instances (`fxi01`..`fxi16`) are active in Rack 0.
- **FX Chain Order in Rack 0**:
  1. `EQ` (module 4) — Slot 0
  2. `Delay` (module 2) — Slot 1
  3. `Reverb` (module 8) — Slot 2
- **Initial Selection**: Slot 0 (`EQ`).
- **Editor Display**: 4-band frequency curve (`low`, `mid`, `mid2` [mode 0: off], `high`).
- **Visible Rows**: 5 visible rows (`numVis_ = 5`, `defaultRows = 2`):
  - Row 0: "LOW AND MID BANDS" (4 controls: `eq.low.cutoff`, `eq.low.gain`, `eq.mid.cutoff`, `eq.mid.gain`)
  - Row 1: "HIGH BAND AND OUTPUT" (3 controls: `eq.high.cutoff`, `eq.high.gain`, `eq.level`, 1 blank)
  - Row 2: "MODES" (4 controls: `eq.low.mode`, `eq.mid.mode`, `eq.high.mode`, `eq.law`)
  - Row 3: "RESONANCE" (3 controls: `eq.low.resonance`, `eq.mid.resonance`, `eq.high.resonance`, 1 blank)
  - Row 4: "MID BAND 2" (1 live control: `eq.mid2.mode`; cutoff, gain, resonance hidden because mode=0)

### 3.2 Preset B: `Acid Saw Reso`
- **File**: `content/presets/Bass/acid_saw_reso.sbpreset`
- **Category**: `Bass`, Author: `Safian Szabolcs`, `tierTrimDb`: `-4.0 dB`
- **Fixed FX in Rack 0 (MAIN)**:
  - `distortion.on = 1`, `distortion.rack = 0`: **Distortion** (fixed module index 3).
    - `distortion.type = 0` (Soft Clip), `distortion.drive = 0.0 dB`, `distortion.mix = 0.0 %`, `distortion.level = 0.0 dB`
    - `distortion.filter_order = 0` (Off), `distortion.filter_cutoff = 4000.0 Hz`, `distortion.filter_resonance = 50.0 %`
  - `chorus.on = 1`, `chorus.rack = 0`, `chorus.mix = 0.0 %`: **Chorus** (fixed module index 0).
  - Delay, Reverb, EQ have `on = 0`. No pool instances active in Rack 0.
- **FX Chain Order in Rack 0**:
  1. `Distortion` (module 3) — Slot 0
  2. `Chorus` (module 0) — Slot 1
- **Initial Selection**: Slot 0 (`Distortion`).
- **Editor Display**: Waveshaper transfer curve (identity diagonal because `distortion.mix == 0`).
- **Visible Rows**: 2 visible rows (`numVis_ = 2`, `defaultRows = 2`):
  - Row 0: "DISTORTION" (4 controls: `distortion.type`, `distortion.drive`, `distortion.mix`, `distortion.level`)
  - Row 1: "FILTER" (1 live control: `distortion.filter_order`; cutoff/resonance hidden because order=0)
  - Row 2 ("FILTER SHAPE") has no live controls and is hidden (`numVis_ = 2`).

---

## 4. UI Architecture and Coordinates

- **Canvas Size**: 1280 × 800 px.
- **Mixer Tab Rect**: `{ 0, 110, 1280, 628 }`.
- **Mixer Routing Bar**: Rect `{ 6, 114, 1268, 56 }`.
- **MAIN FX Button Rect**: Target index 2 in `MixerRoutingBar::peekRect(2)`.
  - Formula: `hitW = 96`, `x = rect.x + int(rect.w * 0.36) - 48 = 6 + 456 - 48 = 414`.
  - Bounding box: `{ 414, 114, 96, 56 }`. Center: `(462, 142)`.
  - Test tap coordinate: `(461, 137)` (standardized from `curves-extra.py`).
- **PEEK Panel Rect**: `{ 8, 134, 1264, 580 }` (`MixerUi::peekPanelRect()`).
- **CLOSE Button**:
  - Bound to `peekClose_` with text `"MAIN FX"`.
  - In native mode the hit area follows `nativePeekCloseRect()`.
  - Test tap coordinate: `(40, 205)`.
- **Card Chain Bar ROI**: `{ 116, 134, 1152, 70 }`.
- **Slot Editor ROI**: `{ 16, 210, 1248, 490 }`.
- **Editor Display/Graph ROI**: `{ 65, 310, 1150, 120 }` (centered display box).

---

## 5. Physical Q-Link Encoders and OLED Target Maps

On the MPC XL, 16 physical Q-Link encoders are arranged in a horizontal row, with OLED displays directly above each encoder. Under `ui::QGroup::wideDevice == true`:

### 5.1 Preset A (`Breath of Machines` — EQ Active)
With 5 visible rows and active row 0 ("LOW AND MID BANDS"), `QRouter::mapGroups` maps groups 0, 1, 2, 3 into blocks 0..3:

| Physical Knob | Parameter Key | Engine Table Idx | Parameter Name | Initial Plain Value | OLED Readout Text |
|---|---|---:|---|---:|---|
| **Q1** | `eq.low.cutoff` | 837 | EQ Low Cutoff | 90.0 Hz | `90 Hz` |
| **Q2** | `eq.low.gain` | 838 | EQ Low Gain | -6.0 dB | `-6.0 dB` |
| **Q3** | `eq.mid.cutoff` | 841 | EQ Mid Cutoff | 1800.0 Hz | `1.8 kHz` |
| **Q4** | `eq.mid.gain` | 842 | EQ Mid Gain | 2.0 dB | `+2.0 dB` |
| **Q5** | `eq.high.cutoff` | 845 | EQ High Cutoff | 7000.0 Hz | `7.0 kHz` |
| **Q6** | `eq.high.gain` | 846 | EQ High Gain | -3.0 dB | `-3.0 dB` |
| **Q7** | `eq.level` | 1500 | EQ Level | 0.0 dB | `0.0 dB` |
| **Q8** | *(none)* | -1 | *(unmapped)* | — | *(blank)* |
| **Q9** | `eq.low.mode` | 836 | EQ Low Mode | 0 (Shelf) | `Shelf` |
| **Q10** | `eq.mid.mode` | 840 | EQ Mid Mode | 0 (Bell) | `Bell` |
| **Q11** | `eq.high.mode` | 844 | EQ High Mode | 0 (Shelf) | `Shelf` |
| **Q12** | `eq.law` | 2635 | EQ Law | 0 (Clean) | `Clean` |
| **Q13** | `eq.low.resonance` | 839 | EQ Low Resonance | 30.0 % | `30 %` |
| **Q14** | `eq.mid.resonance` | 843 | EQ Mid Resonance | 30.0 % | `30 %` |
| **Q15** | `eq.high.resonance` | 847 | EQ High Resonance | 30.0 % | `30 %` |
| **Q16** | *(none)* | -1 | *(unmapped)* | — | *(blank)* |

*Status snapshot vector*:
`page_q_slots: 837 838 841 842 845 846 1500 -1 836 840 844 2635 839 843 847 -1`

### 5.2 Preset B (`Acid Saw Reso` — Distortion Active)
With 2 visible rows (`nGroups = 2 <= 4`), wide mapping keeps groups in blocks 0 and 1, leaving blocks 2 and 3 unmapped:

| Physical Knob | Parameter Key | Engine Table Idx | Parameter Name | Initial Plain Value | OLED Readout Text |
|---|---|---:|---|---:|---|
| **Q1** | `distortion.type` | 828 | Distortion Type | 0 (Soft Clip) | `Soft Clip` |
| **Q2** | `distortion.drive` | 829 | Distortion Drive | 0.0 dB | `0.0 dB` |
| **Q3** | `distortion.mix` | 830 | Distortion Mix | 0.0 % | `0 %` |
| **Q4** | `distortion.level` | 1499 | Distortion Level | 0.0 dB | `0.0 dB` |
| **Q5** | `distortion.filter_order` | 831 | Distortion Filter Order | 0 (Off) | `Off` |
| **Q6** | *(none)* | -1 | *(unmapped)* | — | *(blank)* |
| **Q7** | *(none)* | -1 | *(unmapped)* | — | *(blank)* |
| **Q8** | *(none)* | -1 | *(unmapped)* | — | *(blank)* |
| **Q9** | *(none)* | -1 | *(unmapped)* | — | *(blank)* |
| **Q10** | *(none)* | -1 | *(unmapped)* | — | *(blank)* |
| **Q11** | *(none)* | -1 | *(unmapped)* | — | *(blank)* |
| **Q12** | *(none)* | -1 | *(unmapped)* | — | *(blank)* |
| **Q13** | *(none)* | -1 | *(unmapped)* | — | *(blank)* |
| **Q14** | *(none)* | -1 | *(unmapped)* | — | *(blank)* |
| **Q15** | *(none)* | -1 | *(unmapped)* | — | *(blank)* |
| **Q16** | *(none)* | -1 | *(unmapped)* | — | *(blank)* |

*Status snapshot vector*:
`page_q_slots: 828 829 830 1499 831 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1`

---

## 6. Step-by-Step Hardware Acceptance Procedure

The runner `scripts/measure-xl-main-fx-peek.py` expects one SynthyBoy instance in the test project, the Mixer root visible and the status and debug hooks available. It uses the live instance dump for the program identity and all 2,637 engine parameters. It prompts the operator at the physical Q2 step for a +5.0 dB change. Fixture replay requires recorded status, ACK, images and instance snapshots. Fixture replay and synthetic dry runs report separate offline outcomes.

The runner reloads the initial factory program by index after the checks or on error. `restore-diff.json` records the initial and observed program, each changed parameter's complete before and after values, and selection or verification errors. The original measurement error also remains in `acceptance-summary.txt`. A measured PASS requires a verified restore and a successful hardware run.

### Phase 1: Clean Baseline Setup
1. Switch to Mixer view: `goto('mixer', 'MIX')`.
2. Load Preset A cleanly before opening panel:
   ```bash
   echo 'Breath of Machines' > /tmp/synthyboy-select-program
   ```
   Wait for ACK: `synthyboy-select-program.ack` contains `ok Breath of Machines`.
3. Open MAIN FX PEEK panel: `tap(461, 137)`.
4. Validate status file `/tmp/synthyboy-overlay-status.txt`:
   - `view: mixer`
   - `page_top: 26` (`MixerUi::kPagePeek`)
   - `gpu_state: running`
   - `page_q_slots`: must match Preset A vector (837 838 841 842 845 846 1500 -1 836 840 844 2635 839 843 847 -1).

### Phase 2: Capture Stage A and Reopened Control A
1. Capture open shot `peek-mainfx-A`.
2. Capture reopened reference `peek-mainfx-A-reopen`:
   - Tap CLOSE button: `tap(40, 205)`.
   - Reopen MAIN FX PEEK: `tap(461, 137)`.
   - Capture shot `peek-mainfx-A-reopen`.
3. Verify equality:
   - `delta(peek-mainfx-A, peek-mainfx-A-reopen) == 0` (pixel-for-pixel exact match on composed RGB and raw overlay plane).

### Phase 3: Transition to Preset B with Panel Held Open
1. **With the PEEK panel remaining open**, switch to Preset B:
   ```bash
   echo 'Acid Saw Reso' > /tmp/synthyboy-select-program
   ```
   Wait for ACK confirming program selection.
2. Validate status file:
   - `view: mixer`
   - `page_top: 26`
   - `page_q_slots`: must match Preset B vector (828 829 830 1499 831 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1).
3. Capture open shot `peek-mainfx-B`.
4. Capture reopened reference `peek-mainfx-B-reopen`:
   - `tap(40, 205)`.
   - `tap(461, 137)`.
   - Capture shot `peek-mainfx-B-reopen`.
5. Verify equality:
   - `delta(peek-mainfx-B, peek-mainfx-B-reopen) == 0`.

### Phase 4: Target-Only Physical Q-Link Checks
1. On Preset B with panel open, turn physical Q-Link encoder **Q2** (mapped to `distortion.drive`, table idx 829) clockwise by +5.0 dB.
2. Dump all 2,637 engine parameters via parameter dump tool or status inspect.
3. Assert:
   - `distortion.drive` (param 829) changed from `0.0 dB` to `+5.0 dB`.
   - **All other 2,636 parameters** in the engine snapshot (including `distortion.mix`, `distortion.type`, filter, oscillator, envelope, and mixer levels) are strictly identical to their initial values.
   - OLED readout above encoder 2 updates to `+5.0 dB`.
   - Acid Saw Reso has zero distortion mix. The dry transfer remains a straight line as Drive changes.
4. Return Q2 to `0.0 dB`.

### Phase 5: Transition Back to Preset A2 (Round-Trip Restoration)
1. **With the PEEK panel remaining open**, switch back to Preset A:
   ```bash
   echo 'Breath of Machines' > /tmp/synthyboy-select-program
   ```
   Wait for ACK.
2. Validate status file:
   - `view: mixer`, `page_top: 26`.
   - `page_q_slots` restored to Preset A vector.
3. Capture open shot `peek-mainfx-A2`.
4. Capture reopened reference `peek-mainfx-A2-reopen` (`tap(40, 205); tap(461, 137)`).
5. Verify equalities:
   - `delta(peek-mainfx-A2, peek-mainfx-A2-reopen) == 0`.
   - `delta(peek-mainfx-A, peek-mainfx-A2) == 0`.
6. Close PEEK panel: `tap(40, 205)`. Validate `page_top: 0` and Q-Links restored to Mixer strip levels (`strips 0..9`).

---

## 7. Acceptance Criteria and Results Summary Table

| Check Item | Acceptance Criterion | Source/Evidence Grounding |
|---|---|---|
| **A Initial Open** | Shows 3 cards [EQ (selected), Delay, Reverb]; 4-band EQ editor | `content/presets/FX/breath_of_machines.sbpreset` |
| **A Q-Link Targets** | `837 838 841 842 845 846 1500 -1 836 840 844 2635 839 843 847 -1` | `shell-mpc/ui/fx_layout.h` (EQ layout), `docs/params.json` |
| **A / A-reopen** | `0` differing pixels | Clean-room display idempotence |
| **A → B Open Transition** | Updates to 2 cards [Distortion (selected), Chorus]; Distortion editor | Commit `ef9ff7d6`, `fx_ui.h` lines 166-184 |
| **B Q-Link Targets** | `828 829 830 1499 831 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1` | `shell-mpc/ui/fx_layout.h` (Distortion layout), `docs/params.json` |
| **B / B-reopen** | `0` differing pixels | Clean-room display idempotence |
| **B Target-Only Check** | Turning Q2 changes only param 829; remaining 2,636 params unchanged | `shell-mpc/q_router.h`, `shell-mpc/overlay/fx_ui.h` |
| **B → A2 Open Transition** | Restores 3 cards [EQ (selected), Delay, Reverb]; EQ editor | Commit `ef9ff7d6` |
| **A2 / A2-reopen** | `0` differing pixels | Clean-room display idempotence |
| **A / A2 Round-Trip** | `0` differing pixels | Exact restoration |

---

## 8. Status of Hardware Measurement Values

In accordance with `clean room` and `docs/dsp-knowledge/` policy:

- `UNMEASURED ON HARDWARE: exact differing pixel count between A and B in ROI (65,310)-(1215,430)`.
  - *Theoretical Bound*: Differing pixels will be substantial (estimated > 100,000 px out of 138,000 in ROI due to complete replacement of 4-band EQ curve and controls with Distortion waveshaper and controls).
  - *Hardware Confirmation*: Awaiting owner device run.
- `UNMEASURED ON HARDWARE: frame render pacing during open-panel preset switch on RK3588`.
  - *Hypothesis*: One presentation stride immediately upon state generation acknowledgment, zero helper wakeups at rest after redraw.
  - *Hardware Confirmation*: Awaiting owner device run.

Measured on XL (2026-11-23): MAIN FX PEEK A→B→A changed 19,601 graph pixels on B and reproduced A exactly. Physical Q2 changed only distortion.drive (0 to 5.01562 dB; OLED 5.0). At zero mix the full frame stayed identical across the turn. Reopened panels matched, with a four-pixel strip outside the panel differing in two whole-frame comparisons. Evidence: `.hw-local/mpc-perf/xl-mainfx-20261123-G5hD/results.md`.
