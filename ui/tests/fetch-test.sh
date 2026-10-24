#!/bin/sh
# banana-fetch's test, without a network: archives (ui/tests/archives: 7z and RAR ones, and zips made here), the
# session the Downloads pages talk to and the job runner, against "dir" servers (folders standing in for a NAS,
# BANANA_FETCH_TEST=1; remote_dir.cpp) that drop the connection or go quiet on purpose. 7-Zip's 7zz (for 7z and RAR)
# is used from PATH; without it those checks are skipped.
#   ui/tests/fetch-test.sh            (builds ui/banana-fetch; SMB=0 when there is no libsmb2)
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$HERE/tests/out/fetch}
pkg-config --exists libsmb2 2>/dev/null && smb=1 || smb=0
make -s -C "$HERE" banana-fetch SMB=$smb >/dev/null
F=$HERE/banana-fetch
A=$HERE/tests/archives
rm -rf "$OUT"; mkdir -p "$OUT"
export BANANA_FETCH_TEST=1 BANANA_DATA="$OUT/data" BANANA_RUN="$OUT/run"
mkdir -p "$BANANA_DATA" "$BANANA_RUN"
pass=0 fail=0
ok() { pass=$((pass + 1)); }
bad() { fail=$((fail + 1)); echo "FAIL: $*"; }
check() { if eval "$2"; then ok; else bad "$1"; fi; }
has7zz=0; command -v 7zz >/dev/null && has7zz=1
[ $has7zz = 1 ] || echo "fetch-test: no 7zz in PATH: the 7z and RAR checks are skipped"

# ---- archives ---------------------------------------------------------------------------------------------------
U=$OUT/unpack
python3 - "$OUT" <<'PY'
import os, stat, struct, sys, zipfile, zlib
w = sys.argv[1]
def patch(path, old, new):
    # a name written as ASCII of the same length, then put back as the bytes wanted (zipfile writes any non-ASCII name
    # as UTF-8 with the flag; these are names from zippers that don't)
    assert len(old) == len(new)
    d = open(path, 'rb').read()
    open(path, 'wb').write(d.replace(old, new))
# Windows' zipper on a Russian system: CP866, no UTF-8 flag (the code page is told for the whole archive); a name whose
# Unicode Path field says what it is; two names that become the same on the card; junk and a link
p = w + '/names.zip'
z = zipfile.ZipFile(p, 'w', zipfile.ZIP_DEFLATED)
z.writestr('AAAA/BBBBBB.gb', b'tetris')
raw = b'\x80\x81\x82\x83.txt'
u = 'Мир.txt'.encode()
zi = zipfile.ZipInfo('CCCC.txt')
zi.extra = struct.pack('<HHBI', 0x7075, 5 + len(u), 1, zlib.crc32(raw)) + u
z.writestr(zi, b'peace')
z.writestr('Pokémon (UTF-8).nds', b'poke')
z.writestr('a?b.txt', b'first')
z.writestr('a_b.txt', b'second')
z.writestr('__MACOSX/._x.nds', b'junk')
z.writestr('.DS_Store', b'junk')
zi = zipfile.ZipInfo('link'); zi.create_system = 3; zi.external_attr = (stat.S_IFLNK | 0o777) << 16
z.writestr(zi, '../../etc/passwd')
z.close()
patch(p, b'AAAA/BBBBBB.gb', 'Игры/Тетрис.gb'.encode('cp866'))
patch(p, b'CCCC.txt', raw)
# elsewhere, CP437 (a byte that isn't one of CP866's letters)
p = w + '/cp437.zip'
z = zipfile.ZipFile(p, 'w', zipfile.ZIP_DEFLATED)
z.writestr('Temp 25XC.txt', b'warm')
z.writestr('CafYY.txt', b'cafe')
z.close()
patch(p, b'Temp 25XC.txt', b'Temp 25\xf8C.txt')
patch(p, b'CafYY.txt', b'Caf\x82s.txt')
z = zipfile.ZipFile(w + '/slip.zip', 'w'); z.writestr('ok.txt', b'ok'); z.writestr('../evil.txt', b'evil'); z.close()
z = zipfile.ZipFile(w + '/abs.zip', 'w'); z.writestr('/etc/evil.txt', b'evil'); z.close()
z = zipfile.ZipFile(w + '/z64.zip', 'w', zipfile.ZIP_STORED, allowZip64=True)
with z.open(zipfile.ZipInfo('big.bin'), 'w', force_zip64=True) as f: f.write(bytes(range(256)) * 400)
z.close()
z = zipfile.ZipFile(w + '/bz2.zip', 'w', zipfile.ZIP_BZIP2); z.writestr('bz.txt', b'bzip2 ' * 100); z.close()
z = zipfile.ZipFile(w + '/game.zip', 'w', zipfile.ZIP_DEFLATED); z.writestr('Zip Kart (Game)/Zip Kart.nds', os.urandom(70000)); z.close()
# a file named as a folder the card has, and a folder named as a file it has
z = zipfile.ZipFile(w + '/clash.zip', 'w'); z.writestr('saves', b'a file'); z.writestr('notes/a.txt', b'a'); z.writestr('other.txt', b'o'); z.close()
PY
$F unpack "$OUT/names.zip" "$U/names" 2>/dev/null
check "CP866 names without the flag" '[ "$(cat "$U/names/Игры/Тетрис.gb")" = tetris ]'
$F unpack "$OUT/cp437.zip" "$U/cp437" 2>/dev/null
check "CP437 names" '[ -f "$U/cp437/Temp 25°C.txt" ] && [ "$(cat "$U/cp437/Cafés.txt")" = cafe ]'
check "a name from its Unicode Path field" '[ "$(cat "$U/names/Мир.txt")" = peace ]'
check "UTF-8 names" '[ -f "$U/names/Pokémon (UTF-8).nds" ]'
check "a name the card refuses, renamed beside one it takes" '[ "$(cat "$U/names/a_b.txt")" = second ] && [ "$(cat "$U/names/a_b (2).txt")" = first ]'
check "junk and links left out" '[ ! -e "$U/names/__MACOSX" ] && [ ! -e "$U/names/.DS_Store" ] && [ ! -e "$U/names/link" ]'
check "no unpacking folder left" '[ ! -e "$U/names/.banana-unpack" ]'
for z in slip abs; do
    r=$($F unpack "$OUT/$z.zip" "$U/$z" 2>&1 || true)
    check "$z.zip refused whole" 'echo "$r" | grep -q "(unsafe)" && [ ! -e "$U/$z/ok.txt" ] && [ ! -e "$OUT/evil.txt" ]'
done
$F unpack "$OUT/z64.zip" "$U/z64" 2>/dev/null
check "zip64" '[ "$(wc -c < "$U/z64/big.bin")" = 102400 ]'
$F unpack "$OUT/game.zip" "$U/game" 2>/dev/null
check "a zip of one folder unwrapped" '[ -f "$U/game/Zip Kart.nds" ]'
r=$(BANANA_FETCH_TEST_FREE=1000 $F unpack "$OUT/game.zip" "$U/full" 2>&1 || true)
check "a full card refused before writing" 'echo "$r" | grep -q "(no_space)" && [ ! -e "$U/full/Zip Kart.nds" ]'
mkdir -p "$U/clash/saves"; echo kept > "$U/clash/saves/game.sav"; echo kept > "$U/clash/notes"
$F unpack "$OUT/clash.zip" "$U/clash" 2>/dev/null
check "a file never takes a folder's place, nor a folder a file's" \
    '[ "$(cat "$U/clash/saves/game.sav")" = kept ] && [ "$(cat "$U/clash/saves (2)")" = "a file" ] && [ "$(cat "$U/clash/notes")" = kept ] && [ -f "$U/clash/notes (2)/a.txt" ]'
if [ $has7zz = 1 ]; then
    for f in lzma.7z lzma2.7z bcj2.7z ppmd.7z solid.7z nonsolid.7z rar4.rar rar5.rar solid.rar; do
        rm -rf "$U/x"; $F unpack "$A/$f" "$U/x" 2>/dev/null || true
        check "$f" '[ "$(wc -c < "$U/x/game.nds")" = 32768 ] && [ "$(cat "$U/x/sub/nested.txt")" = nested ]'
    done
    for f in multivolume.7z.001 multivolume.part1.rar multivolume-old.rar; do
        rm -rf "$U/x"; $F unpack "$A/$f" "$U/x" 2>/dev/null || true
        check "$f (volumes)" '[ "$(wc -c < "$U/x/noise.bin")" = 30000 ]'
    done
    rm -rf "$U/x"; $F unpack "$A/singledir.7z" "$U/x" 2>/dev/null
    check "singledir.7z unwrapped, its junk left out" '[ "$(ls -A "$U/x")" = "Pokemon Platinum (USA).nds" ]'
    rm -rf "$U/x"; $F unpack "$A/cyrillic.rar" "$U/x" 2>/dev/null
    check "cyrillic.rar" '[ -f "$U/x/Тетрис.gb" ]'
    for f in encrypted.7z encrypted-headers.7z encrypted.rar encrypted-headers.rar encrypted-rar4.rar; do
        a=$($F inspect "$A/$f" 2>&1 || true); b=$($F inspect "$A/$f" nope 2>&1 || true)
        check "$f: no password, a wrong one" 'echo "$a" | grep -q "(password)" && echo "$b" | grep -q "(wrong_password)"'
        rm -rf "$U/x"; $F unpack "$A/$f" "$U/x" Secret123 2>/dev/null || true
        check "$f: the right one" '[ "$(wc -c < "$U/x/game.nds")" = 32768 ]'
    done
    # the password goes to 7zz on its stdin, never on its command line (a stand-in 7zz notes its arguments)
    printf '#!/bin/sh\necho "$*" >> "%s/7zz.args"\nexec 7zz "$@"\n' "$OUT" > "$OUT/7zz-args"; chmod +x "$OUT/7zz-args"
    rm -rf "$U/x"; BANANA_7ZZ="$OUT/7zz-args" $F unpack "$A/encrypted-headers.7z" "$U/x" Secret123 2>/dev/null || true
    check "an archive's password not on 7zz's command line" '[ "$(wc -c < "$U/x/game.nds")" = 32768 ] && [ -s "$OUT/7zz.args" ] && ! grep -q Secret123 "$OUT/7zz.args"'
    a=$($F inspect "$A/bigdict.7z" 2>&1)
    check "bigdict.7z's dictionary" 'echo "$a" | grep -q "dictionary 64 MB"'
    r=$(BANANA_FETCH_TEST_MEM=50000000 $F unpack "$A/bigdict.7z" "$U/big" 2>&1 || true)
    check "a dictionary bigger than the memory free" 'echo "$r" | grep -q "(no_memory)"'
    $F unpack "$OUT/bz2.zip" "$U/bz2" 2>/dev/null
    check "a bzip2 zip (7zz)" 'grep -q "^bzip2 bzip2" "$U/bz2/bz.txt"'
    # names the card refuses inside a 7z: each comes out of 7zz under its card name
    mkdir -p "$OUT/odd/dir"; echo a > "$OUT/odd/bad:name.txt"; echo b > "$OUT/odd/dir/what?.txt"; echo c > "$OUT/odd/plain.txt"
    (cd "$OUT/odd" && 7zz a -bso0 -bsp0 ../odd.7z . && 7zz a -bso0 -bsp0 -tzip -pSecret123 ../enc.zip plain.txt)
    $F unpack "$OUT/odd.7z" "$U/odd" 2>/dev/null
    check "7z names renamed for the card" '[ "$(cat "$U/odd/bad_name.txt")" = a ] && [ "$(cat "$U/odd/dir/what_.txt")" = b ] && [ -f "$U/odd/plain.txt" ]'
    a=$($F inspect "$OUT/enc.zip" 2>&1 || true)
    $F unpack "$OUT/enc.zip" "$U/enc" Secret123 2>/dev/null || true
    check "an encrypted zip (7zz)" 'echo "$a" | grep -q "(password)" && [ "$(cat "$U/enc/plain.txt")" = c ]'
    # 1502 files: 7zz's listing (a few hundred KB) read whole, the name the card refuses at its start renamed
    mkdir -p "$OUT/many/dir"; printf early > "$OUT/many/dir/aaa:first.txt"; head -c 5000000 /dev/urandom > "$OUT/many/dir/aab_big.bin"
    i=1; while [ $i -le 1500 ]; do echo $i > "$OUT/many/dir/file_$i.txt"; i=$((i + 1)); done
    (cd "$OUT/many" && 7zz a -bso0 -bsp0 ../many.7z dir)
    a=$($F inspect "$OUT/many.7z" 2>&1 | tail -1)
    $F unpack "$OUT/many.7z" "$U/many" 2>/dev/null
    check "a 7z of 1502 files, listed whole ($a)" \
        'echo "$a" | grep -q "^1502 files, 4.8 MB, the largest 4.8 MB" && [ "$(cat "$U/many/aaa_first.txt")" = early ] && [ "$(ls "$U/many" | wc -l)" = 1502 ]'
fi

# ---- the session ------------------------------------------------------------------------------------------------
S=$OUT/server
mkdir -p "$S/Games/NDS" "$S/Games/GBA" "$S/Empty" "$S/.hidden"
head -c 100000 /dev/urandom > "$S/Games/NDS/Banana Kart.nds"
head -c 3000 /dev/urandom > "$S/Games/NDS/bad:name.nds"
head -c 1000 /dev/urandom > "$S/Games/GBA/Peel.gba"
echo x > "$S/Games/NDS/.secret"
cp "$OUT/game.zip" "$S/Games/game.zip"
tab=$(printf '\t')
ask() { printf '%s\n' "$@" | sed "s/|/$tab/g" | $F serve; }	# requests, | for a tab
id=$(ask "put|name=Test NAS|protocol=dir|host=$S|guest=yes" | sed -n "s/^ok$tab//p")
check "a server added" '[ -n "$id" ] && grep -q "^\[$id\]" "$BANANA_DATA/nas.conf"'
r=$(ask "connect|$id" "list|/" "list|/Games/NDS" "list|/Nowhere")
check "connect" 'echo "$r" | head -1 | grep -q "^ok"'
check "the root listed, folders first" 'echo "$r" | grep "^e" | head -4 | cut -f2,5 | tr "\t\n" ":," | grep -q "^d:.hidden,d:Empty,d:Games,"'
check "a folder listed" 'echo "$r" | grep -q "^e${tab}f${tab}100000${tab}[0-9]*${tab}Banana Kart.nds$"'
check "a folder that isn't there" 'echo "$r" | tail -1 | grep -q "^err${tab}not_found"'
T=$OUT/card/roms/nds
r=$(ask "connect|$id" "walk|/Games|$T|0|nds|d::NDS|f:$(wc -c < "$S/Games/game.zip"):game.zip")
check "walk: the server's NDS folder into the DS folder itself, names for the card, no hidden files" \
    'echo "$r" | grep -q "^f${tab}100000${tab}/Games/NDS/Banana Kart.nds${tab}$T/Banana Kart.nds$" && echo "$r" | grep -q "$T/bad_name.nds$" && ! echo "$r" | grep -q secret && echo "$r" | tail -1 | grep -q "^end${tab}3${tab}"'
r=$(ask "connect|$id" "walk|/Games|$T|1||d::NDS")
check "walk: another folder stays a folder, hidden files when asked" 'echo "$r" | grep -q "$T/NDS/Banana Kart.nds$" && echo "$r" | grep -q "$T/NDS/.secret$"'
r=$(ask "inspect|$OUT/game.zip")
check "inspect" 'echo "$r" | grep -q "^file${tab}Zip Kart (Game)/Zip Kart.nds$" && echo "$r" | tail -1 | grep -q "^end${tab}1${tab}70000${tab}"'
# the connection lost meanwhile (an idle NAS hung up, Wi-Fi slept): a listing, and a walk, connect again once
export BANANA_FETCH_TEST_DROPLIST=1
r=$(ask "connect|$id" "list|/Games")
r2=$(ask "connect|$id" "walk|/Games|$T|0||d::NDS")
unset BANANA_FETCH_TEST_DROPLIST
check "a listing connects again" 'echo "$r" | grep -q "^e${tab}d${tab}0${tab}[0-9]*${tab}NDS$"'
check "a walk connects again" 'echo "$r2" | tail -1 | grep -q "^end${tab}2${tab}"'
# a password kept sealed, and asked for again on another handheld
export BANANA_FETCH_TEST_PASSWORD=right BANANA_FETCH_TEST_SERIAL="$OUT/serial"
echo "0123456789abcdef" > "$OUT/serial"
pid=$(ask "put|name=Locked|protocol=dir|host=$S|user=me|password=right|remember=yes" | sed -n "s/^ok$tab//p")
check "the password kept sealed" 'grep -q "^password = v2:" "$BANANA_DATA/nas.conf" && ! grep -q "right" "$BANANA_DATA/nas.conf"'
r=$(ask "connect|$pid" "connect|$pid|wrong")
check "the kept password opens; a wrong one is refused" 'echo "$r" | head -1 | grep -q "^ok" && echo "$r" | tail -1 | grep -q "^err${tab}auth"'
echo "fedcba9876543210" > "$OUT/serial"
r=$(ask "connect|$pid")
check "on another handheld the password is asked for" 'echo "$r" | grep -q "^err${tab}password"'
echo "0123456789abcdef" > "$OUT/serial"
r=$(ask "put|id=$pid|host=$S/|user=me" "connect|$pid")
check "a new address: the password sealed again for it" 'echo "$r" | tail -1 | grep -q "^ok"'
kid=$(ask "put|name=Typed|protocol=dir|host=$S|user=me|remember=yes" | sed -n "s/^ok$tab//p")
r=$(ask "connect|$kid" "connect|$kid|right")
r2=$(ask "connect|$kid")
check "a password typed at the login is kept when the server remembers it" \
    'echo "$r" | head -1 | grep -q "^err${tab}password" && echo "$r" | tail -1 | grep -q "^ok" && echo "$r2" | grep -q "^ok"'
r=$(ask "trust|$id|SHA256:abc" "target|$id|/Games/NDS|data:roms/nds")
check "trust and target" 'grep -q "^hostkey = SHA256:abc" "$BANANA_DATA/nas.conf" && grep -q "^target /Games/NDS = data:roms/nds" "$BANANA_DATA/nas.conf"'
ask "trust|$id|" >/dev/null
unset BANANA_FETCH_TEST_PASSWORD
# an account without a password: an empty one kept (sealed), or typed at the login, is a password
eid=$(ask "put|name=Empty|protocol=dir|host=$S|user=me|password=|remember=yes" | sed -n "s/^ok$tab//p")
r=$(ask "connect|$eid")
tid=$(ask "put|name=Typed empty|protocol=dir|host=$S|user=me|remember=no" | sed -n "s/^ok$tab//p")
r2=$(ask "connect|$tid|")
check "an empty password, kept or typed" \
    'sed -n "/^\[$eid\]/,/^\[/p" "$BANANA_DATA/nas.conf" | grep -q "^password = v2:" && echo "$r" | head -1 | grep -q "^ok" && echo "$r2" | head -1 | grep -q "^ok"'

# ---- the runner -------------------------------------------------------------------------------------------------
J=$BANANA_DATA/downloads
mkdir -p "$J"
job() {   # job <name> <server> <unpack> <delete> lines...
    n=$1 s=$2 u=$3 d=$4; shift 4
    { printf 'title = test %s\nserver = %s\nunpack = %s\ndelete_archive = %s\nunwrap = yes\ntemp_root = %s\n' "$n" "$s" "$u" "$d" "$OUT/card"
      printf '%s\n' "$@" | sed "s/|/$tab/g"; } > "$J/$n.job"
}
status() { cat "$BANANA_RUN/fetch/status" 2>/dev/null; }
# a zip (unpacked into its folder, then deleted), a 7z in volumes, a plain file
cp "$A/multivolume.7z.001" "$A/multivolume.7z.002" "$A/multivolume.7z.003" "$S/Games/"
z=$(wc -c < "$S/Games/game.zip")
job 1-basic "$id" yes yes "get|$z|/Games/game.zip|$T/game.zip" "get|100000|/Games/NDS/Banana Kart.nds|$T/Banana Kart.nds" \
    "get|10240|/Games/multivolume.7z.001|$T/multivolume.7z.001" "get|10240|/Games/multivolume.7z.002|$T/multivolume.7z.002" \
    "get|9642|/Games/multivolume.7z.003|$T/multivolume.7z.003"
$F run
check "a file downloaded" 'cmp -s "$S/Games/NDS/Banana Kart.nds" "$T/Banana Kart.nds"'
check "a zip unpacked and deleted" '[ "$(wc -c < "$T/Zip Kart.nds")" = 70000 ] && [ ! -e "$T/game.zip" ]'
if [ $has7zz = 1 ]; then
    check "a 7z in volumes unpacked after its last one, all deleted" '[ "$(wc -c < "$T/noise.bin")" = 30000 ] && [ ! -e "$T/multivolume.7z.001" ] && [ ! -e "$T/multivolume.7z.003" ]'
    check "the status says done" 'status | grep -q "^done 5 2 0 0|test 1-basic"'
fi
check "the job finished, its outcomes beside it" '[ -f "$J/1-basic.finished" ] && [ "$(grep -c "" "$J/1-basic.finished.done")" -ge 5 ] && [ ! -e "$J/1-basic.job" ]'
check "the menu told, once the job is .finished" '[ "$(cat "$BANANA_RUN/fetch.finished")" = 1-basic.finished ]'
check "the log" 'grep -q "downloaded /Games/NDS/Banana Kart.nds" "$BANANA_DATA/logs/downloads.log"'
check "the runner let go of transfer.pid" '[ ! -e "$BANANA_RUN/transfer.pid" ]'
# the connection drops, then goes quiet: both waited out, the file whole
rm -f "$T/Banana Kart.nds"
job 2-flaky "$id" no no "get|100000|/Games/NDS/Banana Kart.nds|$T/Banana Kart.nds"
BANANA_FETCH_TEST_FAST=1 BANANA_FETCH_TEST_CUT=40000 $F run
check "resumed after a drop" 'cmp -s "$S/Games/NDS/Banana Kart.nds" "$T/Banana Kart.nds" && [ ! -e "$T/Banana Kart.nds.part" ]'
rm -f "$T/Banana Kart.nds"
job 3-stall "$id" no no "get|100000|/Games/NDS/Banana Kart.nds|$T/Banana Kart.nds"
BANANA_FETCH_TEST_FAST=1 BANANA_FETCH_TEST_STALL=40000 $F run
check "a quiet link given up on, then resumed" 'cmp -s "$S/Games/NDS/Banana Kart.nds" "$T/Banana Kart.nds"'
# a part bigger than the file: another file's, started again
head -c 200000 /dev/zero > "$T/Peel.gba.part"
job 4-part "$id" no no "get|1000|/Games/GBA/Peel.gba|$T/Peel.gba"
$F run
check "a stale part dropped" 'cmp -s "$S/Games/GBA/Peel.gba" "$T/Peel.gba"'
# stopped in the middle: the part and the job kept, the next run goes on from there
head -c 3000000 /dev/urandom > "$S/Games/big.bin"
job 5-stop "$id" no no "get|3000000|/Games/big.bin|$T/big.bin"
BANANA_FETCH_TEST_DELAY=40 $F run & rp=$!
sleep 1; kill -TERM $rp; wait $rp || true
check "stopped: the status, the part and the job kept" 'status | grep -q "^stopped" && [ -s "$T/big.bin.part" ] && [ -f "$J/5-stop.job" ]'
$F run
check "the stopped job finished by the next run" 'cmp -s "$S/Games/big.bin" "$T/big.bin" && [ -f "$J/5-stop.finished" ]'
# a server whose password nobody remembered: the job waits for it
export BANANA_FETCH_TEST_PASSWORD=right
nid=$(ask "put|name=Asks|protocol=dir|host=$S|user=me|remember=no" | sed -n "s/^ok$tab//p")
job 6-password "$nid" no no "get|1000|/Games/GBA/Peel.gba|$T/Peel2.gba"
$F run
check "waiting for a password" 'status | grep -q "^need .*|Asks|password" && [ -f "$J/6-password.job" ]'
printf 'right' > "$BANANA_RUN/fetch-$nid.pw"
$F run
check "done once the password came, the password file gone" '[ -f "$T/Peel2.gba" ] && [ ! -e "$BANANA_RUN/fetch-$nid.pw" ]'
unset BANANA_FETCH_TEST_PASSWORD
# no room: the file fails before a byte, the job goes on
job 7-full "$id" no no "get|100000|/Games/NDS/Banana Kart.nds|$OUT/card/roms/gba/Banana Kart.nds" "get|1000|/Games/GBA/Peel.gba|$OUT/card/roms/gba/Peel.gba"
BANANA_FETCH_TEST_FREE=1100000 $F run	# (1 MB is kept free besides the file)
check "no room for one file, the next one done" 'grep -q "no_space" "$J/7-full.finished.done" && [ -f "$OUT/card/roms/gba/Peel.gba" ] && [ ! -e "$OUT/card/roms/gba/Banana Kart.nds.part" ]'
# an encrypted archive among the downloads: left packed, the outcome says why
if [ $has7zz = 1 ]; then
    cp "$A/encrypted.7z" "$S/Games/"
    job 8-encrypted "$id" yes no "get|$(wc -c < "$A/encrypted.7z")|/Games/encrypted.7z|$T/encrypted.7z"
    $F run
    check "an encrypted archive left packed" '[ -f "$T/encrypted.7z" ] && grep -q "packed${tab}password" "$J/8-encrypted.finished.done"'
    # and unpacked from the card with its password (an extract job, as Archives on the card writes it)
    printf 'Secret123' > "$BANANA_RUN/arc.pw"
    { printf 'title = unpack\nunpack = yes\ndelete_archive = yes\nunwrap = yes\ntemp_root = %s\narchive_password_file = %s\n' "$OUT/card" "$BANANA_RUN/arc.pw"
      printf 'extract\t%s\t%s\n' "$T/encrypted.7z" "$T/enc"; } > "$J/9-extract.job"
    $F run
    check "an archive on the card unpacked with its password" '[ "$(wc -c < "$T/enc/game.nds")" = 32768 ] && [ ! -e "$T/encrypted.7z" ] && [ ! -e "$BANANA_RUN/arc.pw" ]'
fi
# a folder whose system plays its games zipped (keep_zips): its zips stay as they are, a 7z beside them is unpacked
K=$OUT/card/roms/gba
printf 'title = keep\nserver = %s\nunpack = yes\nkeep_zips = yes\ndelete_archive = yes\nunwrap = yes\ntemp_root = %s\nget\t%s\t/Games/game.zip\t%s\n' \
    "$id" "$OUT/card" "$z" "$K/game.zip" > "$J/91-keep.job"
if [ $has7zz = 1 ]; then
    cp "$A/solid.7z" "$S/Games/"
    printf 'get\t%s\t/Games/solid.7z\t%s\n' "$(wc -c < "$A/solid.7z")" "$K/solid.7z" >> "$J/91-keep.job"
fi
$F run
check "keep_zips: the zip kept as it is" '[ -f "$K/game.zip" ] && [ ! -e "$K/Zip Kart.nds" ]'
if [ $has7zz = 1 ]; then
    check "keep_zips: the 7z unpacked all the same" '[ "$(wc -c < "$K/game.nds")" = 32768 ] && [ ! -e "$K/solid.7z" ]'
fi
# an empty password from its file (a server that doesn't remember it, the account without one): no waiting for one
job 92-empty "$tid" no no "get|1000|/Games/GBA/Peel.gba|$T/Peel4.gba"
: > "$BANANA_RUN/fetch-$tid.pw"
$F run
check "a job with an empty password" '[ -f "$T/Peel4.gba" ] && [ -f "$J/92-empty.finished" ]'
# a job written while the runner lets go (the queue looked at, the lock not dropped yet: a long sync()): done by it
job 93-first "$id" no no "get|1000|/Games/GBA/Peel.gba|$T/Peel5.gba"
BANANA_FETCH_TEST_LINGER=1500 $F run & rp=$!
i=0; while [ ! -f "$J/93-first.finished" ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i + 1)); done
sleep 0.3
job 94-late "$id" no no "get|1000|/Games/GBA/Peel.gba|$T/Peel6.gba"
wait $rp
check "a job written as the runner let go, done by it" '[ -f "$T/Peel6.gba" ] && [ -f "$J/94-late.finished" ]'
# one runner at a time
sleep 30 & other=$!
echo $other > "$BANANA_RUN/transfer.pid"
job 10-lock "$id" no no "get|1000|/Games/GBA/Peel.gba|$T/Peel3.gba"
$F run
check "a second runner leaves the work to the first" '[ -f "$J/10-lock.job" ] && [ ! -e "$T/Peel3.gba" ]'
kill $other; wait $other 2>/dev/null || true
$F run
check "and does it once the first is gone" '[ -f "$T/Peel3.gba" ]'

echo "fetch-test: $pass passed, $fail failed"
[ $fail = 0 ]
