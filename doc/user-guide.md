# Ninepin UX user guide

For the rev7 and rev7.1 boards. Connectors: [board](./board.md). Read the [errata](./errata.md) first.

> **Tested on hardware:** modes, menu, settings, USB and A4000 keyboards, mice, joystick and CD32
> with a DualSense, two DualSenses over Bluetooth, pad combos, USB-C gamepads, C64 joystick and
> 1351 mouse (C64 Ultimate, GEOS), Wi-Fi (access point and join), updates with rollback.
> **Not yet tested:** Bluetooth keyboards and mice, DualShock 4, PS3 pad, a real C64.

## connecting

| machine | keyboard out | real keyboard in |
|---|---|---|
| A4000D | J10 KBD OUT, mini-DIN6 male-male cable | J12 KBD IN, the A4000 keyboard |
| A2000, A3000, A4000T | J10 through a DIN5-to-mini-DIN6 cable | J12 through a DIN5 (female) to mini-DIN6 adapter |
| A500 | J1 header, 8-way cable to the A500 keyboard header | J8 header, the A500 keyboard's own cable |
| A600, A1200 | no connector (keyboard MCU) | — |

* Controller ports P1 / P2: straight-through DE9 female-female cables, not null-modem. They work
  with only the DE9 cables connected (A600 / A1200, C64).
* Power: the Amiga keyboard connector or USB-C. DE9 5 V never powers the board.
* USB-A: keyboards, mice, pads, hubs. USB-C: power, and the device port in `c64` and `usb` modes.

## first install

1. Download `ninepin-ux.uf2` and `partition-table.uf2` from
   [Releases](https://github.com/jusii/ninepin-ux/releases), or [build them](./build.md).
2. Hold BOOT, plug in USB-C. An `RP2350` drive appears.
3. Copy `partition-table.uf2`. The board restarts into BOOTSEL by itself.
4. Copy `ninepin-ux.uf2`.

The flash holds two firmware slots; every later update goes into the free one.

## modes

BOOT held 1 s steps the mode; the board remembers it.

| mode | keyboards | mice | pads | A4000 keyboard |
|---|---|---|---|---|
| `amiga` | Amiga keyboard port | DE9 port set to mouse | DE9 ports | Amiga |
| `c64` | USB-C | USB-C, or a DE9 port set to 1351 or paddles | DE9 ports | USB-C |
| `usb` | USB-C | USB-C | USB-C, two gamepads | USB-C |

* **C64 1351 mouse:** set a C64 port to 1351 mouse. Left button on fire, right button on the up
  line; pointer speed by Mouse speed. The mouse then no longer goes to USB-C.
* **C64 paddles:** set a C64 port to paddles (a pair, on the POT lines; not yet tried on a C64
  itself). A mouse turns them: sideways paddle X, up and down paddle Y, left / right button their
  buttons, and they stay where they are left. A pad's left stick sideways is paddle X, its right
  stick paddle Y, fire 1 / fire 2 their buttons; the stick's place is the knob's. Whichever moved
  last has the knob. Mouse speed sets how far the mouse turns them.

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

Two levels. The top level holds what changes most; **›** opens a group. Leave (Esc, hold) goes up
a level, and from the top leaves the menu. Each group also ends with **Back**. The menu reopens at
the setting visited last. Defaults in **bold**.

**Top level**
* Mode: **amiga**, c64, usb
* Pad profile: picks a saved profile (see pads); `*` after the name: changed since it was loaded
* Port 1 / Port 2, for the current mode:
  * amiga: mouse, joystick, cd32 pad, off (**port 1 mouse, port 2 joystick**)
  * c64: **joystick**, 1351 mouse, off, paddles
* Pads ›, Mouse ›, Keyboard ›, Bluetooth ›, Wi-Fi ›, Board ›
* Save & exit, Cancel

**Pads ›**
* Swap players: **no**, yes. Player 1 is on port 2, player 2 on port 1.
* Merge all pads: **no**, yes (every pad drives player 1)
* Pad directions: **d-pad + stick**, d-pad, stick
* Stick deadzone: 25%, **50%**, 75%
* Triangle button: **up (jump)**, autofire, off
* Autofire rate: 5, 8, **10**, 15, 20 a second (Triangle = autofire, and combos with ↻ ticked)
* Touchpad mouse: off, **on** (DualSense touchpad moves the pointer, left / right half clicks)
* Pad names: **automatic**, PlayStation, Xbox, Switch, numbers (the button names in the pad
  button combos on the page)

**Mouse ›**
* Mouse wheel: off, **on** (TankMouse / Cocolino protocol)
* Wheel direction: **normal**, reversed
* Mouse speed: **300 us**, 200 us, 150 us, 100 us (Amiga: time per step; C64 1351: mouse counts
  per step 12, 8, 6, 4, so further right is faster on both)

**Keyboard ›**
* Keyboard preset (PC keyboard to the Amiga): **original**, Y<->Z, A<->Q W<->Z
* Help key (the PC key that gives Amiga Help): **insert**, home, page up, F11, none
* Del key: **delete**, backspace, none
* ISO key (the key next to left shift): **backslash**, amiga iso key
* A4000 numpad ( ) (A4000 keyboard to USB-C): **keypad keys** (Linux, any layout), US/UK, Nordic/DE, French
* Ctrl-Amiga-Amiga: **nothing**, ctrl-alt-del
* PC remap 1-8: Enter, press the key, press its new meaning. Left / right clears.
* A4000 remap 1-8: as PC remap

**Bluetooth ›**
* Bluetooth: off, **on**
* BT pairing: **automatic** (any device in pairing mode), paired only
* BT pair (2 min): opens a pairing window
* Forget BT devices: select twice

**Wi-Fi ›**
* Wi-Fi: **off**, access point, join network
* Page password: **asked for**, off (join mode; see Wi-Fi)
* Show page password, New page password (a random one; also switches it back on)
* Network control: **off**, on (see below)
* Net screen: the target's screen size for absolute moves on DE9: **640x256** ... 1920x1080
* Net mouse scale: mouse counts per screen pixel on DE9: 50%, 75%, **100%**, 150%, 200%

**Board ›**
* Display: **status**, hid diag, mouse diag (bottom line: movement, buttons `b`, last wheel step `w`, wheel reports counted `n`), off. Takes effect on save, no restart.
* Watchdog: off, 2 s, **5 s**
* Updates need BOOT: no, **yes**
* Check for update: looks for the latest release; select again (twice) to install it
* Previous firmware: select twice; restarts into the one before the last update
* Factory defaults

## pads

PS5 DualSense (USB, Bluetooth), PS4 DualShock 4 (Bluetooth), PS3 DualShock 3 (USB), generic HID
pads.

* Joystick: d-pad / left stick; ✕ fire 1, ○ fire 2, □ fire 3, △ per Triangle button.
* CD32: ✕ red, ○ blue, □ green, △ yellow, L1 / R1 shoulders, Options play.
* C64: ✕ fire; ○ / □ 2nd / 3rd fire on the POT lines.
* The first pad is player 1, the next player 2. A pad that drops out keeps its player for 30 s.
* Battery (DualSense, DualShock 4 over Bluetooth): on the page beside the player, and on the
  display's third line as `1:60 2:85` (percent, player 1 and 2). At 20% or below, not charging,
  the figure blinks. In Amiga mode with the Amiga off the line starts `amiga:off`.
* Hold PS 1 s: menu.
* Hold PS and press d-pad right / left: the next / previous saved pad profile, then none (no
  combos). The display shows its name. It is stored at once.

### combos

Six slots on the page (Pad button combos): one pad button stands for directions and fire buttons
held together, a keyboard key, or both. Example: R1 = down + fire 1 (the bomb in Blue Max),
L2 = space.

* A combo's direction wins over the stick; the pad button no longer does what it did.
* ↻ (auto) ticked: whilst the button is held its fire buttons and key are pressed again and again
  at the Autofire rate (Pads); its directions stay held. Example: R2 = fire 1 with ↻.
* Directions and fire go to the DE9 ports. The key goes wherever keyboards go, in every mode.
* A button goes by its number (a PlayStation pad's numbering). The names beside the numbers are
  the connected pad's, or as Pad names (Pads) says: PlayStation, Xbox, Switch, numbers. For a pad
  of another make, hold the button and reload the page: Status shows its number.
* Keys: a letter, a digit, `f1`-`f12`, or `space enter esc backspace tab up down left right del
  insert home end pgup pgdn lshift rshift lctrl lalt ralt lgui rgui capslock minus equals comma
  period slash`.

### profiles

A profile keeps the combos, Pad directions, Triangle button and Autofire rate under a name (up to 32 profiles,
names up to 12 characters), one per game.

* Page: type a name, Save as profile (stores the whole form too); Load and Delete for saved ones.
  Saving under an existing name replaces it.
* Menu: Pad profile picks a saved one; Save & exit stores it.
* A profile is a copy: loading one writes it into the settings. Factory defaults keep the profiles.
* A profile is for the Amiga, the C64 or any machine (chosen beside the name; a new one takes the
  current mode). It is offered in that mode only; usb mode offers them all.
* Each mode remembers the profile it had last and loads it when the mode is switched to. A mode
  that had none comes up without combos if the other mode had a profile. Combos edited but not
  saved as a profile are lost on a mode switch.

### profile files

Download (beside Load) gives a saved profile as a text file. Add from a file puts one in: choose
the file, Add profile. A profile of the same name is replaced only with "replace" ticked.
Ready-made ones: [profiles/](../profiles/).

```
ninepin-ux pad profile
# a comment
name=Blue Max
machine=c64
directions=both
triangle=off
autofire=10
combo=r1 down fire1
combo=l2 key=space
```

* `machine`: `any`, `amiga`, `c64`. `directions`: `both`, `dpad`, `stick`. `triangle`: `up`,
  `autofire`, `off`. `autofire`: `5`, `8`, `10`, `15`, `20`. Lines left out are as the factory has
  them.
* `combo=`: a button, then any of `up down left right fire1 fire2 fire3 auto` and `key=` with a
  key name as on the page. Up to six.
* Buttons by their place on the pad, whatever its make: `west south east north` (□ ✕ ○ △),
  `l1 r1 l2 r2`, `select start` (create, options), `l3 r3`, `home`, `touchpad`, `mute`, `b16`;
  or a number 1 to 16.

## type text

On the page, Type text: switch it on, choose the layout the machine is set to (one per mode: US,
UK, Finnish / Swedish, German, French), Save. Then paste or write up to 1000 characters and press
Type; the board types them as a keyboard would. "Press Return at the end" adds one.

* Works on an Amiga, and on a C64 Ultimate or a PC over USB-C. Not on an original C64: the board
  cannot press its keys.
* A character the layout does not have is left out; the page says how many.
* A long text takes a while (about 30 characters a second on an Amiga). The page shows how far it
  is when reloaded, and has a Stop button.
* With it on, whoever can open the page can type on the machine: keep the page password on.

## self-test

The board checks itself: `selftest go` on the console, or Self-test on the page (tick the box).
A first check of a new board, not a replacement for trying it on the machine.

* With nothing attached: chip, flash, I/O expander, display, radio, Bluetooth reception, and
  USB-A if a device is plugged in.
* With a straight DE9 cable from port 1 to port 2 and a 6-pin mini-DIN cable from KBD OUT to
  KBD IN: the port lines each way, and key codes out of one keyboard port into the other. The
  keyboard cable also gives the Amiga side its 5 V, which the DE9 check needs.
* Then the LED blinks blue for 10 s: press BOOT for the checks by hand (encoder, LED colours,
  display pattern; BOOT means yes, each waits 10 s). No press ends the test.
* Each line says `pass`, `FAIL` or `--` (not tested). The LED ends green (all passed), amber
  (passed, something not tested) or red.
* It pulls the port lines and sends key codes: run it with the test cables or nothing on those
  ports, not with a machine attached.
* Not covered: the A500 headers, 5 V in on DE9 pin 7.

## lag

`lag` on the console, or Lag on the page: how long pad reports take through the board, counted
since power on (`lag reset`, or Clear, starts again).

* Timed from the report reaching the board (Bluetooth or USB-A) to the joystick port lines
  changing, or to the USB host collecting the report. Only reports that change an output count.
* Not in it: the pad's own delay, the radio, the machine.
* Per path: changes, least, median, worst, and how many took under 0.25 .. 32 ms.
* The main loop's average and slowest pass, and the last eight reports over 4 ms with the part
  of the firmware that held each up.
* Showing the page is work for the board too and can add a slow one.


* PS5 DualSense: hold Create + PS 3 s until the light bar double-blinks. PS4 DualShock 4: Share + PS.
* Keyboards and mice: their own pairing mode. A passkey, if asked, shows on the OLED.
* Paired devices reconnect by themselves.
* A second pad: put it in pairing mode within 30 s of the first one connecting. Later, open
  BT pair (2 min) first (menu, or Pair on the page): the board does not otherwise search while a
  pad is in use.
* Searching for new devices pauses while the Wi-Fi access point is on and during an upload (shared
  radio). Connected devices keep working; BT pair (2 min) searches anyway.

## Wi-Fi

* **Access point:** open network `ninepin-xxxx`, page at `http://192.168.4.1/`. Off after 15 min
  without a client; any save or a restart brings it back.
* **Join network:** set the network on the page, then Wi-Fi = join network. Page at
  `http://ninepin-xxxx.local/`; log in with the page password (menu: Show page password).
* **Device name:** set on the page; it replaces `ninepin-xxxx` as the access point's name and
  the `.local` address. Letters, digits and `-`, up to 15; empty goes back to `ninepin-xxxx`.
* **Page password:** change it on the page (8 to 16 characters), or switch it off there or in
  the menu. Without it everyone on the network can change settings, use network control and,
  with Updates need BOOT = no, replace the firmware. New page password in the menu makes a
  random one and switches it back on. Scripts can still use HTTP Basic (any user name).

### the page

* Status: mode, firmware and slot, update state, players, Bluetooth, Wi-Fi, and **Running**: how
  long the board has been up and what started it (power on, reset button, brown-out, a restart
  asked for, the watchdog, a fault). After a fault, **Last fault** has the details for a bug
  report.
* Every menu setting (remaps read-only), plus the pad combos and profiles, the device name, the
  page password and Pair (2 min) for Bluetooth.
* The settings are in groups that fold. Each group has a Save button: it stores the whole form
  and comes back to that group. Wi-Fi changes answer with where to find the board again.
* No JavaScript.

## network control

Keyboard and mouse over Wi-Fi, through a subset of the [PiKVM API](https://docs.pikvm.org/api/).
PiKVM and GL.iNet KVM clients and their MCP servers work with it. The screen has to come from a
capture device or an IP KVM; the board has no video.

1. Wi-Fi = join network, Network control = on. Not available in access point mode.
2. Base URL `http://ninepin-xxxx.local/api/`. Auth: any user name and the page password, as
   `X-KVMD-User` / `X-KVMD-Passwd` headers, HTTP Basic, or `POST /api/auth/login`. None with
   the page password switched off.
3. Plain HTTP only.

```shell
curl -u admin:PASSWORD http://ninepin-xxxx.local/api/hid
curl -u admin:PASSWORD -X POST --data-binary 'Hello' 'http://ninepin-xxxx.local/api/hid/print?keymap=en-us'
curl -u admin:PASSWORD -X POST 'http://ninepin-xxxx.local/api/hid/events/send_key?key=Enter&finish=true'
curl -u admin:PASSWORD -X POST 'http://ninepin-xxxx.local/api/hid/events/send_mouse_relative?delta_x=20&delta_y=0'
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

Three ways:

* **From the internet** (join mode): Check for update on the page (Firmware update) or in the
  menu (Board). It asks GitHub for the latest release, and only then; Install downloads it,
  checks our signature on it and installs it. A release older than the firmware, or over a
  self-built ("dev") firmware, is offered too, labelled as such. Input may stutter for a few
  seconds while the board talks to GitHub.
* **Upload** `ninepin-ux.uf2` on the page.
* **Copy** it to the BOOTSEL drive.

With Updates need BOOT
= yes, press BOOT within 30 s when the OLED shows `BOOT: install`. The setting is in the menu and
on the page (Board); with it off, whoever can open the page can replace the firmware.

A new firmware runs on trial and keeps itself after 10 s; if it hangs, the previous one boots.
Previous firmware in the menu goes back one update. A running board enters BOOTSEL on a 1200 baud
touch of its serial port (`stty -F /dev/ttyACM0 1200`).

Boards running firmware from before the rename show up as `amigahid-xxxx` until updated.

## console

USB-C serial, any baud rate except 1200. `help` lists the commands: `info` (firmware, uptime,
what started the board, the last fault), `settings`,
`mode amiga|c64|usb`, `menu open|next|prev|inc|dec|enter|leave`, `bt [pair|forget]`,
`wifi [off|ap|join <ssid> [password]]`, `pads`, `ports`, `lag [reset]`, `selftest [go]`,
`ota [previous]`, `log`, `defaults`,
`reboot`, `bootsel`.
