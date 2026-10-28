"""PV (Growatt via Modbus TCP gateway, read only): hardware test on the test build.
Checks the panel's PV view (harness "PV"), the poll cadence (10 s), the DIAG
lines, compares the panel values with one direct FC04 read from this PC
(read only: function 04 only, one short connection; the gateway allows 4),
and takes screenshots v12_pv_*. Changes no settings, never writes to the
inverter or the gateway.
Usage: python tools/pv_test.py COM14 [GATEWAY_IP]
"""
import os, socket, struct, sys, time
sys.path.insert(0, "tools")
from panel_test import Panel
from info_test import wait, tapn, home

FAILS = []
def check(ok, what):
    print("PASS" if ok else "FAIL", what)
    if not ok: FAILS.append(what)

def fc04(gw, start=0, count=45, unit=1, tid=0x4242):
    """One read-only FC04 request. Returns the registers or None (no answer, e.g. at night)."""
    q = struct.pack(">HHHBBHH", tid, 0, 6, unit, 4, start, count)
    with socket.create_connection((gw, 502), 4) as s:
        s.settimeout(4)
        s.sendall(q)
        buf = b""
        try:
            while len(buf) < 9 + 2 * count:
                d = s.recv(512)
                if not d: break
                buf += d
                if len(buf) >= 9 and buf[7] & 0x80: return None
        except socket.timeout:
            return None
    if len(buf) < 9 + 2 * count or buf[7] != 4: return None
    return list(struct.unpack(">%dH" % count, buf[9:9 + 2 * count]))

def pv(p):
    line = p.cmd("PV", expect=("PV ",))
    return dict(kv.split("=", 1) for kv in line.split()[1:] if "=" in kv), line

def main():
    port = sys.argv[1]
    if port.upper() == "COM5": sys.exit("COM5 is the 3D printer")
    gw = sys.argv[2] if len(sys.argv) > 2 else "192.168.1.244"
    p = Panel(port)
    wait(p, lambda s: "STATE home" in s, 90)
    os.makedirs("v12shots", exist_ok=True)
    t0 = time.time()
    while True:
        d, line = pv(p)
        if d.get("have") == "1" or d.get("asleep") == "1" or time.time() - t0 > 60: break
        time.sleep(2)
    print(line)
    check(d.get("on") == "1" and d.get("gw") == f"{gw}:502" and d.get("unit") == "1", "enabled, default gateway 502 unit 1")
    check(d.get("have") == "1" or d.get("asleep") == "1", "reading received (or inverter asleep)")
    live = d.get("have") == "1" and d.get("stale") == "0"
    if live:
        check(d.get("conn") == "1", "persistent connection open")
        check(d.get("status") in ("0", "1", "3"), "status plausible")
        check(49.0 < float(d["hz"]) < 51.0 and all(200 < float(d[k]) < 260 for k in ("gv1", "gv2", "gv3")), "grid plausible")
        check(float(d["total"]) > 60000, "lifetime energy plausible")
        check(abs(float(d["hist_today"]) - float(d["today"])) < 0.05 or float(d["hist_today"]) == 0.0, "history follows Eac today")
        print("rated power", d.get("rated"), "W")
        time.sleep(0.3)
        try:
            regs = fc04(gw)
        except OSError as e:
            regs = None; print("note: PC cannot reach the gateway directly:", e)
        if regs is None:
            print("note: direct read got no answer (inverter just went to sleep?)")
        else:
            today = (regs[26] << 16 | regs[27]) / 10; total = (regs[28] << 16 | regs[29]) / 10
            ac = (regs[11] << 16 | regs[12]) / 10
            print(f"direct FC04: status {regs[0]} Pac {ac} W today {today} kWh total {total} kWh temp {regs[32] / 10} C")
            check(abs(float(d["today"]) - today) <= 0.1 and abs(float(d["total"]) - total) <= 0.1, "panel matches the direct read")
    # Cadence: one request per 10 s.
    d0, _ = pv(p); time.sleep(31); d1, line = pv(p); print(line)
    n = int(d1["polls"]) - int(d0["polls"])
    check(3 <= n <= 4, f"10 s poll cadence ({n} polls in 31 s)")
    h = p.cmd("DIAGHEALTH", expect=("DIAG health",))
    print(h)
    check(" pv live" in h or " pv asleep" in h, "DIAG health pv state")
    e = p.cmd("DIAGENERGY", expect=("DIAG energy pv today",), timeout=5)
    print(e)
    check("kWh" in e, "DIAGENERGY pv line")
    home(p); time.sleep(1.5)
    p.shot("v12shots/v12_pv_home.png")
    tapn(p, "home:energy", 1.5); time.sleep(1.0)
    p.shot("v12shots/v12_pv_strom.png")
    home(p); tapn(p, "cat:settings", 0.8); tapn(p, "set:g:energy", 0.8); tapn(p, "set:energy", 1.0)
    for _ in range(6): p.swipe(240, 400, 240, 120, 400); time.sleep(0.4)
    time.sleep(1.5); p.shot("v12shots/v12_pv_enset.png")
    home(p)
    print("RESULT", "OK" if not FAILS else f"{len(FAILS)} FAIL")
    sys.exit(1 if FAILS else 0)

if __name__ == "__main__":
    main()
