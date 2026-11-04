"""Firmware-check UI test (test firmware, COM14 only).

  python tools/ota_ui_test.py --port COM14 --out v10shots
"""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(__file__))
from panel_test import Panel  # noqa: E402
from info_test import check, state, wait, tapn, home, FAILS  # noqa: E402


def maint_state(p):
    return state(p)


def open_maint(p):
    home(p)
    tapn(p, "cat:settings")
    tapn(p, "set:g:sys", 0.8); tapn(p, "set:maint", 1.0)


def run_check(p, timeout=40):
    tapn(p, "maint:ota", 0.5)
    s, t0 = "", time.time()
    while time.time() - t0 < timeout:
        s = maint_state(p)
        if "otaui=1" not in s:
            break
        time.sleep(0.5)
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM14")
    ap.add_argument("--out", default="v10shots")
    a = ap.parse_args()
    if a.port.upper() == "COM5":
        sys.exit("COM5 is the 3D printer")
    os.makedirs(a.out, exist_ok=True)
    p = Panel(a.port)
    shot = lambda n: p.shot(os.path.join(a.out, f"v10_{n}.png"))
    wait(p, lambda s: "STATE home" in s, 60)
    time.sleep(5)
    open_maint(p)
    s = maint_state(p)
    check("otaui=0" in s, "initial: only 'Firmware pruefen' " + s)
    shot("idle")

    p.cmd("VERSET 0.7.4", expect=("VERSION",))
    s = run_check(p)
    check("otaui=2" in s, "running 0.7.4 vs latest 0.7.4: Aktuell, no install " + s)
    time.sleep(0.5)
    shot("current")

    p.cmd("VERSET 0.7.3", expect=("VERSION",))
    s = run_check(p)
    check("otaui=3" in s and "new=0.7." in s, "running 0.7.3: install offered " + s)
    time.sleep(0.5)
    shot("newer")

    # (no further tap on the tile here: in this state it installs the update)

    # Advanced: own update URL, version read from the image header (Range).
    # Restored to "Standard verwenden" afterwards.
    tapn(p, "maint:url", 0.8)
    tapn(p, "text:ta", 0.8)  # opens the keyboard
    p.cmd("TYPE https://github.com/meberli/esp_display/releases/download/v0.7.4/loxone_panel.bin", expect=("OK",))
    p.cmd("ENTER", expect=("OK",))
    s, _ = "", 0
    wait(p, lambda s: "STATE maint" in s, 15)
    s = maint_state(p)
    check("otaui=0" in s, "back on Wartung: starts with 'Firmware pruefen' again " + s)
    p.cmd("VERSET 0.7.3", expect=("VERSION",))
    s = run_check(p)
    check("otaui=3" in s and "new=0.7.4" in s, "own URL: version from image header, install offered " + s)
    time.sleep(0.5)
    shot("custom_url")
    p.cmd("VERSET 0.7.4", expect=("VERSION",))
    tapn(p, "maint:url", 0.8)
    tapn(p, "text:default", 0.5)
    wait(p, lambda s: "STATE maint" in s, 15)
    s = run_check(p)
    check("otaui=2" in s, "default URL restored, 0.7.4 is current " + s)
    p.cmd("VERSET", expect=("VERSION",))
    print("RESULT", "OK" if not FAILS else f"{len(FAILS)} FAIL")


if __name__ == "__main__":
    main()
