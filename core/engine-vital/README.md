Vital-derived code lives only here; see docs/ARCHITECTURE.md §2.2.

This directory is intentionally empty in Phase 1. The CMake option `SYNTHYBOY_WITH_VITAL` (default ON) will select it
once the Vital take-over (Phase 2) happens; the iOS build forces it OFF. Nothing outside this directory may contain
Vital-derived code.
