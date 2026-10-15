"""amigahid-pico-UX rev7 schematic design data. Read by make_sch.py.
Part = (ref, lib_id, value, footprint, {pin: net}, extra). Nets: "GND" = ground symbol,
"NC" = no-connect, anything else a label. Net naming follows rev6 so the firmware pin map and
the checker carry over: Amiga-side 5 V nets are plain, the RP2350 side gets the _lo suffix."""

TITLE = "hid-pico Amiga UX"
DATE = "2026-10-14"
COMMENTS = ["rev7 - RP2350A + RM2 chip-down, CBTD3861 bus switches, USB-C + USB-A host, real keyboard passthrough",
            "based on borb's amigahid-pico (github.com/borb/amigahid-pico) and the UX rev 1 (rev6) board",
            "Amiga-side pull-ups and bus switches run from the Amiga's own 5 V (AMIGA_5V_FUSED), never from USB-C"]

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
CELEC = "Capacitor_SMD:CP_Elec_6.3x7.7"
SOD323 = "Diode_SMD:D_SOD-323"
PIN = "Connector_PinHeader_2.54mm:PinHeader_1x0%d_P2.54mm_Vertical"

def R(ref, val, a, b, **x): return (ref, "Device:R", val, R0603, {"1": a, "2": b}, x)
def C(ref, val, a, b, fp=C0603, **x): return (ref, "Device:C", val, fp, {"1": a, "2": b}, x)
def TVS(ref, net):
    if net.startswith("cont1_"):   # port 1 clamps sit in two tight rows north of P1: the SOD-882 variant of the same part
        return (ref, "Device:D_TVS", "PESD5V0S1UL", "Diode_SMD:D_SOD-882", {"1": net, "2": "GND"},
                {"LCSC": "C2060734", "Datasheet": "https://assets.nexperia.com/documents/data-sheet/PESD5V0S1UL.pdf", "Note": "LCSC code to verify before order"})
    return (ref, "Device:D_TVS", "PESD5V0S1BA", SOD323, {"1": net, "2": "GND"},
            {"LCSC": "C19224", "Datasheet": "https://assets.nexperia.com/documents/data-sheet/PESD5V0S1BA_SER.pdf"})
 
def RPACK(ref, nets, common, fp="Resistor_SMD:R_Array_Convex_4x0603", lcsc="C29718"):
    m = {}
    for i, n in enumerate(nets):   # pins 1-4 = R1.1..R4.1, pins 8..5 = R1.2..R4.2
        m[str(i + 1)] = n
        m[str(8 - i)] = common          # the common side is tied even for spare elements (keeps the 0402 pad gaps legal)
    return (ref, "Device:R_Pack04", "4x10k", fp, m, {"LCSC": lcsc})

# Amiga-facing 5 V nets, in bus-switch order
KBD_OUT = ["kbreset", "kbdata", "kbclock"]   # order = bus-switch pin order (A1..A3 at U6's west end)
KBD_IN = ["kbin_dat", "kbin_clk"]
# Bus-switch pin orders are chosen for planar fans on the board (see placement.py / route.py):
# U6 sits at the west edge north of the radio module; port 1 leaves its A row on the bottom layer, runs
# down the west edge and enters P1 from the north (hq..v) and from between its pin rows (b3, b2); the
# keyboard trio sits at the east end next to J1. U7 is under the OLED: kbin on A1/A2 from J12, port 2 on
# A3..A9 fanning straight south into P2 in DE9 pin order.
PORT1 = ["cont1_b3", "cont1_b2", "cont1_v", "cont1_b1", "cont1_h", "cont1_vq", "cont1_hq"]
PORT2 = ["cont2_v", "cont2_b1", "cont2_h", "cont2_vq", "cont2_hq", "cont2_b2", "cont2_b3"]
U6_A = KBD_OUT + PORT1
U7_A = ["kbin_clk", "kbin_dat"] + PORT2 + ["NC"]
AMIGA_NETS = U6_A + U7_A[:9]                           # 19, in switch order; TVS D10.. follow this order
# 10k pull-up arrays, placed where the lines pass: RN1-3 north of P1, RN4 by J1, RN5 by J12/J8, RN6/7 under P2
# RN1/RN2 are 0402 arrays on U6's A pads (0.5 mm pitch suits the 0.635 pin pitch); the rest are 0603 arrays
RPACKS = {"RN1": ["cont1_b1", "cont1_v", "cont1_b2", "cont1_b3"], "RN2": ["NC", "cont1_hq", "cont1_vq", "cont1_h"],
          "RN3": ["kbreset", "kbdata", "kbclock", "NC"], "RN4": ["NC", "NC", "kbin_dat", "kbin_clk"]}
# port 2 pull-ups are discrete 0402 10k on the lines north of P2 (no room for arrays between the clamp rows)
PULLUPS = {"cont2_v": "R61", "cont2_b1": "R62", "cont2_h": "R63", "cont2_vq": "R64", "cont2_hq": "R65", "cont2_b2": "R66", "cont2_b3": "R67"}
RPACK_FP = {"RN1": ("Resistor_SMD:R_Array_Convex_4x0402", "C25518"), "RN2": ("Resistor_SMD:R_Array_Convex_4x0402", "C25518")}

def CBTD(ref, nets):
    m = {"1": "NC", "12": "GND", "23": "GND", "24": "AMIGA_5V_FUSED"}
    for i, n in enumerate(nets):
        m[str(2 + i)] = n                                    # A1..A10 = pins 2..11
        m[str(22 - i)] = (n + "_lo") if n != "NC" else "NC"  # B1..B10 = pins 22..13
    return (ref, "74xx:74CBTD3861", "SN74CBTD3861DBQR", "Package_SO:SSOP-24_3.9x8.7mm_P0.635mm", m,
            {"LCSC": "C44508", "Note": "10-bit bus switch with diode-dropped VCC: 5 V side on A, clamps the B side to ~3.3 V"})

# RP2350A GPIO map (firmware pin map lives here; rev6 numbers kept where possible)
GPIO = {
    0: "i2c_sda", 1: "i2c_scl", 2: "uart_tx", 3: "uart_rx",   # i2c0 on 0/1; uart0 on its aux pins 2/3 (RP2350 funcsel 11)
    4: "kbreset_lo", 5: "kbdata_lo", 6: "kbclock_lo",
    7: "cont1_b3_lo", 8: "cont1_b2_lo", 9: "cont1_v_lo", 10: "cont1_b1_lo", 11: "cont1_h_lo", 12: "cont1_vq_lo", 13: "cont1_hq_lo",
    14: "kbin_clk_lo", 15: "kbin_dat_lo",
    16: "cont2_v_lo", 17: "cont2_b1_lo", 18: "cont2_h_lo", 19: "cont2_vq_lo", 20: "cont2_hq_lo", 21: "cont2_b2_lo", 22: "cont2_b3_lo",
    23: "wl_on", 24: "wl_d", 25: "wl_cs",
    26: "ws_drv", 27: "usb_sel", 28: "amiga_sense", 29: "wl_clk",   # 26/27 are not 5 V tolerant: keep them off the Amiga lines
}
RP_PINS = {"2": 0, "3": 1, "4": 2, "5": 3, "7": 4, "8": 5, "9": 6, "10": 7, "12": 8, "13": 9, "14": 10, "15": 11, "16": 12,
           "17": 13, "18": 14, "19": 15, "27": 16, "28": 17, "29": 18, "31": 19, "32": 20, "33": 21, "34": 22, "35": 23,
           "36": 24, "37": 25, "40": 26, "41": 27, "42": 28, "43": 29}
RP = {p: GPIO[g] for p, g in RP_PINS.items()}
RP.update({"1": "+3V3", "11": "+3V3", "20": "+3V3", "30": "+3V3", "38": "+3V3", "45": "+3V3",   # IOVDD
           "6": "dvdd", "23": "dvdd", "39": "dvdd",                                            # DVDD 1.1 V
           "44": "adc_avdd", "46": "vreg_avdd", "47": "GND", "48": "vreg_lx", "49": "+3V3", "50": "dvdd",
           "53": "+3V3", "54": "+3V3",
           "21": "xin", "22": "xout", "24": "swclk", "25": "swdio", "26": "run",
           "51": "usb_dm", "52": "usb_dp",   # 27R series to the mux (usb_dm_m / usb_dp_m)
           "55": "qspi_sd3", "56": "qspi_sclk", "57": "qspi_sd0", "58": "qspi_sd2", "59": "qspi_sd1", "60": "qspi_ss",
           "61": "GND"})

BLOCKS = [
 {"title": "power tree", "notes": [
   "AMIGA_5V (keyboard connector / A500 header) -> F1 -> AMIGA_5V_FUSED -> D1 -> SYS5V; USB-C -> D2 -> SYS5V. Either source alone runs the board.",
   "AMIGA_5V_FUSED feeds the bus switches and the Amiga-side pull-ups only, so a USB-C-powered board never back-feeds an Amiga that is off.",
   "F1 750 mA fitted (rev6 bring-up measured the stack well under that). SYS5V -> U3 -> +3V3 (RP2350, RM2, flash, OLED, mux). R60 bleeds D1 leakage off the fused rail."], "parts": [
   ("F1", "Device:Polyfuse", "750mA", "Fuse:Fuse_1812_4532Metric", {"1": "AMIGA_5V", "2": "AMIGA_5V_FUSED"}, {"MPN": "MF-MSMF075-2", "LCSC": "C20198", "Note": "fitted at assembly: AMIGA_5V_FUSED is the bus-switch supply, an empty F1 would mute the Amiga side. LCSC code to verify before order"}),
   ("D1", "Device:D_Schottky", "SS34", "Diode_SMD:D_SMA", {"1": "SYS5V", "2": "AMIGA_5V_FUSED"}, {"LCSC": "C8678"}),
   ("D2", "Device:D_Schottky", "SS34", "Diode_SMD:D_SMA", {"1": "SYS5V", "2": "AUX_5V"}, {"LCSC": "C8678"}),
   ("F2", "Device:Polyfuse", "500mA", "Fuse:Fuse_1812_4532Metric", {"1": "SYS5V", "2": "USBA_5V"}, {"LCSC": "C17313", "MPN": "MF-MSMF050-2"}),
   ("F3", "Device:Polyfuse", "500mA", "Fuse:Fuse_1812_4532Metric", {"1": "SYS5V", "2": "KBIN_5V"}, {"LCSC": "C17313", "MPN": "MF-MSMF050-2"}),
   C("C1", "22uF", "SYS5V", "GND", C0805, LCSC="C45783", Note="10 V X5R 0805; with C2 the SYS5V bulk, the host-port bulk is C4 behind F2"), C("C2", "10uF", "SYS5V", "GND", C0805), C("C3", "100nF", "SYS5V", "GND"),
   C("C4", "150uF", "USBA_5V", "GND", CELEC, Note="10 V rated"), C("C5", "100nF", "USBA_5V", "GND"),
   C("C6", "10uF", "KBIN_5V", "GND", C0805), C("C7", "100nF", "KBIN_5V", "GND"),
   ("U3", "Regulator_Linear:TLV75733PDRV", "TLV75733PDRVR", "Package_SON:WSON-6-1EP_2x2mm_P0.65mm_EP1x1.6mm", {"1": "+3V3", "2": "NC", "3": "GND", "4": "SYS5V", "5": "NC", "6": "SYS5V", "7": "GND"}, {"LCSC": "C2865999", "Note": "WSON-6 with thermal pad instead of SOT-23-5: up to 0.45 W during Wi-Fi TX. LCSC code to verify before order"}),
   C("C8", "1uF", "SYS5V", "GND"), C("C9", "1uF", "+3V3", "GND"), C("C10", "10uF", "+3V3", "GND", C0805),
   R("R46", "100k", "AMIGA_5V_FUSED", "amiga_sense", Note="Amiga-on sense divider to ADC2; firmware threshold ~2.5 V"), R("R47", "100k", "amiga_sense", "GND"), C("C11", "100nF", "amiga_sense", "GND"),
   R("R60", "1k", "AMIGA_5V_FUSED", "GND", Note="bleeder: keeps D1 reverse leakage from lifting the fused rail when the board runs on USB-C"),
   ("D3", "Device:D_Schottky", "SS34", "Diode_SMD:D_SMA", {"1": "LED_VDD", "2": "SYS5V"}, {"LCSC": "C8678", "Note": "drops the WS2812 supply so a 3.3 V data drive meets VIH"}),
   C("C12", "100nF", "LED_VDD", "GND"),
 ]},
 {"title": "to amiga (keyboard out) / from real keyboard (keyboard in) / A500 headers", "notes": [
   "Mini-DIN6 uses the PS/2 pin positions: 1 KDAT, 3 GND, 4 +5 V, 5 KCLK. DIN5 Amigas use a passive adapter.",
   "JP1 is for standalone use only (no Amiga on J1): bridges the A500 keyboard power LED through R45."], "parts": [
   ("J10", "rev7:MiniDIN6_PS2_Socket", "KBD OUT (to Amiga)", "rev7:MiniDIN6_PS2_Socket_CUI_MD-60S", {"1": "kbdata", "2": "NC", "3": "GND", "4": "AMIGA_5V", "5": "kbclock", "6": "NC", "SH": "GND"}, {"Sourcing": "hand-fitted THT"}),
   ("J12", "rev7:MiniDIN6_PS2_Socket", "KBD IN (from keyboard)", "rev7:MiniDIN6_PS2_Socket_CUI_MD-60S", {"1": "kbin_dat", "2": "NC", "3": "GND", "4": "KBIN_5V", "5": "kbin_clk", "6": "NC", "SH": "GND"}, {"Sourcing": "hand-fitted THT"}),
   ("J1", "Connector_Generic:Conn_01x08", "A500 KBD OUT", PIN % 8, {"1": "kbclock", "2": "kbdata", "3": "kbreset", "4": "AMIGA_5V", "5": "NC", "6": "GND", "7": "kb_pwrled", "8": "kb_drvled"}, {"LCSC": "C492407"}),
   ("J8", "Connector_Generic:Conn_01x08", "A500 KBD IN", PIN % 8, {"1": "kbin_clk", "2": "kbin_dat", "3": "kbreset", "4": "KBIN_5V", "5": "NC", "6": "GND", "7": "kb_pwrled", "8": "kb_drvled"}, {"LCSC": "C492407"}),
   ("JP1", "Jumper:SolderJumper_2_Open", "KBIN PWRLED", "Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm", {"1": "kb_pwrled", "2": "kb_pwrled_bal"}, {"in_bom": "no"}),
   R("R45", "470", "kb_pwrled_bal", "SYS5V"),
   TVS("D29", "kb_pwrled"), TVS("D30", "kb_drvled"),
 ]},
 {"title": "controller ports (DE9 male, straight-through F-F cables)", "notes": ["Pin 7 (+5 V) is not connected: the ports never power the board."], "parts": [
   ("P1", "Connector:DE9_Pins_MountingHoles", "DE9 PORT 1", "rev7:DSUB-9_Pins_Horizontal_P2.77x2.84mm_EdgePinOffset7.70mm_Housed_MountingHolesOffset9.12mm",
    {"1": "cont1_v", "2": "cont1_h", "3": "cont1_vq", "4": "cont1_hq", "5": "cont1_b3", "6": "cont1_b1", "7": "NC", "8": "GND", "9": "cont1_b2", "SH": "GND"}, {"Sourcing": "TE 5747840-4 / NorComp 182-009-113R531, 4-40 flush insert, hand-fitted"}),
   ("P2", "Connector:DE9_Pins_MountingHoles", "DE9 PORT 2", "rev7:DSUB-9_Pins_Horizontal_P2.77x2.84mm_EdgePinOffset7.70mm_Housed_MountingHolesOffset9.12mm",
    {"1": "cont2_v", "2": "cont2_h", "3": "cont2_vq", "4": "cont2_hq", "5": "cont2_b3", "6": "cont2_b1", "7": "NC", "8": "GND", "9": "cont2_b2", "SH": "GND"}, {"Sourcing": "TE 5747840-4 / NorComp 182-009-113R531, 4-40 flush insert, hand-fitted"}),
 ]},
 {"title": "level shifting: 2x SN74CBTD3861 bus switches, Amiga side on A, RP2350 side on B", "notes": [
   "VCC = AMIGA_5V_FUSED: the diode-dropped gate limits the B side to ~2.6-3.0 V typ (TI fig. 2) and the RP2350 internal pull-up completes the rise to 3.3 V. OE tied low (always on). Keep VCC >= 4.5 V.",
   "F1 is fitted: this rail is the switch supply. Cabling only the DE9 ports to a running Amiga leaves the rail dead and the port pull-ups drag the lines low - connect the keyboard cable or USB-C first (as rev6).",
   "With the Amiga off the switches are open and the B side floats: firmware enables the RP2350 internal pull-ups on every _lo input.",
   "Amiga-side pull-ups: 19 x 10k to AMIGA_5V_FUSED in seven 4-element arrays placed where the lines pass (9 elements spare). ESD clamps D10-D28 on every Amiga-facing line."], "parts": [
   CBTD("U6", U6_A), CBTD("U7", U7_A),
   C("C13", "100nF", "AMIGA_5V_FUSED", "GND"), C("C14", "100nF", "AMIGA_5V_FUSED", "GND"),
   ] + [RPACK(r, n, "AMIGA_5V_FUSED", *RPACK_FP.get(r, ("Resistor_SMD:R_Array_Convex_4x0603", "C29718"))) for r, n in RPACKS.items()]
     + [(r, "Device:R", "10k", "Resistor_SMD:R_0402_1005Metric", {"1": n, "2": "AMIGA_5V_FUSED"}, {"LCSC": "C25744"}) for n, r in PULLUPS.items()] + [TVS("D%d" % (10 + i), n) for i, n in enumerate(AMIGA_NETS)]},
 {"title": "usb: USB-C (power + device for flashing), USB-A host, 2:1 mux on the RP2350 USB PHY", "notes": [
   "U5 selects D1 (USB-C) when usb_sel is low, which R48 guarantees at reset, so a fresh board enumerates on USB-C. Firmware sets GPIO27 high to switch to the USB-A host port.",
   "USB-C is a UFP: 5.1k on each CC. VBUS -> AUX_5V -> D2 -> SYS5V. USB-A VBUS is SYS5V through F2."], "parts": [
   ("J13", "Connector:USB_C_Receptacle_USB2.0_16P", "USB-C", "Connector_USB:USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal",
    {"A1": "GND", "B1": "GND", "A12": "GND", "B12": "GND", "A4": "AUX_5V", "B4": "AUX_5V", "A9": "AUX_5V", "B9": "AUX_5V",
     "A5": "usb_cc1", "B5": "usb_cc2", "A6": "usbc_dp", "B6": "usbc_dp", "A7": "usbc_dm", "B7": "usbc_dm", "A8": "NC", "B8": "NC", "SH": "GND"}, {"LCSC": "C5184243", "MPN": "GCT USB4105-GF-A-120"}),
   R("R43", "5.1k", "usb_cc1", "GND"), R("R44", "5.1k", "usb_cc2", "GND"),
   ("U4", "Power_Protection:USBLC6-2SC6", "USBLC6-2SC6", "Package_TO_SOT_SMD:SOT-23-6", {"1": "usbc_dm", "6": "usbc_dm", "3": "usbc_dp", "4": "usbc_dp", "2": "GND", "5": "AUX_5V"}, {"LCSC": "C7519"}),
   ("J4", "Connector:USB_A", "USB-A HOST", "rev7:USB_A_Molex_67643_Horizontal", {"1": "USBA_5V", "2": "usba_dm", "3": "usba_dp", "4": "GND", "SH": "GND"}, {"LCSC": "C564920", "Sourcing": "hand-fitted THT"}),
   ("U2", "Power_Protection:USBLC6-2SC6", "USBLC6-2SC6", "Package_TO_SOT_SMD:SOT-23-6", {"1": "usba_dm", "6": "usba_dm", "3": "usba_dp", "4": "usba_dp", "2": "GND", "5": "USBA_5V"}, {"LCSC": "C7519"}),
   ("U5", "Interface_USB:TS3USB30EDGSR", "TS3USB30EDGSR", "Package_SO:MSOP-10_3x3mm_P0.5mm",
    {"1": "usb_sel", "2": "usbc_dp", "8": "usbc_dm", "3": "usba_dp", "7": "usba_dm", "4": "usb_dp_m", "6": "usb_dm_m", "5": "GND", "9": "GND", "10": "+3V3"}, {"LCSC": "C131998"}),
   C("C15", "100nF", "+3V3", "GND"), R("R48", "100k", "usb_sel", "GND"),
 ]},
 {"title": "mcu: RP2350A, 16 MB QSPI flash, 12 MHz crystal, BOOT / RESET, SWD", "notes": [
   "Per the RP2350 hardware design guide / Pico 2 W: VREG_VIN and VREG_AVDD on 3V3 (AVDD through 33R + 4.7uF), DVDD 4.7uF at pin 23 + 100nF at 6/39, 4.7uF CIN at VREG_VIN, 100nF per IOVDD pin, one 100nF shared by USB_OTP_VDD/QSPI_IOVDD.",
   "BOOT pulls QSPI_SS low at reset (USB bootloader on USB-C); RESET pulls RUN low."], "parts": [
   ("U1", "MCU_RaspberryPi:RP2350A", "RP2350A", "Package_DFN_QFN:QFN-60-1EP_7x7mm_P0.4mm_EP3.4x3.4mm", RP, {"LCSC": "C42411118"}),
   ("L1", "Device:L", "3.3uH", "Inductor_SMD:L_Murata_DFE201610P", {"1": "dvdd", "2": "vreg_lx"}, {"LCSC": "C42411119", "MPN": "AOTA-B201610S3R3-101-T", "Note": "RP2350 core regulator: 3.3uH +-20%, shielded, DCR <=250mR, Isat >=1.5A. POLARISED: pad 1 (dot) on +1V1/dvdd, pad 2 on VREG_LX per the design guide fig. 4; add a silk dot at pad 1 and check the reel orientation at JLCPCB"}),
   C("C38", "4.7uF", "dvdd", "GND", C0805, Note="core regulator output cap at L1/VREG_FB"),
   C("C20", "4.7uF", "dvdd", "GND", Note="0603 10V, fits the comb pitch"), C("C21", "100nF", "dvdd", "GND"), C("C22", "100nF", "dvdd", "GND"),
   C("C23", "100nF", "+3V3", "GND"), C("C24", "100nF", "+3V3", "GND"), C("C25", "100nF", "+3V3", "GND"), C("C26", "100nF", "+3V3", "GND"), C("C27", "100nF", "+3V3", "GND"), C("C28", "100nF", "+3V3", "GND"),
   C("C29", "4.7uF", "+3V3", "GND"), C("C30", "100nF", "+3V3", "GND"),
   R("R50", "33", "+3V3", "adc_avdd", Note="ADC_AVDD filter"), C("C31", "1uF", "adc_avdd", "GND"),
   R("R59", "33", "+3V3", "vreg_avdd", Note="VREG_AVDD filter per the RP2350 minimal design"), C("C37", "4.7uF", "vreg_avdd", "GND", C0805),
   R("R57", "27", "usb_dp", "usb_dp_m", Note="USB series termination, close to U1"), R("R58", "27", "usb_dm", "usb_dm_m"),
   ("U8", "Memory_Flash:W25Q128JVS", "W25Q128JVSIQ", "Package_SO:SOIC-8_5.3x5.3mm_P1.27mm",
    {"1": "qspi_ss", "2": "qspi_sd1", "3": "qspi_sd2", "4": "GND", "5": "qspi_sd0", "6": "qspi_sclk", "7": "qspi_sd3", "8": "+3V3"}, {"LCSC": "C97521"}),
   C("C32", "100nF", "+3V3", "GND"),
   ("SW1", "Switch:SW_Push", "BOOT", "Button_Switch_THT:SW_Tactile_SPST_Angled_PTS645Vx58-2LFS", {"1": "qspi_ss_sw", "2": "GND"}, {"MPN": "C&K PTS645VL58-2 LFS", "Sourcing": "hand-fitted THT; JLC alternative TS665ZJ C557599, land unverified"}),
   R("R52", "1k", "qspi_ss", "qspi_ss_sw"),
   ("SW2", "Switch:SW_Push", "RESET", "Button_Switch_THT:SW_Tactile_SPST_Angled_PTS645Vx58-2LFS", {"1": "run_sw", "2": "GND"}, {"MPN": "C&K PTS645VL58-2 LFS", "Sourcing": "hand-fitted THT; JLC alternative TS665ZJ C557599, land unverified"}),
   R("R53", "1k", "run", "run_sw", Note="RUN has an internal ~50k pull-up; 1k series as in the minimal design"),
   ("Y1", "Device:Crystal_GND24", "12MHz", "Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm", {"1": "xin", "3": "xout_r", "2": "GND", "4": "GND"}, {"LCSC": "C20625731", "MPN": "ABM8-272-T3"}),
   R("R54", "1k", "xout", "xout_r"), C("C33", "15pF", "xin", "GND"), C("C34", "15pF", "xout_r", "GND"),
   ("J2", "Connector_Generic:Conn_01x03", "SWD", PIN % 3, {"1": "swclk", "2": "GND", "3": "swdio"}, {"LCSC": "C2937625"}),
 ]},
 {"title": "radio: RM2 (CYW43439) on the Pico 2 W pin assignment", "notes": [
   "wl_d is the shared data line: DIN direct, DOUT through 470R, nIRQ through 10k, as on the Pico 2 W. WL_ON and BT_ON tied to GPIO23.",
   "VIN and VDDIO from +3V3 through FB1. GPIO0-2 unused."], "parts": [
   ("U9", "rev7:RM2", "RM2", "rev7:RaspberryPi_RM2",
    {"1": "GND", "4": "GND", "7": "GND", "11": "GND", "15": "GND", "21": "GND", "2": "NC", "19": "NC", "20": "NC",
     "3": "wl_clk", "5": "wl_d", "6": "wl_dout", "8": "NC", "9": "wl_cs", "10": "wl_irq", "12": "wl_on", "13": "wl_on",
     "14": "wl_vin", "16": "wl_vin", "17": "NC", "18": "NC"}, {"LCSC": "C49451547", "MPN": "Raspberry Pi RM2"}),
   ("FB1", "Device:FerriteBead", "600R@100MHz", "Inductor_SMD:L_0603_1608Metric", {"1": "+3V3", "2": "wl_vin"}, {"MPN": "BLM18AG601SN1D", "LCSC": "C1017"}),
   C("C35", "4.7uF", "wl_vin", "GND", C0805), C("C36", "100nF", "wl_vin", "GND"),
   R("R55", "470", "wl_dout", "wl_d"), R("R56", "10k", "wl_irq", "wl_d"),
 ]},
 {"title": "display / status / spare", "notes": ["J3 pin order is the common 0.96in SSD1306 module: GND VCC SCL SDA. LED1 is the only indicator: GPIO26 (output inverted in firmware) -> Q1 -> ws_din swings the full LED_VDD so the WS2812 VIH is met."], "parts": [
   ("J3", "Connector_Generic:Conn_01x04", "OLED", "Connector_PinSocket_2.54mm:PinSocket_1x04_P2.54mm_Vertical", {"1": "GND", "2": "+3V3", "3": "i2c_scl", "4": "i2c_sda"}, {"LCSC": "C2897367", "MPN": "PM254-1-04-Z-8.5"}),
   R("R41", "4.7k", "+3V3", "i2c_sda"), R("R42", "4.7k", "+3V3", "i2c_scl"),
   ("LED1", "LED:WS2812B-2020", "WS2812B-2020", "LED_SMD:LED_WS2812B-2020_PLCC4_2.0x2.0mm", {"4": "LED_VDD", "2": "GND", "3": "ws_din", "1": "NC"}, {"LCSC": "C52917434"}),
   ("Q1", "Transistor_FET:Q_NMOS_GSD", "2N7002", "Package_TO_SOT_SMD:SOT-23", {"1": "ws_drv", "2": "GND", "3": "ws_din"}, {"LCSC": "C8545", "Note": "open-drain inverter: full LED_VDD swing on ws_din; firmware sets GPIO26 output-invert"}),
   R("R49", "1k", "LED_VDD", "ws_din"),
   ("J7", "Connector_Generic:Conn_01x04", "SPARE / UART", PIN % 4, {"1": "GND", "2": "+3V3", "3": "uart_tx", "4": "uart_rx"}, {"LCSC": "C2937625", "Note": "1x3 header cut from stock; or PZ254V-11-04P"}),
 ]},
]

POWER_FLAG_NETS = ["AMIGA_5V", "AUX_5V", "SYS5V", "AMIGA_5V_FUSED", "USBA_5V", "KBIN_5V", "LED_VDD", "wl_vin", "dvdd", "adc_avdd", "vreg_avdd"]
