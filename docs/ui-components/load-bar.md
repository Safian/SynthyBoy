# LoadBar

## What it is
A component that displays load progress. It draws a progress bar in the amber color of the existing loading mark, with text `LOADING n %`.

It has two forms:
- **Compact**: A solid amber bar (total is 0 / unknown).
- **Progress**: A bar filled to `percent` with the rest dimmed.

## Header and types
`shell-mpc/ui/load_bar.h`: `LoadBar` (`LoadBarData`).

## State fields
`LoadBarData`:
- `percent` (`uint32_t`, 0..100)
- `total` (`uint32_t`, 0 = unknown)
- `active` (`bool`)
- `label_id` (`uint32_t`, id into owner-supplied table - not used by the component)

## Size
Depends on the rectangle it is placed into.

## Touch and knob behaviour
None, it is a display-only component.

## Minimal example
```cpp
LoadBar b;
b.place({10, 10, 200, 20});
b.state = { 37, 100, true, 1 }; // 37% loaded
b.draw(sb);
```

## Used on
Background loading of content, e.g., wavetable preparation, sample loading.

## Limits and pitfalls
- Stateless: stores no state of its own; no animation clock inside.
- The amber color is fixed to the existing loading mark color (`0xFFB000`).
