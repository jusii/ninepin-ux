# rev7 bench helpers

Scripts used to flash and drive a rev7 board over USB-C without touching it,
and host tests of the Wi-Fi page and firmware updates. Not part of the build.

## On the board (USB-C to this machine)

/nas is mounted noexec: run the scripts through the interpreter from the repo,
`bash tools/rev7-bench/con.sh ...`, `bash tools/rev7-bench/flash.sh`,
`python3 -I tools/rev7-bench/keytest.py ...`.

- `flash.sh` - flash a UF2 without the BOOT button: a running firmware is sent
  into BOOTSEL with the 1200 baud touch, then the UF2 is copied onto the drive
  and the console is captured for a few seconds.
  `UF2=path WAIT=40 CAPTURE=14 ./flash.sh` (default UF2:
  `/tmp/ahp-ci-rev7-bt/src/amigahid-pico.uf2`).
- `con.sh "cmd" ...` - send console commands and print the replies. Quote each
  command; `LISTEN=2` (seconds) or more. Commands: `help`, `info`, `settings`,
  `mode`, `menu open|next|prev|inc|dec|enter|leave`, `bt`, `wifi`, `pads`,
  `ports`, `ota [previous]`, `log`, `defaults`, `reboot`, `bootsel`, `hang`.
- `smoke.sh` - partition table + two flashes + settings kept across a reboot.
- `keytest.py` - print key events from the board's USB keyboard
  (`sudo python3 -I keytest.py SECONDS [--nograb]`).

Builds go out of source under /tmp (the /nas share is mounted noexec).

## Host tests (gcc, ASan/UBSan)

`hosttest/` runs `http_server.c`, `web_page.c` and `ota.c` on the host against
a fake lwIP (with a TCP window), a fake 16 MB flash and a fake bootrom
(`inc/`, `inc2/`).

    R=/nas/Retro/Projects/amigahid-pico; H=$R/tools/rev7-bench/hosttest
    FLAGS="-std=gnu11 -g -O1 -Wall -Wextra -DBOARD_HIDPICO_REV7 -DENABLE_BLUETOOTH_HID -DPICO_RP2040=0 -fsanitize=address,undefined -I $H/inc2 -I $H/inc -I $R/src"
    gcc $FLAGS -o /tmp/ahp_ota_t $H/ota_test.c $R/src/ota.c $R/src/http_server.c $R/src/web_page.c $R/src/settings_names.c
    B=/tmp/ahp-ci-rev7-bt/src
    ASAN_OPTIONS=detect_leaks=0 /tmp/ahp_ota_t $B/amigahid-pico.uf2 $B/amigahid-pico.bin $B/partition-table.uf2
    ASAN_OPTIONS=detect_leaks=0 /tmp/ahp_ota_t $B/amigahid-pico.uf2 $B/amigahid-pico.bin $B/partition-table.uf2 trial
    gcc $FLAGS -o /tmp/ahp_web_t $H/test.c $R/src/http_server.c $R/src/web_page.c $R/src/settings_names.c
    ASAN_OPTIONS=detect_leaks=0 /tmp/ahp_web_t

`ota_test.c` uploads the real UF2 through the HTTP server into ota.c and checks
the slot byte for byte, the refusals, a dropped upload, the BOOT timeout, the
self-install, and (with `trial`) the accept / retire / boot-previous path. It
also counts flash operations made while the upload's window was open (must be 0).
