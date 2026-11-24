// SynthyBoy - C++ OSC overlay render fixture
// Renders the real OscPageUi + OscDisplay + scene_cpu.h overlay and dumps the
// matching host-native parameter/handle state for documented synthetic fixtures.
// NOTE: Synthetic test fixture for offline verification, NOT a live device capture.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "shell-mpc/native_ui_params.h"
#include "shell-mpc/overlay/osc_page.h"
#include "shell-mpc/overlay/tests/fakes.h"
#include "synthyboy/user_content.h"

namespace {

class OscFixtureInstance final : public synthyboy_mpc::overlay::IInstanceView
{
public:
    OscFixtureInstance()
    {
        const synthyboy::ParamInfo* t = synthyboy::paramTable();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            v_[i] = t[i].def;
    }

    float param (int i) const noexcept override
    {
        return (i >= 0 && static_cast<size_t> (i) < synthyboy::kMaxParams) ? v_[i] : 0.0f;
    }
    bool live (synthyboy::LiveValues& out) const noexcept override
    {
        out = liveValues;
        return haveLive;
    }
    double sampleRate() const noexcept override { return 48000.0; }
    int playSelection (int) const noexcept override { return sel; }
    void setPlaySelection (int g, int i) noexcept override { if (g == 0) sel = i; }
    int playLastSelector() const noexcept override { return 2; }
    void setQGroup (const int* t, int n) noexcept override { for (int i = 0; i < 4; ++i) q[i] = i < n ? t[i] : -1; }
    void writeParam (int i, float norm) noexcept override
    {
        if (i >= 0 && static_cast<size_t> (i) < synthyboy::kMaxParams)
        {
            const synthyboy::ParamInfo& p = synthyboy::paramTable()[i];
            v_[i] = synthyboy::constrainValue (p, synthyboy::fromNormalised (p, norm));
        }
    }
    void setUiFullOpen (bool o) noexcept override { fullOpen = o; }
    uint64_t stateGeneration() const noexcept override { return generation; }

    void setNativeOscState (const synthyboy_mpc::nativeui::osc::ViewState& s) noexcept override
    {
        nativeState = s;
        native.publish (s);
        native.report (native.snapshot());
    }
    bool takeNativeOsc (const synthyboy_mpc::nativeui::osc::ViewState& s, int field, int& choice) noexcept override
    {
        return native.take (s, field, choice);
    }
    bool nativeOscSettingsSupported() const noexcept override { return true; }
    void setNativeOscSettings (int view, const synthyboy_mpc::nativeui::FxCell* fields) noexcept override
    {
        settingsView = view;
        settings.publish (view, fields);
        for (int i = 0; i < synthyboy_mpc::nativeui::osc::kSettingsAll; ++i)
        {
            if (fields != nullptr)
                publishedCells[static_cast<size_t> (i)] = fields[i];
            else
                publishedCells[static_cast<size_t> (i)] = {};
            settings.report (settings.at (i));
        }
    }
    bool takeNativeOscSetting (int field, const synthyboy_mpc::nativeui::FxCell& expected, float& value) noexcept override
    {
        return settings.take (field, expected, value);
    }
    void setNativeOscAbcState (int mask, const char* label) noexcept override
    {
        warpMask = mask;
        sourceLabel = label ? label : "";
    }
    bool nativeOscDetailSupported() const noexcept override { return true; }
    void setNativeOscDetail (const synthyboy_mpc::nativeui::oscdetail::State& s) noexcept override
    {
        detail.publish (s);
        for (int i = 0; i < synthyboy_mpc::nativeui::oscdetail::kBindings; ++i)
            detail.report (detail.at (i));
    }
    bool takeNativeOscDetail (const synthyboy_mpc::nativeui::oscdetail::State& s, int field, float& value) noexcept override
    {
        return detail.take (s, field, value);
    }

    std::shared_ptr<const synthyboy::StateContent> content() const override { return contentPtr; }
    const synthyboy::IContentStore* contentStore() const noexcept override { return storePtr.get(); }
    void wavetableSources (synthyboy::CatalogueSources& out) const override { out = wtSources; }
    void contentRoots (std::vector<ContentRoot>& out) const override { out = roots; }

    void addChirpSample (int osc, const std::string& filename)
    {
        const int rate = 44100;
        const size_t n = static_cast<size_t> (rate * 2);
        std::vector<float> x (n);
        for (size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / rate;
            const double env = std::exp (-2.2 * std::fmod (t, 1.0)) * (t < 1.0 ? 1.0 : 0.65);
            x[i] = static_cast<float> (0.85 * env * std::sin (6.283185307179586 * (110.0 + 220.0 * std::fmod (t, 1.0)) * t));
        }
        if (! storePtr)
            storePtr = std::make_shared<synthyboy::MemoryContentStore>();
        storePtr->add ("Factory", filename, synthyboy::encodeWav (x.data(), nullptr, n, rate, 16));
        auto c = contentPtr ? std::make_shared<synthyboy::StateContent> (*contentPtr)
                            : std::make_shared<synthyboy::StateContent>();
        synthyboy::ContentSample cs;
        cs.osc = osc;
        cs.ref.pack = "Factory";
        cs.ref.file = filename;
        cs.ref.name = filename;
        cs.sampleRate = rate;
        cs.frames = static_cast<int> (n);
        cs.channels = 1;
        c->samples.push_back (cs);
        contentPtr = c;
    }

    static int idx (const std::string& key)
    {
        const auto* p = synthyboy::findParamByKey (key.c_str());
        return p ? static_cast<int> (p - synthyboy::paramTable()) : -1;
    }
    void set (const std::string& key, float v)
    {
        const int i = idx (key);
        if (i >= 0)
            v_[i] = v;
    }
    float get (const std::string& key) const
    {
        const int i = idx (key);
        return i >= 0 ? v_[i] : 0.0f;
    }

    int sel = 2;
    int q[4] = { -1, -1, -1, -1 };
    bool fullOpen = false;
    uint64_t generation = 1;
    bool haveLive = false;
    synthyboy::LiveValues liveValues {};
    synthyboy_mpc::nativeui::osc::Bank native;
    synthyboy_mpc::nativeui::osc::ViewState nativeState;
    synthyboy_mpc::nativeui::NativeCellBank<synthyboy_mpc::nativeui::osc::kSettingsAll> settings;
    std::array<synthyboy_mpc::nativeui::FxCell, synthyboy_mpc::nativeui::osc::kSettingsAll> publishedCells {};
    int settingsView = 0;
    int warpMask = 0;
    std::string sourceLabel;
    synthyboy_mpc::nativeui::oscdetail::Bank detail;
    std::shared_ptr<const synthyboy::StateContent> contentPtr;
    std::shared_ptr<synthyboy::MemoryContentStore> storePtr;
    synthyboy::CatalogueSources wtSources;
    std::vector<ContentRoot> roots;

private:
    float v_[synthyboy::kMaxParams] = {};
};

bool writePngRgba (const std::string& path, int W, int H, const uint32_t* px)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f)
        return false;
    std::vector<unsigned char> raw;
    raw.reserve (static_cast<size_t> (H) * (static_cast<size_t> (W) * 4 + 1));
    for (int y = 0; y < H; ++y)
    {
        raw.push_back (0);
        for (int x = 0; x < W; ++x)
        {
            const uint32_t p = px[y * W + x];
            raw.push_back (static_cast<unsigned char> ((p >> 16) & 255u));
            raw.push_back (static_cast<unsigned char> ((p >> 8) & 255u));
            raw.push_back (static_cast<unsigned char> (p & 255u));
            raw.push_back (static_cast<unsigned char> ((p >> 24) & 255u));
        }
    }
    std::vector<unsigned char> z { 0x78, 0x01 };
    uint32_t a = 1, b = 0;
    for (unsigned char c : raw)
    {
        a = (a + c) % 65521u;
        b = (b + a) % 65521u;
    }
    for (size_t pos = 0; pos < raw.size();)
    {
        const size_t n = std::min<size_t> (65535, raw.size() - pos);
        const bool last = pos + n == raw.size();
        z.push_back (last ? 1 : 0);
        z.push_back (static_cast<unsigned char> (n & 255));
        z.push_back (static_cast<unsigned char> ((n >> 8) & 255));
        const size_t inv = (~n) & 65535u;
        z.push_back (static_cast<unsigned char> (inv & 255));
        z.push_back (static_cast<unsigned char> ((inv >> 8) & 255));
        z.insert (z.end(), raw.begin() + static_cast<long> (pos), raw.begin() + static_cast<long> (pos + n));
        pos += n;
    }
    const uint32_t adler = (b << 16) | a;
    z.push_back (static_cast<unsigned char> ((adler >> 24) & 255));
    z.push_back (static_cast<unsigned char> ((adler >> 16) & 255));
    z.push_back (static_cast<unsigned char> ((adler >> 8) & 255));
    z.push_back (static_cast<unsigned char> (adler & 255));

    static const uint32_t crcTable[256] = {
        0x00000000u,0x77073096u,0xee0e612cu,0x990951bau,0x076dc419u,0x706af48fu,0xe963a535u,0x9e6495a3u,
        0x0edb8832u,0x79dcb8a4u,0xe0d5e91eu,0x97d2d988u,0x09b64c2bu,0x7eb17cbdu,0xe7b82d07u,0x90bf1d91u,
        0x1db71064u,0x6ab020f2u,0xf3b97148u,0x84be41deu,0x1adad47du,0x6ddde4ebu,0xf4d4b551u,0x83d385c7u,
        0x136c9856u,0x646ba8c0u,0xfd62f97au,0x8a65c9ecu,0x14015c4fu,0x63066cd9u,0xfa0f3d63u,0x8d080df5u,
        0x3b6e20c8u,0x4c69105eu,0xd56041e4u,0xa2677172u,0x3c03e4d1u,0x4b04d447u,0xd20d85fdu,0xa50ab56bu,
        0x35b5a8fau,0x42b2986cu,0xdbbbc9d6u,0xacbcf940u,0x32d86ce3u,0x45df5c75u,0xdcd60dcfu,0xabd13d59u,
        0x26d930acu,0x51de003au,0xc8d75180u,0xbfd06116u,0x21b4f4b5u,0x56b3c423u,0xcfba9599u,0xb8bda50fu,
        0x2802b89eu,0x5f058808u,0xc60cd9b2u,0xb10be924u,0x2f6f7c87u,0x58684c11u,0xc1611dabu,0xb6662d3du,
        0x76dc4190u,0x01db7106u,0x98d220bcu,0xefd5102au,0x71b18589u,0x06b6b51fu,0x9fbfe4a5u,0xe8b8d433u,
        0x7807c9a2u,0x0f00f934u,0x9609a88eu,0xe10e9818u,0x7f6a0dbbu,0x086d3d2du,0x91646c97u,0xe6635c01u,
        0x6b6b51f4u,0x1c6c6162u,0x856530d8u,0xf262004eu,0x6c0695edu,0x1b01a57bu,0x8208f4c1u,0xf50fc457u,
        0x65b0d9c6u,0x12b7e950u,0x8bbeb8eau,0xfcb9887cu,0x62dd1ddfu,0x15da2d49u,0x8cd37cf3u,0xfbd44c65u,
        0x4db26158u,0x3ab551ceu,0xa3bc0074u,0xd4bb30e2u,0x4adfa541u,0x3dd895d7u,0xa4d1c46du,0xd3d6f4fbu,
        0x4369e96au,0x346ed9fcu,0xad678846u,0xda60b8d0u,0x44042d73u,0x33031de5u,0xaa0a4c5fu,0xdd0d7cc9u,
        0x5005713cu,0x270241aau,0xbe0b1010u,0xc90c2086u,0x5768b525u,0x206f85b3u,0xb966d409u,0xce61e49fu,
        0x5edef90eu,0x29d9c998u,0xb0d09822u,0xc7d7a8b4u,0x59b33d17u,0x2eb40d81u,0xb7bd5c3bu,0xc0ba6cadu,
        0xedb88320u,0x9abfb3b6u,0x03b6e20cu,0x74b1d29au,0xead54739u,0x9dd277afu,0x04db2615u,0x73dc1683u,
        0xe3630b12u,0x94643b84u,0x0d6d6a3eu,0x7a6a5aa8u,0xe40ecf0bu,0x9309ff9du,0x0a00ae27u,0x7d079eb1u,
        0xf00f9344u,0x8708a3d2u,0x1e01f268u,0x6906c2feu,0xf762575du,0x806567cbu,0x196c3671u,0x6e6b06e7u,
        0xfed41b76u,0x89d32be0u,0x10da7a5au,0x67dd4accu,0xf9b9df6fu,0x8ebeeff9u,0x17b7be43u,0x60b08ed5u,
        0xd6d6a3e8u,0xa1d1937eu,0x38d8c2c4u,0x4fdff252u,0xd1bb67f1u,0xa6bc5767u,0x3fb506ddu,0x48b2364bu,
        0xd80d2bdau,0xaf0a1b4cu,0x36034af6u,0x41047a60u,0xdf60efc3u,0xa867df55u,0x316e8eefu,0x4669be79u,
        0xcb61b38cu,0xbc66831au,0x256fd2a0u,0x5268e236u,0xcc0c7795u,0xbb0b4703u,0x220216b9u,0x5505262fu,
        0xc5ba3bbeu,0xb2bd0b28u,0x2bb45a92u,0x5cb36a04u,0xc2d7ffa7u,0xb5d0cf31u,0x2cd99e8bu,0x5bdeae1du,
        0x9b64c2b0u,0xec63f226u,0x756aa39cu,0x026d930au,0x9c0906a9u,0xeb0e363fu,0x72076785u,0x05005713u,
        0x95bf4a82u,0xe2b87a14u,0x7bb12baeu,0x0cb61b38u,0x92d28e9bu,0xe5d5be0du,0x7cdcefb7u,0x0bdbdf21u,
        0x86d3d2d4u,0xf1d4e242u,0x68ddb3f8u,0x1fda836eu,0x81be16cdu,0xf6b9265bu,0x6fb077e1u,0x18b74777u,
        0x88085ae6u,0xff0f6a70u,0x66063bcau,0x11010b5cu,0x8f659effu,0xf862ae69u,0x616bffd3u,0x166ccf45u,
        0xa00ae278u,0xd70dd2eeu,0x4e048354u,0x3903b3c2u,0xa7672661u,0xd06016f7u,0x4969474du,0x3e6e77dbu,
        0xaed16a4au,0xd9d65adcu,0x40df0b66u,0x37d83bf0u,0xa9bcae53u,0xdebb9ec5u,0x47b2cf7fu,0x30b5ffe9u,
        0xbdbdf21cu,0xcabac28au,0x53b39330u,0x24b4a3a6u,0xbad03605u,0xcdd70693u,0x54de5729u,0x23d967bfu,
        0xb3667a2eu,0xc4614ab8u,0x5d681b02u,0x2a6f2b94u,0xb40bbe37u,0xc30c8ea1u,0x5a05df1bu,0x2d02ef8du
    };
    auto putBe = [&] (uint32_t v)
    {
        unsigned char b4[4] {
            static_cast<unsigned char> ((v >> 24) & 255),
            static_cast<unsigned char> ((v >> 16) & 255),
            static_cast<unsigned char> ((v >> 8) & 255),
            static_cast<unsigned char> (v & 255)
        };
        std::fwrite (b4, 1, 4, f);
    };
    auto chunk = [&] (const char* tag, const unsigned char* data, size_t len)
    {
        putBe (static_cast<uint32_t> (len));
        std::fwrite (tag, 1, 4, f);
        if (len > 0)
            std::fwrite (data, 1, len, f);
        uint32_t c = 0xffffffffu;
        for (int i = 0; i < 4; ++i)
            c = crcTable[(c ^ static_cast<unsigned char> (tag[i])) & 255u] ^ (c >> 8);
        for (size_t i = 0; i < len; ++i)
            c = crcTable[(c ^ data[i]) & 255u] ^ (c >> 8);
        putBe (c ^ 0xffffffffu);
    };
    const unsigned char sig[8] { 137, 80, 78, 71, 13, 10, 26, 10 };
    std::fwrite (sig, 1, 8, f);
    unsigned char ihdr[13] {
        static_cast<unsigned char> ((W >> 24) & 255), static_cast<unsigned char> ((W >> 16) & 255),
        static_cast<unsigned char> ((W >> 8) & 255), static_cast<unsigned char> (W & 255),
        static_cast<unsigned char> ((H >> 24) & 255), static_cast<unsigned char> ((H >> 16) & 255),
        static_cast<unsigned char> ((H >> 8) & 255), static_cast<unsigned char> (H & 255),
        8, 6, 0, 0, 0
    };
    chunk ("IHDR", ihdr, sizeof ihdr);
    chunk ("IDAT", z.data(), z.size());
    chunk ("IEND", nullptr, 0);
    std::fclose (f);
    return true;
}

std::string jsonEscape (const std::string& s)
{
    std::string out;
    out.reserve (s.size() + 8);
    for (unsigned char c : s)
    {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c < 32) { char b[8]; std::snprintf (b, sizeof b, "\\u%04x", c); out += b; }
        else out.push_back (static_cast<char> (c));
    }
    return out;
}

std::string formatParamText (int paramIdx, float plain)
{
    if (paramIdx < 0 || static_cast<size_t> (paramIdx) >= synthyboy::paramCount())
        return "";
    const auto& pi = synthyboy::paramTable()[paramIdx];
    if (pi.numChoices > 0 && pi.choices != nullptr)
    {
        const int idx = std::clamp (static_cast<int> (std::lround (plain - pi.min)), 0, pi.numChoices - 1);
        return synthyboy_mpc::ui::shownChoice (pi.key, idx, pi.choices[idx]);
    }
    char buf[64] {};
    synthyboy_mpc::overlay::uibind::formatValue (pi, plain, buf, sizeof buf);
    return buf;
}

int configureState (const std::string& state, OscFixtureInstance& inst, std::string& tabName)
{
    // Base power OFF for non-selected except explicit configuration
    inst.set ("sub.on", 0.0f);
    inst.set ("osc1.on", 0.0f);
    inst.set ("osc2.on", 0.0f);
    inst.set ("osc3.on", 0.0f);
    inst.set ("noise.on", 0.0f);

    if (state == "osc_a_on" || state == "osc_a_off")
    {
        tabName = "Osc A";
        inst.set ("osc1.on", state == "osc_a_on" ? 1.0f : 0.0f);
        inst.set ("osc1.type", 0.0f);            // Wavetable
        inst.set ("osc1.wavetable", 4.0f);       // Built-in wavetable choice 4
        inst.set ("osc1.frame", 38.0f);
        inst.set ("osc1.transpose", 3.0f);       // 0 OCT, +3 SEM
        inst.set ("osc1.tune", -12.0f);
        inst.set ("osc1.pitch_coarse", 0.0f);
        inst.set ("osc1.pitch_track", 1.0f);
        inst.set ("osc1.unison.voices", 3.0f);
        inst.set ("osc1.unison.detune", 24.0f);
        inst.set ("osc1.unison.blend", 62.0f);
        inst.set ("osc1.warp.type", 1.0f);       // Sync
        inst.set ("osc1.warp.amount", 35.0f);
        inst.set ("osc1.warp2.type", 3.0f);      // Bend
        inst.set ("osc1.warp2.amount", -28.0f);
        inst.set ("osc1.pan", -18.0f);
        inst.set ("osc1.level", 78.0f);
        inst.set ("osc1.destination", 2.0f);     // F1+F2
        return 2;
    }
    if (state == "osc_b_on")
    {
        tabName = "Osc B";
        inst.set ("osc2.on", 1.0f);
        inst.set ("osc2.type", 0.0f);
        inst.set ("osc2.wavetable", 7.0f);
        inst.set ("osc2.frame", 64.0f);
        inst.set ("osc2.transpose", -10.0f);     // -1 OCT, +2 SEM
        inst.set ("osc2.tune", 14.0f);
        inst.set ("osc2.pitch_coarse", 2.0f);
        inst.set ("osc2.pitch_track", 1.0f);
        inst.set ("osc2.unison.voices", 5.0f);
        inst.set ("osc2.unison.detune", 42.0f);
        inst.set ("osc2.unison.blend", 45.0f);
        inst.set ("osc2.warp.type", 2.0f);
        inst.set ("osc2.warp.amount", 52.0f);
        inst.set ("osc2.warp2.type", 0.0f);      // Off
        inst.set ("osc2.pan", 25.0f);
        inst.set ("osc2.level", 66.0f);
        inst.set ("osc2.destination", 0.0f);     // Filter 1
        return 3;
    }
    if (state == "osc_c_on")
    {
        tabName = "Osc C";
        inst.set ("osc3.on", 1.0f);
        inst.set ("osc3.type", 0.0f);
        inst.set ("osc3.wavetable", 11.0f);
        inst.set ("osc3.frame", 82.0f);
        inst.set ("osc3.transpose", 19.0f);      // +1 OCT, +7 SEM
        inst.set ("osc3.tune", -6.0f);
        inst.set ("osc3.pitch_coarse", -3.0f);
        inst.set ("osc3.pitch_track", 1.0f);
        inst.set ("osc3.unison.voices", 2.0f);
        inst.set ("osc3.unison.detune", 15.0f);
        inst.set ("osc3.unison.blend", 80.0f);
        inst.set ("osc3.warp.type", 4.0f);
        inst.set ("osc3.warp.amount", -40.0f);
        inst.set ("osc3.warp2.type", 5.0f);
        inst.set ("osc3.warp2.amount", 30.0f);
        inst.set ("osc3.pan", 0.0f);
        inst.set ("osc3.level", 54.0f);
        inst.set ("osc3.destination", 1.0f);     // Filter 2
        return 4;
    }
    if (state == "sub_on")
    {
        tabName = "Sub";
        inst.set ("sub.on", 1.0f);
        inst.set ("sub.waveform", 3.0f);         // Saw
        inst.set ("sub.octave", -1.0f);
        inst.set ("sub.tune", 12.0f);
        inst.set ("sub.phase", 45.0f);
        inst.set ("sub.env1", 1.0f);
        inst.set ("sub.mono", 1.0f);
        inst.set ("sub.phase_contiguous", 0.0f);
        inst.set ("sub.filter_blend", 46.0f);
        inst.set ("sub.pan", -10.0f);
        inst.set ("sub.level", 72.0f);
        inst.set ("sub.destination", 0.0f);
        return 1;
    }
    if (state == "noise_gen_on")
    {
        tabName = "Noise";
        inst.set ("noise.on", 1.0f);
        inst.set ("noise.type", 0.0f);           // Noise (Generated)
        inst.set ("noise.sample", 1.0f);         // Pink Noise
        inst.set ("noise.filter", -24.0f);
        inst.set ("noise.stereo", 68.0f);
        inst.set ("noise.filter_blend", 62.0f);
        inst.set ("noise.pitch", 4.0f);
        inst.set ("noise.fine", 18.0f);
        inst.set ("noise.keytrack", 1.0f);
        inst.set ("noise.env1", 0.0f);
        inst.set ("noise.pan", 16.0f);
        inst.set ("noise.level", 60.0f);
        inst.set ("noise.destination", 3.0f);    // Out
        return 5;
    }
    if (state == "noise_sample_on")
    {
        tabName = "Noise";
        inst.addChirpSample (4, "Noise_TapeLoop_Fixture.wav");
        inst.addChirpSample (0, "Noise_TapeLoop_Fixture.wav");
        inst.set ("noise.on", 1.0f);
        inst.set ("noise.type", 1.0f);           // Sample
        inst.set ("noise.filter", 32.0f);
        inst.set ("noise.stereo", 40.0f);
        inst.set ("noise.filter_blend", 28.0f);
        inst.set ("noise.pitch", -5.0f);
        inst.set ("noise.fine", -22.0f);
        inst.set ("noise.keytrack", 1.0f);
        inst.set ("noise.env1", 1.0f);
        inst.set ("noise.phase", 25.0f);
        inst.set ("noise.random_phase", 0.0f);
        inst.set ("noise.loop", 1.0f);
        inst.set ("noise.bounce", 1.0f);
        inst.set ("noise.pan", -30.0f);
        inst.set ("noise.level", 84.0f);
        inst.set ("noise.destination", 2.0f);
        return 5;
    }
    return -1;
}

} // namespace

int main (int argc, char** argv)
{
    std::string stateName = "osc_a_on";
    std::string outOverlay = "osc_overlay.png";
    std::string outState = "osc_state.json";
    bool tabOnly = false;

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--state") == 0 && i + 1 < argc)
            stateName = argv[++i];
        else if (std::strcmp (argv[i], "--out-overlay") == 0 && i + 1 < argc)
            outOverlay = argv[++i];
        else if (std::strcmp (argv[i], "--out-state") == 0 && i + 1 < argc)
            outState = argv[++i];
        else if (std::strcmp (argv[i], "--tab-only") == 0)
            tabOnly = true;
    }

    OscFixtureInstance inst;
    std::string tabName;
    const int sel = configureState (stateName, inst, tabName);
    if (sel < 0)
    {
        std::fprintf (stderr, "Unknown --state '%s'\n", stateName.c_str());
        return 1;
    }
    inst.sel = sel;

    synthyboy_mpc::overlay::OscPageUi page;
    synthyboy_mpc::overlay::UiStack stack;
    stack.openFull (synthyboy_mpc::overlay::OscPageUi::kElementPage);
    page.setHostBackground (true);
    page.setNativeInstanceSelector (true);
    page.setNativeRouting (true);

    synthyboy_mpc::overlay::page::Input in;
    in.instance = sel;
    page.update (inst, stack, 1000.0, in);
    page.update (inst, stack, 1100.0, in);

    constexpr int fullW = 1280, fullH = 800;
    auto scene = std::make_unique<synthyboy_mpc::overlay::gpu::SceneData>();
    synthyboy_mpc::overlay::gpu::SceneBuilder sb (*scene);
    sb.begin (synthyboy_mpc::overlay::gpu::kKindPlay, 0, fullW, fullH);
    page.draw (sb);
    std::vector<uint32_t> pixels;
    synthyboy_mpc::overlay::renderScene (*scene, pixels);
    if (sb.overflow)
    {
        std::fprintf (stderr, "osc_render_fixture: scene builder overflow for %s\n", stateName.c_str());
        return 1;
    }

    int outW = fullW, outH = fullH;
    std::vector<uint32_t> cropped;
    const uint32_t* writePtr = pixels.data();
    if (tabOnly)
    {
        outW = 1280;
        outH = 628;
        cropped.resize (static_cast<size_t> (outW) * static_cast<size_t> (outH));
        for (int y = 0; y < outH; ++y)
            std::copy_n (&pixels[static_cast<size_t> ((y + 110) * fullW)], outW, &cropped[static_cast<size_t> (y * outW)]);
        writePtr = cropped.data();
    }

    if (! writePngRgba (outOverlay, outW, outH, writePtr))
    {
        std::fprintf (stderr, "osc_render_fixture: failed writing %s\n", outOverlay.c_str());
        return 1;
    }

    // Emit matching state & virtual host parameter map JSON for the Python skin renderer.
    const int base = static_cast<int> (synthyboy::paramCount()) + 82;
    const int powered = synthyboy_mpc::overlay::OscPageUi::isInstanceOn (inst, sel) ? 1 : 0;
    const int srcIdx = sel - 1;
    namespace nu = synthyboy_mpc::nativeui;

    std::ostringstream js;
    js << std::fixed << std::setprecision (6);
    js << "{\n";
    js << "  \"synthetic_fixture\": true,\n";
    js << "  \"device_test\": false,\n";
    js << "  \"state_name\": \"" << jsonEscape (stateName) << "\",\n";
    js << "  \"tab_name\": \"" << jsonEscape (tabName) << "\",\n";
    js << "  \"selection\": " << sel << ",\n";
    js << "  \"powered\": " << (powered ? "true" : "false") << ",\n";
    js << "  \"param_count\": " << synthyboy::paramCount() << ",\n";
    js << "  \"base\": " << base << ",\n";
    js << "  \"settings_view\": " << inst.settingsView << ",\n";
    js << "  \"warp_mask\": " << inst.warpMask << ",\n";
    js << "  \"source_label\": \"" << jsonEscape (inst.sourceLabel) << "\",\n";
    js << "  \"choice_label\": \"" << jsonEscape (synthyboy_mpc::overlay::OscPageUi::choiceLabel (inst, sel)) << "\",\n";
    if (sel >= 2 && sel <= 4)
    {
        const std::string pfx = "osc" + std::to_string (sel - 1) + ".";
        const int tr = static_cast<int> (std::lround (inst.get (pfx + "transpose")));
        const int sem = synthyboy_mpc::overlay::OscUi::semOf (tr);
        const std::string semDisp = (sem > 0 ? "+" : "") + std::to_string (sem);
        js << "  \"tune_sem_display\": \"" << jsonEscape (semDisp) << "\",\n";
    }

    // Engine params + display strings
    js << "  \"params\": {\n";
    const auto* tbl = synthyboy::paramTable();
    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
    {
        const float plain = inst.param (static_cast<int> (i));
        const float norm = synthyboy::toNormalised (tbl[i], plain);
        const std::string disp = formatParamText (static_cast<int> (i), plain);
        js << "    \"" << i << "\": {\"key\": \"" << jsonEscape (tbl[i].key)
           << "\", \"plain\": " << plain << ", \"norm\": " << norm
           << ", \"display\": \"" << jsonEscape (disp) << "\"}"
           << (i + 1 < synthyboy::paramCount() ? "," : "") << "\n";
    }
    js << "  },\n";

    // Virtual host handles emitted by OscPageUi / host bridge
    js << "  \"virtual\": {\n";
    auto addVirt = [&] (int handle, float norm, const std::string& display, bool trailingComma = true)
    {
        js << "    \"Parameter " << handle << "\": {\"norm\": " << norm
           << ", \"display\": \"" << jsonEscape (display) << "\"}"
           << (trailingComma ? "," : "") << "\n";
    };
    const char* const routeNames[] = { "Filter 1", "Filter 2", "F1+F2", "Out" };
    const int curRouteIdx = synthyboy_mpc::overlay::OscPageUi::destinationParamIndex (sel);
    const int curRouteVal = curRouteIdx >= 0 ? std::clamp (static_cast<int> (std::lround (inst.param (curRouteIdx))), 0, 3) : 0;
    addVirt (base + nu::kOscBase + 0, static_cast<float> (srcIdx) / 4.0f, synthyboy_mpc::overlay::OscPageUi::choiceLabel (inst, sel));
    addVirt (base + nu::kOscBase + 1, static_cast<float> (curRouteVal) / 3.0f, routeNames[curRouteVal]);
    addVirt (base + nu::kOscBase + 2, powered ? 1.0f : 0.5f, powered ? "2" : "1");
    for (int s = 0; s < 5; ++s)
    {
        const bool sOn = synthyboy_mpc::overlay::OscPageUi::isInstanceOn (inst, s + 1);
        addVirt (base + nu::kOscPower + s, sOn ? 1.0f : 0.0f, sOn ? "On" : "Off");
        const int rIdx = synthyboy_mpc::overlay::OscPageUi::destinationParamIndex (s + 1);
        const int rVal = rIdx >= 0 ? std::clamp (static_cast<int> (std::lround (inst.param (rIdx))), 0, 3) : 0;
        addVirt (base + nu::kOscRoutingBySource + s, static_cast<float> (rVal) / 3.0f, routeNames[rVal]);
    }
    addVirt (base + nu::kOscSettingsView,
             static_cast<float> (inst.settingsView) / static_cast<float> (nu::osc::kLastSettings),
             std::to_string (inst.settingsView));
    addVirt (base + nu::kOscWarpLayout,
             static_cast<float> (inst.warpMask) / static_cast<float> (nu::osc::kLastWarpMask),
             std::to_string (inst.warpMask));
    namespace nd = synthyboy_mpc::nativeui::oscdetail;
    addVirt (base + nu::kOscDetail, static_cast<float> (inst.detail.layout()) / nd::LastFrame,
             std::to_string (inst.detail.layout()));
    for (int field = 0; field < nd::kCells; ++field)
    {
        const auto cell = inst.detail.at (field).cell();
        const float value = cell.target >= 0 && cell.kind != nd::Action
            ? synthyboy::toNormalised (tbl[cell.target], inst.param (cell.target)) : 0.0f;
        addVirt (base + nu::kOscDetail + nd::kValue + field, value,
                 cell.target >= 0 && cell.kind != nd::Action ? formatParamText (cell.target, inst.param (cell.target)) : "");
    }
    for (int nav = 0; nav < 3; ++nav)
    {
        const char* name = inst.detail.name (nd::kMore + nav);
        addVirt (base + nu::kOscDetail + nd::kNavigation + nav, 0.0f, name ? name : "");
    }
    addVirt (base + nu::kOscRootLoadView, 0.0f, "0");

    // Settings slots (non-ABC + ABC slots)
    const auto items = page.ui().nativeSettingsItems();
    for (int slot = 0; slot < nu::osc::kSettings; ++slot)
    {
        const auto cell = inst.publishedCells[static_cast<size_t> (slot)];
        float nVal = 0.0f;
        std::string disp;
        if (cell.target >= 0 && items[slot] != nullptr)
        {
            const float plain = inst.param (cell.target);
            nVal = synthyboy_mpc::overlay::OscUi::nativeSettingsValue (cell, plain);
            disp = formatParamText (cell.target, plain);
        }
        addVirt (base + nu::kOscSettings + slot, nVal, disp);
    }
    for (int abc = 0; abc < 3; ++abc)
    {
        for (int s = 0; s < nu::osc::kAbcSettings; ++s)
        {
            const int handleOffset = abc * nu::osc::kAbcSettings + s;
            const int bankSlot = nu::osc::abcSettingIndex (abc + 2, s);
            const auto cell = inst.publishedCells[static_cast<size_t> (bankSlot)];
            float nVal = 0.0f;
            std::string disp;
            if (sel == abc + 2 && cell.target >= 0 && items[bankSlot] != nullptr)
            {
                const float plain = inst.param (cell.target);
                nVal = synthyboy_mpc::overlay::OscUi::nativeSettingsValue (cell, plain);
                if (s == nu::osc::AbcOctave)
                {
                    const int oct = synthyboy_mpc::overlay::OscUi::octOf (static_cast<int> (std::lround (plain)));
                    disp = (oct > 0 ? "+" : "") + std::to_string (oct);
                }
                else if (s == nu::osc::AbcSource)
                    disp = inst.sourceLabel;
                else if (s == nu::osc::AbcBrowse)
                    disp = "BROWSE";
                else if (s == nu::osc::AbcUnisonDetail)
                    disp = "DETAIL";
                else if (cell.kind == nu::osc::SettingsAction)
                    disp = "WARP";
                else
                    disp = formatParamText (cell.target, plain);
            }
            const bool last = (abc == 2 && s + 1 == nu::osc::kAbcSettings);
            addVirt (base + nu::kOscAbcSettings + handleOffset, nVal, disp, ! last);
        }
    }
    js << "  }\n";
    js << "}\n";

    std::ofstream ofs (outState);
    if (! ofs)
    {
        std::fprintf (stderr, "osc_render_fixture: failed writing %s\n", outState.c_str());
        return 1;
    }
    ofs << js.str();
    std::printf ("osc_render_fixture: rendered %s (%dx%d) -> %s\n",
                 stateName.c_str(), outW, outH, outOverlay.c_str());
    return 0;
}
