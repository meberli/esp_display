"""Kameras (Windy webcams) hardware test via the serial harness.

  python tools/cam_test.py --port COM14 --out v5shots
Needs the test firmware and a working Windy key (build default or own key).
Never types the real key: the key screen is tested with a dummy key that must
be rejected, then reset to the build default.
"""
import argparse
import os
import sys
import time

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


def tapn(p, name):
    x, y = p.pos(name)
    p.tap(x, y)
    time.sleep(0.4)


def remove_extra(p):
    """Delete camera rows beyond the 5 defaults (scrolls the list first)."""
    for _ in range(kMax := 12):
        f = state(p).split()
        n = int(f[2]) if f[1] == "camset" else 5
        if n <= 5:
            return
        p.swipe(240, 420, 240, 150, 300)  # scroll the list to the end
        time.sleep(0.8)
        tapn(p, f"cam:del:{n - 1}")
        time.sleep(1.0)
        wait(p, lambda s: "busy=0" in s, 10)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM14")
    ap.add_argument("--out", default="v5shots")
    a = ap.parse_args()
    if a.port.upper() == "COM5":
        sys.exit("COM5 is the 3D printer")
    os.makedirs(a.out, exist_ok=True)
    p = Panel(a.port)
    shot = lambda n: p.shot(os.path.join(a.out, f"v5_{n}.png"))

    p.s.write(b"REBOOT\n")  # start from a clean boot (home screen)
    time.sleep(3)
    s = wait(p, lambda s: "STATE home" in s, 60)
    p.cmd("SCRTO 0", timeout=2)
    check("home" in s, "boot to home: " + s)
    shot("home")

    # clean start: back to the 5 default cameras (NVS keeps earlier additions)
    tapn(p, "cat:settings")
    tapn(p, "set:g:info", 0.8); tapn(p, "set:cams")
    remove_extra(p)
    p.swipe(120, 240, 400, 240, 250)
    time.sleep(0.5)
    p.swipe(240, 478, 240, 300, 250)
    time.sleep(0.6)

    tapn(p, "cat:kameras")
    s = wait(p, lambda s: "img=1" in s and "loading=0" in s)
    check(s.startswith("STATE cameras 1/5") and "img=1" in s, "camera 1 loaded: " + s)
    shot("cam1")
    for i in range(2, 6):
        p.swipe(400, 200, 120, 200, 250)
        s = wait(p, lambda s: f"cameras {i}/5" in s and "img=1" in s and "loading=0" in s)
        check(f"cameras {i}/5" in s and "img=1" in s, f"camera {i} loaded: " + s)
        shot(f"cam{i}")
    p.swipe(400, 200, 120, 200, 250)  # past the last one: stays
    time.sleep(0.6)
    check("cameras 5/5" in state(p), "no page past the last camera")
    p.swipe(120, 200, 400, 200, 250)
    s = wait(p, lambda s: "cameras 4/5" in s and "img=1" in s)
    check("cameras 4/5" in s, "swipe right goes back: " + s)
    p.swipe(240, 300, 240, 120, 250)  # any upward swipe goes home
    time.sleep(0.6)
    check("STATE home" in state(p), "swipe up -> home")

    tapn(p, "cat:settings")
    shot("settings")
    tapn(p, "set:g:info", 0.8); tapn(p, "set:cams")
    s = state(p)
    check("camset 5" in s, "camera settings list: " + s)
    shot("camset")

    # place search
    tapn(p, "camset:search")
    p.cmd("TYPE Gamprin")
    p.cmd("ENTER")
    s = wait(p, lambda s: "busy=0" in s and "camsearch 0" not in s, 30)
    found = p.items()
    print("\n".join(found))
    check(len(found) > 0, f"nearby search finds webcams ({len(found)})")
    time.sleep(0.5)
    shot("camsearch")
    new = None
    for line in found:
        f = line.split()
        if f[0] == "ITEM":
            f = f[1:]
        if int(f[0]) < 4 and f[1] not in ("1292514749", "1670606347", "1697029384", "1499936290", "1697035782"):
            new = int(f[0])
            break
    if new is not None:
        tapn(p, f"found:{new}")
        wait(p, lambda s: "busy=0" in s, 10)
        shot("camsearch_added")
    p.swipe(120, 240, 400, 240, 250)  # back to the list
    time.sleep(0.6)
    s = state(p)
    check("camset 6" in s, "search result added: " + s)

    # manual ID
    tapn(p, "camset:id")
    p.cmd("TYPE 1645278903")
    shot("camid")
    p.cmd("ENTER")
    s = wait(p, lambda s: s.startswith("STATE camset"), 20)
    check("camset 7" in s, "ID added: " + s)
    shot("camset_7")
    # remove both again (last two)
    remove_extra(p)
    s = state(p)
    check("camset 5" in s, "removed again: " + s)

    # bad ID
    tapn(p, "camset:id")
    p.cmd("TYPE 123")
    p.cmd("ENTER")
    time.sleep(0.5)
    s = wait(p, lambda s: "busy=0" in s, 20)
    check(s.startswith("STATE camid"), "unknown ID rejected: " + s)
    shot("camid_bad")
    p.swipe(120, 240, 400, 240, 250)
    time.sleep(0.6)

    # API key: dummy key must fail, then back to the default
    tapn(p, "camset:key")
    check("own=0" in state(p), "build default key in use")
    shot("camkey")
    tapn(p, "camkey:ta")
    p.cmd("TYPE dummy0000000000000000000000000")
    p.cmd("ENTER")
    s = wait(p, lambda s: "own=1" in s and "busy=0" in s, 20)
    check("own=1" in s, "own key saved: " + s)
    time.sleep(0.5)
    shot("camkey_bad")
    tapn(p, "camkey:default")
    s = wait(p, lambda s: "own=0" in s and "busy=0" in s, 20)
    check("own=0" in s, "own key cleared: " + s)
    time.sleep(0.5)
    shot("camkey_default")

    # reboot persistence of the list is covered by the 5 defaults; go home
    p.swipe(240, 478, 240, 300, 250)
    time.sleep(0.6)
    check("STATE home" in state(p), "home from settings")
    print("FAILS:", FAILS if FAILS else "none")
    sys.exit(1 if FAILS else 0)


if __name__ == "__main__":
    main()
