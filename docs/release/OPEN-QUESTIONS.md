# SynthyBoy - Open Questions, Unverified Items, Known Issues (MPC release)

Hand-written list; every entry names the file that holds the detail, and its state. Fixed bugs are not listed. The working list of open tasks is [TODO.md](../../TODO.md); the device checks are in [DEVICE-CHECKLIST.md](DEVICE-CHECKLIST.md).

## Release review observations

| # | Item | State | Source |
| --- | --- | --- | --- |
| A1 | Key 61: the plugin's picture flickered around the host's Q-Link Status pop-up. The cause was found (three faults of the key) and fixed in the code; verified by host tests only | **Open until seen on the device**; user-side workaround if it comes back: switch the host's pop-up off; not confirmed fixed on the device | docs/MPC.md, b0b8cc69 |
| A2 | Stepped Q targets (toggles, lists) driven by accumulated travel: the host's write behaviour is measured on the Key 61 (play log); the increments of a quarter turn of a knob and the MPC XL are not | Not measured on the XL; the quarter turn is to be read from the status file (`turned`) on a device | docs/MPC.mdac74a3 |
| A3 | Q-Link behaviour on the MPC XL: 16 knobs at once, the chips 5 .. 16, the names on the XL's displays | Not measured | docs/MPC.md |
| A4 | Device rule (4 or 16 knobs): fallback by the architecture of the build when the device name is unreadable | A GUESS, not measured on any device (the XL's name rule is measured) | docs/MPC.md, ecb776ad |
| A5 | USB mouse: the XL's pointer plane (hot spot) was not measured; the whole chain (reader, click, wheel on a page) was not run on the Key 61 | Not verified on a device | docs/MPC.md |
| A6 | Plane order under the host's pointer: the rule (our plane under the pointer plane where `zpos` is mutable) | Not verified on a device (the XL's plane table was measured, the rule was tested with that table and a fake driver) | docs/MPC.mdc0654 |
| A7 | Pad pressure: how the opening of the filter sounds from a pad, whether the default amount is right on either device, the CPU of the layer with a full pad bank | Not measured on a device | docs/MPC.md |
| A8 | MPCe Q-Link mode from a drum track to plugin parameters, 16 Levels, Note Repeat, pad lighting from a plugin | Not measured | docs/MPC.md |
| A9 | Host side of programs and chunk: in which order the host restores program and chunk; the CPU load during playback in the MPC app; the sound of the build at the time (`17d6e11`) | Not observed / not verified | docs/MPC.md |
| A10 | The MPC XL (the stride 1 trial, the arm64 helper), the hardware Q-Link buttons, the sound of a loaded sample or instrument | Not verified (later XL measurements exist in other lines of the same file; this line is the old statement) | docs/MPC.md |
| A11 | Two plugin instances on two real tracks on a device | The test with fakes exists; the device test is open | docs/MPC.md |
| A13 | Tapping the browser tile to open the preset list | Not measured (the package has no `.xpl` presets) | docs/MPC.md |
| A14 | That a plugin cannot change the host's page ("it cannot" rests on the SDK documents and the interface, not on a test) | Not measured | docs/MPC.md |
| A15 | `.xpl` generation: the shared set (XL plugin path) loads on the Key 61; the same set on the MPC XL itself after a package install | Verified on the Key 61 (the list appears, the presets load, the path need not match the device); not verified on the XL | docs/RELEASE.md |
| A18 | Name clearance for a public release (trademark registers, plugin databases, stores, domains) | Owner / counsel, not done | docs/NAMING.md |
| A19 | MTS-ESP library licence: believed permissive, not verified | Open | THIRD_PARTY.md |

Not listed because it is fixed:
- The Key 61 "foreign" Q-Link assignments (a project made with an older layout keeps its Q-Link map per track; a user-side fix, described in docs/MPC.md and in the user guide).
