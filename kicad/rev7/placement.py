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
 "C23": (CX + 7, CY - 10.9, 90),   "C21": (CX + 2, CY - 10.9, 90),   "C24": (CX - 3, CY - 10.9, 90),   # IOVDD 1, DVDD 6, IOVDD 11
 # east comb: rows y = cy+1.2k, comb end x = cx+10.5; flash pads 1-3 on rows -7..-5
 "U8":  (CX + 14.0, CY - 6.5, 0),  "C32": (53.6, 23.5, 0),  "R52": (89.4, 13.0, 90),
 "C30": (CX + 12.0, CY - 0.6, 0),  "R57": (CX + 12.3, CY + 1.2, 0),  "R58": (CX + 12.3, CY + 2.8, 0),
 "C29": (CX + 12.3, CY + 4.8, 0),  "L1":  (CX + 12.3, CY + 6.6, 180), "C38": (CX + 15.3, CY + 6.6, 0),
 "R59": (CX + 12.3, CY + 8.7, 180), "C37": (CX + 12.3, CY + 10.6, 0),
 # west comb: rows y = cy+k, comb end x = cx-9.0
 "C25": (10.0, 21.5, 90),  "R54": (CX - 8.1, CY - 1.0, 180), "C22": (CX + 15.6, CY + 3.4, 0),
 "R53": (CX - 8.75, CY + 3.0, 180), "C26": (11.6, 21.5, 90),
 "Y1":  (CX - 12.4, CY - 1.5, 180), "C33": (CX - 12.7, CY - 4.3, 180), "C34": (CX - 14.1, CY + 1.8, 270),
 # south comb: columns x = cx+k, comb bottom y = cy+9.0
 "C27": (13.2, 21.5, 90), "C20": (CX + 1, CY + 9.0, 270),
 "R50": (CX + 4.4, CY + 11.4, 0),  "C31": (CX + 0.6, CY + 11.4, 180), "C28": (CX + 8.6, CY + 9.8, 270),

 # --- RM2 ---
 "FB1": (13.5, 26.3, 0),  "C35": (10.0, 26.3, 0),  "C36": (17.0, 26.3, 0),
 "R55": (21.0, 43.0, 0),  "R56": (21.0, 41.0, 0),
 # --- parked decoupling (plane caps) north of the lanes ---
 "C24": (23.5, 22.5, 0),  "C23": (27.0, 22.5, 0),  "C21": (30.8, 22.5, 0),
 # --- north-centre power row (y 20.8): sense divider, F1, D1, LDO, caps ---
 "R46": (37.5, 20.8, 90), "R47": (39.1, 20.8, 90), "C11": (40.7, 20.8, 90), "R60": (42.3, 20.8, 90),
 "F1":  (34.7, 20.5, 90), "D1":  (51.1, 20.8, 0),  "C8":  (57.5, 20.8, 90),  "U3":  (59.7, 20.8, 0),
 "C9":  (61.9, 20.8, 90), "C10": (63.7, 20.8, 90), "C2":  (55.6, 20.8, 90), "C3":  (48.5, 23.6, 0),
 "C1":  (55.0, 36.4, 0),
 # --- KBIN fuse and caps east of J12, AUX diode and CC resistors by the USB-C ---
 "F3":  (85.3, 7.5, 90),  "C6":  (80.4, 8.5, 90),  "C7":  (82.4, 8.5, 90),
 "D2":  (82.2, 17.4, 0),  "R43": (90.6, 21.5, 90), "R44": (90.6, 18.0, 90),
 # --- bus switches: U6 MCU row south (lanes), U7 MCU row north (vias); arrays east ---
 "U6":  (70.5, 21.0, 270), "U7":  (81.0, 25.0, 90),
 "C13": (75.0, 30.0, 270), "C14": (76.5, 18.8, 90),
 "RN1": (68.0, 29.5, 90), "RN4": (87.65, 19.5, 90), "RN5": (87.65, 23.5, 90), "RN3": (87.65, 27.5, 90), "RN2": (87.65, 31.5, 90),
 "R41": (19.0, 22.5, 90), "R42": (20.6, 22.5, 90),
 # --- usb ---
 "U5":  (70.1, 40.6, 0),  "C15": (74.2, 40.6, 90),  "R48": (65.9, 40.8, 90),
 "F2":  (66.3, 44.6, 0),  "C4":  (58.5, 43.6, 0),  "C5":  (52.6, 44.6, 90), "U2":  (50.0, 46.6, 90),
 "U4":  (90.7, 35.2, 90),
 "Q1":  (92.0, 41.0, 0),  "R49": (87.5, 41.3, 0),  "D3":  (86.8, 44.3, 0),  "C12": (96.5, 41.0, 90),
 # --- keyboard LED pass-through: jumper + resistor west of J1 ---
 "JP1": (6.6, 10.6, 0),   "R45": (9.9, 10.6, 0),
}
P = {k: v for k, v in P.items() if v is not None and not k.endswith("x")}
# USB-A ESD clamp is U2 in design.py? no: U2 = RM2 radio? design.py: U9 = RM2, U2 = USBLC6 on USB-A
P["U9"] = (F.RM2_CENTRE[0], F.RM2_CENTRE[1], F.RM2_ROT)

# port TVS rows: assigned by net so the diode sits in front of its own connector pin
TVS_ROWS = {
 # port rows (y 47.9, 3.8 mm pitch) in front of the DE9s; keyboard-out clamps hang below the J1
 # pins (rot 90, pad 1 up); keyboard-in clamps sit on the J12 lines as pass-throughs
 "cont1_v": (7.4, 47.9, 0), "cont1_h": (11.2, 47.9, 0), "cont1_vq": (15.0, 47.9, 0), "cont1_hq": (18.8, 47.9, 0),
 "cont1_b3": (22.6, 47.9, 0), "cont1_b1": (26.4, 47.9, 0), "cont1_b2": (30.2, 47.9, 0),
 "cont2_v": (66.0, 47.9, 0), "cont2_h": (69.8, 47.9, 0), "cont2_vq": (73.6, 47.9, 0), "cont2_hq": (77.4, 47.9, 0),
 "cont2_b3": (81.2, 47.9, 0), "cont2_b1": (85.0, 47.9, 0), "cont2_b2": (88.8, 47.9, 0),
 "kbclock": (20.0, 18.55, 270), "kbdata": (23.0, 17.55, 270), "kbreset": (34.4, 14.45, 90),
 "kbin_clk": (66.9, 12.2, 180), "kbin_dat": (76.9, 15.9, 0),
 "kb_pwrled": (53.24, 10.5, 90), "kb_drvled": (55.78, 10.5, 90),
}
