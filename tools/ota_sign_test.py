"""Signierte Apps: Web-Upload muss unsignierte / falsch signierte Images ablehnen.

Test build (serial harness). Usage:
  python tools/ota_sign_test.py COM14 <ip> <unsigned.bin> <wrongkey.bin> <signed.bin>
The signed image is installed last (panel reboots into it).
Needs a decent Wi-Fi link (1.7 MB upload). Without one, test the same check
over USB: esptool write-flash 0x620000 <img>, then harness IMGVERIFY
(runs esp_image_verify like esp_ota_end), then erase-region 0x620000 0x600000.
"""
import json, re, sys, time
sys.path.insert(0, "tools")
from panel_test import Panel
from info_test import state, tapn, home
from web_auth_test import req, check, FAILS

def web_on(p):
    home(p); tapn(p, "cat:settings"); tapn(p, "set:g:sys", 0.8); tapn(p, "set:maint", 1.0)
    s = state(p)
    if "web=1" not in s:
        tapn(p, "maint:web", 1.0); s = state(p)
    return re.search(r"pin=(\d{6})", s).group(1)

def main():
    port, ip, unsigned, wrong, signed = sys.argv[1:6]
    p = Panel(port)
    pin = web_on(p)
    org = f"http://{ip}"
    for name, path in (("unsigned", unsigned), ("wrong key", wrong)):
        st, b = req(ip, "/update", pin, "POST", open(path, "rb").read(), origin=org, timeout=700)
        check(st == 500 and b"Signatur" in b, f"{name} image rejected -> {st} {b[:90]!r}")
        time.sleep(2)
    bad = {"format": "esp_display-settings", "version": 1, "cfg_ver": 1, "secrets": False, "keys": {"ota_url": {"t": "str", "v": "http://example.com/ota.json"}}}
    st, b = req(ip, "/restore", pin, "POST", json.dumps(bad).encode(), origin=org, timeout=700)
    check(st >= 400, f"http OTA URL via restore rejected -> {st} {b[:90]!r}")
    st, b = req(ip, "/update", pin, "POST", open(signed, "rb").read(), origin=org, timeout=700)
    check(st == 200, f"correctly signed image accepted -> {st} {b[:60]!r}")
    time.sleep(25)
    p2 = Panel(port)
    s = state(p2)
    check("ver=" in s or len(s) > 0, "panel back after signed OTA: " + s[:80])
    home(p2); tapn(p2, "cat:settings"); tapn(p2, "set:maint", 1.0)
    check("web=0" in state(p2), "web off after reboot")
    print("RESULT", "OK" if not FAILS else f"{len(FAILS)} FAIL")

if __name__ == "__main__":
    main()
