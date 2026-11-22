# The PGO profile for the integrated tree

`prof-int-gcc14.3-4de7beb.tar.gz` is the profile core trained on SuperDrastic `perf/integrated` (the tree of
`patches/integrated/`, code identical to 4de7beb). It holds 127 `.gcda` files from GCC 14.3.0, the Bootlin
toolchain BananaLinux's Buildroot 2026.02.3 uses. It came from 48 test-ROM runs under qemu-user with 0 failures
(`evidence/core/train-int.txt`). Use it:

```sh
tar -xzf prof-int-gcc14.3-4de7beb.tar.gz          # -> prof-int/
cmake ... -DBANANADS_PGO=$PWD/prof-int             # or BANANADS_CONF_OPTS += -DBANANADS_PGO=<dir> in bananads.mk
```

- **It only fits that tree with that GCC.** A later source change makes the changed functions' profiles stale. GCC
  then warns and builds those functions without a profile; the rest keep theirs (findings/core.md, "stale profile").
- **Three files carry a cloud path.** The `bananads-net` files embed `/home/user/perf/core/build-intgen`, so they won't
  match elsewhere. They are network code, not hot.
- **Regenerate rather than reuse when in doubt.** `bananads/tools/perf/pgo/train.sh` does it, with an instrumented
  build `-DBANANADS_PGO_GENERATE=ON`.
- **A game profile from the handheld is the better input.** Collect one and merge it with this one with the
  toolchain's `gcov-tool merge` (findings/core.md, "How PGO would ship").
- **The device decides first.** Use it only after the device A/B confirms PGO (DEVICE-TEST.md 5).
