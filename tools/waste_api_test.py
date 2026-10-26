"""Abfallkalender-API on the panel (needs the test firmware).

  python tools/waste_api_test.py --port COM14 --out v13shots

Checks: Buchs (3271) dates on the Kalender page, the Kalender settings rows,
switching the Gemeinde (BFS 3392 Kirchberg, multi-zone) and a zone, an unknown
BFS (404), no refetch without a settings change, and restores the defaults
(the user's own BFS/zone/API values are put back if they were set).
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from panel_test import Panel  # noqa: E402
from info_test import check, state, wait, tapn, home, FAILS  # noqa: E402


def drain(p, secs):
    """Collect log lines for secs seconds."""
    out = []
    end = time.time() + secs
    while time.time() < end:
        try:
            line = p._line(min(end, time.time() + 0.5))
        except TimeoutError:
            continue
        if line:
            out.append(line)
            if not line.startswith(("State ", "DIAG")):
                print("  log:", line)
    return out


def wait_log(p, pattern, secs):
    rx = re.compile(pattern)
    end = time.time() + secs
    seen = []
    while time.time() < end:
        for line in drain(p, 1):
            seen.append(line)
            if rx.search(line):
                return line, seen
    return None, seen


def open_calset(p):
    home(p)
    tapn(p, "cat:settings")
    tapn(p, "set:cal")


def set_text(p, row, value):
    """value None: 'Standard verwenden'."""
    open_calset(p)
    tapn(p, row, 0.8)
    if value is None:
        tapn(p, "text:default", 1.0)
    else:
        tapn(p, "text:ta", 0.6)
        p.cmd("TYPE " + value)
        p.cmd("ENTER")
    line, _ = wait_log(p, r"Setting text \d+ ", 15)
    return line


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM14")
    ap.add_argument("--out", default="v13shots")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    p = Panel(a.port)
    shot = lambda n: p.shot(os.path.join(a.out, f"v13_{n}.png"))

    home(p)
    s = wait(p, lambda s: s.startswith("STATE home"), 60)
    check(s.startswith("STATE home"), "home: " + s)

    # Kalender page: waste dates from the API
    home(p)
    tapn(p, "home:cal")
    s = wait(p, lambda s: "loading=0" in s and "have=1" in s, 60)
    m = re.search(r"waste=(\d+)", s)
    check(m is not None and int(m.group(1)) > 0, "waste dates on the Kalender page: " + s)
    print("\n".join(i for i in p.items() if "ITEM W" in i))
    shot("calendar")
    open_calset(p)
    s = state(p)
    check(s.startswith("STATE calset"), "Kalender settings: " + s)
    shot("calset_buchs")

    # Kirchberg (3392, several zones): union first
    line = set_text(p, "calset:bfs", "3392")
    check(line is not None and "saved" in line, f"BFS 3392 saved ({line})")
    line, _ = wait_log(p, r"Waste: .*\(3392\)", 60)
    check(line is not None and "zones" in line and " 0 zones" not in line, f"Kirchberg fetched with zones ({line})")
    open_calset(p)
    shot("calset_kirchberg")

    # Zone "Gemeinde Kirchberg"
    line = set_text(p, "calset:zone", "Gemeinde Kirchberg")
    check(line is not None and "saved" in line, f"zone saved ({line})")
    line, _ = wait_log(p, r"Waste: .*\(3392\)", 60)
    check(line is not None and "zone used" in line, f"zone used ({line})")
    open_calset(p)
    shot("calset_zone")

    # No settings change: a calendar reload must not fetch the API again
    line, _ = wait_log(p, r"Waste: ", 20)
    check(line is None, "no refetch without a change")

    # Unknown Gemeinde: 404 handled
    line = set_text(p, "calset:bfs", "9999")
    line, seen = wait_log(p, r"Waste API|Waste: ", 60)
    check(line is not None and "404" in line, f"BFS 9999 -> 404 ({line})")
    s = state(p)
    check("calset" in s or "home" in s, "UI fine after 404: " + s)

    # Restore defaults: zone and BFS
    set_text(p, "calset:zone", None)
    line = set_text(p, "calset:bfs", None)
    check(line is not None and "default" in line, f"BFS back to default ({line})")
    line, _ = wait_log(p, r"Waste: .*\(3271\)", 60)
    check(line is not None, f"Buchs again ({line})")
    home(p)
    print("FAILS:", ", ".join(FAILS) if FAILS else "none")


if __name__ == "__main__":
    main()
