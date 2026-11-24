// SynthyBoy - UI panel: Global Settings & Utilities (docs/ui-design/07-voice-midi-global.md, Page::glb).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <fstream>
#include <iterator>

#include "../ContentIO.h"
#include "../Panel.h"
#include "../ParamHandle.h"
#include "synthyboy/preferences.h"

namespace synthyboy::ui
{
/** Interactive utilities and UI preferences view. */
class GlbSettingsView final : public juce::Component
{
public:
    explicit GlbSettingsView (UiContext& context)
        : ctx (context),
          volHandle (context, "master.volume"),
          directHandle (context, "direct.level"),
          transposeHandle (context, "master.transpose"),
          tuneHandle (context, "master.tune")
    {
        const auto update = [this] { repaint(); };
        volHandle.onChange = update;
        directHandle.onChange = update;
        transposeHandle.onChange = update;
        tuneHandle.onChange = update;

        initPresetBtn.setButtonText ("Init Preset");
        initPresetBtn.onClick = [this] { doInitPreset(); };
        addAndMakeVisible (initPresetBtn);

        initModsBtn.setButtonText ("Init Mods");
        initModsBtn.onClick = [this] { doInitMods(); };
        addAndMakeVisible (initModsBtn);

        initLfosBtn.setButtonText ("Init LFOs");
        initLfosBtn.onClick = [this] { doInitLfos(); };
        addAndMakeVisible (initLfosBtn);

        revertBtn.setButtonText ("Revert Saved");
        revertBtn.onClick = [this] { doRevert(); };
        addAndMakeVisible (revertBtn);

        // Save as Default (G02): the current patch becomes the init patch of new instances and of Init Preset
        saveDefaultBtn.setButtonText ("Save as Default");
        saveDefaultBtn.onClick = [this] { doSaveDefault(); };
        addAndMakeVisible (saveDefaultBtn);

        clearDefaultBtn.setButtonText ("Clear Default");
        clearDefaultBtn.onClick = [this] { doClearDefault(); };
        addAndMakeVisible (clearDefaultBtn);
        refreshDefaultButtons();

        // the preferences (S p.313 - 315, GROUPA2 WP-02): one toggle for each line of the table; the host stores them per user and acts on them
        for (size_t i = 0; i < synthyboy::preferenceCount(); ++i)
        {
            auto& button = prefButtons[i];
            button.setButtonText (synthyboy::preferenceTable()[i].label);
            button.setClickingTogglesState (false);
            button.onClick = [this, i] { setPreference (i, ! preference (i)); };
            addAndMakeVisible (button);
        }
        syncPreferences();

        // MIDI CC map files and the default map (S p.325)
        loadMapBtn.setButtonText ("Load MIDI Map...");
        loadMapBtn.onClick = [this] { requestMapFile (false); };
        saveMapBtn.setButtonText ("Save MIDI Map...");
        saveMapBtn.onClick = [this] { requestMapFile (true); };
        defaultMapBtn.setButtonText ("Set Default Map");
        defaultMapBtn.onClick = [this] { doSetDefaultMap(); };
        clearMapBtn.setButtonText ("Clear Default Map");
        clearMapBtn.onClick = [this] { doClearDefaultMap(); };
        for (auto* b : { &loadMapBtn, &saveMapBtn, &defaultMapBtn, &clearMapBtn })
            addAndMakeVisible (*b);
        refreshMapButtons();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.theme;
        const auto& c = t.colours;
        const auto bounds = getLocalBounds().toFloat().reduced (t.px (2.0f));

        g.setColour (c.panelRaised);
        g.fillRoundedRectangle (bounds, t.px (6.0f));
        g.setColour (c.outline);
        g.drawRoundedRectangle (bounds, t.px (6.0f), 1.0f);

        const float pad = t.px (8.0f);

        // Build and engine info at the bottom (S15-20): version and build date come from the host (CMake project version, __DATE__)
        g.setFont (t.font (t.px (10.0f)));
        g.setColour (linkEnabled() ? c.accent : c.textDim); // a host that can open links makes the line one (the project page)
        g.drawFittedText (aboutLine(), juce::Rectangle<float> (bounds.getX() + pad, bounds.getBottom() - pad - t.px (28.0f), bounds.getWidth() - 2 * pad, t.px (28.0f)).toNearestInt(),
                          juce::Justification::centred, 2);
    }

    bool linkEnabled() const { return ctx.host.canOpenUrl() && ctx.host.aboutUrl().isNotEmpty(); }
    juce::Rectangle<int> aboutArea() const
    {
        const auto& t = ctx.theme;
        const auto bounds = getLocalBounds().toFloat().reduced (t.px (2.0f));
        const float pad = t.px (8.0f);
        return juce::Rectangle<float> (bounds.getX() + pad, bounds.getBottom() - pad - t.px (28.0f), bounds.getWidth() - 2 * pad, t.px (28.0f)).toNearestInt();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (linkEnabled() && e.mouseWasClicked() && aboutArea().contains (e.getPosition()))
            ctx.host.openUrl (ctx.host.aboutUrl());
    }

    /** The About line: what the host says (version, build date, engine, content), "SynthyBoy" alone for a host that says nothing. */
    juce::String aboutLine() const
    {
        const juce::String text = ctx.host.aboutText();
        return text.isNotEmpty() ? text : juce::String ("SynthyBoy");
    }

    void resized() override
    {
        const auto& t = ctx.theme;
        const auto bounds = getLocalBounds().reduced (t.gap());

        // Row group 1: Init / Revert buttons, row group 2: UI preferences. Each wraps into as many rows as the width needs,
        // every button at least two touch targets wide.
        juce::TextButton* const initRow[] = { &initPresetBtn, &initModsBtn, &initLfosBtn, &revertBtn, &saveDefaultBtn, &clearDefaultBtn };
        juce::TextButton* prefRow[kPrefs];
        for (size_t i = 0; i < kPrefs; ++i)
            prefRow[i] = &prefButtons[i];
        juce::TextButton* const mapRow[] = { &loadMapBtn, &saveMapBtn, &defaultMapBtn, &clearMapBtn };
        auto area = bounds;
        layoutGrid (area, initRow, 6, 2);
        layoutGrid (area, prefRow, static_cast<int> (kPrefs), 7);   // long labels: one column on a phone
        layoutGrid (area, mapRow, 4, 2);
    }

    /** Height this view needs at `viewWidth`: the button rows plus the build info line. */
    int requiredHeight (int viewWidth) const
    {
        const auto& t = ctx.theme;
        const int inner = viewWidth - 2 * t.gap();
        const int rows = rowsFor (6, inner, 2) + rowsFor (static_cast<int> (kPrefs), inner, 7) + rowsFor (4, inner, 2);
        return 2 * t.gap() + rows * t.touch() + rows * t.gap() + t.ipx (40.0f);
    }

private:
    /** Columns of a grid whose buttons are at least `minTouches` touch targets wide. */
    int columnsFor (int count, int width, int minTouches) const
    {
        const auto& t = ctx.theme;
        return juce::jlimit (1, count, (width + t.gap()) / (t.touch() * minTouches + t.gap()));
    }

    int rowsFor (int count, int width, int minTouches) const
    {
        const int cols = columnsFor (count, width, minTouches);
        return (count + cols - 1) / cols;
    }

    void layoutGrid (juce::Rectangle<int>& area, juce::TextButton* const* buttons, int count, int minTouches)
    {
        const auto& t = ctx.theme;
        const int cols = columnsFor (count, area.getWidth(), minTouches);
        const int rows = (count + cols - 1) / cols;
        const int perRow = (count + rows - 1) / rows;
        for (int row = 0; row < rows; ++row)
        {
            auto rowArea = area.removeFromTop (t.touch());
            area.removeFromTop (t.gap());
            const int inRow = juce::jmin (perRow, count - row * perRow);
            const int w = (rowArea.getWidth() - (inRow - 1) * t.gap()) / inRow;
            for (int i = 0; i < inRow; ++i)
            {
                buttons[row * perRow + i]->setBounds (rowArea.removeFromLeft (w));
                rowArea.removeFromLeft (t.gap());
            }
        }
    }

public:
    /** Every parameter to its default and the content (custom LFO shapes ...) emptied: a new patch. */
    void doInitPreset()
    {
        // the user's default patch (G02) when there is one, else the factory init patch; the locked modules (G08) stay as they are
        const bool fromDefault = ctx.host.hasUserDefault();
        if (! ctx.host.initPatch())
        {
            applyValues (ctx, ParamValues::defaults());
            ctx.host.setContent (StateContent {});
        }
        ctx.assign.refresh();
        Toast::show (ctx, *this, fromDefault ? "Preset initialised from your default" : "Preset initialised");
        repaint();
    }

    void doSaveDefault()
    {
        const bool ok = ctx.host.saveAsDefault();
        refreshDefaultButtons();
        Toast::show (ctx, *this, ok ? "Saved as the default patch" : "Could not save the default patch");
        repaint();
    }

    void doClearDefault()
    {
        const bool ok = ctx.host.clearUserDefault();
        refreshDefaultButtons();
        Toast::show (ctx, *this, ok ? "Default patch cleared" : "Could not clear the default patch");
        repaint();
    }

    void refreshDefaultButtons()
    {
        saveDefaultBtn.setEnabled (true);
        clearDefaultBtn.setEnabled (ctx.host.hasUserDefault());
    }

    void doInitMods()
    {
        for (int i = 0; i < 64; ++i)
            clearSlot (ctx.assign.matrix(), i);
        ctx.assign.refresh();
        Toast::show (ctx, *this, "All modulations cleared");
        repaint();
    }

    /** All 10 LFOs and the 4 random LFOs back to their defaults, and the custom shapes with them. */
    void doInitLfos()
    {
        auto live = readLiveValues (ctx);
        for (int n = 1; n <= 10; ++n)
            ModuleClipboard::initModule ("lfo" + std::to_string (n), live);
        for (int n = 1; n <= 4; ++n)
            ModuleClipboard::initModule ("rlfo" + std::to_string (n), live);
        applyValues (ctx, live);
        auto content = ctx.host.content();
        content.lfoShapes.clear();
        ctx.host.setContent (content);
        Toast::show (ctx, *this, "LFOs initialised");
        repaint();
    }

    void doRevert()
    {
        Toast::show (ctx, *this, ctx.host.revertToSaved() ? "Reverted to the saved patch" : "Nothing saved to revert to");
        repaint();
    }

    // ---- preferences (GROUPA2 WP-02) ----
    bool preference (size_t index) const { return ctx.host.preferences().*(synthyboy::preferenceTable()[index].field); }
    void setPreference (size_t index, bool on)
    {
        synthyboy::Preferences p = ctx.host.preferences();
        p.*(synthyboy::preferenceTable()[index].field) = on;
        ctx.host.setPreferences (p);
        syncPreferences();
    }
    void syncPreferences()
    {
        for (size_t i = 0; i < kPrefs; ++i)
            prefButtons[i].setToggleState (preference (i), juce::dontSendNotification);
    }
    juce::TextButton& preferenceButton (size_t index) noexcept { return prefButtons[index]; }

    // ---- MIDI CC map files and the default map (S p.325) ----
    /** Writes the map of the plug-in to `file` ({"format":"synthyboy-midi-map"}). */
    FlowResult saveMidiMapFile (const juce::File& file)
    {
        FlowResult r;
        const auto text = synthyboy::serializeMidiMap (ctx.host.midiMapBindings());
        r.ok = file.replaceWithData (text.data(), text.size());
        r.message = r.ok ? "Saved the MIDI map (" + juce::String (ctx.host.midiMapBindings().size()) + " controllers)" : "Could not write " + file.getFileName();
        Toast::show (ctx, *this, r.message);
        return r;
    }
    /** Replaces the map by the one in `file`: ours (.sbmidimap) or a Serum 2 .SerumMIDIMap (only the macros are known). */
    FlowResult loadMidiMapFile (const juce::File& file)
    {
        FlowResult r;
        if (file.hasFileExtension ("SerumMIDIMap"))
        {
            const auto flow = importSerumFile (ctx, file, 0);
            r = flow.flow;
        }
        else
        {
            juce::MemoryBlock data;
            std::vector<std::pair<int, std::string>> bindings;
            const int taken = file.loadFileAsData (data) ? synthyboy::parseMidiMap (static_cast<const char*> (data.getData()), data.getSize(), bindings) : -1;
            r.ok = taken >= 0 && ctx.host.setMidiMapBindings (bindings) >= 0;
            r.message = r.ok ? "Loaded the MIDI map (" + juce::String (taken) + " controllers)" : "Not a MIDI map: " + file.getFileName();
            Toast::show (ctx, *this, r.message);
        }
        ctx.assign.refresh();
        refreshMapButtons();
        return r;
    }
    void requestMapFile (bool save)
    {
        FileRequest request;
        request.mode = save ? FileRequest::Mode::save : FileRequest::Mode::open;
        request.title = save ? "Save the MIDI map" : "Load a MIDI map";
        request.wildcards = save ? "*.sbmidimap" : "*.sbmidimap;*.SerumMIDIMap";
        request.suggestedName = "MIDI Map.sbmidimap";
        request.purpose = "midimap";
        const bool asked = ctx.host.requestFile (request, [safe = juce::Component::SafePointer<GlbSettingsView> (this), save] (const juce::File& file)
        {
            if (safe != nullptr)
                save ? (void) safe->saveMidiMapFile (file) : (void) safe->loadMidiMapFile (file);
        });
        if (! asked)
            Toast::show (ctx, *this, "This host cannot ask for a file");
    }
    void doSetDefaultMap()
    {
        const bool ok = ctx.host.saveDefaultMidiMap();
        refreshMapButtons();
        Toast::show (ctx, *this, ok ? "The MIDI map is the default map now" : "Could not save the default map");
    }
    void doClearDefaultMap()
    {
        const bool ok = ctx.host.clearDefaultMidiMap();
        refreshMapButtons();
        Toast::show (ctx, *this, ok ? "Default MIDI map cleared" : "Could not clear the default map");
    }
    void refreshMapButtons()
    {
        clearMapBtn.setEnabled (ctx.host.hasDefaultMidiMap());
        saveMapBtn.setEnabled (ctx.host.canRequestFile());
        loadMapBtn.setEnabled (ctx.host.canRequestFile());
    }
    juce::TextButton& loadMapButton() noexcept { return loadMapBtn; }
    juce::TextButton& saveMapButton() noexcept { return saveMapBtn; }
    juce::TextButton& defaultMapButton() noexcept { return defaultMapBtn; }
    juce::TextButton& clearMapButton() noexcept { return clearMapBtn; }

    juce::TextButton& initPresetButton() noexcept { return initPresetBtn; }
    juce::TextButton& initModsButton() noexcept { return initModsBtn; }
    juce::TextButton& initLfosButton() noexcept { return initLfosBtn; }
    juce::TextButton& revertButton() noexcept { return revertBtn; }
    juce::TextButton& saveDefaultButton() noexcept { return saveDefaultBtn; }
    juce::TextButton& clearDefaultButton() noexcept { return clearDefaultBtn; }

private:

    UiContext& ctx;
    ParamHandle volHandle;
    ParamHandle directHandle;
    ParamHandle transposeHandle;
    ParamHandle tuneHandle;

    juce::TextButton initPresetBtn;
    juce::TextButton initModsBtn;
    juce::TextButton initLfosBtn;
    juce::TextButton revertBtn;
    juce::TextButton saveDefaultBtn;
    juce::TextButton clearDefaultBtn;

    static constexpr size_t kPrefs = 14;
    std::array<juce::TextButton, kPrefs> prefButtons;
    juce::TextButton loadMapBtn, saveMapBtn, defaultMapBtn, clearMapBtn;
};

class GlbSettingsPanel final : public Panel
{
public:
    explicit GlbSettingsPanel (UiContext& context)
        : Panel (context), settingsView (context)
    {
        addAndMakeVisible (settingsView);
    }

    GlbSettingsView& view() noexcept { return settingsView; }

    juce::String title() const override { return "Global Settings"; }

    std::vector<std::string> keys() const override
    {
        return { "master.volume",
                 "direct.level",
                 "master.transpose",
                 "master.tune" };
    }

    void build() override
    {
        // 4 primary controls next to visual
        addControl ("master.volume");
        addControl ("direct.level");
        addControl ("master.transpose");
        addControl ("master.tune");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const int visualH = settingsView.requiredHeight (width - 2 * t.gap());
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
             + const_cast<GlbSettingsPanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        const int visualH = settingsView.requiredHeight (area.getWidth());
        settingsView.setBounds (area.removeFromTop (visualH));
        area.removeFromTop (ctx.theme.gap());
        layoutControls (area, false);
    }

private:
    GlbSettingsView settingsView;
};

} // namespace synthyboy::ui
