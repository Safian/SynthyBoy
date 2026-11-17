# Batch 2, presets built on the factory wavetables. `add`, `R`, `pressure`, `WT`, `SMP`, `IMP`, `INST`, `C` are provided by factory-presets-batch2.py.

add("Reese Dual Phase Bass", "Bass", "bass,reese,dnb,wavetable,convolve,cabinet,mono-bass",
    "A Reese from a table that is two opposite saws whose distance breathes: the cycle itself is the phasing, five unison voices and a second saw an octave down thicken it, a slow LFO walks the table, a tube and the amp cabinet impulse response put it in a speaker; the bottom stays in the middle",
    {
        "master.polyphony": 6,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 30, "osc1.level": 62, "osc1.unison.voices": 5, "osc1.unison.detune": 22, "osc1.unison.spread": 70,
        "osc2.on": 1, "osc2.wavetable": "Saw", "osc2.level": 34, "osc2.transpose": -12, "osc2.tune": 7, "osc2.unison.voices": 3, "osc2.unison.detune": 14,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 1, "filter1.cutoff": 620, "filter1.resonance": 26, "filter1.keytrack": 30,
        "env1.attack": 4, "env1.decay": 800, "env1.sustain": 100, "env1.release": 220,
        "lfo1.shape": "Sine", "lfo1.rate": 0.35,
        "distortion.on": 1, "distortion.type": "Tube", "distortion.drive": 4, "distortion.mix": 55,
        "convolve.on": 1, "convolve.ir": "Preset IR", "convolve.mix": 55, "convolve.ir_gain": 0,
        "utility.on": 1, "utility.mono_bass": 1, "utility.mono_freq": 150, "utility.width": 100,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 32,
    },
    [
        R("lfo1", "osc1.frame", 34),                                                       # the distance of the two saws breathes
        R("velocity", "filter1.cutoff", 18), R("velocity", "distortion.drive", 20),
        R("mod_wheel", "filter1.cutoff", 24), R("mod_wheel", "distortion.drive", 25),
        pressure(36),
        R("macro1", "osc1.frame", 45),                                                     # Phase
        R("macro2", "filter1.cutoff", 40),                                                 # Cutoff
        R("macro3", "distortion.drive", 50), R("macro3", "distortion.mix", 30),            # Growl
        R("macro4", "convolve.mix", -50),                                                  # Dry Cab (0 = the full cabinet)
        R("macro5", "osc2.level", 40),                                                     # Octave Saw
        R("macro6", "lfo1.rate", 60),                                                      # Movement Rate
    ],
    ["Phase", "Cutoff", "Growl", "Dry Cab", "Octave Saw", "Movement Rate", "", ""],
    content=C(wavetables=[WT(1, "pwm_dual_phase")], impulses=[IMP("cabinet_colour")]))

add("Vowel Table Lead", "Lead", "lead,vowel,formant,wavetable,legato,mod-wheel,expressive",
    "A solo voice from a table of five vowels: the wheel walks A, E, I, O, U on the frame axis, pressure on the pad opens the mouth and brings in a vibrato, velocity decides how bright the attack is; three unison voices make a small section, the legato mono line glides",
    {
        "master.polyphony": 1, "master.legato": 1, "master.portamento.time": 60,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 0, "osc1.level": 66, "osc1.unison.voices": 3, "osc1.unison.detune": 7, "osc1.unison.spread": 60,
        "osc2.on": 1, "osc2.wavetable": "Triangle", "osc2.level": 20, "osc2.transpose": -12,
        "filter1.on": 1, "filter1.model": "Digital", "filter1.style": 0, "filter1.cutoff": 6500, "filter1.resonance": 10, "filter1.keytrack": 55,
        "env1.attack": 50, "env1.decay": 400, "env1.sustain": 95, "env1.release": 260,
        "lfo1.shape": "Sine", "lfo1.rate": 5.2, "lfo1.fade_in": 600, "lfo2.shape": "Sine", "lfo2.rate": 5.2, "lfo2.fade_in": 500,
        "delay.on": 1, "delay.sync": 2, "delay.division": 5, "delay.mix": 14, "delay.feedback": 30,
        "reverb.on": 1, "reverb.type": "Plate", "reverb.mix": 20, "reverb.decay": 2000,
    },
    [
        R("mod_wheel", "osc1.frame", 100),                                                 # the wheel is the vowel
        R("lfo1", "osc1.tune", 5),
        R("lfo2", "osc1.tune", 10, aux="poly_aftertouch"),                                 # a vibrato that the pressure brings
        pressure(34), R("poly_aftertouch", "osc1.frame", 12),
        R("velocity", "filter1.cutoff", 18), R("velocity", "osc1.frame", 10),
        R("macro1", "osc1.frame", 100),                                                    # Vowel
        R("macro2", "filter1.cutoff", -48),                                                # Dark
        R("macro3", "osc1.unison.detune", 60),                                             # Choir
        R("macro4", "osc2.level", 55),                                                     # Octave Down
        R("macro5", "lfo1.rate", 40),                                                      # Vibrato Rate
        R("macro6", "reverb.mix", 30), R("macro6", "delay.mix", 20),                       # Space
    ],
    ["Vowel", "Dark", "Choir", "Octave Down", "Vibrato Rate", "Space", "", ""],
    content=C(wavetables=[WT(1, "formant_sweep_aeiou")]))

add("Drawbar Organ Keys", "Keys", "keys,organ,drawbars,tonewheel,rotary,bus,overdrive,percussion",
    "A tonewheel organ from a table of sixteen drawbar registrations (the table holds the 16 foot fundamental, so the oscillator is set an octave up): the frame is the registration, a key click and a fast second-harmonic percussion sit on the front of every note, the rotary is a chorus and a phaser, and a second copy overdrives on bus 1",
    {
        "master.polyphony": 10,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 20, "osc1.level": 70, "osc1.transpose": 12, "osc1.bus1_send": 35,
        "osc2.on": 1, "osc2.wavetable": "Sine", "osc2.level": 0, "osc2.transpose": 19,
        "noise.on": 1, "noise.level": 0, "noise.sample": "White Noise", "noise.destination": "Filter 1",
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 3600, "filter1.resonance": 8, "filter1.keytrack": 40,
        "env1.attack": 3, "env1.decay": 600, "env1.sustain": 100, "env1.release": 90,
        "env2.attack": 0, "env2.decay": 160, "env2.sustain": 0,
        "env3.attack": 0, "env3.decay": 14, "env3.sustain": 0,
        "lfo1.shape": "Sine", "lfo1.rate": 6.2,
        "bus1.level": -9, "bus1.destination": "Main",
        "distortion.on": 1, "distortion.rack": "Bus 1", "distortion.type": "Tube", "distortion.drive": 14, "distortion.mix": 90,
        "chorus.on": 1, "chorus.mix": 35, "chorus.rate": 0.8, "chorus.depth": 60,
        "phaser.on": 1, "phaser.mix": 22, "phaser.rate": 0.8, "phaser.depth": 40,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 70,
    },
    [
        R("env2", "osc2.level", 26),                                                       # the percussion: a decaying second harmonic
        R("env3", "noise.level", 14),                                                      # key click
        R("lfo1", "osc1.tune", 2, aux="mod_wheel"),                                        # the wheel is the rotary's vibrato
        R("mod_wheel", "chorus.mix", 30), R("mod_wheel", "phaser.mix", 30),
        R("velocity", "filter1.cutoff", 10), R("velocity", "osc1.frame", 14),
        R("macro1", "osc1.frame", 70),                                                     # Registration
        R("macro2", "osc2.level", 40),                                                     # Percussion
        R("macro3", "bus1.level", 12), R("macro3", "distortion.drive", 25),                # Overdrive
        R("macro4", "noise.level", 20),                                                    # Click
        R("macro5", "chorus.rate", 55), R("macro5", "phaser.rate", 45),                    # Rotary Speed
        R("macro6", "filter1.cutoff", 50),                                                 # Bright
    ],
    ["Registration", "Percussion", "Overdrive", "Click", "Rotary Speed", "Bright", "", ""],
    content=C(wavetables=[WT(1, "organ_drawbars_16")]))

add("Throat Singer Drone", "Pad", "pad,drone,overtone,throat,wavetable,scale,mono-ish,meditative",
    "Overtone singing: a drone with one narrow resonance that a slow LFO walks up and down the harmonic series from the fourth to the twentieth partial, so a tune of whistling overtones floats over a steady note; kept in the Phrygian dominant scale on D so that chords of fingers stay consonant",
    {
        "master.polyphony": 3, "master.scale.key": "D", "master.scale.type": "Phrygian Dominant", "master.scale.mode": "Conform", "master.portamento.time": 120,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 30, "osc1.level": 62, "osc1.transpose": -12, "osc1.unison.voices": 2, "osc1.unison.detune": 5, "osc1.unison.spread": 50,
        "osc2.on": 1, "osc2.wavetable": "Sine", "osc2.level": 22, "osc2.transpose": -24,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 2800, "filter1.resonance": 12, "filter1.keytrack": 30,
        "env1.attack": 1500, "env1.decay": 1500, "env1.sustain": 100, "env1.release": 2500,
        "lfo1.shape": "Sine", "lfo1.rate": 0.07,
        "rlfo1.style": "Perlin", "rlfo1.rate": 0.3,
        "reverb.on": 1, "reverb.type": "Basin", "reverb.mix": 34, "reverb.decay": 6000, "reverb.size": 80,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 60,
    },
    [
        R("lfo1", "osc1.frame", 40), R("rlfo1", "osc1.frame", 8),                          # the resonance walks the harmonic series
        pressure(36), R("poly_aftertouch", "osc1.frame", 14),
        R("velocity", "filter1.cutoff", 14),
        R("mod_wheel", "osc1.frame", 25), R("mod_wheel", "lfo1.rate", 30),
        R("macro1", "osc1.frame", 40),                                                     # Overtone
        R("macro2", "lfo1.rate", 60),                                                      # Walk Speed
        R("macro3", "osc2.level", 50),                                                     # Drone
        R("macro4", "filter1.cutoff", 55),                                                 # Bright
        R("macro5", "reverb.mix", 35), R("macro5", "reverb.decay", 20),                    # Space
        R("macro6", "osc1.unison.detune", 60),                                             # Choir
    ],
    ["Overtone", "Walk Speed", "Drone", "Bright", "Space", "Choir", "", ""],
    content=C(wavetables=[WT(1, "overtone_throat_sweep")]))

add("Ladder FM Bass", "Bass", "bass,fm,ratio,wavetable,envelope,mono,sub",
    "An FM bass from the ratio ladder: the table steps through modulator ratios 1 to 8 (eight frames each, the index rising inside every step), an envelope throws the frame up at the front of the note so a hard key lands on a higher ratio and settles back into the fundamental, a sine sub keeps it big",
    {
        "master.polyphony": 4,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 3, "osc1.level": 66,
        "sub.on": 1, "sub.waveform": "Sine", "sub.octave": -1, "sub.level": 40,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 1500, "filter1.resonance": 14, "filter1.keytrack": 30,
        "env1.attack": 0, "env1.decay": 700, "env1.sustain": 70, "env1.release": 160,
        "env2.attack": 0, "env2.decay": 260, "env2.sustain": 0,
        "distortion.on": 1, "distortion.type": "Soft clip", "distortion.drive": 3, "distortion.mix": 50,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 30,
    },
    [
        R("env2", "osc1.frame", 22),                                                       # the ratio is thrown up at the front of the note
        R("velocity", "osc1.frame", 18), R("velocity", "filter1.cutoff", 16),
        R("mod_wheel", "osc1.frame", 30), R("mod_wheel", "distortion.drive", 30),
        pressure(30),
        R("macro1", "osc1.frame", 55),                                                     # Ratio
        R("macro2", "env2.decay", 40),                                                     # Snap Length
        R("macro3", "distortion.drive", 50),                                               # Drive
        R("macro4", "sub.level", 40),                                                      # Sub
        R("macro5", "filter1.cutoff", 50),                                                 # Cutoff
        R("macro6", "env1.decay", 35),                                                     # Decay
    ],
    ["Ratio", "Snap Length", "Drive", "Sub", "Cutoff", "Decay", "", ""],
    content=C(wavetables=[WT(1, "fm_ratio_ladder")]))
