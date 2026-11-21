# Test recipes

- **Test first, and prove it fails:** every change lands as a pair: the test that fails on the
  old code (compile-fail counts, say so), then the change. A test that passes before the fix
  proves nothing.
- **Signals must not hide the defect:** no signal whose zero crossings or whole periods line up
  with the defect (a chirp's zero crossings fooled the granular loop test twice); the defect's
  effect must be at least 10x the threshold.
- **Ratios are not proofs:** "RMS ratio < 0.97" depended on phases; prove silence with exact
  zeros. (own_spectral_a_waiting_voice_is_silent, 2026-11-21)
- **Read inside the render:** a test read 64 frames past its render end and was flaky with heap
  garbage; run suspicious tests with `MallocScribble=1`.
- **Checksums:** `*-CHECKSUM` lines are compared before/after by the build worker; a change that
  must not change sound must leave every line identical; intended changes list their lines.
- **Bit identity across compiles:** `-ffp-contract=off` (see platform file).
- **Batch builds:** workers write, one build worker builds the merged tree once; phase A (old
  code, new tests must fail) and phase B (all pass).
