# SynthyBoy - Formula Parser Implementation Report

## 1. Kész / Implementált funkciók
- **Grammar & Specifikáció (`core/formula/GRAMMAR.md`)**:
  - A Serum 2 kézikönyv (pp. 304–311) alapján parafrazeált, tiszta specifikáció.
  - Tartalmazza a lexikai szabályokat, az operátor-precedenciát, az összes változót és konstanst (`pi`, `e`, `x`, `w`, `y`, `z`, `q`, `in`, `sel`, `rand`).
  - Tartalmazza az összes 1-argumentumos függvényt (21 db) és variadikus függvényt (4 db: `min`, `max`, `sum`, `avg`).
  - Részletesen jelöli a kézikönyv kétértelműségeit `[S unclear]` címkével:
    1. Operátor-precedencia a `&&` és `||` operátorok között (a kézikönyv megfordított prioritást közöl a muParser és C sztenderdhez képest).
    2. Modulo `%` és ternáris `?:` hiánya a kézikönyv operátortáblázatából (bár a 310. oldalon példában szerepelnek).
    3. `q` FFT mód fázis- és DC-rekonstrukció hiánya a kézikönyvben.
- **Lexer & Rekurzív leereszkedő parser (`core/formula/src/formula.cpp`)**:
  - Null-biztos, lebegőpontos és egész számokat, azonosítókat, operátorokat, ternáris feltételes operátort kezel.
  - Pontos hiba-pozíció jelentés (0-alapú karaktereltolás).
  - Korlátos rekurzió (`kMaxRecursionDepth = 64`), kifejezéshossz-korlát (`kMaxExpressionLength = 4096`).
  - Nincs kivételkezelés (`noexcept`), hibakód/eredménystruktúra (`Error`).
- **Kompilált Bytecode VM & Kiértékelő (`Formula::evaluate`)**:
  - Lapos bytecode (`Instruction` struktúra), konstans tábla.
  - Zéró heap allokáció kiértékeléskor (fix 64-es méretű hívási verem).
  - NaN és végtelen védelem (`safeSin`, `safeLog`, `safeDiv`, `safePow`, stb.), nem véges értékek lebegőpontos 0.0-ra és véges határokra szorítása.
  - Ternáris rövidrezárás (`OpJumpIfFalse`, `OpJump`).
- **Wavetable Frame Renderelő (`renderFrames`)**:
  - Időtartomány (2048 minta / frame, 1..256 frame).
  - Bemeneti hullámforma (`in`) és kiválasztott frame (`sel`) mintavételezés meglévő bufferekből.
  - Változatlan véletlenszám-tábla (`rand`) képzése minden frame-re.
  - `q` FFT mód: beépített additív harmonikus szintetizátor (512 bin szintézise 2048 mintára) és `FFTRenderHook` callback interfész külső IFFT / fázismotor bekötéséhez.
  - Végső kimeneti minták korlátozása `[-1.0f, 1.0f]` tartományba.
- **Publikus Fejléc (`core/include/synthyboy/formula.h`)**:
  - `synthyboy::formula` névtér, `Formula` osztály, `evaluate`, `renderFrames`, `FFTRenderHook`.
- **Projekt integráció (`core/CMakeLists.txt` & `core/formula/CMakeLists.txt`)**:
  - Pontosan EGY `add_subdirectory(formula)` sor hozzáadva a `core/CMakeLists.txt`-hez.
  - Új `core/formula/CMakeLists.txt`, amely a forrásokat hozzáadja a `synthyboy_core`-hoz és regisztrálja a `synthyboy_test_formula` tesztet.
- **Egységtesztek (`core/formula/tests/test_formula.cpp`)**:
  - A meglévő `test_runner.h` stílusában írt 8 átfogó teszteset (összesen 20,756 ellenőrzés):
    1. `formula_arithmetic_and_precedence`: operátorok precedenciája, asszociativitás, %, ternáris rövidrezárás.
    2. `formula_each_function_in_manual`: a kézikönyv mind a 25 függvényének tesztje.
    3. `formula_constants_and_variables`: `pi`, `e`, változók, maszk lekérdezések.
    4. `formula_manual_examples`: saw (`x`), sine (`sin(pi*x)`), `q < 17`, `(1/q)^0.25`, Squarify `((q%2)==1)?in:0`.
    5. `formula_error_positions`: szintaktikai hibák pontos karaktermutatói.
    6. `formula_safety_and_clamping`: 0-val osztás, negatív gyök, exp túlcsordulás, rekurziós limit.
    7. `formula_render_frames`: többframe-es idő- és FFT renderelés, `FFTRenderHook` meghívása.
    8. `formula_fuzz_1000_strings`: 1000 véletlenszerű string fuzzer teszt (összeomlás és végtelen ciklus mentes, 0.00 másodperc alatt lefutva).

## 2. Mi maradt
- Minden kitűzött követelmény maradéktalanul elkészült, nincs elmaradt feladat.

## 3. Érintett fájlok
- `core/CMakeLists.txt` (1 sor hozzáadva: `add_subdirectory(formula)`)
- `core/include/synthyboy/formula.h` (új publikus fejléc)
- `core/formula/CMakeLists.txt` (új csomag CMake fájl)
- `core/formula/GRAMMAR.md` (nyelvtan és specifikáció)
- `core/formula/src/formula.cpp` (lexer, parser, bytecode VM, renderFrames)
- `core/formula/tests/test_formula.cpp` (egységtesztek és fuzzer)

## 4. Minőségi kapuk kimenete szó szerint

### 4.1 Build
```
[ 48%] Built target synthyboy_engine_vital
[ 56%] Built target synthyboy_core
[ 57%] Built target synthyboy_static
[ 65%] Built target synthyboy_test_state
[ 66%] Built target synthyboy_test_engine
[ 66%] Built target synthyboy_test_preset
[ 66%] Built target synthyboy_test_params
[ 67%] Built target synthyboy_test_threads
[ 68%] Built target synthyboy_test_rt
[ 69%] Built target synthyboy_test_vital_engine
[ 75%] Built target synthyboy_paramdump
[ 75%] Built target synthyboy_clap
[ 75%] Built target synthyboy_bench
[ 76%] Built target synthyboy_test_vital_params
[ 77%] Built target synthyboy_test_vital_audio
[ 78%] Built target synthyboy_test_shell_clap
[ 99%] Built target synthyboy_test_vital_dsp
[100%] Built target synthyboy_test_formula
```
`-Wall -Wextra -Wpedantic` tiszta, figyelmeztetés nélkül lefordul.

### 4.2 Formula unit teszt futtatás (`./build/core/formula/synthyboy_test_formula`)
```
[ RUN  ] formula_arithmetic_and_precedence
[  OK  ] formula_arithmetic_and_precedence
[ RUN  ] formula_each_function_in_manual
[  OK  ] formula_each_function_in_manual
[ RUN  ] formula_constants_and_variables
[  OK  ] formula_constants_and_variables
[ RUN  ] formula_manual_examples
[  OK  ] formula_manual_examples
[ RUN  ] formula_error_positions
[  OK  ] formula_error_positions
[ RUN  ] formula_safety_and_clamping
[  OK  ] formula_safety_and_clamping
[ RUN  ] formula_render_frames
[  OK  ] formula_render_frames
[ RUN  ] formula_fuzz_1000_strings
[  OK  ] formula_fuzz_1000_strings
8 test case(s), 20756 check(s), 0 failed case(s), 0.00 s
```

### 4.3 Teljes CTest futtatás (`ctest --test-dir build`)
```
Test project /Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-formula/build
      Start  1: synthyboy_test_formula
 1/13 Test  #1: synthyboy_test_formula ...........   Passed    0.00 sec
      Start  2: synthyboy_test_params
 2/13 Test  #2: synthyboy_test_params ............   Passed    0.49 sec
      Start  3: synthyboy_test_state
 3/13 Test  #3: synthyboy_test_state .............   Passed    2.66 sec
      Start  4: synthyboy_test_preset
 4/13 Test  #4: synthyboy_test_preset ............   Passed    0.18 sec
      Start  5: synthyboy_test_shell_clap
 5/13 Test  #5: synthyboy_test_shell_clap ........   Passed    0.26 sec
      Start  6: synthyboy_test_engine
 6/13 Test  #6: synthyboy_test_engine ............   Passed    3.75 sec
      Start  7: synthyboy_test_rt
 7/13 Test  #7: synthyboy_test_rt ................   Passed    3.67 sec
      Start  8: synthyboy_test_threads
 8/13 Test  #8: synthyboy_test_threads ...........   Passed   13.56 sec
      Start  9: synthyboy_test_vital_engine
 9/13 Test  #9: synthyboy_test_vital_engine ......   Passed    2.05 sec
      Start 10: synthyboy_test_vital_params
10/13 Test #10: synthyboy_test_vital_params ......   Passed    1.53 sec
      Start 11: synthyboy_test_vital_audio
11/13 Test #11: synthyboy_test_vital_audio .......   Passed   24.34 sec
      Start 12: synthyboy_test_vital_dsp
12/13 Test #12: synthyboy_test_vital_dsp .........   Passed    3.34 sec
      Start 13: synthyboy_paramdump
13/13 Test #13: synthyboy_paramdump ..............   Passed    0.17 sec

100% tests passed out of 13

Label Time Summary:
slow    =  24.34 sec*proc (1 test)

Total Test time (real) =  56.01 sec
```

## 5. Élőben ellenőrizendők
- UI oldali integráció (ha majd a wavetable editor szövegdoboza átadja a felhasználói kifejezést a `Formula::compile` és `renderFrames` hívásnak).
- FFT fázis-visszaállítás finomhangolása, ha a jövőben saját IFFT fázisadatbázissal kívánjuk kiegészíteni az `FFTRenderHook`-ot.
