# Pokémon White 2 freezes (user report, 2026-11-03)

A user on BananaOS 0.5.2.2 (RG DS Plus) reported two freezes in Pokémon White 2:

1. **BananaDS**: "sometimes the game completely freezes when I switch Pokémon during a battle".
2. **DraStic (SuperDrastic)**: "the game freezes whenever the console wakes up from sleep mode".

They sent their `banana/logs` folder. Both were then tested on an RG DS (non-Plus) with 0.5.2.2, using Black 2
(the same engine as White 2) and an in-game Black 2 save. For most runs a test process held 2.2 GB of RAM, so
the RG DS had about the Plus's free memory.

**Status: BananaDS freeze reproduced and narrowed to melonDS's JIT, not fixed. DraStic stall not reproduced.**
This document is the findings. No code change is in this PR.

## 1. BananaDS: frozen Pokémon switch in battle

### What it looks like

In a wild battle you tap POKéMON, then a Pokémon in the party list, and nothing more happens. The party list
stays up and doesn't answer taps or buttons, while the top screen keeps animating. The BananaDS menu still works.
The shipped BananaDS then logs one line per slice:

```
ARM9: data abort (0207057E)
ARMv5 PC in non executable region 02FE3624
ARMv5 PC in non executable region 02FE3624
...
```

It wrote **944 MB in 18 minutes** to `banana/logs/bananads/bananads.log`. Left running, a frozen game can fill
the card.

### What happens (a debug build logged the address and the protection unit)

```
ARM9: data abort at 00000014 (PU_Map 00, CPSR 2000003F, control 0005707D, data RW 15111111,
      regions 04000033 0200002B 027FF017 08000035 02FE001B 0100002F FFFF001D 02FFF017)
ARM9: data abort (0207057E)
[jit] ARM9 code under the DTCM at 02FE3620: run from main RAM
ARM9: data abort at 00000000 ...  (02FE362C)
ARM9: data abort at 00000550 ...  (02FE3630)
...
ARM9: prefetch abort (02FE363C)
```

1. At the tap on a Pokémon, Thumb code at about `0x0207057A` (system mode) reads **address `0x14`: a null
   pointer + 0x14**. No protection region covers address 0, so the read aborts. A real DS does the same with
   these regions, so the emulation is right at this point. The pointer itself is wrong.
2. The game's abort handler (NitroSDK, through the BIOS vector) runs code at `0x02FE3620`. That is under the
   ARM9's DTCM, which B2W2 moves to `0x02FE0000`. It aborts again and again and stops. In the shipped JIT this
   shows up as "PC in non executable region", because the JIT's code-region setup classifies the address as
   DTCM.

So the real bug is **a pointer that is null when the party screen uses it**. Something earlier in the emulation
went differently from a DS.

### What decides it (RG DS, 0.5.2.2, Black 2 on Route 6)

A round is: a wild battle seen on the bottom panel (counted by a detector, see the kit), switch twice, run away.

| BananaDS | result |
|---|---|
| JIT, defaults, 3 fresh sessions | froze at the 3rd–4th switch (in the 2nd battle) every time |
| JIT, `jit.timing = exact` (cycle-exact timing) | froze the same way |
| JIT, `jit.ldm_mainram = off` | froze the same way |
| **interpreter** (`jit = off`) | 3 battles, about 8 switches, no abort |
| state loaded in battle, then the switch (JIT) | no freeze in 14 tries (a load empties the JIT's code cache) |

So it's the **JIT** (melonDS's or BananaDS's patches to it). It's not its block timing, and not `ldm_mainram`.

**Correction.** An earlier draft of this investigation blamed `jit.ldm_mainram`, and before that a DTCM fix.
Both were wrong. Rounds without a battle had been counted as battles, and a fix that only removed the error
line had been taken for a fix of the freeze. Count battles, and check that the game still answers.

### Next steps

- Bisect the JIT's own options (`docs/SETTINGS.md` `jit.*`) and BananaDS's melonDS patches with the counted
  session below. One data point is about 3 minutes when it freezes, about 10 when it doesn't (use ≥ 3 battles).
- Try upstream melonDS's JIT at the pinned commit without BananaDS's patches. If it freezes too, it's an
  upstream bug (search melonDS issues for B2W2 + JIT).
- With the debug build: find who writes the pointer the code at `0x0207057A` loads (break on the abort,
  dump the registers and the stack).

### Also prepared (SuperDrastic, local branch, not pushed)

SuperDrastic branch `fix/jit-dtcm-code` (one commit, `bef6d71`, on top of the shipped `d941c8f`) adds melonDS
patch `0430-ARMJIT-code-fetches-ignore-the-DTCM.patch`. With it, the JIT skips the DTCM for code addresses (as
the hardware and the interpreter do), and a one-time `[jit] ARM9 code under the DTCM at …` line marks the case.
**It does not fix the freeze.** It does end the 944 MB log loop, and it gives a cheap marker. Its commit message
still claims a fix and must be corrected before it goes anywhere.

## 2. DraStic: "freezes after waking from sleep" — not reproduced

What the user's logs show (`superdrastic.log*`, `sleep.log`, `banana-ui.log.1`):

- **Sleep itself was fine.** After every wake, DraStic presented 60 frames a second.
- The freezes came after **loading a save state from the in-game menu** (the user loads slot 0 right after
  waking, so it looks like a sleep bug to them). The top screen then got no new frames for about 38 s, and
  DraStic didn't call `gettimeofday` for 1–3 s at a time (the `[clock] a +N s gap` lines from #64's fix). The
  CPU was about 10% busy, so DraStic was waiting, not working.
- In their logs, the 3 loads followed by reopening the menu within about 2 s stalled; the 2 loads without a
  reopen did not. New Super Mario Bros loaded states many times and never stalled.
- DraStic exit status 137 is the normal quit (SIGKILL), not a crash.

Tried on the RG DS: 8 loads (White 2 title screen, Black 2 in battle), with and without reopening the menu
after 0.5 / 1.5 / 3 s, and 3 with the Plus's free memory and the file cache dropped. **None stalled.**

Open leads: the Plus's 1 GB with a 512 MB ROM (the stalls look like waiting on the card), and the menu's
load/undo path (`src/menu.c` `do_load`, `src/resume.c` `menu_jobs`) with the clock-gap hook. Ask the user for:
the flight recorder on during a freeze, the `superdrastic` logs right after it, and whether waiting about 40 s
brings the game back.

## Other things in the user's logs

- The Russian-patched New Super Mario Bros ROM segfaults DraStic (exit 139) 3 times. This is a separate issue.
- The exFAT data partition is marked not cleanly unmounted (hard power-offs).
- The device clock jumped about 10 days ahead (the last boot is dated 2026-11-13).
- `aw88166 … device start failed. retry = 0` on every stream start is normal: a healthy Plus logs it too.
  The user's unit reached `retry = 4` on both amps during one "safer way" suspend retry.
- `suspend failed (Connection timed out): trying the safer way` appears often in `sleep.log`.

## The test kit (left on the RG DS in `/data/w2test`)

Everything ran over ssh, under the device lock:

- `k.py <event> <code> [ms]`: a key press (`event7` pad: A 305, B 304, d-pad 544-547; `event3` HOME = 309).
- `tapb.py X Y`: a tap on the RG DS bottom panel (Goodix `fe5e0000`, 640×480 panel pixels; BananaDS logs
  `[touch] down at X,Y on the panel -> x,y on the DS`). B2W2 battle: POKéMON 500,380; party 440,90 / 180,190 /
  440,190 / 180,290 / 440,290; SHIFT 310,210; RUN 310,406.
- `grab.py` (DRM scanout screenshots).
- `isbattle.py`: exit 0 when the bottom panel shows the battle menu (red top half).
- `restart.sh "KEY=off ..."`: quit BananaDS, write `/data/banana/bananads.env` with those `BANANADS_JIT_` keys,
  launch Black 2 and walk into the grass on Route 6.
- `soak4.sh N`: N counted battles (walk, battle seen, 2 switches, run). It stops at the first
  `data abort` (debug and DTCM-patched builds). `soak3.sh` stops at `non executable` instead (the shipped
  binary).
- `hog.py 2200`: holds 2.2 GB, so the RG DS has the Plus's free memory.
- `abort-debug.log`: the debug build's abort lines above.

Also on the card for this: `roms/nds/Pokemon Black 2.nds` and `Pokemon White 2.nds`, the Black 2 save, BananaDS
states `Pokemon Black 2.1/.9.mst` (`.9` is frozen), and DraStic states for both. Remove them when done.

Traps hit:

- `/run/banana/launch` only finds a game the library has scanned. The rescan happens only on HOME (press B in
  the game list).
- A blind key sequence after a launch that didn't start lands in the menu and starts something else.
- B2W2: the first d-pad presses are lost while the map loads. "Down" on the title menu picks NEW GAME when the
  timing is off (quit without saving). The battle menu remembers its cursor, so d-pad sequences pick RUN (use
  taps). A won battle waits on an A press.
- md5 differences between screenshots are not battles: count battles with the detector.
- `pidof sh` plus a grep on the command line kills your own ssh shell.
