#!/bin/sh
# Usage: sh system/tests/bananads-cheats-test.sh <BananaDS source> <melonDS source>
set -eu
HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
BANANADS=${1:?pass the BananaDS source directory containing src/cheats.cpp}
MELONDS=${2:?pass the melonDS source directory containing src/ARCodeFile.h}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
for name in 'Trauma Center - Under the Knife (USA)' 'Trauma Center - Under the Knife (Europe)' \
    'Dual Region (USA, Europe)' 'Dual Region (USA)'; do
    printf 'cheat0_desc = "Test code"\ncheat0_code = "00000000+00000001"\n' > "$T/$name.cht"
done
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I"$BANANADS/src" -I"$MELONDS/src" \
    "$HERE/bananads-cheats-test.cpp" "$BANANADS/src/cheats.cpp" -o "$T/test"
"$T/test" "$T"
