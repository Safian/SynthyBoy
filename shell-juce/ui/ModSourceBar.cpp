// SynthyBoy - UI: mod source chips.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ModSourceBar.h"

namespace synthyboy::ui
{
ModSourceChip::ModSourceChip (UiContext& context, int source) : ctx (context), sourceChoice (source)
{
    const int sourceKey = findParamIndex (hashKey ("mod01.source"));
    const CapState caps = capStateForValue (sourceKey, static_cast<float> (source), ctx.host.engineCaps());
    available = caps.available;
    setAlpha (available ? 1.0f : 0.4f);
    setTooltip (available ? juce::String (modSourceTable()[source - 1].name) : juce::String (caps.tooltip));
    ctx.assign.addChangeListener (this);
}

ModSourceChip::~ModSourceChip()
{
    stopTimer();
    ctx.assign.removeChangeListener (this);
}

bool ModSourceChip::isArmed() const
{
    return ctx.assign.flow().armedSource() == sourceChoice;
}

juce::Point<int> ModSourceChip::preferredSize() const
{
    const auto text = juce::String (sourceChipLabel (sourceChoice));
    return { juce::jmax (ctx.theme.touch(), juce::roundToInt (Theme::textWidth (ctx.theme.valueFont(), text)) + ctx.theme.ipx (20.0f)),
             ctx.theme.touch() };
}

void ModSourceChip::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    const Palette& c = t.colours;
    const bool armed = isArmed();
    const auto r = getLocalBounds().toFloat().reduced (t.px (2.0f));
    g.setColour (armed ? c.mod : c.panelRaised);
    g.fillRoundedRectangle (r, t.px (8.0f));
    g.setColour (armed ? c.mod.brighter (0.3f) : c.outline);
    g.drawRoundedRectangle (r, t.px (8.0f), 1.0f);
    g.setColour (armed ? juce::Colours::white : c.text);
    g.setFont (t.valueFont());
    g.drawFittedText (juce::String (sourceChipLabel (sourceChoice)), getLocalBounds(), juce::Justification::centred, 1);

    if (const int n = ctx.assign.snapshot().destinationCount (sourceChoice); n > 0)
    {
        const float d = t.px (14.0f);
        const juce::Rectangle<float> badge (r.getRight() - d, r.getY() - d * 0.25f, d, d);
        g.setColour (armed ? juce::Colours::white : c.mod);
        g.fillEllipse (badge);
        g.setColour (armed ? c.mod : juce::Colours::white);
        g.setFont (t.font (d * 0.72f, true));
        g.drawText (juce::String (n), badge.toNearestInt(), juce::Justification::centred, false);
    }
}

void ModSourceChip::mouseDown (const juce::MouseEvent& e)
{
    pressed = true;
    consumed = false;
    if (e.mods.isPopupMenu())
    {
        consumed = true;
        showAssignments();
        return;
    }
    startTimer (kLongPressMs);
}

void ModSourceChip::mouseUp (const juce::MouseEvent& e)
{
    stopTimer();
    if (! pressed)
        return;
    pressed = false;
    if (! consumed && getLocalBounds().contains (e.getPosition()))
        ctx.assign.tapSource (sourceChoice);
}

void ModSourceChip::timerCallback()
{
    stopTimer();
    consumed = true;
    showAssignments();
}

void ModSourceChip::showAssignments()
{
    const auto list = ctx.assign.snapshot().entriesForSource (sourceChoice);
    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String (modSourceTable()[sourceChoice - 1].name));
    if (list.empty())
        menu.addItem (999, "Not assigned", false, false);

    int id = 1;
    for (const auto& [dest, e] : list)
    {
        const juce::String name (paramTable()[static_cast<size_t> (dest)].name);
        menu.addItem (id++, (e.bypass ? "Enable   " : "Bypass   ") + name);
        menu.addItem (id++, "Remove   " + name);
    }
    if (! list.empty())
        menu.addItem (1000, "Remove all");

    std::string prefix;
    std::string familyName;
    int familyIndex = 0;
    int familyCount = 0;

    if (sourceChoice >= 1 && sourceChoice <= 6)
    {
        prefix = "env" + std::to_string (sourceChoice);
        familyName = "Envelope";
        familyIndex = sourceChoice;
        familyCount = 6;
    }
    else if (sourceChoice >= 7 && sourceChoice <= 14)
    {
        prefix = "lfo" + std::to_string (sourceChoice - 6);
        familyName = "LFO";
        familyIndex = sourceChoice - 6;
        familyCount = 8;
    }
    else if (sourceChoice == 33)
    {
        prefix = "lfo9";
        familyName = "LFO";
        familyIndex = 9;
        familyCount = 10;
    }
    else if (sourceChoice == 34)
    {
        prefix = "lfo10";
        familyName = "LFO";
        familyIndex = 10;
        familyCount = 10;
    }
    else if (sourceChoice >= 15 && sourceChoice <= 18)
    {
        prefix = "rlfo" + std::to_string (sourceChoice - 14);
        familyName = "Random LFO";
        familyIndex = sourceChoice - 14;
        familyCount = 4;
    }

    if (! prefix.empty())
    {
        menu.addSeparator();
        menu.addSectionHeader ("Module operations");
        menu.addItem (2001, "Copy " + juce::String (familyName) + " " + juce::String (familyIndex));
        menu.addItem (2002, "Copy with modulation");
        const auto& clip = ModuleClipboard::instance();
        const bool canPaste = clip.canPasteTo (prefix);
        menu.addItem (2003, "Paste", canPaste);
        menu.addItem (2004, "Paste with modulation", canPaste);

        juce::PopupMenu swapMenu;
        for (int other = 1; other <= familyCount; ++other)
            if (other != familyIndex)
                swapMenu.addItem (3000 + other, juce::String (familyName) + " " + juce::String (other));
        menu.addSubMenu ("Swap with...", swapMenu);
        menu.addItem (2005, "Init (reset to default)");
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safe = juce::Component::SafePointer<ModSourceChip> (this), list, prefix, familyName, familyCount] (int result)
                        {
                            if (result <= 0 || safe == nullptr)
                                return;
                            auto& assign = safe->ctx.assign;
                            if (result == 1000)
                            {
                                for (const auto& item : list)
                                    clearSlot (assign.matrix(), item.second.slot);
                            }
                            else if (result >= 1 && result <= static_cast<int> (list.size() * 2))
                            {
                                const auto& item = list[static_cast<size_t> ((result - 1) / 2)];
                                if ((result - 1) % 2 == 0)
                                    setSlotBypass (assign.matrix(), item.second.slot, ! item.second.bypass);
                                else
                                    clearSlot (assign.matrix(), item.second.slot);
                            }
                            else if (result == 2001)
                            {
                                const auto live = readLiveValues (safe->ctx);
                                ModuleClipboard::instance().copy (prefix, live, false);
                            }
                            else if (result == 2002)
                            {
                                const auto live = readLiveValues (safe->ctx);
                                ModuleClipboard::instance().copy (prefix, live, true, &assign.matrix());
                            }
                            else if (result == 2003)
                            {
                                auto live = readLiveValues (safe->ctx);
                                if (ModuleClipboard::instance().paste (prefix, live, false))
                                    applyValues (safe->ctx, live);
                            }
                            else if (result == 2004)
                            {
                                auto live = readLiveValues (safe->ctx);
                                if (ModuleClipboard::instance().paste (prefix, live, true, &assign.matrix()))
                                    applyValues (safe->ctx, live);
                            }
                            else if (result == 2005)
                            {
                                auto live = readLiveValues (safe->ctx);
                                ModuleClipboard::initModule (prefix, live);
                                applyValues (safe->ctx, live);
                            }
                            else if (result >= 3001 && result <= 3000 + familyCount)
                            {
                                const int otherIndex = result - 3000;
                                std::string otherPrefix;
                                if (familyName == "Envelope") otherPrefix = "env" + std::to_string (otherIndex);
                                else if (familyName == "LFO") otherPrefix = "lfo" + std::to_string (otherIndex);
                                else if (familyName == "Random LFO") otherPrefix = "rlfo" + std::to_string (otherIndex);
                                if (! otherPrefix.empty())
                                {
                                    auto live = readLiveValues (safe->ctx);
                                    if (ModuleClipboard::swapModules (prefix, otherPrefix, live, true, &assign.matrix()))
                                        applyValues (safe->ctx, live);
                                }
                            }
                            assign.refresh();
                            assign.sendChangeMessage();
                        });
}

// ---- ModSourceBar -----------------------------------------------------------------------------------------------------------------

class ModSourceBar::Row final : public juce::Component
{
public:
    explicit Row (UiContext& context)
    {
        for (int s = 1; s <= kNumModSources; ++s)
        {
            chips.push_back (std::make_unique<ModSourceChip> (context, s));
            addAndMakeVisible (*chips.back());
        }
    }

    int layoutWidth() const
    {
        int x = 0;
        for (const auto& c : chips)
            x += c->preferredSize().x;
        return x;
    }

    void resized() override
    {
        int x = 0;
        for (auto& c : chips)
        {
            const int w = c->preferredSize().x;
            c->setBounds (x, 0, w, getHeight());
            x += w;
        }
    }

    std::vector<std::unique_ptr<ModSourceChip>> chips;
};

ModSourceBar::ModSourceBar (UiContext& context) : ctx (context)
{
    row = std::make_unique<Row> (context);
    viewport.setViewedComponent (row.get(), false);
    viewport.setScrollBarsShown (false, false, false, true);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
    addAndMakeVisible (viewport);

    done.onClick = [this] { ctx.assign.exitAssignMode(); };
    addAndMakeVisible (done);
    ctx.assign.addChangeListener (this);
}

ModSourceBar::~ModSourceBar()
{
    ctx.assign.removeChangeListener (this);
}

int ModSourceBar::preferredHeight() const
{
    return ctx.theme.touch() + ctx.theme.ipx (16.0f);
}

ModSourceChip* ModSourceBar::chipFor (int sourceChoice) const
{
    return sourceChoice >= 1 && sourceChoice <= kNumModSources ? row->chips[static_cast<size_t> (sourceChoice - 1)].get() : nullptr;
}

void ModSourceBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    status = ctx.assign.message();
    repaint();
}

void ModSourceBar::resized()
{
    auto r = getLocalBounds();
    auto statusLine = r.removeFromTop (ctx.theme.ipx (16.0f));
    (void) statusLine;
    done.setBounds (r.removeFromLeft (juce::jmax (ctx.theme.touch() + ctx.theme.ipx (8.0f), ctx.theme.ipx (64.0f))));
    viewport.setBounds (r);
    row->setSize (juce::jmax (row->layoutWidth(), r.getWidth()), r.getHeight());
}

void ModSourceBar::paint (juce::Graphics& g)
{
    g.setColour (status.startsWithIgnoreCase ("Mod Matrix full") ? ctx.theme.colours.warn : ctx.theme.colours.textDim);
    g.setFont (ctx.theme.labelFont());
    g.drawFittedText (status, getLocalBounds().removeFromTop (ctx.theme.ipx (16.0f)).reduced (ctx.theme.ipx (4.0f), 0),
                      juce::Justification::centredLeft, 1);
}
} // namespace synthyboy::ui
