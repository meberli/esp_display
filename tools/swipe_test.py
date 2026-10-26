"""Realistic click-vs-swipe test for the panel (needs the -test firmware).

Gestures are replayed like a finger: PRESS, then MOVE steps every ~15 ms with
ease-in/out, a little jitter and vertical drift, then RELEASE. After each
gesture the panel is asked for STATE (screen, page n/m, relay) and the
result is compared with what the gesture should do. Any 'Activate',
'Command' or 'Relay' log line during a swipe is a failure.

  python tools/swipe_test.py --port COM14 [--out shots] [--seed 1]

Real touches are logged by the firmware as
  Touch gesture from X,Y to X,Y dx=.. dy=.. move=.. NNNms next|prev|home
so a hand check can be read back with tools/serial_log.py.
"""
import argparse
import math
import os
import random
import re
import time

from panel_test import Panel, cards

STARTS = [5, 20, 40, 100, 240, 380, 440, 460, 475]
SPEEDS = [100, 150, 250, 400, 600, 800]
RELAIS = (127, 166)  # Schalter page 1, slot 0


class Tester:
    def __init__(self, p, rng):
        self.p = p
        self.rng = rng
        self.passed = 0
        self.failed = []
        self.counts = {}
        self.log = []

    def drain(self, secs):
        out = []
        end = time.time() + secs
        while time.time() < end:
            try:
                line = self.p._line(min(end, time.time() + 0.1))
            except TimeoutError:
                continue
            if line and not line.startswith("State "):
                out.append(line)
        return out

    def state(self):
        for _ in range(3):
            self.p.s.write(b"STATE\n")
            deadline = time.time() + 2
            try:
                while True:
                    line = self.p._line(deadline)
                    if line.startswith("STATE "):
                        d = self._parse_state(line)
                        pw = re.search(r"scr=(on|off) bl=(\d+) to=(\d+)", line)
                        if pw:
                            d.update(scr=pw.group(1), bl=int(pw.group(2)), to=int(pw.group(3)))
                        return d
                    if line:
                        self.log.append(line)
            except TimeoutError:
                continue
        raise RuntimeError("no STATE reply")

    @staticmethod
    def _parse_state(line):
        w = re.match(r"STATE weather (\d+)/(\d+) relay=(\d) busy=(\d)", line)
        if w:
            return {"screen": "weather", "name": "weather", "page": int(w.group(1)),
                    "pages": int(w.group(2)), "relay": int(w.group(3)), "busy": int(w.group(4))}
        m = re.match(r"STATE (home|message|category (\S+) (\d+)/(\d+)) relay=(\d) busy=(\d)", line)
        if m is None:  # settings screens: "STATE <screen> ... relay= busy="
            g = re.match(r"STATE (\S+)(.*) relay=(\d) busy=(\d)", line)
            kv = dict(re.findall(r"(\w+)=(\d+)", g.group(2)))
            nums = re.findall(r"^ (\d+)", g.group(2))
            return {"screen": g.group(1), "count": int(nums[0]) if nums else 0,
                    **{k: int(v) for k, v in kv.items()},
                    "relay": int(g.group(3)), "busy": int(g.group(4))}
        if m.group(1) == "home" or m.group(1) == "message":
            return {"screen": m.group(1), "relay": int(m.group(5)), "busy": int(m.group(6))}
        return {"screen": "category", "name": m.group(2), "page": int(m.group(3)),
                "pages": int(m.group(4)), "relay": int(m.group(5)), "busy": int(m.group(6))}

    def finger(self, x0, y0, x1, y1, ms, jitter=2.0, drift=0):
        """Press, move along an eased path with jitter, release."""
        rnd = self.rng
        self.log = []
        self.p.cmd(f"PRESS {x0} {y0}")
        steps = max(2, int(ms / 15))
        t0 = time.time()
        for i in range(1, steps + 1):
            f = i / steps
            e = 0.5 - 0.5 * math.cos(math.pi * f)  # ease in/out
            x = x0 + (x1 - x0) * e + rnd.uniform(-jitter, jitter)
            y = y0 + (y1 - y0) * e + drift * math.sin(math.pi * f) + rnd.uniform(-jitter, jitter)
            target = t0 + ms / 1000.0 * f
            while time.time() < target:
                time.sleep(0.002)
            self.p.cmd(f"MOVE {int(round(x))} {int(round(y))}")
        self.p.cmd("RELEASE")
        self.log += self.drain(0.6)

    def tap(self, x, y):
        self.log = []
        self.p.cmd(f"TAP {x} {y}")
        self.log += self.drain(1.2)

    def check(self, kind, ok, detail):
        self.counts.setdefault(kind, [0, 0])
        self.counts[kind][1] += 1
        if ok:
            self.counts[kind][0] += 1
            self.passed += 1
        else:
            self.failed.append((kind, detail))
            print("  FAIL", kind, detail)

    def fired(self):
        return [l for l in self.log if l.startswith(("Activate", "Command", "Relay"))]

    def touch_line(self):
        return next((l for l in self.log if l.startswith("Touch")), "-")

    def go_home(self):
        self.finger(240, 474, 244, 300, 300, drift=3)
        st = self.state()
        self.check("home swipe up", st["screen"] == "home" and not self.fired(), (st, self.touch_line()))

    def open_category(self, name, x, y):
        self.tap(x, y)
        st = self.state()
        ok = st["screen"] == "category" and st["page"] == 1
        self.check("tap card", ok, (name, st, self.log))
        return st

    def hswipe(self, direction, start_x, length, ms, y):
        """direction -1 = finger moves left (next page), +1 = right (prev)."""
        before = self.state()
        x1 = start_x + direction * length
        drift = self.rng.choice([-14, -6, 0, 6, 14])
        dy_end = self.rng.randint(-18, 18)
        self.finger(start_x, y, x1, y + dy_end, ms, drift=drift)
        after = self.state()
        exp = before["page"]
        if direction < 0 and before["page"] < before["pages"]:
            exp += 1
        if direction > 0 and before["page"] > 1:
            exp -= 1
        ok = (after["screen"] == before["screen"] and after.get("name") == before["name"]
              and after["page"] == exp and not self.fired() and after["relay"] == before["relay"])
        kind = f"swipe {'left ' if direction < 0 else 'right'}"
        if before["page"] == (before["pages"] if direction < 0 else 1):
            kind += " (at end)"
        self.check(kind, ok, (before["name"], f"{before['page']}->{after.get('page')} exp {exp}",
                              f"x={start_x} len={length} {ms}ms", self.touch_line(), self.fired()))
        return after


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM14")
    ap.add_argument("--out", default=None)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    p = Panel(a.port)
    t = Tester(p, random.Random(a.seed))
    shot = (lambda n: p.shot(os.path.join(a.out, n + ".png"))) if a.out else (lambda n: None)
    if a.out:
        os.makedirs(a.out, exist_ok=True)

    st = t.state()
    if st["screen"] != "home":
        t.go_home()
    for name, cx, cy in cards(p):
        st = t.open_category(name, cx, cy)
        pages = st.get("pages", 1)
        ys = [166, 320, 250, 400] if name != "schalter" else [166, 250]
        n = 0
        # Every start x in both directions, several passes so first and last
        # pages are both hit. Left swipes need room to the left and so on.
        for rep in range(max(2, pages)):
            for sx in STARTS:
                for direction in (-1, 1):
                    room = sx if direction < 0 else 479 - sx
                    if room < 80:
                        continue
                    length = t.rng.randint(80, min(400, room))
                    ms = t.rng.choice(SPEEDS)
                    y = ys[n % len(ys)]
                    n += 1
                    after = t.hswipe(direction, sx, length, ms, y)
                    if after["page"] == 2 and direction < 0 and rep == 0 and name in ("licht", "storen"):
                        shot(f"left_{name}_p2")
            if rep == 0 and pages > 1:
                # run to the last page, then check the handles there
                for _ in range(pages + 1):
                    t.hswipe(-1, 440, 260, 250, 250)
                shot(f"left_{name}_last") if name in ("licht", "storen") else None
                for _ in range(pages + 1):
                    t.hswipe(1, 40, 260, 250, 250)
                shot(f"left_{name}_first") if name == "storen" else None
        # vertical drag in the middle: nothing should happen
        before = t.state()
        t.finger(240, 150, 236, 380, 300)
        after = t.state()
        t.check("vertical mid drag", after == before and not t.fired(), (before, after, t.touch_line()))
        if name == "schalter":
            st0 = t.state()["relay"]
            t.tap(*RELAIS)
            on = t.state()["relay"]
            shot("left_relais_on")
            t.tap(*RELAIS)
            off = t.state()["relay"]
            t.check("tap relais on/off", st0 == 0 and on == 1 and off == 0, (st0, on, off))
        t.go_home()
    shot("left_home")
    print()
    for k, (ok, tot) in sorted(t.counts.items()):
        print(f"{k:24} {ok}/{tot}")
    print(f"TOTAL {t.passed}/{t.passed + len(t.failed)} passed")


if __name__ == "__main__":
    main()
