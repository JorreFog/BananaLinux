#!/bin/sh
# usr/libexec/banana/run-wayland under a headless sway with two outputs: the program runs inside the compositor with
# Wayland's environment set, its exit status comes back, the compositor is gone after it, the configuration names the
# top output as the main one and has the second-screen rule; --keep-bottom powers the bottom output off; SIGTERM
# reaches the program. Needs sway and swaymsg; skipped otherwise.
HERE=$(cd "$(dirname "$0")/.." && pwd)
W=$HERE/rootfs/usr/libexec/banana/run-wayland
for t in sway swaymsg; do command -v $t >/dev/null || { echo "run-wayland test skipped (no $t)"; exit 0; }; done
T=$(mktemp -d); chmod 700 "$T"
trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL run-wayland: $*"; fail=1; }
run() {
    env BANANA_RUN="$T/run" BANANA_DATA="$T/data" BANANA_HW_CONF=/nonexistent BANANA_WL_OUTPUTS="HEADLESS-1 HEADLESS-2" \
        WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=pixman LIBSEAT_BACKEND=noop \
        sh "$W" "$@"
}
printf '#!/bin/sh\necho "$WAYLAND_DISPLAY|$SDL_VIDEODRIVER|$QT_QPA_PLATFORM|$BANANA_WL_TOP|$BANANA_WL_BOTTOM|$1" > "%s/env"\nswaymsg -t get_outputs > "%s/outputs.json"\nexit 7\n' "$T" "$T" > "$T/prog"
chmod +x "$T/prog"
run --two-screens -- "$T/prog" "an argument with 'quotes'"; rc=$?
[ $rc = 7 ] || bad "the program's exit status came back as $rc"
case $(cat "$T/env" 2>/dev/null) in wayland-*"|wayland|wayland|HEADLESS-1|HEADLESS-2|an argument with 'quotes'") ;; *) bad "the program's environment: $(cat "$T/env" 2>/dev/null)" ;; esac
grep -q '^workspace 1 output HEADLESS-1$' "$T/run/wl/sway.conf" || bad "the top output isn't the main one"
grep -q 'Bottom.*move window to output HEADLESS-2, fullscreen enable' "$T/run/wl/sway.conf" || bad "no second-screen rule"
pgrep -f "sway -c $T/run/wl/sway.conf" >/dev/null && bad "the compositor is still running"
# --keep-bottom: the bottom output off while the program runs
run --keep-bottom -- "$T/prog" x >/dev/null 2>&1
python3 -I -c "
import json, sys
o = {x['name']: x for x in json.load(open(sys.argv[1]))}
sys.exit(0 if o['HEADLESS-1'].get('power', True) and not o['HEADLESS-2'].get('power', True) else 1)" "$T/outputs.json" || bad "--keep-bottom left the bottom output on"
# Quit: SIGTERM to run-wayland ends the program (its own trap sees it) and the compositor
printf '#!/bin/sh\ntrap "echo term > %s/term; exit 3" TERM\nsleep 30 &\nwait\n' "$T" > "$T/slow"; chmod +x "$T/slow"
run -- "$T/slow" & P=$!
sleep 3
# (the script itself, not the test's subshell around it)
kill -TERM "$(pgrep -f "run-wayland -- $T/slow" | head -n 1)"; wait $P; rc=$?
[ -f "$T/term" ] || bad "SIGTERM didn't reach the program"
[ $rc = 3 ] || bad "after SIGTERM the status was $rc"
[ $fail = 0 ] && echo "run-wayland: all passed"
exit $fail
