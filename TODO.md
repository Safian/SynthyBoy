# SynthyBoy — nyitott teendők

Szabály (owner, 2026-11-20): minden talált hiba, hiány és nyitott kérdés ide kerül, hogy ne vesszen el.
Ami elkészült, azt TÖRÖLD innen (nincs „kész" rész: a történet a commit üzenet). Új tétel: egy sor,
mi a baj, hol látszik (fájl:sor / eszköz / mérés), és mitől kész. Aki dolgozik rajta, írja a sor végére: `(fut: <ág>)`.

## Eszköz-felület (MPC XL, Key 61)
- Master oldal: kis piros „CLIP” felirat a kimeneti mérő fölött, mint a keverőkön (owner kérése, 2026-11-20). Akkor gyullad ki, amikor a motor végi master védelem működésbe lép (a jel ±1,8 fölé megy: `core/engine-own/dsp/master_stage.h`, `outputLimit`), mert ez a védelem ma láthatatlan és nem kapcsolható. Kell hozzá: a motor jelezze a felületnek, hogy a blokkban volt-e ilyen minta (zárolás és foglalás nélkül, a meglévő kiolvasó úton); a felirat tartson kb. 2 másodpercig, vagy koppintásra törlődjön, ahogy a host csúcskijelzője. Kész, ha egy szándékosan túlhajtott preseten kigyullad mindkét eszközön, tiszta játéknál soha, és a `test_rt_alloc` átmegy.
- Q-Link gombok az XL-en nem tesztelhetők gépről (nincs befecskendező eszköz, csak a Key 61-re van). Kész, ha a VOLUME Q-Linkkel is igazolt mindkét eszközön.
- Leütéserő az XL-en: a napló szerint az utolsó 16 hang mind 127-tel érkezett. Valódi padütéses próba (Full Level ki és be) az ownerrel; a Key 61 billentyűzet velocity-görbéje sincs megmérve.
- `sample.loop_link` (hurokhossz-összekötés): a specifikáció szerint csak szerkesztő-segéd, de a felületen nincs hozzá látható vezérlő (`shell-juce/ui/panels/OscPanel.cpp:92` csak listázza). Megvalósítani a szerkesztőben, vagy kivenni a kulcsot.
- MPC kimenet 0 dBFS fölött (owner: track +7,06 dB, kimenet +7,11 dB mérve a Key 61-en, +6 dB plugin hangerő és +5,82 dB fader): mit csinál a gép (kemény vágás a konverternél vagy saját védelem): nem mérte senki. Kész, ha egy loopback vagy rögzített kimenet (a gép saját felvétele vagy külső visszahurkolás) megmutatja a vágás módját, és a doksiba kerül.

## Rajzolás
- A V csúcsánál a két szár átfedi egymást és kétszer keveredik: áttetsző vagy fénylő vonalnál világosabb folt lehet. Szemrevételezés eszközön.
- A szerkesztő görbéje 256 pontról oszloponként egy pontra nőtt: a Key 61-en húzás közbeni képidő nincs megmérve (előtte / utána).
- A g103 három commitja (`a231f2c5`, `f806ab54`, `4203996e`) nincs átnézve (review: a „Könyvtár és repo helye" session).
- Key 61, gombtekerés közben: a GPU-s kép 99. percentilise 36–41 ms a 14 ms-os keret helyett (`docs/MPC.md` 19. próba).
- Fő layer kísérlet, XL 1.–2. szint KÉSZ (`agy/g110-xl-commit-shim`, fut): a host egyetlen szálról (a főszál, tid = pid) commitol, 51 képkocka-commit (flags 0x1, user_data nem NULL) és 2 kurzor-commit (0x200, NULL) 5 perc UI-aktivitás alatt (üresjáratban 0/s, csak UI-eseményre rajzol), a valódi commit mediánja 13 ms (vsync-várakozás), `TEST_ONLY` + 46-os sík mind a 13 próbán 0-t adott (17–83 µs). Hátra: élő egyesítés (a 3. szint), a 15 → 23 elemű kérések listájának naplózása, a teszt-puffer felszabadítása `stage2 off`-nál. Eredeti sor: Fő layer kísérlet (a saját síkunk beillesztése a host képkocka-küldésébe, a `work/inhost-draw` hookjára építve): az owner jóváhagyta, nem indult el. Sorrend: XL csak olvasó vizsgálat, Key 61 próbaillesztés majd 30 perces élő futás, vágás/elrejtés próba, a mai `SETPLANE` késleltetésének mérése.

## Hangszint és leütés
- Key 61: a 160 presetes mérés (teljes és fél ütés, két kép presetenként) fut `.hw-local/mpc-perf/level-sweep-k61-full/` (indult 2026-11-20 16:56; fut: `work/sweep-fix`). Kész, ha `device-level-sweep.py verify` átmegy (a fél ütés célja ≥ 6 dB, a telítődő presetek, pl. Bend Formant Growl 6,3 dB, külön kivételek) és a képeket átnéztük. Az XL-en ugyanez még nincs meg (nincs pad-befecskendezés, a `seqnote` útvonal van).
- Mi volt az XL-en a „2/3" olvasat: a koordinátor szerint mentett, szerkesztett példány (−17,3 dB a gyári −13,6 helyett). Kiadási megjegyzés kell: régi projektben a presetet újra be kell tölteni.
- Kimeneti fokozat erősítése (fut: `agy/g108-output-stage-hardening`; `agy/g108-output-stage-hardening`: fordítási idejű zárolásmentesség-ellenőrzés, blokkonként egyszer olvasott cél): review és beolvasztás, utána teljesítménymérés a Key 61 profilon.
- A g107 javításai után nem futott második teljes kapukör.

## Eszközmérés és telepítés (új, 2026-11-20)
- `scripts/device-level-sweep.py` (ág: `work/sweep-fix`, c4deb6c8, nincs develop-ban): (a) a play-log számláló újraindulását (app újraindítás után a régi `/tmp/synthyboy-play-log.txt` sorok) nem kezeli (`hit_reached_plugin`, kb. 463–473. és 216–218. sor): a próba első 4 presetje elbukott; (b) a `verify` fix 11,9 ± 3 dB félütés-célt vár minden presetre, a telítődőknek az offline táblából (`docs/release/OUTPUT-LEVEL-TABLE-2026-11-20.md`) kellene presetenkénti várt érték. Kész, ha mindkettő javítva és tesztelt, és a ág be van olvasztva.
- A telepítő szkriptek (`k61-install.sh`, `xl-install.sh`) a `.hw-local/mpc-perf/` alatt vannak, nem a repóban, és 2026-11-20-ig nem szinkronizálták a `Presets` és `Content` mappát (minden korábbi eszközmérés régi presetekkel ment). Javítva, de nincs repóban és nincs teszt. Kész, ha a szkriptek a `scripts/` alatt vannak, és egy teszt ellenőrzi, hogy a csomag mappái mind átkerülnek.
- A Key 61 Track Mixer útvonala (hwctl `raw "90 34 7f"` + `"90 34 00"` = Main, mód-menü rács (58,52), 2. oldal nyíl (1240,455), TRACK MIXER (1092,160); csúcstartó nullázás koppintás kb. (38,698)) nincs a `docs/MPC.md`-ben. Kész, ha leírt és a sweep-szkript hivatkozik rá.
- A Key 61 padjei MIDI 72..98 hangot küldenek, egyik sem a 48-at (C3): a szintmérés `seqnote` hangot használ, nem padet. Kész, ha dokumentált, és a valódi padütéses próba (owner) megvan.
- `pq.sh` fixen a Key 61-et olvassa: XL-en hibás gépre ment. Kész, ha van `--device` kapcsoló vagy XL-es megfelelő.
- A Key 61 `overlay-status.txt` `markers_read_*` sora a szerkesztőoldalon eltért a vártól (`left=723a2c right=3a7264` a várt `aa3a2c 3aaa64` helyett): megnézni a Track nézeten, hogy csak az oldal miatt volt-e.
- Régi `.hw-local/TODO-MPC.md` (2026-11-14) még nem ellenőrzött pontjai: böngészőcsempe (`Plugin Skins/browser_images/soundsmode.png` 270×110 a skin-generátorba), burkoló-görbület összevetése a host `Envelope` Quadratic rajzával, G2 csatorna-aftertouch (0xD0) a nyomás→cutoff rétegben, G3/G5/G9, MPCe Q-Link Mode X/Y → Macro 1–2 (mérés kell), pad-világítás plugin felől, automatizálás-felvétel (LFO n Cursor / Spectrum n), kétsávos próba (második SynthyBoy-sáv), Serum-preset 1:1 minta (owner), Steinberg VST2 terjesztés és Open MPC katalógus (owner), `feature/mpc-shell` ág törlése. Kész, ha mindegyiket élőnek (külön sor ide) vagy elavultnak (törölve) minősítettük; utána a régi fájl törölhető.

## Szintezés: Key 61 mérés vs offline (2026-11-20, `.hw-local/mpc-perf/level-sweep-k61-full/`, ismétlés: hat ütés presetenként)
- Négy preset ütésről ütésre ingadozik (Lo-Fi Rhodes 5,2 dB, Spectral Vowel Freeze 3,2, Vintage Electric Tine 3,4, Lorenz Garden 1,8; véletlen fázis vagy moduláció; az első ütés a betöltés után 3 presetnél a leghangosabb). A szintezés egyetlen offline renderre épül, tehát a csúcsok és az akkord-korlát (−0,55 dBFS) egy húzásra szól. Kész, ha a szintező több húzás maximumával számol (vagy az ingadozás okát megszüntettük), és a limiter nélküli akkord-szabály mért tartalékkal teljesül.
- Három preset állandóan eltér az offline táblától a Key 61-en: Vowel Table Lead +2,0 dB, Frozen Aurora +1,7 dB, Vocaloid Lead −2,5 dB (a `tierTrimDb` nem magyarázza: Vocaloid Leadnek 0 a trimje). Kész, ha az ok megvan (a `level_probe` `low` profilja eltér az eszköz tényleges beállításaitól?) és az offline szám egyezik az eszközzel 0,5 dB-en belül.
- Pitch Drop 808: az 1. v64 ütés a betöltés után 4,25 dB különbséget adott, a további ütések 6,09 dB-et (offline 6,12): a betöltés utáni első ütésnek más a szintje. Kész, ha az ok ismert (master.volume rámpa vagy első-ütés hatás).
- 18 preset v127–v64 különbsége 14 dB fölött van (Breakbeat Slice Keys 18,6, Lo-Fi Rhodes 18,4, Nylon Zone Pluck 17,4, Glass Zone Bells 17,1 …): erősebb dinamika, mint a gyári 8–12 dB. Owner-döntés kell, kell-e kezelni.
- Key 61, 2026-11-20 20:21: a példány Master oldalán a `master.volume` +6,0 (Q-Link 1 végig felfelé, a play-log `setParameter 2703 (Q 1, real 0)` 44 lépés), a `lfo1.shape` 12, `quality.oversample_4x` 1, `quality.no_smoothing` 1, `master.voice_override` 1 is eltért a gyári presettől; a play-log egyiket sem mutatja `setParameter`-ként. A gyári Acid Saw Reso hook-os újratöltése után mind gyári (−4, 0, 0, 0, 0): a telepített preset rendben van. Kód szerint megerősítve: az overlay érintéses írásai a `PluginInstance::uiWrite` → `writeRealParam` útján mennek (`shell-mpc/synthyboy_mpc.cpp` kb. 1022. sor), ott nincs `PlayLog::line`; a play-log csak a host `setParameter` hívásait (kb. 3049–3109. sor) naplózza, ezért az owner érintéses szerkesztései (a kapcsolók, az LFO-szerkesztő V-alakja → `lfo1.shape` 12) nem látszanak benne; a `master.volume` +6,0 az owner szándékos Q-Link állítása volt: a play-log ne csak a host hívásait, hanem az overlay paraméter-írásokat is naplózza forrásmegjelöléssel. Kész, ha a log megmutatja, ki állított egy paramétert (host / Q-Link / overlay-érintés / hook).
- Eszközfuttatások szabálya: minden futtatás végén a példányt vissza kell állítani a kiinduló presetre a hookkal (gyári állapot), és a futtatás a változtatott paramétereket jelenti. Kész, ha a sweep és a futtató szkriptek ezt maguk teszik és a jelentésben szerepel.

## Kiadás-csomagolás (owner, 2026-11-20: két zip, armv7 és aarch64, az Open MPC eszközével, ugyanaz a plugin, kijelzőréteggel)
- Jelenleg EGY `SynthyBoy-MPC-<verzió>.zip` készül (`docs/RELEASE.md` 11. sor, `scripts/package-mpc-release.py`), és a `scripts/build-mpc.sh` alapértelmezése `--arch all` egy csomagkönyvtárba (`VST/synthyboy-arm32.so` + `synthyboy-arm64.so` + mindkét segédfolyamat). Kész, ha a kiadás két zip: `armv7` (Key 61 és a többi 32 bites gép) és `aarch64` (XL), mindegyik a saját architektúrájú pluginnal és segédfolyamattal, a kijelzőréteg bekapcsolva, és a `docs/RELEASE.md` ezt írja le.
- Az „Open MPC eszköz" (amivel a két zip épülne) nincs azonosítva a csomagoló szkriptekben: a `build-mpc.sh` a Docker/Debian 12 keresztfordítót és az MPC SDK validátorát használja. Kész, ha tudjuk, melyik az az eszköz (katalógus-bejegyzés / builder), és a kiadási lépés azzal készül, vagy leírt döntés, hogy a saját szkript marad.
- A fejlesztői csomagok (ov40–ov43) `-DSYNTHYBOY_MPC_DEBUG_HOOKS=ON`-nal és mindkét architektúrával épültek (Docker kereszt-build); a kiadási zipben a hooks KI kell legyen. Kész, ha a kiadási lánc a hookokat kikapcsolja, és egy teszt ellenőrzi, hogy a kiadási binárisban nincs `synthyboy-select-program` / play-log string.
- Fejlesztéskor és teszteléskor csak az eszköz architektúrájára építünk: Key 61 `--arch arm32`, XL `--arch arm64`; mindkettő csak kiadásnál és kiadásjelöltnél (a koordinátor ma mindkettővel épített: ettől kezdve nem). Kész, ha a gate- és csomag-feladatsablonok ezt írják (a `.hw-local` sablonokban és a Gemini-feladatokban).

## Saját motor (egy motor lesz: a saját)
- X|Y „Density" tengely: a specifikáció (`docs/engine-spec/07`, 7a.2) szerint csak szabad ütemű módban hat, a kód (`core/engine-own/osc/lib_osc.cpp`, X|Y blokk) feltétel nélkül ír. Ellenőrizni.
- A paraméter-audit adatai ellentmondanak egymásnak (`docs/ui-design/param-audit-keys.csv`: ALIVE, `param-audit-baseline-own.json`: UNSUPPORTED): az audit alapját újra kell generálni az X|Y kulcsok bekapcsolása után.
- A három audit független ellenőrzése megvan (Gemini Argon, megcáfolni próbálta mindegyiket): `docs/review/OWN-ENGINE-DSP-AUDIT-{1,2,3}-VERIFICATION-2026-11-20.md`. Javítani CSAK az ellenőrző doksi adott szakasza alapján szabad (az adja a helyes fájl:sort és azt, mi téves az auditban). Ítéletek:
  - 1. rész (27): megerősítve 17; a kód stimmel, a hatás mérendő: 5, 9, 18, 23, 26, 27; részben téves: 2, 8, 10, 21.
  - 2. rész (19): megerősítve 15; a kód stimmel, a hatás mérendő: 6, 10; részben téves: 5, 18.
  - 3. rész (18): megerősítve 1, 5, 9, 17; a kód stimmel, a hatás mérendő: 6, 8, 10, 11, 12, 13, 18; részben téves: 2, 3, 4, 7, 14, 15, 16.
- Megvalósítási sorrend: a 3. rész ellenőrző doksijának „Work List Check" szakasza (15 lépés, az ítéletek után átrendezve). Az elszigetelt, megbízható teljesítménymérés az első (3. rész 9.); a determinisztikus hibajavítások (kis, tesztelhető tételek) ezzel párhuzamosan mehetnek.
- Javítási kör (teszt először; a kis tételek fordítás nélkül készülnek, egy build worker fordít kötegenként egyszer). Kész és a `develop`-ban van: sub pan, fésűszűrők külön csillapítása, létraszűrők korlátja (a dióda kivételével), mip-lekerekítés, HOST-LFO időszámítás, flanger visszacsatolás, multisample 1 pólusú szűrők, háromsávos splitter, granulár hurok-átúsztatás (mindkét irány), torzító visszaállítása és állandói, Bode vágása, kikapcsolt szűrők kihagyása, effekt-lecsengés hossza a valódi mintavételi frekvenciából, a CLIP jelző motor oldala. A kör végén: az érintett presetek listája a koordinátornak, majd egyszer az alapok, a szinttábla és a kapukör.
- Visszaküldve a 2. kötegből (a javítás megvan az ágon, de a teszt a javítással is elbukik, ezért nincs beolvasztva): CLAP shell lecsengés és mono (`agy/nb2-clap-tails`: a teszt zengetőt feltételez, de a próbában nem szól lecsengés); multisample erősítés mintánkénti rámpája (`agy/nb2-multi-gain-ramp`: a legnagyobb lépés 0,00102-ről 0,0000551-re csökkent, de a teszt 0,0000480-at vár). Mindkettőhöz bővebb terv kell.
- Minden hangot változtató javítás után: a bitre azonos alapok (refcompare, paraméter-audit, arany renderek) és a preset-szintek újraellenőrzése egyszer, a javítási kör végén.

## DSP-audit 1. rész megállapításai (hangút) — `docs/review/OWN-ENGINE-DSP-AUDIT-1-VOICE-PATH-2026-11-20.md`
Gemini 4 Neon írta, kódolvasásból, mérés nélkül. Minden tételt a megvalósítás ELŐTT ellenőrizni kell a kódban, és a doksiban megnevezett méréssel bizonyítani. A sorszám a doksi rangsora. Hat tételt szúrópróbával megnéztem: az eredmény a sor végén.
- 1. (maradék) Diódalétra: a `gSafe <= 0.95` korlát ott megmaradt (10,67 kHz 44,1 kHz-en), mert a visszacsatolásában egy mintányi késleltetés van, és a korlát emelése instabillá tenné (`core/filters-serum/src/filter_dsp.h`, `DiodeLadder`). Kész, ha a visszacsatolás késleltetés nélküli megoldásra van átírva és a létrák stabilitási tesztjei rá is átmennek.
- 2. A decimáló szűrő unison-hangonként fut, nem egyszer az összegen (`core/engine-own/osc/wt_osc.h`): nagy CPU-nyereség várható.
- 3. Spektrális külső Mask/Vocode: 2048 pontos FFT minden 32 mintában az 512 mintás lépés helyett (`core/engine-own/osc/lib_osc.cpp:554-569`).
- 4. Warp előtti sávhatár `3.2 * sampleRate` 4x túlmintavételezésnél. Szúrópróba: a szám megvan (`wt_osc.h:672`), de a 2–3,2 fs közti összetevők a hallható sáv fölé tükröződnek és a decimáló kiszűri őket; csak a nemlineáris warpok keverhetik vissza. Feltevés, mérés dönti el.
- 5. Kemény nemlinearitások és hard sync csak túlmintavételezésre támaszkodnak (a „low" szinten 2x), és mintánként `libm` hívások futnak (`core/engine-own/osc/warp.h`): ADAA / BLEP és polinomos közelítés.
- 6. Frame-pásztázás nincs simítva a 32 mintás vezérlési perióduson belül: 1378 Hz-es lépcsőzaj frame-modulációnál (`wt_osc.h:222,705-713`).
- 7. Sample és granulár: a mip-szint választása nem veszi figyelembe a lejátszási sebességet, felfelé hangoláskor akár egy oktávnyi szűretlen aliasing (`core/sample-osc/src/sample_voice.cpp`, `core/granular/src/granular.cpp`).
- 9. Mintánkénti `pow` / `tan` / trigonometria és osztás a szűrők belső ciklusában (`core/engine-own/voice/filter_unit.h`, `core/filters-serum/src/filter_dsp.h`, `filter.cpp`, `classic_filters.h`).
- 10. Memória: minden szűrő 14 késleltetővonalat foglal, a `LibOsc` hangonként előre lefoglal mindent; 60–80 MB munkakészlet, hideg gyorsítótár a Key 61-en.
- 11. Nyers NEON lebegőpontos összehasonlítások maradtak (`wt_osc.h`, `core/spectral/src/kernels.cpp`), ARMv7-en skalárra esnek vissza.
- 12. Spektrális morph: teljes 2048 pontos komplex IFFT a fél méretű valós helyett, magas hangoknál is.
- 16. A szűrő vágási frekvenciája lineáris Hz-ben simít 5,5 ms-mal: elkeni a gyors szűrőburkolót.
- 17. (maradék) Granulár hurok-átúsztatás, szélső esetek (`docs/review/GRANULAR-LOOP-CROSSFADE-ANALYSIS-2026-11-20.md`, Q6): ha a hurokhatár a fájl szélétől 1–2 mintán belül van, az interpoláció a fájlon kívüli (nulla) mintát is érinti; ha se a hurok előtt, se utána nincs elég anyag, az átúsztatás a rendelkezésre álló hosszra rövidül, teljes fájlos huroknál megszűnik. Eldöntendő, kell-e ezekre külön megoldás (pl. körbeforduló olvasás).
- 19. Lorenz / Rössler véletlen LFO: Euler-lépés a stabilitási határon, a sebesség 1,38 Hz alatt és 27,6 Hz fölött nem változik (`core/engine-own/env_lfo/lfo.h:182-210`).
- 21. Zajgenerátorok: 44,1 kHz-re rögzített együtthatók; a zajszűrő és a hangmagasság a 4–8. színt kihagyja; a sample-zaj mindig „high" minőségben fut.
- 22. Mátrix: a hangfrekvenciás források 1378 Hz-re mintavételeződnek szűrés nélkül; aktív slotonként `exp` / `expm1` minden 32 mintában. (Ide tartozik: a mátrix nem mintánként számol, `core/engine-own/README.md`.)
- 23. Mip-szint váltás átúsztatás nélkül: hallható ugrás hajlításnál.
- 24. (maradék) Multisample: az erősítés 32 mintánként lép, nem mintánként (`core/engine-own/osc/lib_osc.cpp:975,991`).
- 25. Keresztmoduláció blokkon belüli eseményfelosztásnál rossz indexre ír; a kattanásgátló csak a Main buszon él; tétlen globális LFO-k is futnak.
- 26. Klasszikus létra és dióda: 8–11 skalár osztás mintánként; több szűrőmodell nincs NEON-ra írva.
- 27. Bitre azonos skalár = NEON szabály (nincs FMA, nincs közelítő reciprok) mindenhol; a `fastmath.h` `union`-nal típust vált (nem definiált viselkedés).
- A Vital kivezetésének terve: build-kapcsolók (`SYNTHYBOY_WITH_VITAL`, `SYNTHYBOY_SHELL_ENGINE`), az asztali plugin alapmotorja, a Vitalhoz mérő eszközök (`tools/own-oracle`, refcompare, paraméter-audit), `NOTICE` / `THIRD_PARTY.md`, a Vital-preset importáló, és a `wtedit::vital` adatmodell licenckérdése (`docs/ui-design/OWN-ENGINE-GAP.md`, történet 5. pont).

## DSP-audit 2. rész megállapításai (effektek, kimenet) — `docs/review/OWN-ENGINE-DSP-AUDIT-2-EFFECTS-OUTPUT-2026-11-20.md`
Gemini 4 Neon írta, kódolvasásból, mérés nélkül; megvalósítás előtt minden tétel ellenőrizendő és mérendő. Öt tételt szúrópróbával megnéztem: az eredmény a sor végén. A jelentés szerint a lánc vége (kimeneti fokozat, master térd, hangszám-függő erősítés) rendben van, a hibák az effektekben vannak.
- 1. Torzító: mind a 14 típus túlmintavételezés és ADAA nélkül fut (erős aliasing a kemény típusoknál); a 12–13. típus (X-Shaper) csak kemény vágás. Szúrópróba: a torzító blokkjában nincs túlmintavételezés (`core/engine-own/fx/fx_effects.h:660-740`).
- 3. A splitter sávjai között nincs késleltetés-kiegyenlítés összeolvasztás előtt: Bode vagy limiter egy sávban fésűszűrőzést okoz (`core/engine-own/fx/fx_chain.cpp:583-610`).
- 4. Konvolúció: egyenletes 128 mintás partíciók (33,5 MB), komplex FFT valós adaton, impulzuscsere átúsztatás nélkül, adatverseny a `live.tone`-on (`core/fx-serum-b/convolve.cpp`, `fx_modules.cpp:1014`).
- 5. Zengető: minden példány mind a 6 algoritmus pufferét lefoglalja (4,7 MB); mintánként sok `sin` / `exp` / osztás; a Nitrous Space csillapítása negatív erősítést ad.
- 6. Késleltető HQ mód: a 8 pontos Lagrange 56 osztást végez mintánként és csatornánként. Szúrópróba: igaz, beágyazott ciklus osztással (`core/engine-own/fx/fx_dsp.h:65-80`).
- 7. (maradék) Bode frekvenciatoló: mintánként 64 egész modulo és 4 trigonometria (`core/fx-serum-b/bode.cpp`); a Hilbert-szűrő 800 Hz alatt gyenge. A ±1-es vágás visszacsatolás nélkül már javítva.
- 8. Kompresszor többsávos módban 100% alatti mixszel: a nedves jel átmegy a keresztváltón, a száraz nem, fáziskioltás a keresztezési frekvenciákon; a limiter mintánként lineárisan pásztáz.
- 9. Hangszám-függő erősítés csak a vezérlési rács elején frissül: közepes és magas szinten egy akkord akár 31 mintáig csillapítatlanul szól (`core/engine-own/own_engine.cpp:1461-1470,1578-1655`).
- 10. Szinte minden effekt 32 mintánként lépteti a paramétereit (lépcsőzaj); a „low" szinten a chorus 4, a phaser 8 mintáig tartja az értéket.
- 11. (maradék) Flanger: fölösleges 512 KB puffer (900 ms, miközben legfeljebb 100 ms kell) és mintánkénti osztás.
- 12. Hyper / Dimension: 11 puffer ugyanazzal a jellel, mintánként akár 23 `sin` / `cos`; a Dimension 0% mixnél is fut.
- 13. CLAP shell: hang nélkül azonnal alvást jelez, levágja a zengető és késleltető lecsengését; mono kimenetnél a bal csatornát felülírja a jobb. Szúrópróba: mindkettő igaz (`shell-clap/synthyboy_clap.cpp:353,392`). JUCE shell: `setLatencySamples()` a hangszálon (`shell-juce/PluginProcessor.cpp:494`).
- 14. (maradék) Torzító: a szűrő Q-ja nem a specifikáció szerinti (lineáris `k = 2 - 1.9r` a `Q = 0.5 + 15.5 r^3` helyett: `core/engine-own/fx/fx_effects.h`, `Distortion`). A visszaállítás és a mintánkénti állandók már javítva.
- 15. EQ: osztás mintánként sávonként; mind a 4 sáv együtthatói 32 mintánként újraszámolódnak akkor is, ha nem változnak; torzulás a Nyquist közelében.
- 16. (maradék) Effektlánc: bekapcsoláskor 0,73 ms-os felúsztatás, kikapcsoláskor nincs leúsztatás (kattanás, levágott lecsengés); sorrendváltásnál nincs átúsztatás; a 255 mintánál hosszabb késleltetés csendben csonkul; a némított splitter-sávok effektjei is futnak. A beégetett `44100` már javítva.
- 17. Chorus: rögzített nedves szint a hangszámtól függetlenül (1 és 4 hang között szintugrás); phaser: nincs denormális-védelem a visszacsatolásban; a szinkronizált LFO-k nem igazodnak az ütemhez.
- 18. Öt effekt (kompresszor, Hyper, Utility, Bode, konvolúció) 0% mixnél sem marad ki a „low" szinten; a lecsengés-tartás 20 másodperc minden típusra.
- 19. Kimenet: a limiter mintacsúcsot néz, nem valódi csúcsot; az `OUTPUT-LEVEL.md` ellentmond magának. Szúrópróba: igaz, a 62. sor még `T = -4.50 dBFS`-t ír a −10,0 mellett.

## DSP-audit 3. rész megállapításai (motor magja, teljesítmény, memória) — `docs/review/OWN-ENGINE-DSP-AUDIT-3-CORE-PERFORMANCE-2026-11-20.md`
Gemini 4 Neon írta, kódolvasásból, mérés nélkül; megvalósítás előtt minden tétel ellenőrizendő és mérendő. Három tételt szúrópróbával megnéztem: az eredmény a sor végén.
- 1. A `prepare()` szinttől függetlenül mind a 36 hangot és mind a 13 effektet lefoglalja 3 rackre: 70–75 MB példányonként a „low" szinten is (8 hang használható).
- 2. `ContentHub`: 8 elemű körpuffer szinkronizálás nélkül, felszabadítás úgy, hogy a hangszál még olvashatja (use-after-free gyors LFO- vagy görbehúzásnál).
- 3. `WakeSignal::notify()` a hangszálról: mutex macOS / iOS / Windows alatt, `futex` rendszerhívás Linuxon; a konvolúció a hangszálon engedhet el `shared_ptr`-t. Szúrópróba: a futex-ébresztés megvan (`core/engine-own/dsp/helper_thread.h:71`); azt, hogy a hangszálról hívódik, nem ellenőriztem, és a futex-ébresztés önmagában nem blokkol.
- 4. ARM32 fordítás `-march=armv7-a`-val a `-mcpu=cortex-a17` helyett (szoftveres egész osztás), hiányzó `-fno-math-errno`, nincs LTO. Szúrópróba: a jelző megvan (`cmake/toolchains/armv7-linux-gnueabihf.cmake:16`).
- 5. 16 egyszerre érkező hang megkerüli a hangszám-korlátot az ütés blokkjában (Key 61-en 8 a korlát): korlátlan munka egy blokkban.
- 6. A `Voice` 33 KB, a forró és hideg mezők keverve; minden oszcillátornak saját 24,6 KB-os morph-munkaterülete van; kb. 3500 apró foglalás példányonként.
- 7. Egyetlen paraméter mozdítása minden blokkban újramásolja mind a 2637 paramétert, újrafordítja a mátrixot és újraszámolja a hangolást (Q-Link tekerésnél tüske).
- 8. Apple ARM64-en a NEON ki van kapcsolva (iOS és Apple Silicon skalár ciklusokon fut); a CLAP shellben és a motorban nincs denormális-védelem (FTZ/DAZ) beállítva.
- 9. Mérési módszer: a `tier_bench` normál ütemezéssel fut a host valós idejű szála mellett (20–25% torzítás a p99-ben, 18% napi szórás), nincs modulonkénti költségtábla. A munkalista ELSŐ lépése: megbízható, elszigetelt mérés.
- 10. A beépített hullámtábla-építő szál nem hívja a segédszál-indító horgot (valós idejű ütemezést és hangmag-affinitást örökölhet), és 10 ms-onként örökké ébred.
- 11. Az importált presetek 4x túlmintavételezés-kapcsolója a „low" szinten nincs korlátozva; a „low" szint 5 effektet teljesen kihagy 31+ gyári presetnél, takarékos pótlék nélkül.
- 12. `process()`: fölösleges nullázás és felülírás, néma buszok feldolgozása, egy `assign()`, ami túlméretes mono blokknál foglalhat.
- 13. MPC shell: az eseménysor pörgő zárat használ; a preset betöltése 2637 paramétert egyenként ír (szakadozó hang presetváltáskor).
- 14. Nulla hangerejű, de még aktív hangok teljes költséggel futnak; a kijelző-visszaolvasás `exp` / `log10` hívásokat végez a hangszálon.
- 15. Nincs `mlock`, a laphibák nincsenek számolva; egy halott tartalomkészlet a memóriában ragad.
- 16. Sehol nincs igazítás (`alignas`) a saját motorban; a `fastmath` segédeknek nincs védelme szélső értékekre.
- 17. A README 20,5 MB-ot ír a hullámtábla-bankra, a jelentés szerint valójában kb. 33,1 MB. Szúrópróba: a 20,5 MB a README-ben megvan; a 33,1 MB-ot nem ellenőriztem.
- 18. A mérőprogram más `libm`-et használ, mint az eszközön futó plugin; kivételkezelés és RTTI bekapcsolva a DSP-könyvtárakban.

## Tudás és ügynök-eszközök
- Tudásmappa (`docs/dsp-knowledge/`: témánként rövid fájl forrásokkal, döntésekkel és bizonyító teszttel, tisztaszoba-szabály, tesztreceptek) és egy rá mutató vékony skill: az owner döntésére vár.
- A `.hw-local/synth-knowledge/vital/code` (405 fájl) és `sources/` Vital-forráskódot tartalmaz: a saját motor munkáihoz tiltani kell, vagy el kell vinni onnan (owner döntése).
- Automatikus ellenőrzés: bukjon el a kapu, ha a `core/engine-own/` alá Vital-azonosító vagy GPL-fejléc kerül.
- A kutatásban csak emlékezetből felsorolt forrásokat (Bristow-Johnson, Brandt, Dattorro, Bencina, MPE- és SFZ-specifikáció) meg kell nyitni, mielőtt a tudásmappába kerülnek.

## Repó-rend
- `agy/g54-pack-versions-010` (0.1.0 verziószövegek): beolvasztás a kiadásjelölt körben.
- `agy/g84-reduced-set-proposal` (csökkentett preset-készlet): az owner döntésére vár.
- `work/sound-batch-3` (24 új preset): meghallgatásra vár.
