"""Checks that a firmware image contains none of the local secret values.

  python tools/check_secrets.py build-release/loxone_panel.bin
Reads the values from the gitignored sdkconfig.defaults.local and
main/panel_config.h / include/panel_config.h and searches the image for them
(also the iCal URL's secret path part). Prints only the setting names, never
the values. Exit code 1 if any value is found.

  python tools/check_secrets.py --generic build/loxone_panel.bin
CI mode (clean checkout, no local values): looks for secret-shaped data
instead (private iCal tokens, Google calendar private URLs, Windy keys
assigned in sdkconfig).
"""
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(__file__), "..")


def secrets():
    out = {}
    p = os.path.join(ROOT, "sdkconfig.defaults.local")
    if os.path.exists(p):
        for line in open(p, encoding="utf-8"):
            m = re.match(r'(CONFIG_\w+)="(.*)"\s*$', line.strip())
            if m and m.group(2):
                out[m.group(1)] = m.group(2)
    for rel in ("main/panel_config.h", "include/panel_config.h"):
        p = os.path.join(ROOT, rel)
        if os.path.exists(p):
            for line in open(p, encoding="utf-8"):
                m = re.match(r'\s*#define\s+(WIFI_PASSWORD|WIFI_SSID|LOXONE_USER|LOXONE_PASSWORD|LOXONE_HOST)\s+"(.*)"', line)
                if m and m.group(2):
                    out[f"{rel}:{m.group(1)}"] = m.group(2)
    # Secret part of the Google iCal URL (".../ical/<id>/private-<token>/basic.ics")
    for k, v in list(out.items()):
        for part in re.findall(r"private-[0-9a-f]{8,}", v):
            out[k + " (token)"] = part
    return out


GENERIC = [
    (rb"private-[0-9a-f]{16,}", "iCal private token"),
    (rb"calendar\.google\.com/calendar/ical/[^\0]{0,80}private", "Google iCal private URL"),
    (rb"eyJ[A-Za-z0-9_-]{10,}\.eyJ[A-Za-z0-9_-]{10,}\.", "JWT (OIDC access/refresh token)"),
    (rb"client_secret=[A-Za-z0-9_.~-]{8,}", "OAuth client_secret"),
]


def generic(path):
    data = open(path, "rb").read()
    found = 0
    for rx, name in GENERIC:
        hit = re.search(rx, data) is not None
        print(f"{'FOUND ' if hit else 'absent'}  {name}")
        found += hit
    cfg = os.path.join(os.path.dirname(os.path.abspath(path)), "..", "sdkconfig")
    if os.path.exists(cfg):
        for line in open(cfg, encoding="utf-8"):
            m = re.match(r'(CONFIG_PANEL_(?:WINDY_API_KEY|ICAL_URL|WIFI_\w+|LOXONE_\w*(?:PASS|USER)\w*))="(.+)"', line.strip())
            if m:
                print(f"FOUND   {m.group(1)} set in sdkconfig")
                found += 1
    print(f"{len(data)} bytes, {found} generic finding(s)")
    sys.exit(1 if found else 0)


def main():
    if sys.argv[1] == "--generic":
        generic(sys.argv[2])
    data = open(sys.argv[1], "rb").read()
    found = 0
    s = secrets()
    if not s:
        sys.exit("no local secrets found to compare against")
    for name, value in s.items():
        b = value.encode("utf-8")
        if len(b) < 8:
            # Short values (user names) also occur inside unrelated strings
            # ("admin1"): only a whole C string counts.
            hit = b"\0" + b + b"\0" in data
        else:
            hit = b in data or value.encode("utf-16-le") in data
        print(f"{'FOUND ' if hit else 'absent'}  {name} ({len(value)} chars)")
        found += hit
    print(f"{len(data)} bytes, {found} secret value(s) found")
    sys.exit(1 if found else 0)


if __name__ == "__main__":
    main()
