# BananaOS issue-fix playbook for Claude Opus 5.5

**Audience:** a Claude Opus 5.5 coding agent (or a human following the same steps) that will land the remaining GitHub issues in [JorreFog/BananaLinux](https://github.com/JorreFog/BananaLinux).

**Companion:** `docs/ISSUES.md` is the cause/fix map of all 98 issues (snapshot 30 October 2026, Europe/Stockholm, tree `12474fb`). This playbook is the *implementation* document: files, current code, the exact patch shape, tests, and done-when. Do not re-derive causes from GitHub threads unless a work package says the tree has moved.

**Do not implement everything in one PR.** One work package = one commit = one reviewable diff. Merge the open fix PRs before rewriting them.

---

## 0. Paste this into a new Opus session

Use this as the system / first-user instruction, then attach this file.

```
You are implementing BananaOS (JorreFog/BananaLinux), a Buildroot Linux for the
Anbernic RG DS and RG DS Plus. Follow docs/ISSUE-FIX-PLAYBOOK.md exactly.

Rules:
- One work package per commit. Do not batch unrelated issues.
- Match existing comment voice (lowercase, parenthetical asides, issue numbers).
- UI strings: Banana prose, not GitHub-issue voice. See §3.
- Never send the user into DraStic's on-device control mapper.
- Never enable CONFIG_USB_GADGET without the sleep/OTG fix (PR #101).
- Do not reimplement open PRs #101 #123 #73 #91 #94. Review/rebase them.
- Do not add PSP, dsperate, YouTube, MTP, undervolt, or a home-screen grid
  unless the user names that package.
- Tests: `make test` (ui/tests/run.sh) after every UI change; `sh -n` on
  every edited shell script; `ui/tests/sync.sh` after sync.cpp; sudo
  system/bananad/tests/sleep-test.sh after bananad/rcK sleep/shutdown.
- If a test is not runnable in this environment, still add the assertion.
- Read the work package, read the cited files, then patch. Do not invent
  a second design.
```

Then: “Start at the first unfinished work package in §6. Stop when that package’s Done-when is green. Commit.”

---

## 1. Hard rules (do not negotiate)

1. **Tree first.** Causes in this playbook were read from `main` at `12474fb`. If a cited snippet is gone, search the symbol; do not recreate a deleted design.
2. **Open PRs first.** `#101` sleep/OTG, `#123` charge LED, `#73` settings fuzz, `#91` trial boot, `#94` flight recorder. Review, request changes, or rebase. Do not write a parallel patch.
3. **One cluster, one fix.** Clusters A–K in `docs/ISSUES.md` are the same bug under several numbers. Fix the cluster once; mention every issue number in the commit body.
4. **No DraStic mapper.** `#113` and `#77`: change `drastic.cfg` from Banana, migrate existing 1036 like the hinge 65535→328 path in `run-drastic`. Do not tell people to open DraStic’s control editor (it can lock them out).
5. **No gadget without sleep.** `# CONFIG_USB_GADGET is not set` is why xHCI halt returns `-110`. Enabling gadget without dual-role OTG + s2idle-for-that-sleep-only (PR `#101`) makes overnight drain worse. MTP (`#130`) waits until that lands.
6. **Do not “improve” Banana voice.** Comments and Settings help are a house style. Copy nearby sentences. See §3.
7. **Do not expand scope.** Folders (`#121`), grid (`#120`), PSP (`#103`), dsperate (`#100`), YouTube (`#38`), undervolt (`#125`) are product work, not leftovers of a small bug.
8. **Hardware you cannot run.** You will not have an RG DS. Prove UI with `banana-ui --headless --script`. Prove shell with `sh -n` and unit-style fixtures. Write the on-device check in the commit body so a human can run it.
9. **Settings help lives on the top screen.** `helpOf` / `gameHelpOf` + `helpText()` draw on screen 0. Do not add a second help paragraph on screen 1 that can run into `face("B","Back")`.
10. **SuperDrastic 3D scale is 1-bit.** BananaDS `BANANADS_RENDER_SCALE` is 1–4. DraStic `hires_3d` is only `0` or `1`. Map 1→0, 2|3|4→1. Never write `hires_3d = 3`.

---

## 2. Repo map (where to stand)

| Path | What it is |
|---|---|
| `ui/` | banana-ui. Headless tests in `ui/tests/run.sh`. `make test` from repo root. |
| `ui/src/library.cpp` | Library scan, resume chip, `dsCartIcons`, `romIconCover`. |
| `ui/rnds/RndsUI.cpp` | DSi-style reel, `plateBox()`, `cart()`, battery bolt. |
| `ui/src/settings.cpp` | Settings + per-game page (`renderGame`, `gameHelpOf`). ~8k lines; search by key. |
| `ui/src/sync.cpp` | banana-sync. Built and tested by `ui/tests/sync.sh` (not in `make test`). |
| `system/rootfs/usr/libexec/banana/run-drastic` | SuperDrastic launcher. **This is the #117 fix.** |
| `system/rootfs/usr/libexec/banana/run-bananads` | BananaDS launcher. Already reads `nds-per-game-res.conf`. |
| `system/rootfs/usr/libexec/banana/nds-game-option` | `awk` reader: `nds-game-option <file> <rom>` prints the value or nothing. |
| `system/rootfs/usr/libexec/banana/nds-saves` | Shares `.dsv` / `.sav` by mtime. `#96` lives here. |
| `system/rootfs/etc/init.d/rcK` | Shutdown: TERM games, umount `/data`. `#46` leftover. |
| `system/bananad/bananad.c` | Sleep, volume, LEDs, ALSA, charger. Tests: `sudo system/bananad/tests/sleep-test.sh`. |
| `buildroot/board/rgds/drastic.cfg` | Stock DraStic maps. Copied to `/data/drastic` on first run. |
| `buildroot/board/rgds/linux-banana.fragment` | Kernel. `# CONFIG_USB_GADGET is not set` at line 162. |
| `docs/EMULATORS.md` | Contract `run-drastic` / `run-bananads` keep. |
| BananaDS sources | **Not in this tree.** Fetched from `JorreFog/SuperDrastic` (`bananads/` subdir) at the pin in `buildroot/package/bananads/bananads.mk`. In-game pause HUD (`#35`, `#114`) is there. |

**CI** (`.github/workflows/ci.yml`): `make test`; `make -C ui`; bananad native + aarch64 `-Werror`; `sudo system/bananad/tests/sleep-test.sh`; `sh -n` on init/libexec scripts; Buildroot defconfig option check. Image build is `workflow_dispatch` only. You do not need a full image to land UI/shell packages.

**Headless UI.** From `ui/` after `make host`:

```
./banana-ui --headless /tmp/shot- --size 640x480 \
  --assets assets/rnds \
  --systems ../system/rootfs/etc/banana/systems.conf \
  --roms <fixture>/roms --data <fixture>/data \
  --script "home 0; press start; shot settings"
```

`ui/tests/run.sh` already builds a fixture. Copy its `--script` patterns. `shot NAME` writes `NAME-top.png` and `NAME-bot.png`. Scripts in run.sh are the regression net: if you change row order on the game page, those scripts break.

---

## 3. Voice and comments (copy this, do not invent a new one)

Comments in launchers and banana-ui:

- Sentence case, often starting mid-thought after `//` or `#`.
- Parenthetical asides for history: `(#27)`, `(a run cut short is put right at the next start, above)`.
- Technical names as the code uses them (`threaded_3d`, `hires_3d`, `dsCartIcons`), not marketing names, except in user-visible strings.
- User-visible Settings help: short words, “the handheld”, “the card”, “Banana”, “A” / “B” / “START” for buttons. No “please”, no “simply”, no GitHub issue titles in the UI.

Example from `run-drastic` (copy this rhythm for `hires_3d`):

```
# the game's own Threaded 3D (#27): DraStic reads it from drastic.cfg alone, so the game's value goes in for this run
# and Settings' comes back after (kept beside it until then: a run cut short is put right at the next start, above)
```

Settings help example (Reset buttons — you will edit this string in WP5):

```
DraStic's buttons back to Banana's, for a button setup gone wrong in DraStic's menu: the pad as printed, F or L3 the menu, R2 save, L2 load, R3 fast forward. (HOME always opens the menu.) Press A twice.
```

After WP5 that must not say “R3 fast forward”.

---

## 4. Tests — run these, do not skip

| Change in | Command | What must hold |
|---|---|---|
| `ui/**` except only comments | `make test` from repo root | `ui/tests/run.sh` exit 0. PNGs in `ui/tests/out/`. |
| `ui/src/sync.cpp` | `sh ui/tests/sync.sh` | Existing RomM/Romhost cases still pass; add the new https cases from WP4. |
| `run-drastic`, `nds-game-option`, `nds-saves`, `rcK`, any `libexec/banana/*` | `sh -n <file>` | Parse. CI runs this on every script. |
| `run-drastic` `#117` | New fixture in WP1 (shell, no device) | Mapping 1→0, 2/3/4→1; restore after; crash-restore on next start. |
| `bananad.c`, `rcK` | `sudo system/bananad/tests/sleep-test.sh` | Existing cases. Add a “shutdown does not write mem” case if you touch suspend. |
| `drastic.cfg` | grep both control sets | `FAST_FORWARD` not 1036. Reset-buttons copies the same values. |
| Kernel fragment | **do not** land gadget in the same commit as a UI fix | PR `#101` only. |

If `make test` needs ImageMagick: `convert` must exist (CI installs it). Headless banana-ui is built by `make ui`.

**Do not** add a test that needs DraStic’s binary. SuperDrastic is not in this tree. Test the launcher’s sed against a fake `config/drastic.cfg`.

---

## 5. Execution order

Do not reorder unless a package is already in `main`.

| Order | Package | Issues | Why this order |
|---|---|---|---|
| 0 | Review open PRs | #101 #123 #73 #91 #94 | They are the sleep/LED/brick/boot work. Parallel rewrites waste the tree. |
| 1 | WP1 SuperDrastic per-game 3D res | #117 leftover of #27 | Small, local, copies an existing pattern. Unblocks closing #27. |
| 2 | WP2 Resume chip uses DS icons | #107 | Ten lines in `Library::system()`. |
| 3 | WP3 START not on the dark band | #128 | One geometry tweak. |
| 4 | WP4 banana-sync TLS | #109 | `probe()` + `sync.sh`. |
| 5 | WP5 SuperDrastic FF / hotkeys | #113 #77 | `drastic.cfg` + Reset-buttons copy + migrate 1036. |
| 6 | WP6 Help vs Back | #122 | Footer / game-page help. |
| 7 | WP7 Cart art contain | #108 | `cart()` blit. |
| 8 | WP8 Shutdown is not sleep | #46 leftover of #84 | `rcK` + bananad. Needs sleep-test. |
| 9 | WP9 nds-saves do not clobber | #96 | `nds-saves` empty/.sav vs .dsv. |
| 10 | WP10 Keys before the boot animation | #87 leftover of #86 | Startup order. |
| 11 | WP11 Settings lists off the UI thread | #41 | Time zone / themes / box-art. |
| 12 | WP12 Missing PortMaster folder | #78 leftover | Boot must not refuse. |
| 13 | WP13 BananaDS pause charging bolt | #35 leftover | BananaDS repo, not this tree. |
| — | Stop | — | Everything else is §8 (close on GitHub) or §9 (out of scope / product). |

After each WP: commit, `make test` if UI, `sh -n` if shell. Push when the human asks; do not open a kitchen-sink PR.

---

## 6. Phase 0 — open PRs (review, do not rewrite)

Clone is `JorreFog/BananaLinux`. For each PR: read the diff, run the tests that apply, leave review comments if needed. **Merge is a human action** unless you were told you may merge.

### PR #101 — sleep / dual-role OTG (`#79`, cluster B)

- Kernel: gadget/OTG so DWC3 is not stuck host-only; xHCI halt `-110` is the overnight drain.
- bananad: s2idle fallback for *that sleep only*, not a permanent demotion (0.5.0 already retries deep every time).
- **Must not** enable gadget in a way that keeps the host controller up in suspend.
- Hardware floor even when suspend works: ~0.5 %/h. `#85`’s Power-off-after-sleeping is the safety net, already in 0.5.0.
- `#64` white SuperDrastic screens after ~36 min: DraStic frame-timer; ignored since 0.5.0. Not this PR.

### PR #123 — `bananad --charge` (`#34` `#99` `#104` `#116`, cluster A)

- Amber LED is PWM on the RK817. Fully off, that rail is down → no light, even while the charger IC takes current.
- 0.5.0 gauge voltage floor should stop the stuck-percent lie. This PR is the LED-while-off path.
- **Must not** undo a real power-off (a tiny on-charger context that never suspends will drain). Confirm a true off still draws like stock off, not like dark-awake.
- Keep `#34` as the tracker until a long off-charge on 0.5.0+ shows the right percent *and* a light.

### PR #73 — settings fuzz (`#88`)

- Unexpected enums crash every boot; `pad()` with an empty filler can hang.
- Merge this before any other `settings.cpp` work if it still applies cleanly; otherwise rebase WP6/WP11 on it.

### PR #91 — trial boot (`#90`)

- Failed *update* (A/B slot), not a blank first flash (`#102` `#83` `#58` are install/docs).
- Trial boot then `DEFAULT` back to the old slot; breadcrumbs on the card.

### PR #94 — flight recorder (`#93`)

- Off by default. No secrets (Wi-Fi PSK, RA password, SMB password) in the tape.

Closed unmerged — **do not resurrect wholesale**:

- PR `#86` buttons: take only “keys live before the boot animation” into WP10. Skip the hardware-check toast unless asked.
- PR `#84` sleep extras: take only “pause suspend until poweroff finishes” into WP8. Skip LED-while-asleep / rewrite of sleep help.

---

## 7. Work packages

Each package: Goal, files, current code, patch, tests, Done-when, Do not.

---

### WP1 — `#117` SuperDrastic ignores per-game 3D resolution

**Goal.** X on a DS game → 3D resolution writes `/data/banana/nds-per-game-res.conf`. SuperDrastic must honour it for that run, then put Settings’ `hires_3d` back. BananaDS already works (`run-bananads` lines 109–110).

**Files**

- `system/rootfs/usr/libexec/banana/run-drastic` (edit)
- `system/rootfs/usr/libexec/banana/nds-game-option` (read only; already parses this file)
- `system/rootfs/usr/libexec/banana/run-bananads` (read only; the pattern for BananaDS)
- `ui/src/settings.cpp` `gameHelpOf` for `PG_RES` (edit the “BananaDS only” sentence)
- `ui/tests/run.sh` already writes `nds-per-game-res.conf`; it does **not** exec `run-drastic`. Add a shell fixture beside it or `system/rootfs/usr/libexec/banana/tests/` if that folder exists; otherwise a small `tools/` or `system/` test script that CI can `sh`.

**Current code — `run-drastic` already swaps `threaded_3d`.** Copy this block. It is at the end of the file (lines 144–163). The `hires_3d` swap belongs *immediately before* the `threaded_3d` swap, and both must share one `exec` vs `wait` path.

```
# the game's own Threaded 3D (#27): DraStic reads it from drastic.cfg alone, so the game's value goes in for this run
# and Settings' comes back after (kept beside it until then: a run cut short is put right at the next start, above)
t=$(/usr/libexec/banana/nds-game-option nds-per-game-3d.conf "$1")
was=$(sed -n '/^threaded_3d = /{s///p;q}' config/drastic.cfg)
if [ -z "$t" ] || [ -z "$was" ] || [ "$t" = "$was" ]; then
    exec $SD/superdrastic-run $D/drastic "$1"
fi
echo "run-drastic: ${1##*/} has its own Threaded 3D: $t" >&2
echo "$was" > config/.threaded_3d.settings && sync
sed -i "s/^threaded_3d = .*/threaded_3d = $t/" config/drastic.cfg
$SD/superdrastic-run $D/drastic "$1" &
...
sed -i "s/^threaded_3d = .*/threaded_3d = $was/" config/drastic.cfg && rm -f config/.threaded_3d.settings
exit $S
```

Crash recovery at the top of the same file (lines 19–25):

```
if [ -f config/.threaded_3d.settings ]; then
    t=$(cat config/.threaded_3d.settings)
    case $t in 0|1) sed -i "s/^threaded_3d = .*/threaded_3d = $t/" config/drastic.cfg ;; esac
    rm -f config/.threaded_3d.settings
fi
```

**`nds-game-option` usage** (already correct):

```
/usr/libexec/banana/nds-game-option nds-per-game-res.conf "$rom"
```

Prints `1` `2` `3` or `4`, or empty when the game follows Settings. File format: `<rom file name> = <value>` in `/data/banana/nds-per-game-res.conf`. Last line for that name wins.

**`run-bananads` already:**

```
r=$(/usr/libexec/banana/nds-game-option nds-per-game-res.conf "$1")
case $r in 1|2|3|4) echo "run-bananads: ${1##*/} has its own 3D resolution: ${r}x" >&2; export BANANADS_RENDER_SCALE=$r ;; esac
```

Do not change BananaDS in this package.

**Patch shape (required)**

1. Map the per-game res to DraStic’s bit:

   ```
   r=$(/usr/libexec/banana/nds-game-option nds-per-game-res.conf "$1")
   case $r in
     1) h=0 ;;
     2|3|4) h=1 ;;
     *) h= ;;
   esac
   ```

   SuperDrastic has no 3x/4x. 3 and 4 become `hires_3d = 1` (2x). Log it: `run-drastic: foo.nds has its own 3D resolution: ${r}x (hires_3d $h)`.

2. Read Settings’ current value: `was_h=$(sed -n '/^hires_3d = /{s///p;q}' config/drastic.cfg)` (actual key is `hires_3d`; confirm with `grep hires_3d` on `buildroot/board/rgds/drastic.cfg` and `/usr/share/drastic/config/drastic.cfg` if present).

3. **Combine with the threaded_3d early-exec.** Today if `t` equals `was`, the script `exec`s and never restores anything. After this patch, `exec` is only valid when *both* threaded_3d and hires_3d need no change. If either differs:

   - write `config/.threaded_3d.settings` as today if threaded changes
   - write `config/.hires_3d.settings` with the old `hires_3d` if hires changes (only `0` or `1`)
   - `sync` the marker files before `sed` on `drastic.cfg`
   - `sed` the lines that change
   - run SuperDrastic in the foreground-with-wait path (the existing `&` / `wait` / trap), not `exec`
   - restore both lines after `wait`
   - `rm -f` the marker files

4. At the top of `run-drastic`, restore `hires_3d` from `config/.hires_3d.settings` the same way as `threaded_3d` (drop an empty or non-0/1 marker).

5. **Do not** leave Settings’ `hires_3d` permanently changed. DraStic writes `drastic.cfg` on quit; restore after wait, as threaded_3d does.

6. Update `gameHelpOf` for `PG_RES` in `ui/src/settings.cpp` (search `row == PG_RES`). Current text starts “BananaDS only.” Change it so SuperDrastic is included: 1x/2x apply to both; 3x/4x are BananaDS only (SuperDrastic treats them as 2x). Keep Banana prose.

**Tests to add** (shell, no DraStic binary)

Create a throwaway tree:

```
D=/tmp/drastic-wp1
mkdir -p "$D/config" /tmp/banana-wp1
printf 'threaded_3d = 0\nhires_3d = 0\n' > "$D/config/drastic.cfg"
printf 'Mario Kart DS.nds = 2\n' > /tmp/banana-wp1/nds-per-game-res.conf
```

The real script uses `/data/banana` and `/data/drastic`. For the unit test, either:

- extract the mapping + restore into a tiny helper the launcher calls, or
- run the launcher with a stub `superdrastic-run` on `PATH` and override dirs if you add `DRASTIC_DIR` / `BANANA_DATA` — **do not add env knobs unless they already exist**. Prefer a stub copied from the restore/sed logic tested by sourcing a extracted function.

Minimum assertions:

| Case | conf | starting hires_3d | after stub-run cfg | after restore |
|---|---|---|---|---|
| no per-game line | empty | 0 | 0 | 0, no marker left |
| `= 1` | 1 | 1 | 1 (no swap) | 1 |
| `= 1` | 1 | 0 | 0 during run, then 0 | wait: during run 0 means 1x; map 1→0 so starting 0 is already right: no swap |
| `= 2` | 2 | 0 | 1 during run | 0 after, marker gone |
| `= 4` | 4 | 0 | 1 during run (not 4) | 0 |
| power cut | marker `1`, cfg `0` | — | next start writes 1 then deletes marker | |

Stub SuperDrastic: a shell script that greps `hires_3d` from cfg into a log, then exits 0. Point `SD` at it by editing only the test copy, not production, if you extract a function. If you cannot stub without refactoring, add a **comment-free** extracted function `apply_per_game_hires` in `run-drastic` and a 40-line test script that copies those functions. Keep production behaviour identical.

Also: `sh -n system/rootfs/usr/libexec/banana/run-drastic`.

`ui/tests/run.sh` already checks that Use 4x writes `Banana Kart DS.nds = 4`. Do not break that script (row order on the game page).

**Done-when**

- A game with `nds-per-game-res.conf` `= 2` (or 3 or 4) runs SuperDrastic with `hires_3d = 1` and Settings’ value is back afterwards.
- `= 1` runs with `hires_3d = 0` for that run.
- No per-game line: Settings’ value untouched, `exec` still used when threaded_3d also matches (no extra wait).
- Crash mid-run: next `run-drastic` restores Settings’ `hires_3d`.
- Game-page help no longer says the row is BananaDS only.
- `sh -n run-drastic` clean. `make test` still passes.

**Do not**

- Write `hires_3d = 2` or `3` or `4`.
- Change BananaDS.
- Patch `drastic.cfg` in the image for a global 2x default (Settings already owns `hires_3d`).
- Call DraStic’s menu.

---

### WP2 — `#107` Last-played chip ignores DS cartridge icons

**Goal.** Settings > Display & sound > DS cartridges = Game icon (`ds_cart_art=icon`) already fills the reel via `Library::game()`. The top-screen Last-played chip is filled by `Library::system()` and still uses box/cart/label. When `dsCartIcons` is on and the resume game is a DS ROM, the chip must show `romIconCover` the same way.

**Files**

- `ui/src/library.cpp` `Library::system()` around lines 841–851
- `ui/src/library.cpp` `Library::game()` around 871–878 (the pattern to copy)
- `ui/src/library.h` `bool dsCartIcons` (already exists, set from `ds_cart_art == "icon"` in `ui/src/main.cpp`)
- `ui/rnds/RndsUI.cpp` resume chip uses `sy.resumeCart, sy.resumeLabel, sy.resumeBox` (around 1766). You should not need to change the draw if the fields are right.

**Current `system()` (wrong for icons):**

```
r.resumeBox = g.box; r.resumeCart = g.cart; r.resumeLabel = g.label;
if (g.box.empty() && g.cart.empty() && g.label.empty() && isDsRom(g.path, s.def.id))
    r.resumeLabel = romIconCover(dataDir, g.path);	// the game's own icon on the chip's cartridge
```

**Current `game()` (right):**

```
if (ext == "nds" || (ext == "zip" && systemOf(i).id == "nds"))
{
    if (dsCartIcons || g.box.empty()) r.rom = g.path;
    if (dsCartIcons) r.cartArt = romIconCover(dataDir, g.path);
}
```

`isDsRom` is already in `library.cpp`:

```
static bool isDsRom(const std::string& path, const std::string& system)
{
    ...
    return ext == "nds" || (ext == "zip" && system == "nds");
}
```

**Patch.** Inside the `if (res >= 0)` block of `Library::system()`, after filling resumeBox/Cart/Label:

```
if (dsCartIcons && isDsRom(g.path, s.def.id))
{
    r.resumeLabel = romIconCover(dataDir, g.path);	// Settings > DS cartridges: Game icon, as the reel
    // a ROM without a banner icon: leave box/cart so the chip is not blank
}
```

Match `game()`: when `dsCartIcons` is set, prefer the icon even if box art exists. If `romIconCover` returns `""`, keep `resumeBox`/`resumeCart` so the chip still has art.

Also set whatever field the chip actually draws as the cartridge face. The draw is:

```
Sprite c = cart(CartArt{ sy.resumeCart, sy.resumeLabel, sy.resumeBox, "", sy.icon }, 54, 56);
```

`CartArt` order in `artOf` is `{ g.cart, g.label, g.box, g.rom, g.icon }`. For the reel, `game()` sets `r.cartArt` (→ `g.cart` path in GameInfo — check `GameInfo` in `RndsUI.h`). Read `struct GameInfo` / `struct SysInfo`:

- `GameInfo.cartArt` is the icon override used as cartridge art.
- `SysInfo` has `resumeBox`, `resumeCart`, `resumeLabel` — **no** `resumeRom`.

So putting the icon path in `resumeLabel` is what the empty-art fallback already did. When `dsCartIcons` is on, set `resumeLabel` to `romIconCover(...)` **and** clear or keep box? `cart()` uses `labelArt(art0)` which prefers scan, then label, then box-without-strip. If `resumeBox` is still the title screen, `labelArt` may ignore `resumeLabel`. **Read `labelArt()`** (`RndsUI.cpp` ~699) before patching:

```
// made from the cover, else the cover without its NINTENDO DS strip
```

If `labelArt` prefers `box` over `label`, you must clear `resumeBox`/`resumeCart` when applying the icon, **or** pass the ROM path. SysInfo has no `resumeRom`. Prefer: when `dsCartIcons && isDsRom`, set `resumeLabel = romIconCover(...)`, and if that string is non-empty, set `resumeBox` and `resumeCart` to `""` so `labelArt` cannot prefer the title screen.

**Tests.** In `ui/tests/run.sh` there is already `romicon` in the long `--script`. Add:

1. Fixture: a DS ROM with a banner icon (the fixture already has `Banana Kart DS.nds` / `Yellow Planet`).
2. `settings.conf`: `ds_cart_art = icon`.
3. Script: open home, `shot resume-icon`.
4. Assert the top PNG is not identical to the title-screen chip (or that a known icon colour is present). If pixel asserts are brittle, at least assert the code path with a log line you add only if the tree already logs library decisions — **do not add noisy logs**. Prefer a screenshot diff against a golden if run.sh already does that for other shots; if not, a comment in the test that `ds_cart_art=icon` was set and the menu rendered is acceptable plus a visual `shot`.

`make test` must stay green on 640x480 and 1024x768.

**Done-when**

- With `ds_cart_art=icon`, Last played on the system page shows the same icon family as the reel, not the scraped title screen.
- With the default (box art), behaviour is unchanged.
- Empty banner: chip still shows box art, not a blank cart.

**Do not** change `romIconCover` scaling. Do not apply icons to non-DS systems.

---

### WP3 — `#128` START sits on the plate’s dark band

**Goal.** The START word is drawn on the dark bottom band of the home/library plate, so dark ink looks cut. Keep the plate 124×162.

**Files:** `ui/rnds/RndsUI.cpp`

`plateBox()` (~1392):

```
b.bands = { Band{ 0, 14, Color::hex(accent.light) }, Band{ 132, -1, Color::hex(accent.dark) } };
```

Plate draw (~1965): `drawBox(plateBox(), 258, ry + 4, 124, 162);`

START (~2026):

```
drawFit(std::string("START"), tsr, 320 - 54, ry + 124, 24, 108, Color::hex(inkOn(accent.base)), true, true);
```

The plate’s top is `ry+4`, height 162, so the plate occupies y `ry+4` .. `ry+166`. Local y 132 of the plate is screen y `ry+4+132 = ry+136`. START is at `ry+124` with height 24 → it spans `ry+124` .. `ry+148`, which crosses the band at `ry+136`.

**Preferred patch (smallest):** move START up so its box sits fully in `accent.base`, above y 132 of the plate.

- Plate local y of the text box should end at ≤ 130 (2 px gap before the band), or the band should start lower.
- `drawFit(..., ry + 124, 24, ...)` → try `ry + 112` (local y 108: 108+24=132, flush with the band — still tight). Better: **`ry + 108`** (local 104; 104+24=128, 4 px above the band).
- If translations of START are taller, `drawFit` already scrolls in width; height stays 24.

**Alternative:** drop the bottom band (`bands` only the top 0–14) or start it at 150. Only do this if moving START collides with the centre cartridge (cartridge is at `ry+16` size 96 → occupies ~`ry+16`..`ry+112`). `ry+108` for START sits just below that. **Do not** change plate size 124×162. **Do not** change radius/border.

**Tests.** `ui/tests/run.sh` already `shot home` and `shot lib`. After the change, those PNGs will differ. If the test compares pixels against goldens, update goldens; if it only checks that files are non-empty / stddev, you are fine. Add a comment in the commit: START’s baseline is above the dark band.

On 640x480 and 1024x768 (run.sh loops both).

**Done-when**

- START letters sit on `accent.base`, not `accent.dark`.
- Plate still 124×162.
- Empty library still has no plate (existing `n > 0` guard).

**Do not** restyle the whole plate, remove the top light band, or change the reel geometry.

---

### WP4 — `#109` `probe()` never tries https / :443

**Goal.** Typing a RomM host without a scheme/port must find a TLS server on 443. Optional skip-verify for a LAN self-signed cert, **off by default**.

**Files**

- `ui/src/sync.cpp` `normUrl`, `probe` (~1232–1299), `http()` (~282)
- `ui/tests/sync.sh`
- Settings row: `ui/src/settings.cpp` Network / Home server group (search `hs_server`)

**Current `probe()`:**

```
bool hasPort = a.find(':', a.find("://") == std::string::npos ? 0 : a.find("://") + 3) != std::string::npos;
if (hasPort || a.find("://") != std::string::npos) tries.push_back(normUrl(a, 0));
if (!hasPort) { tries.push_back(normUrl(a, 8092)); tries.push_back(normUrl(a, 8080)); tries.push_back(normUrl(a, 80)); }
```

`normUrl` prepends `http://` when there is no scheme. So `romm.lan` becomes `http://romm.lan:8092` etc. Never `https://romm.lan` or `:443`.

**Current `http()`:** libcurl via dlopen. The option enum (~204) has **no** `O_SSL_VERIFYPEER`. curl’s `CURLOPT_SSL_VERIFYPEER` is **64**, `CURLOPT_SSL_VERIFYHOST` is **81**. Add them to the enum when you implement skip-verify. Do not include `<curl/curl.h>` if the file does not already (it uses numeric CURLOPT values on purpose).

**Patch**

1. In `probe()`, when `!hasPort` and no scheme, after the http tries (or interleaved, https first for :443 to avoid a 400 on an https-only host that still answers on 80):

   Preferred try order for a bare host `h`:
   1. `http://h:8092` (Romhost default — keep first so LAN Romhost stays fast)
   2. `http://h:8080`
   3. `http://h:80`
   4. `https://h` (443 implied)
   5. `https://h:443` only if (4) is not already that — `https://h` is enough if `normUrl` does not force a port. For `normUrl("https://h", 0)`: no extra port. Good.
   6. If the user typed `https://h` with no port, do **not** also hammer :8092.

2. If the address already has `https://`, use it as-is (`hasPort` / scheme branch already `normUrl(a, 0)`). Do not rewrite https → http.

3. IPv6: `hasPort` using `find(':')` is already wrong for `::1`. **Do not try to fix IPv6 in this package** unless a test already covers it. Mention in the commit body.

4. Skip-verify: Settings key e.g. `homesync_tls = verify` (default) / `off`. Read it in `http()` from the same conf `probe`’s process already uses (`banana/settings.conf`). When `off`: `cu.setopt(h, O_SSL_VERIFYPEER, 0L); cu.setopt(h, O_SSL_VERIFYHOST, 0L);`. Help text: “Off: a Home server with a certificate the handheld has no CA for (a name on the LAN). Leave On on a network you do not trust.” Default On.

5. Do not skip-verify globally for banana-scrape / RA login.

**Tests in `ui/tests/sync.sh`**

Existing: `bs ra probe "127.0.0.1:$P"` against `fake-romm.py` on http. Keep it.

Add:

- A Python `http.server` or extend `fake-romm.py` with TLS (self-signed). `ssl.wrap_socket` / `ssl.SSLContext`. Probe `127.0.0.1` **without** a port must not be required to find it if http :8092 is tried first and something else is there — so bind TLS on a dedicated port and `probe https://127.0.0.1:$TLS`.
- Assert `probe https://127.0.0.1:$TLS` returns `romm`.
- Assert `probe 127.0.0.1:$TLS` (http to a TLS port) is `none` or not a hang (timeout is 5 s per try; keep the extra tries small).
- With skip-verify off, a self-signed cert may fail verify — that is correct. The skip-verify on case should succeed.

If adding a real TLS server is painful in the test harness, a **minimum** is: unit-test the try-list construction by making `probe` log tries when `BANANA_SYNC_DEBUG=1` (only if such a flag exists). Prefer a real TLS fake.

`make test` does not run `sync.sh`. You must run `sh ui/tests/sync.sh` yourself. If CI does not run it, still add the cases.

**Done-when**

- `https://host` and `https://host:443` are probed.
- Bare `host` still finds Romhost on 8092 first.
- Skip-verify is a Settings toggle, default verify on.
- `ui/tests/sync.sh` passes.

**Do not** send RomM passwords in logs. Do not follow http→https redirects in a way that downgrades later.

---

### WP5 — `#113` + `#77` SuperDrastic fast-forward and hotkeys

**Goal.** The Plus has no R3. Stock FF is `1036` (R3). BananaDS FF is hold R2. SuperDrastic must FF on a Plus-present control. Do not open DraStic’s mapper. Optionally move save/load off L2/R2 so R2 can be FF without losing save (see below).

**Files**

- `buildroot/board/rgds/drastic.cfg` — both `controls_a` and `controls_b`:
  - `CONTROL_INDEX_SAVE_STATE = 1031` (R2)
  - `CONTROL_INDEX_LOAD_STATE = 1030` (L2)
  - `CONTROL_INDEX_FAST_FORWARD = 1036` (R3) ← the bug
  - Menu: `1035` / `1034` (F / L3)
- `system/rootfs/usr/libexec/banana/run-drastic` — hinge migrate pattern to copy for 1036
- `ui/src/settings.cpp` Reset buttons help (~575) and the reset action that copies stock maps

**Scancodes (from this cfg):**

| Control | Code | Plus physical |
|---|---|---|
| L2 | 1030 | yes |
| R2 | 1031 | yes |
| L3 | 1034 | yes (stick press) |
| R3 | 1036 | **Plus: no** |
| F / HOME | 1035 | yes |

**Decision (do not invent a third layout):**

**Preferred, matches BananaDS:** FF = R2 (`1031`). Then save/load cannot stay on R2.

Combined with `#77` (F+L / F+R style):

- FF: `1031` (R2), same as BananaDS hold-R2
- Save / load: leave unbound in DraStic (`65535`) and document F-combos **only if SuperDrastic actually sees F as a modifier**. DraStic maps are one key per action, not chords. **Do not claim F+L saves unless the wrapper implements chords.**

Safer layout that needs no chords:

- FF: R2 (`1031`)
- Save: L2 (`1030`) — swap: used to be load
- Load: unbind (`65535`) or keep load on a combo the wrapper already has

**Better safer layout (document in Settings):**

- FF = R2 (`1031`) on **both** a and b
- Save stays R2? **Conflict.** So save must move.

Joar’s BananaDS: hold R2 = FF. SuperDrastic should feel the same: **R2 = FF**. Move save off R2.

Recommended maps to write in `drastic.cfg` **and** in Reset buttons:

- `FAST_FORWARD = 1031` (R2)
- `SAVE_STATE = 65535` (unbind) **or** if a chord-free key remains: do not use R3
- `LOAD_STATE = 65535` (unbind)

Then `#77` is “use the in-game menu (F/L3) for save/load”, which already exists. Update help: “F or L3 the menu (save and load there), R2 fast forward.”

If you can bind save/load to keys the Plus has that are currently `65535` and not used in-game: there are none on the face that are free besides the menu. **Do not** steal A/B/X/Y/L/R/Start/Select.

**Migrate existing cards** in `run-drastic`, next to the hinge block (~26–31):

```
# Fast forward was R3 (1036); the Plus has none (#113): R2 (1031), as BananaDS
if grep -q '^controls_a\[CONTROL_INDEX_FAST_FORWARD\] = 1036' config/drastic.cfg 2>/dev/null; then
    sed -i 's/^controls_a\[CONTROL_INDEX_FAST_FORWARD\] = 1036/controls_a[CONTROL_INDEX_FAST_FORWARD] = 1031/' config/drastic.cfg
fi
# same for controls_b
# if SAVE_STATE is also 1031, unbind save so FF and save are not the same key
```

Only migrate **1036 → 1031**. If the user already remapped FF to something else, leave it.

If after migration FF and SAVE are both 1031, unbind SAVE (`65535`) and LOAD can stay 1030 (L2) — load without save is odd. Unbind both save and load if they collide with FF; menu still saves.

**Reset buttons** must copy the new defaults, not 1036. Find the implementation of `nds-reset-buttons` (search that key in `settings.cpp`) and point it at the image `drastic.cfg`.

Help string (~575) must drop “R3 fast forward”. New:

```
DraStic's buttons back to Banana's, for a button setup gone wrong in DraStic's menu: the pad as printed, F or L3 the menu (save and load there), R2 fast forward. (HOME always opens the menu.) Press A twice.
```

**Tests**

- `grep FAST_FORWARD buildroot/board/rgds/drastic.cfg` → not 1036, is 1031, both a and b.
- `sh -n run-drastic`.
- A tiny shell test of the sed migrate: cfg with 1036 becomes 1031; cfg with 1029 is left alone.

**Done-when**

- New cards: FF on R2, no R3.
- Old cards with 1036: migrated on next SuperDrastic launch.
- Reset buttons help and action match.
- No UI copy tells the user to open DraStic’s control editor.

**Do not** add a Settings row that writes arbitrary scancodes unless it reuses an existing choice widget. Do not bind FF to L3 (that is the menu).

---

### WP6 — `#122` Help / description runs into Back

**Goal.** A long description on a settings-like bottom screen must never draw through the Back chip.

**Where to look (in order)**

1. `Settings::render()` bottom (~2128–2188): rows + `face("B","Back",18,bw)` + A chip. There is **no** help paragraph here; help is on the top via `helpOf`. If the bug is a **translation** of a row label overflowing into Back, `textFit` on the label already clips — check a long locale.
2. `Settings::renderGame()` (~4931–4955): same pattern. Help is on the top (`gameHelpOf`).
3. Search `beginScreen(1)` together with `help` / `wrapped(` / `helpText` in `settings.cpp` and `RndsUI.cpp`.
4. Emulator-options page: the game page `PG_RES` / `PG_SHADER` values are long (“As in Settings (4x (experimental))”) and use `fit(..., 380)` — a long translation of “As in Settings…” can still sit above Back, not through it.

If the reporter’s screenshot is the **top** help looking cramped: that is `helpFit` / `HELP_BOTTOM`, already supposed to shrink to 16 px. Do not invent a bottom help line.

**If you find a bottom-screen sentence** that is not clipped to `[18+bw+8, 622-aw-8]`: clip it. Preferred: **delete** the bottom sentence if the top already has `helpOf`.

**Also in this package (cheap, related):** `gameHelpOf` `PG_RES` “BananaDS only” — if WP1 did not land yet, do it there not here.

**Tests.** `make test`. If you change footer geometry, `shot settings` / `shot settings-list` still exist. For a long string: `--script` onto Nintendo DS > Shader (long help) and assert Back’s pixels are still the chip (optional).

**Done-when.** Back’s letters are not overdrawn by a description on 640x480 and 1024x768. No new help on the bottom screen.

**Do not** move Back. Do not shrink `face()` globally.

---

### WP7 — `#108` Cartridge art: contain + pad, not cover-crop

**Goal.** Title screens on the cartridge label are `coverTop` (aspect preserved, bottom cropped). Reporter wants contain + white (or label-colour) pad, plus the existing Display row for icon vs art.

**Files:** `ui/rnds/RndsUI.cpp` `cart()` ~762–768:

```
if (!art.empty())
{
    int wx = (int)std::lround(4 * sc), wy = (int)std::lround(4 * sc);
    int ww = (int)std::lround(96 * sc) - wx, wh = (int)std::lround(90 * sc) - wy;
    blit(master, coverTop(art, ww, wh), wx, wy);
}
```

`containSize` already exists in `RndsRaster.cpp`. `coverArt()` for the top-screen box already uses contain into a square. **Reuse that idea.**

**Patch**

1. Fill the label window with white (title screens) or `P.cartLabel` (already used for icon carts) — white was requested; use **white** for title/box-on-label so letterbox bars match a DS label, unless the palette’s `cartLabel` is the house fill. Prefer `P.cartLabel` if it is already the label colour (icon carts use it). Do not introduce a new theme key.
2. `containSize(art.w, art.h, ww, wh, fw, fh)` then `resize(art, fw, fh)` and `blit` centred in the window: `wx + (ww-fw)/2`, `wy + (wh-fh)/2`.
3. Keep `iconCart` as it is (already contain-like, nearest-neighbour).
4. Cache key already includes art paths; changing the blit invalidates by code change (same key, different pixels) — **bump the key prefix** from `"cart:"` to `"cart2:"` so old cache files are not reused if any on-disk cache exists. If the cache is in-memory only, still bump the prefix.

**Settings.** “A Display row already picks icon vs art (`#106`). Add Box / Title / Icon if still missing.” Check Settings > Display & sound > DS cartridges. If it is only box vs icon, do **not** add a third mode in this package unless it is a one-line enum already plumbed. Contain applies to whichever picture is on the label.

**Tests.** `make test`; `shot lib` / `shot ready`. Visual: a wide title screen should show side pads, not a cropped centre.

**Done-when.** Label shows the whole picture, padded; icons still pixel-scaled. No cover-crop of title screens on carts.

**Do not** change top-screen `coverArt()` (that is contain already into 214×214). Do not bilinear-upscale 32×32 ROM icons.

---

### WP8 — `#46` Shutdown becomes sleep

**Goal.** A unit that was shut down is found asleep hours later. `rcK` can let bananad suspend again while powering off. 0.5.0 added a Power menu and shutdown animation; it did **not** stop re-sleep.

**Files**

- `system/rootfs/etc/init.d/rcK` (current: TERM games, umount `/data`, **no** suspend guard)
- `system/bananad/bananad.c` `shutdown_now()` (~766), `suspend_now()`, lid/power handlers
- `system/bananad/tests/sleep-test.sh`

**Current `shutdown_now`:**

```
sync();
if (kill(1, SIGUSR2)) reboot(RB_POWER_OFF);
```

busybox init runs `rcK` on SIGUSR2. Meanwhile the lid is still closed → `suspend_now("lid still closed")` can fire from the main loop if shutdown is slow.

**Patch (the useful part of closed PR #84)**

1. A flag file `/run/banana/powering-off` (or a bananad-internal `shutting_down`) set at the start of `shutdown_now`, before SIGTERM to the game.
2. Every `suspend_now(...)` path returns immediately if that flag is set. Also skip `lid still closed` retry.
3. `rcK`: as the first lines, `killall -STOP bananad` or write the flag if bananad did not, so a lid event cannot sleep after umount. **Do not** `kill -9 bananad` before it has TERMed the game — `shutdown_now` already waits up to 5 s for the game.
4. After umount, `reboot -f -p` / `RB_POWER_OFF` remains init’s job. Do not call `echo mem > /sys/power/state` from `rcK`.

**Tests.** Extend `sleep-test.sh`: trigger shutdown with the fake lid closed; assert `/sys/power/state` was **not** written `mem` after the shutdown path started. The test already namespaces `/sys/power/state` as a FIFO.

**Done-when**

- Lid closed + Shut down does not write `mem` after `shutdown_now` starts.
- Sleep from the Power menu still sleeps.
- `sh -n rcK`; `sudo system/bananad/tests/sleep-test.sh`.

**Do not** revive PR #84’s extra LED/idle work. Do not change Power-off-after-sleeping (`#85`, already shipped).

---

### WP9 — `#96` SuperDrastic SM64DS not saving

**Goal.** In-game save missing after reload in SuperDrastic; BananaDS fine. Likely `nds-saves` copying the wrong way: a newer empty `.sav` overwriting a good `.dsv`, or name mismatch.

**Files:** `system/rootfs/usr/libexec/banana/nds-saves`

**Current rule:** whichever of `.dsv` / `.sav` is newer (`-nt`) overwrites the other. A 0-byte or tiny `.sav` that is newer wins.

**Patch**

1. Never replace a non-empty `.dsv` with a `.sav` that is empty or smaller than 512 bytes (DS saves are kilobytes; a 0-byte file is not a save).
2. Never replace a non-empty `.sav` with a 0-byte `.dsv`.
3. If sizes differ by more than the DeSmuME footer (122 bytes) **and** the newer file is smaller than 64 bytes, keep the larger.
4. Log the decision (the script already echoes `nds-saves: $G: ...`).
5. Do not change the ROCKNIX-beside-ROM import (`#25`) except to refuse a 0-byte source.

**Tests.** A 20-line shell test:

```
DSV has 8192 bytes of 'x', SAV is 0 bytes and newer → after nds-saves, DSV unchanged, SAV becomes the raw of DSV (or SAV left empty but DSV not destroyed — prefer fill SAV from DSV, do not wipe DSV).
```

Run `nds-saves` with paths: the script hardcodes `/data/drastic` and `/data/saves/nds`. For a test, either bind-mount `/data` in a tmp dir (root) or add `BANANA_DATA` **only if you must** — prefer `mkdir -p /tmp/fake-data` and run under `fakeroot`/`unshare -m` if CI allows. If you cannot remap `/data`, extract `raw_of_dsv` tests separately and add a comment that on-device is required for the full script. **Best:** at the top of `nds-saves`, `DATA=${BANANA_DATA:-/data}` and use `$DATA/drastic/backup` — a one-line indirection that matches how other banana scripts sometimes use env in tests (`BANANA_NDS_SAVES` already exists for sync.sh). Use `BANANA_DATA` if you add it; document it in the script header.

**Done-when.** A non-empty `.dsv` is never replaced by an empty `.sav`. SM64DS in SuperDrastic keeps in-game saves across a quit (on-device check in the commit body).

**Do not** merge saves bytewise. Do not rename `.dsv` to include the ROM ID unless you prove SM64DS uses a different stem than `${G%.*}`.

---

### WP10 — `#87` Power/volume dead during boot animation

**Goal.** Menu is up before bananad owns power/volume, so those keys do nothing for a few seconds.

**Files.** Search startup: `system/rootfs/etc/init.d/rcS`, `system/rootfs/usr/libexec/banana/` boot, `system/bananad/bananad.c` evdev grab, `ui/src/main.cpp` splash.

**Patch (the useful part of closed PR #86)**

- Start bananad’s input grab **before** banana-ui paints the boot animation, **or**
- banana-ui handles volume/power until it receives a “bananad ready” signal (if one exists — do not invent a protocol if `/run/banana/` already has a socket/flag).

Prefer: init order. If `rcS` starts banana-ui then bananad, swap them. If banana-ui is started first so it can show a splash while bananad opens devices, then banana-ui must pass volume to ALSA itself for that window (there may already be a path — search `volume` in `main.cpp`).

**Do not** resurrect the hardware-check toast from #86 unless asked.

**Tests.** Hard without a device. `sleep-test.sh` plus a note. If there is a boot script test, add “bananad pid exists before banana-ui” by reading `rcS`.

**Done-when.** `rcS` / unit files show bananad grabbing input before the splash, or banana-ui handles volume until then. Power during splash still shuts down/sleeps as designed (do not eat the power key).

---

### WP11 — `#41` Time zone / themes / box-art hitch the UI thread

**Goal.** Opening those rows hitch 15s+ because they walk large lists or the card on the UI thread.

**Files:** `ui/src/settings.cpp` — Time zone `ZONES` (~651), Theme list (~476, ~752), Box art scrape UI.

**Patch**

- Build timezone names once (they are already a static `ZONES` table — if the hitch is `tzset` / scanning zoneinfo, cache).
- Themes: the comment at ~752 says themes on the card are listed live. Enumerate `banana/themes` on a worker (the UI already has workers for carts). Keep the last list; refresh on a timer or when the page opens, not in `draw`.
- Box art source row should not scrape on highlight.

**Never** scan the card inside the row’s draw/open on the UI thread.

**Tests.** `make test`. `shot settings-tz` already exists in run.sh (`press down; press right; shot settings-tz`). Must still open.

**Done-when.** Opening Time zone / Theme / Box art from does not block rendering for multiple frames in headless (if you can assert frame time in the log; run.sh has `bench 120`). Do not add threads if a static cache is enough.

---

### WP12 — `#78` Missing PortMaster folder must not refuse boot

**Goal.** 0.5.0 can hide systems. Deleting the PortMaster folder once refused boot. Guard so a missing `roms/ports` / `PortMaster.sh` cannot brick the menu.

**Files:** `ui/src/library.cpp` ~629 (PortMaster counts as launchable), scan/mkdir of system folders; `ui/src/main.cpp` startup; `systems.conf` ports entry.

**Patch.** If the ports folder is missing, treat the shelf as empty (or Always + PortMaster gone → empty), **log**, and continue. Never `exit` the menu because `opendir` failed on one system. `mkdir` the folder only if the rest of the tree already creates empty system dirs at first boot — do not start creating every system folder here.

**Tests.** `ui/tests/ux.sh` already cares about PortMaster on the home row. Add: fixture **without** `roms/ports`; banana-ui `--script "home 0; shot home"` must exit 0.

**Done-when.** Missing ports dir: menu boots, Ports hidden or empty, not a crash.

---

### WP13 — `#35` Charging bolt missing in BananaDS pause

**Goal.** 0.4.2 home bolt; 0.5.0 banana-ui pause menus. Reporter still sees no bolt in **BananaDS’s** in-game menu.

**BananaDS is not in this tree.** Pin: `buildroot/package/bananads/bananads.mk` `BANANADS_VERSION`. Sources: `https://github.com/JorreFog/SuperDrastic` subdirectory `bananads/`.

**Patch (in SuperDrastic/BananaDS, then bump the pin)**

- Pass charger-online into the pause HUD the way banana-ui does (`Library::power().charging` uses `.../online` because the rk817 gauge says Discharging while it charges — `library.cpp` ~937).
- Draw a bolt using the same 8-row `BOLT` bitmap as `RndsUI.cpp` ~1486, or BananaDS’s own icon if it has a battery widget.
- Do not use gauge status alone.

**This package is a different repo.** If you only have BananaLinux: document the required BananaDS change in the commit that bumps `BANANADS_VERSION` once the emulator PR exists. Do not vendor BananaDS into BananaLinux.

---

## 8. Close on GitHub (no code)

Behaviour already in 0.4.2 or 0.5.0. Close after a one-line check; do not “fix” them.

| Issue | Why close |
|---|---|
| `#70` | About shows kernel + RetroArch. |
| `#69` | OTA from GitHub. |
| `#40` | A on a saved SSID joins with stored PSK. |
| `#39` | SDL_BANANA_PANELS zero-copy in 0.4.2. |
| `#36` | Update explains a bad SHA. |
| `#30` | DS name in setup. |
| `#26` | Threaded 3D in Settings. |
| `#23` | SSH under Network. |
| `#18` | Volume ear curve — close if 0.4.2+ still sounds right. |
| `#65` | sd2 scan lock vs ksmbd. |
| `#8` | Sleep-on-charger LED: dark-awake since 0.4.2. **Confirm**, then close. Off-charge LED stays `#34`/`#123`. |
| `#27` | Close when WP1 (`#117`) lands. |
| `#106` `#111` `#115` `#126` `#98` | Already closed or shipped. |

Kitchen-sink `#49`: split remaining bullets (`#121` folders, `#120` grid, `#56` RA) and close `#49`.

---

## 9. Out of scope for this pass (do not start)

| Issue | Why |
|---|---|
| `#103` PSP | New core + shelf. RK3566 is marginal. |
| `#100` dsperate | New emulator contract, not a SuperDrastic swap. |
| `#125` undervolt | Needs per-board OPP characterisation; can hard-lock. |
| `#38` YouTube | Store/PortMaster app, ToS/codec. |
| `#130` MTP | Needs gadget after PR `#101`, then a Settings toggle. |
| `#120` `#121` `#49` grid/folders | Product UI, not a bug. |
| `#74` two colour curves | VOP2 has **one** `GAMMA_LUT`. Same table on both CRTCs maybe; two independent curves no. |
| `#129` green tints | Bisect LUT / panel-tune / DTS vs ROCKNIX first. Do not guess a LUT. |
| `#131` charge heat | Measure board temp; then maybe lower RK817 current. Do not guess. |
| `#114` `#76` shaders | BananaDS integer-then-`.frag`. Other repo. |
| `#112` capital H | Font glyph. Screenshot at native size. |
| `#118` one-bud BT | Log bluez-alsa PCM channels first; then SBC stereo / reject 1ch. |
| `#105` `#97` `#95` `#82` `#92` `#67` | DraStic / melonDS / Gengis core bugs. Per-game Threaded 3D Off is the workaround for swapped screens. |
| `#48` RA hardcore | Needs core enforcement. |
| `#56` RA browser | Large UI. |
| `#55` `#52` remap/unmap | Related to WP5; full remap table is a feature. |
| `#54` ROCKNIX overlay | BananaDS FPS extend; SuperDrastic will not grow one. |
| `#102` `#83` `#58` | Install/U-Boot at sector 64. Document writer; not runtime. |
| `#64` white screens | DraStic timer; ignored since 0.5.0. |
| `#71` RA speed vs ROCKNIX | 0.5.0 leftovers (threaded video default). Measure first. |
| `#14` Spirit Tracks mic | 0.5.0.1 path shipped; remaining is that title’s sampling. |
| `#110` FF mute | Document in in-game help; optional quiet mix later. |

---

## 10. Commit messages

Subject: `fix: ...` or `ui: ...` or `drastic: ...`, 72 chars, issue number.

Body: cluster numbers, on-device check the human should run, tests you ran.

Example for WP1:

```
fix: SuperDrastic honours per-game 3D resolution (#117)

nds-per-game-res.conf was only read by run-bananads. run-drastic now
swaps hires_3d for that run (1→0, 2/3/4→1) and puts Settings' value
back, same as threaded_3d.

On device: X on a DS game, 3D resolution 2x, SuperDrastic, confirm
sharp 3D; then Settings' 1x still applies to the next game without a
line in the file.
```

One issue (or one cluster) per commit. Do not mix WP1 and WP5.

---

## 11. Quick file index for remaining “cause known” tickets

Use this when a package above is done and the human asks for the next leftover.

| Issue | File:symbol | One-line fix |
|---|---|---|
| `#75` | `settings.cpp` `bottom_idle_mode` | Add a Dark panel choice that ignores the light theme. |
| `#45` | Settings > Nintendo DS | Favourite colour / birthday / region. |
| `#29` leftover | library titles | Show edited Name, not only Sort, after scrape. |
| `#28` leftover | box art search | Preview art before apply. |
| `#25` leftover | ksmbd | Windows tree connect / signing. |
| `#47` | RA HTTP in `settings.cpp` ~5262 | Retry 502; persist token; wait for time. |
| `#118` | `bananad.c` `bt_audio_read` | Log PCM channels; force SBC stereo. |
| `#87` | `rcS` / bananad grab | WP10. |
| `#41` | `settings.cpp` lists | WP11. |
| `#78` | `library.cpp` scan | WP12. |
| `#35` | BananaDS HUD | WP13. |
| `#122` | footer | WP6. |
| `#108` | `cart()` | WP7. |
| `#46` | `rcK` / `shutdown_now` | WP8. |
| `#96` | `nds-saves` | WP9. |
| `#113` `#77` | `drastic.cfg` | WP5. |
| `#117` | `run-drastic` | WP1. |
| `#107` | `Library::system` | WP2. |
| `#128` | `plateBox` / START y | WP3. |
| `#109` | `probe()` | WP4. |

---

## 12. Definition of a finished agent pass

You are done with **this** playbook’s first pass when WP1–WP7 are in `main` or in review PRs, Phase 0 PRs are reviewed (not necessarily merged by you), and WP8–WP12 have either a patch or a one-paragraph blocker (e.g. “needs device”). WP13 is a BananaDS pin bump.

Do not start `#120`/`#121`/`#103` in the same pass.
