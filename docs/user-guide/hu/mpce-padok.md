> Forrás: docs/user-guide/mpce-pads.md (commit: 74d2181ef1f72d8c1bebcf755e8696ae94c438c6)

# MPCe Expresszív 3D Padok (Gen 2) és a SynthyBoy

Ez a fejezet bemutatja, hogyan használhatók a második generációs Akai MPC hardverek (MPC XL és MPC Live III) expresszív padjai a SynthyBoy szintetizátorral. Elmagyarázza, milyen fizikai mozdulatokat érzékelnek a hardveres padok, hogyan kezeli ezeket az MPC Standalone OS, milyen MIDI-üzenetek jutnak el valójában a SynthyBoy plugin sávra, valamint hogyan konfigurálható a SynthyBoy modulációs mátrixa és a globális padnyomás-réteg a mélyen expresszív játékhoz.

---

## 1. Hardvergenerációk: Gen 2 (MPCe) vs Gen 1

### 1.1 Második generációs hardver (MPC XL, MPC Live III)

A második generációs Akai standalone hangszerek újratervezett, többdimenziós szenzoros padokkal rendelkeznek, amelyeket az Akai hivatalosan **MPCe padoknak** vagy **expresszív padoknak** (expressive pads) nevez ([MPC 3.6 Firmware Update Release Notes, 1. oldal]; [MPC Live III, MPC XL User Guide v3.10.1, 4., 11. oldal]).

A hivatalos Akai kézikönyvek szerint az MPCe hardveres szenzorok egyszerre három különálló fizikai dimenziót mérnek:
1. **Ütési leütésérzékenység (Strike Velocity):** Az ujj kezdeti becsapódási ereje [User Guide v3.10.1, 63. oldal].
2. **Folyamatos padnyomás (Polyphonic Aftertouch):** A lenyomva tartott hang teljes időtartama alatt az egyes padokra egyenként kifejtett folyamatos lefelé irányuló nyomás [User Guide v3.10.1, 64., 350. oldal; MPC 3.6, 1. oldal].
3. **Ujjpozíció a felületen (X és Y tengely):** Az ujj vízszintes (X) és függőleges (Y) koordinátái a pad felületén [User Guide v3.10.1, 64., 350. oldal; MPC 3.6, 2. oldal].

Ezekből a dimenziókból az MPC OS másodlagos előadói mérőszámokat származtat a belső dobsávokon (Drum Tracks):
- **Pad XY Center Dist:** Az érintés sugárirányú távolsága a pad középpontjától [MPC 3.6 Firmware Update Release Notes, 2. oldal].
- **Pad XY Press Dist:** A pad felületén a kezdeti leütési ponttól megtett távolság [MPC 3.6, 2. oldal].
- **Pad dX / Pad dY:** Az irányított vízszintes és függőleges elmozdulás a kezdeti érintési ponttól [MPC 3.6, 2. oldal].
- **Pad kvadránsok (Pad Quadrants):** Az egyes padok felosztása négy különálló sarokzónára (bal alsó, jobb alsó, bal felső, jobb felső), amelyekkel külön dob-artikulációk (pl. flam, buzz, roll és paradiddle) vagy hangmintarétegek szólaltathatók meg ([MPC Live III, MPC XL User Guide v3.10.1, 13., 22., 225., 234. oldal]; [MPC 3.6, 5. oldal]; [MPC 3.7 Update Release Notes, 2–3. oldal]).
- **8×8 (64 pad) előadói mód (8×8 Performance Mode):** A 4×4-es fizikai rács felosztásának lehetősége négy egyidejű padbankra (A–D vagy E–H bankok), lehetővé téve 64 külön hang vagy klip egyidejű indítását ([MPC 3.10 Release Notes, 4–5. oldal]; [User Guide v3.10.1, 72., 380. oldal]).
- **MPCe Q-Link mód (MPCe Q-Link Mode):** Az MPCe padok érintésérzékeny makrófelületként való használata automatizációs paraméterek modulálására ([MPC 3.7 Update Release Notes, 1–2. oldal]; [User Guide v3.10.1, 337., 350. oldal]).

### 1.2 Első generációs hardver (MPC Key 61, MPC One, Live I/II, MPC X)

Az első generációs MPC standalone készülékek (pl. MPC Key 61, MPC Key 37, MPC One, MPC One+, MPC Live I és II, MPC X, MPC X Special Edition) **nem** rendelkeznek MPCe padokkal [MPC Standalone OS User Guide v3.10.1, 7. oldal]:
- Padjaik kizárólag **ütési leütésérzékenységet (strike velocity)** és **nyomást (pressure)** érzékelnek [Standalone OS User Guide v3.10.1, 62., 333. oldal].
- Nincs rajtuk **X/Y felületi pozíciókövetés**, **nincs kvadránsérzékelés**, **nincs 8×8 padfelosztás**, és **nincs MPCe Q-Link mód**.
- A billentyűs modellek (mint az MPC Key 61) félig súlyozott billentyűzettel rendelkeznek, amely szabványos **csatorna-utánnyomást (Channel Aftertouch**, `0xD0`) közvetít [Standalone OS User Guide v3.10.1, 369., 479. oldal], amely globálisan hat az összes megszólaló hangra, nem pedig az egyes hangokra egyenként.
- A Gen 1 eszközök 16 fizikai padja vezérlő módban konfigurálható Channel Pressure vagy Poly Aftertouch küldésére [Standalone OS User Guide v3.10.1, 333. oldal], de hiányzik belőlük a Gen 2 MPCe padok folyamatos térbeli érzékenysége.

---

## 2. A gazdarendszer valósága: mi jut el a plugin sávra?

Bár az Akai kézikönyvek részletesen taglalják az MPCe padok kiterjedt modulációs lehetőségeit a belső **Drum Tracks** sávokon belül, a külső VST2 plugin sávok a gazdarendszer MIDI-továbbítási láncának vannak alávetve.

Az alábbi működést közvetlenül egy MPC OS 3.10.1.12-t futtató MPC XL-en mértük diagnosztikai MIDI-naplózással az `Sbtest` projektben ([docs/MPC.md, "MPC XL pads"](../../MPC.md#mpc-xl-pads)):

| MIDI-esemény / Paraméter | Full Level BE | Full Level KI | Viselkedés plugin sávokon |
|---|---|---|---|
| **Note On / Note Off** | Igen | Igen | A hangok az 1. MIDI-csatornán érkeznek. |
| **Strike Velocity (ütési dinamika)** | Mindig 127 | Dinamikus (pl. 96, 127) | Továbbítva a SynthyBoy **Velocity** forrásához. |
| **Polyphonic Aftertouch (`0xA0`)** | **Nincs (0 üzenet)** | **Folyamatos adatfolyam** | 1..127 közötti értékek folyamata lenyomva tartott padonként, kb. 6 ms-onként (~170 Hz). Továbbítva a SynthyBoy **Poly Aftertouch** forrásához. |
| **Channel Pressure (`0xD0`)** | Nincs (0) | Nincs (0) | Az MPC padok nem küldik a plugin sávra. |
| **Pitch Bend** | Nincs (0) | Nincs (0) | Padmozdulatok nem küldik. |
| **CC üzenetek (beleértve a CC 74-et)** | Nincs (0) | Nincs (0) | Padmozdulatok nem küldik. |
| **MPE csatornarotáció** | Nincs | Nincs | Minden hang szigorúan az 1. MIDI-csatornán marad. Nincs MPE hangallokáció. |

### 2.1 Fő tudnivalók a SynthyBoy játékosok számára

1. **A Velocity és a padonkénti nyomás natívan működik:**
   - A pad leütése **Velocity** adatot küld (amelyet a Master VOICES blokk `VEL TRACK` / `master.velocity_track` paramétere skáláz).
   - A lenyomva tartott padba való belenyomás hangonkénti **Poly Aftertouch** (`0xA0`) üzeneteket küld, lehetővé téve, hogy egy akkordban minden ujj egymástól függetlenül modulálja a hangzást.
2. **A pad X/Y pozíciója NEM jut el a pluginhoz:**
   - Az MPC OS nem fordítja le a padokon történő vízszintes vagy függőleges ujjmozdulatokat szabványos MIDI CC-kre (mint a CC 74 Brightness vagy Pitch Bend) a külső pluginek számára.
   - Az olyan modulációs források, mint a **Slide** (MPE Y / CC 74, 36. választás), **Expression X** (56. választás), **Pitch Wheel** (26. választás) és **Mod Wheel** (27. választás) **nem mozdulnak meg a pad mozdulataitól**.
3. **A Full Level letiltja az összes padnyomást:**
   - Amikor a hardveres **Full Level / Set Level** gomb aktív [User Guide v3.10.1, 389. oldal], vagy ha a **Preferences > General > Enable Pads User Level By Default** be van jelölve [User Guide v3.10.1, 66. oldal], a velocity 127-re van rögzítve, és a gazdarendszer **nulla aftertouch üzenetet küld**.
   - > [!IMPORTANT]
     > Ahhoz, hogy az expresszív padnyomást használni tudjuk a SynthyBoy-jal, a **Full Level funkciónak KIKAPCSOLVA (OFF) kell lennie**.

### 2.2 Gazdarendszeri beállítási ellenőrzőlista

Annak biztosítására, hogy az MPC hardver megfelelően továbbítsa az expresszív adatokat:
- **Preferences > Hardware:** Ellenőrizzük a **Pad Threshold**, **Pad Sensitivity** és **Pad Curve** beállításokat (az A görbe lineáris; a B, C és D exponenciális) [User Guide v3.10.1, 63. oldal]. Használjuk a képernyőn látható **Velocity & Aftertouch** kivezérlésjelzőket a billentés kalibrálásához.
- **Preferences > Sequencer:** Győződjünk meg arról, hogy a **Record Pad Aftertouch Events** be van kapcsolva (`On`), így a nyomásmodulációk rögzítésre kerülnek felvételkor [User Guide v3.10.1, 64. oldal]. (Megjegyzés: a **Record Pad XY Events** csak a belső dobsávok automatizálására van hatással).
- **Pad Perform:** A Pad Perform használatakor ügyeljünk arra, hogy az aftertouch engedélyezve legyen a Pad Perform beállítási ablakában (`Notes / Pad CTRL` vagy a **Use Aftertouch** jelölőnégyzet) [User Guide v3.10.1, 83. oldal].

---

## 3. SynthyBoy modulációs források: mi mozdul és mi nem?

A SynthyBoy egy 64 férőhelyes (slot) modulációs mátrixot kínál 57 modulációs forrással ([core/src/modulation.cpp](../../../core/src/modulation.cpp); [docs/params.json](../../params.json)):

| Forrásazonosító | Modulációs forrás neve | Típus | Kap adatot az MPC padoktól? | Megjegyzések / Cél viselkedése |
|---|---|---|---|---|
| **23** | `velocity` (Velocity) | Unipoláris (`0..1`) | **IGEN** | Kezdeti leütési sebesség hangonként. Skálázza a `master.velocity_track` (Master VOICES blokk: `VEL TRACK`). |
| **38** | `poly_aftertouch` (Poly Aftertouch) | Unipoláris (`0..1`) | **IGEN** | Padonkénti és hangonkénti folyamatos aftertouch adatfolyam (`0xA0`, 0.0 és 1.0 közötti értékek). Kiértékelése hangonként történik valós időben (`core/engine-own/voice/voice.cpp:645`). |
| **25** | `aftertouch` (Aftertouch) | Unipoláris (`0..1`) | **NEM (Padokról)** | Globális csatornanyomás (Channel Pressure, `0xD0`). Billentyűzetekről érkezik (pl. MPC Key 61), de MPC padokról nem. |
| **36** | `slide` (Slide / MPE Y) | Unipoláris (`0..1`) | **NEM** | MIDI CC 74 vagy MPE Y vezérli. Nem kapja meg a padok Y-tengelyű pozícióját. |
| **56** | `expression_x` (Expression X) | Bipoláris (`-1..+1`) | **NEM** | MPE hangkifejezés vezérli (Pan/X). Nem kapja meg a padok X-tengelyű pozícióját. |
| **35** | `lift` (Lift / Release Velocity) | Unipoláris (`0..1`) | **NEM / Nem igazolt** | Felengedési sebesség (Note-off release velocity). Az MPC padok jellemzően fix felengedési sebességet küldenek. |
| **26** | `pitch_wheel` (Pitch Wheel) | Bipoláris (`-1..+1`) | **NEM** | Csak fizikai hajlítókerék vagy touch strip érintőcsík vezérli. |
| **27** | `mod_wheel` (Mod Wheel) | Unipoláris (`0..1`) | **NEM** | Csak fizikai modulációs kerék vagy touch strip érintőcsík vezérli. |

---

## 4. Master oldal beállításai: Hangolás és MPE (Tuning & MPE)

A SynthyBoy Master oldalán egy külön **TUNING & MPE** (Hangolás és MPE) rész található ([docs/MPC.md, 1142. sor](../../MPC.md); [docs/params.json](../../params.json)):

```
+-----------------------------------------------------------------------+
|  MASTER: TUNING & MPE                                                 |
|  [ SCALE & KEYBOARD ]        [ TUNING ]            [ MPE ]            |
|  Scale: 12-TET               Tune: 440 Hz          MPE ON: Off        |
|  Root: C                     MTS-ESP: Off          BEND RANGE: 48 st  |
|                                                    XYZ TO MACROS: Off |
|                                                    Y TO WHEEL: Off    |
|                                                    BEND TO X: Off     |
|                                                    Y BIPOLAR: Off     |
+-----------------------------------------------------------------------+
```

### Az MPE paraméterkészlet
- **MPE On** (`master.mpe.on`): Engedélyezi a többcsatornás MPE hangallokációt.
- **MPE Pitch Bend Range** (`master.mpe.bend_range`): A tagcsatorna hajlítási tartománya (1 és 96 félhang között, alapértelmezett: 48).
- **MPE XYZ to Macros** (`master.mpe.xyz_macro`): Hozzárendeli az MPE dimenziókat a makrókhoz: `Off`, `XYZ -> Macro 1, 2, 3`, vagy `YZ -> Macro 1, 2`.
- **MPE Y to Mod Wheel** (`master.mpe.y_modwheel`): Az MPE Y tengelyt a globális Mod Wheel sínre irányítja.
- **MPE Pitch Bend also to X** (`master.mpe.bend_to_x`): A tagcsatorna pitch bendjét az Expression X modulációs forrásra irányítja.
- **MPE Y Bi-directional** (`master.mpe.y_bipolar`): Az Y tengelyt nullpont körül kétirányúként (bipolárisként) kezeli.

> [!WARNING]
> **Az MPC hardverpadjaival történő játék során hagyjuk az MPE-t KIKAPCSOLVA (OFF).**
> Az MPC hardverpadjai **nem** továbbítanak többcsatornás MPE adatokat a külső pluginekhez; minden hang az 1. MIDI-csatornán érkezik. Az `MPE On` vagy az `MPE XYZ to Macros` bekapcsolása nem teszi lehetővé, hogy a padok makrókat vagy Expression tengelyeket vezéreljenek, és váratlan hangallokációs hibákhoz vezethet. Az MPE módot tartsuk fenn az MPC USB host portjához csatlakoztatott külső MPE vezérlők (mint a Roli Seaboard, LinnStrument vagy Expressive E Osmose) számára.

---

## 5. A padnyomás kétféle felhasználási módja a SynthyBoy-ban

A SynthyBoy két külön mechanizmust kínál a padnyomás feldolgozására:
1. **Presethéz kötött modulációs mátrix útvonalak:** Rendkívül rugalmas, a presettel együtt mentődik, bármely paramétert célozhat.
2. **Globális shell padnyomás-réteg (Global Shell Pad Pressure Layer):** Gyors, azonnali szűrőnyitás bármely hangszínen, a Master oldalról kapcsolható.

```
                    +-----------------------------+
                    |  MPC Pad Pressure (0xA0)    |
                    +--------------+--------------+
                                    |
          +-------------------------+-------------------------+
          |                                                   |
          v                                                   v
+-------------------------------+           +-------------------------------+
| 1. Preset modulációs mátrix   |           | 2. Globális shell réteg       |
| - Forrás: Poly Aftertouch     |           | - Master: PAD PRESS kapcsoló  |
| - Célpont: BÁRMELY paraméter  |           | - Master: PAD % gomb (0-100%) |
| - Presetben / projektben mentve           | - KIZÁRÓLAG Filter 1 Cutoff   |
| - Polifónikus hangonkénti vez.|           | - 12 ms-os szűrővel simítva   |
+---------------+---------------+           +---------------+---------------+
                |                                           |
                |   A preset már tartalmaz egy              |
                |   Poly Aftertouch -> Filter 1 Cutoff utat?|
                |                 |                         |
                +-------- IGEN ---+-----> [ ELNYOMVA ] <----+
                |                         (Duplikáció-szűrés: nincs kettőzés)
                v                                   v
+-------------------------------+           +-------------------------------+
| Hang Filter 1 Cutoff eltolása | <---------+ Akár +36 félhangot ad hozzá   |
+-------------------------------+           +-------------------------------+
```

### 5.1 Presethez kötött modulációs mátrix útvonalak

A modulációs mátrix lehetővé teszi a **Poly Aftertouch** (38. forrás) hozzárendelését a SynthyBoy bármely modulálható célparaméteréhez:
- **Hangonkénti polifónia:** Minden lenyomva tartott pad csak a saját hangját modulálja. Leüthetünk egy háromhangos akkordot, és ha csak a legfelső hang padjába nyomunk bele erősebben, kinyithatjuk annak szűrőjét vagy vibrátót adhatunk hozzá, miközben az alsó két hang teljesen érintetlen marad.
- **Presetekben tárolódik:** A modulációs mátrix hozzárendelései a `.sbpreset` fájlban és a DAW/MPC projektállapotban (chunk) tárolódnak.

### 5.2 Globális shell padnyomás-réteg (`PAD PRESS` és `PAD %`)

A modulációs mátrix szerkesztése nélküli gyors zenéléshez a SynthyBoy egy beépített shell réteget tartalmaz ([docs/MPC.md, "Pad pressure"](../../MPC.md#pad-pressure); [shell-mpc/pad_pressure.h](../../../shell-mpc/pad_pressure.h)):
- **Kezelőszervek a Master oldalon (Main oldal, VOICES blokk):**
  - **PAD PRESS:** Kétállású kapcsoló (2673. paraméterindex, `Pad Pressure`, értékei: `Off` / `On`). Alapértelmezés szerint **OFF**.
  - **PAD %:** Értékállító potméter (2674. paraméterindex, `Pad Pressure Amount`, tartománya 0 és 100% között). Alapértelmezés szerint **35%** (amely maximális nyomásnál körülbelül egy oktávnyi szűrőnyitást biztosít).
- **Célpont:** **KIZÁRÓLAG a Filter 1 vágási frekvenciáját (Filter 1 Cutoff)** modulálja (a Filter 2-t, az oszcillátorokat vagy az effekteket nem).
- **Mélység:** Akár +36 félhangnyi vágási frekvencia eltolás 100%-os mértéknél és maximális nyomásnál.
- **Simítás:** A nyomásértékeket egy egypólusú aluláteresztő szűrő simítja 12 ms-os időállandóval (`core/engine-own/voice/voice.cpp:704`) a lépcsőzetes ugrások és a cipzárzaj megszüntetése érdekében.
- **Automatikus duplikáció-szűrés (De-duplication):** Ha egy preset modulációs mátrixa már tartalmaz egy kifejezett útvonalat a `Poly Aftertouch` és a `Filter 1 Cutoff` között (mint pl. a gyári lead hangszínek), a globális réteg automatikusan elnyomja saját magát az adott hangra (`core/engine-own/voice/voice.cpp:706`). Ez garantálja, hogy az egyedi szűrőnyomási görbékkel tervezett presetek ne szenvedjenek kétszeres modulációt vagy nem kívánt túlvezérelt fényességet.
- **Állapotmentés:** A projektfájl végében tárolódik (`mpcPadPressure` és `mpcPadPressureAmount`), függetlenül a preset adataitól.

---

## 6. Hogyan használják a gyári presetek a padnyomást?

A SynthyBoy-hoz mellékelt 136 gyári presetből (112 eredeti preset és 24 a sound set batch 1 csomagból) **65 preset** tartalmaz beépített `Poly Aftertouch -> Filter 1 Cutoff` útvonalat a modulációs mátrixában (és összesen 66 preset irányítja valahová a Poly Aftertouch-ot; [scripts/factory-preset-pad-pressure.json](../../../scripts/factory-preset-pad-pressure.json); [docs/MPC.md](../../MPC.md)):

### Presetkategóriák és kalibrációs mértékek

1. **Drónok és ambient textúrák (Drones & Ambient Textures, 30% és 45% között):**
   - *Bode Shifter Drone* (30%), *Dark Cinema Drone* (45%), *Deep Sea Ambient* (45%), *Frozen Aurora* (45%), *Harmonic Cloud* (45%), *Infinite Horizon* (45%), *Slow Motion Waves* (45%), *Space Odyssey Pad* (45%), *Shepard Ascent* (35%), *Lorenz Garden* (40%), *Breath of Machines* (35%).
   - *Viselkedés:* A kitartott drónok sötéten és atmoszférikusan indulnak; a padba mélyebben belenyomva a felső felharmonikusok kinyílnak és végigsöpörnek a frekvenciaspektrumon az attack burkolók újraindítása nélkül.
2. **Orgonák és billentyűsök (Organs & Keys, 18% és 70% között):**
   - *Accordion Reed* (25%), *B3 Drawbar Organ* (25%), *FM Soft Organ* (25%), *Glass Organ Tones* (25%), *Vibraphone Bars* (25%), *Rotary Rock Organ* (70%), *Tempo Gate Chords* (18%), *Wah Pressure Clav* (46%).
   - *Viselkedés:* Szimulálja a hangerő-pedálokat (swell pedal) vagy a billentyűzet utánnyomását, meghajtva az orgona regiszterek felhangjait és emulálva a forgóhangszórós (rotary) szaturációt.
3. **Leadek és szólók (Leads & Solos, 14% és 70% között):**
   - *Bell Chime Lead* (30%), *Chorused Saw Solo* (30%), *Cosmic Drift Lead* (30%), *Diode Bite Lead* (30%), *Dirty Peak Lead* (30%), *Dual Octave Hero* (30%), *FM Digital Lead* (30%), *Harmonic Shimmer Lead* (30%), *Hyper Saw Lead* (70%), *Laser Beam Lead* (30%), *Metallic Edge Lead* (30%), *Phaser Sweep Lead* (30%), *Pulse Width Mod Lead* (30%), *Screaming Sync Lead* (30%), *Solo Square Lead* (30%), *Tri Soft Solo* (30%), *Twin Warp Lead* (30%), *Vocaloid Lead* (30%), *Warm Vintage Lead* (30%), *MPE Pressure Lead* (55% Cutoff, +20% Unison Detune), *Sync Cry Lead* (26%), *Triple Fifth Lead* (24%), *Vowel Whistle Lead* (18%), *Bell FM Ripple Lead* (14%).
   - *Viselkedés:* A szólódallamok hangról hangra dinamikusan hangsúlyozhatók; az erősebb lenyomás kiterjeszti a szűrő fényességét és növeli a supersaw lehangolási mértékét (detune) a kitartott csúcspontokon.
4. **Padek, vonósok és kórusok (Pads, Strings & Choirs, 40% és 70% között):**
   - *Cathedral Choir* (45%), *Comb Reso Swell* (45%), *Diffusor Cloud Pad* (40%), *Ethereal Glass* (45%), *Gentle Flute Pad* (40%), *Golden Dawn Pad* (45%), *Harmonic Cloud* (45%), *Hyper Dimension Pad* (40%), *Lush Analog Pad* (70%), *Odd Harmonics Pad* (40%), *Radiant Pulse Pad* (40%), *Starlight Shimmer* (40%), *Subtle Tape Drift* (40%), *Tri-Band Split Texture* (40%), *Velvet Strings* (40%), *Vintage Brass Pad* (70%), *Vowel Choir Pad* (45%), *Bowed Pressure Strings* (46%), *Brass Swell Ensemble* (45%), *Mid Side Halo* (40%), *Parallel Dusk Pad* (40%), *Stretched Glass Choir* (42%).
   - *Viselkedés:* A meleg vonós és kórus felúszások természetesen teresednek a hangerő és a jelenlét terén az ujjnyomás hatására, együtt lélegezve az előadó érintésével.
5. **Basszus (Bass, 18%):**
   - *Subterranean Duo* (18%).
   - *Viselkedés:* A kitartott szubbasszus stabilan tartja a mély tartományt, miközben az ujjnyomás kinyitja a felső fűrészhullám harapását és a szűrő jelenlétét a kitartott hangokon.

---

## 7. Lépésről lépésre: Padnyomás hozzárendelése a Mod oldalon

Kövessük az alábbi lépéseket az MPC standalone érintőképernyőjén a padnyomás tetszőleges paraméterhez rendeléséhez a SynthyBoy-ban:

```
+-----------------------------------------------------------------------+
| MODULATION MATRIX                                          [ + ADD ]  |
| Tabs: [ ALL ]  [ BY SOURCE ]  [ BY DEST ]                             |
+-----------------------------------------------------------------------+
| Üres mátrix nézet: 57 forrásos gyorsválasztó tábla (ModSourceBoard)   |
| [ ENVELOPES ]  Env 1   Env 2   Env 3   Env 4   Env 5   Env 6          |
| [ LFOS ]       LFO 1..10   RLFO 1..4                                  |
| [ MACROS ]     Macro 1..8                                             |
| [ PLAYER ]     Velocity  Note  Aftertouch  PitchWhl  ModWhl  Random   |
|                Lift  Slide  OctNote  >>> [ POLY AFTERTOUCH ] <<<      |
| [ SOUND ]      Osc A..C  Noise  Sub  Filter 1..2  ActiveVoices ...    |
+-----------------------------------------------------------------------+
| Feltöltött mátrix nézet: Sorszerkezet (72 px)                         |
| [SRC: Poly Aftertouch]  [DST: Filter 1 Cutoff]  [DEPTH: ====o  +55%]  |
| Q1: Sor kivál.   | Q2: Forrás vált.  | Q3: Célpont vált. | Q4: Mélység|
+-----------------------------------------------------------------------+
```

### 1. lépés: Nyissuk meg a Mod oldalt
- Nyomjuk meg a hardveres fizikai **Mod** funkciógombot, vagy érintsük meg a **Mod** fület az MPC érintőképernyőjén.

### 2. lépés: Válasszuk ki a forrást
- **Ha a mátrixban 0 aktív hely (slot) van:** A SynthyBoy megjeleníti az 57 forrásos gyorsválasztó táblát (`ModSourceBoard`) ([a modulációs oldal](../../ui-map/mod.md)). Érintsük meg a **POLY AFTERTOUCH** mezőt a **PLAYER** kategóriában.
- **Ha már léteznek útvonalak:** Érintsük meg a jobb felső sarokban található **+ ADD** gombot. Az újonnan létrehozott sorban érintsük meg a **SOURCE** (Forrás) gombot (vagy forgassuk a **Q2** Q-Link potmétert). A forrásválasztó listában válasszuk ki a **Poly Aftertouch** lehetőséget (38. forrásválasztás).

### 3. lépés: Válasszuk ki a célpontot
- A forrásmező vagy a **TARGET** (Célpont) oszlop megérintése megnyitja a csoportosított célpontválasztót (`ModTargetsPage`).
- Böngésszünk a kategóriák között (Oscillators, Filters, Envelopes, LFOs, Mixer, FX, Voice Control), és érintsük meg a kívánt paramétert (pl. `Filter 1 Cutoff`, `Osc 1 WT Pos`, `Delay Send`).

### 4. lépés: Állítsuk be a moduláció mélységét
- Húzzuk el a mélységállító csúszkát vízszintesen az érintőkijelzőn, vagy forgassuk a **Q4 (AMOUNT)** Q-Link potmétert.
- *Polaritás:* A **Poly Aftertouch** egy unipoláris forrás (nyugalmi állapotban `0.0`, maximális nyomásnál `1.0`). A pozitív mélység a célparamétert a jelenlegi gombállása fölé növeli; a negatív mélység a minimum felé csökkenti azt.

### 5. lépés: Részletes slot-beállítások (opcionális)
- Érintsük meg a **MORE** gombot, vagy koppintsunk duplán a sorra a **Slot Detail Page** (Slot részletező oldal) megnyitásához:
  - **Auxiliary Source (Segédforrás):** Megszorozhatjuk a padnyomást egy második forrással (pl. skálázhatjuk a nyomást a `Mod Wheel` vagy az `Env 2` segítségével).
  - **Curve / Shape (Görbe / Alak):** Meghajlíthatjuk a válaszreakció görbéjét, hogy keményebb ujjnyomást igényeljen a moduláció beindulása előtt, vagy éppen hiperérzékennyé tehetjük a finom érintésekre.
  - **Rise / Fall Smoothing (Felfutási / lefutási simítás):** Állítsuk be a fordulatszám-korlátozási (slew rate) időket, ha lassabb, filmszerű átmeneteket szeretnénk az azonnali reakció helyett.

---

## 8. Öt konkrét játékrecept

Íme öt azonnal használható modulációs recept, amelyeket kifejezetten az MPCe padokhoz hangoltunk. Mindegyik recepthez töltsünk be egy alapértelmezett (Init) vagy egyszerű hangszínt, állítsuk be az alapparamétereket, majd állítsuk be a modulációs mátrix slotjait.

---

### 1. recept: Basszus — "Growl & Bite Punch"

Dinamikus szintetizátorbasszus, amely normál játéknál szilárd, feszes alapot nyújt, de agresszív analóg rezonanciával és szaturációval harap, amikor mélyebben belenyomunk a padba.

- **1. Modulációs Slot:**
  - **Forrás:** `Poly Aftertouch` (38. választás)
  - **Célpont:** `Filter 1 Resonance`
  - **Mérték (Amount):** `+45%`
- **2. Modulációs Slot:**
  - **Forrás:** `Poly Aftertouch` (38. választás)
  - **Célpont:** `Distortion Drive` (FX Slot 1: Distortion)
  - **Mérték (Amount):** `+30%`
- **Alapbeállítások:**
  - Oscillator A: Fűrészhullám (Saw), 1 hang.
  - Filter 1: 24 dB Low Pass (aluláteresztő), Cutoff alacsonyra állítva (~350 Hz), Resonance alacsony (15%).
  - Env 1 (Amp): Attack 0 ms, Decay 450 ms, Sustain 40%, Release 120 ms.
- **Mit fogunk hallani:**
  A normál ritmikus basszusfutamok tiszták, feszesek maradnak, telt mélyekkel. Amikor kitartunk egy basszushangot és határozottan belenyomunk a padba, a szűrőrezonancia felsikolt, és a torzítóáramkörbe hajtva ropogós felhangokat kelt — anélkül, hogy a hang újraindulna vagy elhangolódna.

---

### 2. recept: Lead — "Singing Vocal Swell & Supersaw Detune"

Expresszív szóló lead, ahol az ujjnyomás egyszerre vezérli a szűrő fényességét és a sztereó unison szélességét, drámai zenei felívelést biztosítva a kitartott hangokon.

- **1. Modulációs Slot:**
  - **Forrás:** `Poly Aftertouch` (38. választás)
  - **Célpont:** `Filter 1 Cutoff`
  - **Mérték (Amount):** `+50%`
- **2. Modulációs Slot:**
  - **Forrás:** `Poly Aftertouch` (38. választás)
  - **Célpont:** `Osc 1 Unison Detune`
  - **Mérték (Amount):** `+35%`
- **3. Modulációs Slot:**
  - **Forrás:** `Poly Aftertouch` (38. választás)
  - **Célpont:** `Osc 1 Morph`
  - **Mérték (Amount):** `+40%`
- **Alapbeállítások:**
  - Oscillator A: Vokális vagy fűrész felharmonikusokat tartalmazó hullámtábla, Unison 5 hangra állítva, Detune gomb szerény 10%-on.
  - Filter 1: 12 dB Low Pass, Cutoff 1.2 kHz-en.
- **Mit fogunk hallani:**
  A gyors dallamfutamok tiszták, fókuszáltak és artikuláltak. Egy kitartott hangon elidőzve és az ujjunkkal belenyomva a szűrő drámaian kinyílik, a hullámtábla nyílt vokális magánhangzóvá alakul, az unison hangok pedig hatalmas, vastag supersaw glóriává szélesednek.

---

### 3. recept: Pad — "Ethereal Polyphonic Crystalline Swell"

Meleg atmoszférikus akkordpad, amely bemutatja a valódi hangonkénti polifónikus aftertouch erejét: az akkord egyetlen hangjára gyakorolt nyomás hatására csak az az egy hang kezd digitális fényességgel csillogni és merül el a zengetőben, miközben a többi hang sötét és száraz marad.

- **1. Modulációs Slot:**
  - **Forrás:** `Poly Aftertouch` (38. választás)
  - **Célpont:** `Osc 1 Frame` (Wavetable Position)
  - **Mérték (Amount):** `+60%`
- **2. Modulációs Slot:**
  - **Forrás:** `Poly Aftertouch` (38. választás)
  - **Célpont:** `Reverb Send` (vagy Reverb Mix)
  - **Mérték (Amount):** `+35%`
- **Alapbeállítások:**
  - Oscillator A: Hullámtábla, amely meleg analóg fűrészhullámról (Frame 0) kristályos harangok/üveg felé megy át (Frame 100). WT Pos gomb 0-n.
  - Reverb: Decay 4.5 s, Mix 20%-on.
  - Filter 1: Meleg 24 dB Ladder Low Pass, Cutoff 900 Hz-en.
- **Mit fogunk hallani:**
  Üssünk le és tartsunk ki egy négyhangos akkordot. Minden hang melegen, sötéten és intim módon szólal meg. Az akkordot kitartva nyomjunk rá határozottan a gyűrűsujjunkkal a legfelső dallamhangra: egyedül ez a hang úszik át csillogó, kristályos felhangokba és oldódik fel a hatalmas zengetett térben. A basszus és a középső harmóniahangleütések teljesen melegek és változatlanok maradnak alatta.

---

### 4. recept: Pluck — "Damped Pizzicato to Resonant Ring"

Sokoldalú, akusztikus jellegű pengetett hangzás, amely zökkenőmentesen vált a rövid, tompított pizzicato pengetések és a hosszú, zengő harangszerű lecsengések között attól függően, hogyan támaszkodunk bele a padba.

- **1. Modulációs Slot:**
  - **Forrás:** `Poly Aftertouch` (38. választás)
  - **Célpont:** `Env 1 Decay`
  - **Mérték (Amount):** `+50%`
- **2. Modulációs Slot:**
  - **Forrás:** `Poly Aftertouch` (38. választás)
  - **Célpont:** `Filter 1 Cutoff`
  - **Mérték (Amount):** `+40%`
- **Alapbeállítások:**
  - Oscillator A: Rövid akusztikus pengetés vagy háromszög/impulzus hullámtábla.
  - Filter 1: 24 dB Low Pass, Cutoff 600 Hz-en.
  - Env 1: Attack 0 ms, Decay 160 ms, Sustain 0%, Release 100 ms.
- **Mit fogunk hallani:**
  A padok gyors felengedéssel történő megérintése feszes, perkusszív, tompított kalimba/pizzicato pengetéseket eredményez. Ha leütjük és határozottan lenyomva tartjuk a padot, a decay (lecsengési) idő azonnal 2 másodperc fölé nyúlik, a szűrő kinyílik, és a leütést zenei módon hosszan lebegő, rezonáns csengéssé alakítja.

---

### 5. recept: FX — "Runaway Feedback Vortex & Tape Flutter"

Kísérleti előadói effekt, ahol a padnyomás az echo buffert analóg gerjedésbe (öngerjedésbe) hajtja, miközben szédítő szalagnyávogást (pitch flutter) hoz létre.

- **1. Modulációs Slot:**
  - **Forrás:** `Poly Aftertouch` (38. választás)
  - **Célpont:** `Delay Feedback` (FX Delay)
  - **Mérték (Amount):** `+55%`
- **2. Modulációs Slot:**
  - **Forrás:** `Poly Aftertouch` (38. választás)
  - **Célpont:** `LFO 1 Rate`
  - **Mérték (Amount):** `+45%`
- **3. Modulációs Slot:**
  - **Forrás:** `Poly Aftertouch` (38. választás)
  - **Célpont:** `Mod 01 Amount` (1. Slot: LFO 1 -> Pitch / Delay Offset)
  - **Mérték (Amount):** `+40%`
- **Alapbeállítások:**
  - FX Rack: Delay engedélyezve, Time 3/16, Feedback 30%, Mix 40%.
  - LFO 1: Szinuszhullám, Rate 1.5 Hz, az 1. slotban az Oscillator Pitch-re vagy a Delay Time eltolásra irányítva.
- **Mit fogunk hallani:**
  Normál játéknál a hangok szabványos ritmikus, tiszta sztereó ismétlődéseket adnak. Erősen belenyomva a padba a delay visszacsatolása (feedback) azonnal 100% fölé emelkedik zúgó analóg szalagos öngerjedésbe fordulva, miközben ezzel párhuzamosan a lebegés sebessége is felgyorsul. Az ujjnyomás lazításakor a visszacsatolás azonnal egységnyi szint alá esik vissza, hagyva, hogy a gerjedéshullám természetes módon csengjen le.

---

## 9. Az 1. generáció és a 2. generáció összehasonlítása

| Funkció / Képesség | Első generáció (MPC Key 61, Live I/II, One/One+, MPC X) | Második generáció (MPC XL, MPC Live III) |
|---|---|---|
| **Hardveres padtípus** | Szabványos vastag gumipadok [Standalone OS Guide v3.10.1, 62. oldal] | MPCe 3D expresszív padok [User Guide v3.10.1, 4., 11. oldal] |
| **Strike Velocity (ütési dinamika)** | Igen (0–127) | Igen (0–127) |
| **Padnyomás-érzékelő** | Alapszintű dinamika/aftertouch érzékelő [Standalone Guide, 333. oldal] | Nagy felbontású, többtengelyes folyamatos nyomásérzékelő [User Guide v3.10.1, 64., 350. oldal] |
| **Padnyomás frissítési sebessége a plugin felé** | Szakaszos / alacsonyabb sebesség | Nagy sebességű folyamatos adatfolyam (~170 Hz / 6 ms-onként) ([docs/MPC.md](../../MPC.md#mpc-xl-pads)) |
| **Polyphonic Aftertouch (`0xA0`)** | Támogatott, ha a pad beállításaiban úgy van konfigurálva | Natívan támogatott lenyomva tartott padonként |
| **Channel Aftertouch (`0xD0`)** | Billentyűzetek küldik (pl. MPC Key 61, 61 billentyű) [Standalone Guide, 369. oldal] | Padok nem küldik (a padok `0xA0`-t küldenek) |
| **Ujj X/Y koordinátáinak érzékelése** | **Nincs** | A hardver érzékeli: X, Y, Center Dist, Press Dist, dX, dY (kizárólag belső dobsávokon) [MPC 3.6, 2. oldal] |
| **Pad kvadránsérzékelés (4 sarok)** | **Nincs** | Igen (dob artikulációk, rétegek indítása, Tune/Mix komponens) [User Guide v3.10.1, 13., 225., 234. oldal] |
| **8×8 (64 pad) előadói mód** | **Nincs** (csak 4×4 rács) | Igen (A–D vagy E–H bankok felosztása 64 játszható padra) [User Guide v3.10.1, 72. oldal; MPC 3.10, 4–5. oldal] |
| **MPCe Q-Link mód** | **Nincs** | Igen (XY érintőfelület a dobsávokon) [User Guide v3.10.1, 337., 350. oldal] |
| **X/Y továbbítása VST plugineknek** | Nem | **Nem** (gazdarendszeri korlát; MPC XL-en kimérve) ([docs/MPC.md](../../MPC.md#mpc-xl-pads)) |
| **SynthyBoy Mod forrás: Velocity** | Aktív (23. forrás) | Aktív (23. forrás) |
| **SynthyBoy Mod forrás: Poly Aftertouch** | Aktív (38. forrás) | **Aktív (38. forrás) — Elsődleges expresszív dimenzió** |
| **SynthyBoy globális PAD PRESS réteg** | Aktív (Filter 1 Cutoff, Master oldal) | Aktív (Filter 1 Cutoff, Master oldal) |

---

## 10. Az eszközön megerősítendő pontok

A szigorú mérnöki alaposság jegyében az alábbi működési részleteket nem lehetett egyértelműen bizonyítani a publikált Akai kézikönyvekből vagy az automatizált fejlesztői tesztrendszerekből, így ezek valós fizikai hardveren való megerősítésre várnak:

1. **A gazdarendszer MPCe Q-Link projekt módja a plugin automatizálásához:**
   - *Kézikönyvi állítás:* Az MPC kézikönyv szerint az MPCe Q-Link Mode elérhető a dobsávokon, és "Track" (Sáv) valamint "Project" (Projekt) módot is kínál ([MPC 3.7 Update Release Notes, 2. oldal]; [User Guide v3.10.1, 350. oldal]).
   - *Kérdés:* Hozzárendelhető-e egy MPCe pad egy dobsávon a Q-Link Learn segítségével "Project" módban egy SynthyBoy VST paraméter (pl. Filter Cutoff vagy Wavetable Frame) automatizálására az X vagy Y tengely használatával, miközben egy szekvencia fut?
2. **Pad Perform mód Poly Aftertouch-csal a plugin sávokon:**
   - *Kézikönyvi állítás:* A Pad Perform skála- és akkordkiosztást kínál a padokon, és tartalmaz egy "Use Aftertouch" kapcsolót [User Guide v3.10.1, 83. oldal].
   - *Kérdés:* Amikor a Pad Perform (Notes, Chords vagy Progressions) aktív egy Plugin sávon, megőrzi-e a gazdarendszer a polifónikus aftertouch-ot (`0xA0`) az akkord minden megszólaló hangjára, vagy összevonja azokat globális csatorna-aftertouch-csá, esetleg teljesen eldobja a nyomást?
3. **8×8 Notes mód Poly Aftertouch viselkedése:**
   - *Kézikönyvi állítás:* 8×8 módban a 16 fizikai pad 64 alzónára oszlik négy bankon keresztül [MPC 3.10 Release Notes, 4–5. oldal].
   - *Kérdés:* Dallamos sávok 8×8 módban történő játéka esetén egy plugin sávon az egyes alkvadránsok egymástól függetlenül továbbítanak-e folyamatos polifónikus aftertouch-ot, és a nyomásérzékenység egységes-e a kvadránsok szélein?
4. **Note Repeat és arpeggiator kölcsönhatása a nyomással:**
   - *Kézikönyvi állítás:* A Note Repeat és az Arpeggiator tartalmaz "Aftertouch to Velocity" beállításokat [Standalone OS Guide, 104. oldal; User Guide v3.10.1, 102. oldal].
   - *Kérdés:* Amikor a Note Repeat gombot lenyomva tartjuk a SynthyBoy-t megszólaltató padon, küld-e a gazdarendszer folyamatos aftertouch adatfolyamot (`0xA0`) az ismételt note-on üzenetek mellett, vagy csak a rákövetkező ismétlések note-on leütésérzékenységét módosítja?
5. **Felengedési sebesség (Release Velocity / Lift) továbbítása:**
   - *Kérdés:* Továbbítanak-e az MPCe padok valós felengedési sebességet a Note-Off eseményekben (`0x80`) a VST pluginek felé, amikor az ujjakat gyorsan felemelik, vagy a Note-Off sebességet a gazdarendszer 0-ra vagy 64-re rögzíti?
6. **Hardveres padgörbék vs. nyomásváltozási sebesség (Slew Rate):**
   - *Kérdés:* A hardveres **Pad Curve** beállítás (A, B, C, D a Preferences > Hardware menüben) [User Guide v3.10.1, 63. oldal] kizárólag a kezdeti note-on ütési leütésérzékenységet formálja át, vagy módosítja a külső pluginek felé küldött folyamatos polifónikus aftertouch átviteli függvényét is?
