#!/usr/bin/env python
"""Finds the two panels' Wi-Fi updaters on the home network (Mike, 2026-11-07: one tap updates both).

Pings every address of the laptop's own /24 subnet so each panel that answers lands in the ARP
table, reads the table, and matches the entries by MAC to TCOMM_MAC_UPSTAIRS and TCOMM_MAC_DOWNSTAIRS
in tcomm/src/config/secrets.h, which this script reads itself. It prints only

    UPSTAIRS <ip>        or    UPSTAIRS not found
    DOWNSTAIRS <ip>      or    DOWNSTAIRS not found

and never a MAC or anything else from secrets.h. Run it as: python -I tools/find_panels.py
The match is exact: the panels use those MACs as their own (board_radio.cpp), and a looser one
could pick another device.
"""
import os
import re
import socket
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
# The project folder locally, or firmware/ on GitHub: secrets.h is in one place or the other.
SECRETS = next((p for p in (os.path.join(HERE, "..", "firmware", "tcomm", "src", "config", "secrets.h"),
                            os.path.join(HERE, "..", "tcomm", "src", "config", "secrets.h")) if os.path.exists(p)),
               os.path.join(HERE, "..", "firmware", "tcomm", "src", "config", "secrets.h"))


def read_macs():
    """{'UPSTAIRS': int or None, 'DOWNSTAIRS': ...}; an all-zero or missing entry is None."""
    out = {"UPSTAIRS": None, "DOWNSTAIRS": None}
    try:
        text = open(SECRETS, encoding="utf-8", errors="replace").read()
    except OSError:
        return out
    for name in out:
        m = re.search(r"#define\s+TCOMM_MAC_" + name + r"\s*\{([^}]*)\}", text)
        if not m:
            continue
        try:
            octets = [int(x.strip(), 0) for x in m.group(1).split(",") if x.strip()]
        except ValueError:
            continue
        if len(octets) == 6 and all(0 <= o <= 255 for o in octets):
            v = int.from_bytes(bytes(octets), "big")
            if v:
                out[name] = v
    return out


def own_ipv4():
    """The laptop's address on its default route. A UDP connect sends nothing."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.0.2.1", 9))
        return s.getsockname()[0]
    except OSError:
        return None
    finally:
        s.close()


def ping(ip):
    flags = ["-n", "1", "-w", "400"] if os.name == "nt" else ["-c", "1", "-W", "1"]
    try:
        subprocess.run(["ping"] + flags + [ip], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
    except (OSError, subprocess.SubprocessError):
        pass


def arp_table():
    """[(ip, mac as int)] from arp -a, dashes or colons."""
    try:
        txt = subprocess.run(["arp", "-a"], capture_output=True, text=True, timeout=15).stdout
    except (OSError, subprocess.SubprocessError):
        return []
    rows = []
    for m in re.finditer(r"(\d+\.\d+\.\d+\.\d+)\D+?((?:[0-9a-fA-F]{1,2}[-:]){5}[0-9a-fA-F]{1,2})", txt):
        mac = m.group(2).lower().replace(":", "-")
        try:
            rows.append((m.group(1), int(mac.replace("-", ""), 16)))
        except ValueError:
            pass
    return rows


def main():
    macs = read_macs()
    me = own_ipv4()
    found = {"UPSTAIRS": None, "DOWNSTAIRS": None}
    if me:
        base = ".".join(me.split(".")[:3])
        hosts = [f"{base}.{i}" for i in range(1, 255)]
        with ThreadPoolExecutor(max_workers=64) as ex:
            list(ex.map(ping, hosts))
        for ip, mac in arp_table():
            if not ip.startswith(base + ".") or ip == me:
                continue
            for name, want in macs.items():
                if want is not None and found[name] is None and mac == want:
                    found[name] = ip
    for name in ("UPSTAIRS", "DOWNSTAIRS"):
        print(f"{name} {found[name] or 'not found'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
