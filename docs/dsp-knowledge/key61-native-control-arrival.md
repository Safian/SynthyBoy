# Key 61 native control arrival

The comparison uses the stopped Sbtest project and its Acid Saw Reso preset. Package A has ARM binary `132fdb30befc` and skin `85bf0659c20f`. Package B has ARM binary `35093c54c65b` and skin `076840e09f03`. The source, binary, skin, device installation checksums, input state, raw captures and analysis are retained in the shared evidence directory [key61-control-audit-wpt592](../../../.hw-local/mpc-perf/key61-control-audit-wpt592/).

## Rendering and asset ownership

| Resource | What the implementation does |
|---|---|
| Native controls | The MPC host instantiates the TUI and renders the skin assets. The plugin publishes layout selectors, parameter values, dynamic labels and gestures. Host-internal texture or font caching cannot be inferred from the TUI. |
| Backgrounds and filmstrips | The skin now references one canvas for eleven screens and shares identical pan/level strips across their users. The two packages have 625 and 611 referenced PNG names respectively, with 604 distinct contents in each. Every TUI field compared equal when PNG names were replaced by their content hashes. The Q-Link files were byte-identical. |
| Shader programs | The overlay GPU helper compiles and links five shared programs when it creates the context. The controls reuse them. |
| Framebuffers | The helper reuses cached layer targets at the same dimensions. It creates a layer on first use, and recreates the bloom targets when their dimensions change. A scratch RGBA buffer is zero-initialized when allocating a texture. This work is in the helper, outside native control construction. |
| Fonts | The overlay helper uploads the compiled SDF atlas during context initialization. Text layout selects existing glyphs. Rendering the host's native labels belongs to the MPC host. |

Counting one RGBA allocation per referenced PNG name gives 126,798,092 bytes for A and 81,399,916 for B, a reduction of 43.30 MiB. This estimate excludes driver layout, caches and shared storage. Actual process memory is sampled separately.

## Measurement

Each physical touch is injected on the device, including the project selection on cold start. The device clock is read before the touch and after each host-plus-overlay frame. After the sequence, the sampler takes a separate settled reference. The reported time is the first qualifying sample in a persistent three-frame match. It is an upper bound on the actual presentation time. Identical artwork across a transition cannot identify when a new widget was instantiated. Transparent input surfaces have no independent paint time. Animated plots are evaluated separately.

The renderer and lowered CPU cost are different measurements. For the latter, standalone ARM binaries call the production idle native UI poll on the same Key 61. Five blocks of 100,000 calls per install give median thread CPU times of 3.846, 1.284 and 3.834 µs for A/B/A. This saves about 2.56 µs per idle poll (66.6%). It measures the cost of the poll itself. Audio callbacks, host frame presentation and the project loading interval are outside it.

Per-control records include device state, control path, gate visibility, bounds, the changed image region and image-state checks. The package comparison, timing and device resources are reproducible from the raw archives and scripts alongside the [individual observations](../../../.hw-local/mpc-perf/key61-control-audit-wpt592/per-control.csv), [screen timings](../../../.hw-local/mpc-perf/key61-control-audit-wpt592/control-arrival.csv) and [matched comparisons](../../../.hw-local/mpc-perf/key61-control-audit-wpt592/arrival-comparison.csv).
