#!/bin/sh
# File sharing's SFTP and SSH (system/rootfs/usr/libexec/banana: ssh, share, sftp-session) for real, in a Linux
# container (system/tests/sftp/Dockerfile: the image's dropbear, gesftpserver and busybox, built from Buildroot's
# sources): logins (root by key only, the share's name by password only, names the system has refused), what SFTP
# sees (the cards, the secrets empty, no way out), no shell and no port forwarding for the share's name, names with
# accents for SFTP 3 to 6, and the server following Settings (SSH, the share, its login, a new password, Guest).
# ksmbd is stubbed (it needs the kernel's module). Needs Docker; privileged (mounts, namespaces).
#   system/tests/sftp-test.sh
set -e
HERE=$(cd "$(dirname "$0")/../.." && pwd)
CTX=$(mktemp -d)
trap 'rm -rf "$CTX"' EXIT
cp "$HERE/system/tests/sftp/Dockerfile" "$HERE/buildroot/patches/gesftpserver/0001-filenames-are-utf-8.patch" \
    "$HERE/buildroot/board/rgds/busybox.fragment" "$CTX/"
docker build -q -t banana-sftp-test "$CTX" >/dev/null
mkdir -p "$CTX/t"
cp "$HERE/system/tests/sftp/inside.sh" "$CTX/t/"
cp "$HERE/system/rootfs/usr/libexec/banana/ssh" "$HERE/system/rootfs/usr/libexec/banana/share" \
    "$HERE/system/rootfs/usr/libexec/banana/sftp-session" "$CTX/t/"
cid=$(docker create --init --privileged banana-sftp-test timeout 300 /bin/bash /t/inside.sh)
docker cp "$CTX/t" "$cid:/t" >/dev/null
rc=0; docker start -a "$cid" || rc=$?
docker rm -f "$cid" >/dev/null
exit $rc
