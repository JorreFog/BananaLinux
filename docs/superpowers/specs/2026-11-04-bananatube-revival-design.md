# BananaTube revival — design

**Date:** 2026-11-04
**Status:** proposed
**Repos:** `~/bananatube` (the client), `~/BananaLinux` (the OS integration)

## Intent

Bring the YouTube client from the Retroid Pocket build to the Anbernic RG DS and RG DS Plus, as a native BananaOS
app. It should feel like part of this system, not a port that landed on it: the RG DS menu's look, both screens used
properly, no compositor, and a memory cost that fits a 1 GB handheld.

`~/bananatube` already is that app. It draws both panels directly through DRM/KMS, plays through libmpv, searches
through Invidious/InnerTube, and has host tests. This design revives it, fixes the bugs the audit found, removes the
unsafe yt-dlp fallback, and completes the shelf/build integration. It does not start from the Retroid SDL/cage code.

## Goals

- A tile on the menu's app shelf that opens BananaTube; HOME returns to the menu in under two seconds.
- Video on the top panel, controls on the bottom; pad and bottom-panel touch both work.
- Search, play, pause, seek, volume, quality, watched list with resume, and the app's settings.
- The RG DS menu's Pixel UI language: its dark/light palette, its accent colour, Pixelify Sans, crisp cards and
  borders. Not the Retroid build's brown Pocket palette.
- No compositor, no Wayland, no SDL window layer, no browser/web view.
- Under 35 MB RSS while browsing and under 95 MB while playing 480p, with the demuxer caps already in the player.
- A clean Buildroot build installs the binary, launcher, app marker and the menu/theme entries.

## Non-goals

- SponsorBlock, the Channels and Local tabs, and the Retroid app's four-tab browser are a follow-up. The first
  revival is the core browse/play/history/settings app.
- No account sign-in, cookies, downloads or offline playback.
- No yt-dlp in the core path, and no shell-out to it. If a device test later shows videos that only yt-dlp can
  resolve, it comes back as an optional package, not a core dependency.
- No second theme engine for the app and no new bitmap assets beyond the existing icon/font set.

## Constraints

- RG DS / RG DS Plus: RK3566/RK3568, 1 GB RAM, two 640×480 panels (the Plus scales the UI by 1.6), bottom GT911
  touch, no compositor.
- Video: H.264 through the kernel's v4l2m2m decoder (the RK3566's VPU is hantro's rk3568-vpu); VP9 only where
  rkvdec binds, which this SoC's kernel is not assumed to do. AV1 is refused. 480p is the default; 360p/720p are choices.
- The app is launched like a game: the menu drops DRM master, the app takes it, and must drop it and exit cleanly.
  bananad owns sleep and sends SIGSTOP/SIGCONT around suspend.
- The root is a read-only squashfs; settings, history, cache and logs live on `/data` (exFAT).
- Audio is ALSA through bananad's default route; the player is a normal ALSA client.
- The client is C11 in `~/bananatube`; the OS side is the `~/BananaLinux` external Buildroot tree.
- The current `main` does not have the app yet: no `buildroot/package/bananatube`, no defconfig entry, no
  `systems.conf`/theme line, and `linux-banana.fragment` still disables the media subsystem. Those come back from
  the earlier `c77a211` integration, not from a new design.
- The RG DS and RG DS Plus must both build. The package is owned by the `~/BananaLinux` external tree; the app
  repo's old package copy is removed so there is one pin and one install script.

## Approaches considered

1. **Port the Retroid app to SDL2/KMSDRM.** Reuses its C++ features, but still needs a window layer and a
   single-screen layout forced onto two panels, and adds memory and surface area. Rejected: the system deliberately
   has no compositor.
2. **Write a new native client.** Same result as the existing port, with none of its tests or hardware lessons.
   Rejected: unnecessary work.
3. **Revive the existing native client.** It already takes DRM master, draws both panels, renders libmpv into EGL,
   and has 13 host tests. Chosen.

For the look, the Retroid app's Pocket palette and the RG DS menu's Pixel UI were both considered. The system's own
look was chosen so the app belongs on this handheld. For stream resolution, packaging yt-dlp and using only the
HTTP APIs were considered; the HTTP APIs are the core, and yt-dlp is out (see below).

## Design

### Runtime and data flow

Keep the current shape: a single main loop owns both panels and a `poll()` set; a search thread, a resolve thread
and two thumbnail threads do the slow work; libmpv renders into the top panel's EGL surface; the bottom panel is
composed on the CPU into dumb buffers.

The search path is Invidious/Piped first, then YouTube's InnerTube API with its `visitorData`. The resolve path is
the same backends' stream data, with the format rules that already refuse AV1 and prefer H.264. The yt-dlp fallback
and its `/data/banana/yt-cookies.txt` argument are removed; when no backend can produce a playable URL, the app says
so and returns to the results. The README, architecture notes and testing notes stop describing yt-dlp as a fallback.

### Fixes before any hardware test

These are the audit's must-fix items, in order:

1. Network work gets one overall deadline; quitting no longer waits out every per-instance curl timeout. A search
   that spends its deadline on dead instances returns to the results instead of continuing through every backend.
2. `AppState.dirty` is folded into the loop's redraw decision, so a key press redraws immediately.
3. The playing video's id and duration are stored when playback starts; history progress is saved by that id, not by
   the visible result. Playing from the watched list updates the watched list. The duration is no longer written as 0.
4. Previous and next get separate branches; previous goes back, next goes forward, both clamp.
5. The `pollfd` array has room for the player wake fd plus every input device.
6. `curl_global_init` runs before the worker threads. The app does not call `curl_global_cleanup` at exit, because a
   worker that missed the shutdown window may still be inside libcurl; process exit reclaims it.
7. The yt-dlp/popen path is deleted.
8. The top panel's video presentation uses page flips and holds its GBM buffer and framebuffer until the flip
   completes, instead of a full modeset and immediate recycle on every frame.
9. Settings and history use write-temp, `fflush`, `fsync`, `rename`, and a directory sync; write errors are checked.
10. The player tags each load and handles end-of-file for the current file, so an old `END_FILE` cannot stop the
    next video; normal completion says "Finished." and clears the resume point.

The should-fix items from the audit are included where they touch these paths: a mutex for `visitorData`, a
per-request IP family, trying every Invidious instance before giving up on a ciphered reply, failing a truncated
JPEG instead of reading uninitialized memory, applying the picture-cache size at the next start instead of resizing
live, and refusing an oversized history file before parsing it.

### Look

The app reads `theme` and `accent` from `/data/banana/settings.conf` (falling back to dark/yellow) and uses the
same six accent colours as `ui/rnds/RndsUI.cpp`. Text on the accent follows the same contrast rule. The default is
the menu's dark ground, gold accent and Pixelify Sans, with Inter for body text where it already is.

Both panels use the menu's card language: a header strip, a raised panel, crisp 1–2 px borders, square-ish
corners, and the same muted metadata. The top panel carries the content — search field, three-column results grid,
the video, the watched list, the settings description. The bottom panel carries the controls — the keyboard, the
transport row, the selected result's details. The existing screen split stays; the change is palette, type and
detail, not a new layout engine.

Empty, loading, failed-search and "no Wi-Fi" states are part of the look, not an afterthought: each says what
happened and what to press.

The app matches the built-in dark/light palettes and the six named accent colours. It does not parse a selected
layout theme's JSON or a user theme's colours; an unknown theme falls back to dark/yellow. Supporting arbitrary
themes would need a shared palette export from the menu and is a separate change. This limitation is accepted: the
default and every named accent match, custom themes keep the app on the system's base look.

### Integration

The OS side is a restoration of the earlier `c77a211` integration, not just a package bump:

- Add `buildroot/package/bananatube` to `~/BananaLinux` and enable it in both `banana_rgds_defconfig` and
  `banana_rgds_plus_defconfig`.
- Restore the media lines in `buildroot/board/rgds/linux-banana.fragment`: `CONFIG_MEDIA_SUPPORT`,
  `CONFIG_MEDIA_CONTROLLER`, the platform/mem2mem/V4L2 H.264 and VP9 bits, `CONFIG_VIDEO_ROCKCHIP_VDEC` and the
  Hantro Rockchip driver. Without them the app installs but every video says "Could not find a valid device".
  This means a full kernel and image build, and a device check that `v4l2m2m-copy` actually decodes H.264 — the
  format the resolver prefers. If the log shows software decoding, the media gate failed even though the image
  built: the ROCKNIX hantro compatible is `rockchip,rk3568-vpu`, and `dmesg` has to show it bound.
- Install the binary, `run-bananatube`, and the app folder with `bananatube.gamelist.xml` and the marker. BananaOS's
  library reads an app folder with a `gamelist.xml`, not the Retroid build's `app.json`.
- Add the `[bananatube]` section to `system/rootfs/etc/banana/systems.conf` and the `bananatube` line with the
  `[app]` marker and `video.png` to `ui/assets/rnds/systems.cfg`, so a clean image has the tile. The app remains
  usable when a card's own `systems.conf` overrides the image's.
- Keep `/data/banana/bananatube.conf`, `/data/banana/bananatube-history.json`, `/data/banana/cache/tube` and the
  log path the launcher already uses.

**Release target and pin.** The work targets the current `main` (0.5.3) on a feature branch, with a PR back to
`main`; no release branch is required first. While the client changes are in review, the package uses a local
`SITE_METHOD` pointing at the checkout. Before merge, the client commits are pushed and the package is pinned to
that commit with a refreshed hash. The image cannot fetch an unmerged client commit.

**Data compatibility.** There is no schema change. Existing `bananatube.conf` and history files are read as they
are. Entries written by the old builds may have a position attached to the wrong id, or a duration of 0; those
values are preserved, not guessed at, and the next play of that video corrects the entry. No migration is required,
and no existing history is deleted.

**Suspend/resume.** A playing video must survive a lid or power sleep. The existing contract stays: bananad
`SIGSTOP`s the process group, `SIGCONT`s it and sends `SIGUSR1` for a redraw; the app redraws on that signal. This
is a hardware test with a video loaded across sleep; if the picture stalls on resume, the fix is a redraw or a
seek-to-current-position, not a new power path.

### Tests

Extend the host tests before the hardware pass: the resolver with no yt-dlp path, history progress by playing id,
previous/next selection, a redraw after a key, a truncated JPEG, and a config/history save round trip. The existing
13 tests stay green. Hardware checks remain the DRM/EGL/mpv/touch/sleep checklist in `docs/TESTING.md`, plus a
video loaded across a real sleep and a check that the kernel's `v4l2m2m` decoder bound after the fragment restore.

## What is left to the builder

- The exact visual measurements (spacing, border widths, dot-grid/bezel treatment) within the palette and type
  decisions above.
- The order of the fix batches and the test details.
- Whether the bottom screen's details view should become the source app's four-tab layout after the core works.
- The optional yt-dlp package, only if hardware testing shows a real need.

## Success

A clean RG DS image shows a YouTube tile; A opens it; search and play work over Wi-Fi with the hardware decoder
bound; pause, seek, volume and quality work; the watched list resumes where a video stopped; a video survives a
sleep; HOME returns to the menu within two seconds; memory stays within the targets; and the app looks like it
shipped with the system.
