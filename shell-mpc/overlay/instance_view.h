// SynthyBoy - MPC overlay: IInstanceView, one plugin instance as the overlay and the plugin-drawn pages of SynthyBoy see it. The generic half is mpckit::IInstance (kit/instance.h); this class adds what is SynthyBoy's: the
// live values of the engine, the content of the state, the spectrum ring, the Play selectors, the content browser, the pad pressure layer, the macro names. Split from curve_source.h (migration step 7 of
// docs/ui-components/TEMPLATE.md): the filter curve (CurveSource) stays there.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../kit/instance.h"
#include "../kit/synthyboy_params.h"
#include "../native_fx_cells.h"
#include "../native_fx_header.h"
#include "../native_fx_chain.h"
#include "../native_play_env_lfo.h"
#include "../spectrum.h"
#include "../ui/clip_hold.h"
#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/readout.h"
#include "synthyboy/state.h"
#include "synthyboy/wavetable_catalogue.h"

#include "../native_lfo_controls.h"
#include "../native_lfo_editor.h"
#include "../native_master_controls.h"
#include "../native_osc_controls.h"
#include "../native_play_osc.h"
#include "../native_voice_controls.h"

namespace synthyboy { namespace own { struct ContentSet; } }
namespace synthyboy_mpc::nativeui::lfodetail { struct State; }
namespace synthyboy_mpc::nativeui::lfopick { struct State; }
namespace synthyboy_mpc::nativeui::oscdetail { struct State; }
namespace synthyboy_mpc::nativeui::targets { struct State; }
namespace synthyboy_mpc::nativeui::expandedplay { struct State; }

namespace synthyboy_mpc { namespace overlay {

struct OscContentSnapshot
{
    std::shared_ptr<const synthyboy::StateContent> state;
    std::shared_ptr<const synthyboy::own::ContentSet> prepared;
};

/** One plugin instance as the overlay sees it: the generic half is mpckit::IInstance (kit/instance.h: param, writeParam, sampleRate, bpm, setQGroup, setQVirtual, takeQVirtual, qWriteCount, qStatus, setUiFullOpen,
    requestOverlayReport: the same signatures as before, documented there), this class adds what is SynthyBoy's (live values, content, spectrum, the Play selectors, pad pressure, ...). All methods are called from the overlay
    thread while the instance is registered; they must be lock free. */
class IInstanceView : public mpckit::IInstance
{
public:
    /** Monotonic marker for a successful preset/chunk state load. UI pages use
        it to reset transient selection without mistaking ordinary knob edits
        for a new program. */
    virtual uint64_t stateGeneration() const noexcept { return 0; }
    /** The parameter table of SynthyBoy as the kit's IParamSource (the same object for every instance). */
    const mpckit::IParamSource& params() const noexcept override { return mpckit::synthyboy_adapter::Params::get(); }
    /** The page-facing generic writes keep their old defaults (do nothing) for the instances that have no host to tell (tools, tests): the kit's interface has them pure. */
    void writeParam (int /*tableIndex*/, float /*normalised*/) noexcept override {}
    void setQGroup (const int* /*targets*/, int /*n*/) noexcept override {}
    /** The kit's opaque live block is SynthyBoy's LiveValues. */
    bool readLive (void* out, size_t bytes) const noexcept override { return out != nullptr && bytes == sizeof (synthyboy::LiveValues) && live (*static_cast<synthyboy::LiveValues*> (out)); }
    /** The live values of the engine (readout of the targets the overlay subscribed, the LFO phases, the envelope stages ...). false: the engine has not written any yet. */
    virtual bool live (synthyboy::LiveValues& out) const noexcept = 0;
    /** True if the master stage safety knee was hit since the last poll (IEngine::takeClipFlag). Cleared on read. */
    virtual bool takeClipFlag() noexcept { return false; }
    /** The timed warning belongs to this instance. Only the overlay thread updates it. */
    bool pollMasterClip (double nowMs) noexcept
    {
        masterClipHold_.update (takeClipFlag(), nowMs);
        if (takeNativeMasterClipClear())
            masterClipHold_.clear();
        return masterClipHold_.active (nowMs);
    }
    void clearMasterClip() noexcept { masterClipHold_.clear(); }
    virtual bool takeNativeMasterClipClear() noexcept { return false; }
    virtual void setNativeMasterOutput (const nativeui::master::OutputReadout&) noexcept {}
    /** The Mixer page is on screen: the engine measures the source strips (LiveValues::stripPeak[0 .. 6]) for the next moments (IEngine::requestMeters). Called every update of that page; lock free. */
    virtual void requestMeters() noexcept {}
    /** Native Mixer widgets receive the same live level and peak hold as the overlay. */
    virtual bool nativeMixerCellsAvailable() const noexcept { return false; }
    virtual void setNativeMixerMeter (int /*strip*/, float /*peak*/, float /*hold*/) noexcept {}
    virtual void setNativeMixerStereoMeter (int /*strip*/, const float* /*sixFields*/) noexcept {}
    /** The native Mixer strips are on screen (and no destination list covers them): the host reads their solo / mute presentation and the page's widgets stay shown. */
    virtual void setNativeMixerActive (bool /*active*/) noexcept {}
    virtual void setNativeMixerStripVisuals (int /*strip*/, int /*soloVisual*/, int /*muteVisual*/) noexcept {}
    virtual void setMixerSolo (int /*strip*/, bool /*solo*/) noexcept {}
    virtual void setMixerMute (int /*strip*/, bool /*mute*/) noexcept {}
    virtual bool mixerSolo (int /*strip*/) const noexcept { return false; }
    virtual bool mixerMute (int /*strip*/) const noexcept { return false; }
    /** One display frame per visible standalone Env selector cell. */
    virtual void setNativeEnvSelector (const int* /*frames*/) noexcept {}
    virtual void setNativeEnvEditor (bool /*active*/, const nativeui::FxCell* /*fields*/) noexcept {}
    /** Display text for TARGETS. The normalized button action stays independent of the routed destination count. */
    virtual void setNativeEnvTargetCount (int /*count*/) noexcept {}
    virtual bool takeNativeEnvField (int /*field*/, const nativeui::FxCell& /*expected*/, float& /*value*/) noexcept { return false; }
    virtual bool nativeLfoControls() const noexcept { return false; }
    virtual void setNativeLfoState (const nativeui::lfo::ViewState&) noexcept {}
    virtual bool takeNativeLfo (const nativeui::lfo::ViewState&, int /*field*/, float& /*value*/) noexcept { return false; }
    virtual bool nativeLfoShapeEditor() const noexcept { return false; }
    virtual void setNativeLfoEditor (const nativeui::lfoeditor::State&) noexcept {}
    virtual bool takeNativeLfoEditor (const nativeui::lfoeditor::State&, int /*field*/, float& /*value*/) noexcept { return false; }
    virtual bool nativeLfoShapeDetails() const noexcept { return false; }
    virtual void setNativeLfoDetail (const nativeui::lfodetail::State&) noexcept {}
    virtual bool takeNativeLfoDetail (const nativeui::lfodetail::State&, int /*field*/, float& /*value*/) noexcept { return false; }
    /** The host choices of the LFO (native_lfo_pick.h): the plugin publishes the current indexes and takes the index the host applied. */
    virtual bool nativeLfoPick() const noexcept { return false; }
    virtual void setNativeLfoPick (const nativeui::lfopick::State&) noexcept {}
    virtual bool takeNativeLfoPick (const nativeui::lfopick::State&, int /*field*/, float& /*value*/) noexcept { return false; }
    virtual bool nativePlayEnvLfoControls() const noexcept { return false; }
    virtual uint64_t nativePlayEnvLfoRevision() const noexcept { return 0; }
    virtual void setNativePlayEnvLfoPopup (int /*field*/) noexcept {}
    virtual void setNativePlayEnvLfo (const nativeui::playenvlfo::State&) noexcept {}
    virtual void suspendNativePlayEnvLfo() noexcept { setNativePlayEnvLfo ({}); }
    virtual bool takeNativePlayEnvLfo (const nativeui::playenvlfo::State&, int /*field*/, float& /*value*/) noexcept { return false; }
    virtual void setNativeFxCells (int /*layout*/, const nativeui::FxCell* /*cells*/) noexcept {}
    virtual bool takeNativeFxCell (int /*slot*/, const nativeui::FxCell& /*expected*/, float& /*value*/) noexcept { return false; }
    virtual void setNativeFxHeader (const nativeui::fxheader::State&) noexcept {}
    virtual void setNativeFxRackPopup (bool /*active*/) noexcept {}
    virtual bool takeNativeFxHeader (int /*field*/, const nativeui::fxheader::State&, float& /*value*/) noexcept { return false; }
    virtual void setNativeFxChain (const nativeui::fxchain::State&) noexcept {}
    virtual void setNativeFxAddPopup (bool /*active*/) noexcept {}
    virtual bool takeNativeFxChain (int /*field*/, const nativeui::fxchain::State&, float& /*value*/) noexcept { return false; }
    virtual void setNativeFilterControls (int /*view*/, const nativeui::FxCell* /*cells*/, const float* /*curveValues*/, const int* /*indicators*/) noexcept {}
    virtual bool takeNativeFilterSlot (int /*slot*/, const nativeui::FxCell& /*expected*/, float& /*value*/) noexcept { return false; }
    virtual bool nativeMacroTargetsSupported() const noexcept { return false; }
    virtual void publishNativeMacroTargets (bool) noexcept {}
    virtual int takeNativeMacroTarget() noexcept { return -1; }
    virtual bool nativeExpandedPlaySupported() const noexcept { return false; }
    virtual void publishNativeExpandedPlay (const nativeui::expandedplay::State&) noexcept {}
    virtual bool takeNativeExpandedPlayClose (const nativeui::expandedplay::State&) noexcept { return false; }
    virtual bool nativeModTargetsSupported() const noexcept { return false; }
    virtual void publishNativeModTargets (const nativeui::targets::State&) noexcept {}
    virtual void hideNativeModTargets (const void*) noexcept {}
    virtual bool takeNativeModTarget (const nativeui::targets::State&, int, float&) noexcept { return false; }
    virtual bool nativePzToolbarSupported() const noexcept { return false; }
    virtual void setNativePzToolbar (int /*filter*/, int /*availability*/) noexcept {}
    virtual bool takeNativePzAction (int /*filter*/, float& /*value*/) noexcept { return false; }
    virtual void setNativeOscState (const nativeui::osc::ViewState&) noexcept {}
    virtual bool takeNativeOsc (const nativeui::osc::ViewState&, int /*field*/, int& /*choice*/) noexcept { return false; }
    virtual bool nativeOscSettingsSupported() const noexcept { return false; }
    virtual void setNativeOscSettings (int /*view*/, const nativeui::FxCell* /*fields*/) noexcept {}
    virtual bool takeNativeOscSetting (int /*field*/, const nativeui::FxCell&, float& /*value*/) noexcept { return false; }
    virtual void setNativeOscAbcState (int /*warpMask*/, const char* /*sourceLabel*/) {}
    virtual bool nativeOscDetailSupported() const noexcept { return false; }
    virtual void setNativeOscDetail (const nativeui::oscdetail::State&) noexcept {}
    virtual bool takeNativeOscDetail (const nativeui::oscdetail::State&, int, float&) noexcept { return false; }
    virtual void setNativeOscDetailMenu (int) noexcept {}
    virtual void setNativeMasterView (nativeui::master::View /*view*/) noexcept {}
    virtual void leaveNativeMasterView (nativeui::master::View /*returnPanel*/) noexcept { setNativeMasterView (nativeui::master::Hidden); }
    virtual bool masterScaleInputActive() const noexcept { return false; }
    virtual int takeMasterScaleRoot() noexcept { return -1; }
    virtual bool nativeMasterPanelSupported() const noexcept { return false; }
    virtual bool takeNativeMasterPanel (nativeui::master::View /*view*/, float& /*value*/) noexcept { return false; }
    virtual bool nativeVoiceSequenceControls() const noexcept { return false; }
    virtual void setNativeVoiceState (const nativeui::voice::ViewState&) noexcept {}
    virtual bool takeNativeVoice (const nativeui::voice::ViewState&, int, float&) noexcept { return false; }
    virtual void setNativeModMatrix (int /*view*/, const nativeui::FxCell* /*fields*/, const int* /*rows*/) noexcept {}
    virtual bool takeNativeModField (int /*field*/, const nativeui::FxCell& /*expected*/, float& /*value*/) noexcept { return false; }
    virtual void setNativeModNavigation (int /*mode*/, int /*source*/, int /*destination*/, const nativeui::FxCell* /*fields*/) noexcept {}
    virtual bool takeNativeModNavigation (int /*field*/, const nativeui::FxCell& /*expected*/, float& /*value*/) noexcept { return false; }
    virtual void setNativeModDetailNavigation (const nativeui::FxCell* /*fields*/) noexcept {}
    virtual bool takeNativeModDetailNavigation (int /*field*/, const nativeui::FxCell& /*expected*/, float& /*value*/) noexcept { return false; }
    virtual void setNativeModBoard (int /*gate*/, const nativeui::FxCell* /*fields*/) noexcept {}
    virtual bool takeNativeModBoard (int /*field*/, const nativeui::FxCell& /*expected*/, float& /*value*/) noexcept { return false; }
    /** true while a wavetable the current program names is still being built (the sound is the table the oscillator had until then): the plugin-drawn pages show a thin amber bar at the top of the tab. */
    virtual bool tablesLoading() const noexcept { return false; }
    /** The content of the state (custom LFO shapes, user wavetables, remap curves): a snapshot that stays valid while the pointer is held; changes when the state changes. */
    virtual std::shared_ptr<const synthyboy::StateContent> content() const { return nullptr; }
    virtual OscContentSnapshot oscContent() const { return { content(), nullptr }; }
    virtual const synthyboy::IContentStore* contentStore() const noexcept { return nullptr; }
    /** The ring of the instance's output (the audio thread writes it, the spectrum view reads it). null: this instance has none (the build without the spectrum): the spectrum view has no data
        and the plane stays off, the 32 meters of the skin stay visible. */
    virtual const SpectrumRing* spectrumRing() const noexcept { return nullptr; }
    virtual bool nativePlayOscControls() const noexcept { return false; }
    virtual uint64_t playOscSelectionRevision() const noexcept { return 0; }
    virtual void setNativePlayOsc (const nativeui::playoscui::State&, const char* /*sourceName*/) noexcept {}
    virtual void suspendNativePlayOsc() noexcept { setNativePlayOsc ({}, ""); }
    virtual bool takeNativePlayOsc (const nativeui::playoscui::State&, int /*field*/, float& /*value*/) noexcept { return false; }
    virtual void setNativePlayOscPopup (int /*field*/) noexcept {}
    /** The instance the Play page shows in panel `group` (0 oscillator 1 .. 5: Sub, A, B, C, Noise (play_osc.h), 1 envelope 1 .. 6, 2 LFO 1 .. 10): the plugin parameters "Play Osc", "Play Env", "Play LFO". 1 when the instance has none. */
    virtual int playSelection (int /*group*/) const noexcept { return 1; }
    /** The plugin-drawn compact view sets the oscillator selector of the Play page (1 .. 5: Sub, A, B, C, Noise) like the host's own widget (saved in the chunk, the proxies and the host follow). */
    virtual void setPlaySelection (int /*group*/, int /*instance*/) noexcept {}
    /** The chunk-only view state of the merged tabs (`mpcTabView`, no host parameter, no engine parameter; docs/MPC.md "Tab view state"): `what` kTabViewRnd = the random LFO being edited (0 = a standard LFO, 1 .. 4), kTabViewFilter =
        the state of the Filter tab (0 F1, 1 F2, 2 NOTE, 3 VELO). Default 0. A project reload (effSetChunk) restores it, a chunk without it gives 0; a program change leaves it. Lock free. The Env and LFO selections themselves are
        the Play selectors (playSelection (1) / (2)): the tab and the Play page share them. */
    static constexpr int kTabViewRnd = 0, kTabViewFilter = 1, kTabViewFields = 2;
    /** The key (which 1: 0 .. 127) and the velocity (which 0: 1 .. 127) of the last note-on the plugin received, -1 until a note was played: the live x marker of the NOTE / VELO curve editors (x = key / 127, velocity / 127, the
        engine's scale). One atomic per source fed from the MIDI the plugin already gets (effProcessEvents); lock free, no allocation on the MIDI thread. */
    virtual int lastPlayed (int /*which*/) const noexcept { return -1; }
    virtual int tabView (int /*what*/) const noexcept { return 0; }
    virtual void setTabView (int /*what*/, int /*value*/) noexcept {}
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
    /** The plugin-drawn NOTE / VELO curve editor writes the curve of the source `which` (0 velocity, 1 note) into the content of the state (content.curves.velocity / .note: 2 .. 64 points of x, y, power with ascending x; an EMPTY vector
        is the identity): a request, applied by the plugin's display thread exactly like setLfoShape (the engine gets the new content, the chunk keeps it, content () shows it a moment later; a newer request for the same source
        replaces an older one that was not applied yet; a curve the content validation would refuse is not applied). There is no engine parameter for the points; the two legato keys are ordinary parameters (writeParam). Called from the
        overlay thread; it only stores the request. */
    virtual void setScalarCurve (int /*which*/, const std::vector<synthyboy::ContentPoint>& /*points*/) {}
    /** A drawn PZ response for F1 / F2. A queued edit belongs to the preset in which it was made. */
    virtual void setFilterCurve (int /*filter*/, const std::vector<synthyboy::ContentFilterCurvePoint>& /*points*/, uint64_t /*generation*/) {}
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
    virtual void setMacroName (int macro, const std::string& name, uint64_t generation)
    {
        if (stateGeneration() == generation)
            setMacroName (macro, name);
    }
    /** Appends the voice and held key diagnostics of the instance to the status file (voices_active, voices_released, voices_sustaining, held_keys, held_lowest, held_highest, orphan_voices,
        note_ons, note_offs, note_offs_unmatched, voices_stolen, sustain_pedal, sound_off_requests): atomics the audio thread updates, no lock; nothing when the engine has none. */
    virtual void voiceStatus (std::string& /*out*/) const {}
    /** The kit's diagnostics of an instance are SynthyBoy's voice status. */
    void diagnostics (std::string& out) const override { voiceStatus (out); }
    /** The content the sound of this instance needs and does not have (IEngine::missingContent () bits; 0 = nothing missing). The oscillator page shows "content missing: ..." from it (the text is the page's). Lock free. */
    virtual unsigned missingContent() const noexcept { return 0; }

private:
    ui::ClipHold masterClipHold_;
};

}} // namespace synthyboy_mpc::overlay
