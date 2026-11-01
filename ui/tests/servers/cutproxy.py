#!/usr/bin/env python3
# A TCP proxy for fetch-servers.sh: each listen port on 127.0.0.1 goes on to the same (or the given) port of host.
# SIGUSR1 cuts every connection going through it, as a NAS restarting or Wi-Fi dropping does; the listeners stay, so
# the connection made again goes through. It says how many it cut on stderr. A port it can't listen on (taken: an
# FTP server's passive ports mapped on this computer) is left out, and FTP's data goes past it then.
#   cutproxy.py <host> <listen port>[:<port>]...
import signal
import socket
import sys
import threading


def main():
    host = sys.argv[1]
    live = []
    lock = threading.Lock()

    def cut(*_):
        with lock:
            n = len(live) // 2
            for s in live:
                try:
                    s.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
            live.clear()
        print('cutproxy: cut %d connections' % n, file=sys.stderr, flush=True)

    def pipe(a, b):
        try:
            while True:
                d = a.recv(65536)
                if not d:
                    break
                b.sendall(d)
        except OSError:
            pass
        for s in (a, b):
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        with lock:
            live[:] = [s for s in live if s is not a and s is not b]

    def serve(listener, port):
        while True:
            c, _ = listener.accept()
            try:
                u = socket.create_connection((host, port))
            except OSError:
                c.close()
                continue
            with lock:
                live.extend([c, u])
            threading.Thread(target=pipe, args=(c, u), daemon=True).start()
            threading.Thread(target=pipe, args=(u, c), daemon=True).start()

    signal.signal(signal.SIGUSR1, cut)
    for spec in sys.argv[2:]:
        listen, _, port = spec.partition(':')
        s = socket.socket()
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.bind(('127.0.0.1', int(listen)))
        except OSError as e:
            print('cutproxy: %s: %s' % (listen, e), file=sys.stderr, flush=True)
            continue
        s.listen(16)
        threading.Thread(target=serve, args=(s, int(port or listen)), daemon=True).start()
    print('cutproxy: ready', file=sys.stderr, flush=True)
    while True:
        signal.pause()


main()
