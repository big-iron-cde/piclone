# Memory Map

The CPU sees a single 64 KB address space split by **A15**, which doubles as the
chip-select. No external address decoder is required.

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

- When **A15 = 0** → RAM is selected (`$0000–$7FFF`). The Pico keeps its data pins Hi-Z.
- When **A15 = 1** → the Pico (acting as ROM) drives the bus (`$8000–$FFFF`), serving
  `rom_image[addr & 0x7FFF]`.

A15 itself does all the decoding: it drives the RAM's active-low `CE#` directly, so
`A15 = 0` selects the RAM and `A15 = 1` deselects it, a perfect match with no inverter.

## Virtual print port

The demo program stores results at **`$4000`**. Use the Hardware API `read` or `monitor`
commands to observe CPU stores over USB. Avoid addresses whose high byte matches common
data values on a noisy breadboard (for example, `$5000` often reads back as `$50` due to
address-line crosstalk onto D0–D7).
