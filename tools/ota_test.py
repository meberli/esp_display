"""OTA over the maintenance web server: upload a .bin to /update and check
that the panel reboots into the other slot.

  set PANEL_IP=<panel-ip>
  python tools/ota_test.py --port COM14 --bin build-test/loxone_panel.bin
Needs the test firmware (serial harness).
"""
import argparse
import json
import os
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(__file__))
from panel_test import Panel  # noqa: E402
from info_test import state, wait, tapn, home  # noqa: E402


def status(ip):
    return json.load(urllib.request.urlopen(f"http://{ip}/status", timeout=5))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM14")
    ap.add_argument("--bin", default="build-test/loxone_panel.bin")
    a = ap.parse_args()
    if a.port.upper() == "COM5":
        sys.exit("COM5 is the 3D printer")
    ip = os.environ["PANEL_IP"]
    p = Panel(a.port)
    wait(p, lambda s: "STATE home" in s, 60)
    tapn(p, "cat:settings")
    tapn(p, "set:maint")
    tapn(p, "maint:web")
    assert "web=1" in state(p), "web server did not start"
    before = status(ip)["partition"]
    data = open(a.bin, "rb").read()
    print(f"before: {before}, uploading {len(data)} bytes")
    req = urllib.request.Request(f"http://{ip}/update", data=data, method="POST",
                                 headers={"Content-Type": "application/octet-stream"})
    t0 = time.time()
    print("response:", urllib.request.urlopen(req, timeout=180).read().decode()[:200])
    print(f"upload {time.time() - t0:.1f}s")
    time.sleep(8)
    s = wait(p, lambda s: "STATE home" in s, 90)
    print("after reboot:", s)
    # web server is off after reboot (not persisted): switch on again to read /status
    time.sleep(10)
    tapn(p, "cat:settings")
    tapn(p, "set:maint")
    tapn(p, "maint:web")
    after = status(ip)["partition"]
    tapn(p, "maint:web")
    home(p)
    ok = after != before
    print(("PASS" if ok else "FAIL") + f" OTA {before} -> {after}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
