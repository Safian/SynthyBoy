// SynthyBoy - MPC UI component library: the pure parameter/Q-Link/layout model contract (no drawing, no touch handling).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace synthyboy_mpc { namespace ui {

struct Size { int w = 0, h = 0; };

struct Rect
{
    int x = 0, y = 0, w = 0, h = 0;
    constexpr int right() const noexcept { return x + w; }
    constexpr int bottom() const noexcept { return y + h; }
    constexpr bool empty() const noexcept { return w <= 0 || h <= 0; }
    constexpr bool contains (int px, int py) const noexcept { return px >= x && px < x + w && py >= y && py < y + h; }
    constexpr bool contains (const Rect& o) const noexcept { return o.x >= x && o.y >= y && o.right() <= right() && o.bottom() <= bottom(); }
    constexpr bool overlaps (const Rect& o) const noexcept { return ! empty() && ! o.empty() && x < o.right() && o.x < right() && y < o.bottom() && o.y < bottom(); }
    constexpr Rect inset (int d) const noexcept { return { x + d, y + d, std::max (0, w - 2 * d), std::max (0, h - 2 * d) }; }
    constexpr bool operator== (const Rect& o) const noexcept { return x == o.x && y == o.y && w == o.w && h == o.h; }
    static Rect unite (const Rect& a, const Rect& b) noexcept
    {
        if (a.empty()) return b;
        if (b.empty()) return a;
        const int x0 = std::min (a.x, b.x), y0 = std::min (a.y, b.y), x1 = std::max (a.right(), b.right()), y1 = std::max (a.bottom(), b.bottom());
        return { x0, y0, x1 - x0, y1 - y0 };
    }
};

/** What a control model or component hands back. Value: `value` is normalised 0 .. 1 (a knob). Index: `index` is a whole number (a list position, a switch 0 / 1, the number of steps from the minimum of a
    number field) and `value` is the same number as a double (a number field: the value in its own units, a whole number of steps). Request: the user asked for a menu / a panel; the owner
    opens whatever it wants. */
enum class ChangeKind : uint8_t { None = 0, Value, Index, Request, Depth, ModsRequest, AddRequest };
struct Change
{
    ChangeKind kind = ChangeKind::None;
    int id = 0;
    double value = 0.0;
    int index = 0;
    explicit operator bool() const noexcept { return kind != ChangeKind::None; }
    /** Depth: the modulation depth (-1 .. +1, `value`) of the source `index` (its id) of a knob / number field changed (a drag that started on the tab). ModsRequest: a long press asks for the
        list of the modulations of the control. */
    static Change depth (int id, int source, double d) noexcept { Change c; c.kind = ChangeKind::Depth; c.id = id; c.index = source; c.value = d; return c; }
    static Change modsRequest (int id) noexcept { Change c; c.kind = ChangeKind::ModsRequest; c.id = id; return c; }
    static Change none() noexcept { return {}; }
    static Change normalised (int id, double v) noexcept { Change c; c.kind = ChangeKind::Value; c.id = id; c.value = v; return c; }
    static Change indexed (int id, int i, double v) noexcept { Change c; c.kind = ChangeKind::Index; c.id = id; c.index = i; c.value = v; return c; }
    static Change request (int id) noexcept { Change c; c.kind = ChangeKind::Request; c.id = id; return c; }
};

/** Copies up to cap - 1 characters (zero terminated). */
inline void setText (char* dst, size_t cap, const char* src) noexcept
{
    if (dst == nullptr || cap == 0)   // no room, not even for the terminator: nothing is written
        return;
    size_t n = src ? std::strlen (src) : 0;   // (strlen + memmove: the compiler knows the length of a literal; a copy loop with two exits made GCC warn about reads past it)
    if (n + 1 > cap)
    {
        n = cap > 0 ? cap - 1 : 0;
        while (n > 0 && (static_cast<unsigned char> (src[n]) & 0xc0u) == 0x80u)   // a cut never falls inside a UTF-8 sequence
            --n;
    }
    if (n > 0)
        std::memmove (dst, src, n);
    dst[n] = '\0';
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
/** The modulation input of a Knob and a NumberField: up to kMaxMods entries {source id (opaque), source name <= 6 characters, depth -1 .. +1 as a fraction of the full range, polarity,
    bypassed} and the selected source (kNoSource = none). */
constexpr int kNoSource = INT32_MIN;
struct ModEntry
{
    int source = 0;
    char name[7] = {};
    double depth = 0.0;
    bool bidirectional = false, bypassed = false;
    bool macro = false;   // the source is a macro: the colour of the macros, whatever the (short) name says (a macro can be called anything)
    /** The colour of the source on the depth arc, the tab and the row: by its kind for a macro, else by its name (defined in core.h via theme::sourceColour). */
    uint32_t colour() const noexcept;
};
struct ModInput
{
    static constexpr int kMaxMods = 8;
    ModEntry mods[kMaxMods];
    int numMods = 0;
    int selectedSource = kNoSource;

    bool add (int source, const char* name, double depth, bool bidirectional = false, bool bypassed = false, bool macro = false) noexcept
    {
        if (numMods >= kMaxMods)
            return false;
        ModEntry& e = mods[numMods++];
        e.source = source; setText (e.name, sizeof e.name, name); e.depth = depth; e.bidirectional = bidirectional; e.bypassed = bypassed; e.macro = macro;
        return true;
    }
    /** The entry of the selected source (null when none is selected or it is not in `mods`). */
    const ModEntry* selected() const noexcept
    {
        for (int i = 0; i < std::min (numMods, kMaxMods); ++i)
            if (mods[i].source == selectedSource && selectedSource != kNoSource)
                return &mods[i];
        return nullptr;
    }
};
/** The tab at the top left: Absent (no modulation entries), Grey (entries, but not the selected source), Coloured (the selected source is in the entries), Hollow (and it is bypassed). */
enum class TabState : uint8_t { Absent, Grey, Coloured, Hollow };
/** The ends (0 .. 1) of the depth arc around the value position v: unidirectional v .. v + depth, bidirectional v - |depth| .. v + |depth|, clamped to the range; from <= to. false when
    the selected source is not in the entries or is bypassed (no arc). */
inline bool depthArc (const ModInput& m, double v, double& from, double& to) noexcept
{
    const ModEntry* e = m.selected();
    if (! e || e->bypassed)
        return false;
    v = std::clamp (v, 0.0, 1.0);
    const double d = std::clamp (e->depth, -1.0, 1.0);
    double a, b;
    if (e->bidirectional) { a = v - std::fabs (d); b = v + std::fabs (d); }
    else { a = std::min (v, v + d); b = std::max (v, v + d); }
    from = std::clamp (a, 0.0, 1.0);
    to = std::clamp (b, 0.0, 1.0);
    return true;
}
/** The name on the tab: the selected source when it is among the entries, else the first entry (the source that modulates the control, so the tab always says WHAT it is). */
inline const char* tabName (const ModInput& m) noexcept
{
    if (const ModEntry* e = m.selected())
        return e->name;
    return m.numMods > 0 ? m.mods[0].name : "";
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
/** Parameter binding handle shared by models and pages: opaque page id, resolved engine parameter index, and static parameter key. */
struct ParamHandle
{
    int id = 0;            // opaque page-local control id reported in Change::id
    int param = -1;        // resolved engine parameter index (-1 = unbound / virtual)
    const char* key = "";  // optional static parameter key

    constexpr bool bound() const noexcept { return param >= 0; }
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
/** Pure parameter/Q-Link/layout model contract: NO drawing, NO touch handling. Both native control models and legacy overlay Component widgets satisfy this contract. */
class ControlModel
{
public:
    ControlModel() = default;
    virtual ~ControlModel() = default;
    ControlModel (const ControlModel&) = delete;
    ControlModel& operator= (const ControlModel&) = delete;

    // ---- identity and parameter binding handle
    int id() const noexcept { return id_; }
    void setId (int id) noexcept { id_ = id; }
    int param() const noexcept { return param_; }
    void setParam (int p) noexcept { param_ = p; }

    // ---- native cell layout
    Rect rect() const noexcept { return rect_; }
    virtual void place (const Rect& r) { rect_ = r; }
    virtual Size minSize() const { return { 0, 0 }; }
    virtual bool sizeOk() const noexcept { const Size m = minSize(); return rect_.w >= m.w && rect_.h >= m.h; }

    // ---- state flags
    bool enabled() const noexcept { return enabled_; }
    virtual void setEnabled (bool e) { enabled_ = e; }
    bool visible() const noexcept { return visible_; }
    virtual void setVisible (bool v) { visible_ = v; }
    /** The INACTIVE look (a switched-off oscillator): still operable when enabled. */
    bool inactive() const noexcept { return inactive_; }
    virtual void setInactive (bool i) { inactive_ = i; }
    bool qFrame() const noexcept { return qFrame_; }
    int qIndex() const noexcept { return qIndex_; }
    /** The control belongs to the active Q-Link group (frame true) as member number `index` (1 .. 4, 0 = no badge). */
    virtual void setQ (bool frame, int index) { qFrame_ = frame; qIndex_ = frame ? index : 0; }

    // ---- value / range / step / text formatting
    virtual double value() const noexcept { return 0.0; }
    virtual double minValue() const noexcept { return 0.0; }
    virtual double maxValue() const noexcept { return 1.0; }
    virtual double stepValue() const noexcept { return 0.0; }
    virtual double defaultValue() const noexcept { return 0.0; }
    virtual void formatText (char* out, size_t cap) const { setText (out, cap, ""); }

    // ---- hardware Q-Link and wheel stepping
    /** The hardware knob assigned to the control turned by `delta` detents (a knob: normalised units; a list / number: whole steps). */
    virtual Change turn (double delta) { return enabled_ ? onTurn (delta) : Change::none(); }
    /** The mouse wheel over the control: `notches` (positive: away from the user, "up") turn it as many steps of its own (wheelUnit) as a hardware knob step would. */
    Change wheel (int notches) { return turn (static_cast<double> (notches) * wheelUnit()); }
    /** What one notch of the wheel is in the units of turn (): a knob and a bar one Q-Link step (1 / 128 of the range), a number / stepper one whole step. */
    virtual double wheelUnit() const noexcept { return 1.0; }
    virtual bool wheelOver (int x, int y, int notches, Change& out)
    {
        if (! enabled_ || ! rect_.contains (x, y))
            return false;
        out = wheel (notches);
        return true;
    }

    // ---- tree and QGroup membership
    virtual int collectModelLeaves (ControlModel** out, int n, int max) { if (n < max) out[n++] = this; return n; }
    virtual ControlModel* primaryModel() const { return nullptr; }

protected:
    virtual Change onTurn (double) { return Change::none(); }

    Rect rect_ = {};
    bool enabled_ = true, visible_ = true, qFrame_ = false, inactive_ = false;
    int qIndex_ = 0, id_ = 0, param_ = -1;
    const char* paramKey_ = "";
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
/** Pure parameter/Q-Link/layout model for a stepped number field. */
class NumberFieldModel : public ControlModel
{
public:
    enum class Format : uint8_t { IntSigned, Decimal2, Degrees, IntPlain };
    enum class Icon : uint8_t { None, Phase, Rand };
    struct State : ModInput
    {
        char label[24] = {};
        Icon icon = Icon::None;
        double value = 0.0, min = 0.0, max = 1.0, step = 1.0, defaultValue = 0.0;
        Format format = Format::IntSigned;
        bool notSet = false, readOnly = false;
        int pxPerStep = 8;
        char text[16] = {};   // owner-provided text override; empty uses formatValue
    };
    State state;
    Size maxBox { 0, 0 };

    NumberFieldModel() = default;
    NumberFieldModel (const char* label, Format f) { setText (state.label, sizeof state.label, label); state.format = f; }
    void setLabel (const char* s) { setText (state.label, sizeof state.label, s); }

    Size minSize() const override { return { 80, 56 }; }

    static void formatValue (const State& s, char* out, size_t cap = 16)
    {
        if (out == nullptr || cap == 0)
            return;
        if (s.notSet)
        {
            std::snprintf (out, cap, "-");
            return;
        }
        if (s.text[0] != '\0')
        {
            std::snprintf (out, cap, "%s", s.text);
            return;
        }
        const double v = s.value;
        switch (s.format)
        {
            case Format::IntSigned: { const long n = std::lround (v); if (n == 0) std::snprintf (out, cap, "0"); else std::snprintf (out, cap, "%+ld", n); break; }
            case Format::Decimal2: std::snprintf (out, cap, "%.2f", v == 0.0 ? 0.0 : v); break;
            case Format::Degrees: std::snprintf (out, cap, "%ld\xc2\xb0", std::lround (v)); break;
            case Format::IntPlain: std::snprintf (out, cap, "%ld", std::lround (v)); break;
        }
    }
    int numSteps() const noexcept { return std::max (0, static_cast<int> (std::lround ((state.max - state.min) / stepSize()))); }
    int indexOf (double v) const noexcept { return std::clamp (static_cast<int> (std::lround ((v - state.min) / stepSize())), 0, numSteps()); }

    double value() const noexcept override { return state.value; }
    double minValue() const noexcept override { return state.min; }
    double maxValue() const noexcept override { return state.max; }
    double stepValue() const noexcept override { return stepSize(); }
    double defaultValue() const noexcept override { return state.defaultValue; }
    void formatText (char* out, size_t cap) const override { formatValue (state, out, cap); }

protected:
    double stepSize() const noexcept { return state.step > 0.0 ? state.step : 1.0; }
    Change onTurn (double delta) override
    {
        const long d = std::lround (delta);
        if (state.readOnly || d == 0)
            return Change::none();
        const int idx = std::clamp (indexOf (state.value) + static_cast<int> (d), 0, numSteps());
        const double next = state.min + idx * stepSize();
        if (! state.notSet && idx == indexOf (state.value) && std::fabs (state.value - next) < 1.0e-9)
            return Change::none();
        return Change::indexed (id_, idx, next);
    }
};

}} // namespace synthyboy_mpc::ui
