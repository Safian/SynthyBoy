# Natív komponenstár: a rajzolt és a készüléken látott megjelenés összevetése (2026-11-26)

Cél: a host-rétegű natív widgetek (Plugin Skin) a tervezett, rajzolt UI-hoz (a `shell-mpc/ui/` C++
komponensei, a régi GPU-s overlay képernyői) a lehető legjobban hasonlítsanak, és a kettő ugyanabból
a stílusforrásból származzon.

## Források

- Rajzolt (cél): a natív előtti Key 61 képek (`/private/tmp/claude-501/gk-old-shots/*.png`, a
  GPU-s overlay teljes rajzolásával), a rajzoló kód: `shell-mpc/ui/base.h` (`Knob`),
  `shell-mpc/ui/mixer_strip.h` (`Fader`), `shell-mpc/ui/core.h` (`Pen::block/control`,
  `inactiveGrey`), `shell-mpc/ui/theme.h` + `theme_tokens.h` (a `neon` look).
- Natív (mostani): Key 61, 1280×800 host-sík: `/private/tmp/claude-501/gk-fix-shots/*.png`.
  XL-ről még csak egy próbakép van (`gk-xl-shots/test-host.png`), XL-összevetés nem készült.
- A natív skin generátora: `components/native/*/*.py`, `components/style.py`,
  `components/colors.json`, `components/sizes.json`, `scripts/mpc-skin-extras/`.
- SDK: `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/docs-official/mpc-sdk/docs/ui-reference.md`
  (a továbbiakban „SDK §”).

## A közös stílusforrás állapota

Már a munka előtt is egy forrásból jött a paletta: a `components/colors.json` `themes.neon` és a
`components/sizes.json` `theme.neon` tokenjeiből a `scripts/gen-ui-theme.py` állítja elő a
`shell-mpc/ui/theme_tokens.h`-t (a GPU-s look), és ugyanezeket olvassa a `components/style.py`
(natív oldal). Az eltérés abból jött, hogy a natív komponensek **saját, külön tokeneket**
használtak (`dial.native.*`, `fader.cap.*`, `text.icon` feliratszín), a rajzolt komponensek
geometriai receptje pedig literálként élt a C++-ban (`base.h`: 135°/270°, 0.25/0.6 mutató,
0.9 × vonalvastagság hüvelykujj, 2.4/2.0 px minimum, 3 px radius-beljebb).

Javaslat és megvalósítás (egy forrás, két renderelő):

1. **Paletta**: a natív művészet a `theme_colors("neon")` színeit használja (track, flow, text,
   textDim, panel0/1, keylineHi), nem saját „native” színeket.
2. **Metrikák**: a look metrikái (`arcTrack`, `arcValue`, `frame`, `rBlock`) a `size("theme.neon")`
   tokenből jönnek mindkét oldalon.
3. **Komponensrecept**: az eddig C++-literál geometriák névvel ellátott tokenek
   (`sizes.json` `knob.*`), amelyekből a generátor `theme::knob::k…` konstansokat ír a
   `theme_tokens.h`-ba; a `ui::Knob` és a natív filmstrip-renderelő
   (`components/native/level/level.py: render_dial_frame`) ugyanazt olvassa.
4. **Tipográfia**: a betűméret-token tervezett em-méretet jelent; a host JUCE-magasságot vár
   (lásd A pont). A `style.native_font_em()` váltja át az előnézetek számára;
   a skin-oldali átváltás nyitott (TODO).
5. Tovább: ugyanígy tokenbe kell emelni a `Fader`, `LedToggle`, `Stepper`/`Selector` és
   `Pen::control` literáljait (csak a `Knob` és a fader thumb készült el).

## Rendszerszintű megállapítások

**A. A natív szöveg kb. 34 %-kal kisebb a tervezettnél.**
Az SDK TextStyleData `font.height` értéke (SDK §6.3) JUCE `Font` magasság, azaz ascent + descent.
A Titillium Web SemiBold (`shell-mpc/overlay/fonts/TitilliumWeb-SemiBold.ttf`) hhea-adatai:
1133 + 388 / 1000 em, ezért 1 em = height / 1.521. Mérés a Key 61 ENV képein:

- A natív „Attack” felirat `font.caption` = 15, tehát em ≈ 9,9 px. A sorok 605–612
  között vannak: 8 px a „k” felső szárával és az élsimítással együtt.
- A rajzolt „ATTACK” `fLabel` = 15 em, a nagybetűmagasság 10 px (689–698).
- A natív „5.0 ms” érték `font.value` = 20, tehát em ≈ 13,1 px. A számjegyek 10 px magasak.
  Ha a 20 em-méret lenne, a magasság 13,7 px volna.

A Pillow-előnézetek (`render_native_catalog_cards.py` és a képernyő-előnézetek) eddig em-méretnek
vették a magasságot, ezért a katalógus és az előnézetek nagyobb betűt mutattak, mint a készülék.
Ez látszik az OSC TUNE kártyán is: a katalógusban 13/15 px, a készüléken kb. 9 px.

A skinbeli magasságokat azért nem szoroztam fel 1,521-gyel, mert a címkék téglalapjai a
mostani magasságokra vannak méretezve. A JUCE Label ilyenkor függőlegesen levágja, vízszintesen
0,7-ig összenyomja, majd kipontozza a szöveget. Ez laponként felülvizsgálandó, készülékpróbával (TODO).

**B. A natív tekerő saját rajzolatot kapott, nem a tervezettet** (javítva). A régi filmstripben:

| Elem | Régi natív filmstrip | Rajzolt `ui::Knob` |
| --- | --- | --- |
| Track | 3 px, világos `#61749e` | 0,06·d, sötét `#1c2350` |
| Értékív | 8 px, egyszínű `#65c6ff` | 0,078·d, négyszínű flow-gradiens (cián → lila → magenta) a kirajzolt íven |
| Mutató | 3,2 px | 1,6 px, 0,25R–0,6R |
| Hüvelykujj | r = 3,5 px pötty | fehér korong, r = max(2,5; 0,9 × vonalvastagság) |

A régi filmstrip ráadásul a 90 px-es rajzot kicsinyítette le, így a 40 px-es tekerő vonalai
arányosan vékonyak lettek. A rajzolt komponens minden méretnél külön számol, 2,4/2 px minimummal.
Az inaktív állapot eddig saját „smooth.*.off” színeket használt, most a rajzolt `inactiveGrey`
receptet (luma × 70 %).

**C. Feliratok** (javítva a tekerőkön). A rajzolt név NAGYBETŰS, `textDim` (`#aab2d8`) színű.
A natív `Name` címke eddig „Original” esetű volt (pl. „Attack”), a stacked/compact változatban
`text.icon` (`#e6e6e6`) színnel. Az SDK §6.3 szerint a `case` lehet „Upper Case”, ez már a
`20_fx.py`-ban is használatban van. Az értékfelirat színe `text.icon` volt, most `text.value`
(= rajzolt `text`).

**D. Panelek/kártyák** (nem komponensszint). A rajzolt oldalakon a csoportok lekerekített
blokkokban állnak (`Pen::block`: panel0 → panel1 gradiens, `rBlock` 14, keyline, címsor), például
ADSR / SHAPE / TIME, UNISON / WARP, TUNE. A natív lapokon csak apró szekciófeliratok vannak sík
háttéren. A host statikus háttérképet tud rajzolni (SDK §6.6, „Images are scaled to the entry
bounds”; `99_backgrounds.py`), így a blokkok a lap hátterébe tehetők. Ez lapszintű munka (TODO).

**E. LED-kapcsolók és LED-listák** (komponenshiány). A rajzolt `LedToggle` / `LedList` egy
gyűrűs pont és a felirat (BEATS, LEGATO, HZ/BPM/KEY, Forward/Reverse, Kill/Steal). A natív
lapokon helyettük ez látszik:

- egy szinte láthatatlan szürke szöveg (`active_text` OFF, pl. LIMIT, NO SMOOTHING, LEGATO,
  PAD PRESS a Masteren),
- vagy egy szegmentált gomb (HZ/BPM/KEY),
- vagy egy legördülő (Direction, Same note, Steal).

Natívan megvalósítható: az SDK §6.4 Button `onImage`/`offImage` képpár a beégetett gyűrűvel és
felirattal (a radio csoport `numButtonsInGroup`-pal). Új natív `led_toggle` komponens kell (TODO).

**F. Q-csoport keret és számozott chipek.** A rajzolt régi képeken sárga számozott chipek
látszanak. Ezt az 5b szabály már tiltja: csak egy keret és egy számozatlan Q marad. A keret az
overlay dolga Key 61-en (gen1), XL-en nincs (5a szabály). Natív widget a Q-fókuszt nem kapja meg:
az SDK nem ad Q-csoport állapotot, csak a `Focus` widget `WhenFocussed` láthatóságát (SDK §6.5).
Natívra ez nem vihető át.

**G. Fader thumb** (javítva). A natív thumb világosszürke „hardveres” lap volt (`#c3d1e8` töltés,
sötét bevágás). A rajzolt `Fader` thumbja sötét blokk: panel0 → panel1, keylineHi kettős
körvonal, fehér középvonal.

**H. Antialiasing, gradiens, lekerekítés.** A natív widget csak képet (filmstrip/Button/Image) vagy
egyszínű Label-t és Focus-keretet rajzol. Ezekben nincs gradiens, árnyék és betűköz, a Focus
lekerekítése sem dokumentált (SDK §6.3, §6.5). Ami a rajzolt UI-ban vektoros (ív, gradiens,
lekerekítés), az natívan csak előrenderelt képként adható át. A képek élsimítását a generátor
adja (4× szuperszamplingolás), a host nem skáláz a Buttonon (SDK §6.4: „centred … at their
native size”), a Knob/Image a keretre skáláz. Ezért a filmstripet pontosan a widget méretére
kell gyártani. A level/pan gyár ezt teszi: a `frame_size` a knob téglalapja.

**I. Folyamatos animáció.** A host csak változáskor rajzol újra: XL-en 20 fps, Key 61-en a Meter
kb. 8 fps (`docs/dsp-knowledge/mpc-display.md`, „Host redraw rate”). A rajzolt UI élő
modulációs íve és jelölője ezért natívan nem adható át, az overlayé marad.

## Komponensenként

| Komponens | Rajzolt (cél) | Natív (mostani, Key 61) | Közelíthető? | Állapot |
| --- | --- | --- | --- | --- |
| Level/Pan tekerő (`level`, `pan`, minden filmstrip-tekerő: ENV, LFO, Filter, Mixer PAN/B1/B2, Macro, Voice, Master) | Lásd B | Lásd B | Igen, teljesen: a filmstrip a rajzolt recepttel készül | **Kész** (`level.render_dial_frame`) |
| Tekerő felirata | NAGYBETŰ, textDim, középre a tekerő alá; az érték alatta, fehér, nagyobb | Vegyes eset, `text.icon`; a lapok egy része balra/felül igazít, kisebb betűvel (A) | Eset/szín igen; méret csak téglalap-újraméretezéssel; a pozíció lapszintű | Eset/szín **kész**; méret TODO |
| Fader thumb | Sötét blokk, kettős keylineHi körvonal, fehér vonal | Világos lap | Igen | **Kész** (`fader.render_fader_cap`) |
| Fader skála | textFaint számok, a „0” erősebb; meter cián → magenta gradiens | Kék „100”, a +6/0/-6 feliratok egymásra csúsznak a F1/F2 csíkon | A skálasűrűség komponensszinten javítható | TODO (mixer_strip skála ütközés) |
| Meter | flow-gradiens (cián → magenta), piros clip | Zöld/borostyán/piros (`docs/dsp-knowledge` „Mixer true stereo”) | Művészet-csere igen | Szándékos (mérési színkonvenció); owner-döntés |
| Selector / legördülő | Stepper `‹ név ›` + lista-gomb, 22 px item, keret | Kártya: kis cím, érték, ⌄ ikon; 13 px-es tétel (A) | Méret A szerint; a forma más komponens | Részben (A) |
| NameValue / Tune mezők | Panelkártya, cím felül középen 15 em, érték 24 em középen | Cím a dobozon kívül balra, kb. 9 px; érték kb. 9 px | Belső elrendezés igen, a kártya külső mérete marad | TODO |
| LED toggle / LED lista | Gyűrűs pont + felirat; ON: cián pont | Halvány szöveg / szegmentált gomb / legördülő | Igen, Button képpárral | TODO (új `led_toggle`) |
| Gomb (MAP/MORE/BROWSE) | `Pen::control` gradiens + keyline, ikon (fogaskerék) | Hasonló kártya, ikon nélkül | Ikon igen (`button.action_art`) | TODO (lapszintű bekötés) |
| Rádió (ENV 1…6) | Kiválasztott: narancs körvonal (a példány színe) | Kiválasztott: cián tömör kitöltés | Képcsere igen | TODO (egyeztetés: példányszín vagy accent) |
| Solo/Mute | (Rajzolt megfelelő nincs: power + név) | S/M kártyák | – | – |
| Panelek / címek | `Pen::block` + cím | Sík háttér, apró cím | Háttérképben | TODO (lapszintű) |
| Q-csoport | Egy keret + Q (gen1) | Nincs natívan | Nem (F) | Overlay-tulajdon |
| Fókusz | – (a rajzolt UI-ban nincs hardveres fókusz) | Focus: 10 % fehér + cián körvonal | – | Változatlan |
| Inaktív | `inactiveGrey` + írásvédelem | Kép-szürkítés (`apply_control_state`) + akció-tiltás | A tekerőn igen | Tekerő **kész** |

## Ami SDK-korlát miatt nem hozható közelebb

- Saját betűtípus nem tölthető be: csak Titillium Web / Roboto / Nexa (SDK §1.5). A Titillium
  Web egyezik a rajzolt UI betűjével, így ez nem okoz eltérést.
- A Label nem tud gradienst, árnyékot, betűközt, automatikus méretezést (SDK §6.3).
- A Knob saját rajzolata csak filmstrip lehet. A GenericLegacy a gyári MPC-tekerőt rajzolja
  (SDK §6.2). A modulációs mélységív és az élő jelölő értékenként változó, több forrású
  állapot, egy filmstrip egyetlen paraméterhez kötött. Overlay marad (I).
- Q-csoport fókuszállapot nincs (F).
- Nincs általános enabled flag: `components/style.py: apply_control_state` docstring;
  a tiltott állapot képcsere + akciótörlés.
- Key 61-en az overlay sík a host primary síkja fölött van, alfa/zpos tulajdonság nélkül
  (`mpc-display.md`, „Plane stacking facts”). A natív és a rajzolt réteg nem keverhető
  átlátszósággal.

## Megvalósítva (ág: `worktree-agent-a6e9792eee79f6cf9`)

- `knob.*` tokenek → `theme::knob` konstansok; a `ui::Knob` ezeket olvassa (azonos értékek).
- A natív tekerő-filmstrip a rajzolt recepttel készül, a rajzolt look palettájából és metrikáiból;
  az aktív és az inaktív (`inactiveGrey`) változat is. A régi `dial.native.*`, `stroke.arc*`,
  `level.write_strip.*` és `smooth.{arc,track,pointer}.off` tokenek törölve.
- Tekerőfeliratok: „Upper Case”, `text.caption` (= textDim); értékfelirat `text.value`.
- Fader thumb a rajzolt `Fader` szerint; a `fader.cap.*` tokenek törölve.
- `font.host.height.per.em` token + `style.native_font_em()`. A katalógus tekerő- és
  selector-előnézete a készüléken látható betűmérettel rajzol.
- Újragenerált komponensképek: `components/native/{level,pan}/img/*`, `fader/img/mixer_fader_cap.png`,
  `mixer_strip/img/*`, `macro_card/img/catalog-macro-card.png`, `selector/img/catalog-selector-56-*.png`.

## Az ownernek a készüléken

- Bármely tekerős lap (ENV, LFO, Filter, Mixer, Macros, Master, Voice): gradiens ív, fehér
  hüvelykujj, sötét track; a 40 px-es kompakt tekerők olvashatósága (Play, Mixer PAN/B1/B2).
- Inaktív modul (pl. OSC B a Mixerben): szürke tekerő.
- Nagybetűs tekerőnevek: nem vágódik-e le hosszabb név (pl. KEYTRACK, RELEASE) a címketéglalapban.
- Mixer fader thumb a sötét sávon: elég kontrasztos-e (a rajzolt UI-ban ugyanez a forma).
