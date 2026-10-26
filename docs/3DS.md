# Nintendo 3DS games

An emulator translates a game's code while you play, which a handheld short of
power cannot afford on top of a 3DS game. Banana does what it does for the DS
and for ports' recompiled code: the code is translated **once, on a PC**, into a
native program for the handheld, and the handheld runs that plus the parts of an
emulator the game still needs.

The runtime is [pocket-native](https://github.com/whovian64/pocket-native), a
fork of [Zakuro](https://github.com/fearkov/zakuro) and
[3dsrecomp](https://github.com/fearkov/3dsrecomp) that adds an **OpenGL ES 3.1
renderer** for Mali GPUs with no Vulkan driver, and a **frontend for a handheld
with no desktop** — KMS for the screens, evdev for the buttons and the touch
panel, ALSA for the sound. The image carries that runtime
(`buildroot/package/pocket-native`); **the games are yours and are in no
image**, built on your PC and copied to the card.

This is a testing feature: it runs, it plays, and some things are still wrong —
see [What is still rough](#what-is-still-rough).

## Building a game (on a PC)

Follow pocket-native's own guide ("Start here" in its repository): download its
kit, drop your own **decrypted `.3ds`/`.cci`** on `Build a game`, and it builds
the game in a container (Windows or Linux with Docker or Podman). A `.cia` needs
your console's `aes_keys.txt`. It writes a `Pocket Native` folder holding, for
that game, `title.so` (the recompiled code) and `game.3ds`.

Banana does not use that folder's launchers. Copy the two things a game needs to
the card:

```
card/roms/3ds/<the game>.cci or .3ds             the game itself
card/pocket-native/games/<title id>/title.so     the code a PC recompiled
card/pocket-native/games/<title id>/sysdata/     shared_font.bin, if the kit made one
```

`<title id>` is the 16-hex-digit id the kit names the folder by (the menu shows
it too). The ROM can be the same file the PC built from. Nothing else is
needed: the profiles that tune a game live in the image, and a game nobody has
profiled gets the defaults.

## Playing

The game appears on the shelf as **Nintendo 3DS** (its `.cci`/`.3ds` under
`roms/3ds`). Starting it, banana-ui closes its display as it does for every
emulator, the game takes the two panels, and the menu comes back on exit.

| Handheld | 3DS |
|---|---|
| A / B / X / Y | A / B / X / Y |
| L1 / R1 | L / R |
| D-pad | D-pad |
| Left stick | Circle pad |
| Right stick | Moves the touch cursor |
| R2 or R3 | Touches the bottom screen under the cursor |
| Start / Select | Start / Select |

The **bottom panel is the 3DS's touch screen**: a finger on it touches there,
where the finger is. The top screen is on the top panel, the bottom screen on
the bottom one. The in-game menu is **F** (tap it; **L3 + R2** does the same),
**F + Start** quits, and **L2** or **F + L1** swaps the two screens if they are
the wrong way round.

Everything else the menu offers — layout, resolution, frame cap, power, and the
**Advanced settings** below — is pocket-native's own.

## Settings, and where they live

Each game's settings are its own file, `card/pocket-native/games/<id>/data/settings.toml`,
which the in-game menu writes. The first run writes the settings that work on
this handheld:

```toml
[advanced]
gpu_clears = false      # whole-surface clears on the GPU: made textures flicker
gpu_arrays = false      # vertex arrays kept on the GPU
trust_meshes = false    # index and vertex buffers trusted between draws
```

The three are the renderer's riskier speed-ups: on the RG DS Plus the driver
(Panfrost) faults on them — a lost draw, so a texture that flickers or vanishes
— and their being off costs speed rather than correctness. **Advanced settings**
in the menu has them, and every other switch, for trying a game that looks
wrong; **Most compatible** turns the lot off.

The **GPU program cache** is kept from growing without bound — a program costs
the driver on the order of a megabyte, and a 1 GB handheld cannot afford a few
hundred of them — and decoded textures are dropped after a while, so a long
session does not fill the card's RAM with the CPU's own copies of them.

## When a game misbehaves

The launcher keeps nothing by default. To have it keep the kernel log (Panfrost
faults, out-of-memory, thermal, all of which Banana's own `dmesg` misses,
because it is taken at boot) and a trace of memory while the game runs, touch
`/data/pocket-native/debug` (or set `BANANA_3DS_DEBUG=1`):

```
/data/banana/logs/pocket-native.kmsg     the kernel log while it ran
/data/banana/logs/pocket-native.mem      memory free, and the game's RSS, every few seconds
/data/banana/logs/pocket-native.smaps    the game's biggest mappings by RSS
/data/pocket-native/games/<id>/data/log.txt   the runtime's own log
```

## What is still rough

- **Memory.** On 1 GB, a big level sits near the limit: the emulator's own RSS
  reaches 700–800 MB, almost all of it the compiled GPU programs. When it runs
  out, textures and models vanish and the driver faults. Capping far below what
  a level needs makes it worse, not better (it compiles programs again); the
  cache drops them by staleness instead. Expect some levels to be slower than
  others and the very first visit to a level to stutter while its shaders are
  built (they are cached afterwards, in the game's `data/`).
- **The speed-ups above are off for a reason.** `gpu_clears` in particular is a
  bug in that path, not a driver quirk: with it on, textures flicker with no
  GPU faults at all.
- **Frame rate.** The renderer is GPU-bound (Mesa's Panfrost on the Mali-G52),
  not CPU-bound: the code runs recompiled with almost nothing interpreted.

Fixes for the two rendering bugs and the changes that made this work here (both
panels, this pad's buttons, the touch screen, the caches) are patches beside the
package, sent upstream.

## How it is built

`buildroot/package/pocket-native` builds the fork at a pinned commit and applies
the patches next to it. It needs a Rust toolchain in the image: the package
selects `BR2_PACKAGE_HOST_RUSTC`, and the defconfigs enable it (and choose the
prebuilt toolchain, `BR2_PACKAGE_RUST_BIN`, over building one — far quicker).
The runtime is MIT, as are the two projects it forks.
