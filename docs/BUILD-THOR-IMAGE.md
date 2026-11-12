# Building the AYN Thor's SD card image

This is the hand-off for whoever builds the Thor's image, a person or an AI on their own machine. The image is
ROCKNIX's SM8550 build with BananaOS put into it (`docs/THOR-ROCKNIX.md` says what that means and where every part
stands). Nothing below has been run yet. The first build will likely need fixes in this repository's ROCKNIX packages,
and those fixes belong here, under `rocknix/`, not in the ROCKNIX checkout.

## What you need

- Linux x86_64 with Docker (ROCKNIX builds in its own build container), about 150 GB free disk and plenty of RAM
  (16 GB or more; some packages, MAME's among them, take 1-2 GB per compiler).
- Several hours for the first build. Later builds reuse ROCKNIX's `sources/` downloads and its build folders.
- git and python3 on the host.

## Steps

1. This repository at the Thor branch (or `main` once it's merged):

   ```sh
   git clone https://github.com/JorreFog/BananaLinux
   cd BananaLinux
   git checkout claude/pensive-rubin-cy7ro4
   ```

2. ROCKNIX at the commit this work was made against (`buildroot/board/thor/ROCKNIX`'s `commit=`):

   ```sh
   git clone https://github.com/ROCKNIX/distribution ../rocknix
   git -C ../rocknix checkout "$(sed -n 's/^commit=//p' buildroot/board/thor/ROCKNIX)"
   ```

3. BananaOS into it. This copies the frontend package and Banana's emulator and app packages into the checkout,
   adds Banana's RetroArch patches, and lists everything where ROCKNIX's build looks (`ADDITIONAL_PACKAGES`, the
   emulator list, `es_features.cfg`). Running it again is safe.

   ```sh
   sh tools/rocknix-banana.sh ../rocknix SM8550
   ```

4. ROCKNIX's own build:

   ```sh
   cd ../rocknix
   make docker-SM8550
   ```

   The image lands in `release/` (`*.img.gz`), with an update tar beside it.

CI does steps 2-4 too: `.github/workflows/ci.yml`'s `rocknix-thor` job, started by hand (Actions > CI > Run workflow,
with "thor_image" ticked). It puts the image up as an artifact.

## When a package fails to build

The packages most likely to need work on a first build, and where they live here:

| Package | Here | Likely trouble |
|---|---|---|
| `banana-frontend` | `rocknix/packages/ui/banana-frontend` | dependency names (`SDL2`, `curl`, `openssl`, `libssh2`), the cross `pkg-config`, banana-fetch's libraries |
| `bananads-sa` | `rocknix/packages/emulators/standalone/bananads-sa` | the melonDS source path from `melonds-sa` (`get_pkg_version`), `PKG_CMAKE_SCRIPT`, the binary's path in the build folder |
| `bananafront-sa` | `.../standalone/bananafront-sa` | `make sdl`'s flags (patch 0001) |
| `mame-gnw` | `.../standalone/mame-gnw` | MAME's cross build (`OVERRIDE_CC`, `SDL_INSTALL_ROOT`, the host python), BananaOS's patches applying to it |
| `banana-apps` | `rocknix/packages/apps/banana-apps` | the Store's patches (0001-0003) and the two-window one (0010) applying in order |
| `bananagba-lr`, `banananes-lr`, `bananasnes-lr` | `rocknix/packages/emulators/libretro` | their own makefiles' flags |

Fix the package here, run `tools/rocknix-banana.sh` again, and rebuild just that package in ROCKNIX's way (`make
docker-SM8550` again, or ROCKNIX's `scripts/build <package>` inside its container). A package that can't be made to
work on the first try can be left out by taking it off the list in `tools/rocknix-register.py` (`EMULATORS`,
`PACKAGES`): the menu and ROCKNIX's own emulators don't need Banana's.

Before pushing a fix, run this repository's checks: `make test` (or at least `sh rocknix/tests/run.sh`).

## Putting it on the Thor

1. Write the image to a microSD card (Raspberry Pi Imager, balenaEtcher or `dd`).
2. Once per handheld, the Thor needs OpenABL in its `abl_a`/`abl_b` partitions so it can boot from the card. Follow
   ROCKNIX's Thor page (https://rocknix.org/devices/ayn/thor-sm8550). **Back up the stock ABL first.** Nothing else on
   the Thor's internal storage is touched; Android stays, and without the card the Thor boots it.
3. Card in, power on. The first boot is ROCKNIX's (it makes its storage partition), then Banana's menu comes up on
   both screens. `banana.frontend=0` in ROCKNIX's settings goes back to EmulationStation.

## What to look at on the first boot

`docs/THOR-ROCKNIX.md`'s "What only the Thor can tell", and the `thor-bringup` skill (`.claude/skills/thor-bringup`),
which turns what the handheld reports into fixes. The menu's log is
`/storage/.config/banana-data/banana/logs/banana-ui.log`; its service is `bananasway` (`journalctl -u bananasway`).
Known gap: the boot animation has no artwork for the Thor's screen shapes yet, so the menu starts without it.
