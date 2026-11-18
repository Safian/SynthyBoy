// SynthyBoy - MPC overlay: IInstanceView, one plugin instance as the overlay and the plugin-drawn pages of SynthyBoy see it. The generic half is mpckit::IInstance (kit/instance.h); this class adds what is SynthyBoy's: the
// live values of the engine, the content of the state, the spectrum ring, the Play selectors, the content browser, the pad pressure layer, the macro names. Split from curve_source.h (migration step 7 of
// docs/ui-components/TEMPLATE.md): the filter curve (CurveSource) stays there.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../kit/instance.h"
#include "../kit/synthyboy_params.h"
#include "../spectrum.h"
#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/readout.h"
#include "synthyboy/state.h"
#include "synthyboy/wavetable_catalogue.h"

namespace synthyboy_mpc { namespace overlay {

/** One plugin instance as the overlay sees it: the generic half is mpckit::IInstance (kit/instance.h: param, writeParam, sampleRate, bpm, setQGroup, setQVirtual, takeQVirtual, qWriteCount, qStatus, setUiFullOpen,
    requestOverlayReport: the same signatures as before, documented there), this class adds what is SynthyBoy's (live values, content, spectrum, the Play selectors, pad pressure, ...). All methods are called from the overlay
    thread while the instance is registered; they must be lock free. */
class IInstanceView : public mpckit::IInstance
{
public:
    /** The parameter table of SynthyBoy as the kit's IParamSource (the same object for every instance). */
    const mpckit::IParamSource& params() const noexcept override { return mpckit::synthyboy_adapter::Params::get(); }
    /** The page-facing generic writes keep their old defaults (do nothing) for the instances that have no host to tell (tools, tests): the kit's interface has them pure. */
    void writeParam (int /*tableIndex*/, float /*normalised*/) noexcept override {}
    void setQGroup (const int* /*targets*/, int /*n*/) noexcept override {}
    /** The kit's opaque live block is SynthyBoy's LiveValues. */
    bool readLive (void* out, size_t bytes) const noexcept override { return out != nullptr && bytes == sizeof (synthyboy::LiveValues) && live (*static_cast<synthyboy::LiveValues*> (out)); }
    /** The live values of the engine (readout of the targets the overlay subscribed, the LFO phases, the envelope stages ...). false: the engine has not written any yet. */
    virtual bool live (synthyboy::LiveValues& out) const noexcept = 0;
    /** true while a wavetable the current program names is still being built (the sound is the table the oscillator had until then): the plugin-drawn pages show a thin amber bar at the top of the tab. */
    virtual bool tablesLoading() const noexcept { return false; }
    /** The content of the state (custom LFO shapes, user wavetables, remap curves): a snapshot that stays valid while the pointer is held; changes when the state changes. */
    virtual std::shared_ptr<const synthyboy::StateContent> content() const { return nullptr; }
    virtual const synthyboy::IContentStore* contentStore() const noexcept { return nullptr; }
    /** The ring of the instance's output (the audio thread writes it, the spectrum view reads it). null: this instance has none (the build without the spectrum): the spectrum view has no data
        and the plane stays off, the 32 meters of the skin stay visible. */
    virtual const SpectrumRing* spectrumRing() const noexcept { return nullptr; }
    /** The instance the Play page shows in panel `group` (0 oscillator 1 .. 5: Sub, A, B, C, Noise (play_osc.h), 1 envelope 1 .. 6, 2 LFO 1 .. 10): the plugin parameters "Play Osc", "Play Env", "Play LFO". 1 when the instance has none. */
    virtual int playSelection (int /*group*/) const noexcept { return 1; }
    /** The plugin-drawn compact view sets the oscillator selector of the Play page (1 .. 5: Sub, A, B, C, Noise) like the host's own widget (saved in the chunk, the proxies and the host follow). */
    virtual void setPlaySelection (int /*group*/, int /*instance*/) noexcept {}
    /** Which of the Play page's envelope / LFO selectors was touched last: 0 none yet, 1 envelope, 2 LFO (the modulation source the plugin-drawn pages treat as "selected"). */
    virtual int playLastSelector() const noexcept { return 0; }
    /** The places the content browser of the oscillator UI starts from (label, absolute folder): the plugin offers the ones that exist on this machine (the SD card, its own content folder, the USB drives).
        Fills `out` (cleared first). Empty: this instance cannot load content. */
    struct ContentRoot { std::string label, path; };
    virtual void contentRoots (std::vector<ContentRoot>& out) const { out.clear(); }
    /** Asks the instance to load a file into oscillator `osc` (1 .. 3): kind 1 = a WAV sample (the sample of the granular oscillator), kind 2 = an instrument for the multisample oscillator (an SFZ file, or a
        WAV that becomes one zone over the keyboard). The load happens off the overlay thread (the plugin's display thread: reading, decoding, the content store, the engine); the result is the
        content () of a later moment and `contentLoad`. Called from the overlay thread; it only stores the request. */
    virtual void requestContent (int /*osc*/, int /*kind*/, const std::string& /*path*/) {}
    /** The state of the last load: 0 none yet, 1 loading, 2 done, 3 failed (`message` gets the reason when it is not null). */
    virtual int contentLoad (std::string* /*message*/) const { return 0; }
    /** The wavetable browser (overlay/wavetable_browser.h): where the catalogue looks for tables, the installed packs and the roots that may hold SynthyBoy/Wavetables (wavetable_catalogue.h). Called from the overlay thread when the
        browser opens. Empty (the default): the browser lists the built-in tables only. */
    virtual void wavetableSources (synthyboy::CatalogueSources& out) const { out = synthyboy::CatalogueSources {}; }
    /** Asks the instance to load the wavetable FILE `ref` (pack, file; the SHA-256 is computed by the instance) as the table of oscillator `osc` (1 .. 3): the display thread reads and checks the file, puts the reference into the content,
        gives it to the engine and then sets oscN.wavetable to "Preset table"; the old table plays until the new one is built. The latest request wins. The result is `contentLoad` (3 = refused, with "not a wavetable file: <reason>") and
        content () of a later moment. Called from the overlay thread; it only stores the request. */
    virtual void requestWavetable (int /*osc*/, const synthyboy::ContentFileRef& /*ref*/) {}
    /** The steps the Q knob of oscillator `osc` made while its table is a file (-1 / +1 summed since the last call; the Q knob of the stepper does not move the parameter then): the overlay loads the previous / next file of the
        category for them. Lock free, 0 when there are none. */
    virtual int takeWavetableSteps(int /*osc*/) noexcept { return 0; }
    /** The plugin-drawn LFO page writes the DRAWN shape of LFO `lfo` (1 .. 10) into the content of the state (a shape with 2 .. 128 points, the loop point, the smooth flag): a request, applied by the plugin's display
        thread (the engine gets the new content, the chunk keeps it, content () shows it a moment later); a newer request for the same LFO replaces an older one that was not applied yet. Called from the overlay
        thread; it only stores the request. */
    virtual void setLfoShape (int /*lfo*/, const synthyboy::ContentLfoShape& /*shape*/) {}
    /** The plugin-wide pad pressure layer (shell parameters "Pad Pressure" and "Pad Pressure Amount", docs/MPC.md "Pad pressure"): fills `on` and `percent` (0 .. 100) and returns true, false when this instance has none
        (the Master page then shows the two controls disabled). Lock free. */
    virtual bool padPressure (bool& /*on*/, float& /*percent*/) const noexcept { return false; }
    /** The Master page sets the layer (the instance applies it to its settings and to the engine, the chunk keeps it). Lock free. */
    virtual void setPadPressure (bool /*on*/, float /*percent*/) noexcept {}
    /** The output limiter of the plugin (Master page, OUTPUT block, "LIMIT"; docs/MPC.md "Output limiter"): fills `on` with the state the output stage runs and returns true, false when this
        instance has none (the toggle is then disabled). Lock free. */
    virtual bool outputLimiter (bool& /*on*/) const noexcept { return false; }
    /** The Master page switches the limiter (the instance applies it to its output stage, the chunk keeps it). Lock free. */
    virtual void setOutputLimiter (bool /*on*/) noexcept {}
    /** The plugin-drawn Macros page names macro `macro` (0 .. 7): `name` is cleaned and cut (synthyboy::sanitizeMacroName), "" is the default name. A request, applied by the plugin's display thread
        (the state's content, the chunk and content () show it a moment later; the host is told the names of the Q proxies changed); a newer request for the same macro replaces an older one that was
        not applied yet. Called from the overlay thread; it only stores the request. */
    virtual void setMacroName (int /*macro*/, const std::string& /*name*/) {}
    /** Appends the voice and held key diagnostics of the instance to the status file (voices_active, voices_released, voices_sustaining, held_keys, held_lowest, held_highest, orphan_voices,
        note_ons, note_offs, note_offs_unmatched, voices_stolen, sustain_pedal, sound_off_requests): atomics the audio thread updates, no lock; nothing when the engine has none. */
    virtual void voiceStatus (std::string& /*out*/) const {}
    /** The kit's diagnostics of an instance are SynthyBoy's voice status. */
    void diagnostics (std::string& out) const override { voiceStatus (out); }
    /** The content the sound of this instance needs and does not have (IEngine::missingContent () bits; 0 = nothing missing). The oscillator page shows "content missing: ..." from it (the text is the page's). Lock free. */
    virtual unsigned missingContent() const noexcept { return 0; }
};

}} // namespace synthyboy_mpc::overlay
