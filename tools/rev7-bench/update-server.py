#!/usr/bin/env python3
"""a stand-in for github's release downloads, for net_update.c on the bench.

    update-server.py <this-host-ip> <port> <tag> <firmware.uf2> <firmware.uf2.sig>

needs srv.crt / srv.key in the current directory (any self-signed pair: the
board verifies no certificate):
    openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes \\
        -keyout srv.key -out srv.crt -days 30 -subj /CN=bench
firmware for it: cmake -DNET_UPDATE_HOST=<this-host-ip> -DNET_UPDATE_PORT=<port> -DNINEPIN_VERSION=N.N.N
"""
import http.server, ssl, sys, time
HOST, port, tag, uf2, sig = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4], sys.argv[5]
REPO = "/jusii/ninepin-ux"
LONG = "sp=r&sv=2018-11-09&sr=b&spr=https&se=2026-11-13T21%3A00%3A00Z&rscd=attachment%3B+filename%3Dninepin-ux.uf2&sig=" + "A" * 80 + "&jwt=" + "eyJ" + "x" * 900

class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, f, *a):
        sys.stderr.write("%s %s\n" % (time.strftime("%T"), f % a)); sys.stderr.flush()
    def redirect(self, where):
        self.send_response(302); self.send_header("Location", where)
        self.send_header("Content-Security-Policy", "default-src 'none'; " + "x" * 2500)
        self.send_header("Content-Length", "0"); self.send_header("Connection", "close"); self.end_headers()
    def do_GET(self):
        base = "https://%s:%d" % (HOST, port)
        if self.path == REPO + "/releases/latest/download/ninepin-ux.uf2":
            return self.redirect("%s%s/releases/download/%s/ninepin-ux.uf2" % (base, REPO, tag))
        for name, path in (("ninepin-ux.uf2.sig", sig), ("ninepin-ux.uf2", uf2)):
            if self.path == "%s/releases/download/%s/%s" % (REPO, tag, name):
                return self.redirect("%s/assets/%s?%s" % (base, name, LONG))
            if self.path.startswith("/assets/%s?" % name):
                data = open(path, "rb").read()
                self.send_response(200); self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Length", str(len(data))); self.send_header("Connection", "close"); self.end_headers()
                self.wfile.write(data); return
        self.send_response(404); self.send_header("Content-Length", "0"); self.send_header("Connection", "close"); self.end_headers()

srv = http.server.ThreadingHTTPServer(("0.0.0.0", port), H)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain("srv.crt", "srv.key")
srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
srv.serve_forever()
