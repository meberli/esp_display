"""Standard suite rule: every page scrolls to its end and the last element is
visible and tappable (harness SCROLLCHK: scrolls all containers of the visible
page to the end, checks the bottom-most clickable widget is on screen, not
clipped and the topmost hit at its centre; scrolls back afterwards).
Usage: python tools/ui_pages_test.py COM14
"""
import re, sys, time
sys.path.insert(0, "tools")
from panel_test import Panel
from info_test import state, wait, tapn, home

FAILS = []

def chk(p, page, tap_last=False):
    time.sleep(0.8)
    line = p.cmd("SCROLLCHK", expect=("SCROLLCHK",), timeout=5)
    ok = line.split()[1] == "ok"
    print("PASS" if ok else "FAIL", f"{page:14s}", line[10:], "|", state(p)[6:40])
    if not ok: FAILS.append(page)
    m = re.search(r"last=(\S+) .*scrolled=(\d+)", line)
    # Real finger scrolling (gesture vs. swipe paging) and a real tap on the last element.
    if tap_last and m and m.group(1) != "?" and int(m.group(2)) > 0 and "no buttons" not in line:
        name = m.group(1)
        s0 = state(p)
        for _ in range(4):
            p.swipe(240, 400, 240, 120, 400); time.sleep(0.6)
        time.sleep(1.5)  # let the scroll throw settle before reading the position
        try:
            x, y = p.pos(name)
        except RuntimeError:
            x, y = -1, 999
        vis = 0 <= y <= 466
        print("PASS" if vis else "FAIL", f"{page:14s} finger-scroll: {name} at y={y}")
        if not vis: FAILS.append(page + " finger-scroll"); return
        if page == "set:play":  # last item is Send: empty fields -> no state change; no radio frames from a UI sweep
            print("SKIP", f"{page:14s} tap {name} (send button, nothing to send)"); return
        p.tap(x, y); time.sleep(1.0)
        s1 = state(p)
        tapped = s1 != s0
        print("PASS" if tapped else "FAIL", f"{page:14s} tap {name} -> {s1[6:30]}")
        if not tapped: FAILS.append(page + " tap")

def settings(p, row=None):
    home(p); tapn(p, "cat:settings", 0.8)
    if row: tapn(p, row, 1.0)

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM14"
    if port.upper() == "COM5": sys.exit("COM5 is the 3D printer")
    p = Panel(port)
    wait(p, lambda s: "STATE home" in s, 90)
    home(p); chk(p, "home")
    for cat in ("cat:fav", "cat:licht", "cat:storen", "cat:schalter"):
        home(p); tapn(p, cat, 1.0); chk(p, cat)
    home(p); tapn(p, "cat:kameras", 2.0); chk(p, "kameras")
    home(p); tapn(p, "wx:now", 1.0); chk(p, "wetter")
    p.swipe(260, 240, 476, 240, 300); time.sleep(1.0); chk(p, "warnungen")
    home(p); tapn(p, "home:oev", 1.5); chk(p, "oev")
    home(p); tapn(p, "home:cal", 1.0); chk(p, "kalender")
    home(p); tapn(p, "home:energy", 1.0); chk(p, "strom")
    p.swipe(440, 240, 200, 240, 300); time.sleep(1.0); chk(p, "bilanz")
    if "page=1" not in state(p): FAILS.append("bilanz page"); print("FAIL bilanz page", state(p)[6:40])
    p.swipe(440, 240, 200, 240, 300); time.sleep(1.0); chk(p, "wasser")
    if "page=2" not in state(p): FAILS.append("wasser page"); print("FAIL wasser page", state(p)[6:40])
    settings(p); chk(p, "einstellungen", tap_last=True)
    for row in ("set:wifi", "set:loc", "set:disp", "set:time", "set:cams", "set:oev", "set:cal", "set:rem",
                "set:energy", "set:maint", "set:play"):
        settings(p, row); chk(p, row, tap_last=True)
    settings(p, "set:cal"); tapn(p, "calset:bfs", 1.0); chk(p, "text-edit")
    home(p)
    print("RESULT", "OK" if not FAILS else f"{len(FAILS)} FAIL: {' '.join(FAILS)}")
    sys.exit(1 if FAILS else 0)

if __name__ == "__main__":
    main()
