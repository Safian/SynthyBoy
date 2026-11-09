// SynthyBoy - core/serum-import: the importer class shared by serum_import.cpp, serum_content.cpp and serum_fx.cpp (internal header).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "serum_internal.h"
#include "serum_map.h"
#include "synthyboy/fx_rack.h"
#include "synthyboy/serum_import.h"

namespace synthyboy
{
namespace serumimport
{
/** Where an effect of a rack ended up (for the modulation destinations that name an effect by rack and position). */
struct PlacedFx
{
    int type = 0;        // kFxType*, 0 = not placed (splitter, dropped)
    int slot = -1;       // pool slot, -1 = a module of instance 0
    const char* prefix = ""; // instance 0: the key prefix ("convolve")
    std::string serumName;
    std::string eq[2];   // FXEQ: the group ("low", "mid", "high") each Serum band was written to
};

class Importer
{
public:
    Importer (const Container& c, const SerumImportOptions& o, SerumImportResult& r, const uint8_t* srcData, size_t srcSize);

    // ---- entry points (serum_import.cpp) ----
    void runPreset();
    void runFxFile();    // .SerumFX / .SerumFXRack
    void runShape();     // .XferShape
    void runUnmapped (const char* what); // the families without an equivalent
    void finish();       // the sweep of unconsumed leaves, counts, fidelity

    // ---- infra ----
    const cbor::Doc& d;
    const Container& con;
    const SerumImportOptions& opt;
    SerumImportResult& R;
    std::vector<uint8_t> used;      // per node: consumed
    std::vector<uint8_t> touched;   // per table index: written by the import
    std::vector<uint8_t> setByFile; // per table index: written from a value of the file (not a default)
    int leafCount = 0;
    std::vector<PlacedFx> placed[3];
    int randomLfoOf[10];            // LFO index -> rlfo number 1..4, 0 = a shape LFO
    int usedRandomLfos = 0;
    int lfoOrigPoints[10];          // points of the file's curve per LFO (for the point modulation assignments), 0 = none / altered
    int lfoPointOffset[10];
    std::vector<int> fxFileTouched;
    int deadlineHit = 0;
    int64_t startMs = 0;

    void entry (ItemKind kind, Reason reason, const std::string& area, const std::string& path, const std::string& value, const std::string& key, const std::string& ours,
                const std::string& detail, int count = 1);
    bool cancelled();

    // value tree access
    const cbor::Node& N (int i) const { return d.nodes[static_cast<size_t> (i)]; }
    int get (int map, const char* key) const { return map >= 0 ? d.find (static_cast<uint32_t> (map), key) : -1; }
    bool isMap (int i) const { return i >= 0 && N (i).type == cbor::Type::map; }
    bool isNumber (int i) const { return i >= 0 && (N (i).type == cbor::Type::integer || N (i).type == cbor::Type::real || N (i).type == cbor::Type::boolean); }
    bool isText (int i) const { return i >= 0 && N (i).type == cbor::Type::text; }
    double num (int i, double fb = 0.0) const;
    std::string str (int i) const { return i >= 0 ? d.text (static_cast<uint32_t> (i)) : std::string(); }
    template <class F> void forMap (int map, F f) const
    {
        if (! isMap (map)) return;
        const cbor::Node& m = N (map);
        for (uint32_t k = 0; k < m.count; ++k) f (d.text (d.kids[m.first + 2 * k]), static_cast<int> (d.kids[m.first + 2 * k + 1]), static_cast<int> (d.kids[m.first + 2 * k]));
    }
    void consume (int node);          // the node and everything below
    int unusedLeaves (int node) const; // leaves below the node that no rule consumed yet
    /** One entry for all the leaves below `node` that are still unconsumed (count = their number), then the whole subtree is consumed. */
    void acctRest (int node, ItemKind kind, Reason reason, const std::string& area, const std::string& path, const std::string& key, const std::string& ours, const std::string& detail);
    void consumeLeaf (int node);      // just this node (a leaf or the shell of a container)
    bool isDefaultBlock (int plain) const; // plainParams: "default"
    /** A block the file stores as the text "default": one leaf, ignored (reported once for the file). */
    void noteDefault (int node);
    int defaultLeaves = 0;
    std::string fmt (double v) const;
    std::string valueText (int node) const;

    // parameters
    int indexOf (const std::string& key) const;
    /** Stores `v` in our parameter (constrained); returns the stored value. `clamped` is set when the range / grid changed the value. */
    double store (const std::string& key, double v, bool* clamped = nullptr);
    static std::string keyOf (const char* pattern, int n);

    /** Applies the rows of one scope to the plainParams map `plain` of module instance `n` (1-based); parameters without a row are returned in `unmapped`
        (name, value node). `area` and `path` are for the report. Rows with a Serum default that the file did not mention are applied afterwards. */
    struct Unmapped { std::string name; int node; };
    void applyScope (std::initializer_list<const char*> scopes, int plain, int n, const std::string& area, const std::string& path, std::vector<Unmapped>& unmapped,
                     bool applyDefaults = true, const std::vector<const char*>& skip = {});
    bool applyRuleValue (const Rule& r, int node, int n, const std::string& area, const std::string& path, double* storedOut = nullptr);
    void dropUnmapped (const std::vector<Unmapped>& list, const std::string& area, const std::string& path, const char* reason, Reason code = Reason::noEngineEquivalent);

    // ---- sections ----
    void meta();
    void oscillator (int i);
    void noiseOsc();
    void subOsc();
    void routing();
    void voiceFilter (int i);
    void envelope (int i);
    void lfo (int i);
    void macro (int i);
    void global();
    void modSlots();
    void curves();
    void unmappedTopLevel();
    void fxRack (int rack, bool fileIsRack);
    void fxEntry (int rack, int entry, int node, const std::string& path);
    void lfoPointAssignments();
    void convolveImpulse (int fxMap, const std::string& path);
    void lfoShapeOf (int lfoIndex, int lfoNode);

    // ---- content (serum_content.cpp) ----
    struct Table { std::vector<float> frames; int numFrames = 0; std::string name; };
    bool loadTableFile (const std::string& relPath, Table& out);
    bool tableFromEmbedded (int node, int channels, Table& out);
    void attachTable (int osc, const Table& t, const std::string& area, const std::string& path, bool fromFile);
    bool loadSample (const std::string& area, const std::string& relPath, const std::string& displayName, int osc, const std::string& path, int baseNote, int* sampleFramesOut = nullptr,
                     int* sampleRateOut = nullptr, std::vector<float>* slices = nullptr);
    bool readContent (const char* area, const std::string& rel, std::vector<uint8_t>& bytes);
    bool pointsFromCurve (int curveNode, std::vector<ContentPoint>& out, bool closeAtOne, int* loopback = nullptr);
    bool applyMultisample (int osc, int msNode, const std::string& path, int* rootOut);

    // geometry shared by the LFO shapes / mod curves
    static double powerOfCurve (double c);
    static std::vector<ContentPoint> reduce (std::vector<ContentPoint> p, size_t maxPoints);

    // the modulation destinations
    struct Dest { std::string key; Fid fid = Fid::exact; std::string note; bool ok = false; bool dbLaw = false; std::string srcKey; Reason why = Reason::modDestinationUnsupported; std::string whyText; };
    Dest destOf (const std::string& type, int id, const std::string& param);

    // LFO point modulation
    struct BusUse { int slot = 0; std::string source; double amount = 0; };
    BusUse bus[16];
};

std::string jsonEscapeString (const std::string& s);
int leavesUnder (const cbor::Doc& d, int node);
} // namespace serumimport
} // namespace synthyboy
