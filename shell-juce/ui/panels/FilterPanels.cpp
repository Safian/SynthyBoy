// SynthyBoy - UI panels: Filter 1, Filter 2, and FX Filter (docs/ui-design/04-filters.md, page `flt`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../Panel.h"
#include "FilterModels.h"
#include "FilterViews.h"

namespace synthyboy::ui
{

// ---- FilterPanelBase ---------------------------------------------------------------------------------------------------------------

class FilterPanelBase : public Panel
{
public:
    /** Filter 1 and 2 are modules (module sheet); the FX filter is not part of ModuleClipboard. */
    std::string modulePrefix() const override { return filterIdx <= 2 ? prefix : std::string(); }

    FilterPanelBase (UiContext& context, int filterIndex, const std::string& keyPrefix)
        : Panel (context),
          filterIdx (filterIndex),
          prefix (keyPrefix),
          onHandle (context, prefix + ".on"),
          modelHandle (context, prefix + ".model"),
          styleHandle (context, prefix + ".style"),
          cutoffHandle (context, prefix + ".cutoff"),
          resHandle (context, prefix + ".resonance"),
          driveHandle (context, prefix + ".drive"),
          blendHandle (context, prefix + ".blend"),
          mixHandle (context, prefix + ".mix"),
          varHandle (context, prefix + ".var"),
          routingHandle (context, "filter.routing"),
          fxHandle (context, prefix + ".formant.x"),
          fyHandle (context, prefix + ".formant.y"),
          ftHandle (context, prefix + ".formant.transpose"),
          responseView (context, cutoffHandle, resHandle, onHandle, modelHandle, styleHandle,
                        blendHandle, varHandle, &fxHandle, &fyHandle, &ftHandle, prefix)
    {
        // Power toggle
        powerButton.setButtonText ("ON");
        powerButton.setClickingTogglesState (false);
        powerButton.onClick = [this] {
            if (onHandle.valid())
                onHandle.setPlainComplete (onHandle.plain() >= 0.5f ? 0.0f : 1.0f);
            refreshPowerState();
        };
        addAndMakeVisible (powerButton);

        // Filter chips F1, F2, FX
        const char* const fChips[] = { "F1", "F2", "FX" };
        for (int i = 0; i < 3; ++i)
        {
            auto b = std::make_unique<juce::TextButton> (fChips[i]);
            b->setClickingTogglesState (false);
            b->setToggleState ((i + 1) == filterIdx, juce::dontSendNotification);
            b->onClick = [this, i] { scrollToFilterPanel (i + 1); };
            addAndMakeVisible (*b);
            filterNavChips.push_back (std::move (b));
        }

        // Routing chip (PAR | 1>2 | 2>1) for Filter 1 and 2
        if (filterIdx <= 2)
        {
            routingChip = std::make_unique<FilterRoutingChip> (context, routingHandle);
            addAndMakeVisible (*routingChip);
        }

        // Model selector button
        modelButton.onClick = [this] { showModelMenu(); };
        addAndMakeVisible (modelButton);

        // Style selector button
        styleButton.onClick = [this] { showStyleMenu(); };
        addAndMakeVisible (styleButton);

        // Type list button
        typeListButton.setButtonText ("Types");
        typeListButton.onClick = [this] {
            showFilterTypePicker (ctx, modelHandle, styleHandle, blendHandle, typeListButton);
        };
        addAndMakeVisible (typeListButton);

        // Big visual
        addAndMakeVisible (responseView);

        // Blend slider and label
        blendLabel.setFont (context.theme.labelFont());
        blendLabel.setColour (juce::Label::textColourId, context.theme.colours.textDim);
        blendContainer.addAndMakeVisible (blendLabel);

        blendSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        blendSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        blendSlider.setRange (0.0, 100.0, 0.1);
        blendSlider.setValue (blendHandle.valid() ? blendHandle.plain() : 0.0, juce::dontSendNotification);
        blendSlider.onValueChange = [this] {
            if (blendHandle.valid())
                blendHandle.setPlainComplete (static_cast<float> (blendSlider.getValue()));
        };
        blendContainer.addAndMakeVisible (blendSlider);
        addAndMakeVisible (blendContainer);

        // Input switches (Filter 1 & 2)
        if (filterIdx <= 2)
        {
            inputsBar = std::make_unique<FilterInputsBar> (context, filterIdx);
            inputsBar->onInputsChanged = [this] { updateInputStatus(); };
            addAndMakeVisible (*inputsBar);
        }

        const auto updateCb = [this] { refreshAll(); };
        onHandle.onChange = updateCb;
        modelHandle.onChange = updateCb;
        styleHandle.onChange = updateCb;
        blendHandle.onChange = updateCb;
        refreshAll();
    }

    void buildControls (const std::vector<std::string>& allKeys)
    {
        for (size_t i = 0; i < allKeys.size(); ++i)
        {
            if (i < 4)
                addControl (allKeys[i]);
            else
                addMoreControl (allKeys[i]);
        }
        refreshControlLabels();
    }

    /** The chip rows under the title: [power][F1][F2][FX] and, for filter 1 / 2, the routing chip beside them or (narrow) in a row of its own.
        Every chip is a full touch target high. */
    bool routingOnOwnRow (int innerWidth) const
    {
        const auto& t = ctx.theme;
        const int navW = juce::jmax (t.touch(), t.ipx (44.0f));
        const int routeW = juce::jmax (t.touch() * 3, t.ipx (132.0f));
        return routingChip != nullptr && innerWidth < 4 * navW + 3 * t.gap() + t.gap() + routeW;
    }

    int chipRowsHeight (int innerWidth) const
    {
        const auto& t = ctx.theme;
        return t.touch() + (routingOnOwnRow (innerWidth) ? t.gap() + t.touch() : 0);
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const int gap = t.gap();
        const int headerH = headerHeight() + gap + chipRowsHeight (width - 2 * gap);
        const int touchH = t.touch();
        const int visualH = t.ipx (140.0f);
        const int inputsH = (filterIdx <= 2 ? touchH + gap : 0);

        const int topHeight = headerH + gap + touchH + gap + visualH + gap + touchH + gap + inputsH;
        const juce::Rectangle<int> area (0, 0, juce::jmax (1, width - 2 * gap), 100000);
        const int controlsH = const_cast<FilterPanelBase*> (this)->layoutControls (area, true);

        return topHeight + controlsH + gap * 2;
    }

    void resized() override
    {
        const auto& t = ctx.theme;
        const int gap = t.gap();
        const int touchH = t.touch();
        const auto bounds = getLocalBounds().reduced (gap);
        int y = bounds.getY();

        // Header: the title is drawn by the Panel; under it the chip row with Power, F1/F2/FX and the Routing chip
        const int headerH = headerHeight();
        y += headerH + gap;
        const int navChipW = juce::jmax (touchH, t.ipx (44.0f));
        powerButton.setBounds (bounds.getX(), y, navChipW, touchH);
        int x = powerButton.getRight() + gap;
        for (size_t i = 0; i < 3; ++i)
        {
            filterNavChips[i]->setBounds (x, y, navChipW, touchH);
            x += navChipW + gap;
        }
        if (routingChip != nullptr)
        {
            if (routingOnOwnRow (bounds.getWidth()))
            {
                y += touchH + gap;
                routingChip->setBounds (bounds.getX(), y, bounds.getWidth(), touchH);
            }
            else
            {
                routingChip->setBounds (x, y, bounds.getRight() - x, touchH);
            }
        }
        y += touchH + gap;

        // Model & Style row: Model button, Style button, Types button
        const int typeBtnW = juce::jmax (touchH, t.ipx (60.0f));
        typeListButton.setBounds (bounds.getRight() - typeBtnW, y, typeBtnW, touchH);

        const int availTypeW = typeListButton.getX() - bounds.getX() - gap;
        const int modelW = availTypeW / 2;
        const int styleW = availTypeW - modelW - gap;
        modelButton.setBounds (bounds.getX(), y, modelW, touchH);
        styleButton.setBounds (bounds.getX() + modelW + gap, y, styleW, touchH);
        y += touchH + gap;

        // Big visual
        const int visualH = t.ipx (140.0f);
        responseView.setBounds (bounds.getX(), y, bounds.getWidth(), visualH);
        y += visualH + gap;

        // Blend slider row
        blendContainer.setBounds (bounds.getX(), y, bounds.getWidth(), touchH);
        const int blendLabelW = t.ipx (130.0f);
        blendLabel.setBounds (0, 0, blendLabelW, touchH);
        blendSlider.setBounds (blendLabelW + gap, 0, blendContainer.getWidth() - blendLabelW - gap, touchH);
        y += touchH + gap;

        // Input switches row
        if (inputsBar != nullptr)
        {
            inputsBar->setBounds (bounds.getX(), y, bounds.getWidth(), touchH);
            y += touchH + gap;
        }

        // Primary and More controls
        const auto controlsArea = juce::Rectangle<int> (bounds.getX(), y, bounds.getWidth(), juce::jmax (10, bounds.getBottom() - y));
        layoutControls (controlsArea, false);
    }

private:
    void refreshAll()
    {
        refreshPowerState();
        refreshModelAndStyle();
        updateInputStatus();
        refreshControlLabels();
        repaint();
    }

    void refreshPowerState()
    {
        const bool isOn = (onHandle.valid() ? onHandle.plain() >= 0.5f : true);
        powerButton.setToggleState (isOn, juce::dontSendNotification);
        powerButton.setButtonText (isOn ? "ON" : "OFF");
        setAlpha (isOn ? 1.0f : 0.65f);
    }

    void refreshModelAndStyle()
    {
        const int model = (modelHandle.valid() ? static_cast<int> (modelHandle.plain()) : 3);
        const int style = (styleHandle.valid() ? static_cast<int> (styleHandle.plain()) : 0);

        modelButton.setButtonText (getFilterTypeNameForModelAndStyle (model, style));

        const auto styles = getStylesForModel (model);
        if (style >= 0 && style < static_cast<int> (styles.size()))
            styleButton.setButtonText (styles[static_cast<size_t> (style)]);
        else if (! styles.empty())
            styleButton.setButtonText (styles[0]);
        else
            styleButton.setButtonText ("Style");

        blendLabel.setText (getBlendLabelForModel (model, style), juce::dontSendNotification);
        if (blendHandle.valid())
            blendSlider.setValue (blendHandle.plain(), juce::dontSendNotification);
    }

    void updateInputStatus()
    {
        if (inputsBar != nullptr)
        {
            const int route = (routingHandle.valid() ? static_cast<int> (routingHandle.plain()) : 0);
            responseView.setHasInputs (inputsBar->hasAnyInputs (route));
        }
        else
        {
            responseView.setHasInputs (true);
        }
    }

    void refreshControlLabels()
    {
        const int model = (modelHandle.valid() ? static_cast<int> (modelHandle.plain()) : 3);
        const int style = (styleHandle.valid() ? static_cast<int> (styleHandle.plain()) : 0);
        const auto& ctls = controls();

        // Primary controls: [0]=cutoff, [1]=res, [2]=drive, [3]=mix
        if (ctls.size() >= 4)
        {
            if (model == 6) // Comb model
            {
                ctls[2]->setLabel ("Cut");
                ctls[3]->setEnabled (false);
                ctls[3]->setAlpha (0.4f);
            }
            else
            {
                ctls[2]->setLabel ("Drive");
                ctls[3]->setEnabled (true);
                ctls[3]->setAlpha (1.0f);
            }
        }

        // More controls: find var control
        const std::string varKey = prefix + ".var";
        for (const auto& c : ctls)
        {
            if (c->key() == varKey)
            {
                const char* varLabel = getVarLabelForModel (model, style);
                if (varLabel[0] != '\0')
                {
                    c->setLabel (varLabel);
                    c->setEnabled (true);
                    c->setAlpha (1.0f);
                }
                else
                {
                    c->setLabel ("Var (N/A)");
                    c->setEnabled (false);
                    c->setAlpha (0.4f);
                }
                break;
            }
        }
    }

    void showModelMenu()
    {
        juce::PopupMenu menu;
        const int curModel = (modelHandle.valid() ? static_cast<int> (modelHandle.plain()) : 3);
        const int curStyle = (styleHandle.valid() ? static_cast<int> (styleHandle.plain()) : 0);

        const auto& catalog = getSerumFilterCatalog();
        const char* const families[] = { "Normal", "Multi", "Flanges", "Misc", "New" };

        struct MenuItemMap { int model; int style; float defaultBlend; };
        std::vector<MenuItemMap> itemMap;
        itemMap.push_back ({ 0, 0, 0.0f }); // 1-based indexing dummy

        int itemIndex = 1;
        for (const char* fam : families)
        {
            juce::PopupMenu sub;
            for (const auto& item : catalog)
            {
                if (std::string (item.family) == fam)
                {
                    const bool isTicked = (curModel == item.model && curStyle == item.style);
                    sub.addItem (itemIndex, item.name, true, isTicked);
                    itemMap.push_back ({ item.model, item.style, item.defaultBlend });
                    ++itemIndex;
                }
            }
            menu.addSubMenu (fam, sub);
        }

        // Classic Vital models
        juce::PopupMenu classicSub;
        static const char* const kClassicNames[] = {
            "Analog", "Dirty", "Ladder", "Digital", "Diode", "Formant", "Comb", "Phaser"
        };
        for (int i = 0; i < 8; ++i)
        {
            const bool isTicked = (curModel == i);
            classicSub.addItem (itemIndex, kClassicNames[i], true, isTicked);
            itemMap.push_back ({ i, 0, 0.0f });
            ++itemIndex;
        }
        menu.addSubMenu ("Classic", classicSub);

        menu.showMenuAsync (popupOptions (modelButton),
                            [this, itemMap] (int result) {
                                if (result > 0 && result < static_cast<int> (itemMap.size()))
                                {
                                    const auto& m = itemMap[static_cast<size_t> (result)];
                                    if (modelHandle.valid())
                                        modelHandle.setPlainComplete (static_cast<float> (m.model));
                                    if (styleHandle.valid())
                                        styleHandle.setPlainComplete (static_cast<float> (m.style));
                                    if (blendHandle.valid() && m.defaultBlend >= 0.0f)
                                        blendHandle.setPlainComplete (m.defaultBlend);
                                    refreshAll();
                                }
                            });
    }

    void showStyleMenu()
    {
        const int model = (modelHandle.valid() ? static_cast<int> (modelHandle.plain()) : 3);
        const auto styles = getStylesForModel (model);
        juce::PopupMenu menu;
        for (size_t i = 0; i < styles.size(); ++i)
        {
            menu.addItem (static_cast<int> (i + 1), styles[i], true,
                          styleHandle.valid() && static_cast<int> (styleHandle.plain()) == static_cast<int> (i));
        }
        menu.showMenuAsync (popupOptions (styleButton),
                            [this] (int result) {
                                if (result > 0 && styleHandle.valid())
                                {
                                    styleHandle.setPlainComplete (static_cast<float> (result - 1));
                                    refreshAll();
                                }
                            });
    }

    void scrollToFilterPanel (int targetIdx)
    {
        if (targetIdx == filterIdx)
            return;
        if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        {
            if (auto* viewed = vp->getViewedComponent())
            {
                const juce::String targetTitle = (targetIdx == 1 ? "Filter 1" : (targetIdx == 2 ? "Filter 2" : "FX Filter"));
                for (auto* child : viewed->getChildren())
                {
                    if (auto* p = dynamic_cast<Panel*> (child))
                    {
                        if (p->title() == targetTitle)
                        {
                            vp->setViewPosition (0, p->getY() - ctx.theme.gap());
                            break;
                        }
                    }
                }
            }
        }
    }

    int filterIdx;
    std::string prefix;

    ParamHandle onHandle;
    ParamHandle modelHandle;
    ParamHandle styleHandle;
    ParamHandle cutoffHandle;
    ParamHandle resHandle;
    ParamHandle driveHandle;
    ParamHandle blendHandle;
    ParamHandle mixHandle;
    ParamHandle varHandle;
    ParamHandle routingHandle;
    ParamHandle fxHandle, fyHandle, ftHandle;

    juce::TextButton powerButton;
    std::vector<std::unique_ptr<juce::TextButton>> filterNavChips;
    std::unique_ptr<FilterRoutingChip> routingChip;

    juce::TextButton modelButton;
    juce::TextButton styleButton;
    juce::TextButton typeListButton;

    FilterResponseView responseView;

    juce::Component blendContainer;
    juce::Label blendLabel;
    juce::Slider blendSlider;

    std::unique_ptr<FilterInputsBar> inputsBar;
};

// ---- Filter 1 Panel ----------------------------------------------------------------------------------------------------------------

class Filter1Panel final : public FilterPanelBase
{
public:
    explicit Filter1Panel (UiContext& context)
        : FilterPanelBase (context, 1, "filter1") {}

    juce::String title() const override { return "Filter 1"; }

    std::vector<std::string> keys() const override
    {
        return {
            "filter1.cutoff", "filter1.resonance", "filter1.drive", "filter1.mix",
            "filter1.on", "filter1.model", "filter1.style", "filter1.blend", "filter1.keytrack",
            "filter1.comb_offset", "filter1.formant.x", "filter1.formant.y", "filter1.formant.resonance",
            "filter1.formant.spread", "filter1.formant.transpose", "filter1.var", "filter1.pan",
            "filter1.level", "filter1.clean_mode", "filter1.destination", "filter1.bus1_send", "filter1.bus2_send", "filter.routing"
        };
    }

    void build() override
    {
        buildControls (keys());
    }
};

// ---- Filter 2 Panel ----------------------------------------------------------------------------------------------------------------

class Filter2Panel final : public FilterPanelBase
{
public:
    explicit Filter2Panel (UiContext& context)
        : FilterPanelBase (context, 2, "filter2") {}

    juce::String title() const override { return "Filter 2"; }

    std::vector<std::string> keys() const override
    {
        return {
            "filter2.cutoff", "filter2.resonance", "filter2.drive", "filter2.mix",
            "filter2.on", "filter2.model", "filter2.style", "filter2.blend", "filter2.keytrack",
            "filter2.comb_offset", "filter2.formant.x", "filter2.formant.y", "filter2.formant.resonance",
            "filter2.formant.spread", "filter2.formant.transpose", "filter2.var", "filter2.pan",
            "filter2.level", "filter2.clean_mode", "filter2.destination", "filter2.bus1_send", "filter2.bus2_send"
        };
    }

    void build() override
    {
        buildControls (keys());
    }
};

// ---- FX Filter Panel ---------------------------------------------------------------------------------------------------------------

class FltFxFilterPanel final : public FilterPanelBase
{
public:
    explicit FltFxFilterPanel (UiContext& context)
        : FilterPanelBase (context, 3, "fxfilter") {}

    juce::String title() const override { return "FX Filter"; }

    std::vector<std::string> keys() const override
    {
        return {
            "fxfilter.cutoff", "fxfilter.resonance", "fxfilter.drive", "fxfilter.mix",
            "fxfilter.on", "fxfilter.model", "fxfilter.style", "fxfilter.blend", "fxfilter.keytrack",
            "fxfilter.comb_offset", "fxfilter.formant.x", "fxfilter.formant.y", "fxfilter.formant.resonance",
            "fxfilter.formant.spread", "fxfilter.formant.transpose", "fxfilter.var", "fxfilter.pan",
            "fxfilter.level", "fxfilter.clean_mode", "fxfilter.rack"
        };
    }

    void build() override
    {
        buildControls (keys());
    }
};

SYNTHYBOY_REGISTER_PANEL (Filter1Panel, Page::flt, 10)
SYNTHYBOY_REGISTER_PANEL (Filter2Panel, Page::flt, 20)
SYNTHYBOY_REGISTER_PANEL (FltFxFilterPanel, Page::flt, 30)

} // namespace synthyboy::ui
