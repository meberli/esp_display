"""Press-feedback test: tiles must look idle mid-swipe and flash on a tap.

  python tools/press_test.py --port COM14 --out shots
Starts on the home screen.
"""
import argparse
import os
import time

from PIL import Image, ImageChops

from panel_test import Panel
from swipe_test import Tester, RELAIS
import random

TILE = (20, 88, 234, 244)       # slot 0 tile box (x0, y0, x1, y1)
TILE_R = (246, 88, 460, 244)    # slot 1



def diff(a, b, box):
    d = ImageChops.difference(Image.open(a).convert("RGB").crop(box), Image.open(b).convert("RGB").crop(box))
    return sum(1 for px in d.get_flattened_data() if max(px) > 8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM14")
    ap.add_argument("--out", default="shots")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    f = lambda n: os.path.join(a.out, n + ".png")
    p = Panel(a.port)
    t = Tester(p, random.Random(1))
    res = []

    # Home button mid-swipe (positions from the harness)
    lx, ly = p.pos("cat:loxone")
    sx, sy = p.pos("cat:kameras")  # never cat:schalter: it switches a real device
    card_licht = (lx - 68, ly - 56, lx + 68, ly + 56)  # 136x112 button
    p.shot(f("press_home_idle"))
    p.cmd(f"PRESS {lx} {ly}"); p.cmd(f"MOVE {lx - 13} {ly + 1}"); time.sleep(0.1); p.cmd(f"MOVE {lx - 53} {ly + 2}")
    time.sleep(0.3)
    p.shot(f("press_home_midswipe"))
    p.cmd("RELEASE"); time.sleep(0.8)
    n = diff(f("press_home_idle"), f("press_home_midswipe"), card_licht)
    res.append(("home card mid-swipe identical", n == 0, n))
    st = t.state(); res.append(("still home after swipe", st["screen"] == "home", st))

    # Licht page 1 tiles mid-swipe (both directions, before and past slop)
    t.tap(lx, ly); time.sleep(1.5)
    p.shot(f("press_licht_idle"))
    for name, path, box in (("left", [(353, 166), (345, 167), (330, 168), (290, 170), (230, 172)], TILE_R),
                            ("right", [(127, 166), (135, 165), (160, 164), (210, 162), (260, 160)], TILE),
                            ("slow-small", [(127, 166), (130, 167), (140, 168), (150, 170)], TILE)):
        p.cmd(f"PRESS {path[0][0]} {path[0][1]}")
        for x, y in path[1:]:
            time.sleep(0.06); p.cmd(f"MOVE {x} {y}")
        time.sleep(0.3)
        p.shot(f(f"press_licht_mid_{name}"))
        before = t.state()
        p.cmd("RELEASE")
        log = t.drain(1.0)
        n = diff(f("press_licht_idle"), f(f"press_licht_mid_{name}"), box)
        res.append((f"licht tile mid-swipe {name} identical", n == 0, n))
        fired = [l for l in log if l.startswith(("Activate", "Command", "Relay"))]
        res.append((f"licht swipe {name} fired nothing", not fired, fired))
        st = t.state()
        if st["page"] != 1:
            t.finger(40, 250, 300, 250, 250)  # back to page 1
    t.go_home()

    # Tap feedback on Relais: idle -> flash (during the 120 ms window) -> on
    t.tap(sx, sy); time.sleep(1.5)
    p.shot(f("press_relais_idle"))
    p.s.write(b"TAP 127 166\n"); time.sleep(0.17); p.s.write(b"SHOT\n")
    # reuse the shot reader: it waits for SHOT_BEGIN, skipping the TAP's OK
    p.shot_after_request = True
    p.shot(f("press_relais_tap"), request=False)
    time.sleep(1.0)
    p.shot(f("press_relais_on"))
    st = t.state()
    d_idle = diff(f("press_relais_idle"), f("press_relais_tap"), TILE)
    d_on = diff(f("press_relais_on"), f("press_relais_tap"), TILE)
    res.append(("tap fired (relay on)", st["relay"] == 1, st))
    res.append(("tap shows flash (differs from idle and from on)", d_idle > 0 and d_on > 0, (d_idle, d_on)))
    t.tap(*RELAIS); time.sleep(0.5)
    st = t.state(); res.append(("relay back off", st["relay"] == 0, st))
    t.go_home()

    ok = 0
    for name, good, info in res:
        ok += bool(good)
        print(f"{'PASS' if good else 'FAIL'}  {name}  {info}")
    print(f"TOTAL {ok}/{len(res)}")


if __name__ == "__main__":
    main()
