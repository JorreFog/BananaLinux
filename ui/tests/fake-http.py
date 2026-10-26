#!/usr/bin/env python3
# A stand-in for GitHub's servers in #69's test: fake-http.py <dir> <port-file>. Serves <dir>'s files on 127.0.0.1 (a
# free port, written to <port-file>) with byte ranges (curl -C - resumes), slowly under /slow/ (the same files,
# 32 KB every 0.1 s: a download there takes long enough to be cancelled), and /limited as GitHub's answer once
# its 60 an hour are used up (403 with X-RateLimit-Reset), and under /auth/ only with the beta channel's login
# (HTTP Basic, beta:BetaTesting0.5.0; else 401, as the BananaOS site's .htaccess answers). Each request is a line in
# <port-file>.log (never the password).
import base64, http.server, os, re, sys, time

ROOT = os.path.abspath(sys.argv[1])

class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        with open(sys.argv[2] + '.log', 'a') as f:      # what was asked for (the test looks for a resumed range)
            f.write('%s %s\n' % (self.path, self.headers.get('Range', '-')))
        if self.path == '/limited':
            self.send_response(403)
            self.send_header('X-RateLimit-Limit', '60')
            self.send_header('X-RateLimit-Remaining', '0')
            self.send_header('X-RateLimit-Reset', str(int(time.time()) + 1800))
            self.send_header('Content-Length', '2')
            self.end_headers()
            self.wfile.write(b'{}')
            return
        rel = self.path.split('?')[0]
        if rel.startswith('/auth/'):
            if self.headers.get('Authorization', '') != 'Basic ' + base64.b64encode(b'beta:BetaTesting0.5.0').decode():
                self.send_response(401); self.send_header('WWW-Authenticate', 'Basic realm="BananaOS beta"')
                self.send_header('Content-Length', '0'); self.end_headers(); return
            rel = rel[5:]
        slow = rel.startswith('/slow/')
        rel = rel[6:] if slow else rel[1:]
        path = os.path.normpath(os.path.join(ROOT, rel))
        if not path.startswith(ROOT + os.sep) or not os.path.isfile(path):
            self.send_response(404); self.send_header('Content-Length', '0'); self.end_headers(); return
        size = os.path.getsize(path)
        start, end = 0, size - 1
        m = re.match(r'bytes=(\d+)-(\d*)$', self.headers.get('Range', ''))
        if m:
            start = int(m.group(1))
            if m.group(2): end = min(end, int(m.group(2)))
            if start >= size:
                self.send_response(416); self.send_header('Content-Range', 'bytes */%d' % size); self.send_header('Content-Length', '0'); self.end_headers(); return
            self.send_response(206); self.send_header('Content-Range', 'bytes %d-%d/%d' % (start, end, size))
        else:
            self.send_response(200)
        self.send_header('Accept-Ranges', 'bytes')
        self.send_header('Content-Length', str(end - start + 1))
        self.send_header('ETag', '"fake"')
        self.end_headers()
        with open(path, 'rb') as f:
            f.seek(start)
            left = end - start + 1
            while left > 0:
                b = f.read(min(32768 if slow else 65536, left))
                if not b: break
                try:
                    self.wfile.write(b); self.wfile.flush()
                except (BrokenPipeError, ConnectionResetError):
                    return
                left -= len(b)
                if slow: time.sleep(0.1)

s = http.server.ThreadingHTTPServer(('127.0.0.1', 0), H)
with open(sys.argv[2] + '.new', 'w') as f:
    f.write(str(s.server_address[1]))
os.rename(sys.argv[2] + '.new', sys.argv[2])
s.serve_forever()
