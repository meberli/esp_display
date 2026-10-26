"""Abfall "Am Vortag ab": hardware test over the serial harness.

Injects a waste collection tomorrow (REMTEST 1440 1), sets the "Am Vortag ab"
time before and after now, checks the reminder card, steps the settings row,
then restores the saved value. Usage: python tools/waste_from_test.py COM14
"""
import os, re, sys, time
sys.path.insert(0, "tools")
from panel_test import Panel
from info_test import state, wait, tapn, home

FAILS = []
def check(ok, what):
    print("PASS" if ok else "FAIL", what)
    if not ok: FAILS.append(what)

def rem(s):
    m = re.search(r"rem=(\d+)", s); return int(m.group(1)) if m else -1

def get(p):
    return int(p.cmd("WASTEFROM", expect=("WASTEFROM",)).split()[1])

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM14"
    if port.upper() == "COM5": sys.exit("COM5 is the 3D printer")
    p = Panel(port)
    wait(p, lambda s: "STATE home" in s, 90)
    p.cmd("SCRTO 0")
    home(p)
    saved = get(p); print("saved from", saved)
    os.makedirs("v11shots", exist_ok=True)
    try:
        run(p, saved)
    finally:
        p.cmd(f"WASTEFROM {saved}", expect=("WASTEFROM",))
        p.cmd("REMCLEAR")
    check(get(p) == saved, "restored")
    print("RESULT", "OK" if not FAILS else f"{len(FAILS)} FAIL")

def run(p, saved):
    lt = time.localtime(); now_min = lt.tm_hour * 60 + lt.tm_min
    if now_min < 2 or now_min > 1430: sys.exit("run away from midnight")
    p.cmd(f"WASTEFROM {now_min + 5}", expect=("WASTEFROM",))
    r0 = rem(state(p))
    p.cmd("REMTEST 1440 1"); time.sleep(1.5)
    s = state(p); print(s)
    check(rem(s) == r0, "before 'Am Vortag ab': no card " + s)
    p.cmd(f"WASTEFROM {now_min - 1}", expect=("WASTEFROM",)); time.sleep(1.5)
    p.shot("v11shots/v11_waste_eve.png")
    s = state(p); print(s)
    check(rem(s) == r0 + 1, "after 'Am Vortag ab': card shown " + s)
    home(p)
    tapn(p, "cat:settings"); tapn(p, "set:cal", 1.0)
    p.cmd(f"WASTEFROM {saved}", expect=("WASTEFROM",)); time.sleep(0.8)
    p.shot("v11shots/v11_calset.png")
    tapn(p, "calset:from", 0.8)
    v = get(p)
    check(v != saved, f"tap steps the preset {saved} -> {v}")
    home(p)

if __name__ == "__main__":
    main()
