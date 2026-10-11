#!/bin/sh
# Buildroot post-build: what the root filesystem doesn't need on a handheld that boots to one program.
#   post-build.sh <target dir> <board: rgds | rgds-plus>
set -e
T=$1
# init's own scripts replace Buildroot's (rcS here starts everything)
rm -f "$T"/etc/init.d/S[0-9]*
# the data partition's mount point, and the boot partition's
mkdir -p "$T/data" "$T/run" "$T/boot"
# the version the settings screen's About shows
cp "$(dirname "$0")/../../../VERSION" "$T/etc/banana/version"
rm -rf "$T/usr/share/man" "$T/usr/share/doc" "$T/usr/share/info"
