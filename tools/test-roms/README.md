# Test ROMs

`ds2dtest.nds`: a Nintendo DS homebrew ROM that animates both 2D engines (text, affine and bitmap layers, sprites,
windows, colour effects, display capture). It is BananaDS's 2D test (`bananads/tests/ds2dtest` in
github.com/JorreFog/SuperDrastic, GPL-3.0, built with its `build.py`: clang and ld.lld, no SDK). `tools/qemu-boot.sh`
runs it in BananaDS from the menu, to see the emulator put both screens on both panels.
