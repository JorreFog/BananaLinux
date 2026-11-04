#!/bin/sh
# A stand-in for banana-sync in the headless test (Network > Home server): one Romhost found on the LAN, any code
# pairs, a sync is done at once. Each call's arguments go to $FAKE_SYNC_LOG
echo "$*" >> "$FAKE_SYNC_LOG"
# what banana-sync saves lists, as $FAKE_SYNC_SAVES says (the sync before a game leaves the same in launch.conflict)
lines() {
    case ${FAKE_SYNC_SAVES:-} in
        none) ;;
        server) printf 'held\t5/autosave\tsaves/gba/Orchard Tactics.srm\tmissing here since the last sync\t\t-1\tbb\t8192\tOther Handheld\t2026-11-01T10:00:00\t/v/x.srm\tserver\tgba/Orchard Tactics.gba\tgone\n' ;;
        format) printf 'held\t5/autosave\tsaves/gba/Orchard Tactics.srm\ta format this handheld doesn'"'"'t read\taa\t8192\tbb\t8192\tOther Handheld\t2026-11-01T10:00:00\t/v/x.zip\tnone\tgba/Orchard Tactics.gba\tformat\n' ;;
        ds) printf 'held\t5/autosave\tsaves/nds/Orchard Tactics.sav\tDraStic'"'"'s and BananaDS'"'"'s saves both changed\taa\t8192\t\t-1\t\t\t\tnone\tnds/Orchard Tactics.nds\tds\n' ;;
        unread) printf 'held\t5/autosave\tsaves/nds/Orchard Tactics.sav\tDraStic'"'"'s save can'"'"'t be read\taa\t8192\t\t-1\t\t\t\tnone\tnds/Orchard Tactics.nds\tunread\n' ;;
        queued) printf 'queued\t5/autosave\tsaves/gba/Orchard Tactics.srm\taa\t/v/q.srm\n' ;;
        twokept) grep -q 'resolve 5/autosave' "$FAKE_SYNC_LOG" || printf 'conflict\t5/autosave\tsaves/gba/Aaa.srm\t\taa\t8192\tbb\t8192\t\t\t/v/a.srm\tboth\tgba/Aaa.gba\tboth\n'
             printf 'conflict\t6/autosave\tsaves/gba/Bbb.srm\t\tcc\t8192\tdd\t8192\t\t\t/v/b.srm\tboth\tgba/Bbb.gba\tboth\nkept\tsaves/gba/Aaa.srm\t/v/a.srm\t8192\n' ;;
        two) grep -q 'resolve 5/autosave' "$FAKE_SYNC_LOG" || printf 'conflict\t5/autosave\tsaves/gba/Aaa.srm\t\taa\t8192\tbb\t8192\t\t\t/v/a.srm\tboth\tgba/Aaa.gba\tboth\n'
             printf 'conflict\t6/autosave\tsaves/gba/Bbb.srm\t\tcc\t8192\tdd\t8192\t\t\t/v/b.srm\tboth\tgba/Bbb.gba\tboth\n' ;;
        *) printf 'conflict\t5/autosave\tsaves/gba/Orchard Tactics.srm\t\taa\t8192\tbb\t8192\tOther Handheld\t2026-11-01T10:00:00\t/v/x.srm\tboth\tgba/Orchard Tactics.gba\tboth\nkept\tsaves/gba/Orchard Tactics.srm\t/v/20261101T100000Z-bb.srm\t8192\n' ;;
    esac
}
conf= run= when=
while [ $# -gt 0 ]; do
    case $1 in
    --conf) conf=$2; shift 2 ;;
    --run) run=$2; shift 2 ;;
    --auto) when=$2; shift 2 ;;
    --root|--settings|--status|--rom) shift 2 ;;
    discover) printf 'ROMHOST\thttp://192.168.50.96:8092\tTest server\n'; exit 0 ;;
    saves) lines; exit 0 ;;
    resolve) case ${FAKE_SYNC_RESOLVE:-} in fail) echo 'error no answer'; exit 1 ;; hang) exec sleep 60 ;; slow) sleep 2 ;; esac; echo ok; exit 0 ;;
    restore) echo ok; exit 0 ;;
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
conflict) say 'answered'; lines > "$run/launch.conflict"; say 'done 0 0 0 0 1 0|' ;;
olderhang) say 'answered'; say 'running 1 1 Saves|game'; : > "$run/launch.older"; exec sleep 60 ;;
older) say 'answered'; : > "$run/launch.older"; say 'done 0 0 0 0 0 1|saves: broken'; exit 1 ;;
fail) say 'answered'; say 'done 0 0 0 0 0 1|saves: broken'; exit 1 ;;
hang) say 'answered'; say 'running 1 1 Saves|game'; exec sleep 60 ;;
silent) : > "$run/sync-dl.$$"; : > "$run/sync-dl.$$.part"; exec sleep 60 ;;
held) say 'waiting 0 0 |saves'; sleep 8; say 'running 0 0 |Test server'; exec sleep 60 ;;
heldlong) say 'waiting 0 0 |saves'; exec sleep 60 ;;
esac
