# Batch 2, the Granular oscillator type. `add`, `R`, `pressure`, `WT`, `SMP`, `IMP`, `INST`, `C` are provided by factory-presets-batch2.py.

add("Chord Cloud Pad", "Pad", "pad,granular,cloud,chords,scan,voice-control,wide",
    "A cloud of grains cut from a four-chord progression (C major 7, A minor 7, F major 7, G 6): a slow LFO walks the play head through the file, so the harmony drifts on its own; the voice control gives every new note its own pan, tuning and brightness, so a chord is never a copy of itself",
    {
        "master.polyphony": 6,
        "osc1.on": 1, "osc1.type": "Granular", "osc1.level": 62, "osc1.sample.root": 60, "osc1.sample.loop_mode": "Forward loop", "osc1.sample.normalize": 1,
        "osc1.gran.manual": 1, "osc1.gran.position": 50, "osc1.gran.density": 26, "osc1.gran.length": 180, "osc1.gran.window_shape": "Hann", "osc1.gran.window_amount": 75,
        "osc1.gran.rand_offset": 10, "osc1.gran.rand_pan": 70, "osc1.gran.rand_level": 20, "osc1.gran.max_grains": 48,
        "osc1.unison.voices": 2, "osc1.unison.detune": 6, "osc1.unison.spread": 80,
        "osc2.on": 1, "osc2.wavetable": "Sine to Saw", "osc2.frame": 20, "osc2.level": 16, "osc2.transpose": -12,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 2400, "filter1.resonance": 12, "filter1.keytrack": 30,
        "env1.attack": 900, "env1.decay": 1500, "env1.sustain": 90, "env1.release": 1900,
        "lfo1.shape": "Sine", "lfo1.rate": 0.05,
        "reverb.on": 1, "reverb.type": "Hall", "reverb.mix": 28, "reverb.decay": 4200, "reverb.size": 70,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 90,
        "voicectl.length": 6,
        "voicectl.s1.pan": -50, "voicectl.s2.pan": 50, "voicectl.s3.pan": -20, "voicectl.s4.pan": 20, "voicectl.s5.pan": -75, "voicectl.s6.pan": 75,
        "voicectl.s1.detune": -8, "voicectl.s2.detune": 8, "voicectl.s3.detune": 4, "voicectl.s4.detune": -4, "voicectl.s5.detune": 12, "voicectl.s6.detune": -12,
        "voicectl.s1.cutoff": 0, "voicectl.s2.cutoff": -18, "voicectl.s3.cutoff": 10, "voicectl.s4.cutoff": -8, "voicectl.s5.cutoff": 16, "voicectl.s6.cutoff": -24,
    },
    [
        R("lfo1", "osc1.gran.position", 48),                                               # the play head walks the progression
        pressure(38), R("poly_aftertouch", "osc1.gran.rand_offset", 12),
        R("velocity", "filter1.cutoff", 14),
        R("mod_wheel", "osc1.gran.length", 6), R("mod_wheel", "osc1.gran.rand_pitch", 10),
        R("macro1", "osc1.gran.position", 30),                                             # Section
        R("macro2", "osc1.gran.density", -2.3), R("macro2", "osc1.gran.rand_level", 60),     # Thin Out (26 grains a second down to 3, every grain at its own level)
        R("macro3", "osc1.gran.length", -4),                                               # Short Grains (180 ms down to 20)
        R("macro4", "osc1.gran.rand_offset", 40), R("macro4", "osc1.gran.rand_pan", 20), R("macro4", "osc1.gran.rand_pitch", 25),   # Scatter
        R("macro5", "filter1.cutoff", 40),                                                 # Bright
        R("macro6", "reverb.mix", 35), R("macro6", "reverb.decay", 20),                    # Space
        R("macro7", "lfo1.rate", 50),                                                      # Drift
    ],
    ["Section", "Thin Out", "Short Grains", "Scatter", "Bright", "Space", "Drift", ""],
    content=C(samples=[SMP(1, "chord_pad_sections")]))

add("Vowel Scrub Choir", "Lead", "lead,choir,granular,vowel,scrub,mod-wheel,plate,convolve",
    "A choir you scrub with the mod wheel: the Granular oscillator holds a voice that sings A, E, I, O, U in five seconds and the wheel is its play head, so the vowel follows your hand and holds wherever you stop; a formant table layered on top moves with it. Plate impulse response, vibrato that arrives late",
    {
        "master.polyphony": 4,
        "osc1.on": 1, "osc1.type": "Granular", "osc1.level": 58, "osc1.sample.root": 48, "osc1.sample.loop_mode": "Forward loop", "osc1.sample.normalize": 1,
        "osc1.gran.manual": 1, "osc1.gran.position": 0, "osc1.gran.density": 58, "osc1.gran.length": 95, "osc1.gran.window_shape": "Hann", "osc1.gran.window_amount": 100,
        "osc1.gran.rand_offset": 2, "osc1.gran.rand_pan": 40, "osc1.gran.max_grains": 40,
        "osc1.unison.voices": 2, "osc1.unison.detune": 5, "osc1.unison.spread": 60,
        "osc2.on": 1, "osc2.wavetable": "Preset table", "osc2.frame": 0, "osc2.level": 30, "osc2.unison.voices": 3, "osc2.unison.detune": 8, "osc2.unison.spread": 70,
        "filter1.on": 1, "filter1.model": "Digital", "filter1.style": 0, "filter1.cutoff": 2800, "filter1.resonance": 8, "filter1.keytrack": 40,
        "env1.attack": 180, "env1.decay": 500, "env1.sustain": 95, "env1.release": 700,
        "lfo1.shape": "Sine", "lfo1.rate": 5.0, "lfo1.fade_in": 900,
        "chorus.on": 1, "chorus.mix": 16, "chorus.rate": 0.3,
        "convolve.on": 1, "convolve.ir": "Preset IR", "convolve.mix": 30, "convolve.ir_gain": 0,
    },
    [
        R("mod_wheel", "osc1.gran.position", 100), R("mod_wheel", "osc2.frame", 100),     # the wheel is the vowel
        R("lfo1", "osc1.tune", 5), R("lfo1", "osc2.tune", 5),
        pressure(34), R("poly_aftertouch", "osc1.gran.rand_pitch", 6),
        R("velocity", "filter1.cutoff", 14),
        R("macro1", "osc1.gran.position", 40), R("macro1", "osc2.frame", 40),             # Vowel
        R("macro2", "osc1.gran.rand_offset", 40), R("macro2", "osc1.gran.rand_pan", 30), R("macro2", "osc1.gran.rand_pitch", 8),   # Cloud
        R("macro3", "osc1.gran.length", -1.5),                                             # Short Grains
        R("macro4", "osc2.level", 40),                                                     # Table Layer
        R("macro5", "convolve.mix", 45), R("macro5", "chorus.mix", 25),                    # Space
        R("macro6", "filter1.cutoff", 45),                                                 # Bright
    ],
    ["Vowel", "Cloud", "Short Grains", "Table Layer", "Space", "Bright", "", ""],
    content=C(wavetables=[WT(2, "formant_sweep_aeiou")], samples=[SMP(1, "voice_sweep_aeiou")], impulses=[IMP("plate")]))

add("Frozen Pluck Drone", "Pad", "pad,drone,granular,freeze,bus,plate,convolve,pluck",
    "A plucked string held still: the play head is parked on the first hundred milliseconds of the pluck, so the Granular oscillator sustains one instant of it for as long as the key is down; a second copy runs on bus 1 through the plate impulse response alone (wet only), the dry copy stays in front",
    {
        "master.polyphony": 6,
        "osc1.on": 1, "osc1.type": "Granular", "osc1.level": 66, "osc1.sample.root": 60, "osc1.sample.loop_mode": "Forward loop", "osc1.sample.normalize": 1, "osc1.bus1_send": 55,
        "osc1.gran.manual": 1, "osc1.gran.position": 3, "osc1.gran.density": 42, "osc1.gran.length": 110, "osc1.gran.window_shape": "Gaussian", "osc1.gran.window_amount": 85,
        "osc1.gran.rand_offset": 3, "osc1.gran.rand_pan": 55, "osc1.gran.max_grains": 48,
        "osc1.unison.voices": 3, "osc1.unison.detune": 8, "osc1.unison.spread": 75,
        "osc2.on": 1, "osc2.wavetable": "Triangle", "osc2.level": 14, "osc2.transpose": -12, "osc2.unison.voices": 2, "osc2.unison.detune": 6,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 2000, "filter1.resonance": 12, "filter1.keytrack": 40,
        "env1.attack": 700, "env1.decay": 1500, "env1.sustain": 92, "env1.release": 1800,
        "rlfo1.style": "Perlin", "rlfo1.rate": 0.2,
        "bus1.level": -2, "bus1.destination": "Main",
        "convolve.on": 1, "convolve.rack": "Bus 1", "convolve.ir": "Preset IR", "convolve.mix": 100, "convolve.ir_gain": 0,
        "chorus.on": 1, "chorus.mix": 20, "chorus.rate": 0.25,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 100,
    },
    [
        R("rlfo1", "osc1.gran.position", 5),                                               # the held instant wanders a little
        pressure(40), R("poly_aftertouch", "osc1.gran.density", 6),
        R("velocity", "filter1.cutoff", 16),
        R("mod_wheel", "osc1.gran.position", 14), R("mod_wheel", "bus1.level", 8),
        R("macro1", "osc1.gran.position", 18),                                             # Freeze Point
        R("macro2", "osc1.gran.rand_offset", 35), R("macro2", "osc1.gran.rand_pan", 25),   # Cloud
        R("macro3", "osc1.gran.length", -2.5),                                             # Short Grains
        R("macro4", "bus1.level", 14),                                                     # Plate Bus
        R("macro5", "osc1.gran.rand_pitch", 14),                                           # Pitch Scatter
        R("macro6", "filter1.cutoff", 40),                                                 # Bright
    ],
    ["Freeze Point", "Cloud", "Short Grains", "Plate Bus", "Pitch Scatter", "Bright", "", ""],
    content=C(samples=[SMP(1, "pluck_string")], impulses=[IMP("plate")]))

add("Gong Grain Swarm", "FX", "fx,granular,gong,swarm,metal,bus,random,texture",
    "The gong shattered into a swarm: sixty grains a second of the metal read in real time, each one with its own pitch (up to a fifth either way), pan and level, some of them backwards; the swarm goes through a second bus with its own delay and reverb so the dry cloud stays close and the echo cloud floats",
    {
        "master.polyphony": 4,
        "osc1.on": 1, "osc1.type": "Granular", "osc1.level": 66, "osc1.sample.root": 36, "osc1.sample.loop_mode": "Forward loop", "osc1.sample.normalize": 1, "osc1.bus2_send": 60,
        "osc1.gran.manual": 0, "osc1.gran.density": 60, "osc1.gran.length": 70, "osc1.gran.window_shape": "Tukey", "osc1.gran.window_amount": 60,
        "osc1.gran.rand_pitch": 7, "osc1.gran.rand_pan": 100, "osc1.gran.rand_level": 35, "osc1.gran.rand_dir": 30, "osc1.gran.rand_offset": 25, "osc1.gran.max_grains": 64,
        "osc1.unison.voices": 2, "osc1.unison.detune": 8, "osc1.unison.spread": 90,
        "osc2.on": 1, "osc2.wavetable": "Bell Partials", "osc2.frame": 60, "osc2.level": 12, "osc2.transpose": 0, "osc2.unison.voices": 3, "osc2.unison.detune": 14, "osc2.unison.spread": 90,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 7500, "filter1.resonance": 10, "filter1.keytrack": 20,
        "env1.attack": 500, "env1.decay": 1500, "env1.sustain": 85, "env1.release": 2200,
        "lfo1.shape": "Sine", "lfo1.rate": 0.12,
        "bus2.level": -3, "bus2.destination": "Main",
        "delay.on": 1, "delay.rack": "Bus 2", "delay.sync": 2, "delay.division": 6, "delay.mix": 45, "delay.feedback": 55, "delay.style": "Ping pong",
        "reverb.on": 1, "reverb.rack": "Bus 2", "reverb.type": "Nitrous", "reverb.mix": 55, "reverb.decay": 6000, "reverb.size": 80,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 120,
    },
    [
        R("lfo1", "filter1.cutoff", 20), R("lfo1", "osc1.gran.rand_pan", 10),
        pressure(36), R("poly_aftertouch", "osc1.gran.rand_pitch", 20),
        R("velocity", "filter1.cutoff", 12),
        R("mod_wheel", "osc1.gran.density", 6), R("mod_wheel", "bus2.level", 8),
        R("macro1", "osc1.gran.density", -4.5),                                             # Sparse (60 grains a second down to 15)
        R("macro2", "osc1.gran.rand_pitch", 45),                                           # Pitch Chaos
        R("macro3", "osc1.gran.rand_offset", 100), R("macro3", "osc1.gran.rand_level", 70), R("macro3", "osc1.gran.rand_dir", 60),   # Scatter
        R("macro4", "osc1.gran.length", -1.65),                                            # Short Grains
        R("macro5", "bus2.level", 12),                                                     # Echo Cloud
        R("macro6", "filter1.cutoff", -40),                                                # Dark
    ],
    ["Sparse", "Pitch Chaos", "Scatter", "Short Grains", "Echo Cloud", "Dark", "", ""],
    content=C(samples=[SMP(1, "gong_metal")]))

add("Stutter Grain Kit", "Pluck", "pluck,rhythm,granular,stutter,drums,tempo-sync,note-position",
    "A drum loop played as a stutter: the grains start on the sixteenth notes of the host tempo and are a thirty-second long, and the key you press chooses where in the two bars the stutter sits (the play head follows the key: about a quarter of the way in at C2, three quarters at C7), pitched like a sampler",
    {
        "master.polyphony": 6,
        "osc1.on": 1, "osc1.type": "Granular", "osc1.level": 72, "osc1.sample.root": 60, "osc1.sample.loop_mode": "Forward loop", "osc1.sample.normalize": 1,
        "osc1.gran.manual": 1, "osc1.gran.position": 0, "osc1.gran.density_mode": "BPM Sync", "osc1.gran.density_sync": 12, "osc1.gran.length_mode": "BPM Sync", "osc1.gran.length_sync": 15,
        "osc1.gran.window_shape": "Exp Dec Rev", "osc1.gran.window_amount": 55, "osc1.gran.rand_offset": 0, "osc1.gran.rand_pan": 20, "osc1.gran.max_grains": 24,
        "osc1.unison.voices": 2, "osc1.unison.detune": 4, "osc1.unison.spread": 40,
        "osc2.on": 1, "osc2.wavetable": "Sine", "osc2.level": 12, "osc2.transpose": -24,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 11000, "filter1.resonance": 14,
        "env1.attack": 0, "env1.decay": 600, "env1.sustain": 70, "env1.release": 90,
        "delay.on": 1, "delay.sync": 2, "delay.division": 6, "delay.mix": 14, "delay.feedback": 30,
        "distortion.on": 1, "distortion.type": "Tube", "distortion.drive": 3, "distortion.mix": 40,
    },
    [
        R("note", "osc1.gran.position", 100),                                               # the key chooses the place in the loop
        R("velocity", "filter1.cutoff", 14), R("velocity", "distortion.drive", 25),
        R("mod_wheel", "osc1.gran.rand_offset", 22), R("mod_wheel", "osc1.gran.rand_pitch", 12),
        R("macro1", "osc1.gran.position", 30),                                             # Nudge
        R("macro2", "osc1.gran.rand_offset", 25),                                          # Scatter
        R("macro3", "osc1.gran.window_amount", 40),                                        # Snap
        R("macro4", "osc1.gran.rand_pitch", 60),                                           # Pitch Rand
        R("macro5", "filter1.cutoff", -55),                                                # Dark
        R("macro6", "delay.mix", 40),                                                      # Echo
    ],
    ["Nudge", "Scatter", "Snap", "Pitch Rand", "Dark", "Echo", "", ""],
    content=C(samples=[SMP(1, "drum_loop_120bpm")]))

add("Rising Grain Riser", "FX", "fx,riser,granular,build,swell,bus,shimmer,sweep",
    "A six second build: the Granular oscillator reads the rising texture in real time (the grains are a hundred and fifty milliseconds, sixty a second), a filter climbs, a delay and reverb on bus 2 throw the grains after it; hold the key to the top, let go for the tail. Play C2 for the plain build, other keys transpose it",
    {
        "master.polyphony": 2,
        "osc1.on": 1, "osc1.type": "Granular", "osc1.level": 74, "osc1.sample.root": 36, "osc1.sample.loop_mode": "One-shot", "osc1.sample.normalize": 1, "osc1.bus2_send": 45,
        "osc1.gran.manual": 0, "osc1.gran.density": 60, "osc1.gran.length": 150, "osc1.gran.window_shape": "Hann", "osc1.gran.window_amount": 80,
        "osc1.gran.rand_offset": 3, "osc1.gran.rand_pan": 60, "osc1.gran.rand_pitch": 0, "osc1.gran.max_grains": 48,
        "osc1.unison.voices": 2, "osc1.unison.detune": 10, "osc1.unison.spread": 85,
        "osc2.on": 1, "osc2.wavetable": "Saw", "osc2.level": 16, "osc2.transpose": 0, "osc2.unison.voices": 3, "osc2.unison.detune": 20, "osc2.unison.spread": 90,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 1, "filter1.cutoff": 500, "filter1.resonance": 18, "filter1.keytrack": 0,
        "env1.attack": 120, "env1.decay": 6000, "env1.sustain": 100, "env1.release": 1800,
        "env2.attack": 5600, "env2.decay": 400, "env2.sustain": 100,
        "bus2.level": -3, "bus2.destination": "Main",
        "delay.on": 1, "delay.rack": "Bus 2", "delay.sync": 2, "delay.division": 6, "delay.mix": 40, "delay.feedback": 50, "delay.style": "Ping pong",
        "reverb.on": 1, "reverb.rack": "Bus 2", "reverb.type": "Hall", "reverb.mix": 45, "reverb.decay": 5000,
    },
    [
        R("env2", "filter1.cutoff", 58), R("env2", "osc1.gran.rand_pitch", 10),
        R("velocity", "filter1.cutoff", 10),
        R("mod_wheel", "bus2.level", 8), R("mod_wheel", "osc1.gran.density", 5),
        R("macro1", "osc1.sample.scan", -40),                                              # Slow
        R("macro2", "osc1.gran.density", -4.5), R("macro2", "osc1.gran.rand_level", 60),   # Sparse
        R("macro3", "osc1.gran.rand_pitch", 40),                                           # Scatter
        R("macro4", "bus2.level", 12),                                                     # Throw
        R("macro5", "filter1.resonance", 60),                                              # Edge
        R("macro6", "osc1.gran.length", -3.5),                                             # Short Grains
    ],
    ["Slow", "Sparse", "Scatter", "Throw", "Edge", "Short Grains", "", ""],
    content=C(samples=[SMP(1, "rising_texture")]))
