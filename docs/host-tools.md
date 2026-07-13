# Host Tools

ROM building and host-side control of the Pico live in the separate
[**Romulan**](https://github.com/big-iron-cde/romulan) repository, checked out as a sibling
of this repo (for example `~/Downloads/romulan`). Romulan assembles ROM images from
annotated hex and speaks the [v1 Hardware API](hardware-api.md) over the framed serial link.

Set it up once:

```bash
cd ~/Downloads/romulan
uv sync
```

## Workflow

Build and upload a ROM, then capture the bus:

```bash
uv run romulan program.txt --build --upload                 # assemble + framed Hardware API upload
uv run romulan hardware upload bin/rom.bin --port /dev/ttyACM0
uv run romulan hardware capture --max-cycles 500 --port /dev/ttyACM0
```

```{note}
`rom_image[]` is lost on Pico power-cycle, re-upload after each reboot.
```

## Python client

```python
from romulan.hardware_api import HardwareAPI

with HardwareAPI("/dev/ttyACM0") as api:
    print(api.status())

    api.reset(assert_reset=True)                       # hold CPU in reset
    api.upload_rom(open("bin/rom.bin", "rb").read())   # chunked, base64-encoded upload
    # commit leaves RESET asserted — capture arms then releases reset

    capture = api.read_until_stp(max_cycles=500)       # disables monitor; polls at ~1 kHz PHI2
    print(capture.reason, len(capture.cycles))
```

`read_until_stp()` captures one JSON event per PHI2 rising edge until the CPU fetches `STP`
(`0xDB`) or `max_cycles` is reached. The firmware default clock is **1 kHz**.

## Demo program

Romulan's sample program ends in `STP` so automated capture stops deterministically:

```
$8000: CLC
       LDA #$05
       STA $4000       ; visible via read / monitor
       ADC #$0F        ; A = 20
       STA $4000
       STP             ; stops read_until_stp capture (opcode $DB)
$FFFC: $8000           ; reset vector
```
