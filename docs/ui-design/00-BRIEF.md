# SynthyBoy UI design – brief for all design files

Goal: design how **every** Serum 2 and Vital function works in SynthyBoy's **mobile-first** UI
(phone portrait is the smallest target; tablet and MPC landscape reuse the same panels side by side).
This directory is design only: no code. One file per related area (list in README.md).

## Scope

- **Out of scope (Kick provides them):** the preset browser, and any grid / sequencer / arranger / clip
  features. Write one line "excluded: Kick" where the manuals have them, nothing more.
- Serum 2 **Arpeggiator** (manual p.244) and **Clips** (p.219) are sequencer features: treat as excluded (Kick), list them once in section 7 as an owner question.
- Everything else must be covered. No "later" branches: if the engine lacks a function, mark it `GAP`
  and still design the UI for it.

## Sources of truth

- Our engine and parameters: `docs/PARAMETERS.md`, `docs/params.json` (880 parameters, keys such as
  `osc1.unison.detune`), `core/engine-vital/README.md`, `core/include/synthyboy/` (public API),
  `docs/QUALITY.md`, `docs/ARCHITECTURE.md` (§3 engine, §5 ParamInfo, §8 GUI).
- **Serum 2 official manual (354 pages, v2.0.18):** `docs/ui-design/_manuals/serum2/Serum2-Manual.txt`
  (text, page markers `=== PAGE n ===`; PDF next to it). The table of contents is on pages 3-12. Read the
  chapters of your area completely and cite pages as `[S p.123]`.
- **Vital manual (unofficial community manual, 8 pages, thin):** `docs/ui-design/_manuals/vital/Vital-Manual-unofficial.txt`,
  cite as `[V p.n]`. Because it is thin, the imported Vital source under `core/engine-vital/` and our
  `docs/PARAMETERS.md` are the real Vital reference.
- `_manuals/` is gitignored (third-party copyright): quote nothing long, paraphrase, never commit it.
- Never invent parameter keys. Use the keys from `docs/params.json`; use `GAP` when none exists.
- A Serum function that is not clearly described in the manual: write what the manual says, mark `[S unclear]`.

## Mobile design principles (decided)

1. One page = one big visual element + at most 4 main knobs; the rest behind a "more" row / sheet.
2. Direct manipulation: drag on the visual (wavetable position, filter curve, envelope nodes, LFO shape).
3. Modulation by touch: tap a source chip, then drag a knob up/down; a purple ring shows depth. No
   desktop drag-and-drop required. Long press on any knob = exact value + assign / reset / MIDI-learn menu.
4. Always visible: preset name with prev/next, 4 macros, mini keyboard / performance strip, page tabs.
5. Touch targets >= 44 px; no hover-only features; every gesture has a visible alternative (button/menu).
6. Landscape / tablet / MPC: same panels, laid out in a grid (Vital-like); phone shows one at a time.
7. The Kick-side UI is generated from `ParamInfo` + remote-control pages: each file ends with a
   "Kick remote pages" section (page name, up to 8 params per page, in `docs/params.json` keys).

## Required structure of every area file

```
# <Area> – mobile UI design
## 1. Function inventory
Table: Function | Origin (Vital / Serum [unverified] / both) | Our params (keys) | Engine status (HAVE / GAP)
## 2. Screen design
Layout of each page/sheet for phone portrait, then tablet/landscape (ASCII wireframes are welcome).
## 3. Interactions
Table: Control | Gesture | Result | Alternative (non-gesture)
## 4. States and edge cases
Missing content, tier-limited features (quality tiers), bypassed/disabled looks, ranges, units, defaults.
## 5. Kick remote pages
## 6. Coverage check
Every key group of this area from docs/params.json, and where it appears in section 2.
## 7. Open questions (owner)
```

Style: English, terse, tables and wireframes over prose. Hungarian only where quoting the owner.
Do not touch any file outside `docs/ui-design/`.
