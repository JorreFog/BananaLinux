# Who can observe the geometry engine (melonDS 906e9eb + BananaDS patches 0001-0020), and what lazy needs for each

Written before coding (geomlazy, session 3). "Run(T)" = GPU3D::Run/RunFast at ARM9 time T.

## How the engine advances today
- The slice loop (patch 0009, NDS.cpp RunFrame JIT loop) calls `gpu3d.Run()` at the end of every ARM9 slice
  (core.slice=64) unless it is idle (pipe empty and GXSTAT busy bit clear, geometry off, or a swap pending), in which
  case it only sets `Timestamp`. GXSTAT reads (Read8/16/32 of 0x04000600-603) call Run() at the ARM9's current time.
- Run(T): `CycleCount -= T - Timestamp`; execute entries while `CycleCount <= 0` and the pipe isn't empty, each
  adding its cost (cost depends on the vertex/normal/polygon pipelines, and the polygon pipeline on whether the
  polygon survived culling/clipping: so costs need the commands' real work); if the pipe ran dry with
  `CycleCount <= 0`: FinishWork (the credit goes into the pipelines, CycleCount = 0, busy bit cleared when they are 0).
- Consequence: the emulated timing is defined by *when Run is called*. Run(Tk) then Run(Tk+1) equals Run(Tk+1) alone
  only when Run(Tk) executes nothing, or when the pipe does not run dry before Tk and no write between Tk and Tk+1
  changes the PIPE/FIFO split. A lazy engine must therefore replay the Run points (Tk, number of entries written by
  Tk) exactly; it cannot just "run to now".

## Observers (each must see exactly what it sees today)
| Observer | Where | What it sees | Lazy design must |
|---|---|---|---|
| GXSTAT read (0x600-603, 8/16/32) | GPU3D::Read* | Run(now) first; busy bit 27, push/pop 14, test 0, stack overflow 15, matrix stack pointers, FIFO level, <half, empty | replay pending Run points, then Run(now) |
| RAM_COUNT (0x604/606) | Read16/32 | NumPolygons/NumVertices as of the last Run point (no Run) | replay pending points, *not* Run(now) |
| CLIPMTX_RESULT (0x640-67F) | Read32 | clip matrix after the last Run point | same as RAM_COUNT |
| VECMTX_RESULT (0x680-6A3) | Read32 | vector matrix after the last Run point | same |
| POS/VEC test results (0x620-635) | Read16/32 | after the last Run point | same |
| GX FIFO full (write of entry 257) | CmdFIFOWrite | stall: CPU (or DMA) stops; slice loop advances ARM9 by CyclesToRunFor | know the true level at the write: sync when the conservative level (true level + unexecuted) reaches 256 |
| FIFO < half -> GXFIFO DMA (mode 7) start | CheckFIFODMA at every pipe refill (every ~2 entries) and at a DMA burst's end | the DMA starts at the slice end where the level first drops below 128 | sync at every slice end while a mode-7 DMA is enabled, not running, and the level could be < 128 |
| FIFO IRQ (GXSTAT bits 30-31: <half or empty) | CheckFIFOIRQ at refills, GXSTAT writes, IF writes (0x214) | IF bit 21 set/cleared at the refill; visible via IF reads, the IRQ line (IE&IME), BIOS IRQ handler | sync at every slice end while the IRQ mode is non-zero and the condition could change; IF writes/reads sync too |
| IF read (0x214) / IRQ line | NDS | IF bit 21 | as above |
| Swap (SWAP_BUFFERS, 0x50) | ExecuteCommand | FlushRequest=1, CycleCount=325; engine halts until VBlank; VBlank takes the polygon RAM | sync before VBlank (GPU::StartScanline 192) |
| VBlank | GPU3D::VBlank | FlushRequest, polygon/vertex RAM, then RAM_COUNT reset | sync first |
| Geometry enable (POWCNT1) / DISP3DCNT | GPU::SetPowerCnt -> SetEnabled; Write32Plain | GeometryEnabled gates writes and Run | sync first |
| GXSTAT write (0x600: IRQ mode, stack-overflow ack) | Write32Plain/16/8 | changes IRQ mode, calls CheckFIFOIRQ | sync first |
| CPU writes to the FIFO (STR/STM/DMA) | WriteToGXFIFO, CmdFIFOWrite | goes to PIPE iff FIFO empty and PIPE not full (depends on interleaving with reads) | record write counts per Run point; replay the PIPE/FIFO split with counters |
| DMA registers / DMA busy bit | DMA::Cnt | a mode-7 DMA's enable bit and remaining count depend on when it ran | the DMA start points above |
| Savestate | GPU3D::DoSavestate | everything | sync first |
| The renderer (RenderFrame at scanline 214/VBlank) | GPU | Render* copies made at VBlank | sync at VBlank |

## Mario Kart's race start (measured, stats build, frames 1700-2150, see stream.txt)
- ~28,900 FIFO entries/frame, ~2,180 Run calls (13.4 entries per working call, 62 queued at its start).
- 31,700 words/frame through the GX FIFO DMA (mode 7, the block path of patch 0020), ~1,260 DMA::Run9 calls.
- GXSTAT IRQ mode 2 ("empty") the whole time: IF bit 21 toggles ~270 times a frame.
- The FIFO sits at ~62 entries (< 128): the GXFIFO DMA restarts at every burst end and every refill checks it.
- GXSTAT read 7 times a frame; RAM_COUNT, clip/vector matrices and tests never.

So in this stream the DMA trigger and the FIFO-empty IRQ make nearly every slice end an observation point.

## Decision: lazy geometry not built (what a lazy engine could save here, and why it isn't exact-able cheaply)
- What batching Run calls could remove: Run's per-call entry/exit (~90 instructions, session 2) x ~2,180 calls =
  ~0.2M instructions a frame (~1.5% of the thread under qemu), plus the slice loop's slower branch when the engine
  is busy. Not the 31%: the commands' work (matrix, lighting, vertices, polygons) and the FIFO traffic stay.
- Why not exact cheaply: (1) the emulated timing is defined by Run's call points (CycleCount credit, FinishWork when
  the pipe runs dry, the PIPE/FIFO split depends on how writes interleave with reads), so a deferred engine must log
  (Tk, entries written by Tk) for every slice end and replay them in order: the same per-entry work, plus the log.
  (2) Command costs depend on the commands' results (a polygon's pipeline cost depends on culling and clipping), so
  the FIFO level over time can't be computed without running the commands. (3) In this stream every slice end is an
  observation point anyway: the FIFO sits under half, the GX FIFO DMA (mode 7) restarts at every 112-word burst's
  end and at the refill that crosses 128, and GXSTAT IRQ mode 2 sets/clears IF bit 21 ~270 times a frame. Bounding
  the level (each entry costs >= 1 cycle) gives at most 1-2 slices of slack after a burst.
- What was built instead (exact, toggles): the per-entry and per-word costs that dominate (0050 cherry-pick, 0060,
  0061, 0062). See qcount.txt and exactness.txt.
- If lazy is tried later: the A55's L1I (IPC 0.49, 9.9 L1I misses per 1000 instructions) may reward running the
  geometry in longer bursts even without fewer instructions; that can only be seen on the device. The log/replay
  design above is the exact way; savestate byte-compares (exactness.txt) are the check that sees timing errors.
