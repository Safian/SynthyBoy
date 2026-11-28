# UI map of the MPC skin (which tab shows which screen)

The tabs of the overlay skin and the code behind each. The views are defined in `shell-mpc/overlay/overlay_layout.json`; the controls of every tab are native components defined once in `skin/defs_<family>.json` (imported by `skin/TUI.json`; see [the component catalogue](../ui-components/README.md)); the live graphs, the Q-Link publication and the page logic are the `*_ui.h` / `*_model.h` files of `shell-mpc/overlay/`. Current hardware captures: `docs/screens/<screen>.jpg`. The Q-Link groups of every page are in `docs/MPC.md` ("Q proxies and the display pages").

| Tab (view) | Screen component | Screen image | Page logic |
|---|---|---|---|
| Play (`play1`) | Play | `play.jpg` | `play_view.h`, `play_osc.h`, `play_env_lfo.h` |
| Macros (`macros`) | Macros | `macros.jpg` | `macro_ui.h`, `spectrum_curve.h` |
| Osc A, Osc B, Osc C, Sub, Noise (`osca`, `oscb`, `oscc`, `oscsub`, `oscnoise`) | `oscScreen` | `osc_a.jpg`, `osc_b.jpg`, `osc_c.jpg`, `sub.jpg`, `noise.jpg` | `osc_ui.h`, `osc_model.h`, `osc_page.h`, `osc_spec_page.h` |
| Env 1 .. Env 6 (`env1` .. `env6`) | `envScreen` | `env.jpg` | `env_ui.h` |
| LFO 1 .. LFO 10 (`lfo1` .. `lfo10`), RND 1 .. RND 4 (`rnd1` .. `rnd4`) | `lfoScreen` | `lfo.jpg`, `rnd.jpg` | `lfo_ui.h`, `lfo_layout.h` |
| FX (`fx`) | FX | `fx.jpg` | `fx_ui.h`, `fx_editor_model.h` |
| Mix (`mixer`) | Mixer | `mixer.jpg` | `mixer_ui.h` |
| Filter 1, Filter 2 (`filter1`, `filter2`) | `filterScreen` | `filter.jpg` | `filter_ui.h`, `filter_geometry.h` |
| Note, Velo (`filternote`, `filtervelo`) | Note, Velo (curve screens) | none | `filter_ui.h`, `scalar_curve_ui.h` |
| Mod (`mod`) | Mod | `mod.jpg` | `mod_ui.h`, `mod_model.h`, `mod_rows_page.h` |
| Master, Tuning (`master`, `tuning`) | Master, Tuning | `master.jpg` | `master_ui.h` |
| Voice (`voice`) | Voice | `voice.jpg` | `voice_ui.h` |

The page of every screen component (what it shows, its skin definitions, the data it binds, its Q groups, the C++ files): [../ui-components/screens/README.md](../ui-components/screens/README.md). The tab names are the `tabName` values of `skin/TUI.json`; `tabOriginY`, the marker squares and the region of every view are in the layout file. A page that has no entry in `docs/screens/` has no current capture yet.
