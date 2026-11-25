#!/bin/sh
# Usage: sh system/tests/bananads-audio-test.sh <BananaDS source> <melonDS source>
set -eu
HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
BANANADS=${1:?pass the BananaDS source directory containing src/alsa.cpp}
MELONDS=${2:?pass the melonDS source directory containing src/Platform.h}
T=$(mktemp)
trap 'rm -f "$T"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread -ffunction-sections -fdata-sections \
    -I"$BANANADS/src" -isystem "$MELONDS/src" $(pkg-config --cflags alsa) \
    "$HERE/bananads-audio-test.cpp" "$BANANADS/src/stretch.cpp" \
    -Wl,--gc-sections,--wrap=clock_gettime,--wrap=snd_pcm_delay,--wrap=snd_pcm_writei \
    $(pkg-config --libs alsa) -o "$T"
"$T"
