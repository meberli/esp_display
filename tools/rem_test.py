"""Termin-Erinnerungen hardware test via the serial harness.

  python tools/rem_test.py --port COM14 --out v7shots
Needs the test firmware. Uses synthetic events (REMTEST) besides the real
calendar; never prints calendar titles.
"""
import argparse
import os
import re
import sys
import time
import atexit

sys.path.insert(0, os.path.dirname(__file__))
from panel_test import Panel  # noqa: E402
from info_test import check, state, wait, tapn, home, FAILS  # noqa: E402


def rem(s):
    m = re.search(r"rem=(\d+)", s)
    return int(m.group(1)) if m else -1


def field(s, name):
    m = re.search(name + r"=(\w+)", s)
    return m.group(1) if m else ""


def set_saver(p, i):
    home(p)
    tapn(p, "cat:settings")
    tapn(p, "set:disp")
    tapn(p, f"saver:{i}")
    home(p)


def ack_all(p, limit=12):
    n = 0
    while rem(state(p)) > 0 and n < limit:
        tapn(p, "rem:ok", 0.8)
        n += 1
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM14")
    ap.add_argument("--out", default="v7shots")
    a = ap.parse_args()
    if a.port.upper() == "COM5":
        sys.exit("COM5 is the 3D printer")
    os.makedirs(a.out, exist_ok=True)
    p = Panel(a.port)
    shot = lambda n: p.shot(os.path.join(a.out, f"v7_{n}.png"))

    p.s.write(b"REBOOT\n")
    time.sleep(3)
    s = wait(p, lambda s: "STATE home" in s, 60)
    p.cmd("SCRTO 0")  # runtime only: the saved timeout (e.g. 30 s) would blank the screen
    check("STATE home" in s, "boot to home")
    saver0 = field(s, "saver")
    to0 = field(s, "to")
    # Save the real acknowledgements and the calendar lead settings; restored at exit
    acks = p.cmd("ACKDUMP", expect=("ACKS",)).split()[1]
    lead0 = int(p.cmd("CALLEAD", expect=("CALLEAD",)).split()[1])
    from0 = int(p.cmd("CALFROM", expect=("CALFROM",)).split()[1])

    def restore():
        p.cmd(f"CALLEAD {lead0}", expect=("CALLEAD",))
        p.cmd(f"CALFROM {from0}", expect=("CALFROM",))
        p.cmd("REMDROP", expect=("OK",))
        for off in range(0, len(acks) // 2, 64):
            p.cmd(f"ACKLOAD {off} {acks[2 * off:2 * off + 128]}", expect=("OK",))
        ok = p.cmd("ACKLOAD END", expect=("OK", "ERR"))
        back = p.cmd("ACKDUMP", expect=("ACKS",)).split()[1]
        print("acks restored", ok, back == acks)
        if back != acks:
            FAILS.append("acks restore")
    atexit.register(restore)
    p.cmd("REMCLEAR", expect=("OK",))
    time.sleep(40)  # calendar fetch after boot
    base = rem(state(p))
    print(f"INFO real reminders pending after clear: {base}")
    if base > 0:
        shot("home_real")

    # Settings screen
    tapn(p, "cat:settings")
    tapn(p, "set:rem")
    s = state(p)
    check(s.startswith("STATE remset on=1"), "Erinnerungen settings: " + s)
    tapn(p, "rem:lead")
    s = state(p)
    check(f"lead={lead0}" not in s, "tap steps the lead: " + s)
    p.cmd("CALLEAD 15", expect=("CALLEAD",))
    s = state(p)
    check("lead=15" in s, "lead 15 min for Termine: " + s)
    time.sleep(1)
    shot("remset")
    tapn(p, "rem:on")
    s = state(p)
    check("on=0" in s and rem(s) == 0, "switched off clears the queue: " + s)
    time.sleep(0.8)
    shot("remset_off")
    tapn(p, "rem:on")
    home(p)

    # Synthetic event in 10 min (inside 15 min lead) and one in 30 min (outside)
    p.cmd("REMTEST 10 0", expect=("OK",))
    p.cmd("REMTEST 30 0", expect=("OK",))
    s = wait(p, lambda s: rem(s) >= base + 1, 10)
    check(rem(s) == base + 1, f"event in 10 min queued, 30 min not ({rem(s)} pending, base {base})")
    time.sleep(1)
    shot("home_reminder")
    n = ack_all(p)
    s = state(p)
    check(rem(s) == 0, f"all reminders acknowledged with OK ({n} taps)")
    p.cmd("REMTEST 10 0", expect=("OK",))  # same start? new uid each call -> new reminder
    time.sleep(1)
    ack_all(p)

    # Screensavers: card over each mode, one tap on OK acknowledges and wakes
    p.cmd("SCRTO 5", expect=("OK",))
    names = {0: "black", 1: "digital", 2: "analog"}
    for i in (0, 1, 2):
        set_saver(p, i)
        p.cmd("SCRTO 5", expect=("OK",))
        if i == 0:
            # Screen goes off first, then the reminder fires: the panel lights up.
            s = wait(p, lambda s: "scr=off" in s, 20)
            check("scr=off" in s, "black saver on")
            p.cmd(f"REMTEST {11 + i} 0", expect=("OK",))
        else:
            p.cmd(f"REMTEST {11 + i} 0", expect=("OK",))
            s = wait(p, lambda s: "scr=off" in s, 20)
        time.sleep(2)
        s = state(p)
        bl = int(field(s, "bl") or 0)
        check("scr=off" in s and rem(s) == 1 and bl >= 40, f"reminder on {names[i]} saver: {s}")
        shot(f"saver_{names[i]}")
        tapn(p, "rem:ok", 1.0)
        s = state(p)
        check(rem(s) == 0 and "scr=on" in s, f"one tap OK on {names[i]} saver acknowledges and wakes: {s}")
    # Regression (v0.7.0 bug): a tap on the card body (not OK) while off must wake the panel
    p.cmd("REMTEST 15 0", expect=("OK",))
    s = wait(p, lambda s: "scr=off" in s, 20)
    p.tap(200, 60)
    time.sleep(1)
    s = state(p)
    check("scr=on" in s and rem(s) >= 1, "tap on the card body wakes the panel: " + s)
    ack_all(p)
    # Tap outside the card while off wakes the panel and keeps the reminder
    p.cmd("REMTEST 14 0", expect=("OK",))
    s = wait(p, lambda s: "scr=off" in s, 20)
    p.tap(240, 420)
    time.sleep(1)
    s = state(p)
    check("scr=on" in s and rem(s) == 1, "tap outside the card wakes, reminder stays: " + s)
    ack_all(p)

    # Waste (all-day today) inside the waste window -> pending
    # (shown until "Hinweis am Abfuhrtag bis": use the latest cutoff, then restore the saved one)
    until0 = int(p.cmd("WASTEUNTIL", expect=("WASTEUNTIL",)).split()[1])
    p.cmd("WASTEUNTIL 1440", expect=("WASTEUNTIL",))
    time.sleep(1)
    r0 = rem(state(p))  # a real collection today would also show now
    p.cmd("REMTEST 0 1", expect=("OK",))
    time.sleep(1)
    s = state(p)
    check(rem(s) == r0 + 1, f"waste today queued ({r0} real)")
    p.cmd(f"WASTEUNTIL {until0}", expect=("WASTEUNTIL",))
    shot("home_waste")
    ack_all(p)

    # Acknowledged reminders survive a reboot
    p.cmd(f"SCRTO {to0 or 120}", expect=("OK",))
    set_saver(p, int(saver0 or 0))
    p.s.write(b"REBOOT\n")
    time.sleep(3)
    wait(p, lambda s: "STATE home" in s, 60)
    p.cmd("SCRTO 0")  # runtime only: the saved timeout (e.g. 30 s) would blank the screen
    time.sleep(40)
    s = state(p)
    check(rem(s) == 0, f"no acknowledged reminder after reboot ({rem(s)} pending)")

    home(p)
    restore()  # lead settings and the real acknowledgements
    atexit.unregister(restore)
    print("FAILS:", FAILS if FAILS else "none")
    sys.exit(1 if FAILS else 0)


if __name__ == "__main__":
    main()
