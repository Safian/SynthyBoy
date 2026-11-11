# Embedded fonts of the bitmap GUI

`Roboto-Regular.ttf` and `Roboto-Bold.ttf`: Roboto 2.137 (2017), Copyright 2011 Google Inc., licensed under the Apache License, Version 2.0 (`LICENSE-Roboto-Apache-2.0.txt`).
They are compiled into the module as data (JUCE `juce_add_binary_data`) and are the default typeface of the bitmap GUI on every platform, so the text is drawn from the same
outlines everywhere and the module needs no system fonts, no fontconfig and no FreeType font directory (the Akai MPC has none).

Apache-2.0 is compatible with the AGPL-3.0-or-later of this project (the combined work is AGPL); the notice is in `THIRD_PARTY.md`.
