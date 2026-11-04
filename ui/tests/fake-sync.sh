#!/bin/sh
# A stand-in for banana-sync in the headless test (Network > Home server): one Romhost found on the LAN, any code
# pairs, a sync is done at once. Each call's arguments go to $FAKE_SYNC_LOG
echo "$*" >> "$FAKE_SYNC_LOG"
conf= run= when=
while [ $# -gt 0 ]; do
    case $1 in
    --conf) conf=$2; shift 2 ;;
    --run) run=$2; shift 2 ;;
    --auto) when=$2; shift 2 ;;
    --root|--settings|--status|--rom) shift 2 ;;
    discover) printf 'ROMHOST\thttp://192.168.50.96:8092\tTest server\n'; exit 0 ;;
    pair) printf '{"backend": "romhost", "server": "%s", "token": "rh1_test", "serverName": "Test server"}\n' "$2" > "$conf"; exit 0 ;;
    *) shift ;;
    esac
done
# the sync before a game, as $FAKE_SYNC_LAUNCH says: done at once, a conflict held, failed, the first answer and then
# nothing, nothing at all, or the saves held by another sync for 8 s (held) or for good (heldlong) and then nothing
[ "$when" = launch ] || exit 0
say() { echo "$1" > "$run/launch.status.new"; mv "$run/launch.status.new" "$run/launch.status"; }
say 'running 0 0 |Test server'
case ${FAKE_SYNC_LAUNCH:-answer} in
answer) say 'answered'; say 'running 1 1 Saves|game'; say 'done 0 0 0 1 0 0|' ;;
conflict) say 'answered'; say 'done 0 0 0 0 1 0|' ;;
fail) say 'answered'; say 'done 0 0 0 0 0 1|saves: broken'; exit 1 ;;
hang) say 'answered'; say 'running 1 1 Saves|game'; exec sleep 60 ;;
silent) : > "$run/sync-dl.$$"; : > "$run/sync-dl.$$.part"; exec sleep 60 ;;
held) say 'waiting 0 0 |saves'; sleep 8; say 'running 0 0 |Test server'; exec sleep 60 ;;
heldlong) say 'waiting 0 0 |saves'; exec sleep 60 ;;
esac
