#!/usr/bin/env python3
"""
Lab 6 server — runs on your laptop / Pi / phone on the same WiFi.

Handles:
  POST /temp     -> Lab 6.2: body has sensor_temp_c
  GET  /location -> Lab 6.3: returns the configured LOCATION
  POST /report   -> Lab 6.3: body has location, outdoor_temp_c, sensor_temp_c

Run:
    python3 server.py
The script binds to 0.0.0.0:1234. The ESP32 should POST/GET to your
laptop's IP on port 1234.
"""

import json
from http.server import BaseHTTPRequestHandler, HTTPServer

# Change this to your actual location. wttr.in accepts city names, zip codes,
# airport codes, or even GPS coords.
LOCATION = "Santa Cruz"
PORT = 1234


class Handler(BaseHTTPRequestHandler):
    # ---- GET /location ----
    def do_GET(self):
        if self.path == "/location":
            body = LOCATION.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            print(f"[GET /location] -> {LOCATION}")
        else:
            self.send_response(404)
            self.end_headers()

    # ---- POST /temp or /report ----
    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length).decode("utf-8", errors="replace")

        try:
            data = json.loads(raw)
        except json.JSONDecodeError:
            data = {"_raw": raw}

        if self.path == "/temp":
            t = data.get("sensor_temp_c")
            print(f"[POST /temp] sensor_temp_c = {t}")

        elif self.path == "/report":
            loc = data.get("location")
            outdoor = data.get("outdoor_temp_c")
            sensor = data.get("sensor_temp_c")
            print("[POST /report]")
            print(f"    server location:    {loc}")
            print(f"    outdoor (wttr.in):  {outdoor} C")
            print(f"    ESP32 sensor temp:  {sensor} C")

        else:
            print(f"[POST {self.path}] body = {raw}")

        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(b"ok\n")

    # quieter default logs (the prints above are enough)
    def log_message(self, fmt, *args):
        return


def main():
    server = HTTPServer(("0.0.0.0", PORT), Handler)
    print(f"Weather station server listening on 0.0.0.0:{PORT}")
    print(f"Configured LOCATION: {LOCATION}")
    print("Routes:")
    print("    GET  /location")
    print("    POST /temp    (lab 6.2)")
    print("    POST /report  (lab 6.3)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nshutting down")
        server.server_close()


if __name__ == "__main__":
    main()
