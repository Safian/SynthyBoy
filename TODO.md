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
- Meredek V az LFO-szerkesztőben: a javítás az ov43-ban van, de eszközön senki nem nézte meg. Kész, ha a segédfolyamat képének visszaolvasása mindkét eszközön kitöltött sorokat mutat.
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
- X|Y vezérlő Y tengelye (12 kulcs): a DSP kész, a `core/engine-own/own_support.cpp` 50–53. sora letagadja. (fut: `agy/own-xy-claim`)
- X|Y „Density" tengely: a specifikáció (`docs/engine-spec/07`, 7a.2) szerint csak szabad ütemű módban hat, a kód (`core/engine-own/osc/lib_osc.cpp`, X|Y blokk) feltétel nélkül ír. Ellenőrizni.
- Az X|Y tengelyek közül eddig csak a Level volt tesztelve.
- A paraméter-audit adatai ellentmondanak egymásnak (`docs/ui-design/param-audit-keys.csv`: ALIVE, `param-audit-baseline-own.json`: UNSUPPORTED): az audit alapját újra kell generálni az X|Y kulcsok bekapcsolása után.
- DSP-audit: 1. rész, hangút (fut: `agy/own-audit-1-voice`); 2. rész: effektek, kimenet, szintkezelés; 3. rész: teljesítmény és memória az ARMv7-en. Mindegyik megállapításból külön tétel lesz ide.
- A modulációs mátrix 32 mintánként számol, nem mintánként (`core/engine-own/README.md`, „What is not there yet").
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
