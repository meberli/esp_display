"""Weather day view test (test firmware): open from home, realistic swipes
across the 7 days, home gestures, and screenshots weather_day0..2.

  python tools/weather_test.py --port COM14 --out shots
"""
import argparse
import os
import random
import time

from panel_test import Panel
from swipe_test import Tester, STARTS, SPEEDS

DAY_PANEL = [(244, 107), (380, 107)]  # Heute, Morgen on the home card


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM14")
    ap.add_argument("--out", default="shots")
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    f = lambda n: os.path.join(a.out, n + ".png")
    p = Panel(a.port)
    t = Tester(p, random.Random(a.seed))
    if t.state()["screen"] != "home":
        t.go_home()
    p.shot(f("weather_home"))

    # Tap Heute and Morgen
    for day, (x, y) in enumerate(DAY_PANEL):
        t.tap(x, y)
        st = t.state()
        t.check("tap day panel", st["screen"] == "weather" and st["page"] == day + 1, (day, st, t.log))
        if day == 0:
            p.shot(f("weather_day0"))
        t.go_home()

    # Realistic swipes on the day view
    t.tap(*DAY_PANEL[0])
    ys = [120, 260, 330, 420]
    n = 0
    for rep in range(3):
        for sx in STARTS:
            for direction in (-1, 1):
                room = sx if direction < 0 else 479 - sx
                if room < 80:
                    continue
                t.hswipe(direction, sx, t.rng.randint(80, min(400, room)), t.rng.choice(SPEEDS), ys[n % 4])
                n += 1
    # walk to day 2 and 3 for screenshots, then to the end and back
    while t.state()["page"] > 1:
        t.hswipe(1, 60, 300, 250, 300)
    t.hswipe(-1, 420, 300, 250, 300)
    p.shot(f("weather_day1"))
    t.hswipe(-1, 470, 300, 400, 300)
    p.shot(f("weather_day2"))
    for _ in range(6):
        t.hswipe(-1, 440, 300, 200, 300)
    p.shot(f("weather_day_last"))

    # tap on the day view does nothing harmful
    t.tap(240, 300)
    st = t.state()
    t.check("tap on day view stays", st["screen"] == "weather" and not t.fired(), (st, t.log))

    # home: bottom-edge swipe up, and a normal upward swipe mid-screen
    t.go_home()
    t.tap(*DAY_PANEL[1])
    t.finger(240, 380, 236, 180, 300, drift=4)
    st = t.state()
    t.check("upward swipe mid-screen -> home", st["screen"] == "home" and not t.fired(), (st, t.touch_line()))

    for k, (ok, tot) in sorted(t.counts.items()):
        print(f"{k:32} {ok}/{tot}")
    print(f"TOTAL {t.passed}/{t.passed + len(t.failed)} passed")


if __name__ == "__main__":
    main()
