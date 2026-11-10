# rgds-sim A/B outputs (rgds-sim/ab.sh: A and B runs of one binary with a toggle off/on, or base vs head), MEASURED. Model v1 until 17:00 UTC, v2 after (write streaming).
# dev-L5-base-vs-b6d-ab.txt, dev-L5-pipeline-ab.txt, dev-L5-quarters-ab.txt (the quarters v1 loss), dev-L5-zip-ab.txt, dev1x-L5-share-ab.txt: 16:xx, b5d/b6d (f65f846), dev mode
# st-membench-unroll-ab.txt, st-*-base-vs-b6d-ab.txt, st-L10-sort-ab.txt, st-geomtest-sort-ab.txt: st mode (1x), the comp_unroll and the dropped geom_sort
# st2x-mwin-winneon-ab.txt, st1x-mwin-winneon-ab.txt, dev-mwin-winneon-ab.txt, st1x-ds2d-winneon-ab.txt: render.2d_win_neon off -> on, b13d (4d1ee7c), membench-win / ds2dtest (v2)
# st2x-*-steps-ab.txt, st1x-membench-steps-ab.txt: render.2d_steps_cache off -> on, b15d (dc50d19), st mode (v2) -- copied at the session's end if they finished
# ../jobstride-ab/: render.2d_job_stride inline -> compact, b12d (dc02f53), dev mode (v2)
