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

## Plane stacking facts (measured read-only 2026-11-21, plugin page open)

- Primary plane FB format is **XR24 (no alpha)** on both devices: XL plane 34 (JUCE message thread allocates, 800x1280, pitch 3200), Key 61 plane 33. Source: `/sys/kernel/debug/dri/0/state` and `drmprobe info`.
- The host writes alpha byte **255 in every pixel** of its primary buffer (drmgrab dump of both devices, 1,024,000 of 1,024,000 pixels). With XR24 the plane scans out opaque in any case, so a hole in the host's skin background cannot be transparent.
- XL (RK3588 VOP2): planes have `zpos` (range, mutable; primary 0, cursor plane 40 = 1), `alpha` (65535) and `pixel blend mode` (value 0) properties. Key 61 (RK3288 VOP): planes 33/35/38/40 have NO zpos, alpha or blend property; order is fixed (primary 33 lowest, our plane 38 above it).
- Consequence: an under-plane design needs the primary to be non-opaque (the shim would have to re-wrap the host buffer as AR24, which only works if the host writes alpha 0 in the holes; it writes 255 today) and, on the Key 61, is impossible because the primary cannot be moved above our plane. Open: whether XL accepts the AR24 re-wrap (`ADDFB2` on the same GEM handle) and what the blend mode value 0 means in the enum list.

## Host filmstrip cache (XL, read-only, 2026-11-21)

- The MPC process does NOT map `/var/tmp/filmstrips` (0 mappings in `/proc/<pid>/maps`, no open fd): the decoded images are read into heap; the files are a cache from an EARLIER run (mtime 2026-11-20 17:58, the app started 01:43 on 21st). Writing into the files cannot change what the host draws: hypothesis rejected.
- 838 files `temp_<hex>.img` on tmpfs (`/var/tmp` -> `/var/volatile/tmp`, 8 GB), mode 0644 root:root. Raw pixels, no header: the size equals w*h*4. 85 files are exactly 3,215,360 B = 1280x628x4 (a full tab page), 90 are 2,419,200 B, 52 are 576,000 B.
- The stock skins carry full-page backgrounds as plain PNGs: of the 211 PNGs over 60 kB under `/usr/share/Akai`, 62 are 1280x629 and 8 are 1280x630 (the stock synth skins, `Plugin Skins`), a few 1280x628 (MPC60, SP1200, MPC3000 skins, Program Edit). So the host accepts full-page images and a decoded full page costs 3.2 MB in the cache.

## Decisions

- g117: full takeover, one plane, one writer: the shim merges the helper's finished buffer
  into the next host frame commit; if the host does not present within 20 ms the helper
  presents itself. Host TEST_ONLY commits are never merged.
