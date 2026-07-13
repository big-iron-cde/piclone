# Pre-power Checks & Bring-up

## Pre-power sanity checks

1. **Verify all 6 pull-up resistors:** each is 10 kΩ from a 65C02 control pin to +3.3 V
   (R1–R6, see [Wiring](wiring.md)).
2. **Verify RAM OE# (pin 22) → +3.3 V**, not GND.
3. **Verify chip orientations:** both the 65C02 and RAM have notches at the top. Reversed
   orientation usually shorts VCC to GND instantly.
4. **Continuity-check the shared bus wires.** A0 should beep between 65C02 pin 9, Pico
   pin 1, and RAM pin 10. Repeat for each address and data line.
5. **Check no short** between the +3.3 V rail and GND rail.
6. **Power the breadboard** (external 3.3 V). Measure 3.3 V at 65C02 pin 8 and RAM pin 28.
   Do **not** connect external 3.3 V to Pico VSYS yet.
7. **Flash `piclone.uf2`**, plug in USB (Pico only), tie Pico GND to breadboard GND.

## Bring-up sequence

1. **Wire per [Wiring](wiring.md)**, flash `piclone.uf2`, connect USB (Pico) +
   external 3.3 V (breadboard), common GND.
2. **Set up host tools:** install [Romulan](https://github.com/big-iron-cde/romulan):
   `cd ~/Downloads/romulan && uv sync`.
3. **Dumb-ROM test** (no upload needed after a fresh boot): plug in USB, the built-in demo
   starts automatically and the CPU runs at **1 kHz**. Observe it:

   ```bash
   cd ~/Downloads/romulan
   uv run python -c "
   from romulan.hardware_api import open_hardware_api
   with open_hardware_api() as api:
       api.monitor(enable=True)
       input('Press Enter to stop...')
       api.monitor(enable=False)   # before upload or read capture
   "
   ```

   Or capture structured bus data (built-in ROM ends in `STP`):
   `uv run romulan hardware capture --max-cycles 500 --port /dev/ttyACM0`.
4. **Full program test:**

   ```bash
   cd ~/Downloads/romulan
   uv run romulan program.txt --build --upload
   uv run romulan hardware capture --max-cycles 500 --port /dev/ttyACM0
   ```
5. **RAM writes (optional):** a program that `STA`s to `$0200` (or `$4000`) can be observed
   via `read` / `monitor`. With `OE#` tied high the HM62256 never drives the bus, so
   `LDA` from RAM returns floating garbage. The built-in demo only writes to RAM.
