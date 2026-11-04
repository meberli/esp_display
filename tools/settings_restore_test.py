"""Restore validation (schema) + web auto-off. Test build, serial harness.

Never prints values: only status codes, key names and counts.
Usage: python tools/settings_restore_test.py COM14 <panel-ip>
"""
import json, re, sys, time
sys.path.insert(0, "tools")
from panel_test import Panel
from info_test import state, wait, tapn, home
from web_auth_test import req

FAILS = []
def check(ok, what):
    print("PASS" if ok else "FAIL", what)
    if not ok: FAILS.append(what)

def web_on(p):
    home(p); tapn(p, "cat:settings"); tapn(p, "set:g:sys", 0.8); tapn(p, "set:maint", 1.0)
    s = state(p)
    if "web=1" not in s:
        tapn(p, "maint:web", 1.0); s = state(p)
    return re.search(r"pin=(\d{6})", s).group(1)

def main():
    port, ip = sys.argv[1], sys.argv[2]
    p = Panel(port)
    pin = web_on(p)
    st, b = req(ip, "/backup", pin)
    doc = json.loads(b)
    check(st == 200 and doc.get("cfg_ver") == 1, f"backup has cfg_ver -> {doc.get('cfg_ver')}, {len(doc['keys'])} keys")
    check("wifi_pass" not in doc["keys"] and "lox_pass" not in doc["keys"], "no secrets in plain backup")
    check("cfg_ver" not in doc["keys"] and "wifi_seeded" not in doc["keys"], "internal keys not exported")
    def bad(mut, what, code=400, expect=b""):
        d = json.loads(b); mut(d)
        st2, r = req(ip, "/restore", pin, "POST", json.dumps(d).encode())
        check(st2 == code and expect in r, f"{what} -> {st2} {r.decode(errors='replace')[:120]}")
    bad(lambda d: d["keys"].__setitem__("scr_saver", {"t": "u8", "v": 9}), "scr_saver=9 rejected", expect=b"scr_saver")
    bad(lambda d: d["keys"].__setitem__("ota_url", {"t": "str", "v": "http://example.com/x.bin"}), "http ota_url rejected", expect=b"https")
    bad(lambda d: d["keys"].__setitem__("scr_to", {"t": "str", "v": "120"}), "wrong type rejected", expect=b"scr_to")
    bad(lambda d: d["keys"].__setitem__("loc_lat", {"t": "blob", "v": "zz"}), "bad hex rejected", expect=b"loc_lat")
    bad(lambda d: d.__setitem__("cfg_ver", 99), "newer cfg_ver rejected", expect=b"neuerer")
    bad(lambda d: d.__setitem__("format", "x"), "not a settings file")
    # nothing was written by the rejected files: backup unchanged
    st, b2 = req(ip, "/backup", pin)
    check(json.loads(b2)["keys"] == doc["keys"], "rejected restores changed nothing")
    # auto-off: shorten to 20 s, no requests, expect web=0
    p.cmd("WEBIDLE 20")
    time.sleep(26)
    check("web=0" in state(p), "web switched itself off after idle time")
    p.cmd("WEBIDLE 0")
    # valid restore of the same values + one unknown key: accepted, panel restarts
    pin = web_on(p)
    d = json.loads(b); d["keys"]["zz_future"] = {"t": "u8", "v": 1}
    st, r = req(ip, "/restore", pin, "POST", json.dumps(d).encode())
    check(st == 200 and b"zz_future" in r, f"valid restore (same values) -> {st} {r.decode(errors='replace')[:100]}")
    time.sleep(12)  # restart: the harness does not answer while booting
    wait(p, lambda s: "STATE home" in s, 120)
    home(p); tapn(p, "cat:settings"); tapn(p, "set:g:sys", 0.8); tapn(p, "set:maint", 1.0)
    check("web=0" in state(p), "after restart web is off")
    print("RESULT", "OK" if not FAILS else f"{len(FAILS)} FAIL")

if __name__ == "__main__":
    main()
