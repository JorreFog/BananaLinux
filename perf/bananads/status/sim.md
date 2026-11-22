# sim: status — FINAL 17:50 UTC
- rgds-sim/ final: model v2 default (L1D write streaming), v3 opt-in (SIMARGS=prfm=1, timed software prefetch), v1 = ws=0.
  README (model, versions, parameters with sources/assumptions, calibration, limitations, running, re-calibration),
  params.json (v2), params-fit.json (sensitivity), calibrate.py + calib/ (device table, device-calib.sh), regions.py,
  tools/exact.sh, tools/set2d.sh.
- LEDGER final: L1-L3, L5-L7, L10, L11 VERIFIED; L9 REFUTED (layout lottery); L4 superseded; L8, L12, L13, L14 PENDING;
  L15 = the combined measurement.
- Integrated series: perf/integrated @ 4de7beb in /home/user/perf/sim/v-int (repo /home/user/jorrefog/superdrastic);
  the coordinator exported it to patches/integrated/. My own BananaDS commits: none (no patches/sim).
- Evidence: evidence/sim/*.txt (calibration-v1, determinism-v1, where-the-time-goes-v1, v2-2d-thread, verify-L*, verify-jit2d,
  combined-int1, combined-int2, align-integrated-L5).
- #3 done (17:53): perf/integrated with core's retrained prof-int: L5 1x st -5.8%, ds2dtest st 2x -27.4% (evidence/sim/combined-int3.txt).
- Next (for whoever continues): calibrate on the device (rgds-sim/calib/device-calib.sh), then re-run the combined set.
