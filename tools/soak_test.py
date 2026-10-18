"""Memory soak (test firmware, COM14 only): for >= MINUTES, repeatedly walk all
pages (tools/ui_pages_test.py: weather, warnings, transit, calendar, cameras,
energy, settings) and force an OTA check (Firmware pruefen), then collect the
DIAG heap/mem lines. Everything the panel logs goes to soak_<tag>.log.
  python tools/soak_test.py COM14 25 before
"""
import os, re, subprocess, sys, time
sys.path.insert(0, os.path.dirname(__file__))
from panel_test import Panel  # noqa: E402
from info_test import wait  # noqa: E402
from ota_ui_test import open_maint, run_check  # noqa: E402

port, minutes, tag = sys.argv[1], float(sys.argv[2]), sys.argv[3]
if port.upper() == "COM5":
    sys.exit("COM5 is the 3D printer")
import serial as _serial
_r = _serial.Serial(); _r.port = port; _r.dtr = False; _r.rts = False; _r.open()
_r.rts = True; time.sleep(0.2); _r.rts = False; _r.close()  # fresh boot: minima start from zero
time.sleep(45)
log = open(f"soak_{tag}.log", "w", encoding="utf-8")
t0 = time.time(); rounds = 0
while time.time() - t0 < minutes * 60:
    r = subprocess.run([sys.executable, "tools/ui_pages_test.py", port], capture_output=True, text=True)
    log.write(r.stdout + r.stderr); log.flush()
    p = Panel(port)
    try:
        wait(p, lambda s: "STATE" in s, 30)
        from info_test import home as _home, state as _state
        for _ in range(4):
            if "STATE home" in _state(p): break
            from panel_test import back as _back
            try: _back(p)
            except Exception: pass
            time.sleep(1.0); _home(p); time.sleep(1.5)
        open_maint(p)
        s = run_check(p)
        log.write("OTACHECK " + s + "\n")
        from info_test import home
        home(p)
    finally:
        p.s.close()
    rounds += 1
    print(f"round {rounds} at {(time.time()-t0)/60:.1f} min", flush=True)
# Final: wait for two periodic DIAG blocks (60 s period)
import serial
s = serial.Serial(); s.port = port; s.baudrate = 460800; s.timeout = 0.5; s.dtr = False; s.rts = False; s.open()
end = time.time() + 130
while time.time() < end:
    l = s.readline().decode(errors="replace").strip()
    if l: log.write(l + "\n")
log.close()
txt = open(f"soak_{tag}.log", encoding="utf-8").read()
for pat in (r"DIAG heap .*", r"DIAG mem .*", r"DIAG stack .*"):
    m = re.findall(pat, txt)
    if m: print(m[-1])
for l in re.findall(r"HEAP: .*", txt): print(l)
print("OTA checks:", txt.count("OTACHECK"), " TLS waits:", len(re.findall(r"TLS: .*", txt)), " rounds:", rounds)
