# Native Keyboard (Component & Overlay)

The native on-screen touch keyboard component for standalone Akai MPC hardware (MPC Key 61, MPC XL), designed for Macro renaming and LFO shape naming without third-party text-loss issues.

Source: `components/native/keyboard/keyboard.py`; catalogue entry: `components/native/keyboard/catalog.json`; skin hook: `scripts/mpc-skin-extras/96_native_keyboard.py`; tests: `scripts/tests/test_native_keyboard.py`.

## Background & Architecture

As proven in [TEXT-ENTRY-NATIVE-KEYBOARD-2026-11-27.md](../review/TEXT-ENTRY-NATIVE-KEYBOARD-2026-11-27.md) and [AGENTS.md](../../AGENTS.md), the host `TextEditOverlay` drops typed text for third-party VST parameters on standalone hardware because `PluginWrapperElement` returns false for the text setter capability (`+0x98`).

The solution is a host-native on-screen keyboard built entirely from shared native `Button`, `Image`, and `Label` components. Each key press dispatches a key code float to a dedicated plugin parameter (`TextEdit Key`). The plugin updates its internal buffer and caret, incrementing the counter on `TextEdit Text`, which causes the host `Label` to re-read and display the updated string immediately.

## Layout & Specifications

- **Overlay Bounds**: Full-screen modal overlay `1280 × 800` (`"native keyboard overlay"`).
- **Panel Bounds**: `(164, 220, 952, 340)` centered horizontally, styled with `surface.panel` and `control.border`.
- **Key Dimensions**: Keys are `40 px` high (`height.intermediate` from `components/sizes.json`) with `8 px` gaps.
- **4 Key Rows (QWERTY layout)**:
  - **Row 1** (`y = 320`): 10 keys (`80 px` wide, `x = 204..996`).
    - Layer 0: `q w e r t y u i o p`
    - Layer 1: `Q W E R T Y U I O P`
    - Layer 2: `1 2 3 4 5 6 7 8 9 0`
  - **Row 2** (`y = 368`): 9 keys (`80 px` wide, indented `44 px`, `x = 248..952`).
    - Layer 0: `a s d f g h j k l`
    - Layer 1: `A S D F G H J K L`
    - Layer 2: `! @ # $ % & * ( )`
  - **Row 3** (`y = 416`): Shift/Layer (`124 px`), 7 keys (`80 px`), Backspace (`124 px`).
    - Layer 0: `SHIFT`, `z x c v b n m`, `BKSP`
    - Layer 1: `SHIFT` (highlighted active), `Z X C V B N M`, `BKSP`
    - Layer 2: `ABC`, `- _ / : ; , .`, `BKSP`
  - **Row 4** (`y = 464`): Layer Toggle (`120 px`), CLEAR (`100 px`), SPACE (`360 px`), CANCEL (`120 px`), OK (`140 px`).
    - Layer 0/1: `?123` switches to symbols.
    - Layer 2: `ABC` switches back to letters.
    - Common controls: `CLEAR` (code 97), `SPACE` (code 1), `CANCEL` (code 99), `OK` (code 98, cyan accent).

## Layer Switching via `Local/3/layer`

Layers are toggled using the official SDK local variable radio group:
- Handle: `Local/3/layer` (3 states: `0` = lower, `1` = upper, `2` = symbols).
- Visibility gating: Components specify `additionalInvalidatingHandles`:
  - `IndexedEnabling/0/3/Local/3/layer` for lowercase keys.
  - `IndexedEnabling/1/3/Local/3/layer` for uppercase keys.
  - `IndexedEnabling/2/3/Local/3/layer` for symbol keys.
- Common keys (`BKSP`, `CLEAR`, `SPACE`, `CANCEL`, `OK`, Title, Text Box) are ungated and remain visible across all layers.

## Key Code Protocol

All key buttons belong to a radio group bound to `ResetOnGestureEnd/Parameter <TextEdit Key>` with `numButtonsInGroup = 100`:
- `0`: Idle (released state, restored automatically by `ResetOnGestureEnd`).
- `1..95`: Printable ASCII characters (`ord(char) - 31`, e.g. Space = 1, '!' = 2, 'A' = 34, 'a' = 66, '~' = 95).
- `96`: Backspace (`BKSP`) - deletes character before caret.
- `97`: Clear (`CLEAR`) - clears input buffer.
- `98`: OK (`OK`) - commits string to target (macro name or LFO shape) and sets context to 0.
- `99`: Cancel (`CANCEL` / Header Close) - discards edits and sets context to 0.

## Plugin-Side Handles (to be added in C++)

1. **`TextEdit Context`** (`kTextEntryContext = kLfoTargetsAction + 1`, offset 963):
   - Range: 0..10 (0 = closed, 1..8 = Macro 1..8, 9 = Shape Save As, 10 = Shape Rename).
   - Display string: Context title (`<=31` bytes, e.g. "NAME MACRO 1").
2. **`TextEdit Key`** (`kTextEntryKey = kTextEntryContext + 1`, offset 964):
   - Range: 0..99 (100 steps).
   - Driven by host touch events; consumed immediately on the audio/parameter thread.
3. **`TextEdit Text`** (`kTextEntryText = kTextEntryKey + 1`, offset 965):
   - Range: 0..1023 (step counter).
   - Display string: Current text buffer + caret (`<=31` bytes).

## Environment Guard & Activation

The keyboard overlay generation is intentionally guarded:
- Environment variable: `SYNTHYBOY_NATIVE_KEYBOARD=1`.
- When unset, `96_native_keyboard.py` returns immediately, ensuring default skin generation and packaged `TUI.json` remain byte-identical.
