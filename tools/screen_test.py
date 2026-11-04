"""Screen timeout test (needs the test firmware).

Bildschirm settings (timeout choice and hint toggle, saved in NVS and checked
again after a REBOOT), screen off after a short harness timeout (SCRTO), the
dim 'wake me up' hint (backlight ~4 %) or full off, and waking: the waking tap
must not fire a control and the waking swipe must not page. The defaults
(2 min, hint on) are restored at the end.

  python tools/screen_test.py --port COM14 --out shots
"""
import argparse
import os
import random
import time

from panel_test import Panel
from swipe_test import Tester

CHOICES = [0, 30, 60, 120, 300, 600]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM14")
    ap.add_argument("--out", default="shots")
    ap.add_argument("--prefix", default="v3")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    p = Panel(a.port)
    t = Tester(p, random.Random(5))
    shot = lambda n: p.shot(os.path.join(a.out, f"{a.prefix}_{n}.png"))
    res = []

    def check(name, ok, info=""):
        res.append((name, bool(ok), info))
        print(f"{'PASS' if ok else 'FAIL'}  {name}  {info}")

    def tap(name):
        x, y = p.pos(name)
        t.tap(x, y)

    def open_display():
        if t.state()["screen"] != "home":
            t.go_home()
        tap("cat:settings")
        tap("set:disp")
        return t.state()

    def set_display(timeout, hint, saver=None):
        st = open_display()
        if saver is not None and st.get("saver") != saver:
            tap(f"saver:{saver}")
        if st.get("timeout") != timeout:
            tap(f"to:{CHOICES.index(timeout)}")
        if t.state().get("hint") != hint:
            tap("disp:hint")
        return t.state()

    def wait_off(secs):
        end = time.time() + secs
        st = t.state()
        while st.get("scr") != "off" and time.time() < end:
            time.sleep(0.5)
            st = t.state()
        return st

    def reconnect_after_reboot():
        time.sleep(4)
        end = time.time() + 30
        while time.time() < end:
            try:
                p.rx = bytearray()
                p.s.reset_input_buffer()
                p.cmd("PING", timeout=2)
                break
            except (TimeoutError, RuntimeError):
                time.sleep(1)
        while time.time() < end + 30:  # boot shows "Connecting" until Loxone is loaded
            try:
                if t.state()["screen"] != "message":
                    return True
                time.sleep(1)
            except (TimeoutError, RuntimeError):
                time.sleep(1)
        return False

    if t.state()["screen"] != "home":
        t.go_home()
    shot("home")
    st = open_display()
    # The user's settings, restored at the end.
    orig = (st.get("timeout"), st.get("hint"), st.get("saver"))
    check("open Bildschirm settings", st["screen"] == "display", st)
    print("  before:", orig)
    # The hint / black checks below need the black screensaver (a clock
    # saver keeps the backlight at the clock level).
    set_display(st.get("timeout"), st.get("hint"), 0)

    # Persistence: 30 s, hint off, reboot, read back
    st = set_display(30, 0)
    check("set timeout 30 s + hint off", st.get("timeout") == 30 and st.get("hint") == 0, st)
    shot("display_30s_hint_off")
    p.cmd("REBOOT")
    check("reboot", reconnect_after_reboot())
    p.cmd("SCRTO 0")
    st = open_display()
    check("settings persisted after reboot", st.get("timeout") == 30 and st.get("hint") == 0, st)

    st = set_display(120, 1, 0)
    check("set timeout 2 min + hint on", st.get("timeout") == 120 and st.get("hint") == 1, st)
    shot("display")
    t.go_home()
    tap("cat:settings")
    shot("settings")

    # Timeout with hint: dim backlight and 'wake me up'
    t.go_home()
    p.cmd("SCRTO 3")
    st = t.state(); check("screen on before timeout", st.get("scr") == "on" and st.get("bl") == 100, st)
    st = wait_off(8)
    check("screen off after timeout (hint: dim)", st.get("scr") == "off" and 1 <= st.get("bl", 0) <= 5, st)
    shot("off_hint")

    # Waking tap on a home tile: no control, no category
    x, y = p.pos("cat:loxone")
    t.tap(x, y)
    p.cmd("SCRTO 0")  # stay on for the screenshot and the next steps
    st = t.state()
    check("wake by tap", st.get("scr") == "on" and st.get("bl") == 100, st)
    check("waking tap fired nothing", st["screen"] == "home" and not t.fired(), (st["screen"], t.fired()))
    shot("woken_home")

    # Waking swipe in a category: no paging
    t.tap(x, y)
    st = t.state()
    if st["screen"] == "category" and st.get("pages", 1) > 1:
        p.cmd("SCRTO 3")
        st = wait_off(8)
        check("off in category", st.get("scr") == "off", st)
        t.finger(400, 250, 80, 252, 250)
        st = t.state()
        check("wake by swipe, no paging", st.get("scr") == "on" and st["screen"] == "category" and st["page"] == 1
              and not t.fired(), (st, t.fired()))
        t.finger(400, 250, 80, 252, 250)
        st = t.state(); check("next swipe pages again", st["screen"] == "category" and st["page"] == 2, st)
    else:
        check("category with pages for swipe wake", False, st)
    p.cmd("SCRTO 0")
    t.go_home()

    # Wake by home swipe-up on home: stays home, nothing fires
    p.cmd("SCRTO 3")
    wait_off(8)
    t.finger(240, 474, 244, 300, 300)
    st = t.state(); check("wake by swipe up on home", st.get("scr") == "on" and st["screen"] == "home" and not t.fired(), st)

    # Hint off: backlight fully off
    p.cmd("SCRTO 0")
    set_display(120, 0)
    t.go_home()
    p.cmd("SCRTO 3")
    st = wait_off(8)
    check("hint off: backlight 0", st.get("scr") == "off" and st.get("bl") == 0, st)
    shot("off_black")
    t.tap(240, 240)
    st = t.state(); check("wake from full off", st.get("scr") == "on" and st["screen"] == "home" and not t.fired(), st)

    # Clock screensaver: dimmed backlight, clock shown
    p.cmd("SCRTO 0")
    set_display(120, 1, 1)
    t.go_home()
    p.cmd("SCRTO 3")
    st = wait_off(8)
    check("clock saver: backlight 40", st.get("scr") == "off" and st.get("bl") == 40, st)
    t.tap(240, 240)
    st = t.state(); check("wake from clock saver", st.get("scr") == "on" and not t.fired(), st)

    # Restore the user's settings (written 1.5 s after the last tap)
    p.cmd("SCRTO 0")
    if orig[0] in CHOICES and orig[1] in (0, 1) and orig[2] in (0, 1, 2):
        st = set_display(*orig)
        time.sleep(2.5)
        check("restore original display settings", (st.get("timeout"), st.get("hint"), st.get("saver")) == orig, st)
    else:
        check("original settings readable", False, orig)
    t.go_home()
    p.cmd("SCRTO 0")

    ok = sum(1 for _, g, _ in res if g)
    print(f"TOTAL {ok}/{len(res)}")


if __name__ == "__main__":
    main()
