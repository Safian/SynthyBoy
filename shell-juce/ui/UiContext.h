// SynthyBoy - UI: the shared context every control and panel receives, and the modulation assign controller.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Theme.h"
#include "model/FxKeySet.h"
#include "LiveBus.h"
#include "UiHost.h"
#include "model/ModModel.h"

namespace synthyboy::ui
{
/** ModMatrixAccess on the APVTS: reads the raw atomics, writes with setValueNotifyingHost, wraps every write in a host gesture
    (begin / end) unless the caller holds one open. Message thread. */
class ApvtsModMatrix final : public ModMatrixAccess
{
public:
    explicit ApvtsModMatrix (juce::AudioProcessorValueTreeState& apvts);

    float get (int paramIndex) const override;
    void set (int paramIndex, float plain) override;
    void beginGesture (int paramIndex) override;
    void endGesture (int paramIndex) override;

private:
    juce::RangedAudioParameter* parameterAt (int index) const;

    std::vector<juce::RangedAudioParameter*> parameters;
    std::vector<std::atomic<float>*> raw;
    std::vector<char> inGesture;
};

/** Owns the assign flow state machine (touch modulation) for one editor and tells the knobs when to redraw: the armed source and
    the used slots change what every modulatable control looks like. Message thread. */
class ModAssignController final : public juce::ChangeBroadcaster
{
public:
    explicit ModAssignController (UiHost& host);

    AssignFlow& flow() noexcept { return assignFlow; }
    ApvtsModMatrix& matrix() noexcept { return access; }
    const ModSnapshot& snapshot() const noexcept { return current; }

    /** Source chip tapped (choice value 1..38). Returns the new phase. A chip whose cap bits the engine lacks is refused. */
    AssignFlow::Phase tapSource (int sourceChoice);
    void exitAssignMode();
    /** Re-reads the 64 slots; broadcasts if anything changed (called by the editor timer, host automation can move a slot). */
    void refresh();

    /** One-line status for the source bar ("Mod Matrix full (64/64 slots used)", ...); empty when nothing to say. */
    const juce::String& message() const noexcept { return statusMessage; }
    void setMessage (const juce::String& m);

    EngineCapsMask engineCaps() const noexcept { return host.engineCaps(); }

private:
    UiHost& host;
    uint32_t lastMidiRevision = 0;
    ApvtsModMatrix access;
    AssignFlow assignFlow;
    ModSnapshot current;
    juce::String statusMessage;
};

class ContentCache;

/** Editor-wide view state that panels share (which tab of a tabbed page is shown). Message thread. */
struct ViewState
{
    int lfoTab = 0; // LfoTabsPanel: 0..9 = LFO 1..10, 10..13 = random LFO 1..4
    int oscTab = 0; // OscTabsPanel: 0..2 = oscillator 1..3
    int envTab = 0; // EnvelopePanel tabs E1..E6: 0..5
    std::shared_ptr<ContentCache> contentCache; // decoded samples / tables of the content library for the views (ContentIO.h), made on first use
};

/** What a control or panel gets: the host, the theme, the assign controller. Lives as long as the editor. */
struct UiContext
{
    UiHost& host;
    Theme& theme;
    ModAssignController& assign;
    std::shared_ptr<ViewState> view = std::make_shared<ViewState>();
    /** Block B22: the panel is built for a slot of the effect instance pool. Every key of the panel is then resolved through this set (FxKeySet), so one panel
        class edits instance 0 (null, the keys as written) and any slot. */
    std::shared_ptr<const FxKeySet> keys = nullptr;
    /** The live values of the engine for the displays (GROUPA2 WP-17); shared by every context copy of the editor. */
    std::shared_ptr<LiveBus> live = std::make_shared<LiveBus> (host);

    juce::AudioProcessorValueTreeState& apvts() const { return host.parameters(); }
    /** The key of a control of a panel: resolveKey () for instance 0, the slot's key and the ParamInfo of the type's key for a pool slot. */
    KeyRef resolve (std::string_view key) const noexcept { return keys != nullptr ? keys->resolve (key) : resolveKey (key); }
};

/** The text of a value as the controls show it: displayText () with the frequency units preference (cutoff readouts in semitones, GROUPA2 WP-02). */
inline std::string valueText (const UiContext& ctx, const ParamInfo& info, float plain) { return displayText (info, plain, ctx.host.preferences().freqSemitones); }

ParamValues readLiveValues (const UiContext& ctx);
void applyValues (const UiContext& ctx, const ParamValues& values);
} // namespace synthyboy::ui
