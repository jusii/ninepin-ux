#!/bin/bash
# rev7 smoke test after steps 1-2: partition table, firmware into a slot, settings
# kept across a reboot, and a second flash landing in the other slot.
B=/tmp/ahp-rev7-nort/src
S=$(dirname "$0")
drive() { lsblk -rno NAME,LABEL | awk '$2=="RP2350"{print "/dev/"$1; exit}'; }
tty() { ls /dev/serial/by-id/*amigahid* 2>/dev/null | head -1; }

wait_drive() {   # $1 = seconds
    for i in $(seq 1 $(( $1 * 2 ))); do d=$(drive); [ -n "$d" ] && { echo "$d"; return 0; }; sleep 0.5; done
    return 1
}
wait_gone() {
    for i in $(seq 1 40); do [ -z "$(drive)" ] && return 0; sleep 0.5; done
    return 1
}
copy_to_drive() {  # $1 = file
    local d m
    d=$(wait_drive 30) || { echo "FAIL: no BOOTSEL drive"; exit 1; }
    sleep 1
    m=$(udisksctl mount -b "$d" 2>&1 | sed -n 's/.* at \(.*\)$/\1/p')
    [ -z "$m" ] && { echo "FAIL: mount $d"; exit 1; }
    cp "$1" "$m/" && sync
    echo "copied $(basename $1) at $(date +%T)"
}
console() {   # $1 = command, $2 = seconds to listen
    local t; t=$(tty); [ -z "$t" ] && { echo "FAIL: no console"; return 1; }
    stty -F "$t" raw -echo 115200
    ( timeout "$2" cat "$t" > "$S/smoke.out" ) &
    sleep 0.5
    printf '%s\r' "$1" > "$t"
    wait
    tr -d '\033' < "$S/smoke.out" | sed 's/\[[0-9;]*[A-HJKf]//g' | grep -vE '^\s*$' | grep -A30 -- "> $1" | head -30
}
wait_firmware() {
    for i in $(seq 1 60); do lsusb -d cafe:4a50 >/dev/null && [ -n "$(tty)" ] && { sleep 2; return 0; }; sleep 0.5; done
    echo "FAIL: firmware did not come back"; exit 1
}

echo "== 1. into BOOTSEL by the 1200 baud touch"
[ -z "$(drive)" ] && stty -F "$(tty)" 1200 2>/dev/null
echo "== 2. partition table"
copy_to_drive "$B/partition-table.uf2"
wait_gone && echo "drive went away (bootrom rebooting)"
d=$(wait_drive 30) && echo "back in BOOTSEL as $d: no bootable image yet, as expected"
echo "== 3. firmware"
copy_to_drive "$B/amigahid-pico.uf2"
wait_firmware && echo "firmware up at $(date +%T)"
console info 3
console settings 3
echo "== 4. switch mode, reboot, check it was kept"
console "mode usb" 3
console reboot 2
wait_firmware && echo "firmware back at $(date +%T)"
console settings 3
echo "== 5. second flash: should land in the other slot"
console bootsel 2
copy_to_drive "$B/amigahid-pico.uf2"
wait_firmware && echo "firmware up at $(date +%T)"
console info 3
console settings 3
echo "== 6. back to amiga mode"
console "mode amiga" 3
