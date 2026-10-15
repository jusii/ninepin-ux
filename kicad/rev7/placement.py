"""rev7 part placement (kicad mm, rot degrees CCW, all top side). Connectors, holes and the
OLED come from floorplan.py; everything else is here. Edit and re-run make_board.py."""
import floorplan as F

MCU = (33.0, 36.8, 270)     # RP2350A: GPIO0-15 north, QSPI+USB+VREG east, SWD+crystal west, RM2 SPI south

CX, CY = MCU[0], MCU[1]
P = {
 # --- RP2350A core, rot 270 (GPIO N, QSPI/USB/VREG E, SWD/crystal W, RM2+port2 S) ---
 # Every side fans to a comb of straight tracks: N/W/S at 1.0 mm pitch (column k = pin offset
 # from the side centre), E at 1.2 mm. A 0603 fits inside a comb column (pads clear the
 # neighbouring tracks by 0.45) but never in adjacent columns at the same height.
 "U1": MCU,
 # north comb: columns x = cx+k, comb top y = cy-9.2; caps at the top of their column
 "C23": (30.0, 27.1, 90), "C21": (35.5, 27.45, 0), "C24": (40.5, 27.45, 0),   # 0402 caps at the N-side column vias: IOVDD 11, DVDD 6, IOVDD 1
 # east comb: rows y = cy+1.2k, comb end x = cx+10.5; flash pads 1-3 on rows -7..-5
 "U8":  (CX + 14.0, CY - 6.5, 0),  "C32": (52.9, 26.9, 0),  "R52": (44.9, 26.3, 0),      # BOOT resistor at the flash, so the chip-select line has no stub
 "C30": (41.6, 36.0, 0),  "R57": (64.5, 40.15, 0), "R58": (64.5, 41.65, 0),   # USB_OTP/QSPI_IOVDD cap and the USB series Rs right off the E pins
 "C29": (41.4, 42.2, 270),  "L1":  (43.6, 42.2, 180), "C38": (45.8, 42.2, 270),   # CIN | L | COUT across the VIN..PGND rows (RP2350 DS fig. 26)
 "R59": (43.6, 47.0, 180), "C37": (43.6, 45.5, 0),                             # AVDD filter below the group
 # west comb: rows y = cy+k, comb end x = cx-9.0
 "R54": (CX - 8.1, CY - 1.0, 180), "C22": (47.6, 41.2, 0),  # DVDD 39: 0402 at the S39 via
 "R53": (23.7, CY + 3.0, 180),
 "Y1":  (CX - 12.4, CY - 1.5, 180), "C33": (19.6, 32.65, 180), "C34": (CX - 14.1, CY + 1.8, 270),
 # south comb: columns x = cx+k, comb bottom y = cy+9.0
 "C27": (67.0, 20.0, 0), "C20": (25.3, 37.1, 180),   # C20: DVDD 23 on its own column; C27 stays as plane bulk under the OLED
 "R50": (37.3, 47.5, 90),  "C31": (39.7, 47.8, 0), "C28": (39.1, 45.0, 270),
 "C25": (24.4, 34.0, 180), "C26": (25.2, 44.3, 0),                             # IOVDD 20 / 30 at the W-side vias

 # --- RM2 ---
 "FB1": (18.8, 30.35, 90), "C35": (20.6, 30.1, 90), "C36": (19.3, 22.5, 90),
 "R55": (20.9, 43.0, 0),  "R56": (20.9, 41.4, 0),
 # --- parked decoupling (plane caps) north of the lanes ---
 # --- north-centre power row (y 20.8): sense divider, F1, D1, LDO, caps ---
 "R46": (41.5, 20.8, 270), "R47": (43.1, 20.8, 90), "C11": (44.7, 20.8, 90), "R60": (46.3, 20.8, 90),
 "F1":  (38.3, 20.5, 270), "D1":  (51.1, 21.1, 0),  "C8":  (58.1, 20.8, 90),  "U3":  (60.6, 20.8, 180),
 "C9":  (62.7, 20.8, 90), "C10": (64.5, 20.8, 90), "C2":  (56.3, 20.8, 90), "C3":  (48.0, 25.4, 0),
 "C1":  (63.5, 24.5, 0),
 # --- KBIN fuse and caps east of J12, AUX diode and CC resistors by the USB-C ---
 "F3":  (81.4, 7.5, 90),  "C6":  (81.8, 11.6, 0),  "C7":  (81.8, 13.5, 0),   # F3 against the mini-DIN, its caps below it, the header and expander east
 "D2":  (82.2, 17.4, 0),  "R43": (91.0, 33.0, 0),  "R44": (90.9, 29.5, 180),
 # --- bus switches: U6 MCU row south (lanes), U7 MCU row north (vias); arrays east ---
 "U6":  (13.0, 22.0, 90), "U7":  (80.65, 25.0, 90),
 "C13": (7.1, 22.0, 270),  "C14": (75.4, 18.8, 90),
 "RN1": (12.7975, 27.5, 270), "RN2": (15.4725, 27.5, 270),   # pad 4 / pad 3 exactly under U6 pins 5 / 10                            # port 1: 0402 arrays under U6's A pads
 "RN3": (31.9, 12.1, 270), "RN4": (42.4, 17.5, 270),                             # keyboard out under J1, keyboard in under J8
 "R61": (70.55, 48.5, 180), "R62": (71.93, 46.8, 180), "R63": (74.34, 46.8, 0), "R64": (76.09, 48.5, 180), "R65": (78.86, 46.8, 180), "R66": (80.24, 48.5, 180), "R67": (82.65, 46.8, 0),                                # port 2, east of the clamp rows, fed by B.Cu stubs from the P2 pins
 "R41": (77.3, 16.5, 90), "R42": (72.7, 16.5, 90),   # i2c pull-ups by the OLED socket
 # --- usb ---
 "R70": (53.3, 47.85, 180), "R71": (48.1, 43.8, 0),     # PIO-USB 27R at U2 (the mux is gone)
 "U10": (92.5, 10.6, 180), "C39": (90.0, 14.4, 0), "R68": (94.3, 24.0, 0), "R69": (97.4, 24.0, 0),   # I/O expander east of the OLED post, VBUS sense divider under the AUX lane
 "F2":  (62.4, 46.0, 90), "C4":  (55.5, 43.4, 180),  "C5":  (65.35, 44.0, 270), "U2":  (50.0, 46.6, 90),
 "U4":  (90.25, 37.0, 270),
 "Q1":  (94.2, 41.2, 0),  "R49": (97.3, 38.0, 270), "D3":  (90.3, 43.5, 270), "C12": (97.3, 41.1, 90),
 # --- keyboard LED pass-through: jumper + resistor west of J1 ---
 "JP1": (61.0, 6.5, 0),   "R45": (61.0, 9.0, 0),
}
P = {k: v for k, v in P.items() if v is not None and not k.endswith("x")}
# USB-A ESD clamp is U2 in design.py? no: U2 = RM2 radio? design.py: U9 = RM2, U2 = USBLC6 on USB-A
P["U9"] = (F.RM2_CENTRE[0], F.RM2_CENTRE[1], F.RM2_ROT)

# port TVS rows: assigned by net so the diode sits in front of its own connector pin
TVS_ROWS = {
 # port rows (y 47.9, 3.8 mm pitch) in front of the DE9s; keyboard-out clamps hang below the J1
 # pins (rot 90, pad 1 up); keyboard-in clamps sit on the J12 lines as pass-throughs
 # port 1 clamps (SOD-882) north of the P1 pins in two rows, pad 1 on the pin x, reached by F.Cu stubs up from the pins
 "cont1_v": (18.11, 48.3, 180), "cont1_h": (21.58, 48.3, 0), "cont1_vq": (23.65, 48.3, 180), "cont1_hq": (26.42, 48.3, 180), "cont1_b3": (29.89, 48.3, 0),
 "cont1_b1": (19.49, 46.7, 180), "cont1_b2": (28.51, 46.7, 0),
 # port 2 clamps sit inline on the lines in three staggered rows so the lines run straight into P2:
 # row 1 (y 47.9) v/vq/b3, row 2 (45.6) h/hq, row 3 (43.3) b1/b2; pad 1 (the line) is 1.05 west of the centre
 "cont2_v": (72.11, 45.0, 0), "cont2_vq": (77.65, 45.0, 0), "cont2_b3": (83.19, 45.0, 0),
 "cont2_h": (74.88, 42.7, 0), "cont2_hq": (80.42, 42.7, 0),
 "cont2_b1": (73.49, 40.4, 0), "cont2_b2": (81.8, 40.4, 0),
 "kbclock": (38.4, 8.5, 90), "kbdata": (43.2, 8.5, 90), "kbreset": (45.6, 8.5, 90),
 "kbin_clk": (66.9, 12.2, 180), "kbin_dat": (70.2, 16.6, 90),
 "kb_pwrled": (55.6, 8.5, 90), "kb_drvled": (58.1, 8.5, 90),
}
