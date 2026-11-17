# TextEntry

## What it is
An on-screen keyboard page: a title, a text field with a caret, a QWERTY layer with shift / caps and a digit / symbol layer, SPACE, backspace, CLEAR, CANCEL, OK. The host offers no text entry to a plugin-drawn page, so this is the library's own. Use it to name something (a macro).

## Header and types
`shell-mpc/ui/text_entry.h`: `TextEntry`, `TextEntry::Key`, `TextEntry::Layer { Letters, Symbols }`, `TextEntry::Shift { Off, Once, Caps }`, `TextEntry::KeyKind { Char, ShiftKey, LayerKey, Backspace, Space, Clear, Cancel, Ok }`.

## State fields
No public state struct: the entry is started with
`begin (title, initial, maxChars, okId, cancelId)`.
Readers: `text()` (`const std::string&`, UTF-8), `chars()`, `maxChars()`, `layer()`, `shift()`, `title()`, `numKeys()`, `key (i)`, `fieldRect()`, `keyAt (x, y)`. Test helpers: `keyOf (c)`, `keyOfKind (k)`, `press (i)`.
Constants: `kBarH` 64, `kFieldH` 64, `kKeyH` 72, `kGap` 8, `kMargin` 16, `kRows` 5, `kCols` 20, `kMaxKeys` 64.

## Size
`minSize()` = 1280 x 536. The keys fill the whole width; every key is at least 56 x 56 (rows 72 px high). Place it over the whole tab (1280 x 628).

## Touch and knob behaviour
| Gesture | Result |
|---|---|
| tap on a character key | the character is appended at the caret (the end), if under `maxChars` characters (UTF-8 code points) |
| shift, layer, SPACE, backspace, CLEAR | edit the text or the layers; no change out |
| OK | `Request` with `okId` |
| CANCEL | `Request` with `cancelId` |
| finger slides off the key | cancels that key press |

A key acts when the finger lifts on it. A mouse click works the same. No `turn`.

## Q-Link
Not a Q member.

## Options and variants
An empty initial text starts with shift on (a name begins with a capital).

## Minimal example
```cpp
TextEntry keyboard;
keyboard.place ({ 0, 110, 1280, 628 });
keyboard.begin ("NAME THE MACRO", currentName, 12, kOk, kCancel);
// events to it while it is the top page:
Change c = keyboard.touchDown (x, y, nowMs);   // the key is pressed
c = keyboard.touchUp (x, y);                   // ... and acts here
if (c.kind == ChangeKind::Request && c.id == kOk)
    store (keyboard.text());                   // the owner trims and stores it
```

## Used on
`shell-mpc/overlay/macro_ui.h`: renaming a macro (a tap on the name beside a macro knob, `MacroCard::nameId`).

## Limits and pitfalls
- `text()` is the owner's to trim and validate; the component enforces only the character limit.
- A second `begin` replaces the text.
- Limit counts characters, not bytes; size your storage for UTF-8.
