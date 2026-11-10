// SynthyBoy - UI: the drawn wavetable position remap graph (see RemapEditor.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "RemapEditor.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

#include "synthyboy/params.h"

namespace synthyboy::ui
{
namespace
{
constexpr size_t kMinPoints = 2;
constexpr float kMaxPower = static_cast<float> (kMaxPointPower);
constexpr int kPencilColumns = 32;

const char* const kPresetNames[RemapEditor::kNumPresets] = { "Linear", "Reverse", "Ease in", "Ease out", "S curve", "Steps 4", "Steps 8", "Jump" };
const char* const kSpectralPresetNames[RemapEditor::kNumSpectralPresets] = { "Flat", "Low pass", "High pass", "Band pass", "Notch", "Tilt down", "Comb", "Formants" };

shapes::ShapeData toShapeData (const ContentRemap& r)
{
    shapes::ShapeData d;
    d.numPoints = std::min<size_t> (r.points.size(), shapes::ShapeData::kMaxPoints);
    for (size_t i = 0; i < d.numPoints; ++i)
    {
        d.points[i].x = r.points[i].x;
        d.points[i].y = r.points[i].y;
        d.points[i].curve = r.points[i].power;
        d.points[i].smooth = false;
    }
    d.sanitize();
    return d;
}

bool sameRemap (const ContentRemap& a, const ContentRemap& b)
{
    return a.osc == b.osc && a.slot == b.slot && a.points.size() == b.points.size() && std::equal (a.points.begin(), a.points.end(), b.points.begin());
}

/** n + 1 samples of f over x in 0..1. */
void sampled (ContentRemap& r, int n, const std::function<float (float)>& f)
{
    r.points.clear();
    for (int i = 0; i <= n; ++i)
    {
        const float x = static_cast<float> (i) / static_cast<float> (n);
        r.points.emplace_back (x, juce::jlimit (0.0f, 1.0f, f (x)), 0.0f);
    }
}

/** A flat step per column (the two points of a step share their y). */
void stepped (ContentRemap& r, int n)
{
    r.points.clear();
    for (int i = 0; i < n; ++i)
    {
        const float y = n > 1 ? static_cast<float> (i) / static_cast<float> (n - 1) : 0.0f;
        r.points.emplace_back (static_cast<float> (i) / static_cast<float> (n), y, 0.0f);
        r.points.emplace_back (static_cast<float> (i + 1) / static_cast<float> (n), y, 0.0f);
    }
}
} // namespace

const char* RemapEditor::spectralPresetName (int preset) noexcept
{
    return preset >= 0 && preset < kNumSpectralPresets ? kSpectralPresetNames[preset] : "";
}

const char* RemapEditor::presetName (int preset) noexcept
{
    return preset >= 0 && preset < kNumPresets ? kPresetNames[preset] : "";
}

RemapEditor::RemapEditor (UiContext& context, int oscIndex)
    : ctx (context), oscN (oscIndex), slotHandle (std::make_unique<ParamHandle> (context, "osc" + std::to_string (oscIndex) + ".remap.slot"))
{
    init();
}

RemapEditor::RemapEditor (UiContext& context, ModCurveMode, int modSlot)
    : ctx (context), oscN (0), modMode (true), modSlotNumber (juce::jlimit (1, kMaxContentModCurves, modSlot)), maxPoints (static_cast<size_t> (kMaxModCurvePoints))
{
    init();
}

RemapEditor::RemapEditor (UiContext& context, SpectralFilterMode, int oscIndex)
    : ctx (context), oscN (oscIndex), specMode (true)
{
    init();
}

void RemapEditor::init()
{
    current.osc = oscN;
    current.slot = modMode ? modSlotNumber : 1;
    setIdentityPoints();
    buttons = { &pencilBtn, &addBtn, &deleteBtn, &undoBtn, &redoBtn, &presetBtn, &resetBtn };
    for (auto* b : buttons)
        addAndMakeVisible (*b);
    pencilBtn.onClick = [this] { setPencil (! pencil); };
    addBtn.onClick = [this] { addPointAt (selected >= 0 && selected + 1 < static_cast<int> (current.points.size())
                                              ? 0.5f * (current.points[static_cast<size_t> (selected)].x + current.points[static_cast<size_t> (selected) + 1].x)
                                              : 0.5f); };
    deleteBtn.onClick = [this] { deleteSelected(); };
    undoBtn.onClick = [this] { undo(); };
    redoBtn.onClick = [this] { redo(); };
    resetBtn.onClick = [this] { resetToIdentity(); };
    presetBtn.onClick = [this]
    {
        juce::PopupMenu menu;
        for (int i = 0; i < (specMode ? kNumSpectralPresets : kNumPresets); ++i)
            menu.addItem (i + 1, specMode ? spectralPresetName (i) : presetName (i));
        menu.showMenuAsync (popupOptions (&presetBtn),
                            [safe = juce::Component::SafePointer<RemapEditor> (this)] (int result)
                            {
                                if (safe != nullptr && result > 0)
                                    safe->applyPreset (result - 1);
                            });
    };
    if (slotHandle != nullptr)
        slotHandle->onChange = [this] { reloadFromHost(); };
    reloadFromHost();
    updateButtons();
    startTimerHz (30);
}

RemapEditor::~RemapEditor()
{
    stopTimer();
    if (dirty)
        commitNow();
}

int RemapEditor::slot() const
{
    if (modMode)
        return modSlotNumber;
    if (specMode)
        return 1;
    return slotHandle->valid() ? juce::jlimit (1, kNumRemapSlots, juce::roundToInt (slotHandle->plain()) + 1) : 1;
}

void RemapEditor::setModSlot (int modSlot)
{
    if (! modMode)
        return;
    modSlotNumber = juce::jlimit (1, kMaxContentModCurves, modSlot);
    reloadFromHost();
}

juce::TextButton* RemapEditor::button (const juce::String& text)
{
    for (auto* b : buttons)
        if (b->getButtonText() == text)
            return b;
    return nullptr;
}

void RemapEditor::setIdentityPoints()
{
    current.points.clear();
    current.points.emplace_back (0.0f, specMode ? 0.5f : 0.0f, 0.0f);
    current.points.emplace_back (1.0f, specMode ? 0.5f : 1.0f, 0.0f);
    cacheValid = false;
}

shapes::ShapeData RemapEditor::shapeData() const
{
    if (! cacheValid)
    {
        cache = toShapeData (current);
        cacheValid = true;
    }
    return cache;
}

float RemapEditor::evaluateAt (float x) const
{
    if (! cacheValid)
        shapeData();
    return cache.evaluate (juce::jlimit (0.0f, 1.0f, x));
}

// ---- host ----------------------------------------------------------------------------------------------------------------------

void RemapEditor::reloadFromHost()
{
    if (drag != Drag::none)
        return;
    const int wanted = slot();
    const auto content = ctx.host.content();
    ContentRemap next;
    next.osc = oscN;
    next.slot = wanted;
    bool found = false;
    if (modMode)
    {
        const auto mit = std::find_if (content.modCurves.begin(), content.modCurves.end(), [wanted] (const ContentModCurve& m) { return m.slot == wanted; });
        if (mit != content.modCurves.end() && mit->points.size() >= kMinPoints)
        {
            next.points = mit->points;
            found = true;
        }
    }
    else
    {
        auto& curves = specMode ? content.spectralFilters : content.remaps;
        const auto it = std::find_if (curves.begin(), curves.end(), [this, wanted] (const ContentRemap& r) { return r.osc == oscN && r.slot == wanted; });
        if (it != curves.end() && it->points.size() >= kMinPoints)
        {
            next = *it;
            found = true;
        }
    }
    if (! found)
    {
        next.points.emplace_back (0.0f, specMode ? 0.5f : 0.0f, 0.0f);
        next.points.emplace_back (1.0f, specMode ? 0.5f : 1.0f, 0.0f);
    }
    if (wanted == loadedSlot && found == stored && sameRemap (next, current))
        return;
    if (wanted != loadedSlot && dirty)
        commitNow(); // never lose an edit of the slot that was shown
    current = next;
    stored = found;
    loadedSlot = wanted;
    undoStack.clear(); // changed behind our back (slot, preset load): the old history no longer applies
    redoStack.clear();
    selected = -1;
    cacheValid = false;
    updateButtons();
    repaint();
}

void RemapEditor::commitNow()
{
    dirty = false;
    auto content = ctx.host.content();
    if (modMode)
    {
        const auto mit = std::find_if (content.modCurves.begin(), content.modCurves.end(), [this] (const ContentModCurve& m) { return m.slot == current.slot; });
        if (! stored)
        {
            if (mit != content.modCurves.end())
                content.modCurves.erase (mit);
        }
        else if (mit != content.modCurves.end())
            mit->points = current.points;
        else
            content.modCurves.push_back ({ current.slot, current.points });
        ctx.host.setContent (content);
        return;
    }
    auto& curves = specMode ? content.spectralFilters : content.remaps;
    const auto it = std::find_if (curves.begin(), curves.end(),
                                  [this] (const ContentRemap& r) { return r.osc == current.osc && r.slot == current.slot; });
    if (! stored)
    {
        if (it != curves.end())
            curves.erase (it);
    }
    else if (it != curves.end())
        *it = current;
    else
        curves.push_back (current);
    if (specMode)
        std::sort (curves.begin(), curves.end(), [] (const ContentRemap& a, const ContentRemap& b) { return a.osc < b.osc; });
    ctx.host.setContent (content);
}

void RemapEditor::timerCallback()
{
    if (dirty)
    {
        commitNow();
        return;
    }
    if (drag == Drag::none && ++idleTicks >= 8)
    {
        idleTicks = 0;
        reloadFromHost();
    }
}

// ---- editing -------------------------------------------------------------------------------------------------------------------

void RemapEditor::pushUndo()
{
    if (drag != Drag::none) // a gesture records its start state once, on mouse up
        return;
    undoStack.push_back (current);
    if (undoStack.size() > 100)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void RemapEditor::touched()
{
    cacheValid = false;
    stored = true;
    dirty = true;
    gestureChanged = true;
    updateButtons();
    repaint();
}

void RemapEditor::setPencil (bool on)
{
    pencil = on;
    updateButtons();
    repaint();
}

void RemapEditor::undo()
{
    if (undoStack.empty())
        return;
    redoStack.push_back (current);
    current = undoStack.back();
    undoStack.pop_back();
    selected = -1;
    touched();
}

void RemapEditor::redo()
{
    if (redoStack.empty())
        return;
    undoStack.push_back (current);
    current = redoStack.back();
    redoStack.pop_back();
    selected = -1;
    touched();
}

bool RemapEditor::addPointAt (float x)
{
    if (current.points.size() >= maxPoints)
        return false;
    x = juce::jlimit (0.0f, 1.0f, x);
    size_t at = 1;
    while (at + 1 < current.points.size() && current.points[at].x <= x)
        ++at;
    const float y = evaluateAt (x); // on the line: the shape does not change
    pushUndo();
    current.points.insert (current.points.begin() + static_cast<std::ptrdiff_t> (at), ContentPoint (x, y, 0.0f));
    selected = static_cast<int> (at);
    touched();
    return true;
}

bool RemapEditor::deletePoint (int index)
{
    if (index <= 0 || index + 1 >= static_cast<int> (current.points.size()) || current.points.size() <= kMinPoints)
        return false; // the end points stay
    pushUndo();
    current.points.erase (current.points.begin() + index);
    selected = -1;
    touched();
    return true;
}

bool RemapEditor::movePoint (int index, float x, float y)
{
    if (index < 0 || index >= static_cast<int> (current.points.size()))
        return false;
    const auto i = static_cast<size_t> (index);
    const bool endPoint = index == 0 || index + 1 == static_cast<int> (current.points.size());
    y = juce::jlimit (0.0f, 1.0f, y);
    if (endPoint)
        x = current.points[i].x;
    else
        x = juce::jlimit (current.points[i - 1].x, current.points[i + 1].x, x);
    if (juce::exactlyEqual (x, current.points[i].x) && juce::exactlyEqual (y, current.points[i].y))
        return false;
    pushUndo();
    current.points[i].x = x;
    current.points[i].y = y;
    selected = index;
    touched();
    return true;
}

bool RemapEditor::setSegmentPower (int segment, float power)
{
    if (segment < 0 || segment + 1 >= static_cast<int> (current.points.size()))
        return false;
    power = juce::jlimit (-kMaxPower, kMaxPower, power);
    if (juce::exactlyEqual (current.points[static_cast<size_t> (segment)].power, power))
        return false;
    pushUndo();
    current.points[static_cast<size_t> (segment)].power = power;
    touched();
    return true;
}

bool RemapEditor::paintStroke (const std::vector<juce::Point<float>>& stroke)
{
    if (stroke.empty())
        return false;
    // 33 values at the column borders, started from the curve as it is; the stroke sets the values it passes
    std::vector<float> value (kPencilColumns + 1);
    for (int i = 0; i <= kPencilColumns; ++i)
        value[static_cast<size_t> (i)] = evaluateAt (static_cast<float> (i) / static_cast<float> (kPencilColumns));
    juce::Point<float> last = stroke.front();
    const auto setAt = [&value] (float x, float y)
    {
        const int column = juce::jlimit (0, kPencilColumns, juce::roundToInt (x * static_cast<float> (kPencilColumns)));
        value[static_cast<size_t> (column)] = juce::jlimit (0.0f, 1.0f, y);
    };
    setAt (last.x, last.y);
    for (size_t k = 1; k < stroke.size(); ++k)
    {
        const auto p = stroke[k];
        const int a = juce::jlimit (0, kPencilColumns, juce::roundToInt (last.x * static_cast<float> (kPencilColumns)));
        const int b = juce::jlimit (0, kPencilColumns, juce::roundToInt (p.x * static_cast<float> (kPencilColumns)));
        const int lo = std::min (a, b), hi = std::max (a, b);
        for (int c = lo; c <= hi; ++c)
        {
            const float t = hi > lo ? static_cast<float> (c - a) / static_cast<float> (b - a) : 1.0f;
            value[static_cast<size_t> (c)] = juce::jlimit (0.0f, 1.0f, last.y + (p.y - last.y) * (b != a ? t : 1.0f));
        }
        last = p;
    }
    pushUndo();
    current.points.clear();
    for (int i = 0; i <= kPencilColumns; ++i)
        current.points.emplace_back (static_cast<float> (i) / static_cast<float> (kPencilColumns), value[static_cast<size_t> (i)], 0.0f);
    selected = -1;
    touched();
    return true;
}

void RemapEditor::applyPreset (int preset)
{
    if (preset < 0 || preset >= (specMode ? kNumSpectralPresets : kNumPresets))
        return;
    pushUndo();
    if (specMode)
    {
        // gain curves over the log frequency axis (0.5 = unity)
        switch (preset)
        {
            case 0: current.points.clear(); current.points.emplace_back (0.0f, 0.5f, 0.0f); current.points.emplace_back (1.0f, 0.5f, 0.0f); break;
            case 1: sampled (current, 32, [] (float x) { return x < 0.5f ? 0.5f : 0.5f * std::max (0.0f, 1.0f - (x - 0.5f) / 0.15f); }); break;
            case 2: sampled (current, 32, [] (float x) { return x > 0.5f ? 0.5f : 0.5f * std::max (0.0f, 1.0f - (0.5f - x) / 0.15f); }); break;
            case 3: sampled (current, 32, [] (float x) { return 0.5f * std::max (0.0f, 1.0f - std::fabs (x - 0.5f) / 0.12f); }); break;
            case 4: sampled (current, 32, [] (float x) { return 0.5f * std::min (1.0f, std::fabs (x - 0.5f) / 0.1f); }); break;
            case 5: sampled (current, 16, [] (float x) { return 0.5f - 0.45f * x; }); break;
            case 6: sampled (current, 32, [] (float x) { return 0.5f * (0.15f + 0.85f * (0.5f + 0.5f * std::cos (x * 12.566371f * 2.0f))); }); break;
            default: sampled (current, 48, [] (float x)
                              {
                                  float g = 0.04f;
                                  for (float centre : { 0.42f, 0.55f, 0.66f, 0.76f })
                                      g = std::max (g, 0.5f * std::exp (-(x - centre) * (x - centre) / (2.0f * 0.025f * 0.025f)));
                                  return g;
                              }); break;
        }
        selected = -1;
        touched();
        return;
    }
    switch (preset)
    {
        case 0: current.points.clear(); current.points.emplace_back (0.0f, 0.0f, 0.0f); current.points.emplace_back (1.0f, 1.0f, 0.0f); break;
        case 1: current.points.clear(); current.points.emplace_back (0.0f, 1.0f, 0.0f); current.points.emplace_back (1.0f, 0.0f, 0.0f); break;
        case 2: sampled (current, 16, [] (float x) { return x * x; }); break;
        case 3: sampled (current, 16, [] (float x) { return 1.0f - (1.0f - x) * (1.0f - x); }); break;
        case 4: sampled (current, 16, [] (float x) { return x * x * (3.0f - 2.0f * x); }); break;
        case 5: stepped (current, 4); break;
        case 6: stepped (current, 8); break;
        default:
            current.points.clear();
            current.points.emplace_back (0.0f, 0.0f, 0.0f);
            current.points.emplace_back (0.5f, 0.0f, 0.0f);
            current.points.emplace_back (0.5f, 1.0f, 0.0f);
            current.points.emplace_back (1.0f, 1.0f, 0.0f);
            break;
    }
    selected = -1;
    touched();
}

void RemapEditor::resetToIdentity()
{
    const float first = specMode ? 0.5f : 0.0f, last = specMode ? 0.5f : 1.0f;
    if (! stored && current.points.size() == 2 && std::abs (current.points[0].y - first) < 1.0e-6f && std::abs (current.points[1].y - last) < 1.0e-6f)
        return;
    pushUndo();
    setIdentityPoints();
    selected = -1;
    touched();
    stored = false; // the identity needs no entry: it is removed from the content
    updateButtons();
}

// ---- layout and geometry ---------------------------------------------------------------------------------------------------------

int RemapEditor::barHeight (int width, bool layout)
{
    const auto& t = ctx.theme;
    const auto font = t.valueFont();
    int x = 0, y = 0;
    for (auto* b : buttons)
    {
        const int w = juce::jmax (t.touch(), juce::roundToInt (Theme::textWidth (font, b->getButtonText())) + 2 * t.gap());
        if (x > 0 && x + w > width)
        {
            x = 0;
            y += t.touch() + t.gap();
        }
        if (layout)
            b->setBounds (x, y, w, t.touch());
        x += w + t.gap();
    }
    return y + t.touch();
}

int RemapEditor::preferredHeight (int width) const
{
    const auto& t = ctx.theme;
    return const_cast<RemapEditor*> (this)->barHeight (width, false) + t.gap() + t.ipx (170.0f);
}

void RemapEditor::resized()
{
    const auto& t = ctx.theme;
    const int bar = barHeight (getWidth(), true);
    plot = getLocalBounds().toFloat().withTrimmedTop (static_cast<float> (bar + t.gap())).reduced (static_cast<float> (t.gap()));
    cacheValid = false;
}

float RemapEditor::toX (float x) const noexcept { return plot.getX() + x * plot.getWidth(); }
float RemapEditor::toY (float y) const noexcept { return plot.getBottom() - y * plot.getHeight(); }
float RemapEditor::fromX (float px) const noexcept { return plot.getWidth() > 0.0f ? juce::jlimit (0.0f, 1.0f, (px - plot.getX()) / plot.getWidth()) : 0.0f; }
float RemapEditor::fromY (float py) const noexcept { return plot.getHeight() > 0.0f ? juce::jlimit (0.0f, 1.0f, (plot.getBottom() - py) / plot.getHeight()) : 0.0f; }

juce::Point<float> RemapEditor::pointPosition (int index) const
{
    const auto& p = current.points[static_cast<size_t> (index)];
    return { toX (p.x), toY (p.y) };
}

juce::Point<float> RemapEditor::handlePosition (int segment) const
{
    const auto& a = current.points[static_cast<size_t> (segment)];
    const auto& b = current.points[static_cast<size_t> (segment) + 1];
    const float xm = 0.5f * (a.x + b.x);
    return { toX (xm), toY (evaluateAt (xm)) };
}

int RemapEditor::hitPoint (juce::Point<float> p) const
{
    const float radius = static_cast<float> (ctx.theme.touch()) * 0.5f;
    int best = -1;
    float bestD = radius;
    for (int i = 0; i < static_cast<int> (current.points.size()); ++i)
    {
        const float d = pointPosition (i).getDistanceFrom (p);
        if (d <= bestD)
        {
            bestD = d;
            best = i;
        }
    }
    return best;
}

int RemapEditor::hitHandle (juce::Point<float> p) const
{
    const float radius = static_cast<float> (ctx.theme.touch()) * 0.5f;
    for (int i = 0; i + 1 < static_cast<int> (current.points.size()); ++i)
    {
        const auto& a = current.points[static_cast<size_t> (i)];
        const auto& b = current.points[static_cast<size_t> (i) + 1];
        if ((b.x - a.x) * plot.getWidth() < radius)
            continue; // too narrow to carry a handle
        if (handlePosition (i).getDistanceFrom (p) <= radius)
            return i;
    }
    return -1;
}

void RemapEditor::updateButtons()
{
    pencilBtn.setToggleState (pencil, juce::dontSendNotification);
    undoBtn.setEnabled (canUndo());
    redoBtn.setEnabled (canRedo());
    deleteBtn.setEnabled (selected > 0 && selected + 1 < static_cast<int> (current.points.size()) && current.points.size() > kMinPoints);
    addBtn.setEnabled (current.points.size() < maxPoints);
    resetBtn.setEnabled (stored);
}

// ---- painting --------------------------------------------------------------------------------------------------------------------

void RemapEditor::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    const auto& c = t.colours;
    const auto frame = plot.expanded (static_cast<float> (t.gap()) * 0.5f);
    g.setColour (c.panelRaised);
    g.fillRoundedRectangle (frame, t.px (6.0f));
    g.setColour (c.outline);
    g.drawRoundedRectangle (frame, t.px (6.0f), 1.0f);
    if (plot.getWidth() < 8.0f || plot.getHeight() < 8.0f || current.points.size() < kMinPoints)
        return;

    g.setColour (c.outline.withAlpha (0.45f));
    for (int i = 1; i < 4; ++i)
    {
        const float f = static_cast<float> (i) / 4.0f;
        g.drawVerticalLine (juce::roundToInt (toX (f)), plot.getY(), plot.getBottom());
        g.drawHorizontalLine (juce::roundToInt (toY (f)), plot.getX(), plot.getRight());
    }
    // the identity: what the table does without a remap (the flat unity line of the spectral filter)
    g.setColour (c.textDim.withAlpha (0.5f));
    const float dashes[] = { t.px (4.0f), t.px (4.0f) };
    g.drawDashedLine ({ toX (0.0f), toY (specMode ? 0.5f : 0.0f), toX (1.0f), toY (specMode ? 0.5f : 1.0f) }, dashes, 2, t.px (1.0f));

    constexpr int steps = 256;
    juce::Path path;
    for (int i = 0; i <= steps; ++i)
    {
        const float x = static_cast<float> (i) / static_cast<float> (steps);
        const float px = toX (x), py = toY (evaluateAt (x));
        if (i == 0)
            path.startNewSubPath (px, py);
        else
            path.lineTo (px, py);
    }
    g.setColour (stored ? c.accent : c.textDim);
    g.strokePath (path, juce::PathStrokeType (t.px (2.0f)));

    g.setColour (c.textDim);
    g.setFont (t.labelFont());
    g.drawText (specMode ? "Frequency 20 Hz - 20 kHz" : modMode ? "Source value in" : "Frame position in", plot.toNearestInt().removeFromBottom (t.ipx (16.0f)), juce::Justification::centredRight, false);
    g.drawText (specMode ? "gain" : "out", plot.toNearestInt().removeFromTop (t.ipx (16.0f)), juce::Justification::centredLeft, false);
    g.drawText (specMode ? juce::String ("Spectral filter") + (stored ? "" : " (flat)")
                         : juce::String (modMode ? "Slot curve " : "Remap ") + juce::String (slot()) + (stored ? "" : modMode ? " (straight)" : " (identity)"),
                plot.toNearestInt().removeFromTop (t.ipx (16.0f)), juce::Justification::centredRight, false);

    if (! pencil)
    {
        const float hr = t.px (5.0f);
        g.setColour (c.accentDim);
        for (int i = 0; i + 1 < static_cast<int> (current.points.size()); ++i)
        {
            const auto& a = current.points[static_cast<size_t> (i)];
            const auto& b = current.points[static_cast<size_t> (i) + 1];
            if ((b.x - a.x) * plot.getWidth() < static_cast<float> (t.touch()) * 0.5f)
                continue;
            const auto h = handlePosition (i);
            juce::Path diamond;
            diamond.addQuadrilateral (h.x, h.y - hr, h.x + hr, h.y, h.x, h.y + hr, h.x - hr, h.y);
            g.fillPath (diamond);
        }
        for (int i = 0; i < static_cast<int> (current.points.size()); ++i)
        {
            const auto pos = pointPosition (i);
            const bool sel = i == selected;
            const float r = t.px (sel ? 7.0f : 5.0f);
            g.setColour (sel ? c.accent : c.text);
            g.fillEllipse (pos.x - r, pos.y - r, 2.0f * r, 2.0f * r);
            g.setColour (c.panel);
            g.drawEllipse (pos.x - r, pos.y - r, 2.0f * r, 2.0f * r, t.px (1.5f));
        }
    }
}

// ---- gestures --------------------------------------------------------------------------------------------------------------------

void RemapEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! plot.expanded (static_cast<float> (ctx.theme.gap())).contains (e.position))
        return;
    gestureStart = current;
    gestureChanged = false;
    if (pencil)
    {
        drag = Drag::pencil;
        strokePoints.clear();
        strokePoints.emplace_back (fromX (e.position.x), fromY (e.position.y));
        return;
    }
    if (const int i = hitPoint (e.position); i >= 0)
    {
        drag = Drag::point;
        dragIndex = i;
        selected = i;
        updateButtons();
        repaint();
        return;
    }
    if (const int s = hitHandle (e.position); s >= 0)
    {
        drag = Drag::handle;
        dragIndex = s;
        dragStartPower = current.points[static_cast<size_t> (s)].power;
        return;
    }
    if (selected >= 0)
    {
        selected = -1;
        updateButtons();
        repaint();
    }
}

void RemapEditor::mouseDrag (const juce::MouseEvent& e)
{
    switch (drag)
    {
        case Drag::point:
            movePoint (dragIndex, fromX (e.position.x), fromY (e.position.y));
            break;
        case Drag::handle:
        {
            // dragging the diamond up bends the curve above the straight line, whatever the direction of the segment
            const float up = -(e.position.y - e.getMouseDownPosition().toFloat().y);
            const float perPixel = kMaxPower / juce::jmax (1.0f, plot.getHeight() * 0.5f);
            const auto& a = current.points[static_cast<size_t> (dragIndex)];
            const auto& b = current.points[static_cast<size_t> (dragIndex) + 1];
            const float sign = b.y >= a.y ? -1.0f : 1.0f;
            setSegmentPower (dragIndex, dragStartPower + sign * up * perPixel);
            break;
        }
        case Drag::pencil:
            strokePoints.emplace_back (fromX (e.position.x), fromY (e.position.y));
            break;
        case Drag::none:
            break;
    }
}

void RemapEditor::mouseUp (const juce::MouseEvent&)
{
    const Drag finished = drag;
    drag = Drag::none;
    if (finished == Drag::pencil)
    {
        if (strokePoints.size() >= 2)
            paintStroke (strokePoints);
        strokePoints.clear();
    }
    else if (finished != Drag::none && gestureChanged)
    {
        // one drag = one undo step: the state before the gesture
        undoStack.push_back (gestureStart);
        if (undoStack.size() > 100)
            undoStack.erase (undoStack.begin());
        redoStack.clear();
        updateButtons();
    }
    if (dirty)
        commitNow();
    gestureChanged = false;
}

void RemapEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (pencil || ! plot.expanded (static_cast<float> (ctx.theme.gap())).contains (e.position))
        return;
    if (const int i = hitPoint (e.position); i >= 0)
    {
        if (! deletePoint (i))
            movePoint (i, current.points[static_cast<size_t> (i)].x, i == 0 ? 0.0f : 1.0f); // an end point: back to its corner
    }
    else if (const int s = hitHandle (e.position); s >= 0)
        setSegmentPower (s, 0.0f); // straight
    else
        addPointAt (fromX (e.position.x));
    if (dirty)
        commitNow();
}
} // namespace synthyboy::ui
