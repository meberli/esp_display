"""Remote test client for the panel's serial test harness.

Needs the esp32-4848s040c-test firmware. Examples:
  python tools/panel_test.py --port COM14 ping
  python tools/panel_test.py --port COM14 shot home.png
  python tools/panel_test.py --port COM14 tap 127 270
  python tools/panel_test.py --port COM14 swipe 475 240 250 240 300
  python tools/panel_test.py --port COM14 smoke --out shots
"""
import argparse
import base64
import os
import sys
import time
import zlib

import serial
from PIL import Image

BAUD = 460800  # 921600 drops byte runs on this CH340 link


class Panel:
    def __init__(self, port, baud=BAUD, keep_awake=True):
        # Keep DTR/RTS low so opening the port does not reset the ESP32.
        self.s = serial.Serial()
        self.s.port = port
        self.s.baudrate = baud
        self.s.timeout = 0.05
        self.s.dtr = False
        self.s.rts = False
        self.s.open()
        if hasattr(self.s, 'set_buffer_size'):
            self.s.set_buffer_size(rx_size=1 << 20, tx_size=4096)
        time.sleep(0.2)
        self.s.write(b"\n")  # clear any half line on the panel
        time.sleep(0.1)
        self.s.reset_input_buffer()
        self.rx = bytearray()
        if keep_awake:
            try:  # no screen timeout during the suites (runtime only, not saved)
                self.cmd("SCRTO 0", timeout=2)
            except (TimeoutError, RuntimeError):
                pass

    def _line(self, deadline):
        # Bulk reads: byte-by-byte reading is too slow at 921600 and the
        # Windows CH340 driver then drops data.
        while True:
            i = self.rx.find(b"\n")
            if i >= 0:
                line = bytes(self.rx[:i])
                del self.rx[:i + 1]
                return line.rstrip(b"\r").decode("utf-8", "replace")
            if time.time() >= deadline:
                raise TimeoutError("no reply")
            self.rx += self.s.read(max(1, self.s.in_waiting))

    def cmd(self, text, expect=("OK", "PONG", "ERR"), timeout=3):
        self.s.write((text + "\n").encode())
        deadline = time.time() + timeout
        while True:
            line = self._line(deadline)
            if line.startswith(expect):
                if line.startswith("ERR"):
                    raise RuntimeError(f"{text}: {line}")
                return line
            if line:
                print("  log:", line)

    def pos(self, name):
        """Centre of a named, visible widget (harness POS), e.g. 'cat:licht'."""
        line = self.cmd(f"POS {name}", expect=("POS", "ERR"))
        _, x, y = line.split()
        return int(x), int(y)

    def items(self):
        """ITEM lines of the open Wi-Fi or location list (harness LIST)."""
        self.s.write(b"LIST\n")
        deadline = time.time() + 3
        out = []
        while True:
            line = self._line(deadline)
            if line == "ITEM_END":
                return out
            if line.startswith("ITEM "):
                out.append(line[5:])

    def ping(self):
        return self.cmd("PING")

    def tap(self, x, y):
        self.cmd(f"TAP {x} {y}")

    def press(self, x, y):
        self.cmd(f"PRESS {x} {y}")

    def release(self):
        self.cmd("RELEASE")

    def swipe(self, x1, y1, x2, y2, ms=300):
        self.cmd(f"SWIPE {x1} {y1} {x2} {y2} {ms}")

    def _collect(self, data, have, end_tag, deadline):
        while True:
            line = self._line(deadline)
            if line == end_tag:
                return
            if not line.startswith("L "):
                if line and not line.startswith("SHOT"):
                    print("  log:", line[:80])
                continue
            try:
                _, off, crc, b64 = line.split(" ", 3)
                chunk = base64.b64decode(b64, validate=True)
                off = int(off)
            except Exception:
                continue  # damaged line, asked again below
            if zlib.crc32(chunk) == int(crc, 16) and off + len(chunk) <= len(data):
                data[off:off + len(chunk)] = chunk
                have.add(off)

    def shot(self, path, timeout=60, test=False, request=True):
        if request:
            self.s.write(b"SHOT TEST\n" if test else b"SHOT\n")
        deadline = time.time() + timeout
        while True:
            line = self._line(deadline)
            if line.startswith("SHOT_BEGIN"):
                break
            if line.startswith("ERR"):
                raise RuntimeError(line)
            if line:
                print("  log:", line)
        _, w, h, n, lb = line.split()
        w, h, n, lb = int(w), int(h), int(n), int(lb)
        data = bytearray(n)
        have = set()
        self._collect(data, have, "SHOT_END", deadline)
        offsets = list(range(0, n, lb))
        for attempt in range(10):
            missing = [o for o in offsets if o not in have]
            if not missing:
                break
            if attempt == 0:
                print(f"  re-requesting {len(missing)}/{len(offsets)} lines")
            for o in missing:
                self.s.write(f"SHOTR {o} {lb}\n".encode())
            for o in missing:
                self._collect(data, have, "SHOTR_END", deadline)
        missing = [o for o in offsets if o not in have]
        if missing:
            raise RuntimeError(f"{len(missing)} lines still missing")
        data = bytes(data)
        if test:
            ok = all(data[i] == (i & 255) for i in range(n))
            print("  test pattern", "OK" if ok else "BAD")
            return path
        img = Image.frombytes("RGB", (w, h), rgb565_to_rgb888(data))
        img.save(path)
        return path


def rgb565_to_rgb888(data):
    out = bytearray(len(data) // 2 * 3)
    j = 0
    for i in range(0, len(data), 2):
        v = data[i] | (data[i + 1] << 8)
        r = (v >> 11) & 0x1F
        g = (v >> 5) & 0x3F
        b = v & 0x1F
        out[j] = (r << 3) | (r >> 2)
        out[j + 1] = (g << 2) | (g >> 4)
        out[j + 2] = (b << 3) | (b >> 2)
        j += 3
    return bytes(out)


# Home category buttons by harness name (positions come from POS, not constants).
CARD_NAMES = [("favoriten", "cat:fav"), ("licht", "cat:licht"), ("storen", "cat:storen"), ("schalter", "cat:schalter")]


def cards(p):
    """[(name, x, y)] of the home category buttons; the home screen must be open."""
    return [(name, *p.pos(key)) for name, key in CARD_NAMES]


def back(p):
    # Right-edge swipe to the left = back_level() in panel_ui.cpp.
    p.swipe(476, 240, 260, 240, 300)


def smoke(p, out):
    os.makedirs(out, exist_ok=True)
    print(p.ping())
    p.shot(os.path.join(out, "00_home.png"))
    for i, (name, x, y) in enumerate(cards(p), 1):
        p.tap(x, y)
        time.sleep(1.5)
        p.shot(os.path.join(out, f"{i:02d}_{name}.png"))
        back(p)
        time.sleep(1.0)
        p.shot(os.path.join(out, f"{i:02d}_{name}_back.png"))
    print("smoke done ->", out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM14")
    ap.add_argument("--baud", type=int, default=BAUD)
    ap.add_argument("action")
    ap.add_argument("args", nargs="*")
    ap.add_argument("--out", default="shots")
    a = ap.parse_args()
    p = Panel(a.port, a.baud)
    act, args = a.action.lower(), a.args
    if act == "ping":
        print(p.ping())
    elif act == "shottest":
        p.shot("unused", test=True)
    elif act == "shot":
        print(p.shot(args[0] if args else "shot.png"))
    elif act == "tap":
        p.tap(*map(int, args))
    elif act == "press":
        p.press(*map(int, args))
    elif act == "release":
        p.release()
    elif act == "swipe":
        p.swipe(*map(int, args))
    elif act == "smoke":
        smoke(p, a.out)
    else:
        sys.exit("unknown action")


if __name__ == "__main__":
    main()
