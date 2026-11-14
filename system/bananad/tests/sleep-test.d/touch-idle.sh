# sleep-test.sh scenario: the touchscreen counts for the idle timers, the flight recorder on or off
# (sourced by sleep-test.sh, after its own: its helpers start, stop, ev, wake, asleep, ok and bad)

# tev <type> <code> <value>...: events on the stand-in touchscreen, with the monotonic time (as the real one stamps them)
tev() { python3 -c "
import struct, sys, time
t = time.clock_gettime(time.CLOCK_MONOTONIC); s = int(t); u = int((t - s) * 1e6)
a = [int(x) for x in sys.argv[1:]]
sys.stdout.buffer.write(b''.join(struct.pack('llHHi', s, u, a[i], a[i + 1], a[i + 2]) for i in range(0, len(a), 3)))" "$@" >&4; }
rm -f /run/banana/game.pid
for R in off on; do
    echo "touch-idle ($R). the menu used by touch alone, recorder $R: no dim while touched, and a touch undims"
    echo 200 > /sys/class/backlight/backlight1/brightness; echo 200 > /sys/class/backlight/backlight0/brightness
    start "dim_minutes = 0.02" "dim_percent = 30" "recorder = $R"      # 1.2 s
    for i in $(seq 10); do tev 3 57 $i  3 53 100  3 54 200  0 0 0; tev 3 57 -1  0 0 0; sleep 0.3; done
    b=$(cat /sys/class/backlight/backlight1/brightness)
    [ "$b" = 200 ] && ok "recorder $R: not dimmed while being touched" || bad "recorder $R: dimmed to $b while being touched"
    sleep 1.8
    b=$(cat /sys/class/backlight/backlight1/brightness)
    [ "$b" = 60 ] && ok "recorder $R: dimmed once the touches stopped" || bad "recorder $R: not dimmed when idle ($b)"
    tev 3 57 20  3 53 100  3 54 200  0 0 0; tev 3 57 -1  0 0 0; sleep 0.5
    b=$(cat /sys/class/backlight/backlight1/brightness)
    [ "$b" = 200 ] && ok "recorder $R: a touch undims" || bad "recorder $R: still $b after a touch"
    stop
done
