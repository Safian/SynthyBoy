# EnvGraph

## What it is
The picture of a DAHDSR envelope (delay, attack, hold, decay, sustain, release and three curves) with handles that edit it by touch and the running dots of the sounding voices. The x axis is time with a ruler of round marks under the graph. Use it for any envelope with those stages.

## Header and types
`shell-mpc/ui/env_graph.h`: `EnvGraph`, `EnvGraph::State`, `EnvGraph::Dot`, `EnvGraph::Edit`, `EnvGraph::Geom`; namespace `envgraph`: `Handle { None = -1, Delay, Attack, Hold, Decay, Release, CurveA, CurveD, CurveR, kNumHandles }`, `Shape`, `kMinStageW`, `kSustainWidth`, time functions (`stageTime`, `timeFromNorm`, `normFromTime`, `levelAt`, `curveForMiddle`). Uses `time_ruler.h`.

## State fields (EnvGraph::State)
| Field | Meaning | Default |
|---|---|---|
| `shape`, `defaults` | `envgraph::Shape`: `t[5]` normalised times (delay, attack, hold, decay, release), `timeMs[5]`, `sustain`, `curve[3]` (-20 to 20) | see header |
| `ghost`, `ghostShape` | another envelope drawn behind | false |
| `numDots`, `dots[kMaxDots = 8]` | running dots: `stage` 1 to 6 (0 = none), `fraction` | 0 |
| `text[kNumHandles][16]` | the value text of every handle, formatted by the owner | empty |
| `decayTime`, `sustainText` | labels under the axis | empty |
| `colour` | `0xRRGGBB` of the curve; 0 = the envelope colours of the look | 0 |
| `selected` | the handle whose value is written on the graph (the owner's: set from a `kSelect` request or from the handle of a drag; the handle the finger holds is lit at once, from the gesture) | `None` |
| `beats` | time unit: ms or beats | false |
| `locked` | true: the envelope fills the plot; false: the zoom below (the owner's: set from a `kLock` request) | true |
| `zoom` | unlocked: the time the plot width stands for (0 = what the envelope needs now; the owner's: set from `kLock` / `kZoom` requests) | 0 |

## Size
`minSize()` = 300 x 160. Below 520 x 260 it draws the compact chrome (no LOCK button, always locked). Handles have 56 x 56 px targets (`kHit`). `plot()` is the drawing area. `plotArea` (a `Rect`, default empty): when the owner sets it (the Play page: `playrows::plotRect` of the display, below the header band) `plot()` returns it and everything of the graph (grid, ruler labels, curve, fill, handles, the value bubble, the running dots) lies in it or in the margin under it; empty = the margins inside the rectangle (the Env tab).

## Touch and knob behaviour
| Gesture | Change |
|---|---|
| drag a time handle (delay, attack, hold, decay, release) | `Value`, `id` of the graph, `value` = the new normalised time; `edit()` says `{ handle, t, level, curve, reset }` |
| drag the decay handle | moves its time and the sustain level; `edit().level` has the level |
| vertical drag on a small curve handle in the middle of attack, decay, release | bends the stage, `edit().curve` |
| double tap on a handle | the default; `edit().reset` true |
| tap on LOCK (full chrome) | `Request`; `edit().handle` is `kLock` (100): `edit().level` 1 = locked next, 0 = unlocked next, `edit().t` = the time window the picture stands for now (the unlocked zoom starts there, so the picture does not jump) |
| drag along the ruler band while unlocked | `Request`; `edit().handle` is `kZoom` (101), `edit().level` is the new zoom (the same zoom again is not reported) |
| tap on a handle other than `state.selected` | `Request`; `edit().handle` is `kSelect` (102), `edit().touched` is the handle |

Read `edit()` after each `Change`. The owner writes the parameters and gives the new shape back with the next state. The graph stores NOTHING (rule 2 of `shell-mpc/ui/AGENTS.md`): the three requests above are how it asks the owner to change its VIEW state (selected handle, lock, zoom); the owner keeps them and sets `state.selected / locked / zoom` on every update (a drag or a double tap selects its handle: `edit().handle` of its `Value` change). Private and transient: the handle held, the shape and the geometry at the touch, the finger's start, the zoom last reported. A `Value` step is reported when it differs from `state.shape`.

## Q-Link
Not a Q member (its values are the ADSR, SHAPE and TIME knobs around it).

## Options and variants
Locked / unlocked axis, beats mode, ghost shape, colour. `drawLive (sb)` draws the running dots and is not cached.

## Minimal example
```cpp
EnvGraph g;
g.setId (300);
g.place ({ 20, 120, 760, 300 });
g.state.shape.t[1] = normAttack;  g.state.shape.sustain = sustain;
g.state.colour = 0xff9a3d;
Change c = g.touchDown (x, y, nowMs);
c = g.touchMove (x, y);
if (c.kind == ChangeKind::Value && c.id == 300)
{
    const EnvGraph::Edit& e = g.edit();
    if (e.handle == envgraph::Attack) writeAttack (e.t);   // owner maps handle to parameter
    selected = e.handle;                                   // the owner's view state
}
else if (c.kind == ChangeKind::Request && c.id == 300)     // select / lock / zoom
{
    const EnvGraph::Edit& e = g.edit();
    if (e.handle == EnvGraph::kSelect) selected = e.touched;
    else if (e.handle == EnvGraph::kLock) { locked = e.level >= 0.5; if (! locked) zoom = e.t; }
    else if (e.handle == EnvGraph::kZoom) zoom = e.level;
}
// every update: g.state.selected = selected;  g.state.locked = locked;  g.state.zoom = zoom;
// every picture: g.draw (sb); g.drawLive (sb);
```

## Used on
`EnvCompact::graph`, `EnvFullScreen::graph` (`shell-mpc/ui/env_screen.h`), wired in `shell-mpc/overlay/env_ui.h` (`T::Graph`). Pictures: `docs/screens/env.jpg`, `play-env.png`.

## Limits and pitfalls
- `selected`, `locked` and `zoom` are view state that the OWNER keeps (the graph only asks, by the requests above): set them from your own members on every update, or the lock and the zoom jump back and no handle stays selected. `EnvUi` keeps them in `graphView_`, the template's `ShapePage` in `selected_ / locked_ / zoom_`.
- Never store the shape in the graph: copy it from the parameters every update.
- Value text and time scales are the owner's: format with the unit.
