#!/bin/sh
# A stand-in for /usr/libexec/banana/bluetooth in the headless test: three devices around, the paired and connected
# ones kept in $FAKE_BT (paired, connected: an address a line), every call logged in $FAKE_BT/log. The 8BitDo pad
# refuses to pair.
D=$FAKE_BT
mkdir -p "$D"; touch "$D/paired" "$D/connected"
echo "$*" >> "$D/log"
has() { grep -qx "$2" "$D/$1"; }
add() { has "$1" "$2" || echo "$2" >> "$D/$1"; }
del() { grep -vx "$2" "$D/$1" > "$D/$1.new"; mv "$D/$1.new" "$D/$1"; }
list() {
    for dev in "AA:BB:CC:DD:EE:01|audio|WH-1000XM4" "AA:BB:CC:DD:EE:02|gamepad|Wireless Controller" "AA:BB:CC:DD:EE:03|other|8BitDo Pro 2"; do
        mac=${dev%%|*}; rest=${dev#*|}; kind=${rest%%|*}; name=${rest#*|}
        p=0; c=0; has paired $mac && p=1; has connected $mac && c=1
        printf '%s\t%s\t%s\t%s\t%s\n' $mac $p $c $kind "$name"
    done
}
case $1 in
up|down) ;;
scan|devices) list ;;
pair)
    [ "$2" = AA:BB:CC:DD:EE:03 ] && { echo "Attempting to pair"; echo "ERROR Failed to pair: AuthenticationFailed"; exit 1; }
    add paired $2; add connected $2; echo OK ;;
connect) add connected $2; echo OK ;;
disconnect) del connected $2; echo OK ;;
forget) del paired $2; del connected $2; echo OK ;;
esac
