"""Settings navigation test (needs the test firmware, starts anywhere).

Opens Einstellungen, the WLAN list (scan only: the Wi-Fi is NOT changed, the
password screen is left without OK), and the Wetter-Ort search: search and
select Zürich, then Buchs (St. Gallen), then LOCRESET back to the default
Buchs SG coordinates. Positions come from the harness (POS), not constants.

  python tools/settings_test.py --port COM14 --out shots
"""
import argparse
import os
import random
import time

from panel_test import Panel
from swipe_test import Tester


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM14")
    ap.add_argument("--out", default="shots")
    ap.add_argument("--prefix", default="v3")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    p = Panel(a.port)
    t = Tester(p, random.Random(3))
    shot = lambda n: p.shot(os.path.join(a.out, n + ".png"))
    res = []

    def check(name, ok, info=""):
        res.append((name, bool(ok), info))
        print(f"{'PASS' if ok else 'FAIL'}  {name}  {info}")

    def wait_state(pred, secs):
        end = time.time() + secs
        st = t.state()
        while not pred(st) and time.time() < end:
            t.log += t.drain(0.5)
            st = t.state()
        return st

    def tap(name):
        x, y = p.pos(name)
        t.tap(x, y)

    def to_settings():
        if t.state()["screen"] != "settings":
            t.go_home()
            tap("cat:settings")

    def right_swipe():
        t.finger(60, 250, 330, 252, 250)

    if t.state()["screen"] != "home":
        t.go_home()
    shot(a.prefix + "_home")
    for key in ("cat:fav", "cat:licht", "cat:storen", "cat:schalter", "cat:settings"):
        x, y = p.pos(key)
        check(f"home button {key} visible", 0 < x < 480 and 200 < y < 480, (x, y))

    tap("cat:settings")
    st = t.state(); check("open settings", st["screen"] == "settings", st)
    shot(a.prefix + "_settings")
    t.go_home()
    st = t.state(); check("swipe up from settings -> home", st["screen"] == "home", st)

    # WLAN list: scan only
    tap("cat:settings"); tap("set:wifi")
    st = wait_state(lambda s: s.get("scanning", 1) == 0, 20)
    check("wifi scan lists networks", st["screen"] == "wifi" and st["count"] > 0, st)
    items = p.items()
    print("  networks:", len(items), "|", "; ".join(items[:6]))
    shot(a.prefix + "_wifi_list")
    t.finger(240, 400, 238, 160, 300)  # vertical drag scrolls the list, no tap
    st = t.state(); check("drag in wifi list stays", st["screen"] == "wifi" and not t.fired(), (st, t.fired()))
    right_swipe()
    st = t.state(); check("right swipe wifi -> settings", st["screen"] == "settings", st)

    tap("set:wifi")
    st = wait_state(lambda s: s.get("scanning", 1) == 0, 20)
    tap("wifi:0")
    st = t.state(); check("tap network -> password screen", st["screen"] == "wifipass", st)
    p.cmd("TYPE test")  # typed but never confirmed: the Wi-Fi is not changed
    time.sleep(0.3)
    shot(a.prefix + "_wifi_password")
    right_swipe()
    st = t.state()
    check("leave password screen without connecting", st["screen"] == "settings" and st.get("connecting", 0) == 0,
          st)
    log = t.drain(1.0)
    check("no wifi switch happened", not any("switching" in l for l in t.log + log), "")

    # Wetter-Ort: search, select, restore
    tap("set:loc")
    st = t.state(); check("open location search", st["screen"] == "location", st)
    shot(a.prefix + "_location_keyboard")

    def search_and_pick(query, want):
        p.cmd(f"TYPE {query}")
        p.cmd("ENTER")
        st = wait_state(lambda s: s.get("searching", 1) == 0, 20)
        items = p.items()
        print(f"  {query}:", "; ".join(items[:5]))
        idx = next((i for i, it in enumerate(items) if want(it)), None)
        check(f"search '{query}' finds it", st["count"] > 0 and idx is not None, (st, items[:3]))
        return idx

    idx = search_and_pick("Zürich", lambda it: it.split(" ", 2)[2].startswith("Zürich|"))
    shot(a.prefix + "_location_results")
    if idx is not None:
        t.log = []
        tap(f"loc:{idx}")
        st = wait_state(lambda s: s["screen"] == "settings", 10)
        t.log += t.drain(8)  # weather refresh right after the change
        check("select Zürich -> settings", st["screen"] == "settings", st)
        check("weather refreshed for Zürich", any(l.startswith("Location Zürich") for l in t.log) and
              any(l.startswith("Weather ok") for l in t.log), [l for l in t.log if l.startswith(("Location", "Weather"))])
        shot(a.prefix + "_settings_zuerich")
        t.go_home()
        shot(a.prefix + "_home_zuerich")

    to_settings()
    tap("set:loc")
    idx = search_and_pick("Buchs", lambda it: "St. Gallen" in it)
    if idx is not None:
        t.log = []
        tap(f"loc:{idx}")
        st = wait_state(lambda s: s["screen"] == "settings", 10)
        t.log += t.drain(3)
        check("select Buchs (St. Gallen)", any(l.startswith("Location Buchs") for l in t.log), t.log[-3:])
    to_settings()
    t.log = []
    p.cmd("LOCRESET")
    t.log += t.drain(8)
    check("restore default Buchs SG", any(l.startswith("Location reset to Buchs SG") for l in t.log) and
          any(l.startswith("Weather ok") for l in t.log), [l for l in t.log if l.startswith(("Location", "Weather"))])
    t.go_home()
    shot(a.prefix + "_home_restored")

    ok = sum(1 for _, g, _ in res if g)
    print(f"TOTAL {ok}/{len(res)}")


if __name__ == "__main__":
    main()
