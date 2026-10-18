# Session 2, agent `memio`: the GX FIFO's input side on the emulator thread

Branch **`claude/bananads-perf-memio`** on JorreFog/SuperDrastic, from `00c34c78e8de` (the commit `bananads.mk` pins).
Head: HEADSHA. Everything here is **untested on the device**. qemu instruction counts below only show that the path
I changed got cheaper; they are not results.

## The commits

| # | Commit | Toggle (default) | melonDS patch | What |
|---|---|---|---|---|
| 1 | C1 | `render.gx_batch` (`on`); `MELONDS_GXBATCH=0` | 0021 | the JIT's STMs into the GX FIFO (0x04000400-0x0400043F) handed to the geometry engine in one call; the packed commands decoded and appended to the FIFO with their state in registers; DMA's GX block path (0020) uses the same loop |
| 2 | C2 | `render.gx_str` (`on`); `MELONDS_GXSTR=0` | 0022 | the JIT's single STRs to the GX FIFO port and the command ports with fewer layers; a FIFO-port word that is a parameter before its command's last goes straight into the FIFO |

Plus `bananads/tests/gxstm/` (new test ROM, commit 1): NNS-style packed display lists (1-4 commands per header, packed
NOPs and all-NOP headers, matrix loads/multiplies, push/pop/store/restore, scale/translate, materials, every vertex
format, POS/VEC/BOX tests) sent by ARM STMIA bursts of 1-8 words (as NitroSDK's `GX_SendFifo*`), STMDB, Thumb
STMIA, single STRs, immediate DMA and GX-FIFO DMA, and STMs that straddle the FIFO's end; it logs GXSTAT, RAM_COUNT,
the clip matrix and the tests' results to main RAM (0x02200000) and draws the polygons. ~2,300 STM words and ~400
STR words a frame.

## Expected device gains, ranked (with the reasoning)

The thread to win on is HeartGold's emulator thread (99% busy at 2x and 1x+2D thread). Its only profile
(2026-10-12, an older build) put `SlowBlockTransfer9` = STMs into the GX FIFO at **5.7%** of the thread, memory/IO
slow paths 18% in all, 3D geometry 13%.

1. **`render.gx_batch` (0021): +1 to +2.5% fps on HeartGold (2x and 1x), if the profile's 5.7% is still there.**
   What an STM into the FIFO cost before, per the device build's disassembly (GCC 14.3 -O2 LTO): the JIT's slow call
   (~20-25 instructions: the registers to the stack, caller-saved guest registers, the call), `SlowBlockTransfer9`'s
   general checks with a 6-pair frame (~50), then per word a call to `WriteToGXFIFO` -> `WriteToGXFIFOBody<true>`
   (~55-65: a 4-pair frame, NumCommands/CurCommand/ParamCount/TotalParams loaded and stored, the FIFO's count, write
   position and GXSTAT loaded and stored per entry). Now: the JIT side unchanged, ~25 for the entry checks plus the
   frame, ~25-30 per word (state in registers, stored once). For NitroSDK's shape (a 4x3 matrix is one header STR and
   four 3-word STMs) an STM goes from ~255 to ~140 instructions (-45%); for 8-word STMs from ~550 to ~260. On gxstm
   (qemu-count, device build) the whole GX FIFO input path (`SlowBlockTransfer9*`, `WriteToGXFIFO*`, `SlowWrite9`'s GX
   route, `DMA::Run9`'s GX block) went 327.8k -> 239.8k instructions a frame (-27%; that ROM also sends DMA and STR
   words, which 0021 helps less). So: if 5.7% of the thread is this path, about 40% of it goes: **-2.3% of the
   thread at best, -1% if the profile's share has shrunk since 0010/0011** (I can't tell: the 2026-10-12 build's patch
   level isn't recorded). Instruction counts aren't A55 time, but this path is loads/stores to a few hot lines and
   calls, nothing that should run better than its count.
2. **`render.gx_str` (0022): 0 to +1%.** A single STR to the FIFO port (NitroSDK writes a header word before each
   `GX_SendFifo*`; `MI_CpuSend32`, if NNS G3D sends display lists with it, is a word-by-word STR loop) or to a command
   port (NitroSDK's immediate `G3_*` writes) cost ~95-130 instructions: `SlowWrite9` (~33, its region switch first),
   `GXWrite9Route` (~8), then `WriteToGXFIFO` + `WriteToGXFIFOBody` (~65, a 4-pair frame) or `GPU3D::Write32` (~46).
   Now the GX test comes before the region switch, the route's call is gone, and a parameter word before its
   command's last (most words of a matrix, the first word of each VTX_16) goes into the pipe/FIFO with no call and
   no frame. QCOUNTSUMMARY How many such STRs HeartGold does per frame is unknown (the IO stats below say); at
   ~1,000 a frame it is ~0.1% of the thread, at ~10,000 (display lists by `MI_CpuSend32`) up to ~1%.

Both are exact by construction (the same statements in the same order for the cases they take, everything else
handed to the old code) and checked (below).

## The device A/B commands, most promising first

Binaries: build this branch (or the coordinator's merge) as the device runs it. The base is the same binary with
the toggle off (each toggle's `off` is the code before it).

```sh
# 1. render.gx_batch (0021): new default vs off, 2x and 1x
CLK=1104000 EXTRA="--set render.scale=2" hgquick.sh ./bananads
CLK=1104000 EXTRA="--set render.scale=2 --set render.gx_batch=off" hgquick.sh ./bananads
CLK=1104000 EXTRA="--set render.scale=1" hgquick.sh ./bananads
CLK=1104000 EXTRA="--set render.scale=1 --set render.gx_batch=off" hgquick.sh ./bananads
# 2. render.gx_str (0022)
CLK=1104000 EXTRA="--set render.scale=2 --set render.gx_str=off" hgquick.sh ./bananads
# both off (the base's GX input path) against both on
CLK=1104000 EXTRA="--set render.scale=2 --set render.gx_batch=off --set render.gx_str=off" hgquick.sh ./bananads
# threads: did the emulator thread's share drop?
threads.sh ./bananads --set render.scale=2
threads.sh ./bananads --set render.scale=2 --set render.gx_batch=off --set render.gx_str=off
# exactness on the game (the kit's exact.sh): on vs off must give the same md5 lists
exact.sh memio-on ./bananads --set render.scale=2
exact.sh memio-off ./bananads --set render.scale=2 --set render.gx_batch=off --set render.gx_str=off
```

### Please also run (decides the next batch): HeartGold's slow accesses by address

Patch 0005's counters, on HeartGold's overworld. With them on, every slow access takes the counting path, so the
fps of this run means nothing. Two runs, ~30 s each:

```sh
cd /data/bananads-dev; rm -rf /tmp/hgb; mkdir -p /tmp/hgb; cp hg.sav /tmp/hgb/hg.sav
HG="--frontend headless --set save.dir=/tmp/hgb --set state.dir=/tmp/hgb --set resume.on_start=off --set resume.on_quit=off \
 --set wifi.dns=off --set rtc.fixed=2026-10-11_12:00:00 --set render.gengis_threads=2 --set render.scale=2 \
 --set headless.frames=2100 --set headless.input=$(cat walk-lr.txt)"
MELONDS_IOSTATS=1500 ./bananads $HG hg.nds 2>&1 | sed -n '/IO stats/,$p' > iostats-hg.txt
rm -rf /tmp/hgb; mkdir -p /tmp/hgb; cp hg.sav /tmp/hgb/hg.sav
MELONDS_IOSTATS=1500 MELONDS_IOFAST=0 ./bananads $HG hg.nds 2>&1 | sed -n '/IO stats/,$p' > iostats-hg-noroutes.txt
```

The first gives the JIT's slow loads, stores, LDMs and STMs by address and region per frame (STM words into
0x04000400 are 0021's path, STRs to 0x04000400-0x5FF are 0022's); the second completes the "IO register (all
callers)" table. And with `tools`' profiler in the binary, `hgprofile.sh ./bananads --set render.scale=2` twice
(`render.gx_batch`/`gx_str` on, then both off): its "memory/IO slow paths" and "3D geometry" rows are the device's
answer to the two estimates above. `GPU3D::Run` is one symbol under LTO (the whole command path is inlined into it);
`prof.raw=<file>` plus `addr2line -e bananads.debug` splits it by source line, which is what the `geom` agent needs.

## Exactness checks run (qemu-dev, build-a64-device.sh builds; toggle off = A, on = B, everything else default)

- **0021** (`render.gx_batch=off` vs `on`): `exact-suite.sh` (17 ROMs' pictures every 50 frames to 300 and dmasnd's
  audio, difftest main RAM every frame on 9 ROMs, bananads-tests): **FAILS=0**, on the first version and again on
  the final one. `bananads-difftest --every --frames 300` on gxstm: **SAME**. Sabotaged builds: all-NOP headers not
  written to the FIFO (only GXSTAT/RAM_COUNT timing would show it) -> **caught at frame 2**; one word of 7-word STMs
  dropped -> **caught at frame 1**.
- **0022** (`render.gx_str=off` vs `on`): `exact-suite.sh` **SUITE22**; gxstm difftest `--every`: **SAME**. Sabotage
  (ParamCount not counted for 3-parameter commands on the fast path): **SAB22**.
- **Both** (all off vs all on), every 10th frame of 300: gxstm, geomtest, membench, dsstress L10, each with the soft
  renderer at 1x, Gengis at 2x with 2 threads and the 2D thread, and a savestate save+reload at frame 150; difftest
  `--every` on gxstm, geomtest, dsstress L5 and iorom L5: EXTRA

## Risks

- The 5.7% is from a profile of an older build; if HeartGold's share of this path is now smaller, the gain is
  smaller (the A/B decides). If HeartGold sends its display lists by DMA rather than STM, 0021 still covers 0020's
  DMA block path, but that path was already cheaper per word.
- 0021 adds a 4-instruction check (flags, the address) in front of every `SlowBlockTransfer9` call that isn't a GX
  STM (LDMs/STMs through the slow path to anything else): a tail call into the old body.
- 0022 puts the GX port check before `SlowWrite9`'s region switch for every 32-bit slow store (a load-free compare on
  the address, the flags already loaded).
- With `MELONDS_IOSTATS` on, 0021's STM path is skipped (the counting path runs), as the other fast paths do.
- `FIFO<T,N>` gained three accessors (used only by 0021's loop); `geom` (the geometry engine's command path) was told
  which GPU3D functions 0021/0022 touch (`WriteGXFIFOWords*`, `WriteGXFIFOBlock`, `WriteToGXFIFO`,
  `WriteGXFIFOPortFast`; 0021's loop replicates `CmdFIFOWriteFast` inline, so a change there must be mirrored).

## Next, if the device confirms

1. Rank by HeartGold's own IO stats (above). Candidates I looked at and left for that data: GPU3D register reads
   (GXSTAT polls, CLIPMTX/VECMTX by LDM, which run `GPU3D::Run` per word) through a direct route; the ARM7's sound
   register writes; LDM from IO.
2. If 0021 shows and HeartGold's STMs are short (3 words), the remaining fixed cost is the JIT's slow call itself
   (~20-25 instructions: registers to the stack and back): an STM whose address the JIT can see is the GX FIFO could
   call a GX-only entry directly.
3. The FIFO's input and the geometry engine's FIFO reads now differ in shape (the input in registers per block, the
   reads per entry); `geom` owns the read side.
