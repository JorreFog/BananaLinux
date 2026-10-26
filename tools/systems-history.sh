#!/bin/sh
# tools/systems-history.sh: system/rootfs/etc/banana/systems.history from git: every "id <TAB> key <TAB> value" that
# an earlier systems.conf of this branch shipped and the current one doesn't. systems-merge treats such a value on the
# card as the image's old default (a card copy made from an older systems.conf.default), not as the user's change, so
# the image's current value wins over it. Run it after changing systems.conf, and commit the result.
#   --check: only say whether the committed file is up to date (exit 1 if not; 0 outside a git checkout)
cd "$(dirname "$0")/.." || exit 1
CHECK=0; [ "$1" = --check ] && CHECK=1
git rev-parse --git-dir >/dev/null 2>&1 || { [ $CHECK = 1 ] && exit 0; echo "not a git checkout" >&2; exit 1; }
F=system/rootfs/etc/banana/systems.conf
norm() {  # file -> "id\tkey\tvalue" lines, read as systems-merge reads them
    awk 'function trim(s) { sub(/^[ \t\r]+/, "", s); sub(/[ \t\r]+$/, "", s); return s }
    { l = trim($0) } l == "" || substr(l, 1, 1) == "#" { next }
    substr(l, 1, 1) == "[" { id = tolower(trim(substr(l, 2, length(l) - 2))); next }
    id != "" && index(l, "=") { e = index(l, "="); k = tolower(trim(substr(l, 1, e - 1))); v = trim(substr(l, e + 1)); if (k != "" && v != "") printf "%s\t%s\t%s\n", id, k, v }'
}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
for c in $(git log --format=%H --follow -- $F); do git show "$c:$F" 2>/dev/null | norm; done | sort -u > "$T/all"
norm < $F | sort -u > "$T/now"
{
    echo "# The values earlier BananaOS systems.conf files shipped and this one doesn't (tools/systems-history.sh, from git):"
    echo "# systems-merge takes such a value on the card for the old default it is, so this image's value is used."
    echo "# id<TAB>key<TAB>value"
    comm -23 "$T/all" "$T/now"
} > "$T/hist"
if [ $CHECK = 1 ]; then
    cmp -s "$T/hist" system/rootfs/etc/banana/systems.history && exit 0
    echo "FAIL system/rootfs/etc/banana/systems.history is out of date: run tools/systems-history.sh"; exit 1
fi
cp "$T/hist" system/rootfs/etc/banana/systems.history
echo "$(grep -vc '^#' system/rootfs/etc/banana/systems.history) old values in system/rootfs/etc/banana/systems.history"
