# sleep-test.sh scenario: sleep requests that must not outlive the sleep, or the failed one
# (sourced by sleep-test.sh, after its own: its helpers start, stop, ev, wake, asleep, ok and bad)
sr_asleep_in() { for i in $(seq $(($1 * 10))); do [ "$(cat /proc/$BD/wchan 2>/dev/null)" = wait_for_partner ] && return 0; sleep 0.1; done; return 1; }

echo "sleep-requests 1. the menu's Sleep, then the lid shut at once: one sleep, and awake when the lid opens"
# (the menu's request is half a second on; the lid's sleep came first and the request stayed: the handheld went back
# to sleep the moment it was opened, and that sleep the lid doesn't end)
rm -f /run/banana/game.pid /run/banana/transfer.pid
start
: > /run/banana/sleep-now; kill -HUP $BD; sleep 0.15
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1
if asleep; then ok "asleep (the lid)"; echo 0 > /run/test/lid; wake; else bad "no sleep"; echo 0 > /run/test/lid; fi
if sr_asleep_in 2; then bad "asleep again right after the lid opened (the menu's Sleep from before)"; wake; else ok "awake after the lid opened"; fi
[ "$(grep -c "sleep (the menu's Sleep)" /data/banana/logs/sleep.log)" = 0 ] && ok "no second sleep logged" || bad "logged: $(grep 'sleep (' /data/banana/logs/sleep.log | tail -n 2 | tr '\n' '|')"
# ...and the menu's Sleep on its own still sleeps
: > /run/banana/sleep-now; kill -HUP $BD
if sr_asleep_in 3; then ok "the menu's Sleep alone: asleep"; wake; else bad "the menu's Sleep alone: no sleep"; fi
stop

echo "sleep-requests 2. a sleep that failed with the lid open doesn't stop a later \"lid still closed\" sleep"
# (the power button's sleep fails three times; from then on a lid shut during a download stayed dark and awake after
# the download was done, until the battery guard)
: > /data/banana/logs/sleep.log
rm -f /sys/power/state; mkdir /sys/power/state      # (a directory can't be written: the suspend fails)
start
ev $EV_KEY $KEY_POWER 1; ev $EV_KEY $KEY_POWER 0; sleep 2
grep -q 'suspend failed' /data/banana/logs/sleep.log && ok "the power button's sleep failed" || bad "(the test itself: no failed suspend)"
rmdir /sys/power/state; mkfifo /sys/power/state     # suspends work again
echo "[s2idle] deep" > /sys/power/mem_sleep
setsid sleep 600 & SRD=$!; sleep 0.1; echo $SRD > /run/banana/transfer.pid
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1; sleep 1
grep -q 'lid closed during a download' /data/banana/logs/sleep.log && ok "the lid shut during a download: dark and awake" || bad "not held for the download"
kill $SRD; wait $SRD 2>/dev/null || true
# (woken only when it sleeps: nothing would answer the wake otherwise, and the test would wait for good)
if sr_asleep_in 9; then ok "asleep once the download is done"; echo 0 > /run/test/lid; wake; else bad "still awake after the download (lid shut)"; echo 0 > /run/test/lid; fi
rm -f /run/banana/transfer.pid
stop
