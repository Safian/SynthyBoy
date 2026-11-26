# UI map of the MPC overlay (what is on each screen, what it does, what it opens)

One file per page kind of the overlay skin (`shell-mpc/overlay/overlay_layout.json`, `scripts/overlay-skin/mpc-ui-pages-overlay.json`). Owner request 2026-11-21: one place that shows where everything is, so that duplicated and useless controls can be found and removed.

Every page file has the same sections: Purpose, Layout, Controls (label | parameter key | gesture | effect), Opens (popups, detail pages, full screen, and how), Q-Link groups, Live parts (what moves by itself), Duplicates (controls that also exist elsewhere, with where), Questionable (things that look useless or confusing, marked as a judgement). Checked by `tools/lcw-checks/ui-map.py <page>` (sections present, every cited parameter key exists in `docs/params.json`, every cited file exists).

[Page overview](OVERVIEW.md) · [Q-Link audit](q-link-audit.md)

[Play](play.md) · [Macros](macros.md) · [Osc](osc.md) · [Mixer](mixer.md) · [Filter](filter.md) · [Env](env.md) · [LFO](lfo.md) · [Mod](mod.md) · [FX](fx.md) · [Master](master.md) · [Voice](voice.md)

## Saving current screenshots

Use `python3 scripts/save-ui-map-image.py capture.png mixer` for the default Mixer screen. For a distinct visible state use a stable name, for example `python3 scripts/save-ui-map-image.py capture.png mixer --state destination-popup`. Reuse the same screen and state on subsequent captures. Each pair owns one PNG in `docs/ui-map/current/<screen>/<state>.png`. Use the same capture source and device for comparisons, or include the device or renderer in the state when keeping distinct references. Historical before/after measurement images stay with their measurements.
