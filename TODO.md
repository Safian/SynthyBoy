# SynthyBoy — nyitott teendők

Szabály (owner, 2026-11-20): minden talált hiba, hiány és nyitott kérdés ide kerül, hogy ne vesszen el.
Ami elkészült, azt TÖRÖLD innen (nincs „kész" rész: a történet a commit üzenet). Új tétel: egy sor,
mi a baj, hol látszik (fájl:sor / eszköz / mérés), és mitől kész. Aki dolgozik rajta, írja a sor végére: `(fut: <ág>)`.

## Eszköz-felület (MPC XL, Key 61)
- VOLUME gomb a Master oldalon: kb. 6 képpont alatti húzás nem csinál semmit (holtsáv), finoman nem állítható (XL-mérés: −17,30 helyett csak −16,88-ra lehetett visszatenni). Kész, ha 0,1 dB-es lépés érintéssel beállítható.
- VOLUME gomb: az értékre koppintva nincs számbevitel. Kész, ha szám írható be, vagy leírt döntés van, hogy nem lesz.
- A húzás érzékenysége nem állandó (30 képpont = 5–7 dB). Ellenőrizni, hogy ez szándékos görbe-e.
- Q-Link gombok az XL-en nem tesztelhetők gépről (nincs befecskendező eszköz, csak a Key 61-re van). Kész, ha a VOLUME Q-Linkkel is igazolt mindkét eszközön.
- Leütéserő az XL-en: a napló szerint az utolsó 16 hang mind 127-tel érkezett. Valódi padütéses próba (Full Level ki és be) az ownerrel; a Key 61 billentyűzet velocity-görbéje sincs megmérve.
- `sample.loop_link` (hurokhossz-összekötés): a specifikáció szerint csak szerkesztő-segéd, de a felületen nincs hozzá látható vezérlő (`shell-juce/ui/panels/OscPanel.cpp:92` csak listázza). Megvalósítani a szerkesztőben, vagy kivenni a kulcsot.

## Rajzolás
- Meredek V az LFO-szerkesztőben a Key 61-en: az XL-en az owner megnézte, jó (2026-11-20, ov43); a Key 61-en (csökkentett részletesség, minden második pont) még senki nem nézte meg. Kész, ha ott is látszik a teljes vonal.
- A V csúcsánál a két szár átfedi egymást és kétszer keveredik: áttetsző vagy fénylő vonalnál világosabb folt lehet. Szemrevételezés eszközön.
- A szerkesztő görbéje 256 pontról oszloponként egy pontra nőtt: a Key 61-en húzás közbeni képidő nincs megmérve (előtte / utána).
- A g103 három commitja (`a231f2c5`, `f806ab54`, `4203996e`) nincs átnézve (review: a „Könyvtár és repo helye" session).
- Key 61, gombtekerés közben: a GPU-s kép 99. percentilise 36–41 ms a 14 ms-os keret helyett (`docs/MPC.md` 19. próba).
- Fő layer kísérlet (a saját síkunk beillesztése a host képkocka-küldésébe, a `work/inhost-draw` hookjára építve): az owner jóváhagyta, nem indult el. Sorrend: XL csak olvasó vizsgálat, Key 61 próbaillesztés majd 30 perces élő futás, vágás/elrejtés próba, a mai `SETPLANE` késleltetésének mérése.

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

## Saját motor (egy motor lesz: a saját)
- X|Y „Density" tengely: a specifikáció (`docs/engine-spec/07`, 7a.2) szerint csak szabad ütemű módban hat, a kód (`core/engine-own/osc/lib_osc.cpp`, X|Y blokk) feltétel nélkül ír. Ellenőrizni.
- A paraméter-audit adatai ellentmondanak egymásnak (`docs/ui-design/param-audit-keys.csv`: ALIVE, `param-audit-baseline-own.json`: UNSUPPORTED): az audit alapját újra kell generálni az X|Y kulcsok bekapcsolása után.
- A három audit független ellenőrzése megvan (Gemini Argon, megcáfolni próbálta mindegyiket): `docs/review/OWN-ENGINE-DSP-AUDIT-{1,2,3}-VERIFICATION-2026-11-20.md`. Javítani CSAK az ellenőrző doksi adott szakasza alapján szabad (az adja a helyes fájl:sort és azt, mi téves az auditban). Ítéletek:
  - 1. rész (27): megerősítve 17; a kód stimmel, a hatás mérendő: 5, 9, 18, 23, 26, 27; részben téves: 2, 8, 10, 21.
  - 2. rész (19): megerősítve 15; a kód stimmel, a hatás mérendő: 6, 10; részben téves: 5, 18.
  - 3. rész (18): megerősítve 1, 5, 9, 17; a kód stimmel, a hatás mérendő: 6, 8, 10, 11, 12, 13, 18; részben téves: 2, 3, 4, 7, 14, 15, 16.
- Megvalósítási sorrend: a 3. rész ellenőrző doksijának „Work List Check" szakasza (15 lépés, az ítéletek után átrendezve). Az elszigetelt, megbízható teljesítménymérés az első (3. rész 9.); a determinisztikus hibajavítások (kis, tesztelhető tételek) ezzel párhuzamosan mehetnek.
- Folyamatban lévő javítások (teszt először, Gemini Flash, tételenként egy ág): 1. rész 15. sub pan (fut: `agy/fix-sub-pan`); 1. rész 14. fésűszűrők közös állapota (fut: `agy/fix-comb-lp`); 1. rész 17. granulár hurok-átúsztatás (fut: `agy/fix-gran-loop`).
- Minden hangot változtató javítás után: a bitre azonos alapok (refcompare, paraméter-audit, arany renderek) és a preset-szintek újraellenőrzése egyszer, a javítási kör végén.

## DSP-audit 1. rész megállapításai (hangút) — `docs/review/OWN-ENGINE-DSP-AUDIT-1-VOICE-PATH-2026-11-20.md`
Gemini 4 Neon írta, kódolvasásból, mérés nélkül. Minden tételt a megvalósítás ELŐTT ellenőrizni kell a kódban, és a doksiban megnevezett méréssel bizonyítani. A sorszám a doksi rangsora. Hat tételt szúrópróbával megnéztem: az eredmény a sor végén.
- 1. Létraszűrők (Moog/EMS/Acid, dióda): a `gSafe <= 0.95` korlát 10,67 kHz-nél megállítja a vágási frekvenciát 44,1 kHz-en. Szúrópróba: a korlát megvan (`core/filters-serum/src/filter.cpp:406`).
- 2. A decimáló szűrő unison-hangonként fut, nem egyszer az összegen (`core/engine-own/osc/wt_osc.h`): nagy CPU-nyereség várható.
- 3. Spektrális külső Mask/Vocode: 2048 pontos FFT minden 32 mintában az 512 mintás lépés helyett (`core/engine-own/osc/lib_osc.cpp:554-569`).
- 4. Warp előtti sávhatár `3.2 * sampleRate` 4x túlmintavételezésnél. Szúrópróba: a szám megvan (`wt_osc.h:672`), de a 2–3,2 fs közti összetevők a hallható sáv fölé tükröződnek és a decimáló kiszűri őket; csak a nemlineáris warpok keverhetik vissza. Feltevés, mérés dönti el.
- 5. Kemény nemlinearitások és hard sync csak túlmintavételezésre támaszkodnak (a „low" szinten 2x), és mintánként `libm` hívások futnak (`core/engine-own/osc/warp.h`): ADAA / BLEP és polinomos közelítés.
- 6. Frame-pásztázás nincs simítva a 32 mintás vezérlési perióduson belül: 1378 Hz-es lépcsőzaj frame-modulációnál (`wt_osc.h:222,705-713`).
- 7. Sample és granulár: a mip-szint választása nem veszi figyelembe a lejátszási sebességet, felfelé hangoláskor akár egy oktávnyi szűretlen aliasing (`core/sample-osc/src/sample_voice.cpp`, `core/granular/src/granular.cpp`).
- 8. HOST-hoz zárt LFO: a `songSeconds` a `bpm` és a `songBeats` betöltése ELŐTT számolódik (`core/engine-own/own_engine.cpp:1371-1378`). Szúrópróba: a sorrend tényleg ez; az viszont nem igaz, hogy a `songBeats` nem halad blokkon belül (`own_engine.cpp:1154` lépteti).
- 9. Mintánkénti `pow` / `tan` / trigonometria és osztás a szűrők belső ciklusában (`core/engine-own/voice/filter_unit.h`, `core/filters-serum/src/filter_dsp.h`, `filter.cpp`, `classic_filters.h`).
- 10. Memória: minden szűrő 14 késleltetővonalat foglal, a `LibOsc` hangonként előre lefoglal mindent; 60–80 MB munkakészlet, hideg gyorsítótár a Key 61-en.
- 11. Nyers NEON lebegőpontos összehasonlítások maradtak (`wt_osc.h`, `core/spectral/src/kernels.cpp`), ARMv7-en skalárra esnek vissza.
- 12. Spektrális morph: teljes 2048 pontos komplex IFFT a fél méretű valós helyett, magas hangoknál is.
- 13. Mip-táblák lekerekítése: a feltétel 80%-nál lép be, a koszinusz 85%-tól indul, 80–85% között horpadás. Szúrópróba: igaz (`core/engine-own/osc/wavetable_bank.cpp:300-301`; 80%-nál a súly 0,75).
- 14. `Combs` és `Reverb` szűrőtípus: egy közös aluláteresztő állapot a négy fésűvonalra (`filter.cpp:1294,1326`). Szúrópróba: nem tudtam megerősíteni.
- 15. Sub oszcillátor: a `subTick.panRight` soha nem kap értéket, a jobb csatorna nem követi a pant (`core/engine-own/voice/voice.cpp:1390`). Szúrópróba: ilyen értékadást nem találtam, valószínűleg igaz.
- 16. A szűrő vágási frekvenciája lineáris Hz-ben simít 5,5 ms-mal: elkeni a gyors szűrőburkolót.
- 17. Granulár hurok-átúsztatás negatív indexről olvas, ha a hurok eleje közelebb van, mint az átúsztatás hossza; visszafelé játszott szemcsénél nem működik (`granular.cpp:485,739`).
- 18. A kikapcsolt szűrők ágai is lefutnak (`voice.cpp:1751-1824`).
- 19. Lorenz / Rössler véletlen LFO: Euler-lépés a stabilitási határon, a sebesség 1,38 Hz alatt és 27,6 Hz fölött nem változik (`core/engine-own/env_lfo/lfo.h:182-210`).
- 20. Unison középső hang: +3 dB kiemelés 0% szórásnál (`wt_osc.h:257-271,384`).
- 21. Zajgenerátorok: 44,1 kHz-re rögzített együtthatók; a zajszűrő és a hangmagasság a 4–8. színt kihagyja; a sample-zaj mindig „high" minőségben fut.
- 22. Mátrix: a hangfrekvenciás források 1378 Hz-re mintavételeződnek szűrés nélkül; aktív slotonként `exp` / `expm1` minden 32 mintában. (Ide tartozik: a mátrix nem mintánként számol, `core/engine-own/README.md`.)
- 23. Mip-szint váltás átúsztatás nélkül: hallható ugrás hajlításnál.
- 24. Multisample: az 1 pólusú szűrők valójában 2 pólusúak; az erősítés 32 mintánként lép.
- 25. Keresztmoduláció blokkon belüli eseményfelosztásnál rossz indexre ír; a kattanásgátló csak a Main buszon él; tétlen globális LFO-k is futnak.
- 26. Klasszikus létra és dióda: 8–11 skalár osztás mintánként; több szűrőmodell nincs NEON-ra írva.
- 27. Bitre azonos skalár = NEON szabály (nincs FMA, nincs közelítő reciprok) mindenhol; a `fastmath.h` `union`-nal típust vált (nem definiált viselkedés).
- A Vital kivezetésének terve: build-kapcsolók (`SYNTHYBOY_WITH_VITAL`, `SYNTHYBOY_SHELL_ENGINE`), az asztali plugin alapmotorja, a Vitalhoz mérő eszközök (`tools/own-oracle`, refcompare, paraméter-audit), `NOTICE` / `THIRD_PARTY.md`, a Vital-preset importáló, és a `wtedit::vital` adatmodell licenckérdése (`docs/ui-design/OWN-ENGINE-GAP.md`, történet 5. pont).

## DSP-audit 2. rész megállapításai (effektek, kimenet) — `docs/review/OWN-ENGINE-DSP-AUDIT-2-EFFECTS-OUTPUT-2026-11-20.md`
Gemini 4 Neon írta, kódolvasásból, mérés nélkül; megvalósítás előtt minden tétel ellenőrizendő és mérendő. Öt tételt szúrópróbával megnéztem: az eredmény a sor végén. A jelentés szerint a lánc vége (kimeneti fokozat, master térd, hangszám-függő erősítés) rendben van, a hibák az effektekben vannak.
- 1. Torzító: mind a 14 típus túlmintavételezés és ADAA nélkül fut (erős aliasing a kemény típusoknál); a 12–13. típus (X-Shaper) csak kemény vágás. Szúrópróba: a torzító blokkjában nincs túlmintavételezés (`core/engine-own/fx/fx_effects.h:660-740`).
- 2. Háromsávos splitter: az alsó sáv fázisigazítása 4. rendű mindentáteresztő a 2. rendű helyett, bevágás a keresztezésnél (`core/fx-serum-a/.../splitter_lmh.*`).
- 3. A splitter sávjai között nincs késleltetés-kiegyenlítés összeolvasztás előtt: Bode vagy limiter egy sávban fésűszűrőzést okoz (`core/engine-own/fx/fx_chain.cpp:583-610`).
- 4. Konvolúció: egyenletes 128 mintás partíciók (33,5 MB), komplex FFT valós adaton, impulzuscsere átúsztatás nélkül, adatverseny a `live.tone`-on (`core/fx-serum-b/convolve.cpp`, `fx_modules.cpp:1014`).
- 5. Zengető: minden példány mind a 6 algoritmus pufferét lefoglalja (4,7 MB); mintánként sok `sin` / `exp` / osztás; a Nitrous Space csillapítása negatív erősítést ad.
- 6. Késleltető HQ mód: a 8 pontos Lagrange 56 osztást végez mintánként és csatornánként. Szúrópróba: igaz, beágyazott ciklus osztással (`core/engine-own/fx/fx_dsp.h:65-80`).
- 7. Bode frekvenciatoló: mintánként 64 egész modulo és 4 trigonometria; a Hilbert-szűrő 800 Hz alatt gyenge; a `softLimit` visszacsatolás nélkül is vág ±1-nél.
- 8. Kompresszor többsávos módban 100% alatti mixszel: a nedves jel átmegy a keresztváltón, a száraz nem, fáziskioltás a keresztezési frekvenciákon; a limiter mintánként lineárisan pásztáz.
- 9. Hangszám-függő erősítés csak a vezérlési rács elején frissül: közepes és magas szinten egy akkord akár 31 mintáig csillapítatlanul szól (`core/engine-own/own_engine.cpp:1461-1470,1578-1655`).
- 10. Szinte minden effekt 32 mintánként lépteti a paramétereit (lépcsőzaj); a „low" szinten a chorus 4, a phaser 8 mintáig tartja az értéket.
- 11. Flanger: a visszacsatolás bemenete ±1-re vágva, DC-szűrő nélkül; fölösleges 512 KB puffer. Szúrópróba: csak a paraméter korlátozását találtam (`fx_effects.h:537`), a jel vágását nem tudtam megerősíteni.
- 12. Hyper / Dimension: 11 puffer ugyanazzal a jellel, mintánként akár 23 `sin` / `cos`; a Dimension 0% mixnél is fut.
- 13. CLAP shell: hang nélkül azonnal alvást jelez, levágja a zengető és késleltető lecsengését; mono kimenetnél a bal csatornát felülírja a jobb. Szúrópróba: mindkettő igaz (`shell-clap/synthyboy_clap.cpp:353,392`). JUCE shell: `setLatencySamples()` a hangszálon (`shell-juce/PluginProcessor.cpp:494`).
- 14. Torzító: `pow`, osztás és `exp` mintánként; a `reset()` nem nullázza a DC-állapotot; a szűrő Q-ja nem a specifikáció szerinti.
- 15. EQ: osztás mintánként sávonként; mind a 4 sáv együtthatói 32 mintánként újraszámolódnak akkor is, ha nem változnak; torzulás a Nyquist közelében.
- 16. Effektlánc: bekapcsoláskor 0,73 ms-os felúsztatás, kikapcsoláskor nincs leúsztatás (kattanás, levágott lecsengés); sorrendváltásnál nincs átúsztatás; beégetett `44100`; a 255 mintánál hosszabb késleltetés csendben csonkul.
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
