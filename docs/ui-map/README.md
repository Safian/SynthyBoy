# UI map of the MPC skin (which tab shows which screen)

The tabs of the overlay skin and the code behind each. The views are defined in `shell-mpc/overlay/overlay_layout.json`; the controls of every tab are native components in `skin/TUI.json` (one definition per component, see [the component catalogue](../ui-components/README.md)); the live graphs, the Q-Link publication and the page logic are the `*_ui.h` / `*_model.h` files of `shell-mpc/overlay/`. Current hardware captures: `docs/screens/<screen>.jpg`. The Q-Link groups of every page are in `docs/MPC.md` ("Q proxies and the display pages").

| Tab (view) | Screen image | Page logic |
|---|---|---|
| Play (`play1`) | `play.jpg` | `play_view.h`, `play_osc.h`, `play_env_lfo.h` |
| Macros (`macros`) | `macros.jpg` | `macro_ui.h`, `spectrum_curve.h` |
| Sub, Osc A, Osc B, Osc C, Noise (`oscsub`, `osca`, `oscb`, `oscc`, `oscnoise`) | `sub.jpg`, `osc_a.jpg`, `osc_b.jpg`, `osc_c.jpg`, `noise.jpg` | `osc_ui.h`, `osc_model.h`, `osc_page.h`, `osc_spec_page.h` |
| Env, Env 2 .. Env 6 (`env1` .. `env6`) | `env.jpg` | `env_ui.h`, `env_geometry.h` |
| LFO, LFO 2 .. LFO 10 (`lfo1` .. `lfo10`), RND 1 .. RND 4 (`rnd1` .. `rnd4`) | `lfo.jpg`, `rnd.jpg` | `lfo_ui.h`, `lfo_layout.h` |
| FX (`fx`) | `fx.jpg` | `fx_ui.h`, `fx_editor_model.h` |
| Mixer (`mixer`) | `mixer.jpg` | `mixer_ui.h` |
| Filter, Filter 2, Note, Velo (`filter1`, `filter2`, `filternote`, `filtervelo`) | `filter.jpg` | `filter_ui.h`, `filter_geometry.h`, `scalar_curve_ui.h` |
| Mod (`mod`) | `mod.jpg` | `mod_ui.h`, `mod_model.h`, `mod_rows_page.h` |
| Master, Tuning (`master`, `tuning`) | `master.jpg` | `master_ui.h` |
| Voice (`voice`) | `voice.jpg` | `voice_ui.h` |

The tab names are the names the host shows; `tabOriginY`, the marker squares and the region of every view are in the layout file. A page that has no entry in `docs/screens/` has no current capture yet.
