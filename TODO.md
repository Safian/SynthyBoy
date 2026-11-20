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
- Key 61: a 160 presetes mérés (teljes és fél ütés) fut; az XL-en ugyanez még nincs meg.
- Mi volt az XL-en a „2/3" olvasat: a koordinátor szerint mentett, szerkesztett példány (−17,3 dB a gyári −13,6 helyett). Kiadási megjegyzés kell: régi projektben a presetet újra be kell tölteni.
- A telepítő szkriptek eddig nem vitték át a preseteket és a tartalmat az eszközre (javítva a koordinátor szerint): ellenőrizni, hogy a javítás commitban van és teszt védi.
- Preset-ellenőrző kapu (`synthyboy_preset_sanity`): átállítani a saját motorra a küszöbök lazítása nélkül (owner döntése: egy motor).
- A plafon közeli presetek belső erősítése (legalább 5 dB tartalék a hangerőgombon minden preseten) és a szaturáló presetek velocity → drive útvonala: az állapotuk nincs visszaigazolva.
- Kimeneti fokozat erősítése (`agy/g108-output-stage-hardening`: fordítási idejű zárolásmentesség-ellenőrzés, blokkonként egyszer olvasott cél): review és beolvasztás, utána teljesítménymérés a Key 61 profilon.
- A g107 javításai után nem futott második teljes kapukör.

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
- `work/gate-ebed844f` helyi ág és az `origin/feature/mpc-overlay` távoli ág: kell-e még (a helyi `feature/mpc-overlay` már törölve, a `develop` tartalmazza).
- `CLAUDE.md` link az `AGENTS.md`-re a repó gyökerében, a `template-mpc/` és a `shell-mpc/ui/` alatt (globális szabály: egyetlen forrás az `AGENTS.md`).
- A régi `.hw-local/TODO-MPC.md` (2026-11-14) még élő tételeit ide átvezetni, utána a régi fájlt törölni.
