"""Energie (whatwatt Go): hardware test over the serial harness.
Sets the whatwatt address through Einstellungen > Energie (text edit), waits
for live data (ENERGY), checks the Strom/Wasser pages and the home chip, the
stale detection (unreachable address -> stale after 60 s), the poll stepping,
and saves screenshots (v12shots/v12_*.png, no addresses shown on the pages).
Leaves the address set to HOST (Michael's reader) and other settings restored.
Usage: python tools/energy_test.py COM14 HOST
"""
import os, re, sys, time
sys.path.insert(0, "tools")
from panel_test import Panel
from info_test import state, wait, tapn, home

FAILS = []
def check(ok, what):
    print("PASS" if ok else "FAIL", what)
    if not ok: FAILS.append(what)

def energy(p):
    line = p.cmd("ENERGY", expect=("ENERGY",))
    return dict(kv.split("=", 1) for kv in line.split()[1:] if "=" in kv), line

def set_host(p, host):
    home(p); tapn(p, "cat:settings", 0.8); tapn(p, "set:energy", 1.0); tapn(p, "en:host", 0.8)
    tapn(p, "text:ta", 0.6)
    p.cmd("TYPE " + host)
    p.cmd("ENTER")
    time.sleep(1.5)

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM14"
    host = sys.argv[2] if len(sys.argv) > 2 else os.environ.get("WHATWATT_HOST", "")
    if not host: sys.exit("usage: energy_test.py PORT HOST (or WHATWATT_HOST)")
    if port.upper() == "COM5": sys.exit("COM5 is the 3D printer")
    p = Panel(port)
    wait(p, lambda s: "STATE home" in s, 90)
    os.makedirs("v12shots", exist_ok=True)
    e, _ = energy(p)
    print("initial", e.get("on"), e.get("err"))
    home(p); tapn(p, "cat:settings", 0.8); tapn(p, "set:energy", 1.0)
    p.shot("v12shots/v12_enset.png")  # before the address is set: no address on screen
    set_host(p, host)
    check("enset" in state(p), "back on Einstellungen > Energie after saving")
    t0 = time.time()
    while time.time() - t0 < 90:
        e, line = energy(p)
        if e.get("have") == "1" and e.get("stale") == "0": break
        time.sleep(2)
    print(line)
    check(e.get("have") == "1" and e.get("stale") == "0" and e.get("http") == "200", "live data from the whatwatt Go")
    pin = float(e.get("p_in", "-1"))
    check(0 <= pin < 30000, f"import power plausible ({pin:.0f} W)")
    check(float(e.get("water_l", "-1")) >= 0, "water today >= 0")
    check("water_y" in e, "yesterday water reported (" + e.get("water_y", "?") + " L)")
    check(e.get("slot") == "0", "2026: flat tariff slot")
    # Read-only diagnostics (also in the release image)
    p.cmd("DIAGENERGY", expect=("DIAG energy on",))
    days = [p.cmd("", expect=("DIAG energy day",), timeout=3) for _ in range(8)]
    for d in days: print(" ", d)
    check(len(days) == 8 and days[0].startswith("DIAG energy day0 "), "DIAGENERGY prints 8 days")
    h = p.cmd("DIAGHEALTH", expect=("DIAG health",))
    print(" ", h)
    check(" reset=" in h and " whatwatt=" in h and " pv " in h, "DIAGHEALTH line")
    check("://" not in h, "health line has no URLs")
    # Pages
    home(p); time.sleep(1.5)
    s = state(p)
    p.shot("v12shots/v12_home.png")
    tapn(p, "home:energy", 1.5)
    check("energy page=0" in state(p), "home chip opens Strom")
    time.sleep(1.0); p.shot("v12shots/v12_strom.png")
    p.swipe(440, 240, 200, 240, 300); time.sleep(1.2)
    check("energy page=1" in state(p), "swipe left -> Wasser")
    p.shot("v12shots/v12_wasser.png")
    p.swipe(200, 240, 440, 240, 300); time.sleep(1.0)
    check("energy page=0" in state(p), "swipe right -> Strom")
    # Poll interval stepping (10 -> 30 -> 60 -> 5 -> 10)
    home(p); tapn(p, "cat:settings", 0.8); tapn(p, "set:energy", 1.0)
    for _ in range(4): tapn(p, "en:poll", 0.6)
    check("enset" in state(p), "poll stepping stays on the page")
    # Stale: unreachable address -> stale within ~70 s, then back
    set_host(p, "192.168.1.254")
    t0 = time.time(); stale = False
    while time.time() - t0 < 110:
        e, line = energy(p)
        if e.get("stale") == "1": stale = True; break
        time.sleep(3)
    print(line, f"after {time.time() - t0:.0f} s")
    check(stale, "unreachable reader -> stale")
    home(p); tapn(p, "home:energy", 1.5); p.shot("v12shots/v12_strom_stale.png")
    set_host(p, host)
    t0 = time.time()
    while time.time() - t0 < 90:
        e, line = energy(p)
        if e.get("stale") == "0" and e.get("have") == "1": break
        time.sleep(2)
    check(e.get("stale") == "0", "back to live data")
    home(p)
    print("RESULT", "OK" if not FAILS else f"{len(FAILS)} FAIL")
    sys.exit(1 if FAILS else 0)

if __name__ == "__main__":
    main()
