# Batch 2, the Sample oscillator type (and the first Convolve presets). `add`, `R`, `pressure`, `WT`, `SMP`, `IMP`, `INST`, `C` are provided by factory-presets-batch2.py.

add("Dusty Rhodes Keys", "Keys", "keys,electric-piano,fm,vinyl,sample,convolve,cabinet,lofi",
    "A tine electric piano from a frequency-modulation table, an amp cabinet convolved on it, and a loop of vinyl crackle under every note that comes from the Sample oscillator (it bypasses the filter, so it stays dusty and wide)",
    {
        "master.polyphony": 8,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 14, "osc1.level": 62, "osc1.unison.voices": 2, "osc1.unison.detune": 8, "osc1.unison.spread": 35,
        "osc2.on": 1, "osc2.wavetable": "Bell Partials", "osc2.frame": 30, "osc2.level": 12, "osc2.transpose": 12,
        "osc3.on": 1, "osc3.type": "Sample", "osc3.level": 16, "osc3.destination": "Effects only", "osc3.random_phase": 100, "osc3.sample.keytrack": 0, "osc3.sample.loop_mode": "Forward loop",
        "osc3.sample.loop_start": 0, "osc3.sample.loop_end": 100, "osc3.sample.normalize": 1,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.cutoff": 2600, "filter1.resonance": 14, "filter1.keytrack": 45,
        "env1.attack": 2, "env1.decay": 2400, "env1.sustain": 22, "env1.release": 420,
        "env2.attack": 0, "env2.decay": 380, "env2.sustain": 0,
        "lfo1.shape": "Sine", "lfo1.rate": 4.6,
        "chorus.on": 1, "chorus.mix": 22, "chorus.rate": 0.35, "chorus.depth": 35,
        "utility.on": 1, "utility.width": 125,
        "convolve.on": 1, "convolve.ir": "Preset IR", "convolve.mix": 85, "convolve.ir_gain": 0, "convolve.decay": 100,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 55,
    },
    [
        R("env2", "osc1.frame", 16),                                                       # the bark of the tine, gone in 380 ms
        R("velocity", "osc1.frame", 22), R("velocity", "filter1.cutoff", 14),
        R("lfo1", "utility.pan", 9, aux="mod_wheel"),                                      # the suitcase tremolo grows with the wheel
        R("mod_wheel", "osc1.frame", 18), R("mod_wheel", "chorus.mix", 22),
        R("macro1", "osc1.frame", 40),                                                     # Tine Bark
        R("macro2", "osc3.level", 70),                                                     # Dust
        R("macro3", "chorus.mix", 38), R("macro3", "chorus.depth", 30),                    # Chorus
        R("macro4", "convolve.mix", -85),                                                  # Dry Amp (0 = the full cabinet, 100 = no cabinet at all: the amount is negative)
        R("macro5", "env1.decay", 40), R("macro5", "env1.release", 25),                    # Sustain
        R("macro6", "filter1.cutoff", 55),                                                 # Tone
    ],
    ["Tine Bark", "Dust", "Chorus", "Dry Amp", "Sustain", "Tone", "", ""],
    content=C(wavetables=[WT(1, "fm_index_sweep")], samples=[SMP(3, "vinyl_noise_bed")], impulses=[IMP("cabinet_colour")]))

add("Glass Ping Pluck", "Pluck", "pluck,glass,bell,sample,plate,convolve,shimmer",
    "A struck wine glass: the Sample oscillator plays the ping at every key (it is tuned, so the whole keyboard is a glass harmonica), a thin bell table adds the inharmonic body, and the plate impulse response carries the ring into a long tail",
    {
        "master.polyphony": 10,
        "osc1.on": 1, "osc1.type": "Sample", "osc1.level": 72, "osc1.sample.root": 72, "osc1.sample.keytrack": 1, "osc1.sample.loop_mode": "One-shot", "osc1.sample.normalize": 1,
        "osc1.unison.voices": 2, "osc1.unison.detune": 10, "osc1.unison.spread": 55,
        "osc2.on": 1, "osc2.wavetable": "Bell Partials", "osc2.frame": 55, "osc2.level": 18, "osc2.transpose": 0,
        "filter1.on": 1, "filter1.model": "Digital", "filter1.style": 0, "filter1.cutoff": 7000, "filter1.resonance": 10, "filter1.keytrack": 55,
        "env1.attack": 0, "env1.decay": 3200, "env1.sustain": 0, "env1.release": 700,
        "delay.on": 1, "delay.sync": 2, "delay.division": 5, "delay.mix": 14, "delay.feedback": 32, "delay.style": "Ping pong",
        "convolve.on": 1, "convolve.ir": "Preset IR", "convolve.mix": 38, "convolve.ir_gain": 0,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 140,
    },
    [
        R("velocity", "filter1.cutoff", 18), R("velocity", "osc2.level", 14),
        R("mod_wheel", "convolve.mix", 30), R("mod_wheel", "osc1.unison.detune", 25),
        R("macro1", "filter1.cutoff", -45),                                                # Dark
        R("macro2", "convolve.mix", 45), R("macro2", "convolve.attack", 10),               # Plate
        R("macro3", "osc1.unison.detune", 60),                                             # Shimmer
        R("macro4", "delay.mix", 38),                                                      # Echo
        R("macro5", "osc2.level", 50),                                                     # Body
        R("macro6", "env1.release", 35), R("macro6", "env1.decay", 20),                    # Ring
    ],
    ["Dark", "Plate", "Shimmer", "Echo", "Body", "Ring", "", ""],
    content=C(samples=[SMP(1, "glass_ping")], impulses=[IMP("plate")]))

add("Breakbeat Slice Keys", "Keys", "keys,drums,slice,loop,sample,bus,parallel,crush,convolve,room",
    "A two bar break cut into 32 slices that you play from C1: every key is one sixteenth, so a rhythm is a melody; a second, crushed and compressed copy runs on bus 1 in parallel, a small-room impulse response glues it together",
    {
        "master.polyphony": 6,
        "osc1.on": 1, "osc1.type": "Sample", "osc1.level": 78, "osc1.sample.root": 60, "osc1.sample.loop_mode": "One-shot", "osc1.sample.normalize": 1,
        "osc1.sample.slice_mode": "Manual", "osc1.sample.slice_play": "Play single slice", "osc1.sample.slice_root": 36, "osc1.bus1_send": 70,
        "osc2.on": 1, "osc2.wavetable": "Sine", "osc2.level": 10, "osc2.transpose": -24,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.cutoff": 16000, "filter1.resonance": 12,
        "env1.attack": 0, "env1.decay": 450, "env1.sustain": 100, "env1.release": 60,
        "bus1.level": -6, "bus1.destination": "Main",
        "distortion.on": 1, "distortion.rack": "Bus 1", "distortion.type": "Bit crush", "distortion.drive": 9, "distortion.mix": 80,
        "compressor.on": 1, "compressor.rack": "Bus 1", "compressor.bands": "Single band", "compressor.mid.upper_threshold": -28, "compressor.mid.upper_ratio": 70, "compressor.attack": 6, "compressor.release": 70, "compressor.mid.gain": 8,
        "convolve.on": 1, "convolve.ir": "Preset IR", "convolve.mix": 24, "convolve.ir_gain": 0,
    },
    [
        R("velocity", "distortion.drive", 35), R("velocity", "bus1.level", 6),
        R("mod_wheel", "distortion.drive", 40), R("mod_wheel", "bus1.level", 8),
        R("macro1", "bus1.level", 14), R("macro1", "distortion.drive", 35),               # Smash
        R("macro2", "convolve.mix", 45),                                                   # Room
        R("macro3", "osc1.sample.slice", 60),                                              # Shift
        R("macro4", "filter1.cutoff", -62),                                                # Dark
        R("macro5", "env1.sustain", -85),                                                  # Snap (the slice decays to 15 % in 450 ms instead of ringing to its end)
        R("macro6", "osc2.level", 55),                                                     # Sub Layer
    ],
    ["Smash", "Room", "Shift", "Dark", "Snap", "Sub Layer", "", ""],
    content=C(samples=[SMP(1, "drum_loop_120bpm", slices=[k / 32 for k in range(32)])], impulses=[IMP("small_room")]))

add("Bowed MPE Strings", "Pad", "pad,strings,bowed,sample,loop,mpe,pressure,expressive",
    "A bowed string recorded from nothing: the Sample oscillator loops the sustain of the bow tone (the loop closes on whole cycles of its vibrato, so it does not pump) and plays the release after the key goes up; a saw layer under it carries the pitch even where the sample is thin. Set up for an MPE controller: slide opens the filter, pressure adds bow noise and vibrato",
    {
        "master.polyphony": 8, "master.mpe.on": 1, "master.mpe.y_modwheel": 1,
        "osc1.on": 1, "osc1.type": "Sample", "osc1.level": 62, "osc1.sample.root": 60, "osc1.sample.keytrack": 1, "osc1.sample.loop_mode": "Tailed",
        "osc1.sample.loop_start": 14.3, "osc1.sample.loop_end": 77.7, "osc1.sample.loop_xfade": 25, "osc1.sample.normalize": 1,
        "osc1.unison.voices": 3, "osc1.unison.detune": 14, "osc1.unison.spread": 70,
        "osc2.on": 1, "osc2.wavetable": "Saw", "osc2.level": 26, "osc2.tune": 4, "osc2.unison.voices": 3, "osc2.unison.detune": 10, "osc2.unison.spread": 60,
        "noise.on": 1, "noise.level": 0, "noise.destination": "Filter 1", "noise.sample": "Pink Noise",
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 1, "filter1.cutoff": 2400, "filter1.resonance": 14, "filter1.keytrack": 50,
        "env1.attack": 140, "env1.decay": 900, "env1.sustain": 100, "env1.release": 800,
        "lfo1.shape": "Sine", "lfo1.rate": 5.3, "lfo1.fade_in": 600, "lfo2.shape": "Sine", "lfo2.rate": 5.3, "lfo2.fade_in": 600,
        "chorus.on": 1, "chorus.mix": 16, "chorus.rate": 0.3,
        "reverb.on": 1, "reverb.type": "Hall", "reverb.mix": 22, "reverb.decay": 2600, "reverb.size": 65,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 70,
    },
    [
        R("poly_aftertouch", "filter1.cutoff", 40),                                        # the pad's pressure route
        R("poly_aftertouch", "noise.level", 12),                                           # bow scratch grows with the weight
        R("lfo2", "osc1.tune", 12, aux="poly_aftertouch"), R("lfo2", "osc2.tune", 12, aux="poly_aftertouch"),   # and a vibrato that was not there
        R("slide", "filter1.cutoff", 28),                                                  # MPE slide (CC74); also the mod wheel of a plain keyboard through y_modwheel
        R("lfo1", "osc1.tune", 5), R("lfo1", "osc2.tune", 5),
        R("velocity", "filter1.cutoff", 16), R("velocity", "env1.attack", -25),
        R("mod_wheel", "filter1.cutoff", 22), R("mod_wheel", "filter1.resonance", 12),
        R("macro1", "filter1.cutoff", 40),                                                 # Brightness
        R("macro2", "noise.level", 22),                                                    # Rosin
        R("macro3", "lfo1.rate", 40), R("macro3", "lfo2.rate", 40),                        # Vibrato Rate
        R("macro4", "env1.attack", 35), R("macro4", "env1.release", 25),                   # Bow Speed
        R("macro5", "reverb.mix", 35), R("macro5", "chorus.mix", 28),                      # Hall
        R("macro6", "osc2.level", 40),                                                     # Saw Layer
    ],
    ["Brightness", "Rosin", "Vibrato Rate", "Bow Speed", "Hall", "Saw Layer", "", ""],
    content=C(samples=[SMP(1, "bowed_tone")]))

add("Reverse Gong Swell", "FX", "fx,gong,reverse,swell,sample,riser,convolve,plate",
    "A gong played backwards: the whole decay of the metal is a four second crescendo that ends in the strike; the lower you play, the slower it runs (C1 swells for about eight seconds, C3 for two), noise lifts with it and a plate carries the tail past the hit",
    {
        "master.polyphony": 4,
        "osc1.on": 1, "osc1.type": "Sample", "osc1.level": 80, "osc1.sample.root": 36, "osc1.sample.keytrack": 1, "osc1.sample.reverse": 1, "osc1.sample.loop_mode": "One-shot", "osc1.sample.normalize": 1,
        "osc1.unison.voices": 2, "osc1.unison.detune": 12, "osc1.unison.spread": 80,
        "noise.on": 1, "noise.level": 14, "noise.sample": "Pink Noise", "noise.destination": "Filter 1",
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 600, "filter1.resonance": 12, "filter1.keytrack": 0,
        "env1.attack": 30, "env1.decay": 4000, "env1.sustain": 100, "env1.release": 900,
        "env2.attack": 3600, "env2.decay": 500, "env2.sustain": 100,
        "reverb.on": 1, "reverb.type": "Plate", "reverb.mix": 20, "reverb.decay": 3400,
        "convolve.on": 1, "convolve.ir": "Preset IR", "convolve.mix": 30, "convolve.ir_gain": 0,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 50, "eq.high.cutoff": 9000, "eq.high.gain": 2,
    },
    [
        R("env2", "filter1.cutoff", 52),                                                   # the filter opens with the swell
        R("velocity", "filter1.cutoff", 10),
        R("mod_wheel", "osc1.sample.scan", -40),                                           # the wheel slows the run
        R("macro1", "osc1.sample.scan", -40),                                              # Slow Down (to a fifth of the speed)
        R("macro2", "osc1.unison.detune", 80),                                             # Shimmer
        R("macro3", "noise.level", 30),                                                    # Air
        R("macro4", "convolve.mix", 40), R("macro4", "reverb.mix", 30),                    # Space
        R("macro5", "filter1.cutoff", 30),                                                 # Dark
    ],
    ["Slow Down", "Shimmer", "Air", "Space", "Bright", "", "", ""],
    content=C(samples=[SMP(1, "gong_metal")], impulses=[IMP("plate")]))

add("Breath Flute Lead", "Lead", "lead,flute,breath,sample,legato,pressure,expressive",
    "A solo flute: a sine-to-saw tone with a vibrato that fades in, and the Sample oscillator puts a breath chiff on the front of every separate note; pressure on the pad adds the air that blows through the tone and opens the sound. Mono, legato",
    {
        "master.polyphony": 1, "master.legato": 1, "master.portamento.time": 35,
        "osc1.on": 1, "osc1.wavetable": "Sine to Saw", "osc1.frame": 8, "osc1.level": 64,
        "osc2.on": 1, "osc2.type": "Sample", "osc2.level": 34, "osc2.sample.keytrack": 0, "osc2.sample.loop_mode": "One-shot", "osc2.sample.normalize": 1, "osc2.destination": "Filter 1",
        "noise.on": 1, "noise.level": 5, "noise.sample": "Pink Noise", "noise.destination": "Filter 1",
        "filter1.on": 1, "filter1.model": "Digital", "filter1.style": 0, "filter1.cutoff": 2400, "filter1.resonance": 8, "filter1.keytrack": 70,
        "env1.attack": 70, "env1.decay": 300, "env1.sustain": 92, "env1.release": 240,
        "lfo1.shape": "Sine", "lfo1.rate": 5.0, "lfo1.fade_in": 800, "lfo2.shape": "Sine", "lfo2.rate": 5.0, "lfo2.fade_in": 800,
        "delay.on": 1, "delay.sync": 2, "delay.division": 5, "delay.mix": 12, "delay.feedback": 28,
        "reverb.on": 1, "reverb.type": "Plate", "reverb.mix": 18, "reverb.decay": 1800,
    },
    [
        R("lfo1", "osc1.tune", 6), R("lfo2", "osc1.tune", 8, aux="mod_wheel"),         # the wheel deepens the vibrato (a second LFO of the same rate: the checker counts two routes of one pair as duplicates)
        R("poly_aftertouch", "noise.level", 10), R("poly_aftertouch", "filter1.cutoff", 30), R("poly_aftertouch", "osc1.frame", 16),
        R("velocity", "osc2.level", 25), R("velocity", "filter1.cutoff", 16),
        R("mod_wheel", "osc1.frame", 20),
        R("macro1", "noise.level", 18),                                                    # Breath
        R("macro2", "osc2.level", 42),                                                     # Chiff
        R("macro3", "lfo1.rate", 40), R("macro3", "lfo2.rate", 40),                        # Vibrato Rate
        R("macro4", "filter1.cutoff", 55),                                                 # Bright
        R("macro5", "osc1.frame", 35),                                                     # Overblow
        R("macro6", "reverb.mix", 30), R("macro6", "delay.mix", 20),                       # Space
    ],
    ["Breath", "Chiff", "Vibrato Rate", "Bright", "Overblow", "Space", "", ""],
    content=C(samples=[SMP(2, "breath_noise_burst")]))
