// SynthyBoy - bitmap GUI: the UiHost of the native CLAP shell (see NativeUiHost.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "NativeUiHost.h"

#include <algorithm>
#include <filesystem>

#include "synthyboy/offline_render.h"

namespace synthyboy::bitmap
{
NativeUiHost::NativeUiHost (SynthyBoyClap& owner, const safian_host_bitmap_gui_t* hostExtension)
    : plugin (owner), hostExt (hostExtension), presetBrowser (owner.userDir)
{
    store.setAllFromHost (plugin.currentValues());
    // an edit of the GUI is posted to the audio thread (handled like host automation there, reported to the host as gesture + value events)
    store.onUiChange = [this] (int index, float value)
    {
        plugin.ui.postValue (static_cast<size_t> (index), value);
        plugin.ui.publishQuiet (static_cast<size_t> (index), value); // the host reads the value at once (params.get_value), the GUI already shows it
        plugin.requestFlush();                                       // a host that does not process (yet) is asked to call params.flush
    };
    store.onUiGesture = [this] (int index, bool starting)
    {
        plugin.ui.postGesture (static_cast<size_t> (index), starting);
        plugin.requestFlush();
    };
    keyboard.addListener (this);
    plugin.ui.guiActive.store (true, std::memory_order_relaxed);
}

NativeUiHost::~NativeUiHost()
{
    plugin.ui.guiActive.store (false, std::memory_order_relaxed);
    keyboard.removeListener (this);
    // keys still held when the GUI goes away must not hang
    for (int channel = 1; channel <= 16; ++channel)
        for (int note = 0; note < 128; ++note)
            if (keyboard.isNoteOn (channel, note))
                plugin.ui.postMidi (static_cast<uint8_t> (0x80 | (channel - 1)), static_cast<uint8_t> (note), 0);
    plugin.ui.postMidi (0xB0, 123, 0); // all notes off
    plugin.requestFlush();
}

void NativeUiHost::pullChangesFromHost()
{
    plugin.ui.takeChanges ([this] (size_t index, float value)
    {
        if (index < paramCount())
            store.setFromHost (static_cast<int> (index), value);
    });
}

// ---- keyboard, wheels -------------------------------------------------------------------------------------------------------------------------

void NativeUiHost::handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity)
{
    const auto v = static_cast<uint8_t> (std::clamp (static_cast<int> (velocity * 127.0f + 0.5f), 1, 127));
    plugin.ui.postMidi (static_cast<uint8_t> (0x90 | ((channel - 1) & 15)), static_cast<uint8_t> (note & 127), v);
    plugin.requestFlush();
}

void NativeUiHost::handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity)
{
    const auto v = static_cast<uint8_t> (std::clamp (static_cast<int> (velocity * 127.0f + 0.5f), 0, 127));
    // a note off is never lost: when the ring is full the oldest wait is on the audio thread, the retry costs nothing
    for (int attempt = 0; attempt < 4 && ! plugin.ui.postMidi (static_cast<uint8_t> (0x80 | ((channel - 1) & 15)), static_cast<uint8_t> (note & 127), v); ++attempt)
        plugin.requestFlush();
    plugin.requestFlush();
}

void NativeUiHost::injectMidiMessage (const juce::MidiMessage& message)
{
    const int size = message.getRawDataSize();
    if (size < 1 || size > 3)
        return;
    const auto* data = message.getRawData();
    plugin.ui.postMidi (data[0], size > 1 ? data[1] : uint8_t { 0 }, size > 2 ? data[2] : uint8_t { 0 });
    plugin.requestFlush();
}

// ---- presets, patches --------------------------------------------------------------------------------------------------------------------------

void NativeUiHost::stepPreset (int direction)
{
    PresetBrowser::Loaded loaded;
    if (presetBrowser.size() == 0)
        presetBrowser.refresh();
    if (! presetBrowser.step (direction, loaded))
        return;
    loadPatch (loaded.values, loaded.content, loaded.midiMap.empty() ? nullptr : &loaded.midiMap);
}

void NativeUiHost::applyMapOfPresetChange (const ui::MidiMapBindings* presetMidiMap)
{
    if (plugin.prefs.loadMidiMapFromPresets && presetMidiMap != nullptr && ! presetMidiMap->empty())
    {
        plugin.midiMap.setBindings (*presetMidiMap);
        return;
    }
    plugin.applyDefaultMidiMap (true);
}

bool NativeUiHost::loadPatch (const ParamValues& values, const StateContent& newContent, const ui::MidiMapBindings* presetMidiMap)
{
    if (! validateContent (newContent))
        return false;
    plugin.applyPatch (values, newContent, true);
    applyMapOfPresetChange (presetMidiMap);
    return true;
}

bool NativeUiHost::initPatch()
{
    auto values = ParamValues::defaults();
    StateContent initContent;
    loadUserDefault (plugin.userDir, values, initContent); // untouched (the factory init patch) when there is no default
    plugin.applyPatch (values, std::move (initContent), true);
    applyMapOfPresetChange (nullptr); // Init Preset loads the default MIDI map (S p.315)
    presetBrowser.setCurrentName ("Init");
    return true;
}

bool NativeUiHost::revertToSaved()
{
    plugin.applyPatch (plugin.savedValues, plugin.savedContent, false);
    return true;
}

void NativeUiHost::reloadContent()
{
    if (plugin.engine)
        plugin.engine->setContent (plugin.content);
}

void NativeUiHost::setModuleLocked (const std::string& prefix, bool locked)
{
    const bool before = isLocked (plugin.locks, prefix);
    if (setLocked (plugin.locks, prefix, locked) && before != isLocked (plugin.locks, prefix))
        plugin.markStateDirty();
}

// ---- MIDI learn --------------------------------------------------------------------------------------------------------------------------------

void NativeUiHost::startMidiLearn (const std::string& key)
{
    const auto* info = findParamByKey (key.c_str());
    plugin.midiMap.armLearn (info != nullptr ? findParamIndex (info->id) : -1);
}

std::string NativeUiHost::midiLearnKey() const
{
    const int armed = plugin.midiMap.armedParam();
    return armed >= 0 && static_cast<size_t> (armed) < paramCount() ? std::string (paramTable()[armed].key) : std::string();
}

int NativeUiHost::midiCcForKey (const std::string& key) const
{
    const auto* info = findParamByKey (key.c_str());
    return info != nullptr ? plugin.midiMap.ccForParam (findParamIndex (info->id)) : -1;
}

void NativeUiHost::clearMidiCc (const std::string& key)
{
    if (const auto* info = findParamByKey (key.c_str()))
    {
        plugin.midiMap.clearParam (findParamIndex (info->id));
        plugin.markStateDirty();
    }
}

int NativeUiHost::setMidiMapBindings (const ui::MidiMapBindings& bindings)
{
    const int taken = plugin.midiMap.setBindings (bindings);
    plugin.markStateDirty();
    return taken;
}

// ---- preferences, about -----------------------------------------------------------------------------------------------------------------------

void NativeUiHost::setPreferences (const Preferences& prefs)
{
    if (prefs == plugin.prefs)
        return;
    plugin.prefs = prefs;
    plugin.prefSilenceOnStop.store (prefs.silenceOnStop);
    savePreferences (plugin.userDir, plugin.prefs);
    plugin.prefsRev.fetch_add (1, std::memory_order_relaxed);
}

juce::String NativeUiHost::aboutText() const
{
   #ifdef SYNTHYBOY_VERSION_STRING
    const juce::String version (SYNTHYBOY_VERSION_STRING);
   #else
    const juce::String version ("dev");
   #endif
    const juce::String engineName = (engineCaps() & kCapVoiceControl) != 0 ? "vital DSP (AGPL-3.0)" : "own engine";
    int packs = 0; // installed content packs: the directories below <userDir>/packs
    std::error_code ec;
    if (! plugin.userDir.empty())
        for (std::filesystem::directory_iterator it ((std::filesystem::path (plugin.userDir) / "packs"), ec), end; ! ec && it != end; it.increment (ec))
            packs += it->is_directory (ec) ? 1 : 0;
    return "SynthyBoy " + version + " | built " + juce::String (__DATE__) + " | Engine: " + engineName + " | " + juce::String (paramCount()) + " parameters | content packs: "
           + juce::String (packs);
}

// ---- engine displays, offline render ---------------------------------------------------------------------------------------------------

void NativeUiHost::setReadoutTargets (const std::vector<int>& paramIndices)
{
    if (plugin.engine)
        plugin.engine->setReadoutTargets (paramIndices.data(), static_cast<int> (paramIndices.size()));
}

bool NativeUiHost::renderOffline (const OfflineRequest& request, OfflineAudio& out)
{
    if (! plugin.engine)
        return false;
    // the values of the parameter mirror: the engine only has them after the next audio block, a render right after a gesture must hear them
    auto values = std::make_unique<ParamValues> (plugin.currentValues());
    OfflineRequest copy = request;
    if (copy.values == nullptr)
        copy.values = values.get();
    EngineKind kind = EngineKind::own;
   #if SYNTHYBOY_HAS_VITAL
    kind = EngineKind::vital;
   #endif
    return renderOfflineAny (*plugin.engine, kind, &plugin.content, &plugin.contentStore, copy, out);
}

ui::UiHost::LastNote NativeUiHost::lastNote() const
{
    LastNote note;
    const int key = plugin.lastKey.load (std::memory_order_relaxed);
    if (key < 0)
        return note;
    note.valid = true;
    note.key = key;
    note.velocity = plugin.lastVelocity.load (std::memory_order_relaxed);
    const int64_t on = plugin.lastOnSample.load (std::memory_order_relaxed), off = plugin.lastOffSample.load (std::memory_order_relaxed);
    const int64_t end = off >= 0 ? off : plugin.samplesDone.load (std::memory_order_relaxed);
    note.heldSeconds = std::max (0.05, static_cast<double> (end - on) / std::max (plugin.sampleRate, 1.0));
    return note;
}

// ---- files: the host does it -----------------------------------------------------------------------------------------------------------------

bool NativeUiHost::requestFile (const ui::FileRequest& request, std::function<void (const juce::File&)> done)
{
    if (! canRequestFile() || ! done)
        return false;
    const uint32_t id = newRequestId();
    const juce::String title = request.title, wildcards = request.wildcards, suggested = request.suggestedName, purpose = request.purpose;
    safian_bitmap_gui_file_request_t native {};
    native.mode = request.mode == ui::FileRequest::Mode::save ? SAFIAN_BITMAP_GUI_FILE_SAVE : SAFIAN_BITMAP_GUI_FILE_OPEN;
    native.title = title.toRawUTF8();
    native.wildcards = wildcards.toRawUTF8();
    native.suggested_name = suggested.toRawUTF8();
    native.purpose = purpose.toRawUTF8();
    fileRequests[id] = std::move (done); // before the call: a host may answer from inside it
    if (hostExt->request_file (plugin.host, id, &native))
        return true;
    fileRequests.erase (id);
    return false;
}

bool NativeUiHost::shareFile (const juce::File& file, const juce::String& title, std::function<void (bool)> done)
{
    if (! canShareFiles() || ! file.existsAsFile())
        return false;
    const uint32_t id = newRequestId();
    const juce::String path = file.getFullPathName(), titleText = title;
    shareRequests[id] = std::move (done);
    if (hostExt->share_file (plugin.host, id, path.toRawUTF8(), titleText.toRawUTF8()))
        return true;
    shareRequests.erase (id);
    return false;
}

void NativeUiHost::requestDone (uint32_t requestId, uint32_t result, const char* path)
{
    if (const auto it = fileRequests.find (requestId); it != fileRequests.end())
    {
        auto callback = std::move (it->second);
        fileRequests.erase (it);
        if (result == SAFIAN_BITMAP_GUI_RESULT_OK && path != nullptr && callback)
        {
            const juce::File file (juce::String::fromUTF8 (path));
            const bool saveMode = false; // an open request names an existing file; a save request names the target, which may not exist yet
            (void) saveMode;
            callback (file);
        }
        return;
    }
    if (const auto it = shareRequests.find (requestId); it != shareRequests.end())
    {
        auto callback = std::move (it->second);
        shareRequests.erase (it);
        if (callback)
            callback (result == SAFIAN_BITMAP_GUI_RESULT_OK);
    }
}
} // namespace synthyboy::bitmap
