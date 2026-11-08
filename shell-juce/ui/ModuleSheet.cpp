// SynthyBoy - UI: the module sheet (see ModuleSheet.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ModuleSheet.h"

#include <cstdlib>

namespace synthyboy::ui
{
namespace module_ops
{
namespace
{
struct Family
{
    const char* stem;
    const char* label;
    int count;
};
constexpr Family kFamilies[] = { { "osc", "OSC", 3 }, { "filter", "FILTER", 2 }, { "env", "ENV", 6 }, { "lfo", "LFO", 10 }, { "rlfo", "RANDOM LFO", 4 } };

bool parse (const std::string& prefix, const Family*& family, int& number)
{
    for (const auto& f : kFamilies)
    {
        const std::string stem (f.stem);
        if (prefix.size() > stem.size() && prefix.compare (0, stem.size(), stem) == 0)
        {
            const std::string rest = prefix.substr (stem.size());
            if (rest.find_first_not_of ("0123456789") != std::string::npos)
                continue; // "lfo" must not claim "rlfo1" nor the other way round
            const int n = std::atoi (rest.c_str());
            if (n >= 1 && n <= f.count)
            {
                family = &f;
                number = n;
                return true;
            }
        }
    }
    return false;
}
} // namespace

bool isModule (const std::string& prefix)
{
    const Family* f = nullptr;
    int n = 0;
    return parse (prefix, f, n);
}

juce::String displayName (const std::string& prefix)
{
    const Family* f = nullptr;
    int n = 0;
    return parse (prefix, f, n) ? juce::String (f->label) + " " + juce::String (n) : juce::String();
}

std::vector<std::string> siblings (const std::string& prefix)
{
    std::vector<std::string> out;
    const Family* f = nullptr;
    int n = 0;
    if (parse (prefix, f, n))
        for (int i = 1; i <= f->count; ++i)
            if (i != n)
                out.push_back (std::string (f->stem) + std::to_string (i));
    return out;
}

bool copy (UiContext& ctx, const std::string& prefix, bool withMods)
{
    if (! isModule (prefix))
        return false;
    ModuleClipboard::instance().copy (prefix, readLiveValues (ctx), withMods, withMods ? &ctx.assign.matrix() : nullptr);
    return ModuleClipboard::instance().hasContent();
}

bool paste (UiContext& ctx, const std::string& prefix, bool withMods)
{
    if (! isModule (prefix) || ! ModuleClipboard::instance().canPasteTo (prefix))
        return false;
    auto live = readLiveValues (ctx);
    if (! ModuleClipboard::instance().paste (prefix, live, withMods, withMods ? &ctx.assign.matrix() : nullptr))
        return false;
    applyValues (ctx, live);
    ctx.assign.refresh();
    ctx.assign.sendChangeMessage();
    return true;
}

bool init (UiContext& ctx, const std::string& prefix)
{
    if (! isModule (prefix))
        return false;
    auto live = readLiveValues (ctx);
    ModuleClipboard::initModule (prefix, live);
    applyValues (ctx, live);
    return true;
}

bool swap (UiContext& ctx, const std::string& a, const std::string& b, bool withMods)
{
    if (! isModule (a) || ! isModule (b))
        return false;
    auto live = readLiveValues (ctx);
    if (! ModuleClipboard::swapModules (a, b, live, withMods, withMods ? &ctx.assign.matrix() : nullptr))
        return false;
    applyValues (ctx, live);
    ctx.assign.refresh();
    ctx.assign.sendChangeMessage();
    return true;
}

bool copyOnto (UiContext& ctx, const std::string& from, const std::string& to, bool withMods)
{
    if (! isModule (from) || ! isModule (to) || from == to)
        return false;
    ModuleClipboard scratch; // the user's clipboard stays as it is
    scratch.copy (from, readLiveValues (ctx), withMods, withMods ? &ctx.assign.matrix() : nullptr);
    auto live = readLiveValues (ctx);
    if (! scratch.paste (to, live, withMods, withMods ? &ctx.assign.matrix() : nullptr))
        return false;
    applyValues (ctx, live);
    ctx.assign.refresh();
    ctx.assign.sendChangeMessage();
    return true;
}
} // namespace module_ops

// ---- ModuleSheet --------------------------------------------------------------------------------------------------------------------

ModuleSheet::ModuleSheet (UiContext& context, const std::string& prefixIn) : ctx (context), modulePrefix (prefixIn)
{
    const auto add = [this] (const juce::String& text, std::function<void()> handler) -> juce::TextButton*
    {
        auto* b = buttons.add (new juce::TextButton (text));
        b->onClick = [this, fn = std::move (handler)]
        {
            fn();
            closeSheet();
        };
        addAndMakeVisible (b);
        return b;
    };

    add ("Copy", [this] { module_ops::copy (ctx, modulePrefix, false); });
    add ("Copy with mods", [this] { module_ops::copy (ctx, modulePrefix, true); });
    add ("Paste", [this] { module_ops::paste (ctx, modulePrefix, false); });
    add ("Paste with mods", [this] { module_ops::paste (ctx, modulePrefix, true); });

    swapLabel.setText ("Swap with", juce::dontSendNotification);
    swapLabel.setFont (ctx.theme.labelFont());
    swapLabel.setColour (juce::Label::textColourId, ctx.theme.colours.textDim);
    addAndMakeVisible (swapLabel);
    for (const auto& other : module_ops::siblings (modulePrefix))
    {
        const juce::String text = other.substr (other.find_first_of ("0123456789")).c_str();
        auto* b = add (text, [this, other] { module_ops::swap (ctx, modulePrefix, other, true); });
        swapButtons.emplace_back (other, b);
    }

    add ("Init", [this] { module_ops::init (ctx, modulePrefix); });
    auto* lock = add ("Lock module", [] {});
    lock->setEnabled (false); // GAP (01 section 1.4): the engine has no lock key
    lock->setTooltip ("not available in this engine");
    refreshEnabled();
}

void ModuleSheet::refreshEnabled()
{
    const bool canPaste = ModuleClipboard::instance().canPasteTo (modulePrefix);
    if (auto* b = button ("Paste"))
        b->setEnabled (canPaste);
    if (auto* b = button ("Paste with mods"))
        b->setEnabled (canPaste);
}

juce::TextButton* ModuleSheet::button (const juce::String& text)
{
    for (auto* b : buttons)
        if (b->getButtonText() == text)
            return b;
    return nullptr;
}

juce::TextButton* ModuleSheet::swapButton (const std::string& siblingPrefix)
{
    for (auto& s : swapButtons)
        if (s.first == siblingPrefix)
            return s.second;
    return nullptr;
}

int ModuleSheet::preferredWidth() const
{
    return juce::jmax (ctx.theme.ipx (300.0f), ctx.theme.touch() * 5 + ctx.theme.gap() * 6);
}

int ModuleSheet::preferredHeight() const
{
    const auto& t = ctx.theme;
    const int perRow = juce::jmax (1, (preferredWidth() - t.gap()) / (t.touch() + t.gap()));
    const int swapRows = (static_cast<int> (swapButtons.size()) + perRow - 1) / perRow;
    return t.touch() /*title*/ + 2 * (t.touch() + t.gap()) + t.touch() /*swap label*/ + swapRows * (t.touch() + t.gap()) + (t.touch() + t.gap()) + t.gap();
}

void ModuleSheet::resized()
{
    const auto& t = ctx.theme;
    const int gap = t.gap(), h = t.touch();
    auto r = getLocalBounds().reduced (gap);
    r.removeFromTop (h); // title (painted)

    const auto row2 = [&] (const char* a, const char* b)
    {
        auto row = r.removeFromTop (h);
        r.removeFromTop (gap);
        const int w = (row.getWidth() - gap) / 2;
        button (a)->setBounds (row.removeFromLeft (w));
        row.removeFromLeft (gap);
        button (b)->setBounds (row);
    };
    row2 ("Copy", "Copy with mods");
    row2 ("Paste", "Paste with mods");

    swapLabel.setBounds (r.removeFromTop (h));
    const int perRow = juce::jmax (1, (r.getWidth() + gap) / (h + gap));
    int col = 0;
    auto line = r.removeFromTop (h);
    for (size_t i = 0; i < swapButtons.size(); ++i)
    {
        if (col == perRow)
        {
            col = 0;
            r.removeFromTop (gap);
            line = r.removeFromTop (h);
        }
        swapButtons[i].second->setBounds (line.removeFromLeft (h));
        line.removeFromLeft (gap);
        ++col;
    }
    r.removeFromTop (gap);
    row2 ("Init", "Lock module");
}

void ModuleSheet::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    g.fillAll (t.colours.panel);
    g.setColour (t.colours.text);
    g.setFont (t.titleFont());
    g.drawFittedText (module_ops::displayName (modulePrefix), getLocalBounds().reduced (t.gap()).removeFromTop (t.touch()),
                      juce::Justification::centredLeft, 1);
}

void ModuleSheet::closeSheet()
{
    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
}

void showModuleSheet (UiContext& context, const std::string& prefix, juce::Component& anchor)
{
    if (! module_ops::isModule (prefix))
        return;
    auto sheet = std::make_unique<ModuleSheet> (context, prefix);
    sheet->setSize (sheet->preferredWidth(), sheet->preferredHeight());
    auto& box = juce::CallOutBox::launchAsynchronously (std::move (sheet), anchor.getScreenBounds(), nullptr);
    (void) box;
}
} // namespace synthyboy::ui
