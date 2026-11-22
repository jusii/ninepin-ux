#!/bin/bash
# a new board in one go: flash it if it sits in BOOTSEL (a blank board does),
# run its self-test over the console, and keep the report.
#
#   newboard.sh            the latest release from github (downloaded once a day)
#   newboard.sh --local    the local build in $BUILD (default /tmp/ahp-ci-rev7-bt/src)
#
# fit the test cables first if the ports are to be checked (de9 port 1 to port
# 2 straight, KBD OUT to KBD IN), plug in usb-c, run this, press BOOT when the
# led blinks blue for the checks by hand. reports: _local/selftest/.
here=$(cd "$(dirname "$0")" && pwd); repo=$(cd "$here/../.." && pwd)
BUILD=${BUILD:-/tmp/ahp-ci-rev7-bt/src}
out="$repo/_local/selftest"; mkdir -p "$out"

if [ "$1" = "--local" ]; then
  files="$BUILD"
else
  files="$repo/_local/release/$(date +%F)"
  if [ ! -s "$files/ninepin-ux.uf2" ] || [ ! -s "$files/partition-table.uf2" ]; then
    mkdir -p "$files"
    for f in ninepin-ux.uf2 partition-table.uf2; do
      curl -fsSL -o "$files/$f" "https://github.com/jusii/ninepin-ux/releases/latest/download/$f" \
        || { echo "could not download $f"; exit 1; }
    done
  fi
fi
[ -s "$files/ninepin-ux.uf2" ] && [ -s "$files/partition-table.uf2" ] || { echo "no firmware in $files"; exit 1; }

drive() { lsblk -rno NAME,LABEL | awk '$2=="RP2350"{print "/dev/"$1; exit}'; }
console() { ls /dev/serial/by-id/*Ninepin* 2>/dev/null | head -1; }

echo "waiting for a board on usb-c ..."
for i in $(seq 1 600); do
  dev=$(drive); tty=$(console)
  [ -n "$dev" ] || [ -n "$tty" ] && break
  sleep 0.5
done
[ -z "$dev" ] && [ -z "$tty" ] && { echo "no board appeared"; exit 1; }

if [ -n "$dev" ]; then
  echo "in BOOTSEL: first install from $files"
  sleep 1
  mnt=$(udisksctl mount -b "$dev" 2>&1 | sed -n 's/.* at \(.*\)$/\1/p')
  [ -z "$mnt" ] && { echo "mount failed"; exit 1; }
  cp "$files/partition-table.uf2" "$mnt/" && sync
  # the board takes the table and comes back as the drive
  for i in $(seq 1 40); do [ -e "$mnt/INFO_UF2.TXT" ] || break; sleep 0.5; done
  sleep 2
  UF2="$files/ninepin-ux.uf2" WAIT=60 CAPTURE=0 bash "$here/flash.sh" | grep -E "copied|enumerated|no cafe|failed" || exit 1
  sleep 3
fi

for i in $(seq 1 40); do tty=$(console); [ -n "$tty" ] && break; sleep 0.5; done
[ -z "$tty" ] && { echo "the firmware's console did not appear"; exit 1; }
serial=$(basename "$tty" | sed 's/.*_\([0-9A-F]*\)-if.*/\1/')
log="$out/$serial-$(date +%F-%H%M%S).txt"
stty -F "$tty" raw -echo 115200

# the run: up to 10 s of listening, 10 s for BOOT, then three steps of 10 s
( timeout 75 cat "$tty" > "$log.raw" ) &
sleep 0.4
printf 'selftest go\r' > "$tty"
shown=0
for i in $(seq 1 148); do
  sleep 0.5
  now=$(tr -d '\033\r' < "$log.raw" | grep -c '^\[test\]')
  if [ "$now" -gt "$shown" ]; then
    tr -d '\033\r' < "$log.raw" | grep '^\[test\]' | sed -n "$((shown + 1)),${now}p"
    shown=$now
  fi
  grep -q 'not covered:' "$log.raw" && break
  grep -q 'unknown command' "$log.raw" && break
done
pkill -P $$ -x timeout 2>/dev/null; wait 2>/dev/null

if grep -q 'unknown command' "$log.raw"; then
  rm -f "$log.raw"
  echo "this board's firmware has no self-test: hold BOOT, plug it in again and run this once more"
  exit 1
fi
tr -d '\033\r' < "$log.raw" | grep '^\[test\]' | sed 's/^\[test\] //' > "$log"; rm -f "$log.raw"
echo
if grep -q 'RESULT: all passed' "$log"; then echo "PASS      $serial   ($log)"
elif grep -q 'RESULT: passed' "$log"; then echo "INCOMPLETE $serial: $(grep -c '^--' "$log") not tested   ($log)"
elif grep -q 'RESULT: FAILED' "$log"; then echo "FAILED    $serial   ($log)"; grep '^FAIL' "$log"; exit 2
else echo "no result from $serial   ($log)"; exit 1
fi
