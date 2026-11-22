# Contributing to BananaOS

Reports, ideas, translations, themes and code are all welcome. This page says how each gets in, and how to build and
check a change on a PC: almost nothing here needs a handheld.

## Reports and ideas

Use the forms under [New issue](https://github.com/JorreFog/BananaLinux/issues/new/choose). A report that can be
acted on says which handheld (RG DS or RG DS Plus), which version (Settings › System › About, slot included), which
emulator for a game, how to make it happen, and brings the logs: they are in `banana/logs/` on the card's `BANANA`
drive, and [docs/FIRST-BOOT.md](docs/FIRST-BOOT.md#when-something-is-wrong) says what each file holds. One problem
or one idea per issue.

## Build and test on a PC

On Debian or Ubuntu, what CI installs (`.github/workflows/ci.yml`, Ubuntu 24.04):

```sh
sudo apt-get install -y libfreetype-dev libdrm-dev libasound2-dev zlib1g-dev imagemagick g++-aarch64-linux-gnu \
    libcurl4-openssl-dev libssh2-1-dev libssl-dev cmake shellcheck
```

Two more are optional, and CI builds both (the workflow has the exact steps and hashes): libsmb2 6.2, without which
`banana-fetch` is built with no SMB, and 7-Zip's `7zz`, without which the 7z and RAR checks are skipped.

```sh
make help          # every target and what it needs
make ui            # ui/banana-ui for this computer (headless, no ALSA) and its helpers
make test          # make ui, then every suite below marked "make test"
```

`banana-ui` is compiled by one command from all its sources, so any change to `ui/src` builds all of it again:
about three and a half minutes on four cores, and `-j` doesn't shorten it. `make test` takes about eight
minutes after that. While working on one thing, run the one suite that covers it:

| Command | What it checks | Needs |
|---|---|---|
| `ui/tests/run.sh` (make test) | the menu rendered headless at both panel sizes (640x480 and 1024x768) from a made-up card, Settings, the first-time setup, updates against a stand-in for GitHub, the home server's sync, the companions' readers; the pictures stay in `ui/tests/out` for a look | `make ui`, ImageMagick, python3, bash |
| `sh ui/tests/bshare-test.sh` (make test) | Bshare against a made-up LocalSend app | libcurl, OpenSSL |
| `sh ui/tests/fetch-test.sh` (make test) | Files' servers and archives, with folders standing in for a NAS | libcurl, libssh2, OpenSSL; `7zz` for 7z and RAR |
| `sh store/apps/activity/tests/sync-test.sh` (make test) | Activity's history on a made-up card | g++ |
| `python3 store/test-banana.py <checkout>` (make test, with `ROCKNIXDS_STORE=<checkout>`) | the Store's package manager | a rocknixds-store checkout with BananaOS's patches; skipped without |
| `make -C system/bananad CFLAGS="-O2 -Werror"` | bananad and banana-panel-tune build with no warning (CI also cross-builds them: `CC=aarch64-linux-gnu-gcc`) | gcc |
| `sudo system/bananad/tests/sleep-test.sh` | bananad's sleep, lid and power button against a made-up `/sys`, in namespaces of its own | root |
| `sh system/tests/launchers-test.sh` | the DS launchers' file work on a made-up card | sh |
| `python3 tools/companions/validate.py` | every game companion the image ships, against the schema in [docs/COMPANIONS.md](docs/COMPANIONS.md) | python3 |
| `make theme-studio` | the Banana Theme Studio: its expression port against `ui/src/expr.cpp`, the browser's engine against banana-ui, the preview server, the schema | node, g++, `make ui`, ImageMagick; Firefox and Python's `jsonschema` for the last parts (skipped without) |
| `make fuzz` | the readers of the card's files under libFuzzer, ASan and UBSan (`FUZZ_SECONDS` each, 60 by default) | clang |

CI runs the first four, the bananad build, the sleep and launcher tests, the validator, a parse and a ShellCheck
pass over the image's shell scripts, and a check that the Buildroot defconfigs keep every option they name.

Things that are easy to trip over:

- `ui/tests/run.sh` run as root (a container) gives itself an empty `/run` and `/data` in a mount namespace; as a
  user it leaves both alone. Its `systems.history` check needs the whole git history (`git fetch --unshallow`) and
  only notes it when the clone is shallow.
- The shell scripts under `system/rootfs` run in busybox ash on the handheld: POSIX sh with `local` and `-nt`, no
  bashisms. CI reads them with `sh -n` and ShellCheck.
- `ui/rnds` is ROCKNIXDS Pixel's engine as `tools/import-rnds.sh` imports it, and `ui/third_party` is upstream's code:
  changes to either belong upstream first.

## The image

```sh
make rgds          # output/rgds/images/banana-rgds.img.gz
make rgds-plus     # output/rgds-plus/images/banana-rgds-plus.img.gz
```

On Debian or Ubuntu: `build-essential bc bison flex libssl-dev libelf-dev unzip rsync cpio file wget python3-dev
swig libgnutls28-dev`. The first build clones Buildroot into `.buildroot/` and builds the toolchain, the kernel,
Mesa and the emulators into `output/`: an hour or more. `make menuconfig-rgds`, `make linux-menuconfig-rgds` and so
on pass any Buildroot target to a board's build. `buildroot/` is the external
tree (the defconfigs in `configs/`, Banana's packages, the board's kernel patches, device trees and image layout),
and `system/rootfs` is the overlay copied over the root as it is.

Next to the image the build leaves `banana-<board>-<version>.update.tar`: in `banana/updates/` on a card it is
installed from Settings › System › Updates into the other system slot, and Previous version there goes back, so a
build of your own is one restart away from the release it replaced. With no handheld, `tools/qemu-boot.sh <image>` boots
the whole image in QEMU (CI's `boot` job does the same; it and the image build only run when asked for, from the
Actions page).

[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) is the map of how it all fits; the README's
[Work on the menu](README.md#build) lists what each folder holds.

## Code

- Indentation is in `.editorconfig`: tabs in C and C++, four spaces in shell and Python, two in JavaScript and JSON.
  Please don't reformat lines a change doesn't touch.
- Comments say why, and what went wrong before; many name the report (`#123`) that led to the line.
- A fix comes with the check that would have caught it, in the suite that covers that part.
- Text on screen is written in English in the code, as it is shown; the translations look it up as it is (see
  below), so rewording a line makes it English again in every language until the next round.
- `VERSION` is set when a release is made: leave it as it is in a pull request.

## Commits and pull requests

One topic per commit and per pull request. The subject says what is different for the player or the contributor,
often starting with the part it is about, and ends with the report it answers:

```
Settings: 3D resolution lists 1x, 2x, 3x, 4x in order (#250)
bananad: one-channel recording takes the microphone's channel, not the two channels' average (#14)
.gitignore: banana-scrape, banana-sync and the lease shim, as the other programs make builds in ui/
```

No `fix:` or `feat:` prefixes, no capital needed, no full stop. The body, wrapped at about 120 columns, says what
was wrong and why, how it is now, and how it was checked (which test has the case, which handheld it ran on).
In the pull request, say the same, and which of the commands above you ran.

## Translations

The languages are `ui/assets/lang/<code>.tsv` (Spanish, Finnish, Italian, Russian and Simplified Chinese so far):
tab-separated lines of part, context, English, translation, with the language's name and the translator's at the
top. The menu, the settings, BananaDS and SuperDrastic all read the same file.

- **A new language:** open an issue to ask for the translation document, a Word file with every line on screen,
  where it is shown and a column to fill in. `tools/lang-from-docx.py <document> <code> ui/assets/lang/<code>.tsv`
  turns the filled-in document into the language file.
- **A correction** to a language that is there: edit its `.tsv` in a pull request. Keep the English column and the
  placeholders (`{name}`, `%d`, `%s`) exactly as they are: a line is found by its English text.
- **Checks:** `ui/tests/run.sh` runs `lang-test` on the language files (the button names stay, the rest is
  translated) and `lang-raw-test` on the placeholders, and renders the menu in a made-up language 1.6 times as long
  (`ui/tests/pseudo-lang.py`) to find texts that don't fit.
- **For the next document** (the maintainer's side): `python3 -I tools/lang-extract.py --out strings.tsv` lists
  every English line the sources show (give it the emulators' checkouts with `--sd`, `--bd`, `--bf` for theirs),
  and `tools/lang-to-docx.py` writes the next document from the last one, each line marked kept, changed, unused
  or new. `tools/tests/lang-roundtrip.py` checks that a document and a language file survive the round trip.

## Themes, companions and apps

- A theme: [docs/MAKE-A-THEME.md](docs/MAKE-A-THEME.md) and [docs/THEMES.md](docs/THEMES.md); the
  [Banana Theme Studio](https://jorrefog.github.io/BananaLinux/theme-studio/) edits one in the browser.
- A game companion (a second-screen page for a game): [docs/COMPANIONS.md](docs/COMPANIONS.md), checked with
  `python3 tools/companions/validate.py <file>`.
- An emulator or a port in the image: [docs/EMULATORS.md](docs/EMULATORS.md) has the contract a launcher keeps to.
- An app for the Store: `store/catalog.json` is the catalogue the handhelds read.

## Licence

BananaOS is under the MIT licence ([LICENSE](LICENSE)); what it builds on is listed in
[THIRD_PARTY.md](THIRD_PARTY.md). A contribution is made under the same licence.
