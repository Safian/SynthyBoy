# Batch 3, presets built on the Noise wavetables (no earlier preset used any of them). `add`, `R`, `pressure`, `WT`, `C` are provided by factory-presets-batch3.py.
# A frozen noise in a table repeats once per cycle, so it is heard as a pitch of that rate. The two noise presets that should not follow the keys (the wind, the crackle, the breath of
# the flute) switch the pitch tracking of the oscillator off and name the rate of the cycle (osc1.fixed_pitch, a MIDI note): the texture is then the same on every key.

add("Wind Sweep Riser", "FX", "fx,riser,wind,noise,whistle,sweep,wavetable",
    "A gale in a cave: frozen white noise with one narrow band lifted 30 dB, the band climbing from the third to the three hundredth harmonic of a cycle that does not follow the keys (a low cycle, so the whole climb is audible), the swell and the climb are one envelope; the wheel lifts the band by hand, a chorus widens the wind, dotted echoes and a plate are the cave",
    {
        "master.polyphony": 2,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 0, "osc1.level": 66, "osc1.pitch_track": 0, "osc1.fixed_pitch": 24,
        "filter1.on": 1, "filter1.model": "Digital", "filter1.style": 0, "filter1.cutoff": 9000, "filter1.resonance": 18, "filter1.keytrack": 0,
        "env1.attack": 4600, "env1.decay": 900, "env1.sustain": 0, "env1.release": 1500,
        "chorus.on": 1, "chorus.rate": 0.4, "chorus.depth": 60, "chorus.mix": 32,
        "delay.on": 1, "delay.sync": "Tempo dotted", "delay.division": "1/16 note", "delay.feedback": 35, "delay.mix": 18,
        "reverb.on": 1, "reverb.type": "Plate", "reverb.decay": 3200, "reverb.mix": 30,
    },
    [
        R("env1", "osc1.frame", 75),                                                       # the band climbs with the swell
        R("mod_wheel", "osc1.frame", 60),
        R("velocity", "filter1.cutoff", 10),
        R("macro1", "osc1.frame", 80),                                                     # Band Pitch
        R("macro2", "env1.attack", 50),                                                    # Sweep Time
        R("macro3", "filter1.resonance", 40),                                              # Whistle
        R("macro4", "reverb.mix", 30), R("macro4", "delay.mix", 25),                       # Space
        R("macro5", "filter1.cutoff", -50),                                                # Muffle
        R("macro6", "chorus.mix", 40),                                                     # Width
    ],
    ["Band Pitch", "Sweep Time", "Whistle", "Space", "Muffle", "Width", "", ""],
    content=C(wavetables=[WT(1, "Noise/noise_band_sweep")]))

add("Air Chiff Flute", "Lead", "lead,flute,breath,chiff,noise,wavetable,mono,legato,pressure",
    "A breathy flute: a triangle sings the note and a frozen noise through two broad resonances (the chiff and the tube of a flute) breathes under it, a short spit of the noise at the front of every key and a steady breath that the pad pressure and the wheel bring in; mono, legato, with a glide and a late vibrato, dotted echoes and a vintage room",
    {
        "master.polyphony": 1, "master.legato": 1, "master.portamento.time": 40,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 10, "osc1.level": 10, "osc1.pitch_track": 0, "osc1.fixed_pitch": 36,
        "osc2.on": 1, "osc2.wavetable": "Triangle", "osc2.level": 64,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 3500, "filter1.resonance": 10, "filter1.keytrack": 60,
        "env1.attack": 60, "env1.decay": 300, "env1.sustain": 88, "env1.release": 200,
        "env2.attack": 0, "env2.decay": 130, "env2.sustain": 0,
        "lfo1.shape": "Sine", "lfo1.rate": 5.2, "lfo1.fade_in": 700,
        "delay.on": 1, "delay.sync": "Tempo dotted", "delay.division": "1/8 note", "delay.feedback": 25, "delay.mix": 16,
        "reverb.on": 1, "reverb.type": "Vintage", "reverb.decay": 2000, "reverb.mix": 24,
    },
    [
        R("env2", "osc1.level", 40),                                                       # the chiff: a spit of noise at the front of the key
        R("lfo1", "osc2.tune", 5), R("lfo1", "osc2.tune", 4, aux="poly_aftertouch"),
        R("poly_aftertouch", "osc1.level", 22), pressure(30),                              # the pressure is the breath
        R("mod_wheel", "osc1.level", 22), R("mod_wheel", "osc1.frame", 30),
        R("velocity", "osc1.frame", 25), R("velocity", "filter1.cutoff", 14),
        R("macro1", "osc1.frame", 60),                                                     # Breath Tone
        R("macro2", "osc1.level", 45),                                                     # Chiff
        R("macro3", "filter1.cutoff", -35),                                                # Dark
        R("macro4", "delay.mix", 30),                                                      # Echo
        R("macro5", "lfo1.rate", 40),                                                      # Vibrato Rate
        R("macro6", "reverb.mix", 40), R("macro6", "reverb.decay", 20),                    # Room
    ],
    ["Breath Tone", "Chiff", "Dark", "Echo", "Vibrato Rate", "Room", "", ""],
    content=C(wavetables=[WT(1, "Noise/breath_air_bumps")]))

add("Pink Noise Sub", "Bass", "bass,sub,808,noise,pink,wavetable,thump,mono-bass",
    "A sub bass with a noise thump: a frozen pink noise through a resonant low pass whose cutoff falls along the frame axis, an envelope starts it bright and closes it in a few hundred milliseconds, so every note opens with a pitched burst of hiss and settles into the sine sub; soft clip, a high pass under the sub and the bottom kept in the middle",
    {
        "master.polyphony": 4,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 70, "osc1.level": 48,
        "sub.on": 1, "sub.waveform": "Sine", "sub.octave": -1, "sub.level": 52,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 3000, "filter1.resonance": 15, "filter1.keytrack": 30,
        "env1.attack": 2, "env1.decay": 1300, "env1.sustain": 40, "env1.release": 200,
        "env2.attack": 0, "env2.decay": 260, "env2.sustain": 0,
        "distortion.on": 1, "distortion.type": "Soft clip", "distortion.drive": 3, "distortion.mix": 35,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 28,
        "utility.on": 1, "utility.mono_bass": 1, "utility.mono_freq": 150,
    },
    [
        R("env2", "osc1.frame", -65), R("env2", "osc1.level", 30),                          # starts bright and loud, the filter of the table closes
        R("velocity", "osc1.level", 20), R("velocity", "filter1.cutoff", 14),
        R("mod_wheel", "osc1.frame", -35), R("mod_wheel", "distortion.drive", 25),
        pressure(30),
        R("macro1", "osc1.frame", -45),                                                    # Brighten (0 = the dark hum)
        R("macro2", "sub.level", 40),                                                      # Sub Boom
        R("macro3", "env2.decay", 45),                                                     # Thump Length
        R("macro4", "distortion.drive", 50),                                               # Drive
        R("macro5", "filter1.cutoff", 50),                                                 # Cutoff
        R("macro6", "env1.decay", 40),                                                     # Decay
    ],
    ["Brighten", "Sub Boom", "Thump Length", "Drive", "Cutoff", "Decay", "", ""],
    content=C(wavetables=[WT(1, "Noise/pink_lowpass_sweep")]))

add("Vinyl Crackle Bed", "FX", "fx,texture,crackle,vinyl,dust,noise,wavetable,bed",
    "A bed of dust and crackle for the back of a track: the table has three clicks per cycle growing to sixty-four with a short decay tail, and the cycle runs about fourteen times a second whatever key is held, so a few thumps become a crackle and then a rattle as the frame goes up; the noise source adds the sharp surface ticks of a record, a slow drift wanders along the density, a low sine is the hum of the motor, a dark filter and a basin keep it in the back",
    {
        "master.polyphony": 2,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 10, "osc1.level": 55, "osc1.pitch_track": 0, "osc1.fixed_pitch": 9,
        "osc2.on": 1, "osc2.wavetable": "Sine", "osc2.level": 10, "osc2.transpose": -24,
        "noise.on": 1, "noise.sample": "Vinyl Clicks", "noise.level": 45, "noise.destination": "Filter 1",
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 3500, "filter1.resonance": 10, "filter1.keytrack": 0,
        "env1.attack": 700, "env1.decay": 1000, "env1.sustain": 100, "env1.release": 1500,
        "rlfo1.style": "Perlin", "rlfo1.rate": 0.25,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 90, "eq.high.mode": "Shelf", "eq.high.cutoff": 4000, "eq.high.gain": -6,
        "reverb.on": 1, "reverb.type": "Basin", "reverb.decay": 3000, "reverb.mix": 25,
    },
    [
        R("rlfo1", "osc1.frame", 20),                                                      # the density wanders
        pressure(30), R("poly_aftertouch", "osc1.frame", 15),
        R("mod_wheel", "osc1.frame", 45),
        R("velocity", "osc1.level", 12),
        R("macro1", "osc1.frame", 75),                                                     # Density
        R("macro2", "filter1.cutoff", -50),                                                # Dark
        R("macro3", "osc2.level", 40),                                                     # Hum
        R("macro4", "reverb.mix", 30), R("macro4", "reverb.decay", 20),                    # Space
        R("macro5", "rlfo1.rate", 55),                                                     # Drift Rate
        R("macro6", "noise.level", 40),                                                    # Surface Ticks
    ],
    ["Density", "Dark", "Hum", "Space", "Drift Rate", "Surface Ticks", "", ""],
    content=C(wavetables=[WT(1, "Noise/crackle_pulses")]))
