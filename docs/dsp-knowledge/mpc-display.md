# MPC display path

## Facts (XL, 2026-11-20/21)

- The host commits frames from **one thread** (its main thread), with atomic commits:
  frame commits (flags 0x1 PAGE_FLIP_EVENT, user data set, 15-23 property items) and cursor
  commits (0x200 NONBLOCK). A real frame commit takes a median 13-16 ms (it waits for vsync).
  `measured: tools/xl-commit-shim, .hw-local/mpc-perf/xl-commit-shim/`
- **The host presents only on UI events:** 0 frame commits per second when idle; a tab tap
  gives a burst of about 5 commits within 0.4 s. Anything that animates on its own cannot rely
  on host frames. `measured: stage3-live-2026-11-21.log`
- **Merging our plane into the host's commit works:** 45 merged commits, all ret 0, no extra
  frame time (median 14.7-15.0 ms vs 15-16 ms). `measured: same log`
- **Our overlay helper already owns plane 46 (XL) / 38 (Key 61)** via SETPLANE. Two writers on
  one plane fight: the test picture showed only on some pages. One plane needs one writer.
- **A plane is above the host's primary plane:** merging into the host commit gives the host's
  timing (page change and our layer change in the same frame), NOT host z-order (host popups
  still cannot cover a plane above them).
- **Drawing:** two persistent layer textures, per-cell damage, live ops drawn every picture
  (protocol 8); all live ops of a page must be one contiguous pass after the static cells
  (`shell-mpc/overlay/gpu/cell_plan.h`). Plan: `docs/plans/overlay-static-anim-layers.md`.

## Decisions

- g117: full takeover, one plane, one writer: the shim merges the helper's finished buffer
  into the next host frame commit; if the host does not present within 20 ms the helper
  presents itself. Host TEST_ONLY commits are never merged.
