# Effects – mobile UI design

Covers the FX chain routing, bus system, and every effect processor and parameter in Vital (`docs/PARAMETERS.md`, `[V p.27-38]`) and Serum 2 (`[S p.146-182, 350]`). All 9 Vital effects (`Chorus`, `Compressor`, `Delay`, `Distortion`, `EQ`, `FX Filter`, `Flanger`, `Phaser`, `Reverb`) and the 9-slot reorderable chain (`fx.order.1..9`) are implemented in `engine-vital` (120 parameter keys total: 105 in `FX` + 15 in `FX Filter`). Serum 2-exclusive FX modules (`Bode`, `Convolve`, `Hyper/Dimension`, `Utility`, `Splitter L/H`, `Splitter L/M/H`, `Splitter M/S`) and multi-bus send racks (`MAIN`, `BUS 1`, `BUS 2`) are marked `GAP` and fully designed below.

* **Out-of-scope exclusions (`00-BRIEF.md`):** Preset browser (`[S p.327]`): excluded: Kick. Clips (`[S p.219]`): excluded: Kick. Arpeggiator (`[S p.244]`): excluded: Kick.

---

## 1. Function inventory

### 1.1 FX Chain, Racks, Busses & Operations (`[S p.146-160]`, `[V p.27]`)

| Function | Origin (Vital / Serum / both) | Our params (keys) | Engine status (HAVE / GAP) |
|----------|-------------------------------|-------------------|----------------------------|
| **Reorderable 9-Slot Master FX Chain** (top-to-bottom serial signal flow; duplicates resolved by permutation in `vital_bindings.cpp` l.58) | Both `[S p.155-156]`, `[V p.27]` | `fx.order.1` .. `fx.order.9` (`0 Chorus`, `1 Compressor`, `2 Delay`, `3 Distortion`, `4 EQ`, `5 FX Filter`, `6 Flanger`, `7 Phaser`, `8 Reverb`) | **HAVE** (9 keys) |
| **Per-Effect Enable / Bypass Switch** (Serum red bypass button `[S p.157]`; Vital top-left activator `[V p.27]`) | Both `[S p.157]`, `[V p.27]` | `chorus.on`, `compressor.on`, `delay.on`, `distortion.on`, `eq.on`, `fxfilter.on`, `flanger.on`, `phaser.on`, `reverb.on` (`0 Off, 1 On`) | **HAVE** (9 keys) |
| **Bypass All FX on Bus** (`Opt/Alt-click` any bypass button to toggle all FX on/off at once) | Serum `[S p.157, 350]` | UI snapshot toggle of all active `<fx>.on` keys | **HAVE** (UI macro over `<fx>.on`) |
| **Expand / Collapse FX Rack & List View (`Opt/Alt-F`)** | Serum `[S p.152-153, 350]` | UI layout toggle (expands chain strip into full-screen multi-card rack) | **HAVE** (UI state) |
| **Multi-Bus FX Racks (`MAIN`, `BUS 1`, `BUS 2`) & Osc/Filter Bus Sends** (3 parallel FX racks; per-osc and per-filter Send knobs to `BUS 1`/`BUS 2`; Bus routing `Main / Direct / Bus 1 / Bus 2` + Bus/Main/Direct Levels) | Serum `[S p.146-153]` | `osc1..3.destination` & `noise.destination` (`3 Effects only`, `4 Direct out`) route to single FX chain or bypass it; `BUS 1` / `BUS 2` parallel racks are `GAP` | **GAP** (`Effects only` & `Direct out` **HAVE**; `BUS 1`/`BUS 2` sends are **GAP**) |
| **Dynamic Module Instancing (Add, Remove, Duplicate/Copy Module with or without Modulations)** (`+` add any of 13 modules in any count, `Opt-drag` copy without mods, `Shift-Opt-drag` copy with mods, `X` remove) | Serum `[S p.155-157, 160, 350]` | Fixed 1 instance per effect type in `sbvital::SoundEngine`; multiple instances of same FX is `GAP` | **GAP** (single instance per FX **HAVE**; multi-instance is **GAP**) |
| **FX Rack & Module Presets / Bus Clipboard & Lock** (`Load/Save Rack Preset`, `Init`, `Cut/Copy/Paste/Clear FX Bus`, `Lock FX Bus`, `Lock All FX Busses`, per-module `Factory / Save FX Preset / Save as Default`) | Serum `[S p.154, 158, 160]` | UI partial-state JSON preset (`fx.*`, `<fx>.*`) + UI parameter lock | **HAVE** (UI partial-state preset & lock) |
| **Modulating FX Parameters (Paraphonic / Sum Output DSP)** (any modulatable FX knob can be targeted by per-voice or global mod sources) | Both `[S p.159]`, `[V p.27]` | 62 FX parameters + 12 `fxfilter` parameters flagged `mod` in `docs/params.json` | **HAVE** (74 modulatable FX destinations) |
| **Per-Module Output `LEVEL` Trim (dB)** (every Serum 2 FX module ends with a `LEVEL` dB trim) | Serum `[S p.161-182]` | `GAP` (except `compressor.{low,mid,high}.gain` and `eq.{low,mid,high}.gain`) | **GAP** |

---

### 1.2 Individual Effect Modules (9 Vital + 7 Serum-Exclusive = 16 Modules)

| Module & Parameter | Origin | Our params (keys) | Engine status |
|--------------------|--------|-------------------|---------------|
| **1. CHORUS** (4-voice stereo chorus with dual delay taps & post-filter) | Both `[S p.162]`, `[V p.27-28]` | 12 keys (`chorus.*`) | **HAVE** |
| — Enable (`On`), Dry/Wet (`MIX`) | Both `[S p.162]`, `[V p.27-28]` | `chorus.on` (`0/1`), `chorus.mix` (`0..100%`, def `50%`, `mod`) | **HAVE** |
| — LFO Rate (`RATE`), Sync (`BPM` / Free / Dotted / Triplet), Division (`Freeze`..`1/16`) | Both `[S p.162]`, `[V p.28]` | `chorus.rate` (`0.02..8 Hz`, def `0.4 Hz`, `mod`), `chorus.sync` (`0..3`), `chorus.division` (`0..10`, def `1/4`) | **HAVE** |
| — Tap Delays (`DELAY 1`, `DELAY 2`), Modulation Depth (`DEPTH`), Feedback (`FEEDBACK`) | Both `[S p.162]`, `[V p.28]` | `chorus.delay1` (`1..20 ms`, def `2 ms`, `mod`), `chorus.delay2` (`1..20 ms`, def `8 ms`, `mod`), `chorus.depth` (`0..100%`, def `50%`, `mod`), `chorus.feedback` (`-95..95%`, def `40%`, `mod`) | **HAVE** |
| — Voices (`Voices`: `1..4` in param table, `4..16` in Vital display) | Vital `[V p.27]` | `chorus.voices` (`1..4 voices`, step `1`, def `4`) | **HAVE** |
| — Post Filter (`Cutoff` & `Spread` bandpass in Vital; `LPF/HPF` toggle + freq in Serum) & `LEVEL` | Both `[S p.162]`, `[V p.28]` | `chorus.filter_cutoff` (`20..20000 Hz`, def `260 Hz`, `mod`), `chorus.filter_spread` (`0..100%`, def `100%`, `mod`); Serum `LEVEL` is `GAP` | **HAVE** (filter); **GAP** (`LEVEL`) |
| **2. COMPRESSOR** (Single-band & 3-band OTT Upward/Downward Compressor) | Both `[S p.162-164]`, `[V p.28-30]` | 20 keys (`compressor.*`) | **HAVE** |
| — Enable (`On`), Dry/Wet (`MIX`), Band Mode (`Multiband`, `Low band`, `High band`, `Single band`) | Both `[S p.163-164]`, `[V p.29-30]` | `compressor.on` (`0/1`), `compressor.mix` (`0..100%`, def `100%`, `mod`), `compressor.bands` (`0..3`, def `0 Multiband`) | **HAVE** |
| — Attack (`ATTACK`) & Release (`RELEASE`) | Both `[S p.164]`, `[V p.29-30]` | `compressor.attack` (`0..100%`, def `50%`, `mod`), `compressor.release` (`0..100%`, def `50%`, `mod`) | **HAVE** |
| — Per-Band Downward (`upper_threshold`, `upper_ratio`), Upward (`lower_threshold`, `lower_ratio` / Serum `BELOW`), & Makeup Gain (`L / M / H` `gain`) | Both `[S p.163-164]`, `[V p.29-30]` | `compressor.{low,mid,high}.{lower_threshold,upper_threshold,lower_ratio,upper_ratio,gain}` (15 keys; `gain` `-30..30 dB` is `mod`) | **HAVE** (all 15 band keys) |
| — Serum True Peak `Limit` Mode (at max Ratio + `Limiter Latency Comp`), Adjustable Crossovers (`X-LOW`, `X-HIGH`; fixed at 150 Hz / 3 kHz in Vital), Output `LEVEL` | Serum `[S p.163-164]` | `GAP` | **GAP** |
| **3. DELAY** (Stereo / Ping-Pong / Mid Ping-Pong / Tap->Delay with Feedback Filter) | Both `[S p.165-167]`, `[V p.30-31]` | 11 keys (`delay.*`) | **HAVE** |
| — Enable (`On`), Dry/Wet (`MIX`), Feedback (`FEEDBACK`) | Both `[S p.166]`, `[V p.31]` | `delay.on` (`0/1`), `delay.mix` (`0..100%`, def `30%`, `mod`), `delay.feedback` (`-100..100%`, def `40%`, `mod`) | **HAVE** |
| — Delay Style / Mode (`Mono`, `Stereo`, `Ping pong`, `Mid ping pong`; Serum `Normal`, `Ping-Pong`, `Tap->Delay`) | Both `[S p.166]`, `[V p.31]` | `delay.style` (`0 Mono`, `1 Stereo`, `2 Ping pong`, `3 Mid ping pong`, def `1`); Serum `Tap->Delay` & `High Quality` switch are `GAP` | **HAVE** (4 styles); **GAP** (`Tap->Delay`, `HQ`) |
| — Left & Right Delay Times (`time`, `time2`), Sync (`Free`, `Tempo`, `Dotted`, `Triplet`), Divisions (`4 bars`..`1/64`), `LINK` L/R, Serum continuous scalar `Offset` (`1.333 Trip` / `1.5 Dot`) | Both `[S p.166]`, `[V p.30-31]` | `delay.time` (`2..4000 ms`, `mod`), `delay.time2` (`2..4000 ms`, `mod`), `delay.sync` (`0..3`, def `1 Tempo`), `delay.division` (`0..8`, def `1/8`), `delay.division2` (`0..8`, def `1/8`); UI `LINK` locks L=R; continuous scalar offset is `GAP` | **HAVE** (L/R times & divisions); **GAP** (scalar offset) |
| — Delay Filter (`FREQ` / `filter_cutoff`, `Q` / `filter_spread` + XY Filter Pad + double-click FFT overlay) & `LEVEL` | Both `[S p.166-167]`, `[V p.31]` | `delay.filter_cutoff` (`20..20000 Hz`, def `260 Hz`, `mod`), `delay.filter_spread` (`0..100%`, def `100%`, `mod`); `LEVEL` is `GAP` | **HAVE** (filter & XY pad); **GAP** (`LEVEL`) |
| **4. DISTORTION** (Waveshaper / Bitcrush / Downsample with Pre/Post SVF Filter) | Both `[S p.167-169]`, `[V p.31-33]` | 8 keys (`distortion.*`) | **HAVE** |
| — Enable (`On`), Dry/Wet (`MIX`), Drive (`DRIVE`, `-30..30 dB`, drag vertically on wave chart) | Both `[S p.168]`, `[V p.32-33]` | `distortion.on` (`0/1`), `distortion.mix` (`0..100%`, def `100%`, `mod`), `distortion.drive` (`-30..30 dB`, def `0 dB`, `mod`) | **HAVE** |
| — Distortion Type (`Soft clip`, `Hard clip`, `Linear fold`, `Sine fold`, `Bit crush`, `Down sample`; Serum adds `Tube`, `Diode 1/2`, `Zero Square`, `Asym`, `Rectify`, `X-Shaper` & `X-Shaper (Asym)` dual editable graphs `Edit A`/`Edit B`) | Both `[S p.168-169]`, `[V p.33]` | `distortion.type` (`0..5`, def `0 Soft clip`) | **HAVE** (6 types); **GAP** (`X-Shaper` custom A/B graphs & Tube/Diode/Rectify) |
| — Filter Placement (`None / Pre / Post`), Cutoff (`FREQ`), Resonance (`Q`), Blend (`LP..BP..HP`), Serum Filter `Key Track` & `LEVEL` | Both `[S p.168]`, `[V p.33]` | `distortion.filter_order` (`0 None, 1 Pre, 2 Post`), `distortion.filter_cutoff` (`20..20000 Hz`, `mod`), `distortion.filter_resonance` (`0..100%`, `mod`), `distortion.filter_blend` (`0..100%`, `mod`); Filter `Key Track` & `LEVEL` are `GAP` | **HAVE** (Pre/Post SVF filter); **GAP** (`Key Track`, `LEVEL`) |
| **5. EQUALIZER (EQ)** (Vital 3-Band Low/Mid/High Parametric EQ vs Serum 2-Band LF/HF EQ) | Both `[S p.169-170]`, `[V p.33-34]` | 13 keys (`eq.*`) | **HAVE** |
| — Enable (`On`), Low Band (`Shelf / High pass`, `Cutoff`, `Gain`, `Resonance`), Mid Band (`Peak / Notch`, `Cutoff`, `Gain`, `Resonance`), High Band (`Shelf / Low pass`, `Cutoff`, `Gain`, `Resonance`), Serum LF/HF `Peak` mode & `LEVEL` | Both `[S p.169-170]`, `[V p.34]` | `eq.on` (`0/1`), `eq.low.{mode,cutoff,gain,resonance}`, `eq.mid.{mode,cutoff,gain,resonance}`, `eq.high.{mode,cutoff,gain,resonance}` (all 9 cutoff/gain/res keys are `mod`) | **HAVE** (3-band EQ is a superset of Serum's 2-band EQ; `LEVEL` is **GAP**) |
| **6. FX FILTER** (Master Post-Voice Multi-Model Filter in FX Chain) | Both `[S p.170-172]`, `[V p.34]` | 15 keys (`fxfilter.*`) | **HAVE** |
| — Enable (`On`), Model (`Analog`, `Dirty`, `Ladder`, `Digital`, `Diode`, `Formant`, `Comb`, `Phaser`), Style (`A..F`), Cutoff, Resonance, Drive, Blend, Mix, Keytrack, Comb Offset, Formant (`X`, `Y`, `Resonance`, `Spread`, `Transpose`), Serum `Clean Mode` (-24/+24 dB), `PAN` (L/R cutoff offset), `LEVEL`, Display modes (`Freq Response`, `Freq + FFT`, `Phase + FFT`) | Both `[S p.170-172]`, `[V p.13-15, 34]` | `fxfilter.{on,model,style,cutoff,resonance,drive,blend,mix,keytrack,comb_offset}`, `fxfilter.formant.{x,y,resonance,spread,transpose}` (12 modulatable keys) | **HAVE** (15 keys); **GAP** (`Clean Mode`, `PAN` cutoff offset, `LEVEL`) |
| **7. FLANGER** (Stereo Comb-Modulated Flanger) | Both `[S p.172-173]`, `[V p.35-36]` | 9 keys (`flanger.*`) | **HAVE** |
| — Enable (`On`), Dry/Wet (`MIX`), Rate (`RATE`), Sync (`BPM`), Division, Depth (`DEPTH`), Feedback (`FEEDBACK`), Center (`flanger.center`), Stereo Phase Offset (`PHASE`), Serum `LEVEL` | Both `[S p.172-173]`, `[V p.35-36]` | `flanger.{on,mix,feedback,rate,sync,division,depth,center,phase_offset}` (6 modulatable keys) | **HAVE** (9 keys); **GAP** (`LEVEL`) |
| **8. PHASER** (Multi-Stage All-Pass Notch Phaser) | Both `[S p.174-175]`, `[V p.36-37]` | 10 keys (`phaser.*`) | **HAVE** |
| — Enable (`On`), Dry/Wet (`MIX`), Rate (`RATE`), Sync (`BPM`), Division, Depth (`DEPTH` `0..48 st`), Center (`FREQ`), Feedback (`FEEDBACK`), Blend / Poles (`POLES`), Stereo Phase (`PHASE`), Serum `DEPTH 2` & `LEVEL` | Both `[S p.174-175]`, `[V p.36-37]` | `phaser.{on,mix,feedback,rate,sync,division,depth,center,blend,phase_offset}` (7 modulatable keys) | **HAVE** (10 keys); **GAP** (`DEPTH 2`, `LEVEL`) |
| **9. REVERB** (Stereo Reverb with Pre-Cut, Damping Shelf EQ & Internal Chorus; Serum 5 Styles `Plate`, `Hall`, `Vintage`, `Nitrous`, `Basin`) | Both `[S p.175-177]`, `[V p.37-38]` | 13 keys (`reverb.*`) | **HAVE** |
| — Enable (`On`), Dry/Wet (`MIX`), Size (`SIZE`), Decay (`DECAY`), Pre-Delay (`PRE-DLY`), Pre Low/High Cut (`LO CUT`, `HI CUT`), Post Low/High Damping Shelf (`DAMP`), Chorus Amount & Rate (`CHORUS` / `SPIN`) | Both `[S p.175-177]`, `[V p.38]` | `reverb.{on,mix,size,decay,predelay,pre_low_cut,pre_high_cut,low_shelf_cutoff,low_shelf_gain,high_shelf_cutoff,high_shelf_gain,chorus_amount,chorus_rate}` (12 modulatable keys) | **HAVE** (13 keys) |
| — Serum Reverb Algorithm `TYPE` (`Plate`, `Hall`, `Vintage`, `Nitrous` + 5 room modes `Space/Marble/Rectangle/Hexagon/Box`, `Basin`), `WIDTH`, `ER SIZE`, `DIFF A/B`, `FEEDBACK`, `DIFFUSION`, `LEVEL` | Serum `[S p.175-177]` | `GAP` | **GAP** |
| **10. BODE FREQUENCY SHIFTER** (`MONO INPUT`, `SHIFT` + right-click `Retrig`, `RANGE`, `DIR`, `WIDTH`, `DELAY`, `BPM`, `FEED`, `BALANCE`, `BLUR`, `MIX`, `LEVEL`) | Serum `[S p.161]` | `GAP` | **GAP** |
| **11. CONVOLVE (IR Convolution)** (`IMPULSE` selector + external IR drag/load + `Embed in Preset`, `SIZE`, `TONE`, `phi MIN` minimum phase, `PRE-DLY`, `BPM`, `ATTACK`, `DECAY`, `DAMP`, `IR GAIN`, `MIX`, `LEVEL`) | Serum `[S p.164-165]` | `GAP` | **GAP** |
| **12. HYPER / DIMENSION** (Hyper 1–7 voice micro-delay chorus: `RATE`, `UNISON`, `DETUNE`, `RETRIG`, `MIX`; Dimension 4-tap out-of-phase expander: `SIZE`, `MIX`; `LEVEL`) | Serum `[S p.173-174]` | `GAP` | **GAP** |
| **13. UTILITY** (`POLARITY INV` L/R, `LPF`, `HPF`, `MONO BASS` switch + `FREQ`, `WIDTH`, `PAN`, `MIX`, `LEVEL`) | Serum `[S p.182]` | `GAP` | **GAP** |
| **14–16. SPLITTERS: `SPLITTER L/H`, `SPLITTER L/M/H`, `SPLITTER M/S`** (band/mid-side crossover splitters hosting independent sub-racks per band with band bypass, `SPLIT FREQ`, `LEVEL`) | Serum `[S p.177-181]` | `GAP` | **GAP** |

---

## 2. Screen design

### 2.1 FX Page Shell & Reorderable Chain Ribbon (Phone Portrait) (`00-BRIEF.md` #1, #2)

On phone portrait, the top of the `[FX]` page shows the **Reorderable 9-Slot FX Chain Ribbon** (`fx.order.1..9`) plus a Bus/Rack bar (`[MAIN*] [BUS 1 (GAP)] [BUS 2 (GAP)] [Byp All] [Rack Preset v]`). Below the ribbon sits **one active FX panel at a time** with **1 interactive graphic visual + 4 primary knobs + a `[More...]` row/sheet**:

```
+-------------------------------------------------------------------+
| FX RACK: [MAIN*] [BUS 1(GAP)] [BUS 2(GAP)] [Byp All] [Presets v]  |
+-------------------------------------------------------------------+
| CHAIN ORDER (`fx.order.1..9` — tap to select, drag `=` to move):  |
| [1.Filt]>[2.Dist*]>[3.Comp*]>[4.EQ]>[5.Chor]>[6.Flg!]>[7.Phs]... |
|  (Off)     (ON)      (ON)    (Off)   (Off)  (LOW-CAP)  (Off)      |
+-------------------------------------------------------------------+
| ACTIVE MODULE: #2 DISTORTION  [ON: x]  [Type: Soft Clip v]  [=]   |
|  +-------------------------------------------------------------+  |
|  | WAVESHAPER CURVE & PRE/POST FILTER RESPONSE  [Wave] [Filt]  |  |
|  |     1.0 |          . * * * *     Filter: [None | Pre* | Post]|  |
|  |         |       . *              Drag X/Y: Cutoff / Reso    |  |
|  |     0.0 | - - -* - - - - - -     Drag Vert: Drive (+6.5 dB) |  |
|  |         |   * .                  Cutoff: 3.2 kHz  Res: 40%  |  |
|  |    -1.0 | * * * .                                           |  |
|  +-------------------------------------------------------------+  |
|                                                                   |
|   (( DRIVE ))       (( MIX ))       (( FILT CUT ))  (( FILT RES ))|
|     +6.5 dB          100.0 %           3200 Hz          40.0 %    |
| (`distortion.drive`)(`distortion.mix`)(`filter_cutoff`)(`..._res`)|
+-------------------------------------------------------------------+
|  [More: Filter Order (Pre) | Filter Blend (0%) | Edit A/B (GAP)]  |
+-------------------------------------------------------------------+
```

---

### 2.2 Per-Effect Screen Designs (1 Visual + 4 Main Knobs + "More" Sheet)

Every effect module follows the exact same mobile template inside the active FX viewport:

#### 1. Chorus (`chorus.*`, 12 keys) (`[S p.162]`, `[V p.27-28]`)
- **Header:** `[ON: chorus.on]` | `[Voices: 4 v (chorus.voices)]` | `[Sync: Free / Tempo / Dot / Trip (chorus.sync)]`
- **Visual:** Animated dual-tap stereo delay voice visualizer + bandpass filter response curve (drag X = `chorus.filter_cutoff`, drag Y = `chorus.filter_spread`).
- **4 Main Knobs:** `1. Rate / Division` (`chorus.rate` or `chorus.division`), `2. Depth` (`chorus.depth`), `3. Feedback` (`chorus.feedback`), `4. Mix` (`chorus.mix`).
- **"More" Sheet:** `Delay 1` (`chorus.delay1`), `Delay 2` (`chorus.delay2`), `Filter Cutoff` (`chorus.filter_cutoff`), `Filter Spread` (`chorus.filter_spread`), `Output Level (GAP)`.

#### 2. Compressor (`compressor.*`, 20 keys) (`[S p.162-164]`, `[V p.28-30]`)
- **Header:** `[ON: compressor.on]` | `[Mode: Multiband / Low / High / Single (compressor.bands)]` | `[Band Tab: All | Low | Mid | High]`
- **Visual:** 3-Band OTT Upward/Downward Transfer Bars (`Low <150 Hz`, `Mid`, `High >3 kHz`) with real-time green output meters and white input peak lines `[V p.29]`. Dragging the top/bottom box edges adjusts `upper_threshold` / `lower_threshold`; dragging inside a box adjusts `upper_ratio` / `lower_ratio`; dragging the post-EQ handle adjusts `low/mid/high.gain`.
- **4 Main Knobs (Global view):** `1. Attack` (`compressor.attack`), `2. Release` (`compressor.release`), `3. Mid Gain` (`compressor.mid.gain`), `4. Mix` (`compressor.mix`).
- **"More" Sheet (Band Inspector for `Low / Mid / High`):** `Low Gain` (`compressor.low.gain`), `Mid Gain` (`compressor.mid.gain`), `High Gain` (`compressor.high.gain`), plus per-band `Upper Threshold`, `Lower Threshold`, `Upper Ratio`, `Lower Ratio`, and Serum `X-Low / X-High / Limit Mode (GAP)`.

#### 3. Delay (`delay.*`, 11 keys) (`[S p.165-167]`, `[V p.30-31]`)
- **Header:** `[ON: delay.on]` | `[Style: Mono / Stereo / Ping Pong / Mid PP (delay.style)]` | `[Sync: Free / Tempo / Dot / Trip (delay.sync)]` | `[Link L/R]`
- **Visual:** Split L/R Echo Impulse Timeline (top) + Interactive Filter XY Pad (bottom: drag X = `delay.filter_cutoff`, drag Y = `delay.filter_spread`, double-tap toggles live FFT overlay `[S p.167]`).
- **4 Main Knobs:** `1. Time L / Div 1` (`delay.time` or `delay.division`), `2. Time R / Div 2` (`delay.time2` or `delay.division2`), `3. Feedback` (`delay.feedback`), `4. Mix` (`delay.mix`).
- **"More" Sheet:** `Filter Cutoff` (`delay.filter_cutoff`), `Filter Spread` (`delay.filter_spread`), `Tap->Delay Mode (GAP)`, `L/R Scalar Offset (GAP)`, `High Quality (GAP)`.

#### 4. Distortion (`distortion.*`, 8 keys) (`[S p.167-169]`, `[V p.31-33]`)
- **Header:** `[ON: distortion.on]` | `[Type: Soft Clip / Hard Clip / Lin Fold / Sine Fold / Bit Crush / Down Sample (distortion.type)]` | `[Filter: None / Pre / Post (distortion.filter_order)]`
- **Visual:** Real-time Waveshaper Transfer Curve (`In` vs `Out`, drag vertically for `distortion.drive` `[S p.168]`) overlaid with red Filter Response Curve when Pre/Post filter is active (drag X = `distortion.filter_cutoff`, drag Y = `distortion.filter_resonance`).
- **4 Main Knobs:** `1. Drive` (`distortion.drive`), `2. Mix` (`distortion.mix`), `3. Filter Cutoff` (`distortion.filter_cutoff`), `4. Filter Resonance` (`distortion.filter_resonance`).
- **"More" Sheet:** `Filter Order` (`distortion.filter_order`), `Filter Blend` (`distortion.filter_blend` LP..BP..HP), Serum `X-Shaper Edit A / Edit B (GAP)`, `Filter Key Track (GAP)`.

#### 5. Equalizer (`eq.*`, 13 keys) (`[S p.169-170]`, `[V p.33-34]`)
- **Header:** `[ON: eq.on]` | Band Selector Pills: `[LOW: Shelf / HP (eq.low.mode)]` `[MID: Peak / Notch (eq.mid.mode)]` `[HIGH: Shelf / LP (eq.high.mode)]`
- **Visual:** Interactive 3-Node Parametric EQ Curve over real-time FFT spectrum. Dragging Node 1 (`LOW`), Node 2 (`MID`), or Node 3 (`HIGH`) sets `cutoff` (X) and `gain` (Y); pinch/two-finger vertical drag sets `resonance` (Q).
- **4 Main Knobs (follow selected band `Low / Mid / High`):** `1. Band Cutoff` (`eq.<band>.cutoff`), `2. Band Gain` (`eq.<band>.gain`), `3. Band Resonance` (`eq.<band>.resonance`), `4. Band Mode` (`eq.<band>.mode`).
- **"More" Sheet:** Full 9-knob matrix showing Low, Mid, and High `Cutoff / Gain / Resonance` side by side + Serum `Output Level (GAP)`.

#### 6. FX Filter (`fxfilter.*`, 15 keys) (`[S p.170-172]`, `[V p.34]`)
- **Header:** `[ON: fxfilter.on]` | `[Model: Analog / Dirty / Ladder / Digital / Diode / Formant / Comb / Phaser (fxfilter.model)]` | `[Style: A..F (fxfilter.style)]`
- **Visual:** Interactive Filter Response + Phase + FFT Curve (`Opt-tap` cycles `Freq Response` / `Freq + FFT` / `Phase + FFT` `[S p.171-172]`). Drag node X = `fxfilter.cutoff` (or `formant.x`), Y = `fxfilter.resonance` (or `formant.y`); top slider bar = `fxfilter.blend` (or `formant.transpose`).
- **4 Main Knobs (Standard Models):** `1. Cutoff` (`fxfilter.cutoff`), `2. Resonance` (`fxfilter.resonance`), `3. Drive` (`fxfilter.drive`), `4. Mix` (`fxfilter.mix`). *(When `Formant` model is selected, knobs switch to `Formant X`, `Formant Y`, `FormantRes`, `Mix`.)*
- **"More" Sheet:** `Blend` (`fxfilter.blend`), `Key Track` (`fxfilter.keytrack`), `Comb Offset` (`fxfilter.comb_offset`), `Formant Spread` (`fxfilter.formant.spread`), `Formant Transpose` (`fxfilter.formant.transpose`), Serum `Clean Mode (GAP)`, `Pan Offset (GAP)`.

#### 7. Flanger (`flanger.*`, 9 keys) (`[S p.172-173]`, `[V p.35-36]`)
- **Header:** `[ON: flanger.on]` | `[Sync: Free / Tempo / Dot / Trip (flanger.sync)]`
- **Visual:** Animated L/R Stereo Comb-Notch Frequency Response Curve (drag X = `flanger.center`, drag Y = `flanger.feedback`).
- **4 Main Knobs:** `1. Rate / Division` (`flanger.rate` or `flanger.division`), `2. Depth` (`flanger.depth`), `3. Feedback` (`flanger.feedback`), `4. Mix` (`flanger.mix`).
- **"More" Sheet:** `Center` (`flanger.center`), `Stereo Phase Offset` (`flanger.phase_offset`), `Sync` (`flanger.sync`), `Division` (`flanger.division`).

#### 8. Phaser (`phaser.*`, 10 keys) (`[S p.174-175]`, `[V p.36-37]`)
- **Header:** `[ON: phaser.on]` | `[Sync: Free / Tempo / Dot / Trip (phaser.sync)]`
- **Visual:** Animated L/R All-Pass Notch Response Curve with top `Blend / Poles` slider (`phaser.blend`); drag X = `phaser.center`, drag Y = `phaser.feedback`.
- **4 Main Knobs:** `1. Rate / Division` (`phaser.rate` or `phaser.division`), `2. Depth` (`phaser.depth`), `3. Center` (`phaser.center`), `4. Mix` (`phaser.mix`).
- **"More" Sheet:** `Feedback` (`phaser.feedback`), `Blend / Poles` (`phaser.blend`), `Phase Offset` (`phaser.phase_offset`), Serum `Depth 2 (GAP)`.

#### 9. Reverb (`reverb.*`, 13 keys) (`[S p.175-177]`, `[V p.37-38]`)
- **Header:** `[ON: reverb.on]` | `[Type (GAP): Plate / Hall / Vintage / Nitrous / Basin]`
- **Visual:** Interactive Post-Reverb Damping EQ & Decay Envelope Graph with two draggable purple shelf nodes (`LOW` = `reverb.low_shelf_cutoff` & `low_shelf_gain`, `HIGH` = `reverb.high_shelf_cutoff` & `high_shelf_gain` `[V p.38]`) plus Pre-Cut curtains (`reverb.pre_low_cut`, `reverb.pre_high_cut`).
- **4 Main Knobs:** `1. Size` (`reverb.size`), `2. Decay` (`reverb.decay`), `3. Pre-Delay` (`reverb.predelay`), `4. Mix` (`reverb.mix`).
- **"More" Sheet:** `Pre Low Cut` (`reverb.pre_low_cut`), `Pre High Cut` (`reverb.pre_high_cut`), `Low Shelf Cutoff & Gain` (`reverb.low_shelf_cutoff`, `reverb.low_shelf_gain`), `High Shelf Cutoff & Gain` (`reverb.high_shelf_cutoff`, `reverb.high_shelf_gain`), `Chorus Amount & Rate` (`reverb.chorus_amount`, `reverb.chorus_rate`), plus Serum `Width / ER Size / Diffusion / Feedback (GAP)`.

#### 10–16. Serum 2 Exclusive Modules (`Bode`, `Convolve`, `Hyper/Dimension`, `Utility`, `Splitter L/H`, `Splitter L/M/H`, `Splitter M/S` — Engine `GAP`)
Accessible via `[+ Add Serum FX (GAP)]` in the chain bar for UI prototyping:
- **Bode (`[S p.161]`):** Dual-direction frequency-shift visualizer + `Shift`, `Range`, `Delay`, `Mix` knobs; "More" sheet holds `Mono In`, `Dir`, `Width`, `BPM`, `Feed`, `Balance`, `Blur`, `Level`.
- **Convolve (`[S p.164-165]`):** IR waveform display (`[<] Impulse Name [>]`, `[Load IR]`, `[Embed]`, `[phi Min]`) + `Size`, `Tone`, `Pre-Dly`, `Mix` knobs; "More" sheet holds `Attack`, `Decay`, `Damp`, `IR Gain`, `BPM`, `Level`.
- **Hyper / Dimension (`[S p.173-174]`):** Split stereo unison/dimension field graphic + `Hyper Mix`, `Hyper Detune`, `Dim Size`, `Dim Mix` knobs; "More" sheet holds `Hyper Rate`, `Voices (0..7)`, `Retrig`, `Level`.
- **Utility (`[S p.182]`):** Stereo correlation goniometer + `Width`, `Pan`, `Mono Bass Freq`, `Level` knobs; "More" sheet holds `Polarity Inv L/R`, `LPF`, `HPF`, `Mono Bass On`, `Mix`.
- **Splitters (`L/H`, `L/M/H`, `M/S`, `[S p.177-181]`):** Crossover spectrum with draggable `SPLIT FREQ` divider lines and band sub-rack tabs (`[LOWS] [MIDS] [HIGHS]` or `[MID] [SIDE]`) with per-band bypass buttons.

---

### 2.3 Tablet & MPC Key 61 Landscape Layout (`00-BRIEF.md` #6)

On tablet and MPC Key 61 landscape, the left sidebar shows the vertical **9-Slot Drag-and-Drop FX Chain List** (`fx.order.1..9`) with instant bypass toggles, while the main viewport displays **2 to 3 expanded FX module panels stacked vertically** (matching Vital's Effects tab and Serum 2's expanded FX rack `[S p.153; V p.27]`).

---

## 3. Interactions

| Control | Gesture | Result | Alternative (non-gesture) |
|---------|---------|--------|---------------------------|
| **Chain Slot Chip (`[1.Filt]`..`[9.Rev]`)** | Single tap | Scrolls/switches active FX viewport to that effect module | Pick module from FX dropdown |
| **Chain Slot Power Dot** | Double-tap or tap `[ON]` button | Toggles `<fx>.on` (`0 Off / 1 On`); red bypass highlight when bypassed `[S p.157]` | Toggle `<fx>.on` on Kick remote page |
| **Chain Slot Handle (`=`)** | Long-press & drag left/right (or up/down in list) | Reorders effect in `fx.order.1..9` with yellow drop-target line `[S p.156; V p.27]` | Chain menu `[Move Left]` / `[Move Right]` buttons |
| **`[Byp All]` Header Button** | Single tap (`Opt/Alt-click` on desktop `[S p.157]`) | Temporarily mutes all enabled effects or restores previous `<fx>.on` state | Individual `<fx>.on` switches |
| **Chorus / Delay / Dist / FX Filter Curve** | 1-finger drag X/Y inside graph | Adjusts filter `cutoff` (X) and `spread` or `resonance` (Y) simultaneously `[S p.167, 171; V p.13, 28, 31]` | Turn `(( CUTOFF ))` and `(( RES / SPREAD ))` knobs |
| **Compressor 3-Band Bars** | Drag box top/bottom edge; drag inside box; drag gain handle | Sets `upper_threshold` / `lower_threshold`, `upper_ratio` / `lower_ratio`, and band `gain` `[V p.29]` | Band Inspector "More" sheet sliders |
| **Distortion Waveshaper Display** | Vertical drag up/down on wave graph | Adjusts `distortion.drive` (`-30..+30 dB`) with live curve deformation `[S p.168; V p.33]` | Turn `(( DRIVE ))` knob |
| **EQ 3-Band Nodes (`Low / Mid / High`)** | Drag node X/Y; pinch node | Selects band and adjusts `eq.<band>.cutoff` (X), `gain` (Y), and `resonance` (pinch) `[V p.34]` | Select `[LOW/MID/HIGH]` pill and turn the 4 main knobs |
| **Reverb Damping EQ Nodes (`Low / High`)** | Drag purple node X/Y | Adjusts `reverb.{low,high}_shelf_cutoff` (X) and `_shelf_gain` (`-6..0 dB`, Y) `[V p.38]` | Reverb "More" sheet knobs |

---

## 4. States and edge cases

1. **Quality Tier FX Bypass (`Tier::low` on MPC ARMv7, `core/include/synthyboy/quality.h` l.64):**
   - On `Tier::low`, `Quality::fxMask` clears `kFxReverb` and `kFxFlanger`. Even if `reverb.on == 1` or `flanger.on == 1` in the preset, the engine bypasses them without modifying the stored parameter values.
   - **UI State:** On `Tier::low`, the `Flanger` and `Reverb` chain chips and module headers display an amber `[BYPASSED BY LOW TIER]` badge. All knobs remain editable so presets saved on the MPC preserve their settings.
2. **Duplicate Values in `fx.order.1..9` (`core/engine-vital/README.md` l.58, `docs/QUALITY.md` l.93):**
   - If host automation or a remote control sets two `fx.order.N` parameters to the same effect index (`0..8`), the engine adapter deterministically assigns the lowest unplaced effect index to the later slot so the 9 effects always form a valid permutation. The UI highlights any duplicate choice with a subtle `(-> Auto: <Effect>)` label and normalizes `fx.order.1..9` when dragging slots in the UI.
3. **Sync vs Free Rate Modes (`chorus.sync`, `delay.sync`, `flanger.sync`, `phaser.sync`):**
   - When `sync == 0 (Free Hz)`, the primary Rate/Time knob controls `<fx>.rate` (Hz) or `delay.time` / `delay.time2` (ms), and `<fx>.division` is dimmed.
   - When `sync == 1..3 (Tempo / Dotted / Triplet)`, the primary knob switches to `<fx>.division` (`delay.division` / `delay.division2`), and the Hz/ms parameter is dimmed in the "More" sheet.
   - When `division == 0 (Freeze)` on Chorus, Flanger, or Phaser, LFO modulation freezes at its static center `[V p.28, 36, 37]`.
4. **EQ Band Mode Gain Disabling (`docs/PARAMETERS.md` l.596-607, `[S p.169-170; V p.34]`):**
   - When `eq.low.mode == 1 (High pass)`, `eq.mid.mode == 1 (Notch)`, or `eq.high.mode == 1 (Low pass)`, the corresponding `eq.<band>.gain` knob has no audible effect and is visually dimmed (`40%` opacity) with label `(N/A in Cut/Notch mode)`.
5. **Reverb Shelf Gain Range (`docs/PARAMETERS.md` l.635-637, `[V p.38]`):**
   - `reverb.low_shelf_gain` and `reverb.high_shelf_gain` are damping controls restricted to `-6..0 dB` (never boosting above `0 dB` to prevent feedback runaway).
6. **Own Engine (`core/engine-own`, iOS build):**
   - All 105 `FX` keys and 15 `FX Filter` keys have `ownEngine: false` in `docs/params.json` (`filter1` is the active filter in `engine-own`). On iOS builds, the FX rack displays an `[iOS Engine: Bypassed]` status pill until FX processors are ported to `engine-own`.

---

## 5. Kick remote pages

Every single one of the 120 FX + FX Filter parameters in `docs/params.json` is mapped to 8-knob Kick remote-control pages:

| Page ID | Page Name | Knob 1 | Knob 2 | Knob 3 | Knob 4 | Knob 5 | Knob 6 | Knob 7 | Knob 8 |
|---------|-----------|--------|--------|--------|--------|--------|--------|--------|--------|
| `fx_order_1` | **FX Chain Order 1–8** | `fx.order.1` | `fx.order.2` | `fx.order.3` | `fx.order.4` | `fx.order.5` | `fx.order.6` | `fx.order.7` | `fx.order.8` |
| `fx_switches` | **FX Order 9 & Switches** | `fx.order.9` | `fxfilter.on` | `distortion.on` | `compressor.on` | `eq.on` | `chorus.on` | `delay.on` | `reverb.on` |
| `fx_chorus_1` | **Chorus Main** | `chorus.on` | `chorus.mix` | `chorus.rate` | `chorus.sync` | `chorus.division` | `chorus.depth` | `chorus.feedback` | `chorus.voices` |
| `fx_chorus_2` | **Chorus Delays & Filter** | `chorus.delay1` | `chorus.delay2` | `chorus.filter_cutoff` | `chorus.filter_spread` | `chorus.mix` | `chorus.depth` | `chorus.feedback` | `chorus.on` |
| `fx_comp_main` | **Compressor Main & Gain** | `compressor.on` | `compressor.mix` | `compressor.bands` | `compressor.attack` | `compressor.release` | `compressor.low.gain` | `compressor.mid.gain` | `compressor.high.gain` |
| `fx_comp_low` | **Compressor Low Band** | `compressor.low.lower_threshold` | `compressor.low.upper_threshold` | `compressor.low.lower_ratio` | `compressor.low.upper_ratio` | `compressor.low.gain` | `compressor.attack` | `compressor.release` | `compressor.mix` |
| `fx_comp_mid` | **Compressor Mid Band** | `compressor.mid.lower_threshold` | `compressor.mid.upper_threshold` | `compressor.mid.lower_ratio` | `compressor.mid.upper_ratio` | `compressor.mid.gain` | `compressor.attack` | `compressor.release` | `compressor.mix` |
| `fx_comp_high` | **Compressor High Band** | `compressor.high.lower_threshold` | `compressor.high.upper_threshold` | `compressor.high.lower_ratio` | `compressor.high.upper_ratio` | `compressor.high.gain` | `compressor.attack` | `compressor.release` | `compressor.mix` |
| `fx_delay_1` | **Delay Main** | `delay.on` | `delay.mix` | `delay.style` | `delay.feedback` | `delay.sync` | `delay.time` | `delay.time2` | `delay.division` |
| `fx_delay_2` | **Delay Filter & Div 2** | `delay.division2` | `delay.filter_cutoff` | `delay.filter_spread` | `delay.feedback` | `delay.mix` | `delay.style` | `delay.sync` | `delay.on` |
| `fx_distortion` | **Distortion** | `distortion.on` | `distortion.type` | `distortion.drive` | `distortion.mix` | `distortion.filter_order` | `distortion.filter_cutoff` | `distortion.filter_resonance` | `distortion.filter_blend` |
| `fx_eq_low_mid` | **EQ Low & Mid** | `eq.on` | `eq.low.mode` | `eq.low.cutoff` | `eq.low.gain` | `eq.low.resonance` | `eq.mid.mode` | `eq.mid.cutoff` | `eq.mid.gain` |
| `fx_eq_high` | **EQ Mid Q & High** | `eq.mid.resonance` | `eq.high.mode` | `eq.high.cutoff` | `eq.high.gain` | `eq.high.resonance` | `eq.low.gain` | `eq.mid.gain` | `eq.on` |
| `fx_filter_1` | **FX Filter Main** | `fxfilter.on` | `fxfilter.model` | `fxfilter.style` | `fxfilter.cutoff` | `fxfilter.resonance` | `fxfilter.drive` | `fxfilter.blend` | `fxfilter.mix` |
| `fx_filter_2` | **FX Filter Formant/Comb** | `fxfilter.keytrack` | `fxfilter.comb_offset` | `fxfilter.formant.x` | `fxfilter.formant.y` | `fxfilter.formant.resonance` | `fxfilter.formant.spread` | `fxfilter.formant.transpose` | `fxfilter.mix` |
| `fx_flanger` | **Flanger** | `flanger.on` | `flanger.mix` | `flanger.rate` | `flanger.sync` | `flanger.division` | `flanger.depth` | `flanger.feedback` | `flanger.center` |
| `fx_phaser_1` | **Phaser & Flanger Phase** | `phaser.on` | `phaser.mix` | `phaser.rate` | `phaser.sync` | `phaser.division` | `phaser.depth` | `phaser.feedback` | `flanger.phase_offset` |
| `fx_phaser_2` | **Phaser Tone & Stereo** | `phaser.center` | `phaser.blend` | `phaser.phase_offset` | `phaser.depth` | `phaser.feedback` | `phaser.mix` | `phaser.rate` | `phaser.on` |
| `fx_reverb_1` | **Reverb Space & Cuts** | `reverb.on` | `reverb.mix` | `reverb.size` | `reverb.decay` | `reverb.predelay` | `reverb.pre_low_cut` | `reverb.pre_high_cut` | `reverb.chorus_amount` |
| `fx_reverb_2` | **Reverb Damping & Chorus** | `reverb.low_shelf_cutoff` | `reverb.low_shelf_gain` | `reverb.high_shelf_cutoff` | `reverb.high_shelf_gain` | `reverb.chorus_amount` | `reverb.chorus_rate` | `reverb.decay` | `reverb.mix` |

---

## 6. Coverage check

Every parameter in `FX` (105 keys) and `FX Filter` (15 keys) in `docs/params.json` is covered in Section 2 and Section 5:

| Parameter Group (`docs/params.json`) | Keys | Count | Where it appears in Section 2 |
|--------------------------------------|------|-------|-------------------------------|
| **FX / Order** | `fx.order.1` .. `fx.order.9` | 9 | §2.1 Reorderable 9-Slot FX Chain Ribbon |
| **FX / Chorus** | `chorus.{on,mix,rate,sync,division,depth,feedback,delay1,delay2,voices,filter_cutoff,filter_spread}` | 12 | §2.2 #1 Chorus panel (4 main knobs + visual + "More" sheet) |
| **FX / Compressor** | `compressor.{on,mix,attack,release,bands}`, `compressor.{low,mid,high}.{lower_threshold,upper_threshold,lower_ratio,upper_ratio,gain}` | 20 | §2.2 #2 Compressor panel (3-band OTT transfer visual + 4 main knobs + Band Inspector "More" sheet) |
| **FX / Delay** | `delay.{on,mix,feedback,time,time2,sync,division,division2,style,filter_cutoff,filter_spread}` | 11 | §2.2 #3 Delay panel (Timeline/XY filter visual + 4 main knobs + "More" sheet) |
| **FX / Distortion** | `distortion.{on,type,drive,mix,filter_order,filter_cutoff,filter_resonance,filter_blend}` | 8 | §2.1 & §2.2 #4 Distortion panel (Waveshaper/Filter curve + 4 main knobs + "More" sheet) |
| **FX / EQ** | `eq.on`, `eq.{low,mid,high}.{mode,cutoff,gain,resonance}` | 13 | §2.2 #5 Equalizer panel (3-node interactive FFT EQ graph + 4 band-following knobs + "More" sheet) |
| **FX Filter** & **FX Filter / Model Settings** *(also in `04-filters.md`)* | `fxfilter.{on,model,style,cutoff,resonance,drive,blend,mix,keytrack,comb_offset}`, `fxfilter.formant.{x,y,resonance,spread,transpose}` | 15 | §2.2 #6 FX Filter panel (Response/FFT graph + 4 main knobs + "More" sheet) |
| **FX / Flanger** | `flanger.{on,mix,feedback,rate,sync,division,depth,center,phase_offset}` | 9 | §2.2 #7 Flanger panel (Comb response visual + 4 main knobs + "More" sheet) |
| **FX / Phaser** | `phaser.{on,mix,feedback,rate,sync,division,depth,center,blend,phase_offset}` | 10 | §2.2 #8 Phaser panel (Notch response visual + 4 main knobs + "More" sheet) |
| **FX / Reverb** | `reverb.{on,mix,size,decay,predelay,pre_low_cut,pre_high_cut,low_shelf_cutoff,low_shelf_gain,high_shelf_cutoff,high_shelf_gain,chorus_amount,chorus_rate}` | 13 | §2.2 #9 Reverb panel (Damping EQ & decay visual + 4 main knobs + "More" sheet) |
| **Total covered in `08-effects.md`** | `FX` (105) + `FX Filter` (15) | **120 / 120** | 100% of FX and FX Filter parameters |

---

## 7. Open questions (owner)

1. **Sequencer exclusions (`00-BRIEF.md`):** Serum 2 **Clips** (`[S p.219]`) and **Arpeggiator** (`[S p.244]`) are excluded (Kick provides them).
2. **Serum-Only FX Modules (`Bode`, `Convolve`, `Hyper/Dimension`, `Utility`, `Splitters L/H, L/M/H, M/S`):** Should SynthyBoy stick to the 9 Vital effects for v1.0, or are any Serum-exclusive modules (e.g., `Hyper/Dimension` as a low-CPU unison alternative `[S p.173, 348]` or `Utility` for Mono Bass `[S p.182]`) desired as custom DSP additions?
3. **Parallel FX Busses (`MAIN`, `BUS 1`, `BUS 2` `[S p.146-153]`):** Keep the single 9-slot serial FX chain (`fx.order.1..9`) for mobile/MPC simplicity and CPU budget, or add parallel FX sends in a later version?
4. **Low Tier Bypass Override (`Tier::low` bypasses Reverb & Flanger, `quality.h` l.64):** On the MPC Key 61 (`Tier::low`), should the user have an optional "Force Enable Reverb" toggle in the Reverb header (trading polyphony for reverb), or should `Tier::low` strictly enforce `~(kFxReverb | kFxFlanger)` unless the user switches to `Tier::medium`?
