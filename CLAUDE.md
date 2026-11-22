# BananaOS, for coding agents

A Buildroot Linux for the Anbernic RG DS and RG DS Plus (two screens, a lid, RK3568). Everything but the image
itself builds and is tested on a PC. People's side of this page: [CONTRIBUTING.md](CONTRIBUTING.md). How it works:
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (long; search it for the part you touch).

## Where things are

| Path | What |
|---|---|
| `ui/src` | `banana-ui`, the menu and Settings (C++17), and its helpers: `banana-fetch` (`src/fetch`), `banana-bshare`, `banana-sync`, `banana-scrape` |
| `ui/rnds`, `ui/third_party` | imported code (ROCKNIXDS Pixel's engine by `tools/import-rnds.sh`; rcheevos, stb): don't edit |
| `ui/tests` | `run.sh` (the headless suite) and what it sources; `*-test.cpp` unit tests built by `ui/Makefile` |
| `system/bananad` | `bananad.c` (sleep, lid, power, sound routing, battery) and `banana-panel-tune.c`, C, one file each |
| `system/rootfs` | the root overlay: `etc/init.d/rcS`, the launchers and scripts in `usr/libexec/banana` (busybox ash), default settings in `etc/banana` |
| `buildroot/` | the external tree: `configs/*_defconfig`, `package/*`, `board/rgds` (kernel and U-Boot patches, device trees, image layout) |
| `store/` | the Store's `catalog.json`, the apps that come with the image (`apps/activity`) |
| `tools/` | generators and checks run on a PC: translations (`lang-*.py`), `companions/validate.py`, `qemu-boot.sh`, `theme-studio/` |
| `docs/`, `web/` | the documents; the GitHub Pages site |

## Commands, and how long they take (4 cores)

| Command | Time | Notes |
|---|---|---|
| `make ui` | 3.5 min | one compiler command for all of banana-ui: any change in `ui/src` rebuilds all of it, `-j` doesn't help |
| `make test` | 8 min after the build | runs `ui/tests/run.sh`, then `bshare-test.sh`, `fetch-test.sh`, Activity's test |
| `make -C ui tests/<name>-test && ui/tests/<name>-test` | seconds | one unit test alone (see `run.sh` for each one's arguments) |
| `make -C system/bananad CFLAGS="-O2 -Werror"` | seconds | CI's build; warnings are errors there, and for `CC=aarch64-linux-gnu-gcc` too |
| `system/bananad/tests/sleep-test.sh` | 3 min | root (it mounts in a namespace of its own) |
| `sh system/tests/launchers-test.sh` | 2 s | |
| `python3 tools/companions/validate.py -q` | under a second | |
| `make rgds` / `make rgds-plus` | an hour or more | the image: downloads Buildroot and builds everything; not for a quick check |

`ui/tests/run.sh` has no way to run one section: to check one thing, run the unit test that covers it, or start
`ui/banana-ui --headless <out prefix> --size 640x480 ...` yourself as `run.sh` does (copy a call from it; the
fixture is `ui/tests/fixture.sh <dir>`).

After a change, run what CI runs for that part (`.github/workflows/ci.yml`): besides the above, `sh -n` and
`shellcheck -s dash -S warning` (with the exclusions listed there) over the scripts in `system/rootfs`,
`buildroot/board/rgds/*.sh` and `tools/*.sh`.

## Conventions

- Tabs in C and C++, four spaces in shell and Python, two in JS and JSON (`.editorconfig`). Comments and
  documents wrap at about 120 columns; code lines are as long as they need to be. Don't reformat what the change
  doesn't touch.
- Comments explain why a line is there and what went wrong without it, with the issue number (`#123`). The prose,
  in comments, documents and commit messages, is plain: "the card's files", "a game that ends", not "implements X".
- Commit subjects: the part, a colon, what is different now, the issue in brackets:
  `Settings: 3D resolution lists 1x, 2x, 3x, 4x in order (#250)`. No `fix:`/`feat:`. The body says what was wrong,
  what is done now and how it was checked. One topic per commit.
- A fix comes with a check in the suite for that part.
- Text shown on screen is plain English in the code; the translations (`ui/assets/lang/*.tsv`) find a line by its
  English text, so a reworded line shows in English in every language until the next translation round.
- Don't change `VERSION` or the README's release notes unless asked.
- Scripts in `system/rootfs` are for busybox ash: POSIX sh, `local` and `-nt` are fine, nothing from bash.

## Things that go wrong

- A bare `make` prints the help. `make rgds` and the like start an hour-long build and a Buildroot clone.
- The themes (`system/rootfs/usr/share/banana/themes/*.json`, `docs/themes/*.json`) hold comments: a strict JSON
  parser refuses them. `store/catalog.json`, `docs/theme.schema.json` and the companions are strict JSON.
- `ui/tests/run.sh` as root moves into a mount namespace with an empty `/run` and `/data`; as a user it writes
  nothing there. Its output and pictures are in `ui/tests/out` (ignored by git); `FAIL` lines are what to look for.
- `make ui` builds the helpers only when their libraries are there and says so when it skips one; `make test` then
  skips that helper's tests without failing. Read the output before reporting "all tests pass".
- `banana-fetch` is built without SMB when libsmb2 isn't installed (it isn't packaged for Ubuntu 24.04), and
  `fetch-test.sh` skips the 7z and RAR checks without `7zz`. CI has both.
- `make theme-studio` compares a prebuilt WebAssembly engine (`tools/theme-studio/wasm`) with banana-ui: after
  changes to the drawing code it fails until that engine is rebuilt with Emscripten (`wasm/build.sh`). It is not in
  CI.
- The Python tools that read files from elsewhere are meant to be run with `python3 -I`.
- Paths on the handheld: the data partition is `/data` (the `BANANA` drive on a computer), settings in
  `/data/banana`, logs in `/data/banana/logs`, state for the running session in `/run/banana`. The root is a
  read-only squashfs: nothing is ever written to it.
- Settings › System › Developer options (the flight recorder) only shows after A seven times on About.
