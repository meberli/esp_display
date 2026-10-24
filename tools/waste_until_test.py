"""Hinweis am Abfuhrtag bis: hardware test over the serial harness.

Injects a waste collection today (WASTETEST), sets the cutoff after and
before now, checks the home chip and the reminder card, then restores the
saved cutoff. Usage: python tools/waste_until_test.py COM14
"""
import os, sys, time
sys.path.insert(0, "tools")
from panel_test import Panel
from info_test import state, wait, tapn, home

FAILS = []
def check(ok, what):
    print("PASS" if ok else "FAIL", what)
    if not ok: FAILS.append(what)

import re
def rem(s):
    m = re.search(r"rem=(\d+)", s); return int(m.group(1)) if m else -1

def main():
    p = Panel(sys.argv[1] if len(sys.argv) > 1 else "COM14")
    wait(p, lambda s: "STATE home" in s, 90)
    home(p)
    saved = int(p.cmd("WASTEUNTIL", expect=("WASTEUNTIL",)).split()[1])
    print("saved cutoff", saved)
    os.makedirs("v10shots", exist_ok=True)
    try:
        run(p, saved)
    finally:  # never leave the user's cutoff changed
        p.cmd(f"WASTEUNTIL {saved}", expect=("WASTEUNTIL",))
    print("RESULT", "OK" if not FAILS else f"{len(FAILS)} FAIL")


def run(p, saved):
    lt = time.localtime(); now_min = lt.tm_hour * 60 + lt.tm_min
    p.cmd(f"WASTEUNTIL {min(now_min + 5, 1440)}", expect=("WASTEUNTIL",))
    time.sleep(1.5)
    r0 = rem(state(p))  # baseline incl. a real collection today (no REMCLEAR: keeps acks)
    check(p.cmd("WASTETEST") == "OK", "injected waste today")
    time.sleep(1.5)
    p.shot("v10shots/v10_waste_before_cutoff.png")
    s = state(p); print(s)
    check(rem(s) >= r0 + 1, "before cutoff: reminder card shown " + s)
    home(p)
    p.cmd(f"WASTEUNTIL {max(now_min - 1, 0)}", expect=("WASTEUNTIL",))
    time.sleep(1.5)
    p.shot("v10shots/v10_waste_after_cutoff.png")
    s = state(p); print(s)
    check(rem(s) == 0 or rem(s) < r0 + 1, "after cutoff: no reminder card " + s)
    # settings row
    home(p)
    tapn(p, "cat:settings"); tapn(p, "set:cal", 1.0)
    p.cmd(f"WASTEUNTIL {saved}", expect=("WASTEUNTIL",))
    time.sleep(0.8)
    p.shot("v10shots/v10_waste_setting.png")
    tapn(p, "calset:until", 0.8)
    v = int(p.cmd("WASTEUNTIL", expect=("WASTEUNTIL",)).split()[1])
    check(v != saved, f"tap steps the preset {saved} -> {v}")
    p.cmd(f"WASTEUNTIL {saved}", expect=("WASTEUNTIL",))
    check(int(p.cmd("WASTEUNTIL", expect=("WASTEUNTIL",)).split()[1]) == saved, "restored")

if __name__ == "__main__":
    main()
