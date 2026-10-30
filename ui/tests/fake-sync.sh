#!/bin/sh
# A stand-in for banana-sync in the headless test (Network > Home server): one Romhost found on the LAN, any code
# pairs, a sync is done at once. Each call's arguments go to $FAKE_SYNC_LOG
echo "$*" >> "$FAKE_SYNC_LOG"
conf=
while [ $# -gt 0 ]; do
    case $1 in
    --conf) conf=$2; shift 2 ;;
    --root|--settings|--run|--status|--auto) shift 2 ;;
    discover) printf 'ROMHOST\thttp://192.168.50.96:8092\tTest server\n'; exit 0 ;;
    pair) printf '{"backend": "romhost", "server": "%s", "token": "rh1_test", "serverName": "Test server"}\n' "$2" > "$conf"; exit 0 ;;
    *) shift ;;
    esac
done
