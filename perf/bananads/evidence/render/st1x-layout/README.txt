# the 1x L1I question: dsstress-L5 at render.scale=1, st mode (deterministic: everything on the emulator's thread, no Gengis threads, no 2D thread), rgds-sim v2, frames 100-300, device builds. MEASURED.
# build-dev = base 00c34c7; b10d = eee302e (11 commits); b12d = dc02f53 (+ render.2d_job_stride: no code that runs at 1x without the 2D thread); b13d = 4d1ee7c (+ render.2d_win_neon: at 1x only ApplyWindow/DrawBG_3D on windowed lines, none on L5)
# cmd: OUT=... rgds-sim/run.sh <bin> /home/user/perf/roms/dsstress-L5.nds st1x-<b>-L5 --mode st --frames 100:300 --set render.scale=1
st1x-base:   per frame: L1I miss 10.8k (0.79% of fetch blocks; to DRAM 0.5k)  L1D | main thread: 5471.9k insns, 6.887 ms@1104, 4.073 ms@1992
st1x-b10d:   per frame: L1I miss 13.8k (1.01% of fetch blocks; to DRAM 0.5k)  L1D | main thread: 5472.9k insns, 6.938 ms@1104, 4.101 ms@1992
st1x-b12d:   per frame: L1I miss 10.9k (0.80% of fetch blocks; to DRAM 0.5k)  L1D | main thread: 5469.3k insns, 6.837 ms@1104, 4.045 ms@1992
st1x-b13d:   per frame: L1I miss 12.3k (0.90% of fetch blocks; to DRAM 0.5k)  L1D | main thread: 5469.4k insns, 6.885 ms@1104, 4.071 ms@1992
