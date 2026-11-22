# File sharing's SFTP on Linux, as the image has it (system/tests/sftp-test.sh builds this): Buildroot 2026.02.3's
# dropbear (with crypt, as libxcrypt gives it), gesftpserver (--enable-daemon and Banana's patch, as external.mk and
# buildroot/patches build it) and busybox (Buildroot's configuration with Banana's fragment), with OpenSSH's clients.
FROM debian:trixie
RUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
        build-essential libcrypt-dev zlib1g-dev bzip2 curl ca-certificates openssh-client sshpass python3 >/dev/null
COPY . /src
WORKDIR /src
RUN get() { curl -fsSL -o "$2" "$1" && echo "$3  $2" | sha256sum -c - >/dev/null; } && \
    get https://matt.ucc.asn.au/dropbear/releases/dropbear-2026.91.tar.bz2 dropbear.tar.bz2 \
        defa924475abf6bc1e74abc00173e46bfdc804bd47caafa14f5a4ef0cc76da34 && \
    get http://www.greenend.org.uk/rjk/sftpserver/sftpserver-2.tar.gz sftpserver.tar.gz \
        b18b10405ef2b05c45450aafd6803f69efb4d2138e4e07fd186c1618027ee169 && \
    get https://busybox.net/downloads/busybox-1.37.0.tar.bz2 busybox.tar.bz2 \
        3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4 && \
    curl -fsSL -o busybox.config https://raw.githubusercontent.com/buildroot/buildroot/2026.02.3/package/busybox/busybox.config
RUN tar xjf dropbear.tar.bz2 && cd dropbear-2026.91 && ./configure -q --disable-harden >/dev/null && \
    make -s -j4 MULTI=1 PROGRAMS="dropbear dropbearkey dropbearconvert scp" >/dev/null 2>&1 && \
    install -m755 dropbearmulti /usr/sbin/ && for p in dropbear dropbearkey scp; do ln -sf dropbearmulti /usr/sbin/$p; done
RUN tar xzf sftpserver.tar.gz && cd sftpserver-2 && patch -p1 -s < ../0001-filenames-are-utf-8.patch && \
    CFLAGS="-O2 -std=c99" LIBS=-lpthread ./configure -q --disable-warnings-as-errors --enable-daemon rjk_cv_python3=false \
        --prefix=/usr --libexecdir=/usr/libexec >/dev/null && make -s -j4 >/dev/null 2>&1 && make -s install >/dev/null && \
    ln -sf gesftpserver /usr/libexec/sftp-server && cp sftpclient /usr/local/bin/gesftpclient
# (the fragment's lines replace the configuration's: busybox's kconfig keeps the first of two; and no x86 SHA code,
# which doesn't build here, nor tc)
RUN tar xjf busybox.tar.bz2 && cd busybox-1.37.0 && python3 - ../busybox.config ../busybox.fragment > .config <<'PY' && \
    yes "" | make -s oldconfig >/dev/null && make -s -j4 >/dev/null 2>&1 && install -m755 busybox /opt/busybox
import re, sys
base = open(sys.argv[1]).read().split("\n")
frag = [l for l in open(sys.argv[2]).read().split("\n") if l.startswith("CONFIG_")]
frag += ["# CONFIG_TC is not set", "# CONFIG_SHA1_HWACCEL is not set", "# CONFIG_SHA256_HWACCEL is not set"]
sym = lambda l: (re.match(r"(?:# )?(CONFIG_\w+)", l) or [None, None])[1]
drop = {sym(l) for l in frag}
print("\n".join([l for l in base if sym(l) not in drop] + frag))
PY
