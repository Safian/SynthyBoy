# FxGallery

> Bound to SynthyBoy's effect types: `fx_gallery.h` includes `synthyboy/fx_types.h`; `drawPicture (pen, sb, type, plot)` has a case per `synthyboy::FxInstanceType`. To reuse it, replace the type numbers and the `drawPicture` switch.

## What it is
The gallery of effect types: a card per type with its name and a small picture of what it does. It fills the FX page while the chain has no effect; one tap on a card asks the owner to add that effect. The gallery knows nothing of the chain.

## Header and types
`shell-mpc/ui/fx_gallery.h`: `FxGallery`. Constants `kMaxTypes` 16, `kHeadH` 44, `kPad` 12, `kGap` 10, `kMaxCardH` 220.

## State fields
No state struct: fill it with
- `setTypes (const int* types, const char* const* names, int n)`: the engine's type numbers (1 and up) and their names (shown in capitals);
- `setHeadline (text)` (default "TAP AN EFFECT TO ADD IT TO THE CHAIN"), `setNote (text)` (shown when there is no type to offer);
Readers: `numTypes()`, `typeAt (i)`, `nameAt (i)`, `cardRect (i)`, `cardRectOfType (type)`, `pictureRect (i)`, `numRows()`, `hasContent()`. Static `drawPicture (...)` is public for tests.

## Size
A one-line headline, then the cards in two rows (the last row centred), each at least 56 px high and wide where the rectangle allows (up to `kMaxCardH` 220 px high).

## Touch and knob behaviour
| Gesture | Change |
|---|---|
| tap on a card (on touch up) | `Index`: `index` = `value` = the TYPE NUMBER of the card |

## Q-Link
Not a Q member.

## Options and variants
Headline and note text.

## Minimal example
```cpp
FxGallery gallery;
const int types[] = { 1, 2, 3 };
const char* const names[] = { "Chorus", "Compressor", "Delay" };
gallery.setTypes (types, names, 3);
gallery.setId (3200);
gallery.place ({ 0, 180, 1280, 548 });
Change c = gallery.touchDown (x, y, nowMs);
c = gallery.touchUp (x, y);
if (c.kind == ChangeKind::Index && c.id == 3200) addEffect (c.index);   // the type number
```

## Used on
`FxFullScreen::gallery` (`shell-mpc/ui/fx_screen.h`), shown while the chain is empty (`FxFullScreen::galleryShown()`). Picture: `docs/user-guide/img/fx-gallery.png`.

## Limits and pitfalls
- At most 16 types.
- The EQ, compressor and distortion pictures use the models of `FxDisplay`, so they show the shape the effect really has.
- The gallery is shown only when `FxFullScreen::editorShown()` is false.
