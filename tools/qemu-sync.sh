#!/bin/sh
# The home server's save sync inside a built card image, in QEMU: the image's own banana-sync, nds-saves and rootfs,
# where the tests in ui/tests run banana-sync built on a PC. After a first boot through the setup, over the VM's SSH:
#   1. the card paired
#   2. a GBA and a DS save each way: up from the card, and down from another device (a DraStic .dsv with DeSmuME's
#      footer, which the card takes as its raw save with DraStic's own put in step). Around every sync the server's
#      rows for the games are recorded: none may go or change in place
#   3. a power cut during a sync, five times: the VM killed at a random moment while saves come down slowly, the card
#      booted again and synced. The journal must have nothing open, each game's save must be the version that came
#      down, whole, and the one it replaced kept in the vault. The cuts land in the slow
#      downloads, which go to /run first; the few milliseconds an install has its journal line open are cut by
#      ui/tests/vault-test and sync-slots.sh instead
#   4. a conflict asked about on the panels, driven by the VM's keyboard: before a game (A keeps this handheld's, the
#      game starts after), and on Settings > Network > Home server > Saves (Use the server's, then Restore puts this
#      handheld's back); each choice checked on the card, the server and in the vault, both panels shot
#   5. the server gone at a game's start, refusing and not answering: the sync before a game tries, says the saves'
#      error, and gives up within its budget (3 s to connect, 10 s for a call; the menu's own cap on the wait is 20 s)
#      and leaves nothing in /run
# What QEMU can't show is left for a handheld: suspend to RAM (Power off after sleeping), the Wi-Fi's timings, the
# wait's feel on the panels.
#
#   tools/qemu-sync.sh <banana-*.img[.gz]> [out dir]
#
# Cases 1 and 2 run against ui/tests/fake-romm.py, started here, or against a real RomM 5.4 with ROMM_URL (as this
# machine reaches it; the VM reaches the same address through QEMU's user network, 127.0.0.1 being 10.0.2.2 there, a
# host name kept when the VM can look it up) and ROMM_TOKENS (three client tokens of one user as JSON {"A", "B", "C"},
# or a file of it); they write into the first GBA and DS games of that server. Cases 3 to 5 always use the stand-in.
# Needs what tools/qemu-boot.sh needs, and curl.
set -e
IMG=$1; OUT=${2:-qemu-sync-out}
[ -n "$IMG" ] && [ -f "$IMG" ] || { echo "usage: $0 <banana-*.img[.gz]> [out dir]" >&2; exit 2; }
HERE=$(cd "$(dirname "$0")/.." && pwd)
. "$HERE/tools/qemu-lib.sh"
set +e
jget() { python3 -c "import json,sys; print(json.load(sys.stdin)$1)"; }
port() { python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])'; }
waitport() { for i in $(seq 50); do curl -s "http://127.0.0.1:$1$2" >/dev/null && return 0; sleep 0.1; done; return 1; }
md5f() { md5sum < "$1" | cut -c1-32; }
. "$HERE/ui/tests/sync-card.sh"     # (dsv: a .dsv as DraStic writes it, as the save sync's tests make one)
# ra <command> ...: ui/tests/romm-api.py, a wrong answer from the server a failure
ra() { python3 "$HERE/ui/tests/romm-api.py" "$@" || bad "romm-api.py $1: a wrong answer (above)" >&2; }

# ---- the servers: the stand-in always, a real RomM for cases 1 and 2 when given
LIB=$OUT/lib; rm -rf "$LIB"; mkdir -p "$LIB/gba" "$LIB/nds"
# (the stand-in starts empty each run, so what its peer remembers of it goes too; a real server's peer keeps its own)
rm -rf "$OUT/peer-stand-in"
# (cases 1 and 2 take the first GBA game, the cuts the four of their own)
printf 'gba rom qemu' > "$LIB/gba/Both Ways (USA).gba"
for g in 1 2 3 4; do printf 'gba rom qemu %s' "$g" > "$LIB/gba/Cut $g (USA).gba"; done
head -c 4096 /dev/urandom > "$LIB/nds/Cut DS (USA).nds"
FP=$(port)
python3 "$HERE/ui/tests/fake-romm.py" "$FP" "$LIB" 2> "$OUT/fake-romm.log" & FAKE=$!
trap 'kill $FAKE 2>/dev/null; cleanup' EXIT
waitport "$FP" /api/heartbeat || { echo "fake RomM didn't start: $(tail -n 3 "$OUT/fake-romm.log")"; exit 1; }
FU=http://127.0.0.1:$FP
ftok() { curl -s -XPOST "$FU/test/token" -d '{}' | jget '["token"]'; }
FTA=$(ftok); FTC=$(ftok)
if [ -n "$ROMM_URL" ] && [ -n "$ROMM_TOKENS" ]; then
    RU=${ROMM_URL%/}; RTA=$(ra token "$ROMM_TOKENS" A); RTC=$(ra token "$ROMM_TOKENS" C)
    WHERE="the RomM at $RU"
else
    RU=$FU; RTA=$FTA; RTC=$FTC; WHERE="the stand-in"
fi
# vmurl <url>: the address as the VM reaches it. QEMU's user network sends 10.0.2.2 to this machine's loopback and
# anything else out as this machine would; a host name the VM looks up is kept (an https server's certificate names
# it), one it can't goes by the address this machine has for it
vmurl() {
    _h=$(python3 -c 'import sys, urllib.parse; print(urllib.parse.urlsplit(sys.argv[1]).hostname)' "$1")
    case $_h in
        127.0.0.1|localhost) _to=10.0.2.2 ;;
        *[!0-9.]*)
            if vm "nslookup '$_h'" >/dev/null 2>&1; then _to=$_h
            else
                _to=$(python3 -c 'import socket, sys; print(socket.gethostbyname(sys.argv[1]))' "$_h")
                echo "  ($_h isn't known in the VM: it goes by $_to, which a certificate for the name won't match)" >&2
            fi ;;
        *) _to=$_h ;;
    esac
    python3 -c 'import sys, urllib.parse
u = urllib.parse.urlsplit(sys.argv[1])
print(urllib.parse.urlunsplit((u.scheme, sys.argv[2] + (":%d" % u.port if u.port else ""), u.path, "", "")))' "$1" "$_to"
}
peer() { _u=$1; _t=$2; shift 2; _d=peer-real; [ "$_u" = "$FU" ] && _d=peer-stand-in; python3 "$HERE/ui/tests/romm-peers.py" "$_u" "$_t" bananaos "$OUT/$_d" "$@"; }
# newhead <url> <token> <rom id> <file>: the file put up by another device, over the head it has seen
newhead() { peer "$1" "$2" join "$3" >/dev/null; peer "$1" "$2" upload "$3" "$4" >/dev/null; }
ra roms "$RU" "$RTA" gba > "$OUT/gba.tsv"; ra roms "$RU" "$RTA" nds > "$OUT/nds.tsv"
G=$(head -n 1 "$OUT/gba.tsv"); D=$(head -n 1 "$OUT/nds.tsv")
[ -n "$G" ] || { echo "$WHERE has no GBA game"; exit 1; }
GI=${G%%	*}; GN=${G#*	}; GS=${GN%.*}
DI=${D%%	*}; DN=${D#*	}; DS=${DN%.*}
ra roms "$FU" "$FTA" gba | grep '	Cut ' > "$OUT/cuts.tsv"
[ "$(wc -l < "$OUT/cuts.tsv")" = 4 ] || { echo "the stand-in's four games not found: $(cat "$OUT/cuts.tsv")"; exit 1; }
CUTS=$(cut -f1 "$OUT/cuts.tsv" | tr '\n' ' ')

echo "1. the first boot, the card's setup done, paired with $WHERE"
boot first || exit 1
finish_setup
shutdown_from_settings first
prep() {
    enable_ssh
    # (no sync by itself: each one here is the test's)
    sed -i '/^homesync_/d' "$DATA/banana/settings.conf"; echo "homesync_auto = off" >> "$DATA/banana/settings.conf"
    mkdir -p "$DATA/roms/gba" "$DATA/roms/nds"
    ra fetch "$RU" "$RTA" "$GI" "$GN" "$DATA/roms/gba/$GN" && ok "the server's GBA game on the card" || bad "the server's GBA game not fetched"
    [ -z "$D" ] || { ra fetch "$RU" "$RTA" "$DI" "$DN" "$DATA/roms/nds/$DN" && ok "the server's DS game on the card" || bad "the server's DS game not fetched"; }
    for f in "$LIB/gba/"*; do [ -e "$DATA/roms/gba/${f##*/}" ] || cp "$f" "$DATA/roms/gba/"; done     # (the fetched kept)
}
with_data prep
boot paired || exit 1
wait_ssh paired || exit 1
VRU=$(vmurl "$RU")
vm "banana-sync pair '$VRU' '$RTA'" > "$OUT/pair.log" 2>&1 && ok "paired from the card ($VRU)" || bad "pairing from the card: $(tail -n 3 "$OUT/pair.log")"

echo "2. a save each way, GBA and DS"
vsync() {   # vsync <name> <url> <token> <ids...>: a sync of the saves in the VM, the server's rows around it compared
    _n=$1; _u=$2; _t=$3; shift 3
    ra rows "$_u" "$_t" "$@" > "$OUT/rows.before"
    vm 'banana-sync sync --saves-only' > "$OUT/sync-$_n.log" 2>&1
    ra rows "$_u" "$_t" "$@" > "$OUT/rows.after"
    python3 "$HERE/ui/tests/romm-api.py" compare "$OUT/rows.before" "$OUT/rows.after" "$OUT/rows.diff" &&
        ok "$_n: no row on the server removed or changed" || bad "$_n: rows removed or changed: $(cat "$OUT/rows.diff")"
}
IDS="$GI${D:+ $DI}"
# (whatever a server used before holds as these games' heads, the card starts from one it can take: put up by another
# device, then taken, from no save of its own)
for i in $IDS; do head -c 8192 /dev/urandom > "$OUT/start.sav"; newhead "$RU" "$RTC" "$i" "$OUT/start.sav"; done
vsync first "$RU" "$RTA" $IDS
vm "mkdir -p /data/saves/gba; head -c 8192 /dev/urandom > '/data/saves/gba/$GS.srm'"
vsync gba-up "$RU" "$RTA" $IDS
[ "$(ra head "$RU" "$RTA" "$GI")" = "$(vmd5 "/data/saves/gba/$GS.srm")" ] && ok "a GBA save up from the card" || bad "the GBA save not sent: $(cat "$OUT/sync-gba-up.log")"
head -c 8192 /dev/urandom > "$OUT/g-other.srm"; newhead "$RU" "$RTC" "$GI" "$OUT/g-other.srm"
vsync gba-down "$RU" "$RTA" $IDS
[ "$(vmd5 "/data/saves/gba/$GS.srm")" = "$(md5f "$OUT/g-other.srm")" ] && ok "a GBA save down from another device" || bad "the GBA save not taken: $(cat "$OUT/sync-gba-down.log")"
if [ -n "$D" ]; then
    vm "mkdir -p /data/saves/nds; head -c 8192 /dev/urandom > '/data/saves/nds/$DS.sav'"
    vsync ds-up "$RU" "$RTA" $IDS
    [ "$(ra head "$RU" "$RTA" "$DI")" = "$(vmd5 "/data/saves/nds/$DS.sav")" ] && ok "a DS save up from the card" || bad "the DS save not sent: $(cat "$OUT/sync-ds-up.log")"
    head -c 8192 /dev/urandom > "$OUT/d-raw.sav"; dsv "$OUT/d-raw.sav" "$OUT/$DS.dsv"; newhead "$RU" "$RTC" "$DI" "$OUT/$DS.dsv"
    vsync ds-down "$RU" "$RTA" $IDS
    [ "$(vmd5 "/data/saves/nds/$DS.sav")" = "$(md5f "$OUT/d-raw.sav")" ] && [ "$(vmd5 "/data/drastic/backup/$DS.dsv")" = "$(md5f "$OUT/d-raw.sav")" ] &&
        ok "DraStic's .dsv down from another device: the raw save taken, DraStic's own in step (the image's nds-saves)" ||
        bad "the .dsv not taken, or DraStic's not in step: $(cat "$OUT/sync-ds-down.log")"
else
    echo "  (no DS game on $WHERE: the DS's half skipped)"
fi

echo "3. a power cut during a sync, five times over"
# each game's new version comes down at CUT_RATE bytes a second, so the four take CUT_WINDOW seconds between them; a
# cut lands at random in all but the last second or two of that
CUT_SAVE=131072 CUT_RATE=32768
CUT_WINDOW=$(( 4 * CUT_SAVE / CUT_RATE ))
vm "banana-sync forget" >/dev/null 2>&1
vm "banana-sync pair 'http://10.0.2.2:$FP' '$FTA'" > "$OUT/pair-fake.log" 2>&1 && ok "paired with the stand-in" || bad "pairing with the stand-in: $(tail -n 3 "$OUT/pair-fake.log")"
cutsave() { printf '/data/saves/gba/%s.srm' "$(awk -F'\t' -v i="$1" '$1 == i { sub(/\.gba$/, "", $2); print $2 }' "$OUT/cuts.tsv")"; }
for i in $CUTS; do vm "head -c $CUT_SAVE /dev/urandom > '$(cutsave "$i")'"; done
vm 'banana-sync sync --saves-only' > "$OUT/cut-0.log" 2>&1
grep -q ', 0 conflicts, 0 errors' "$OUT/cut-0.log" && ok "the cuts' saves sent" || bad "the cuts' saves not sent: $(cat "$OUT/cut-0.log")"
for n in 1 2 3 4 5; do
    # each game's version before the cut (the card's, which the server has too), and a new one from another device
    for i in $CUTS; do
        vmd5 "$(cutsave "$i")" > "$OUT/prev-$i"
        head -c $CUT_SAVE /dev/urandom > "$OUT/cut.srm"; md5f "$OUT/cut.srm" > "$OUT/new-$i"; newhead "$FU" "$FTC" "$i" "$OUT/cut.srm"
    done
    # (eight downloads slowed: the four, and each again should the sync try one twice)
    curl -s -XPOST "$FU/test/fault" -d "{\"path\": \"GET /api/saves/[0-9]+/content\", \"kind\": \"slow\", \"arg\": $CUT_RATE, \"count\": 8}" >/dev/null
    vm 'nohup banana-sync sync --saves-only > /dev/null 2>&1 < /dev/null &'
    s=$(python3 -c "import random; print(round(random.uniform(1, $CUT_WINDOW - 2), 1))"); sleep "$s"
    kill -9 $QEMU 2>/dev/null; wait $QEMU 2>/dev/null
    curl -s -XPOST "$FU/test/fault" -d '{"clear": true}' >/dev/null
    boot "cut-$n" || { bad "cut $n: the card didn't boot again"; continue; }
    wait_ssh "cut-$n" || continue
    vm 'banana-sync sync --saves-only' > "$OUT/cut-$n.log" 2>&1
    # (read as banana-sync reads it: a begin cut short, under its ten fields, is skipped)
    vm "awk -F'\t' '\$1 == \"begin\" && NF >= 10 { o[\$2] = 1 } \$1 == \"end\" && NF >= 2 { delete o[\$2] } END { for (i in o) { print i; n++ } exit n > 0 }' /data/banana/logs/sync/journal.log" > "$OUT/open-$n" &&
        ok "cut $n at ${s} s: the journal has nothing open after the next sync" || bad "cut $n: operations left open: $(cat "$OUT/open-$n")"
    vm "find /data/banana/save-vault -type f -exec md5sum {} +" | cut -c1-32 > "$OUT/vault-$n"
    for i in $CUTS; do
        h=$(vmd5 "$(cutsave "$i")"); p=$(cat "$OUT/prev-$i"); w=$(cat "$OUT/new-$i")
        if [ "$h" = "$w" ]; then grep -qx "$p" "$OUT/vault-$n" && echo "new" || echo "new, the one before it not in the vault"
        elif [ "$h" = "$p" ]; then echo "whole, but not brought up to date"
        else echo "neither its version before nor the new one ($h)"; fi | sed "s#^#$(cutsave "$i"): #"
    done > "$OUT/after-$n"
    ! grep -qv ': new$' "$OUT/after-$n" && ok "cut $n: every save the new version, whole, the one it replaced in the vault" ||
        bad "cut $n: $(grep -v ': new$' "$OUT/after-$n" | tr '\n' ' ')"
done

echo "4. a conflict asked about on the panels: before a game, and on the Saves page"
# The menu's wait before a game asks wlan0 whether Wi-Fi is up, and the VM has a wired network instead: renamed wlan0
# (its address asked for again), it stands in for Wi-Fi up, the case the wait is for. Only two GBA games are left on
# the card, so from the DS shelf the menu opens on, Right, A and A start Cut 1
cutid() { awk -F'\t' -v n="$1" '$2 == n ".gba" { print $1 }' "$OUT/cuts.tsv"; }
C1=$(cutid "Cut 1 (USA)"); C2=$(cutid "Cut 2 (USA)")
vm "sed -i '/^homesync_auto/d; /^homesync_launch/d' /data/banana/settings.conf; printf 'homesync_auto = quit\\nhomesync_launch = on\\n' >> /data/banana/settings.conf
    find /data/roms/gba -type f ! -name 'Cut 1 (USA).gba' ! -name 'Cut 2 (USA).gba' -exec rm -f {} +; sync; reboot" 2>/dev/null
for i in $(seq 120); do kill -0 $QEMU 2>/dev/null || break; sleep 0.5; done
kill -0 $QEMU 2>/dev/null && { bad "the VM still running 60 s after a reboot"; kill $QEMU 2>/dev/null; }
wait $QEMU 2>/dev/null
asked() { grep -q "sync before the game: $1" "$OUT/serial.log"; }
if boot prompt && wait_ssh prompt; then
    vm 'nohup sh -c "kill \$(cat /run/banana/udhcpc-eth0.pid); ip link set eth0 down; ip link set eth0 name wlan0; ip link set wlan0 up;
        udhcpc -b -q -t 5 -i wlan0 -p /run/banana/udhcpc-wlan0.pid -s /usr/share/udhcpc/default.script" > /dev/null 2>&1 < /dev/null &'
    sleep 3; wait_ssh "wlan0"
    [ "$(vm 'find /data/roms/gba -type f | wc -l')" = 2 ] && ok "only Cut 1 and Cut 2 on the GBA shelf" || bad "other GBA games still on the card: $(vm 'find /data/roms/gba -type f')"
    # Cut 1 changed on the card and by another device: the sync before the game holds it and the menu asks; A keeps
    # this handheld's, which goes up as the new version, the server's kept in the vault, and then the game starts
    vm "head -c $CUT_SAVE /dev/urandom > '$(cutsave "$C1")'"; mine=$(vmd5 "$(cutsave "$C1")")
    head -c $CUT_SAVE /dev/urandom > "$OUT/p1-server.srm"; newhead "$FU" "$FTC" "$C1" "$OUT/p1-server.srm"
    shot prompt-menu; key right ret; sleep 1; key ret
    wait_for "sync before the game: asking about a conflict" 60 "the game's conflict asked about before it starts" && { sleep 2; shot prompt; }
    asked "game starts\|played now" && bad "the game started before the conflict was answered"
    key ret
    wait_for "sync before the game: settled" 60 "A on the prompt settled it"
    [ "$(ra head "$FU" "$FTA" "$C1")" = "$mine" ] && ok "A on the prompt: this handheld's save is the server's head" ||
        bad "A on the prompt: the server's head isn't this handheld's save"
    vm "find /data/banana/save-vault -type f -exec md5sum {} +" | cut -c1-32 | grep -qx "$(md5f "$OUT/p1-server.srm")" &&
        ok "A on the prompt: the server's save kept in the vault" || bad "A on the prompt: the server's save not in the vault"
    wait_for "starting gba" 30 "then the game started"
    for i in $(seq 120); do grep -q 'game ended' "$OUT/serial.log" && break; sleep 0.5; done
    sleep 3
    # Cut 2 the same, left held by a sync: Settings > Network (the fourth tile) > Home server (the row above its last) >
    # Saves (the eleventh row) lists it first (conflicts come first); A opens it, Down and A uses the server's
    vm "head -c $CUT_SAVE /dev/urandom > '$(cutsave "$C2")'"; mine=$(vmd5 "$(cutsave "$C2")")
    head -c $CUT_SAVE /dev/urandom > "$OUT/p2-server.srm"; newhead "$FU" "$FTC" "$C2" "$OUT/p2-server.srm"
    # (the menu's sync after Cut 1's game may still have the saves: banana-sync waits 10 s for them, then says so)
    for i in 1 2 3 4 5 6; do
        vm 'banana-sync sync --saves-only' > "$OUT/saves-held.log" 2>&1
        grep -q 'another sync is saving' "$OUT/saves-held.log" || break
    done
    vm 'banana-sync saves' > "$OUT/saves-1.tsv"
    head -n 1 "$OUT/saves-1.tsv" | grep -q "^conflict	$C2/" && ok "Cut 2 held, first on the list" || bad "Cut 2 not held first: $(head -n 3 "$OUT/saves-1.tsv")"
    key s pgdn pgdn pgdn ret up up ret down down down down down down down down down down ret
    # (the page reads banana-sync saves in the background, which takes the VM a few seconds or more)
    wait_for "saves: the list read" 60 "Saves: the page has read the card"; sleep 1; shot saves-list
    key ret; sleep 1; shot saves-game
    key down ret
    for i in $(seq 30); do [ "$(vmd5 "$(cutsave "$C2")")" = "$(md5f "$OUT/p2-server.srm")" ] && break; sleep 1; done
    [ "$(vmd5 "$(cutsave "$C2")")" = "$(md5f "$OUT/p2-server.srm")" ] && ok "Saves: Use the server's put the server's save on the card" ||
        bad "Saves: the server's save not on the card"
    vm "find /data/banana/save-vault -type f -exec md5sum {} +" | cut -c1-32 | grep -qx "$mine" &&
        ok "Saves: this handheld's save kept in the vault" || bad "Saves: this handheld's save not in the vault"
    vm 'banana-sync saves' > "$OUT/saves-2.tsv"
    grep -q "^conflict	$C2/" "$OUT/saves-2.tsv" && bad "Saves: Cut 2 still in conflict" || ok "Saves: the conflict gone from the list"
    sleep 2; shot saves-settled
    # the game stays open, its rows now its kept copies in the list's order. Cut 2 played since (new bytes, in no copy
    # yet), then the copy of this handheld's save put back (Up to the first, Down to it): the save it replaces kept
    vm "head -c $CUT_SAVE /dev/urandom > '$(cutsave "$C2")'"; played=$(vmd5 "$(cutsave "$C2")")
    vm "find /data/banana/save-vault -type f -exec md5sum {} +" | cut -c1-32 | grep -qx "$played" && bad "Saves: the save played since already in the vault"
    r=$(cutsave "$C2" | sed 's#^/data/##')
    vm "banana-sync saves | awk -F'\t' -v r='$r' '\$1 == \"kept\" && \$2 == r { print \$3 }' | while read -r f; do md5sum < \"\$f\"; done" | cut -c1-32 > "$OUT/kept-2"
    at=$(grep -nx "$mine" "$OUT/kept-2" | head -n 1 | cut -d: -f1)
    if [ -n "$at" ]; then
        key up up up up up up up up
        n=1; while [ $n -lt "$at" ]; do key down; n=$((n + 1)); done
        key ret
        for i in $(seq 30); do [ "$(vmd5 "$(cutsave "$C2")")" = "$mine" ] && break; sleep 1; done
        [ "$(vmd5 "$(cutsave "$C2")")" = "$mine" ] && ok "Saves: Restore put this handheld's save back" || bad "Saves: Restore didn't put this handheld's save back"
        vm "find /data/banana/save-vault -type f -exec md5sum {} +" | cut -c1-32 | grep -qx "$played" &&
            ok "Saves: the save it replaced kept too" || bad "Saves: the save a restore replaced not kept"
    else
        bad "Saves: no kept copy of this handheld's save listed: $(cat "$OUT/kept-2")"
    fi
    sleep 2; shot saves-restored
    key s; sleep 2       # (START leaves the settings for the menu, where the shutdown below starts)
else
    bad "the card didn't come back for the prompt's case"
fi

echo "5. the server gone at a game's start"
kill $FAKE 2>/dev/null; wait $FAKE 2>/dev/null
vm "sed -i '/^homesync_auto/d; /^homesync_launch/d' /data/banana/settings.conf; printf 'homesync_auto = quit\\nhomesync_launch = on\\n' >> /data/banana/settings.conf"
# gone <name> <how> <seconds at least> <at most>: the sync before a game, timed; it must have tried (its status the
# saves' error) and given up within its budget
gone() {
    t=$(vm "rm -f /run/banana/launch.status; a=\$(date +%s); banana-sync sync --auto launch --rom 'gba/Cut 1 (USA).gba' > /tmp/launch-$1.log 2>&1; echo \$(( \$(date +%s) - a ))")
    vm "cat /run/banana/launch.status 2>/dev/null; cat /tmp/launch-$1.log" > "$OUT/launch-$1.log"
    st=$(head -n 1 "$OUT/launch-$1.log")
    echo "$st" | grep -q '^done \([0-9]* \)\{5\}[1-9][0-9]*|saves: ' && ok "the sync before a game with the server $2 tried: $st" || bad "the sync before a game with the server $2: $(cat "$OUT/launch-$1.log")"
    [ -n "$t" ] && [ "$t" -ge "$3" ] && [ "$t" -le "$4" ] && ok "the sync before a game with the server $2 ended in $t s" || bad "the sync before a game with the server $2 took ${t:-?} s, not $3 to $4"
}
gone refused refusing 0 2
# (a server that doesn't answer at all, an address nothing routes: the connect timeout's case, 3 s, not a refusal)
vm "sed -i 's#\"server\": \"[^\"]*\"#\"server\": \"http://10.255.255.1:8080\"#' /data/banana/romsync.json"
gone silent "not answering" 2 11
[ -z "$(vm 'ls /run/banana | grep "^sync-"')" ] && ok "nothing of it left in /run" || bad "files left in /run: $(vm 'ls /run/banana')"
shutdown_from_settings last
echo "qemu sync test: $pass passed, $fail failed ($OUT)"
[ $fail = 0 ]
