"""Wi-Fi robustness test via the serial harness (test firmware, COM14 only).

  python tools/wifi_test.py --port COM14 --out v8shots

A wrong password chosen in Einstellungen must revert to the previous network;
boot without Wi-Fi and boot with a wrong saved password must open the WLAN
selection, which closes again when the saved network works. The real SSID and
password are read from main/panel_config.h and never printed.
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from panel_test import Panel  # noqa: E402
from info_test import check, FAILS  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def creds():
    out = {}
    for line in open(os.path.join(ROOT, "main", "panel_config.h"), encoding="utf-8"):
        m = re.match(r'\s*#define\s+(WIFI_SSID|WIFI_PASSWORD)\s+"(.*)"', line)
        if m:
            out[m.group(1)] = m.group(2)
    return out["WIFI_SSID"], out["WIFI_PASSWORD"]


class LogPanel(Panel):
    """Keeps the log lines (with the SSID masked) for checks."""

    def __init__(self, port, secret):
        self.log = []
        self.secret = secret
        super().__init__(port)

    def cmd(self, text, expect=("OK", "PONG", "ERR"), timeout=3):
        self.s.write((text + "\n").encode())
        deadline = time.time() + timeout
        while True:
            line = self._line(deadline)
            if line.startswith(expect):
                if line.startswith("ERR"):
                    raise RuntimeError(f"{text}: {line}")
                return line.replace(self.secret, "<ssid>")
            if line:
                line = line.replace(self.secret, "<ssid>")
                self.log.append((time.time(), line))
                print("  log:", line)

    def drain(self, secs):
        end = time.time() + secs
        while time.time() < end:
            try:
                self.cmd("PING", expect=("PONG",), timeout=2)
            except TimeoutError:
                pass
            time.sleep(0.5)

    def saw(self, pat, since=0):
        return any(t >= since and re.search(pat, l) for t, l in self.log)


def state(p):
    return p.cmd("STATE", expect=("STATE",), timeout=3)


def wait(p, pred, timeout):
    t0 = time.time()
    s = ""
    while time.time() - t0 < timeout:
        try:
            s = state(p)
        except TimeoutError:
            continue
        if pred(s):
            return s, time.time() - t0
        time.sleep(0.5)
    return s, time.time() - t0


def tapn(p, name, settle=0.6):
    x, y = p.pos(name)
    p.tap(x, y)
    time.sleep(settle)


def reboot(p):
    p.s.write(b"REBOOT\n")
    time.sleep(4)
    p.s.reset_input_buffer()
    p.rx = bytearray()
    for _ in range(20):
        try:
            p.cmd("SCRTO 0", timeout=2)
            return
        except (TimeoutError, RuntimeError):
            time.sleep(0.5)


def pick_ssid(p, ssid, password):
    s, _ = wait(p, lambda s: re.search(r"STATE wifi \d+ scanning=0", s), 30)
    items = p.items()
    idx = next((i for i, it in enumerate(items) if it.split(" ", 2)[-1] == ssid), -1)
    check(idx >= 0, f"own network in scan list ({len(items)} networks)")
    if idx < 0:
        return False
    tapn(p, f"wifi:{idx}")
    p.cmd("TYPE " + password, expect=("OK",))
    p.cmd("ENTER", expect=("OK",))
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM14")
    ap.add_argument("--out", default="v8shots")
    a = ap.parse_args()
    if a.port.upper() == "COM5":
        sys.exit("COM5 is the 3D printer")
    os.makedirs(a.out, exist_ok=True)
    ssid, pw = creds()
    p = LogPanel(a.port, ssid)
    shot = lambda n: p.shot(os.path.join(a.out, f"v8_{n}.png"))

    # 0) normal boot (WPA3 quirk: a first rejection must not fail the boot)
    reboot(p)
    s, el = wait(p, lambda s: "STATE home" in s, 60)
    check("STATE home" in s, f"normal boot reaches home ({el:.0f} s)")
    check(not p.saw("WLAN selection"), "normal boot: no WLAN selection")
    print("INFO first-attempt rejections at boot:", sum(1 for _, l in p.log if "disconnect reason" in l))

    # 1) wrong password chosen in Einstellungen -> revert
    tapn(p, "cat:settings")
    tapn(p, "set:wifi")
    t0 = time.time()
    if pick_ssid(p, ssid, "wrong-pass-123"):
        s, el = wait(p, lambda s: "connecting=0" in s, 90)
        check("connecting=0" in s, f"wrong password: attempt ended after {el:.0f} s")
        time.sleep(1)
        shot("wrong_password")
        check(p.saw(r"failed \(status 1", t0), "wrong password classified as auth failure")
        check(p.saw(r"back on .*: connected", t0), "reverted to the previous network and connected")
        check(p.saw(r"Passwort falsch\? Zurück zu", t0), "panel message 'Passwort falsch? Zurück zu ...'")
        check(not p.saw(r"connected and saved", t0), "wrong password not saved")
    p.swipe(240, 478, 240, 300, 250)
    time.sleep(1)

    # 2) boot without Wi-Fi -> WLAN selection
    p.cmd("WIFIFORGET", expect=("OK",))
    reboot(p)
    s, el = wait(p, lambda s: s.startswith("STATE wifi"), 30)
    check(s.startswith("STATE wifi"), f"no Wi-Fi: WLAN selection opened ({el:.0f} s)")
    wait(p, lambda s: "scanning=0" in s, 20)
    time.sleep(1)
    shot("no_wifi")
    p.cmd("WIFIRESTORE", expect=("OK",))

    # 3) boot with a wrong saved password -> WLAN selection after ~30 s, saved creds kept
    p.cmd("WIFIPASS wrong-pass-456", expect=("OK",))
    reboot(p)
    t0 = time.time()
    s, el = wait(p, lambda s: s.startswith("STATE wifi"), 75)
    check(s.startswith("STATE wifi"), f"wrong saved password: WLAN selection after {el:.0f} s")
    check(el > 25, "not opened on the first rejection")
    wait(p, lambda s: "scanning=0" in s, 20)
    time.sleep(1)
    shot("boot_failed")
    # saved network "comes back" (correct creds restored, station restarted)
    p.cmd("WIFIRESTORE", expect=("OK",))
    p.cmd("WIFIRELOAD", expect=("OK",))
    s, el = wait(p, lambda s: "STATE home" in s, 60)
    check("STATE home" in s, f"saved network back: selection closed, home after {el:.0f} s")
    check(p.saw("is back: closing", t0), "auto-close logged")

    # 4) real network chosen again with the right password (WPA3 quirk must not revert)
    tapn(p, "cat:settings")
    tapn(p, "set:wifi")
    t0 = time.time()
    if pick_ssid(p, ssid, pw):
        s, el = wait(p, lambda s: not s.startswith("STATE wifipass connecting=1"), 60)
        check(p.saw("connected and saved", t0), f"correct password accepted ({el:.0f} s), no revert")
        check(not p.saw("back to", t0), "no premature revert")
    time.sleep(1)
    shot("settings_ok")
    p.swipe(240, 478, 240, 300, 250)
    s, _ = wait(p, lambda s: "STATE home" in s, 20)
    shot("home")
    print("RESULT", "OK" if not FAILS else f"{len(FAILS)} FAIL")


if __name__ == "__main__":
    main()
