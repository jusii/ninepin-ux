"""amigahid-pico-UX rev7 floorplan: single source of truth for the board outline, the
connector positions and the case. KiCad coordinates (mm, origin at the board's NW corner,
y down). Read by make_board.py (pcbnew) and case/enclosure.py (FreeCAD).

Case rule (Rev7 Plan): the case OUTER wall is the reference. Mini-DIN and USB-A mating faces
sit flush with the outer wall, i.e. overhang the board edge by GAP + WALL. The DE9 flange
sits behind the wall (D-cutout, wall thinned to DE9_WALL, 4-40 jackscrews through the wall).
The USB-C receptacle can only overhang ~1.1 mm (its rear pads are 1.07 mm from the edge
line), so it gets the same local wall thinning as the DE9 instead of a full overhang. The
mini-DIN shield pin (3.2 mm pad) limits its overhang to 2.5 mm, so the wall is thinned to
MDIN_WALL over each mini-DIN body and the face stays flush (decided 2026-10-14).
"""

# ---- board ----
W, H = 100.0, 62.0
BOARD_T = 1.6
CORNER_R = 2.0
HOLES = [(4.0, 4.0), (W - 4.0, 4.0), (4.0, H - 4.0), (W - 4.0, H - 4.0)]   # M3

# ---- case ----
GAP = 0.5            # board edge to inner wall
WALL = 2.5           # outer wall thickness
OVH = GAP + WALL     # connector face overhang past the board edge for wall-flush faces
DE9_WALL = 1.5       # wall thickness in front of a DE9 flange / USB-C face
MDIN_WALL = 2.0      # wall thinned locally at the mini-DIN bodies so the face is flush at a 2.5 mm overhang
MDIN_OVH = OVH - (WALL - MDIN_WALL)
STAND = 4.0          # board underside above the base plate
BASE_T = 2.5
CEIL = 15.5          # inner ceiling above board underside (OLED glass top 15.24, DE9 14.5)
TOP_T = 2.5

# ---- connectors: (ref, value, lib, footprint, x, y, rot, note) ----
# Footprint-local geometry used to derive the origins:
#   MD-60S   : face at local y=-0.25, body to y=12.65, pins y 4.70..11.00, 14.5 wide
#   USB-A    : face at local y=+13.49 (rear -2.77), pins y 0..2.71, body x -4.2..11.2
#   DE9      : near pin row local y=2.84, must be 7.70 mm behind the board edge (.318" family);
#              flange front is assumed DE9_FLANGE mm past the board edge (verify on datasheet)
#   USB4105  : local "PCB Edge" line at y=+3.1, face at y=4.18, pads y -3.68..1.07
DE9_FLANGE = 0.9
DE9_W = 31.86                       # flange + fab outline width (local x -10.39..21.47)
DE9_X0 = -10.39

MDIN_Y = -MDIN_OVH + 0.25
USBA_Y = H + OVH - 13.49
DE9_Y = H - 7.70 - 2.84
USBC_X = W + OVH - DE9_WALL - 4.18          # face sits DE9_WALL behind the outer surface
USBC_Y = 31.0
SW_X = W + OVH - 5.85                       # PTS645Vx58 actuator tip flush with the outer wall
SW_Y = (16.0, 46.0)                         # BOOT, RESET
LED = (94.5, 40.0)                          # between the USB-C and RESET, clear of the OLED window

CONNECTORS = [
    ("J10", "KBD OUT mDIN6", "rev7", "MiniDIN6_PS2_Socket_CUI_MD-60S", 30.0, MDIN_Y, 0, "to Amiga"),
    ("J12", "KBD IN mDIN6",  "rev7", "MiniDIN6_PS2_Socket_CUI_MD-60S", 72.0, MDIN_Y, 0, "from keyboard"),
    ("P1",  "DE9 PORT 1", "rev7", "DSUB-9_Pins_Horizontal_P2.77x2.84mm_EdgePinOffset7.70mm_Housed_MountingHolesOffset9.12mm", 18.46, DE9_Y, 0, ""),
    ("P2",  "DE9 PORT 2", "rev7", "DSUB-9_Pins_Horizontal_P2.77x2.84mm_EdgePinOffset7.70mm_Housed_MountingHolesOffset9.12mm", 71.06, DE9_Y, 0, ""),
    ("J4",  "USB-A HOST", "rev7", "USB_A_Molex_67643_Horizontal", 46.8, USBA_Y, 0, ""),
    ("J13", "USB-C", "Connector_USB", "USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal", USBC_X, USBC_Y, 90, "power + flashing"),
    ("J1",  "A500 KBD OUT", "Connector_PinHeader_2.54mm", "PinHeader_1x08_P2.54mm_Vertical", 13.0, 14.0, 90, ""),
    ("J8",  "A500 KBD IN",  "Connector_PinHeader_2.54mm", "PinHeader_1x08_P2.54mm_Vertical", 38.0, 14.0, 90, ""),
    ("J2",  "SWD", "Connector_PinHeader_2.54mm", "PinHeader_1x03_P2.54mm_Vertical", 40.0, 41.5, 90, "SWCLK GND SWDIO"),
    ("J7",  "SPARE GPIO", "Connector_PinHeader_2.54mm", "PinHeader_1x06_P2.54mm_Vertical", 40.0, 45.5, 90, ""),
    ("SW1", "BOOT",  "Button_Switch_THT", "SW_Tactile_SPST_Angled_PTS645Vx58-2LFS", SW_X, SW_Y[0], -90, "right-angle, actuator through east wall"),
    ("SW2", "RESET", "Button_Switch_THT", "SW_Tactile_SPST_Angled_PTS645Vx58-2LFS", SW_X, SW_Y[1], -90, "right-angle, actuator through east wall"),
    ("LED1", "WS2812B-2020", "LED_SMD", "LED_WS2812B-2020_PLCC4_2.0x2.0mm", LED[0], LED[1], 0, "lid peek hole"),
]

# ---- OLED: 0.96" SSD1306 module, 27.5 x 27.8 pcb, holes 23.5 x 23.8 on 2.0 insets, 1x4
# header on the top-hole line. Plugs into J3 (1x4 female, 8.4 mm) + four M2 posts (11 mm).
OLED_PCB = (27.5, 27.8)
OLED_X0, OLED_Y0 = 62.0, 11.0            # NE, pulled toward centre, clear of the RM2 antenna
OLED_HOLES = [(OLED_X0 + 2.0, OLED_Y0 + 2.0), (OLED_X0 + 25.5, OLED_Y0 + 2.0),
              (OLED_X0 + 2.0, OLED_Y0 + 25.8), (OLED_X0 + 25.5, OLED_Y0 + 25.8)]
OLED_GLASS = (26.7, 19.3)                 # window; glass sits in the lower part of the module
OLED_GLASS_Y0 = OLED_Y0 + 6.0
J3 = ("J3", "OLED", "Connector_PinSocket_2.54mm", "PinSocket_1x04_P2.54mm_Vertical",
      OLED_X0 + OLED_PCB[0] / 2 - 3.81, OLED_Y0 + 2.0, 90, "GND VCC SCL SDA")
OLED_Z_TOP = BOARD_T + 8.4 + 2.54 + 1.0 + 1.7   # glass top above board underside

# ---- RM2 radio module (datasheet RP-008943-DS-7, fig. 6): 14.5 x 16.5 mm, antenna on one short
# edge, 21 castellations on the other three. Metal keep-out 35.5 x 18.5 centred on the module:
# 13.5 mm beyond the antenna edge, 10.5 mm each side, 5.0 mm into the module. Antenna edge on
# the west board edge, so only the 35.5 x 5.0 band lands on the board (all layers).
RM2_SIZE = (16.5, 14.5)                   # along x (antenna axis), along y
RM2_X0, RM2_Y0 = 0.3, 37.0 - 14.5 / 2     # antenna edge 0.3 mm inside the west board edge (courtyard)
RM2_CENTRE = (RM2_X0 + RM2_SIZE[0] / 2, RM2_Y0 + RM2_SIZE[1] / 2)
RM2_KEEPOUT = (RM2_X0 - 13.5, RM2_CENTRE[1] - 17.75, RM2_X0 + 5.0, RM2_CENTRE[1] + 17.75)
RM2_ROT = 90                              # footprint has the antenna toward -y; rotate so it faces -x

# ---- silicon zones (guidance only, placed in step 5) ----
ZONES = {
    "RP2350A + flash + xtal": (19, 26, 38, 48),      # next to the RM2 for a short SPI run
    "2x CBTD3861 + 10k arrays (under OLED)": (64, 16, 88, 36),
    "USB mux + ESD": (76, 39, 92, 48),               # between USB-A, USB-C and the RP2350
    "power: diodes, fuse, 3V3": (42, 18, 62, 28),    # fed from J10 (north) and J13 (east)
}
