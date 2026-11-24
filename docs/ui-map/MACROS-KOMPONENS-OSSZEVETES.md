# Macros Képernyő Komponens- és Referenciaegyezés Összevetés

Készült: 2026-11-24  
Hivatkozási szabályok: [AGENTS.md](../../AGENTS.md), [shell-mpc/ui/AGENTS.md](../../shell-mpc/ui/AGENTS.md), [components/AGENTS.md](../../components/AGENTS.md), [docs/ui-components/README.md](../ui-components/README.md), [docs/dsp-knowledge/mpc-display.md](../dsp-knowledge/mpc-display.md)

---

## 1. Megnyitható Képi Bizonyítékok (Szintetikus Előnézeti Kompozitok)

Az alábbi képek a valódi C++ overlay ([render_macros_overlay.cpp](../../tools/render_macros_overlay.cpp) -> `ui::MacroView` / `SpectrumStrip`) és a generált MPC natív skin ([render_ui_screen_macros_preview.py](../../scripts/render_ui_screen_macros_preview.py) -> `MacrosSketch`) kompozitjai:

1. **Kanonikus referencia (MPC XL, Default fixture, mind a 8 kártya keret nélkül)**:  
   [../screens/macros.jpg](../screens/macros.jpg)  
   *(Tükrözve: [docs/user-guide/img/macros.png](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/user-guide/img/macros.png), [docs/user-guide/img/ui_screen_macros_preview.png](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/user-guide/img/ui_screen_macros_preview.png), [docs/ui-components/komponensek/ui_screen_macros_preview.png](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-components/komponensek/ui_screen_macros_preview.png))*
2. **Key 61 Gen1 Bank 1 fókusz (számozatlan 'Q' keret az 1..4 kártyákon)**:  
   [macros-screen-preview-key61.png](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/macros-screen-preview-key61.png)
3. **Key 61 Gen1 Bank 2 fókusz (számozatlan 'Q' keret az 5..8 kártyákon)**:  
   [macros-screen-preview-bank2.png](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/macros-screen-preview-bank2.png)
4. **Sound design named fixture (MPC XL, egyedi nevek + eltérő értékek)**:  
   [macros-screen-preview-named.png](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/macros-screen-preview-named.png)
5. **Komponens- és referencia-összevetési grafika**:  
   [macros-component-comparison.png](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/macros-component-comparison.png)

*(Fontos: ezek szintetikus offline fixture-ökből renderelt tesztképek, nem fizikai hardveren készült képernyőmentések vagy mérések.)*

---

## 2. Tulajdonosi Elrendezés (8 Kártya Alul Egy Sorban + Felső Nagy Spektrum)

1. **Képernyőszerkezet**:
   - **Tiszta fejléc (`{0, 0, 1280, 64}`)**: csak `"MACROS"` cím és `y = 62` elválasztóvonal. A felső `BANK 1 / BANK 2` váltó teljesen el lett távolítva mind a C++ overlayből, mind a natív skin szerződésből.
   - **Felső Stúdió Spektrum (`{8, 72, 1264, 290}`)**: összefüggő teljes szélességű téglalap a felső részen (`20 Hz .. 20 kHz` log tengely, `-60 .. 0 dB`, valódi `SpectrumStrip`).
   - **Alsó 8-as kártyasor (`{8, 382, 1264, 238}`)**: mind a 8 `152 × 238 px`-es `MacroCard` egymás mellett látható (`x = 8, 166, 324, 482` Bank 1, `12 px` bankköz, `646, 804, 962, 1120` Bank 2). Nincs rejtett bank, lapozás vagy új UI bankváltó.
2. **Kompakt vertikális kártyabelrendezés (152 × 238 px)**:
   - A dial aránya továbbra is **szigorúan négyzetes 90 × 90 px** saját unipolar kék (`#3568c0`, `play_sub_level_90.png`, nem torzított, nem `knob_uni`).
   - Alatta középre igazítva sorakozik az érték (`20 px`, `{8, 96, 136, 22}`), az interaktív névgomb (`144 × 56 px` hitbox, `136 × 24 px` felirat + adaptív aláhúzás) és a `48 px`-es `TARGETS` akciógomb (`144 × 56 px` hitbox).
   - Minden interaktív zóna (dial, névgomb, `TARGETS` gomb) `>= 56 px` és átfedésmentes.
3. **Fix hardveres Q-Link routing + Gen1/Gen2 vizuális fókusz**:
   - Mind 4-knobos (Key 61), mind 16-knobos (MPC XL) eszközön **fix** a slot-hozzárendelés (`Q 1..4 = macro.1..4`, `Q 5..8 = macro.5..8`, `Q 9..16 = kNone`).
   - Kártyaérintés vagy 4-knobos tekerés csak a vizuális fókuszbankot jelzi (`view_.setBank(0 | 1)`), soha nem cseréli fel a Bank 1/2 targettáblát.
   - Gen1 Key 61: egyetlen sárga bankkeret az aktív 4-es bank körül + egyetlen számozatlan `'Q'` chip; MPC XL (`wideDevice() == true`): keret nélkül; Gen2 (`SYNTHYBOY_MPC_QFRAME_ENABLED == 0`): keret és `'Q'` jel kizárva fordítási időben.

---

## 3. Részletes Komponens- és Geometria-Összevetés

| Tulajdonság / Elem | Közös Szerződés (`sizes.json` -> `macro_card_contract.h`) | Mai Valós Generált Skin + C++ Overlay | Megfelelőség |
|---|---|---|---|
| **Kártyaméret és rács** | `152 × 238 px`, `y = 382`, `gap = 6 px`, `bank_gap = 12 px` | 8 egyforma kártya alul egy sorban (`8..1272`) | **Teljes egyezés** |
| **Dial geometria és szín** | `90 × 90 px` @ `{31, 4}`, `native_knob_rect` `{29, 2, 94, 92}`, `dial_rect` `{0, 0, 152, 118}` | Saját `play_sub_level_90.png` 127 frame (`#3568c0` ív, fehér mutató) | **Arányos 1:1 négyzet**, nincs gyári fallback |
| **Értékkiírás (Value)** | `{8, 96, 136, 22}`, `20 px` középre igazítva | Natív host skin rajzolja (`macroSubLevel` Value) | **Egy érték elv**: nulla duplázás |
| **Makró név (Name)** | Felirat `{8, 136, 136, 24}`, hitbox `{4, 120, 144, 56}` | C++ Overlay rajzolja középre igazítva + adaptív aláhúzással | **Tiszta apertura**, `>= 56 px` érintés |
| **Target gomb** | `48 px` gomb `{4, 178, 144, 56}` hitboxban (`TARGETS`) | Natív `action_button`, `kMacroTarget -> kPageTargets (50)` | **Átfedésmentes**, értéket nem módosít |
| **Fejléc (Header)** | `"MACROS"` `{14, 4, 260, 56}`, keyline `y = 62` | Csak cím + vonal, bankváltó törölve | **Tiszta fejléc**, zéró kövület |
| **Spektrum (SpectrumStrip)** | `{8, 72, 1264, 290}`, 256 log pont (`20 Hz..20 kHz`) | C++ `SpectrumStrip::drawLive`, teljes felső sáv | **Nagy stúdió spektrum** alulról felszabadítva |
| **Q-Link & Policy** | Fix Bank 1 (`1..4`) / Bank 2 (`5..8`), Gen1 `Q` keret / Gen2+XL keret nélkül | `qSlots_.publish(v, 2, 0, ...)` + `QGroup` / `QVisualPolicy` | **Fix hardware routing**, nulla számjel |

---

## 4. Célzott Tesztelési Összegzés

- **Natív komponens, skin és előnézeti pipeline** (`test_native_macro_card.py`, `test_ui_screen_macros.py`, `test_render_ui_screen_macros_preview.py`, `test_native_macros.py`, `gen-macro-contract.py --check`): igazolja az `152 × 238 px` 8-as egy soros geometriát, középre igazított érték/név/gomb elrendezést, fejléc-tisztaságot, lossless RGB `overlay_bg_macros.png` tömörítést (`12,329` byte) és a Key 61 / XL Q-keret szabályait.
- **C++ Overlay és Routing** (`macro_names_ui_host`, `mod_ui_host` Gen1 & Gen2 compile check, `qslots_host`, `ui_gallery_host --test`, `ui_gallery_extra_host --test`, `layout_lint_host`): igazolja a nem átfedő `56 px` hitboxokat, hosszú makrónevek változatlan dial-geometriáját (`Knob::geom()` 90 px), fix `Q 1..4 = macro.1..4` és `Q 5..8 = macro.5..8` routingot, Key 61 vizuális fókuszváltást érintésre és tekerésre, valamint `0` pixel eltérést az exportált háttér + élő kompozit és az all-in-one render között.
