// SynthyBoy - the CLAP extension "synthyboy.content" (shell-clap/synthyboy_content_ext.h) on top of a ContentHost, shared by the native shell and the JUCE-wrapped CLAP build.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The functions below were written for the native shell (WP-01 g, GROUPD) and moved here unchanged apart from the access to the plug-in: everything the plug-in provides
// (the library, the engine, the parameter values and the content, the last note) is a ContentHost, and enter () finds the host of a clap_plugin.

#include "synthyboy_content_service.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/offline_render.h"
#include "synthyboy/params.h"
#include "synthyboy/preferences.h"
#include "synthyboy/shell_features.h"
#include "synthyboy/state.h"
#include "synthyboy/user_content.h"
#include "synthyboy/wave_convert.h"
#include "synthyboy/vital_plugin_import.h"

namespace synthyboy
{
namespace
{
ContentHostResolver s_resolver = nullptr;

/** The host of the plug-in `p` with a fresh copy of its values and content, or null. */
ContentHost* enter (const clap_plugin_t* p) noexcept
{
    ContentHost* host = s_resolver != nullptr ? s_resolver (p) : nullptr;
    if (host != nullptr)
        host->begin();
    return host;
}

// synthyboy.content (shell-clap/synthyboy_content_ext.h): the user content library for a host without a file dialog
bool readWholeFile (const char* path, std::vector<uint8_t>& bytes)
{
    if (path == nullptr || path[0] == '\0')
        return false;
    std::ifstream in (path, std::ios::binary);
    if (! in)
        return false;
    bytes.assign ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    return ! bytes.empty() && bytes.size() <= kMaxContentFileBytes;
}

void copyText (char* dest, size_t capacity, const std::string& text)
{
    std::strncpy (dest, text.c_str(), capacity - 1);
    dest[capacity - 1] = '\0';
}

void fillItem (synthyboy_content_item_t& out, const UserContentItem& item)
{
    out = synthyboy_content_item_t {};
    out.kind = static_cast<uint32_t> (item.kind);
    copyText (out.name, sizeof out.name, item.ref.name);
    copyText (out.pack, sizeof out.pack, item.ref.pack);
    copyText (out.file, sizeof out.file, item.ref.file);
    copyText (out.sha256, sizeof out.sha256, item.ref.sha256);
    out.size_bytes = item.size;
    out.embedded = item.embedded ? 1u : 0u;
}

bool validKind (uint32_t kind) noexcept { return kind <= SYNTHYBOY_CONTENT_IMPULSE; }

/** The state changed outside the audio path: the engine gets the content, the host is told to save. */
void contentChanged (ContentHost* self)
{
    self->contentChanged();
}

uint32_t ext_content_count (const clap_plugin_t* p, uint32_t kind) noexcept
{
    ContentHost* self = enter (p);
    return self != nullptr && validKind (kind) ? static_cast<uint32_t> (self->store().list (static_cast<ContentKind> (kind)).size()) : 0u;
}

int32_t ext_content_get (const clap_plugin_t* p, uint32_t kind, uint32_t index, synthyboy_content_item_t* item) noexcept
{
    if (item == nullptr || ! validKind (kind))
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    ContentHost* self = enter (p);
    if (self == nullptr)
        return SYNTHYBOY_CONTENT_ERR_STATE;
    const auto items = self->store().list (static_cast<ContentKind> (kind));
    if (index >= items.size())
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    fillItem (*item, items[index]);
    return SYNTHYBOY_CONTENT_OK;
}

int32_t ext_content_import_file (const clap_plugin_t* p, uint32_t kind, const char* path, synthyboy_content_item_t* item) noexcept
{
    if (path == nullptr || ! validKind (kind))
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    ContentHost* self = enter (p);
    if (self == nullptr)
        return SYNTHYBOY_CONTENT_ERR_STATE;
    std::vector<uint8_t> bytes;
    if (! readWholeFile (path, bytes))
        return SYNTHYBOY_CONTENT_ERR_READ;
    const std::string name = fileStemOf (path);
    ContentFileRef ref;
    UserContentItem result;
    result.kind = static_cast<ContentKind> (kind);
    if (kind == SYNTHYBOY_CONTENT_WAVETABLE && vitalFileKindFromName (path) == VitalFileKind::wavetable)
    {
        const VitalPluginImport imported = importVitalFileForPlugin (path, bytes, self->store(), userDataDirectory(), self->engineCaps());
        if (! imported.ok)
            return SYNTHYBOY_CONTENT_ERR_FORMAT;
        ref = imported.tableRef;
    }
    else
    {
        SampleFacts facts;
        if (! probeSampleWav (bytes, path, facts))
            return SYNTHYBOY_CONTENT_ERR_FORMAT;
        if (! self->store().addFile (static_cast<ContentKind> (kind), name, bytes, ref))
            return SYNTHYBOY_CONTENT_ERR_STORE;
    }
    result.ref = ref;
    result.size = bytes.size();
    if (item != nullptr)
        fillItem (*item, result);
    return SYNTHYBOY_CONTENT_OK;
}

int32_t ext_content_attach (const clap_plugin_t* p, uint32_t kind, uint32_t index, uint32_t osc) noexcept
{
    if (! validKind (kind))
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    ContentHost* self = enter (p);
    if (self == nullptr)
        return SYNTHYBOY_CONTENT_ERR_STATE;
    const auto items = self->store().list (static_cast<ContentKind> (kind));
    if (index >= items.size())
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    const ContentFileRef& ref = items[index].ref;
    switch (kind)
    {
        case SYNTHYBOY_CONTENT_WAVETABLE:
            if (osc < 1 || osc > 3 || ! attachStoredWavetable (self->content(), static_cast<int> (osc), ref, self->store()))
                return osc < 1 || osc > 3 ? SYNTHYBOY_CONTENT_ERR_ARGUMENT : SYNTHYBOY_CONTENT_ERR_FORMAT;
            break;
        case SYNTHYBOY_CONTENT_SAMPLE:
            if (osc < 1 || osc > 3 || ! attachStoredSample (self->content(), &self->values(), static_cast<int> (osc), ref, self->store()))
                return osc < 1 || osc > 3 ? SYNTHYBOY_CONTENT_ERR_ARGUMENT : SYNTHYBOY_CONTENT_ERR_FORMAT;
            self->applyAllValues();
            break;
        default:
            attachImpulse (self->content(), ref);
            break;
    }
    if (! validateContent (self->content()))
        return SYNTHYBOY_CONTENT_ERR_FORMAT;
    contentChanged (self);
    return SYNTHYBOY_CONTENT_OK;
}

int32_t ext_content_remove (const clap_plugin_t* p, uint32_t kind, uint32_t index) noexcept
{
    if (! validKind (kind))
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    ContentHost* self = enter (p);
    if (self == nullptr)
        return SYNTHYBOY_CONTENT_ERR_STATE;
    const auto items = self->store().list (static_cast<ContentKind> (kind));
    if (index >= items.size())
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    if (! self->store().remove (items[index].ref))
        return SYNTHYBOY_CONTENT_ERR_STORE;
    self->contentRefresh(); // a reference to the removed file is now a missing file: the engine falls back
    return SYNTHYBOY_CONTENT_OK;
}

int32_t ext_content_import_vital (const clap_plugin_t* p, const char* path, char* summary, uint32_t capacity) noexcept
{
    if (path == nullptr)
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    ContentHost* self = enter (p);
    if (self == nullptr)
        return SYNTHYBOY_CONTENT_ERR_STATE;
    std::vector<uint8_t> bytes;
    if (! readWholeFile (path, bytes))
        return SYNTHYBOY_CONTENT_ERR_READ;
    const VitalPluginImport imported = importVitalFileForPlugin (path, bytes, self->store(), userDataDirectory(), self->engineCaps());
    const auto say = [&] (const std::string& text) { if (summary != nullptr && capacity > 0) copyText (summary, capacity, text); };
    if (! imported.ok)
    {
        say (imported.error);
        return SYNTHYBOY_CONTENT_ERR_FORMAT;
    }
    if (imported.bank)
    {
        say ("bank " + imported.name + ": " + std::to_string (imported.summary.written) + " written, " + std::to_string (imported.summary.skippedSame) + " already there, "
             + std::to_string (imported.summary.failed) + " failed");
        return SYNTHYBOY_CONTENT_OK;
    }
    if (imported.kind != VitalFileKind::preset)
    {
        say (imported.name + ": stored in the library"); // a table: use import_file / attach, a shape needs the editor
        return imported.kind == VitalFileKind::wavetable ? SYNTHYBOY_CONTENT_OK : SYNTHYBOY_CONTENT_ERR_FORMAT;
    }
    self->values() = imported.values;
    self->content() = imported.content;
    self->applyAllValues();
    contentChanged (self);
    self->valuesRescan();
    say (imported.name + ": " + std::to_string (imported.report.numImported) + " imported, " + std::to_string (imported.report.numApproximated) + " approximated, "
         + std::to_string (imported.report.numDropped) + " dropped");
    return SYNTHYBOY_CONTENT_OK;
}

// ---- GROUPD (WP-14 / 15 / 16): the conversions and offline renders of core/wave_convert.h through the library of this shell ----------------------

/** Sets a parameter of the stored values and of the engine; the host re-reads the values after the action (rescan). */
void setValue (ContentHost* self, const std::string& key, float plain)
{
    const ParamInfo* info = findParamByKey (key.c_str());
    if (info == nullptr)
        return;
    self->setValue (*info, constrainValue (*info, plain));
}

float valueOf (ContentHost* self, const std::string& key, float fallback = 0.0f)
{
    const ParamInfo* info = findParamByKey (key.c_str());
    return info != nullptr ? self->values().plain[static_cast<size_t> (findParamIndex (info->id))] : fallback;
}

void say (char* summary, uint32_t capacity, const std::string& text)
{
    if (summary != nullptr && capacity > 0)
        copyText (summary, capacity, text);
}

convert::OfflineRunner offlineRunner (ContentHost* self)
{
    return [self] (const OfflineRequest& request, OfflineAudio& audio)
    {
        OfflineRequest copy = request;
        copy.ultra = request.ultra || self->prefs().ultraRender; // "Use Ultra quality when rendering" (S p.315)
        if (copy.values == nullptr)
            copy.values = &self->values();
        return self->renderOffline (copy, audio);
    };
}

/** A new table for oscillator `osc`: the library, the state reference, oscN.type = Wavetable, oscN.wavetable = Preset table. */
bool installTable (ContentHost* self, int osc, const wtedit::Wavetable& table)
{
    const std::vector<uint8_t> wav = wavetableToWav (table);
    ContentFileRef ref;
    if (wav.empty() || ! self->store().addFile (ContentKind::wavetable, table.name().empty() ? std::string ("Table") : table.name(), wav, ref))
        return false;
    attachWavetable (self->content(), osc, ref, table.author(), static_cast<int> (table.numFrames()));
    const std::string o = "osc" + std::to_string (osc) + ".";
    setValue (self, o + "type", static_cast<float> (kOscTypeWavetable));
    setValue (self, o + "wavetable", static_cast<float> (kWavetablePresetTable));
    return true;
}

/** The action is done: the engine gets the content, the host the new values and the dirty flag. */
void actionDone (ContentHost* self)
{
    contentChanged (self);
    self->valuesRescan();
}

wtedit::ImportOptions importOptionsOf (uint32_t mode)
{
    wtedit::ImportOptions options;
    const auto& choices = convert::importChoices();
    options.mode = choices[std::min<size_t> (mode, choices.size() - 1)].mode;
    return options;
}

int32_t ext_content_switch_to_wavetable (const clap_plugin_t* p, uint32_t osc, uint32_t importMode, char* summary, uint32_t capacity) noexcept
{
    ContentHost* self = enter (p);
    if (self == nullptr)
        return SYNTHYBOY_CONTENT_ERR_STATE;
    if (osc < 1 || osc > 3 || importMode >= convert::importChoices().size())
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    const std::string o = "osc" + std::to_string (osc) + ".";
    const int type = static_cast<int> (std::lround (valueOf (self, o + "type")));
    if (type != kOscTypeSample && type != kOscTypeGranular && type != kOscTypeSpectral)
    {
        say (summary, capacity, "needs a Sample, Granular or Spectral oscillator");
        return SYNTHYBOY_CONTENT_ERR_STATE;
    }
    const ContentSample* sample = nullptr;
    for (const auto& s : self->content().samples)
        if (s.osc == static_cast<int> (osc))
            sample = &s;
    std::vector<uint8_t> bytes;
    if (sample == nullptr || ! self->store().readFile (sample->ref, bytes))
    {
        say (summary, capacity, "no sample is loaded in this oscillator");
        return SYNTHYBOY_CONTENT_ERR_STATE;
    }
    const auto preview = SamplePreview::create (bytes, sample->ref.name);
    if (preview == nullptr || preview->frames() <= 0)
        return SYNTHYBOY_CONTENT_ERR_FORMAT;
    wtedit::Wavetable table;
    std::string error;
    if (! convert::sampleRegionToWavetable (preview->mono().data(), preview->mono().size(), static_cast<double> (preview->sampleRate()), valueOf (self, o + "sample.start") * 0.01f,
                                            valueOf (self, o + "sample.end", 100.0f) * 0.01f, importOptionsOf (importMode), (sample->ref.name.empty() ? std::string ("Sample") : sample->ref.name) + " wavetable", table, &error))
    {
        say (summary, capacity, error);
        return SYNTHYBOY_CONTENT_ERR_FORMAT;
    }
    if (! installTable (self, static_cast<int> (osc), table))
        return SYNTHYBOY_CONTENT_ERR_STORE;
    setValue (self, o + "frame", 0.0f);
    actionDone (self);
    say (summary, capacity, "wavetable of " + std::to_string (table.numFrames()) + " frames");
    return SYNTHYBOY_CONTENT_OK;
}

int32_t ext_content_render_osc_warp (const clap_plugin_t* p, uint32_t osc, char* summary, uint32_t capacity) noexcept
{
    ContentHost* self = enter (p);
    if (self == nullptr)
        return SYNTHYBOY_CONTENT_ERR_STATE;
    if (osc < 1 || osc > 3)
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    convert::WarpRenderOptions options;
    options.osc = static_cast<int> (osc);
    wtedit::Wavetable table;
    std::string error;
    float scale = 1.0f;
    const std::string o = "osc" + std::to_string (osc) + ".";
    if (static_cast<int> (std::lround (valueOf (self, o + "type"))) != kOscTypeWavetable)
    {
        say (summary, capacity, "Render OSC Warp needs a wavetable oscillator");
        return SYNTHYBOY_CONTENT_ERR_STATE;
    }
    if (! convert::renderOscWarp (offlineRunner (self), &self->values(), options, "Osc " + std::string (osc == 1 ? "A" : (osc == 2 ? "B" : "C")) + " warp", table, &error, &scale))
    {
        say (summary, capacity, error);
        return error.empty() ? SYNTHYBOY_CONTENT_ERR_RENDER : SYNTHYBOY_CONTENT_ERR_STATE;
    }
    const float amount = valueOf (self, o + "warp.amount"), level = valueOf (self, o + "level", 50.0f);
    if (! installTable (self, static_cast<int> (osc), table))
        return SYNTHYBOY_CONTENT_ERR_STORE;
    for (const auto& change : convert::warpBakeParameterChanges (static_cast<int> (osc), amount, scale, level))
        setValue (self, change.first, change.second);
    actionDone (self);
    say (summary, capacity, std::to_string (table.numFrames()) + " frames across WARP");
    return SYNTHYBOY_CONTENT_OK;
}

int32_t ext_content_resample (const clap_plugin_t* p, uint32_t target, uint32_t importMode, char* summary, uint32_t capacity) noexcept
{
    ContentHost* self = enter (p);
    if (self == nullptr)
        return SYNTHYBOY_CONTENT_ERR_STATE;
    if (target > 3 || importMode >= convert::importChoices().size())
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    convert::ResampleOptions options;
    const ContentHost::LastNote last = self->lastNote();
    options.key = last.valid ? last.key : 60;
    options.velocity = last.valid ? last.velocity : 0.8f;
    options.bpm = self->tempoBpm();
    OfflineAudio audio;
    if (! convert::renderResample (offlineRunner (self), options, audio))
        return SYNTHYBOY_CONTENT_ERR_RENDER;
    wtedit::Wavetable left, right;
    std::string error;
    if (! convert::audioToWavetables (audio, importOptionsOf (importMode), "Resample", left, target == 0 ? &right : nullptr, &error))
    {
        say (summary, capacity, error);
        return SYNTHYBOY_CONTENT_ERR_STATE;
    }
    left.setName (target == 0 ? "Resample Osc A" : "Resample Osc " + std::string (target == 1 ? "A" : (target == 2 ? "B" : "C")));
    if (! installTable (self, target == 0 ? 1 : static_cast<int> (target), left))
        return SYNTHYBOY_CONTENT_ERR_STORE;
    if (target == 0)
    {
        right.setName ("Resample Osc B");
        if (! installTable (self, 2, right))
            return SYNTHYBOY_CONTENT_ERR_STORE;
    }
    actionDone (self);
    say (summary, capacity, "one bar, " + std::to_string (left.numFrames()) + " frames");
    return SYNTHYBOY_CONTENT_OK;
}

int32_t ext_content_render_last_note (const clap_plugin_t* p, const char* path, char* summary, uint32_t capacity) noexcept
{
    ContentHost* self = enter (p);
    if (self == nullptr)
        return SYNTHYBOY_CONTENT_ERR_STATE;
    if (path == nullptr || path[0] == '\0')
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    const ContentHost::LastNote last = self->lastNote();
    if (! last.valid)
    {
        say (summary, capacity, "play a note first");
        return SYNTHYBOY_CONTENT_ERR_STATE;
    }
    convert::NoteRender note;
    note.key = last.key;
    note.velocity = last.velocity;
    note.heldSeconds = std::clamp (last.heldSeconds, 0.25, 20.0);
    OfflineAudio audio;
    if (! convert::renderNote (offlineRunner (self), note, audio))
        return SYNTHYBOY_CONTENT_ERR_RENDER;
    const std::vector<uint8_t> wav = convert::audioToWav (audio, 24);
    std::error_code ec;
    std::filesystem::create_directories (std::filesystem::path (path).parent_path(), ec);
    std::ofstream out (path, std::ios::binary);
    if (wav.empty() || ! out)
        return SYNTHYBOY_CONTENT_ERR_STORE;
    out.write (reinterpret_cast<const char*> (wav.data()), static_cast<std::streamsize> (wav.size()));
    if (! out)
        return SYNTHYBOY_CONTENT_ERR_STORE;
    say (summary, capacity, std::to_string (audio.frames()) + " frames at " + std::to_string (static_cast<int> (audio.sampleRate)) + " Hz");
    return SYNTHYBOY_CONTENT_OK;
}

int32_t ext_content_wavetable_frame_to_lfo (const clap_plugin_t* p, uint32_t osc, uint32_t lfo, char* summary, uint32_t capacity) noexcept
{
    ContentHost* self = enter (p);
    if (self == nullptr)
        return SYNTHYBOY_CONTENT_ERR_STATE;
    if (osc < 1 || osc > 3 || lfo < 1 || lfo > static_cast<uint32_t> (kMaxContentLfoShapes))
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    convert::WarpRenderOptions options;
    options.osc = static_cast<int> (osc);
    options.frames = 2;
    options.rawFrame = true;
    wtedit::Wavetable one;
    std::string error;
    if (! convert::renderOscWarp (offlineRunner (self), &self->values(), options, "frame", one, &error) || one.numFrames() == 0)
    {
        say (summary, capacity, error);
        return SYNTHYBOY_CONTENT_ERR_STATE;
    }
    ContentLfoShape shape;
    float reached = 0.0f;
    if (! convert::frameToLfoShape (one.getFrame (0), static_cast<int> (lfo), shape, 0.002f, kMaxLfoShapePoints, &reached))
        return SYNTHYBOY_CONTENT_ERR_FORMAT;
    shape.name = "Osc " + std::string (osc == 1 ? "A" : (osc == 2 ? "B" : "C")) + " frame";
    auto& shapes = self->content().lfoShapes;
    shapes.erase (std::remove_if (shapes.begin(), shapes.end(), [lfo] (const ContentLfoShape& s) { return s.lfo == static_cast<int> (lfo); }), shapes.end());
    shapes.push_back (shape);
    std::sort (shapes.begin(), shapes.end(), [] (const ContentLfoShape& a, const ContentLfoShape& b) { return a.lfo < b.lfo; });
    setValue (self, "lfo" + std::to_string (lfo) + ".shape", static_cast<float> (kLfoShapeCustom));
    actionDone (self);
    say (summary, capacity, std::to_string (shape.points.size()) + " points");
    return SYNTHYBOY_CONTENT_OK;
}

int32_t ext_content_lfo_shape_to_wavetable (const clap_plugin_t* p, uint32_t lfo, uint32_t osc, char* summary, uint32_t capacity) noexcept
{
    ContentHost* self = enter (p);
    if (self == nullptr)
        return SYNTHYBOY_CONTENT_ERR_STATE;
    if (osc < 1 || osc > 3 || lfo < 1 || lfo > static_cast<uint32_t> (kMaxContentLfoShapes))
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    const std::string l = "lfo" + std::to_string (lfo) + ".shape";
    const int builtIn = static_cast<int> (std::lround (valueOf (self, l)));
    const auto it = std::find_if (self->content().lfoShapes.begin(), self->content().lfoShapes.end(), [lfo] (const ContentLfoShape& s) { return s.lfo == static_cast<int> (lfo); });
    ContentLfoShape shape;
    if (builtIn == kLfoShapeCustom && it != self->content().lfoShapes.end() && it->points.size() >= 2)
        shape = *it;
    else if (builtIn >= 0 && builtIn < kNumLfoShapes)
        shape = convert::builtInLfoShape (static_cast<int> (lfo), builtIn);
    else
    {
        say (summary, capacity, "this LFO has no shape to copy");
        return SYNTHYBOY_CONTENT_ERR_STATE;
    }
    wtedit::Frame frame;
    convert::lfoShapeToFrame (shape, frame);
    wtedit::Wavetable table;
    const std::string o = "osc" + std::to_string (osc) + ".";
    bool replaced = false;
    const ContentWavetable* current = nullptr;
    for (const auto& w : self->content().wavetables)
        if (w.osc == static_cast<int> (osc))
            current = &w;
    std::vector<uint8_t> bytes;
    if (current != nullptr && static_cast<int> (std::lround (valueOf (self, o + "wavetable"))) == kWavetablePresetTable && self->store().readFile (current->ref, bytes)
        && wavetableFromWav (bytes, current->ref.name, table) && table.numFrames() > 0)
    {
        const float position = std::clamp (valueOf (self, o + "frame") * 0.01f, 0.0f, 1.0f);
        table.setFrame (static_cast<size_t> (std::lround (position * static_cast<float> (table.numFrames() - 1))), frame);
        replaced = true;
    }
    else
    {
        table = wtedit::Wavetable ("LFO " + std::to_string (lfo) + " shape");
        table.setFrame (0, frame);
    }
    if (! installTable (self, static_cast<int> (osc), table))
        return SYNTHYBOY_CONTENT_ERR_STORE;
    actionDone (self);
    say (summary, capacity, replaced ? "the current frame was replaced" : "a new one-frame table");
    return SYNTHYBOY_CONTENT_OK;
}

const synthyboy_plugin_content_t s_content = {
    ext_content_count,
    ext_content_get,
    ext_content_import_file,
    ext_content_attach,
    ext_content_remove,
    ext_content_import_vital,
    ext_content_switch_to_wavetable,
    ext_content_render_osc_warp,
    ext_content_resample,
    ext_content_render_last_note,
    ext_content_wavetable_frame_to_lfo,
    ext_content_lfo_shape_to_wavetable
};
} // namespace

void setContentHostResolver (ContentHostResolver resolver) noexcept { s_resolver = resolver; }

const synthyboy_plugin_content_t* contentExtension() noexcept { return &s_content; }
} // namespace synthyboy
