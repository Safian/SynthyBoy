// MpcKit - SynthyBoy's implementation of the kit interfaces: a thin layer that FORWARDS to the code SynthyBoy already has (its parameter table, IInstanceView, the generated overlay layout). It
// exists so that the generic pages and runtime pieces of the kit can run on SynthyBoy while the existing files are migrated one by one; every
// value it returns is the value the old direct call returned. NOT part of the kit proper: a plugin without SynthyBoy's engine never includes this file.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstring>
#include <vector>

#include "synthyboy/params.h"

#include "../overlay/instance_view.h"
#include "../overlay/overlay_layout.h"
#include "instance.h"
#include "page_set.h"
#include "param_source.h"
#include "synthyboy_params.h"

namespace mpckit { namespace synthyboy_adapter {

/** The three shell-level settings SynthyBoy's Master page has, as IInstanceState: 0 output limiter (toggle), 1 pad pressure (toggle), 2 pad pressure amount (percent 0 .. 100). Forwards to
    IInstanceView::outputLimiter / padPressure and their setters. */
class State final : public IInstanceState
{
public:
    explicit State (synthyboy_mpc::overlay::IInstanceView& v) noexcept : v_ (v) {}
    int numSettings() const noexcept override { return 3; }
    const SettingSpec& setting (int index) const noexcept override
    {
        static const SettingSpec specs[3] = { { "mpcOutputLimiter", "LIMIT", SettingSpec::Kind::toggle, 0.0f, 1.0f, 0.0f },
                                              { "padPressure", "PAD PRESSURE", SettingSpec::Kind::toggle, 0.0f, 1.0f, 0.0f },
                                              { "padPressureAmount", "AMOUNT", SettingSpec::Kind::number, 0.0f, 100.0f, 35.0f } };
        return specs[index < 0 ? 0 : (index > 2 ? 2 : index)];
    }
    bool get (int index, float& value) const noexcept override
    {
        bool on = false;
        float percent = 35.0f;
        if (index == 0)
        {
            if (! v_.outputLimiter (on))
                return false;
            value = on ? 1.0f : 0.0f;
            return true;
        }
        if ((index != 1 && index != 2) || ! v_.padPressure (on, percent))
            return false;
        value = index == 1 ? (on ? 1.0f : 0.0f) : percent;
        return true;
    }
    void set (int index, float value) noexcept override
    {
        bool on = false;
        float percent = 35.0f;
        if (index == 0)
            v_.setOutputLimiter (value >= 0.5f);
        else if ((index == 1 || index == 2) && v_.padPressure (on, percent))
            v_.setPadPressure (index == 1 ? value >= 0.5f : on, index == 2 ? value : percent);
    }

private:
    synthyboy_mpc::overlay::IInstanceView& v_;
};

/** IInstance over an IInstanceView: the bridge for as long as IInstanceView does not derive from IInstance itself (migration step 2). Forwards every call. */
class Instance final : public IInstance
{
public:
    explicit Instance (synthyboy_mpc::overlay::IInstanceView& v) noexcept : v_ (v), state_ (v) {}
    const IParamSource& params() const noexcept override { return Params::get(); }
    float param (int index) const noexcept override { return v_.param (index); }
    void writeParam (int index, float normalised) noexcept override { v_.writeParam (index, normalised); }
    double sampleRate() const noexcept override { return v_.sampleRate(); }
    double bpm() const noexcept override { return v_.bpm(); }
    void setQGroup (const int* targets, int n) noexcept override { v_.setQGroup (targets, n); }
    void setQVirtual (int id, float normalised) noexcept override { v_.setQVirtual (id, normalised); }
    bool takeQVirtual (int id, float& normalised) noexcept override { return v_.takeQVirtual (id, normalised); }
    unsigned long qWriteCount() const noexcept override { return v_.qWriteCount(); }
    void qStatus (std::string& out) const override { v_.qStatus (out); }
    void setUiFullOpen (bool open) noexcept override { v_.setUiFullOpen (open); }
    void requestOverlayReport() noexcept override { v_.requestOverlayReport(); }
    IInstanceState* state() noexcept override { return &state_; }
    const IInstanceState* state() const noexcept override { return &state_; }

private:
    synthyboy_mpc::overlay::IInstanceView& v_;
    State state_;
};

/** The PageDef of one entry of a generated table (overlay_layout.h; the tests have tables of their own). The table has no separate tab name: `tab` is the view's name. */
inline PageDef pageDefOf (const synthyboy_mpc::overlay::layout::View& v) noexcept
{
    PageDef d;
    d.name = v.name; d.tab = v.name; d.kind = v.kind; d.index = v.index; d.opaque = v.opaque;
    d.region = { v.region.x, v.region.y, v.region.w, v.region.h };
    d.instance = { v.instance.x, v.instance.y, v.instance.w, v.instance.h };
    for (int m = 0; m < 2; ++m)
        d.markers[m] = { { v.markers[m].rect.x, v.markers[m].rect.y, v.markers[m].rect.w, v.markers[m].rect.h }, v.markers[m].rgb, v.markers[m].cover };
    return d;
}

/** IPageSet over a table of the generated form (synthyboy_mpc::overlay::layout::View, overlay_layout.h): the tables of the tests, and the table of SynthyBoy (PageSet). The instance colours are the ones of
    overlay_layout.h. The table must have at least one entry and outlive nothing: the entries are copied. */
class TablePageSet : public IPageSet
{
public:
    TablePageSet (const synthyboy_mpc::overlay::layout::View* table, int count)
    {
        namespace L = synthyboy_mpc::overlay::layout;
        defs_.resize (static_cast<size_t> (count > 0 ? count : 1));
        for (int i = 0; i < count; ++i)
            defs_[static_cast<size_t> (i)] = pageDefOf (table[i]);
        count_ = count;
        for (int s = 0; s < L::kMaxInstances; ++s)
            colours_.slot[s] = L::kInstanceSlotRgb[s];
        colours_.none = L::kInstanceNoneRgb;
        colours_.blank = L::kInstanceBlankRgb;
    }
    int numPages() const noexcept override { return count_; }
    const PageDef& page (int index) const noexcept override { return defs_[static_cast<size_t> (index < 0 ? 0 : (index >= count_ ? (count_ > 0 ? count_ - 1 : 0) : index))]; }
    const InstanceColours& instanceColours() const noexcept override { return colours_; }

private:
    std::vector<PageDef> defs_;
    InstanceColours colours_;
    int count_ = 0;
};

/** IPageSet over the generated table synthyboy_mpc::overlay::layout::kViews (overlay_layout.h). The table has no separate tab name: `tab` is the view's name. */
class PageSet final : public TablePageSet
{
public:
    static const PageSet& get()
    {
        static const PageSet s;
        return s;
    }

private:
    PageSet() : TablePageSet (synthyboy_mpc::overlay::layout::kViews, synthyboy_mpc::overlay::layout::kNumViews) {}
};

static_assert (synthyboy_mpc::overlay::layout::kMaxInstances == kMaxInstances && synthyboy_mpc::overlay::layout::kInstanceSteps == kInstanceSteps && synthyboy_mpc::overlay::layout::kInstanceFrames == kInstanceFrames,
               "the generated layout and the kit agree on the film strip of the instance picture");
static_assert (synthyboy_mpc::overlay::layout::kKindPlay == kPlayKind && synthyboy_mpc::overlay::layout::kKindFilterUi == kFirstPageKind, "the generated layout and the kit agree on the page kinds");

}} // namespace mpckit::synthyboy_adapter
