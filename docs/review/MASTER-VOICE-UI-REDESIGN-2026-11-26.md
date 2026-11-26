# Master és Voice natív UI: felülvizsgálat és újratervezés (2026-11-26)

Képek (szintetikus előnézetek, nem hardverfotók; a generált skin + a C++ overlay-jelenet):
[Master MAIN](../screens/master.jpg), [Voice](../screens/voice.jpg). Régi referencia: `docs/screens/master_old.jpg` (változatlan).
A TUNING & MPE oldal és a Key 61-generáció: `python3 scripts/render_ui_screen_master_preview.py --out-dir <ideiglenes mappa>`.

## Mi volt a baj (a Key 61-es Master felvétel és a natív Voice alapján)

- **Nincs csoportosítás.** A natív átállás után a blokkok kerete eltűnt (az overlay rajzolta), csak 13 px-es, szétszórt szekciófeliratok maradtak (`static-native-captions.json`). Az OUTPUT, PITCH, VOICES, GLIDE vezérlői összefolytak; a Voice alsó három blokkja is keret nélkül lebegett.
- **Olvashatatlan kapcsolók.** A Master ActiveText kapcsolói 11 px-es alapbetűvel készültek (LIMIT, LEGATO, ALWAYS…): a kikapcsolt állapot szinte láthatatlan volt.
- **Apró számmezők.** TRANSPOSE, BEND, POLY, PAD % a 34 px-es dobozú, 10 px-es címkéjű oldalsó név–érték mező volt, a cellák többi része üres.
- **Üres sávok, szétszórt elrendezés.** A cellák 112–124 px magasak, de a tartalom 56 px; PITCH alatt és VOICES mellett nagy üres terület; a Voice rácsa 236 px magas, a lépéssáv és az alsó blokkok között üres sáv.
- **Két geometriaforrás.** A Master natív téglalapjai a `components/sizes.json`-ban (`screen.master_controls.rect.rect.N`, vászonkoordinátában) és a C++ `MasterScreen::layout()` számításában külön éltek; csak kézzel egyeztek.
- **A sentinel a vezérlők közé esett.** A háttér-sentinel (Master 960,386; Voice 792,574) a régi elrendezés egy szabad pontja volt; az új elrendezésben vezérlő alá kerülne.

## Leltár (kulcs, típus, tartomány, alapérték, Q-Link, új téglalap tabkoordinátában)

Q-Link: MAIN-on OUTPUT Q1–4, PITCH Q5–8, VOICES Q9–12, GLIDE Q13–16 (a VOICE OPTS csoport tag nélkül, érintéssel; ld. TODO „Master GLIDE a VOICE OPTS helyett”). TUNING & MPE: SCALE Q1–3, TUNING Q5–8, MPE Q9–11, MPE Y Q13–15. Voice: STEP Q1–4 (STEP, ROW, LENGTH, VALUE), RANDOM Q5–8, SCALING Q9–12, TARGETS Q13–16; NOISE érintéssel. A sorrend nem változott.

A „típus” a spec widgetje; a `MASTER_NUMBER_FIELDS` tagjai (TRANSPOSE, BEND UP/DOWN, POLY, VEL TRACK, PAD %, A4 HZ, MPE BEND) stacked számmezőként jelennek meg.

| oldal | kulcs | felirat | típus | tartomány | alap | Q | téglalap |
|---|---|---|---|---|---|---|---|
| main | `master.volume` | VOLUME | dial_bi | -60 .. 6 dB | -12 dB | Q1 | 132 98 150 152 |
| main | `master.stereo_spread` | SPREAD | dial_uni | 0 .. 100 percent | 100 percent | Q2 | 286 98 150 152 |
| main | `master.stereo_routing` | STEREO | dial_uni | 0 .. 100 percent | 100 percent | Q3 | 440 98 150 152 |
| main | `master.voice_override` | VOICE OVERRIDE | selector | Kill / Steal | Kill | Q4 | 598 128 150 80 |
| main | `quality.no_smoothing` | NO SMOOTHING | toggle | Off / On | Off | érintés | 380 256 180 56 |
| main | `quality.oversample_4x` | OVERSAMPLE 4X | toggle | Off / On | Off | érintés | 566 256 186 56 |
| main | `master.tune` | TUNE | dial_bi | -100 .. 100 cents | 0 cents | Q5 | 784 98 116 152 |
| main | `master.transpose` | TRANSPOSE | dial_bi | -48 .. 48 semitone | 0 semitone | Q6 | 904 98 116 152 |
| main | `master.bend_range` | BEND UP | dial_uni | 0 .. 24 semitone | 2 semitone | Q7 | 1024 98 116 152 |
| main | `master.bend_down_range` | BEND DOWN | dial_uni | 0 .. 24 semitone | 2 semitone | Q8 | 1144 98 116 152 |
| main | `master.bend_down_link` | LINK | toggle | Off / On | On | érintés | 1024 256 236 56 |
| main | `master.polyphony` | POLY | dial_uni | 1 .. 32 voices | 16 voices | Q9 | 20 360 176 80 |
| main | `master.legato` | LEGATO | toggle | Off / On | Off | Q10 | 204 384 176 56 |
| main | `master.poly_legato` | POLY LEG | toggle | Off / On | Off | Q11 | 388 384 176 56 |
| main | `voice.phase_legato` | PHASE LEG | toggle | Off / On | Off | Q12 | 572 384 180 56 |
| main | `master.velocity_track` | VEL TRACK | dial_bi | -100 .. 100 percent | 100 percent | érintés | 20 446 176 80 |
| main | `master.voice.same_note` | SAME NOTE | selector | Steal / Stack / Ignore | Steal | érintés | 204 446 268 80 |
| main | `master.voice.steal_priority` | STEAL | selector | Newest / Oldest / Highest / Lowest … | Newest | érintés | 480 446 272 80 |
| main | `shell.pad_pressure` | PAD PRESS | toggle | Off / On | Off | érintés | 20 556 176 56 |
| main | `shell.pad_pressure_amount` | PAD % | number | 0 .. 100 % | 35 % | érintés | 204 532 176 80 |
| main | `master.portamento.time` | GLIDE | dial_uni | 0 .. 8000 ms | 0 ms | Q13 | 1028 360 116 152 |
| main | `master.portamento.slope` | SLOPE | dial_bi | -8 .. 8 none | 0 none | Q14 | 1148 360 112 152 |
| main | `master.portamento.always` | ALWAYS | toggle | Off / On | Off | Q15 | 1028 556 116 56 |
| main | `master.portamento.scaled` | SCALED | toggle | Off / On | Off | Q16 | 1148 556 112 56 |
| tuning | `master.scale.type` | SCALE | selector | Chromatic (off) / Major / Natural Minor / Harmonic Minor … | Chromatic (off) | Q1 | 20 102 500 80 |
| tuning | `master.scale.key` | KEY | selector | C / C# / D / D# … | C | Q2 | 528 102 300 80 |
| tuning | `master.scale.mode` | MODE | selector | Conform / Mark only | Conform | Q3 | 836 102 424 80 |
| tuning | `master.tuning.a4` | A4 HZ | dial_uni | 400 .. 480 Hz | 440 Hz | Q5 | 20 240 300 80 |
| tuning | `master.tuning.scale` | TUNING | selector | 12-TET / Pythagorean / Just intonation / User file | 12-TET | Q6 | 328 240 400 80 |
| tuning | `master.tuning.mts_esp` | MTS-ESP | toggle | Off / On | Off | Q7 | 736 264 256 56 |
| tuning | `master.tuning.mts_note_on_only` | NOTE-ON | toggle | Off / On | Off | Q8 | 1000 264 260 56 |
| tuning | `master.mpe.on` | MPE | toggle | Off / On | Off | Q9 | 20 404 404 56 |
| tuning | `master.mpe.bend_range` | MPE BEND | dial_uni | 1 .. 96 semitone | 48 semitone | Q10 | 432 380 404 80 |
| tuning | `master.mpe.xyz_macro` | XYZ TO MACROS | selector | Off / XYZ -> Macro 1, 2, 3 / YZ -> Macro 1, 2 | Off | Q11 | 844 380 416 80 |
| tuning | `master.mpe.y_modwheel` | Y TO WHEEL | toggle | Off / On | Off | Q13 | 20 510 404 56 |
| tuning | `master.mpe.bend_to_x` | BEND TO X | toggle | Off / On | Off | Q14 | 432 510 404 56 |
| tuning | `master.mpe.y_bipolar` | Y BIPOLAR | toggle | Off / On | Off | Q15 | 844 510 416 56 |
| voice | `voicectl.rand.pan` | PAN | dial | 0 .. 100 percent | 0 percent | Q5 | 20 406 116 146 |
| voice | `voicectl.rand.detune` | DETUNE | dial | 0 .. 100 cents | 0 cents | Q6 | 140 406 116 146 |
| voice | `voicectl.rand.cutoff` | CUTOFF | dial | 0 .. 100 percent | 0 percent | Q7 | 260 406 116 146 |
| voice | `voicectl.rand.env` | ENV | dial | 0 .. 100 percent | 0 percent | Q8 | 380 406 116 146 |
| voice | `voicectl.rand.detune10x` | 10x DETUNE | toggle | Off / On | Off | Q9 | 20 558 236 56 |
| voice | `voicectl.scale.env` | ENV TIME | dial | 25 .. 400 percent | 100 percent | Q10 | 528 484 170 132 |
| voice | `voicectl.scale.lfo` | LFO TIME | dial | 25 .. 400 percent | 100 percent | Q11 | 706 484 170 132 |
| voice | `voicectl.scale.lfo_mode` | LFO MODE | choice | Percent / Rate | Percent | Q12 | 528 400 348 80 |
| voice | `voicectl.osc.a` | OSC A | toggle | Off / On | On | Q13 | 908 402 172 64 |
| voice | `voicectl.osc.b` | OSC B | toggle | Off / On | On | Q14 | 1088 402 172 64 |
| voice | `voicectl.osc.c` | OSC C | toggle | Off / On | On | Q15 | 908 472 172 64 |
| voice | `voicectl.osc.sub` | SUB | toggle | Off / On | On | Q16 | 1088 472 172 64 |
| voice | `voicectl.osc.noise` | NOISE | toggle | Off / On | On | érintés | 908 542 172 64 |
| voice | `voicectl.length` | LENGTH | number | 1 .. 8 none | 8 none | Q3 | 788 4 200 56 |

Nem vezérlő, de élő/állandó elem: CLIP (jelzés + törlés, 20 98 100 56), a sztereó csúcsmérő (20 158 100 154), LIMIT (132 256 140 56), a hangszám-sáv „n / POLY VOICES” (388 550 364 44), a GLIDE görbe (overlay, 784 360 236 252); Voice: a lépésrács (268 94 996 206), a kiválasztott lépés sávja (20 306 1240 48), a ROW lista (20 98 240 208), STEP (560 4 220 56), VALUE (996 4 228 56).

Fontosság (használati gyakoriság szerint): VOLUME, POLY, GLIDE idő, LEGATO, BEND és TRANSPOSE a leggyakoribb (ezek a Q-sorok első tagjai és a legnagyobb vezérlők); SPREAD/STEREO, VEL TRACK, PAD PRESS közepes; QUALITY, SAME NOTE, STEAL, VOICE OVERRIDE ritka (alsó sor vagy érintéses). A TUNING & MPE ritkán állított, ezért alszint.

## Az új terv és miért

**Közös szabályok mindkét oldalon**
- Minden blokk egy **címkézett natív panel** (új közös komponens: `components/native/panel`): `surface.panel` kitöltés, 1 px `panel.border` keret, 16 px-es cím a kártya bal felső sarkában. Képfájl nélkül, csak színkitöltésből (nincs textúramemória), érintést és fókuszt nem fog; a fül listájában elöl áll, így minden vezérlő és jelölő fölötte van. A régi 13 px-es szekciófeliratok törölve.
- **Egy panel = egy Q-csoport**, a panelek olvasási sorrendje = a hardveres Q-Link sorrend. A 4 vezérlő/sor elv: minden csoport első sora a Q-tagoké, alatta a csak érintéses kiegészítők.
- **Betűskála:** tárcsa- és mezőfelirat 15 px, érték 20–22 px, kapcsoló 19 px (a Voice TARGETS mintája), panelcím 16 px.
- **Számmező = stacked név–érték** (új változat a közös `name_value`-ban): név felül, alatta legalább 56 px magas, 132 px széles doboz nagy értékkel; tárcsasorban a doboz a tárcsa középvonalán ül.
- **Lista = címkézett választó** (`build_titled_selector`): cím felül, 56 px-es választó alatta, a Voice LFO MODE mintájára.
- Minden érintési cél legalább 56 px magas; egy oldalon nincs átfedés (teszt).

**Master MAIN (2 × 2 rács, Q1–16 olvasási sorrendben)**
- OUTPUT (bal felső, 756 × 252): bal oszlopban CLIP a csúcsmérő fölött (a mérő 128-ról 154 px-re nőtt, a dB-skála újraszámolva); a Q-sor VOLUME, SPREAD, STEREO tárcsa + VOICE OVERRIDE választó; alattuk a csak érintéses sor: LIMIT, „QUALITY”, NO SMOOTHING, OVERSAMPLE 4X.
- PITCH (jobb felső, 500 × 252): TUNE tárcsa, TRANSPOSE, BEND UP, BEND DOWN egy sorban; LINK a hajlítómezők alatt; LINK bekapcsolva a BEND egy mező a két cella helyén (a feltételes kapuk változatlanok).
- VOICES (bal alsó, 756 × 294): 1. sor POLY, LEGATO, POLY LEG, PHASE LEG (Q9–12); 2. sor VEL TRACK, SAME NOTE, STEAL; 3. sor PAD PRESS, PAD % (csak PAD PRESS mellett), és az élő hangszám-sáv.
- GLIDE (jobb alsó, 500 × 294): a görbe balra, teljes magasságban (kijelző bal, vezérlők jobb – az FX Distortion mércéje), jobbra GLIDE és SLOPE tárcsa, alattuk ALWAYS, SCALED.

**Master TUNING & MPE (alszint, három teljes szélességű sor)**
- SCALE: SCALE (20 skála), KEY, MODE választó egy sorban; a fejlécben a skálabevitel gombja (változatlan).
- TUNING: A4 HZ mező, TUNING választó, MTS-ESP és NOTE-ON kapcsoló egy sorban.
- MPE: MPE, MPE BEND, XYZ TO MACROS; alatta Y TO WHEEL, BEND TO X, Y BIPOLAR (MPE kikapcsolva a függő vezérlők láthatók maradnak, de inaktívak – a meglévő kötés változatlan).
A korábbi két oszlopos TUNING/MPE elrendezés helyett a sorok követik a Q-csoportokat, és nem marad félig üres blokk.

**Voice (Voice Control)**
- Fejléc: VOICE CONTROL cím, STEP választó, LENGTH, a kiválasztott lépés értéke (nagyobb betűvel).
- SEQUENCE panel a teljes szélességben: ROW lista, a nyolc lépésoszlop (a rács 236-ról 206 px-re tömörítve, a lépésválasztó lábmező így is 76 px), alatta a kiválasztott lépés hat értékének sávja.
- RANDOM (PAN, DETUNE, CUTOFF, ENV tárcsa; 10x DETUNE alatta), SCALING (LFO MODE, ENV TIME, LFO TIME), TARGETS (OSC A/B, OSC C/SUB, NOISE; a „PAN / DETUNE” hatókör a panelcím mellett) egymás mellett. A 10x DETUNE érintése a SCALING Q-csoportot választja (ahogy a Q-tagság eddig is volt).

**Egy geometriaforrás**
- Master: `scripts/native_master/master_layout.json` (tabkoordináta) a skin és a C++ közös forrása; `scripts/gen-master-native-layout.py` generálja a `shell-mpc/ui/master_native_layout.h`-t, a `MasterScreen::layout()` és `blockRect()` ebből dolgozik. A `components/sizes.json` régi `screen.master_controls.*` kulcsai törölve. Teszt bukik, ha a fejléc elavult.
- Voice: a meglévő `voice_layout.json` kibővült a panelekkel és a lépéssávval (a `sizes.json` `screen.voice_tab.rect.*` kulcsai törölve); a `voice_native_layout.h` belőle generálódik.
- Sentinel: Master 600,74 (az OUTPUT/SCALE panel címsávjában, a címtől jobbra; a sentinel színe közel van a panelhez, így nem látszik), Voice 509,500 (a RANDOM és a SCALING közti résben, a vászonhoz közeli szín). A `backgrounds_manifest.h` újragenerálva.

**Ami nem változott:** paraméterkulcsok, hangmotor, Q-tagság és -sorrend, feltételes elrejtés/inaktiválás (LINK, MPE, PAD PRESS), a natív kötések és kapuk, a C++ adapterek szerepe. Az overlay továbbra is csak a GLIDE görbét, a Voice élő lépéspontjait és a Q-keretet rajzolja; a natív alap-megjelenés (panelek, címek, vezérlők) az overlaytől független.

## Iteráció

1. kör: a Voice RANDOM Q-kerete a panelcímet súrolta → a tárcsák 6 px-szel lejjebb; a Master VOICES kapcsolói a számdobozok középvonalára igazítva; a VOICE OVERRIDE a Q-keret szélén ült → keskenyebb.
2. kör: a sentinel első helye (a panelek közti résben) a panelek között egy apró világos négyzetként látszott → a Master sentinel a panel címsávjába, a Voice-é a sötét résbe került.

## Ellenőrzés (célzott, a kiinduló HEAD 444f3638-hoz képest, azonos Mac-környezetben)

| teszt | kiinduló | új |
|---|---|---|
| `mastervoice_ui_host` | 0 hiba | 0 hiba |
| `qslots_host` | 10 hiba (Filter/Sub/Noise, nem Master/Voice) | ugyanaz a 10 |
| `layout_lint_host` | 20 új jelzés (mixer peek-main-fx, osca/oscc/oscnoise) | ugyanaz a 20 |
| `coverage_host` | SIGSEGV, kimenet nélkül | ugyanaz (TODO) |
| `test_native_master.py` | OK | OK (+1 teszt: a C++ fejléc frissessége) |
| `test_ui_screen_master_preview.py` (SDK validate) | OK | OK |
| `test_native_voice.py` | OK, 1 kihagyva | OK; a csomagolt TUI ↔ C++ szerződés (`--voice-skin-contract`) is lefutott: OK |
| `test_ui_screen_voice.py` | OK | OK |
| `test_mpc_backgrounds.py` | OK | OK |
| `test_native_component_isolation.py panel / name_value` | – | OK |

A frissített tesztek a régi, kézzel beírt téglalapok helyett a layoutfájlokból számolnak, vagy az új értékeket rögzítik.

## Ami maradt (TODO.md)

- Eszközös ellenőrzés Key 61 + XL (panelek, betűk, sentinel, Q-keret, oldalváltás), majd valódi felvétel a `docs/screens`-be.
- Az előnézet-rajzoló nem rajzolja a natív `Meter` típust (a Master mérősávjai az előnézeten üresek).
- `coverage_host` összeomlás a megosztott core-ral (a kiinduló állapotban is).
- A PANEL komponens katalógus-jóváhagyása.
