# UI map of the MPC overlay (what is on each screen, what it does, what it opens)

One file per page kind of the overlay skin (`shell-mpc/overlay/overlay_layout.json`, `scripts/overlay-skin/mpc-ui-pages-overlay.json`). Owner request 2026-11-21: one place that shows where everything is, so that duplicated and useless controls can be found and removed.

Every page file has the same sections: Purpose, Layout, Controls (label | parameter key | gesture | effect), Opens (popups, detail pages, full screen, and how), Q-Link groups, Live parts (what moves by itself), Duplicates (controls that also exist elsewhere, with where), Questionable (things that look useless or confusing, marked as a judgement). Checked by `tools/lcw-checks/ui-map.py <page>` (sections present, every cited parameter key exists in `docs/params.json`, every cited file exists).

Pages: play, macros, osc, mixer, filter, env, lfo, mod, fx, master, voice.
