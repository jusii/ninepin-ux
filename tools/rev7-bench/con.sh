#!/bin/bash
# send console commands to the board and print the replies: con.sh "cmd1" "cmd2" ...
S=$(dirname "$0")
t=$(ls /dev/serial/by-id/*amigahid* 2>/dev/null | head -1)
[ -z "$t" ] && { echo "no console"; exit 1; }
stty -F "$t" raw -echo 115200
for c in "$@"; do
  ( timeout ${LISTEN:-2} cat "$t" > "$S/con.out" ) &
  sleep 0.4
  printf '%s\r' "$c" > "$t"
  wait
  tr -d '\033' < "$S/con.out" | sed 's/\[[0-9;]*[A-HJKf]//g' | grep -vE '^\s*$' | grep -A25 -- "> $c" | head -25
done
