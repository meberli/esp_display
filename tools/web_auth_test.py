"""Wartungs-Webseite: PIN, CSRF/Origin, Secrets-Export mit Bestaetigung am Panel.

Test build (serial harness). Switches the web page on, checks the API, and
switches it off again. Never prints secret values (only sizes / key counts).
Usage: python tools/web_auth_test.py COM14 <panel-ip>
"""
import json, re, sys, time, urllib.request, urllib.error
sys.path.insert(0, "tools")
from panel_test import Panel
from info_test import state, wait, tapn, home

FAILS = []
def check(ok, what):
    print("PASS" if ok else "FAIL", what)
    if not ok: FAILS.append(what)

def req(ip, path, pin=None, method="GET", body=None, origin=None, timeout=15):
    r = urllib.request.Request(f"http://{ip}{path}", data=body, method=method)
    if pin is not None: r.add_header("X-Panel-Pin", pin)
    if origin: r.add_header("Origin", origin)
    try:
        with urllib.request.urlopen(r, timeout=timeout) as resp:
            return resp.status, resp.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()

def main():
    port, ip = sys.argv[1], sys.argv[2]
    p = Panel(port)
    home(p); tapn(p, "cat:settings"); tapn(p, "set:g:sys", 0.8); tapn(p, "set:maint", 1.0)
    s = state(p)
    if "web=1" not in s:
        tapn(p, "maint:web", 1.0); s = state(p)
    m = re.search(r"pin=(\d{6})", s)
    check(m is not None, "web on, PIN shown " + ("ok" if m else s))
    pin = m.group(1)
    p.shot("v11shots/v11_web_pin.png")
    st, b = req(ip, "/")
    check(st == 200 and b"X-Panel-Pin" in b, f"page without PIN: {st}")
    st, _ = req(ip, "/status")
    check(st == 401, f"/status without PIN -> {st}")
    st, b = req(ip, "/status", pin)
    check(st == 200 and b"version" in b, f"/status with PIN -> {st}")
    st, _ = req(ip, "/status", "000000" if pin != "000000" else "111111")
    check(st == 401, f"wrong PIN -> {st}")
    st, _ = req(ip, "/restore", pin, "POST", b"{}", origin="http://evil.example")
    check(st == 403, f"foreign Origin POST -> {st}")
    st, b = req(ip, "/backup", pin)
    ok = st == 200
    keys = len(json.loads(b)["keys"]) if ok else 0
    check(ok and json.loads(b)["secrets"] is False, f"backup without secrets -> {st}, {keys} keys")
    st, _ = req(ip, "/backup?secrets=1", pin)
    check(st == 403, f"secrets export before confirm -> {st}")
    time.sleep(1.5)
    s = state(p)
    check("confirm=1" in s, "panel shows the confirm dialog")
    p.shot("v11shots/v11_web_confirm.png")
    tapn(p, "web:deny", 0.8)
    st, _ = req(ip, "/backup?secrets=1", pin)
    check(st == 403, f"after deny -> {st} (asks again)")
    time.sleep(1.5)
    tapn(p, "web:allow", 0.8)
    st, b = req(ip, "/backup?secrets=1", pin)
    check(st == 200 and json.loads(b)["secrets"] is True, f"after allow -> {st} ({len(b)} bytes, values not shown)")
    st, _ = req(ip, "/backup?secrets=1", pin)
    check(st == 403, f"grant is single-use -> {st}")
    time.sleep(1.5)
    tapn(p, "web:deny", 0.8)
    # lockout after 5 wrong PINs
    for _ in range(5): req(ip, "/status", "999999" if pin != "999999" else "888888")
    st, _ = req(ip, "/status", pin)
    check(st == 429, f"locked after 5 wrong PINs -> {st}")
    # switch off again
    home(p); tapn(p, "cat:settings"); tapn(p, "set:g:sys", 0.8); tapn(p, "set:maint", 1.0)
    if "web=1" in state(p): tapn(p, "maint:web", 1.0)
    check("web=0" in state(p), "web switched off again")
    print("RESULT", "OK" if not FAILS else f"{len(FAILS)} FAIL")

if __name__ == "__main__":
    main()
