#!/usr/bin/env python3
"""
A release, as the watch's updater sees it, served from this machine.

    python3 tools/update_test_server.py --port 18393
    python3 tools/update_test_server.py --port 18393 --bin build/amoledos.bin --ver v0.18.0-prueba

It serves two manifests, each in the shape tools/gen_update.py writes:

    /good/amoledos-update.json   the image, its size and its real SHA-256
    /bad/amoledos-update.json    the same image with a SHA-256 that is not its

and the image itself behind two redirects, the way GitHub serves a release's
file: /dl/amoledos.bin -> 302 to /redir/amoledos.bin (a path) -> 302 to an
absolute address -> /files/amoledos.bin. Without --bin the image is 300 KB of
pseudo-random bytes (enough for tools/update_harness.c; the watch would refuse
it at its first write, which is its own check).

The addresses in a manifest are built from the Host the request came with,
so the same server answers the Mac (127.0.0.1) and the watch (the Mac's IP).
On the watch: Settings' source, or

    curl -X POST http://<watch>/api/update -d '{"source":"http://<mac>:18393/good/amoledos-update.json"}'
"""
import argparse, hashlib, http.server, json, os, random, socketserver

ap = argparse.ArgumentParser()
ap.add_argument('--port', type=int, default=18393)
ap.add_argument('--bind', default='0.0.0.0')
ap.add_argument('--bin', help='the image to offer (build/amoledos.bin)')
ap.add_argument('--ver', default='v99.0.0', help='the version the manifest says')
ap.add_argument('--abi', type=int, default=2)
args = ap.parse_args()

if args.bin:
    IMAGE = open(args.bin, 'rb').read()
else:
    rnd = random.Random(1234)
    IMAGE = bytes(rnd.getrandbits(8) for _ in range(300 * 1024))
SHA = hashlib.sha256(IMAGE).hexdigest()


def manifest(base, good):
    return json.dumps({
        'schema': 1, 'ver': args.ver, 'date': '2026-11-25', 'abi': args.abi,
        'url': base + '/dl/amoledos.bin', 'size': len(IMAGE),
        'sha256': SHA if good else '0' * 64,
        'notes_es': 'Prueba en español: ñandú, café.\nSegunda línea.',
        'notes_en': 'A test, in English.',
        'notes_de': 'Ein Test, auf Deutsch: Größe.',
    }, ensure_ascii=True).encode()


class H(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.0'

    def send(self, code, body=b'', ctype='application/octet-stream', loc=None):
        self.send_response(code)
        if loc:
            self.send_header('Location', loc)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        if self.command == 'GET':
            self.wfile.write(body)

    def do_GET(self):
        base = 'http://' + (self.headers.get('Host') or '127.0.0.1:%d' % args.port)
        p = self.path
        if p == '/good/amoledos-update.json':
            return self.send(200, manifest(base, True), 'application/json')
        if p == '/bad/amoledos-update.json':
            return self.send(200, manifest(base, False), 'application/json')
        if p == '/dl/amoledos.bin':
            return self.send(302, loc='/redir/amoledos.bin')
        if p == '/redir/amoledos.bin':
            return self.send(302, loc=base + '/files/amoledos.bin')
        if p in ('/files/amoledos.bin', '/good/amoledos.bin'):
            return self.send(200, IMAGE)
        return self.send(404, b'not here', 'text/plain')

    do_HEAD = do_GET

    def log_message(self, fmt, *a):
        print('%s %s' % (self.address_string(), fmt % a), flush=True)


class S(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


print('offering %s, %d B, sha256 %s, on %s:%d' % (args.ver, len(IMAGE), SHA, args.bind, args.port), flush=True)
S((args.bind, args.port), H).serve_forever()
