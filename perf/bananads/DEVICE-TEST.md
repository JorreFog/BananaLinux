# Testing the session's BananaDS changes on an RG DS / RG DS Plus

For the local agent with the handheld. Everything here was measured off the device only (rgds-sim, an A55 model under
qemu, and exact instruction counts), so the device decides every default. Each change is behind a toggle whose
"off" is the old behaviour, so one binary can A/B most of them.

## 1. Get the code onto the device

The changes are a `git format-patch` series against SuperDrastic `00c34c78e8de` (the BananaDS commit BananaLinux's
`buildroot/package/bananads/bananads.mk` pins):

```sh
git clone https://github.com/JorreFog/SuperDrastic && cd SuperDrastic
git checkout -b bananads-perf-1018 00c34c78e8deca40d19565bbd6f755385e17ae1c
git am /path/to/BananaLinux/perf/bananads/patches/integrated/*.patch   # everything together (see SUMMARY.md)
```

`patches/<branch>/` hold the same work per agent (render, jit, jit-2d, core) if one part has to be tested alone.
To build the image with it: push the branch to SuperDrastic, set `BANANADS_VERSION` in `bananads.mk` to its head
(nothing else in the package changes; melonDS stays at the same commit, the new melonDS patches are files under
`bananads/patches/melonds/` that Buildroot's patch step applies), `make rgds-plus` / `make rgds`. Do not release it:
it is for the A/B runs below.

The build options (PGO, alignment) need a second build each: add them to `BANANADS_CONF_OPTS` in `bananads.mk`
for that build only (see 5).

## 2. Correctness first (every build)

- `tools/device/quick.sh <bananads> <difftest>` (BananaDS repo; the kit in `/storage/bananads-device`).
- The 42-frame HeartGold compare with `--set rtc.fixed=2026-10-11_12:00:00` and `headless.dump_dir` /
  `headless.dump_every=50` over 2100 frames (docs/PERF.md "Exactness"): pictures identical between the base binary
  and the new one at its defaults, then with everything off. Any difference: bisect by toggle (the lists below).
- Play: a HeartGold walk, Mario Kart DS (a race), a game with a HUD window over 3D, Golden Sun: Dark Dawn's 3D world,
  a game that uses the microphone; listen for crackles, look for black or torn frames.

## 3. Speed: what to measure

- `tools/device/hgquick.sh <bin>...` (HeartGold overworld, 600 measured frames at 1992 MHz): fps. `EXTRA="--set ..."`
  passes settings. Run each A/B line against a plain run of the same binary, alternating, at least twice.
- Per-thread CPU time at 2x: `top -H -p $(pidof bananads)` (or `/proc/<pid>/task/*/stat`): the emulator's thread,
  `bananads-2d`, the Gengis workers. At 2x the 2D thread is the frame's slowest thread in the model once the 3D
  pipeline is on; the device should confirm or refute that.
- The clock cpugov settles at for a game (`tools/device/hgprofile.sh`): lower = cooler, longer battery.
- `perf stat -e L1-icache-load-misses,iTLB-load-misses,L1-dcache-load-misses` where perf exists (ROCKNIX has it).
- Both handhelds, 1x and 2x (`render.scale=1|2`); the RG DS Plus's 1024x768 panels too.

## 4. The one-binary A/Bs, most important first

```sh
# the 3D pipeline: the emulator's thread stops drawing Gengis bins at VBlank (model: -27% of that thread at 2x)
EXTRA="--set render.3d_pipeline=off" tools/device/hgquick.sh <bin>           # at 2x, with gengis_threads 2 and 3
#   and read the "[gengis] the emulator's thread drew N of M bins" log line in both runs (on: N ~ 0)
# everything of the 2D phase off at once (the 2D thread at 2x, the emulator's thread at 1x)
EXTRA="--set render.2d_comp_unroll=off --set render.2d_quarters=off --set render.2d_emit_zip=off --set render.2d_job_stride=inline --set render.2d_win_neon=off --set render.2d_steps_cache=off --set render.2d_conv16=off --set render.2d_prefetch16=off" tools/device/hgquick.sh <bin>
# windows over 3D (HUDs): the model's biggest 2D win on a windowed scene; needs a game with a HUD window
EXTRA="--set render.2d_win_neon=off" ...
# prefetches: the model can't judge their timing; device-only verdicts
EXTRA="--set render.2d_prefetch=on" ...        # off by default
EXTRA="--set render.2d_prefetch16=off" ...     # on by default
# the waits for the 2D thread: block (default) vs spin (old); read kms.stats' repeats and drops (BANANADS_KMS_STATS=600)
EXTRA="--set render.2d_fence_wait=spin" ...
# the 2D thread's batch (wakeups per frame)
EXTRA="--set render.2d_batch=48" ...
# the 2D thread at 1x (off by default at 1x; model: the emulator's thread -36% on dsstress L5): a default change if it wins
EXTRA="--set render.scale=1 --set render.2d_thread=on" ...
# the JIT (melonDS patches 0021-0024; all on by default)
EXTRA="--set jit.hugepages=off" ...            # check the log: "[jit] code memory at ..., N kB on huge pages" (N = 32768)
EXTRA="--set jit.irqchain=off" ...             # first count it: MELONDS_JIT_EXITSTATS=1500 with jit.irqchain=off
EXTRA="--set jit.entry_x26=off" ...
```

Full lists with the reasoning and the expected sizes: `findings/render.md`, `findings/jit.md`, `findings/core.md`.

## 5. The build A/Bs (two binaries each)

```sh
# PGO: the test-ROM profile (pgo/ in this folder, see SUMMARY.md for which tree it matches)
tools/device/hgquick.sh bananads-plain bananads-pgo          # -DBANANADS_PGO=<profile dir>
# code alignment on top of PGO (model: -0.1..-3.9%, L1I misses -72% on dsstress L5)
tools/device/hgquick.sh bananads-pgo bananads-pgo-align      # + -DBANANADS_ALIGN=32:16:16
```
If PGO wins, collect a profile from real games on the handheld (`findings/core.md`, "How PGO would ship") and A/B
that against the test-ROM profile; commit the winner with the source it was made from.

## 6. Feed the numbers back

`rgds-sim/calib/device-calib.sh` turns device fps/perf numbers into a refit of the simulator's cost parameters
(`rgds-sim/README.md`), so the next cloud session's model is calibrated on this hardware. Post the device numbers
next to the model's in `LEDGER.md` (a "device" column), and move any toggle whose device A/B loses to off.
