#!/bin/sh
# How a save is put under the name an emulator reads, for the scripts that do it (gba-seed, saves-flat, nds-saves), who
# read this file in (. save-put.sh) so the three can't come to do it differently. The save is written beside its name
# as <name>.<tag>.<the run's number>.tmp, the card synced, then renamed over and the card synced again: a power cut
# leaves the old file or the new one, never a half-written save under the name the emulators read. The name ends .tmp,
# which the Home server's sync and the vault's listing never count as a save, and the tag is each script's own, so one
# script's file is never another's nor the sync's. A run cut short leaves its file behind: the next run clears it, by
# the run's number, and leaves one whose run is still going (a launcher's beside a sync's).

# stale_tmps <tag> <file>...: of those files (a pattern that matched nothing comes as itself, and is no file), each
# <name>.<tag>.<number>.tmp whose run has gone is removed
stale_tmps() {
    _tag=$1; shift
    for _t; do
        _p=${_t##*."$_tag".}; _p=${_p%.tmp}
        [ -f "$_t" ] && [ ! -d "/proc/$_p" ] && rm -f "$_t"
    done
    return 0
}
# put_synced <from> <to> <tag>: <from> copied (its times kept) to <to>, as above. Fails with <to> as it was
put_synced() {
    _t="$2.$3.$$.tmp"
    cp -p "$1" "$_t" && sync && mv -f "$_t" "$2" && sync && return 0
    rm -f "$_t"
    return 1
}
