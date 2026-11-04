"""Familienkalender reminder windows: hardware test over the serial harness.

All-day events: from "Am Vortag ab" (CALFROM) on the day before until the end
of the event day. Timed events: CALLEAD minutes before the start. Uses
synthetic events (REMTEST, kind 2 = all-day family event, 0 = timed), never
clears acknowledgements; restores the saved settings.
Usage: python tools/cal_window_test.py COM14
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

def val(p, c):
    return int(p.cmd(c, expect=(c.split()[0],)).split()[1])

def settle(p):
    time.sleep(1.5); return rem(state(p))

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM14"
    if port.upper() == "COM5": sys.exit("COM5 is the 3D printer")
    p = Panel(port)
    wait(p, lambda s: "STATE home" in s, 90)
    p.cmd("SCRTO 0")
    home(p)
    time.sleep(40)  # after a boot: SNTP sync (reminders wait for the clock)
    lead0, from0 = val(p, "CALLEAD"), val(p, "CALFROM")
    acks = p.cmd("ACKDUMP", expect=("ACKS",)).split()[1]
    print("saved lead", lead0, "from", from0)
    os.makedirs("v12shots", exist_ok=True)
    try:
        run(p, lead0, from0)
    finally:
        p.cmd(f"CALLEAD {lead0}", expect=("CALLEAD",))
        p.cmd(f"CALFROM {from0}", expect=("CALFROM",))
        p.cmd("REMDROP", expect=("OK",))
    check(val(p, "CALLEAD") == lead0 and val(p, "CALFROM") == from0, "settings restored")
    check(p.cmd("ACKDUMP", expect=("ACKS",)).split()[1] == acks, "acknowledgements untouched")
    print("RESULT", "OK" if not FAILS else f"{len(FAILS)} FAIL")

def run(p, lead0, from0):
    lt = time.localtime(); now_min = lt.tm_hour * 60 + lt.tm_min
    if now_min < 2 or now_min > 1430: sys.exit("run away from midnight")
    # Timed: event in 20 min; lead 15 -> not yet, lead 30 -> shown
    p.cmd("CALLEAD 15", expect=("CALLEAD",))
    r0 = settle(p)
    p.cmd("REMTEST 20 0", expect=("OK",))
    s = settle(p)
    check(s == r0, f"timed in 20 min, lead 15: no card ({s} vs {r0})")
    p.cmd("CALLEAD 30", expect=("CALLEAD",))
    s = settle(p)
    check(s == r0 + 1, f"timed in 20 min, lead 30: card ({s} vs {r0})")
    p.shot("v12shots/v12_timed.png")
    p.cmd("REMDROP", expect=("OK",))
    # All-day tomorrow: from after now -> not yet; from before now -> shown
    p.cmd(f"CALFROM {now_min + 5}", expect=("CALFROM",))
    r0 = settle(p)
    p.cmd("REMTEST 1440 2", expect=("OK",))
    s = settle(p)
    check(s == r0, f"all-day tomorrow, from {now_min + 5}: no card ({s} vs {r0})")
    p.cmd(f"CALFROM {now_min - 1}", expect=("CALFROM",))
    s = settle(p)
    check(s == r0 + 1, f"all-day tomorrow, from {now_min - 1}: card ({s} vs {r0})")
    p.shot("v12shots/v12_allday_eve.png")
    p.cmd("REMDROP", expect=("OK",))
    # All-day today: shown regardless of the from time (until end of day)
    p.cmd(f"CALFROM {now_min + 5}", expect=("CALFROM",))
    r0 = settle(p)
    p.cmd("REMTEST 0 2", expect=("OK",))
    s = settle(p)
    check(s == r0 + 1, f"all-day today: card until end of day ({s} vs {r0})")
    p.cmd("REMDROP", expect=("OK",))
    # Settings rows step
    home(p)
    tapn(p, "cat:settings"); tapn(p, "set:g:info", 0.8); tapn(p, "set:rem", 1.0)
    p.cmd(f"CALLEAD {lead0}", expect=("CALLEAD",)); p.cmd(f"CALFROM {from0}", expect=("CALFROM",))
    time.sleep(0.8)
    p.shot("v12shots/v12_remset.png")
    print(state(p))
    tapn(p, "rem:lead", 0.8)
    check(val(p, "CALLEAD") != lead0, "tap steps the lead")
    tapn(p, "rem:from", 0.8)
    check(val(p, "CALFROM") != from0, "tap steps the all-day from")
    home(p)

if __name__ == "__main__":
    main()
