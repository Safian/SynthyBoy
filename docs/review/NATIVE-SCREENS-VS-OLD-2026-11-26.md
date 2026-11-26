# Natív screenek a natív refaktor előtti állapothoz képest: design-review (2026-11-26)

Tervezési és review-dokumentum. A forráskódhoz nem nyúl. Minden képi állítás az alább felsorolt képek valamelyikén látott elemre hivatkozik; a koordináták a teljes 1280×800-as képernyőre vonatkoznak (a tab-terület ebből y 110..738), kivéve, ahol „tab-koordinátát” írok (az SDK szerinti 1280×628-as tab-terület, y=0 a képernyő 110. sora).

## 0. Bemenetek és amit nem tudtam megnézni

| Forrás | Mi van rajta | Megnézve |
|---|---|---|
| `/private/tmp/claude-501/gk-old-shots/<screen>.png` | RÉGI: commit `0473eb47`, XL, host+overlay összeillesztve | mind a 15 (play, osc_a/b/c, sub, noise, env, lfo, filter, fx, mixer, mod, macros, voice, master) |
| `.../gk-old-shots/<screen>-hostonly.png` | RÉGI, csak a host-sík | play, mixer (mindkettő üres szürke tab: a régi UI 100 %-ban az overlay-en élt) |
| `docs/screens/*.jpg` (a `62807d8d` fán) | ÚJ: szintetikus előnézet | mind a 16 (+ `filter_old`, `master_old`, `mixer_old`, `mod_old`) |
| `/private/tmp/claude-501/gk-fix-shots/` | ÚJ: valódi Key 61, csak host-sík | osc, h_osc, env, filter, fx, n1, lfo, master, r_master2, mix, mod, x, h0, s0 (14 a 24-ből; a többi – h_filter, h_fx, h_master, h_mix, n2, n3, next, r0, r_master, s1 – nem; a nevek alapján ugyanazon oldalak ismétlései) |
| `/private/tmp/claude-501/gk-xl-shots/` | XL | egyetlen kép (`test-host.png`), az a gyári „Select Project” lap, SynthyBoy nincs rajta: **XL-en az új natív lapokat nem láttam** |
| `pre-native-screens-sheet.png` | összefoglaló lap | **nem létezik** a megadott helyen (`gepekere-fresh-version-65be77/` gyökér); nem néztem meg |

Fontos különbség a bemenetek között:
- A Key 61 képeken csak a host-sík van (az overlay-sík nincs lefotózva). Ez nem hiba a képen, de pontosan megmutatja, mi látszik, ha az overlay nem rajzol (ld. 2.1).
- A szintetikus `docs/screens/fx.jpg` 1280×628-as (csak tab-terület, MPC-fejléc és -lábléc nélkül), a többi 1280×800. Nem egységes.
- A szintetikus `fx.jpg` (nagy, 18–20 px-es feliratok, RACK MAIN doboz) és a Key 61 `fx.png` / `n1.png` (9–10 px-es feliratok, RACK doboz nélkül, a vezérlők y 340–560 között) eltér: a készüléken még nem a szintetikus képen látható Distortion-elrendezés fut, vagy a szintetikus render más betűméretet használ. Nem tudtam eldönteni, melyik.
- A régi és az új képek állapota nem mindenhol azonos (pl. régi Play/LFO: LFO 1 „DRAWN” custom alak, új: Sine; régi Filter cutoff 57,9 Hz, új 450 Hz; régi Macros CUTOFF 43,8 %, új minden makró 0.0). Ezeket nem értékelem designhibaként.

## 1. Összefoglaló verdiktek

| Oldal | Verdikt | Egy mondatban |
|---|---|---|
| Play | **rondább** (közepesen) | A három oszlop szerkezete megmaradt, de a feliratok 9–10 px-esek, eltűnt a mértékegység és a szomszéd-név a fejlécből, a Sub oszlopban egy félig takart Q-keret maradványa látszik. |
| OSC A | **jelentősen rondább** | A hullámkijelző a Q-keret és a FRAME-poti alá csúszott, a UNISON blokk (x 18–560, y 460–700) szinte üres, a WT POS/BLEND/WARP-mennyiség eltűnt, a vezérlők 35 px-es potik 7–9 px-es feliratokkal. |
| OSC B / C | **jelentősen rondább** | Mint az A, plusz elveszett a kijelzőn az „OFF” felirat; a kikapcsolt állapotot csak a szürkítés jelzi. |
| Sub | **rondább** | A hat hullámforma-ikonsor legördülővé vált, az ENV 1 / MONO / FREE RUN kapcsolók alig látható szürke szövegek, a BUS 1/2 elveszett, a bal felső negyed (x 20–280, y 120–220) üres. |
| Noise | **rondább** | A BLEND értéke csonkolva („0....”, x 1218 y 593), egy lapon három potiméret (≈35 / 80 / 110 px), a KEY TRACK és az ENV 1 alig látszik. |
| ENV | **egyenértékű** (enyhén rosszabb) | Ez maradt a legközelebb a régihez; elveszett a három csoportkártya és a TARGETS szám, a feliratok kisbetűs/nagybetűs keveréke és 9 px-es csoportcímek gyengítik. |
| LFO | **rondább** | A 13 ikonos alakrács és az 1–10 / R1–R4 számsor egy-egy legördülővé vált, a potik három méretben és háromféle felirat-elrendezéssel; a HZ/BPM/KEY nagy gombjai viszont jobbak. |
| RND | nincs régi önálló kép; **gyenge** | A kijelző jó, alatta egy poti, három gomb, egy szürke „STEREO” felirat és egy legördülő; a y 540–720 sáv kétharmada üres. |
| Filter | **vegyes, összességében rondább** | Új, értékes NOTE/VELO görbeszerkesztő és nagyobb válaszgörbe-keret; a vezérlősáv viszont 40 px-es potikra és 6–9 px-es feliratokra zsugorodott, az OUT csoportból csak BLEND maradt. |
| FX (Distortion) | **jobb** (a szintetikus képen) | Nagyobb, kitöltött görbe bal oldalt, olvasható 18–20 px-es feliratok jobbra; a Key 61-en még a régebbi, apró feliratos változat látszik. Folyamatban. |
| Mixer | **egyenértékű** (enyhén rosszabb) | Új S/M gombok és útvonal-fülsor (jó), de az értékek 7 px-esek, az F1/F2 skálafeliratai egymásra csúsznak, a kikapcsolt csatornák útvonal-mezője üres. |
| Mod | **egyenértékű** (enyhén rosszabb) | A sorok szinte azonosak; elveszett az oszlopfejléc, a sorszám, a forrás→cél nyíl, a „12 / 64 SLOTS” és a görgetősáv, a UNI/ON ikonból 7 px-es szöveg lett. |
| Macros | **rondább** | A célpontlisták helyett 8 egyforma TARGETS gomb, az értékek 7 px-esek, egy üres spektrum foglalja a lap felét. A 8 makró egy sorban viszont jó. |
| Voice | **rondább** (folyamatban) | A lépéssor függőleges „oszlopai” helyett 8 poti, így a minta nem olvasható egy pillantással; a csoportkártyák és a TARGETS jelzőpontjai eltűntek. |
| Master | **jelentősen rondább** (folyamatban) | Szétszórt, 6–9 px-es feliratok, szürke szöveg-kapcsolók, a hangszámláló sáv és a BEND UP/DOWN szétválasztás eltűnt, a csúcsmérő skálája egymásra írt számokból áll. |

Röviden: **1 jobb (FX), 3 egyenértékű (ENV, Mixer, Mod), 10 rondább (Play, OSC A/B/C, Sub, Noise, LFO, Filter, Macros, Voice, Master), az RND-nek nincs régi párja.**

## 2. Közös okok: miért lettek rondábbak

### 2.1 A kétrétegű felosztás „lyukakat” hagy
A régi UI egyetlen overlay-kép volt (a `play-hostonly.png` és `mixer-hostonly.png` üres szürke tab). Az újban a vezérlők a host-síkon (natív TUI), a görbék az overlay-síkon élnek. A Key 61 host-sík képeken a kijelzők **kerettel együtt** hiányoznak:
- `gk-fix-shots/osc.png`: x 20–970, y 180–460 teljesen üres; a BROWSE alatt és a TUNE bal oldalán semmi nem jelzi, hogy ott kijelző van.
- `env.png`: y 180–550 üres (a teljes görbe és kerete hiányzik).
- `h0.png` / `x.png` (Play): mindhárom oszlop y 250–490 üres; csak a power és az expand gomb lebeg.
- `filter.png`: a két kijelző (y 180–550) üres, csak két expand gomb látszik.
Ez sérti az elvárást, hogy az alap natív UI az overlay nélkül is önálló legyen: overlay nélkül a lap félkész képet ad, és egy lassú overlay-indulásnál a felhasználó ezt látja először.

### 2.2 A csoportkártyák szándékos eltávolítása, pótlás nélkül
`docs/dsp-knowledge/static-backgrounds.md` szerint (owner, 2026-11-24) minden lap egységes `#090d19` vásznat kap, a háttérképek megszűntek. A régi lapok hierarchiáját viszont épp a kártyák adták (ENV: ADSR / SHAPE / TIME, LFO: RATE / TIMING / MOTION, Master: OUTPUT / PITCH / VOICES / GLIDE, OSC: UNISON / WARP / TUNE). A kártya helyére csak egy 9–10 px-es csoportcím került (`env.jpg` „ADSR” x 23 y 565, „SHAPE” x 483, „TIME” x 830), keret, elválasztó és térköz nélkül. A csoportok így összefolynak: az ENV-en az A CURVE (x 498) ugyanolyan távol van a Release-től (x 370), mint a Decay a Sustaintől.

### 2.3 Tipográfia: a tokenskála alja lett az alapméret
A `components/sizes.json` skálája jó (micro 9 … hero 24), de az új lapok szinte mindenhol a skála alját használják:
- Régi: vezérlőfelirat ≈14–15 px nagybetűs, érték 20–22 px (`env.png` régi: „ATTACK” / „5.0 ms”; `osc_a.png` régi: „LEVEL” / „50.0”).
- Új: vezérlőfelirat ≈9–10 px (`env.jpg` „Attack” x 19 y 609), értékek ≈12–14 px; a TUNE mezők címkéi („OCT”, „SEM”, „FIN”, „CRS” x 1016–1160 y 354–390) ≈7 px-esek, a Filter EDIT sor („POINT”, „GRID X”, x 788 y 638–702) szintén.
- A kis-nagybetű keveredik egy lapon belül: `env.jpg` „Attack / Decay / Sustain / Release / Delay / Hold” vegyes írású, „A CURVE / D CURVE / R CURVE” nagybetűs; Play: „Attack”, „Rlse” (elírás, x 938 y 679: a „Rise” nagy I-vel), „SMOOTH”. Ez két különböző natív gyártó (dial-cella vs. name_value) eltérő címkestílusából jön.

### 2.4 Mértékegységek és formázás elvesztek
Régi: „5.0 ms”, „400 ms”, „100.0%”, „+3.3 dB”, „R 2%”. Új Play: „5.0”, „400.0”, „100.0”, „200.0” (x 547–779 y 566–690); az ENV lapon ugyanez a paraméter „400 ms”. Ugyanaz az érték két lapon kétféleképp jelenik meg (ez a `shell-mpc/ui/AGENTS.md` „one text for one value” szabályát lapok között sérti). Master: VOLUME „-12.6” dB nélkül, VEL TRACK „100.0”, GLIDE „0.0” ms nélkül. LFO: RATE „2.00” Hz nélkül.

### 2.5 Az ActiveText OFF színe olvashatatlan
A kapcsolók nagy része `ActiveText` lett (owner kérte: szín jelzi az állapotot). Az OFF szín `#384252` a `#090d19` vásznon **1,91 : 1** kontrasztú (mért, WCAG-képlettel; ugyanígy számolva `text.label` 7,48 : 1, `text.caption` 9,3 : 1). Eredmény:
- `env.jpg` BEATS / RETRIG (x 1099 / 1211, y 659), `sub.jpg` ENV 1 / MONO / FREE RUN (x 578 / 800 / 1022, y 572), `lfo.jpg` HOST / MONO (x 716 / 962, y 693), `master.jpg` LIMIT, NO SMOOTHING, OVERSAMPLE 4X, LEGATO, POLY LEG, PHASE LEG, PAD PRESS, ALWAYS, SCALED, `noise.jpg` KEY TRACK, `filter.jpg` CLEAN, SNAP, LEGATO, `voice.jpg` 10x DETUNE, `rnd.jpg` STEREO: mind alig látszik.
- Ugyanez a szín a „letiltott” (inaktív modul) megjelenése is, így egy kikapcsolt-de-állítható kapcsoló és egy nem állítható vezérlő nem különböztethető meg.
- Nincs LED-pont, így nem is látszik, hogy a szöveg érinthető.

### 2.6 Vizuális választók helyett legördülők
A régi lapok erőssége a képes, egyérintéses választás volt; az újban ezek `selector.dropdown`-ná váltak:
- Sub: SINE/HALF/TRI/SAW/SQR/PULSE ikonsor (régi `sub.png` x 166–688 y 137–205) → „WAVEFORM Sine” legördülő (`sub.jpg` x 282–972 y 143–198).
- OSC: WAVE/MULTI/SAMPLE/GRAIN/SPEC ikonsor (régi x 166–556) → „TYPE Wavetable” legördülő (x 122–378).
- LFO: 13 alak-ikon (régi x 970–1270 y 200–365) → „WAVEFORM Sine” legördülő; LFO 1–10 + R1–R4 gombsor (régi y 130–182) → „SOURCE LFO 1” legördülő.
- ENV: ENV 1–6 fülsor → „1 / 2· / +” (ez a `UsedSelector` logika, funkcionálisan jó, de a régi „TARGETS 0” számláló elveszett).
Az SDK-ban a képes gombsor natívan is megvan (`Button` on/off képpel, `Indicator`), tehát ez nem natív korlát, hanem elrendezési döntés.

### 2.7 Nincs közös rács; laponként kézzel rakott koordináták
- OSC unison-blokk: „VOICES” x 27 y 540, „1 voice” gomb x 127–194, DETUNE poti x 205 y 570, „DETUNE” felirat x 252 y 558, érték x 300 y 579, „DETAIL” x 91 y 607: öt különböző alapvonal egy 300 px-es területen.
- Noise: PAN poti ≈35 px (x 870), FILTER/STEREO ≈80 px, BLEND ≈110 px (x 1095–1210) egy lapon.
- LFO: RATE ≈80 px poti felirat alatta, RISE ≈50 px felirat jobbra, SMOOTH poti x 290 y 645 a „SMOOTH” felirat (x 215 y 668) jobbján, értéke (x 265 y 703) a poti alatt balra: három elrendezés hat vezérlőre.
- Master: „TRANSPOSE” (x 888 y 276) + mező, „BEND” mező x 1199–1265, „LINK” x 1042 y 376 magában; a PITCH csoport x 680–1270, y 190–400 túlnyomóan üres.

### 2.8 A Q-keret nincs a rácshoz kötve
- OSC A/B/C: a sárga/szürke Q-keret (x 122–1139, y 115–249) a TYPE/SOURCE sort, a hullámkijelző felső részét és a FRAME potit (x 1000–1035 y 210–240) egyszerre fogja közre: a keret egy kijelzőt vág ketté.
- Play Sub oszlop: a Q-keret a WAVE legördülő és az OCT mező mögött van; csak a darabjai látszanak (felső él x 306–420 y 515, függőleges élek x 14 és x 420 y 590–620, a csonka Q-jel x 22–36 y 505–520). A Key 61 host-sík képen (`x.png`) ezek nincsenek, tehát ez az overlay-keret és a natív vezérlő ütközése.
- Macros: a Q-keret (x 6–636, y 490–733) jó, mert pontosan négy egyforma cellát fog közre. Ez a minta.

### 2.9 Elveszett információ
- OSC B/C: „OFF” felirat a kijelzőn (régi x 92–126 y 312–328).
- Sub: BUS 1, BUS 2 (régi x 730–780 y 680–720). Noise régi: ENV 1 jelzőpont + felirat egy cellában.
- OSC: WT POS, BLEND, WARP 1/2 mennyiség és PHASE (régi y 560–730).
- Mod: oszlopfejléc (SOURCE/TARGET/AMOUNT/POLARITY/STATE), 01–07 sorszám, „12 / 64 SLOTS”, görgetősáv, élő modulációs pont (régi 01. sor x 764).
- Macros: makrónkénti célpont-lista (régi „F1 Cutoff · F2 Cutoff”, „NO TARGET – TAP TO ADD”).
- Master: hangszámláló sáv (régi x 400–780 y 662–706 16 cellás LED-sor) → csak „0 / 16 VOICES” szöveg és egy magányos függőleges vonal (x 407 y 670–700); BEND UP / BEND DOWN két mezőből egy „BEND 2”.
- Filter: OUT LEVEL / PAN, COMB, ROUTING (Parallel / Series 1→2 / 2→1).

### 2.10 Szentinel-pontok a vezérlőkön
A host-háttér detektor 5×5-ös színes pontjai (x ≈35/48, y 135 és x 1243, y 135) a Key 61 képeken rácsúsznak vezérlőkre: `fx.png` / `n1.png` a „MOVE” felirat tetején, `mix.png` a SUB és a MAIN csatorna fejlécén. A szintetikus `fx.jpg` x 30 y 20 körül ugyanezt mutatja („■■” a MOVE fölött). Ezek helyét a rácsnak kell lefoglalnia.

### 2.11 Ami jobb lett (nem csak romlott)
- Nagy, egyértelmű szegmentált gombok: HZ / BPM / KEY (Play x 1052–1258 y 578–630, LFO x 398–544 y 480–644, RND x 240–644 y 574–674), PAN/DETUNE/… sorválasztó a Voice-on (x 20–248 y 242–414), NOTE/VELO/TARGETS a Filteren. Jó érintési célok, egyértelmű kijelölt állapot (cián kitöltés).
- Egységes OSC aloldal-fülsor: SUB / A / B / C / NOISE (y 700–735), Mixer: MIXER / FLT 1 / FLT 2 / MAIN FX / BUS 1 / BUS 2. Ez valódi navigációs nyereség.
- Mixer S/M gombok minden csatornán, és a teljes magasságú busz-faderek.
- Filter: kulcsgörbe-szerkesztő (NOTE/VELO) új funkció, a válaszgörbe saját kerettel.
- FX Distortion: a görbe-kitöltés és a „display bal / vezérlők jobb” arány jó, a feliratok olvashatók.
- Macros: mind a 8 makró egy képen, bankváltás nélkül; a Q-bank kerete pontosan négy cellát fog.
- ENV: az „1 / 2· / +” választó csak a használt burkolókat mutatja.

## 3. Közös design nyelv (javaslat, minden lapra)

Minden elem a meglévő natív SDK-primitívekből és a projekt natív komponenseiből épül (SDK `ui-reference.md` §6: Label, Button, Knob, Slider, Image, Separator, Indicator, Path, Meter, Envelope). Új tokenek a `components/sizes.json` / `colors.json`-ba kerülnek, nem a lapokba.

### 3.1 Rács (tab-koordináták, 1280×628)
- **Oszloprács:** 8 cella × 150 px, 8 px köz, 12 px margó: `x_i = 12 + 158·i` → 12, 170, 328, 486, 644, 802, 960, 1118; jobb szél 1268. **Fél sor = 4 cella = 624 px = egy Q-bank.** A bal fél x 12–636, a jobb x 644–1268.
- **Sávok:**
  - `H` fejléc: y 8–64 (56): modulválasztó, power, típus, akciógombok.
  - `D` kijelző: y 72–312 (240), teljes vagy fél szélesség.
  - `R1` vezérlősor: y 320–432 (112), `R2`: y 440–552 (112).
  - `S` alfül-sor: y 580–628 (48) (OSC, Mixer); ahol nincs, ott `R3`: y 560–620 (60, csak kompakt cellák).
  - Kijelző nélküli lapon (Master, Voice) négy sor: y 72, 192, 312, 432 (112 + 8).
- **Szentinel-zóna:** tab y 0–8, x 0–64 és x 1216–1280 foglalt, ott vezérlő és felirat nem lehet.
- A Q-Link oldalsáv (x ≥ 1025, SDK §1.4) alá ne kerüljön olyan elem, amely Q-állítás közben fontos; a jobb Q-bank cellái ezért 4 egyforma, önmagukban is értelmes cellák.

### 3.2 Cella-anatómia
- **Knob-cella 150×112:** felirat felül (y+4, 18 px magas, 14 px SemiBold, nagybetű, `text.caption`), poti 64 px középen (y+24..88), érték alul (y+90..110, 18–20 px SemiBold, `text.value`, mértékegységgel). Egy lapon legfeljebb két potiméret: 64 (sor) és 40 (kompakt, csak Mixer/Play).
- **Mező-cella 150×56 vagy 308×56:** felső sor felirat 12 px, alsó sor érték 18 px (a `name_value` „medium” preset: label 14 / value 16 helyett 12/18 javasolt).
- **Gomb:** 56 px magas, 18 px felirat (`button.text.font`), kijelölve cián kitöltés (a HZ/BPM/KEY minta).
- **Választó** (több mint 8 elem): a mostani legördülő maradhat, de 56 px magas, felirat 12 px + érték 18 px.
- **Képes választó** (≤8 elem: hullámforma, OSC-típus, LFO-alak): natív Button-sor 72×56 képpel, kijelölt keret cián; 13 LFO-alak 2 sorban (7+6).

### 3.3 Tipográfiai skála (Titillium Web, más nem szállítható)
| Szerep | Méret | Stílus | Szín |
|---|---|---|---|
| Lapcím (MASTER, MOD MATRIX) | 24 | SemiBold, nagybetű | `text.value` |
| Csoportcím | 13 | SemiBold, nagybetű, +1 px betűköz | `text.label` |
| Vezérlőfelirat | 14 | SemiBold, nagybetű | `text.caption` |
| Érték | 20 (hero 24) | SemiBold | `text.value` |
| Gomb, fül | 18 | SemiBold | `text.value` / kijelölve `radio.active.text` |
| Tengely, tick | 11 | Regular | `preview.annotation` |
Szabály: 12 px alatt csak tengelyfelirat lehet. A 9–10 px-es `font.micro` / `font.tiny` vezérlőfeliratként tilos (lint-szabályként is felvehető).

### 3.4 Csoportosítás a „egységes vászon” döntésen belül
Két változat; az **A** nem igényel új owner-döntést:
- **A (javasolt):** csoportcím (13 px) + alatta 1 px natív `Separator` (`panel.border` #1d2855) a csoport teljes szélességében, csoportok között 24 px köz (a cellaközök 8 px-esek, így a csoporthatár a térközből is olvasható).
- **B (owner-döntés kell, mert a 2026-11-24-es döntés a háttereket kivette):** csoportonként egy natív `Image` kártya (`surface.panel` #121930 kitöltés, `panel.border` keret, `radius.panel` 12), a kártya-PNG-t a méret-tokenekből generálva, csoportméretenként egyet. Ez a régi hierarchiát adja vissza, teljes oldalas háttér nélkül.

### 3.5 Állapotjelzés (három szint, mindig megkülönböztethető)
| Állapot | Szöveg | Jel |
|---|---|---|
| BE | `accent.cyan` #2ee6f0 | teli LED-pont (natív `Indicator` vagy Button-kép), 8 px |
| KI, de állítható | `text.caption` #aab2d8 (9,3 : 1) | üres LED-gyűrű |
| Letiltott / inaktív modul | `#384252` (1,9 : 1) + 40 % átlátszó vezérlő | nincs pont |
Ehhez az `ActiveText` OFF tokenjét kell kettéválasztani (`active_text.off` → caption szín; új `active_text.disabled` = a mostani #384252), és a LED-pontot a komponens kapja meg. Ez az owner által kért „a szín jelzi az állapotot” elvet megtartja, csak a KI állapotot teszi olvashatóvá.

### 3.6 Kijelzők a host-síkon is
Minden kijelző-téglalapnak legyen natív kerete és statikus tartalma a host-síkon: `Image` vagy `Path` kerettel (`radius.panel`), a cím (pl. „FILTER 1 · RESPONSE”) és a tengelyfeliratok natív `Label`-ként. Az overlay csak a görbét, a kitöltést, a fogantyúkat és az élő jelzőket rajzolja. Így overlay nélkül is rendezett, keretezett lap látszik (2.1). Az ENV-hez az SDK natív `Envelope` (DAHDSR) widgetje is kipróbálható tartalék-görbének (SDK §6.13, „test on hardware”).

### 3.7 Q-keret (Gen 1)
A Q-keret mindig egy fél sor (4 cella, 624×112) + 4 px, soha nem metsz kijelzőt vagy legördülőt. Ha egy Q-bank tagjai nem férnek egy fél sorba, a tagokat kell átrendezni, nem a keretet nyújtani. (Gen 2 / XL: keret nincs, a rács ugyanaz.)

## 4. Oldalankénti elemzés és javaslat

Jelölés a vázlatokban: `[ ]` cella (150 széles), `[=====]` széles mező, `( )` poti, `<x>` léptető, `▣` gomb. Koordináták tab-koordinátában.

### 4.1 Play
**Régi jó:** fejléc a szomszédok nevével („Noise | Sub | Osc A”, x 16–420 y 122–172), meleg színátmenetes Sub-görbe, egységes „FELIRAT / érték egységgel” cellák (ATTACK 5.0 ms), az LFO-ban DIVISION léptető.
**Romlott:** a fejléc csak „< Sub >” (szomszéd neve nélkül) és a Sub fejléc szürke, a másik kettő kék (x 14–422 vs 446–834 y 124–172); Sub-görbe szürke (x 22–410 y 270–450); értékek mértékegység nélkül; „RIse” elírás; SMOOTH értéke 7 px (x 1205 y 691); a Q-keret-maradvány (2.8); a host-síkon mindhárom kijelző üres (2.1).
**Jobb:** HZ/BPM/KEY gombok.
**Javaslat:** három 410 px-es oszlop (x 12–422, 435–845, 858–1268).
```
y 8   [< Noise |  SUB  | Osc A >]  [< Env 6 | ENV 1 | Env 2 >]  [< RND 4 | LFO 1 | LFO 2 >]   (Carousel natívan: 3 Label + 2 Button)
y 72  ┌ kijelző 410×216 ┐ power ⏻ bal felső, ⤢ jobb felső (AGENTS 17), keret natív Image/Path
y 296 [WAVE ikonsor 6×64]        [ATTACK][DECAY]                   [SHAPE <Custom>][SYNC HZ|BPM|KEY]
y 408 [OCT][PAN][LEVEL]          [SUSTAIN][RELEASE]                [RATE/DIV][RISE][SMOOTH●]
```
Cellák 2×200 (ENV) ill. 3×134 (Sub, LFO alsó sor); potik 64 px; értékek egységgel; a Sub Q-bank kerete az alsó két sort fogja, a választót nem takarja. Méret: **M**.

### 4.2 OSC A / B / C
**Régi jó:** egy fejléc-sávban power, 5 típus-ikon, forrás-léptető, PAN/LEVEL; alatta FRAME/PHASE/RAND/MAP mezősor, jobbra TUNE 2×2 mező + TRACK/MORE; alul UNISON (WT POS, VOICES, DETUNE, BLEND) és WARP (2× mennyiség + típus + fázis) kártya. Sűrű, de minden csoport keretezett és olvasható (feliratok 12–14 px, értékek 20–22 px).
**Romlott:** 2.6 (típus legördülő), 2.8 (keret a kijelzőn), 2.7 (unison-blokk), 2.9 (WT POS, BLEND, WARP mennyiség, PHASE eltűnt), a TUNE mezők 7 px-es címkéi, PAN/LEVEL ≈35 px-es potikkal a jobb alsó sarokban (x 868–1205 y 648–680), a „Filter 1” célválasztó felirat nélkül lebeg (x 1125–1268 y 150–192). OSC B/C-n nincs OFF felirat.
**Jobb:** alfül-sor SUB/A/B/C/NOISE.
**Javaslat:**
```
H  y 8–64   ⏻ [WAVE][MULTI][SAMPLE][GRAIN][SPEC] [<  Saw  >======] ▣BROWSE [→ Filter 1]  (PAN 40)(LEVEL 40)
D  y 72–288 ┌ hullámkijelző x 12–964 (952×216), OFF-címke ha ki ┐   TUNE x 976–1268: [OCT][SEM] / [FIN][CRS] / ▣TRACK● ▣MORE
R1 y 296–408 [FRAME][PHASE][RAND][WT POS] | [VOICES <1>][DETUNE][BLEND][▣ UNISON…]
R2 y 416–528 [WARP 1 ( )][<None>======][PHASE] | [WARP 2 ( )][<None>======][PHASE]
S  y 580–628 SUB | A | B | C | NOISE
```
A típus-ikonsor 5×72 px natív Button-kép; a célválasztó „DEST” felirattal; PAN/LEVEL a meglévő `level` komponens 40 px-es kompakt változata (`level.compact_48_rects`), felirattal és egységgel. A Q-bankok a fél sorokra esnek (R1 bal, R1 jobb, R2 bal, R2 jobb), a fejléc tagjai a meglévő Basic Q-csoport. A kijelző és a keret nem fedik egymást. Méret: **L** (három lap egy közös képernyőn: egyszer kell megcsinálni).

### 4.3 Sub
**Régi jó:** hat képes hullámforma-gomb, OCT mező, PAN/LEVEL a fejlécben; alatta TUNE / PHASE / ROUTING kártya (DEST léptető, BLEND, BUS 1, BUS 2) és jól látható ENV 1 / MONO / FREE RUN jelzőpontok.
**Romlott:** 2.6, 2.5 (a három kapcsoló szürke szöveg x 564–1022 y 572), BUS 1/2 hiányzik, BLEND szürke poti a jobb szélen (x 1176–1246 y 516–580), a power a bal oldalon lebeg (x 20–68 y 230–278), a bal felső negyed üres.
**Javaslat:** az OSC-rács egyszerűsítve:
```
H  ⏻ [SINE][HALF][TRI][SAW][SQR][PULSE]  [OCT -1]  [DEST <Effects only>====]  (PAN 40)(LEVEL 40)
D  y 72–312 teljes széles görbe (meleg színátmenet maradhat), OFF-címke ha ki
R1 [CRS][PHASE][●ENV 1][●MONO] | [●FREE RUN][BLEND][BUS 1][BUS 2]
S  SUB | A | B | C | NOISE
```
Méret: **M** (az OSC-vel együtt).

### 4.4 Noise
**Régi jó:** SOUND léptető + FILTER/PAN/LEVEL a fejlécben; TUNE (PITCH, FINE, KEY TRACK) és NOISE (KIND, STEREO, ENV 1) kártya.
**Romlott:** BLEND érték csonkolva („0....”), három potiméret (2.7), KEY TRACK és ENV 1 szürke szöveg, PITCH/FINE apró dobozok 7 px-es felirattal (x 27–360 y 548–595).
**Javaslat:**
```
H  ⏻ [KIND <Noise>======] [SOUND <White Noise>======]  [DEST <Filter 1>]  (PAN 40)(LEVEL 40)
D  y 72–312 zajkijelző
R1 [PITCH][FINE][●KEY TRACK][FILTER] | [STEREO][BLEND][●ENV 1][   ]
S  SUB | A | B | C | NOISE
```
Méret: **S** (ha az OSC-rács kész).

### 4.5 ENV
**Régi jó:** ENV 1–6 fülsor + TARGETS számláló; nagy görbe; három keretezett csoport egyforma cellákkal, 12 Q-jelzés.
**Romlott:** 2.2, 2.3 (vegyes kis- és nagybetű, 9 px-es csoportcím), 2.5 (BEATS/RETRIG), a TARGETS csak cián szöveg (x 1122 y 151) szám nélkül.
**Jobb:** „1 / 2· / +” választó.
**Javaslat:** a jelenlegi geometria szinte jó, csak a rácsra kell igazítani:
```
H  ENV [1][2·][+]                                              ▣ TARGETS 3
D  y 72–312 görbe (natív keret; tartaléknak kipróbálható a natív Envelope widget)
R1 ADSR: [ATTACK][DECAY][SUSTAIN][RELEASE] | SHAPE: [A CURVE][D CURVE][R CURVE][   ]
R2 TIME: [DELAY][HOLD][●BEATS][●RETRIG]
```
Feliratok 14 px nagybetűvel, értékek 20 px egységgel. Méret: **S**.

### 4.6 LFO
**Régi jó:** 14 forrásgomb egy sorban, EDIT és TARGETS; 13 alak-ikon rácsban; RATE / TIMING / MOTION kártya.
**Romlott:** 2.6 (forrás és alak legördülő), 2.7 (három poti-elrendezés), DIVISION / TRIP / DOT nem látszik (lehet, hogy HZ módban szándékosan rejtett: nem ellenőriztem BPM módban), HOST / MONO szürke szöveg.
**Jobb:** HZ/BPM/KEY nagy szegmentált gombok.
**Javaslat:**
```
H  [1][2][3][4][5][6][7][8][9][10][R1][R2][R3][R4]   ▣EDIT  ▣TARGETS      (14 × 64 px gomb + 2 akció)
D  y 72–312 alakkijelző x 12–828 | alakrács x 840–1268: 7+6 ikon 60×56 (2 sor) + ●HOST
R1 RATE: [RATE/DIV][RISE][SMOOTH●][TRIP/DOT] | TIMING: [HZ|BPM|KEY 2 cella][PHASE][DELAY]
R2 MOTION: [DIRECTION <Fwd>][SWING][STEREO][●MONO] | [MODE <Retrigger>==========]
```
Méret: **M**.

### 4.7 RND
Nincs régi önálló kép (a régi LFO lap R1–R4 gombjain ért el). Az új lap: jó kijelző (x 8–1272 y 183–525), alatta egy RATE poti, HZ/BPM/KEY, szürke „STEREO” (x 716 y 625), MODE legördülő; a sáv nagy része üres.
**Javaslat:** az LFO-rács, ugyanazzal a fejléccel (az RND-k a forrásgombsorban), R1: [RATE][STYLE ikonok…][SMOOTH][●STEREO] | [HZ|BPM|KEY][MODE]. Méret: **S** (az LFO-val együtt).

### 4.8 Filter
**Régi jó:** fejléc F1/F2/F1+F2 választóval, típus- és meredekség-léptetővel, OUTPUT M gombbal; széles válaszgörbe; MAIN / TRACK / OUT / ROUTING kártya 12 egyforma cellával.
**Romlott:** a vezérlősáv 40 px-es potikra és 6–9 px-es feliratokra zsugorodott (x 20–620 y 580–710), OUT csoportban csak BLEND, LEVEL/PAN/ROUTING/COMB nem látszik; az EDIT sor hat apró mezője (x 788–1265 y 622–718); CLEAN, SNAP, LEGATO szürke szöveg; az F1 választó apró (x 12–60 y 135–167).
**Jobb:** NOTE/VELO görbeszerkesztő, válaszgörbe saját keretben, MODEL/STYLE legördülők olvashatók.
**Javaslat:** két fél képernyő, mindkettőn azonos rács:
```
H  [F1][F2][F1+F2]  [MODEL <Ladder>====] [STYLE <12 dB>] ●CLEAN | [NOTE][VELO] ▣TARGETS
D  bal: válaszgörbe x 12–636 (⏻ bal felső, ⤢ jobb felső) | jobb: kulcsgörbe x 644–1268
R1 MAIN: [CUTOFF][RESO][DRIVE][MIX]          | EDIT: [POINT][X][Y][CURVE]
R2 TRACK: [KEYTRACK][●ENV 1][VAR][COMB]      | [GRID X][GRID Y][●SNAP][●LEGATO]
R3 (kompakt, y 560–620) OUT: [LEVEL][PAN][BLEND][ROUTING <Parallel>]
```
Méret: **M**.

### 4.9 FX (folyamatban: másik worker; csak megfigyelés)
- Szintetikus `fx.jpg`: jó irány (görbe bal x 8–636 y 208–620, vezérlők jobbra). Megfigyelések: a TYPE és FILTER ORDER legördülők szürkék (x 655–942), eltérnek a többi lap kék választóitól; a „RACK” felirat (x 82 y 152) magában áll, a „RACK MAIN” doboz fekete (x 907–1135 y 144–200), más stílusú; a potik ≈55 px-esek a 18–20 px-es szöveghez képest kicsik; a 16 slot-kártya felirata üres, csak az első („Dist MAIN”) nevesített; a „MOVE” gomb fölött szentinel-maradvány (x 30 y 20).
- Key 61 `fx.png` / `n1.png`: más elrendezés fut (feliratok ≈9–10 px, nincs RACK doboz, a bal kétharmad x 0–640 y 310–730 üres a host-síkon). Az „+ ADD” felirat 8 px-es (x 1237 y 206).
- Az FX a minta a többi lapnak: **display bal / vezérlők jobb**, nagy feliratok.

### 4.10 Mixer
**Régi jó:** útvonal-diagram felül, csatornánként power, célválasztó, nagy értékek („50.0%”, „-2.6 dB”, „PAN C”), vízszintes PAN/B1/B2 csúszkák nagy értékkel.
**Romlott:** a kikapcsolt csatornák (SUB, B, C, NOISE) útvonalmezője üres a szintetikus képen (x 10–125 y 196–252 stb.), pedig a régin „Effects only” / „Filter 1” látszott; az értékmezők 7 px-esek (x 16–98 y 430–448 „50.0”); az F1/F2 skála felső tickjei egymásra írva (x 660–680 y 288–330 „+1 / 0 / -6”), és a fader-sapka rájuk esik; PAN/B1/B2 ≈30 px-es potik 7 px-es felirattal.
**Eltérés a Key 61-hez:** a `mix.png`-n minden S/M aktív színű, a szintetikus képen a kikapcsolt csatornák S/M-je szürke. A kettő közül az egyik nem a valós állapotot mutatja.
**Jobb:** S/M, útvonal-fülsor, teljes magasságú busz-faderek, sztereó mérők (a régin is volt).
**Javaslat:** a sáv-geometria (115 px) maradjon; az értékmező 18 px-es szöveggel és egységgel; a dB-skála ticket a faderhossz alapján ritkítani (min. 14 px függőleges távolság két felirat között: F1/F2-n „0 / -12 / -24 / -36 / -60”); a kikapcsolt csatorna célmezője a célt halványan mutassa, ne üresen; PAN/B1/B2 maradhat poti, de 40 px és 14 px-es felirat. Méret: **S–M**.

### 4.11 Mod
**Régi jó / romlott:** ld. 2.9. A sorok, a legördülők és az amount-csúszkák szinte azonosak.
**Javaslat:** vissza a fejléc-sor (SOURCE / TARGET / AMOUNT / POLARITY / STATE, 13 px) és a sorszám-oszlop (x 12–56), a „→” nyíl a két legördülő között, „N / 64 SLOTS” a +ADD mellett, görgetősáv a jobb szélen; UNI/BI és ON/OFF a meglévő `mod_btn_uni/bi/on/off.png` natív képekkel (ezek már megvannak a `components/native/mod_row/img/`-ben), nem 7 px-es szöveggel; a lapcím 24 px. Méret: **S**.

### 4.12 Macros
**Régi jó:** 8 kártya 2×4-ben (poti, név, érték egységgel, célpont-lista vagy „NO TARGET – TAP TO ADD”), bankválasztó, alul spektrum.
**Romlott:** 2.9 (célpontlista), 8 egyforma TARGETS gomb (x 36–1244 y 674–722) vizuális zaj, értékek 7 px (y 600), a spektrum üresen a lap felét foglalja (x 8–1272 y 190–470).
**Jobb:** mind a 8 makró egy sorban; Q-bank keret 4 cellára.
**Javaslat:**
```
H  MACROS
D  y 64–216 spektrum (152 magas; üresen is natív kerettel és skálával)
R  y 228–620 nyolc MacroCard 150×392 egy sorban: [név 14 px][poti 72][érték 20 px egységgel]
             [célpont-lista: legfeljebb 3 sor 12 px + „+N”] [▣ TARGETS csak ikon, 56 px]
```
A célpont-lista statikus natív Label-ek a meglévő `macro_card` komponensben. Méret: **M**.

### 4.13 Voice (folyamatban: másik worker; csak megfigyelés)
- A régi függőleges lépés-oszlopok (x 272–1270 y 198–428, a pont helye mutatja az értéket) egy pillantással olvasható mintát adtak; az új 8 poti (x 290–1245 y 236–302) egyenként olvasandó. Ha az SDK `Slider` függőleges változata natívan elérhető, az visszaadná a régi „step-sequencer” képet.
- A LENGTH értéke a dobozban 7 px-es (x 937–1015 y 131–185), a PAN „MID” szintén (x 1113–1222).
- A TARGETS (OSC A … NOISE, x 990–1183 y 548–695) cián szöveg jelzőpont nélkül: a ki/be állapot nem látszik (2.5).
- A „10x DETUNE” (x 49 y 696) a letiltott színben van.
- A sor-összegző (y 456) jobb: címke és érték külön oszlopban, de a címke-érték párok távolsága egyenetlen (PAN x 181 / MID x 275, DETUNE x 370 / 0 c x 464).

### 4.14 Master (folyamatban: másik worker; csak megfigyelés)
- A csúcsmérő (x 14–114 y 280–404) skálája 0 / -12 / -24 egymásra zsúfolva (0 és -12 között ≈16 px), mérősáv nincs (nyugalmi állapot; a `master_old.jpg`-n lila-cián sávok látszanak).
- „CLIP” gomb (x 35–93 y 224–272) nagyobb, mint a régi, ez jó.
- A Q-keret (x 123–641 y 211–343) VOLUME / SPREAD / STEREO / VOICE OVERRIDE négy tagot fog: helyes, de a VOICE OVERRIDE legördülő magasabban ül (y 254–310), mint a három poti cellája.
- PITCH: TUNE (x 728–785), TRANSPOSE mező (x 946–990) és BEND mező (x 1199–1265) között ≈200 px üres; a régi BEND UP / BEND DOWN két mező volt.
- VOICES: POLY mező 7 px-es feliratokkal (x 27 / 141–203 y 484), LEGATO / POLY LEG / PHASE LEG szürke szövegek, a hangszámláló sáv hiányzik (2.9), „PAD %” 6 px (x 220 y 684).
- GLIDE: a GLIDE CURVE (x 1020–1265 y 457–722) rendben; ALWAYS / SCALED szürke szöveg.
- A régi Master (`master.png` régi) a legjobb sűrűségű lap volt: ez legyen a visszatérési cél, a 3. fejezet rácsán.

## 5. Függőségi sorrend (hol a legnagyobb nyereség)

1. **Közös design-tokenek és szabályok** (3.1–3.5, 3.7): rács-, tipográfia-, állapot-tokenek, `ActiveText` OFF/disabled szétválasztás + LED-pont, lint-szabály a 12 px alatti vezérlőfeliratra és a mértékegység nélküli időre/dB-re. Ez minden lapot egyszerre javít, és a folyamatban lévő FX/Master/Voice munkát is ugyanarra a nyelvre tereli.
2. **Kijelzőkeretek a host-síkon** (3.6): egy komponens (natív keret + cím + tengely), minden görbés lapon újrahasználva. Azonnal megszünteti a Key 61 „lyukakat”.
3. **OSC A/B/C + Sub + Noise** egy közös képernyőként: a legrondább és a leggyakrabban használt lapcsoport (5 fül).
4. **Play** (ugyanazokat a cellákat használja, mint az OSC/ENV/LFO).
5. **LFO + RND**, **Filter**, **Macros**.
6. **ENV, Mod, Mixer** finomítás (kicsi munka).
7. FX, Master, Voice: a futó workerek a 3. fejezet nyelvét kövessék; utána csak egyeztetés.

## 6. Rangsorolt megvalósítási lista

| # | Oldal | Változtatás | Méret | Függőség |
|---|---|---|---|---|
| 1 | közös | Rács-, tipográfia- és állapot-tokenek a `sizes.json` / `colors.json`-ban (3.1–3.3, 3.5); lint: vezérlőfelirat ≥ 12 px, idő/dB/% érték egységgel | M | – |
| 2 | közös | `ActiveText`: OFF = `text.caption`, új `disabled` token, LED-pont (`Indicator` vagy Button-kép) | S | 1 |
| 3 | közös | Natív kijelzőkeret-komponens (Image/Path keret, cím, tengely) a host-síkon | M | 1 |
| 4 | közös | Csoportcím + `Separator` (3.4 A); a B változat (kártya-Image) owner-döntésre | S | 1 |
| 5 | közös | Szentinel-zóna lefoglalása a rácsban (tab y 0–8, x 0–64 / 1216–1280) | S | 1 |
| 6 | OSC A/B/C | 4.2 elrendezés: típus-ikonsor, kijelző a Q-kerettől külön, WT POS / BLEND / WARP mennyiség + fázis vissza, OFF-címke | L | 1–4 |
| 7 | Sub | 4.3: hullámforma-ikonsor, BUS 1/2 vissza, kapcsolók LED-del | M | 6 |
| 8 | Noise | 4.4: egy potiméret, BLEND csonkolás vége | S | 6 |
| 9 | Play | 4.1: szomszédneves fejléc, egységes cellák egységgel, „Rise” elírás, Q-keret a cellákon | M | 1–3 |
| 10 | LFO + RND | 4.6–4.7: forrás-gombsor, alakrács, EDIT, egységes cellák | M | 1–3 |
| 11 | Filter | 4.8: két fél rács, OUT/ROUTING/COMB vissza, EDIT sor cellákban | M | 1–3 |
| 12 | Macros | 4.12: célpont-lista a kártyán, értékek 20 px, kisebb spektrum | M | 1 |
| 13 | ENV | 4.5: rácsra igazítás, TARGETS szám, BEATS/RETRIG LED | S | 1–2 |
| 14 | Mod | 4.11: fejléc, sorszám, nyíl, slotszám, görgetősáv, UNI/ON képek | S | 1 |
| 15 | Mixer | 4.10: értékek 18 px egységgel, skála-ritkítás, kikapcsolt csatorna célja halványan, S/M állapot egyeztetése a készülékkel | S–M | 1 |
| 16 | FX / Master / Voice | folyamatban: a 4.9, 4.13, 4.14 megfigyelései a futó workereknek | – | 1 |

## 7. Ami nem ellenőrzött

- XL-en egyik új natív lapot sem láttam (`gk-xl-shots` csak a projektválasztót tartalmazza).
- A Key 61 képeken az overlay-sík nincs rajta; a görbék, Q-keretek és élő jelzők valós megjelenését a készüléken nem láttam.
- A `pre-native-screens-sheet.png` nem létezik a megadott helyen.
- A Key 61 képek közül tízet (h_filter, h_fx, h_master, h_mix, n2, n3, next, r0, r_master, s1) nem nyitottam meg.
- Nem ellenőriztem, hogy a natív `Envelope`, `Indicator`, függőleges `Slider` és `Path` widgetek a Key 61 / XL firmware-en a javasolt módon működnek-e (az SDK „less tested” / „test on hardware” jelölést ad nekik).
- A régi és az új képek preset-állapota több helyen eltér (0. fejezet); az ebből adódó eltéréseket nem értékeltem.
- A szintetikus `fx.jpg` és a Key 61 FX képe közti eltérés oka (régebbi build vagy eltérő render) nincs eldöntve.
