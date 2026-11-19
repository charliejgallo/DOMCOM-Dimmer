# PCB specification

Requirements for the DOMCOM Dimmer's own board (phase 4), replacing the
hand-made legacy board ([docs/HARDWARE.md](../docs/HARDWARE.md)). The
design itself (KiCad project, BOM, fabrication files) goes in this folder.

The firmware does not change for the new board as long as the pins below
are kept: the same binaries run on it, Zigbee or Wi-Fi.

## Two variants, one schematic

Both for fabrication at JLCPCB / PCBWay, 2 layers:

| | **THT** | **PCBA** |
|---|---|---|
| Purpose | hand assembly with a soldering iron | factory assembly (JLCPCB PCBA) |
| Components | through-hole only | SMD from the LCSC catalogue, *basic* parts preferred |
| Exceptions | — | through-hole parts no SMD can replace: screw terminals, female headers for the C6, the buck module, the electrolytic if needed (hand-soldered or THT assembly) |
| Output | Gerbers, drill, BOM | Gerbers, drill, BOM with LCSC part numbers, CPL (placement) |

Same circuit, same connectors and mounting holes in both, so that one
enclosure fits both. Prefer one KiCad schematic with two boards (or two
projects sharing the schematic) over two diverging designs.

## Microcontroller

- **ESP32-C6 Super Mini in female headers** (2.54 mm), not soldered: cheap,
  replaceable, its USB-C and BOOT/RESET buttons included. Check the
  module's actual pinout and footprint before routing; clones differ.
- Pins (the firmware uses these; keep them):

  | Signal | Super Mini pin | Notes |
  |---|---|---|
  | PWM channel 1 | **GPIO18** | to the gate driver input; pull-down so the output is off while the C6 boots or is unplugged |
  | External button | **GPIO19** | to GND; internal pull-up in firmware (add an external one, see below) |
  | BOOT | **GPIO9** | the module's own button; optionally a second tactile switch on the board to GND, **nothing else on GPIO9** (strapping pin: must not be pulled low at power-up) |
  | PWM channel 2 (reserved) | **GPIO20** (check it is free and not a strapping pin on the module) | populated only in a future CCT version |
  | 5 V, GND | 5V, G | from the buck converter |

  Avoid the other strapping pins (GPIO4, 5, 8, 15) for anything new.
- **USB access:** the module's USB-C must stay reachable with the board in
  its enclosure (flashing and recovery), and its BOOT button too: BOOT held
  10 s is the only way to reset the device.
- **Antenna:** the module's PCB antenna end overhangs the board edge or sits
  over a copper keep-out (no copper on either layer, no tall parts next to
  it). The device is a Zigbee router; range matters.

## Power

- **Input:** 12 or 24 V DC (design for 9–30 V), **up to 10 A** of strip
  current.
- **Reverse polarity:** protect at least the logic side (the buck input);
  ideally the whole board (e.g. a P-MOSFET or an ideal-diode controller
  rated for 10 A, or a fuse plus a crowbar diode). Choose and justify.
- **Fuse:** on the input, sized for 10 A continuous (blade fuse holder or
  SMD/THT fuse rated for it; PTC only for the logic side).
- **Transients:** TVS across the input suitable for a 24 V system
  (e.g. SMBJ30A-class / 1.5KE33A-class in THT), plus bulk capacitance
  (≥ 220 µF, rated ≥ 35 V, low ESR) near the output stage.
- **5 V for the module:** the **HW-613** buck module (replaces the Mini360
  of the legacy board), in both variants as a plug-in or soldered module.
  Check its footprint, pinout and input range (it must take 30 V) from the
  actual module. Optional for PCBA only: an on-board buck IC instead, if it
  clearly simplifies assembly; keep the HW-613 footprint as the default.

## Output stage (per channel)

What the legacy board taught ([docs/HARDWARE.md](../docs/HARDWARE.md)):
the PWM runs at **19.5 kHz** (4 kHz hummed), and with the gate driven
straight from the GPIO through a resistor the edges were so slow that
nothing lit below ~5 % duty. The new board should turn the MOSFET on and
off fast enough to bring that floor well below 1 % (a 1 % pulse at
19.5 kHz is 0.5 µs).

- **Low-side N-MOSFET**, logic level, **V_DS ≥ 40 V** (60 V preferred),
  R_DS(on) ≤ 10 mΩ at the actual gate voltage, so that 10 A dissipates
  about 1 W or less. THT: TO-220 with room for a small clip-on heatsink;
  PCBA: a power SMD package (DPAK / D2PAK / PowerPAK-class) with a copper
  area on both layers and thermal vias.
- **Gate driver** between the GPIO (3.3 V) and the gate: rise/fall times
  of tens of ns. THT: a DIP-8 low-side driver; PCBA: a SOT-23-5 single
  low-side driver. Driver supply 5 V (gate at 5 V, so the MOSFET must be
  fully on at V_GS = 4.5 V) or, if a higher gate voltage is chosen, a rail
  that stays within the driver's and the MOSFET's V_GS limits at 30 V in.
- Gate resistor (≈ 4.7–22 Ω), gate-source pull-down (≈ 10 kΩ), and a
  pull-down at the driver input.
- **Strip side:** freewheel / snubber is not normally needed for LED strips;
  leave an optional footprint for an RC snubber across drain–source.
- **Channel 2:** the same stage with every part optional (footprints
  only), for a future warm/cool white (CCT) version.

## Connectors

- **Power in:** 2-pole screw terminal, 5.08 mm, rated ≥ 15 A.
- **LED out:** V+ and CH1− (and CH2− when populated): 3-pole screw terminal,
  5.08 mm, rated ≥ 15 A. V+ is the input voltage after the fuse.
- **Button:** 2-pole terminal (3.5 or 5.08 mm) to GND and GPIO19, for a
  wall button on a possibly long cable: series resistor (≈ 1 kΩ), pull-up
  (≈ 10 kΩ to 3.3 V), filter capacitor (≈ 100 nF) and ESD protection.
- Optional: a status LED and test points for 5 V, 3.3 V, PWM and the gate.

## Layout

- **High-current path** (input → fuse → V+ terminal; CH− → MOSFET → GND →
  input): short and wide, copper pours on both layers stitched with vias,
  sized for 10 A with 1 oz copper (or specify 2 oz); no thermal reliefs on
  the power terminals.
- Keep the gate loop (driver → resistor → gate → source → driver GND) small;
  the power ground and the logic ground meet at one point near the MOSFET
  source.
- Antenna keep-out as above; the buck converter and the power loop away
  from the antenna.
- **M3 mounting holes** in the corners, insulated from the power copper.
- Size: no larger than the legacy board (about 8 × 4.5 cm) if possible.
- Silkscreen: polarity and function of every terminal, input voltage range,
  board name and version, the BOOT button's location.

## Deliverables

- KiCad 9 project(s) in `hardware/`: schematic, both PCBs, footprints and
  symbols that are not in the standard libraries, 3D models where
  available.
- For each variant: Gerbers and drill files (JLCPCB settings), BOM, and for
  PCBA the CPL file and LCSC part numbers; renders of both sides.
- ERC and DRC clean against the manufacturer's rules.
- `hardware/README.md`: what each variant is, how to order it, assembly
  notes, and the choices made (reverse polarity, MOSFET, driver, buck)
  with their reasons.
- `docs/HARDWARE.md` and the README updated for the new board.
- License for the hardware files: **CC BY-NC-SA 4.0** (the firmware stays
  under PolyForm Noncommercial).

## Out of scope

- Firmware changes (the pins above are fixed by the current firmware).
- The enclosure (later; leave the board dimensions and mounting holes
  documented for it).
