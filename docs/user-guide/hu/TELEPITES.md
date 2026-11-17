> Forrás: docs/user-guide/INSTALL.md (commit: 74d2181ef1f72d8c1bebcf755e8696ae94c438c6)

# SynthyBoy Akai MPC-hez — Telepítés és első lépések

Ez az útmutató bemutatja a SynthyBoy önálló (standalone) MPC csomagjának telepítését, konfigurálását és első használatát MPC OS 3.10.1 vagy újabb rendszert futtató Akai MPC hardvereken.

> [!IMPORTANT]
> **Harmadik féltől származó pluginekkel kapcsolatos megjegyzés és stabilitási figyelmeztetés**  
> A SynthyBoy egy független, nyílt forráskódú szoftverszintetizátor, amely az Akai kísérleti külső plugin-támogatásán keresztül fut. Ahogy az Akai hivatalos felhasználói kézikönyvei (*MPC Standalone OS User Guide v3.10.1*, 9. oldal; *MPC Live III, MPC XL User Guide v3.10.1*, 7. oldal) kiemelik:  
> *"Ezeket a plugineket az inMusic nem tesztelte és nem támogatja. Egy hibás plugin fagyást vagy összeomlást idézhet elő, a rendelkezésre állónál több processzorkapacitást köthet le, vagy hanghibákat okozhat. Ha a funkció engedélyezve van, mentsd el a munkádat gyakran."*

---

## 1. A csomag tartalma

A letöltött és kicsomagolt SynthyBoy MPC kiadási zip archívumban egyetlen gyökérmappa található, amelynek pontos neve:

```text
Safian - VST - SynthyBoy
```

Ez az önálló könyvtár tartalmazza az összes olyan bináris fájlt, felületi elemet és tartalmat, amely a 32 bites és 64 bites standalone MPC hardvereken történő futtatáshoz szükséges. A mappán belül az alábbi fájlok találhatók:

- `plugin.json` — Az MPC plugin manifesztfájlja. Ez regisztrálja a plugint az MPC OS gazdarendszerben, és megadja a belépési pontként szolgáló megosztott függvénykönyvtárat mindkét processzorarchitektúrához: a `file32` a `VST/synthyboy-arm32.so` fájlra, a `file64` a `VST/synthyboy-arm64.so` fájlra mutat.
- `version.xml` — XML verziómanifeszt, amely deklarálja a csomag tartalomverzióját és az érintőképernyős felület skin-verzióját (`uiversion`). Az MPC OS ezt a fájlt ellenőrzi a beolvasás (scanning) során.
- `VST/` — A futtatható megosztott fájlokat (shared object) és háttér-segédprogramokat tartalmazó könyvtár:
  - `synthyboy-arm32.so`: Stripelt (szimbólummentesített) 32 bites ARMv7 hard-float megosztott programkönyvtár (GCC-vel fordítva ARMv7-A-ra NEON utasításkészlettel, glibc 2.36 kompatibilitással, kizárólag a `VSTPluginMain` függvényt exportálva). Ezt töltik be a 32 bites (első generációs) MPC egységek.
  - `synthyboy-arm64.so`: Stripelt 64 bites AArch64 megosztott programkönyvtár (GCC-vel fordítva ARMv8-A-ra, glibc 2.36 kompatibilitással, kizárólag a `VSTPluginMain` függvényt exportálva). Ezt töltik be a 64 bites (második generációs) MPC egységek.
  - `synthyboy-overlay-gpu-arm32` és `synthyboy-overlay-gpu-arm64` *(segédprogramok)*: Stripelt segédfájlok, amelyeket a kísérleti közvetlen GPU overlay-t (`SYNTHYBOY_MPC_OVERLAY`) engedélyező buildek használnak. A plugin bináris a megfelelő segédfolyamatot indítja el a hardveres DRM sík rajzolásának és az EGL renderelésnek a kezelésére úgy, hogy ne blokkolja a valós idejű audio szálakat vagy az elsődleges képernyősíkot.
- `Plugin Skins/` — A teljes érintőképernyős felület (Touch User Interface – TUI) és a hardveres vezérlőhozzárendelések definíciói:
  - `TUI.json`: Meghatározza a képernyőfüleket, alfüleket, grafikus vezérlőket (widgets), vizuális stílusokat és elrendezési elemeket az MPC érintőkijelzőjén.
  - `Q-Links.json`: Hozzárendeli a hardveres, kapacitív Q-Link forgópotmétereket a plugin paramétereihez a különböző funkcióbankokon keresztül.
  - `browser_images/soundsmode.png`: Egy pontosan 270 × 110 pixeles PNG grafikus ikon/csempe, amely a hangszerkiválasztó képernyőn jelenik meg a **Sounds > INSTRUMENTS** (Hangok > Hangszerek) módban.
- `Content/presets/` — A 112 gyári preset (előre beállított hangszín) gyűjteménye kategóriákba rendezve (`Bass/`, `Lead/`, `Pad/`, `Pluck/`, `Keys/`, `FX/`). A plugin ezeket a fájlokat a `plugin.json` helyéhez képest relatívan oldja fel, és VST programként teszi elérhetővé őket.
- `Presets/` *(ha jelen van a kiadásban)* — Standalone MPC preset fájlok (`.xpl`), amelyek lehetővé teszik a hangszínek közvetlen betöltését az MPC OS Sounds (Hangok) böngészőjén keresztül.

---

## 2. Támogatott hardverek

A SynthyBoy a 32 bites és a 64 bites binárisokat egyetlen mappába csomagolva tartalmazza. Az MPC OS dinamikus betöltője automatikusan a hardver architektúrájának megfelelő binárist választja ki:

- **1. generáció (32 bites ARMv7 hard-float):**  
  Tesztelve és ellenőrizve az **MPC Key 61** hangszeren (MPC OS 3.10.1.12 futtatásával).  
  *Teszteletlen modellek:* Az MPC Live, MPC Live II, MPC One, MPC One+, MPC X, MPC X SE és MPC Key 37 azonos 32 bites ARMv7 CPU platformot használnak, de ezeken a szerző fizikailag **nem** tesztelte a működést valós hardveren.
- **2. generáció (64 bites AArch64):**  
  Tesztelve és ellenőrizve az **MPC XL** munkaállomáson (MPC OS 3.10.1.12 futtatásával).  
  *Teszteletlen modellek:* Az MPC Live III azonos 64 bites AArch64 platformra épül, de a szerző valós hardveren fizikailag **nem** tesztelte.

---

## 3. Hová másoljuk a csomagot?

Ahhoz, hogy a plugin láthatóvá váljon az MPC OS számára, másoljuk be a teljes `Safian - VST - SynthyBoy` mappát egy pontosan `VST2 Plugins` nevű mappába a belső meghajtó vagy bármely csatlakoztatott külső tároló gyökérkönyvtárában:

```text
<Meghajtó gyökérkönyvtára>/
└── VST2 Plugins/
    └── Safian - VST - SynthyBoy/
        ├── plugin.json
        ├── version.xml
        ├── VST/
        ├── Plugin Skins/
        └── Content/
```

### Támogatott tárolóhelyek

A hivatalos Akai kézikönyvek (*MPC Standalone OS User Guide v3.10.1*, 9. oldal; *MPC Live III, MPC XL User Guide v3.10.1*, 7. oldal) alapján:
- **Belső meghajtó (Internal Drive):** A belső SATA/NVMe SSD vagy a belső felhasználói tárhely (pl. `/nvme/VST2 Plugins/` vagy `/internal/VST2 Plugins/`).
- **Külső csatlakoztatott meghajtó (External Attached Drive):** Bármely csatlakoztatott SD kártya gyökere (pl. `/sdcard/VST2 Plugins/`) vagy USB flash meghajtó/merevlemez.

> [!NOTE]
> **Meghajtó-elérési utak tapasztalatai**  
> A fejlesztői hardvertesztek (`docs/MPC.md`) alapján: az MPC Key 61-en a plugin mappa működése az `/sdcard/VST2 Plugins/Safian - VST - SynthyBoy/` útvonalon lett megerősítve. Az MPC XL-en az `/nvme/VST2 Plugins/Safian - VST - SynthyBoy/` útvonalon igazolódott a működés.

---

## 4. Külső pluginek engedélyezése az MPC OS-ben

A külső, harmadik féltől származó VST2 pluginek mindaddig teljesen rejtve maradnak, amíg a kísérleti külső plugin-támogatást kifejezetten be nem kapcsoljuk a rendszerbeállításokban.

### A konfigurálás pontos lépései

*(Forrás: MPC Standalone OS User Guide v3.10.1, 9. oldal; MPC Live III, MPC XL User Guide v3.10.1, 7. oldal)*

1. Nyomjuk meg a hardveres **Menu** (Menü) gombot a Mode Menu (Üzemmód menü) megnyitásához.
2. Érintsük meg a **Preferences** (Beállítások, fogaskerék ikon) ikont a képernyő alján.  
   *(Az MPC XL-en alternatívaként tartsuk lenyomva a **Shift** gombot, és nyomjuk meg a **Menu / Prefs** gombot).*
3. A Preferences képernyőn görgessünk le a bal oldali sávban, és válasszuk ki az **Enhancements** (Bővítmények / Fejlesztések) fület.
4. Érintsük meg az **Enable Enhancements** (Bővítmények engedélyezése) elemet, és állítsuk **On** (Be) állásba.
5. Miután a fejlesztések engedélyezve vannak, görgessünk le a listában az **External Plugin Support** (Külső plugin támogatás) opcióhoz, és kapcsoljuk azt is **On** (Be) állásba.

> [!WARNING]
> Mindkét kapcsolónak (**Enable Enhancements** ÉS **External Plugin Support**) **On** (Be) állásban kell lennie. Ha bármelyik ki van kapcsolva, a SynthyBoy egyáltalán nem jelenik meg a plugin böngészőben.

### Újraindítás szükséges (megfigyelt gazdarendszeri működés)

Az Akai kézikönyvek nem említik az újonnan bemásolt fájlok beolvasási idejét. A közvetlen hardveres mérések azonban mind az MPC Key 61-en, mind az MPC XL-en (`docs/MPC.md`) kimutatták:
- **Az újonnan másolt plugin csak az MPC alkalmazás újraindítása után jelenik meg.**
- Miután átmásoltuk a `Safian - VST - SynthyBoy` mappát a meghajtóra, indítsuk újra az MPC-t, vagy indítsuk újra az MPC szoftvert.

---

## 5. A SynthyBoy betöltése és használata

Miután a külső plugin-támogatás engedélyezve van és az eszközt újraindítottuk, kövessük az alábbi lépéseket a hangszer betöltéséhez:

### Betöltés Plugin sávra

*(Forrás: MPC Standalone OS User Guide v3.10.1, 16., 50., 119. oldal; MPC Live III, MPC XL User Guide v3.10.1, 15., 51., 120. oldal)*

1. **Main Mode** (Fő üzemmód) nézetben érintsük meg a **+ New Track** (+ Új sáv) gombot az érintőképernyő bal alsó sarkában (vagy jelöljünk ki egy létező sávot).
2. A sávtípus-választó ablakban válasszuk a **Plugin** lehetőséget.
3. Érintsük meg a **Plugin** mezőt a plugin-kiválasztó ablak megnyitásához:
   - A SynthyBoy az **External Plugins** (Külső pluginek) kategóriában található.
   - A képernyő alján a **Type** (Típus – *Synth* / *Instrument*) vagy a **Manufacturer** (Gyártó – *Safian* alatt) gombok megérintésével szűrhetjük a plugineket.
   - Válasszuk ki a **SynthyBoy**-t, majd a megerősítéshez érintsük meg a **Select** (Kiválasztás) gombot.
4. Érintsük meg a plugin előnézeti ikonját, vagy érintsük meg az **Edit Instrument** (Hangszer szerkesztése) gombot (Sounds módban) a Track Edit Mode (Sávszerkesztő mód) megnyitásához és a plugin felületének megjelenítéséhez.

### Preset (hangszín) kiválasztása

- Main Mode-ban vagy a Track View (Sávnézet) felületen érintsük meg a **Preset** mezőt a plugin sávon a 112 gyári preset (VST program) böngészéséhez és kiválasztásához.
- Ha az önálló `.xpl` presetek telepítve vannak a `Presets/` mappába, a hangszínek a **Sounds** (Hangok) mód böngészőjén keresztül is kereshetők.

### Projekt mentése

*(Forrás: MPC Standalone OS User Guide v3.10.1, 67. oldal; MPC Live III, MPC XL User Guide v3.10.1, 69. oldal)*

1. Nyomjuk meg a **Menu** gombot, és érintsük meg a képernyő alján lévő **Save** (Mentés) gombot (vagy nyomjuk meg a hardveres **Save** / **Shift + Save** gombot).
2. A Save Window (Mentési ablak) felületén válasszuk a **Project** (a jelenlegi projekt felülírása) vagy a **Project As** (mentés új fájlként) lehetőséget.
3. Az MPC a teljes plugin állapotot (minden paramétert, modulációs hozzárendelést és aktuális oszcillátor üzemmódot) elmenti a projektfájlban (`.xpj`).

> [!TIP]
> **A Q-Link állapotmentés működése elmentett projektekben**  
> A `docs/MPC.md` dokumentáció alapján: amikor egy plugint hozzáadunk egy sávhoz, az MPC gazdarendszer elmenti az aktuális `Q-Links.json` hozzárendelés pillanatképét az adott sáv adatai közé az `.xpj` projektben. Ha ezt követően a SynthyBoy-t olyan verzióra frissítjük, amely új skinnel/potméter-elrendezéssel rendelkezik, a korábbi projektek meglévő sávjai megtartják a régi Q-Link hozzárendelésüket. Ha az új Q-Link kiosztást szeretnénk alkalmazni egy létező projektben, távolítsuk el, majd szúrjuk be újra a SynthyBoy-t az adott sávra.

### Plugin program mentése (preset exportálása)

*(Forrás: MPC Standalone OS User Guide v3.10.1, 67. oldal; MPC Live III, MPC XL User Guide v3.10.1, 69. oldal)*

Ha saját hangszínünket egyedi presetfájlként szeretnénk exportálni, hogy más projektekben is újra felhasználható legyen:
1. Győződjünk meg arról, hogy az adott SynthyBoy plugin sáv van kiválasztva.
2. Nyissuk meg a mentési ablakot (**Menu** > **Save**).
3. Érintsük meg a **Plugin Program** gombot.
4. Adjunk meg egy nevet a képernyő-billentyűzet segítségével, válasszuk ki a célmappát a meghajtón, és mentsük el az `.xpl` preset fájlt.

---

## 6. Frissítés új verzióra

A SynthyBoy frissítésekor:

1. Csatlakoztassuk a meghajtót a számítógéphez, vagy kapcsoljuk az MPC-t USB Storage / Controller (USB tárhely / vezérlő) módba.
2. Cseréljük le a `VST2 Plugins/` mappában lévő `Safian - VST - SynthyBoy` mappát az új mappára.
3. Válasszuk le/távolítsuk el biztonságosan a meghajtót, és **indítsuk újra az MPC alkalmazást vagy a hardvert**.

### Miért tilos a futó plugint közvetlenül felülírni?

> [!CAUTION]
> **NE másoljunk vagy írjunk felül plugin fájlokat, miközben a plugin be van töltve egy futó munkamenetben!**  
> Linux (MPC OS) alatt a megosztott programkönyvtárakat (`.so`) a rendszer lapozva (demand-paged), memóriába képezett fájlokként (`mmap`) tölti be közvetlenül a lemezről a folyamat memóriájába. Ha egy bináris fájlt a lemezen felülírunk vagy megcsonkítunk, miközben az MPC alkalmazás azt betöltve tartja:
> - A Linux kernel adott fájlhoz tartozó lapgyorsítótára (page cache) érvénytelenné válik vagy sérül.
> - Amikor az audiomotor vagy a kezelőfelület szála legközelebb egy még be nem töltött lapról hajtana végre kódot, vagy meghívna egy függvényt, a kernel azonnali végzetes `SIGBUS` vagy `SIGSEGV` hibajelzést ad.
> - Ez a teljes MPC gazdaalkalmazás azonnali összeomlásához vezet, ami a nem mentett projektadatok elvesztését okozza!
> - Továbbá a `Plugin Skins` mappa cseréje a gazdarendszer futása közben képi hibákhoz vezet, mivel a dekódolt skin elemek a `/var/tmp/filmstrips` mappában gyorsítótárazódnak.
> 
> A frissítést mindig lezárt projekt mellett, a sávokról eltávolított pluginnal, vagy a fájlátvitelt követően teljesen újraindított hardverrel végezzük.

---

## 7. A SynthyBoy eltávolítása

A SynthyBoy eltávolításához:
1. Töröljük a `Safian - VST - SynthyBoy` mappát a meghajtón lévő `VST2 Plugins` könyvtárból.
2. Ha megnyitunk egy korábbi projektet, amely a SynthyBoy-ra hivatkozik, az MPC OS gazdarendszer biztonságosan nyitja meg a projektet: a hiányzó plugin sávot kihagyja, vagy egy szabványos hiányzó plugin értesítést jelenít meg adatvesztés nélkül.

---

## 8. MPC Key 61 javaslat: a "Show Q-Link Status When Touched" kikapcsolása

Amennyiben **MPC Key 61** hangszert használunk, kifejezetten ajánlott kikapcsolni a Q-Link érintési felugró ablakát.

### A kikapcsolás lépései

*(Forrás: MPC Standalone OS User Guide v3.10.1, 65. oldal)*

1. Lépjünk a **Preferences > General** (Beállítások > Általános) menübe.
2. Keressük meg a **Show Q-Link Status When Touched** (Q-Link állapot megjelenítése érintéskor) beállítást.
3. Kapcsoljuk **Off** (Ki) állásba.  
   *(Alternatívaként tartsuk lenyomva a hardveres **Q-Link** gombot, és a megjelenő ablakban érintsük meg a **Q-Link Status** elemet).*

### Technikai indoklás

A Key 61 hardveres Q-Link potméterei kapacitív érintésérzékelőkkel vannak ellátva. Alapértelmezés szerint bármely gomb megérintésekor a firmware egy teljes képernyőmagasságú állapotsávot rajzol a kijelző jobb szélére (x = 1024 és 1280 közötti képernyőkoordinátákon), megjelenítve az aktuális hozzárendeléseket.

Mivel az MPC gazdarendszer ezt a panelt egy átlátszatlan elsődleges megjelenítési síkra rajzolja közvetlenül a plugin grafikus területe fölé:
- A pluginnak folyamatosan egy optikai panel-maszkoló rutint kell futtatnia (átvizsgálva a pixelsávokat és kivágva a plugin hardveres DRM overlay síkját), hogy a gazdarendszer felugró panele képi szétesés és villódzás nélkül átlátszódhasson.
- Amikor a **Show Q-Link Status When Touched** beállítást **Off** (Ki) állásba kapcsoljuk, a gazdarendszer a `ShowQLinksStatusOverlay="0"` értéket menti a `/data/Settings/MPC/MPC.settings` fájlba.
- A SynthyBoy észleli ezt a beállítást, és teljesen deaktiválja a panelmaszkoló detektorát. Ez megszünteti a képelemzés processzorterhelését (nulla háttérköltség), és biztosítja, hogy a jobb szélen lévő plugin-grafika stabil és vágatlan maradjon.

---

## 9. Első generációs (Gen 1) vs. Második generációs (Gen 2) minőségi profilok

A SynthyBoy automatikusan felismeri a hardvermodellt a rendszer eszközfájljából (`/proc/device-tree/model`), és egy optimalizált DSP teljesítményprofilt rendel hozzá:

> Az **1. generációs (Gen 1)** hardvereken (MPC Key 61 és egyéb még nem mért 32 bites egységek) a motor a konzervatív `low` (alacsony) minőségi profillal fut: a polifónia legfeljebb 8 hangra, az oszcillátor-unison 3 hangra van korlátozva, a warp módok kétszeres túlmintavételezést (2x oversampling) használnak, a spektrális morfózis fél sebességgel frissül, gyors matematikai közelítéseket használ, és 5 számításigényes effekt (Reverb, Flanger, Hyper/Dimension, Bode frekvenciaeltoló és Convolve konvolúciós zengető) ki van iktatva (bypass), garantálva, hogy a jelfeldolgozás biztonságosan beleférjen a valós idejű audioblokk határidejébe (2,902 ms 128 mintánként 44,1 kHz-en).  
> A **2. generációs (Gen 2)** hardvereken (MPC XL és 64 bites nyolcmagos egységek) a motor a `high,voices=16,control=economy` profilt aktiválja: 16 hang polifóniát, oszcillátoronként akár 16 unison hangot, mind a 16 effekt teljes értékű működését, négyszeres (4x) warp túlmintavételezést, teljes sebességű morfózist és egzakt matematikai számításokat biztosítva, bőséges tartalékkal. Bármely fel nem ismert hardvermodell biztonsági okokból alapértelmezés szerint a `low` profilra áll be.

---

## 10. Hibaelhárítás

Az alábbi hibaelhárítási pontok szigorúan a `docs/MPC.md` dokumentumban azonosított és dokumentált működésen és kiváltó okokon alapulnak:

| Probléma | Dokumentált ok és megoldás |
|---|---|
| **A plugin nem szerepel a plugin böngészőben** | 1. **Elavult firmware**: MPC OS 3.10.1 vagy újabb verzió szükséges.<br>2. **Bővítmények letiltva**: A **Preferences > Enhancements** alatt győződjünk meg arról, hogy **mindkét** kapcsoló ("Enable Enhancements" és "External Plugin Support") **On** állásban van.<br>3. **Helytelen mappaelérési út**: A mappának pontosan `Safian - VST - SynthyBoy` nevűnek kell lennie, és közvetlenül a meghajtó gyökerében lévő `VST2 Plugins` mappában kell elhelyezkednie.<br>4. **Hiányzó manifeszt**: Ellenőrizzük, hogy a `plugin.json` jelen van-e a plugin könyvtárában.<br>5. **Az MPC nem lett újraindítva**: A gazdarendszer csak az alkalmazás indulásakor indexeli a külső plugineket. Indítsuk újra az MPC-t. |
| **Nincs hangkimenet** | 1. **Polifónia-korlát elérve**: Gen 1 hardveren a polifónia 8 hangra korlátozott. Hosszú lecsengési idők (release) vagy sűrű akkordok esetén a motor hangokat csippent le (voice stealing; a lecsippentett hangok 3 ms alatt halkulnak el a CPU túlterhelésének megelőzésére).<br>2. **Kiiktatott effektek Gen 1-en**: Ha egy preset mixe 100% wet-re van állítva a Reverb, Flanger, Hyper/Dimension, Bode vagy Convolve effekteken keresztül, ezek a `low` profilban ki vannak iktatva, ami némaságot eredményez az adott jelfolyamon.<br>3. **Sáv / Monitor útválasztás**: Győződjünk meg arról, hogy a plugin sáv Monitor beállítása **Auto** vagy **In** állásban van, és a sáv felvételre élesített (*SEQ REC ARM* aktív). |
| **A kirajzolt oldalak / a képi overlay nem jelenik meg** | 1. **Készenléti képernyősötétítés**: Amikor az MPC készenléti módban elsötétíti a kijelzőt, a hardveres DRM overlay sík kikapcsol, amíg egy ébresztő érintés nem történik.<br>2. **Modális párbeszédablak van nyitva**: Egy teljes képernyős választó vagy rendszermenü megnyitása átmenetileg elnyomja a plugin rajzolt rétegét, amíg a menüt be nem zárjuk.<br>3. **Korábbi projektek elavult Q-Link hozzárendelései**: A korábbi felületi elrendezésekkel készült projektek megőrzik a sávjukhoz tartozó pillanatképet; töltsünk be egy új projektet, vagy szúrjuk be újra a plugint a sávra.<br>4. **A segédprogram inicializálása**: A kísérleti overlay-es buildekben a GPU segédprogram ~70–120 ms alatt indul el. Ha nem jelenik meg, indítsuk újra az egységet. |
| **Hiányoznak a gyári presetek** | 1. **Hiányzó Content mappa**: A SynthyBoy a `plugin.json` helyét felderítve keresi a preseteket a `Content/presets/` mappában a bináris útvonalához képest. Ha a `Content/` mappa nem lett átmásolva, a presetlista üres marad.<br>2. **Böngészés a Sounds módon keresztül**: A gyári presetek a sáv **Preset** mezőjén keresztül érhetők el VST programként. Az önálló MPC Sounds képernyőn történő böngészéshez `.xpl` fájlok szükségesek a `Presets/` mappában. |

---

## 11. Licenc és támogatás

### Licenc

Copyright (C) 2026 Safian Szabolcs.

A SynthyBoy szabad szoftver, amely a **GNU Affero General Public License 3. verziója, vagy (választása szerint) bármely későbbi verziója** (`AGPL-3.0-or-later`) feltételei szerint került kiadásra.
- A teljes licencszövegért tekintse meg a tárhely [LICENSE](../../LICENSE) fájlját.
- A harmadik féltől származó függőségek licencei és nyilatkozatai a [THIRD_PARTY.md](../../THIRD_PARTY.md) és a [NOTICE](../../NOTICE) fájlokban találhatók.
- *Jogi nyilatkozat:* A SynthyBoy egy független fejlesztés, és nem áll kapcsolatban az inMusic Brands vagy az Akai Professional vállalattal, azok nem támogatják és nem hagyták jóvá.

### Hibák bejelentése

Kérjük, hogy a hibákat, teljesítményproblémákat vagy funkciókéréseket a hivatalos hibakövetőn keresztül jelentse be:
- **GitHub Issues:** [https://github.com/Safian/SynthyBoy/issues](https://github.com/Safian/SynthyBoy/issues)

*(Kérjük, ne küldjön támogatási kéréseket privát személyes címekre).*

---

## 12. Az eszközön megerősítendő pontok

Az alábbi pontok a `docs/MPC.md` fejlesztői mérésein, tesztkörnyezeteken vagy a gazdarendszer forráskódjának elemzésén alapulnak, és közvetlenül az Akai felhasználói kézikönyveiből nem voltak megerősíthetők:

1. **Egyéb MPC hardvermodellek**: A működés és a termikus stabilitás a még nem tesztelt hardvermodelleken (MPC Live I/II, MPC One/One+, MPC X/X SE, MPC Key 37, MPC Live III) nem lett tesztelve, és fizikai eszközökön kell megerősíteni.
2. **A beállítások kiírásának időzítése (Flush Timing)**: Az eszközön szükséges megerősíteni, hogy a "Show Q-Link Status When Touched" beállítás átkapcsolása a Preferences menüben azonnal kiírja-e a `/data/Settings/MPC/MPC.settings` fájlt a háttértárra, vagy megvárja az alkalmazásból való kilépést / leállítást.
3. **A Sounds böngésző preset-navigációja**: Fizikai hardveren ellenőrizendő, hogy a grafikus csempe megérintése Sounds módban helyesen listázza-e a `Presets/` mappában lévő `.xpl` preseteket az MPC OS 3.10.1+ összes firmware-verzióján.
