#!/bin/sh
# A new system on trial (/usr/libexec/banana/boot-guard) against a made-up card: the boot partition's extlinux.conf
# and trial, the running slot, the data partition's logs; the restart only noted.
#   sh system/tests/boot-guard-test.sh
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
G=$HERE/rootfs/usr/libexec/banana/boot-guard
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
pass=0 fail=0
ok() { pass=$((pass + 1)); echo "  ok: $*"; }
bad() { fail=$((fail + 1)); echo "  FAIL: $*"; }
printf '#!/bin/sh\necho "$1" >> "%s/restarts"\n' "$T" > "$T/restart"; chmod +x "$T/restart"

# card <default slot> <running slot> [trial line]
card() {
    R=$T/card; rm -rf "$R" "$T/restarts"
    mkdir -p "$R/boot/extlinux" "$R/run/banana" "$R/data/banana/logs" "$R/proc"
    printf 'DEFAULT %s\nTIMEOUT 0\nLABEL a\n  LINUX /a/Image\nLABEL b\n  LINUX /b/Image\n' "$1" > "$R/boot/extlinux/extlinux.conf"
    echo "$2" > "$R/run/banana/slot"; : > "$R/proc/cmdline"
    [ -n "$3" ] && echo "$3" > "$R/boot/trial"
    return 0
}
run() { BOOTGUARD_ROOT=$R BOOTGUARD_REBOOT=$T/restart BOOTGUARD_WAIT=${W:-3} sh "$G" "$@"; }
default() { sed -n 's/^DEFAULT *//p' "$R/boot/extlinux/extlinux.conf"; }
restarts() { cat "$T/restarts" 2>/dev/null | tr '\n' ' ' | sed 's/ $//'; }
settle() { sleep "${1:-1}"; }

echo "1. no trial: nothing happens"
card a a; run early; settle
[ "$(default)" = a ] && [ ! -e "$R/boot/trial" ] && [ -z "$(restarts)" ] && ok "DEFAULT a, no trial, no restart" || bad "default $(default), restarts '$(restarts)'"

echo "2. a new system's first start, the menu up: kept"
card b b "b a"; run early
[ "$(cat "$R/boot/trial")" = "b a 1" ] && ok "trial marked started (b a 1)" || bad "trial: $(cat "$R/boot/trial" 2>/dev/null)"
settle 1; : > "$R/run/banana/boot-ok"; settle 2
[ ! -e "$R/boot/trial" ] && ok "the trial over once the menu was up" || bad "trial still there: $(cat "$R/boot/trial")"
[ "$(default)" = b ] && [ -z "$(restarts)" ] && ok "DEFAULT still b, no restart" || bad "default $(default), restarts '$(restarts)'"
grep -q "slot b started: kept" "$R/run/banana/boot-times" && ok "said so in boot-times" || bad "not in boot-times"

echo "3. its first start, no menu: back to the slot before, a clean restart"
card b b "b a"; W=2 run early; settle 4
[ "$(default)" = a ] && ok "DEFAULT back to a" || bad "default $(default)"
[ ! -e "$R/boot/trial" ] && ok "the trial gone" || bad "trial: $(cat "$R/boot/trial")"
[ "$(restarts)" = clean ] && ok "restarted (clean)" || bad "restarts '$(restarts)'"
grep -q "slot b didn't start (no menu after 2 s): back to slot a" "$R/data/banana/logs/updates.log" && ok "updates.log: $(cut -c21- "$R/data/banana/logs/updates.log")" || bad "updates.log: $(cat "$R/data/banana/logs/updates.log" 2>/dev/null)"
grep -q "didn't start" "$R/boot/rolled-back" && ok "noted in /boot/rolled-back" || bad "no /boot/rolled-back"

echo "4. a second start of the trial (the first never reached the menu: turned off and on): back at once"
card b b "b a 1"; run early || true
[ "$(default)" = a ] && [ "$(restarts)" = now ] && ok "DEFAULT a, restarted at once (now)" || bad "default $(default), restarts '$(restarts)'"
grep -q "its last start didn't reach the menu" "$R/boot/rolled-back" && ok "the reason noted" || bad "rolled-back: $(cat "$R/boot/rolled-back" 2>/dev/null)"

echo "5. a trial for the other slot (DEFAULT changed by hand): cleared, nothing else"
card a a "b a"; run early; settle
[ ! -e "$R/boot/trial" ] && [ "$(default)" = a ] && [ -z "$(restarts)" ] && ok "trial cleared, DEFAULT a, no restart" || bad "trial $(cat "$R/boot/trial" 2>/dev/null), default $(default), restarts '$(restarts)'"

echo "6. the breadcrumbs: the milestones on the card until the menu is up, the boot before kept"
card a a; echo "the boot before" > "$R/data/banana/logs/boot-progress.txt"
printf 'rcS: started                   1.90 s\nrcS: data partition ready      2.90 s\n' > "$R/run/banana/boot-times"
run progress; settle 1
P=$R/data/banana/logs/boot-progress.txt
grep -q "rcS: data partition ready" "$P" && ok "milestones copied" || bad "$(cat "$P")"
grep -q "the boot before" "$R/data/banana/logs/boot-progress.1.txt" && ok "the boot before in boot-progress.1.txt" || bad "no boot-progress.1.txt"
echo "bananad: buttons live           3.40 s" >> "$R/run/banana/boot-times"; settle 3
grep -q "buttons live" "$P" && ok "a later milestone copied too" || bad "the later milestone missing"
: > "$R/run/banana/boot-ok"; settle 3
grep -q "boot complete: the menu is up" "$P" && ok "ends with: boot complete" || bad "no end line: $(tail -n1 "$P")"

echo "boot-guard test: $pass passed, $fail failed"
[ $fail = 0 ]
