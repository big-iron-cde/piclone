# Firmware

The firmware lives in [`src/`](https://github.com/big-iron-cde/piclone/tree/main/src)
and is built with the Raspberry Pi Pico SDK. It targets the **Pico 2** (`PICO_BOARD=pico2`)
and bundles a small JSON parser (cJSON) for the v1 Hardware API.

## What it does (auto-run)

1. **Auto-start on USB connect:** clock, ROM emulation, and RESET release happen
   automatically. No commands are needed for the CPU to run.
2. **Generate PHI2:** a GP28 square wave at **1 kHz** by default (~1 ms per cycle) for
   faster CI and capture. The `read` command accepts optional `phi2_hz` (0.1–1000 Hz).
3. **Reset control:** GP27 starts as OUTPUT LOW, then releases to INPUT (the pull-up runs
   the CPU) once USB is connected.
4. **ROM emulation:** a 32 KB `rom_image[]` in SRAM, mapped to CPU `$8000–$FFFF`. When
   A15 = 1, drive GP15–GP22 from `rom_image[addr & 0x7FFF]`; when A15 = 0, the data bus is
   Hi-Z. Implemented with **GPIO polling** (reliable at the supported **0.1–1000 Hz** PHI2
   range; PIO + DMA is a future upgrade for higher clocks).
5. **Built-in demo program:** matches Romulan `demo.txt` — writes `$05` then `$14` then
   `$08` to RAM-side stores and ends in **`STP` (`$DB`)** so capture stops cleanly.
6. **Hardware API:** structured host control over USB-CDC serial (see
   [Hardware API](hardware-api.md)).

## Build

```bash
export PICO_SDK_PATH=~/vsarm/pico-sdk   # your SDK checkout path
cd src
mkdir -p build && cd build
cmake ..
make
```

The build produces `build/piclone.uf2`. The project bundles `pico_sdk_import.cmake`, so no
separate SDK-import step is needed. See [Host Tools](host-tools.md) for flashing and
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
