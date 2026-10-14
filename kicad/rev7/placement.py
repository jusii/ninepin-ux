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
 "U8":  (CX + 14.0, CY - 6.5, 0),  "C32": (CX + 18.0, CY - 11.0, 90),  "R52": (CX + 12.5, CY - 10.4, 0),
 "C30": (CX + 12.0, CY - 0.6, 0),  "R57": (CX + 12.3, CY + 1.2, 0),  "R58": (CX + 12.3, CY + 2.8, 0),
 "C29": (CX + 12.3, CY + 4.8, 0),  "L1":  (CX + 12.3, CY + 6.6, 180), "C38": (CX + 15.3, CY + 6.6, 0),
 "R59": (CX + 12.3, CY + 8.7, 180), "C37": (CX + 12.3, CY + 10.6, 0),
 # west comb: rows y = cy+k, comb end x = cx-9.0
 "C25": (CX - 9.0, CY - 9.8, 90),  "R54": (CX - 8.1, CY - 1.0, 180), "C22": (CX + 15.6, CY + 3.4, 0),
 "R53": (CX - 8.75, CY + 3.0, 180), "C26": (CX - 12.2, CY - 9.8, 90),
 "Y1":  (CX - 12.4, CY - 1.5, 180), "C33": (CX - 12.7, CY - 4.3, 180), "C34": (CX - 14.1, CY + 1.8, 270),
 # south comb: columns x = cx+k, comb bottom y = cy+9.0
 "C27": (CX - 10.6, CY - 9.8, 90), "C20": (CX + 1, CY + 9.0, 270),
 "R50": (CX + 4.4, CY + 11.4, 0),  "C31": (CX + 0.6, CY + 11.4, 180), "C28": (CX + 8.6, CY + 9.8, 270),
 # --- RM2 ---
 "FB1": (13.5, 26.3, 0),  "C35": (10.0, 26.3, 0),  "C36": (17.0, 26.3, 0),
 "R55": (21.0, 43.0, 0),  "R56": (21.0, 41.0, 0),
 # --- power tree ---
 "F1":  (45.0, 20.5, 0),  "D1":  (52.0, 20.5, 0),  "D2":  (88.0, 18.0, 0),
 "C1":  (56.5, 36.4, 0),  "C2":  (55.5, 24.3, 90), "C3":  (53.3, 24.3, 90),
 "U3":  (44.5, 24.0, 0),  "C8":  (42.0, 24.0, 90), "C9":  (46.8, 24.0, 90), "C10": (48.8, 24.3, 90),
 "R46": (44.0, 17.0, 0),  "R47": (47.4, 17.0, 0),  "C11": (50.8, 17.0, 0),  "R60": (54.2, 17.0, 0),
 "F3":  (59.5, 19.5, 90), "C6":  (58.0, 24.5, 90), "C7":  (59.8, 24.5, 90),
 "F2":  (68.5, 44.0, 0),  "C4":  (59.5, 43.6, 0),  "C5":  (53.0, 46.8, 0),
 "D3":  (86.8, 44.3, 0),  "C12": (91.2, 37.5, 90),
 # --- keyboard / headers / jumper ---
 "JP1": (14.0, 10.5, 0),  "R45": (17.5, 10.5, 0),
 # --- bus switches, arrays (under the OLED) ---
 "U6":  (72.5, 22.5, 90), "U7":  (72.5, 31.0, 90),
 "C13": (79.0, 22.5, 90), "C14": (79.0, 31.0, 90),
 "RN1": (63.5, 19.5, 90), "RN2": (63.5, 24.5, 90), "RN3": (63.5, 29.5, 90),
 "RN4": (82.8, 22.5, 90), "RN5": (82.8, 31.0, 90),
 "R41": (66.5, 16.5, 0),  "R42": (70.0, 16.5, 0),
 # --- usb ---
 "U5":  (80.0, 42.0, 0),  "C15": (84.4, 40.5, 90),  "R48": (75.5, 42.0, 90),
 "U2":  (49.5, 46.6, 90),
 "U4":  (90.8, 31.5, 90), "R43": (91.0, 22.0, 90), "R44": (91.0, 26.0, 90),
 "Q1":  (92.0, 41.0, 0),  "R49": (87.5, 41.3, 0),
}
P = {k: v for k, v in P.items() if v is not None and not k.endswith("x")}
# USB-A ESD clamp is U2 in design.py? no: U2 = RM2 radio? design.py: U9 = RM2, U2 = USBLC6 on USB-A
P["U9"] = (F.RM2_CENTRE[0], F.RM2_CENTRE[1], F.RM2_ROT)

# port TVS rows: assigned by net so the diode sits in front of its own connector pin
TVS_ROWS = {
 # net: (x, y, rot). SOD-323 courtyard is ~3.6 x 1.8, so 3.8 mm pitch.
 "cont1_v": (7.4, 47.9, 0), "cont1_h": (11.2, 47.9, 0), "cont1_vq": (15.0, 47.9, 0), "cont1_hq": (18.8, 47.9, 0),
 "cont1_b3": (22.6, 47.9, 0), "cont1_b1": (26.4, 47.9, 0), "cont1_b2": (30.2, 47.9, 0),
 "cont2_v": (66.0, 47.2, 0), "cont2_h": (69.8, 47.2, 0), "cont2_vq": (73.6, 47.2, 0), "cont2_hq": (77.4, 47.2, 0),
 "cont2_b3": (81.2, 47.2, 0), "cont2_b1": (85.0, 47.2, 0), "cont2_b2": (88.8, 47.2, 0),
 "kbreset": (14.0, 18.0, 0), "kbdata": (17.8, 18.0, 0), "kbclock": (21.6, 18.0, 0),
 "kb_pwrled": (25.4, 18.0, 0), "kb_drvled": (29.2, 18.0, 0),
 "kbin_clk": (36.0, 18.0, 0), "kbin_dat": (39.8, 18.0, 0),
}
