// SynthyBoy - core: the user content library (user_content.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/user_content.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>

#include "sha256.h"
#include "synthyboy/sampleosc/sample_data.h"
#include "synthyboy/sampleosc/wav_reader.h"

namespace synthyboy
{
namespace fs = std::filesystem;

namespace
{
bool safeRelative (const std::string& file)
{
    if (file.empty() || file.front() == '/' || file.front() == '\\' || file.find (':') != std::string::npos)
        return false;
    size_t pos = 0;
    while (pos <= file.size())
    {
        const size_t next = file.find_first_of ("/\\", pos);
        const std::string part = file.substr (pos, next == std::string::npos ? std::string::npos : next - pos);
        if (part == "..")
            return false;
        if (next == std::string::npos)
            break;
        pos = next + 1;
    }
    return true;
}

bool writeBytes (const fs::path& path, const std::vector<uint8_t>& bytes)
{
    std::error_code ec;
    fs::create_directories (path.parent_path(), ec);
    const fs::path tmp = path.string() + ".tmp";
    {
        std::ofstream out (tmp, std::ios::binary | std::ios::trunc);
        if (! out)
            return false;
        out.write (reinterpret_cast<const char*> (bytes.data()), static_cast<std::streamsize> (bytes.size()));
        out.close();
        if (! out.good())
        {
            fs::remove (tmp, ec);
            return false;
        }
    }
    fs::rename (tmp, path, ec);
    if (ec)
    {
        fs::remove (tmp, ec);
        return false;
    }
    return true;
}

void put16 (std::vector<uint8_t>& b, uint32_t v) { b.push_back (static_cast<uint8_t> (v & 0xFF)); b.push_back (static_cast<uint8_t> ((v >> 8) & 0xFF)); }
void put32 (std::vector<uint8_t>& b, uint32_t v) { put16 (b, v & 0xFFFF); put16 (b, v >> 16); }
void putTag (std::vector<uint8_t>& b, const char* t) { for (int i = 0; i < 4; ++i) b.push_back (static_cast<uint8_t> (t[i])); }

uint32_t readU32 (const uint8_t* p) { return static_cast<uint32_t> (p[0]) | (static_cast<uint32_t> (p[1]) << 8) | (static_cast<uint32_t> (p[2]) << 16) | (static_cast<uint32_t> (p[3]) << 24); }

/** The root note of the 'inst' / 'smpl' chunk of a WAV file, -1 when the file has none. */
int wavChunkRoot (const std::vector<uint8_t>& b)
{
    if (b.size() < 12 || std::memcmp (b.data(), "RIFF", 4) != 0 || std::memcmp (b.data() + 8, "WAVE", 4) != 0)
        return -1;
    int found = -1;
    size_t offset = 12;
    while (offset + 8 <= b.size())
    {
        const uint32_t size = readU32 (b.data() + offset + 4);
        const size_t data = offset + 8;
        if (data > b.size() || size > b.size() - data)
            break;
        if (std::memcmp (b.data() + offset, "inst", 4) == 0 && size >= 7 && b[data] <= 127)
            found = b[data];
        else if (std::memcmp (b.data() + offset, "smpl", 4) == 0 && size >= 36 && readU32 (b.data() + data + 12) <= 127)
            found = static_cast<int> (readU32 (b.data() + data + 12));
        offset = data + size + (size & 1u);
    }
    return found;
}

std::string sha12 (const std::string& sha) { return sha.substr (0, 12); }

void listDirectory (const fs::path& base, const std::string& pack, const std::string& relBase, ContentKind kind, bool embedded, std::vector<UserContentItem>& out)
{
    std::error_code ec;
    if (! fs::is_directory (base, ec))
        return;
    for (fs::recursive_directory_iterator it (base, fs::directory_options::skip_permission_denied, ec), end; ! ec && it != end; it.increment (ec))
    {
        std::error_code e2;
        if (! it->is_regular_file (e2))
            continue;
        const fs::path& p = it->path();
        std::string ext = p.extension().string();
        std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
        if (ext != ".wav")
            continue;
        UserContentItem item;
        item.kind = kind;
        item.embedded = embedded;
        item.ref.pack = pack;
        item.ref.file = (relBase.empty() ? std::string() : relBase + "/") + fs::relative (p, base, e2).generic_string();
        item.ref.name = p.stem().string();
        item.size = static_cast<uint64_t>(it->file_size (e2));
        if (item.size == 0 || item.size > kMaxContentFileBytes)
            continue;
        std::ifstream in (p, std::ios::binary);
        std::vector<uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        item.ref.sha256 = Sha256::hashBufferHex (bytes.data(), bytes.size());
        out.push_back (std::move (item));
    }
}
} // namespace

std::string sanitizeFileStem (const std::string& name)
{
    std::string s;
    for (char c : name)
    {
        const unsigned char u = static_cast<unsigned char> (c);
        const bool ok = std::isalnum (u) || u >= 0x80 || c == ' ' || c == '-' || c == '_' || c == '#' || c == '(' || c == ')' || c == '.';
        s += ok ? c : '_';
    }
    while (! s.empty() && (s.front() == ' ' || s.front() == '.'))
        s.erase (s.begin());
    while (! s.empty() && (s.back() == ' ' || s.back() == '.'))
        s.pop_back();
    if (s.size() > 80)
    {
        s.resize (80);
        while (! s.empty() && (static_cast<unsigned char> (s.back()) & 0xC0) == 0x80) // do not cut a UTF-8 sequence in half
            s.pop_back();
        if (! s.empty() && static_cast<unsigned char> (s.back()) >= 0xC0)
            s.pop_back();
    }
    return s.empty() ? std::string ("untitled") : s;
}

std::string fileStemOf (const std::string& path)
{
    std::string s = path;
    const size_t slash = s.find_last_of ("/\\");
    if (slash != std::string::npos)
        s = s.substr (slash + 1);
    const size_t dot = s.find_last_of ('.');
    if (dot != std::string::npos && dot > 0 && s.size() - dot <= 6)
        s = s.substr (0, dot);
    return s;
}

// ---- UserContentStore ------------------------------------------------------------------------------------------------------------------

UserContentStore::UserContentStore (const std::string& userDir)
    : root_ (userDir.empty() ? std::string() : (fs::path (userDir) / "content").string())
{
}

void UserContentStore::addPackRoot (const std::string& dir)
{
    if (! dir.empty())
        extraRoots_.push_back (dir);
}

const char* UserContentStore::packFor (ContentKind kind, bool embedded) noexcept
{
    if (embedded)
        return kPackEmbedded;
    switch (kind)
    {
        case ContentKind::wavetable: return kPackWavetables;
        case ContentKind::sample:    return kPackSamples;
        case ContentKind::impulse:   return kPackImpulses;
    }
    return kPackWavetables;
}

const char* UserContentStore::directoryOfUserPack (const std::string& pack) noexcept
{
    if (pack == kPackWavetables) return "wavetables";
    if (pack == kPackSamples)    return "samples";
    if (pack == kPackImpulses)   return "impulses";
    if (pack == kPackEmbedded)   return "embedded";
    return "";
}

bool UserContentStore::readFile (const ContentFileRef& ref, std::vector<uint8_t>& bytes) const
{
    if (root_.empty() || ! safeRelative (ref.file) || (! ref.pack.empty() && ! safeRelative (ref.pack)))
        return false;
    const std::string dir = directoryOfUserPack (ref.pack);
    if (! dir.empty())
    {
        ContentFileRef inner = ref;
        inner.pack.clear();
        return DirectoryContentStore ((fs::path (root_) / dir).string()).readFile (inner, bytes);
    }
    if (DirectoryContentStore (root_).readFile (ref, bytes))
        return true;
    for (const std::string& extra : extraRoots_)
        if (DirectoryContentStore (extra).readFile (ref, bytes))
            return true;
    return false;
}

bool UserContentStore::contains (const ContentFileRef& ref) const
{
    std::vector<uint8_t> bytes;
    return readFile (ref, bytes);
}

bool UserContentStore::addFile (ContentKind kind, const std::string& fileName, const std::vector<uint8_t>& bytes, ContentFileRef& ref, bool embedded)
{
    if (root_.empty() || bytes.empty() || bytes.size() > kMaxContentFileBytes)
        return false;
    try
    {
        const std::string pack = packFor (kind, embedded);
        const std::string sha = Sha256::hashBufferHex (bytes.data(), bytes.size());
        const std::string stem = sanitizeFileStem (fileStemOf (fileName));
        const std::string rel = sha12 (sha) + "/" + stem + ".wav";
        const fs::path full = fs::path (root_) / directoryOfUserPack (pack) / rel;
        std::error_code ec;
        if (! fs::is_regular_file (full, ec) || static_cast<uint64_t> (fs::file_size (full, ec)) != bytes.size())
            if (! writeBytes (full, bytes))
                return false;
        ref.pack = pack;
        ref.file = rel;
        ref.sha256 = sha;
        ref.name = stem;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::vector<UserContentItem> UserContentStore::list (ContentKind kind) const
{
    std::vector<UserContentItem> items;
    if (root_.empty())
        return items;
    try
    {
        const char* pack = packFor (kind);
        listDirectory (fs::path (root_) / directoryOfUserPack (pack), pack, {}, kind, false, items);
        if (kind != ContentKind::impulse)
            listDirectory (fs::path (root_) / kPackImportedVital / (kind == ContentKind::wavetable ? "tables" : "samples"), kPackImportedVital,
                           kind == ContentKind::wavetable ? "tables" : "samples", kind, false, items);
        std::sort (items.begin(), items.end(), [] (const UserContentItem& a, const UserContentItem& b)
        {
            return a.ref.name != b.ref.name ? a.ref.name < b.ref.name : a.ref.file < b.ref.file;
        });
    }
    catch (...)
    {
        items.clear();
    }
    return items;
}

bool UserContentStore::remove (const ContentFileRef& ref)
{
    const std::string dir = directoryOfUserPack (ref.pack);
    if (root_.empty() || dir.empty() || ! safeRelative (ref.file))
        return false;
    std::error_code ec;
    const fs::path full = fs::path (root_) / dir / ref.file;
    fs::remove (full, ec);
    const fs::path parent = full.parent_path();
    if (parent != fs::path (root_) / dir && fs::is_empty (parent, ec))
        fs::remove (parent, ec);
    return ! fs::exists (full, ec);
}

// ---- state side ------------------------------------------------------------------------------------------------------------------------

void attachWavetable (StateContent& content, int osc, const ContentFileRef& ref, const std::string& author, int frames)
{
    detachWavetable (content, osc);
    ContentWavetable w;
    w.osc = osc;
    w.ref = ref;
    w.author = author;
    w.frames = frames;
    content.wavetables.push_back (std::move (w));
    std::sort (content.wavetables.begin(), content.wavetables.end(), [] (const ContentWavetable& a, const ContentWavetable& b) { return a.osc < b.osc; });
}

void attachSample (StateContent& content, int osc, const ContentFileRef& ref, int sampleRate, int frames, int channels, std::vector<float> slices)
{
    detachSample (content, osc);
    ContentSample s;
    s.osc = osc;
    s.ref = ref;
    s.sampleRate = sampleRate;
    s.frames = frames;
    s.channels = channels;
    s.slices = std::move (slices);
    content.samples.push_back (std::move (s));
    std::sort (content.samples.begin(), content.samples.end(), [] (const ContentSample& a, const ContentSample& b) { return a.osc < b.osc; });
}

void attachImpulse (StateContent& content, const ContentFileRef& ref)
{
    content.impulses.assign (1, ref);
}

void detachWavetable (StateContent& content, int osc)
{
    content.wavetables.erase (std::remove_if (content.wavetables.begin(), content.wavetables.end(), [osc] (const ContentWavetable& w) { return w.osc == osc; }),
                              content.wavetables.end());
}

void detachSample (StateContent& content, int osc)
{
    content.samples.erase (std::remove_if (content.samples.begin(), content.samples.end(), [osc] (const ContentSample& s) { return s.osc == osc; }),
                           content.samples.end());
}

void detachImpulse (StateContent& content)
{
    content.impulses.clear();
}

bool attachStoredSample (StateContent& content, ParamValues* values, int osc, const ContentFileRef& ref, const IContentStore& store)
{
    std::vector<uint8_t> bytes;
    SampleFacts facts;
    if (osc < 1 || osc > kMaxContentSamples || ! store.readFile (ref, bytes) || ! probeSampleWav (bytes, ref.file, facts))
        return false;
    attachSample (content, osc, ref, facts.sampleRate, static_cast<int> (std::min<int64_t> (facts.frames, 0x7FFFFFFF)), facts.channels);
    if (values != nullptr && facts.rootNote >= 0)
        if (const ParamInfo* info = findParamByKey (("osc" + std::to_string (osc) + ".sample.root").c_str()))
        {
            const int index = findParamIndex (info->id);
            if (index >= 0)
                values->plain[static_cast<size_t> (index)] = static_cast<float> (facts.rootNote);
        }
    return true;
}

bool attachStoredWavetable (StateContent& content, int osc, const ContentFileRef& ref, const IContentStore& store, const std::string& author)
{
    std::vector<uint8_t> bytes;
    wtedit::Wavetable table;
    if (osc < 1 || osc > kMaxContentWavetables || ! store.readFile (ref, bytes) || ! wavetableFromWav (bytes, ref.name, table))
        return false;
    attachWavetable (content, osc, ref, author, static_cast<int> (table.numFrames()));
    return true;
}

std::vector<MissingContent> findMissingContent (const StateContent& content, const IContentStore& store)
{
    std::vector<MissingContent> missing;
    const auto check = [&] (ContentKind kind, int osc, const ContentFileRef& ref)
    {
        std::vector<uint8_t> bytes;
        if (store.readFile (ref, bytes))
            return;
        MissingContent m;
        m.kind = kind;
        m.osc = osc;
        m.ref = ref;
        ContentFileRef noHash = ref;
        noHash.sha256.clear();
        m.reason = ! ref.sha256.empty() && store.readFile (noHash, bytes) ? MissingReason::hashMismatch : MissingReason::notFound;
        missing.push_back(std::move (m));
    };
    for (const auto& w : content.wavetables)
        check (ContentKind::wavetable, w.osc, w.ref);
    for (const auto& s : content.samples)
        check (ContentKind::sample, s.osc, s.ref);
    for (const auto& i : content.impulses)
        check (ContentKind::impulse, 0, i);
    return missing;
}

bool relinkMissing (StateContent& content, const MissingContent& item, const std::vector<uint8_t>& bytes, const std::string& fileName,
                    UserContentStore& store, bool acceptDifferent)
{
    if (bytes.empty())
        return false;
    if (! item.ref.sha256.empty() && ! acceptDifferent && Sha256::hashBufferHex (bytes.data(), bytes.size()) != item.ref.sha256)
        return false;
    ContentFileRef ref;
    if (! store.addFile (item.kind, fileName, bytes, ref))
        return false;
    if (! item.ref.name.empty())
        ref.name = item.ref.name;
    switch (item.kind)
    {
        case ContentKind::wavetable:
            for (auto& w : content.wavetables)
                if (w.osc == item.osc)
                    w.ref = ref;
            break;
        case ContentKind::sample:
            for (auto& s : content.samples)
                if (s.osc == item.osc)
                    s.ref = ref;
            break;
        case ContentKind::impulse:
            if (! content.impulses.empty())
                content.impulses[0] = ref;
            break;
    }
    return true;
}

void dropMissing (StateContent& content, const MissingContent& item)
{
    switch (item.kind)
    {
        case ContentKind::wavetable: detachWavetable (content, item.osc); break;
        case ContentKind::sample:    detachSample (content, item.osc); break;
        case ContentKind::impulse:   detachImpulse (content); break;
    }
}

// ---- audio -----------------------------------------------------------------------------------------------------------------------------

std::vector<uint8_t> encodeWav (const float* left, const float* right, size_t frames, int sampleRate, int bits)
{
    if (left == nullptr || frames == 0 || sampleRate <= 0 || (bits != 16 && bits != 24 && bits != 32))
        return {};
    const int channels = right != nullptr ? 2 : 1;
    const size_t bytesPer = static_cast<size_t> (bits / 8);
    const size_t dataBytes = frames * static_cast<size_t> (channels) * bytesPer;
    if (dataBytes > 0xFFFFFF00u - 64u)
        return {};
    std::vector<uint8_t> b;
    b.reserve (44 + dataBytes);
    putTag (b, "RIFF");
    put32 (b, static_cast<uint32_t> (36 + dataBytes));
    putTag (b, "WAVE");
    putTag (b, "fmt ");
    put32 (b, 16);
    put16 (b, bits == 32 ? 3u : 1u);
    put16 (b, static_cast<uint32_t> (channels));
    put32 (b, static_cast<uint32_t> (sampleRate));
    put32 (b, static_cast<uint32_t> (sampleRate) * static_cast<uint32_t> (channels) * static_cast<uint32_t> (bytesPer));
    put16 (b, static_cast<uint32_t> (channels) * static_cast<uint32_t> (bytesPer));
    put16 (b, static_cast<uint32_t> (bits));
    putTag (b, "data");
    put32 (b, static_cast<uint32_t> (dataBytes));
    const auto sample = [&] (float v)
    {
        v = std::isfinite (v) ? std::max (-1.0f, std::min (1.0f, v)) : 0.0f;
        if (bits == 16)
            put16 (b, static_cast<uint16_t> (static_cast<int16_t> (std::lround (v * 32767.0f))));
        else if (bits == 24)
        {
            const int32_t q = static_cast<int32_t> (std::lround (static_cast<double> (v) * 8388607.0));
            b.push_back (static_cast<uint8_t> (q & 0xFF));
            b.push_back (static_cast<uint8_t> ((q >> 8) & 0xFF));
            b.push_back (static_cast<uint8_t> ((q >> 16) & 0xFF));
        }
        else
        {
            uint32_t u;
            std::memcpy (&u, &v, 4);
            put32 (b, u);
        }
    };
    for (size_t i = 0; i < frames; ++i)
    {
        sample (left[i]);
        if (right != nullptr)
            sample (right[i]);
    }
    return b;
}

bool probeSampleWav (const std::vector<uint8_t>& wav, const std::string& fileName, SampleFacts& facts)
{
    sampleosc::WavInfo info;
    if (! sampleosc::readWavInfo (wav.data(), wav.size(), info))
        return false;
    facts.sampleRate = info.sampleRate;
    facts.channels = info.numChannels;
    facts.frames = info.numFrames;
    const int fromName = sampleosc::parseRootNoteFromFilename (fileName.c_str());
    facts.rootFromName = fromName >= 0;
    facts.rootNote = fromName >= 0 ? fromName : wavChunkRoot (wav);
    return true;
}

bool wavetableFromWav (const std::vector<uint8_t>& wav, const std::string& name, wtedit::Wavetable& out)
{
    std::vector<float> left, right;
    sampleosc::WavInfo info;
    if (! sampleosc::readWav (wav.data(), wav.size(), left, right, info) || left.empty())
        return false;
    wtedit::Wavetable table (name);
    table.clear();
    const size_t n = left.size();
    if (n >= wtedit::kWaveformSize && n % wtedit::kWaveformSize == 0)
    {
        const size_t count = std::min<size_t> (wtedit::kMaxFrames, n / wtedit::kWaveformSize);
        for (size_t f = 0; f < count; ++f)
        {
            wtedit::Frame frame;
            std::copy (left.begin() + static_cast<std::ptrdiff_t> (f * wtedit::kWaveformSize), left.begin() + static_cast<std::ptrdiff_t> ((f + 1) * wtedit::kWaveformSize),
                       frame.data.begin());
            if (f == 0)
                table.setFrame (0, frame);
            else
                table.insertFrame (f, frame);
        }
    }
    else
    {
        wtedit::Frame frame;
        wtedit::resampleBuffer (left.data(), n, frame.sampleData(), wtedit::kWaveformSize);
        table.setFrame (0, frame);
    }
    table.setSampleRate (info.sampleRate > 0 ? info.sampleRate : 44100.0);
    out = std::move (table);
    return true;
}

std::vector<uint8_t> wavetableToWav (const wtedit::Wavetable& table)
{
    wtedit::ExportOptions options;
    options.format = wtedit::ExportFormat::Wav16Bit;
    std::vector<uint8_t> bytes;
    if (! wtedit::exportWav (table, options, bytes))
        return {};
    return bytes;
}

std::vector<uint8_t> trimSampleWav (const std::vector<uint8_t>& wav, float startNorm, float endNorm)
{
    std::vector<float> left, right;
    sampleosc::WavInfo info;
    if (! sampleosc::readWav (wav.data(), wav.size(), left, right, info) || left.empty())
        return {};
    const double n = static_cast<double> (left.size());
    const size_t a = static_cast<size_t> (std::max (0.0, std::min (n, std::floor (static_cast<double> (std::min (startNorm, endNorm)) * n))));
    const size_t b = static_cast<size_t> (std::max (0.0, std::min (n, std::ceil (static_cast<double> (std::max (startNorm, endNorm)) * n))));
    if (b <= a || b - a < 16)
        return {};
    return encodeWav (left.data() + a, info.numChannels >= 2 ? right.data() + a : nullptr, b - a, info.sampleRate, 24);
}

// ---- SamplePreview ---------------------------------------------------------------------------------------------------------------------

std::shared_ptr<const SamplePreview> SamplePreview::create (const std::vector<uint8_t>& wav, const std::string& fileName)
{
    std::vector<float> left, right;
    sampleosc::WavInfo info;
    if (! sampleosc::readWav (wav.data(), wav.size(), left, right, info, fileName.c_str()) || left.empty())
        return nullptr;
    auto p = std::make_shared<SamplePreview>();
    p->sampleRate_ = info.sampleRate;
    p->channels_ = info.numChannels;
    p->mono_.resize (left.size());
    for (size_t i = 0; i < left.size(); ++i)
        p->mono_[i] = info.numChannels >= 2 ? 0.5f * (left[i] + right[i]) : left[i];
    sampleosc::SampleData data;
    if (data.loadFromFrames (left.data(), info.numChannels >= 2 ? right.data() : nullptr, static_cast<int64_t> (left.size()), info.sampleRate))
        for (const auto& o : data.detectOnsets())
            p->onsets_.emplace_back (o.position, o.strength);
    return p;
}

void SamplePreview::peaks (float startNorm, float endNorm, int columns, std::vector<float>& minOut, std::vector<float>& maxOut) const
{
    minOut.assign (static_cast<size_t> (std::max (columns, 0)), 0.0f);
    maxOut.assign (static_cast<size_t> (std::max (columns, 0)), 0.0f);
    if (mono_.empty() || columns <= 0)
        return;
    const double n = static_cast<double> (mono_.size());
    const double a = std::max (0.0, std::min (1.0, static_cast<double> (startNorm))) * n;
    const double b = std::max (a, std::min (1.0, static_cast<double> (endNorm)) * n);
    for (int c = 0; c < columns; ++c)
    {
        const size_t i0 = static_cast<size_t> (a + (b - a) * c / columns);
        const size_t i1 = std::min (mono_.size(), std::max (i0 + 1, static_cast<size_t> (a + (b - a) * (c + 1) / columns)));
        float lo = mono_[std::min (i0, mono_.size() - 1)], hi = lo;
        for (size_t i = i0; i < i1; ++i)
        {
            lo = std::min (lo, mono_[i]);
            hi = std::max (hi, mono_[i]);
        }
        minOut[static_cast<size_t> (c)] = lo;
        maxOut[static_cast<size_t> (c)] = hi;
    }
}

std::vector<float> SamplePreview::autoSlices (float threshold) const
{
    std::vector<float> slices { 0.0f };
    const float need = 1.0f - std::max (0.0f, std::min (1.0f, threshold));
    for (const auto& o : onsets_)
        if (o.second >= need && o.first > 0.0f && static_cast<int> (slices.size()) < kMaxSampleSlices)
            slices.push_back (o.first);
    return slices;
}

float SamplePreview::valueAt (float norm) const
{
    if (mono_.empty())
        return 0.0f;
    const size_t i = static_cast<size_t> (std::max (0.0, std::min (1.0, static_cast<double> (norm))) * static_cast<double> (mono_.size() - 1));
    return mono_[i];
}

float SamplePreview::snapToZero (float norm, int windowFrames) const
{
    if (mono_.size() < 4)
        return norm;
    const int64_t n = static_cast<int64_t> (mono_.size());
    const int64_t centre = static_cast<int64_t> (std::max (0.0, std::min (1.0, static_cast<double> (norm))) * static_cast<double> (n - 1));
    for (int64_t d = 0; d <= windowFrames; ++d)
        for (int sign : { 1, -1 })
        {
            const int64_t i = centre + sign * d;
            if (i < 1 || i >= n)
                continue;
            if (mono_[static_cast<size_t> (i - 1)] <= 0.0f && mono_[static_cast<size_t> (i)] > 0.0f)
                return static_cast<float> (static_cast<double> (i) / static_cast<double> (n - 1));
        }
    return norm;
}

float snapMarker (SnapMode mode, float norm, const SamplePreview* sample, float beatGrid, float loopStart, float loopEnd, float loopTolerance)
{
    norm = std::max (0.0f, std::min (1.0f, norm));
    switch (mode)
    {
        case SnapMode::off:
            return norm;
        case SnapMode::zero:
            return sample != nullptr ? sample->snapToZero (norm) : norm;
        case SnapMode::beats:
            return beatGrid > 1.0e-6f ? std::max (0.0f, std::min (1.0f, std::round (norm / beatGrid) * beatGrid)) : norm;
        case SnapMode::loop:
        {
            const float dStart = std::abs (norm - loopStart), dEnd = std::abs (norm - loopEnd);
            if (std::min (dStart, dEnd) > loopTolerance)
                return norm;
            return dStart <= dEnd ? loopStart : loopEnd;
        }
    }
    return norm;
}

std::string serializeLfoShapeFile (const LfoShapeFile& shape)
{
    std::string s = "{\"format\":\"synthyboy-lfo-shape\",\"version\":1,\"name\":\"";
    for (char c : shape.name)
    {
        if (c == '"' || c == '\\')
        {
            s += '\\';
            s += c;
        }
        else if (static_cast<unsigned char> (c) < 0x20)
            s += ' ';
        else
            s += c;
    }
    s += "\",\"smooth\":";
    s += shape.smooth ? "true" : "false";
    s += ",\"points\":[";
    for (size_t i = 0; i < shape.points.size(); ++i)
    {
        char buf[96];
        std::snprintf (buf, sizeof buf, "%s{\"x\":%.9g,\"y\":%.9g,\"power\":%.9g}", i > 0 ? "," : "", static_cast<double> (shape.points[i].x),
                       static_cast<double> (shape.points[i].y), static_cast<double> (shape.points[i].power));
        s += buf;
    }
    s += "]}";
    return s;
}
} // namespace synthyboy
