# Pictures of the Env / Filter / LFO plan

| Picture | What it shows |
|---|---|
| `used_selector_empty.png`, `used_selector_partly.png`, `used_selector_all.png` | the item selector of the merged Env / LFO tabs (step 1): empty preset, partly used, all used |
| `scalar_curve_velo_identity.png` | NOTE / VELO curve editor (step 5), VELO, identity curve, nothing routed ("NOT ROUTED" hint), legato off |
| `scalar_curve_velo_3_points.png` | VELO, a 3 point curve with a bent first segment, the middle point selected, three slots in USED BY |
| `scalar_curve_note_legato_bent.png` | NOTE, 4 points with bends, LEGATO on with the read-only GLIDE chip, four rows shown of eight slots ("+ 4 more") incl. an AUX use |
| `scalar_curve_note_64_points.png` | NOTE, the 64 point limit, SNAP on, point 21 selected |

Rendered with `python3 scripts/render-ui-gallery.py --build --only-prefix ScalarCurve` (`ui_gallery_host`, function `renderScalarCurves`), 1280 x 556 (the Filter tab body).
