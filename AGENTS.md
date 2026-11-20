# AGENTS.md

UI component library (`shell-mpc/ui/`): instructions for AI coding agents are in [shell-mpc/ui/AGENTS.md](shell-mpc/ui/AGENTS.md); human docs in [docs/ui-components/README.md](docs/ui-components/README.md).

## Release packaging and which architecture to build (owner, 2026-11-20)

- A release has TWO zips, built with the Open MPC tool: `armv7` (MPC Key 61 and the other 32-bit devices) and `aarch64` (MPC XL). Both hold the same plugin, with the display layer (overlay) enabled.
- During development and testing build ONLY for the architecture of the device being worked on (Key 61: armv7, XL: aarch64). Both architectures are built together only for a release or a release candidate.
- Open items live in [TODO.md](TODO.md): a finished item is deleted there, the commit message is the history.
