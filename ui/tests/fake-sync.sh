#!/bin/sh
# A stand-in for banana-sync in the headless test (Network > Home server): one Romhost found on the LAN, any code
# pairs, a sync is done at once. RomM at any address: probe --romm answers as $FAKE_SYNC_PROBE says (ok, old, or a
# failure: redirect, for an http:// address only, access, tls, dns), and pair --romm shows a code and is approved 2 s
# later (FAKE_SYNC_PAIR=deny: refused; hang: no answer; tls, redirect: the pairing's own probe failing; slowstop: a
# second to end once stopped; latepid: its sync.pid a second late), a code given pairs at once (FAKE_SYNC_FIND=romm: no Romhost
# found, and RomM at the address typed instead); games lists two GBA games, one on the card; a sync run by hand leaves
# its status as it is, or with FAKE_SYNC_DONE says it is done a second later. Each call's arguments go to $FAKE_SYNC_LOG
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
conf= run= when= status= gonly=
report=
while [ $# -gt 0 ]; do
    case $1 in
    --conf) conf=$2; shift 2 ;;
    --run) run=$2; shift 2 ;;
    --auto) when=$2; shift 2 ;;
    --status) status=$2; shift 2 ;;
    --report) report=$2; shift 2 ;;
    --root|--settings|--rom) shift 2 ;;
    --games-only) gonly='games only'; shift ;;
    discover) [ "${FAKE_SYNC_FIND:-}" = romm ] && exit 0; printf 'ROMHOST\thttp://192.168.50.96:8092\tTest server\n'; exit 0 ;;
    saves) lines; exit 0 ;;
    # the server's games for Get games: one on the card, one not
    games) printf 'gba\tgba/Orchard Tactics.gba\tOrchard Tactics\t1024\t0\t1\ngba\tgba/Pocket Hack.gba\tPocket Hack [Hack by Someone v1.0]\t16777216\t0\t0\n'; exit 0 ;;
    resolve) case ${FAKE_SYNC_RESOLVE:-} in fail) echo 'error no answer'; exit 1 ;; hang) exec sleep 60 ;; slow) sleep 2 ;; esac; echo ok; exit 0 ;;
    restore) echo ok; exit 0 ;;
    forget) rm -f "$conf"; exit 0 ;;
    # (rename: the name kept as the pairing's, as banana-sync does once RomM has it)
    rename) sed -i "s/\"name\": \"[^\"]*\"/\"name\": \"$2\"/" "$conf"; echo ok; exit 0 ;;
    probe)
        # (an address typed where Romhost's was asked for: RomM's there with FAKE_SYNC_FIND=romm)
        [ "$2" = --romm ] || { [ "${FAKE_SYNC_FIND:-}" = romm ] && { echo "romm http://$2 RomM 5.4.1"; exit 0; }; exit 1; }
        case ${FAKE_SYNC_PROBE:-ok} in
        ok) echo "romm $3 RomM 5.4.1" ;;
        old) echo "romm $3 RomM 5.3.0 old" ;;
        redirect) case $3 in http://*) echo "fail redirect https://${3#http://}/api/heartbeat"; exit 1 ;; *) echo "romm $3 RomM 5.4.1" ;; esac ;;
        access) echo "fail access https://team.cloudflareaccess.com/cdn-cgi/access/login/x"; exit 1 ;;
        *) echo "fail $FAKE_SYNC_PROBE"; exit 1 ;;
        esac
        exit 0 ;;
    pair)
        st() { [ -n "$status" ] && { echo "$1" > "$status.new"; mv "$status.new" "$status"; }; }
        if [ "$2" = --romm ]; then
            # (the version the probe gave, kept as banana-sync keeps it)
            v=5.4.1; [ "${FAKE_SYNC_PROBE:-ok}" = old ] && v=5.3.0
            romm() { printf '{"backend": "romm", "server": "%s", "token": "rmm_test", "serverName": "RomM %s", "serverVersion": "%s", "name": "RG DS"}\n' "$1" $v $v > "$conf"; st "paired RomM $v"; }
            [ -n "${4:-}" ] && { romm "$3"; exit 0; }
            st "pair ABCD-EFGH $3/pair/device?user_code=ABCD-EFGH 600"
            case ${FAKE_SYNC_PAIR:-approve} in
            hang) exec sleep 60 ;;
            # (the pairing's own probe failing on the certificate, or sent elsewhere, after the form's probe passed)
            tls) st "error tls SSL certificate problem: self-signed certificate"; exit 1 ;;
            redirect) st "error redirect https://${3#*://}/api/heartbeat"; exit 1 ;;
            # (run as banana-sync, its pid in sync.pid as banana-sync's own: a second to end once stopped, as a curl
            # waiting on a slow server takes; gone by itself after 10 s)
            slowstop) echo $$ > "$run/sync.pid"; trap 'sleep 1; st "error stopped"; exit 1' TERM
                for i in $(seq 100); do sleep 0.1; done; exit 1 ;;
            # (sync.pid written only a second in, as a banana-sync slow to start; approved a second after that)
            latepid) sleep 1; echo $$ > "$run/sync.pid"; sleep 1; romm "$3"; rm -f "$run/sync.pid"; exit 0 ;;
            deny) sleep 2; st "error refused in RomM"; exit 1 ;;
            esac
            sleep 2; romm "$3"; exit 0
        fi
        printf '{"backend": "romhost", "server": "%s", "token": "rh1_test", "serverName": "Test server"}\n' "$2" > "$conf"; st "paired Test server"; exit 0 ;;
    *) shift ;;
    esac
done
# the sync before a game, as $FAKE_SYNC_LAUNCH says: done at once, a conflict held, failed, the first answer and then
# nothing, nothing at all, or the saves held by another sync for 8 s (held) or for good (heldlong) and then nothing
# (FAKE_SYNC_DONE: a sync run by hand says it's done a second later, as banana-sync's own would, Get games' own saying so)
# (FAKE_SYNC_SILENT: one by hand that runs on without a word, no sync.pid, as banana-sync waiting for the lock)
[ -z "$when" ] && [ -n "${FAKE_SYNC_SILENT:-}" ] && [ -n "$status" ] && { sleep 30; exit 0; }
[ -z "$when" ] && [ -n "${FAKE_SYNC_DONE:-}" ] && [ -n "$status" ] && { sleep 1; echo "done 0 0 0 0 0 0|$gonly" > "$status.new"; mv "$status.new" "$status"; exit 0; }
# the sync after a game, as $FAKE_SYNC_QUIT says: the save sent (a second's run with slow), nothing to send, a conflict
# held, the server out of reach (the save kept to send later), a note with nothing sent, a game whose saves stay here,
# or a run gone with its "running" line left (stuck: the line three minutes old); held: that line as old, but the run
# still holding save.lock as a long upload does, its done after. Its lines go to the menu's --report file
if [ "$when" = quit ] && [ -n "$run" ]; then
    out=${report:-$run/gameend.status}
    end() { echo "$1" > "$out.new"; mv "$out.new" "$out"; }
    end 'running 0 0 |Test server'
    case ${FAKE_SYNC_QUIT:-sent} in
    sent) end 'done 0 0 1 0 0 0|' ;;
    slow) sleep 2; end 'done 0 0 1 0 0 0|' ;;
    same) end 'done 0 0 0 0 0 0|' ;;
    conflict) end 'done 0 0 0 0 1 0|' ;;
    away) end "done 0 0 0 0 0 1|saves: the server's name wasn't found (no connection yet?); kept to send with the next sync" ;;
    note) end 'done 0 0 0 0 0 0|1 state from the server kept in the vault' ;;
    local) end 'done 0 0 0 0 0 0|its saves stay on this handheld' ;;
    stuck) touch -d '3 minutes ago' "$out" ;;
    held) ( flock 9; touch -d '3 minutes ago' "$out"; sleep 2; end 'done 0 0 1 0 0 0|' ) 9> "$run/save.lock" ;;
    esac
    exit 0
fi
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
