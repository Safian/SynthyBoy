# ModRow Native Component

Host-native MPC compound component for modulation matrix rows. [Historical Mod screen](../../../docs/screens/mod_old.jpg).

## Public API (`mod_row.py`)

- `build_cell_definition(kind, width, height, *, image_name=None)`:
  Builds local component definitions for row cells:
  - `"bar"`: full 56 px high Slider input hitbox (`sliderType`: `TrackAndThumb`, `isBipolar`: `True`, touch target >= 56 px) with a 5 px bipolar rail, a 16 px round thumb and signed readout.
  - `"toggle"`: compact button for POLARITY (`UNI`/`BI`) and ROUTE (`ON`/`OFF`), single value label without repeated header.
  - `"list"` / `"group"`: shared Selector dropdown with centered value and chevron.
  - `"action"`: MORE button with instant toggle action.
- `build_mod_row_definition(skins_dir=None, *, layout="normal", row_type=None)`:
  Constructs the compound row definition containing all 6 interactive controls (`AMOUNT`, `POLARITY`, `ROUTE`, `SOURCE`, `TARGET`, `MORE`) with individual 56 px touch hitboxes.
- `build_mod_row_definitions(skins_dir=None)`:
  Exports the canonical set of definitions required by ModRow compounds (`modNative_bar_*`, `modNative_toggle_*`, `modNative_list_*`, `modNative_action_*`, `modNativeRowNormal`, `modNativeRowScroll`, and canonical `modRow`).
- `build_mod_row_instance(name, row_idx, rect, *, row_type="modNativeRowNormal", ...)`:
  Creates an instance entry binding the continuous amount value, route state, polarity, source and target navigation handles, and MORE action handle.
- `write_assets(dest_dir=IMG_DIR)`:
  Generates all verified PNG assets: dropdown selectors, arrows, buttons, 56 px slider track/revealed/thumb art, fullwidth before/after comparisons, and standalone previews.
- `render_standalone_mod_row(...)`:
  Authentic offline renderer generating fullwidth (1280 px) row previews with zero right-edge clipping.
- `extract_historical_before_crop(source_path)`:
  Extracts the real historical first-row crop from `docs/screens/mod_old.jpg`, preserving the original yellow Q-Link bank frame and numbered Q badges.

## Verification & Limitations

- **Touch Hitbox**: Every interactive control (Slider, Toggles, Selectors, MORE) has a height >= 56 px meeting MPC Rule 3.
- **Handle Independence**: 7 matrix row instances resolve their handles independently without cross-row collision.
- **Known Limitation - Live Deflection**: Live modulation deflection animation on the base row requires an active deflection handle not yet present in host skin data; dynamic modulation arc animation remains on the C++ overlay layer.
- **Separate Overlay Elements**: `ModSourceBoard` (quick-add chips) and `ModSlotDetail` (full slot parameter page) remain on the overlay plane.
- **Static Preview Distinction**: Offline previews and Sketch adapters render synthetic fixtures; hardware input forwarding must be verified on Akai MPC Key 61 and MPC XL devices.
