# SynthyBoy — Magyar nyelvű felhasználói dokumentáció

Ez a mappa a SynthyBoy szoftverszintetizátor hivatalos magyar nyelvű felhasználói kézikönyveit és útmutatóit tartalmazza az Akai MPC hardverplatformhoz.

A hivatalos angol nyelvű referencia-dokumentáció a szülőkönyvtárban található (`docs/user-guide/`).

---

## A dokumentáció felépítése

1. **[SynthyBoy-MPC-felhasznaloi-kezikonyv.md](SynthyBoy-MPC-felhasznaloi-kezikonyv.md)**:  
   A teljes felhasználói kézikönyv az Akai MPC felülethez igazítva. Részletesen tárgyalja a képernyőt, a kezelőszerveket (érintőképernyő, 16 Q-Link potméter, egér), az összes szerkesztőoldalt (Play, Osc A/B/C, Sub, Noise, Mixer, Filter 1/2, Envelopes, LFO-k és grafikus rajzoló, Mod matrix, FX, Master, Voice control), a wavetable-böngészőt és a saját hullámtáblák betöltését, a modulációs rendszert, valamint a preseteket és a gyári hangkönyvtárat.

2. **[TELEPITES.md](TELEPITES.md)**:  
   Részletes telepítési és indítási útmutató: a csomag elhelyezése a meghajtókon (SD-kártya, USB, belső meghajtó), a külső plugin-támogatás bekapcsolása az MPC Preferences felületén, frissítés, eltávolítás és hibaelhárítás.

3. **[mpce-padok.md](mpce-padok.md)**:  
   Útmutató az MPC második generációs nyomásérzékeny padjaihoz (MPCe, MPC XL) és az expresszív játéktechnikákhoz (polyphonic aftertouch, szűrővezérlés, hullámtábla-pásztázás, modulációs receptek).

4. **Gyári tartalomcsomag és hullámtábla-katalógus**:  
   A 72 gyári hullámtábla teljes leírását, műszaki adatait és pásztázási viselkedését tartalmazó 72 soros angol nyelvű katalógus az eredeti referencia-dokumentumban érhető el: [factory-content.md](../factory-content.md).

---

## Nyelvi és fordítási irányelvek (Wording Conventions)

- **Természetes, hétköznapi magyar megfogalmazás**: A dokumentáció a magyar zenészek által használt természetes kifejezéseket alkalmazza, kerülve az anglicizmusokat és a mesterkélt tükörfordításokat (például „pásztázás” a frame scan helyett, „léptetés” a step helyett, „átfordulás / körbefordulás” a wrap helyett).
- **Képernyőfeliratok és vezérlők megőrzése**: A hardveren és a képernyőn megjelenő gombok, mezők, vezérlők, paraméterek és kategórianevek pontosan angol formájukban szerepelnek (például `WAVE`, `WT POS`, `User`, `Basic`, `Morph`, `Analog`, `Digital`, `Spectral`, `Vocal`, `Metallic`, `Noise`, `Osc A`, `Q-Link`).
- **Harmadik fél termékneveinek mellőzése**: Felhasználói szövegekben tilos harmadik fél kereskedelmi termékneveit közvetlenül megnevezni; ezekre általános leírással hivatkozunk (például „más szintetizátorok”, „más hullámtáblás hangszerek”, „a referencia-kézikönyv”).
