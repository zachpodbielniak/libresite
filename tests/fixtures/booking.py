#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""A small fake booking host for tests/booking.sh.

Serves DIR on 127.0.0.1:PORT. GET /book?format=json answers two open
times two days from now; POST /book answers 409 for the first (it was
just taken) and 201 for the second, and appends each request body to
LOG. With --empty the list of open times is empty. Nothing leaves the
machine and no real booking is made.
"""
import datetime
import functools
import http.server
import json
import sys


def slots():
    day = (datetime.date.today() + datetime.timedelta(days=2)).isoformat()
    return [
        {"start": f"{day}T14:00:00Z", "end": f"{day}T14:30:00Z", "label": "Slot one", "day": day},
        {"start": f"{day}T15:00:00Z", "end": f"{day}T15:30:00Z", "label": "Slot two", "day": day},
    ]


class Host(http.server.SimpleHTTPRequestHandler):
    log_path = ""
    empty = False

    def log_message(self, format, *args):
        pass

    def answer(self, status, body):
        data = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/book?format=json":
            self.answer(200, [] if self.empty else slots())
            return
        super().do_GET()

    def do_POST(self):
        if self.path != "/book":
            self.answer(404, {"error": "not found"})
            return
        length = int(self.headers.get("Content-Length", "0"))
        body = json.loads(self.rfile.read(length) or b"{}")
        with open(self.log_path, "a", encoding="utf-8") as log:
            log.write(json.dumps(body) + "\n")
        if body.get("start") == slots()[0]["start"]:
            self.answer(409, {"error": "taken"})
        else:
            self.answer(201, {"status": "booked"})


def main():
    if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help"):
        print("Usage: booking.py DIR PORT LOG [--empty]")
        return 0
    if len(sys.argv) < 4:
        print("Usage: booking.py DIR PORT LOG [--empty]", file=sys.stderr)
        return 2
    Host.log_path = sys.argv[3]
    Host.empty = "--empty" in sys.argv[4:]
    handler = functools.partial(Host, directory=sys.argv[1])
    http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[2])), handler).serve_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
