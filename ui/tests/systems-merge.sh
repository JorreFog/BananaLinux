#!/bin/sh
# systems-merge (the card's banana/systems.conf merged over the image's per system): the cases it has to get right.
#   tests/systems-merge.sh [image systems.conf] [history]   (from run.sh; also runs on a device, with busybox's awk)
HERE=$(cd "$(dirname "$0")" && pwd)
M=$HERE/../../system/rootfs/usr/libexec/banana/systems-merge
[ -f "$M" ] || M=$HERE/systems-merge
IMG=${1:-$HERE/../../system/rootfs/etc/banana/systems.conf}
HIST=${2:-${IMG%.conf}.history}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL systems-merge $1: $2"; fail=1; }
# merge <case> <card text>: $T/<case>.out, stderr in $T/<case>.err
merge() { printf '%b' "$2" > "$T/$1.card"; sh "$M" "$IMG" "$T/$1.card" "$T/$1.out" "$HIST" 2> "$T/$1.err"; }
# val <case> <id> <key>: the merged value ("" when the key isn't there)
val() { awk -v id="[$2]" -v k="$3" '/^\[/{s=$0; next} s==id && index($0, k " = ")==1 {print substr($0, length(k)+4)}' "$T/$1.out"; }
img() { awk -v id="[$1]" -v k="$2" '/^\[/{s=$0; next} s==id && index($0, k " = ")==1 {print substr($0, length(k)+4)}' "$IMG"; }
eq() { [ "$(val "$1" "$2" "$3")" = "$4" ] || bad "$1" "[$2] $3 is '$(val "$1" "$2" "$3")', not '$4'"; }
ids() { grep '^\[' "$T/$1.out" | tr -d '[]' | tr '\n' ' '; }
same_as_image() { [ "$(grep -v '^#' "$T/$1.out" | grep -v '^$')" = "$(grep -v '^#' "$IMG" | grep -v '^$')" ] || bad "$1" "not the image's list"; }

# no card file: the image's list as it is
sh "$M" "$IMG" "$T/none.card" "$T/none.out" "$HIST" 2>/dev/null; cmp -s "$IMG" "$T/none.out" || bad none "not a copy of the image's file"
# an empty card file, or one with only comments: the image's
merge empty ''; same_as_image empty
merge comments '# nothing here\n\n   # nor here\n'; same_as_image comments

# a card that changes one key of one system: only that key
merge one '[psx]\nperf = performance\n'
eq one psx perf performance; eq one psx run "$(img psx run)"; eq one psx ext "$(img psx ext)"; eq one nds run "$(img nds run)"
[ "$(ids one)" = "$(grep '^\[' "$IMG" | tr -d '[]' | tr '\n' ' ')" ] || bad one "the systems or their order changed: $(ids one)"

# a full copy of an older image's list (0.4.2's, with a system of the user's own at the end): the old defaults give
# way to the image's current values, the user's system stays
cat > "$T/old.card" <<'EOF'
[nds]
name = Nintendo DS
maker = Nintendo
ext = nds zip
run = /usr/libexec/banana/run-nds %ROM%
perf = superdrastic
always = yes
[psx]
name = PlayStation
maker = Sony
ext = chd cue pbp m3u
run = /usr/libexec/banana/run-retroarch pcsx_rearmed %ROM%
perf = balanced
[rocknixdsbank]
name = Bank & Trade
[ports]
name = Ports
always = yes
[mysys]
name = My System
ext = xyz
run = /data/mine/run.sh %ROM%
EOF
sh "$M" "$IMG" "$T/old.card" "$T/old.out" "$HIST" 2> "$T/old.err"
eq old psx run "$(img psx run)"; eq old psx perf "$(img psx perf)"; eq old nds ext "$(img nds ext)"
eq old rocknixdsbank name "$(img rocknixdsbank name)"; eq old ports always "$(img ports always)"
eq old mysys name "My System"; eq old mysys run "/data/mine/run.sh %ROM%"
[ "$(ids old | awk '{print $NF}')" = mysys ] || bad old "the card's own system isn't last: $(ids old)"
[ -s "$T/old.err" ] && bad old "said something: $(cat "$T/old.err")"

# "key =" with nothing: the key taken away (the menu's default for it)
merge emptyval '[psx]\nperf =\n[nds]\nalways = \n'
eq emptyval psx perf ""; eq emptyval nds always ""; eq emptyval psx run "$(img psx run)"
grep -q '^perf *=$' "$T/emptyval.out" && bad emptyval "an empty 'perf =' line was written"

# case and spaces in headers and keys, CRLF line ends, comments between
merge spacing '\r\n# mine\r\n[ PSX ]\r\n   Perf   =   performance  \r\n\t# a comment\r\nEXT = cue\r\n'
eq spacing psx perf performance; eq spacing psx ext cue; grep -q "$(printf '\r')" "$T/spacing.out" && bad spacing "a CR in the output"

# a system seen twice on the card: one section, the later keys win; a key the image's system doesn't have is added
merge twice '[psx]\nperf = performance\n[gba]\nflat = yes\n[psx]\nperf = powersave\n'
eq twice psx perf powersave; eq twice gba flat yes
[ "$(grep -c '^\[psx\]' "$T/twice.out")" = 1 ] || bad twice "[psx] written more than once"

# malformed card files: the image's list, and the line named in the log
for c in 'garbage line' '[psx\nperf = performance' 'perf = performance\n[psx]' '[]\nname = x' '[psx]\n= performance'; do
    merge bad "$c"
    same_as_image bad
    grep -q 'systems-merge: .* line [0-9]* .*not used' "$T/bad.err" || bad bad "no line named for '$c': $(cat "$T/bad.err")"
done

# the image's file unreadable: nothing half-written (the readers fall back to /etc themselves)
sh "$M" "$T/missing.conf" "$T/one.card" "$T/miss.out" "$HIST" 2>/dev/null; ls "$T"/miss.out.tmp.* >/dev/null 2>&1 && bad missing "a temporary file left"

[ $fail = 0 ] && echo "systems-merge: all checks passed"
exit $fail
