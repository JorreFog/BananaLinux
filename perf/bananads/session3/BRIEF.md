# BananaDS: Mario Kart DS's race start, a brief for cloud agents

Written 2026-10-18, after BananaLinux issue #17 ("Degraded performance with BananaDS with update 0.4.0 on the RG DS
Plus") was measured on a real RG DS Plus. Read `perf/bananads/GUIDE-FOR-CLOUD-AGENTS.md` (BananaLinux PR #16) first:
its rules still hold. This brief adds one game, its profile, and the one job that matters for it.

## 1. The problem

Mario Kart DS at 2x (BananaOS's default) drops to **52-57 fps for ~12 s at every race's intro and countdown, even at
the top clock (1992 MHz)**. After the countdown the race holds a steady 60 and the governor steps down to 1416 MHz
without a missed frame. Players see stutter and, with 0.4.0's audio servo, hear the pitch drop with each dip.

Measured on the Plus through the real launcher (`run-bananads`, KMS frontend, `kms.stats=60`, empty cpugov memory),
Figure-8 Circuit Grand Prix, the player holding A:

| Build | Race intro/countdown | Rest of the race |
|---|---|---|
| 0.4.0 (BananaDS 00c34c78, `core.slice=64`, the default) | 52-57 fps at 1992 MHz | 60 fps, 1416 MHz is enough |
| 0.3.0-fixes.5 (BananaDS a62122b, `core.slice=auto`) | 50-54 fps | 60 fps |
| 0.4.0 with `core.slice=auto` | mostly 60, one dip to 52 | 60 fps |

So 0.4.0 didn't make the emulator slower. The race start was always too heavy. `core.slice=auto` is not the answer:
it breaks Golden Sun: Dark Dawn (DEAD-ENDS.md). The fix has to be an exact speed-up of the emulator thread.

## 2. Where the time goes

Per-thread CPU during the countdown, KMS, 1992 MHz:

| Thread | Busy |
|---|---|
| `bds-emu` (the emulator thread) | **93.5%** |
| 3 Gengis workers | 41-44% each |
| `bananads-2d` | 30% |
| audio, mic | ~2.5% each |

The emulator thread is the limit, as in HeartGold. But the mix inside it is different. Here is the `prof.hz` profile
(the tools branch's profiler, CI build of `claude/bananads-perf-tools` 40c432e, headless, frames 1650-2150, 1992 MHz,
`core.slice=64`, 59.8 fps over the window, slowest 100-frame window 53.7 fps):

| Area | Mario Kart race start | HeartGold overworld (2026-10-12) |
|---|---|---|
| **3D geometry** | **31.1%** | 13% |
| JIT-generated code | 29.1% | 32% |
| scheduler, RunFrame, events | 9.3% | 10% |
| SPU | 7.7% | 6.6% |
| DMA | 6.3% | |
| memory/IO slow paths | 5.5% | 18% |
| 2D and video | 3.7% | |
| JIT compiler + invalidation | 1.2% | |
| JIT dispatch and exits | 1.2% | 7% |
| waits (yield, futex) | 1.1% | |

Top symbols: `GPU3D::Run()` 13.0%, `main::{lambda()#1}` 8.0% (the emulator thread's frame loop with what was inlined
into it: find out what that is), `GPU3D::WriteToGXFIFOBody<true>` 6.4%, `DMA::Run9()` 5.3%,
`GPU3D::SubmitPolygonBody<true>` 5.3%, the SPU's event lambda 3.7%, `GPU3D::SubmitVertexBody<true>` 3.0%,
`RunValid<2>` (SPU) 3.0%, `pthread_cond_signal` 0.9%, polygon sorting (`__move_merge` + `__insertion_sort`) 0.9%,
`ClipAgainstPlane` ~1.3% together.

PMU over the window: 29.95M cycles and 14.71M instructions a frame, **IPC 0.49**, 13.4 branch misses, 9.9 L1I misses
and 4.15 iTLB misses per 1000 instructions. A frame at 60 fps has 33.2M cycles at 1992 MHz, so the slow frames are
right at the edge. **The target: about -15% of the emulator thread's cycles in this scene.** That gives 60 at 1992 MHz
with headroom, and every percent beyond that lowers the clock in the rest of the race.

The full profile is next to this file: [prof-mk-race-start.txt](prof-mk-race-start.txt).

## 3. The job: the geometry engine

The geom agent of session 2 already found it: after patch 0011 the geometry engine has no single hot spot, and
**"the CPU paces the command stream (2.7-5.3 FIFO entries per `Run` call), so the next lever there is running the
geometry engine lazily"**. In Mario Kart that lever is worth up to a third of the emulator thread.

What "lazy" means here, as a starting point (check it against the code, don't take it as a design):

1. **Run the geometry engine in large batches, not per CPU catch-up.** Today `GPU3D::Run()` runs every time the CPUs
   catch up, a few FIFO entries each time. Instead, let commands queue up and run them only when something can see
   the result:
   - a read of GXSTAT, RAM_COUNT, the clip matrix or vector results, or the position/vector test results;
   - the FIFO reaching its "full" or "half empty" level for an IRQ or DMA the game uses (GXFIFO DMA mode, GXSTAT IRQ
     bits);
   - VBlank or the swap (SwapBuffers);
   - a savestate.
2. **Keep the timing exact.** The command cycles still have to be accounted for. Compute when the FIFO would have
   drained, its level at any time, and when the busy bit clears from the queued commands' costs, without running them
   one by one. Any game that polls GXSTAT in a loop must see the same values at the same cycles.
3. **Or move it to a thread**, if the batch boundaries make it possible: the geometry results only have to exist by
   the swap. Exactness rules are the same, and the waits must not land back on the emulator thread.

Smaller items from the same profile, after the geometry engine or by a second agent:

- `main::{lambda()#1}` at 8%: find out what is inlined into the frame loop. If it's the scheduler, see session 2's
  `loop` branch.
- `DMA::Run9` at 5.3%: likely the GX FIFO DMA that feeds the geometry. It may disappear with item 1 (DMA in blocks
  into a batched FIFO).
- The SPU at ~7.7% (event lambda + `RunValid`): the same in HeartGold. Look at whether channels can be mixed per
  block instead of per sample event, exactly.
- Polygon sorting and clipping at ~2%: small. Leave them.

## 4. How to reproduce it off the device

The scene needs the user's Mario Kart save (it skips the first-time setup). Ask for it, or use the headless input
below with a fresh save and adjust it (a fresh save goes through name entry first).

The headless run that reaches the race (frames are with `rtc.fixed=on`):

```sh
IN=$(python3 -c "print(','.join([f'{f}:a' for f in range(300,1400,45)]+['1400:a:1400']))")
bananads --frontend headless --set rtc.fixed=on --set render.renderer=gengis --set render.scale=2 \
  --set core.slice=64 --set wifi.dns=off --set resume.on_start=off --set resume.on_quit=off \
  --set headless.frames=2700 --set headless.measure_from=1650 --set headless.window=100 \
  --set headless.input=$IN "Mario Kart DS (USA) (En,Fr,De,Es,It).nds"
```

With the user's save, A taps reach Single Player > Grand Prix > Mario > the first kart > Mushroom Cup > Figure-8
Circuit. The race intro loads around frame 1080, the countdown and start run from about 1650 to 2150 (the heavy part),
and A is held from frame 1400 (gas). Use `headless.dump_dir` and `headless.dump_every=60` to check you are in the
race. The scene's frames differ between builds only if your change isn't exact.

Exactness, as in the guide:

- Frames: `headless.dump_every` dumps at frames 1700, 1900, 2100, 2400, base vs yours.
- Audio: `headless.audio_dump`, compared with `cmp`.
- Main RAM: as in the existing exact suite.
- Golden Sun: Dark Dawn and HeartGold as the other two games. Golden Sun is the one that broke with `core.slice=auto`
  and catches IPC and timing mistakes.
- A sabotage build that your check catches. One of session 2's toggles (`jit.flagstub`) went uncaught. Don't repeat
  that.

## 5. What the user will run on the device

Each branch gets these runs on the Plus. Give the exact commands in your hand-over:

1. `mk.sh` headless at 1992 MHz, frames 1650-2150 and 1700-2700, base vs yours. **Report the slowest 100-frame
   window, not only the average.**
2. The same with `prof.hz=1000` if the branch has the profiler, so the next session starts from a new profile.
3. A KMS run through `run-bananads` (`kmsmk.sh`): `kms.stats=60` windows through the countdown, and which clock the
   governor settles at for the rest of the race.

Keep runs short (the user's rule): one or two configurations, under a minute each.

## 6. Rules (the same as session 2, plus two)

- **Emulator thread only.** The Gengis workers and the 2D thread are at 30-44%: making them faster changes nothing.
- **One toggle per change.** Its "off" is the old behaviour. Default it on only when it's exact.
- **Base: SuperDrastic `00c34c78e8de`** (BananaLinux's pin), as session 2. Say if your branch also needs one of
  session 2's branches.
- **No simulators**, and no tuning on test ROMs. Mario Kart's race start is the benchmark. Qemu instruction counts
  only to confirm a path got cheaper.
- **New: report per-scene worst windows.** Averages hid this problem: 0.4.0 averages 78 fps headless over the race.
- **New: don't touch the audio path's rate control.** The pitch fix lives on SuperDrastic
  `claude/bananads-audio-pitch` (c489375: WSOLA stretch, a calmer servo, a 60 ms buffer). It will be merged
  separately. If your change affects frame pacing, say so.

## 7. Deliverables

- A pushed branch on JorreFog/SuperDrastic (`claude/bananads-perf-geomlazy` or similar), one commit per change, each
  with its toggle, the exactness checks it passed, and "untested on device".
- A hand-over in BananaLinux PR #16 under `perf/bananads/session3/`. It needs:
  - the expected gain on Mario Kart's race start and why;
  - the device commands;
  - what you found that the next session should know.
- If the CI device build (`bananads-device` workflow) runs on your branch, name the artifact. The user tests that
  binary rather than building one.
