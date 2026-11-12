#!/bin/sh
# usr/libexec/banana/run-wayland under a headless sway with two outputs: the program runs inside the compositor with
# Wayland's environment set, its exit status comes back, the compositor is gone after it, the configuration names the
# top output as the main one and has the second-screen rule; --keep-bottom powers the bottom output off; SIGTERM
# reaches the program, also when the launcher's whole process group gets it; --keyboard starts the keyboard on the
# bottom output. Needs sway and swaymsg; skipped otherwise.
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
# the menu's lease shim, preloaded into the compositor, doesn't reach the program in it; another preload does
# (the files aren't there: the loader says so for each process and goes on)
printf '#!/bin/sh\necho "[$LD_PRELOAD]" > "%s/preload"\n' "$T" > "$T/prog2"; chmod +x "$T/prog2"
LD_PRELOAD=/nonexistent/banana-leaseshim.so run -- "$T/prog2" 2>/dev/null
[ "$(cat "$T/preload" 2>/dev/null)" = "[]" ] || bad "the lease shim reached the program: $(cat "$T/preload" 2>/dev/null)"
LD_PRELOAD=/nonexistent/other.so run -- "$T/prog2" 2>/dev/null
[ "$(cat "$T/preload" 2>/dev/null)" = "[/nonexistent/other.so]" ] || bad "a launcher's own preload was dropped: $(cat "$T/preload" 2>/dev/null)"
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
# the menu's Quit: SIGTERM to the launcher's whole process group (the launcher a session leader, run-wayland its
# background job, as the launchers start it). The compositor is not in that group: the program saves first and its
# status comes back
printf '#!/bin/sh\ntrap "trap \\"\\" TERM; sleep 2; echo saved > %s/saved; exit 3" TERM\nsleep 30 &\nwait\n' "$T" > "$T/saves"; chmod +x "$T/saves"
{
    echo "echo \$\$ > '$T/lpid'"
    echo "sh '$W' -- '$T/saves' &"
    echo 'PID=$!'
    echo "trap 'kill -TERM \$PID 2>/dev/null' TERM INT HUP"
    echo 'wait $PID; RC=$?'
    echo 'while kill -0 $PID 2>/dev/null; do wait $PID; RC=$?; done'
    echo "echo \"\$RC \$(cat '$T/saved' 2>/dev/null)\" > '$T/lrc'"
} > "$T/launcher"
( export BANANA_RUN="$T/run" BANANA_DATA="$T/data" BANANA_HW_CONF=/nonexistent BANANA_WL_OUTPUTS="HEADLESS-1 HEADLESS-2" \
      WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=pixman LIBSEAT_BACKEND=noop
  setsid sh "$T/launcher" ) &
sleep 3
kill -TERM -"$(cat "$T/lpid")"
i=0; while [ ! -f "$T/lrc" ] && [ $i -lt 10 ]; do sleep 1; i=$((i + 1)); done
[ "$(cat "$T/lrc" 2>/dev/null)" = "3 saved" ] || bad "after SIGTERM to the launcher's group: '$(cat "$T/lrc" 2>/dev/null)' (want the program's status, after it saved)"
pgrep -f "sway -c $T/run/wl/sway.conf" >/dev/null && bad "the compositor is still running after the group's SIGTERM"
# --keyboard: the on-screen keyboard is started shown, with the bottom output focused (where the compositor puts a
# layer surface that names no output), and the top one is focused again when the program starts (a stand-in keyboard
# that writes down its arguments and the focused output)
mkdir "$T/bin"
printf '#!/bin/sh\necho "$*" > "%s/kbd.args"\nswaymsg -t get_outputs > "%s/kbd.json"\nsleep 5\n' "$T" "$T" > "$T/bin/wvkbd-mobintl"; chmod +x "$T/bin/wvkbd-mobintl"
focused() { python3 -I -c "
import json, sys
print(' '.join(x['name'] for x in json.load(open(sys.argv[1])) if x.get('focused')))" "$1" 2>/dev/null; }
rm -f "$T/outputs.json"
PATH=$T/bin:$PATH run --keyboard -- "$T/prog" x >/dev/null 2>&1
case $(cat "$T/kbd.args" 2>/dev/null) in *hidden*|"") bad "the keyboard wasn't started shown: '$(cat "$T/kbd.args" 2>/dev/null)'" ;; esac
[ "$(focused "$T/kbd.json")" = HEADLESS-2 ] || bad "the keyboard came up with '$(focused "$T/kbd.json")' focused, not the bottom output"
[ "$(focused "$T/outputs.json")" = HEADLESS-1 ] || bad "the program started with '$(focused "$T/outputs.json")' focused, not the top output"
grep -q wvkbd "$T/run/wl/sway.conf" && bad "a window rule for the keyboard (a layer surface: no rule applies to it)"
pgrep -x wvkbd-mobintl >/dev/null && bad "the keyboard is still running"
[ $fail = 0 ] && echo "run-wayland: all passed"
exit $fail
