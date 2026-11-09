#!/usr/bin/env python3
"""Print key events from the amigahid-pico USB keyboard for a while.

Usage: sudo python3 -I keytest.py SECONDS [--nograb]

By default the device is grabbed (EVIOCGRAB), so keystrokes reach only this
script and not the desktop. --nograb leaves them flowing to the desktop, which
is needed to watch the host's caps lock state change.
"""
import fcntl
import glob
import os
import re
import select
import struct
import sys
import time

EVIOCGRAB = 0x40044590
EV_KEY, EV_LED = 0x01, 0x11
LED_CAPSL = 0x01
EVENT = struct.Struct("llHHi")


def key_names():
    names = {}
    try:
        with open("/usr/include/linux/input-event-codes.h") as f:
            for line in f:
                m = re.match(r"#define\s+(KEY_\w+)\s+(0x[0-9a-fA-F]+|\d+)\b", line)
                if m:
                    names.setdefault(int(m.group(2), 0), m.group(1))
    except OSError:
        pass
    return names


def caps_led(path):
    # EVIOCGLED(len): _IOC(_IOC_READ, 'E', 0x19, len)
    buf = bytearray(8)
    fd = os.open(path, os.O_RDONLY)
    try:
        fcntl.ioctl(fd, 0x80084519, buf)
    finally:
        os.close(fd)
    return bool(buf[0] & (1 << LED_CAPSL))


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 30
    grab = "--nograb" not in sys.argv
    devs = sorted(p for p in glob.glob("/dev/input/by-id/*amigahid*event-kbd*"))
    if not devs:
        print("no amigahid keyboard event device found")
        return 1
    path = os.path.realpath(devs[0])
    names = key_names()
    fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
    if grab:
        fcntl.ioctl(fd, EVIOCGRAB, 1)
    print(f"listening on {devs[0]} ({path}) for {seconds:.0f}s, grab={grab}, host caps led={caps_led(path)}", flush=True)
    end = time.time() + seconds
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.2)
        if not r:
            continue
        data = os.read(fd, EVENT.size * 64)
        for i in range(0, len(data) - EVENT.size + 1, EVENT.size):
            _, _, etype, code, value = EVENT.unpack_from(data, i)
            if etype == EV_KEY and value in (0, 1):
                name = names.get(code, f"code{code}")
                extra = f"  (host caps led={caps_led(path)})" if name == "KEY_CAPSLOCK" and value == 0 else ""
                print(f"{time.strftime('%H:%M:%S')} {name:<22} {'down' if value else 'up'}{extra}", flush=True)
    if grab:
        fcntl.ioctl(fd, EVIOCGRAB, 0)
    os.close(fd)
    return 0


if __name__ == "__main__":
    sys.exit(main())
