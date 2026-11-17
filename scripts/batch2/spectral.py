# Batch 2, the Spectral oscillator type (the dearest of the sample types: every one of these keeps its polyphony low and its unison at 2 or less).
# `add`, `R`, `pressure`, `WT`, `SMP`, `IMP`, `INST`, `C` are provided by factory-presets-batch2.py.

add("Spectral Vowel Freeze", "Pad", "pad,spectral,vowel,freeze,choir,resynthesis,hall",
    "A sung vowel frozen into the spectrum: the Spectral oscillator re-synthesises one instant of the voice pad that a slow Perlin LFO moves along the file, so the choir changes its vowel by itself without ever beating like grains do; added harmonics make it brighter, the shift moves its formants",
    {
        "master.polyphony": 4,
        "osc1.on": 1, "osc1.type": "Spectral", "osc1.level": 58, "osc1.sample.root": 60, "osc1.sample.loop_mode": "Forward loop", "osc1.sample.normalize": 1,
        "osc1.spec.manual": 1, "osc1.spec.position": 45, "osc1.spec.freq_lo": 90, "osc1.spec.freq_hi": 14000, "osc1.spec.phase_lock": 1,
        "osc1.warp.type": "Add Harmonics", "osc1.warp.amount": 25,
        "osc1.unison.voices": 2, "osc1.unison.detune": 6, "osc1.unison.spread": 70,
        "osc2.on": 1, "osc2.wavetable": "Sine to Saw", "osc2.frame": 22, "osc2.level": 14, "osc2.transpose": -12,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 6000, "filter1.resonance": 10, "filter1.keytrack": 30,
        "env1.attack": 800, "env1.decay": 1500, "env1.sustain": 92, "env1.release": 1800,
        "rlfo1.style": "Perlin", "rlfo1.rate": 0.12,
        "reverb.on": 1, "reverb.type": "Hall", "reverb.mix": 28, "reverb.decay": 4000, "reverb.size": 70,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 80,
    },
    [
        R("rlfo1", "osc1.spec.position", 30),                                              # the vowel drifts along the file
        pressure(38), R("poly_aftertouch", "osc1.warp.amount", 30),
        R("velocity", "filter1.cutoff", 14),
        R("mod_wheel", "osc1.spec.position", 35), R("mod_wheel", "osc1.spec.freq_shift", 4),
        R("macro1", "osc1.spec.position", 40),                                             # Vowel
        R("macro2", "osc1.warp.amount", 55),                                               # Harmonics
        R("macro3", "osc1.spec.freq_shift", 6),                                            # Shift
        R("macro4", "osc1.spec.freq_hi", -65),                                             # Hi Cut
        R("macro5", "reverb.mix", 35), R("macro5", "reverb.decay", 20),                    # Space
        R("macro6", "osc2.level", 45),                                                     # Saw Layer
    ],
    ["Vowel", "Harmonics", "Shift", "Hi Cut", "Space", "Saw Layer", "", ""],
    content=C(samples=[SMP(1, "vowel_pad")]))

add("Spectral Bowed Glass", "Lead", "lead,spectral,bowed,glass,comb,legato,expressive",
    "A lead that is half a bow and half a glass harmonica: the Spectral oscillator re-synthesises the bowed tone (the sustain loop, read in real time) through a spectral comb that the wheel and the pressure close, and a glass table sings the fundamental with it. Mono, legato, glide",
    {
        "master.polyphony": 1, "master.legato": 1, "master.portamento.time": 55,
        "osc1.on": 1, "osc1.type": "Spectral", "osc1.level": 56, "osc1.sample.root": 60, "osc1.sample.loop_mode": "Forward loop", "osc1.sample.loop_start": 14.3, "osc1.sample.loop_end": 77.7, "osc1.sample.start": 14.3,
        "osc1.sample.normalize": 1, "osc1.spec.freq_lo": 100, "osc1.spec.freq_hi": 12000, "osc1.warp.type": "Spectral Comb", "osc1.warp.amount": 10,
        "osc2.on": 1, "osc2.wavetable": "Preset table", "osc2.frame": 20, "osc2.level": 30, "osc2.unison.voices": 2, "osc2.unison.detune": 6, "osc2.unison.spread": 60,
        "filter1.on": 1, "filter1.model": "Digital", "filter1.style": 0, "filter1.cutoff": 2500, "filter1.resonance": 10, "filter1.keytrack": 60,
        "env1.attack": 90, "env1.decay": 400, "env1.sustain": 92, "env1.release": 420,
        "lfo1.shape": "Sine", "lfo1.rate": 5.2, "lfo1.fade_in": 700,
        "delay.on": 1, "delay.sync": 2, "delay.division": 5, "delay.mix": 14, "delay.feedback": 30,
        "reverb.on": 1, "reverb.type": "Plate", "reverb.mix": 20, "reverb.decay": 2000,
    },
    [
        R("lfo1", "osc1.tune", 5), R("lfo1", "osc2.tune", 5),
        R("mod_wheel", "osc1.warp.amount", 45), R("mod_wheel", "osc2.frame", 25),
        pressure(30), R("poly_aftertouch", "osc1.warp.amount", 30),
        R("velocity", "filter1.cutoff", 16), R("velocity", "osc1.warp.amount", 12),
        R("macro1", "osc1.warp.amount", 50),                                               # Comb
        R("macro2", "osc2.level", 40),                                                     # Glass
        R("macro3", "osc2.frame", 50),                                                     # Glass Shape
        R("macro4", "filter1.cutoff", 55),                                                 # Bright
        R("macro5", "lfo1.rate", 40),                                                      # Vibrato Rate
        R("macro6", "reverb.mix", 30), R("macro6", "delay.mix", 20),                       # Space
    ],
    ["Comb", "Glass", "Glass Shape", "Bright", "Vibrato Rate", "Space", "", ""],
    content=C(wavetables=[WT(2, "additive_glass_harmonica")], samples=[SMP(1, "bowed_tone")]))

add("Spectral Gong Pad", "Pad", "pad,spectral,gong,metal,inharmonic,drone,wide",
    "The gong's spectrum held and bowed: the Spectral oscillator scans a slice of the gong's decay back and forth (loop forward-reverse), so the inharmonic partials swell and fade like a bowed metal plate, and every unison voice reads it from another place; the bass below 200 Hz is kept in the middle",
    {
        "master.polyphony": 4,
        "osc1.on": 1, "osc1.type": "Spectral", "osc1.level": 56, "osc1.sample.root": 36, "osc1.sample.loop_mode": "Forward/reverse loop", "osc1.sample.loop_start": 8, "osc1.sample.loop_end": 55, "osc1.sample.normalize": 1,
        "osc1.sample.scan": 25, "osc1.spec.freq_lo": 120, "osc1.spec.freq_hi": 10000, "osc1.spec.phase_lock": 1,
        "osc1.unison.voices": 2, "osc1.unison.detune": 10, "osc1.unison.spread": 100, "osc1.sample.uni_start": 20, "osc1.sample.uni_span": 25,
        "osc2.on": 1, "osc2.wavetable": "Bell Partials", "osc2.frame": 35, "osc2.level": 18, "osc2.transpose": 0,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 2500, "filter1.resonance": 10, "filter1.keytrack": 40,
        "env1.attack": 1200, "env1.decay": 1500, "env1.sustain": 90, "env1.release": 2600,
        "rlfo1.style": "Perlin", "rlfo1.rate": 0.15,
        "reverb.on": 1, "reverb.type": "Nitrous", "reverb.mix": 30, "reverb.decay": 5000, "reverb.size": 75,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 100,
        "utility.on": 1, "utility.mono_bass": 1, "utility.mono_freq": 200, "utility.width": 120,
    },
    [
        R("rlfo1", "osc1.sample.scan", 20),                                                # the speed of the scan drifts
        pressure(40), R("poly_aftertouch", "osc1.spec.freq_shift", 3),
        R("velocity", "filter1.cutoff", 14),
        R("mod_wheel", "osc1.spec.freq_shift", 4), R("mod_wheel", "utility.width", 25),
        R("macro1", "osc1.sample.scan", 75),                                               # Scan Speed (a quarter of the speed up to the full one: the amount is a share of the whole -1 .. 1 range of the scan)
        R("macro2", "osc1.spec.freq_shift", 7),                                            # Shift
        R("macro3", "osc1.sample.uni_span", 60),                                           # Spread Read
        R("macro4", "filter1.cutoff", 40),                                                 # Bright
        R("macro5", "reverb.mix", 35),                                                     # Space
        R("macro6", "osc2.level", 45),                                                     # Bell Layer
    ],
    ["Scan Speed", "Shift", "Spread Read", "Bright", "Space", "Bell Layer", "", ""],
    content=C(samples=[SMP(1, "gong_metal")]))

add("Spectral Chord Morph", "FX", "fx,spectral,morph,chords,scan,transient,evolving",
    "A four-chord progression re-synthesised as one evolving spectrum: the Spectral oscillator plays the file once, in real time, so holding a key walks through C major 7, A minor 7, F major 7 and G 6 as a morph; the spectral shift and the gate turn the harmony into something stranger as the wheel goes up",
    {
        "master.polyphony": 3,
        "osc1.on": 1, "osc1.type": "Spectral", "osc1.level": 58, "osc1.sample.root": 60, "osc1.sample.loop_mode": "Forward loop", "osc1.sample.normalize": 1,
        "osc1.sample.scan": 100, "osc1.spec.freq_lo": 60, "osc1.spec.freq_hi": 12000, "osc1.warp.type": "Spectral Shift", "osc1.warp.amount": 0,
        "osc1.unison.voices": 2, "osc1.unison.detune": 7, "osc1.unison.spread": 80,
        "osc2.on": 1, "osc2.wavetable": "Sine to Saw", "osc2.frame": 25, "osc2.level": 14, "osc2.transpose": -12,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 2600, "filter1.resonance": 10, "filter1.keytrack": 25,
        "env1.attack": 500, "env1.decay": 1500, "env1.sustain": 92, "env1.release": 2200,
        "reverb.on": 1, "reverb.type": "Hall", "reverb.mix": 26, "reverb.decay": 4500, "reverb.size": 70,
        "chorus.on": 1, "chorus.mix": 18, "chorus.rate": 0.2,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 80,
    },
    [
        R("mod_wheel", "osc1.warp.amount", 60), R("mod_wheel", "osc1.spec.freq_shift", 4),
        pressure(36),
        R("velocity", "filter1.cutoff", 14),
        R("macro1", "osc1.sample.scan", -60),                                              # Slow
        R("macro2", "osc1.warp.amount", 70),                                               # Shift Warp
        R("macro3", "osc1.spec.freq_shift", 7),                                            # Freq Shift
        R("macro4", "osc1.spec.freq_hi", -65),                                             # Hi Cut
        R("macro5", "reverb.mix", 35), R("macro5", "chorus.mix", 25),                      # Space
        R("macro6", "filter1.cutoff", 50),                                                 # Bright
    ],
    ["Slow", "Shift Warp", "Freq Shift", "Hi Cut", "Space", "Bright", "", ""],
    content=C(samples=[SMP(1, "chord_pad_sections")]))
