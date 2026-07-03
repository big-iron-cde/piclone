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
uv run romulan program.txt --build --upload                 # assemble + upload via Hardware API
uv run romulan hardware upload bin/rom.bin --port /dev/ttyACM0
uv run romulan hardware capture --max-cycles 500 --port /dev/ttyACM0
```

```{note}
`rom_image[]` is lost on Pico power-cycle — re-upload after each reboot.
```

## HTTP REST API

Romulan also exposes an HTTP REST server, so any client (curl, browser, CI) can drive the
device without speaking the framed serial protocol directly:

```bash
PICO_PORT=/dev/ttyACM0 uvicorn api_server:app --host 127.0.0.1 --port 8080
curl http://127.0.0.1:8080/v1/status
curl -X POST http://127.0.0.1:8080/v1/reset -H 'Content-Type: application/json' -d '{"assert":true}'
curl -X POST http://127.0.0.1:8080/v1/rom --data-binary @bin/rom.bin
```

OpenAPI docs are served at `http://127.0.0.1:8080/docs`.

## Python client

```python
from romulan.hardware_api import HardwareAPI

with HardwareAPI("/dev/ttyACM0") as api:
    print(api.status())

    api.reset(assert_reset=True)                       # hold CPU in reset
    api.upload_rom(open("bin/rom.bin", "rb").read())   # chunked, base64-encoded upload
    api.reset(assert_reset=False)                      # release → run

    capture = api.read_until_stp(max_cycles=500)       # disables monitor; ~12 s/frame
    print(capture.reason, len(capture.cycles))
```

`read_until_stp()` captures one JSON event per PHI2 rising edge until the CPU fetches `STP`
(`0xDB`) or `max_cycles` is reached. At the default 0.2 Hz clock, frames arrive about every
5 s (the host waits up to 12 s per frame).

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
