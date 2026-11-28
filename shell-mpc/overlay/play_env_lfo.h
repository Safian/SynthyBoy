// SynthyBoy - native widget ownership for the compact Play ENV and LFO panels.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../native_play_env_lfo.h"
#include "env_ui.h"
#include "lfo_ui.h"

namespace synthyboy_mpc::overlay
{
namespace playenvlfo = nativeui::playenvlfo;

class PlayEnvLfo
{
public:
    static ui::Rect rect (const EnvUi& env, const LfoUi& lfo, int field) noexcept
    {
        (void) env;
        return lfo.compactNativeRect (field);
    }

    bool take (IInstanceView& v, UiStack& stack, const EnvUi& env, LfoUi& lfo)
    {
        namespace n = playenvlfo;
        const auto state = current (v, stack, env, lfo);
        if (state.context.view == n::Hidden)
            return false;
        float selection;
        if (v.takeNativePlayEnvLfo (state, n::Choose, selection))
        {
            const int choice = 1 + static_cast<int> (std::lround (selection * n::kLastChoice));
            if (choice != state.context.lfo)
            {
                uibind::chooseLfo (v, choice);
                return true;
            }
        }
        bool changed = false;
        constexpr int inputOrder[] { n::Sync, n::StandardSync, n::Shape, n::Rate, n::Division, n::Style, n::Stereo };
        static_assert (std::size (inputOrder) + 1 == n::Count);   // + Choose
        for (int field : inputOrder)
        {
            float value;
            if (! v.takeNativePlayEnvLfo (state, field, value))
                continue;
            if (lfo.applyCompactNative (v, stack, field, value))
            {
                changed = true;
                // The active layout or panel may have changed. Take any
                // remaining fields against the updated binding on the next tick.
                if (field == n::Sync || field == n::StandardSync)
                    return true;
            }
        }
        return changed;
    }

    void publish (IInstanceView& v, const UiStack& stack, EnvUi& env, LfoUi& lfo)
    {
        namespace n = playenvlfo;
        if (owner_ != nullptr && owner_ != &v)
            leave();
        state_ = current (v, stack, env, lfo);
        lfo.setCompactNativeParts (state_);
        if (v.nativePlayEnvLfoControls())
            v.setNativePlayEnvLfo (state_);
        owner_ = state_.context.view != n::Hidden ? &v : nullptr;
    }

    void setPopup (int action) noexcept
    {
        if (owner_ != nullptr)
            owner_->setNativePlayEnvLfoPopup (playenvlfo::popupField (action));
    }

    void leave()
    {
        if (owner_ != nullptr)
            owner_->suspendNativePlayEnvLfo();
        owner_ = nullptr;
        state_ = {};
    }

    bool coversNative (const EnvUi& env, const LfoUi& lfo, int x, int y) const noexcept
    {
        for (int field = 0; field < playenvlfo::Count; ++field)
            if (state_.fields[field].target >= 0 && rect (env, lfo, field).contains (x, y))
                return true;
        return false;
    }

    void drawApertures (gpu::SceneBuilder& sb, const EnvUi& env, const LfoUi& lfo) const
    {
        for (int field = 0; field < playenvlfo::Count; ++field)
            if (field != playenvlfo::Choose && state_.fields[field].target >= 0)
            {
                const auto r = rect (env, lfo, field);
                const gpu::ScopedClip clip (sb, r.x, r.y, r.right(), r.bottom());
                if (clip)
                    sb.clear (0u);
            }
    }

private:
    static playenvlfo::State current (const IInstanceView& v, const UiStack& stack, const EnvUi& env, const LfoUi& lfo)
    {
        namespace n = playenvlfo;
        static_assert (n::kStandardLfos == uibind::kSelLfos && n::kSources == uibind::kSelLfos + uibind::kSelRnds);
        if (! v.nativePlayEnvLfoControls() || ! stack.isMain() || &ui::theme::L() != &ui::theme::kLook1)
            return {};
        if (uibind::selectedLfo (v) != lfo.selection() + (lfo.random() ? uibind::kRndBase : 0) || env.selection() != uibind::selectedEnv (v))
            return {};
        return compactPresentation (v, lfo);
    }

public:
    static playenvlfo::State compactPresentation (const IInstanceView& v, const LfoUi& lfo)
    {
        namespace n = playenvlfo;
        n::State state;
        const bool random = lfo.random();
        const auto mode = lfo.compact().rate.mode();
        const auto view = static_cast<n::View> ((random ? n::RndHz : n::LfoHz) + static_cast<int> (mode));
        const int lfoId = uibind::selectedLfo (v);
        const int envId = uibind::selectedEnv (v);
        state.context = { view, envId, lfoId, v.stateGeneration(), v.nativePlayEnvLfoRevision() };
        state.fields[n::Choose] = { n::Choose, lfoId, nativeui::kFxDial };
        auto bind = [&] (int field)
        {
            const int parameter = lfo.compactNativeTarget (field);
            if (parameter >= 0)
                state.fields[field] = { parameter, lfoId, nativeui::kFxDial };
        };
        if (mode == ui::lfo::SyncMode::Hz)
            bind (n::Rate);
        else if (mode == ui::lfo::SyncMode::Bpm)
            bind (n::Division);
        if (random)
        {
            bind (n::Sync);
            bind (n::Style);
            bind (n::Stereo);
        }
        else
        {
            bind (n::StandardSync);
            bind (n::Shape);
        }
        return state;
    }

private:
    IInstanceView* owner_ = nullptr;
    playenvlfo::State state_;
};
}
