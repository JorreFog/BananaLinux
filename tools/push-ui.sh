#!/bin/sh
# Banana's menu, iterated on the handheld without an image. A full update is a ~156 MB squashfs; this cross-builds
# banana-ui with the image's own toolchain (so it is the same binary the image ships), pushes the ~1.5 MB aarch64
# binary to the card, and runs it in the menu's place.
#
# How it swaps in: the root is a read-only squashfs, so /usr/bin/banana-ui cannot be written. inittab respawns
# /usr/bin/banana-ui-session, which execs /usr/bin/banana-ui; a small shim is bind-mounted over that path, and the
# shim execs /data/banana-ui. The shim never changes, so a later push only replaces /data/banana-ui (a rename over a
# running executable is fine) and restarts the menu -- no unmount, no write to the card's system.
#
#   tools/push-ui.sh                 cross-build banana-ui, push it, restart the menu into it
#   tools/push-ui.sh --no-build      skip the build: push the copy already in output/.../target
#   tools/push-ui.sh --revert        unmount the shim and restart the menu into the shipped binary
#
# The host defaults to $BANANA_HOST, else root@192.168.1.112; ssh options to $SSH_OPTS. Nothing on the card is
# written (only /data), and a reboot restores the shipped menu too.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
BOARD=${BOARD:-retroid-pocket-classic}
OUT=${OUT:-$HERE/output}
BIN=$OUT/$BOARD/target/usr/bin/banana-ui
REMOTE=/data/banana-ui
SHIM=/data/banana-ui-shim
SSH_OPTS=${SSH_OPTS:-"-o BatchMode=yes -o ConnectTimeout=8 -o ServerAliveInterval=15"}
BUILD=1
REVERT=
while [ $# -gt 0 ]; do
    case "$1" in
    --no-build) BUILD= ;; --revert) REVERT=1 ;; -*) echo "unknown option $1" >&2; exit 2 ;; *) HOST=$1 ;;
    esac
    shift
done
HOST=${HOST:-${BANANA_HOST:-root@192.168.1.112}}
run() { ssh $SSH_OPTS "$HOST" "$@"; }
restart() { run '
    old=$(pidof banana-ui)
    for p in $old; do kill -TERM "$p"; done
    i=0; while [ $i -lt 40 ] && pidof banana-ui >/dev/null; do sleep 0.25; i=$((i+1)); done    # wait for it to exit
    i=0; while [ $i -lt 40 ] && ! pidof banana-ui >/dev/null; do sleep 0.5; i=$((i+1)); done   # then for inittab to start it
    p=$(pidof banana-ui)
    if [ -n "$p" ] && [ "$p" != "$old" ]; then echo "menu back (pid $p)"; else echo "the menu has not come back (see /data/banana/logs/banana-ui.log)"; fi
' ; }

if [ -n "$REVERT" ]; then
    run "if grep -q ' /usr/bin/banana-ui ' /proc/mounts; then umount /usr/bin/banana-ui && echo 'shim unmounted'; else echo 'no shim mounted'; fi"
    restart
    echo "the shipped banana-ui runs again"
    exit 0
fi

if [ -n "$BUILD" ]; then
    echo "== building banana-ui for $BOARD (cross, ccache)"
    make -C "$OUT/$BOARD" banana-ui-rebuild
fi
[ -f "$BIN" ] || { echo "no $BIN -- build the image first (make $BOARD), or run without --no-build" >&2; exit 1; }

size=$(wc -c < "$BIN")
echo "== pushing $((size / 1024)) KiB to $HOST:$REMOTE"
run "cat > $REMOTE.new" < "$BIN"
run "mv -f $REMOTE.new $REMOTE; chmod 755 $REMOTE"
local=$(sha256sum "$BIN" | cut -d' ' -f1)
remote=$(run "sha256sum $REMOTE" | cut -d' ' -f1)
[ "$local" = "$remote" ] || { echo "the pushed binary is not the one built" >&2; exit 1; }
echo "   sha256 $local"

# the shim, once: it is what /usr/bin/banana-ui becomes
if ! run "[ -f $SHIM ]"; then
    echo "== installing the shim over /usr/bin/banana-ui"
    run "cat > $SHIM && chmod 755 $SHIM" <<EOF
#!/bin/sh
exec $REMOTE "\$@"
EOF
fi
# (re)bind it: the shim is a stable file (the running menu exec'd away from it), so umounting it is never busy
run "grep -q ' /usr/bin/banana-ui ' /proc/mounts && umount /usr/bin/banana-ui 2>/dev/null; mount --bind $SHIM /usr/bin/banana-ui"

echo "== restarting the menu into the dev binary"
restart
echo
echo "the dev banana-ui is up. log:  ssh $HOST 'tail -f /data/banana/logs/banana-ui.log'"
echo "shipped again:  tools/push-ui.sh --revert    (a reboot does it too)"
