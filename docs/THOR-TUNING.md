# Tuning BananaOS for the AYN Thor

What BananaOS's own image (`make thor`) does to run the Snapdragon 8 Gen 2 well, set against what ROCKNIX does on the
same chip, and how to check each choice on a Thor.

**Nothing here has been measured on a Thor.** Where a number is given it was measured on a PC and says so. Everything
else is an expected effect, with the reason, and the command that will settle it on the handheld. Paths under
"ROCKNIX" are in ROCKNIX's `distribution` at `6cf94dba` (the pin in `buildroot/board/thor/ROCKNIX`).

Where it lives:

- Runtime settings: `system/rootfs/etc/banana/hw/thor.conf` (the values) and `usr/libexec/banana/tune` (the code,
  run from `rcS`; tested by `system/tests/tune-test.sh` against a made-up `/sys` and `/proc`). A board whose profile
  sets none of the keys, like the RG DS's, runs none of it.
- Build settings: `buildroot/configs/banana_thor_defconfig`, `buildroot/external.mk`, `buildroot/board/thor/`.

## The changes

### Build

| What | ROCKNIX | BananaOS | Same or different, and why | Measure on a Thor |
|---|---|---|---|---|
| CPU target | `-mcpu=cortex-x3+...+nosve+nosve2` (`devices/SM8550/options`) | `-mcpu=cortex-a710+nosve+nosve2` for every package (`external.mk`, `GCC_TARGET_CPU`); BananaDS and BananaFront follow it instead of `cortex-a55` | **A fix.** The defconfig's `BR2_cortex_a710` made Buildroot's compiler wrapper pass `-mcpu=cortex-a710`, which lets GCC use SVE and SVE2. The 8 Gen 2 has SVE turned off (ROCKNIX and Box64's `SD8G2` target both build with `+nosve`). Measured on a PC with the Bootlin GCC 14.3 the image uses: a two-loop C file at `-O2` gets 16 SVE instructions with `cortex-a710`, 0 with `+nosve+nosve2`. Such a binary would die with SIGILL on the Thor. A710 tuning, not X3: Buildroot's own choice, and the middle cores are four of the eight | `grep -c -w sve /proc/cpuinfo` (expect 0). On the build: `output/thor/host/bin/aarch64-linux-objdump -d output/thor/target/usr/bin/retroarch \| grep -c -E 'whilelo\|ptrue\|\bz[0-9]+\.'` (expect 0) |
| Root filesystem | squashfs (ROCKNIX's own default) | squashfs **zstd**, mksquashfs's level 15, 128 KB blocks (was LZ4HC) | Measured on a PC, on 90 MB of the Thor's root (glibc, Box64): LZ4HC 33.7 MB, zstd 20.9 MB (-38%), zstd-19 20.6 MB at three times the build time. Expected: the card (60-90 MB/s) is the limit, not the cores, so less to read is faster; a smaller update download and more room in the 3 GB slot | `sync; echo 3 > /proc/sys/vm/drop_caches; time tar -cf /dev/null /usr/lib /usr/bin` on an LZ4 and a zstd image; `/run/banana/boot-times` ("rcS: bananad ready") on both |
| Optimisation level, LTO | `-O2`, `-Ofast` for some emulators | `-O2` (unchanged); no Buildroot-wide LTO | Same. Buildroot's LTO only reaches the few packages that support it, and each new LTO build is a new risk; BananaDS already builds with LTO | - |
| Turnip | ROCKNIX's autotune patches 0013-0017 and `tu_autotune_flags=adapt` (`packages/graphics/mesa/patches`, `config/01-rocknix-turnip.conf`) | The same five patches (`buildroot/patches/mesa3d`, Mesa 26.2.6) and the same drirc option (`board/thor/turnip-drirc.conf`, installed by `external.mk`) | Same. Each render pass's GMEM/sysmem choice is corrected by its measured time, with a table of costs that has an A740 entry. Checked on a PC: the patches apply to 26.2.6 with no fuzz and Turnip compiles with them (x86 host build of `libvulkan_freedreno.so`); never run | `TU_DEBUG=startup` in a Vulkan game's environment logs the autotune's mode. A/B: the same scene's fps with `TU_AUTOTUNE_FLAGS=` (empty: off) and without |
| OpenGL driver | `MESA_LOADER_DRIVER_OVERRIDE=zink` (`quirks/platforms/SM8550/095-force_zink`, a link to SM8750's) | freedreno (unchanged), no zink | Different. The menu, RetroArch and the DS emulators use GLES, which freedreno does natively; zink adds a translation layer to Vulkan. ROCKNIX forces it for its desktop-GL emulators, which BananaOS doesn't ship | fps of a RetroArch GL core with `MESA_LOADER_DRIVER_OVERRIDE=zink` and without |
| Serial login | none | the RG DS's `ttyS2` getty removed from the Thor's `inittab` (`board/rgds/post-build.sh`) | **A fix.** The Thor has no port there; busybox init restarts a failing respawn entry every second, for good, with a log line each time (its source: "BusyBox init does not stop processes from respawning out of control") | `grep -c ttyS2 /etc/inittab` (expect 0); `ps \| grep -c '[g]etty.*ttyS2'` (expect 0: USB debugging keeps a getty of its own on `ttyGS0`); `grep -c ttyS2 /run/banana/messages` |

### Boot and kernel command line

| What | ROCKNIX | BananaOS | Same or different, and why | Measure on a Thor |
|---|---|---|---|---|
| Command line | `quiet rootwait console=tty0 allow_mismatched_32bit_el0 fw_devlink.strict=1 pcie_ports=compat irqaffinity=0-2 cgroup.memory=nokmem,nosocket nosoftlockup usbcore.interrupt_interval_override=045e:028e:2 gpt` (`devices/SM8550/options`) | ROCKNIX's without `console=tty0`, plus `loglevel=3 rcupdate.rcu_expedited=1 cpufreq.default_governor=performance driver_async_probe=edt_ft5x06` (as before), and now `cgroup_disable=memory` instead of `cgroup.memory=nokmem,nosocket`, plus the Xbox 360 pad's 2 ms polling and `gpt` (`board/thor/extlinux.conf.in`, `banana.conf.in`) | No `console=tty0`: this kernel has no virtual terminals (`CONFIG_VT` off), so it named a console that doesn't exist and made the kernel ignore the device tree's `stdout-path`; without it the console is the debug UART (`serial0`, 115200n8). `cgroup_disable=memory` goes further than ROCKNIX: BananaOS mounts one cgroup2 hierarchy, only for the Home button's freezer (`rcS`), and uses no memory controller (no systemd), so its accounting of every page is pure cost; the kernel's documentation calls it the one `cgroup_disable` worth using. ROCKNIX needs memcg for its systemd slices. `nosoftlockup` is left out: the kernel config has no soft-lockup detector | `cat /sys/fs/cgroup/cgroup.controllers` (no `memory` in it; this kernel has no v1 memory controller, so `/proc/cgroups` doesn't list it either way), or `dmesg \| grep 'Disabling memory'`. A fixed RetroArch run, vsync off: `time retroarch -L <core> <rom> --max-frames=6000` with and without |
| Boot order | systemd units | `rcS` as before: only the menu's needs before it, the rest after (`tune boot` is a few writes before the menu; zram, OOM protection and Wi-Fi after it) | Unchanged; already parallel | `cat /run/banana/boot-times`, `dmesg \| head` |
| Initramfs | none for SM8550 | none | Same | - |

### Memory

| What | ROCKNIX | BananaOS | Same or different, and why | Measure on a Thor |
|---|---|---|---|---|
| zram size | "Auto": as big as the RAM, `mem_limit` half the RAM (`packages/rocknix/sources/scripts/rocknix-memory-manager`, `devices/SM8550/options`) | Half the RAM (Base 8 GB: 4 GB, Max 16 GB: 8 GB), `mem_limit` a quarter (`HW_ZRAM=50%`, `HW_ZRAM_LIMIT=25%`; was a fixed 2 GB) | Sized by the RAM like ROCKNIX, smaller. Expected: a game's working set should stay in RAM; zram is for idle pages (the menu, a paused game, Wine's idle parts), and LZ4 pages take about 40% of their size, so 4 GB of zram is ~1.6 GB of RAM at most | `cat /sys/block/zram0/mm_stat` and `free -m` during the heaviest game (PS2, a PC game); `grep -E 'pswpin\|pswpout' /proc/vmstat` |
| zram algorithms | LZ4, zstd recompression of idle pages every 10 min (`rocknix-memory-manager`, `system.d/rocknix-memory-recompress.timer`) | The same, with the kernel's advised `priority=1` instead of the algorithm's name: `type=idle priority=1`, no threshold (`tune recompress`, a loop in the background) | Same scheme. No size threshold: the kernel also throws away every result that isn't under the threshold and marks the page incompressible, never to be tried again | `cat /sys/block/zram0/mm_stat` (2nd field) before and after `/usr/libexec/banana/tune recompress`, 10 minutes into a session |
| zswap | turned off | turned off (it is also not built) | Same | `cat /sys/module/zswap/parameters/enabled` (if present) |
| swappiness, page-cluster | 100, 0 | 100, 0 (as before) | Same | `sysctl vm.swappiness vm.page-cluster` |
| Reclaim watermarks | `watermark_scale_factor=100` above 2 GB (`rocknix-memory-manager`), `min_free_kbytes=16384` (`packages/sysutils/busybox/sysctl.d/memory.conf`) | `vm.watermark_scale_factor=100` (`HW_SYSCTL`); `min_free_kbytes` the kernel's own | Same factor: kswapd wakes with 1% of the RAM free, not 0.1%, so a game's allocations don't reclaim memory themselves. `min_free_kbytes`: the kernel already computes ~11-16 MB for 8-16 GB | `grep -E 'allocstall\|pgscan_direct\|pgscan_kswapd' /proc/vmstat` before and after a game (allocstall should stay near 0) |
| Dentry and inode caches | `vfs_cache_pressure=100` (the default) | 50 | Different. The menu scans the card's game folders on exFAT; with RAM to spare, keeping their dentries and inodes costs little and saves the card's reads | `time` of the menu's library refresh after an idle hour; `grep -E '^(dentry\|exfat_inode_cache) ' /proc/slabinfo` |
| Dirty data | `dirty_ratio=10`, `dirty_background_ratio=5` | `vm.dirty_bytes=128 MB`, `vm.dirty_background_bytes=32 MB`, written out after 2 s (as before) | Different. Ratios scale with the RAM, the card doesn't: 10% of 16 GB is 1.6 GB of unsaved data, which a 20-60 MB/s card takes 30-80 s to write, and a save's `fsync` waits behind all of it. Bytes keep the queue to a few seconds | During a large copy to the card: `time dd if=/dev/zero of=/data/banana/t bs=4k count=1 conv=fsync` (a save's cost); `grep -E 'Dirty\|Writeback' /proc/meminfo` |
| Overcommit | `overcommit_memory=1` on SM8xxx (`rocknix-memory-manager`: Box64 and Wine) | 1 | Same, for the same reason | a PC game's start; `sysctl vm.overcommit_memory` |
| MGLRU | on (kernel default) | on, and `min_ttl_ms=1000` (`HW_MGLRU_TTL_MS`) | Further than ROCKNIX: the pages used in the last second are never evicted, so a game that runs out of memory is ended by the OOM killer instead of thrashing the handheld into a freeze (the setting ChromeOS uses) | Run a memory hog past the RAM and zram: `dmesg \| grep -i 'out of memory'` within seconds instead of a frozen screen |
| Huge pages | kernel default (`madvise`); `compaction_proactiveness=40` on "high performance" SoCs | `madvise` (unchanged); defrag `defer` (`HW_THP_DEFRAG`); proactiveness the default 20 | Different. `defer` never compacts inside a page fault (a stall in the game's frame); kcompactd does it in the background. No extra proactive compaction: it runs on a core while a game plays | `grep -E 'thp_fault_alloc\|thp_fault_fallback\|compact_stall' /proc/vmstat` (compact_stall should not grow during play) |
| malloc and huge pages | - | `GLIBC_TUNABLES=glibc.malloc.hugetlb=1` for every program (`HW_ENV`) | New. glibc asks for huge pages for its heaps, where large heaps (Box64, Dolphin, PPSSPP) see fewer TLB misses. Expected cost: a few MB more resident in small programs | `grep AnonHugePages /proc/$(pidof retroarch)/smaps_rollup`; fps with and without |
| KSM | on, scanning every 3 s on 8 GB+ (`rocknix-memory-manager`) | off (kernel default) | Different. KSM only merges memory a program marks mergeable, which none of BananaOS's emulators do; ksmd would wake every 3 s for nothing | - |
| OOM choice | PipeWire and WirePlumber `OOMScoreAdjust=-500` (`packages/audio/*/system.d/*.service.d/10-rocknix.conf`) | `-900` for udevd, D-Bus, bananad, syslogd (`tune protect`, `HW_OOM_PROTECT`) and PipeWire and WirePlumber (`sound-server`); the menu stays at 0 | More than ROCKNIX. The menu isn't lowered: the games it starts would inherit the value, and the kernel already picks the largest process, the game. No userspace killer: MGLRU's `min_ttl_ms` makes the kernel's own come in time | `for p in $(pidof udevd dbus-daemon bananad syslogd pipewire wireplumber banana-ui); do echo $p $(cat /proc/$p/oom_score_adj); done` |
| Shader caches | each program's own | `XDG_CACHE_HOME=/data/banana/cache` for programs without their own (`HW_ENV`) | New. init's `HOME` is `/`, read-only: SuperDrastic, BananaFront and anything else started without a HOME had no Mesa shader cache and compiled every shader at every start. Launchers that set their own (RetroArch, Dolphin, PPSSPP...) keep theirs | `ls /data/banana/cache/mesa_shader_cache*` after a SuperDrastic game; the second start's stutter |

### CPU, GPU, thermal, idle

| What | ROCKNIX | BananaOS | Same or different, and why | Measure on a Thor |
|---|---|---|---|---|
| Clusters | policy0, 3, 7 (`quirks/platforms/SM8550/010-governors`) | `banana-perf`'s per-cluster profiles (policy0: 3x A510, policy3: 2x A715 + 2x A710, policy7: X3; checked against the kernel's `sm8550.dtsi`) | Same clusters. BananaOS's profiles are per system and per game | `cat /sys/devices/system/cpu/cpufreq/policy*/{related_cpus,scaling_governor,scaling_max_freq}` in a game |
| Governor | ondemand or performance per game (`autostart/008-perfmode`, `profile.d/099-freqfunctions`) | performance until the menu is up, then schedutil with the profile's caps (unchanged) | Same idea, schedutil where ROCKNIX uses ondemand: it follows the scheduler's own load per task | - |
| Big cores for games | `FAST_CORES="taskset -c 3-7"` for some emulators (`quirks/platforms/SM8550/040-affinity`) | none: the kernel's energy-aware scheduling (energy model on) | Not copied. Unmeasured either way; pinning takes the A510s from an emulator's helper threads. busybox here has no `taskset` | `top -H` in a game: which CPU its main thread is on (`ps -eLo tid,psr,comm`) |
| IRQs | `irqaffinity=0-2` | the same | Same | `cat /proc/interrupts` |
| Idle states | cpu0's state1 off, "seems to cause GMU issues" (`quirks/platforms/SM8550/096-cpuidle`) | the same (`HW_CPUIDLE_OFF`), lifted for the length of a sleep (`etc/banana/sleep.d/80-cpuidle`, `tune sleep pre\|post`) | Copied as a stability quirk. Its cost is cpu0's deepest own idle state. A state kept off is skipped in a suspend-to-idle too, and cpu0 would then keep its cluster and the SoC out of their sleep states, so the state is allowed while the handheld sleeps and off again after. ROCKNIX has no need: it doesn't sleep on SM8550 | `cat /sys/devices/system/cpu/cpu0/cpuidle/state1/{name,disable}`; `dmesg \| grep -i gmu`; menu-idle `current_now` with it on and off |
| Fan | `fancontrol`: an `awk` and a `sleep` every 3 s (`quirks/platforms/SM8550/bin/fancontrol`) | `handheld`'s curve (ROCKNIX's) by the mean of the `cpu*` and `gpuss*` thermal zones, as ROCKNIX's `fancontrol` takes it, read with the shell's own `read` | The loop is the fan's only control: the device tree has no cooling-maps for `pwm_fan`. Lighter than before and than ROCKNIX: measured on a PC against a made-up `/sys` with 16 CPU zones, the loop started 52 programs per look (every 3 s), now only its `sleep` | `a=$(cut -d' ' -f5 /proc/loadavg); sleep 60; cut -d' ' -f5 /proc/loadavg` at menu idle (processes made per minute) |
| GPU top clock | `gpu_overclock` 680 MHz / 1 GHz | 680 MHz, no boost | Different, for now. Linux 7.2's OPP table for the Adreno 740 ends at 680 MHz and the AYN device tree adds no 1 GHz entry, so `handheld caps` doesn't offer `gpuboost` on this image and Settings hides the row. `handheld gpu` raises the clock only where the kernel's table has it | `cat /sys/class/devfreq/3d00000.gpu/{cur_freq,max_freq,available_frequencies}` |

### Storage

| What | ROCKNIX | BananaOS | Same or different, and why | Measure on a Thor |
|---|---|---|---|---|
| I/O scheduler, read-ahead | nothing set | the card: `mq-deadline`, 1 MB read-ahead, at boot and for a card put in later (`HW_IO`, `etc/udev/rules.d/60-banana-io.rules`) | New. `mq-deadline` lets reads (a game loading) go ahead of a save's writes; 1 MB read-ahead (the kernel's 128 KB) for disc images streamed from the card. The internal UFS isn't used | `cat /sys/block/mmcblk0/queue/{scheduler,read_ahead_kb}`; `sync; echo 3 > /proc/sys/vm/drop_caches; time cat <a PS2 ISO> > /dev/null` with 128 and 1024 |
| Mount options | - | exFAT `noatime` (unchanged) | - | - |
| fstrim | a setting (`packages/sysutils/autostart/sources/daemons/005-fstrim`) | none | Not copied: the data partition is exFAT, unmeasured whether this kernel trims it | `fstrim -v /data` |

### Sound

| What | ROCKNIX | BananaOS | Same or different, and why | Measure on a Thor |
|---|---|---|---|---|
| Quantum | PipeWire's defaults on SM8550 (SM6115 gets a 1024-frame period, `devices/SM6115/.../90-sm6115-s16le.conf`) | PipeWire's defaults, and a floor: `default.clock.min-quantum = 256` (`board/thor/pipewire/50-banana-thor.conf`) | The default quantum stays (21 ms when nothing asks for less); the graph still follows the program that asks for the least. The floor (5.3 ms instead of 0.7) keeps a program from driving the DSP's stream into underruns | `pw-top` while a game plays: QUANT and the ERR column (underruns) |
| Suspend on idle | 5 s on SM6115 (`91-suspend-on-idle.conf`), defaults elsewhere | WirePlumber's default (5 s) | Same | `wpctl status` a few seconds after a sound (the sink "suspended") |
| Real-time priority | rtkit | PipeWire's own module-rt as root (no rtkit) | Root needs no rtkit; the module sets its threads' priority directly | `ps -eLo pid,tid,cls,rtprio,comm \| grep -i -E 'data-loop\|pipewire'` (expect FF) |
| RetroArch's audio | `audio_driver = pulse` (`quirks/devices/AYN Thor/050-game-configs`) | ALSA's `default`, which bananad points at PipeWire (unchanged) | No pulse server in BananaOS; the PipeWire ALSA plugin is the same path with one layer less | RetroArch's audio latency setting at 32 ms with no underruns in `pw-top` |

### Sleep

| What | ROCKNIX | BananaOS | Same or different, and why | Measure on a Thor |
|---|---|---|---|---|
| Sleep | off on SM8550, power key ignored (`quirks/platforms/SM8550/030-suspend_mode`) | bananad's sleep with its fallbacks (unchanged) | BananaOS tries; `docs/THOR-PORT.md` M3 has the open questions | `cat /sys/power/mem_sleep`; `/data/banana/logs/sleep.log` |
| Lights and fan | LEDs off before, back after (`quirks/platforms/SM8550/sleep.d/pre/001-led`, `post/001-led`: lit, they keep it from sleeping) | the same, and the fan stopped (`etc/banana/sleep.d/30-handheld`, `handheld sleep`) | Same for the lights; the fan's loop would otherwise stay frozen at its last speed. Asleep, nothing in the kernel drives the fan (no thermal zone is mapped to it): only the SoC's passive and critical trips remain | rings dark with the lid shut; the battery's drop overnight in `sleep.log` |
| Governors around sleep | powersave before, restored after (`sleep.d/pre/002-freq`) | none | Not copied: in a real or s2idle sleep the CPUs are frozen, and bananad's dark idle already holds the clocks low | - |
| Async suspend | off for every device (`packages/linux/patches/default/9901-pm-disable-async-suspend-resume-by-default.patch`) | on, with bananad falling back to one device at a time after a failed suspend (`fast_resume`) | BananaOS's is faster when it works and as safe when it doesn't | `sleep_debug = yes` in `banana/bananad.conf`, then `logs/sleep-devices.log` |
| Modules unloaded | none listed for SM8550 (`packages/sysutils/sleep/sources/modules.bad`) | none (`HW_WIFI_MODULE` empty) | Same until the Thor shows otherwise | Wi-Fi up after a wake |

## Considered and left out

- **uclamp** (a minimum clock for the game's threads): the kernel config has no `UCLAMP_TASK`, and setting it per game
  needs the menu's launcher. Worth trying with the kernel option and `banana-perf`.
- **schedutil's `rate_limit_us`**: 1 ms by default on `qcom-cpufreq-hw` (it gives no transition latency). A lower
  value ramps the clocks faster at a frame's start; measure frame times first.
- **`HZ=1000`** (the config has 250): finer scheduling at more timer cost; needs a frame-time measurement.
- **PCIe ASPM L1 substates** for the Wi-Fi (`pcie_aspm.policy=powersupersave`): could save idle power, but ath12k has a
  history of ASPM trouble. Measure `current_now` at idle and Wi-Fi stability first.
- **`mitigations=off`**: a few percent in syscall-heavy code, at a security cost for a device that runs downloaded
  Windows games. Not taken.
- **RetroArch on Vulkan, and its PipeWire driver**: RetroArch is built without both; Vulkan on KMS through the
  menu's DRM lease is untried.
- **sway**: tearing needs async page flips, which the msm driver doesn't offer; the panels have no VRR; direct scan-out
  needs a buffer already turned for the portrait panels (ROCKNIX's Mesa pre-rotation patches 0004-0012 are the way).
- **WirePlumber features** (video, MIDI): Buildroot's PipeWire is built without the cameras' plugins and the kernel
  without the ALSA sequencer, so there is nothing to turn off.

## Before trusting any of it

1. Build `make thor` **from clean**: the CPU change (`GCC_TARGET_CPU`) is not something Buildroot tracks, so an
   existing `output/thor` keeps its old compiler wrapper and packages until `make clean`.
2. Boot, then `cat /run/banana/boot-times`, `sysctl -a 2>/dev/null | grep -E 'vm\.(watermark|dirty|overcommit|vfs)'`,
   `cat /sys/block/zram0/{disksize,mem_limit,comp_algorithm,recomp_algorithm}`, `swapon -s`.
3. Then the measurements in the tables, each against the value before it, on the same game and card.
