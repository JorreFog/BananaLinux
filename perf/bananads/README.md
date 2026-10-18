# BananaDS speed work, 2026-10-18 (four cloud agents)

**Newest:** [session2/README.md](session2/README.md): the second session's five SuperDrastic branches (emulator thread only), ranked device A/Bs.

**Start here:** [GUIDE-FOR-CLOUD-AGENTS.md](GUIDE-FOR-CLOUD-AGENTS.md) is the local agent's device verdict on this
session and how the next one should work (start from a device profile of a real game). [SUMMARY.md](SUMMARY.md) has the simulated results against the start of the session and what is in the
patches. [DEVICE-TEST.md](DEVICE-TEST.md) has how to apply them and A/B them on an RG DS / RG DS Plus.
`patches/integrated/` is the series to apply. The rest of this page is the protocol the agents worked by.

The shared place of a team of four agents working on BananaDS's speed on the RG DS and RG DS Plus, without the
handheld. Everything they find, measure, try and drop is written here, so that the local agent with a handheld can
apply the changes and measure them on the device. BananaDS lives in JorreFog/SuperDrastic under `bananads/`; the
work starts from `00c34c78e8de` (`bananads-perf/dma`, the commit BananaLinux's `bananads.mk` pins).

| Agent | Name | Model | What |
|---|---|---|---|
| 1 | `sim` | Opus 5.5 | **rgds-sim**, the RG DS simulator (an A55 timing model under qemu), then the **verifier**: re-measures every claim |
| 2 | `jit` | Opus 5.5 | melonDS's AArch64 JIT and its runtime (patches 0001-0020's area) |
| 3 | `core` | Fable 5.1 | the build (profile-guided optimisation, code layout) and the C++ core's hot paths (scheduler, IO, divider, ARM7, DMA, sound) |
| 4 | `render` | Fable 5.1 | Gengis 3D, the Banana 2D engine, the geometry engine, the threads over the four A55s, the KMS frontend's frame path |
| - | coordinator | Opus 5.5 | the main session: integrates, checks, pushes |

## Files

| File | Who writes | What |
|---|---|---|
| `BOARD.md` | everyone, **append only** | the bulletin board: claims, results, questions, dead ends, hand-overs (format below) |
| `LEDGER.md` | `sim` and the coordinator | every result that was measured again by someone else: VERIFIED, REFUTED or PENDING |
| `DEAD-ENDS.md` | everyone, append only | what was tried and doesn't work, and what earlier sessions already ruled out. **Read it before starting an idea** |
| `status/<name>.md` | that agent only, overwritten | what it is doing now, its branch and head, what is next: enough for another agent to take over |
| `evidence/<name>/` | that agent | the raw output behind every number on the board (text only, small) |
| `rgds-sim/` | `sim` | the simulator: plugin, scripts, calibration, README |
| `patches/` | each agent at the end, then the coordinator | `git format-patch` series against `00c34c78e8de`, to apply to SuperDrastic with `git am` |

## Rules (they are there to stop hallucinated numbers and work in circles)

1. **No number without evidence.** A number on the board points to a file in `evidence/` that holds the exact
   command line and its raw output. If it isn't in a file, it wasn't measured.
2. **Say what kind of claim it is:** `MEASURED` (ran it, here is the file), `READ` (from code or docs, with
   `file:line`), `GUESS` (a hypothesis; never written as a result).
3. **Claim before working.** Post a `CLAIM` before starting an idea, after checking `DEAD-ENDS.md`, `LEDGER.md`,
   `BOARD.md` and BananaDS's `docs/PERF.md` (sections "Tried and dropped", "What not to redo", "What's left").
   Someone else's claim on the same thing means talk first (`SendMessage` to them, or a `QUESTION` on the board).
4. **Time-box.** An idea gets about 75 minutes. No measurable gain by then: a `DEAD-END` post with the numbers, and
   the next idea. A refuted claim is never retried without new evidence.
5. **Exact or nothing.** Every change keeps the emulation exact (the same pictures, main RAM and sound as the base on
   the test ROMs), and sits behind a toggle (a `--set` setting or a `MELONDS_*`/`BANANADS_*` variable) so the device
   can A/B it. Changes that trade accuracy for speed are allowed only as an off-by-default toggle, said so loudly.
6. **Wall time on this machine means nothing** (4 shared x86 cores, four agents). Speed is measured with
   `rgds-sim` (deterministic) or, until it is ready, qemu-count's instruction counts. Instruction counts alone have
   misled before: BananaDS's PERF.md records a JIT change that cut 1% of instructions and ran slower on the A55
   (L1I +22%, iTLB +43%).
7. **Verified means re-measured by someone else.** A `RESULT` goes into `LEDGER.md` as PENDING; `sim` (or the
   coordinator) re-runs it from the agent's branch and marks it VERIFIED or REFUTED.
8. **Git:** each agent works only in its own worktree (`/home/user/perf/<name>/sd`, branch `perf/<name>`) and
   commits there. Nobody but the coordinator runs git in `/home/user/BananaLinux`.

## Board format

Append with a heredoc (`cat >> BOARD.md <<'EOF' ... EOF`), never by rewriting the file:

```
### 16:05 UTC · jit · RESULT · cycle budget in x27
MEASURED. cpubench-thumb, rgds-sim v1: main thread 11.20 -> 10.31 ms/frame at 1104 MHz (-7.9%); pictures and RAM
identical (difftest --every, 300 frames). Toggle: MELONDS_JIT_BUDGET=0. Branch perf/jit @ 1a2b3c4.
Evidence: evidence/jit/budget-ab.txt
```

Kinds: `CLAIM` (I'm starting this), `RESULT` (measured), `DEAD-END` (tried, no gain or not exact), `QUESTION`
(`@name` or `@coordinator`), `ANSWER`, `INFO` (a fact others need: a tool, a trap, a path), `HANDOFF`.

## The shared setup (cloud machine)

| Path | What |
|---|---|
| `/home/user/perf/base` | BananaDS at `00c34c78e8de` (read-only: a git worktree of `/home/user/jorrefog/superdrastic`) |
| `/home/user/perf/tools/build-a64.sh <tree> <build-dir> [-D...]` | an aarch64 Cortex-A55 LTO headless build, as BananaLinux builds it (ccache shared) |
| `/home/user/perf/build-ref/b/bananads` | the reference build of the base (also `bananads-difftest`, `bananads-tests`) |
| `/home/user/perf/roms/` | every test ROM built from the repo (stress L1-L10 ARM/Thumb/busy ARM7, cpubench, membench, ds2dtest, iorom, geomtest, sndbench, jitloops, ipcrom, dmasnd) |
| `/home/user/perf/qemu-src/build/qemu-aarch64` | qemu 9.2.4 with TCG plugins (Ubuntu's `qemu-aarch64` has none) |
| `/home/user/perf/<name>/sd` | each agent's worktree, branch `perf/<name>` |

Session 3 starts from [session3/BRIEF.md](session3/BRIEF.md): Mario Kart DS's race start (issue #17), its device
profile, and the geometry engine as the job.
