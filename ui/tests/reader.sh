# The reader (settings_text.cpp, #173, #203), sourced by run.sh once per panel size ($size, $OUT, $HERE and $fail are
# its): text files opened from Files on a card of their own, and a game's guide from its page. A guide in UTF-8 with
# Windows' line ends, tabs, Chinese and a line longer than the panel, moved a line, a page, by a drag on the bottom
# panel, and opened again where it was left; an empty file; a log bigger than the reader reads (its start, and the
# note); a .txt that is a program's bytes (not opened: its actions, and why); an old guide in Windows-1252.
RD=$OUT/rd-$size; rlog=$OUT/$size-reader.log; : > "$rlog"
"$HERE/tests/fixture.sh" "$RD" >/dev/null
T=$RD/texts; mkdir -p "$T"
python3 -I - "$T" <<'PY'
import random, sys
d = sys.argv[1]
lines = ["Peel Quest: a walkthrough", "第一章：香蕉森林。往北走，然后在大树下找到钥匙。", ""]
lines += ["\tChapter %d. Go north from the orchard, past the old windmill, and talk to the farmer twice: the second time he gives you the ladder." % i for i in range(1, 61)]
lines += ["A-very-long-line-with-no-spaces-" * 12, "", "The end."]
open(d + "/guide.txt", "wb").write("\r\n".join(lines).encode("utf-8") + b"\r\n")
open(d + "/empty.txt", "wb").close()
open(d + "/notes.md", "wb").write("# Notes\n\n* caf\xe9 au lait, \x93quoted\x94\n".encode("latin-1"))
random.seed(3)
open(d + "/junk.txt", "wb").write(bytes(random.getrandbits(8) for _ in range(8192)))
with open(d + "/big.log", "wb") as f:
    for i in range(30000): f.write(b"%06d the banana-ui log goes on and on, line after line, until it is far bigger than a guide\n" % i)
PY
run_rd() { "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
    --banana "$HERE/assets/banana" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$RD/roms" \
    --data "$RD/data" --script "$1" 2>> "$rlog" || { echo "FAIL $size: banana-ui (reader) exited $?"; fail=1; }; }
rcheck() { grep -qF -- "$1" "$rlog" || { echo "FAIL $size: reader: no '$1'"; fail=1; }; }
# This card's root is data, roms, texts; texts is big.log, empty.txt, guide.txt, junk.txt, notes.md
open="homeid files; press a; press a; press down; press down; press a"
# the guide: its second line (Chinese, no \r) a line down; a page on (both panels: 26 lines); B, and A again opens it
# where it was; a drag up on the bottom panel of 280 px, 10 lines on
run_rd "$open; press down; press down; shot reader-files; press a; shot reader-guide; press down; press r; shot reader-page; press b; \
    press a; swipe 300 380 300 100; press b"
rcheck "text: $T/guide.txt: 66 lines, "
rcheck "text: line 2 of "
grep -q "text: line 2 of [0-9]*: 第一章：香蕉森林。" "$rlog" || { echo "FAIL $size: reader: the Chinese line isn't the second"; fail=1; }
rcheck "text: line 28 of "
# (opened again on the first drawn line of the file's line it was left on; the drag 10 lines on from there)
at=$(sed -n 's/.*, from its line [0-9]* (drawn line \([0-9]*\)).*/\1/p' "$rlog" | head -1)
[ -n "$at" ] && [ "$at" -le 28 ] && [ "$at" -gt 20 ] || { echo "FAIL $size: reader: not opened again where it was left ($at)"; fail=1; }
rcheck "text: line $(( ${at:-0} + 10 )) of "
grep -q "$(printf '\r')" "$rlog" && { echo "FAIL $size: reader: a carriage return drawn"; fail=1; }
# every drawn line fits the panel: the long line without spaces is in pieces
n=$(grep -m1 "text: $T/guide.txt" "$rlog" | sed 's/.* lines, \([0-9]*\) on the panels.*/\1/')
[ "${n:-0}" -gt 66 ] || { echo "FAIL $size: reader: the guide's lines weren't wrapped ($n)"; fail=1; }
# the empty file; the old guide (Windows-1252); the log (its first MB, and the note at its end: R from the start wraps
# round, so L from it... the note is checked in the log); the program's bytes: Files' actions for it, and why
run_rd "$open; press down; press a; shot reader-empty; press b; press down; press down; press down; press a; shot reader-legacy; press b; \
    press up; press up; press up; press up; press a; shot reader-big; press b; press down; press down; press down; press a; shot reader-junk"
rcheck "text: $T/empty.txt: 0 lines, 1 on the panels"
rcheck "text: $T/notes.md: 3 lines, 3 on the panels, not UTF-8"
rcheck "text: $T/big.log: "
rcheck ", the first 1048576 of $(stat -c %s "$T/big.log") bytes"
rcheck "text: $T/junk.txt isn't text"
rcheck "files: actions for junk.txt: Copy, Move, Rename, Delete"
# a game's guide: Banana Kart DS.txt beside its game, the first row of its page; A reads it, B comes back to the page
printf 'Banana Kart DS\nHold R to drift.\n' > "$RD/roms/nds/Banana Kart DS.txt"
run_rd "lib 0 0; press x; shot reader-gamepage; press a; shot reader-gameguide; press down; press b; press b"
rcheck "text: $RD/roms/nds/Banana Kart DS.txt: 2 lines"
for s in reader-files reader-guide reader-page reader-empty reader-legacy reader-big reader-junk reader-gamepage reader-gameguide; do
    for p in top bot; do
        f=$OUT/$size-$s-$p.png
        [ -s "$f" ] || { echo "FAIL $size: no $f"; fail=1; continue; }
        [ "$(convert "$f" -format '%[fx:standard_deviation>0.01]' info:)" = 1 ] || { echo "FAIL $size: $f is blank"; fail=1; }
    done
done
# a page on is another picture on both panels
for p in top bot; do cmp -s "$OUT/$size-reader-guide-$p.png" "$OUT/$size-reader-page-$p.png" && { echo "FAIL $size: reader: a page on drew the same $p panel"; fail=1; }; done
true
