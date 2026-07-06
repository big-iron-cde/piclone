# Power Rails

A split-supply scheme: USB powers the Pico, a separate external 3.3 V supply powers the
65C02, RAM, and pull-ups.

## Recommended setup (USB + external supply)

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

```{warning}
Do **not** connect external +3.3 V to **Pico pin 39 (VSYS)** or **pin 36 (3V3 OUT)** while
USB is plugged in. USB back-feeds ~4.5 V on VSYS and fights the breadboard rail, which
freezes the CPU with a stuck address bus.
```

## Notes

- Use the breadboard's power rails for the external supply: run +3.3 V along one rail and
  GND along the other. Every chip's VCC/GND wire is then a short hop to the nearest rail.
- A **common ground** between the Pico and breadboard is required even with split supplies.
- **Alternative (USB disconnected):** a single external 3.3 V supply can feed the
  breadboard rail and Pico pin 36 (3V3 OUT) directly. Never use VSYS for a regulated 3.3 V
  input when USB is also connected.
