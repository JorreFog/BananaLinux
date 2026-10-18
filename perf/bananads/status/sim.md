# sim: status (overwritten by sim only) — 15:19 UTC
- Phase 0 done: qemu-count v0 works (INFO on board, evidence/sim/v0-base-counts.txt).
- Phase 1 in progress: rgds-sim v1 plugin written (rgds-sim/rgdssim.c, report.py, run.sh); builds to rgds-sim/build/.
  Works on cpubench-thumb (37 s). Calibration: e67e3ef tree = 4536791 (CI merge of PR #9 into an unmoved main), worktree
  /home/user/perf/sim/e67; kit binaries built as package-device.sh did (Release, no LTO, no -mcpu, SDL off) in
  /home/user/perf/sim/build-e67-{banana,melonds}; kit ROMs in /home/user/perf/sim/roms-e67. Calibration runs:
  /home/user/perf/sim/calib/run-calib.sh -> /home/user/perf/sim/calib/runs/.
- Next: calibrate.py (fit base_scale, l3_cyc, dram_ns, mispredict_cyc), ab.sh, README, INFO post.
