# A card laid out as a BananaOS handheld's /data really is after some play, for the save sync's tests (sourced, POSIX
# sh). tests/sync.sh keeps its own simpler card: it is the legacy protocol's test.
#   realcard <dir>                       the folders, settings.conf with each system's core, RetroArch's config as
#                                        run-retroarch and RetroArch leave it, a ROM per system
#   realsave <dir> <system> <stem> [<bytes>]   a save with data in it where the system's emulator writes it;
#                                        prints the path
#   blank <file> <bytes> <hex byte>      a save as a core writes it before the game has saved anything
#   dsv <raw> <out>                      a .dsv as DraStic writes it: the raw save and DeSmuME's 122-byte footer
#   core_table                           what each shipped core is called and writes (below), tab-separated
#   fakecore <so>                        a core installed there, as the save sync looks for one (only that it is)
# The helpers' own variables start with _, so a test that sources this keeps its names. What the cores do was
# measured in a chroot of the built image (RetroArch 1.22.2) and read from each core's source at the commit its
# package pins.

# core, library_name (retro_get_system_info: the folder under saves/ a card from before 0.5.3 kept its saves in, and
# under states/ its states still are in), systems, the bytes of a fresh save, its sizes (BananaNES, the image's NES
# core, a fork of FCEUmm, says it is FCEUmm: its saves are FCEUmm's)
core_table() {
    cat <<'EOF'
gambatte	Gambatte	gb gbc	ff	0 8192 32768 131072
gpsp	gpSP	gba	ff	131072
mgba	mGBA	gba	ff	512 8192 32768 65536 131072
fceumm	FCEUmm	nes	00	128-32768
banananes	FCEUmm	nes	00	128-32768
snes9x	Snes9x	snes	60	1024-131072
picodrive	PicoDrive	megadrive	00	none until written
pcsx_rearmed	PCSX-ReARMed	psx	card	131072
mupen64plus_next	Mupen64Plus-Next	n64	ff	296960
fake08	fake-08	pico8	-	-
EOF
}

# system -> the core systems.conf runs it with
default_core() {
    case $1 in
        gb|gbc) echo gambatte ;; gba) echo gpsp ;; nes) echo banananes ;; snes) echo snes9x ;; megadrive) echo picodrive ;;
        psx) echo pcsx_rearmed ;; n64) echo mupen64plus_next ;; *) return 1 ;;
    esac
}
library_name() { core_table | awk -F'\t' -v c="$1" '$1 == c { print $2 }'; }

realcard() {
    _c=$1
    mkdir -p "$_c/banana" "$_c/run" "$_c/states" "$_c/retroarch" "$_c/drastic/backup" "$_c/drastic/savestates" "$_c/saves/nds" "$_c/saves/gba"
    for _s in gb gbc gba nes snes megadrive psx n64 nds; do mkdir -p "$_c/roms/$_s"; done
    {
        echo "homesync_auto = off"
        for _s in gb gbc gba nes snes megadrive psx n64; do echo "core.$_s = $(default_core $_s)"; done
    } > "$_c/banana/settings.conf"
    # (savefile_directory from the image's template; sort_savefiles_enable as run-retroarch sets it at every start)
    printf 'savefile_directory = "/data/saves"\nsavestate_directory = "/data/states"\nsort_savefiles_enable = "false"\n' > "$_c/retroarch/retroarch.cfg"
    printf 'gb rom' > "$_c/roms/gb/Pocket Tale (USA).gb"
    printf 'gba rom' > "$_c/roms/gba/Banana Run (USA).gba"
    printf 'snes rom' > "$_c/roms/snes/Peel Kart (USA).sfc"
    printf 'psx rom' > "$_c/roms/psx/Disc Game (USA).m3u"
    printf 'nds rom' > "$_c/roms/nds/Peel Quest (USA).nds"
}

# a save with data in it at the path the system's emulator writes (GBA: the one folder every GBA core shares,
# run-retroarch's; DS: BananaDS's .sav, which nds-saves keeps in step with DraStic's .dsv; any other: the flat saves/)
realsave() {
    _c=$1 _s=$2 _n=$3 _b=${4:-8192}
    case $_s in
        gba) _p="$_c/saves/gba/$_n.srm" ;;
        nds) _p="$_c/saves/nds/$_n.sav" ;;
        *) _p="$_c/saves/$_n.srm" ;;
    esac
    head -c "$_b" /dev/urandom > "$_p"
    echo "$_p"
}

fakecore() {
    mkdir -p "$(dirname "$1")" && : > "$1"
}

blank() {
    python3 -c 'import sys; open(sys.argv[1], "wb").write(bytes([int(sys.argv[3], 16)]) * int(sys.argv[2]))' "$1" "$2" "$3"
}

# DeSmuME's footer: an 82-byte line saying where to cut, then six u32 (actual size, padded size, type, address size,
# memory size, version 0), then the 16-byte cookie
dsv() {
    python3 - "$1" "$2" <<'EOF'
import struct, sys
raw = open(sys.argv[1], "rb").read()
snip = b"|<--Snip above here to create a raw sav by excluding this DeSmuME savedata footer:"
assert len(snip) == 82
open(sys.argv[2], "wb").write(raw + snip + struct.pack("<6I", len(raw), len(raw), 0, 2, len(raw), 0) + b"|-DESMUME SAVE-|")
EOF
}
