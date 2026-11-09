# UX rev7 instructions

Firmware for the [UX rev7 board](./ux-rev7.md). Read the [errata](./errata.md) first.

> **Status:** tested on hardware: modes, menu, settings, USB and A4000 keyboards, mice, joystick
> and CD32 with a DualSense, USB-C gamepads, Wi-Fi (access point and join), updates with rollback.
> Not yet tested: Bluetooth pads and keyboards, PS3 pad, C64 joystick, 1351 mouse.

## first install

1. Hold BOOT, plug in USB-C. An `RP2350` drive appears.
2. Copy `partition-table.uf2`. The board restarts into BOOTSEL by itself.
3. Copy `amigahid-pico.uf2`.

The flash holds two firmware slots; every later update goes into the free one.

## modes

BOOT held 1 s steps the mode; the board remembers it.

| mode | keyboards | mice | pads | A4000 keyboard |
|---|---|---|---|---|
| `amiga` | Amiga keyboard port | DE9 port set to mouse | DE9 ports | Amiga |
| `c64` | USB-C | USB-C, or a DE9 port set to 1351 | DE9 ports | USB-C |
| `usb` | USB-C | USB-C | USB-C, two gamepads | USB-C |

`usb` turns the board into a USB adapter for a PC or Mac: the Amiga keyboard works as a USB
keyboard (see the A4000 settings below).

## the menu

| control | open | move | change / select | leave |
|---|---|---|---|---|
| keyboard | hold Help 1 s (Insert on a PC) | up / down | left / right; Enter on actions | Esc |
| BOOT | double-press | press | hold 1 s, release | hold 3 s |
| encoder | push | turn | push, turn, push; push on actions | hold push |
| PlayStation pad | hold PS 1 s | d-pad up / down | d-pad left / right; ✕ on actions | ○ |

Changes take effect on **Save & exit**. Leaving with changes jumps to Save & exit; leaving again
discards them. While the menu is open, input goes nowhere else.

## menu map

Defaults in **bold**.

**Mode**
* Mode: **amiga**, c64, usb

**Ports** (shown for the current mode)
* amiga, Port 1 / Port 2: mouse, joystick, cd32 pad, off (**port 1 mouse, port 2 joystick**)
* c64, Port 1 / Port 2: **joystick**, 1351 mouse, off

**Pads**
* Swap players: **no**, yes. Player 1 is on port 2, player 2 on port 1.
* Merge all pads: **no**, yes (every pad drives player 1)
* Pad directions: **d-pad + stick**, d-pad, stick
* Stick deadzone: 25%, **50%**, 75%
* Triangle button: **up (jump)**, autofire, off
* Touchpad mouse: off, **on** (DualSense touchpad moves the pointer, left / right half clicks)

**Mouse**
* Mouse wheel: off, **on** (TankMouse / Cocolino protocol)
* Wheel direction: **normal**, reversed

**PC keyboard to the Amiga**
* Keyboard preset: **original**, Y<->Z, A<->Q W<->Z
* Help key (the PC key that gives Amiga Help): **insert**, home, page up, F11, none
* Del key: **delete**, backspace, none
* ISO key (the key next to left shift): **backslash**, amiga iso key
* PC remap 1-8: Enter, press the key, press its new meaning. Left / right clears.

**A4000 keyboard to USB-C**
* A4000 numpad ( ): **keypad keys** (Linux, any layout), US/UK, Nordic/DE, French
* Ctrl-Amiga-Amiga: **nothing**, ctrl-alt-del
* A4000 remap 1-8: as PC remap

**Board**
* Mouse speed: **300 us**, 200 us, 150 us, 100 us
* Display: **status**, hid diag, mouse diag, off
* Watchdog: off, 2 s, **5 s**

**Bluetooth**
* Bluetooth: off, **on**
* BT pairing: **automatic** (any device in pairing mode), paired only
* BT pair (2 min): opens a pairing window
* Forget BT devices: select twice

**Wi-Fi**
* Wi-Fi: **off**, access point, join network
* Show page password, New page password
* Network control: **off**, on (see below)
* Net screen: the target's screen size for absolute moves on DE9: **640x256** ... 1920x1080
* Net mouse scale: mouse counts per screen pixel on DE9: 50%, 75%, **100%**, 150%, 200%

**Updates**
* Updates need BOOT: no, **yes**
* Previous firmware: select twice; restarts into the one before the last update

**End**
* Factory defaults, Save & exit, Cancel

## pads

PS5 DualSense (USB, Bluetooth), PS4 DualShock 4 (Bluetooth), PS3 DualShock 3 (USB), generic HID
pads.

* Joystick: d-pad / left stick; ✕ fire 1, ○ fire 2, □ fire 3, △ per Triangle button.
* CD32: ✕ red, ○ blue, □ green, △ yellow, L1 / R1 shoulders, Options play.
* C64: ✕ fire; ○ / □ 2nd / 3rd fire on the POT lines.
* The first pad is player 1, the next player 2. A pad that drops out keeps its player for 30 s.
* Hold PS 1 s: menu.

## Bluetooth

PS5 DualSense: hold Create + PS 3 s until the light bar double-blinks. PS4 DualShock 4: Share +
PS.
Keyboards and mice: their own pairing mode. A passkey, if asked, shows on the OLED. Paired devices
reconnect by themselves.

Searching for new devices pauses while the Wi-Fi access point is on and during an upload (shared
radio). Connected devices keep working; BT pair (2 min) searches anyway.

## Wi-Fi

* **Access point:** open network `amigahid-xxxx`, page at `http://192.168.4.1/`. Off after 15 min
  without a client; any save or a restart brings it back.
* **Join network:** set the network on the page, then Wi-Fi = join network. Page at
  `http://amigahid-xxxx.local/`; any user name, password from Show page password.

The page shows the status and every menu setting except Updates need BOOT (remaps read-only).

## network control

Keyboard and mouse over Wi-Fi, through a subset of the [PiKVM API](https://docs.pikvm.org/api/).
PiKVM and GL.iNet KVM clients and their MCP servers work with it. The screen has to come from a
capture device or an IP KVM; the board has no video.

1. Wi-Fi = join network, Network control = on. Not available in access point mode.
2. Base URL `http://amigahid-xxxx.local/api/`. Auth: any user name and the page password, as
   `X-KVMD-User` / `X-KVMD-Passwd` headers, HTTP Basic, or `POST /api/auth/login`.
3. Plain HTTP only.

```shell
curl -u admin:PASSWORD http://amigahid-xxxx.local/api/hid
curl -u admin:PASSWORD -X POST --data-binary 'Hello' 'http://amigahid-xxxx.local/api/hid/print?keymap=en-us'
curl -u admin:PASSWORD -X POST 'http://amigahid-xxxx.local/api/hid/events/send_key?key=Enter&finish=true'
curl -u admin:PASSWORD -X POST 'http://amigahid-xxxx.local/api/hid/events/send_mouse_relative?delta_x=20&delta_y=0'
```

* Calls: `info`, `hid`, `hid/keymaps`, `hid/print` (`keymap`, `slow`, `delay`, `limit`),
  `hid/events/send_key` (`state`, `finish`), `send_shortcut`, `send_mouse_button`,
  `send_mouse_move`, `send_mouse_relative`, `send_mouse_wheel`, `hid/reset`, `auth/*`.
* Keys are web `KeyboardEvent.code` names (`KeyA`, `ShiftLeft`, `MetaLeft` = Left Amiga).
* `keymap` is the target's layout: `en-us`, `en-gb`, `fi`, `sv`, `de`, `fr`.
* Input follows the mode, like any keyboard and mouse; the menu blocks it while open.
* Typing is paced so the target loses nothing: the Amiga gets at most about 30 characters a second.
* Mouse on DE9 is relative (`/api/hid` reports `absolute: false`). An absolute move pushes into
  the top-left corner first, then moves by Net screen and Net mouse scale; turn off mouse
  acceleration in Amiga Input prefs. USB-C has a true absolute pointer.
* Safety: 5 wrong passwords lock that address out for 30 s; held keys and buttons are released
  after 60 s without a request; "net ctl active" shows on the OLED.

## updates

Copy `amigahid-pico.uf2` to the BOOTSEL drive, or upload it on the page. With Updates need BOOT
= yes, press BOOT within 30 s when the OLED shows `BOOT: install`.

A new firmware runs on trial and keeps itself after 10 s; if it hangs, the previous one boots.
Previous firmware in the menu goes back one update. A running board enters BOOTSEL on a 1200 baud
touch of its serial port (`stty -F /dev/ttyACM0 1200`).

## console

USB-C serial, any baud rate except 1200. `help` lists the commands: `info`, `settings`,
`mode amiga|c64|usb`, `menu open|next|prev|inc|dec|enter|leave`, `bt [pair|forget]`,
`wifi [off|ap|join <ssid> [password]]`, `pads`, `ports`, `ota [previous]`, `log`, `defaults`,
`reboot`, `bootsel`.
