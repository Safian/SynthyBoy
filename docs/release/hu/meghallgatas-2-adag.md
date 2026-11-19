# SynthyBoy meghallgatási ív – 2. adag (24 új preset)

Ez a dokumentum a SynthyBoy szintetizátor gyári hangmintacsomagjára épülő második adag 24 új presetjének meghallgatási és ellenőrzési íve az MPC XL hangszeren. A kipróbálás során minden egyes hangszínnél jelöld be a megfelelő négyzetet, szükség esetén leírva a javítási észrevételeket. Az új presetek a hangszeren a Sounds böngésző > Instruments > SynthyBoy alatt, vagy a plugin PRESET mezőjében találhatók.

## Granuláris oszcillátor (Granular oscillator)

### Chord Cloud Pad (Pad)
- **Mi ez:** Négyrészes akkordmenetből metszett lebegő szemcsefelhő, ahol egy lassú LFO önállóan vándoroltatja az akkordokat a hangmintában, a voicectl pedig minden egyes leütött hangnak külön panorámát, hangolást és szűrőnyitást biztosít.
- **Mit játssz:** Lassú C2 és C4 közötti akkordokat, legalább tíz másodpercig kitartva; játssz le egy akkordot hangonként külön leütve is, hogy megfigyeld az egyes hangok eltérő sztereó elhelyezkedését.
- **Makrók:** Section (akkordsorozat kezdőpozíciójának eltolása), Thin Out (szemcsék sűrűségének ritkítása), Short Grains (szemcsehosszúság rövidítése), Scatter (pozíció és térbeli szórás), Bright (szűrő vágási frekvenciája), Space (zengető tér mérete), Drift (akkordvándorlás LFO sebessége).
- **Pad nyomás / mod wheel:** Pad nyomás: világosabbá teszi a hangszínt és fokozza a térbeli szórást. Mod wheel: hosszabbá teszi a szemcséket és hangmagasság-szórást hoz be.
- **Mire figyelj:** Az alapértelmezett szemcsesűrűségnél a hangfelhő padként vagy inkább rajzásként hat-e; a hatlépéses hangonkénti vezérlés sokszínűségként vagy kásásodásként érvényesül-e.
- [ ] jó   [ ] javítandó: ______

### Vowel Scrub Choir (Lead)
- **Mi ez:** A mod wheel segítségével kézzel pásztázható énekkari szólóhangszín, amelyben a granuláris oszcillátor egy öt másodperces énekmintát olvas, felette a kerékkel együtt mozgó formáns hullámtábla-réteggel és lemezzengetővel.
- **Mit játssz:** C3 és C5 közötti egyes hangokat vagy kéthangos intervallumokat; a mod wheel 0 állásában „A”, 127 állásában „U” hangzót énekel, közte „E”, „I”, „O” magánhangzókkal, és a kerék megállításakor kitartja az adott hangzót.
- **Makrók:** Vowel (magánhangzó pásztázási pozíciója), Cloud (szemcsék időbeli szórása), Short Grains (szemcsehosszúság csökkentése), Table Layer (formáns hullámtábla szintje), Space (lemezzengető konvolúciós tere), Bright (szűrő vágási frekvenciája).
- **Pad nyomás / mod wheel:** Pad nyomás: hangmagasság-szórást ad a szemcsékhez. Mod wheel: magánhangzó-pásztázóként működik: 0-nál „A”, 127-nél „U”, közte „E”, „I”, „O”, megállítva megtartja az adott hangzót.
- **Mire figyelj:** A szemcsefelhő képes-e tiszta magánhangzót énekelni saját önálló hangmagasság nélkül, mivel a mintavételezett hang formánsai a leütött billentyűvel együtt tolódnak el; a sima hangzás kellően tiszta-e.
- [ ] jó   [ ] javítandó: ______

### Frozen Pluck Drone (Pad)
- **Mi ez:** Időben befagyasztott pengetett húros drónhangszín, ahol a lejátszófej a pengetés legelső pillanatán parkol, a száraz jel mögött pedig egy csak effektezett lemezzengetős konvolúciós kísérőbusz fut.
- **Mit játssz:** C2 és C4 közötti akkordokat hosszan kitartva, így a pengetett húr sosem cseng le.
- **Makrók:** Freeze Point (befagyasztott pillanat kiválasztása), Cloud (szemcsék időbeli szórása), Short Grains (szemcsék hosszának rövidítése), Plate Bus (lemezzengető busz szintje), Pitch Scatter (szemcsék magasságának véletlenszerűsítése), Bright (szűrő vágási pontja).
- **Pad nyomás / mod wheel:** Pad nyomás: növeli a szemcsék mennyiségét és sűrűségét. Mod wheel: kissé elmozdítja a lejátszófejet a befagyasztott pontról (érdesebb hangzás) és hangosítja a zengető buszt.
- **Mire figyelj:** A befagyasztott pillanat füllel nem hallatszik-e kattogássorozatként; a szemcsék közötti fázisviszony határozza meg a hangzás simaságát.
- [ ] jó   [ ] javítandó: ______

### Gong Grain Swarm (FX)
- **Mi ez:** Hatvan szemcsére szilánkolt fémgong-rajzás, ahol minden egyes szemcse önálló hangmagassággal, panorámával és szinttel rendelkezik (egyesek visszafelé játszva), külön buszra küldött visszhangfelhővel.
- **Mit játssz:** C2 és C4 közötti egyes hangokat legalább öt másodpercig kitartva, miközben a lejátszófej valós időben olvassa és ismétli a gong mintáját.
- **Makrók:** Sparse (szemcsesűrűség jelentős ritkítása), Pitch Chaos (hangmagasság kaotikus szórása), Scatter (szemcsék elhelyezkedése és iránya), Short Grains (szemcsék hosszának csökkentése), Echo Cloud (visszhangfelhő busz szintje), Dark (aluláteresztő tompítás és sötétítés).
- **Pad nyomás / mod wheel:** Pad nyomás: hangmagasság-szórást hoz létre a szemcsék között. Mod wheel: növeli a szemcsesűrűséget és hangosítja a 2-es busz visszhangfelhőjét.
- **Mire figyelj:** A szemcserajzás fémes textúraként vagy pusztán zajként hat-e; a hosszú zengetés nem mossa-e el túlságosan a részleteket.
- [ ] jó   [ ] javítandó: ______

### Stutter Grain Kit (Pluck)
- **Mi ez:** Hoszt tempóra ütemezett dobminta-akadozás (stutter), ahol a leütött billentyű kiválasztja a kétütemes break adott pozícióját, a tizenhatod ütemezésű szemcsék pedig gördülő ritmust képeznek samplerszerű transzponálással.
- **Mit játssz:** Bármely billentyűt C2 és C6 között, mintavételező hangszerként kezelve; a leütött billentyű határozza meg, hogy a kétütemes break melyik pontján szólal meg a tizenhatodos szemcseismétlés.
- **Makrók:** Nudge (mintapozíció finom eltolása), Scatter (pozíció és magasság szórása), Snap (szemcseablak karakterének határozottsága), Pitch Rand (véletlenszerű transzponálás mértéke), Dark (hangszín tompítása és sötétítése), Echo (késleltető visszhang aránya).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: pozíció- és hangmagasság-szórást hoz létre (glitch hatás).
- **Mire figyelj:** Tempóilleszkedés: a szemcseütem követi a hoszt tempóját, de maga a forrásként szolgáló break fix 120 BPM tempójú, így más tempónál a minta szétcsúszhat a zenei rácstól; az alsó szub-szinusz réteg jelenléte megfelelő-e.
- [ ] jó   [ ] javítandó: ______

### Rising Grain Riser (FX)
- **Mi ez:** Hat másodperces emelkedő effektus (riser), amelyben a granuláris oszcillátor valós időben olvassa az emelkedő textúrát, miközben a szűrő fokozatosan felfelé kúszik, a 2-es busz pedig zengetett visszhangot szór utána.
- **Mit játssz:** Egyetlen billentyűt lefogva hat másodpercig (C2 az alaphang, más billentyűk transzponálják és megváltoztatják a lefutási időt), majd a tetőponton felengedve a lecsengéshez.
- **Makrók:** Slow (olvasási sebesség lelassítása), Sparse (szemcsék sűrűségének ritkítása), Scatter (szemcsék pozíciójának szórása), Throw (téreffektek buszának szintje), Edge (szűrőrezonancia csúcsának élessége), Short Grains (szemcsék hosszának rövidítése).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: hangosítja a 2-es buszt (késleltető és zengető) és sűríti a szemcséket.
- **Mire figyelj:** A mintában lévő saját emelkedés és az arra ráülő szűrőburkoló nem ütközik-e, nem vékonyodik-e el túlságosan a hangzás a csúcsponton; a hangerő a felépülés végső szakaszára lett méretezve.
- [ ] jó   [ ] javítandó: ______

## Többmintás, billentyűzetre kiosztott hangszínek (Multisample, key-mapped)

### Nylon Zone Pluck (Pluck)
- **Mi ez:** Hat billentyűzeti zónára kiosztott akusztikus nylongitár pengetés, ahol a voicectl egy nyolclépéses ismétlődő mintában minden egyes leütött hangnak külön panorámát, hangolást és vágási frekvenciát ad, az élő kézi játék természetességét modellezve.
- **Mit játssz:** C1 és C6 közötti egyes hangokat és arpeggiókat; játszd le többször egymás után ugyanazt a billentyűt, hogy megfigyeld, miként kap mind a nyolc leütés más-más finom eltolást.
- **Makrók:** Bright (szűrő vágási frekvenciája), Saw Pluck (fűrészfog kísérőréteg szintje), Double (uniszónó húrmegkettőzés mértéke), Sustain (lecsengési időtartam hossza), Echo (késleltető effektus szintje), Room (szobazengető tér mérete).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: megkettőzi a húrokat (két szétcsúszó uniszónó hang) és kiemeli a visszhangot.
- **Mire figyelj:** A szomszédos zónahatárok közötti átmeneteknél hallható-e ugrás a hangszínben; a hangminta alatt futó szintetizált fűrészfog pengetés réteg szükséges-e.
- [ ] jó   [ ] javítandó: ______

### Marimba Layer Keys (Keys)
- **Mi ez:** Öt billentyűzeti zónára és két billentésdinamikai rétegre felosztott marimba, ahol a lágy billentés sötét, kerek rudakat szólaltat meg, az erős leütés pedig a faverő koppanásával ellátott fényes fahengereket, kis szoba konvolúcióval kísérve.
- **Mit játssz:** C2 és C6 közötti dallamokat és akkordokat; üsd le ugyanazt a hangot gyengén és erősen a dinamikai mintaváltás megfigyeléséhez.
- **Makrók:** Zone Shift (zónák kiosztásának eltolása), Room (kisszoba konvolúciós zengetése), Octave Sine (felső oktáv szinuszréteg), Mellow (hangszín tompítása és lágyítása), Ring (lecsengési idő hosszabbítása), Chorus (kórus effektus mélysége).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: tremolóeffektust hoz be és növeli az oktáv szinusz arányát.
- **Mire figyelj:** A dinamikaváltási határpontnál a váltás lépcsőzetes, nincs átúszás a rétegek között; a mod wheel zónaeltolási hatása a billentyűzet szélein jelentős, középen elenyésző.
- [ ] jó   [ ] javítandó: ______

### Glass Zone Bells (Keys)
- **Mi ez:** Négy zónára kiosztott üvegharang-hangszín a D hirajoshi skálára zárolva, így a billentyűkön nem lehet melléütni, lemezzengetős konvolúciós térrel és finom alsó szub-szinusszal kísérve.
- **Mit játssz:** C2 és C6 közötti egyes hangokat és lassú akkordokat bármely billentyűn, mivel minden hang automatikusan a legközelebbi D hirajoshi skálahangra (D, E, F, A, A#) igazodik.
- **Makrók:** Bright (aluláteresztő szűrő felső határa), Plate (lemezzengető konvolúció szintje), Bell Layer (harangfelhangos szintetizált réteg), Echo (késleltető visszhang aránya), Ring (lecsengési időtartam hossza), Sub (szub oszcillátor szintje).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: eltolja a zónakiosztást és hangosítja a lemezzengetőt.
- **Mire figyelj:** Kívánatos-e a billentyűzet skálára kényszerítése (a Conform mód miatt a kromatikus skála fekete billentyűi nem szólalnak meg önállóan); a négy üvegminta-zóna a billentyűzet szélein akár két oktávos nyújtást is kap.
- [ ] jó   [ ] javítandó: ______

## Hangminta oszcillátor (Sample oscillator)

### Dusty Rhodes Keys (Keys)
- **Mi ez:** Elektromos zongorahangszín frekvenciamodulált hullámtáblából, gitárhangfal-konvolúcióval és a szűrőt megkerülő analóg bakelitsercegés hangmintával minden leütött hang alatt.
- **Mit játssz:** C2 és C5 közötti akkordokat és dallamokat 50–127 leütési dinamikával: a keményebb billentés határozottabb fémes harapást ad a tónusnak.
- **Makrók:** Tine Bark (billentési fémhang harapása), Dust (bakelitsercegés hangerejének szintje), Chorus (kórus sztereó kiterjedése), Dry Amp (közvetlen és hangfalkonvolúció aránya), Sustain (hang lecsengési ideje), Tone (magashangok fényének szabályozása).
- **Pad nyomás / mod wheel:** Pad nyomás: nincs (a leírás szerint a hang lecsengése miatt nincs használatban). Mod wheel: beúsztatja a suitcase tremolót és kiszélesíti a kórust.
- **Mire figyelj:** Sok billentyű egyidejű lefogásakor a bakelitsercegés nem válik-e túl tolakodóvá (minden hanghoz külön sercegésréteg indul); a hangfalkonvolúció aránya megfelelő-e; az FM tábla a legfelső fekvésben hogyan szól füllel.
- [ ] jó   [ ] javítandó: ______

### Glass Ping Pluck (Pluck)
- **Mi ez:** Megkocogtatott borospohár hangmintájára épülő pengetős hangszer teljes billentyűzetre hangolva, vékony szintetizált harangtáblával és hosszú lemezzengetős konvolúciós lecsengéssel.
- **Mit játssz:** C3 és C6 közötti egyes hangokat és lassú futamokat 40–127 leütési dinamikával; a hangminta C5 magasságra van hangolva, alatta lassul és sötétedik, felette rövidebbé válik.
- **Makrók:** Dark (magas tartomány tompítása), Plate (lemezzengető térhatásának mértéke), Shimmer (uniszónó lebegés intenzitása), Echo (késleltető visszhang szintje), Body (harangtábla szintetizált teste), Ring (lecsengési idő hosszabbítása).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: több lemezzengetőt ad hozzá és kiszélesíti az uniszónó párt.
- **Mire figyelj:** A billentyűzet egésze egységes üveghangszerként szólal-e meg, vagy túlságosan érződik a hangminta elnyújtása (a rezonanciák a billentyűvel együtt tolódnak); a lemezzengető nem mossa-e el a gyors futamokat.
- [ ] jó   [ ] javítandó: ______

### Breakbeat Slice Keys (Keys)
- **Mi ez:** Harminckét szeletre vágott kétütemes dobtörés, ahol minden egyes billentyű C2-től felfelé egy-egy tizenhatodot szólaltat meg, párhuzamosan futó torzított és komprimált busszal, valamint kis szoba konvolúcióval.
- **Mit játssz:** C2 (MIDI 36) és G4 (MIDI 67) közötti harminckét billentyűt, a dobminta tizenhatod szeleteit sorban egymás után megszólaltatva; a hangmagasság nem követi a billentyűt.
- **Makrók:** Smash (torzítás és kompresszió mértéke), Room (kisszoba konvolúciós tere), Shift (szeletkiosztás billentyűzeti eltolása), Dark (aluláteresztő szűrés és sötétítés), Snap (szeletlecsengés burkolójának ideje), Sub Layer (alsó szub-szinusz réteg).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: a leírás nem tér ki rá.
- **Mire figyelj:** Az 1-es busz párhuzamos torzítása és kompressziója hasznos-e vagy túl agresszív a Smash makró magas állásánál; a dobminta fix 120 BPM tempójú, más tempónál a szeletek elcsúsznak a zenei rácstól; G4 felett minden billentyű az utolsó szeletet játssza.
- [ ] jó   [ ] javítandó: ______

### Bowed MPE Strings (Pad)
- **Mi ez:** Akusztikus vonóshangmintát végtelenítve ismétlő vonós hangszer alatta futó fűrészfog réteggel, MPE kontrollerhez kialakított csúsztatási és nyomásérzékeny modulációkkal.
- **Mit játssz:** C2 és C5 közötti hosszú akkordokat vagy dallamvonalakat; a billentyű felengedésekor a vonó felemelésének hangja hallható, ahogy a hangminta túlfut a ciklus végén.
- **Makrók:** Brightness (szűrő alapértelmezett nyitása), Rosin (vonógyanta karcos zajszintje), Vibrato Rate (vibrató LFO sebessége), Bow Speed (felfutás és lecsengés ideje), Hall (teremzengető tér mérete), Saw Layer (szintetizált fűrészfog réteg).
- **Pad nyomás / mod wheel:** Pad nyomás: beúsztatja a gyantazajt (rosin) és a vibratót (Full Level beállítást kapcsold KI). Mod wheel: hagyományos billentyűzeten a slide funkciót látja el: kinyitja a szűrőt.
- **Mire figyelj:** A kitartott hang hurokpontja füllel valóban teljesen sima-e, vagy hallható kétévenkénti enyhe duzzadás; hagyományos billentyűzetről használva nem okoz-e nemkívánatos hatást a megnövelt hajlítási tartomány.
- [ ] jó   [ ] javítandó: ______

### Reverse Gong Swell (FX)
- **Mi ez:** Visszafelé lejátszott fémgong felépülési effekt, ahol a lecsengés négy másodperces felfutássá alakul és a leütési tranziensben végződik, a billentyűvel arányos lefutási sebességgel.
- **Mit játssz:** C1 és C4 közötti egyes billentyűket lenyomva tartva a becsapódásig (a C2 alaphang négy másodpercig tart, a C1 nyolc, a C3 kettő másodperc alatt fut le), majd az ütés után felengedve a lemezzengetős lecsengéshez.
- **Makrók:** Slow Down (visszafelé lejátszás lelassítása), Shimmer (uniszónó lebegés hangolása), Air (felfutó zaj szintje), Space (lemezzengető tér mérete), Bright (szűrő nyitásának mértéke).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: lelassítja a lefutást és tompítja a hangszínt.
- **Mire figyelj:** A felfutás végén a becsapódás hirtelen vágásként vagy zenei csúcspontként hat-e; C3 felett a lefutási idő nem túl rövid-e a felépülés érzetéhez.
- [ ] jó   [ ] javítandó: ______

### Breath Flute Lead (Lead)
- **Mi ez:** Egyhangú, legato fuvola szólóhangszín szinusz-fűrészfog oszcillátorral, beúszó vibratóval és minden külön leütött hang elejére levegőfuvallatot illesztő hangminta oszcillátorral.
- **Mit játssz:** C4 és C6 közötti egyhangos legato dallamokat enyhe glissandóval; minden nem legato módon játszott különálló hang levegőfuvallattal indul, míg az összekötött legato játékban a levegőzaj elmarad.
- **Makrók:** Breath (fúvási levegőzaj szintje), Chiff (induló fuvallat ereje), Vibrato Rate (vibrató LFO sebessége), Bright (szűrő vágási pontja), Overblow (átfújásos felső felhangok), Space (késleltető és zengető tere).
- **Pad nyomás / mod wheel:** Pad nyomás: levegőzajt ad a hanghoz és nyitja a hangszínt. Mod wheel: növeli az átfújt felső felhangokat és mélyíti a vibratót.
- **Mire figyelj:** Az indító fuvallat fix frekvenciájú zajsávból áll, így a magasabb hangoknál nem hallatszik-e túl mélynek; a vibrató kb. 800 ms-os beúszási ideje zeneileg megfelelő-e.
- [ ] jó   [ ] javítandó: ______

## Spektrális oszcillátor (Spectral oscillator)

### Spectral Vowel Freeze (Pad)
- **Mi ez:** Spektrálisan újraszintetizált énekkari padhangszín, amelyben egy lassú Perlin LFO és a kerék folyamatosan pásztázza az énekmintát a szemcsékre jellemző lebegés nélkül, hozzáadott spektrális felhangokkal kísérve.
- **Mit játssz:** C2 és C4 közötti lassú akkordokat, legalább tíz másodpercig kitartva, hogy a magánhangzók önálló átalakulása érvényesüljön.
- **Makrók:** Vowel (pásztázási pozíció a hangmintában), Harmonics (hozzáadott spektrális felhangok), Shift (spektrális frekvenciaeltolás mértéke), Hi Cut (magas frekvenciák tompítása), Space (zengetett térhatás mértéke), Saw Layer (fűrészfog kísérőréteg szintje).
- **Pad nyomás / mod wheel:** Pad nyomás: növeli a hozzáadott felhangokat és nyitja a szűrőt. Mod wheel: pásztázza a magánhangzót és frekvenciaeltolást alkalmaz.
- **Mire figyelj:** A spektrális oszcillátor a szintetizátor legnagyobb számításigényű típusa, ezért az MPC processzorterhelését és a polifóniát érdemes elsőként megfigyelni; a frekvenciaeltolás inharmonikussá teszi a spektrumot.
- [ ] jó   [ ] javítandó: ______

### Spectral Bowed Glass (Lead)
- **Mi ez:** Félig vonós, félig üvegharmonika jellegű szólóhangszín, amelyben a spektrális oszcillátor a vonóshang kitartott ciklusát szintetizálja újra egy fokozatosan záródó spektrális fésűsszűrőn keresztül.
- **Mit játssz:** C3 és C5 közötti egyhangos, összekötött legato dallamokat enyhe glissandóval.
- **Makrók:** Comb (spektrális fésűsszűrő mélysége), Glass (üveghangszer táblaréteg szintje), Glass Shape (hullámtábla alakjának módosítása), Bright (szűrő vágási frekvenciája), Vibrato Rate (vibrató LFO sebessége), Space (késleltető és zengető mértéke).
- **Pad nyomás / mod wheel:** Pad nyomás: szűkíti a spektrális fésűt a vonós hangon (üvegszerűbbé teszi). Mod wheel: szűkíti a spektrális fésűt (üretesebb, üveghangszerűbb hangzás).
- **Mire figyelj:** A spektrális fésű valóban üvegszerűen vagy inkább phaser effektként hangzik-e; a spektrális oszcillátor processzorigénye miként alakul a hardveren.
- [ ] jó   [ ] javítandó: ______

### Spectral Gong Pad (Pad)
- **Mi ez:** Spektrálisan újraszintetizált és vonóval meghúzott gonglecsengés oda-vissza pásztázott inharmonikus részhangokkal, ahol minden uniszónó hang más pontról olvassa a fém mintáját.
- **Mit játssz:** C2 és C4 közötti akkordokat hosszan kitartva, nagy zengetett térben elmerülve.
- **Makrók:** Scan Speed (oda-vissza pásztázási sebesség), Shift (frekvenciaeltolás mértékének állítása), Subharmonics (alsó szubharmonikusok hozzáadása), Bright (szűrő vágási frekvenciája), Space (teremzengető térhatásának mértéke), Bell Layer (harangfelhangos szintetizált réteg).
- **Pad nyomás / mod wheel:** Pad nyomás: inharmonikus frekvenciaeltolást alkalmaz. Mod wheel: a leírás nem tér ki rá.
- **Mire figyelj:** Az újraszintetizált inharmonikus részhangok miatt a hangzás valóban zenei padként funkcionál-e, vagy inkább filmzenés drónhatást kelt (háromhangos akkord már disszonáns klaszterré válhat); a hang felfutási ideje hosszabb.
- [ ] jó   [ ] javítandó: ______

### Spectral Chord Morph (FX)
- **Mi ez:** Négy akkordból álló harmóniamenet folyamatos spektrális morfiumként újraszintetizálva, ahol egyetlen billentyű lefogása végigvezeti a C-dúr7, a-moll7, F-dúr7 és G6 akkordokat.
- **Mit játssz:** Egyetlen billentyűt tarts lenyomva nyolc másodpercig vagy tovább, miközben a hangminta valós időben végigfut az akkordokon; más billentyűk transzponálják a teljes harmóniamenetet.
- **Makrók:** Slow (spektrális lejátszás lelassítása), Shift Warp (spektrális formánsok eltolása), Freq Shift (hordozó frekvenciaeltolás mértéke), Hi Cut (magas frekvenciák tompítása), Space (zengető és kórus tere), Bright (szűrő vágási pontja).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: spektrális formánseltolást és frekvenciaeltolást aktivál (különös harmóniák).
- **Mire figyelj:** A hangminta végén a visszaugrás hallható-e zökkenésként vagy szintcsökkenésként; a mod wheel felső állásában a hangzás drasztikusan és szokatlanul alakul át.
- [ ] jó   [ ] javítandó: ______

## A hangmintacsomag hullámtáblái (Wavetables of the pack)

### Reese Dual Phase Bass (Bass)
- **Mi ez:** Két egymással szemközti, folyamatosan távolodó és közeledő fűrészfog hullámtáblájára épülő Reese basszus, öt uniszónó hanggal, alsó fűrészfoggal, csöves torzítással és gitárhangfal-konvolúcióval.
- **Mit játssz:** Hosszú hangokat C1 és C3 között (MIDI 24–48), vagy kvint-akkordokat; a keményebb billentés világosabb és harapósabb torzítást eredményez.
- **Makrók:** Phase (hullámtábla fázistávolságának eltolása), Cutoff (szűrő vágási frekvenciája), Growl (csöves torzítás meghajtása), Dry Cab (hangfalkonvolúció száraz/nedves aránya), Octave Saw (alsó oktáv fűrészfog aránya), Movement Rate (fázismozgás LFO sebessége).
- **Pad nyomás / mod wheel:** Pad nyomás: emeli a vágási frekvenciát. Mod wheel: növeli a vágási frekvenciát és a torzítás meghajtását.
- **Mire figyelj:** A gitárhangfal-konvolúció nem vékonyítja-e el túlságosan a basszus mélyközép tartományát; a Key 61-en a konvolúció ki van iktatva, emiatt érezhetően halkabb a hang és hiányzik a láda karaktere.
- [ ] jó   [ ] javítandó: ______

### Vowel Table Lead (Lead)
- **Mi ez:** Öt magánhangzó hullámtáblájából építkező szólóhangszín, ahol a kerék az A, E, I, O, U magánhangzókat pásztázza, a pad nyomása nyitja a hangot és beúsztatja a vibratót, a három uniszónó hang pedig kis kórust alkot.
- **Mit játssz:** C3 és C5 közötti egyhangos legato vonalakat glissandóval; a mod wheel pásztázza a magánhangzókat, a billentés pedig az indító tranzienst élénkíti.
- **Makrók:** Vowel (hullámtábla magánhangzó-kockájának pozíciója), Dark (magas frekvenciák tompítása), Choir (uniszónó énekkari szélesség), Octave Down (alsó oktáv réteg szintje), Vibrato Rate (vibrató LFO sebessége), Space (késleltető és zengető tere).
- **Pad nyomás / mod wheel:** Pad nyomás: kinyitja a hangzást és beúsztatja a vibratót. Mod wheel: végigpásztázza az öt magánhangzót (A, E, I, O, U) a hullámtábla-kockákon.
- **Mire figyelj:** A hullámtábla formánsai fix frekvenciájúak, így a magasabb regiszterekben a formánsok eltolódnak és a hangzás inkább fényes szintetizátorként, nem tiszta magánhangzóként szól (a legtermészetesebb tartomány: A2..A4).
- [ ] jó   [ ] javítandó: ______

### Drawbar Organ Keys (Keys)
- **Mi ez:** Tizenhat regisztrációs beállítást tartalmazó hullámtáblára épülő elektromechanikus orgonahangszín billentyűkattanással, második felhangos perkusszióval, forgó hangszóró szimulációval és párhuzamosan túlhajtott busszal.
- **Mit játssz:** C2 és C5 közötti akkordokat; a Registration makró végiglépteti a 16 klasszikus regisztrációt (jazz, gospel, telt, sípoló karakterek).
- **Makrók:** Registration (tizenhat orgonaregisztráció közötti váltás), Percussion (második felhangos lecsengő perkusszió), Overdrive (túlhajtott busz szintje), Click (billentyűkattanás zajának ereje), Rotary Speed (forgó hangszóró effekt sebessége), Bright (szűrő vágási frekvenciája).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: beúsztatja a forgó hangszóró vibratóját, és több kórust és phasert ad hozzá.
- **Mire figyelj:** A 16 lábas alaphang oktávval feljebb transzponálása miatt C2 alatt a legalsó oktáv regisztrációja szokatlanul hathat; a Leslie hangszórót kórus és phaser kombinációja modellezi hangerő-ingadozás nélkül.
- [ ] jó   [ ] javítandó: ______

### Throat Singer Drone (Pad)
- **Mi ez:** Torokéneklést modellező meditatív drónhangszín, amelyben egy lassú LFO keskeny felhangrezonanciát vándoroltat a felhangsor 4. és 20. részhangja között a D fríg domináns skálához igazítva.
- **Mit játssz:** Egy vagy két hangot C3 és C4 között hosszan kitartva (egy oktávval mélyebben szólalnak meg), kötött legato játékkal és hosszú glissandóval; a leütött hangok a D fríg domináns skálára igazodnak.
- **Makrók:** Overtone (kiemelt felhang rezonanciapontja), Walk Speed (felhangvándorlás LFO sebessége), Drone (mély szub-drón szintje), Bright (aluláteresztő szűrő felső határa), Space (zengetett tér mérete), Choir (uniszónó kórushatás mélysége).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: felgyorsítja a felhangok közötti vándorlást.
- **Mire figyelj:** A felhangrezonancia dallamként vagy csupán szűrősöprésként hallatszik-e; a hang felfutási ideje hosszabb, a teljes hangerőt kitartva éri el.
- [ ] jó   [ ] javítandó: ______

### Ladder FM Bass (Bass)
- **Mi ez:** Modulációs aránylépcsőből táplálkozó FM basszus, ahol a billentés ereje a leütés indításakor magasabb aránylépcsőre dobja a hullámtáblát, amely azután visszatér az alaphanghoz, szub-szinusszal megtámogatva.
- **Mit játssz:** C1 és C3 közötti egyes hangokat (MIDI 24–48), röviden billentve vagy kitartva; a leütési dinamika határozza meg, milyen magas arányra ugrik fel a hang indításkor az FM létrán.
- **Makrók:** Ratio (FM modulációs aránylépcső kiválasztása), Snap Length (induló arányburkoló lecsengése), Drive (torzító meghajtásának mértéke), Sub (szub-szinusz oszcillátor szintje), Cutoff (aluláteresztő szűrő nyitása), Decay (hang lecsengési időtartama).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: emeli az FM aránylépcsőt és a torzítás meghajtását.
- **Mire figyelj:** Az aránylépcsők közötti váltások valóban diszkrét lépésekként hallatszanak-e vagy csak folyamatos fényesedésként; mélyebb hangoknál a szűrő eltakarhatja a magasabb arányokat (a Cutoff makróval kinyitható).
- [ ] jó   [ ] javítandó: ______

## Általános kérdések

- **Hangerő a korábbi hangszínekhez képest:** A fenntartott kitartásnál (sustain) behangolt új 24 preset hangereje hogyan illeszkedik az 1. adag 24 új presetéhez és a korábbi 112 gyári presethez képest?
- **Torzítás teljes akkordoknál a kimeneti fokozaton (historic kérdés a korábbi +18 dB-es kimeneti fokozathoz):** A korábbi fix +18 dB-es analóg kimeneti fokozaton a négyhangos vagy sokhangos akkordok maximális billentésnél okoztak-e nemkívánatos digitális vagy analóg torzítást limiter nélkül? (Megjegyzés: a jelenlegi kimeneti fokozat -2,5 dB-es utóerősítést és bekapcsolt limitert használ -0,5 dBFS plafonnal.)
- **Monó kompatibilitás:** A granuláris szemcsefelhők, a spektrális újraszintézisek és a sztereó mintarétegek (mint a bakelitzaj vagy a gitárhangfal) monó összegzéskor nem szenvednek-e fáziskioltást vagy üregessé válást?
- **Key 61 eltérések:** A Key 61 profilon a konvolúciós effektek (Convolve) és zengetők kiiktatása miatt az érintett hangszínek (különösen a Reese Dual Phase Bass, Dusty Rhodes Keys, Marimba Layer Keys és Frozen Pluck Drone) nem veszítenek-e túl sokat a térélményükből vagy a hangerejükből?
- **A gyári mintacsomag hiánya esetén:** Amennyiben a mintacsomag fájljai nem érhetők el a rendszerben, az alatta megszólaló szintetizált hullámtáblás tartalékréteg önmagában elfogadható és zenei hangzást nyújt-e?
