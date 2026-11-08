// SynthyBoy - core: ContentPack format, manifest parser, and verification (docs/ARCHITECTURE.md 6).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/content_pack.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "sha256.h"

namespace synthyboy
{
namespace
{
constexpr size_t kMaxManifestBytes = 512 * 1024; // 512 KiB

void escapeJsonString (const std::string& in, std::string& out)
{
    out += '"';
    for (char c : in)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char> (c) < 0x20)
                {
                    char buf[8];
                    std::snprintf (buf, sizeof buf, "\\u%04x", static_cast<unsigned char> (c));
                    out += buf;
                }
                else
                {
                    out += c;
                }
                break;
        }
    }
    out += '"';
}

class ManifestParser
{
public:
    ManifestParser (const char* b, const char* e) noexcept : p (b), end (e) {}

    bool parse (ContentPackInfo& out) noexcept
    {
        skipBOM();
        skipWs();
        if (! consume ('{'))
            return false;

        bool hasId = false;
        bool hasVersion = false;
        bool hasSchema = false;
        bool hasLicense = false;
        bool hasAttribution = false;
        bool hasKind = false;
        bool hasRequires = false;
        bool hasFiles = false;

        while (p < end)
        {
            skipWs();
            if (consume ('}'))
                break;

            char key[64];
            bool truncated = false;
            if (! parseString (key, sizeof key, truncated))
                return false;

            skipWs();
            if (! consume (':'))
                return false;
            skipWs();

            char strVal[256];
            if (std::strcmp (key, "id") == 0)
            {
                if (! parseString (strVal, sizeof strVal, truncated) || truncated) return false;
                out.id = strVal;
                hasId = true;
            }
            else if (std::strcmp (key, "name") == 0)
            {
                if (! parseString (strVal, sizeof strVal, truncated) || truncated) return false;
                out.name = strVal;
            }
            else if (std::strcmp (key, "version") == 0)
            {
                if (! parseString (strVal, sizeof strVal, truncated) || truncated) return false;
                out.version = strVal;
                hasVersion = true;
            }
            else if (std::strcmp (key, "schemaVersion") == 0)
            {
                int val = 0;
                if (! parseInt (val) || val != 1) return false;
                out.schemaVersion = val;
                hasSchema = true;
            }
            else if (std::strcmp (key, "license") == 0)
            {
                if (! parseString (strVal, sizeof strVal, truncated) || truncated) return false;
                out.license = strVal;
                hasLicense = true;
            }
            else if (std::strcmp (key, "attribution") == 0)
            {
                if (! parseString (strVal, sizeof strVal, truncated) || truncated) return false;
                out.attribution = strVal;
                hasAttribution = true;
            }
            else if (std::strcmp (key, "kind") == 0)
            {
                if (! parseString (strVal, sizeof strVal, truncated) || truncated) return false;
                out.kind = strVal;
                hasKind = (out.kind == "presets" || out.kind == "wavetables" || out.kind == "samples");
                if (! hasKind) return false;
            }
            else if (std::strcmp (key, "requires_plugin") == 0)
            {
                if (! parseString (strVal, sizeof strVal, truncated) || truncated) return false;
                out.requiresPlugin = strVal;
                hasRequires = true;
            }
            else if (std::strcmp (key, "description") == 0)
            {
                if (! parseString (strVal, sizeof strVal, truncated) || truncated) return false;
                out.description = strVal;
            }
            else if (std::strcmp (key, "files") == 0 && p < end && *p == '[')
            {
                if (! parseFiles (out.files)) return false;
                hasFiles = true;
            }
            else
            {
                if (! skipValue (1)) return false;
            }

            skipWs();
            if (consume (','))
                continue;
            if (consume ('}'))
                break;
            return false;
        }

        return hasId && hasVersion && hasSchema && hasLicense && hasAttribution && hasKind && hasRequires && hasFiles;
    }

private:
    const char* p;
    const char* end;

    void skipBOM() noexcept
    {
        if (end - p >= 3 && static_cast<unsigned char> (p[0]) == 0xef && static_cast<unsigned char> (p[1]) == 0xbb
            && static_cast<unsigned char> (p[2]) == 0xbf)
            p += 3;
    }

    void skipWs() noexcept
    {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            ++p;
    }

    bool consume (char c) noexcept
    {
        if (p < end && *p == c)
        {
            ++p;
            return true;
        }
        return false;
    }

    bool parseInt (int& out) noexcept
    {
        skipWs();
        if (p >= end) return false;
        bool neg = false;
        if (*p == '-') { neg = true; ++p; }
        else if (*p == '+') { ++p; }
        if (p >= end || *p < '0' || *p > '9') return false;
        long v = 0;
        while (p < end && *p >= '0' && *p <= '9')
        {
            v = v * 10 + (*p - '0');
            if (v > 1000000) return false;
            ++p;
        }
        out = static_cast<int> (neg ? -v : v);
        return true;
    }

    bool parseString (char* out, size_t cap, bool& truncated) noexcept
    {
        truncated = false;
        if (! consume ('"'))
            return false;

        size_t n = 0;
        auto put = [&] (char c) noexcept
        {
            if (out != nullptr && n + 1 < cap)
                out[n++] = c;
            else
                truncated = true;
        };

        while (p < end)
        {
            const unsigned char c = static_cast<unsigned char> (*p++);
            if (c == '"')
            {
                if (out != nullptr && cap > 0)
                    out[n] = '\0';
                return true;
            }
            if (c < 0x20)
                return false;
            if (c != '\\')
            {
                put (static_cast<char> (c));
                continue;
            }

            if (p >= end) return false;
            const char e = *p++;
            switch (e)
            {
                case '"': put ('"'); break;
                case '\\': put ('\\'); break;
                case '/': put ('/'); break;
                case 'b': put ('\b'); break;
                case 'f': put ('\f'); break;
                case 'n': put ('\n'); break;
                case 'r': put ('\r'); break;
                case 't': put ('\t'); break;
                default: put (e); break;
            }
        }
        return false;
    }

    bool skipValue (int depth) noexcept
    {
        if (depth > 16 || p >= end) return false;
        skipWs();
        if (p >= end) return false;
        const char c = *p;
        if (c == '"')
        {
            bool tr = false;
            return parseString (nullptr, 0, tr);
        }
        if (c == '{' || c == '[')
        {
            const char close = (c == '{') ? '}' : ']';
            ++p;
            skipWs();
            if (consume (close)) return true;
            for (;;)
            {
                if (c == '{')
                {
                    bool tr = false;
                    if (! parseString (nullptr, 0, tr)) return false;
                    skipWs();
                    if (! consume (':')) return false;
                }
                if (! skipValue (depth + 1)) return false;
                skipWs();
                if (consume (',')) continue;
                if (consume (close)) return true;
                return false;
            }
        }
        if (c == '-' || (c >= '0' && c <= '9'))
        {
            while (p < end && (*p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E' || (*p >= '0' && *p <= '9')))
                ++p;
            return true;
        }
        if (end - p >= 4 && std::strncmp (p, "true", 4) == 0) { p += 4; return true; }
        if (end - p >= 5 && std::strncmp (p, "false", 5) == 0) { p += 5; return true; }
        if (end - p >= 4 && std::strncmp (p, "null", 4) == 0) { p += 4; return true; }
        return false;
    }

    bool parseFiles (std::vector<ContentPackFile>& files) noexcept
    {
        if (! consume ('[')) return false;
        skipWs();
        if (consume (']')) return true;

        for (;;)
        {
            skipWs();
            if (! consume ('{')) return false;

            ContentPackFile f;
            bool hasPath = false;
            bool hasHash = false;

            for (;;)
            {
                skipWs();
                if (consume ('}')) break;

                char k[64];
                bool tr = false;
                if (! parseString (k, sizeof k, tr)) return false;
                skipWs();
                if (! consume (':')) return false;
                skipWs();

                if (std::strcmp (k, "path") == 0)
                {
                    char pbuf[256];
                    if (! parseString (pbuf, sizeof pbuf, tr) || tr) return false;
                    f.path = pbuf;
                    hasPath = true;
                }
                else if (std::strcmp (k, "sha256") == 0)
                {
                    char hbuf[128];
                    if (! parseString (hbuf, sizeof hbuf, tr) || tr) return false;
                    f.sha256 = hbuf;
                    hasHash = (f.sha256.size() == 64);
                }
                else if (std::strcmp (k, "size") == 0)
                {
                    int sz = 0;
                    if (! parseInt (sz)) return false;
                    f.size = static_cast<size_t> (sz >= 0 ? sz : 0);
                }
                else
                {
                    if (! skipValue (1)) return false;
                }

                skipWs();
                if (consume (',')) continue;
                if (consume ('}')) break;
                return false;
            }

            if (! hasPath || ! hasHash)
                return false;

            files.push_back (std::move (f));

            skipWs();
            if (consume (',')) continue;
            if (consume (']')) return true;
            return false;
        }
    }
};
} // namespace

bool parseContentPackManifest (const void* data, size_t size, ContentPackInfo& out) noexcept
{
    if (data == nullptr || size == 0 || size > kMaxManifestBytes)
        return false;

    const char* b = static_cast<const char*> (data);
    ManifestParser parser (b, b + size);
    return parser.parse (out);
}

std::string serializeContentPackManifest (const ContentPackInfo& info)
{
    std::string s;
    s.reserve (256 + info.files.size() * 128);
    s += "{\n";
    s += "  \"schemaVersion\": ";
    s += std::to_string (info.schemaVersion > 0 ? info.schemaVersion : 1);
    s += ",\n  \"id\": ";
    escapeJsonString (info.id, s);
    if (! info.name.empty())
    {
        s += ",\n  \"name\": ";
        escapeJsonString (info.name, s);
    }
    s += ",\n  \"version\": ";
    escapeJsonString (info.version.empty() ? "1.0.0" : info.version, s);
    s += ",\n  \"license\": ";
    escapeJsonString (info.license.empty() ? "user-supplied" : info.license, s);
    s += ",\n  \"attribution\": ";
    escapeJsonString (info.attribution.empty() ? "user-supplied" : info.attribution, s);
    s += ",\n  \"kind\": ";
    escapeJsonString (info.kind.empty() ? "wavetables" : info.kind, s);
    s += ",\n  \"requires_plugin\": ";
    escapeJsonString (info.requiresPlugin.empty() ? ">=0.1.0" : info.requiresPlugin, s);
    if (! info.description.empty())
    {
        s += ",\n  \"description\": ";
        escapeJsonString (info.description, s);
    }
    s += ",\n  \"files\": [";
    for (size_t i = 0; i < info.files.size(); ++i)
    {
        const auto& f = info.files[i];
        s += "\n    {\n      \"path\": ";
        escapeJsonString (f.path, s);
        s += ",\n      \"sha256\": ";
        escapeJsonString (f.sha256, s);
        s += ",\n      \"size\": ";
        s += std::to_string (f.size);
        s += "\n    }";
        if (i + 1 < info.files.size())
            s += ",";
    }
    if (! info.files.empty())
        s += "\n  ";
    s += "]\n}\n";
    return s;
}

bool writeContentPackManifest (const std::string& packDirectory, const ContentPackInfo& info)
{
    std::filesystem::path root (packDirectory);
    std::error_code ec;
    std::filesystem::create_directories (root, ec);
    if (ec)
        return false;

    const std::string jsonText = serializeContentPackManifest (info);
    const std::filesystem::path manifestPath = root / "contentpack.json";
    const std::filesystem::path tmpPath = root / "contentpack.json.tmp";

    std::ofstream out (tmpPath.string(), std::ios::binary);
    if (! out)
        return false;
    out.write (jsonText.data(), static_cast<std::streamsize> (jsonText.size()));
    out.close();
    if (! out.good())
        return false;

    std::filesystem::rename (tmpPath, manifestPath, ec);
    if (ec)
    {
        // Fallback to direct write
        std::ofstream fallback (manifestPath.string(), std::ios::binary);
        if (! fallback)
            return false;
        fallback.write (jsonText.data(), static_cast<std::streamsize> (jsonText.size()));
        fallback.close();
        std::filesystem::remove (tmpPath, ec);
        return fallback.good();
    }
    return true;
}

bool verifyContentPack (const std::string& packDirectory, const ContentPackInfo& info, std::vector<std::string>& errorsOut)
{
    bool allOk = true;
    std::filesystem::path root (packDirectory);

    for (const auto& f : info.files)
    {
        std::filesystem::path full = root / f.path;
        std::error_code ec;
        if (! std::filesystem::exists (full, ec))
        {
            errorsOut.push_back ("Missing file: " + f.path);
            allOk = false;
            continue;
        }

        const std::string computed = Sha256::hashFileHex (full.string());
        if (computed != f.sha256)
        {
            errorsOut.push_back ("Checksum mismatch for " + f.path + ": expected " + f.sha256 + ", got " + computed);
            allOk = false;
        }
    }

    return allOk;
}

ContentRegistry::ContentRegistry() = default;

void ContentRegistry::addSearchPath (const std::string& path)
{
    searchPaths_.push_back (path);
    rescanPacks();
}

void ContentRegistry::rescanPacks()
{
    packs_.clear();
    std::error_code ec;

    for (const auto& sp : searchPaths_)
    {
        std::filesystem::path root (sp);
        if (! std::filesystem::exists (root, ec) || ! std::filesystem::is_directory (root, ec))
            continue;

        // Check if root itself is a pack
        std::filesystem::path manifestPath = root / "contentpack.json";
        if (std::filesystem::exists (manifestPath, ec))
        {
            std::ifstream file (manifestPath.string(), std::ios::binary);
            if (file)
            {
                std::string content ((std::istreambuf_iterator<char> (file)), std::istreambuf_iterator<char>());
                ContentPackInfo info;
                if (parseContentPackManifest (content.data(), content.size(), info))
                    packs_.push_back (std::move (info));
            }
        }

        // Check subdirectories
        for (const auto& sub : std::filesystem::directory_iterator (root, ec))
        {
            if (ec) break;
            if (sub.is_directory (ec))
            {
                std::filesystem::path subManifest = sub.path() / "contentpack.json";
                if (std::filesystem::exists (subManifest, ec))
                {
                    std::ifstream file (subManifest.string(), std::ios::binary);
                    if (file)
                    {
                        std::string content ((std::istreambuf_iterator<char> (file)), std::istreambuf_iterator<char>());
                        ContentPackInfo info;
                        if (parseContentPackManifest (content.data(), content.size(), info))
                            packs_.push_back (std::move (info));
                    }
                }
            }
        }
    }
}

std::string ContentRegistry::resolvePresetPath (const std::string& relativePath)
{
    std::error_code ec;
    for (const auto& sp : searchPaths_)
    {
        std::filesystem::path cand = std::filesystem::path (sp) / relativePath;
        if (std::filesystem::exists (cand, ec) && std::filesystem::is_regular_file (cand, ec))
            return cand.string();
    }

    markMissingContent();
    return {};
}
} // namespace synthyboy
