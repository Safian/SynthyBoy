/*
 * SynthyBoy - CLAP extension "synthyboy.content": the user content library (WP-01 g, docs/ui-design/13-new-keys.md 4.25).
 * Copyright (C) 2026 Safian Szabolcs
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A CLAP host has no file dialog for a plugin and the native shell has no GUI, so the host (Kick) brings the file: it asks the plugin to import a file
 * by PATH, lists what the library holds, attaches an item to an oscillator or the Convolve impulse response, and removes items. The plugin keeps the
 * files in `<user dir>/content/` (the same library the JUCE shell uses) and the references in the `content` section of its state, so a saved project
 * finds them again (pack + file + SHA-256).
 *
 * Main thread only. Strings are UTF-8. The shell decodes WAV files itself (it links no audio file library): Kick converts other formats to WAV before
 * the call (or passes the original path of a WAV).
 */
#pragma once

#include <clap/clap.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTHYBOY_EXT_CONTENT "synthyboy.content"

/** Kind of an item. */
enum
{
    SYNTHYBOY_CONTENT_WAVETABLE = 0, /* a WAV whose length is a multiple of 2048 samples = that many frames, anything else one resampled cycle; .vitaltable */
    SYNTHYBOY_CONTENT_SAMPLE = 1,    /* a WAV; the root note comes from the file name ("Piano Eb3.wav") or the WAV instrument / sampler chunk */
    SYNTHYBOY_CONTENT_IMPULSE = 2    /* a WAV impulse response for the Convolve effect */
};

/** Result codes. */
enum
{
    SYNTHYBOY_CONTENT_OK = 0,
    SYNTHYBOY_CONTENT_ERR_ARGUMENT = 1, /* bad kind / index / oscillator / null pointer */
    SYNTHYBOY_CONTENT_ERR_READ = 2,     /* the file cannot be read */
    SYNTHYBOY_CONTENT_ERR_FORMAT = 3,   /* not a file of that kind */
    SYNTHYBOY_CONTENT_ERR_STORE = 4,    /* the library cannot be written / the item does not exist */
    SYNTHYBOY_CONTENT_ERR_STATE = 5,    /* the oscillator / patch is not in a state the action needs (wrong oscillator type, no sample, silent, no note yet) */
    SYNTHYBOY_CONTENT_ERR_RENDER = 6    /* the engine cannot render offline */
};

typedef struct synthyboy_content_item
{
    uint32_t kind;
    char name[128];
    char pack[48];
    char file[192];
    char sha256[65];
    uint64_t size_bytes;
    uint32_t embedded; /* 1: saved by "embed", not listed by the editor */
} synthyboy_content_item_t;

typedef struct synthyboy_plugin_content
{
    /* Number of items of a kind the library holds (wavetables include the tables of imported Vital presets). */
    uint32_t (*count) (const clap_plugin_t* plugin, uint32_t kind);
    /* Item `index` (0 .. count-1, ascending by name). */
    int32_t (*get) (const clap_plugin_t* plugin, uint32_t kind, uint32_t index, synthyboy_content_item_t* item);
    /* Copies the file at `path` into the library (content addressed: the same file is stored once) and returns the item. A `.vitaltable` file is
       converted to a table. Does not touch the sound: attach () does. */
    int32_t (*import_file) (const clap_plugin_t* plugin, uint32_t kind, const char* path, synthyboy_content_item_t* item);
    /* Uses an item: wavetable -> osc 1..3 (the state gets the reference; osc<N>.wavetable stays as it is: set it to "Preset table"), sample ->
       osc 1..3 (osc<N>.sample.root is set from the file when it names a note), impulse -> the Convolve IR (osc ignored). The engine is updated at once
       and the host is told the state is dirty. */
    int32_t (*attach) (const clap_plugin_t* plugin, uint32_t kind, uint32_t index, uint32_t osc);
    /* Deletes the item from the library. References to it in the state turn into missing files (the engine falls back as documented). */
    int32_t (*remove) (const clap_plugin_t* plugin, uint32_t kind, uint32_t index);
    /* Imports a Vital `.vital` preset (the sound is loaded and the host re-reads the parameters) or a `.vitalbank` (every preset is written to
       `<user dir>/presets/<bank>/`). `summary` (if not null) receives a one line result. */
    int32_t (*import_vital) (const clap_plugin_t* plugin, const char* path, char* summary, uint32_t summary_capacity);

    /* ---- GROUPD (WP-14 / 15 / 16, docs/ui-design/13-new-keys.md 4.40). Appended: a client that knows the older table does not use them. All run on the main thread
       and may take long (an offline render at the quality of the high tier; "Use Ultra quality when rendering" of the preferences is honoured). `summary` (may be null) gets a
       one line result. ---- */

    /* Switch to Wavetable (Sample, Granular, Spectral oscillator `osc` 1..3): the START .. END region of its sample becomes the oscillator's wavetable. import_mode: 0 Constant
       framesize (Pitch Average), 1 Dynamic pitch zero snap, 2 Dynamic pitch follow, 3 Frequency estimation, 4..7 FFT 256 .. 2048, 8 Single cycle. oscN.type becomes Wavetable. */
    int32_t (*switch_to_wavetable) (const clap_plugin_t* plugin, uint32_t osc, uint32_t import_mode, char* summary, uint32_t summary_capacity);
    /* Render OSC Warp of wavetable oscillator `osc` 1..3: 256 frames across WARP 0..100 %, stored and attached (WARP type None, Morph None, Remap off, WT POS = the warp amount). */
    int32_t (*render_osc_warp) (const clap_plugin_t* plugin, uint32_t osc, char* summary, uint32_t summary_capacity);
    /* Resample: one bar of the last played note (C3 before any note; tempo of the host transport) becomes the wavetable of `target` 1..3, or of Osc 1 + Osc 2 (target 0: left
       channel to 1, right channel to 2). import_mode as above. */
    int32_t (*resample) (const clap_plugin_t* plugin, uint32_t target, uint32_t import_mode, char* summary, uint32_t summary_capacity);
    /* The last played note (held as long as it was, with its release tail) rendered to a 24-bit stereo WAV file at `path`: the host (Kick) shares it or drags it out. */
    int32_t (*render_last_note) (const clap_plugin_t* plugin, const char* path, char* summary, uint32_t summary_capacity);
    /* Wavetable `osc` frame at WT POS -> shape of LFO `lfo` 1..10 (lfoN.shape = Custom), and LFO `lfo` shape -> the table of oscillator `osc` (replaces the current frame of its own
       table, or becomes a one-frame table). */
    int32_t (*wavetable_frame_to_lfo) (const clap_plugin_t* plugin, uint32_t osc, uint32_t lfo, char* summary, uint32_t summary_capacity);
    int32_t (*lfo_shape_to_wavetable) (const clap_plugin_t* plugin, uint32_t lfo, uint32_t osc, char* summary, uint32_t summary_capacity);
} synthyboy_plugin_content_t;

#ifdef __cplusplus
}
#endif
