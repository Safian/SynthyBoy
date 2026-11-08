// SynthyBoy - UI: long-press sheet.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Sheet.h"

#include <cmath>

namespace synthyboy::ui
{
/** One assigned modulation: "LFO 3  +35.0 %", a bypass toggle and a remove button. */
class ParamSheet::ModRow final : public juce::Component
{
public:
    ModRow (ParamSheet& owner, ModEntry entryToShow) : sheet (owner), entry (entryToShow)
    {
        bypass.setClickingTogglesState (true);
        bypass.setToggleState (entry.bypass, juce::dontSendNotification);
        bypass.onClick = [this]
        {
            setSlotBypass (sheet.ctx.assign.matrix(), entry.slot, bypass.getToggleState());
            sheet.ctx.assign.refresh();
        };
        remove.onClick = [this]
        {
            clearSlot (sheet.ctx.assign.matrix(), entry.slot);
            sheet.ctx.assign.refresh();
            sheet.ctx.assign.sendChangeMessage();
        };
        addAndMakeVisible (bypass);
        addAndMakeVisible (remove);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = sheet.ctx.theme;
        g.setColour (entry.amount >= 0.0f ? t.colours.mod : t.colours.modNegative);
        g.fillEllipse (0.0f, static_cast<float> (getHeight()) * 0.5f - t.px (5.0f), t.px (10.0f), t.px (10.0f));
        g.setColour (entry.bypass ? t.colours.textDim : t.colours.text);
        g.setFont (t.valueFont());
        const ParamInfo& amount = *findParamByKey ("mod01.amount");
        g.drawFittedText (juce::String (modSourceTable()[entry.source - 1].name) + "   " + juce::String (displayText (amount, entry.amount)),
                          getLocalBounds().withTrimmedLeft (t.ipx (16.0f)).withTrimmedRight (t.touch() * 2 + t.gap()),
                          juce::Justification::centredLeft, 1);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int w = sheet.ctx.theme.touch();
        remove.setBounds (r.removeFromRight (w));
        bypass.setBounds (r.removeFromRight (w));
    }

private:
    ParamSheet& sheet;
    ModEntry entry;
    juce::TextButton bypass { "byp" }, remove { "x" };
};

ParamSheet::ParamSheet (UiContext& context, const std::string& key) : ctx (context), handle (context, key)
{
    const auto& t = ctx.theme;
    title.setFont (t.titleFont());
    title.setText (handle.valid() ? juce::String (handle.info().name) : "Unknown key " + juce::String (key), juce::dontSendNotification);
    subtitle.setFont (t.labelFont());
    subtitle.setColour (juce::Label::textColourId, t.colours.textDim);
    subtitle.setText (juce::String (key) + (handle.valid() ? "   " + juce::String (handle.info().group) : juce::String()), juce::dontSendNotification);
    info.setFont (t.labelFont());
    info.setColour (juce::Label::textColourId, t.colours.textDim);

    valueBox.setFont (t.valueFont());
    valueBox.setJustification (juce::Justification::centred);
    valueBox.setSelectAllWhenFocused (true);
    valueBox.onReturnKey = [this]
    {
        applyValueText (valueBox.getText());
        refreshValue();
    };
    valueBox.onEscapeKey = [this] { refreshValue(); };

    minus.onClick = [this] { nudge (-1); };
    plus.onClick = [this] { nudge (+1); };
    reset.onClick = [this] { handle.resetToDefault(); };
    learn.setEnabled (ctx.host.canMidiLearn());
    learn.setTooltip (ctx.host.canMidiLearn() ? juce::String ("Move a MIDI controller") : juce::String ("MIDI learn is not available yet"));
    learn.onClick = [this]
    {
        ctx.host.startMidiLearn (handle.key());
        close();
    };
    assign.setEnabled (handle.valid() && modDestinationChoiceOf (static_cast<size_t> (handle.index())) > 0);
    assign.setTooltip (assign.isEnabled() ? juce::String ("Pick a modulation source, then drag the control") : juce::String ("This control cannot be modulated"));
    assign.onClick = [this] { showSourceMenu(); };

    for (auto* c : std::initializer_list<juce::Component*> { &title, &subtitle, &info, &valueBox, &minus, &plus, &reset, &learn, &assign })
        addAndMakeVisible (c);

    handle.onChange = [this] { refreshValue(); };
    ctx.assign.addChangeListener (this);
    refreshValue();
    rebuildModRows();
    setSize (preferredWidth(), preferredHeight());
}

ParamSheet::~ParamSheet()
{
    ctx.assign.removeChangeListener (this);
}

int ParamSheet::preferredWidth() const
{
    return ctx.theme.ipx (300.0f);
}

int ParamSheet::preferredHeight() const
{
    const auto& t = ctx.theme;
    const int pad = t.gap();
    return pad + t.ipx (22.0f) + t.ipx (16.0f) + pad + t.touch() + t.ipx (18.0f) + pad + t.touch() + pad
           + static_cast<int> (rows.size()) * t.touch() + pad;
}

void ParamSheet::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildModRows();
}

void ParamSheet::refreshValue()
{
    if (! handle.valid())
        return;
    if (! valueBox.hasKeyboardFocus (false))
        valueBox.setText (juce::String (displayText (handle.info(), handle.plain())), juce::dontSendNotification);
    const ParamInfo& pi = handle.info();
    info.setText ("default " + juce::String (displayText (pi, pi.def)) + "     range " + juce::String (displayText (pi, pi.min)) + " .. "
                      + juce::String (displayText (pi, pi.max)),
                  juce::dontSendNotification);
}

void ParamSheet::rebuildModRows()
{
    rows.clear();
    if (handle.valid())
        for (const auto& e : ctx.assign.snapshot().entriesFor (handle.index()))
        {
            auto* row = rows.add (new ModRow (*this, e));
            addAndMakeVisible (row);
        }
    setSize (preferredWidth(), preferredHeight());
    resized();
    repaint();
}

bool ParamSheet::applyValueText (const juce::String& text)
{
    if (! handle.valid() || text.trim().isEmpty())
        return false; // empty text + Enter = cancel
    // "1.2k" / "1.2 kHz" for Hz values: scaled here, because textToValue clamps the bare number into the range first
    juce::String entry = text.trim();
    if (handle.info().unit == Unit::Hz && (entry.endsWithIgnoreCase ("khz") || entry.endsWithIgnoreCase ("k")))
        entry = juce::String (entry.getDoubleValue() * 1000.0, 3);

    float v = handle.plain();
    if (! textToValue (handle.info(), entry.toRawUTF8(), v))
        return false;
    handle.setPlainComplete (v);
    return true;
}

void ParamSheet::nudge (int direction)
{
    if (! handle.valid())
        return;
    const ParamInfo& pi = handle.info();
    if (pi.scale == Scale::stepped || pi.interval > 0.0f)
    {
        const float step = pi.interval > 0.0f ? pi.interval : 1.0f;
        handle.setPlainComplete (handle.plain() + step * static_cast<float> (direction));
    }
    else
        handle.setPlainComplete (fromNormalised (pi, juce::jlimit (0.0f, 1.0f, toNormalised (pi, handle.plain()) + 0.01f * static_cast<float> (direction))));
}

void ParamSheet::showSourceMenu()
{
    juce::PopupMenu menu;
    const int sourceKey = findParamIndex (hashKey ("mod01.source"));
    for (int s = 1; s <= kNumModSources; ++s)
    {
        const bool ok = capStateForValue (sourceKey, static_cast<float> (s), ctx.host.engineCaps()).available;
        menu.addItem (s, juce::String (modSourceTable()[s - 1].name), ok, ctx.assign.flow().armedSource() == s);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&assign),
                        [safe = juce::Component::SafePointer<ParamSheet> (this)] (int result)
                        {
                            if (result <= 0 || safe == nullptr)
                                return;
                            auto& flow = safe->ctx.assign.flow();
                            if (flow.armedSource() != result)
                                safe->ctx.assign.tapSource (result);
                            safe->close();
                        });
}

void ParamSheet::close()
{
    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
}

void ParamSheet::paint (juce::Graphics&) {}

void ParamSheet::resized()
{
    const auto& t = ctx.theme;
    const int pad = t.gap();
    auto r = getLocalBounds().reduced (pad);
    title.setBounds (r.removeFromTop (t.ipx (22.0f)));
    subtitle.setBounds (r.removeFromTop (t.ipx (16.0f)));
    r.removeFromTop (pad);

    auto valueRow = r.removeFromTop (t.touch());
    minus.setBounds (valueRow.removeFromLeft (t.touch()));
    plus.setBounds (valueRow.removeFromRight (t.touch()));
    valueBox.setBounds (valueRow.reduced (pad, 0));
    info.setBounds (r.removeFromTop (t.ipx (18.0f)));
    r.removeFromTop (pad);

    auto buttons = r.removeFromTop (t.touch());
    const int bw = (buttons.getWidth() - 2 * pad) / 3;
    reset.setBounds (buttons.removeFromLeft (bw));
    buttons.removeFromLeft (pad);
    learn.setBounds (buttons.removeFromLeft (bw));
    buttons.removeFromLeft (pad);
    assign.setBounds (buttons);
    r.removeFromTop (pad);

    for (auto* row : rows)
        row->setBounds (r.removeFromTop (t.touch()));
}

void showParamSheet (UiContext& context, const std::string& key, juce::Component& anchor)
{
    auto sheet = std::make_unique<ParamSheet> (context, key);
    auto& box = juce::CallOutBox::launchAsynchronously (std::move (sheet), anchor.getScreenBounds(), nullptr);
    (void) box;
}
} // namespace synthyboy::ui
