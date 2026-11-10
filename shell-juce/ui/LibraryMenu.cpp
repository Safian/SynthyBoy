// SynthyBoy - UI: the menu of a user library category (LibraryMenu.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "LibraryMenu.h"

#include "ContentIO.h"
#include "Overlays.h"

namespace synthyboy::ui
{
namespace
{
TextPromptHandler& promptHandler()
{
    static TextPromptHandler handler;
    return handler;
}
} // namespace

void setTextPromptHandler (TextPromptHandler handler)
{
    promptHandler() = std::move (handler);
}

void askText (UiContext& context, const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)> done)
{
    if (promptHandler())
    {
        promptHandler() (title, initial, std::move (done));
        return;
    }
    showTextPrompt (context, title, "Name", initial, "Save", std::move (done));
}

LibraryMenu::LibraryMenu (UiContext& context, LibraryMenuSpec specIn) : ctx (context), spec (std::move (specIn)) {}

void LibraryMenu::say (const juce::String& text)
{
    lastMessage = text;
    if (onMessage)
        onMessage (text);
}

std::vector<LibraryEntry> LibraryMenu::entries() const
{
    if (auto* library = ctx.host.userLibrary())
        return library->list (spec.category);
    return {};
}

bool LibraryMenu::saveCurrent (const juce::String& name, bool overwrite)
{
    auto* library = ctx.host.userLibrary();
    if (library == nullptr || name.trim().isEmpty() || ! spec.makeItem)
        return false;
    const std::string json = spec.makeItem (name.trim());
    if (json.empty())
    {
        say ("Nothing to save");
        return false;
    }
    const bool ok = library->save (spec.category, name.trim().toStdString(), json, overwrite);
    say (ok ? "Saved " + spec.noun + " \"" + name.trim() + "\"" : "Could not save the " + spec.noun);
    return ok;
}

bool LibraryMenu::load (const juce::String& name)
{
    auto* library = ctx.host.userLibrary();
    std::string json;
    if (library == nullptr || ! library->load (spec.category, name.toStdString(), json))
    {
        say ("Could not read \"" + name + "\"");
        return false;
    }
    const bool ok = spec.applyItem && spec.applyItem (name, json);
    say (ok ? "Loaded " + spec.noun + " \"" + name + "\"" : "\"" + name + "\" is not a usable " + spec.noun);
    return ok;
}

bool LibraryMenu::setFavourite (const juce::String& name, bool favourite)
{
    auto* library = ctx.host.userLibrary();
    return library != nullptr && library->setFavourite (spec.category, name.toStdString(), favourite);
}

bool LibraryMenu::remove (const juce::String& name)
{
    auto* library = ctx.host.userLibrary();
    const bool ok = library != nullptr && library->remove (spec.category, name.toStdString());
    say (ok ? "Deleted \"" + name + "\"" : "Could not delete \"" + name + "\"");
    return ok;
}

void LibraryMenu::addTo (juce::PopupMenu& menu, int firstId)
{
    actions.clear();
    baseId = firstId;
    const auto add = [this] (std::function<void()> action)
    {
        actions.push_back (std::move (action));
        return baseId + static_cast<int> (actions.size()) - 1;
    };
    const bool haveLibrary = ctx.host.userLibrary() != nullptr;

    menu.addItem (add ([this]
    {
        askText (ctx, "Save " + spec.noun, spec.suggestedName, [self = shared_from_this()] (const juce::String& name) { self->saveCurrent (name); });
    }), "Save current " + spec.noun + " as...", haveLibrary && spec.makeItem != nullptr);

    if (spec.fileWildcards.isNotEmpty() && spec.applyFile)
        menu.addItem (add ([this]
        {
            FileRequest request;
            request.title = "Load a " + spec.noun + " from a file";
            request.wildcards = spec.fileWildcards;
            request.purpose = "library";
            auto self = shared_from_this();
            if (! ctx.host.requestFile (request, [self] (const juce::File& file)
                {
                    self->say (self->spec.applyFile (file) ? "Loaded " + file.getFileName() : "Could not use " + file.getFileName());
                }))
                say ("This host cannot open a file");
        }), "Load " + spec.noun + " from a file...", ctx.host.canRequestFile());

    const auto items = entries();
    if (! items.empty())
        menu.addSeparator();
    for (const auto& item : items)
    {
        const juce::String name (item.name);
        juce::PopupMenu sub;
        sub.addItem (add ([this, name] { load (name); }), "Load");
        sub.addItem (add ([this, name, fav = item.favourite] { setFavourite (name, ! fav); }), item.favourite ? "Remove from favourites" : "Add to favourites");
        sub.addItem (add ([this, name] { remove (name); }), "Delete");
        menu.addSubMenu ((item.favourite ? juce::String (juce::CharPointer_UTF8 ("\xE2\x98\x85 ")) : juce::String()) + name, sub);
    }
}

bool LibraryMenu::run (int id)
{
    const int index = id - baseId;
    if (index < 0 || index >= static_cast<int> (actions.size()))
        return false;
    auto action = actions[static_cast<size_t> (index)];
    action();
    return true;
}

void LibraryMenu::show (juce::Component& target)
{
    juce::PopupMenu menu;
    addTo (menu);
    menu.showMenuAsync (popupOptions (&target), [self = shared_from_this()] (int result)
    {
        if (result > 0)
            self->run (result);
    });
}
} // namespace synthyboy::ui
