# MPC skin extras

Modules in this folder post-process the generated MPC skin (`Plugin Skins/TUI.json` and `Q-Links.json`).
`scripts/apply-mpc-skin-extras.py` runs after the SDK `gen_ui.py` (see `scripts/build-mpc.sh`; `--meta skin-meta.json` is the file `scripts/gen-mpc-ui.py --layout` writes next to ui.json), loads every
`*.py` file here in name order (for example `20_fx.py`, `50_filter_overlay.py`, `95_subtabs.py` and `98_tile.py`) and calls

    def apply(tui: dict, qlinks: dict, ctx) -> None

A module changes `tui` / `qlinks` in place. It may copy files into `ctx.skins_dir`. A module that has nothing to do
leaves everything alone; if no module changes anything the two JSON files are not rewritten (bytes stay identical).

## ctx

| Member | Meaning |
| --- | --- |
| `ctx.param_index(key)` | 0-based parameter index of a `docs/params.json` key (the N of `Parameter N`). Unknown key: error naming the key. Never write indexes by hand. |
| `ctx.tab(title)` | The tab object whose `tabName` is `title` (the dict inside `tui["pageData"]["tabs"]`). Unknown or ambiguous title: error listing the tab names. |
| `ctx.param_key(index)` | The `docs/params.json` key of a parameter index (the inverse of `param_index`), or the key a module registered with `add_param_key`. |
| `ctx.doc` | The whole parsed `docs/params.json` (`modSources`, `modDestinations`, `params`). |
| `ctx.param_count` | Number of entries of `docs/params.json`; the plugin's own parameters (LFO cursors, spectrum columns, pad pressure) have the indexes after it. |
| `ctx.add_param_key(index, key)` | Registers the name of a shell parameter for subsequent hooks. |
| `ctx.meta` | The parsed `skin-meta.json` (`fnKeys`: function keys and their tab names; `tabs`: per tab the short names of its controls; `variant`: only when the layout file names a skin variant, e.g. `"overlay"`) or `None` without `--meta`; `95_subtabs.py` raises without it. A module of a variant (`50_filter_overlay.py`) returns at once unless `variant` is its own: the production skin stays byte for byte as it is. |
| `ctx.skins_dir` | `pathlib.Path` of the `Plugin Skins` folder; put PNGs here, reference them by bare file name. |
| `ctx.add_component_definition(name, obj)` | Appends `{"key": name, "value": obj}` to `pageData.componentDefinitions.localComponentDefinitions`. Name clash: error. |

## Shape of the data

- `tui["pageData"]["tabs"]` is a list; every tab is `{"version": 1, "tabName", "fnKeyIndex", "fnKeySubIndex", "componentDefinition", "qlinkBoundsData"}`.
- The tab content is a list at `tab["componentDefinition"]["componentsData"]`; each entry is
  `{"version": 2, "componentData": {"version": 1, "name", "type", "data": {...}}, "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter N"}]}, "bounds": {"version": 1, "acceptsHWFocus", "showWhenDataModelInvalid", "whenVisible", "boundsType": "Absolute", "bounds": "x y w h"}}`.
  Tab coordinates: 1280 x 628. Append to the list; later entries are drawn on top.
- The SDK writes some objects with repeated keys (for example two `version` keys). The loader keeps them: the second and later
  occurrence of a key `k` is held as `k + "\x00" + n` and written back as `k`. Leave such keys alone; new keys you add are plain.
- Output formatting is the SDK's (4 space indent, UTF-8, one trailing newline).

## Errors

A module that raises, has no `apply`, or asks for an unknown key/tab stops the whole run with exit code 1 and a message naming the module.
