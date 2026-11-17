# MacroView, MacroCard, SpectrumStrip

## What it is
The Macros page: eight macro cards in two rows of four (BANK 1: macros 1 to 4, BANK 2: 5 to 8), each with its knob (name and value beside the dial) and the chips of the targets it drives; under the cards the live spectrum of the output. `MacroCard` is one card. `SpectrumStrip` is the spectrum as one line with a faint fill on a log frequency axis (no touch).

## Header and types
`shell-mpc/ui/mod_macros.h`: `MacroView : Composite`, `MacroCard : Composite`, `SpectrumStrip : Component`.

## State
`MacroView`: `bankList` (`LedList` "BANK 1: 1-4", "BANK 2: 5-8"), `cards[kNumMacros = 8]`, `spectrum`, `qgroup`; `setBank (b)`, `bank()`, `updateQGroup()`, `turnQ`, `cardsRect()`; `kBarH` 64, `kSpectrumH` 140.
`MacroCard`: `macroIndex` (0 to 7), `knob` (`Knob`, side), `chips[kMaxChips = 4]` (`Chip { text[40] }`), `numChips`, `extraDestCount`, `nameId`; `setMacro (idx, name)` (empty name = "MACRO"), `setValue (val, text)`, `setDestinations (names, shown, total)`, `chipsRect()`, `nameRect()`; `kChipsH` 56.
`SpectrumStrip`: `levels` (`const float*`, `kPoints` = 256 values 0 to 1, 20 Hz to 20 kHz; null draws only the frame).

## Size
`MacroView::minSize()` 1280 x 628; `MacroCard::minSize()` 280 x 190; `SpectrumStrip::minSize()` 400 x 100.

## Touch and knob behaviour
| Gesture | Change |
|---|---|
| drag or turn on a macro knob | `Value` of the knob's id |
| tap on the name beside the dial (when `nameId` is set; 56 px high target) | `Request` with `nameId` (the owner opens a [TextEntry](text-entry.md)) |
| tap on the chips area | `Request` with the card's own `id` (the targets of the macro are wanted) |
| bank list | `Index` 0 / 1 |
A touch on a card makes its bank the active group.

## Q-Link
Two groups: the shown bank (the knobs of its four cards, one frame around the four cards, never through a card) is active; the other bank is on Q 5 to 8 (idle chips on its cards, `QGroup::drawMappedMembers (sb, 1, ...)`).

## Options and variants
`setBank`, the name button, up to 4 target chips plus a "+n" count.

## Minimal example
```cpp
MacroView mv;
mv.place ({ 0, 110, 1280, 628 });
for (int i = 0; i < MacroView::kNumMacros; ++i)
{
    mv.cards[i].knob.setId (600 + i);
    mv.cards[i].nameId = 620 + i;
    mv.cards[i].setId (640 + i);
    mv.cards[i].setMacro (i, macroName (i));
    mv.cards[i].setValue (macroNorm (i), macroText (i));
}
mv.spectrum.levels = spectrum256;             // owner's array, refreshed every picture
Change c = mv.touchDown (x, y, nowMs);
c = mv.touchUp (x, y);
if (c.kind == ChangeKind::Request && c.id >= 620 && c.id < 628) renameMacro (c.id - 620);
```

## Used on
`shell-mpc/overlay/macro_ui.h` (`MacroUi`, `kElementPage` 8; the macro names come from `macro_names.h`). Picture: `docs/user-guide/img/macros.png`.

## Limits and pitfalls
- A macro can be called anything: its colour in the modulation chips comes from the `macro` flag, not from its name.
- Only the first 4 targets are shown as chips; pass the total to get the "+n".
- `SpectrumStrip` draws every picture (the owner refreshes `levels`).
