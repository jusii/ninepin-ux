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
 "C23": (70.0, 20.0, 0), "C21": (24.4, 20.5, 0), "C24": (73.05, 20.0, 0),   # plane caps: DVDD by the chip, IOVDD ones parked under the OLED
 # east comb: rows y = cy+1.2k, comb end x = cx+10.5; flash pads 1-3 on rows -7..-5
 "U8":  (CX + 14.0, CY - 6.5, 0),  "C32": (54.6, 24.2, 0),  "R52": (91.3, 14.3, 90),
 "C30": (CX + 12.0, CY - 0.6, 0),  "R57": (64.5, 40.15, 0), "R58": (64.5, 41.65, 0),
 "C29": (CX + 12.3, CY + 4.8, 0),  "L1":  (CX + 12.0, CY + 6.6, 180), "C38": (CX + 15.15, CY + 6.6, 0),
 "R59": (46.7, CY + 8.7, 180), "C37": (43.0, 46.6, 180),
 # west comb: rows y = cy+k, comb end x = cx-9.0
 "C25": (67.0, 23.0, 0),  "R54": (CX - 8.1, CY - 1.0, 180), "C22": (CX + 15.6, CY + 3.4, 0),
 "R53": (23.7, CY + 3.0, 180), "C26": (70.1, 23.0, 0),
 "Y1":  (CX - 12.4, CY - 1.5, 180), "C33": (19.6, 32.65, 180), "C34": (CX - 14.1, CY + 1.8, 270),
 # south comb: columns x = cx+k, comb bottom y = cy+9.0
 "C27": (67.0, 20.0, 0), "C20": (24.3, 22.5, 0),
 "R50": (37.3, 47.5, 90),  "C31": (39.7, 47.8, 0), "C28": (40.0, 44.6, 270),

 # --- RM2 ---
 "FB1": (18.8, 30.35, 90), "C35": (20.6, 30.1, 90), "C36": (19.3, 22.5, 90),
 "R55": (20.9, 43.0, 0),  "R56": (20.9, 41.4, 0),
 # --- parked decoupling (plane caps) north of the lanes ---
 # --- north-centre power row (y 20.8): sense divider, F1, D1, LDO, caps ---
 "R46": (41.5, 20.8, 270), "R47": (43.1, 20.8, 90), "C11": (44.7, 20.8, 90), "R60": (46.3, 20.8, 90),
 "F1":  (38.3, 20.5, 90), "D1":  (51.1, 21.1, 0),  "C8":  (58.1, 20.8, 90),  "U3":  (60.3, 20.8, 180),
 "C9":  (62.7, 20.8, 90), "C10": (64.5, 20.8, 90), "C2":  (56.3, 20.8, 90), "C3":  (47.5, 25.2, 0),
 "C1":  (63.5, 24.5, 0),
 # --- KBIN fuse and caps east of J12, AUX diode and CC resistors by the USB-C ---
 "F3":  (85.3, 7.5, 90),  "C6":  (80.4, 8.5, 90),  "C7":  (82.4, 8.5, 90),
 "D2":  (82.2, 17.4, 0),  "R43": (91.0, 33.0, 0),  "R44": (90.9, 29.5, 180),
 # --- bus switches: U6 MCU row south (lanes), U7 MCU row north (vias); arrays east ---
 "U6":  (13.0, 22.0, 90), "U7":  (80.65, 25.0, 90),
 "C13": (7.1, 22.0, 270),  "C14": (75.4, 18.8, 90),
 "RN1": (12.85, 26.85, 270), "RN2": (15.4, 26.85, 270),                            # port 1: 0402 arrays under U6's A pads
 "RN3": (31.9, 12.1, 270), "RN4": (42.4, 17.5, 270),                             # keyboard out under J1, keyboard in under J8
 "R61": (70.06, 48.5, 180), "R62": (71.94, 46.8, 180), "R63": (74.33, 46.8, 0), "R64": (76.1, 48.5, 180), "R65": (78.37, 46.8, 180), "R66": (80.25, 48.5, 180), "R67": (82.64, 46.8, 0),                                # port 2, east of the clamp rows, fed by B.Cu stubs from the P2 pins
 "R41": (77.3, 16.5, 90), "R42": (72.7, 16.5, 90),   # i2c pull-ups by the OLED socket
 # --- usb ---
 "U5":  (68.0, 43.4, 270), "C15": (67.5, 47.4, 180),   "R48": (69.0, 38.7, 90),
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
 "cont1_b1": (19.49, 46.9, 180), "cont1_b2": (28.51, 46.9, 0),
 # port 2 clamps sit inline on the lines in three staggered rows so the lines run straight into P2:
 # row 1 (y 47.9) v/vq/b3, row 2 (45.6) h/hq, row 3 (43.3) b1/b2; pad 1 (the line) is 1.05 west of the centre
 "cont2_v": (72.11, 45.0, 0), "cont2_vq": (77.65, 45.0, 0), "cont2_b3": (83.19, 45.0, 0),
 "cont2_h": (74.88, 42.7, 0), "cont2_hq": (80.42, 42.7, 0),
 "cont2_b1": (73.95, 40.4, 0), "cont2_b2": (81.8, 40.4, 0),
 "kbclock": (38.4, 8.5, 90), "kbdata": (43.2, 8.5, 90), "kbreset": (45.6, 8.5, 90),
 "kbin_clk": (66.9, 12.2, 180), "kbin_dat": (70.2, 16.6, 90),
 "kb_pwrled": (55.6, 8.5, 90), "kb_drvled": (58.1, 8.5, 90),
}
