# Filter 60/40 terv – olvasóreview

Csak forrásolvasás, build/teszt nélkül. A terv egy 1280×628-as **plugintartalom-terv** (HTML/CSS demó). Nem MPC-panelfelbontás, nem natív plugin, nem gyári screenshot. Az `assets.*`/lucide/DOM/`aria-*` elemek csak demók.

## Ítélet: nem szűkíthető változatlanul

`ScalarCurveEditor::minSize()` = 1100×500 (`scalar_curve_editor.h:634`). Az oldalsáv fix `kSideW = 400` (`:593`, `:794`). 500 px-en a vászon szélessége kb. 500−24−400 = 76 px, tehát a vászon túl keskeny marad (nem nulla). Az alsó két 4 mezős blokk (`:811-825`) 500 px-en mezőnként kb. 53 px lenne, ami a 56 px-es target alatt van. Ez **tervkorlát**, nem kódhiba. A 3fr/2fr felosztás kb. 751/501 px (a jobb oldal 501 px).

## Korrekciók

1. **`scalar_curve_editor.h:634/593/794`** – az 500 px-es oldalhoz külön kompakt elrendezés kell: vászon, POINT/X/Y/CURVE, GRID X/Y/SNAP/LEGATO, INIT/UNDO/DELETE POINT és TARGETS. A teljes 1100×500 szerkesztő változatlan marad a jobb oldali nagyítás nézetnek (`bodyRect`, 1280 × (628−72) = 556, belefér).
   - A **kompakt nézetből kimaradó** részeket a nagyításnak kell megtartania: USED BY lista (`:604`, `:805-808`), GLIDE chip (`:806`, `:839-846`, csak LEGATO bekapcsolva), tárolt értékek és TARGETS.
   - A terv nagyítása (`.graph{height:380px}`) csak a grafikont nagyítja. Ez nem elég: a nagyítás a teljes szerkesztőt mutassa.
2. **`filter_display.h:947/689-691`** – a bal oldal 751 px: `unit=(735−12−100−150)/10 ≈ 47`, ez a 56 px-es dial alatt van. **Tervkorlát**, a számolás származtatott. Javítás: bal oldalon a ROUTING (150) és a LedToggle (100) szélességét át kell tervezni, vagy a TRACK/OUT blokkot két sorba kell törni.
3. **`filter_display.h:705-711, 925-1015`** – a HTML-ből hiányzik: COMB (csak Comb modellnél), a FORMANT blokk (X/Y/TRANSPOSE/SPREAD; `formantRes` a VAR helyén; Formant modellnél a VAR rejtett; 36% szélességű blokk, `:996`), a DEST/BUS1/BUS2/LEVEL/PAN read-only összesítő és az F1+F2 nézet. Ezek **ne vesszenek el**: a feltételes rejtés marad, nem szürkítés (`:684`). A bal oldalon külön helyet kell adni a Formant-blokknak, vagy a nagyításba kell áttenni.

## Egyéb megállapítások

- **Független váltás**: a kód egyetlen 4 állapotú `UsedSelector view`-t használ (`:692-695`: F1|F2|NOTE|VELO). A két független választó új állapotmodell, ezért **tervkorlát**, nem hiba. Ehhez az `FilterFullScreen` állapotát kell átalakítani.
- **OFF**: a HTML csak DOM-ban tilt (`applyPower`, `:105-110`), a dialokra csak `aria-disabled` kerül. Natív oldalon az érintés/Q-Link/stale input tiltása **nem igazolt** ebből a forrásból, ellenőrizendő. Marad: a bekapcsolás, a modulválasztás, a read-only nagyítás és a független NOTE/VELO.
- **56 px target**: a valós target 56 px (`kTopH`, a dialok 56 px-esek), a terv ezt `min-height:56px`-szel jelzi. Az OFF által érintett vezérlők (MODEL/STYLE/CLEAN/dialok/ENV 1/ROUTING) külön választandók a mindig aktívaktól (power, F1/F2, nagyítás, NOTE/VELO).
- A HTML-selector és a demó **nem production kód**: ne alakítsd át natív gyári képpé.
