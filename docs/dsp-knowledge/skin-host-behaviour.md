# Host behaviour of skins seen on the devices (measured 2026-11-27)

Facts about how the MPC host treats our skin, each with its evidence. Raw files: `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-perf/host-skin-findings-2026-11-27/`. FACT = measured on a device, SDK = official SDK doc, HYPOTHESIS = not proven. Ask the mpc-expert first; test on a device only for what this page and the SDK do not answer, and write the result here.

## Host list (MenuOverlay) selection and closing
- FACT (Key 61, 2026-11-27): a left-drawer list opened by a native selector (`Show Overlay` on `Mouse Down`/`Enter Pressed`): a row tap only moves the host check mark; the tap on the list header (upper left, about (106,49)) applies the choice and closes. A tap outside does not close. Evidence: `list-probe-results/none-*`.
- FACT: the skin action triggers are only `Mouse Down`, `Double Click`, `Enter Pressed` (SDK `ui-reference.md` section on onAction); there is no `Mouse Up`, so the order of a choice write and a `Focus Lost` cannot be controlled.
- FACT (Key 61 probes A and B): a `Mouse Down -> Focus Lost` action on every row's Choose component (A) or on the overlay component's own actions (B) closes the list on one row tap, but the selection is NOT applied (the selector and the panel keep the old value). Evidence: `list-probe-results/A-*`, `B-*`. Select-and-close is therefore not possible with the stock MenuOverlay.
- Decision: lists keep the factory behaviour (row marks, header applies). Names only, no custom row badges.

## TextEditOverlay
- FACT (XL, disassembly of /usr/bin/MPC 3.10.1.12): the string setter is never called for plugin parameters; SDK `ui-reference.md` 6.12 says TextEditOverlay is meaningful only for text-valued handles. Key 61 not inspected. Details: `native-text-entry.md`, `text-entry-research.md` in the raw folder.
- Plan: a native Button keyboard writing key codes into plugin parameters (see `docs/review/TEXT-ENTRY-NATIVE-KEYBOARD-2026-11-27.md`).

## A skin change that froze the host on project load
- FACT (XL, Key 61, 2026-11-27): a skin that differed from a working one only in the Mixer strip labels (a `value` Label of `mixerDestination56` became the shared type `sharedNativeLabel459` with `showWhenDataModelInvalid` "Hide" -> "Show", plus changed Mixer tab text styles) froze the XL project load at the progress dialog "Loading Track Outputs 31/32" (18 %, identical frames 15 s apart; `xl-project-load-freeze-progress-18pct.png`) and the Key 61 rebooted (uptime 1 min after the install). The same skin without that change loads fine.
- HYPOTHESIS: the "Show" value for an invalid data model, or the order/reference of the shared label definition, is the trigger. Not isolated yet. Rule: compare the TUI.json by definition key against the last working skin before an install; install on the Key 61 first.

## Tab background
- SDK `ui-reference.md` 4.3 `backgroundData`: the `colour` is filled first, the `image` (if any) is drawn over it; `colour: "0", image: ""` is transparent; a tab root normally sets an explicit colour. A plain colour needs no bitmap.
- FACT: our skin used one shared bitmap `overlay_canvas.png` (3 292 B on disk, 1280x628 decoded = 3.2 MB RGBA in the host) as the root background of all tabs, with the fallback colour `ff1e1d1e`, while the visible ground is `#090d19`. FACT (Key 61, 2026-11-27, adcfaa5b-based skin): `colour ff090d19` with an empty image on the tabs Env 1 and Mix gives exactly the same ground (9,13,25) as the bitmap tabs (Osc A, Play); the project loads normally, the overlay page detection still reads the markers (`view: env1`, `host_background: on (sentinel read)`), no black areas. A plain colour replaces the bitmap (saves the 3.2 MB decoded background). Evidence: `exp-bg-k61/` photos (copied to the raw folder).
- DONE (2026-11-27): every tab of the overlay skin now has `colour ff090d19` and an empty image (`scripts/mpc-skin-extras/99_backgrounds.py`, `PAGE_COLOUR`); `overlay_canvas.png` is deleted. Skin-only generation before/after: the 468 component definitions are identical and only `backgroundData` differs, on all 37 tabs. The sentinel pictures stay (page detection).

## Capture facts used by the tools
- Key 61: the host picture is the primary plane of `/dev/dri/card1`, our overlay is plane 38 (AR24, 800x1280, rotated); `mpc-tools/k61-photo.sh` composites both. XL: card0, overlay plane 46 (`pgrab`).

- FACT: after a Key 61 reboot the DRM card number can change (the host picture was on card1 before, card0 after); the capture tools must try the cards in turn.
