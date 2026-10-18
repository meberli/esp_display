"""PV (Growatt ShineLan) on-panel server: hardware test with a simulated stick.
Runs on a PC in the LAN: temporarily sets the accepted stick IP to this PC,
connects to <panel>:5279 and plays a ShineLan session (ping, announce, live
data protocol 05 and 02, buffered record, bad CRC), checks the replies and the
decoded values (harness PV), takes screenshots v12_pv_*, then restores the
stick IP to the default (192.168.1.39). Never talks to the real stick.
Usage: python tools/pv_sim_test.py COM14 PANEL_IP [PC_IP]
"""
import os, socket, struct, sys, time
sys.path.insert(0, "tools")
from panel_test import Panel
from info_test import state, wait, tapn, home

FAILS = []
def check(ok, what):
    print("PASS" if ok else "FAIL", what)
    if not ok: FAILS.append(what)

MASK = b"Growatt"
def xor(b):
    b = bytearray(b)
    for i in range(8, len(b)): b[i] ^= MASK[(i - 8) % 7]
    return bytes(b)

def crc16(d):
    c = 0xFFFF
    for x in d:
        c ^= x
        for _ in range(8): c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c

def frame(proto, rtype, payload, seq=7):
    rec = struct.pack(">HBBHBB", seq, 0, proto, len(payload) + 2, 1, rtype) + payload
    if proto == 2: return rec
    rec = xor(rec)
    return rec + struct.pack(">H", crc16(rec))

def classic(ac=3987.0, today=18.7):
    r = bytearray(220 - 8)
    def put(hexoff, ln, v):
        o = hexoff // 2 - 8
        r[o:o + ln] = int(v).to_bytes(ln, "big")
    r[0:10] = b"YU34130305"; r[10:20] = b"RW45030008"
    put(78, 2, 1); put(82, 4, (ac + 136) * 10); put(90, 2, 3512); put(94, 2, 62); put(98, 4, 21770)
    put(106, 2, 3398); put(110, 2, 57); put(114, 4, 19460); put(122, 4, ac * 10); put(130, 2, 4998)
    put(134, 2, 2311); put(150, 2, 2324); put(166, 2, 2298); put(182, 4, today * 10); put(190, 4, 412345)
    put(206, 2, 412)
    return bytes(r)

def recv_some(s, t=2.0):
    s.settimeout(t)
    try: return s.recv(4096)
    except socket.timeout: return b""

def pv(p):
    line = p.cmd("PV", expect=("PV ",))
    return dict(kv.split("=", 1) for kv in line.split()[1:] if "=" in kv), line

def set_pv_ip(p, value):
    home(p); tapn(p, "cat:settings", 0.8); tapn(p, "set:energy", 1.0)
    for _ in range(6): p.swipe(240, 400, 240, 120, 400); time.sleep(0.4)
    time.sleep(1.5)
    tapn(p, "pv:ip", 1.0)
    if value is None:
        tapn(p, "text:default", 1.5)
    else:
        tapn(p, "text:ta", 0.6); p.cmd("TYPE " + value); p.cmd("ENTER"); time.sleep(1.5)

def main():
    port, panel = sys.argv[1], sys.argv[2]
    if port.upper() == "COM5": sys.exit("COM5 is the 3D printer")
    me = sys.argv[3] if len(sys.argv) > 3 else socket.gethostbyname(socket.gethostname())
    p = Panel(port)
    wait(p, lambda s: "STATE home" in s, 90)
    os.makedirs("v12shots", exist_ok=True)
    d, line = pv(p); print(line)
    check(d.get("listen") == "1", "listening on 5279 with the default stick IP")
    # 1) not the configured stick -> refused
    rej0 = int(d.get("rejected", "0"))
    s = socket.create_connection((panel, 5279), 5)
    data = recv_some(s, 3); s.close()
    time.sleep(0.5)
    d, _ = pv(p)
    check(data == b"" and int(d["rejected"]) == rej0 + 1, "connection from another IP refused")
    try:
        set_pv_ip(p, me)
        time.sleep(4)
        run(p, panel, me)
    finally:
        set_pv_ip(p, None)  # back to the default 192.168.1.39
        time.sleep(3)
        d, line = pv(p); print(line)
        home(p)
    print("RESULT", "OK" if not FAILS else f"{len(FAILS)} FAIL")
    sys.exit(1 if FAILS else 0)

def run(p, panel, me):
    s = socket.create_connection((panel, 5279), 5)
    time.sleep(0.5)
    check("conn=1" in pv(p)[1], "simulated stick accepted")
    ping = frame(5, 0x16, b"YU34130305", seq=2)
    s.sendall(ping)
    check(recv_some(s) == ping, "ping echoed unchanged")
    s.sendall(frame(5, 0x03, b"YU34130305RW45030008" + bytes(40), seq=3))
    ack = recv_some(s, 2)
    check(len(ack) >= 11 and ack[3] == 5 and ack[5] == 3 and ack[7] == 3 and ack[8] == 0x47, "announce acked")
    tc = ack[11:] if len(ack) > 11 else recv_some(s, 3)
    ok = len(tc) > 10 and crc16(tc[:-2]) == struct.unpack(">H", tc[-2:])[0]
    plain = xor(tc[:-2]) if ok else b""
    when = plain[22:].decode(errors="replace") if ok else ""
    lt = time.strftime("%Y-%m-%d %H:%M", time.localtime())
    check(ok and plain[7] == 0x18 and plain[19] == 31 and when.startswith(lt[:15]), f"time sync (local time) '{when}'")
    s.sendall(frame(5, 0x04, classic()))
    ack = recv_some(s)
    check(len(ack) == 11 and ack[7] == 4 and ack[8] == 0x47 and crc16(ack[:9]) == struct.unpack(">H", ack[9:])[0], "live data acked (05)")
    time.sleep(0.5)
    d, line = pv(p); print(line)
    check(d.get("have") == "1" and d.get("stale") == "0" and d.get("ac") == "3987.0" and d.get("today") == "18.7"
          and d.get("total") == "41234.5" and d.get("gv3") == "229.8" and d.get("inv") == "RW45030008", "values decoded")
    bad = bytearray(frame(5, 0x04, classic(ac=1.0))); bad[-1] ^= 1
    s.sendall(bytes(bad))
    check(recv_some(s, 2) == b"" and pv(p)[0].get("ac") == "3987.0", "bad CRC ignored, no reply")
    s.sendall(frame(5, 0x50, classic(ac=2.0)))
    ack = recv_some(s)
    check(len(ack) == 11 and ack[7] == 0x50 and pv(p)[0].get("ac") == "3987.0", "buffered record acked, live value kept")
    home(p); time.sleep(1.5)
    p.shot("v12shots/v12_pv_home.png")
    tapn(p, "home:energy", 1.5); time.sleep(1.0)
    p.shot("v12shots/v12_pv_strom.png")
    s.sendall(frame(2, 0x04, classic(ac=2500.0, today=19.9)))
    ack = recv_some(s)
    check(ack == struct.pack(">HBBHBB", 7, 0, 2, 3, 1, 4) + b"\x00", "live data acked (02, plain)")
    time.sleep(0.5)
    d, _ = pv(p)
    check(d.get("ac") == "2500.0" and d.get("proto") == "2", "protocol 02 decoded")
    s.close()
    time.sleep(1.0)
    check("conn=0" in pv(p)[1], "disconnect noticed")
    s = socket.create_connection((panel, 5279), 5)
    s.sendall(ping); r = recv_some(s); s.close()
    check(r == ping, "reconnect works")
    home(p); tapn(p, "cat:settings", 0.8); tapn(p, "set:energy", 1.0)
    for _ in range(6): p.swipe(240, 400, 240, 120, 400); time.sleep(0.4)
    time.sleep(1.5); p.shot("v12shots/v12_pv_enset.png")

if __name__ == "__main__":
    main()
