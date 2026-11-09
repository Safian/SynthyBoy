// SynthyBoy - UI: free-form LFO shape editor (see LfoShapeEditor.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "LfoShapeEditor.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

#include "../ContentIO.h"
#include "../ParamHandle.h"
#include "synthyboy/params.h"

namespace synthyboy::ui
{
namespace
{
constexpr int kDrawColumns = 32;
constexpr size_t kMinPoints = 2;
constexpr float kMaxPower = static_cast<float> (kMaxPointPower);
constexpr float kTwoPi = 6.28318530717958647692f;

const char* const kPresetNames[kNumLfoShapes] = { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Pulse 25%",
                                                  "Steps Up", "Steps Down", "Random Steps", "Exp Up", "Exp Down", "Half Sine" };

shapes::ShapeData toShapeData (const ContentLfoShape& s)
{
    shapes::ShapeData d;
    d.numPoints = std::min<size_t> (s.points.size(), shapes::ShapeData::kMaxPoints);
    for (size_t i = 0; i < d.numPoints; ++i)
    {
        d.points[i].x = s.points[i].x;
        d.points[i].y = s.points[i].y;
        d.points[i].curve = s.points[i].power;
        d.points[i].smooth = s.smooth;
    }
    d.sanitize();
    return d;
}

bool sameShape (const ContentLfoShape& a, const ContentLfoShape& b)
{
    return a.lfo == b.lfo && a.smooth == b.smooth && a.loopPoint == b.loopPoint && a.points.size() == b.points.size()
        && std::equal (a.points.begin(), a.points.end(), b.points.begin());
}

float snapTo (float v, int divisions) noexcept
{
    const auto d = static_cast<float> (divisions);
    return std::round (v * d) / d;
}

/** Fills `s.points` with n + 1 samples of f over x in 0..1 (y in 0..1). */
void sampled (ContentLfoShape& s, int n, const std::function<float (float)>& f)
{
    for (int i = 0; i <= n; ++i)
    {
        const float x = static_cast<float> (i) / static_cast<float> (n);
        s.points.emplace_back (x, juce::jlimit (0.0f, 1.0f, f (x)));
    }
}
} // namespace

const char* lfoShapePresetName (int builtInShape)
{
    return kPresetNames[juce::jlimit (0, kNumLfoShapes - 1, builtInShape)];
}

ContentLfoShape lfoShapePreset (int lfo, int builtInShape)
{
    ContentLfoShape s;
    s.lfo = lfo;
    s.name = "Custom";
    auto add = [&s] (float x, float y, float power = 0.0f) { s.points.emplace_back (x, y, power); };

    switch (builtInShape)
    {
        case 0:  sampled (s, 32, [] (float x) { return 0.5f + 0.5f * std::sin (kTwoPi * x); }); break;
        case 1:  add (0.0f, 0.0f); add (0.5f, 1.0f); add (1.0f, 0.0f); break;
        case 2:  add (0.0f, 0.0f); add (1.0f, 1.0f); break;
        case 3:  add (0.0f, 1.0f); add (1.0f, 0.0f); break;
        case 4:  add (0.0f, 1.0f); add (0.5f, 1.0f); add (0.5f, 0.0f); add (1.0f, 0.0f); break;
        case 5:  add (0.0f, 1.0f); add (0.25f, 1.0f); add (0.25f, 0.0f); add (1.0f, 0.0f); break;
        case 6:
        case 7:
            for (int i = 0; i < 4; ++i)
            {
                const float level = static_cast<float> (builtInShape == 6 ? i : 3 - i) / 3.0f;
                add (static_cast<float> (i) / 4.0f, level);
                add (static_cast<float> (i + 1) / 4.0f, level);
            }
            break;
        case 8:
        {
            static constexpr float steps[8] = { 0.4f, -0.7f, 0.9f, -0.2f, -0.9f, 0.6f, 0.1f, -0.5f };
            for (int i = 0; i < 8; ++i)
            {
                add (static_cast<float> (i) / 8.0f, 0.5f + 0.5f * steps[i]);
                add (static_cast<float> (i + 1) / 8.0f, 0.5f + 0.5f * steps[i]);
            }
            break;
        }
        case 9:  add (0.0f, 0.0f, 5.0f); add (1.0f, 1.0f); break;   // power 5: (e^(5x) - 1) / (e^5 - 1), the built-in
        case 10: add (0.0f, 1.0f, -5.0f); add (1.0f, 0.0f); break;
        case 11: sampled (s, 16, [] (float x) { return std::sin (3.14159265358979f * x); }); break;
        default: add (0.0f, 0.5f); add (1.0f, 0.5f); break;
    }
    return s;
}

bool copyLfoSettings (UiContext& ctx, bool random, int from, int to)
{
    const int count = random ? 4 : kMaxContentLfoShapes;
    if (from == to || from < 1 || to < 1 || from > count || to > count)
        return false;

    const std::string base = random ? "rlfo" : "lfo";
    const std::string src = base + std::to_string (from) + ".";
    const std::string dst = base + std::to_string (to) + ".";
    for (size_t i = 0; i < paramCount(); ++i)
    {
        const std::string key = paramTable()[i].key;
        if (key.compare (0, src.size(), src) != 0)
            continue;
        ParamHandle source (ctx, key);
        ParamHandle target (ctx, dst + key.substr (src.size()));
        if (source.valid() && target.valid())
            target.setPlainComplete (source.plain());
    }

    if (! random)
    {
        auto content = ctx.host.content();
        const auto find = [&content] (int n) { return std::find_if (content.lfoShapes.begin(), content.lfoShapes.end(), [n] (const ContentLfoShape& l) { return l.lfo == n; }); };
        const auto source = find (from);
        ContentLfoShape copy;
        const bool hasSource = source != content.lfoShapes.end();
        if (hasSource)
        {
            copy = *source;
            copy.lfo = to;
        }
        const auto target = find (to);
        if (target != content.lfoShapes.end())
            content.lfoShapes.erase (target);
        if (hasSource)
            content.lfoShapes.push_back (std::move (copy));
        ctx.host.setContent (content);
    }
    return true;
}

// ---- LfoShapeEditor ---------------------------------------------------------------------------------------------------------------

LfoShapeEditor::LfoShapeEditor (UiContext& context, int lfoIndex) : ctx (context), lfo (lfoIndex)
{
    current = lfoShapePreset (lfo, 1);
    buttons = { &pointBtn, &drawBtn, &stepsBtn, &brushBtn, &brushPickBtn, &snapBtn, &smoothBtn, &propBtn, &allCurvesBtn, &addBtn, &deleteBtn,
                &undoBtn, &redoBtn, &presetBtn, &xField, &yField, &loopBtn, &startBtn, &segBtn, &colsMinus, &colsField, &colsPlus, &rowsMinus,
                &rowsField, &rowsPlus };
    for (auto* b : buttons)
        addAndMakeVisible (*b);

    for (auto* b : { &pointBtn, &drawBtn, &stepsBtn, &brushBtn, &snapBtn, &smoothBtn, &propBtn, &allCurvesBtn })
        b->setClickingTogglesState (false);
    pointBtn.onClick = [this] { setTool (Tool::point); };
    drawBtn.onClick = [this] { setTool (Tool::draw); };
    stepsBtn.onClick = [this] { setTool (Tool::steps); };
    brushBtn.onClick = [this] { setTool (Tool::brush); };
    brushPickBtn.onClick = [this]
    {
        juce::PopupMenu menu;
        for (int i = 0; i < kNumBrushes; ++i)
            menu.addItem (i + 1, brushName (i), true, i == brush_);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&brushPickBtn),
                            [safe = juce::Component::SafePointer<LfoShapeEditor> (this)] (int result)
                            {
                                if (safe != nullptr && result > 0)
                                {
                                    safe->setBrush (result - 1);
                                    safe->setTool (Tool::brush);
                                }
                            });
    };
    snapBtn.onClick = [this] { setSnap (! snap); };
    smoothBtn.onClick = [this] { setSmooth (! current.smooth); };
    propBtn.onClick = [this] { setProportional (! prop); };
    allCurvesBtn.onClick = [this] { setAllCurves (! allCurvesOn); };
    loopBtn.onClick = [this] { loopHere(); };
    startBtn.onClick = [this] { startHere(); };
    segBtn.onClick = [this]
    {
        juce::PopupMenu menu;
        const char* const names[] = { "Straight", "Ease in", "Ease out", "Hard in", "Hard out" };
        for (int i = 0; i < 5; ++i)
            menu.addItem (i + 1, names[i]);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&segBtn),
                            [safe = juce::Component::SafePointer<LfoShapeEditor> (this)] (int result)
                            {
                                if (safe != nullptr && result > 0)
                                    safe->setSegmentShape (result - 1);
                            });
    };
    colsMinus.onClick = [this] { setGrid (gridCols - 1, gridRowsN); };
    colsPlus.onClick = [this] { setGrid (gridCols + 1, gridRowsN); };
    rowsMinus.onClick = [this] { setGrid (gridCols, gridRowsN - 1); };
    rowsPlus.onClick = [this] { setGrid (gridCols, gridRowsN + 1); };
    for (auto* f : { &xField, &yField, &colsField, &rowsField })
    {
        f->setInputRestrictions (6, "0123456789.");
        f->setJustification (juce::Justification::centred);
        f->setSelectAllWhenFocused (true);
        f->onReturnKey = [this] { applyFieldEditors(); };
        f->onFocusLost = [this] { applyFieldEditors(); };
    }
    xField.setTextToShowWhenEmpty ("X %", ctx.theme.colours.textDim);
    yField.setTextToShowWhenEmpty ("Y %", ctx.theme.colours.textDim);
    xField.setTooltip ("X of the selected point, percent");
    yField.setTooltip ("Y of the selected point, percent");
    colsField.setTooltip ("Grid columns");
    rowsField.setTooltip ("Grid rows");
    press.onLongPress = [this]
    {
        // held still on a point: its modulation sheet (nothing has changed yet, or the press would have been cancelled by the slop)
        if (pendingSheet >= 0 && ! gestureChanged && (drag == Drag::point || drag == Drag::pointMod || drag == Drag::selection))
        {
            const int index = pendingSheet;
            pendingSheet = -1;
            drag = Drag::none;
            showPointSheet (index);
            return;
        }
        // held still on the empty plot: a marquee rectangle
        if (drag == Drag::none && tool_ == Tool::point && pendingMarquee)
        {
            drag = Drag::marquee;
            marqueeFrom = marqueeTo = selectionDown;
            repaint();
        }
    };
    addBtn.onClick = [this]
    {
        // the middle of the widest gap, on the curve
        float bestX = 0.5f, bestGap = -1.0f;
        for (size_t i = 0; i + 1 < current.points.size(); ++i)
        {
            const float gap = current.points[i + 1].x - current.points[i].x;
            if (gap > bestGap)
            {
                bestGap = gap;
                bestX = 0.5f * (current.points[i].x + current.points[i + 1].x);
            }
        }
        addPointAt (bestX, evaluateAt (bestX));
    };
    deleteBtn.onClick = [this] { deleteSelected(); };
    undoBtn.onClick = [this] { undo(); };
    redoBtn.onClick = [this] { redo(); };
    {
        LibraryMenuSpec spec;
        spec.category = kLibLfoShapes;
        spec.noun = "shape";
        spec.suggestedName = "My shape";
        spec.makeItem = [this] (const juce::String& name)
        {
            LfoShapeFile file;
            file.name = name.toStdString();
            file.smooth = current.smooth;
            file.points = current.points;
            return file.points.size() >= 2 ? serializeLfoShapeFile (file) : std::string();
        };
        spec.applyItem = [this] (const juce::String&, const std::string& json)
        {
            LfoShapeFile file;
            return parseLfoShapeFile (json, file) && applyShapeFile (file);
        };
        spec.fileWildcards = "*.json;*.vitallfo";
        spec.applyFile = [this] (const juce::File& file) { return applyShapeFromFile (file); };
        shapeLibrary = std::make_shared<LibraryMenu> (ctx, std::move (spec));
    }
    presetBtn.onClick = [this]
    {
        juce::PopupMenu menu;
        for (int i = 0; i < kNumLfoShapes; ++i)
            menu.addItem (i + 1, lfoShapePresetName (i));
        menu.addSeparator();
        shapeLibrary->addTo (menu, 1000);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetBtn).withMinimumWidth (ctx.theme.ipx (160.0f)),
                            [safe = juce::Component::SafePointer<LfoShapeEditor> (this)] (int result)
                            {
                                if (safe == nullptr || result <= 0)
                                    return;
                                if (result >= 1000)
                                    safe->shapeLibrary->run (result);
                                else
                                    safe->applyPreset (result - 1);
                            });
    };
    updateButtons();
}

LfoShapeEditor::~LfoShapeEditor()
{
    stopTimer();
    if (dirty)
        commitNow();
}

void LfoShapeEditor::setActive (bool shouldBeActive, int startFrom)
{
    if (shouldBeActive == active)
        return;
    active = shouldBeActive;
    if (! active)
    {
        stopTimer();
        if (dirty)
            commitNow();
        return;
    }

    undoStack.clear();
    redoStack.clear();
    selectOnly (-1);
    const auto content = ctx.host.content();
    const auto it = std::find_if (content.lfoShapes.begin(), content.lfoShapes.end(), [this] (const ContentLfoShape& l) { return l.lfo == lfo; });
    if (it != content.lfoShapes.end() && it->points.size() >= kMinPoints)
    {
        current = *it;
    }
    else
    {
        current = lfoShapePreset (lfo, startFrom);
        dirty = true;
    }
    cacheValid = false;
    if (dirty)
        commitNow();
    updateButtons();
    repaint();
    idleTicks = 0;
    startTimerHz (30);
}

void LfoShapeEditor::reloadFromHost()
{
    if (drag != Drag::none)
        return;
    const auto content = ctx.host.content();
    const auto it = std::find_if (content.lfoShapes.begin(), content.lfoShapes.end(), [this] (const ContentLfoShape& l) { return l.lfo == lfo; });
    if (it == content.lfoShapes.end() || it->points.size() < kMinPoints || sameShape (*it, current))
        return;
    current = *it; // changed behind our back (preset load, copy from another LFO): the old history no longer applies
    undoStack.clear();
    redoStack.clear();
    selectOnly (-1);
    cacheValid = false;
    updateButtons();
    repaint();
}

void LfoShapeEditor::timerCallback()
{
    if (const int armed = armedSource(); armed != lastArmed)
    {
        lastArmed = armed;
        repaint(); // the target rings of the point modulation
    }
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

void LfoShapeEditor::commitNow()
{
    dirty = false;
    auto content = ctx.host.content();
    const auto it = std::find_if (content.lfoShapes.begin(), content.lfoShapes.end(), [this] (const ContentLfoShape& l) { return l.lfo == lfo; });
    if (it != content.lfoShapes.end())
        *it = current;
    else
        content.lfoShapes.push_back (current);
    ctx.host.setContent (content);
}

void LfoShapeEditor::pushUndo()
{
    if (drag != Drag::none) // a gesture records its start state once, on mouse up
        return;
    undoStack.push_back (current);
    if (undoStack.size() > 100)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void LfoShapeEditor::touched()
{
    cacheValid = false;
    dirty = true;
    gestureChanged = true;
    updateButtons();
    repaint();
}

void LfoShapeEditor::undo()
{
    if (undoStack.empty())
        return;
    redoStack.push_back (current);
    current = undoStack.back();
    undoStack.pop_back();
    selectOnly (-1);
    touched();
}

void LfoShapeEditor::redo()
{
    if (redoStack.empty())
        return;
    undoStack.push_back (current);
    current = redoStack.back();
    redoStack.pop_back();
    selectOnly (-1);
    touched();
}

bool LfoShapeEditor::addPointAt (float x, float y)
{
    if (current.points.size() >= static_cast<size_t> (kMaxLfoShapePoints))
        return false;
    x = juce::jlimit (0.0f, 1.0f, x);
    y = juce::jlimit (0.0f, 1.0f, y);
    // after the last point with x <= the new one (a step keeps its two points in order), never before the first / after the last
    size_t at = 1;
    while (at + 1 < current.points.size() && current.points[at].x <= x)
        ++at;
    pushUndo();
    current.points.insert (current.points.begin() + static_cast<std::ptrdiff_t> (at), ContentPoint (x, y, 0.0f));
    if (current.loopPoint >= static_cast<int> (at))
        ++current.loopPoint;
    selectOnly (static_cast<int> (at));
    touched();
    return true;
}

bool LfoShapeEditor::deletePoint (int index)
{
    const int n = static_cast<int> (current.points.size());
    if (index <= 0 || index >= n - 1 || n <= static_cast<int> (kMinPoints))
        return false; // the two end points stay
    pushUndo();
    current.points.erase (current.points.begin() + index);
    if (current.loopPoint == index)
        current.loopPoint = -1;
    else if (current.loopPoint > index)
        --current.loopPoint;
    selectOnly (-1);
    touched();
    return true;
}

bool LfoShapeEditor::movePoint (int index, float x, float y)
{
    const int n = static_cast<int> (current.points.size());
    if (index < 0 || index >= n)
        return false;
    auto& p = current.points[static_cast<size_t> (index)];
    if (index == 0)
        x = 0.0f;
    else if (index == n - 1)
        x = 1.0f;
    else
        x = juce::jlimit (current.points[static_cast<size_t> (index) - 1].x, current.points[static_cast<size_t> (index) + 1].x, x);
    y = juce::jlimit (0.0f, 1.0f, y);
    if (juce::exactlyEqual (p.x, x) && juce::exactlyEqual (p.y, y))
        return false;
    pushUndo();
    p.x = x;
    p.y = y;
    touched();
    return true;
}

bool LfoShapeEditor::setSegmentPower (int segment, float power)
{
    if (segment < 0 || segment + 1 >= static_cast<int> (current.points.size()))
        return false;
    power = juce::jlimit (-kMaxPower, kMaxPower, power);
    auto& p = current.points[static_cast<size_t> (segment)];
    if (juce::exactlyEqual (p.power, power))
        return false;
    pushUndo();
    p.power = power;
    touched();
    return true;
}

void LfoShapeEditor::applyPreset (int builtInShape)
{
    pushUndo();
    current = lfoShapePreset (lfo, builtInShape);
    selectOnly (-1);
    touched();
}

bool LfoShapeEditor::applyShapeFile (const LfoShapeFile& shape)
{
    if (shape.points.size() < kMinPoints || static_cast<int> (shape.points.size()) > kMaxLfoShapePoints)
        return false;
    pushUndo();
    current.points = shape.points;
    current.smooth = shape.smooth;
    current.loopPoint = -1;
    current.name = shape.name;
    selectOnly (-1);
    touched();
    return true;
}

bool LfoShapeEditor::applyShapeFromFile (const juce::File& file)
{
    LfoShapeFile shape;
    if (file.getFileExtension().equalsIgnoreCase (".vitallfo"))
    {
        const auto result = importVitalFile (ctx, file, 0, 0); // a shape is applied below to THIS editor's LFO, not to a slot chosen by the flow
        if (! result.imported.ok || result.imported.kind != VitalFileKind::lfo)
            return false;
        shape = result.imported.lfoShape;
    }
    else if (! parseLfoShapeFile (file.loadFileAsString().toStdString(), shape))
        return false;
    return applyShapeFile (shape);
}

void LfoShapeEditor::setSmooth (bool shouldSmooth)
{
    if (current.smooth == shouldSmooth)
        return;
    pushUndo();
    current.smooth = shouldSmooth;
    touched();
}

void LfoShapeEditor::setSnap (bool shouldSnap)
{
    snap = shouldSnap;
    updateButtons();
}

void LfoShapeEditor::setDrawTool (bool shouldDraw)
{
    setTool (shouldDraw ? Tool::draw : Tool::point);
}

void LfoShapeEditor::setTool (Tool newTool)
{
    tool_ = newTool;
    updateButtons();
    repaint();
}

const char* LfoShapeEditor::brushName (int brush) noexcept
{
    static const char* const names[kNumBrushes] = { "Step", "Half", "Down", "Up", "Tri" }; // Vital's paint patterns
    return names[juce::jlimit (0, kNumBrushes - 1, brush)];
}

void LfoShapeEditor::setBrush (int newBrush)
{
    brush_ = juce::jlimit (0, kNumBrushes - 1, newBrush);
    updateButtons();
}

void LfoShapeEditor::setGrid (int columns, int rows)
{
    gridCols = juce::jlimit (1, 32, columns);
    gridRowsN = juce::jlimit (1, 32, rows);
    refreshFields();
    repaint();
}

void LfoShapeEditor::setProportional (bool on)
{
    prop = on;
    updateButtons();
}

void LfoShapeEditor::setAllCurves (bool on)
{
    allCurvesOn = on;
    updateButtons();
}

juce::TextButton* LfoShapeEditor::barButton (const juce::String& text)
{
    for (auto* c : buttons)
        if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == text)
            return b;
    return nullptr;
}

void LfoShapeEditor::selectOnly (int index)
{
    selected = index;
    multi.clear();
    if (index >= 0)
        multi.push_back (index);
}

bool LfoShapeEditor::selectInRect (juce::Rectangle<float> rect)
{
    multi.clear();
    for (int i = 0; i < static_cast<int> (current.points.size()); ++i)
        if (rect.contains (pointPosition (i)))
            multi.push_back (i);
    selected = multi.empty() ? -1 : multi.front();
    updateButtons();
    repaint();
    return ! multi.empty();
}

void LfoShapeEditor::applySelectionMove (const std::vector<ContentPoint>& base, int anchor, float dx, float dy)
{
    if (multi.empty() || base.size() != current.points.size())
        return;
    const int n = static_cast<int> (base.size());
    float dmax = 0.0f;
    const float ax = base[static_cast<size_t> (juce::jlimit (0, n - 1, anchor))].x;
    for (const int i : multi)
        dmax = juce::jmax (dmax, std::abs (base[static_cast<size_t> (i)].x - ax));

    std::vector<float> xs (static_cast<size_t> (n)), ys (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        xs[static_cast<size_t> (i)] = base[static_cast<size_t> (i)].x;
        ys[static_cast<size_t> (i)] = base[static_cast<size_t> (i)].y;
    }
    for (const int i : multi)
    {
        // rainbow falloff: 1 at the finger, 0 at the farthest selected point; flat 1 without Prop
        float w = 1.0f;
        if (prop && dmax > 1.0e-6f)
            w = 0.5f * (1.0f + std::cos (3.14159265f * std::abs (base[static_cast<size_t> (i)].x - ax) / dmax));
        const auto k = static_cast<size_t> (i);
        ys[k] = juce::jlimit (0.0f, 1.0f, base[k].y + w * dy);
        xs[k] = i == 0 ? 0.0f : i == n - 1 ? 1.0f : juce::jlimit (0.0f, 1.0f, base[k].x + w * dx);
    }
    // the order of the points never changes
    for (int i = 1; i < n; ++i)
        xs[static_cast<size_t> (i)] = juce::jmax (xs[static_cast<size_t> (i)], xs[static_cast<size_t> (i) - 1]);
    for (int i = n - 2; i >= 0; --i)
        xs[static_cast<size_t> (i)] = juce::jmin (xs[static_cast<size_t> (i)], xs[static_cast<size_t> (i) + 1]);
    for (int i = 0; i < n; ++i)
    {
        current.points[static_cast<size_t> (i)].x = xs[static_cast<size_t> (i)];
        current.points[static_cast<size_t> (i)].y = ys[static_cast<size_t> (i)];
    }
    touched();
}

bool LfoShapeEditor::moveSelection (int anchor, float dx, float dy)
{
    if (multi.empty())
        return false;
    pushUndo();
    applySelectionMove (current.points, anchor, dx, dy);
    return true;
}

bool LfoShapeEditor::setSelectedXY (float x, float y)
{
    return selected >= 0 && movePoint (selected, x, y);
}

bool LfoShapeEditor::applyFieldText (bool isX, const juce::String& text)
{
    if (selected < 0 || selected >= static_cast<int> (current.points.size()))
        return false;
    double v = 0.0;
    if (! parseNumber (text.toStdString(), v) || v < 0.0 || v > 100.0)
        return false;
    const auto& p = current.points[static_cast<size_t> (selected)];
    const bool changed = isX ? movePoint (selected, static_cast<float> (v / 100.0), p.y) : movePoint (selected, p.x, static_cast<float> (v / 100.0));
    if (changed)
        commitNow();
    refreshFields();
    return changed;
}

void LfoShapeEditor::applyFieldEditors()
{
    const auto number = [] (juce::TextEditor& f, double& out) { return parseNumber (f.getText().toStdString(), out); };
    double v = 0.0;
    int cols = gridCols, rows = gridRowsN;
    if (number (colsField, v))
        cols = juce::roundToInt (v);
    if (number (rowsField, v))
        rows = juce::roundToInt (v);
    if (cols != gridCols || rows != gridRowsN)
        setGrid (cols, rows);
    if (selected >= 0 && selected < static_cast<int> (current.points.size()))
    {
        const auto& p = current.points[static_cast<size_t> (selected)];
        if (number (xField, v) && std::abs (v / 100.0 - static_cast<double> (p.x)) > 0.0005)
            applyFieldText (true, xField.getText());
        else if (number (yField, v) && std::abs (v / 100.0 - static_cast<double> (p.y)) > 0.0005)
            applyFieldText (false, yField.getText());
    }
    refreshFields();
}

void LfoShapeEditor::refreshFields()
{
    if (! xField.hasKeyboardFocus (true))
        xField.setText (selected >= 0 && selected < static_cast<int> (current.points.size())
                            ? juce::String (current.points[static_cast<size_t> (selected)].x * 100.0f, 1)
                            : juce::String(),
                        juce::dontSendNotification);
    if (! yField.hasKeyboardFocus (true))
        yField.setText (selected >= 0 && selected < static_cast<int> (current.points.size())
                            ? juce::String (current.points[static_cast<size_t> (selected)].y * 100.0f, 1)
                            : juce::String(),
                        juce::dontSendNotification);
    if (! colsField.hasKeyboardFocus (true))
        colsField.setText (juce::String (gridCols), juce::dontSendNotification);
    if (! rowsField.hasKeyboardFocus (true))
        rowsField.setText (juce::String (gridRowsN), juce::dontSendNotification);
}

bool LfoShapeEditor::loopHere()
{
    if (selected < 0 || selected >= static_cast<int> (current.points.size()))
        return false;
    pushUndo();
    current.loopPoint = current.loopPoint == selected ? -1 : selected; // the same point again takes the loop off
    touched();
    commitNow();
    return true;
}

bool LfoShapeEditor::startHere()
{
    if (selected < 0 || selected >= static_cast<int> (current.points.size()))
        return false;
    ParamHandle phase (ctx, "lfo" + std::to_string (lfo) + ".phase");
    if (! phase.valid())
        return false;
    phase.setPlainComplete (juce::jlimit (0.0f, 360.0f, current.points[static_cast<size_t> (selected)].x * 360.0f));
    return true;
}

bool LfoShapeEditor::setSegmentShape (int shape)
{
    if (selected < 0)
        return false;
    const int segment = selected >= static_cast<int> (current.points.size()) - 1 ? selected - 1 : selected;
    static constexpr float powers[5] = { 0.0f, 3.0f, -3.0f, 8.0f, -8.0f };
    const bool changed = setSegmentPower (segment, powers[juce::jlimit (0, 4, shape)]);
    if (changed)
        commitNow();
    return changed;
}

void LfoShapeEditor::updateButtons()
{
    const int n = static_cast<int> (current.points.size());
    pointBtn.setToggleState (tool_ == Tool::point, juce::dontSendNotification);
    drawBtn.setToggleState (tool_ == Tool::draw, juce::dontSendNotification);
    stepsBtn.setToggleState (tool_ == Tool::steps, juce::dontSendNotification);
    brushBtn.setToggleState (tool_ == Tool::brush, juce::dontSendNotification);
    brushPickBtn.setButtonText (brushName (brush_));
    snapBtn.setToggleState (snap, juce::dontSendNotification);
    smoothBtn.setToggleState (current.smooth, juce::dontSendNotification);
    propBtn.setToggleState (prop, juce::dontSendNotification);
    allCurvesBtn.setToggleState (allCurvesOn, juce::dontSendNotification);
    undoBtn.setEnabled (canUndo());
    redoBtn.setEnabled (canRedo());
    deleteBtn.setEnabled (selected > 0 && selected < n - 1);
    addBtn.setEnabled (n < kMaxLfoShapePoints);
    const bool has = selected >= 0 && selected < n;
    for (auto* c : { static_cast<juce::Component*> (&loopBtn), static_cast<juce::Component*> (&startBtn), static_cast<juce::Component*> (&segBtn),
                     static_cast<juce::Component*> (&xField), static_cast<juce::Component*> (&yField) })
        c->setEnabled (has);
    loopBtn.setToggleState (has && current.loopPoint == selected, juce::dontSendNotification);
    refreshFields();
}

// ---- geometry ----------------------------------------------------------------------------------------------------------------------

float LfoShapeEditor::toX (float x) const noexcept { return plot.getX() + x * plot.getWidth(); }
float LfoShapeEditor::toY (float y) const noexcept { return plot.getBottom() - y * plot.getHeight(); }
float LfoShapeEditor::fromX (float px) const noexcept { return plot.getWidth() > 0.0f ? juce::jlimit (0.0f, 1.0f, (px - plot.getX()) / plot.getWidth()) : 0.0f; }
float LfoShapeEditor::fromY (float py) const noexcept { return plot.getHeight() > 0.0f ? juce::jlimit (0.0f, 1.0f, (plot.getBottom() - py) / plot.getHeight()) : 0.0f; }

float LfoShapeEditor::evaluateAt (float x) const
{
    if (! cacheValid)
    {
        cache = toShapeData (current);
        cacheValid = true;
    }
    return cache.evaluate (juce::jlimit (0.0f, 1.0f, x));
}

juce::Point<float> LfoShapeEditor::pointPosition (int index) const
{
    const auto& p = current.points[static_cast<size_t> (juce::jlimit (0, static_cast<int> (current.points.size()) - 1, index))];
    return { toX (p.x), toY (p.y) };
}

juce::Point<float> LfoShapeEditor::handlePosition (int segment) const
{
    const auto& a = current.points[static_cast<size_t> (segment)];
    const auto& b = current.points[static_cast<size_t> (segment) + 1];
    const float xm = 0.5f * (a.x + b.x);
    return { toX (xm), toY (evaluateAt (xm)) };
}

int LfoShapeEditor::hitPoint (juce::Point<float> p) const
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

int LfoShapeEditor::hitHandle (juce::Point<float> p) const
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

int LfoShapeEditor::barHeight (int width, bool layout)
{
    const auto& t = ctx.theme;
    const auto font = t.valueFont();
    int x = 0, y = 0;
    for (auto* b : buttons)
    {
        const auto* tb = dynamic_cast<juce::TextButton*> (b);
        const int w = tb != nullptr ? juce::jmax (t.touch(), juce::roundToInt (Theme::textWidth (font, tb->getButtonText())) + 2 * t.gap())
                                    : juce::jmax (t.touch(), t.ipx (62.0f)); // the number fields
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

int LfoShapeEditor::preferredHeight (int width) const
{
    const auto& t = ctx.theme;
    return const_cast<LfoShapeEditor*> (this)->barHeight (width, false) + t.gap() + t.ipx (170.0f);
}

void LfoShapeEditor::resized()
{
    const auto& t = ctx.theme;
    const int bar = barHeight (getWidth(), true);
    plot = getLocalBounds().toFloat().withTrimmedTop (static_cast<float> (bar + t.gap())).reduced (static_cast<float> (t.gap()), static_cast<float> (t.gap()));
    cacheValid = false;
}

// ---- painting ----------------------------------------------------------------------------------------------------------------------

void LfoShapeEditor::paint (juce::Graphics& g)
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

    // grid: the snap grid (16 x 8), the centre line brighter
    g.setColour (c.outline.withAlpha (0.45f));
    for (int i = 1; i < gridCols; ++i)
        g.drawVerticalLine (juce::roundToInt (toX (static_cast<float> (i) / static_cast<float> (gridCols))), plot.getY(), plot.getBottom());
    for (int i = 1; i < gridRowsN; ++i)
        g.drawHorizontalLine (juce::roundToInt (toY (static_cast<float> (i) / static_cast<float> (gridRowsN))), plot.getX(), plot.getRight());
    g.setColour (c.outline);
    g.drawHorizontalLine (juce::roundToInt (toY (0.5f)), plot.getX(), plot.getRight());

    // the curve, evaluated by the engine's own shape code
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
    g.setColour (c.accent);
    g.strokePath (path, juce::PathStrokeType (t.px (2.0f)));

    // the loop point of the shape: a dashed vertical line
    if (current.loopPoint >= 0 && current.loopPoint < static_cast<int> (current.points.size()))
    {
        const float lx = toX (current.points[static_cast<size_t> (current.loopPoint)].x);
        g.setColour (c.ok);
        const float dashes[] = { t.px (5.0f), t.px (4.0f) };
        g.drawDashedLine ({ lx, plot.getY(), lx, plot.getBottom() }, dashes, 2, t.px (1.5f));
        g.setFont (t.labelFont());
        g.drawText ("LOOP", juce::Rectangle<float> (lx + t.px (3.0f), plot.getY(), t.px (40.0f), t.px (14.0f)).toNearestInt(), juce::Justification::centredLeft, false);
    }

    // curve handles (diamonds) and points
    if (tool_ == Tool::point)
    {
        const float hr = t.px (5.0f);
        for (int i = 0; i + 1 < static_cast<int> (current.points.size()); ++i)
        {
            const auto& a = current.points[static_cast<size_t> (i)];
            const auto& b = current.points[static_cast<size_t> (i) + 1];
            if ((b.x - a.x) * plot.getWidth() < static_cast<float> (t.touch()) * 0.5f)
                continue;
            const auto h = handlePosition (i);
            juce::Path d;
            d.addPolygon (h, 4, hr * 1.3f, 0.0f);
            g.setColour (! juce::exactlyEqual (a.power, 0.0f) ? c.mod : c.textDim);
            g.fillPath (d);
        }
        paintPointModulation (g);
        for (int i = 0; i < static_cast<int> (current.points.size()); ++i)
        {
            const auto pos = pointPosition (i);
            const bool inMulti = std::find (multi.begin(), multi.end(), i) != multi.end();
            const float r = t.px (i == selected || inMulti ? 8.0f : 6.0f);
            g.setColour (i == selected || inMulti ? c.warn : c.text);
            g.fillEllipse (pos.x - r, pos.y - r, 2.0f * r, 2.0f * r);
            g.setColour (c.panelRaised);
            g.drawEllipse (pos.x - r, pos.y - r, 2.0f * r, 2.0f * r, 1.0f);
        }
    }

    g.setColour (c.textDim);
    g.setFont (t.labelFont());
    const char* const toolNames[] = { "", "  |  Draw", "  |  Steps", "  |  Brush" };
    g.drawText (juce::String (static_cast<int> (current.points.size())) + " points" + toolNames[static_cast<int> (tool_)],
                plot.reduced (t.px (6.0f), t.px (2.0f)), juce::Justification::bottomRight, false);
    if (drag == Drag::marquee)
    {
        const auto r = juce::Rectangle<float> (marqueeFrom, marqueeTo);
        g.setColour (c.mod.withAlpha (0.18f));
        g.fillRect (r);
        g.setColour (c.mod);
        g.drawRect (r, 1.5f);
    }
}

// ---- gestures ----------------------------------------------------------------------------------------------------------------------

void LfoShapeEditor::drawAt (juce::Point<float> p)
{
    const float x = fromX (p.x);
    float y = fromY (p.y);
    if (snap)
        y = snapTo (y, gridRowsN);
    const int column = juce::jlimit (0, kDrawColumns, juce::roundToInt (x * static_cast<float> (kDrawColumns)));

    if (lastDrawColumn < 0)
    {
        // the first touch turns the shape into a 32 column grid sampled from the current shape; painting then sets columns
        ContentLfoShape grid;
        grid.lfo = lfo;
        grid.name = current.name;
        grid.smooth = false;
        for (int i = 0; i <= kDrawColumns; ++i)
        {
            const float gx = static_cast<float> (i) / static_cast<float> (kDrawColumns);
            grid.points.emplace_back (gx, juce::jlimit (0.0f, 1.0f, evaluateAt (gx)), 0.0f);
        }
        current = std::move (grid);
        selectOnly (-1);
        lastDrawColumn = column;
        lastDrawY = y;
    }

    const int from = std::min (lastDrawColumn, column), to = std::max (lastDrawColumn, column);
    for (int i = from; i <= to; ++i)
    {
        const float f = to == from ? 1.0f : static_cast<float> (i - lastDrawColumn) / static_cast<float> (column - lastDrawColumn);
        current.points[static_cast<size_t> (i)].y = juce::jlimit (0.0f, 1.0f, lastDrawY + (y - lastDrawY) * f);
    }
    lastDrawColumn = column;
    lastDrawY = y;
    touched();
}

namespace
{
/** One grid cell of the Steps / Brush tools as points (cell fraction 0..1, level 0..1) for brush 0 Step, 1 Half, 2 Down, 3 Up, 4 Tri. */
std::vector<std::pair<float, float>> brushPoints (int brush, float level)
{
    switch (brush)
    {
        case 1: // Half: a half sine bump
            return { { 0.0f, 0.0f }, { 0.25f, 0.7071f * level }, { 0.5f, level }, { 0.75f, 0.7071f * level }, { 1.0f, 0.0f } };
        case 2: return { { 0.0f, level }, { 1.0f, 0.0f } };                    // Down
        case 3: return { { 0.0f, 0.0f }, { 1.0f, level } };                    // Up
        case 4: return { { 0.0f, 0.0f }, { 0.5f, level }, { 1.0f, 0.0f } };    // Tri
        default: return { { 0.0f, level }, { 1.0f, level } };                  // Step
    }
}
} // namespace

void LfoShapeEditor::rebuildFromCells()
{
    const int n = static_cast<int> (cellLevel.size());
    ContentLfoShape out;
    out.lfo = lfo;
    out.name = current.name;
    out.smooth = false;
    out.loopPoint = -1;
    for (int i = 0; i < n; ++i)
        for (const auto& [f, v] : brushPoints (cellBrush[static_cast<size_t> (i)], cellLevel[static_cast<size_t> (i)]))
            out.points.emplace_back ((static_cast<float> (i) + f) / static_cast<float> (n), juce::jlimit (0.0f, 1.0f, v), 0.0f);
    current = std::move (out);
}

void LfoShapeEditor::paintCellAt (juce::Point<float> p)
{
    const float x = fromX (p.x);
    float y = fromY (p.y);
    if (snap)
        y = snapTo (y, gridRowsN);
    const int pattern = tool_ == Tool::steps ? 0 : brush_;
    const int cell = juce::jlimit (0, gridCols - 1, static_cast<int> (x * static_cast<float> (gridCols)));

    if (lastCell < 0)
    {
        // the first touch turns the shape into one cell per grid column, each at the level the shape has in the middle of the cell
        cellLevel.assign (static_cast<size_t> (gridCols), 0.0f);
        cellBrush.assign (static_cast<size_t> (gridCols), 0);
        for (int i = 0; i < gridCols; ++i)
            cellLevel[static_cast<size_t> (i)] = juce::jlimit (0.0f, 1.0f, evaluateAt ((static_cast<float> (i) + 0.5f) / static_cast<float> (gridCols)));
        lastCell = cell;
        lastCellY = y;
        selectOnly (-1);
    }

    const int from = std::min (lastCell, cell), to = std::max (lastCell, cell);
    for (int i = from; i <= to; ++i)
    {
        const float f = to == from ? 1.0f : static_cast<float> (i - lastCell) / static_cast<float> (cell - lastCell);
        cellLevel[static_cast<size_t> (i)] = juce::jlimit (0.0f, 1.0f, lastCellY + (y - lastCellY) * f);
        cellBrush[static_cast<size_t> (i)] = pattern;
    }
    lastCell = cell;
    lastCellY = y;
    rebuildFromCells();
    touched();
}

void LfoShapeEditor::mouseDown (const juce::MouseEvent& e)
{
    gestureStart = current;
    gestureChanged = false;
    dragIndex = -1;
    drag = Drag::none;
    pendingMarquee = false;
    pendingSheet = -1;
    modAxisChosen = false;
    if (! getLocalBounds().toFloat().contains (e.position))
        return;

    if (tool_ == Tool::draw)
    {
        if (plot.expanded (static_cast<float> (ctx.theme.gap())).contains (e.position))
        {
            drag = Drag::draw;
            lastDrawColumn = -1;
            drawAt (e.position);
        }
        return;
    }
    if (tool_ == Tool::steps || tool_ == Tool::brush)
    {
        if (plot.expanded (static_cast<float> (ctx.theme.gap())).contains (e.position))
        {
            drag = Drag::cells;
            lastCell = -1;
            paintCellAt (e.position);
        }
        return;
    }

    if (const int i = hitPoint (e.position); i >= 0)
    {
        const bool partOfSelection = multi.size() > 1 && std::find (multi.begin(), multi.end(), i) != multi.end();
        pendingSheet = i;
        press.press (e.position);
        if (armedSource() > 0)
        {
            // a source is armed: this press sets its depth on the point instead of moving it
            drag = Drag::pointMod;
            dragIndex = i;
            selectOnly (i);
        }
        else if (partOfSelection)
        {
            drag = Drag::selection;
            dragIndex = i;
            selectionBase = current.points;
            selectionDown = e.position;
            selected = i;
        }
        else
        {
            drag = Drag::point;
            dragIndex = i;
            selectOnly (i);
        }
        updateButtons();
        repaint();
        return;
    }
    if (const int s = hitHandle (e.position); s >= 0)
    {
        drag = Drag::handle;
        dragIndex = s;
        dragStartPower = current.points[static_cast<size_t> (s)].power;
        handleStartPowers.clear();
        for (const auto& pt : current.points)
            handleStartPowers.push_back (pt.power);
        return;
    }
    if (selected >= 0 || ! multi.empty())
    {
        selectOnly (-1);
        updateButtons();
        repaint();
    }
    // held still on the empty plot = marquee (nothing else is under the finger)
    if (plot.expanded (static_cast<float> (ctx.theme.gap())).contains (e.position))
    {
        pendingMarquee = true;
        selectionDown = e.position;
        press.press (e.position);
    }
}

void LfoShapeEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (pendingMarquee || pendingSheet >= 0)
        press.moved (e.position);
    switch (drag)
    {
        case Drag::pointMod:
        {
            const auto delta = e.position - e.getMouseDownPosition().toFloat();
            const auto n = current.points.size();
            if (! modAxisChosen)
            {
                if (delta.getDistanceFromOrigin() < static_cast<float> (ctx.theme.px (6.0f)))
                    break;
                modAxis = pointModDragAxis (delta.x, delta.y, canModulatePointAxis (static_cast<size_t> (dragIndex), n, PointAxis::x));
                modAxisChosen = true;
                const auto& m = pointModOf (gestureStart.points[static_cast<size_t> (dragIndex)], modAxis);
                modStartDepth = m.present && pointModSourceChoice (m.source) == armedSource() ? m.amount : 0.0f;
            }
            const float depth = modAxis == PointAxis::x ? pointModDragAmount (modStartDepth, delta.x, plot.getWidth())
                                                         : pointModDragAmount (modStartDepth, -delta.y, plot.getHeight());
            setPointMod (current.points[static_cast<size_t> (dragIndex)], static_cast<size_t> (dragIndex), n, modAxis, armedSource(), depth);
            touched();
            break;
        }
        case Drag::point:
        {
            float x = fromX (e.position.x), y = fromY (e.position.y);
            if (snap)
            {
                x = snapTo (x, gridCols);
                y = snapTo (y, gridRowsN);
            }
            movePoint (dragIndex, x, y);
            break;
        }
        case Drag::handle:
        {
            // dragging the diamond up bends the curve above the straight line, whatever the direction of the segment
            const float up = -(e.position.y - e.getMouseDownPosition().toFloat().y);
            const float perPixel = kMaxPower / juce::jmax (1.0f, plot.getHeight() * 0.5f);
            if (allCurvesOn)
            {
                // "All curves": every segment bends by the same amount (each in its own direction)
                for (int i = 0; i + 1 < static_cast<int> (current.points.size()); ++i)
                {
                    const auto& a = current.points[static_cast<size_t> (i)];
                    const auto& b = current.points[static_cast<size_t> (i) + 1];
                    const float sign = b.y >= a.y ? -1.0f : 1.0f;
                    const float start = static_cast<size_t> (i) < handleStartPowers.size() ? handleStartPowers[static_cast<size_t> (i)] : 0.0f;
                    setSegmentPower (i, start + sign * up * perPixel);
                }
            }
            else
            {
                const auto& a = current.points[static_cast<size_t> (dragIndex)];
                const auto& b = current.points[static_cast<size_t> (dragIndex) + 1];
                const float sign = b.y >= a.y ? -1.0f : 1.0f;
                setSegmentPower (dragIndex, dragStartPower + sign * up * perPixel);
            }
            break;
        }
        case Drag::draw:
            drawAt (e.position);
            break;
        case Drag::cells:
            paintCellAt (e.position);
            break;
        case Drag::selection:
            applySelectionMove (selectionBase, dragIndex, (e.position.x - selectionDown.x) / juce::jmax (1.0f, plot.getWidth()),
                                -(e.position.y - selectionDown.y) / juce::jmax (1.0f, plot.getHeight()));
            break;
        case Drag::marquee:
            marqueeTo = e.position;
            repaint();
            break;
        case Drag::none:
            break;
    }
}

void LfoShapeEditor::mouseUp (const juce::MouseEvent&)
{
    press.cancel();
    pendingMarquee = false;
    pendingSheet = -1;
    if (drag == Drag::pointMod && ! modAxisChosen)
    {
        // a tap with a source armed: Y gets the source at 50 % unless that source is already on it
        const auto i = static_cast<size_t> (dragIndex);
        const auto& m = current.points[i].modY;
        if (! (m.present && pointModSourceChoice (m.source) == armedSource()))
            if (setPointMod (current.points[i], i, current.points.size(), PointAxis::y, armedSource(), 0.5f))
                touched();
    }
    modAxisChosen = false;
    if (drag == Drag::marquee)
    {
        drag = Drag::none;
        selectInRect (juce::Rectangle<float> (marqueeFrom, marqueeTo));
        return;
    }
    if (drag == Drag::none)
        return;
    drag = Drag::none;
    lastDrawColumn = -1;
    lastCell = -1;
    if (gestureChanged)
    {
        undoStack.push_back (gestureStart);
        if (undoStack.size() > 100)
            undoStack.erase (undoStack.begin());
        redoStack.clear();
        updateButtons();
        commitNow();
    }
    gestureChanged = false;
}

void LfoShapeEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (tool_ != Tool::point || plot.getWidth() < 8.0f)
        return;
    // the second press of the double click opened a gesture on the point; the edits below are discrete undo steps of their own
    press.cancel();
    pendingMarquee = false;
    pendingSheet = -1;
    drag = Drag::none;
    gestureChanged = false;
    if (armedSource() > 0)
        return; // the point modulation targets are armed: a double tap neither adds nor deletes
    if (const int i = hitPoint (e.position); i >= 0)
    {
        deletePoint (i);
        commitNow();
        return;
    }
    if (const int s = hitHandle (e.position); s >= 0)
    {
        setSegmentPower (s, 0.0f);
        commitNow();
        return;
    }
    if (plot.expanded (static_cast<float> (ctx.theme.gap())).contains (e.position))
    {
        float x = fromX (e.position.x), y = fromY (e.position.y);
        if (snap)
        {
            x = snapTo (x, gridCols);
            y = snapTo (y, gridRowsN);
        }
        addPointAt (x, y);
        commitNow();
    }
}

// ---- point modulation (row 05-22) --------------------------------------------------------------------------------------------------

int LfoShapeEditor::armedSource() const { return ctx.assign.flow().armedSource(); }

float& LfoShapeEditor::memoFor (int index, PointAxis axis)
{
    if (depthMemo.size() != current.points.size())
        depthMemo.assign (current.points.size(), { 0.0f, 0.0f });
    return depthMemo[static_cast<size_t> (index)][axis == PointAxis::x ? 0 : 1];
}

bool LfoShapeEditor::assignPointMod (int index, PointAxis axis, int sourceChoice, float depth)
{
    if (index < 0 || index >= static_cast<int> (current.points.size()))
        return false;
    auto next = current.points[static_cast<size_t> (index)];
    if (! setPointMod (next, static_cast<size_t> (index), current.points.size(), axis, sourceChoice, depth))
        return false;
    if (next == current.points[static_cast<size_t> (index)])
        return false;
    pushUndo();
    current.points[static_cast<size_t> (index)] = next;
    touched();
    commitNow();
    return true;
}

bool LfoShapeEditor::clearPointMod (int index, PointAxis axis)
{
    if (index < 0 || index >= static_cast<int> (current.points.size()) || ! pointModOf (current.points[static_cast<size_t> (index)], axis).present)
        return false;
    pushUndo();
    removePointMod (current.points[static_cast<size_t> (index)], axis);
    touched();
    commitNow();
    return true;
}

bool LfoShapeEditor::pointModBypassed (int index, PointAxis axis) const
{
    if (index < 0 || index >= static_cast<int> (current.points.size()))
        return false;
    const auto& pt = current.points[static_cast<size_t> (index)];
    return pointModOf (pt, axis).present && ! pointModActive (pt, axis);
}

bool LfoShapeEditor::bypassPointMod (int index, PointAxis axis, bool bypass)
{
    if (index < 0 || index >= static_cast<int> (current.points.size()))
        return false;
    auto& m = pointModOf (current.points[static_cast<size_t> (index)], axis);
    if (! m.present || pointModBypassed (index, axis) == bypass)
        return false;
    pushUndo();
    float& memo = memoFor (index, axis);
    if (bypass)
    {
        memo = m.amount;
        m.amount = 0.0f;
    }
    else
    {
        m.amount = std::abs (memo) > 0.0f ? memo : 0.5f;
    }
    touched();
    commitNow();
    return true;
}

void LfoShapeEditor::showPointSheet (int index)
{
    const int n = static_cast<int> (current.points.size());
    if (index < 0 || index >= n)
        return;
    selectOnly (index);
    updateButtons();
    repaint();

    const auto& p = current.points[static_cast<size_t> (index)];
    juce::PopupMenu menu;
    menu.addSectionHeader ("Point " + juce::String (index + 1) + " modulation");
    for (const auto axis : { PointAxis::x, PointAxis::y })
    {
        if (! canModulatePointAxis (static_cast<size_t> (index), static_cast<size_t> (n), axis))
            continue;
        const int base = axis == PointAxis::x ? 100 : 200;
        const juce::String name = axis == PointAxis::x ? "X" : "Y";
        const auto& m = pointModOf (p, axis);
        const int sourceNow = m.present ? pointModSourceChoice (m.source) : 0;

        juce::PopupMenu sources;
        for (int s = 1; s <= kNumModSources; ++s)
            sources.addItem (base + 10 + s, juce::String (modSourceTable()[s - 1].name), true, s == sourceNow);
        menu.addSubMenu (m.present ? name + ": " + juce::String (modSourceTable()[juce::jmax (1, sourceNow) - 1].name) + " "
                                         + juce::String (juce::roundToInt (m.amount * 100.0f)) + "%"
                                   : "Modulate " + name,
                         sources);
        if (m.present)
        {
            menu.addItem (base + 1, "Bypass " + name, true, pointModBypassed (index, axis));
            menu.addItem (base + 0, "Remove " + name);
        }
    }
    const auto at = pointPosition (index);
    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetScreenArea (localAreaToGlobal (juce::Rectangle<float> (at, at).expanded (ctx.theme.px (8.0f)).toNearestInt()))
                            .withMinimumWidth (ctx.theme.ipx (180.0f)),
                        [safe = juce::Component::SafePointer<LfoShapeEditor> (this), index] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;
                            const auto axis = result >= 200 ? PointAxis::y : PointAxis::x;
                            const int what = result % 100;
                            if (what == 0)
                                safe->clearPointMod (index, axis);
                            else if (what == 1)
                                safe->bypassPointMod (index, axis, ! safe->pointModBypassed (index, axis));
                            else if (what > 10 && index < static_cast<int> (safe->current.points.size()))
                            {
                                const auto& m = pointModOf (safe->current.points[static_cast<size_t> (index)], axis);
                                safe->assignPointMod (index, axis, what - 10, m.present && std::abs (m.amount) > 0.0f ? m.amount : 0.5f);
                            }
                        });
}

void LfoShapeEditor::paintPointModulation (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    const auto& c = t.colours;
    const bool armed = armedSource() > 0;
    for (int i = 0; i < static_cast<int> (current.points.size()); ++i)
    {
        const auto& pt = current.points[static_cast<size_t> (i)];
        const auto pos = pointPosition (i);
        const float r = t.px (11.0f);
        bool any = false;
        for (const auto axis : { PointAxis::x, PointAxis::y })
        {
            const auto& m = pointModOf (pt, axis);
            if (! m.present)
                continue;
            any = true;
            const bool live = pointModActive (pt, axis);
            const auto colour = ! live ? c.modIdle : m.amount >= 0.0f ? c.mod : c.modNegative;
            g.setColour (colour.withAlpha (live ? 0.75f : 0.5f));
            // the swing of the source: from the point to point + depth (clamped to the plot), as a bar
            if (axis == PointAxis::y)
            {
                const float to = juce::jlimit (plot.getY(), plot.getBottom(), toY (pt.y + m.amount));
                g.fillRoundedRectangle (juce::Rectangle<float> (pos.x - t.px (2.5f), juce::jmin (pos.y, to), t.px (5.0f), juce::jmax (t.px (2.0f), std::abs (to - pos.y))), t.px (2.0f));
            }
            else
            {
                const float to = juce::jlimit (plot.getX(), plot.getRight(), toX (pt.x + m.amount));
                g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (pos.x, to), pos.y - t.px (2.5f), juce::jmax (t.px (2.0f), std::abs (to - pos.x)), t.px (5.0f)), t.px (2.0f));
            }
        }
        if (any)
        {
            const bool live = pointModActive (pt, PointAxis::x) || pointModActive (pt, PointAxis::y);
            g.setColour (live ? c.mod : c.modIdle);
            g.drawEllipse (pos.x - r, pos.y - r, 2.0f * r, 2.0f * r, t.px (2.0f));
            if (i == selected)
            {
                g.setFont (t.labelFont());
                for (const auto axis : { PointAxis::x, PointAxis::y })
                {
                    const auto& m = pointModOf (pt, axis);
                    if (! m.present)
                        continue;
                    const int choice = pointModSourceChoice (m.source);
                    g.drawText ((axis == PointAxis::x ? "X " : "Y ") + juce::String (choice > 0 ? modSourceTable()[choice - 1].name : "?") + " "
                                    + juce::String (juce::roundToInt (m.amount * 100.0f)) + "%",
                                juce::Rectangle<float> (pos.x + r + t.px (2.0f), pos.y + (axis == PointAxis::x ? -t.px (14.0f) : 0.0f), t.px (110.0f), t.px (14.0f)).toNearestInt(),
                                juce::Justification::centredLeft, false);
                }
            }
        }
        else if (armed)
        {
            // a target for the armed source
            juce::Path ring, dashed;
            ring.addEllipse (pos.x - r, pos.y - r, 2.0f * r, 2.0f * r);
            const float dashes[] = { t.px (3.0f), t.px (3.0f) };
            juce::PathStrokeType (t.px (1.5f)).createDashedStroke (dashed, ring, dashes, 2);
            g.setColour (c.mod.withAlpha (0.7f));
            g.fillPath (dashed);
        }
    }
}
} // namespace synthyboy::ui
