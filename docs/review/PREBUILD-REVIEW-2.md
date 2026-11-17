# Pre-Build Review 2

Review of `origin/work/engine-fixes`, `origin/work/open-time`, and `origin/work/play-rows` based on `origin/feature/mpc-overlay`.

## 1. Branch: origin/work/engine-fixes

| File:Line | Code | Issue | Fix | Confidence |
| :--- | :--- | :--- | :--- | :--- |
| `core/engine-own/fx/fx_modules.cpp`:360 | `s.driveDb = V (b, d, drive);` | `drive` is `0..1` normalized in some contexts, but `driveDb` expects dB. | Ensure `drive` scaling matches. | Likely |

## 2. Branch: origin/work/open-time

| File:Line | Code | Issue | Fix | Confidence |
| :--- | :--- | :--- | :--- | :--- |
| `core/src/preset.cpp`:355 | `std::string val (std::min<size_t> (static_cast<size_t> (end - p) + 1, 8192u), '\0');` | String size calculation might be over-optimistic or incorrect in boundary cases. | Tighten bounds check. | Possible |

## 3. Branch: origin/work/play-rows

| File:Line | Code | Issue | Fix | Confidence |
| :--- | :--- | :--- | :--- | :--- |
| `core/filters-serum/src/filter_types.cpp`:... | `static constexpr const char* const kClassicCombStyles[1]` | Array size mismatch with usage (index 6, count 6). | Expand array size. | Certain |
