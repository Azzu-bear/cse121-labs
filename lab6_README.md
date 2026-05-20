# CSE121 Lab 6 — Weather Station

Author: Azam Mohamed

The ESP32-C3 acts as a weather station that talks to wttr.in and a small
Python server running on a laptop/Pi/phone over the same WiFi network.

---

## Before you build any of the three parts

Open `main/main.c` and edit the macros at the top:

```c
#define WIFI_SSID    "YOUR_WIFI_SSID"
#define WIFI_PASS    "YOUR_WIFI_PASSWORD"
#define SERVER_IP    "192.168.1.100"   // for 6.2 and 6.3 only
```

For 6.1 you can also change `WEATHER_CITY` (use `+` for spaces, leave it
blank for wttr.in's auto-detect).

To find your server's IP on macOS / Linux:

```bash
ifconfig | grep "inet "
```

It will be the one in the same subnet as the ESP32 (usually `192.168.x.x`).

---

## Build / flash / monitor (same for each part)

```bash
. $HOME/esp/esp-idf/export.sh
cd lab6_1                              # or lab6_2 / lab6_3
rm -rf build
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/tty.usbmodem1412301 flash monitor
```

(Adjust the port — `ls /dev/cu.*` or `ls /dev/tty.usb*` to find it.)

---

## Lab 6.1 — Get the weather

Just flashes and prints. Output looks like:

```
I (5234) lab6_1: got ip: 192.168.1.42
Weather: Santa Cruz: +17°C
```

Trick used: `wttr.in/Santa+Cruz?format=%l:+%t&m`
- `%l` = location
- `%t` = temperature
- `&m` = force metric (Celsius)
- `User-Agent: curl/...` so wttr.in returns plain text, not the HTML page

---

## Lab 6.2 — Post results

1. Find your laptop's IP, put it in `SERVER_IP` in `lab6_2/main/main.c`.
2. Start the server on the laptop / Pi:
   ```bash
   cd lab6_server
   python3 server.py
   ```
3. Build & flash the ESP32.

The ESP32 reads its onboard temperature sensor every 5 seconds and POSTs
JSON like `{"sensor_temp_c": 32.4}` to `http://SERVER_IP:1234/temp`.

The server prints each POST it receives.

---

## Lab 6.3 — Integrate both

Same setup as 6.2 but the ESP32 now does a full loop every 30 seconds:

1. `GET http://SERVER_IP:1234/location` — server returns its configured
   location (e.g. "Santa Cruz")
2. `GET http://wttr.in/Santa+Cruz?format=%t&m` — outdoor temperature
3. Read internal temperature sensor
4. `POST http://SERVER_IP:1234/report` with all three values as JSON
5. Print all three over serial

Both server and ESP32 log the same trio: server location, outdoor temp
from wttr.in, and ESP32 sensor temp.

Quick test that the server's GET route works (independent of the ESP32):

```bash
wget -qO- http://SERVER_IP:1234/location
# -> Santa Cruz
```

---

## Files

```
lab6_1/        # GET weather from wttr.in
lab6_2/        # POST onboard sensor temp to server
lab6_3/        # GET location -> GET wttr.in -> POST both temps
lab6_server/   # python3 server.py — used by 6.2 and 6.3
```
