/*
 * SynthyBoy - the CLAP extension "synthyboy.content" in the JUCE-wrapped CLAP build (REMAINDER item 3, docs/ui-design/13-new-keys.md 4.43)
 * Copyright (C) 2026 Safian Szabolcs
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The wrapper of clap-juce-extensions asks the processor for extensions it does not know through clapPluginExtension () (third_party/patches/
 * clap-juce-extensions-extension-hook.patch). The processor that the CLAP build creates is a SynthyBoyProcessor that also implements that hook and answers
 * "synthyboy.content" with the same functions as the native shell (shell-clap/synthyboy_content_service.cpp); BridgeHost is the processor as a ContentHost.
 * The VST3 build never creates it (createPluginFilter () asks clap_properties::building_clap).
 */
#include <clap-juce-extensions/clap-juce-extensions.h>

#include <cstring>
#include <map>
#include <mutex>

#include "PluginProcessor.h"
#include "synthyboy/preset.h"
#include "synthyboy_content_service.h"

namespace
{
using namespace synthyboy;

/** The processor as the ContentHost: the working copies of the values and the content are taken from the parameter tree and the patch at the start of every call, the setters write
    the parameters as host parameter changes (the processor hands them to the engine with the next block) and the content through setContent (). */
class BridgeHost final : public ContentHost
{
public:
    explicit BridgeHost (SynthyBoyProcessor& processor) : proc (processor), workValues (ParamValues::defaults()) {}

    void begin() override
    {
        workValues = proc.allValues();
        workContent = proc.content();
    }
    UserContentStore& store() override { return *proc.contentLibrary(); }
    EngineCapsMask engineCaps() override { return proc.engineCaps(); }
    ParamValues& values() override { return workValues; }
    StateContent& content() override { return workContent; }
    const Preferences& prefs() override { return proc.preferences(); }

    void setValue (const ParamInfo& info, float constrained) override
    {
        workValues.plain[static_cast<size_t> (findParamIndex (info.id))] = constrained;
        write (info, constrained);
    }

    void applyAllValues() override
    {
        const ParamValues current = proc.allValues();
        for (size_t i = 0; i < paramCount(); ++i)
            if (! juce::exactlyEqual (workValues.plain[i], current.plain[i]))
                write (paramTable()[i], workValues.plain[i]);
    }

    void contentChanged() override { proc.setContent (workContent); }
    void contentRefresh() override { proc.reloadContent(); }
    void valuesRescan() override {}   // every change went to the host as a parameter change already

    bool renderOffline (const OfflineRequest& request, OfflineAudio& audio) override { return proc.renderOffline (request, audio); }

    LastNote lastNote() override
    {
        const auto note = proc.lastNote();
        LastNote out;
        out.valid = note.valid;
        out.key = note.key;
        out.velocity = note.velocity;
        out.heldSeconds = note.heldSeconds;
        return out;
    }
    double tempoBpm() override { return proc.tempoBpm(); }

private:
    void write (const ParamInfo& info, float plain)
    {
        if (auto* parameter = proc.getValueTreeState().getParameter (info.key))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
    }

    SynthyBoyProcessor& proc;
    ParamValues workValues;
    StateContent workContent;
};

std::mutex s_registryMutex;
std::map<const clap_plugin_t*, ContentHost*> s_registry;

ContentHost* resolve (const clap_plugin_t* plugin)
{
    const std::lock_guard<std::mutex> lock (s_registryMutex);
    const auto it = s_registry.find (plugin);
    return it != s_registry.end() ? it->second : nullptr;
}

class BridgeProcessor final : public SynthyBoyProcessor,
                              public clap_juce_extensions::clap_juce_audio_processor_capabilities
{
public:
    BridgeProcessor() : host (*this) {}

    ~BridgeProcessor() override
    {
        const std::lock_guard<std::mutex> lock (s_registryMutex);
        for (auto it = s_registry.begin(); it != s_registry.end();)
            it = it->second == &host ? s_registry.erase (it) : std::next (it);
    }

    const void* clapPluginExtension (const clap_plugin* plugin, const char* id) noexcept override
    {
        if (id == nullptr || std::strcmp (id, SYNTHYBOY_EXT_CONTENT) != 0)
            return nullptr;
        {
            const std::lock_guard<std::mutex> lock (s_registryMutex);
            s_registry[plugin] = &host;
        }
        setContentHostResolver (resolve);
        return contentExtension();
    }

    bool supportsPresetLoad() const noexcept override
    {
        return true;
    }

    bool presetLoadFromLocation (uint32_t location_kind, const char* location,
                                 const char* load_key) noexcept override
    {
        if (load_key != nullptr && load_key[0] != '\0')
        {
            const int idx = findProgramIndex (juce::String::fromUTF8 (load_key));
            if (idx >= 0)
            {
                setCurrentProgram (idx);
                return true;
            }
        }

        if (location != nullptr && location[0] != '\0')
        {
            const juce::String locStr = juce::String::fromUTF8 (location);
            const int idx = findProgramIndex (locStr);
            if (idx >= 0)
            {
                setCurrentProgram (idx);
                return true;
            }

            if (location_kind == 0 /* CLAP_PRESET_DISCOVERY_LOCATION_FILE */)
            {
                const juce::File f (locStr);
                if (f.existsAsFile())
                {
                    juce::MemoryBlock mb;
                    if (f.loadFileAsData (mb))
                    {
                        synthyboy::PresetMeta meta;
                        synthyboy::ParamValues values = synthyboy::ParamValues::defaults();
                        synthyboy::StateContent content;
                        auto res = synthyboy::loadPreset (mb.getData(), mb.getSize(), meta, values, content);
                        if (res.status == synthyboy::LoadStatus::ok || res.status == synthyboy::LoadStatus::newer_version)
                        {
                            loadPatch (values, content, nullptr);
                            setPresetNameText (f.getFileNameWithoutExtension());
                            return true;
                        }
                    }
                }
            }
        }

        return false;
    }

private:
    BridgeHost host;
};
} // namespace

juce::AudioProcessor* createClapBridgeProcessorIfBuildingClap(); // declared in PluginProcessor.cpp

juce::AudioProcessor* createClapBridgeProcessorIfBuildingClap()
{
    return clap_juce_extensions::clap_properties::building_clap ? new BridgeProcessor() : nullptr;
}
