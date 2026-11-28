# Screen components of the SynthyBoy skin

One page per screen component. A screen component is what a tab (or a family of tabs) shows: it is built from the shared native components ([../README.md](../README.md)), defined once in `skin/defs_<family>.json` and instantiated by the tabs of `skin/TUI.json`. The skin has 37 tabs and 14 screen components: four repeat over several tabs, ten are one tab body each.

The repeating screens are ONE component each. The tab holds an instance (`Screen`, `Screen Top`, ...) plus the per-tab items (the marker squares, the instance picture, the captions, the background sentinel). The plugin binds the shown oscillator, envelope or LFO to the screen by the view index of the tab (`page::Input::instance`, `shell-mpc/overlay/overlay_layout.json`), so the screen's Parameter handles are the same for every tab of the family. `scripts/validate_synthyboy_skin.py` (`SCREEN_FAMILIES`) checks that each tab of a family holds its screen instances in order.

Choice lists: every dropdown of these screens opens the plugin's own one-tap list (owner rule 2026-11-28, AGENTS.md "Lists: one tap chooses and closes"): a `list open` Button in the field, its `ListOpen` handle on the slot of the field's Data handle (`shell-mpc/native_choice_list.h`), and the shared modal `nativeListModal` (`defs_common.json`) gated by `Parameter 3903`, instanced at the end of `oscScreenTop`, `lfoScreenTop3`, `filterScreenTop` and of the FX, Mix, Mod, Master, Tuning and Voice tab bodies. The Play page keeps its own screen lists ([play.md](play.md)).

| Screen component | Tabs (view names) | Definitions file | Picture | Q groups (reading order) | Page |
|---|---|---|---|---|---|
| `oscScreen` + `oscScreenTop`, three forms | Osc A, Osc B, Osc C (`osca`, `oscb`, `oscc`), Sub (`oscsub`), Noise (`oscnoise`) | `defs_osc.json` | `osc_a.jpg`, `osc_b.jpg`, `osc_c.jpg`, `sub.jpg`, `noise.jpg` | Basic, then the blocks of the bottom band | [osc.md](osc.md) |
| `envScreen` + `envScreenTop` | Env 1 .. Env 6 (`env1` .. `env6`) | `defs_env.json` | `env.jpg` | ADSR, SHAPE, TIME | [env.md](env.md) |
| `lfoScreen` + `lfoScreenTop`, `Top2`, `Top3` | LFO 1 .. LFO 10 (`lfo1` .. `lfo10`), RND 1 .. RND 4 (`rnd1` .. `rnd4`) | `defs_lfo.json` | `lfo.jpg`, `rnd.jpg` | RATE, TIMING, MOTION, KEY FOLLOW, SHAPE; RANDOM, TRIGGER | [lfo.md](lfo.md) |
| `filterScreen` + `filterScreenTop` | Filter 1, Filter 2 (`filter1`, `filter2`) | `defs_filter.json` | `filter.jpg` | MAIN, TRACK, OUT, FORMANT | [filter.md](filter.md) |
| Note and Velo curve screens (tab bodies) | Note, Velo (`filternote`, `filtervelo`) | `defs_filter.json`, `defs_mod.json` | none | EDIT, LEGATO AND GRID | [filter.md](filter.md) |
| Play (tab body) | Play (`play1`) | `defs_play.json` | `play.jpg` | oscillator Basic, envelope ADSR, LFO RATE, a fourth group | [play.md](play.md) |
| Macros (tab body) | Macros (`macros`) | `defs_macro.json` | `macros.jpg` | MACROS 1-4, MACROS 5-8 | [macros.md](macros.md) |
| Mixer (tab body) | Mix (`mixer`) | `defs_mixer.json` | `mixer.jpg` | the ten LEVEL faders, banks of four | [mixer.md](mixer.md) |
| FX (tab body) | FX (`fx`) | `defs_fx.json` | `fx.jpg` | the rows of four values of the open effect | [fx.md](fx.md) |
| Mod (tab body) | Mod (`mod`) | `defs_mod.json` | `mod.jpg` | matrix groups, slot page, targets rows | [mod.md](mod.md) |
| Master (tab body) | Master (`master`) | `defs_master.json` | `master.jpg` | OUTPUT, PITCH, VOICES, GLIDE | [master.md](master.md) |
| Tuning (tab body) | Tuning (`tuning`) | `defs_tuning.json` | none | SCALE, TUNING, MPE, MPE Y | [master.md](master.md) |
| Voice (tab body) | Voice (`voice`) | `defs_voice.json` | `voice.jpg` | STEP, RANDOM, SCALING, TARGETS | [voice.md](voice.md) |

Pictures are `docs/screens/<name>.jpg` (relative to this folder: `../../screens/<name>.jpg`). Definitions used by several families (the modulation editor, the targets title and keyline, the compact Play dial family, the picker list rows) are in `defs_common.json`.

## What every page of this folder states

- **Handles by role**: the screen binds handles of the native-UI block of the plugin parameters, named by block (`kOscBase`, `kEnvFields`, ...) in `shell-mpc/native_ui_params.h`. The Parameter numbers in the skin are `2719 + block offset`; they move when a block grows, so the blocks are the stable names.
- **Q-Link mapping**: every tab maps Q-Link n to the proxy Q n (`skin/Q-Links.json`, 16 links, `Bank Direction "Row"`); the groups a page publishes into the 16 slots, and the rule, are in [../../MPC.md](../../MPC.md), "Q proxies and the display pages" (`shell-mpc/q_router.h`, `shell-mpc/overlay/q_publish.h`).
- **Gating**: a control that applies only in some state is gated in the skin by `IndexedEnabling/<k>/<n>/Parameter <handle>` or `NonZero/` on a published handle; a control of a switched-off source has an `Off` (`NativeInactive`) twin or a translucent veil (`oscNativeOffVeil`).
- **The page model**: the C++ files `shell-mpc/overlay/*_ui.h` and `*_model.h` hold the parameter rows, the Q tables, the geometry and the native bridge; the controls themselves are native components.
- **Page-loading and tab exit**: whether leaving the tab retains a read-only display cache (`suspend()`, e.g. Osc, Mod, Voice, Master) or tears down native handles on exit.
