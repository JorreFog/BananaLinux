# BananaDS speed session 2 (2026-10-18, 19:00-20:49 UTC): five branches for the device

This session followed [../GUIDE-FOR-CLOUD-AGENTS.md](../GUIDE-FOR-CLOUD-AGENTS.md):

- **Emulator thread only.** HeartGold's limit on the RG DS Plus, at 99% busy.
- **One toggle per change.** Its "off" is the old behaviour.
- **Exactness checked off the device.** Pictures, main RAM and audio, with a sabotaged build caught where noted.
- **No simulator.** qemu instruction counts were used only to confirm a path got cheaper.

**Nothing here is measured on a device.** Every gain below is an estimate with its reasoning.

Each branch is pushed to JorreFog/SuperDrastic, based on `00c34c78e8de` (the commit `bananads.mk` pins), with one
commit per change. Each agent's hand-over (`<agent>.md` here) has the exact device commands and the checks it ran.

| Branch | Head | Agent's hand-over |
|---|---|---|
| `claude/bananads-perf-tools` | 40c432e | [tools.md](tools.md) |
| `claude/bananads-perf-memio` | 2bfb6bf | [memio.md](memio.md) |
| `claude/bananads-perf-loop` | 275e1a0 | [loop.md](loop.md) |
| `claude/bananads-perf-jitcode` | 96b339f | [jitcode.md](jitcode.md) |
| `claude/bananads-perf-geom` | 74378c2 | [geom.md](geom.md) |

## Do these first: measurement, then the one known win

1. **Set up the kit.** Follow tools.md's numbered steps: the CI artifact from the branch, `bananads.debug` next to the
   binary, `HG_HASH_1X`/`HG_HASH_2X` filled in once, and a new baseline. The fixed scripts walk left now and check the
   scene, so their fps isn't comparable with older numbers.
2. **Profile the emulator thread in HeartGold** at 1x and 2x with `hgprofile.sh` (the tools branch's `prof.hz`
   profiler; BananaOS has no `perf`).
   - Its `# waiting:` line tells how much of the "99% busy" is the emulator thread spinning while it waits for the
     2D thread.
   - Its areas give today's shares for JIT code, the slow memory and IO paths, geometry, the scheduler and the SPU,
     which the next session ranks its work by.
   - Also run the counters the agents asked for: `MELONDS_IOSTATS=1500` (memio.md), `MELONDS_HALTSKIP_STATS=1`
     (loop.md) and, from a `-DBANANADS_GEOMSTATS=ON` build, `MELONDS_GEOMSTATS` (geom.md).
3. **`render.2d_thread` on at 1x** (tools, already measured: +21% base). A/B it against
   `--set render.2d_thread=off --set render.3d_share=off` (the old 1x) to confirm on the new binary, and run
   `exact.sh`.

## Then the A/Bs, ranked by expected HeartGold gain (each against a plain run of the same branch's binary)

| # | Branch | Toggle (default) | What | Expected | Confidence |
|---|---|---|---|---|---|
| 1 | tools | `render.3d_share` at 1x (on) | the 3D line shared with the 2D thread at 1x | +1..4% at 1x | medium: part of the device's 21% -> 26% gap |
| 2 | memio | `render.gx_batch` (on) | STMs into the GX FIFO in one call | +1..2.5% | depends on whether the old profile's 5.7% for these STMs still holds |
| 3 | loop | `core.halt_skip` (on) | both CPUs halted: skip to the next event | +1..3% | depends on how long HeartGold halts both CPUs (`MELONDS_HALTSKIP_STATS=1`) |
| 4 | tools | `render.2d_fence_wait=block` (default spin) | wait for the 2D thread by blocking, not spinning | 0..+3%, a truthful busy figure, maybe a lower settled clock | the profile's `# waiting:` line decides |
| 5 | jitcode | `jit.entry_x26`, `jit.dyncold`, `jit.flagstub` (on) | JIT blocks 9.4% smaller (bigcode) | +0.5..2% together if the JIT's code is fetch-bound, ~0 if not | `jit.flagstub`'s sabotage wasn't caught by the test ROMs: run the device `exact.sh` with it, or leave it off |
| 6 | memio | `render.gx_str` (on) | single STRs to the GX ports with fewer layers | 0..+1% | depends on how many such stores HeartGold makes |
| 7 | loop | `jit.slice_chain` (on) | a slice ending inside a block chain goes on without the loop | -1..+3% | sign uncertain: a cost with a busy ARM7 |
| 8 | geom | `render.geom_batch` (on) | the GX FIFO's DMA/IRQ checks once per `Run` | +0.2..0.5% | likely within noise |

What each agent found matters as much as its patches:

- **geom:** the geometry engine after patch 0011 has no hot spot left. The CPU paces the command stream (2.7-5.3
  FIFO entries per `Run` call), so the next lever there is running the geometry engine lazily, which is a day's work.
- **jitcode:** the ARM9's RegionCodeCycles store (2.9% of block bytes) and the R15 store before a block's last
  instruction (3.2%) are the next code-size targets.

## Combining the branches

- **Two branches don't apply together yet.** `jitcode`'s patches 0040-0042 conflict with `loop`'s 0030-0031 in
  surrounding code (the compiler's option lines, `Gen_ExitStubs`). They need a rebase and the exact suite again; no
  one did that this session.
- **One pair is checked.** `geom`'s 0050-0051 apply on top of `memio`'s 0021-0022.
- **`tools` stands alone.** It touches the frontend, the 2D thread's defaults, the scripts and CI, not melonDS's
  emulation.
- **Not on a branch.** Unfinished work was left out: `loop`'s port of the first session's `jit.irqchain` and a cheaper
  `slice_chain` bail (patches in loop-evidence/), and none of the first session's other 2D work.

## The rules this session kept, and the one it couldn't

- **Kept.** Every commit is exact by its agent's checks (exact-suite FAILS=0; difftest; sabotage builds caught except
  `jit.flagstub`), has a toggle, and says "untested on device".
- **Could not keep: starting from a profile.** The guide says to start from a device profile of the game. None
  newer than 2026-10-12 existed, so the agents ranked by that profile and by reading the code. The profiler on the
  tools branch is there so the next batch can start from a real one.
