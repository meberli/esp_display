"""OTA from the configured Update-URL (default: latest GitHub release).

  python tools/ota_url_test.py --port COM14
Needs the test firmware to start the update (Wartung > Update); the release
image itself has no harness, so the result is read from the boot log.
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from panel_test import Panel  # noqa: E402
from info_test import state, wait, tapn  # noqa: E402

KEYS = ("App version", "Running partition", "Settings:", "Wi-Fi connected", "LoxAPP3", "Home categories",
        "Stationboard", "Weather ok", "MeteoSchweiz", "Calendar:", "Waste:", "Reminder", "rollback", "Not configured",
        "not configured", "Guru", "abort", "panic")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM14")
    a = ap.parse_args()
    if a.port.upper() == "COM5":
        sys.exit("COM5 is the 3D printer")
    p = Panel(a.port)
    wait(p, lambda s: "STATE home" in s, 60)
    tapn(p, "cat:settings")
    tapn(p, "set:maint")
    tapn(p, "maint:ota")
    t0 = time.time()
    last = -2
    while time.time() - t0 < 300:
        try:
            s = state(p)
        except Exception:
            break  # rebooting
        m = re.search(r"ota=(-?\d+)", s)
        pct = int(m.group(1)) if m else -1
        if pct != last and pct % 20 == 0:
            print("progress", pct)
        last = pct
        if pct < 0 and time.time() - t0 > 15:
            break
        time.sleep(1)
    print(f"OTA phase ended after {time.time() - t0:.0f}s, reading boot log")
    p.s.baudrate = 115200  # release image: default console speed
    t1 = time.time()
    buf = b""
    while time.time() - t1 < 75:
        buf += p.s.read(4096)
    for line in buf.decode("utf-8", "replace").splitlines():
        if any(k in line for k in KEYS):
            print("  log:", re.sub(r"\x1b\[[0-9;]*m", "", line)[:160])


if __name__ == "__main__":
    main()
