# FxSlotCard, FxChainBar, FxBandTile, FxBandBoard

> Bound to SynthyBoy's effect types: `fx_chain_bar.h` includes `synthyboy/fx_types.h` (the card pictures switch on `synthyboy::kFxType...` and on `kNumFxInstanceTypes + 0 .. 2` for the splitters); `fx_band_board.h` includes `fx_chain_bar.h`. To reuse, replace the picture switch of `FxSlotCard` and the type numbers; the behaviour is generic.

## What it is
`FxSlotCard`: one effect in the chain: a type picture, the name, a power zone and a rack badge ("M", "B1", "B2"); or a "+" card; or a back card. `FxChainBar`: the top bar with up to 16 cards, a "+" card and quiet "+" places; drag a card to reorder. `FxBandBoard`: the band lanes of a splitter: a tile (name, range, info) and the chain of the band as cards plus "+ ADD", one lane per band. Use them to show an ordered chain of modules.

## Header and types
`shell-mpc/ui/fx_chain_bar.h`: `FxSlotCard` (`Action { kSelect = 1, kTogglePower = 2, kMove = 3, kAdd = 4, kBack = 5 }`, `State`), `FxChainBar : Composite`. `shell-mpc/ui/fx_band_board.h`: `FxBandTile` (`Action { kOpen = 1 }`, `State`), `FxBandBoard : Composite` (`Lane`).

## State fields
`FxSlotCard::State`: `slot` (0), `type` (0, an FX instance type or a splitter kind), `name` (`char[24]`), `on` (true), `rack` (0 Main, 1 Bus 1, 2 Bus 2), `selected`, `isPlus`, `faint` (a quiet "+"), `empty`, `selectOnPower` (true: a tap on the power zone also selects), `isBack`, `tag` (`char[20]`, back card: the open band).
`FxChainBar`: `cards[kMaxSlots = 16]`, `plusCard`, `backCard`, `spare[16]`; `setNumActiveSlots (n)`, `numActiveSlots()`, `setBackShown (bool)`, `assignIds (base)` (cards `base + i`, plus `base + 16`, back `base + 17`).
`FxBandTile::State`: `name`, `range`, `info`, `colour` (index into the flow colours), `bypassed`, `idle`.
`FxBandBoard`: `lane[3]` each `{ tile, cards[13], add }`; `setShape (bands, cardsPerBand)`, `assignIds (base)` (lane b: cards `kLaneIds * b + 0..12`, tile `kOpenId` 14, add `kAddId` 15 within the lane), `decode (id, band, what)`, `numBands()`, `numCards (b)`.

## Size
`FxSlotCard::minSize()` 64 x 56; `FxChainBar::minSize()` 400 x 68 (cards of `kCardW` 124 px, the page uses a 88 px bar `FxFullScreen::kBarH`); `FxBandTile` 120 x 56; the band board uses a tile of `kTileW` 190 px.

## Touch and knob behaviour
`FxSlotCard` changes are `Index` with `id` = the card's id and `index` = the ACTION:
| Gesture | `index` | `value` |
|---|---|---|
| touch down on the power zone | `kTogglePower` | 1.0 on, 0.0 off (the card flips its own `state.on`) |
| tap on the card | `kSelect` | 0 |
| tap on a "+" card | `kAdd` | 0 |
| tap on the back card | `kBack` | 0 |
| horizontal drag over 24 px (past 12 px a drag is a candidate) | `kMove` | the x of the finger on touch up: the owner finds the target place |
`FxBandTile`: a tap gives `Index` with `kOpen`. `FxBandBoard` hands the ids on: use `decode (id, band, what)`.

## Q-Link
Not Q members.

## Options and variants
`selectOnPower` false for the cards of a band lane (a select opens the band); `faint` for the free places; `setBackShown` for the chain of a band.

## Minimal example
```cpp
FxChainBar bar;
bar.setNumActiveSlots (3);
bar.assignIds (3000);
bar.place ({ 0, 110, 1280, 88 });
bar.cards[0].state.type = kFxTypeDelay;        // synthyboy::FxInstanceType, the owner's
bar.cards[0].state.on = true;
setText (bar.cards[0].state.name, sizeof bar.cards[0].state.name, "Delay");
Change c = bar.touchDown (x, y, nowMs);
c = bar.touchUp (x, y);
if (c.kind == ChangeKind::Index && c.id >= 3000 && c.id < 3016)
    handleAction (c.id - 3000, c.index, c.value);   // slot, Action, value
```

## Used on
`FxFullScreen::chainBar` and `slotEditor.bands`, `shell-mpc/overlay/fx_ui.h` (`kChainBase` 3000, `kBandBase` 3100). Pictures: `docs/user-guide/img/fx-chorus.png`, `fx-splitter.png`, `fx-band.png`.

## Limits and pitfalls
- A power touch on a card acts on touch DOWN and flips `state.on` inside the card: write the real value back next update or it drifts.
- A reorder is the owner's job: the card only reports the x.
- At most 16 cards, 13 per band.
