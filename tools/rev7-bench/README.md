# rev7 bench helpers

Flash and drive a rev7 board over USB-C, and host tests of the Wi-Fi page and firmware
updates. Not part of the firmware build. Run from the repo root.

## board (USB-C)

* `bash tools/rev7-bench/flash.sh`: flash a UF2 without the BOOT button. A running board is
  sent into BOOTSEL with the 1200 baud touch, the UF2 is copied, the console captured.
  `UF2=path WAIT=40 CAPTURE=14` (default UF2: `/tmp/ahp-ci-rev7-bt/src/ninepin-ux.uf2`).
* `bash tools/rev7-bench/con.sh "cmd" ...`: console commands, one reply each (`LISTEN=2`
  seconds). `help` lists them.

## host tests (gcc, ASan/UBSan)

`hosttest/` runs `http_server.c`, `web_page.c`, `web_auth.c` and `ota.c` against a fake lwIP
(with a TCP window), a fake 16 MB flash and a fake bootrom, and the pad combos in `pads.c`.

```shell
R=$PWD; H=$R/tools/rev7-bench/hosttest; B=/tmp/ahp-ci-rev7-bt/src
F="-std=gnu11 -g -O1 -Wall -Wextra -DBOARD_HIDPICO_REV7 -DENABLE_BLUETOOTH_HID -DPICO_RP2040=0 \
   -fsanitize=address,undefined -I $H/inc2 -I $H/inc -I $R/firmware/src"
gcc $F -o /tmp/ahp_ota_t $H/ota_test.c $R/firmware/src/{ota,http_server,web_page,web_auth,profiles,settings_names}.c
ASAN_OPTIONS=detect_leaks=0 /tmp/ahp_ota_t $B/ninepin-ux.uf2 $B/ninepin-ux.bin $B/partition-table.uf2
ASAN_OPTIONS=detect_leaks=0 /tmp/ahp_ota_t $B/ninepin-ux.uf2 $B/ninepin-ux.bin $B/partition-table.uf2 trial
gcc $F -o /tmp/ahp_web_t $H/test.c $R/firmware/src/{http_server,web_page,web_auth,profiles,settings_names}.c
ASAN_OPTIONS=detect_leaks=0 /tmp/ahp_web_t
gcc $F -DCFG_TUSB_MCU=0 -I $H/inc3 -include $H/inc2/fake_sdk.h -o /tmp/ahp_pads_t $H/pads_test.c $R/firmware/src/pads.c
/tmp/ahp_pads_t
```

`test.c` covers the page, the log-in form, sessions, the page password form, the pad combo and
device name fields, pad profiles (with `profiles.c` on the fake flash) and uploads; `pads_test.c` what a combo button does to the outputs.

`ota_test.c` uploads the real UF2 through the server into the update code and checks the slot
byte for byte, refusals, a dropped upload, the BOOT timeout, self-install, and (`trial`) accept,
retire and boot-previous. Flash operations with a flowing upload's TCP window open must be 0.
