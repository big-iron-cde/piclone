# Firmware

The firmware lives in [`piclone/`](https://github.com/big-iron-cde/piclone/tree/main/piclone)
and is built with the Raspberry Pi Pico SDK.

## What it does (auto-run)

1. **Auto-start on USB connect** — clock, ROM emulation, and RESET release happen
   automatically. No commands are needed for the CPU to run.
2. **Generate PHI2** — a GP28 square wave at **0.2 Hz** (5 s per cycle) for step-by-step
   learning. Configurable in firmware for faster speeds.
3. **Reset control** — GP27 starts as OUTPUT LOW, then releases to INPUT (the pull-up runs
   the CPU) once USB is connected.
4. **ROM emulation** — a 32 KB `rom_image[]` in SRAM, mapped to CPU `$8000–$FFFF`. When
   A15 = 1, drive GP15–GP22 from `rom_image[addr & 0x7FFF]`; when A15 = 0, the data bus is
   Hi-Z. Implemented with **GPIO polling** (reliable at 100 kHz–1 MHz on this build;
   PIO + DMA is a future upgrade).
5. **Built-in demo program** — a tiny loop at `$8000` that writes `$05` then `$14`
   (20 decimal) to `$4000`.
6. **Hardware API** — structured host control over USB-CDC serial (see
   [Hardware API](hardware-api.md)).

## Build

```bash
cd piclone
mkdir -p build && cd build
cmake ..
make
```

The build produces `piclone.uf2`. See [Host Tools](host-tools.md) for flashing and
uploading ROMs.

## C API reference (Doxygen)

The following is generated from the firmware sources by Doxygen and rendered via Breathe.

### Hardware API (`hardware_api.h`)

```{doxygenfile} hardware_api.h
:project: piclone
```

### Serial framing (`protocol.h`)

```{doxygenfile} protocol.h
:project: piclone
```
