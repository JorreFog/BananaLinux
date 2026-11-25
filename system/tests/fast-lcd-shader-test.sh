#!/bin/sh
# Usage: sh system/tests/fast-lcd-shader-test.sh <installed handheld/shaders/zfast_lcd.glsl>
# A GLES2 renderer that honors mediump is needed to reproduce the banding on the baseline.
set -eu
HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
SHADER=${1:?pass the installed RetroArch zfast_lcd.glsl shader}
T=$(mktemp)
trap 'rm -f "$T"' EXIT
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror $(pkg-config --cflags egl glesv2) \
    "$HERE/fast-lcd-shader-test.c" $(pkg-config --libs egl glesv2) -o "$T"
EGL_PLATFORM=${EGL_PLATFORM:-surfaceless} "$T" "$SHADER"
