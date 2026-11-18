# Prompt template: ask a local model for wavetable recipes

The recipe schema, the list of building blocks with their parameter ranges, three worked examples and the per-category guidance are in one place: **`.agents/skills/wavetable-lab/SKILL.md`**, section "Recipe JSON" and "Inventing recipes". Paste that section (from "## Recipe JSON" to the end of "Inventing recipes") into the model's context first, then this request:

```
You write wavetable recipes for a synthesizer, in the JSON format described above.
Write {N} NEW recipes for the category "{CATEGORY}". Each one must be a different idea (different building block or a different scan), with a title that describes the sound
(plain words, never the name of a commercial product) and a one sentence description of what the scan does from the first to the last frame.
Rules: use only the node types and parameters listed above and keep every value inside its range; every recipe has "schema", "title", "category": "{CATEGORY}", "description", "frames": 64,
"seed" (any short text) and "source"; moving parameters use a law ("linear", "curve", "steps", "lfo" or "keys"); the first and the last frame must clearly differ.
Answer with ONE JSON list of {N} recipe objects and nothing else: no explanation, no markdown.
```

Then:

1. Save the answer to a text file (prose around the JSON is tolerated).
2. `python3 scripts/wavetable-lab.py propose --from-file answer.txt --category {CATEGORY}` validates and clamps; it prints every error of a rejected recipe (paste those back to the model to get a corrected version).
3. `python3 scripts/wavetable-lab.py score --run <run folder>` and `preview --run <run folder>` rate the proposals the same way as the random candidates, so the owner can listen to both side by side.
