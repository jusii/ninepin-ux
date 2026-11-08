# rev7 firmware guide

What the rev7 firmware does and how to drive it: modes, the menu, game controllers, Bluetooth,
the Wi-Fi settings page and firmware updates. The rev7 board needs an RP2350 with 16 MB flash and
an RM2 radio module for Bluetooth and Wi-Fi. A build without the radio (`-DENABLE_BLUETOOTH_HID=OFF`)
has everything except Bluetooth, Wi-Fi and updates over Wi-Fi.

> **Status (November 2026):** new firmware, not yet tested on real hardware end to end. These parts
> are proven on a board: the three modes, the menu, the settings store, the A4000 keyboard to
> USB-C, USB keyboards and mice, Bluetooth start-up, the watchdog, firmware updates through the
> BOOTSEL drive with rollback, and the Wi-Fi access point.
> These parts are still waiting for their test session: game controllers, the CD32 pad, C64
> joysticks and the 1351 mouse, page uploads over Wi-Fi, and joining a home network.

## first install

The flash is split into two firmware slots, A and B, so that an update can never leave the board
without a working firmware. The layout is a partition table, installed once:

1. Hold BOOT while plugging in USB-C (or while pressing reset). An `RP2350` drive appears.
2. Copy `partition-table.uf2` onto it. The board restarts into BOOTSEL by itself.
3. Copy `amigahid-pico.uf2` onto the drive.

Both files are in the build directory under `src/`. After this first time, a firmware `.uf2`
always goes into the slot that is not running.

Once the firmware runs, it can be put back into BOOTSEL without the button: open the USB-C serial
port at 1200 baud (`stty -F /dev/ttyACM0 1200` on Linux), and the drive appears.

## modes

The board has three modes. A 1 s hold of BOOT steps to the next mode (amiga → c64 → usb). The
OLED shows the mode, and the board remembers it across power cycles.

| mode | keyboards | mice | game controllers | real A4000 keyboard |
|---|---|---|---|---|
| **amiga** | Amiga keyboard port | DE9 port set to mouse | DE9 ports (joystick / CD32) | Amiga |
| **c64** | USB-C (e.g. a C64 Ultimate) | USB-C, or a DE9 port set to 1351 | DE9 ports (C64 joystick) | USB-C |
| **usb** | USB-C | USB-C | USB-C, as two gamepads | USB-C |

USB-C is a composite device: keyboard, mouse, two gamepads ("amigahid gamepad 1/2") and a serial
console.

## the menu

Every setting is in the menu on the OLED. These controls open and drive it:

| control | open | move | change | leave |
|---|---|---|---|---|
| keyboard | hold Help 1 s (Insert on a PC keyboard) | up / down | left / right, Enter on actions | Esc |
| BOOT button | double-press | press | release after a 1 s hold | hold 3 s |
| PlayStation pad | hold PS 1 s | d-pad up / down | d-pad left / right, ✕ on actions | ○ |
| encoder (if fitted) | push | turn | push, turn, push | hold push |

Changes are made on a copy. **Save & exit** stores and applies them, and **Cancel** throws them
away. If you leave with unsaved changes, the menu first moves to Save & exit; leaving again
discards them. While the menu is open it owns the display and every keyboard, mouse and pad, so
nothing reaches the Amiga or the host.

### settings

| setting | values | default |
|---|---|---|
| Mode | amiga, c64, usb | amiga |
| Port 1 / Port 2 (amiga) | mouse, joystick, cd32 pad, off | mouse / joystick |
| Port 1 / Port 2 (c64) | joystick, 1351 mouse, off | joystick / joystick |
| Swap players | no, yes | no |
| Merge all pads | no, yes: every pad drives player 1 | no |
| Pad directions | d-pad + stick, d-pad, stick | d-pad + stick |
| Stick deadzone | 25%, 50%, 75% | 50% |
| Triangle button | up (jump), autofire, off | up |
| Touchpad mouse | off, on: the DualSense touchpad moves the pointer | on |
| Mouse wheel / Wheel direction | off, on / normal, reversed | on / normal |
| Keyboard preset | original, Y<->Z, A<->Q W<->Z | original |
| Help key | insert, home, page up, F11, none | insert |
| Del key | delete, backspace, none | delete |
| ISO key | backslash, amiga iso key | backslash |
| PC remap 1–8 | your own key swaps, PC keyboard → Amiga | empty |
| A4000 numpad ( ) | keypad keys, US/UK, Nordic/DE, French | keypad keys |
| Ctrl-Amiga-Amiga | nothing, ctrl-alt-del (sent once to USB-C) | nothing |
| A4000 remap 1–8 | your own key swaps, A4000 → USB-C | empty |
| Mouse speed | 300, 200, 150, 100 µs per step | 300 µs |
| Display | status, hid diag, mouse diag, off | status |
| Watchdog | off, 2 s, 5 s | 5 s |
| Bluetooth / BT pairing | off, on / automatic, paired only | on / automatic |
| Wi-Fi | off, access point, join network | off |
| Updates need BOOT | no, yes | yes |

The menu also has these actions: BT pair (2 min), Forget BT devices, Show page password, New page
password, Previous firmware and Factory defaults.

**Remaps.** Select a remap slot with Enter, press the key you want to change, then press what it
should become. The second key can come from either keyboard. Left or right on a slot clears it,
and Esc cancels while it waits for a key. "A4000 numpad ( )" decides how the A4000's numpad
brackets reach a computer. Keypad keys works on Linux with any layout. The other styles send the
shifted digits of that national layout.

## game controllers

Pads can be plugged into USB-A (directly or through a hub) or connected over Bluetooth:
- DualSense (PS5), USB and Bluetooth
- DualShock 3 (PS3), USB
- DualShock 4 (PS4), Bluetooth
- generic USB HID gamepads

The first pad becomes player 1 and the next one player 2. A pad that drops out keeps its player
for 30 s, so a replug comes back as the same player.

Where players go:
- **amiga:** player 1 drives port 2 and player 2 drives port 1, for each port set to joystick or
  cd32 pad. ✕ ○ □ are fire 1–3, and △ is up or autofire (the Triangle setting).
- **c64:** the same ports, as C64 joysticks. The 2nd and 3rd fire buttons appear on the POT
  lines for games that read them.
- **usb:** players 1 and 2 are the two USB-C gamepads, with analog sticks.

## Bluetooth

Bluetooth is on by default. With pairing set to automatic, the board pairs with any keyboard,
mouse or pad that is in pairing mode, at any time. With paired only, devices it already knows
still connect, but a new one pairs only during the 2-minute window that **BT pair (2 min)**
opens. **Forget BT devices** (select twice)
removes all bonds. A keyboard that needs a passkey shows it on the OLED.

## Wi-Fi and the settings page

Wi-Fi is off by default.
- **Access point.** The board opens an open network called `amigahid-xxxx` (xxxx comes from the
  board's ID). Join it and browse to `http://192.168.4.1/`. The access point switches itself off
  after 15 minutes without a client. Saving any setting or a restart brings it back.
- **Join network.** Enter your network's name and password on the page, then set Wi-Fi to join
  network. The board shows its address on the OLED and answers at `http://amigahid-xxxx.local/`.
  On your own network the page asks for the **page password**: any user name, and the password
  from the menu (Show page password). New page password makes a fresh one.

The page shows the board's state and every setting from the menu, except "Updates need BOOT",
which can only be changed on the board. It also takes firmware uploads.

## updating the firmware

Every rev7 firmware is installed **on trial**. After an update, the new firmware runs once and
keeps itself only after 10 s of clean running. If it hangs or crashes before then, the board
returns to the previous firmware. A firmware that is not this one never confirms itself, so it
is dropped the same way.

There are two ways to update:
- **BOOTSEL drive:** copy the new `amigahid-pico.uf2` onto it, as above.
- **Settings page:** choose the `.uf2` under firmware update and send it. The board checks the
  file, then shows `BOOT: install (30s)`. Press BOOT within 30 s to install, or nothing happens.
  With "Updates need BOOT" set to no, the page alone installs it, so keep that setting on unless
  only you can reach the page.

**Previous firmware** in the menu (select twice), or `ota previous` on the console, goes back to
the firmware that ran before the last update.

## the console

USB-C also carries a serial console (any terminal, any baud rate except 1200). Type `help` for
the list. The commands are:

| command | what it does |
|---|---|
| `info` | firmware, chip, boot slot |
| `settings` | every setting in effect |
| `mode amiga\|c64\|usb` | switches mode and remembers it |
| `menu open\|next\|prev\|inc\|dec\|enter\|leave` | drives the menu |
| `bt [pair\|forget]` | Bluetooth state, pairing window, forget bonds |
| `wifi [off\|ap\|join <ssid> [password]]` | Wi-Fi state and page password, set the mode |
| `pads` | pads and players |
| `ports` | what the DE9 ports are and what they send |
| `ota [previous]` | update state, previous firmware |
| `log` | the last 2K of console output, boot included |
| `defaults` | stores the factory defaults |
| `reboot` | restarts the board |
| `bootsel` | restarts into the USB bootloader |

## credits

The settings store, the menu input handling, the gamepad parser, PS3, DualShock 4, CD32 and the
Bluetooth host are ported from Stefan Reinauer's
[reinauer/amigahid-pico](https://github.com/reinauer/amigahid-pico) (features branch, EPL-2.0).
The DualSense support, the player manager, the C64 outputs, the USB-C gamepads, Wi-Fi and the
updates are this tree's own.
