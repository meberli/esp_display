#!/usr/bin/env python3
"""Fail if a UI string in main/ uses a character outside the Inter text fonts.

Inter Regular/SemiBold (14/16/20) include U+0020-007E, U+00A0-00FF,
U+2013 (en dash) and U+2026 (ellipsis). Em dash U+2014 is not in the font
and shows as a missing-glyph box. Skips generated font files and icon code
points in material_icons.h (those strings are drawn with the icon fonts).
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "main"
ALLOWED = set(range(0x20, 0x7F)) | set(range(0xA0, 0x100)) | {0x00, 0x09, 0x0A, 0x0D, 0x2013, 0x2026}
SKIP_NAMES = {"material_icons.h"}
SKIP_PREFIX = "font_"

def strings_in(src: bytes) -> list[tuple[int, str]]:
    out: list[tuple[int, str]] = []
    i = 0
    n = len(src)
    line = 1
    while i < n:
        if src[i] == 0x0A:
            line += 1
            i += 1
            continue
        if src[i : i + 2] == b"//":
            while i < n and src[i] != 0x0A:
                i += 1
            continue
        if src[i : i + 2] == b"/*":
            i += 2
            while i + 1 < n and src[i : i + 2] != b"*/":
                if src[i] == 0x0A:
                    line += 1
                i += 1
            i += 2
            continue
        if src[i] == 0x22:
            start = line
            i += 1
            buf = bytearray()
            while i < n and src[i] != 0x22:
                if src[i] == 0x0A:
                    line += 1
                    i += 1
                    continue
                if src[i] == 0x5C and i + 1 < n:
                    nxt = src[i + 1]
                    if nxt == ord("x") and i + 3 < n:
                        try:
                            buf.append(int(src[i + 2 : i + 4], 16))
                        except ValueError:
                            pass
                        i += 4
                        continue
                    mapping = {
                        ord("n"): 10,
                        ord("t"): 9,
                        ord("r"): 13,
                        ord("0"): 0,
                        ord('"'): 0x22,
                        ord("'"): 0x27,
                        ord("\\"): 0x5C,
                    }
                    buf.append(mapping.get(nxt, nxt))
                    i += 2
                    continue
                buf.append(src[i])
                i += 1
            i += 1
            try:
                s = buf.decode("utf-8")
            except UnicodeDecodeError:
                s = buf.decode("latin-1", "replace")
            out.append((start, s))
            continue
        i += 1
    return out


def main() -> int:
    hits = []
    for path in sorted(ROOT.rglob("*")):
        if path.suffix not in {".cpp", ".h", ".inc", ".c"}:
            continue
        if path.name.startswith(SKIP_PREFIX) or path.name in SKIP_NAMES:
            continue
        for line, s in strings_in(path.read_bytes()):
            bad = [f"U+{ord(ch):04X}" for ch in s if ord(ch) not in ALLOWED]
            if bad:
                hits.append(f"{path.relative_to(ROOT.parent)}:{line}: {', '.join(bad)} in {s[:80]!r}")
    if hits:
        print("Characters outside the Inter text font:")
        print("\n".join(hits))
        return 1
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
