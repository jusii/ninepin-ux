#!/bin/bash
# send console commands to the board and print the replies: con.sh "cmd1" "cmd2" ...
# LISTEN = seconds to collect each reply (default 2)
out=$(mktemp)
trap 'rm -f "$out"' EXIT
t=$(ls /dev/serial/by-id/*amigahid* 2>/dev/null | head -1)
[ -z "$t" ] && { echo "no console"; exit 1; }
stty -F "$t" raw -echo 115200
for c in "$@"; do
  ( timeout ${LISTEN:-2} cat "$t" > "$out" ) &
  sleep 0.4
  printf '%s\r' "$c" > "$t"
  wait
  tr -d '\033' < "$out" | sed 's/\[[0-9;]*[A-HJKf]//g' | grep -vE '^\s*$' | grep -A25 -- "> $c" | head -25
done
