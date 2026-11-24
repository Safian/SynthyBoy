// SynthyBoy - UI panels: the wavetable position remap of oscillators 1-3 (docs/ui-design/02-oscillators.md row 02-14 "Remap graph (pencil)",
// docs/ui-design/13-new-keys.md 4.14), page `osc`, right below the oscillator panel it belongs to.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../Panel.h"
#include "RemapEditor.h"

namespace synthyboy::ui
{
template <int OscIndex>
class OscRemapPanelBase : public Panel
{
public:
    explicit OscRemapPanelBase (UiContext& context)
        : Panel (context),
          editor (context, OscIndex),
          onHandle (context, "osc" + std::to_string (OscIndex) + ".remap.on")
    {
        addAndMakeVisible (editor);
        onHandle.onChange = [this]
        {
            updateDimming();
            repaint();
        };
        updateDimming();
    }

    /** Only the oscillator chosen on the chip row takes part in the layout, like the oscillator panel itself. */
    bool hiddenInLayout() const override { return ctx.view->oscTab != OscIndex - 1; }

    juce::String title() const override { return "Oscillator " + juce::String (OscIndex) + " Remap"; }

    std::vector<std::string> keys() const override
    {
        const std::string o = "osc" + std::to_string (OscIndex) + ".remap.";
        return { o + "on", o + "slot", o + "rise", o + "fall" };
    }

    /** The graph is the visual; the four keys (on, the Remap 1..4 slot, RISE, FALL) are the controls under it. */
    void build() override
    {
        for (const auto& key : keys())
            addControl (key);
    }

    RemapEditor& graph() noexcept { return editor; }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const int graphWidth = width - 2 * t.gap();
        const juce::Rectangle<int> area (0, 0, graphWidth, 100000);
        return headerHeight() + editor.preferredHeight (graphWidth) + 3 * t.gap() + const_cast<OscRemapPanelBase*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        const auto& t = ctx.theme;
        auto area = contentArea();
        editor.setBounds (area.removeFromTop (editor.preferredHeight (area.getWidth())));
        area.removeFromTop (t.gap());
        layoutControls (area, false);
    }

    /** Remap off: the panel dims (the graph stays editable). */
    void updateDimming() { setAlpha (onHandle.valid() && onHandle.normalised() < 0.5f ? 0.6f : 1.0f); }

private:
    RemapEditor editor;
    ParamHandle onHandle;
};

class OscRemap1Panel final : public OscRemapPanelBase<1> { using OscRemapPanelBase<1>::OscRemapPanelBase; };
class OscRemap2Panel final : public OscRemapPanelBase<2> { using OscRemapPanelBase<2>::OscRemapPanelBase; };
class OscRemap3Panel final : public OscRemapPanelBase<3> { using OscRemapPanelBase<3>::OscRemapPanelBase; };

SYNTHYBOY_REGISTER_PANEL (OscRemap1Panel, Page::osc, 11)
SYNTHYBOY_REGISTER_PANEL (OscRemap2Panel, Page::osc, 21)
SYNTHYBOY_REGISTER_PANEL (OscRemap3Panel, Page::osc, 31)
} // namespace synthyboy::ui
