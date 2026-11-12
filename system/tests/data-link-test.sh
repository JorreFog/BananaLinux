#!/bin/sh
# usr/libexec/banana/data-link: a folder shown at a second place. A symbolic link where the file system takes one; a
# bind mount where it doesn't (the card's exFAT), which needs root: that half is skipped without it. What a program
# left at the place before is moved into the folder once; a read-only folder never empties the card's own.
HERE=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d)
trap 'for m in "$T"/b/*; do umount -l "$m" 2>/dev/null; done; umount -l "$T/ro" 2>/dev/null; rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL data-link: $*"; fail=1; }
. "$HERE/rootfs/usr/libexec/banana/data-link"
# a link: the program's old folder's files go into the folder, the place becomes the link
mkdir -p "$T/saves" "$T/emu/SAVEDATA/GAME1"; echo old > "$T/emu/SAVEDATA/GAME1/save"; echo dot > "$T/emu/SAVEDATA/.hidden"
link_dir "$T/saves" "$T/emu/SAVEDATA" || bad "link_dir failed"
[ -L "$T/emu/SAVEDATA" ] || bad "no link where one can be made"
[ "$(cat "$T/saves/GAME1/save" 2>/dev/null)" = old ] && [ -f "$T/saves/.hidden" ] || bad "the old folder's files weren't moved"
echo new > "$T/emu/SAVEDATA/GAME1/save2"; [ -f "$T/saves/GAME1/save2" ] || bad "a write through the link didn't land in the folder"
link_dir "$T/saves" "$T/emu/SAVEDATA" || bad "a second call failed"
[ -z "$BANANA_BOUND" ] || bad "a link was counted as a bind mount"
# a link that points elsewhere is put right
mkdir -p "$T/other"; ln -sfn "$T/other" "$T/emu/L"; link_dir "$T/saves" "$T/emu/L"
[ "$(readlink "$T/emu/L")" = "$T/saves" ] || bad "a link to another folder was kept"
# the bind mount (root only)
if [ "$(id -u)" = 0 ] && mkdir -p "$T/b/x" "$T/src" && mount --bind "$T/src" "$T/b/x" 2>/dev/null; then
    umount "$T/b/x"; rmdir "$T/b/x"
    export BANANA_NOLINK=1
    mkdir -p "$T/b/states"; echo s > "$T/b/states/a.state"; mkdir -p "$T/st"
    link_dir "$T/st" "$T/b/states" || bad "bind: link_dir failed"
    [ -L "$T/b/states" ] && bad "bind: a link was made"
    grep -q " $T/b/states " /proc/mounts || bad "bind: not mounted"
    [ -f "$T/st/a.state" ] || bad "bind: the old folder's files weren't moved"
    echo n > "$T/b/states/b.state"; [ -f "$T/st/b.state" ] || bad "bind: a write didn't land in the folder"
    link_dir "$T/st" "$T/b/states"; [ "$(grep -c " $T/b/states " /proc/mounts)" = 1 ] || bad "bind: mounted twice"
    # a read-only folder (the image's): bound; the card's own folder there, when it has files, is left as it is
    mkdir -p "$T/ro-src/shaders"; echo sh > "$T/ro-src/shaders/a"
    mount --bind "$T/ro-src" "$T/ro-src" && mount -o remount,ro,bind "$T/ro-src" && RO=1
    if [ -n "$RO" ]; then
        link_dir "$T/ro-src/shaders" "$T/b/shaders" || bad "bind: a read-only folder failed"
        [ -f "$T/b/shaders/a" ] || bad "bind: the read-only folder isn't shown"
        mkdir -p "$T/b/own"; echo mine > "$T/b/own/keep"
        link_dir "$T/ro-src/shaders" "$T/b/own"
        [ "$(cat "$T/b/own/keep" 2>/dev/null)" = mine ] || bad "bind: the card's own folder was emptied for a read-only one"
    fi
    unlink_dirs
    grep -q " $T/b/" /proc/mounts && bad "bind: still mounted after unlink_dirs"
    [ -f "$T/st/b.state" ] || bad "bind: the folder lost its files at the unmount"
    [ -n "$RO" ] && umount -l "$T/ro-src"
    unset BANANA_NOLINK
else
    echo "data-link: the bind-mount half skipped (not root, or no mount here)"
fi
[ $fail = 0 ] && echo "data-link: all checks passed"
exit $fail
