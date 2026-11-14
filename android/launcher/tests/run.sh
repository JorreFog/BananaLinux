#!/bin/sh
# BananaLauncher's tests on a PC (no Android needed): the launch table's reader and the tools' layer (the shell's
# syntax, the forked children's tools, the mapped paths). sh android/launcher/tests/run.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
ui=$here/../../../ui/src
out=${TMPDIR:-/tmp}/banana-launcher-tests
mkdir -p "$out"
${CXX:-c++} -std=c++17 -Wall -I"$ui" -o "$out/launch-test" "$here/launch-test.cpp" "$ui/android_launch.cpp"
"$out/launch-test" "$here"
${CXX:-c++} -std=c++17 -Wall -D__ANDROID__ -I"$here/host" -include "$here/host/funopen.h" -I"$ui" -o "$out/tools-test" \
	"$here/tools-test.cpp" "$ui/android_tools.cpp" -lpthread
"$out/tools-test"
