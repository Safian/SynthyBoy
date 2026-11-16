# The names of the eight macros (format change before the first public release, 2026-11-16)

Each macro has a name. The names belong to the sound: they are stored with the macros in a preset (`.sbpreset`) and in the state chunk of a project, and a program / preset change replaces them. The engine's parameter list (`docs/params.json`, 2637 parameters; the shell's 2719) does not change: names are state, not parameters.

## The stored format

| | |
|---|---|
| Member | `macro_names` |
| Where | inside the `content` section of the state document (`{"format":"synthyboy-state","version":4,"params":{...},"content":{...,"macro_names":[...]}}`); the `.sbpreset` holds the same state document, so the member sits at `state.content.macro_names` there; the plugin's state chunk of a project is the same document (the MPC shell adds its own members `mpcProgram` ... at the front and the pad pressure ones at the end, they do not touch it) |
| Value | an array of eight strings, entry i is macro i + 1; `""` is the default name `Macro n` |
| Order | the last member of `content` (after `mod_curves`), written only when at least one macro has a name; a document without names is byte for byte the document of before this change |
| State version | UNCHANGED (4). The member is additive like the content members before it (remaps, spectral filters, instruments, mod curves, shaper curves): see "Compatibility". Reading rule: the member is read from a document of version 3 or later (as the whole `content` section), from an older document a stray member is ignored |
| Code | `StateContent::macroNames[8]` (`core/include/synthyboy/content_state.h`), reader `Parser::readMacroNames` and writer `putContent` in `core/src/state.cpp`, the cleaning `sanitizeMacroName` in `core/src/content_state.cpp` |

Example: `"content":{"macro_names":["Filter Sweep","","Cutoff","Detune + Chorus","","Reso","",""]}`.

### What a name is

- Any printable text, at most **16 characters** (Unicode code points; UTF-8 in the document, at most 64 bytes). `sanitizeMacroName` makes the form that is kept: invalid UTF-8 is dropped (stray / truncated / overlong / surrogate bytes), control characters (C0, DEL, C1) and the invisible format characters (U+200B .. U+200F, U+2028 .. U+202E, U+2060 .. U+2064, U+2066 .. U+2069, U+FEFF, no-break space, ideographic space) become a space, runs of spaces become one, spaces at both ends go, the text is cut after 16 characters (never inside a UTF-8 sequence) and trimmed again. Idempotent. An empty result, or the default name of its OWN macro (`Macro 3` for macro 3), is `""`; `Macro 5` as the name of macro 3 is a name.
- Quotes, backslashes and the other characters JSON escapes are escaped by the writer (`\"`, `\\`, `\n`, `\u00xx`) and decoded by the reader; non-ASCII text is written as UTF-8 (the reader also takes `\uXXXX`). The text has no number in it that depends on the locale (the document is written with `%.9g` through the locale independent formatter, as before).
- The reader never rejects a document or the rest of the content because of a name: an entry that is not a string is the default name, entries after the eighth and a member that is not an array are ignored, a text that is too long is cut. The writer cleans a name that was set in another form (a program that put a tab in a name writes a space).
- A load replaces the names: a document without the member (every document written before this change) gives the default names, and the names of the previous patch do not leak into it (`loadStateWithContent` clears the content first, as it does for every part of it). `loadState` without content skips the section as before. Init (`StateContent {}`) resets them. A rejected content section (outside its caps) is dropped as a whole, with the names in it, the parameters still apply (unchanged rule).

### Why 16 characters

The limit comes from the places the name has to fit, narrowest first:

1. **the chip on a knob** (`ModEntry::name`, 6 bytes + the end): not the whole name but a short form (below), so the chip does not limit the name;
2. **the macro card of the Macros page**: the name sits beside the dial in a text column of about 166 px at 15 px (card 310 px wide); the library fits a label into its box by shrinking it down to 9 px and then cuts it with an ellipsis. Sixteen of the widest letters (`WWWWWWWWWWWWWWWW`) still fit at the smallest size without an ellipsis (gallery picture `Named_macros.png`); more would be cut;
3. **what the host shows for a parameter name**: the shell answers `effGetParamName` into a buffer of 64 bytes (the proxies report the names of their targets, up to 25 characters of table names such as `Osc 1 Unison Detune Curve` already); 16 characters take at most 64 bytes, the shell cuts at a character boundary. The knob displays of the MPC XL and the Key 61's pop-up were not measured with long names (no device access in this work): 16 characters is below the longest parameter names that are shown today;
4. the label buffer of a library knob (23 bytes): a name of non-ASCII characters can be longer than that in bytes, `setText` cuts it at a character.

### Short form (chips, rows of the modulations page)

At most six bytes: `MAC n` for a macro without a name, else the name cut at a character boundary after the first letters that fit, at the end of a word when the cut would fall inside one and at least three letters come before it, without trailing blanks and signs: `Filter Sweep` -> `Filter`, `Big Wobble` -> `Big`, `Cutoff` -> `Cutoff`, `Resonance` -> `Resona`, `Drive + Warp` -> `Drive` (`MacroNames::shortForm`, `shell-mpc/overlay/macro_names.h`). A chip, a row and a board chip of a macro keep the colour of the macros whatever the name says (`ModEntry::macro`).

## Compatibility (each shown by a test)

| Case | Result | Test |
|---|---|---|
| A preset / chunk written before this change | loads with the default names (and clears the names of the patch before) | `core/tests/test_macro_names.cpp` `a_document_before_the_names_loads_with_the_default_names_and_a_load_replaces_the_names`, `shell-mpc/tests/macro_names_host.cpp` (chunk of before, program change) |
| A document with names read by the code as it was BEFORE this change (feature/mpc-overlay `a9251b1b`) | the state parser skips an unknown member of the document and of `content` (checked with a throw-away program linked against the old library: a new state and a new `.sbpreset` with a custom LFO shape and names load with status `ok`, the parameters and the LFO shape applied, the names ignored; `loadState` and `loadPreset` without content too) | the same check is in the repository as the unknown member tests of `test_macro_names.cpp` (`a_reader_that_does_not_know_the_member_skips_it_and_keeps_the_rest`) |
| What an old build does with a new preset | loads the sound, shows no error, ignores the names; macros are called Macro n; if the user saves the project the names are gone (an old build writes what it knows) | above |
| A name with quotes, backslashes, non-ASCII characters, control characters, too many characters | escaped on write, decoded on read, cleaned and cut both ways | `names_that_need_escaping_survive_and_dirty_ones_are_cleaned_both_ways`, `default_names_and_the_cleaning_rules`, `macro_names_host.cpp` (chunk round trip of an escaped name) |
| An empty name | the default | `default_names_and_the_cleaning_rules`, `macro_names_host.cpp` (rename to "") |
| Locale | the text is the same under another `LC_ALL` | `the_text_is_locale_independent` |

Why the state version stays 4: a bump would make every build before this change report `newer_version` for every document written after it (still loaded, but flagged), would change every `"version":4` literal of the tests, scripts and fixtures, and would buy nothing, because the content section is additive by design (ARCHITECTURE 5.2) and a document without names is unchanged byte for byte. The rule for the next change of the member: its shape is fixed by its name, a different shape gets a different member name.

## Where the name shows

- **Macros page**: the title of each card, in capitals (`FILTER SWEEP`; `MACRO n` without a name), underlined: the name is a button (below). The title of a macro's targets page is the name too.
- **Mod page**: the source board (the macro chips), the source and target pickers, the source of every matrix row, the slot detail page; the board is filled again when the names change.
- **Chips on knobs** of every page (oscillator, filter, envelope, LFO, mixer, FX, master, voice pages and the compact views of the Play page) and the rows of the modulations page: the short form.
- **The Q proxy** whose target is a macro reports the name as its parameter name (`effGetParamName`, 64 bytes, cut at a character): the hardware knob displays of the XL and the Key 61's Q-Link pop-up show `Filter Sweep` instead of `Macro 1`. The host is told (`audioMasterUpdateDisplay`, the path of the group changes) when a rename or a program / preset / chunk load brings other names. The macro's own parameter keeps its table name `Macro n` (the parameter list does not change). The status file's Q lines print table indices, not names, so they did not change.

## Editing on the device

A tap on the name on a macro card opens the keyboard page (`MacroUi::kPageKeyboard`, a detail page of the page stack; `shell-mpc/ui/text_entry.h`, `ui::TextEntry`): the title bar, the text field with the caret at its end and the count `n/16`, a QWERTY layout (shift: tap = next letter a capital, again = caps lock, again = off; an empty text starts with shift on), a second layer (`?123` / `ABC`) with the digits and `- + / & ' ( ) # % _ . , ! ? : * =`, SPACE, backspace (a left arrow, takes a whole character), CLEAR, CANCEL and OK. Keys are 72 px high, at least 56 px wide, and the five rows use the whole width. A key acts when the finger lifts on it (a finger that slides off does nothing); a key that would exceed 16 characters does nothing. OK stores the trimmed name through `IInstanceView::setMacroName` (an empty text is the default name); CANCEL, BACK (any other way the page leaves the stack) and a tab change leave the name as it was. The Q-Links publish no group on the page (all sixteen slots without a target). A mouse click is a tap.

A tap on the name is the name's; a drag that starts on it is the knob's (it is replayed from where it began), so a macro can still be turned from the whole card.

The plugin applies a rename on its display thread (`PluginInstance::macroNamePoll`): the content under `contentMutex`, the overlay's copy (`publishOverlayContent`), the host told. The engine is not touched (names are not sound). Test hook: `SYNTHYBOY_MPC_MACRO_NAME_FILE` names a file whose line `n name` is such a rename (the host tests have no pages), read only when the variable is set.

## Factory presets

`scripts/name-preset-macros.py` derives a name for every macro of the 112 presets that drives something (a mod slot with a macro as source, a destination, an amount that is not 0, not bypassed) and keeps the default for the others. The short name of a target comes from the parameter table (`Filter 1 Cutoff` -> `Cutoff`, `Filter 1 Resonance` -> `Reso`, `Osc 1 Warp Amount` -> `Warp`, `Osc 1 Unison Detune` -> `Detune`, `LFO 1 Rate` -> `LFO Rate`, ...; a few are written out in `SHORT_NAMES`, each checked against `docs/params.json`). One target (or targets of one short name): that name; two: `A + B` when it fits 16 characters, else the module both belong to, else the two modules, else `A +`; more: the short name most of them share, else their modules (two at most), else the module most of them belong to, else the default name. The names are data, `scripts/factory-preset-macro-names.json` (an entry that exists is kept when the script runs again: correct a name there), and `scripts/gen-factory-presets.py` writes them into the presets and the pack manifest (`python3 scripts/name-preset-macros.py --write`); `synthyboy_factory_presets_reproducible` and `scripts/tests/test_name_preset_macros.py` check them. The list of preset, macro, targets and name for correction: `.hw-local/mpc-ui-plans/macro-names-proposed.md`.
