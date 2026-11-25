#!/bin/sh
# Run against the unpacked ksmbd-tools source used by Buildroot, with BananaOS's patch applied.
# The real parser and file writer run without starting mountd or loading the kernel module.
set -eu

src=$(cd "${1:?usage: ksmbd-subauth-test.sh KSMBD_TOOLS_SOURCE}" && pwd)
tmp=$(mktemp -d)
trap 'chmod u+w "$tmp/etc/ksmbd"; rm -rf "$tmp"' EXIT
trap 'exit 1' HUP INT TERM
mkdir -p "$tmp/etc/ksmbd" "$tmp/data/banana"
chmod a-w "$tmp/etc/ksmbd"
if [ -w "$tmp/etc/ksmbd" ]; then
    echo "Run as an unprivileged user to test a read-only configuration directory" >&2
    exit 1
fi

cat > "$tmp/test.c" <<'TEST'
#include <stdio.h>
#include "config_parser.h"
#include "tools.h"

/* Select mountd's parser behavior without running its network and kernel setup. */
int mountd_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 1;
}

int addshare_main(int argc, char **argv) { return mountd_main(argc, argv); }
int adduser_main(int argc, char **argv) { return mountd_main(argc, argv); }
int control_main(int argc, char **argv) { return mountd_main(argc, argv); }

int main(void)
{
    tool_main = mountd_main;
    if (cp_parse_subauth()) {
        fputs("FAIL: mountd could not load or create its subauthority file\n", stderr);
        return 1;
    }

    for (size_t i = 0; i < ARRAY_SIZE(global_conf.gen_subauth); i++)
        printf("%u\n", global_conf.gen_subauth[i]);
    return 0;
}
TEST

# Drop code outside the parser and its file I/O. Link the unmodified upstream functions used on startup.
for file in config_parser tools; do
    ${CC:-cc} -std=gnu99 -ffunction-sections -fdata-sections -Dmain=ksmbd_tools_main \
        "-DSYSCONFDIR=\"$tmp/etc\"" "-DRUNSTATEDIR=\"$tmp/run\"" "-DBANANA_DATA_DIR=\"$tmp/data\"" \
        $(pkg-config --cflags glib-2.0 libnl-3.0) -I"$src/include" \
        -c "$src/tools/$file.c" -o "$tmp/$file.o"
done
${CC:-cc} -std=gnu99 -Wall -Wextra -Werror $(pkg-config --cflags glib-2.0) -I"$src/include" \
    -Wl,--gc-sections -o "$tmp/parse" "$tmp/test.c" "$tmp/config_parser.o" "$tmp/tools.o" $(pkg-config --libs glib-2.0)

"$tmp/parse" > "$tmp/first"
test -s "$tmp/data/banana/ksmbd.subauth"
test ! -e "$tmp/etc/ksmbd/ksmbd.subauth"
# A new process with no runtime state must reconstruct the identity from persistent data.
mkdir "$tmp/run"
"$tmp/parse" > "$tmp/second"
grep -Eq '^[0-9]+:[0-9]+:[0-9]+$' "$tmp/data/banana/ksmbd.subauth"
tail -n 3 "$tmp/first" | paste -sd ':' - | cmp - "$tmp/data/banana/ksmbd.subauth"
tail -n 3 "$tmp/first" | cmp - "$tmp/second"
echo "ksmbd: persistent subauthority created and reused with read-only configuration"
