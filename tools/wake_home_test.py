"""After the screen goes off (screensaver / black), waking shows the home page.
Usage: python tools/wake_home_test.py COM14   (SCRTO is runtime only, restored to 0)
"""
import sys, time
sys.path.insert(0, "tools")
from panel_test import Panel
from info_test import state, wait, tapn, home

FAILS = []
def check(ok, what):
    print("PASS" if ok else "FAIL", what)
    if not ok: FAILS.append(what)

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM14"
    if port.upper() == "COM5": sys.exit("COM5 is the 3D printer")
    p = Panel(port)
    wait(p, lambda s: "STATE home" in s, 90)
    pages = [("settings", ["cat:settings", "set:disp"]), ("energie", ["home:energy"]), ("wetter", ["wx:now"]),
             ("licht", ["cat:licht"])]
    try:
        for name, taps in pages:
            home(p)
            for t in taps: tapn(p, t, 1.0)
            before = state(p)
            p.cmd("SCRTO 3")
            time.sleep(5.5)  # screen goes off after 3 s
            p.tap(240, 240)  # waking tap (swallowed)
            time.sleep(1.5)
            p.cmd("SCRTO 0")
            s = wait(p, lambda s: "STATE home" in s, 5)
            check("STATE home" in s, f"wake from {name} ({before[6:30]}) -> {s[6:30]}")
    finally:
        p.cmd("SCRTO 0")
    print("RESULT", "OK" if not FAILS else f"{len(FAILS)} FAIL")
    sys.exit(1 if FAILS else 0)

if __name__ == "__main__":
    main()
