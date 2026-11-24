# Natív MPC-képernyők fejlesztési leltára

A táblázat a 2026. november 24-én ellenőrzött `4a50afed` develop-állapotot rögzíti. A hardveres elfogadás és a még készülő teljes képernyőképek a [TODO.md-ben](../../TODO.md) követhetők.

| Képernyő | Integrált működés és ellenőrzési pont |
|---|---|
| Play és önálló OSC választók | A Play modulfejlécei és SHAPE mezője, az önálló OSC példány- és forrásválasztója, valamint a Warp menü és ROUTE definíciója a `f281537a` változással került developba. A tényleges választás, példányváltás és fizikai Q-Link ellenőrzése MPC-n nyitott. [Play skin](../../scripts/mpc-skin-extras/82_play.py) |
| OSC A/B/C – TUNE | A jóváhagyott 278 × 154 px-es kártyát 32 px-es mezőkkel, az aktív oszcillátor paramétereihez kötve építi fel a skin. A `68f85518` a TUNE kártyának önálló területet ad a hullámforma bal oldalán, az Unison felett. Az önálló OSC és a kibontott Play azonos kártyát használ. A numerikus választás, Q-Link és MORE utáni visszalépés eszközös próbája hátravan. [TUNE bekötés](../../scripts/mpc-skin-extras/98_osc_tune.py) |
| OSC – kompakt fejléc | A 48 px magas natív PAN, LEVEL és ROUTE mezők a kiválasztott oszcillátort vezérlik. A Basic sor alájuk került. A fejléc megérintése a Mix Q-Link csoportot választja. A skin és overlay bekötése a `3d8b7d6f` commitban szerepel. Az MPC-s ellenőrzés nyitott. [OSC routing](../../scripts/mpc-skin-extras/55_osc_routing.py) |
| Macros | A `9d0a1092` változással a nyolc natív kártya mindegyike a LEVEL tárcsát és külön célgombot kapott. A gomb saját host-paramétere a kiválasztott makró modulációs céllistáját nyitja az overlayben. A két bank eszközös próbája és a friss képernyőkompozit még nyitott. [Macros skin](../../scripts/mpc-skin-extras/99_ui_screen_macros.py) · [Overlay-navigáció](../../shell-mpc/overlay/macro_ui.h) |
| Voice | A `992b7a3b` a hat sor nyolc-nyolc bipoláris faderét a megfelelő paraméterhez köti. A host rajzolja a sávokat, az overlay kezeli a lépésszámokat és a megszólaló hang jelzését. [Voice képernyő](../../scripts/native_voice/voice_tab.py) |
| XL Play/Sub standby | A `8bb1feb5` javítás a `99f65a8f` merge-ben került developba. A felismerő az elmosott két nézetjelölőt és a példányjelölőt a képernyősor mért hátterével veti össze. A korábbi hiba XL-en rögzített eseménysora rendelkezésre áll, a javítás egyperces pihenési és ébresztési próbája nyitott. [Kijelzőmérések](../dsp-knowledge/mpc-display.md) |
| LFO | A képernyőmodul, a renderer és az előnézetek a `96725b80` változással kerültek developba. [LFO skin](../../scripts/mpc-skin-extras/99_ui_screen_lfo.py) |
| ENV, Filter, FX, Mixer, Mod, Sub, Noise, OSC A/B/C és Master | A képernyőmodulok a [skinforrásban](../../scripts/mpc-skin-extras) találhatók. A Master belépési pontja a [75_master.py](../../scripts/mpc-skin-extras/75_master.py). |

A Q-Linkek vizuális szabályát a `c1e79cd4`, az aktív bank számozatlan jelölését a `0ecf57b2` rögzíti. A kompakt VOICES léptető a `7630fb18` commitban található.
