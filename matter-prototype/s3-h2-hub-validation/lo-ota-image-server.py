#!/usr/bin/env python3
"""LED Orchestra offline OTA image server (host-side ingress).

Serves one Matter ``.ota`` image over **plain HTTP** on the control LAN so the
hub's OTA Provider can stream it to LED nodes over Thread — no internet, no DCL,
no cloud. This is the operator-laptop half of the offline OTA path today; the
Kubernetes control plane plays the exact same role later. Nothing in the firmware
changes between the two: the provider only ever sees an HTTP URL, and the only
thing that moves laptop -> K8s is the ``<http-uri>`` you pass to
``lo-ota-set-image``. Keep this host OFF the Matter fabric — it only delivers
image bytes.

Usage:
    ./lo-ota-image-server.py path/to/led_node.ota
    ./lo-ota-image-server.py led_node.ota --port 8070 \
        --sw-version 2 --version-string 0.2.0 --vendor-id 0xFFF1 --product-id 0x8001

With the metadata flags it prints a ready-to-paste ``lo-ota-set-image`` command;
without them it prints a template with the URL and byte size already filled in.

Dependency-free: standard library only (Python 3.8+).
"""

import argparse
import http.server
import os
import socket
import sys
import threading


def get_lan_ips():
    """Best-effort list of this host's non-loopback IPv4 addresses.

    Offline-friendly: the UDP "connect" never sends a packet, it just asks the
    kernel which local address would be used to reach a target on the LAN. We try
    a couple of common private targets (incl. the esp softAP gateway) plus the
    hostname so the operator can pick the one on the controller's subnet.
    """
    ips = []

    def add(ip):
        if ip and not ip.startswith("127.") and ip not in ips:
            ips.append(ip)

    for target in ("192.168.4.1", "192.168.1.1", "10.0.0.1"):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.connect((target, 9))
            add(s.getsockname()[0])
        except OSError:
            pass
        finally:
            s.close()

    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            add(info[4][0])
    except OSError:
        pass

    return ips


def make_handler(image_path, url_name):
    image_size = os.path.getsize(image_path)

    class OtaImageHandler(http.server.BaseHTTPRequestHandler):
        server_version = "lo-ota-image-server/1.0"

        def _serve(self, body):
            if self.path.lstrip("/") != url_name:
                self.send_error(404, "only /%s is served" % url_name)
                return
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(image_size))
            self.end_headers()
            if not body:
                return
            # Stream in chunks; TCP backpressure paces us to the BDX read rate.
            with open(image_path, "rb") as f:
                while True:
                    chunk = f.read(8192)
                    if not chunk:
                        break
                    try:
                        self.wfile.write(chunk)
                    except (BrokenPipeError, ConnectionResetError):
                        return

        def do_HEAD(self):
            self._serve(body=False)

        def do_GET(self):
            self._serve(body=True)

        def log_message(self, fmt, *args):
            sys.stderr.write("  [http] %s - %s\n" % (self.address_string(), fmt % args))

    return OtaImageHandler, image_size


def main():
    ap = argparse.ArgumentParser(description="Offline OTA image server for the LED Orchestra hub.")
    ap.add_argument("image", help="path to the Matter .ota image to serve")
    ap.add_argument("--port", type=int, default=8070, help="HTTP port (default 8070)")
    ap.add_argument("--bind", default="0.0.0.0", help="bind address (default 0.0.0.0)")
    ap.add_argument("--sw-version", help="image software version (uint) for the lo-ota-set-image hint")
    ap.add_argument("--version-string", help="image software version string for the hint")
    ap.add_argument("--vendor-id", help="requestor vendor id (e.g. 0xFFF1) for the hint")
    ap.add_argument("--product-id", help="requestor product id (e.g. 0x8001) for the hint")
    args = ap.parse_args()

    if not os.path.isfile(args.image):
        ap.error("image not found: %s" % args.image)

    url_name = os.path.basename(args.image)
    handler, image_size = make_handler(args.image, url_name)

    httpd = http.server.ThreadingHTTPServer((args.bind, args.port), handler)

    ips = get_lan_ips() or ["<this-host-lan-ip>"]
    primary_url = "http://%s:%d/%s" % (ips[0], args.port, url_name)

    print("LED Orchestra OTA image server")
    print("  image : %s (%d bytes)" % (args.image, image_size))
    print("  serving on these control-LAN URLs (pick the one on the controller's subnet):")
    for ip in ips:
        print("    http://%s:%d/%s" % (ip, args.port, url_name))
    print()

    sw = args.sw_version or "<sw-version>"
    ver = args.version_string or "<version-string>"
    vid = args.vendor_id or "<vendor-id>"
    pid = args.product_id or "<product-id>"
    print("On the controller console, stage it with:")
    print("  lo-ota-set-image %s %s %s %d %s %s" % (primary_url, sw, ver, image_size, vid, pid))
    print()
    print("Keep this host off the Matter fabric. Ctrl-C to stop.")
    sys.stdout.flush()

    t = threading.Thread(target=httpd.serve_forever, daemon=True)
    t.start()
    try:
        while True:
            t.join(1.0)
    except KeyboardInterrupt:
        print("\nstopping")
        httpd.shutdown()


if __name__ == "__main__":
    main()
