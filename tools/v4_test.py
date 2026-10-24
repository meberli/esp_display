"""Dark Glass / screensaver / NTP walkthrough (test firmware).

  python tools/v4_test.py --port COM14 --out v4shots
Screens: home, Licht, Storen, weather day, settings, Bildschirm, Uhrzeit,
digital clock, analog clock (the waking tap must be swallowed). Restores the
black saver and a 2 min timeout at the end.
"""
import argparse, os, re, time
from panel_test import Panel

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM14")
    ap.add_argument("--out", default="v4shots")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    p = Panel(a.port)
    res = []
    def check(n, ok, info=""):
        res.append(ok); print(("PASS " if ok else "FAIL ") + n + " " + str(info))
    def state():
        return p.cmd("STATE", expect=("STATE", "OK"))
    def shot(n): p.shot(os.path.join(a.out, f"v4_{n}.png"))
    def tap(n):
        x, y = p.pos(n); p.tap(x, y); time.sleep(0.6)
    def home():
        p.swipe(240, 475, 240, 300); time.sleep(0.8)
    for _ in range(60):
        if "home" in state(): break
        time.sleep(1)
    time.sleep(3)
    # Remember the user's screensaver and timeout: restored at the end (an
    # earlier version always restored "Schwarz", which removed the clock).
    s0 = state()
    m = re.search(r"saver=(\d)", s0); saver0 = int(m.group(1)) if m else 0
    m = re.search(r" to=(\d+)", s0); to0 = int(m.group(1)) if m else 120
    shot("home"); check("home", "home" in state())
    tap("cat:licht"); shot("licht"); check("licht", "Licht" in state()); home()
    tap("cat:storen"); shot("storen"); home()
    tap("cat:schalter"); shot("schalter"); home()
    tap("day:0"); shot("weather"); check("weather", "weather" in state())
    p.swipe(400, 300, 100, 300); time.sleep(0.8); shot("weather_tomorrow"); home()
    tap("cat:settings"); shot("settings"); check("settings", "settings" in state())
    tap("set:time"); time.sleep(1); shot("time"); s = state(); check("time screen", "time" in s, s)
    home(); tap("cat:settings"); tap("set:disp")
    tap("saver:1"); shot("display_digital"); check("saver digital", "saver=1" in state())
    p.cmd("SCRTO 2"); time.sleep(4); s = state(); check("off digital", "scr=off" in s, s)
    shot("clock_digital")
    p.tap(240, 240); time.sleep(1); s = state(); check("wake digital", "scr=on" in s and "display" in s, s)
    p.cmd("SCRTO 0"); tap("saver:2")
    p.cmd("SCRTO 2"); time.sleep(4); shot("clock_analog"); time.sleep(2); shot("clock_analog_2")
    s = state(); check("off analog", "scr=off" in s and "saver=2" in s, s)
    p.tap(240, 240); time.sleep(1); s = state(); check("wake analog swallowed", "scr=on" in s and "display" in s, s)
    p.cmd("SCRTO 0"); tap(f"saver:{saver0}")
    kTimeouts = [0, 30, 60, 120, 300, 600]
    tap(f"to:{kTimeouts.index(to0) if to0 in kTimeouts else 3}"); shot("display_restored")
    s = state(); check("user saver restored", f"saver={saver0}" in s, s)
    home()
    print("RESULT", sum(res), "/", len(res))

if __name__ == "__main__":
    main()
