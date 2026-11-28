# MIXER STRIP (catalogue design, 160 px)

One channel strip composed from existing recipes; no screen, skin or overlay uses it yet (the live Mixer is described in [../ui-map/mixer.md](../ui-map/mixer.md)). The Mixer page is just the strips, so the strip has no tab bar, no peek and no docked editor. Source: `components/native/mixer_strip/mixer_strip.py` (section "CATALOGUE DESIGN"), examples: `components/native/mixer_strip/catalog.json`, test: `scripts/tests/test_native_mixer_strip_design.py`. Bottom row sliders: [hslider.md](hslider.md).

## Structure, top to bottom (all gaps `mixer_strip.design.gap` = `header.gap` = 8, padding `header.padding` = 8 on all sides)

| Zone | Recipe | Size |
|---|---|---|
| Name + POWER | name: Label (`font.value.large`, `text.value`) at the left; POWER: IconButton, ghost look (no fill, no keyline), green when on, plain icon colour when off | row 32 (`mixer_strip.design.row` = `height.small`) |
| S and M | Button recipe (legacy look), 32 px, each (inner width - gap) / 2 wide, one row | 32; on every channel strip, NOT on MASTER |
| Destination | dropdown (`selector.render_dropdown_variant`: value only, centred) | 32 |
| Fader + meter | scale (`fader.render_fader_scale`) and groove left, contrasty thumb, the stereo meter (`meter_block.render_stereo_meter`: two bars L/R, clip lamps) right | takes the rest of the height |
| Readout | box (`surface.control`, `field.border`), `font.value`, `text.value`: `25.0%` or `-12.0 dB` | `mixer_strip.design.readout` 24 |
| Bottom | variant A: old rotary cells PAN, B1, B2 (56 px cells, `voice.surface` / `voice.border`, `level.render_dial_labelled`); variant B: three HSliders (40 px, 8 gap) | 176 / 136 |

Strip: `mixer_strip.design.width` 160 x `mixer_strip.height` 606, panel `surface.panel`, keyline `button.border`, radius `radius.control` (the old strip card). Inner width 144. The fader gets the height the other zones leave (254 px with variant A, 294 px with B, 442 px on MASTER). If a strip has no S/M (MASTER) the row is gone: the dropdown follows the name row directly. MASTER has no PAN, B1 or B2 in the engine, so it has only one picture (no A/B choice).

## S and M

Every channel strip has them, MASTER is the exception (it has no S/M row at all). States: off (normal), solo active (text and keyline `accent.cyan`), mute active (`accent.red`), the colours of the existing solo_mute faces; disabled = `style.dim_artwork`. The catalogue shows both active at once.

## States

- POWER off: name and POWER stay; everything below takes the shared disabled look (`style.dim_artwork`), POWER is unlit. S and M stay visible.
- Meter: `PeakL`, `PeakR` fill the bars (green `meter.peak`, amber from -6 dBFS, red from -1 dBFS: the meter recipe's colours).

## What each look contributed

- Old mixer (base): panel and strip card, the groove with the tick scale and the cyan unity mark, the level readout box, the rotary PAN/B1/B2 cells (variant A), the S/M faces.
- Newer mixer: the name at the top with POWER, the contrasty thumb, the stereo meter, the horizontal sliders (variant B, a choice for the owner), the S and M row under the name.

## Tokens

New: `mixer_strip.design.width` 160 (the live Mixer's 10 strips are 115 px: 10 x 160 does not fit 1280; the design is one strip), `.padding`/`.gap` (refs `header.*`), `.row` (ref `height.small`), `.scale_width` 56, `.meter_gap` 8, `.readout` 24, `.cell_gap` 4, `.dial` 40, `.thumb` (36 x 20 contrasty vertical-fader thumb); `hslider.*`. New colours (measured from the current skin picture `docs/screens/mixer.jpg`, the old picture shows a dark thumb `#121833` with a white line, contrast to the groove 1.14:1; the newer thumb 12.9:1): `fader.thumb.fill` #c2d1e8, `fader.thumb.edge` #edf4fa, `fader.thumb.line` #181f39. Reused: `mixer_strip.height`, `mixer_strip.meter.width`, `mixer_strip.fader.*` scale pads, `mixer_strip.control.height_56`, `meter.stereo.*`.

## Not drawn from existing recipes

The meter is the recipe's green/amber/red, not the purple-to-cyan of the newer picture; the MASTER scale is the existing skew dB scale (+6, 0, -12, -24, -60), not +6 .. -48.

## Add an example

Add an object to `mixer_strip.examples` in the catalog.json (`id`, `file`, `alt`, `caption`, `section`, `name`, `power`, `scale`, `level`, `level_text`, `meter` [L, R], `bottom` = `knobs`/`sliders`/`none`; optional `destination`, `buttons` {solo, mute}, `sends`). Render and generate from the repo root:

```sh
PYTHONPATH=. python3 -c "from components.native.mixer_strip import mixer_strip as s; s.render_catalog()"
```
