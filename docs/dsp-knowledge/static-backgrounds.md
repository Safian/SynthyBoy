# Fixed MPC canvas assets

Owner decision, 2026-11-24: every page uses a uniform dark-blue canvas, the existing
`surface.canvas` colour `#090d19`. Committed `overlay_bg_*.png` files are fixed
assets. Packaging copies them byte-for-byte; no build or preview regenerates them.

Native component artwork still owns control faces, frames, values and labels.
The Mixer strip artwork and bindings are unchanged. The root canvas no longer
contains a duplicate of its strips, tracks, scales or names. Required fixed group
captions use the existing native Name/Value Label child factory; their positions
are recorded in `skin/TUI.json` (the `Native Caption ...` components).
LFO group captions retain their layout gates. Play captions already belong to its
native dial components, so no second copies are inserted.

The existing detector keeps its sentinel coordinates/colours. Each sentinel is a
separate fixed 5×5 native Image, not a patch to the background. Graph-owned grids,
axes and time scales remain with their live graph. In particular the Macro
spectrum draws its own scale/grid with the live curve after the old image is gone.

Removed background-only producers: `render-native-furniture.py`,
`render-play-background.py`, `render-mixer-fx-backgrounds.py`,
`render-lfo-native-furniture.py`, and their two C++ offline renderers.
Mixer preview no longer writes runtime backgrounds. Unused Mod demo-background
generators (including hard-coded slot counts/content) were removed from its skin
hook. Native component artwork production remains intact.

Verification: production skin generation and overlay-enabled plugin syntax
compilation. Device deployment/visual result is recorded separately by the
installation task. This document does not claim hardware visual acceptance.
