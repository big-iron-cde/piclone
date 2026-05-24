# Pico-as-ROM — Minimal Breadboard Prototype

A Raspberry Pi Pico 2 acts as the ROM for a W65C02S CPU on a breadboard. Single 3.3 V logic level throughout, no MOSFETs, no oscillator chip, no bypass caps. Just the Pico, the CPU, the SRAM, some resistors, and wires.

**Power in practice:** USB powers the Pico; a separate external 3.3 V supply powers the 65C02, RAM, and pull-ups. Do **not** tie the external supply to Pico VSYS while USB is plugged in (see §5).

## Inventory (what this build uses)

| Part | Qty | Notes |
|---|---|---|
| Raspberry Pi Pico 2 | 1 | Acts as ROM + clock source |
| W65C02S | 1 | The CPU |
| HM62256LP | 1 | RAM. **5 V part, running at 3.3 V — out of spec, may be flaky** |
| 10 kΩ resistor | 6 | Pull-ups for 65C02 control inputs |
| External 3.3 V power supply | 1 | Powers 65C02 + RAM + pull-ups (see §5) |
| Breadboard | 1 | Full-size (~830 tie-points) recommended |
| Jumper wires | ~50 | |

**Not used in this prototype** (and not needed): MOSFETs, oscillator can, bypass caps, level shifters.

---

## 1. Memory map

```
$0000 ─┬─────────────────┐
       │   RAM (HM62256) │   A15 = 0
       │                 │
$7FFF ─┤                 │
$8000 ─┼─────────────────┤
       │   ROM (Pico)    │   A15 = 1
       │                 │
$FFFF ─┴─────────────────┘
```

A15 is the chip-select. When A15 = 0 → RAM is selected. When A15 = 1 → Pico (acting as ROM) drives the bus.

No external address decoder needed. A15 itself does all the decoding.

---

## 2. Pi Pico 2 — GPIO allocation

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

**Power (recommended — split supply):**
- **Pico:** powered from **USB only** while developing. Do **not** connect external 3.3 V to Pico pin 39 (VSYS) or pin 36 (3V3 OUT) when USB is plugged in — USB back-feeds ~4.5 V on VSYS and fights the breadboard rail, which stalls the CPU.
- **Breadboard rail:** external 3.3 V → 65C02 VDD, RAM VCC, pull-up resistors, RAM OE#.
- **Common ground:** tie Pico GND pins and breadboard GND together (required even with split supplies).

**Alternative (USB disconnected):** one external 3.3 V supply can feed the breadboard rail **and** Pico pin 36 (3V3 OUT) directly — never use VSYS for a regulated 3.3 V input when you also have USB connected.

**Notes on the special GPIOs:**
- **GP26 (A15)**: also goes to the RAM's CE# pin. When A15 = 1 (ROM region), RAM is deselected and Pico drives data. When A15 = 0 (RAM region), Pico stays Hi-Z and RAM drives data. Same wire, both purposes.
- **GP27 (RESET)**: configured as INPUT to release reset, OUTPUT LOW to assert reset. With a 10 kΩ pull-up on the RESB line, INPUT = line floats high = CPU runs. OUTPUT LOW = line forced low = CPU held in reset.
- **GP28 (PHI2)**: PWM output, 50 % duty. Replaces the oscillator can. Start at **100 kHz** (`c100`) for bring-up; increase to 500 kHz–1 MHz once stable.

---

## 3. W65C02S — pin-by-pin handling

40-pin DIP, top view (notch up):

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

**Power and ground:**
- Pin 8 (VDD) → +3.3 V rail
- Pin 21 (VSS) → GND rail

**Pull-ups to +3.3 V (10 kΩ each, 6 resistors):**
- Pin 2 (RDY) → 10 kΩ → +3.3 V (R1)
- Pin 4 (IRQB) → 10 kΩ → +3.3 V (R2)
- Pin 6 (NMIB) → 10 kΩ → +3.3 V (R3)
- Pin 36 (BE) → 10 kΩ → +3.3 V (R4 — Pico doesn't touch BE; keeps bus enabled)
- Pin 40 (RESB) → 10 kΩ → +3.3 V (R5 — Pico GP27 pulls low to assert reset)
- Pin 38 (SOB) → 10 kΩ → +3.3 V (R6)

**Leave open:** pins 1 (VPB), 3 (PHI1O), 5 (MLB), 7 (SYNC), 35 (NC), 39 (PHI2O).

**Special wire:** pin 34 (RWB) → goes to **RAM pin 27 (WE#)**. Not to the Pico. This is how the CPU tells the RAM "I'm writing now."

---

## 4. HM62256LP — pin-by-pin handling

28-pin DIP, top view (notch up):

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

**Power and ground:**
- Pin 28 (VCC) → +3.3 V rail
- Pin 14 (VSS) → GND rail

**Wires the RAM needs:**

| RAM pin | Goes to | Notes |
|---|---|---|
| 1 (A14) | 65C02 pin 24 + Pico GP14 | shared address bus |
| 2 (A12) | 65C02 pin 22 + Pico GP12 | |
| 3 (A7) | 65C02 pin 16 + Pico GP7 | |
| 4 (A6) | 65C02 pin 15 + Pico GP6 | |
| 5 (A5) | 65C02 pin 14 + Pico GP5 | |
| 6 (A4) | 65C02 pin 13 + Pico GP4 | |
| 7 (A3) | 65C02 pin 12 + Pico GP3 | |
| 8 (A2) | 65C02 pin 11 + Pico GP2 | |
| 9 (A1) | 65C02 pin 10 + Pico GP1 | |
| 10 (A0) | 65C02 pin 9 + Pico GP0 | |
| 11–13, 15–19 (D0–D7) | 65C02 D0–D7 + Pico GP15–GP22 | shared data bus |
| 20 (CE#) | Pico GP26 + 65C02 pin 25 (A15) | RAM selected when A15 = 0 |
| 21 (A10) | 65C02 pin 19 + Pico GP10 | |
| 22 (OE#) | +3.3 V rail | **outputs disabled** — CPU writes still work via WE#; prevents RAM from fighting the data bus |
| 23 (A11) | 65C02 pin 20 + Pico GP11 | |
| 24 (A9) | 65C02 pin 18 + Pico GP9 | |
| 25 (A8) | 65C02 pin 17 + Pico GP8 | |
| 26 (A13) | 65C02 pin 23 + Pico GP13 | |
| 27 (WE#) | 65C02 pin 34 (RWB) | only the CPU triggers writes |

**Why RAM CE# = A15 works:** CE# is active-low. We want RAM selected when address is in $0000–$7FFF (A15 = 0). Tying CE# directly to A15: A15 = 0 → CE# = 0 → RAM selected. A15 = 1 → CE# = 1 → RAM Hi-Z. Perfect match, no inverter needed.

**Why OE# = +3.3 V (not GND):** OE# is active-low. Tied high, the RAM never drives the data bus — only accepts writes. This avoids bus contention during CPU stores (confirmed necessary on this breadboard build). Reads from RAM return floating garbage, which is fine for ROM-only test programs that only write to RAM.

**Virtual print port:** the demo program stores results at **$4000** so the Pico `watch` command can snoop CPU writes over USB. Avoid addresses whose high byte matches common data values on a noisy breadboard (e.g. `$5000` often reads back as `$50` due to address-line crosstalk onto D0–D7).

---

## 5. Power rails

**Recommended setup (USB + external supply):**

```
USB ──────────────→ Pico (5 V via USB, onboard reg → 3.3 V logic)

External +3.3 V ──┬─→ 65C02 pin 8 (VDD)
                  ├─→ RAM pin 28 (VCC)
                  ├─→ RAM pin 22 (OE#)
                  └─→ Top of all 6 pull-up resistors (R1–R6)

Common GND ───────┬─→ Pico pins 3, 8, 13, 18, 23, 28, 33, 38
                  ├─→ 65C02 pin 21 (VSS)
                  ├─→ RAM pin 14 (VSS)
                  └─→ External supply GND
```

**Do not connect** external +3.3 V to **Pico pin 39 (VSYS)** while USB is plugged in. That creates a power fight (~4.5 V from USB vs 3.3 V external) that freezes the CPU with a stuck address bus.

**Use the breadboard's power rails** for the external supply. Run +3.3 V along one rail and GND along the other. Every chip's VCC/GND wire is a short hop to the nearest rail.

---

## 6. Complete connection list (one place to look while wiring)

### Bus connections (shared by 65C02, Pico, RAM)

| Net | 65C02 pin | Pico pin (GP) | RAM pin |
|---|---|---|---|
| A0 | 9 | 1 (GP0) | 10 |
| A1 | 10 | 2 (GP1) | 9 |
| A2 | 11 | 4 (GP2) | 8 |
| A3 | 12 | 5 (GP3) | 7 |
| A4 | 13 | 6 (GP4) | 6 |
| A5 | 14 | 7 (GP5) | 5 |
| A6 | 15 | 9 (GP6) | 4 |
| A7 | 16 | 10 (GP7) | 3 |
| A8 | 17 | 11 (GP8) | 25 |
| A9 | 18 | 12 (GP9) | 24 |
| A10 | 19 | 14 (GP10) | 21 |
| A11 | 20 | 15 (GP11) | 23 |
| A12 | 22 | 16 (GP12) | 2 |
| A13 | 23 | 17 (GP13) | 26 |
| A14 | 24 | 19 (GP14) | 1 |
| A15 / RAM CE# | 25 | 31 (GP26) | 20 |
| D0 | 33 | 20 (GP15) | 11 |
| D1 | 32 | 21 (GP16) | 12 |
| D2 | 31 | 22 (GP17) | 13 |
| D3 | 30 | 24 (GP18) | 15 |
| D4 | 29 | 25 (GP19) | 16 |
| D5 | 28 | 26 (GP20) | 17 |
| D6 | 27 | 27 (GP21) | 18 |
| D7 | 26 | 29 (GP22) | 19 |

### Point-to-point connections

| From | To | Purpose |
|---|---|---|
| 65C02 pin 34 (RWB) | RAM pin 27 (WE#) | Write enable to RAM |
| Pico pin 32 (GP27) | 65C02 pin 40 (RESB) | Reset control |
| Pico pin 34 (GP28) | 65C02 pin 37 (PHI2) | Clock (start 100 kHz, up to 1 MHz) |

### Pull-up resistors (6 × 10 kΩ, all top to +3.3 V)

| Resistor | Pulls up | Why |
|---|---|---|
| R1 | 65C02 pin 2 (RDY) | CPU stalls if RDY floats low |
| R2 | 65C02 pin 4 (IRQB) | Inactive (high) when unused |
| R3 | 65C02 pin 6 (NMIB) | Inactive (high) when unused |
| R4 | 65C02 pin 36 (BE) | Bus always enabled |
| R5 | 65C02 pin 40 (RESB) | High when Pico isn't asserting reset |
| R6 | 65C02 pin 38 (SOB) | Inactive (high) when unused — optional but cheap insurance |

### Power and ground (separate from the bus table)

| From | To |
|---|---|
| USB | Pico (development — powers Pico only) |
| External +3.3 V | 65C02 pin 8, RAM pin 28, RAM pin 22 (OE#), top of R1–R6 |
| Common GND | Pico GND pins, 65C02 pin 21, RAM pin 14, external supply GND |

**Not connected when USB is in use:** Pico pin 39 (VSYS) and pin 36 (3V3 OUT) ← external +3.3 V

### Leave open

| 65C02 pins | Pico pins |
|---|---|
| 1 (VPB), 3 (PHI1O), 5 (MLB), 7 (SYNC), 35 (NC), 39 (PHI2O) | 30 (RUN), 35 (ADC_VREF), 36 (3V3 OUT), 37 (3V3_EN), 40 (VBUS) |

---

## 7. Firmware and software workflow

Working code lives in **`pico-rom-test/`** (Pico firmware) and **`rom-builder/`** (host tools).

### What the Pico firmware does

1. **Generate PHI2** — GP28 as PWM square wave. Commands: `c100` (100 kHz), `c500`, `c1` (1 MHz), `cs` (stop).
2. **Reset control** — GP27 starts as OUTPUT LOW. Commands: `r0` (hold reset), `r1` (release — switches to INPUT, pull-up runs CPU).
3. **ROM emulation** — 32 KB `rom_image[]` in SRAM, mapped to CPU $8000–$FFFF. When A15 = 1, drive GP15–GP22 from `rom_image[addr & 0x7FFF]`; when A15 = 0, data bus Hi-Z. Commands: `rom` / `roms`. Implemented with **GPIO polling** (works reliably at 100 kHz–1 MHz on this build; PIO+DMA is a future upgrade).
4. **ROM upload** — `loadbin` receives 32 KB raw binary over USB-CDC into `rom_image[]`.
5. **Bus watch (print port)** — `watch 4000` prints data whenever the CPU accesses that address. Demo program writes `$05` then `$08` to `$4000` each loop. Firmware filters spurious samples where data equals the address high byte (breadboard crosstalk).
6. **Debug** — `a` / `am` / `as` (address bus), `s` (status), `h` (help).

Flash **`pico-rom-test/build/pico-rom-test.uf2`** to the Pico (BOOTSEL + drag, or `picotool load`).

### Host-side workflow

```bash
# 1. Build a 32 KB ROM image (program at CPU $8000, vectors at $FFFC)
cd rom-builder
python3 build-rom.py          # → bin/rom.bin

# 2. Upload to Pico and start the CPU (closes any stale screen session first)
python3 upload-rom.py         # loadbin → rom on → watch 4000 → c100 → r1
```

Expected output after upload:

```
[$4000 = $05]
[$4000 = $08]
[$4000 = $05]
...
```

**Note:** `rom_image[]` is lost on Pico power cycle — re-run `upload-rom.py` after each reboot. Only one program can hold `/dev/ttyACM0` at a time; exit `screen` (`Ctrl-A k`) before uploading.

### Demo program (in `build-rom.py`)

```
$8000: CLC
       LDA #$05
       STA $4000       ; Pico watch port
       ADC #$03        ; A = 8
       STA $4000
       JMP $8000
$FFFC: $8000           ; reset vector
```

---

## 8. Pre-power sanity checks

1. **Verify all 6 pull-up resistors** — each is 10 kΩ from a 65C02 control pin to +3.3 V (R1–R6 in §6).
2. **Verify RAM OE# (pin 22) → +3.3 V**, not GND.
3. **Verify chip orientations** — both 65C02 and RAM have notches at the top. Reversed orientation usually shorts VCC to GND instantly.
4. **Continuity-check the shared bus wires.** A0 should beep between 65C02 pin 9, Pico pin 1, and RAM pin 10. Repeat for each address and data line.
5. **Check no short** between +3.3 V rail and GND rail.
6. **Power the breadboard** (external 3.3 V). Measure 3.3 V at 65C02 pin 8 and RAM pin 28. **Do not** connect external 3.3 V to Pico VSYS yet.
7. **Flash `pico-rom-test.uf2`**, plug in USB (Pico only), tie Pico GND to breadboard GND.

---

## 9. Bring-up sequence

1. **Wire per §6**, flash **`pico-rom-test.uf2`**, connect USB (Pico) + external 3.3 V (breadboard), common GND.
2. **Open serial** — `python3 upload-rom.py` handles upload and live output; or `picocom /dev/ttyACM0 -b 115200` for interactive commands (only one at a time).
3. **Hardware smoke test** (interactive serial):
   ```
   r0          ; hold CPU in reset
   c100        ; 100 kHz clock — safe starting speed
   r1          ; release reset
   am          ; address bus should be changing
   as          ; stop monitor
   ```
4. **Dumb-ROM test** (no upload needed after fresh boot):
   ```
   rom         ; serve $EA NOPs for all ROM reads
   r0 / r1
   am          ; addresses should march through $8000+ region
   ```
5. **Full program test:**
   ```bash
   cd rom-builder
   python3 build-rom.py
   python3 upload-rom.py
   ```
   Expect `[$4000 = $05]` / `[$4000 = $08]` alternating. Increase speed with `c500` or `c1` once stable.
6. **RAM test (optional):** program that `STA`s then `LDA`s from `$0200`. HM62256 at 3.3 V may still be flaky — if reads fail, try slower clock or replace with 3.3 V SRAM later.

---

## 10. Things to watch out for during bring-up

| Symptom | Likely cause |
|---|---|
| Address bus stuck at one value, never changes | No clock on PHI2 (check GP28 wire to CPU pin 37) or power fight (external 3.3 V on Pico VSYS while USB connected) |
| 65C02 address bus doesn't change | RDY floating (verify R1) or clock not reaching CPU |
| Address bus changes but stuck at $FFFE forever | CPU in reset (RESB low — check R5 + Pico GP27 = INPUT after `r1`) |
| Address goes $FFFC, $FFFD, then random garbage | ROM emu off (`rom` not enabled) or D0–D7 / address lines mis-wired |
| Reset vector fetches correct bytes but jumps to wrong address | Endianness — reset vector low byte at $FFFC, high byte at $FFFD |
| Watch shows `$50` at `$5000` or `$40` at `$4000` | Address-line crosstalk onto data bus — use `$4000` print port; firmware filters high-byte matches |
| Watch shows `$05` but never `$08`, or CPU crashes quickly | RAM OE# still on GND — move to +3.3 V; or drop clock to `c100` |
| RAM reads return garbage (writes seem to work) | HM62256 at 3.3 V out of spec — try `c100`; replace with 3.3 V SRAM for production |
| Random behavior when touching breadboard | Loose wire or missing decoupling — re-seat connections; 100 nF cap helps |
| `upload-rom.py`: Device or resource busy | `screen` or another program holds `/dev/ttyACM0` — kill it first |
| Pico USB disconnects when 65C02 boots | Brownout — 3.3 V supply can't deliver enough current |

---

## 11. What this prototype can and cannot do

**Can:**
- Run the 65C02 from a Pico-hosted 32 KB ROM image ($8000–$FFFF)
- Build ROM on a laptop (`build-rom.py`), upload over USB (`upload-rom.py` / `loadbin`)
- Reset the 65C02 and change clock speed from the Pico
- Snoop a virtual print port (`watch 4000`) — see CPU stores over USB
- Read and write RAM (subject to HM62256 + 3.3 V behavior)

**Cannot (deliberately deferred for the prototype):**
- Fail-safe behavior when Pico is unpowered — line state of RESB is governed only by the pull-up, which is correct, but the data bus could be back-powered through Pico GPIO protection diodes. Don't leave a powered 65C02 connected to an unpowered Pico for long.
- Tri-state the CPU mid-program for hot-swap ROM updates — BE is permanently held high. To update the ROM you must drive RESET# low first.
- Robust noise immunity — no decoupling caps.
- Guaranteed RAM reliability — HM62256 is out of spec at 3.3 V.

All four of those are fixable with parts you don't have right now (MOSFETs, bypass caps, 3.3 V SRAM). They're not blockers for proving the design works.

---

*Last updated: 2026-05-23. Verified on breadboard with Pico 2, W65C02S, HM62256LP (OE# → 3.3 V), split USB + external 3.3 V power.*
