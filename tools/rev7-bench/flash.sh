#!/bin/bash
# flash the rev7 board: if its firmware is running, ask it into BOOTSEL with the
# 1200 baud touch (firmware with the trigger only); then wait for the BOOTSEL
# drive, copy the UF2, and report what comes back.
UF2=${UF2:-/tmp/ahp-ci-rev7-bt/src/ninepin-ux.uf2}
drive() { lsblk -rno NAME,LABEL | awk '$2=="RP2350"{print "/dev/"$1; exit}'; }
dev=$(drive)
if [ -z "$dev" ]; then
  tty=$(ls /dev/serial/by-id/*Ninepin* /dev/serial/by-id/*amigahid* 2>/dev/null | head -1)
  if [ -n "$tty" ]; then
    echo "board running ($tty): sending 1200 baud touch"
    stty -F "$tty" 1200 2>/dev/null
  fi
fi
for i in $(seq 1 ${WAIT:-3600}); do
  dev=$(drive); [ -n "$dev" ] && break
  sleep 0.5
done
[ -z "$dev" ] && { echo "no BOOTSEL drive (waited ${WAIT:-3600} half-seconds)"; exit 1; }
echo "found $dev at $(date +%T)"
sleep 1
mnt=$(udisksctl mount -b "$dev" 2>&1 | sed -n 's/.* at \(.*\)$/\1/p')
[ -z "$mnt" ] && { echo "mount failed"; udisksctl mount -b "$dev"; exit 1; }
echo "mounted at $mnt"
cp "$UF2" "$mnt/" && sync && echo "copied $(stat -c '%s bytes, built %y' $UF2) at $(date +%T)"
for i in $(seq 1 30); do
  sleep 1
  if lsusb -d cafe:4a50 >/dev/null; then
    echo "firmware enumerated as cafe:4a50 after ${i}s"; sleep 2
    tty=$(ls /dev/serial/by-id/*Ninepin* /dev/serial/by-id/*amigahid* 2>/dev/null | head -1); echo "console: $tty"
    [ -n "$tty" ] && [ "${CAPTURE:-8}" -gt 0 ] && { stty -F "$tty" raw -echo 115200; timeout ${CAPTURE:-8} cat "$tty" | tr -d '\033' | sed 's/\[[0-9;]*[A-HJKf]//g'; }
    exit 0
  fi
done
echo "no cafe:4a50 within 30 s"; lsusb -d 2e8a:
