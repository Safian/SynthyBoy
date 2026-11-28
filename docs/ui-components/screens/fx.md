# FX screen

**Tab:** FX (`fx`, function key FX): one tab body. **Picture:** `../../screens/fx.jpg`.

## What it shows

`fxNativeEditorSurface` (0 0 1280 628) holds the whole editor: the 32 header items (`fxNativeHeader`: power, rack, page, remove), the effect chain (`fxNativeChainRow`, `fxNativeChainCard`, add menu `fxNativeChainAdd`, filtered add, BACK, page buttons, move image), the gallery rows (`fxNativeGalleryRow1/2`, `fxNativeGalleryCard`), three band lanes of a splitter (`fxNativeBandLane`, `fxNativeBandCard`) and the parameter cells of the selected effect (`fxNativeParameterCells`; cell kinds Uni, Bi, Switch, Choice, Number, ReadOnly, each with an inactive twin). The tab adds the markers, the instance picture, the gallery headline `FX Native Gallery Headline` and the sentinel. The effect display is drawn by the plugin ([../fx-display.md](../fx-display.md)). Racks: Main, Bus 1, Bus 2.

## Definitions

`skin/defs_fx.json` (30). Which cells exist for an effect is a table (`shell-mpc/ui/fx_layout.h`, `native_fx_cells.h` `kFxLayouts`, `kFxLayoutLast` layouts); a cell's kind is `kFxHidden`, `kFxDial`, `kFxToggle` or a domain choice (`FxDomain`).

## Data

Blocks of `shell-mpc/native_ui_params.h`: `kFxValueBase`, `kFxKindBase`, `kFxLayout`, `kFxDomainBase`, `kFxBipolar`, `kFxHeader` (`fxheader`, `native_fx_header.h`), `kFxChain` (`native_fx_chain.h`), `kFxBandBase`, `kFxBandMode`, `kFxBandLanes` (`fxbands`, `native_fx_bands.h`), `kFxEditorPower`.

## Tab exit and page loading

On leaving the tab (`FxUi::leftTab`, `shell-mpc/overlay/fx_ui.h:148-155`), the gallery title text is cleared and all four banks are torn down (`setNativeFxCells(0, nullptr)`, `setNativeFxHeader({})`, `setNativeFxChain({})`, `setNativeFxBands({})`). There is no read-only display cache on exit yet. On the display thread, `nativeUiPoll` publishes parameter and chain values.

## Q groups

The rows of four values of the open effect that are on the editor page (first and second row, or three rows without a display).

## C++ owners

`shell-mpc/overlay/fx_ui.h`, `fx_editor_model.h`; `shell-mpc/native_fx_cells.h`, `native_fx_header.h`, `native_fx_chain.h`, `native_fx_bands.h`; `shell-mpc/ui/fx_layout.h`, `fx_display.h`, `fx_gallery_picture.h`. Retired unreferenced C++ symbols: `kElementFx`, `listOpen`, `Layout::defaultRows`, and `Kind::List` (`c188ec59`).
