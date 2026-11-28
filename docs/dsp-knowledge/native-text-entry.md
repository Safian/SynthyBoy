# Factory text entry and external plugin parameters

## Open requirement

Replace the Macro name and LFO shape Save As/Rename `ui::TextEntry` with the
factory keyboard once a real arbitrary-string round trip exists. Keep the working
keyboard until initial text, UTF-8 OK, cancel, stale context rejection, return to
the owning editor and persistence can be verified. No device deployment or input
experiment was performed in this investigation.

## XL hardware input (official source, opened 2026-11-24)

The MPC XL / Live III v3.10.1 User Guide, printed p387, documents the XL's physical
**Numeric Keypad**: when the selected field is a number, its numbered buttons enter
a value and keypad Enter commits; an onscreen numeric keypad with additional
operations also appears. Printed p40 documents XL centre cursor/Enter as an
alternative to double-tapping a selected control. This is device-specific numeric
input evidence; it does not document a plugin arbitrary-text return channel.

Source PDF SHA-256: `bd10926445a159597dcfdc1b4234d8c69fc5f8c1e04bc610dbb81daa6f2970ba`.

## Actual factory definition (read-only source inspection, 2026-11-24)

On the XL, `/usr/share/Akai/SME0/Resources/Pages/Generic/Generic Text Overlay.json`
and `Program Edit/Components/Generic Text Overlay.json` define `TextEditOverlay`
with `handleName: Data`, `withNumbers: false`, and a `Focus Lost` action.
`Program Edit/Midi/TUI.json` imports it; its button definition uses `Show Overlay`
with `additionalData: text edit overlay` for `Enter Pressed`, `Double Click` and
`Mouse Down`. Its instances remap Data to `PageIndex/Set/1, 1` etc. This proves
factory invocation syntax, not a plugin string setter.

## Actual XL host string setter gate (offline binary inspection, 2026-11-24)

The installed `/usr/bin/MPC` was copied off the device without attaching a debugger
or changing the device. ELF `.note.mpc.version`: **3.10.1.12 (f088e9c8427)**;
GNU build ID `8b28179d89277a5f73c30be90740766e6166b837`; SHA-256
`bd55df49a40bc030da13b67578d5bf88a33abd0b4b9fbdc1da7bc3db00bc61aa`.
Raw factory JSON, extracted disassembly/vtables, identity and verification output:
shared `.hw-local/mpc-perf/native-text-contract-xl-20261124/` (outside this repository).
The full host executable is not stored in the repository or the evidence directory.
`verify_contract.py` checked the source ELF relocation targets and instructions;
its PASS result and the separate input-filter base-RTTI check are retained there.
This is offline structural verification, not a hardware callback test.

Offsets below are ELF virtual addresses for that exact binary, not reusable APIs.
The type identities come from RTTI and vtables from ELF relative relocations.

- `TouchConnectableTextEditorOverlay` construction at `0x381f0e0` binds the Data
  handle. Its submitted-text callback at `0x381e72c` calls `0x1cba62c` at
  `0x381e7e0` before closing the overlay.
- The handle helper at `0x1cba62c` calls element vtable slot `+0x98` at
  `0x1cba66c`. A false result skips the write. A true result calls the string
  setter in slot `+0xa8` at `0x1cba69c`, passing the submitted JUCE string.
- `PluginWrapperElement`, RTTI `0x7c36338`, primary vtable `0x7c36438`, has
  `+0x98 = 0x1cb7bc0` (constant false) and `+0xa8 = 0x1cb7bf0` (immediate return).
- `ProgramEditParameterElement`, RTTI `0x7c65a80`, vtable `0x7c65aa8`, has the same
  false capability and no-op setter. These are fixed host implementations; a
  plugin parameter flag or a plugin-side dispatcher change cannot alter them.
- `NameSetterDataAdapter` is an input filter, not a string-return extension:
  its RTTI at `0x7d79750` has the single-inheritance base `0x7bc44d0`, identified
  as `juce::TextEditor::InputFilter`. Construction at `0x381f3f4..0x381f434`
  installs it on the text editor through `0x1245008`, with ownership false.
  Its `filterNewText` implementation at `0x381f760` forwards to the supplied
  object's vtable `+0x28`. The [official JUCE InputFilter reference](https://docs.juce.com/master/classjuce_1_1TextEditor_1_1InputFilter.html)
  describes filtering text as it enters the editor (opened 2026-11-24).
  The actual submitted-text write remains the separate gate above.


**Conclusion limited to the inspected path:** binding this factory overlay to the
normal external-plugin parameter element cannot deliver submitted text to
`effString2Parameter`; the host returns before invoking any plugin setter.
This is stronger evidence than the SDK reference's text-valued-handle warning.
It is not proof that every undocumented host extension is impossible, nor an
on-device round-trip test. No Key 61 binary was inspected here.

## Exact prerequisite for replacement

A supported host binding must expose an external-plugin-owned text handle whose
string capability is true, initial text is supplied by the plugin and whose
string setter returns submitted UTF-8 to that same plugin instance. Alternatively,
a verified existing host extension must provide that equivalent contract. No such
binding was found in the inspected factory resources or SDK. Changing only
`effString2Parameter`, creating a JSON overlay, or using the XL numeric keypad
cannot bridge the demonstrated host-side false gate. Host patching is outside
this plugin change and was not attempted. Program/preset-name hijacking and
per-character numeric transports are not substitutes for the requested contract.

The local SDK `framework/mpc_plugin.h` currently parses `effString2Parameter` as
choice/numeric input, and ignores `effSetProgramName`. These facts alone are not
the blocker: the missing link is the host's plugin-owned **string-valued handle**.

## Independent mpc-expert review (2026-11-24)

The updated read-only Sonnet `mpc-expert` role independently read the preserved
factory JSON, disassembly, vtable verification, current SDK and model-correct
v3.10.1 manuals. It confirmed the narrow XL 3.10.1.12 parameter-element gate and
the InputFilter identity; it found no supported alternative string-return handle
in the inspected SDK. It did not run another ELF check or a device round trip.
Its conclusion remains scoped to the inspected path: a documented alternative
callback or a demonstrated full string returning to the owning plugin instance
would change the result. The physical numeric keypad is documented separately.

## Expert review 2026-11-28: every host text path to a third-party VST2 (sources only, no device)

Question: can the host's own keyboard (Key 61, XL) deliver text to SynthyBoy, so
`ui/text_entry.h` can go? SDK commit `febd2ca5` (active `ext/mpc-sdk`); manual v3.10.1.
Verdict per path (FACT = documented or seen in source; HYPOTHESIS = unproven):

1. **Skin `TextEditOverlay` on a plugin parameter: NOT USABLE on XL, UNKNOWN on Key 61.**
   FACT: SDK `docs/ui-reference.md:1201-1206` (6.12, "less tested"): meaningful only for
   text-valued handles, "which third-party VST parameters are not"; parameters are floats
   0..1 (`docs/host-contract.md:55`). FACT: XL 3.10.1.12 gate above (setter slot is a no-op).
   HYPOTHESIS: Key 61 3.10.1.x has the same code; no binary inspected, no on-device run.
2. **`effString2Parameter`: NOT USABLE.** FACT: `docs/host-contract.md:59` "it does not call
   the text-to-value opcode either"; SDK `framework/mpc_plugin.h:679` parses it as
   choice/number only. The host never sends it, so a text parameter display cannot take input.
3. **Program/preset name: NOT USABLE as a channel.** FACT: the host only reads program names
   (`effGetProgramNameIndexed`/`effGetProgramName`, `host-contract.md:33`, `tools/host-sim/README.md:88`);
   `framework/mpc_plugin.h:662` ignores `effSetProgramName`; the community wrapper
   (`docs-community/open-mpc/wrapper/vst2_wrap.c:927-930`) does not handle opcode 4 either,
   and its NOTES never record a host call to it (absence of evidence only). The manual's
   "Save Plugin Preset" is the disk icon on the Plugin Track page (v3.10.1 XL manual p278,
   txt line 7702): the host writes an `.xpl` with its own `<preset>` name and the plugin's
   state chunk (`ext/mpc-sdk/docs/presets.md:17-37`); the plugin only sees `effGetChunk`,
   never the name. FACT: a skin can trigger only four handlers: `Q-Link`, `Show Overlay`,
   `Toggle Switch`, `Focus Lost` (`ui-reference.md:512-517`); none opens the save dialog.
   HYPOTHESIS (unproven either way): the host might call `effSetProgramName` on a rename;
   nothing documents such a rename UI for plugin programs.
4. **Host-internal rename keyboards (track/program/macro/MIDI-CC names): NOT USABLE.** FACT:
   the manual lists many (p279 MIDI CC rename, p343 macro rename), all host-owned names.
   The XL factory MIDI page uses a host-side name element (`ParameterMapNameSetter`,
   `vtables.txt`) that has a working string setter; there is no skin handle that binds it to
   a plugin (`ui-reference.md` section 7.1 lists no text handle).
5. **Plugin-drawn keyboard from skin Buttons: USABLE, proven in the community.** FACT
   (community, Live III OS 3.9.1, 2026-10-30): the OpenMPC Plugin Manager types text with a
   keyboard drawn from skin Buttons and display-string Labels (`docs-community/open-mpc/docs/NOTES.md:136`
   "dynamic text works through parameter display strings"; `NOTES.md:2054`, `PORTING.md:119`).
   FACT, important for our plan: there each tap arrived as TWO `set key=1` events 30-140 ms
   apart with no `=0` between; the fix is a per-key debounce (250 ms) in the plugin. Our
   REPORT (`textentry-research`) assumed a clean `k, 0` pair from `ResetOnGestureEnd`; that
   is unproven and the community evidence argues for a plugin-side debounce plus a
   no-edge-reliance design. Same-key repeats within the debounce window are then lost
   (accept, or alternate two key parameters).

**Minimal measurement that would settle the only open point (Key 61, point 1):** a throw-away
SDK plugin with one float parameter, skin Button with `Show Overlay` / additionalData
`text edit overlay` bound to it (factory syntax, `midi-TUI.json:193-209`); open it, type, Enter;
count `setParameter` and opcode 27 calls in a log. Expected from the XL result: zero calls.
It is not needed for the decision: path 5 does not depend on it.

**Recommendation:** no host-provided text path reaches a third-party plugin; keep text entry
plugin-side. The host keyboard cannot replace `ui/text_entry.h` on the XL (fact) and no
source shows it on the Key 61; the only supported route is the Button/Label keyboard of path 5.
