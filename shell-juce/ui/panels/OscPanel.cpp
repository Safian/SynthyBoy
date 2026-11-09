// SynthyBoy - UI panels: Oscillators 1-3 (docs/ui-design/02-oscillators.md, page `osc`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../Panel.h"
#include "../model/RatioSource.h"
#include "OscWaveView.h"
#include "RemapEditor.h"
#include "TransposeControl.h"

namespace synthyboy::ui
{
template <int OscIndex>
class OscPanelBase : public Panel
{
public:
    explicit OscPanelBase (UiContext& context)
        : Panel (context),
          waveView (context, OscIndex),
          onHandle (context, "osc" + std::to_string (OscIndex) + ".on")
    {
        addAndMakeVisible (waveView);
        specFilter = std::make_unique<RemapEditor> (context, RemapEditor::SpectralFilterMode {}, OscIndex);
        addChildComponent (*specFilter); // visible for the Spectral type
        onHandle.onChange = [this]
        {
            updateDimming();
            repaint();
            waveView.repaint();
        };
        // the swipe on the view (two fingers) goes to the next / previous oscillator, like the chips
        waveView.onSwitchOscillator = [this] (int direction)
        {
            ctx.view->oscTab = juce::jlimit (0, 2, ctx.view->oscTab + direction);
            if (onHeightChanged)
                onHeightChanged();
        };
        updateDimming();
    }

    /** Only the oscillator chosen on the chip row takes part in the layout. */
    bool hiddenInLayout() const override { return ctx.view->oscTab != OscIndex - 1; }
    std::string modulePrefix() const override { return "osc" + std::to_string (OscIndex); }
    OscWaveView& view() noexcept { return waveView; }
    /** The spectral filter graph (GROUPC WP-12), visible while the oscillator is a Spectral one. */
    RemapEditor& spectralFilter() noexcept { return *specFilter; }

    /** Power dot off: the whole panel dims (design 02 section 3; the controls stay editable). */
    void updateDimming()
    {
        setAlpha (onHandle.valid() && onHandle.normalised() < 0.5f ? 0.4f : 1.0f); // normalised (): the listener runs before the atomic updates
    }

    juce::String title() const override
    {
        return "Oscillator " + juce::String (OscIndex);
    }

    /** Every key of the oscillator panel, 4 primary controls first next to the visual (brief principle 1).
        All 64 keys of oscN in docs/params.json are reachable. */
    std::vector<std::string> keys() const override
    {
        const std::string o = "osc" + std::to_string (OscIndex);
        return {
            // Primary (<= 4 next to the visual, 02-oscillators.md §2.1)
            o + ".frame", o + ".warp.amount", o + ".morph.amount", o + ".level",

            // Behind More (60 controls: unison, pitch, phase, sample, warp 2, mapping, routing)
            o + ".on", o + ".type", o + ".wavetable", o + ".pan", o + ".transpose", o + ".tune",
            o + ".phase", o + ".random_phase", o + ".destination", o + ".filter_blend",
            o + ".bus1_send", o + ".bus2_send",
            o + ".unison.voices", o + ".unison.detune", o + ".unison.detune_curve",
            o + ".unison.spread", o + ".unison.blend", o + ".unison.stack", o + ".frame_spread",
            o + ".warp.type", o + ".warp.phase", o + ".warp.spread",
            o + ".warp2.type", o + ".warp2.amount", o + ".warp2.phase", o + ".warp2.spread",
            o + ".morph.type", o + ".morph.spread",
            o + ".map.key_low", o + ".map.key_high", o + ".map.vel_low", o + ".map.vel_high",
            o + ".map.key_fold", o + ".map.key_warp",
            o + ".sample.start", o + ".sample.end", o + ".sample.root", o + ".sample.keytrack",
            o + ".sample.loop_mode", o + ".sample.loop_start", o + ".sample.loop_end",
            o + ".sample.loop_xfade", o + ".sample.loop_relative", o + ".sample.loop_link",
            o + ".sample.loop_exit", o + ".sample.fade_edges", o + ".sample.normalize",
            o + ".sample.reverse", o + ".sample.slice_mode", o + ".sample.slice_threshold",
            o + ".sample.slice_play", o + ".sample.slice_root", o + ".sample.scan",
            o + ".sample.scan_range", o + ".sample.scan_reverse", o + ".sample.scan_lock_tempo",
            o + ".sample.scan_to_bpm", o + ".sample.length_beats", o + ".sample.uni_start",
            o + ".sample.uni_span",
            // Block B14: Pitch & unison modes, transpose snap, phase legato (76 keys total)
            o + ".pitch_mode", o + ".pitch_coarse", o + ".pitch_track", o + ".fixed_pitch",
            o + ".pitch_bend_track", o + ".transpose_snap",
            o + ".unison.mode", o + ".unison.range", o + ".unison.blend_shape", o + ".unison.stack_repeat",
            o + ".phase_legato", o + ".phase_memory",
            // Block B27 (GROUPB WP-05): Ratio SRC, smooth interpolation of the frame position, the slice destination of Play Single Slice
            o + ".ratio_src", o + ".smooth_interp", o + ".sample.slice",
            // Block B28 (GROUPC WP-11): the granular oscillator
            o + ".gran.window_amount", o + ".gran.window_skew", o + ".gran.window_shape", o + ".gran.density_mode",
            o + ".gran.density", o + ".gran.density_sync", o + ".gran.density_grains", o + ".gran.length_mode",
            o + ".gran.length", o + ".gran.length_sync", o + ".gran.length_percent", o + ".gran.jump_start",
            o + ".gran.max_grains", o + ".gran.loop_grains", o + ".gran.manual", o + ".gran.position",
            o + ".gran.scan_keytrack", o + ".gran.rand_offset", o + ".gran.rand_dir", o + ".gran.reverse_grains",
            o + ".gran.rand_pitch", o + ".gran.rand_length", o + ".gran.rand_pan", o + ".gran.rand_level",
            o + ".gran.rand_window", o + ".gran.rand_skew", o + ".gran.rand_warp", o + ".gran.rand_warp2",
            o + ".gran.spawn_pattern", o + ".gran.y_axis", o + ".gran.xy_y",
            // Block B29 (GROUPC WP-12): the spectral oscillator
            o + ".spec.freq_lo", o + ".spec.freq_hi", o + ".spec.lohi_smooth", o + ".spec.lohi_post_warp",
            o + ".spec.cut", o + ".spec.mix", o + ".spec.freq_shift", o + ".spec.manual",
            o + ".spec.position", o + ".spec.scan_keytrack", o + ".spec.phase_lock", o + ".spec.transients",
            o + ".spec.y_axis", o + ".spec.xy_y",
            // Block B30 (GROUPC WP-13): the multisample oscillator
            o + ".multi.override", o + ".multi.delay", o + ".multi.attack", o + ".multi.hold",
            o + ".multi.decay", o + ".multi.sustain", o + ".multi.release", o + ".multi.vel_track",
            o + ".multi.vel_override", o + ".multi.timbre"
        };
    }

    void build() override
    {
        const auto all = keys();
        for (size_t i = 0; i < all.size(); ++i)
        {
            if (i < 4)
                addControl (all[i]);
            else if (all[i] == "osc" + std::to_string (OscIndex) + ".transpose")
                addMoreControl (std::make_unique<TransposeControl> (ctx, all[i])); // OCT segments + SEM wheel
            else if (all[i] == "osc" + std::to_string (OscIndex) + ".transpose_snap")
                addMoreControl (std::make_unique<TransposeSnapControl> (ctx, all[i])); // Row 02-10 Transpose snap keyboard
            else if (auto& control = addMoreControl (all[i]);
                     all[i] == "osc" + std::to_string (OscIndex) + ".warp.type" || all[i] == "osc" + std::to_string (OscIndex) + ".warp2.type")
            {
                if (auto* choice = dynamic_cast<ParamChoice*> (&control))
                {
                    choice->setGroups (warpTypeGroups()); // Basic / Alt / Filter / Distortion / FM / PD / AM / RM (Serum 2 manual p.49-56)
                    choice->setExtraAction ("Swap Warps", [this] { swapWarps(); });
                }
            }
            else if (all[i] == "osc" + std::to_string (OscIndex) + ".ratio_src")
            {
                // the oscillator itself and a source that would close a loop are greyed (the engine would play the note of the voice instead)
                if (auto* choice = dynamic_cast<ParamChoice*> (&control))
                    choice->setEntryFilter ([this] (int value) { return ratioSourceAllowed (OscIndex, value, ratioSources()); });
            }
        }

        // what the Serum 2 manual ties to other controls (p.35-36, p.47): the fixed pitch is the pitch of a wavetable oscillator with pitch tracking off,
        // the pitch bend switch is for the sample types with pitch tracking off, the stack repeat (1-3x) belongs to the Octaves / Power Chord stacks
        const std::string o = "osc" + std::to_string (OscIndex);
        for (const char* suffix : { ".pitch_track", ".type", ".unison.stack", ".warp.type", ".warp2.type", ".pitch_mode", ".sample.slice_mode", ".sample.slice_play",
                                    ".gran.density_mode", ".gran.length_mode", ".gran.manual", ".spec.manual", ".multi.override", ".multi.vel_override" })
        {
            auto handle = std::make_unique<ParamHandle> (ctx, o + suffix);
            handle->onChange = [this] { updateControlDimming(); };
            dimHandles.push_back (std::move (handle));
        }
        updateControlDimming();
    }

    void updateControlDimming()
    {
        const auto choiceOf = [] (ParamHandle& h)
        {
            // (read the parameter itself: the raw atomic of the APVTS is updated after the callbacks)
            return h.valid() ? juce::roundToInt (h.info().min + h.normalised() * (h.info().max - h.info().min)) : 0;
        };
        if (dimHandles.size() < 14)
            return;
        const bool tracking = choiceOf (*dimHandles[0]) != 0;
        const int type = choiceOf (*dimHandles[1]);
        const int stack = choiceOf (*dimHandles[2]);
        const std::string o = "osc" + std::to_string (OscIndex);
        // Sync Var (warp mode 11): the phase knob of the slot is the VAR fader of the manual (p.50: hard sync .. very soft sync)
        const int warpType = choiceOf (*dimHandles[3]);
        const int warp2Type = choiceOf (*dimHandles[4]);
        const int pitchMode = choiceOf (*dimHandles[5]), sliceMode = choiceOf (*dimHandles[6]), slicePlay = choiceOf (*dimHandles[7]);
        const int densityMode = choiceOf (*dimHandles[8]), lengthMode = choiceOf (*dimHandles[9]);
        const bool granManual = choiceOf (*dimHandles[10]) != 0, granular = type == kOscTypeGranular, sampleClass = type >= kOscTypeSample && type <= kOscTypeSpectral;
        const bool specManual = choiceOf (*dimHandles[11]) != 0, spectral = type == kOscTypeSpectral;
        const bool multiOverride = choiceOf (*dimHandles[12]) != 0, multiVelOverride = choiceOf (*dimHandles[13]) != 0, multisample = type == kOscTypeMultisample;
        if (specFilter != nullptr && specFilter->isVisible() != spectral)
        {
            specFilter->setVisible (spectral);
            if (onHeightChanged)
                onHeightChanged();
        }
        for (const auto& c : controls())
        {
            const auto& key = c->key();
            bool applies = true;
            if (key == o + ".fixed_pitch")
                applies = ! tracking && type == 0;
            else if (key == o + ".pitch_bend_track")
                applies = ! tracking && sampleClass;
            else if (key == o + ".unison.stack_repeat")
                applies = stack == 3 || stack == 5;
            else if (key == o + ".ratio_src")
                applies = pitchMode == 2;                                  // only the Ratio pitch mode reads a source (Serum 2 manual p.30)
            else if (key == o + ".smooth_interp")
                applies = type == 0;                                       // the frame position of a wavetable
            else if (key == o + ".sample.slice")
                applies = sampleClass && sliceMode != 0 && slicePlay == 1; // Play Single Slice only
            else if (key.rfind (o + ".multi.", 0) == 0)
            {
                // the multisample keys apply to the Multisample type; the envelope only while OVERRIDE is on, the velocity tracking only with its override (manual p.61 - 62)
                applies = multisample;
                for (const char* tail : { "delay", "attack", "hold", "decay", "sustain", "release" })
                    if (key == o + ".multi." + tail)
                        applies = applies && multiOverride;
                if (key == o + ".multi.vel_track")
                    applies = applies && multiVelOverride;
            }
            else if (key.rfind (o + ".spec.", 0) == 0)
                applies = spectral && (key != o + ".spec.position" || specManual);   // the spectral keys apply to the Spectral type; the position only in the manual mode
            else if (key.rfind (o + ".gran.", 0) == 0)
            {
                // the granular keys apply to the Granular type; of the rate and length triples only the one the mode reads (manual p.101 - 102)
                applies = granular;
                if (key == o + ".gran.density")                 applies = applies && densityMode == 0;
                else if (key == o + ".gran.density_sync")       applies = applies && densityMode == 1;
                else if (key == o + ".gran.density_grains")     applies = applies && densityMode == 2;
                else if (key == o + ".gran.length")             applies = applies && lengthMode == 0;
                else if (key == o + ".gran.length_sync")        applies = applies && lengthMode == 1;
                else if (key == o + ".gran.length_percent")     applies = applies && lengthMode == 2 && densityMode != 2;
                else if (key == o + ".gran.position")           applies = applies && granManual;
            }
            c->setAlpha (c->isAvailable() && applies ? 1.0f : 0.4f);
            const bool first = key == o + ".warp.phase", second = key == o + ".warp2.phase";
            if (first || second)
            {
                if (phaseLabel.isEmpty())
                    phaseLabel = c->label();
                const bool var = (first ? warpType : warp2Type) == kWarpSyncVar;
                const juce::String wanted = var ? juce::String ("Var") : phaseLabel;
                if (c->label() != wanted)
                    c->setLabel (wanted);
            }
        }
    }

    /** The three `oscN.ratio_src` values (0 Base note, 1 .. 3 Osc 1 .. 3, 4 Sub), for the entry filter. */
    std::array<int, 3> ratioSources() const
    {
        std::array<int, 3> sources { 0, 0, 0 };
        for (int n = 1; n <= 3; ++n)
        {
            ParamHandle handle (ctx, "osc" + std::to_string (n) + ".ratio_src");
            if (handle.valid())
                sources[static_cast<size_t> (n - 1)] = juce::roundToInt (handle.plain());
        }
        return sources;
    }

    /** Swap Warps (Serum 2 manual p.56): the warp 1 mode, amount, phase and spread change places with the warp 2 values. */
    void swapWarps()
    {
        const std::string o = "osc" + std::to_string (OscIndex);
        for (const char* what : { "type", "amount", "phase", "spread" })
        {
            ParamHandle first (ctx, o + ".warp." + what), second (ctx, o + ".warp2." + what);
            if (! first.valid() || ! second.valid())
                continue;
            const float a = first.plain(), b = second.plain();
            first.setPlainComplete (b);
            second.setPlainComplete (a);
        }
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const int visualH = t.ipx (180.0f);
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        const int filterH = specFilter != nullptr && specFilter->isVisible() ? specFilter->preferredHeight (width - 2 * t.gap()) + t.gap() : 0;
        return headerHeight() + visualH + 3 * t.gap() + filterH + const_cast<OscPanelBase*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        const auto& t = ctx.theme;
        const int visualH = t.ipx (180.0f);
        auto area = contentArea();
        auto topArea = area.removeFromTop (visualH);
        waveView.setBounds (topArea);
        area.removeFromTop (t.gap());
        if (specFilter != nullptr && specFilter->isVisible())
        {
            specFilter->setBounds (area.removeFromTop (specFilter->preferredHeight (area.getWidth())));
            area.removeFromTop (t.gap());
        }
        layoutControls (area, false);
    }

    void paint (juce::Graphics& g) override
    {
        Panel::paint (g);
        if (onHandle.valid() && onHandle.plain() < 0.5f)
        {
            const auto& t = ctx.theme;
            g.setFont (t.labelFont());
            g.setColour (t.colours.warn);
            g.drawText ("[MUTED / OFF]",
                        getLocalBounds().reduced (t.gap(), 0).withTrimmedRight (t.touch()).withHeight (headerHeight() + t.gap() / 2),
                        juce::Justification::centredRight, false);
        }
    }

private:
    static constexpr int kWarpSyncVar = 11; // oscN.warp.type entry "Sync Var"
    juce::String phaseLabel;
    OscWaveView waveView;
    std::unique_ptr<RemapEditor> specFilter;
    ParamHandle onHandle;
    std::vector<std::unique_ptr<ParamHandle>> dimHandles;
};

class Osc1Panel final : public OscPanelBase<1> { using OscPanelBase<1>::OscPanelBase; };
class Osc2Panel final : public OscPanelBase<2> { using OscPanelBase<2>::OscPanelBase; };
class Osc3Panel final : public OscPanelBase<3> { using OscPanelBase<3>::OscPanelBase; };

SYNTHYBOY_REGISTER_PANEL (Osc1Panel, Page::osc, 10)
SYNTHYBOY_REGISTER_PANEL (Osc2Panel, Page::osc, 20)
SYNTHYBOY_REGISTER_PANEL (Osc3Panel, Page::osc, 30)
} // namespace synthyboy::ui
