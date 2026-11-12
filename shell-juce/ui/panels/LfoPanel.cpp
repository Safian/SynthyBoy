// SynthyBoy - UI: LFO panels - lfo1..lfo10 and the random LFOs rlfo1..rlfo4 (docs/ui-design/05-envelopes-lfos.md sections 2.2, 2.3).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// One panel per LFO: a shape display (the built-in shape, a phase marker, a drag-to-set-phase gesture), a shape stepper
// ([<] name [>]) and the controls: four primary knobs next to the visual (rate, fade in, delay, smooth time), every other key
// of the LFO behind "More" (sync mode, division, trigger mode, phase, smooth on/off, stereo, key follow, mono, direction, host).
// While lfoN.shape is Custom the display becomes the free-form shape editor (LfoShapeEditor: points, curve power, presets, undo; the
// point list is content, read and written through UiHost). Built-in shapes: the display is read-only apart from the phase.
// Only the LFO chosen on the tab chips (LfoTabsPanel) is shown; non-matching sync modes dim the rate / division / key-follow knobs.
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../Panel.h"
#include "../ParamHandle.h"
#include "LfoPanelProbe.h"
#include "LfoShapeEditor.h"

namespace synthyboy::ui
{
namespace
{
constexpr int kNumLfos = 10;
constexpr int kNumRandomLfos = 4;
constexpr int kDisplayPoints = 192;

/** Value of the built-in shape `shape` (the choice index of lfoN.shape) at phase x in 0..1, range -1..+1. */
float shapeValue (int shape, float x) noexcept
{
    constexpr float pi = 3.14159265358979f;
    switch (shape)
    {
        case 0:  return std::sin (2.0f * pi * x);                                   // Sine
        case 1:  return 1.0f - 4.0f * std::fabs (x - 0.5f);                           // Triangle
        case 2:  return 2.0f * x - 1.0f;                                              // Saw Up
        case 3:  return 1.0f - 2.0f * x;                                              // Saw Down
        case 4:  return x < 0.5f ? 1.0f : -1.0f;                                      // Square
        case 5:  return x < 0.25f ? 1.0f : -1.0f;                                     // Pulse 25 %
        case 6:  return std::floor (x * 4.0f) / 3.0f * 2.0f - 1.0f;                    // Steps Up
        case 7:  return 1.0f - std::floor (x * 4.0f) / 3.0f * 2.0f;                    // Steps Down
        case 8:                                                                        // Random Steps (fixed pattern for the display)
        {
            static constexpr float steps[8] = { 0.4f, -0.7f, 0.9f, -0.2f, -0.9f, 0.6f, 0.1f, -0.5f };
            return steps[static_cast<size_t> (juce::jlimit (0, 7, static_cast<int> (x * 8.0f)))];
        }
        case 9:  return 2.0f * (std::exp (5.0f * x) - 1.0f) / (std::exp (5.0f) - 1.0f) - 1.0f;           // Exp Up
        case 10: return 2.0f * (std::exp (5.0f * (1.0f - x)) - 1.0f) / (std::exp (5.0f) - 1.0f) - 1.0f; // Exp Down
        case 11: return 2.0f * std::sin (pi * x) - 1.0f;                               // Half Sine
        default: return 0.0f;                                                          // Custom: point list is content, not shown here
    }
}

/** Choice index of a stepped key, read from the parameter itself: inside onChange the raw atomic of the APVTS can still hold the
    previous value (it is updated by a listener that runs after ours), the normalised value is already the new one. */
int choiceIndexOf (const ParamHandle& handle)
{
    if (! handle.valid())
        return 0;
    const auto& info = handle.info();
    return juce::roundToInt (info.min + handle.normalised() * (info.max - info.min));
}

/** One cycle of a random LFO style for the display (style = choice index of rlfoN.style), range -1..+1. Computed once. */
std::array<float, kDisplayPoints> randomStyleCurve (int style)
{
    std::array<float, kDisplayPoints> out {};
    auto hash = [] (int i) { const float v = std::sin (static_cast<float> (i) * 12.9898f) * 43758.5453f; return 2.0f * (v - std::floor (v)) - 1.0f; };
    constexpr float pi = 3.14159265358979f;
    const auto n = static_cast<float> (kDisplayPoints);

    if (style == 3 || style == 4) // Lorenz / Rossler attractor: x component of a short integration, normalised
    {
        float x = 1.0f, y = 1.0f, z = style == 3 ? 1.0f : 0.1f;
        std::array<float, kDisplayPoints> raw {};
        float peak = 0.001f;
        for (size_t i = 0; i < raw.size(); ++i)
        {
            for (int k = 0; k < 4; ++k)
            {
                float dx, dy, dz;
                if (style == 3) { dx = 10.0f * (y - x); dy = x * (28.0f - z) - y; dz = x * y - 2.667f * z; }
                else            { dx = -y - z; dy = x + 0.2f * y; dz = 0.2f + z * (x - 5.7f); }
                const float dt = style == 3 ? 0.004f : 0.02f;
                x += dx * dt; y += dy * dt; z += dz * dt;
            }
            raw[i] = x;
            peak = std::fmax (peak, std::fabs (x));
        }
        for (size_t i = 0; i < raw.size(); ++i)
            out[i] = raw[i] / peak;
        return out;
    }

    const int points = style == 0 ? 6 : 8;
    for (size_t i = 0; i < out.size(); ++i)
    {
        const float pos = static_cast<float> (i) / n * static_cast<float> (points);
        const int i0 = static_cast<int> (pos);
        const float f = pos - static_cast<float> (i0);
        const float a = hash (i0 + 3), b = hash (i0 + 4);
        if (style == 1)      out[i] = a;                                                       // Sample and hold
        else if (style == 2) out[i] = a + (b - a) * (0.5f - 0.5f * std::cos (pi * f));          // Sine interpolate
        else                 out[i] = a + (b - a) * (f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f)); // Perlin: smootherstep
    }
    return out;
}
} // namespace

/** Shape display with the shape stepper. Dragging horizontally sets the start phase (lfoN.phase). */
class LfoShapeView final : public juce::Component
{
public:
    LfoShapeView (UiContext& context, const std::string& prefix, int lfoIndex, bool randomLfo)
        : ctx (context), random (randomLfo), number (lfoIndex),
          shapeHandle (context, prefix + (randomLfo ? ".style" : ".shape")),
          phaseHandle (context, prefix + ".phase"),
          syncHandle (context, prefix + ".sync"),
          rateHandle (context, prefix + ".rate"),
          divisionHandle (context, prefix + ".division")
    {
        setViewportIgnoreDragFlag (true); // it uses the drag itself: the panel does not scroll under it
        prev.setButtonText ("<");
        next.setButtonText (">");
        prev.onClick = [this] { stepShape (-1); };
        next.onClick = [this] { stepShape (1); };
        addAndMakeVisible (prev);
        addAndMakeVisible (next);
        if (! random)
        {
            editor = std::make_unique<LfoShapeEditor> (ctx, lfoIndex);
            addChildComponent (*editor);
        }

        shapeHandle.onChange = [this] { refresh(); };
        phaseHandle.onChange = [this] { repaint(); };
        syncHandle.onChange = [this] { repaint(); };
        rateHandle.onChange = [this] { repaint(); };
        divisionHandle.onChange = [this] { repaint(); };
        refresh();
        // WP-17: the playhead of the LFO (and the trace of a random LFO) follows the engine while the view is on screen
        ctx.live->bind (*this, {}, [this] { if (ctx.live->changed()) repaint (plot); });
    }

    ~LfoShapeView() override
    {
        if (phaseHandle.inGesture())
            phaseHandle.endGesture();
    }

    int shapeIndex() const { return choiceIndexOf (shapeHandle); }
    bool isCustom() const { return ! random && shapeIndex() == kLfoShapeCustom; }
    LfoShapeEditor* shapeEditor() noexcept { return editor.get(); }

    /** Height at `width`: the stepper bar and the read-only display, or the stepper bar and the shape editor. */
    int preferredHeight (int width) const
    {
        const auto& t = ctx.theme;
        if (isCustom())
            return 2 * t.gap() + t.touch() + t.gap() + editor->preferredHeight (width - 2 * t.gap());
        return t.touch() + t.ipx (150.0f);
    }

    /** Set by the panel: the view changed height (the shape became / stopped being Custom). */
    std::function<void()> onLayoutChanged;

    void resized() override
    {
        const auto& t = ctx.theme;
        auto r = getLocalBounds().reduced (t.gap());
        auto bar = r.removeFromTop (t.touch());
        prev.setBounds (bar.removeFromLeft (t.touch()));
        next.setBounds (bar.removeFromRight (t.touch()));
        nameArea = bar;
        r.removeFromTop (t.gap());
        const bool custom = isCustom();
        plot = custom ? juce::Rectangle<int>() : r;
        if (editor != nullptr)
            editor->setBounds (custom ? r : juce::Rectangle<int>());
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.theme;
        const auto& c = t.colours;
        const auto bounds = getLocalBounds().toFloat().reduced (t.px (2.0f));
        g.setColour (c.panelRaised);
        g.fillRoundedRectangle (bounds, t.px (6.0f));
        g.setColour (c.outline);
        g.drawRoundedRectangle (bounds, t.px (6.0f), 1.0f);

        g.setColour (c.text);
        g.setFont (t.valueFont());
        g.drawFittedText (shapeName(), nameArea, juce::Justification::centred, 1);

        const auto area = plot.toFloat().reduced (t.px (4.0f));
        if (isCustom() || area.getWidth() < 4.0f || area.getHeight() < 4.0f)
            return;

        // grid: centre line and quarters
        g.setColour (c.outline);
        g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
        for (int i = 1; i < 4; ++i)
            g.drawVerticalLine (juce::roundToInt (area.getX() + area.getWidth() * static_cast<float> (i) / 4.0f), area.getY(), area.getBottom());

        // the curve; the part before the start phase is drawn dimmer (the LFO starts at the phase marker)
        const float phase = phaseHandle.valid() ? phaseHandle.plain() / 360.0f : 0.0f;
        const int shape = shapeIndex();
        const auto* randomCurve = random ? &randomCurveCache : nullptr;
        juce::Path path;
        for (int i = 0; i <= kDisplayPoints; ++i)
        {
            const float x = static_cast<float> (i) / static_cast<float> (kDisplayPoints);
            float v;
            if (randomCurve != nullptr)
                v = (*randomCurve)[static_cast<size_t> (juce::jmin (i, kDisplayPoints - 1))];
            else
                v = shapeValue (shape, std::fmod (x + phase, 1.0f));
            const float px = area.getX() + x * area.getWidth();
            const float py = area.getCentreY() - v * area.getHeight() * 0.45f;
            if (i == 0)
                path.startNewSubPath (px, py);
            else
                path.lineTo (px, py);
        }
        g.setColour (c.accent);
        g.strokePath (path, juce::PathStrokeType (t.px (2.0f)));

        // phase marker
        const float markerX = area.getX() + phase * area.getWidth();
        g.setColour (c.warn);
        g.drawVerticalLine (juce::roundToInt (markerX), area.getY(), area.getBottom());

        paintLive (g, area, phase);

        // rate read-out
        g.setColour (c.textDim);
        g.setFont (t.labelFont());
        g.drawText (rateText(), plot.toFloat().reduced (t.px (6.0f), t.px (2.0f)), juce::Justification::bottomRight, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! phaseHandle.valid() || ! plot.contains (e.getPosition()))
            return;
        phaseHandle.beginGesture();
        setPhaseFrom (e.position.x);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (phaseHandle.inGesture())
            setPhaseFrom (e.position.x);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (phaseHandle.inGesture())
            phaseHandle.endGesture();
    }

    juce::TextButton prev, next;

    /** WP-17: where the engine is in the cycle of this LFO (a playhead and a dot on the curve), or the last seconds of a random LFO (a trace). Public for the tests:
        the playhead position in the plot as a fraction of its width, -1 when the engine plays no voice with this LFO. */
    float playheadFraction() const
    {
        if (random || ! ctx.live->available() || number < 1 || number > LiveValues::kNumLfos)
            return -1.0f;
        const float engine = ctx.live->values().lfoPhase[number - 1];
        if (engine < 0.0f)
            return -1.0f;
        const float start = phaseHandle.valid() ? phaseHandle.plain() / 360.0f : 0.0f;
        return std::fmod (engine - start + 2.0f, 1.0f);   // the plot starts at the phase marker
    }
    juce::Rectangle<int> plotBounds() const noexcept { return plot; }

private:
    void paintLive (juce::Graphics& g, juce::Rectangle<float> area, float startPhase)
    {
        const auto& t = ctx.theme;
        const auto& c = t.colours;
        if (! ctx.live->available())
            return;
        if (random)
        {
            // the trace of the last seconds (oldest on the left), the newest value as a dot at the right edge
            const auto history = ctx.live->randomHistory (number);
            juce::Path trace;
            for (size_t i = 0; i < history.size(); ++i)
            {
                const float x = area.getX() + area.getWidth() * static_cast<float> (i) / static_cast<float> (history.size() - 1);
                const float y = area.getCentreY() - juce::jlimit (-1.0f, 1.0f, history[i]) * area.getHeight() * 0.45f;
                if (i == 0) trace.startNewSubPath (x, y); else trace.lineTo (x, y);
            }
            g.setColour (c.mod.withAlpha (0.85f));
            g.strokePath (trace, juce::PathStrokeType (t.px (1.5f)));
            const float r = t.px (4.0f);
            g.fillEllipse (area.getRight() - r, area.getCentreY() - juce::jlimit (-1.0f, 1.0f, history.back()) * area.getHeight() * 0.45f - r, 2.0f * r, 2.0f * r);
            return;
        }
        const float fraction = playheadFraction();
        if (fraction < 0.0f)
            return;
        (void) startPhase;
        const float x = area.getX() + fraction * area.getWidth();
        g.setColour (c.text.withAlpha (0.8f));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
        const float value = ctx.live->source (liveSourceChoice (number));
        const float r = t.px (4.0f);
        g.setColour (c.mod);
        g.fillEllipse (x - r, area.getCentreY() - juce::jlimit (-1.0f, 1.0f, value) * area.getHeight() * 0.45f - r, 2.0f * r, 2.0f * r);
    }

    /** Modulation source choice value of LFO n: lfo1 .. lfo8 are 7 .. 14, lfo9 and lfo10 33 and 34. */
    static int liveSourceChoice (int n) noexcept { return n <= 8 ? 6 + n : 24 + n; }

    void refresh()
    {
        if (random)
            randomCurveCache = randomStyleCurve (shapeIndex());
        if (editor != nullptr)
        {
            const bool custom = isCustom();
            if (! custom)
                lastBuiltIn = juce::jlimit (0, kNumLfoShapes - 1, shapeIndex());
            editor->setActive (custom, lastBuiltIn); // a Custom shape without a point list starts as the shape that was showing
            editor->setVisible (custom);
            if (custom != wasCustom)
            {
                wasCustom = custom;
                resized();
                if (onLayoutChanged)
                    onLayoutChanged();
            }
        }
        repaint();
    }

    void stepShape (int direction)
    {
        if (! shapeHandle.valid())
            return;
        const int count = shapeHandle.info().numChoices;
        const int now = shapeIndex();
        shapeHandle.setPlainComplete (static_cast<float> ((now + direction + count) % count));
    }

    void setPhaseFrom (float x)
    {
        const auto area = plot.toFloat();
        if (area.getWidth() <= 0.0f)
            return;
        phaseHandle.setNormalised (juce::jlimit (0.0f, 1.0f, (x - area.getX()) / area.getWidth()));
    }

    juce::String shapeName() const
    {
        if (! shapeHandle.valid())
            return "-";
        return juce::String (choiceName (shapeHandle.info(), static_cast<size_t> (juce::jlimit (0, shapeHandle.info().numChoices - 1, shapeIndex()))));
    }

    juce::String rateText() const
    {
        if (! syncHandle.valid())
            return {};
        const int mode = juce::roundToInt (syncHandle.plain());
        if (mode == 0 && rateHandle.valid())
            return juce::String (rateHandle.plain(), 2) + " Hz";
        if (mode == 4)
            return "Key follow";
        if (divisionHandle.valid())
        {
            const int d = juce::roundToInt (divisionHandle.plain());
            if (d == 0)
                return "frozen";
            return juce::String (choiceName (divisionHandle.info(), static_cast<size_t> (d))) + (mode == 2 ? " dotted" : (mode == 3 ? " triplet" : ""));
        }
        return {};
    }

    UiContext& ctx;
    bool random;
    int number;
    ParamHandle shapeHandle, phaseHandle, syncHandle, rateHandle, divisionHandle;
    juce::Rectangle<int> nameArea, plot;
    std::array<float, kDisplayPoints> randomCurveCache {};
    std::unique_ptr<LfoShapeEditor> editor;
    int lastBuiltIn = 0;
    bool wasCustom = false;
};

namespace
{
class LfoPanelBase : public Panel
{
public:
    LfoPanelBase (UiContext& context, int index, bool randomLfo)
        : Panel (context), idx (index), random (randomLfo),
          syncHandle (context, std::string (randomLfo ? "rlfo" : "lfo") + std::to_string (index) + ".sync") {}

    /** Only the LFO chosen on the tab chips takes part in the layout. */
    bool hiddenInLayout() const override { return ctx.view->lfoTab != (random ? 9 + idx : idx - 1); }

    LfoShapeView* shapeView() noexcept { return visual.get(); }

    juce::String title() const override
    {
        return (random ? "Random LFO " : "LFO ") + juce::String (idx);
    }

    std::string modulePrefix() const override { return std::string (random ? "rlfo" : "lfo") + std::to_string (idx); }

    std::vector<std::string> keys() const override
    {
        const std::string p = (random ? "rlfo" : "lfo") + std::to_string (idx) + ".";
        if (random)
            return { p + "rate", p + "stereo", p + "sync_type", p + "keytrack_transpose",   // primary: rate, stereo, trigger, key follow
                     p + "style", p + "sync", p + "division", p + "keytrack_tune" };
        return { p + "rate", p + "fade_in", p + "delay", p + "smooth_time",                    // primary
                 p + "shape", p + "sync", p + "division", p + "sync_type", p + "phase", p + "smooth", p + "stereo",
                 p + "keytrack_transpose", p + "keytrack_tune", p + "mono", p + "direction", p + "host", p + "swing" };
    }

    void build() override
    {
        visual = std::make_unique<LfoShapeView> (ctx, std::string (random ? "rlfo" : "lfo") + std::to_string (idx), idx, random);
        visual->onLayoutChanged = [this] { if (onHeightChanged) onHeightChanged(); };
        addAndMakeVisible (*visual);
        const auto all = keys();
        for (size_t i = 0; i < all.size(); ++i)
        {
            if (i < 4)
                addControl (all[i]);
            else
                addMoreControl (all[i]);
        }
        syncHandle.onChange = [this] { updateDimming(); };
        updateDimming();
    }

    /** Sync mode decides which rate controls apply (design 05 section 4): Free = rate in Hz, Tempo / dotted / triplet = division,
        Key follow = the key-follow offsets (rate and division are off). A control the engine lacks stays dimmed anyway. */
    void updateDimming()
    {
        const int mode = choiceIndexOf (syncHandle);
        for (const auto& c : controls())
        {
            const auto& key = c->key();
            const auto endsWith = [&key] (const char* suffix) { const std::string sfx (suffix); return key.size() >= sfx.size() && key.compare (key.size() - sfx.size(), sfx.size(), sfx) == 0; };
            bool applies = true;
            if (endsWith (".rate"))
                applies = mode == 0;
            else if (endsWith (".division"))
                applies = mode >= 1 && mode <= 3;
            else if (endsWith (".keytrack_transpose") || endsWith (".keytrack_tune"))
                applies = mode == 4;
            c->setAlpha (c->isAvailable() && applies ? 1.0f : 0.4f);
        }
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        return headerHeight() + 2 * t.gap() + visualHeight (area.getWidth()) + t.gap() + const_cast<LfoPanelBase*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        if (visual != nullptr)
            visual->setBounds (area.removeFromTop (visualHeight (area.getWidth())));
        area.removeFromTop (ctx.theme.gap());
        layoutControls (area, false);
    }

private:
    int visualHeight (int width) const { return visual != nullptr ? visual->preferredHeight (width) : ctx.theme.touch() + ctx.theme.ipx (150.0f); }

    int idx;
    bool random;
    ParamHandle syncHandle;
    std::unique_ptr<LfoShapeView> visual;

public:
    LfoShapeView* display() noexcept { return visual.get(); }
    bool isRandom() const noexcept { return random; }
};

template <int N>
class LfoPanel final : public LfoPanelBase
{
public:
    explicit LfoPanel (UiContext& context) : LfoPanelBase (context, N, false) {}
};

template <int N>
class RandomLfoPanel final : public LfoPanelBase
{
public:
    explicit RandomLfoPanel (UiContext& context) : LfoPanelBase (context, N, true) {}
};

static_assert (kNumLfos == 10 && kNumRandomLfos == 4, "register one panel per LFO below");

using Lfo1Panel = LfoPanel<1>;
using Lfo2Panel = LfoPanel<2>;
using Lfo3Panel = LfoPanel<3>;
using Lfo4Panel = LfoPanel<4>;
using Lfo5Panel = LfoPanel<5>;
using Lfo6Panel = LfoPanel<6>;
using Lfo7Panel = LfoPanel<7>;
using Lfo8Panel = LfoPanel<8>;
using Lfo9Panel = LfoPanel<9>;
using Lfo10Panel = LfoPanel<10>;
using RandomLfo1Panel = RandomLfoPanel<1>;
using RandomLfo2Panel = RandomLfoPanel<2>;
using RandomLfo3Panel = RandomLfoPanel<3>;
using RandomLfo4Panel = RandomLfoPanel<4>;
} // namespace

SYNTHYBOY_REGISTER_PANEL (Lfo1Panel, Page::lfo, 10)
SYNTHYBOY_REGISTER_PANEL (Lfo2Panel, Page::lfo, 20)
SYNTHYBOY_REGISTER_PANEL (Lfo3Panel, Page::lfo, 30)
SYNTHYBOY_REGISTER_PANEL (Lfo4Panel, Page::lfo, 40)
SYNTHYBOY_REGISTER_PANEL (Lfo5Panel, Page::lfo, 50)
SYNTHYBOY_REGISTER_PANEL (Lfo6Panel, Page::lfo, 60)
SYNTHYBOY_REGISTER_PANEL (Lfo7Panel, Page::lfo, 70)
SYNTHYBOY_REGISTER_PANEL (Lfo8Panel, Page::lfo, 80)
SYNTHYBOY_REGISTER_PANEL (Lfo9Panel, Page::lfo, 90)
SYNTHYBOY_REGISTER_PANEL (Lfo10Panel, Page::lfo, 100)
SYNTHYBOY_REGISTER_PANEL (RandomLfo1Panel, Page::lfo, 110)
SYNTHYBOY_REGISTER_PANEL (RandomLfo2Panel, Page::lfo, 120)
SYNTHYBOY_REGISTER_PANEL (RandomLfo3Panel, Page::lfo, 130)
SYNTHYBOY_REGISTER_PANEL (RandomLfo4Panel, Page::lfo, 140)

bool probeLfoPanel (Panel& panel, LfoLiveProbe& out)
{
    auto* lfo = dynamic_cast<LfoPanelBase*> (&panel);
    if (lfo == nullptr || lfo->display() == nullptr)
        return false;
    out.random = lfo->isRandom();
    out.playhead = lfo->display()->playheadFraction();
    out.plot = lfo->display()->plotBounds();
    out.bound = LiveBus::shown (*lfo->display());
    return true;
}
} // namespace synthyboy::ui
