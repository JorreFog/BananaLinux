# Banana 3DS Bench

A deterministic 3DS homebrew stress test, for benchmarking 3DS emulators (Panda3DS on the RG DS). It needs no input and
runs forever, cycling a fixed schedule of scenes. The scene depends only on the number of rendered frames, so frame N
always shows the same content. It draws both screens every frame (top 400x240, bottom 320x240) and syncs to VBlank with
`C3D_FrameBegin(C3D_FRAME_SYNCDRAW)`. Press START to exit.

Outputs: `3ds-bench.3dsx` (with an SMDH titled "Banana 3DS Bench") and `3ds-bench.elf`. Panda3DS loads either one.

## Build

```sh
./get-devkitarm.sh /home/user/devkitpro   # once: pulls the devkitpro/devkitarm Docker image over the registry API
source /home/user/devkitpro/env.sh        # DEVKITPRO, DEVKITARM, PATH
make                                      # make clean to start over
```

`get-devkitarm.sh` does not need a Docker daemon. It uses curl to get an anonymous token from auth.docker.io, fetches the
linux/amd64 manifest and layers of `devkitpro/devkitarm:latest`, checks each layer's sha256, and unpacks the layers in
order into `$DEST/rootfs`. It applies whiteouts and skips device nodes. Keep the toolchain outside this repository.

The textures are made in code: `gen-assets.py` (python3, standard library only) writes `gfx/*.png`, `gfx/sprites/*.png`
and `icon.png`, and `make` runs it. tex3ds then converts the PNGs in the formats that `gfx/*.t3s` gives. The three
vertex shaders in `source/*.v.pica` are assembled with picasso. The build is the standard devkitPro 3dsx Makefile
template, with `-O2`.

`make NO_FAKE_DSP=1` turns off the fallback DSP component (see Audio). `make FPS_OVERLAY=1` adds an FPS / frame-time
readout to the bottom-screen overlay. It is off by default because it depends on timing, and frame dumps
(`panda3ds-bench --dump`) must depend only on the frame number. Run `make clean` before switching either option.

## Schedule

Each scene lasts 300 frames. After the last scene the schedule wraps: one loop is 1500 frames, 25 s at 60 fps.

| scene | name    | frames (loop-relative) |
|-------|---------|------------------------|
| 0     | `cpu`   | 0 - 299                |
| 1     | `3d`    | 300 - 599              |
| 2     | `2d`    | 600 - 899              |
| 3     | `mixed` | 900 - 1199             |
| 4     | `fill`  | 1200 - 1499            |

### Debug-string markers

The markers go out through `svcOutputDebugString`, and Panda3DS prints them as `[OutputDebugString] ...`:

```
BENCH audio init rc=00000000
BENCH start version=1 scenes=5 frames_per_scene=300 text=1 audio=1
BENCH scene=<n> name=<name> frame=<global frame>      # first frame of a scene (logged before it is drawn)
BENCH stats scene=<n> name=<name> tris=.. draws=.. sprites=.. xfer_bytes=.. cmdbuf=..   # counts for that first frame
BENCH scene_end scene=<n> name=<name> frame=<f> hash=<8 hex>   # checksum of the scene's CPU results
BENCH loop n=<loops> frame=<f>                         # the schedule wrapped (comes before the scene=0 line)
BENCH exit frame=<f>
```

The `hash` values are deterministic. If they differ between emulator builds, the CPU emulation is wrong.

## Scenes

Workload numbers below are the `BENCH stats` counts for one frame. The top screen is drawn with 3D, then citro2d draws the
2D on top. In every scene, the bottom screen has an overlay drawn with citro2d that shows the scene and frame number.

1. **`cpu`**: game logic on the ARM11, with a simple 3D scene.
   - 2000 entities run an AI state machine (idle, seek/attack, flee, wander) with VFP vector math and `sqrtf`. Each one
     has a uniform spatial grid lookup, separation against up to 8 neighbours, physics, and bounce.
   - 4 skeletons of 64 bones each: a 4x4 matrix hierarchy with `sinf`/`cosf`.
   - About 384 KB of `memset`/`memcpy` per frame.
   - Thumb-1 code (`cpu_thumb.c`): a merge sort of 4096 (key, value) pairs, 4096 inserts and lookups in an 8192-slot
     open-addressing hash table, and a CRC32 over 16 KB.
   - Top screen: 2.7k tris in 26 draws. Bottom screen: HUD with about 66 sprites and rectangles, plus text.
2. **`3d`**: heavy 3D. **About 23.1k tris in 248 draw calls**, plus one render-to-texture pass.
   - 16 terrain chunks (ETC1 with mipmaps, fog). 180 props, alternating RGB565 and LA8, with a per-object tint from a
     second combiner stage; every 10th prop is alpha-blended. 60 cross-quad trees (ETC1A4, alpha test).
   - 8 tori (RGBA8 with mipmaps) and 2 spheres of 768 tris lit by **PICA fragment lighting**: a `C3D_LightEnv` with
     D0/D1 Phong LUTs and a `MULTIPLY_ADD` combiner.
   - Per-vertex-lit vertex shader for the rest. Geometry is in one static VBO + u16 IBO (`C3D_DrawElements`).
   - 600 additive particles in a vertex buffer that the CPU rewrites every frame (A8 texture). 4 immediate-mode
     triangle strips (`C3D_ImmDrawBegin`).
   - An alpha-blended water plane with 2 textures (RGBA5551 and RGBA4), a 3-stage combiner and fog.
   - Render-to-texture: a 256x256 RGBA8 target gets an overhead view (1 torus and 24 props). `GX_TextureCopy` keeps the
     previous frame's image as a history texture. Two `GX_DisplayTransfer`s downscale it 2x and tile it again (a bloom
     chain). Two "monitor" quads sample all 3 textures with a 3-stage combiner. The bottom screen shows the RTT texture
     as a minimap, with a HUD of about 120 sprites.
3. **`2d`**: sprite-heavy, drawn with citro2d.
   - **2000 sprites per frame** (1200 top + 800 bottom) from a 16-image RGBA8 atlas. They are rotated, scaled and
     tinted, some are alpha-blended, and the CPU updates their positions.
   - System-font text (`C2D_Text`), parsed again every frame.
   - Two textures are updated every frame. A 128x128 RGBA8 plasma made on the CPU goes from linear to tiled with
     `GX_DisplayTransfer`. A 128x128 RGB565 image is swizzled in Thumb code and uploaded with `C3D_TexUpload`.
   - Both screens get full-screen clears (`GX_MemoryFill` through `C3D_RenderTargetClear`).
4. **`mixed`**: a typical game frame.
   - CPU work at about 1/3 of scene 1: 700 entities, 1366-element sort and hash, 128 KB of memory churn.
   - Top screen: a 3D world of **about 9.5k tris in 93 draws** (16 terrain chunks, 2 fragment-lit tori, 60 props,
     trees, water, 200 particles, 2 ribbons).
   - Bottom screen: a HUD with **about 300 sprites** and text.
   - Audio: ndsp plays a looping 1 s stereo PCM16 chord on channel 0, with polyphase interpolation. Channel 1 plays a
     looping mono DSP-ADPCM buffer. Both are synthesised at startup, and the ADPCM is encoded at startup by a small
     encoder in `audio.c`. The channels play only during this scene, but ndsp runs the whole time, as it does in games.
5. **`fill`**: fill rate and overdraw. Each screen gets 6 full-screen quads that are alpha- or additive-blended. Each
   quad samples 3 textures (RGBA4, RGBA8, RGBA5551, LA8, ETC1, RGB565) through a 4-stage combiner. Only 24 tris, but
   each screen is overdrawn about 6x on top of the clear.

## Notes

- **Thumb code.** `cpu_thumb.c` is built with `-mthumb -mfloat-abi=softfp -fno-tree-loop-distribute-patterns`. Thumb-1
  cannot use the hard-float ABI, so the file is integer-only and marks itself as VFP-ABI-compatible. It must not call
  into libc: 3dsxtool rejects the halfword-aligned `R_ARM_THM_CALL` relocations that such calls produce ("Unaligned
  relocation").
- **Audio.** If `ndspInit` fails because there is no DSP firmware (no `sdmc:/3ds/dspfirm.cdc` and no `hb:ndsp`), the
  ROM gives ndsp a minimal `DSP1` header and tries again. Emulators with HLE audio, like Panda3DS, accept it. On real
  hardware, use a real firmware dump or build with `NO_FAKE_DSP=1`.
- **citro3d.** Do not call `C3D_TexBind(unit, NULL)` with this citro3d: it dereferences the texture. In Panda3DS that
  shows up as a fatal read at address 0xC.
- **Frame budget.** Every scene fits a 60 fps frame in Panda3DS's emulated time: each scene takes exactly 300 emulator
  frames in `panda3ds-bench`.
