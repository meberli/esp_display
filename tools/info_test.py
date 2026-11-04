"""ÖV, Kalender, MeteoSchweiz and Wartung hardware test via the serial harness.

  python tools/info_test.py --port COM14 --out v6shots [--web]
Needs the test firmware. Never prints calendar titles (private): only counts.
--web also switches the maintenance web server on and checks /status and
/backup from this PC (same LAN), then switches it off again.
"""
import argparse
import json
import os
import re
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(__file__))
from panel_test import Panel  # noqa: E402

FAILS = []


def check(cond, what):
    print(("PASS " if cond else "FAIL ") + what)
    if not cond:
        FAILS.append(what)


def state(p):
    return p.cmd("STATE", expect=("STATE",), timeout=3)


def wait(p, pred, timeout=40, step=0.5):
    t0 = time.time()
    s = ""
    while time.time() - t0 < timeout:
        s = state(p)
        if pred(s):
            return s
        time.sleep(step)
    return s


def tapn(p, name, settle=0.5):
    x, y = p.pos(name)
    p.tap(x, y)
    time.sleep(settle)


def home(p):
    p.swipe(240, 478, 240, 300, 250)
    time.sleep(0.7)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM14")
    ap.add_argument("--out", default="v6shots")
    ap.add_argument("--web", action="store_true")
    ap.add_argument("--only", default="oev,cal,rain,maint")
    a = ap.parse_args()
    if a.port.upper() == "COM5":
        sys.exit("COM5 is the 3D printer")
    os.makedirs(a.out, exist_ok=True)
    p = Panel(a.port)
    shot = lambda n: p.shot(os.path.join(a.out, f"v6_{n}.png"))
    only = a.only.split(",")

    p.s.write(b"REBOOT\n")
    time.sleep(3)
    s = wait(p, lambda s: "STATE home" in s, 60)
    p.cmd("SCRTO 0", timeout=2)
    check("home" in s, "boot to home: " + s)
    time.sleep(20)  # first transit / MeteoSchweiz / calendar fetches
    shot("home")

    if "oev" in only:
        tapn(p, "home:oev")
        s = wait(p, lambda s: "transit 1/2" in s and "loading=0" in s and "ok=1" in s, 30)
        check("transit 1/2" in s and "ok=1" in s, "ÖV board 1: " + s)
        n = int(re.search(r"n=(\d+)", s).group(1)) if "n=" in s else 0
        check(n > 0, f"departures from station 1 ({n})")
        print("\n".join(p.items()[:6]))
        shot("oev1")
        p.swipe(400, 240, 120, 240, 250)
        s = wait(p, lambda s: "transit 2/2" in s and "loading=0" in s and "ok=1" in s, 30)
        check("transit 2/2" in s and "ok=1" in s, "ÖV board 2 (swipe left): " + s)
        print("\n".join(p.items()[:6]))
        shot("oev2")
        p.swipe(120, 240, 400, 240, 250)
        time.sleep(0.6)
        check("transit 1/2" in state(p), "swipe right back to board 1")
        p.swipe(240, 300, 240, 100, 250)
        time.sleep(0.6)
        check("STATE home" in state(p), "swipe up -> home")
        # settings: station search (search only, keep the defaults)
        tapn(p, "cat:settings")
        shot("settings")
        tapn(p, "set:g:info", 0.8); tapn(p, "set:oev")
        s = state(p)
        check("trset 8509404|8587407" in s, "default stations Buchs SG / Räfis, Schulhaus: " + s)
        shot("oevset")
        tapn(p, "trset:1")
        p.cmd("TYPE Sevelen")
        p.cmd("ENTER")
        s = wait(p, lambda s: "busy=0" in s and "stsearch 0" not in s, 20)
        items = p.items()
        print("\n".join(items[:5]))
        check(len(items) > 0, f"station search finds stops ({len(items)})")
        time.sleep(0.4)
        shot("stsearch")
        p.swipe(120, 240, 400, 240, 250)  # back without choosing
        time.sleep(0.6)
        check("trset 8509404|8587407" in state(p), "stations unchanged after cancel")
        home(p)

    if "cal" in only:
        tapn(p, "home:cal")
        s = wait(p, lambda s: "calendar" in s and "have=1" in s, 60)
        check("have=1" in s, "calendar loaded: " + s)
        m = re.search(r"events=(\d+) waste=(\d+)", s)
        ev, ws = (int(m.group(1)), int(m.group(2))) if m else (0, 0)
        check(ev > 0, f"family events ({ev})")
        check(ws > 0, f"waste dates ({ws})")
        print("\n".join(i for i in p.items() if i.startswith("ITEM W")))
        shot("calendar")
        home(p)
        tapn(p, "cat:settings")
        tapn(p, "set:g:info", 0.8); tapn(p, "set:cal")
        shot("calset")
        tapn(p, "calset:ical")
        s = state(p)
        check(s.startswith("STATE text 0"), "iCal URL editor: " + s)
        shot("calset_ical")  # masked: host only
        p.swipe(120, 240, 400, 240, 250)
        time.sleep(0.5)
        home(p)

    if "rain" in only:
        # weather pager: Heute, then swipe right -> warnings + rain
        tapn(p, "day:0")
        time.sleep(0.5)
        p.swipe(120, 240, 400, 240, 250)
        s = wait(p, lambda s: "rain" in s and "ok=1" in s, 30)
        check("STATE rain" in s and "ok=1" in s, "rain / warnings page: " + s)
        print("\n".join(p.items()))
        shot("rain")
        p.swipe(400, 240, 120, 240, 250)
        time.sleep(0.6)
        check("weather 1/" in state(p), "swipe left back to Heute")
        home(p)
        try:
            x, y = p.pos("home:warn")
            p.tap(x, y)
            time.sleep(0.6)
            check("STATE rain" in state(p), "warning pill opens the rain page")
            home(p)
        except Exception:
            print("INFO no active warning pill")

    if "maint" in only:
        tapn(p, "cat:settings")
        tapn(p, "set:g:sys", 0.8); tapn(p, "set:maint")
        s = state(p)
        check(s.startswith("STATE maint web=0"), "Wartung (web off by default): " + s)
        time.sleep(1.2)
        shot("maint")
        if a.web:
            tapn(p, "maint:web")
            s = state(p)
            check("web=1" in s, "web server on: " + s)
            time.sleep(1.2)
            shot("maint_web")
            ip = None
            for _ in range(3):
                p.s.write(b"\n")
            # IP from the status label is not readable; ask the board over HTTP via mDNS-less discovery:
            ip = os.environ.get("PANEL_IP")
            if ip:
                st = json.load(urllib.request.urlopen(f"http://{ip}/status", timeout=5))
                print("status:", st)
                check("version" in st and st["heap_internal"] > 0, "GET /status")
                bk = json.load(urllib.request.urlopen(f"http://{ip}/backup", timeout=5))
                keys = bk.get("keys", {})
                check(bk.get("format") == "esp_display-settings", f"GET /backup ({len(keys)} keys)")
                check(all(k not in keys for k in ("wifi_pass", "windy_key", "ical_url")), "backup without secrets")
                with open(os.path.join(a.out, "backup.json"), "w") as f:
                    json.dump(bk, f, indent=1)
            tapn(p, "maint:web")
            check("web=0" in state(p), "web server off again")
        home(p)

    print("FAILS:", FAILS if FAILS else "none")
    sys.exit(1 if FAILS else 0)


if __name__ == "__main__":
    main()
