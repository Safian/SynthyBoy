---
name: mpc-screen-photo
description: SynthyBoy képernyőfotó készítése vagy frissítése az aktuális Akai MPC hardverről. Használd fotó- és screenshotkéréskor, illetve amikor egy felület módosítása után frissíted a képernyőképét.
---

# Aktuális MPC-képernyőfotó

1. A célgépen nyisd meg a kért nézetet. Javítás utáni fotónál ellenőrizd, hogy a javított csomag fut. Várd meg a nézetváltást, és a készülék teljes kijelzőjét rögzítsd a natív vezérlőkkel és a látható overlayjel együtt.
2. Nézd meg a felvételt. A kért nézetet és annak tényleges állapotát kell mutatnia, az esetleges megjelenítési hibával együtt. Sikertelen rögzítéskor őrizd meg a jelenlegi JPG-t.
3. A képernyő nevét a `scripts/screen-catalog.json` fájlból vedd. A projekt gyökeréből a `scripts.screen_images` modul `save_screen(image, screen_path(name))` hívásával írd felül a `docs/screens/<name>.jpg` fájlt. Ez az aktuális fotó egyetlen mentési helye. A rögzítés köztes képkockáit memóriában kezeld. A `_old.jpg` rögzített történeti referencia.
4. Frissítsd az érintett katalógustétel forrásmegjelölését a tényleges készülék és azonosított build alapján, majd futtasd a `python3 scripts/update_native_katalogus.py` parancsot. A válaszban közvetlenül az elmentett JPG-t mutasd.
