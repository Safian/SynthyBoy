# Komponensek

Ez a szabály a teljes `components/` könyvtárra és minden alkönyvtárára érvényes.

- Minden komponens képeinek egyetlen tárolási helye a komponens saját `img/` mappája. A LEVEL esetében ez a `components/native/level/img/`. Ez vonatkozik az előnézetekre és a vezérlő által használt képekre, köztük a filmstripekre is.
- A komponens fejlesztésekor ellenőrizd, hogy az ott lévő képek az aktuális megvalósítást mutatják. Ha a megjelenés változott, ugyanabban a módosításban frissítsd az érintett képeket a meglévő útvonalukon. A régi változat a Git-előzményekből érhető el.
- A komponens képgenerátora (`components/native/<név>/<név>.py`) közvetlenül a komponens saját `img/` mappájába írja a végleges képet. A dokumentáció és a katalógus erre hivatkozzon. A csomagépítéshez vagy összehasonlításhoz létrehozott példány csak ideiglenes kimenet.

## Komponensenkénti tesztelés

Önálló komponens módosításakor az adott komponens izolációs próbáját és az érintett viselkedés célzott tesztjét futtasd. A repó gyökeréből a LEVEL önálló ellenőrzése: `python3 scripts/tests/test_native_component_isolation.py level`. Másik komponensnél a saját mappanevét add meg. Változatlan commiton használd a már lefutott teszt eredményét. A képernyőbekötést és a `skin/TUI.json` szerkesztését akkor ellenőrizd együtt, amikor ezek is változnak (a skint közvetlenül szerkesztjük, nincs generálás).

## Közös natív stílus és terjesztés (owner, 2026-11-24)

- A natív színek egyetlen forrása `components/colors.json`, a méreteké és helyi elrendezéseké `components/sizes.json`. A natív gyártófüggvények, a képgenerátorok és a kézzel karbantartott katalógus-HTML a kis `components/style.py` betöltőn keresztül ugyanazokat a névvel ellátott adatokat olvassák. Ne adj hozzá másolt hex/RGB/RGBA színt, beégetett képpontméretet vagy fallback-palettát. Verziók, paraméterazonosítók, mintadarabszámok és normalizált arányok nem stílusadatok.
- Egy natív komponens hordozható csomagja: `components/native/<név>/<név>.py`, a saját `img/` és szükség szerint `catalog.json`, valamint mellettük, a fenti közös `components/` helyen az EGY `style.py`, `colors.json`, `sizes.json`. A natív komponensek használhatják egymás publikus API-ját; az összetett komponens a meglévő alapkomponenseket használja, ne másolja azok megvalósítását. Hordozható terjesztéskor a használt natív komponensfüggőségeket és azok képeit is csomagold. A komponensenként külön másolt/generált tokenfájl nem kanonikus forrás.
- A katalógus HTML-oldalai (`docs/ui-components/kepes-katalogus.html`, `native-katalogus.html`) kézzel karbantartottak: közvetlenül szerkeszd őket, nincs generátor. Képeket és változatokat a saját komponens metaadatában válassz, méretfeliratot közös tokentől vagy képmérettől származtass.
- UI-átalakítás közben (overlay-vezérlők kivezetése, oldalak natív cellákra váltása) a tulajdonos döntése szerint nincs teszt- és buildfuttatás; az integrációs kör fordítása az ellenőrzés.
