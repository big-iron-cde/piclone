# Pinout Reference

The full per-chip pin maps. For the condensed bus/connection tables, see below:
[Wiring](wiring.md).

## Raspberry Pi Pico 2 — GPIO allocation

| Pico physical pin | GP | Function | Wired to |
|---|---|---|---|
| 1 | GP0 | A0 in | 65C02 pin 9, RAM pin 10 |
| 2 | GP1 | A1 in | 65C02 pin 10, RAM pin 9 |
| 4 | GP2 | A2 in | 65C02 pin 11, RAM pin 8 |
| 5 | GP3 | A3 in | 65C02 pin 12, RAM pin 7 |
| 6 | GP4 | A4 in | 65C02 pin 13, RAM pin 6 |
| 7 | GP5 | A5 in | 65C02 pin 14, RAM pin 5 |
| 9 | GP6 | A6 in | 65C02 pin 15, RAM pin 4 |
| 10 | GP7 | A7 in | 65C02 pin 16, RAM pin 3 |
| 11 | GP8 | A8 in | 65C02 pin 17, RAM pin 25 |
| 12 | GP9 | A9 in | 65C02 pin 18, RAM pin 24 |
| 14 | GP10 | A10 in | 65C02 pin 19, RAM pin 21 |
| 15 | GP11 | A11 in | 65C02 pin 20, RAM pin 23 |
| 16 | GP12 | A12 in | 65C02 pin 22, RAM pin 2 |
| 17 | GP13 | A13 in | 65C02 pin 23, RAM pin 26 |
| 19 | GP14 | A14 in | 65C02 pin 24, RAM pin 1 |
| 20 | GP15 | D0 io | 65C02 pin 33, RAM pin 11 |
| 21 | GP16 | D1 io | 65C02 pin 32, RAM pin 12 |
| 22 | GP17 | D2 io | 65C02 pin 31, RAM pin 13 |
| 24 | GP18 | D3 io | 65C02 pin 30, RAM pin 15 |
| 25 | GP19 | D4 io | 65C02 pin 29, RAM pin 16 |
| 26 | GP20 | D5 io | 65C02 pin 28, RAM pin 17 |
| 27 | GP21 | D6 io | 65C02 pin 27, RAM pin 18 |
| 29 | GP22 | D7 io | 65C02 pin 26, RAM pin 19 |
| 31 | GP26 | A15 in (chip enable) | 65C02 pin 25, RAM pin 20 |
| 32 | GP27 | RESET drive (open-drain emulated) | 65C02 pin 40 (RESB) |
| 34 | GP28 | PHI2 clock out (PWM, up to 1 MHz) | 65C02 pin 37 (PHI2) |
| 36 | GP23 | RWB in (read/write bar) | 65C02 pin 34 (RWB) |

### Notes on the special GPIOs

- **GP26 (A15):** also drives the RAM's `CE#`. When A15 = 1 (ROM region) the RAM is
  deselected and the Pico drives data; when A15 = 0 (RAM region) the Pico stays Hi-Z and
  the RAM drives data. Same wire, both purposes.
- **GP23 (RWB):** the Pico reads this to distinguish CPU read cycles (`RWB high → 0`) from
  write cycles (`RWB low → 1`) in the bus monitor.
- **GP27 (RESET):** configured as INPUT to release reset (10 kΩ pull-up runs the CPU),
  OUTPUT LOW to assert reset.
- **GP28 (PHI2):** fixed clock output at **0.2 Hz** (5 s per cycle) for step-by-step
  learning. Configurable in firmware for faster speeds.

## W65C02S — 40-pin DIP (top view, notch up)

```
                ┌────────U────────┐
            VPB │ 1            40 │ RESB     ← Pico GP27 + 10 kΩ pull-up
            RDY │ 2            39 │ PHI2O    leave open
          PHI1O │ 3            38 │ SOB      ← 10 kΩ pull-up (R6)
           IRQB │ 4            37 │ PHI2     ← Pico GP28
            MLB │ 5            36 │ BE       ← 10 kΩ pull-up only
           NMIB │ 6            35 │ NC       leave open
           SYNC │ 7            34 │ RWB      → RAM WE# (pin 27)
            VDD │ 8            33 │ D0       ↔ Pico GP15, RAM pin 11
             A0 │ 9            32 │ D1       ↔ Pico GP16, RAM pin 12
             A1 │ 10           31 │ D2       ↔ Pico GP17, RAM pin 13
             A2 │ 11           30 │ D3       ↔ Pico GP18, RAM pin 15
             A3 │ 12           29 │ D4       ↔ Pico GP19, RAM pin 16
             A4 │ 13           28 │ D5       ↔ Pico GP20, RAM pin 17
             A5 │ 14           27 │ D6       ↔ Pico GP21, RAM pin 18
             A6 │ 15           26 │ D7       ↔ Pico GP22, RAM pin 19
             A7 │ 16           25 │ A15      → Pico GP26, RAM CE# (pin 20)
             A8 │ 17           24 │ A14      → Pico GP14, RAM pin 1
             A9 │ 18           23 │ A13      → Pico GP13, RAM pin 26
            A10 │ 19           22 │ A12      → Pico GP12, RAM pin 2
            A11 │ 20           21 │ VSS      → GND
                └─────────────────┘
```

**Power and ground:** pin 8 (VDD) → +3.3 V; pin 21 (VSS) → GND.

**Pull-ups to +3.3 V (10 kΩ each):** pin 2 (RDY, R1), pin 4 (IRQB, R2), pin 6 (NMIB, R3),
pin 36 (BE, R4), pin 40 (RESB, R5), pin 38 (SOB, R6).

**Leave open:** pins 1 (VPB), 3 (PHI1O), 5 (MLB), 7 (SYNC), 35 (NC), 39 (PHI2O).

**Special wire:** pin 34 (RWB) → **RAM pin 27 (WE#)** (not the Pico). This is how the CPU
tells the RAM "I'm writing now."

## HM62256LP — 28-pin DIP (top view, notch up)

```
                ┌────────U────────┐
            A14 │ 1            28 │ VCC      → +3.3 V rail
            A12 │ 2            27 │ WE#      ← 65C02 pin 34 (RWB)
             A7 │ 3            26 │ A13      ← Pico GP13 (also 65C02 pin 23)
             A6 │ 4            25 │ A8       ← Pico GP8 (also 65C02 pin 17)
             A5 │ 5            24 │ A9       ← Pico GP9 (also 65C02 pin 18)
             A4 │ 6            23 │ A11      ← Pico GP11 (also 65C02 pin 20)
             A3 │ 7            22 │ OE#      → +3.3 V (outputs disabled)
             A2 │ 8            21 │ A10      ← Pico GP10 (also 65C02 pin 19)
             A1 │ 9            20 │ CE#      ← Pico GP26 (= A15, also 65C02 pin 25)
             A0 │ 10           19 │ D7       ↔ Pico GP22 (also 65C02 pin 26)
             D0 │ 11           18 │ D6       ↔ Pico GP21 (also 65C02 pin 27)
             D1 │ 12           17 │ D5       ↔ Pico GP20 (also 65C02 pin 28)
             D2 │ 13           16 │ D4       ↔ Pico GP19 (also 65C02 pin 29)
            VSS │ 14           15 │ D3       ↔ Pico GP18 (also 65C02 pin 30)
                └─────────────────┘
```

**Power and ground:** pin 28 (VCC) → +3.3 V; pin 14 (VSS) → GND.

**Why RAM CE# = A15 works:** `CE#` is active-low. We want the RAM selected for
`$0000–$7FFF` (A15 = 0). Tying `CE#` to A15 gives `A15 = 0 → CE# = 0 → RAM selected` and
`A15 = 1 → CE# = 1 → RAM Hi-Z`. No inverter needed.

**Why OE# = +3.3 V (not GND):** `OE#` is active-low. Held high, the RAM never drives the
data bus — it only accepts writes (via `WE#`). This avoids bus contention during CPU
stores. Reads from RAM return floating garbage, which is fine for ROM-only test programs
that only write to RAM.
