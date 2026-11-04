# BananaTube Revival Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Revive the native dual-screen BananaTube client for the RG DS / RG DS Plus, fix the audit's must-fix bugs, match the RG DS menu look, and restore the clean-image Buildroot integration.

**Architecture:** Keep the existing C11 client in `~/bananatube`: one main loop, both panels through DRM/KMS, libmpv on the top panel through EGL, CPU-composed bottom panel, search/resolve/thumbnail threads, and no compositor. Remove the yt-dlp fallback; resolve streams only through Invidious/Piped and InnerTube. Add the OS integration in `~/BananaLinux`: the Buildroot package, both defconfigs, the kernel's V4L2/VDEC fragment, the shelf entries, and the app folder.

**Tech Stack:** C11, CMake/CTest, libdrm/GBM/EGL/GLES2, libmpv, libcurl, cJSON, libjpeg, FreeType, Buildroot, POSIX shell.

**Spec:** `docs/superpowers/specs/2026-11-04-bananatube-revival-design.md`

## Global Constraints

- No compositor, Wayland, SDL window layer, browser or web view.
- The client stays C11 in `~/bananatube`; the OS side stays in the `~/BananaLinux` external Buildroot tree.
- `yt-dlp` is not a core dependency and must not be spawned. The `popen()` fallback and its cookies argument are removed.
- Keep the memory targets: under 35 MB browsing, under 95 MB playing 480p.
- Keep the existing paths: `/data/banana/bananatube.conf`, `/data/banana/bananatube-history.json`, `/data/banana/cache/tube`, `/data/banana/logs/bananatube.log`.
- The app must release DRM master and exit promptly; HOME returns to the menu in under two seconds.
- Feature branches only. Never push to `main` or `release/*`; open a PR. In `~/BananaLinux`, never stage `AGENTS.md`, `.opencode/` or `opencode.json`.
- The app's host gate is `sh tests/run.sh`; BananaLinux's is `make -s test`. A full image build is only run when the
  user asks.

## Review Focus

- A user quits during a dead-network search or resolve; HOME must return in under two seconds. Task 5.
- A video ends normally versus the user pressing stop; the watched list must resume correctly or clear. Task 7.
- Switching video or quality quickly; an old mpv `END_FILE` must not stop the new file. Task 7.
- YouTube returns only ciphered or AV1 streams; the app must say why instead of hanging. Tasks 1 and 8.
- A card with a huge or corrupt history/settings file; the app must still start and not OOM. Task 2.

## File Structure

**Client (`~/bananatube`):**
- `src/net/resolver.c`: backend-only stream resolution; no process spawn.
- `src/net/http.c`, `src/net/innertube.c`, `src/net/invidious.c`: HTTP deadlines, thread-safe visitorData, all-instance resolution.
- `src/net/searchjob.c`, `src/net/resolvejob.c`, `src/net/thumbworker.c`: bounded shutdown.
- `src/main.c`: redraw flag, play/previous/next, history id, poll slots, EOF handling.
- `src/ui/view.h`, `src/ui/view.c`: the playing id/duration and `ui_set_playing`.
- `src/ui/results.h`, `src/ui/results.c`: `results_step`.
- `src/ui/theme.h`, `src/ui/theme.c`: menu palette/accent.
- `src/player/player.c`: event drain and EOF reason.
- `src/display/gbm_egl.h`, `src/display/gbm_egl.c`: page-flipped video.
- `src/util.c`, `src/conf.c`, `src/history.c`: atomic writes, key/value read, file cap.
- `tests/`: `test_util`, `test_http`, `test_theme`, plus the existing tests.

**OS (`~/BananaLinux`):**
- `buildroot/package/bananatube/`: the one package and pin.
- `buildroot/Config.in`, both defconfigs, `buildroot/board/rgds/linux-banana.fragment`: package entry and decoder.
- `system/rootfs/etc/banana/systems.conf`, `ui/assets/rnds/systems.cfg`: the shelf tile.

---

### Task 1: Remove the yt-dlp fallback and its docs

**Files:**
- Modify: `src/net/resolver.c`
- Modify: `src/net/resolver.h`, `src/net/resolvejob.h`, `src/net/backend.h`, `src/main.c`
- Modify: `README.md`, `docs/ARCHITECTURE.md`, `docs/TESTING.md`

**Interfaces:**
- Consumes: nothing new.
- Produces: `resolver_resolve()` returns only backend results; `RESOLVE_ALL_FAILED` means both HTTP backends failed.

- [ ] **Step 1: Create the client branch and confirm the baseline**

```bash
cd ~/bananatube
git checkout -b feat/bananatube-revival
sh tests/run.sh
```
Expected: 13 tests pass. Do not stage `AGENTS.md` or `opencode.json`.

- [ ] **Step 2: Remove the fallback code**

Delete `ytdlp_available()`, `ytdlp_resolve()`, the `char url[URL_LEN]` fallback block in `resolve_once()`, and the `#include <stdio.h>` if it is no longer used. The end of `resolve_once()` becomes:

```c
    return worst;
}
```

- [ ] **Step 3: Update the comments and docs**

In `src/net/resolver.c`, `resolver.h`, `resolvejob.h`, `backend.h` and `main.c`, replace "yt-dlp fallback" wording with "the HTTP backends". In `README.md`, `docs/ARCHITECTURE.md` and `docs/TESTING.md`, remove the Tier 3 fallback, the cookies paragraph and the claim that a last-resort resolver exists; state that when both backends fail the app returns to the results with the backend's error.

- [ ] **Step 4: Verify nothing can spawn it**

```bash
grep -rn "popen\|ytdlp_resolve\|yt-cookies\|yt-dlp -g" src || true
cmake --build build -j"$(nproc)"
cd build && ctest --output-on-failure
```
Expected: no `popen`, fallback or cookies references; all tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/net/resolver.c src/net/resolver.h src/net/resolvejob.h src/net/backend.h src/main.c README.md docs/ARCHITECTURE.md docs/TESTING.md
git commit -m "bananatube: resolve streams over HTTP only"
```

---

### Task 2: Durable settings and history writes

**Files:**
- Modify: `src/util.h`, `src/util.c`, `src/conf.c`, `src/history.c`
- Create: `tests/test_util.c`
- Modify: `CMakeLists.txt`, `tests/test_conf.c`, `tests/test_history.c`

**Interfaces:**
- Produces: `bool file_write_atomic(const char *path, const void *data, size_t len);`
  Writes `path.new`, `fsync`s it, `rename`s it over `path`, and `fsync`s the containing directory. Returns false and removes the temp on any error.
- Produces: `bool file_key_value(const char *path, const char *key, char *out, size_t cap);`
  Reads `key = value` from a simple settings file; used by Task 10.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_util.c`:

```c
#include "test.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    const char *path = "/tmp/bananatube-util-atomic.txt";
    unlink(path);
    check(file_write_atomic(path, "hello", 5));
    size_t n = 0;
    char *got = file_read(path, &n);
    check(got != NULL);
    check_int((int)n, 5);
    check_str(got, "hello");
    free(got);

    check(!file_write_atomic("/tmp/bananatube-no-such-dir/x", "x", 1));

    const char *big = "/tmp/bananatube-util-too-big.txt";
    unlink(big);
    truncate(big, 2 * 1024 * 1024 + 1);
    check(file_read(big, &n) == NULL);
    unlink(big);

    const char *ini = "/tmp/bananatube-util-ini.conf";
    FILE *f = fopen(ini, "w");
    check(f != NULL);
    if (f) { fputs("# c\ntheme = light\naccent=blue\n", f); fclose(f); }
    char v[32];
    check(file_key_value(ini, "theme", v, sizeof v));
    check_str(v, "light");
    check(file_key_value(ini, "accent", v, sizeof v));
    check_str(v, "blue");
    check(!file_key_value(ini, "missing", v, sizeof v));
    check(!file_key_value("/tmp/bananatube-no-such-ini", "theme", v, sizeof v));

    unlink(path);
    unlink(ini);
    test_done();
}
```

Add `util` to the `foreach(t ...)` list in `CMakeLists.txt`. In `tests/test_conf.c`, add `#include <stdlib.h>` for `mkstemp` and add a save round trip:

```c
char save_path[] = "/tmp/bananatube-conf-save-XXXXXX";
int sfd = mkstemp(save_path);
check(sfd >= 0);
if (sfd >= 0) {
    close(sfd);
    check(conf_save(&c, save_path));
    Conf saved;
    check(conf_load(&saved, save_path));
    check_int(saved.cache_size, c.cache_size);
    check_str(saved.instance, c.instance);
    unlink(save_path);
}
```

In `tests/test_history.c`, after `history_save`, add:

```c
char tmp_name[300];
snprintf(tmp_name, sizeof tmp_name, "%s.new", path);
check(access(tmp_name, F_OK) != 0);
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `sh tests/run.sh`
Expected: link failure for `file_write_atomic` / `file_key_value` (or the new test target fails to build).

- [ ] **Step 3: Implement `file_write_atomic` and `file_key_value` in `src/util.c`**

`file_write_atomic` uses `open(path.new, O_WRONLY|O_CREAT|O_TRUNC, 0644)`, a write loop that retries `EINTR`, `fsync(fd)`, `close`, `rename`, then opens the parent directory with `O_RDONLY|O_DIRECTORY` and `fsync`s it. File write/fsync/rename failures are fatal; the directory fsync is best-effort because exFAT may refuse it. On a fatal failure, `unlink` the temp and return false.

`file_key_value` reads lines with `fgets`, skips `#`/`;`, splits on `=`, trims spaces/tabs/newlines, compares the key with `strcmp`, and returns the trimmed value.

Add a 2 MiB cap to `file_read`: define `FILE_READ_MAX` in `src/util.h`, check `ftell` before allocating, and return NULL when the file is larger. A history file is a few KB; this only refuses a corrupted or hostile card file.

- [ ] **Step 4: Use the helper in `conf_save` and `history_save`**

`conf_save` builds the same text into a `char buf[4096]` with `snprintf`, rejects a result `>= sizeof buf`, and calls `file_write_atomic(path, buf, strlen(buf))`.
`history_save` calls `file_write_atomic(path, text, strlen(text))` after `cJSON_Print`, then frees the text.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `sh tests/run.sh`
Expected: all tests pass, including the new `util` test.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/util.h src/util.c src/conf.c src/history.c tests/test_util.c tests/test_conf.c tests/test_history.c
git commit -m "bananatube: write settings and history atomically"
```

---

### Task 3: Redraw immediately, fix previous/next, and reserve the poll slots

**Files:**
- Modify: `src/main.c`, `src/ui/results.h`, `src/ui/results.c`, `tests/test_ui.c`

**Interfaces:**
- Produces: `bool results_step(Results *r, int delta);`
  Moves the selection by `delta` (usually -1 or +1), clamps to `[0, count-1]`, and returns true when it moved.

- [ ] **Step 1: Write the failing test for `results_step`**

Add to `tests/test_ui.c` after the grid tests:

```c
fill(&r, 6);
r.sel = 0;
check(!results_step(&r, -1));
check(results_step(&r, 1));
check_int(r.sel, 1);
r.sel = 5;
check(!results_step(&r, 1));
check(results_step(&r, -1));
check_int(r.sel, 4);
```

Run: `sh tests/run.sh`
Expected: build fails because `results_step` is not defined.

- [ ] **Step 2: Implement `results_step`**

```c
bool results_step(Results *r, int delta)
{
    if (!r || r->count <= 0) return false;
    int want = r->sel + delta;
    if (want < 0) want = 0;
    if (want >= r->count) want = r->count - 1;
    if (want == r->sel) return false;
    r->sel = want;
    return true;
}
```

- [ ] **Step 3: Fix the main loop**

In `src/main.c`:

- Change `struct pollfd pfds[1 + INPUT_MAX_DEV];` to `struct pollfd pfds[2 + INPUT_MAX_DEV];`.
- Call `http_init();` after signal setup and before any worker starts. Do not call `http_fini()` at exit: a timed-out
  worker may still be inside libcurl, and process exit reclaims it. The tests call `http_fini()` after their own
  requests.
- After the input and touch handling, before `if (dirty)`, add:

```c
        if (ui.dirty) { dirty = true; ui.dirty = false; }
```

- Replace the shared `UI_PREV_VIDEO` / `UI_NEXT_VIDEO` case with two cases. Add a helper:

```c
static void play_neighbour(AppState *ui, Player *player, History *hist, const Conf *conf,
                           const char *conf_path, ResolveJob *job, PendingPlay *pending, int delta)
{
    if (!ui->results.count) return;
    save_progress(ui, player, hist, conf_path, true);
    if (!results_step(&ui->results, delta)) return;
    const SearchResult *r = &ui->results.item[ui->results.sel];
    start_video(ui, job, pending, conf, r->id, r->title, r->channel, 0);
}
```

Task 4 adds the duration argument to this call. Use `play_neighbour(..., -1)` for `UI_PREV_VIDEO` and
`play_neighbour(..., +1)` for `UI_NEXT_VIDEO`. Delete the `ui_key(&ui, BK_NONE)` call.

- [ ] **Step 4: Run the tests and build**

Run: `sh tests/run.sh`
Expected: all tests pass, including the new `results_step` checks.

- [ ] **Step 5: Commit**

```bash
git add src/main.c src/ui/results.h src/ui/results.c tests/test_ui.c
git commit -m "bananatube: redraw on input and fix previous/next"
```

---

### Task 4: Track the playing video for history

**Files:**
- Modify: `src/ui/view.h`, `src/ui/view.c`, `src/main.c`, `tests/test_ui.c`

**Interfaces:**
- Produces: `void ui_set_playing(AppState *st, const char *id, const char *title, const char *channel, int duration_sec);`
- Produces: `Playing.id` (`VIDEO_ID_LEN`) and `Playing.duration_sec`.
- Produces: `start_video(..., int duration_sec, double resume_at)` and `PendingPlay.duration_sec`.

- [ ] **Step 1: Write the failing test**

In `tests/test_ui.c`, add:

```c
ui_init(&st);
ui_set_playing(&st, "dQw4w9WgXcQ", "A video", "A channel", 754);
check_str(st.playing.id, "dQw4w9WgXcQ");
check_str(st.playing.title, "A video");
check_int(st.playing.duration_sec, 754);
```

Run: `sh tests/run.sh`
Expected: compile fails because `ui_set_playing` has the old 3-argument signature.

- [ ] **Step 2: Implement the state change**

Add the two fields to `Playing` in `src/ui/view.h`. Change `ui_set_playing` to copy the id, title, channel and duration, set `duration_sec = duration_sec > 0 ? duration_sec : -1`, and reset the other playback state as it does today. Update every caller: `src/main.c` (`take_resolved`, the playing shot, `cli_bench`, `cli_keys`) and `tests/test_ui.c`.

- [ ] **Step 3: Use the playing id for progress**

Add `int duration_sec` to `PendingPlay` and `start_video`'s parameter list (before `resume_at`). Pass `r->duration_sec` for a search result and `it->duration_sec` for a history item. Update the `play_neighbour()` call from Task 3 to pass `r->duration_sec`.

In `save_progress()`:

```c
    if (!player || !ui->playing.active || !hist) return;
    if (!ui->playing.id[0]) return;
    history_progress(hist, ui->playing.id, player_position(player));
```

In `take_resolved()`, call:

```c
    history_touch(hist, pending->id, pending->title, pending->channel, pending->duration_sec);
    ui_set_playing(ui, pending->id, pending->title, pending->channel, pending->duration_sec);
```

In `switch_quality()`, use `ui->playing.id`, `ui->playing.title`, `ui->playing.channel` and `ui->playing.duration_sec` instead of the selected search result.

- [ ] **Step 4: Run the tests**

Run: `sh tests/run.sh`
Expected: all tests pass. The history test still covers the duration; the new UI test covers the stored id.

- [ ] **Step 5: Commit**

```bash
git add src/ui/view.h src/ui/view.c src/main.c tests/test_ui.c
git commit -m "bananatube: save history by the playing video"
```

---

### Task 5: Bound network work and stop waiting on quit

**Files:**
- Modify: `src/net/http.h`, `src/net/http.c`, `src/net/searchjob.c`, `src/net/resolvejob.c`, `src/net/thumbworker.c`, `src/main.c`
- Create: `tests/test_http.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `void http_set_deadline_ms(int64_t deadline_ms);`
  Sets an absolute monotonic deadline for the current thread; `0` clears it. Every transfer uses the smaller of its own timeout and the remaining deadline.
- Produces: `http_get()` fails with `"timed out"` when the deadline is spent.

- [ ] **Step 1: Write the failing deadline test**

Create `tests/test_http.c` with a small local server: bind to `127.0.0.1:0`, listen, accept, sleep 500 ms, close. Then:

```c
http_init();
http_set_deadline_ms(now_ms() + 100);
HttpResponse r;
check(!http_get(url, 5000, &r));
check(str_contains(http_error(&r), "timed out"));
http_response_free(&r);
http_set_deadline_ms(0);
http_fini();
```

The test must finish in under one second. Add `http` to the CMake test list.

Run: `sh tests/run.sh`
Expected: build fails because `http_set_deadline_ms` is not defined.

- [ ] **Step 2: Implement the deadline**

In `src/net/http.c`, add:

```c
static _Thread_local int64_t g_deadline_ms;
void http_set_deadline_ms(int64_t deadline_ms) { g_deadline_ms = deadline_ms; }
```

At the top of `transfer()`, after `memset(out, ...)`:

```c
    if (g_deadline_ms) {
        int64_t left = g_deadline_ms - now_ms();
        if (left <= 0) { str_copy(out->error, sizeof out->error, "timed out"); return false; }
        if (left < timeout_ms) timeout_ms = (int)left;
    }
```

- [ ] **Step 3: Give the workers a deadline and a bounded stop**

In `search_main()`, set `http_set_deadline_ms(now_ms() + 10000);` before `backend_search_auto()` and clear it after. In `resolve_main()`, use `now_ms() + 15000`. Keep the `http_init()` call added in Task 3; do not add `http_fini()` to the app.

Change `search_job_stop()` and `resolve_job_stop()` to set `quit`, signal, then use `pthread_timedjoin_np()` with a 250 ms absolute timeout. If the join times out, `pthread_detach()` the thread and do **not** free the job; the process exit reclaims it. Add a `shortcut:` comment on the detached path. Do the same per thread in `thumb_worker_stop()`; if any thread is detached, do not free the worker or its finished pixels.

- [ ] **Step 4: Run the tests**

Run: `sh tests/run.sh`
Expected: all tests pass; the deadline test fails fast instead of sleeping 500 ms.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/net/http.h src/net/http.c src/net/searchjob.c src/net/resolvejob.c src/net/thumbworker.c src/main.c tests/test_http.c
git commit -m "bananatube: bound network work and quit promptly"
```

Hardware/Wi-Fi check: set `instance = https://10.255.255.1` in `bananatube.conf`, press play, then press HOME during the spinner. The menu must return in under two seconds.

---

### Task 6: Present video frames with page flips

**Files:**
- Modify: `src/display/gbm_egl.h`, `src/display/gbm_egl.c`

**Interfaces:**
- Produces: `GbmEgl.displayed`, `GbmEgl.displayed_fb`, `GbmEgl.previous`, `GbmEgl.previous_fb`, `GbmEgl.pending`.
- The DRM flip handler already decrements an `int *`; the top panel passes `&g->pending` to `drmModePageFlip()`.

- [ ] **Step 1: Extend the state**

Add to `GbmEgl`:

```c
struct gbm_bo *displayed;      /* the buffer the CRTC is on now */
uint32_t       displayed_fb;
struct gbm_bo *previous;       /* the buffer the last completed/queued flip came from */
uint32_t       previous_fb;
int            pending;
```

- [ ] **Step 2: Replace the per-frame modeset**

In `gbm_egl_swap()`:

1. `eglSwapBuffers()`.
2. If `g->pending > 0`, lock the new front buffer and release it; the flip is still in flight and this frame is dropped.
3. If `g->previous` exists, remove `previous_fb` and release `previous`; its flip has completed. Clear both.
4. Lock the new front buffer and `add_fb()` it.
5. On the first frame, `drmModeSetCrtc()` with the connector and mode; set `displayed`/`displayed_fb`; there is no previous.
6. On later frames, `drmModePageFlip(fd, crtc, fb, DRM_MODE_PAGE_FLIP_EVENT, &g->pending)` and increment `g->pending` when it returns 0. Move the old `displayed`/`displayed_fb` into `previous`/`previous_fb`, then store the new bo and fb in `displayed`/`displayed_fb`.
7. If the flip is refused, fall back to `drmModeSetCrtc()`, release the previous buffer immediately, and clear `previous`.

- [ ] **Step 3: Release at shutdown**

In `gbm_egl_fini()`, remove `displayed_fb` and `previous_fb` and release both buffers before destroying the GBM
surface.

- [ ] **Step 4: Build and run the host tests**

Run: `sh tests/run.sh`
Expected: build clean and all tests pass. This path has no host display; the hardware check is a video with no tearing and no frozen top panel.

- [ ] **Step 5: Commit**

```bash
git add src/display/gbm_egl.h src/display/gbm_egl.c
git commit -m "bananatube: page-flip the video panel"
```

---

### Task 7: Handle end-of-file and video switches

**Files:**
- Modify: `src/player/player.c`, `src/main.c`, `tests/test_player.c`

**Interfaces:**
- `Player` keeps `eof` true only for `MPV_END_FILE_REASON_EOF`; a user stop is not a finished video.
- `player_open()` drains queued events after `player_close()` before loading the new file.

- [ ] **Step 1: Write the failing state test**

In `tests/test_player.c`, after the existing open checks, add:

```c
check(!player_eof(p));
player_close(p);
check(!player_active(p));
check(!player_eof(p));
```

This catches an `eof` that survives a close.

- [ ] **Step 2: Implement the event rule**

In `player.c`, add a helper that drains `mpv_wait_event(p->mpv, 0)` until `MPV_EVENT_NONE`. Call it immediately after the `stop` command in `player_close()`.

In `player_tick()`'s `MPV_EVENT_END_FILE` case:

```c
            p->active = false;
            p->eof = f && f->reason == MPV_END_FILE_REASON_EOF;
            if (f && f->reason == MPV_END_FILE_REASON_ERROR) {
                const char *r = mpv_error_string(f->error);
                snprintf(p->error, sizeof p->error, "%s", r ? r : "playback failed");
                log_line("player: %s", p->error);
            } else if (p->eof) {
                log_line("player: end of file");
            }
```

- [ ] **Step 3: Handle EOF before the active check in `main`**

Replace the playback state block with:

```c
            player_tick(player);
            if (player_eof(player)) {
                player_close(player);
                if (ui.playing.active) {
                    if (ui.playing.id[0]) {
                        history_progress(&history, ui.playing.id, 0);
                        char path[512];
                        history_path(conf_path, path, sizeof path);
                        history_save(&history, path);
                    }
                    ui_set_playing_stopped(&ui, "Finished.");
                }
            } else if (player_active(player)) {
                ui_set_playing_state(&ui, player_position(player), player_duration(player),
                                     player_paused(player), player_buffering(player),
                                     player_width(player), player_height(player));
            } else if (ui.playing.active) {
                const char *why = player_error(player);
                ui_set_playing_stopped(&ui, why && *why ? why : "Stopped.");
            }
```

- [ ] **Step 4: Run the tests**

Run: `sh tests/run.sh`
Expected: all tests pass, including the new close/eof checks.

- [ ] **Step 5: Commit**

```bash
git add src/player/player.c src/main.c tests/test_player.c
git commit -m "bananatube: finish, stop and switch without stale events"
```

Hardware check: play to the end and reopen from the watched list; it must start from the beginning and say "Finished." when it ends.

---

### Task 8: Make the shared API state thread-safe and try every Invidious instance

**Files:**
- Modify: `src/net/innertube.c`, `src/net/http.c`, `src/net/invidious.c`

**Interfaces:**
- `g_visitor` is protected by a `pthread_mutex_t`; `client_context()` and `remember_visitor()` lock it.
- `g_ip_family` becomes `static _Thread_local int`, so a resolver thread's choice does not change the search or thumbnail threads.
- `invidious_streams()` returns `RESOLVE_OK` from any instance, `NEEDS_CIPHER` or `NO_SUITABLE_FORMAT` only when no instance succeeded.

- [ ] **Step 1: Change the globals**

`src/net/http.c`: change `static int g_ip_family` to `static _Thread_local int g_ip_family = AF_UNSPEC;`. `http_set_ip_family()` is unchanged.

`src/net/innertube.c`: add `<pthread.h>` and `static pthread_mutex_t g_visitor_lock = PTHREAD_MUTEX_INITIALIZER;`. Lock around the comparison/copy in `remember_visitor()` and around the whole `snprintf` in `client_context()`.

- [ ] **Step 2: Loop over every Invidious instance**

Keep a `ResolveStatus best = RESOLVE_NO_NETWORK;`; for each instance, return immediately on `RESOLVE_OK`, remember `RESOLVE_NEEDS_CIPHER` or `RESOLVE_NO_SUITABLE_FORMAT` in `best`, and continue. Return `best` after the loop.

- [ ] **Step 3: Build and run the tests**

Run: `sh tests/run.sh`
Expected: all tests pass; no new host network test is needed for these paths.

- [ ] **Step 4: Commit**

```bash
git add src/net/http.c src/net/innertube.c src/net/invidious.c
git commit -m "bananatube: share API state safely and try every instance"
```

---

### Task 9: Fail truncated thumbnails and apply cache size at startup

**Files:**
- Modify: `src/net/thumbnail.c`, `tests/test_worker.c`, `src/main.c`, `src/ui/settings.c`

**Interfaces:**
- `thumb_decode()` returns false unless every output row was read and `jpeg_finish_decompress()` succeeded.
- `cache_size` no longer calls `thumb_init()` live; it takes effect on the next start.

- [ ] **Step 1: Write the failing truncated-JPEG test**

In `tests/test_worker.c`, after the valid `big` decode:

```c
uint8_t *cut = malloc(big_n / 2);
check(cut != NULL);
if (cut) {
    memcpy(cut, big, big_n / 2);
    pixels = (uint8_t *)0x1;
    w = h = 7;
    check(!thumb_decode(cut, big_n / 2, &pixels, &w, &h, THUMB_DRAW_W, THUMB_DRAW_H));
    check(pixels == NULL);
    free(cut);
}
```

Run: `sh tests/run.sh`
Expected: the test fails because the current decoder returns true on a short JPEG.

- [ ] **Step 2: Implement the check**

After the scanline loop, fail if `cinfo.output_scanline < cinfo.output_height`; check `jpeg_finish_decompress()` and fail if it returns false. Free `rgb` on those paths and return false.

- [ ] **Step 3: Stop applying cache size live**

In `main.c`'s `apply_setting()`, remove the `thumb_init(cache, conf->cache_size)` call. In `src/ui/settings.c`, change the `cache_size` help text to say it takes effect the next time BananaTube starts.

- [ ] **Step 4: Run the tests**

Run: `sh tests/run.sh`
Expected: all tests pass, including the truncated-JPEG check.

- [ ] **Step 5: Commit**

```bash
git add src/net/thumbnail.c tests/test_worker.c src/main.c src/ui/settings.c
git commit -m "bananatube: reject truncated thumbnails and defer cache resize"
```

---

### Task 10: Read the menu palette and accent

**Files:**
- Modify: `src/ui/theme.h`, `src/ui/theme.c`, `src/main.c`
- Create: `tests/test_theme.c`
- Modify: `CMakeLists.txt`, `tests/test_util.c`

**Interfaces:**
- Produces: `UiTheme ui_theme_with(const char *theme, const char *accent);`
- Produces: `file_key_value()` from Task 2.
- Reads `/data/banana/settings.conf` for `theme` and `accent`; missing or unknown values fall back to dark/yellow.

- [ ] **Step 1: Write the failing theme tests**

Create `tests/test_theme.c`:

```c
#include "test.h"
#include "ui/theme.h"

int main(void)
{
    UiTheme t = ui_theme_with("dark", "yellow");
    check_int(t.bg, 0xff17161c);
    check_int(t.accent, 0xfff0b429);

    t = ui_theme_with("light", "blue");
    check_int(t.bg, 0xfff3efe6);
    check_int(t.accent, 0xff4a9fe0);
    check_int(t.ink, 0xff2a2620);

    t = ui_theme_with("nonsense", "nonsense");
    check_int(t.bg, 0xff17161c);
    check_int(t.accent, 0xfff0b429);

    test_done();
}
```

Add `theme` to the CMake test list. In `tests/test_util.c`, add a `file_key_value` case for a missing key and a missing file.

Run: `sh tests/run.sh`
Expected: build fails because `ui_theme_with` is not defined.

- [ ] **Step 2: Implement the palette**

Keep the current dark values. Add the light values from `ui/rnds/RndsUI.cpp`'s `lightPal()`: bg `0xf3efe6`, bar `0xfdfcf9`, panel `0xf6f2ea`, panel_hi `0xffffff`, ink `0x2a2620`, muted `0x7a7262`, faint `0xa69d8c`.

Map the six accent names exactly as `ui/rnds/RndsUI.cpp` does: yellow `0xf0b429`, blue `0x4a9fe0`, red `0xe8574d`, green `0x56bf6b`, purple `0xa07ae6`, pink `0xec6fa8`. Set `accent_ink` with the same relative-luminance contrast rule the menu uses; dark ground for the default.

- [ ] **Step 3: Use the menu's settings**

In `main()`, after `ui_init(&ui)`, read the two keys and set `ui.theme`:

```c
    char theme[16] = "dark", accent[16] = "yellow";
    file_key_value("/data/banana/settings.conf", "theme", theme, sizeof theme);
    file_key_value("/data/banana/settings.conf", "accent", accent, sizeof accent);
    ui.theme = ui_theme_with(theme, accent);
```

The headless `--shot`/`--keys` paths keep the default because `/data/...` does not exist on the host.

- [ ] **Step 4: Run the tests and render a frame**

Run: `sh tests/run.sh`
Expected: all tests pass.

Run:
```bash
build/bananatube --shot /tmp/bananatube-style.ppm results both
```
Expected: a PPM of both panels using the dark/gold palette; inspect it for the menu's card and header language.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/ui/theme.h src/ui/theme.c src/main.c tests/test_theme.c tests/test_util.c
git commit -m "bananatube: wear the menu palette and accent"
```

---

### Task 11: Restore the BananaLinux package, defconfig, kernel decoder and shelf entries

**Files:**
- Create: `buildroot/package/bananatube/Config.in`, `bananatube.mk`, `bananatube.hash`
- Modify: `buildroot/Config.in`, `buildroot/configs/banana_rgds_defconfig`, `buildroot/configs/banana_rgds_plus_defconfig`
- Modify: `buildroot/board/rgds/linux-banana.fragment`
- Modify: `system/rootfs/etc/banana/systems.conf`, `ui/assets/rnds/systems.cfg`
- Delete: `~/bananatube/buildroot/package/bananatube/`, `~/bananatube/system/systems.conf.snippet`, `~/bananatube/system/systems.cfg.snippet`
- Modify: `~/bananatube/README.md`

**Interfaces:**
- Consumes: the client commit from Tasks 1–10.
- Produces: `BR2_PACKAGE_BANANATUBE`, the `[bananatube]` system, the theme line, and the kernel V4L2/VDEC options.

- [ ] **Step 1: Create the BananaLinux branch**

```bash
cd ~/BananaLinux
git checkout -b feat/bananatube-revival
```

- [ ] **Step 2: Restore the package files**

Add `source "$BR2_EXTERNAL_BANANA_PATH/package/bananatube/Config.in"` to `buildroot/Config.in`. Copy the `bananatube` package from commit `c77a211` (`git show c77a211:buildroot/package/bananatube/...`). Keep `BANANATUBE_DEPENDENCIES = libdrm mesa3d-panfrost libcurl cjson jpeg freetype zlib mpv host-pkgconf`, the CMake options and the install commands. The install must use `system/bananatube.gamelist.xml`, not `app.json`, and touch `BananaTube.app`.

Set `BANANATUBE_VERSION` to the client commit that contains Tasks 1–10. During development, point `BANANATUBE_SITE` at the local checkout with `SITE_METHOD = local`; do not commit that local path. Refresh the hash from the downloaded tarball.

- [ ] **Step 3: Enable the package and the decoder**

Add `BR2_PACKAGE_BANANATUBE=y` to both defconfigs. Restore the media fragment block from `c77a211`:

```text
CONFIG_MEDIA_SUPPORT=y
CONFIG_MEDIA_CONTROLLER=y
CONFIG_MEDIA_PLATFORM_SUPPORT=y
CONFIG_MEDIA_PLATFORM_DRIVERS=y
CONFIG_V4L2_MEM2MEM_DEV=y
CONFIG_V4L2_H264=y
CONFIG_V4L2_VP9=y
CONFIG_VIDEO_ROCKCHIP_VDEC=y
CONFIG_VIDEO_HANTRO=y
CONFIG_VIDEO_HANTRO_ROCKCHIP=y
# CONFIG_MEDIA_CAMERA_SUPPORT is not set
# CONFIG_MEDIA_ANALOG_TV_SUPPORT is not set
# CONFIG_MEDIA_DIGITAL_TV_SUPPORT is not set
# CONFIG_MEDIA_RADIO_SUPPORT is not set
# CONFIG_MEDIA_SDR_SUPPORT is not set
# CONFIG_MEDIA_TEST_SUPPORT is not set
# CONFIG_MEDIA_USB_SUPPORT is not set
```

- [ ] **Step 4: Add the shelf entries**

Add the `[bananatube]` section to `system/rootfs/etc/banana/systems.conf` and the `bananatube` line with `video.png`, `#e0402f`, `Banana [app]` to `ui/assets/rnds/systems.cfg`, copying `c77a211`.

- [ ] **Step 5: Remove the duplicate package in the client repo**

Delete `~/bananatube/buildroot/package/bananatube/` and the two `system/*.snippet` files. Update the client README's "Getting it on the handheld" section to point at the BananaLinux package and the two image files it installs.

- [ ] **Step 6: Commit the client cleanup**

```bash
cd ~/bananatube
git add README.md
git add -u buildroot/package/bananatube system/systems.conf.snippet system/systems.cfg.snippet
git commit -m "bananatube: one Buildroot package, in BananaOS"
```

- [ ] **Step 7: Verify the integration without a full image build**

```bash
grep -R "BR2_PACKAGE_BANANATUBE" buildroot/configs
grep -n "MEDIA_SUPPORT\|VIDEO_ROCKCHIP_VDEC\|VIDEO_HANTRO" buildroot/board/rgds/linux-banana.fragment
grep -n "bananatube" system/rootfs/etc/banana/systems.conf ui/assets/rnds/systems.cfg
git diff --check
```
Expected: each command finds the new entries.

- [ ] **Step 8: Commit**

```bash
git add buildroot/Config.in buildroot/package/bananatube buildroot/configs/banana_rgds_defconfig buildroot/configs/banana_rgds_plus_defconfig buildroot/board/rgds/linux-banana.fragment system/rootfs/etc/banana/systems.conf ui/assets/rnds/systems.cfg
git commit -m "bananatube: restore the app and its video decoder"
```

---

### Task 12: Update the hardware checklist and run the final verification

**Files:**
- Modify: `~/bananatube/docs/TESTING.md`, `~/bananatube/README.md`

**Interfaces:**
- Consumes: every task above.
- Produces: the checklist the hardware pass follows.

- [ ] **Step 1: Update the checklist**

Add explicit checks for: HOME during a dead-network search or resolve; normal end versus stop and the watched-list resume; switching video and quality quickly; a video across sleep; the decoder line in `dmesg`; the top panel's flip/tearing; the bottom-panel touch keyboard and transport; a clean image showing the YouTube tile.

- [ ] **Step 2: Run the app test gate**

```bash
cd ~/bananatube
sh tests/run.sh
```
Expected: all tests pass.

- [ ] **Step 3: Run the BananaLinux host gates**

```bash
cd ~/BananaLinux
make -s ui
make -s test
```
Expected: the UI builds and the headless tests pass.

- [ ] **Step 4: Commit the docs**

```bash
cd ~/bananatube
git add docs/TESTING.md README.md
git commit -m "bananatube: hardware checklist for the revival"
```

- [ ] **Step 5: Check the feature branches, push and open PRs**

```bash
cd ~/bananatube && git status --short
cd ~/BananaLinux && git status --short
```
Expected: only intended changes; no `AGENTS.md`, `.opencode/` or `opencode.json` staged. Then, for each repo:

```bash
git push -u origin HEAD
gh pr create --fill
```
Never push `main` or `release/*`.

- [ ] **Step 6: Full image build, only when the user asks**

```bash
cd ~/BananaLinux
make rgds-plus
```
Expected: `output/rgds-plus/images/banana-rgds-plus.img.gz` and the update tarball. This is the 1h+ step; do not run it unless the user explicitly asks.
