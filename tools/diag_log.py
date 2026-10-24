"""Boot log with diagnostics (release/normal image, no harness).

  python tools/diag_log.py --port COM14 --secs 100 [--tap 423,331@70] [--out boot.log]
Resets the panel, prints DIAG lines and key events with timestamps, and can
inject driver-level touches (DIAGTAP x y at a given second) to check the
whole touch path without the test harness.
"""
import argparse
import re
import sys
import time

import serial

KEYS = ("DIAG", "GT911", "Touch", "Activate", "Wi-Fi connected", "LoxAPP3", "Home categories", "Stationboard",
        "Weather ok", "MeteoSchweiz", "Calendar:", "Waste:", "Reminder", "Screen", "Wake", "App version", "Guru",
        "abort", "wdt", "WDT", "panic", "Settings:")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM14")
    ap.add_argument("--secs", type=int, default=100)
    ap.add_argument("--tap", action="append", default=[], help="x,y@second (or x1,y1,x2,y2,ms@second swipe)")
    ap.add_argument("--raw", action="append", default=[], help="rawx,rawy@second: GT911 coordinates (DIAGRAW)")
    ap.add_argument("--out", default="")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--noreset", action="store_true")
    a = ap.parse_args()
    if a.port.upper() == "COM5":
        sys.exit("COM5 is the 3D printer")
    taps = []
    for t in a.tap:
        pts, at = t.split("@")
        taps.append((float(at), [int(v) for v in pts.split(",")]))
    for t in a.raw:
        pts, at = t.split("@")
        taps.append((float(at), ["RAW"] + [int(v) for v in pts.split(",")]))
    p = serial.Serial(a.port, 115200, timeout=0.1)
    if not a.noreset:
        p.dtr = False
        p.rts = True
        time.sleep(0.1)
        p.rts = False
    t0 = time.time()
    buf = b""
    raw = b""
    while time.time() - t0 < a.secs:
        el = time.time() - t0
        for item in list(taps):
            if el >= item[0]:
                v = item[1]
                if v[0] == "RAW":
                    cmd = f"DIAGRAW {v[1]} {v[2]}\n"
                else:
                    cmd = f"DIAGTAP {v[0]} {v[1]}\n" if len(v) == 2 else f"DIAGSWIPE {v[0]} {v[1]} {v[2]} {v[3]} {v[4]}\n"
                p.write(cmd.encode())
                print(f"[{el:6.1f}] >> {cmd.strip()}")
                taps.remove(item)
        chunk = p.read(4096)
        raw += chunk
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            s = re.sub(r"\x1b\[[0-9;]*m", "", line.decode("utf-8", "replace")).rstrip()
            if a.all or any(k in s for k in KEYS) or s.startswith(("ui ", "net ", "IDLE", "diag", "wifi", "tiT", "ipc", "esp_timer", "sys_evt", "main", "Tmr")) or "\t" in s:
                print(f"[{time.time() - t0:6.1f}] {s[:200]}")
    if a.out:
        open(a.out, "wb").write(raw)


if __name__ == "__main__":
    main()
