# MPC expert startup and lexical retrieval

`scripts/mpc_expert_knowledge.py` is the executable startup hook. Its `start QUERY --prepare` builds the first local index if needed, starts a detached refresh on every startup (an active refresh is deduplicated by the lock), and immediately returns citations from the last complete SQLite FTS5 index. `ask QUERY` supplies that context to `claude --agent mpc-expert --model sonnet -p`; `ask QUERY --prepare` supports an authenticated Codex/other reader without Claude CLI authentication. This is lexical retrieval, not semantic embeddings or proof that the retrieved claim applies to every model.

Global options precede the command: `--root KNOWLEDGE --project REPO --registry JSON --max-age SECONDS`. Commands: `search QUERY --limit 6`, `index`, `refresh`, `refresh --offline`, `status`, `start QUERY --prepare`, `ask QUERY [--prepare]`.

Runtime databases, locks, source results and the bounded latest refresh log live under the knowledge root `.runtime/`, never in the repository. `index` and refresh share a nonblocking process lock. Incremental indexing fingerprints documents, removes deleted records and replaces a complete database atomically. Existing read connections retain their old snapshot. Failed downloads preserve the previous source and index; individual source failures are reported in `status`. A failed index build preserves the complete previous index.

The checked-in source registry explicitly identifies official versioned manuals, release notes, the public SDK archive and Open MPC workflow. Downloads have per-source size limits and a 20-second socket timeout, with at most two simultaneous sources. PDF extraction preserves numbered physical pages as `=====PAGE N`; these are PDF page numbers, not necessarily printed page numbers. Existing unchanged PDFs/text are preserved. SDK refresh stores Markdown/text reference documentation in its own generated snapshot; it never changes the active SDK or existing snapshot. Source SHA256, URL, ETag and Last-Modified are recorded when supplied by the server. The runtime source inventory is generated in `findings/mpc-expert-runtime-sources.md`; the curated current source manifest remains authoritative for model/version selection.

Official XL and Key 61 product pages are checked for direct inMusic CDN PDF links; discoveries and missing direct links are recorded in status. The registry is a bounded known-source refresh, not a global crawler: up to eight discovered MPC user guides/manuals/release notes per page are automatically downloaded from the official CDN, page-extracted and indexed. Discovered PDFs live under `sources/runtime-discovered-manuals/`, preserving curated canonical manuals. The original filename preserves version identity; old differently versioned files remain available. Discovery metadata records each URL, SHA and extracted header version when present. These discoveries do not automatically override curated model/version facts. Conditional ETag/Last-Modified requests avoid unchanged downloads when supported. A successful check of a versioned URL does not prove it is the newest release. The expert still uses official product/download pages and web search for newly published versions and updates the curated source inventory with evidence.

Retrieval includes knowledge Markdown/text, the curated source inventory, and explicitly selected project MPC topic files. It excludes symlink traversal, private-do-not-publish, assets, userxpl, third-party/vendor folders, hidden directories and secret-named files. It does not index binary assets or third-party DSP source. Retrieved excerpts retain the absolute file and section/PDF page citation; inspect the underlying evidence before changing host integration.

Verification: `python3 -m unittest discover -s scripts/tests -p test_mpc_expert_knowledge.py`. Offline local preparation: `python3 scripts/mpc_expert_knowledge.py refresh --offline` followed by `python3 scripts/mpc_expert_knowledge.py search 'numeric keypad'`. No device or product build is involved.

Local installed entry point: `/Users/szabi/.local/bin/mpc-expert` forwards to this
checkout's script. The shared role configuration is
`/Users/szabi/Documents/Projects/Safian/.claude/agents/mpc-expert.md`; it calls
`start QUERY --prepare` before answering and rechecks retrieval if the background
index completed. Native Codex Sonnet workers can use the same role/context when
the separate Claude CLI is not authenticated; `ask` itself requires that CLI's
valid authentication.

Integration verified 2026-11-24: canonical startup returned the XL manual page 387
immediately, launched the real refresh process, then reported eight successful
sources and 281 documents/4243 chunks. The three current PDF metadata versions
were 3.10.1. These are observed snapshot counts, not permanent expected totals.
