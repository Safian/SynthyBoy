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
- X|Y vezérlő Y tengelye (12 kulcs): a letagadó sorok kivéve, a kulcsok élnek, 16 tengely teszttel igazolva, a hiánylista „No key remains"; nincs még a `develop`-ban. Hátravan 8 tengely tesztje (Warp, Warp 2, Rand Warp, Rand Warp 2, spektrális szűrő cutoff és mix), mert a próbaállásban a warp és a spektrális szűrő ki volt kapcsolva. (fut: `agy/own-xy-claim-2`)
- X|Y „Density" tengely: a specifikáció (`docs/engine-spec/07`, 7a.2) szerint csak szabad ütemű módban hat, a kód (`core/engine-own/osc/lib_osc.cpp`, X|Y blokk) feltétel nélkül ír. Ellenőrizni.
- A paraméter-audit adatai ellentmondanak egymásnak (`docs/ui-design/param-audit-keys.csv`: ALIVE, `param-audit-baseline-own.json`: UNSUPPORTED): az audit alapját újra kell generálni az X|Y kulcsok bekapcsolása után.
- DSP-audit 2. rész: effektek, kimenet, szintkezelés (fut: `agy/own-audit-2-fx`); 3. rész: a motor magja, teljesítmény és memória az ARMv7-en (nem indult).

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

## Tudás és ügynök-eszközök
- Tudásmappa (`docs/dsp-knowledge/`: témánként rövid fájl forrásokkal, döntésekkel és bizonyító teszttel, tisztaszoba-szabály, tesztreceptek) és egy rá mutató vékony skill: az owner döntésére vár.
- A `.hw-local/synth-knowledge/vital/code` (405 fájl) és `sources/` Vital-forráskódot tartalmaz: a saját motor munkáihoz tiltani kell, vagy el kell vinni onnan (owner döntése).
- Automatikus ellenőrzés: bukjon el a kapu, ha a `core/engine-own/` alá Vital-azonosító vagy GPL-fejléc kerül.
- A kutatásban csak emlékezetből felsorolt forrásokat (Bristow-Johnson, Brandt, Dattorro, Bencina, MPE- és SFZ-specifikáció) meg kell nyitni, mielőtt a tudásmappába kerülnek.

## Repó-rend
- `agy/g54-pack-versions-010` (0.1.0 verziószövegek): beolvasztás a kiadásjelölt körben.
- `agy/g84-reduced-set-proposal` (csökkentett preset-készlet): az owner döntésére vár.
- `work/sound-batch-3` (24 új preset): meghallgatásra vár.
