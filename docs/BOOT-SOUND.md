# The boot chime on the Retroid Pocket Classic

This is a self-contained write-up of why Banana's boot animation could not play its click on the RPC, the
measurements, the confirmed root cause, the fixes, and what is left. It is meant to be read on its own.

Cross-reference: `docs/RPC-PORT.md` ("The menu's audio delay") and `docs/ARCHITECTURE.md` ("Sound").

## TL;DR

- The boot animation's click is a cue fired ~1 s into the splash, i.e. **~10.5 s after the kernel starts**.
- The RPC's audio could not play a single frame until roughly **19–33 s** after the kernel: a write returned
  `-EIO`, `snd_pcm_prepare()` returned `-EBUSY`, and it only recovered after several stream open/close cycles.
- **Root cause (confirmed in the driver source and by dmesg):** `aw88166_start_pa()` is kicked off from DAPM's
  `PRE_PMU`, which on this SoC runs **before the backend DAI starts the I2S bit/word clocks**. The amp's start then
  fails its `SYSST` check (`CLKS_STABLE | PLLS_LOCKED`) — `aw_dev_check_sysst()` returns `-EPERM` — and the driver
  burned its 5 retries **re-uploading the whole 85 KB ACF over I2C (~2 s each, ~12 s of blocking I2C)** before giving
  up, leaving the amp off until some later stream open happened to line up with a running clock.
- **Fix A** (kernel patch `0062-ASoC-codecs-aw88166-lock-on-the-i2s-clocks-not-the-acf-re-upload.patch`): retry the
  light start sequence on a short timer, without the ACF re-upload, so the amp locks a few attempts after the first
  write starts BCLK. The final failure line now carries the `SYSST` value.
- **Fix B** (userspace, in progress): a second, independent ~7 s is lost *before* the card binds — `q6apm`'s first
  `APM_CMD_GET_SPF_STATE` times out (`wait_event_timeout(..., 5 * HZ)` in `audioreach_send_cmd_sync()`), and only then
  does the DAIS service → rx_macro → SoundWire → WCD937x → card cascade run. `rcS` now emits per-step timing marks.

## The device and its audio stack

- **Hardware**: Retroid Pocket Classic — Qualcomm Snapdragon G1 Gen 2 (SM4450), one 1240×1080 AMOLED panel, no lid.
- **OS**: Banana (Buildroot, read-only squashfs root + exFAT data partition; no systemd, no udev, no sound server).
- **Audio**: WCD937x codec + **two AW88166 amplifiers** on I2S, driven by Qualcomm's **AudioReach DSP** (the
  `sc8280xp` / `q6apm` machine stack). No UCM profile: the route mixers are written *before* a playback frontend is
  opened, because the DSP instantiates a frontend's graph at the open and latches the mixer values it finds then.
  Only the **first** frontend produces sound.
- **Kernel** (from the built `.config`): `CONFIG_SND_SOC_AW88166=y` (built-in), `CONFIG_SND_SOC_WCD937X=m`,
  `CONFIG_DYNAMIC_DEBUG_CORE=y` but `# CONFIG_DYNAMIC_DEBUG is not set` (Banana's own fragment disables it — so
  `dyndbg=` and `/sys/kernel/debug/dynamic_debug/control` are empty; dev_dbg output needs a driver change, not dyndbg).
- **Device tree**: two `awinic,aw88166` nodes at I2C `0x34`/`0x35` on `i2c8` (`clock-frequency = <400000>`), each with
  `firmware-name = "qcom/sm4450/RPClassic/aw88166_acf.bin"` (~85 KB, present at `/lib/firmware/`).
- **rcS** brings the stack up: ADSP remoteproc → LPASS macros → SoundWire (WCD937x) → q6 services
  (`q6apm`/`q6prm`) → the machine driver (`snd-soc-sc8280xp`, which loads the AudioReach topology), then the two
  speaker routes.

## Measured timeline (one cold boot, `dmesg`)

```
[ 1.100] remoteproc0: powering up adsp
[ 1.241] remoteproc0: remote processor adsp is now up
[ 1.272] qcom,apr ...: Adding APR/GPR dev: gprsvc:service:2:1
         <-- ~7 s with no kernel log at all ->
[ 8.413] qcom-apm gprsvc:service:2:1: CMD timeout for [1001021] opcode     <- APM_CMD_GET_SPF_STATE
[ 8.413] platform ...:gpr:service@1:dais: Adding to iommu group 8
[ 8.451] rx_macro 3200000.codec: Driver needs fixing: Unsorted reg_defaults ...
[ 8.455] qcom-soundwire 3210000.soundwire: din-ports (0) mismatch ...
[ 8.467] wcd937x_codec audio-codec: bound sdw:2 ... / sdw:3 ...
[ 8.981] snd-sc8280xp sound: ASoC: Parent card not yet available, widget card binding deferred
[ 9.008] ALSA: Control name 'stream0.vol_ctrl0 MultiMedia1 Playback Volume' truncated to ...
[19.648] aw88166 0-0035: start failure (-1)
[19.881] aw88166 0-0034: start failure (-1)
[33.487] aw88166 0-0035: start failure (-1)
[33.726] aw88166 0-0034: start failure (-1)
```

and the menu's own log (`/data/banana/logs/banana-ui.log`, `sound:` lines): the device opens at **10.37 s**, every
write is `-EIO`, every `snd_pcm_prepare()` is `-EBUSY`, and the **first write that sticks is ~32.9 s**.

## Root cause 1 — the amp start runs before BCLK (Fix A)

`sound/soc/codecs/aw88166.c`:

- `aw88166_playback_event()` handles `SND_SOC_DAPM_PRE_PMU` → `aw88166_start(..., AW88166_ASYNC_START)` →
  `queue_delayed_work(system_dfl_wq, &start_work, 0)`.
- `aw88166_startup_work()` → `aw88166_start_pa()` → `aw88166_dev_start()`, which powers the amp up, configures the
  SYS PLL, then calls **`aw_dev_check_sysst()`**.
- `aw_dev_check_sysst()` (aw88166.c) polls `AW88166_SYSST_REG` for `CLKS_STABLE | PLLS_LOCKED` — **the I2S clocks and
  the PLL** — for `10 × 2 ms`, and returns **`-EPERM`** if they never appear. It logs only at `dev_dbg`, which is why
  nothing about it showed in dmesg.

`PRE_PMU` runs during `soc_pcm_prepare()`, *before* `soc_pcm_trigger(START)` — and the SM4450's backend DAI starts
toggling BCLK/WCK at the trigger (the first write). So the first attempt always fails the SYSST check.

What made it cost ~12 s instead of ~20 ms:

```c
for (i = 0; i < AW88166_START_RETRIES /* 5 */; i++) {
	ret = aw88166_dev_start(aw88166);          /* ~20 ms, fails at check_sysst */
	if (ret) {
		fw_ret = aw88166_dev_fw_update(aw88166, AW88166_DSP_FW_UPDATE_ON, true);  /* ~2 s ACF re-upload */
		...
	}
}
if (ret != 0) dev_err(..., "start failure (%d)\n", ret);
```

Five passes of a ~20 ms register sequence plus a ~2 s ACF re-upload. The ACF is already applied by the probe
(`aw88166_codec_probe → aw88166_dev_fw_update(DSP_FW_UPDATE_ON)`), so the re-upload is pointless when the failure is
"no clocks yet".

And because the start is only armed by `PRE_PMU`, a *held-open* stream never retries it: only a close/reopen does —
which is why the menu's reopens at 13.7/17.0/20.x s kept hitting a `start_work` still stuck in the upload loop, and
why "hold the stream open and retry the write" never recovered.

### Fix A

Patch `0062-ASoC-codecs-aw88166-lock-on-the-i2s-clocks-not-the-acf-re-upload.patch`:

- `aw88166_start_pa()` now does **one** light `aw88166_dev_start()`; on `-EPERM` (the clocks) it **re-queues
  `start_work` after 10 ms** (`AW88166_START_CLKW_RETRY_MS`) instead of re-uploading the ACF, up to
  `AW88166_START_CLKW_TRIES` (100 ≈ 1–2 s of attempts). The amp therefore locks a few attempts after the first write
  starts BCLK.
- Only a **non-clock** failure still reloads the ACF once (the old recovery for a wedged amp).
- On success after waiting, it logs `start: the amp locked after N I2S-clock waits`; on final failure it logs the
  `SYSST` register value (`start failure (%d) after %u clock waits, SYSST=0x%04x`) so a stuck clock is visible in
  dmesg without `CONFIG_DYNAMIC_DEBUG`.

## Fix A result, and what is left

Fix A works, and settles the amp question: dmesg now reads

```
[ 10.157] aw88166 0-0035: start: the amp locked after 2 I2S-clock waits
[ 11.003] aw88166 0-0035: start: the amp locked after 1 I2S-clock waits
...
```

— the amp locks in tens of ms, `start failure` is gone, and the 12 s of ACF re-upload along with it. But **the menu's
writes still fail until ~20–27 s**, so the amp was *a* cause of the 12 s spiral, not the cause of the frontier.

What the `-EIO` actually is, from the kernel source: `sound/core/pcm_lib.c`, `snd_pcm_lib_write1()` — when the DSP
never consumes the buffer it times out and returns `-EIO` ("playback write timeout (DMA or IRQ trouble?)"). So the
stream is prepared and the DAPM graph start returns 0, but **the ADSP does not pull audio until ~20 s**, and
`/proc/asound/card0/pcm0p/sub0/status` confirms it: `trigger_time` is the moment it finally ran.

Two candidates remain, and the rcS instruments above are what separates them:
1. the ~5 s `q6apm` timeout (below) delaying the whole bring-up; and/or
2. the ADSP's AudioReach service simply not scheduling the graph until ~20 s after boot.

## Root cause 2 — the ~7 s before the card binds (Fix B, in progress)

`audioreach_send_cmd_sync()` (the shared GPR command helper) waits `wait_event_timeout(..., 5 * HZ)` and prints
`CMD timeout for [%x] opcode` when it expires. `q6apm_get_apm_state()` sends `APM_CMD_GET_SPF_STATE` and does not
check the result. On this boot that one command blocked from ~3.4 s to **8.413 s**, and *everything* else
(DAIS GPR service, `rx_macro`, SoundWire, WCD937x, the card) only ran after it — the whole audio stack is behind that
single 5 s wait. The card then binds at ~9.0 s.

So two independent delays stack up: ~7 s to bind the card, then the ~20 s the ADSP takes before it pulls audio (the amp
spiral on top of that is what Fix A removed).

`rcS` now `mark`s each audio step, which located the 5 s exactly — the `modprobe … q6apm+q6prm` step alone takes
**5.12 s** (3.06 s → 8.18 s) while every other step is ~0.05 s:

```
rcS: audio: adsp modprobe       0.74 s
rcS: audio: adsp running+sleep  2.99 s     <- the fixed `sleep 2`
rcS: audio: macros+soundwire    3.04 s
rcS: audio: wcd937x             3.06 s
rcS: audio: q6apm+q6prm         8.18 s     <- 5.12 s: audioreach_send_cmd_sync()'s 5 * HZ
rcS: audio: machine             8.19 s
rcS: audio: card                8.84 s
```

The Fix B experiment: drop the fixed `sleep 2`, load the DSP services (`snd-q6apm`/`q6prm`) **first** (right after the
ADSP is up), and see whether the first good write moves earlier. If it does, the timeout was the gate; if it does not,
the ADSP's own ~20 s is the floor and no userspace ordering can beat it.

## What we tried (for the record)

All userspace, before the driver was touched: (a) hold the stream open and retry the write longer → **worse** (all
`-EBUSY`, recovered only after a reopen); (b) hold the splash until a "warm" signal → works but +10–20 s boot,
rejected; (c) reopen-cycling → ~20 s. None move the hardware frontier.

## Ruled out

- **No card / route off**: `snd_pcm_open()` succeeds and the route reads `'on'`.
- **ADSP not up**: it is up at 1.24 s.
- **Missing amp firmware**: present, and the probe loads it (the codec probe runs at card-bind time, after the rootfs).
  (The *RG DS* board's `aw88166: load firmware without stalling boot` patch is a different failure mode.)
- **`aw_dev_check_sram()`**: it logs at `dev_err` and never fired, so the DSP SRAM check passes; the failing step is
  the I2S clock (`check_sysst`).
- **`CONFIG_DYNAMIC_DEBUG`**: not set (Banana's fragment), so dyndbg cannot be used here — hence explicit logging in
  the patch.

## How to measure it

```sh
# the menu's own attempt log, with times since the kernel
grep -a "sound:" /data/banana/logs/banana-ui.log

# the amp, the card, the DSP, and the new per-step marks
dmesg | grep -iE "aw88166|sc8280xp|wcd937x|qcom-apm"
cat /data/banana/boot.txt        # includes the rcS: audio: … marks

# the amps' level
for c in Left\ PCM Right\ PCM; do amixer -c 0 sget "$c"; done
```

Expected after Fix A: `start: the amp locked after N I2S-clock waits` a few tens of ms after the menu first opens the
device, instead of `start failure (-1)` ~12 s later and no sound until ~20–33 s.

## Current state

- The boot animation has **no click** and, on the RPC's single panel, is a single screen.
- The cartridge-insert animation **moved to the game launch** (`themes/pocket.json` and its colour variants).
- The boot warm-up (`Sound::warm()`) still pre-opens the device so the first *menu* cue is not cold.
- **Fix A** is the kernel patch above; **Fix B** needs the `rcS` marks' first results to choose between a shorter /
  retried `APM_CMD_GET_SPF_STATE`, a different module order, or polling the ADSP's readiness.
