# Dead ends: read before starting an idea (append only)

## Already ruled out before this session (from BananaDS docs/PERF.md and docs/BANANALINUX.md at 00c34c78e8de)

- Inlining the JIT's exit checks into each block: 1% fewer instructions, but L1I refills +22% and iTLB walks +43%:
  slower on the A55 (PERF.md, patch 0001).
- `msr nzcv` + `b.cond` for a condition check: no better than melonDS's table (4.2 vs 3.2 cycles, A55).
- Continuing from `ARM_Ret` without returning to the loop (busy ARM7): exact but +144k instructions/frame: dropped.
- One ring buffer for the GX FIFO and pipe: exact with savestate conversion, but more instructions: dropped.
- Clearing GXSTAT's busy bits only after a command: not exact (a mid-run savestate differed): dropped.
- Deferring or batching sound mixing: not exact (channels read RAM at mix time while games refill played halves).
- Longer idle slices (`core.slice_idle` 1024/2048): no change in frame times.
- "The JIT cache resets during gameplay": false; profiles that showed `ResetBlockCache`/`munmap` sampled the exit.
- SuperDrastic measured on this hardware and removed: CPU pinning, `SCHED_FIFO` for the emulator's own threads,
  clock lock + PLL pacing, a fixed latch.
- `render.threaded=on` (melonDS's soft 3D thread) was slower on the device than off (48.6 vs 52.2 fps, L10).
- `MELONDS_JIT_LOOPREGS` (patch 0007): black screen in HeartGold on the device, cause unknown, off by default; the
  test ROMs don't reproduce it. Don't build on it without a repro.
- core.slice > 64 (`auto`, 256, 512) changes timing: Golden Sun: Dark Dawn drew no 3D world with `auto`; the default
  is back to 64 (BananaLinux run-bananads). Not a free lever.
- qemu instruction counts in JIT code did not carry over to HeartGold on the device (0007: -40% JIT-code
  instructions on cpubench, nothing measurable in HeartGold). Use the A55 model, and treat JIT-code-only wins with
  suspicion.

## This session
