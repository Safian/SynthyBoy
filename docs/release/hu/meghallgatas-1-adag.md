# SynthyBoy meghallgatási ív – 1. adag (24 új preset)

Ez a dokumentum a SynthyBoy szintetizátor első adagjában elkészült 24 új preset meghallgatási és ellenőrzési íve az MPC XL hangszeren. A kipróbálás során minden egyes hangszínnél jelöld be a megfelelő négyzetet, szükség esetén leírva a javítási észrevételeket. Az új presetek a hangszeren a Sounds böngésző > Instruments > SynthyBoy alatt, vagy a plugin PRESET mezőjében találhatók.

## Basszusok (Bass)

### Subterranean Duo (Bass)
- **Mi ez:** Az MPC XL-re tervezett basszushangszín, amely alul tiszta marad, középen pedig agresszíven morog a szűrőt megkerülő szinusz szub-basszus és az oktávhatárok közé hajtogatott fűrészfog réteg révén.
- **Mit játssz:** C1 és B2 közötti egyes hangokat (MIDI 24–47), röviden billentve vagy hosszan kitartva; a 60–127 leütési dinamika tartományban a keményebb billentés fényesebb és harapósabb hangzást ad.
- **Makrók:** Bite (szűrő vágási frekvenciájának nyitása), Growl (meghajtás és lebegtetés növelése), Punch (szűrőlepattanási tranzienst mélysége), Sub Weight (szub-basszus szintjének emelése), Squeeze (kompresszoros sűrítés mértéke).
- **Pad nyomás / mod wheel:** Pad nyomás: kitartott hangnál a padra nehezedve enyhén emeli a vágási frekvenciát. Mod wheel: felfelé tolva növeli a torzítást (drive) és a rezonanciát.
- **Mire figyelj:** A szub és a fűrészfog réteg aránya (a fűrészfog jóval halkabb a szubnál C2-nél); hallható-e ugrásként a key fold a határoknál (a morajló réteg oktávugrása C2 / B3 széleinél); a French LP szűrő rezonanciája füllel még nem lett kipróbálva.
- [ ] jó   [ ] javítandó: ______

### Bend Formant Growl (Bass)
- **Mi ez:** Dubstep stílusú beszélő basszushangszín mérsékelt erőforrás-igénnyel, amelyben Bend warp oszcillátor, formánsszűrő, diódatelítés és EQ biztosítja a vokális morajlást.
- **Mit játssz:** C1 és G2 közötti tartományban staccato nyolcadokat, a mod wheel egyidejű mozgatásával; a leütési dinamika a magánhangzó formánsát és a torzítás mértékét vezérli.
- **Makrók:** Mouth (magánhangzó formánsának beállítása), Growl Depth (morajlás modulációs mélysége), Wobble Rate (wobble LFO sebessége), Dirt (diódatelítés és torzítás mértéke), Sub (szub-basszus szintjének szabályozása).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: mélyíti a Bend warp modulációját és a morajlás mélységét.
- **Mire figyelj:** A Formant szűrőmodell magánhangzó-pozícióit válaszdiagramok alapján állították be, nem füllel lettek behangolva; a mod wheel legalsó állásában hallható morajlás mélysége nem túl sok vagy túl kevés-e.
- [ ] jó   [ ] javítandó: ______

### Triple Fold Reese (Bass)
- **Mi ez:** Mindhárom oszcillátort használó Reese basszus lebegtetett fűrészfog párral, alsó négyszöghullámmal, aluláteresztő szűrővel, lassú phaserrel és a leütés elején felmorduló szinuszos hullámhajtogatóval.
- **Mit játssz:** Hosszú hangokat C1 és C2 között, vagy kvint-akkordokat.
- **Makrók:** Fold (hullámhajtogatás és szűrő nyitása), Phase Drift (oszcillátorok lebegtetésének mozgatása), Open (szűrő vágási frekvenciája), Width (sztereó szélesség állítása), Drive (torzító meghajtásának mértéke).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: emeli a szűrő vágási frekvenciáját és a hullámhajtogatás mértékét.
- **Mire figyelj:** A hullámhajtogatás egyenáramú összetevője és felső felhangjai a 2–5 kHz közötti tartományban kellemesek-e maximális Fold állásnál; a torzítás meghajtása globális effektként mindig a legutóbb leütött hang burkolóját követi, így akkordoknál a korábbi hangok hajtogatása az utolsó hanghoz igazodik.
- [ ] jó   [ ] javítandó: ______

### Pitch Drop 808 (Bass)
- **Mi ez:** Egyhangú 808-as lábdob basszus gyorsan lezuhanó szinuszos hangmagassággal, induló impulzuskattanással, kisméretű hangszórókon is átszóló csöves torzítással és kompresszorral.
- **Mit játssz:** Egyes hangokat C1 és G1 között (MIDI 24–31), hosszan kitartva a hosszú lecsengéshez; az erősebb leütési dinamika nagyobb hangmagasság-esést és fényesebb induló koppanást eredményez.
- **Makrók:** Drop (hangmagasság-esés mértékének állítása), Tail (lecsengési idő hosszúsága), Knock (induló impulzuskattanás ereje), Grit (csöves torzítás mértéke), Boom (mélytartomány teltsége és súlya).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: a leírás nem tér ki rá.
- **Mire figyelj:** A tíz félhangos zuhanás lábdobként vagy inkább „zip” effekthangként hat-e; az induláskor hallható kattanás szándékos.
- [ ] jó   [ ] javítandó: ______

## Szólók (Leads)

### Sync Cry Lead (Lead)
- **Mi ez:** Éneklő hard-sync szólóhangszín, amelyben a szinkronmoduláció leütési dinamikát, pad nyomást és burkológörbét követ, a sztereó szélességet pedig egy kvinttel lejjebb hangolt lebegő fűrészfog réteg adja.
- **Mit játssz:** C3 és C5 közötti legato dallamokat, 90–127 leütési dinamikával; a hang megszólalása után erősebben rányomva a padra a szinkron kinyílik és a vágási frekvencia követi azt.
- **Makrók:** Bright (hangszín világossága és szűrőnyitás), Cry (hard-sync moduláció mélysége), Thick (szélesítő fűrészfog réteg szintje), Space (visszhang és térérzet mértéke), Fifth (alsó kvint réteg szintje).
- **Pad nyomás / mod wheel:** Pad nyomás: a hang indítása után rányomva megnyitja a hard-syncet és emeli a vágási frekvenciát. Mod wheel: fokozatosan beúszó vibratót hoz be.
- **Mire figyelj:** A hard-sync moduláció tartománya teljes pad nyomásnál nem válik-e túlzottan vadul kontrollálhatatlanná; a tempószinkronos nyolcad ping-pong delay alapértelmezetten be van kapcsolva alacsony szinten.
- [ ] jó   [ ] javítandó: ______

### Vowel Whistle Lead (Lead)
- **Mi ez:** Egyhangú, emberi hangkarakterű legato szólóhangszín formáns modulációval, formánsszűrővel és Multi SVF kiemelő szűrőcsúccsal.
- **Mit játssz:** C4 és C6 közötti legato dallamvonalakat enyhe glissandóval; kitartott hang mellett lassan mozgasd a mod wheelt 0-tól 127-ig a magánhangzók végigpásztázásához.
- **Makrók:** Vowel (magánhangzó formánsának kiválasztása), Reson (formánsszűrő rezonanciájának mértéke), Shape (formáns skálázás és alak), Air (kiemelő szűrőcsúcs frekvenciája), Vibrato Rate (vibrató LFO sebessége), Space (térhatás és zengetés szintje).
- **Pad nyomás / mod wheel:** Pad nyomás: kinyitja a formáns „szájat”. Mod wheel: lassan mozgatva folyamatosan végigpásztázza a magánhangzókat a hang kitartása közben.
- **Mire figyelj:** A magánhangzók a Formant szűrőmodell koordinátasíkja alapján lettek kiválasztva, nincsenek füllel konkrét magánhangzókhoz hangolva.
- [ ] jó   [ ] javítandó: ______

### Triple Fifth Lead (Lead)
- **Mi ez:** Rétegzett zenekari összhangzást idéző szólóhangszín három oszcillátorral (kvinttel rétegzett fűrészfog, felső oktáv impulzushullám, duodecimával magasabb háromszöghullám), Multi SVF aluláteresztő szűrővel és Hyper/Dimension térhatással.
- **Mit játssz:** C3 és C5 közötti egyes hangokat, ne akkordokat (a rétegzés maga képezi az akkordot); a leütési dinamika nyitja az impulzushullámot és a szűrőt.
- **Makrók:** Bright (szűrő vágási frekvenciája), Stack (felső két oszcillátor aránya), Spread (sztereó térbeli széthúzás), Space (téreffektusok és visszhang mértéke), Edge (meghajtás és rezonancia éle).
- **Pad nyomás / mod wheel:** Pad nyomás: beúsztatja a duodecimával feljebb hangolt háromszög oszcillátort. Mod wheel: a leírás nem tér ki rá.
- **Mire figyelj:** A kvint-összeállítású oszcillátor-rétegzés a Key 61-en (kevesebb uniszónó hanggal és kikapcsolt Hyper/Dimension effekttel) még nem lett füllel összehasonlítva az XL-lel.
- [ ] jó   [ ] javítandó: ______

### Bell FM Ripple Lead (Lead)
- **Mi ez:** Szinuszvivőt egy második oszcillátorral fázismoduláló harangszerű szólóhangszín, amelynél a modulációs index a dinamikát, a burkológörbét és a pad nyomást követi, így a lágy leütések üvegszerűek, a kemények pedig fémcsengésűek.
- **Mit játssz:** C4 és C6 közötti lassú dallamokat, 40-től 127-ig terjedő leütési dinamikával (a dinamikai különbség adja a lényeget).
- **Makrók:** Index (FM modulációs index mélysége), Ring (csengés kitartási szintje), Shimmer (harangfelhangok csillogó rétege), Space (térérzet és késleltető effektus), Ratio (modulátor hangmagasság-arányának eltolása).
- **Pad nyomás / mod wheel:** Pad nyomás: a hang csengése közben rányomva növeli az FM moduláció mélységét. Mod wheel: a leírás nem tér ki rá.
- **Mire figyelj:** A 19 félhangos alap FM arány szándékoltan inharmonikus haranghangot ad, a Ratio makró forgatásakor azonban útközben harmonikussá, majd ismét inharmonikussá alakul.
- [ ] jó   [ ] javítandó: ______

## Padek (Pads)

### Stretched Glass Choir (Pad)
- **Mi ez:** Nyújtott részhangú lebegő pad, amelyben az oszcillátor felhangnyújtását egy véletlenszerű Perlin LFO pásztázza, felette lebegő felhangsor-réteggel, kórussal és lemezzengetővel.
- **Mit játssz:** C3 és C5 közötti akkordokat, legalább négy másodpercig kitartva.
- **Makrók:** Bright (szűrő vágási frekvenciájának nyitása), Stretch (harmonikus felhangok nyújtásának mértéke), Shimmer (felső felhangsor csillogó szintje), Space (lemezzengető tér mérete), Slow (felfutás és lecsengés lassítása).
- **Pad nyomás / mod wheel:** Pad nyomás: kinyitja a szűrőt és fokozza a felhangnyújtás mértékét. Mod wheel: a leírás nem tér ki rá.
- **Mire figyelj:** A Harmonic Stretch magas értékeinél a jelleg kórusból harangszerűvé válik; a véletlenszerű vándorlás tartománya nem túl tág-e.
- [ ] jó   [ ] javítandó: ______

### Mid Side Halo (Pad)
- **Mi ez:** Három oszcillátoros és szubos meleg padhangszín, amely mid/side szétválasztással a szilárd monó középrészt szárazon tartja és komprimálja, míg az oldalsó sávokra kórust és phasert helyez a széles glóriaérzetért.
- **Mit játssz:** Széles fekvésű C2–C4 akkordokat lassan leütve; érdemes összevetni monóban és sztereóban.
- **Makrók:** Brightness (szűrő vágási frekvenciája), Halo (szélesség és oldalsó effektek), Colour (szűrő hangszínének színezete), Space (zengető térhatásának mértéke), Weight (szub és mélytartomány súlya).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: a leírás nem tér ki rá.
- **Mire figyelj:** Fejhallgatón, a Halo makró maximális állásánál nem túl feltűnő vagy tolakodó-e az oldalsó sáv phaser effektusa.
- [ ] jó   [ ] javítandó: ______

### Parallel Dusk Pad (Pad)
- **Mi ez:** Lassú, sötét tónusú padhangszín két párhuzamos szűrővel a sztereó tér két oldalán (bal oldalon analóg aluláteresztő, jobb oldalon Flanges fésűsszűrő), ellentétesen modulált vágási frekvenciákkal és fázismemóriával.
- **Mit játssz:** Lassú C2–C4 akkordokat, a legteljesebb élmény érdekében fejhallgatóval hallgatva.
- **Makrók:** Left Open (bal oldali aluláteresztő nyitása), Comb Colour (jobb oldali fésűsszűrő színe), Breath Rate (lélegző LFO sebessége), Space (zengető térérzetének mértéke), Upper Layer (felső oszcillátor-réteg aránya).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: a leírás nem tér ki rá.
- **Mire figyelj:** Ennek a presetnek a leggyengébb a monó kompatibilitása a teljes adagból; felépítéséből adódóan kifejezetten sztereó hallgatásra tervezett hangszín.
- [ ] jó   [ ] javítandó: ______

### Brass Swell Ensemble (Pad)
- **Mi ez:** Az 1980-as éveket idéző rézfúvós szekciópad három lebegtetett fűrészfoggal, szubbal, indító fúvászajjal, valamint a leütés erejével és kitartásával fokozatosan kinyíló szűrőduzzadással.
- **Mit játssz:** C2 és C4 közötti akkordokat; az erőteljesebb billentés gyorsabbá és világosabbá teszi a hangszín felfutását.
- **Makrók:** Bright (szűrő maximális nyitása), Swell Time (felfutás és duzzadás ideje), Breath (induló fúvási zaj szintje), Hall (teremzengető tér mérete), Section (fúvósszekció lebegtetésének mértéke).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: a leírás nem tér ki rá.
- **Mire figyelj:** A 650 ms-os duzzadási idő füllel valóban rézfúvós szekcióra vagy inkább vonósokra emlékeztet-e.
- [ ] jó   [ ] javítandó: ______

## Billentyűk és pengetősök (Keys / plucks)

### Tine Glass Keys (Keys)
- **Mi ez:** Elektromos zongorahangszín két egymást fázismoduláló szinuszból és kemény leütésre felcsendülő harangrúd-részhangból, auto-pan tremolóval, phaserrel és flangerrel.
- **Mit játssz:** C2 és C6 közötti akkordokat és egyes hangokat, 40–127 leütési dinamikával: a lágy leütés kerek, a kemény harap és fémcsengéssel szólal meg.
- **Makrók:** Bell (csilingelő fémhang szintje), Bark (kemény leütés FM harapása), Sustain (hang kitartása és lecsengése), Phase (phaser moduláció mélysége), Mellow (magas frekvenciák tompítása).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: beúsztatja az auto-pan tremolót.
- **Mire figyelj:** A maximális leütési dinamikánál jelentkező FM harapás nem túl rideg-e; a legfelső csengettyűs felhang nem igényel-e halkítást.
- [ ] jó   [ ] javítandó: ______

### Karplus Harp (Pluck)
- **Mi ez:** Karplus-Strong elvű pengetett húros hangszín, amelyben egy rövid zajlöket hozza rezgésbe a hangmagasságra kalibrált fésűsszűrőt, második csillapító szűrővel és a pengetési tranzienst megfogó limiterrel.
- **Mit játssz:** Arpeggiókat C3 és C6 között, 50–127 közötti dinamikával.
- **Makrók:** Brightness (csillapító szűrő vágási pontja), Ring Tone (felső felhangok fésűs eltolása), Pick Softness (pengetési zajimpulzus lágysága), Space (térhatás és zengetés), Body (hangszer mélyrezonanciájának teste).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: tompítja a húrok lecsengését (palm mute tenyértompítás).
- **Mire figyelj:** A fésűsszűrő csengése füllel valóban húrszerű vagy inkább fuvolaszerű; az induló zaj fehérzajból áll; a C6 feletti magas hangok lecsengése nagyon rövid.
- [ ] jó   [ ] javítandó: ______

### Ring Mod Gamelan (Pluck)
- **Mi ez:** Fémes indonéz gamelán hangszert idéző pengetős hangszín inharmonikus felhangokkal, háromszöghullámú oszcillátorral, gyűrűs modulációval és pontozott nyolcad ping-pong késleltetővel.
- **Mit játssz:** C3 és C6 közötti pentaton mintákat; az erősebb leütési dinamika fémesebb hangkaraktert ad.
- **Makrók:** Metal (gyűrűs moduláció aránya), Inharmonic (vivőfrekvencia arányának eltolása), Ring Time (lecsengési idő hossza), Space (térérzet és késleltető), Shimmer (harangfelhangos csillogás szintje).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: a leírás nem tér ki rá.
- **Mire figyelj:** A 2,76-szoros vivőarány elméletben marimbaszerű fémrudat mintáz, füllel ellenőrizendő; az Inharmonic makró ezt a viszonyt erősen módosítja.
- [ ] jó   [ ] javítandó: ______

## Szekvenciák és ritmus (Sequence / rhythm)

### Pentatonic Step Pluck (Pluck)
- **Mi ez:** Egyetlen billentyű lefogásával önmagát játszó szekvencia, amelyben egy rajzolt 16 lépéses LFO lépteti az oszcillátort moll pentaton skálában egy ütemen át, tizenhatodos pengetésekkel és limiterrel.
- **Mit játssz:** Egyszerre csak egyetlen billentyűt tarts le C3 és C4 között a hoszt zenei tempójában (ha nincs hoszt, 120 BPM az alapértelmezett).
- **Makrók:** Bright (szűrő vágási frekvenciája), Hold (lépések kitartási hossza), Squelch (szűrőrezonancia harapásának mértéke), Echo (késleltető effektus aránya), Fat (uniszónó vastagság és kórus).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: kitartja a hangokat a pengetések között (legato átkötés).
- **Mire figyelj:** A beprogramozott pentaton dallam megfelel-e zeneileg; akkordok lefogásakor minden leütött hang külön elindítja a dallamot, ami kásásodást okoz, ezért egyhangos játékra való.
- [ ] jó   [ ] javítandó: ______

### Tempo Gate Chords (Keys)
- **Mi ez:** Akkord billentyűhangszín, amelyet egy rajzolt 16 lépéses ritmikai kapu szaggat a tempóval szinkronban egy ütemen át, negyedes szűrőpumpálással kísérve.
- **Mit játssz:** C3 és C5 közötti lefogott akkordokat.
- **Makrók:** Smooth (szaggatás elsimítása paddá), Pump (negyedes szűrőpumpálás mértéke), Bright (szűrő vágási frekvenciája), Echo (késleltető visszhang szintje), Fat (sztereó vastagság és uniszónó).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: növeli a rezonanciát.
- **Mire figyelj:** A pumpálás nem hangerő-, hanem szűrővágási pumpálásként működik (a motorbeli szorzási korlát miatt), így kevésbé lehet nyilvánvaló, mint egy hagyományos sidechain.
- [ ] jó   [ ] javítandó: ______

## Textúrák és atmoszférák (Textures / atmospheres)

### Shepard Ascent (Pad)
- **Mi ez:** Végtelenül felfelé emelkedő drónhangszín két oktávtávolságú réteggel, Shepard Tone hullámtábla-morfiummal, lassan emelkedő felüláteresztő szűrővel és Lorenz kaotikus LFO modulációval.
- **Mit játssz:** Egy vagy két hangot C2 és C3 között, 30 másodpercig kitartva.
- **Makrók:** Bright (aluláteresztő szűrő felső határa), Climb Speed (emelkedési LFO sebessége), Thin (felüláteresztő szűrő vékonyítása), Space (térméret és zengetés), Drift (kaotikus eltolódás mértéke).
- **Pad nyomás / mod wheel:** Pad nyomás: felgyorsítja az emelkedés ütemét. Mod wheel: a leírás nem tér ki rá.
- **Mire figyelj:** AZ EMELKEDÉS FÜLLEL MÉG NINCS BIZONYÍTVA: a harmonikus kiemelés ciklikusan ismétlődik, a cél a zökkenőmentes végtelen emelkedés érzete, amit feltétlenül füllel kell ellenőrizni.
- [ ] jó   [ ] javítandó: ______

### Lorenz Garden (Pad)
- **Mi ez:** Lassú, folyamatosan átalakuló felhangágy, ahol négy különféle véletlenszerű LFO (Lorenz, Rössler, Perlin, szinusz interpoláció) folyamatosan vándorol a hullámtábla kockáin, a panorámán és a szűrőkön.
- **Mit játssz:** C3 és C5 közötti akkordokat, legalább 10 másodpercig kitartva; a Sparkle makró a harangfelhangokat egy oktávval magasabbra emeli.
- **Makrók:** Bright (aluláteresztő szűrő nyitása), Wander Speed (véletlenszerű LFO-k sebessége), Sparkle (harangfelhangok felső oktávja), Echo (pontozott sztereó visszhang), Depth (Smear szűrő mélysége).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: a leírás nem tér ki rá.
- **Mire figyelj:** A Lorenz LFO nagyon lassú sebessége miatt a moduláció egy rövidebb zenei frázison belül szinte észrevehetetlen lehet.
- [ ] jó   [ ] javítandó: ______

### Breath of Machines (FX)
- **Mi ez:** Lélegző gépházat idéző atmoszférahangszín, amelyben formánsszűrőn átküldött zaj, Lorenz LFO, hangmagasságot adó fésűsszűrő és sample-and-hold lépcsőzetes színezés működik együtt.
- **Mit játssz:** Egy mély hangot C1 és C2 között legalább 10 másodpercig kitartva, vagy egy kvint-hangközt.
- **Makrók:** Vowel (formánsszűrő magánhangzójának eltolása), Breath Rate (gépi lélegzés LFO sebessége), Hum (fésűsszűrős zúgás szintje), Room (szobaméret és térhatás), Stutter (mintavételező szaggatás sebessége).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: a leírás nem tér ki rá.
- **Mire figyelj:** A rezonáns fésűsszűrőn átküldött zaj vajon kellemes atmoszférát kelt-e, vagy füllel bántóan kellemetlennek hat.
- [ ] jó   [ ] javítandó: ______

## Effektek és one-shotok (FX / one-shots)

### Bode Barber Riser (FX)
- **Mi ez:** Ötmásodperces felépülési effekt (riser) két oktávot emelkedő fűrészfoggal, emelkedő sávszűrős zajjal és önmagába visszacsatolt Bode frekvenciaeltolóval, amely a billentyű felengedéséig a csúcson kitart.
- **Mit játssz:** Egy hangot C2 és C4 között, hat másodpercig lenyomva tartva, majd a drop pillanatában felengedve.
- **Makrók:** Height (hangmagasság-emelkedés végső magassága), Spin (frekvenciaeltoló forgási sebessége), Noise (emelkedő zajsáv szintje), Space (térérzet és késleltetés), Length (emelkedési időtartam hossza).
- **Pad nyomás / mod wheel:** Pad nyomás: a frekvenciaeltoló visszacsatolását (feedback) növeli keményebbé. Mod wheel: a leírás nem tér ki rá.
- **Mire figyelj:** MPC XL-en a Bode visszacsatolása valóban végtelen barber-pole emelkedésként érzékelhető-e; Key 61-en a Bode és a flanger teljesen ki van iktatva, ott a hangszín szárazabb és hangosabb.
- [ ] jó   [ ] javítandó: ______

### Detonation Tail (FX)
- **Mi ez:** Mozis becsapódási effekt (impact) zajrobbanással, zuhanó szinuszos mélypuffanással, 300 Hz-nél elválasztott szaturált mélyekkel és nagy térbe küldött magasakkal.
- **Mit játssz:** Egyetlen leütést C1 vagy C2 billentyűn, hagyva hat másodpercig lecsengeni.
- **Makrók:** Impact (becsapódási zajtranziens ereje), Boom (mély robbanás szintje), Tail (zengetett lecsengés ideje), Crush (mélytartomány torzítása és telítése), Air (magastartomány térbeli lebegése).
- **Pad nyomás / mod wheel:** Pad nyomás: a leírás nem tér ki rá. Mod wheel: a leírás nem tér ki rá.
- **Mire figyelj:** A beépített limiter miatt a leütési dinamika magasabb tartományaiban alig változik a hangerő, inkább a hangszín telítettsége módosul.
- [ ] jó   [ ] javítandó: ______

## Előadói játék, pad nyomás és makrók (Performance, pad pressure + macros)

### Bowed Pressure Strings (Pad)
- **Mi ez:** Ujjnyomással életre kelthető vonóshangszín, ahol az erősebb rányomás nyitja a szűrőt, beúsztatja a gyantás karcolást, a vibratót és a felső réteget, míg felengedve lágy kamarazenekar marad.
- **Mit játssz:** C2 és C5 közötti akkordokat vagy dallamvonalakat kikapcsolt Full Level pad-beállítás mellett; a hang kitartása közben váltogasd az ujjnyomás erejét.
- **Makrók:** Brightness (szűrő alapértelmezett nyitása), Bow Speed (vonásmegszólalás és lecsengés ideje), Rosin (vonógyanta karcoló zajszintje), Vibrato Rate (vibrató LFO sebessége), Hall (teremzengető mérete és tere).
- **Pad nyomás / mod wheel:** Pad nyomás: kinyitja a szűrőt, beúsztatja a vonó karcolását (rosin), aktiválja a vibratót és élénkíti a felső réteget. Mod wheel: a leírás nem tér ki rá.
- **Mire figyelj:** A pad fizikai nyomástartománya a valóságban mennyire finoman kontrollálható; a réteg az ujjnyomás teljes elmozdulási tartományára lett skálázva.
- [ ] jó   [ ] javítandó: ______

### Wah Pressure Clav (Keys)
- **Mi ez:** Ujjhegy alá helyezett wah-pedálos klavinet, ahol a pad nyomása sáváteresztő szűrőt pásztáz és élezi a rezonanciát, a billentés pedig a lecsengést és a diódatelítést vezérli.
- **Mit játssz:** Funk kíséreteket C2 és C4 között, rövid akkordokkal; az akkord kitartása közben nyomd meg a padot a wah hatáshoz (Full Level beállítást kapcsold KI).
- **Makrók:** Wah Range (ujjnyomásos wah modulációmélysége), Quack (wah szűrő rezonanciája), Pick (pengetési tranziens lecsengése), Dirt (diódatelítés és torzítás), Phase (fázistoló effektus aránya).
- **Pad nyomás / mod wheel:** Pad nyomás: végigpásztázza a Multi SVF sáváteresztő wah szűrőt és megnöveli a rezonanciát (Full Level OFF szükséges). Mod wheel: a leírás nem tér ki rá.
- **Mire figyelj:** A maximális ujjnyomásnál jelentkező wah-tartomány nem túl széles és nehezen uralható-e; a Wah Range makróval finomítható.
- [ ] jó   [ ] javítandó: ______

## Általános kérdések

- **Hangerő a korábbi hangszínekhez képest:** Hogyan viszonyul az új 24 preset hangereje az eredeti 112 gyári presethez képest; a fenntartott kitartásnál (sustain) behangolt új lassú padek nem hatnak-e halkabbnak, mint a korábbi padek?
- **Torzítás teljes akkordoknál a kimeneti fokozaton:** A hangszer fix +18 dB-es analóg kimeneti fokozatán torzítanak-e a teljes, sokhangos akkordok maximális billentésnél limiter nélkül, vagy szükség van a kimeneti limiter (vagy sávlimiter) bekapcsolására?
- **Monó kompatibilitás:** Monó összegzéskor (például klubhangrendszeren vagy kis hangszórón) nem vesznek-e el fontos zenei részletek, különösen a kifejezetten sztereóra épített rétegeknél (mint a Parallel Dusk Pad vagy a Mid Side Halo)?
- **Key 61 eltérések:** A Key 61 hardverprofilján a kikapcsolt effektek (zengető, flanger, Hyper-Dimension, Bode, Convolve) miatt a hangszínek (különösen a Bode Barber Riser, Triple Fifth Lead, Shepard Ascent és Detonation Tail) nem válnak-e túl szárazzá, tompává vagy aránytalanul hangossá/halkká az XL-hez képest?
