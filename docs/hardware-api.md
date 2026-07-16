# Hardware API

The host talks to the Pico over USB-CDC at **115200 baud** using a framed serial protocol.
**All payloads are JSON with `"v":1`:** including the chunked, base64-encoded ROM upload.
An optional `"id"` is echoed back in the matching response. This is designed for scripted
bring-up and CI.

## Framing

Every transaction follows the same byte sequence. The **receiver** always sends ACK (or
NACK on error) after EOT, whether the host or the Pico is sending:

```
Sender                         Receiver
  ENQ (0x05)          ────────►
  STX (0x02)          ────────►
                      ◄────────  ACK (0x06)     ← receiver ready for payload
  payload bytes       ────────►
  EOT (0x04)          ────────►
                      ◄────────  ACK (0x06) or NACK (0x15)   ← accepted/rejected
```

| Byte | Value | Meaning |
|---|---|---|
| ENQ | `0x05` | Start frame |
| STX | `0x02` | Start payload |
| ACK | `0x06` | Ready / accepted |
| EOT | `0x04` | End payload |
| NACK | `0x15` | Rejected (bad frame, unknown command, or payload too large) |

```{important}
Do not open a plain serial monitor on the port while using the Hardware API; unstructured
output corrupts framing. Only one process may hold the port at a time. The **`monitor`**
command also prints unstructured ASCII, and that state **persists on the Pico** until you
disable it or start a **`read`** capture (which auto-disables it).
```

## Commands

All commands are JSON sent in a framed payload (host → Pico), and every request includes
`"v":1`. The Pico responds with a framed JSON payload (Pico → host).

| Command | Request | Response |
|---|---|---|
| **reset** | `{"v":1,"cmd":"reset","assert":true}` or `"assert":false` | `{"v":1,"ok":true,"cmd":"reset","asserted":true}` |
| **upload_rom** | `begin` → `chunk` (base64) × N → `commit` | per-phase acks; `commit` returns `reset_vector` |
| **read** | `{"v":1,"cmd":"read","until":"stp","max_cycles":10000,"batch_size":32,"phi2_hz":1000,"release_reset":true}` | ack, then poll `read_event` for batched cycle/`done` events |
| **read_event** | `{"v":1,"cmd":"read_event","batch_size":32}` | `cycles` batch / `done` / `none` while capture is armed |
| **clock** | `{"v":1,"cmd":"clock","hz":1000}` | `{"v":1,"ok":true,"cmd":"clock","hz":1000}` |
| **request_addr** | `{"v":1,"cmd":"request_addr"}` | `{"v":1,"ok":true,"cmd":"request_addr","addr":"4000","phi2_hz":1000}` |
| **peek** | `{"v":1,"cmd":"peek","offset":28672,"count":16}` | bytes from `rom_image[offset]` as a hex string |
| **monitor** | `{"v":1,"cmd":"monitor","enable":true}` | enables/disables the ASCII bus table (off by default) |
| **status** | `{"v":1,"cmd":"status"}` | full hardware snapshot (clock, reset, ROM, monitor, last bus sample, raw pins) |
| **drive** | `{"v":1,"cmd":"drive","value":"EA"}` or `{"v":1,"cmd":"drive","enable":false}` | force D0–D7 to a byte, or release the bus |

### reset

Assert or release the 6502 RESET line (GP27). Use `"assert":true` to hold the CPU in
reset, `"assert":false` to let it run.

### upload_rom

A JSON-only chunked transfer (up to 32768 raw bytes per chunk, base64-encoded; a full ROM fits in one chunk):

1. `{"v":1,"cmd":"upload_rom","action":"begin","size":32768}`
2. `{"v":1,"cmd":"upload_rom","action":"chunk","offset":0,"data":"<base64>"}`, repeat until
   all 32 KB have been sent.
3. `{"v":1,"cmd":"upload_rom","action":"commit"}` → `{"v":1,"ok":true,"reset_vector":"8000",...}`

RESET is asserted for the duration of the upload so the CPU cannot fetch half-written ROM
data. **Commit keeps RESET asserted** so the host can arm capture from `$8000` before release.

### read

Captures bus activity as JSON. Streams batched events until the CPU **fetches STP**
(`0xDB` on a read cycle) or `max_cycles` is reached. Each batch contains up to `batch_size`
cycles:

```json
{"v":1,"type":"event","event":"cycles","cycles":[{"seq":1,"addr":"8000","data":"18","rw":0}]}
```

Final event:

```json
{"v":1,"type":"event","event":"done","ok":true,"reason":"stp","cycles":14,"addr":"800D"}
```

To use this in automated tests, end your ROM with a **`STP` (`0xDB`)** instruction (not
`BRK`, that opcode is `0x00`). Starting a `read` automatically disables the ASCII monitor
on the Pico and releases any active `drive` diagnostic.

By default `read` also **releases RESET** immediately after arming capture (`release_reset`
defaults to `true`). This ensures the first captured cycle is the reset-vector fetch after
the `upload_rom commit` workflow leaves the CPU held in reset. Set `release_reset:false` if
you want to arm capture while keeping the CPU halted.

`batch_size` defaults to `1` if omitted, and is clamped to a firmware maximum (64). Hosts
that do not send `batch_size` receive the legacy single-`cycle` event; hosts that send it
receive the batched `cycles` array. The Romulan client defaults to `32`.

At the default **1 kHz** clock (~1 ms per PHI2 cycle), cycle events are polled quickly over
USB. The Romulan host client uses a serial/frame timeout (default **30 s**, configurable)
while draining `read_event` responses. A full capture from reset through STP is typically
under a second for short demo programs.

### request_addr

Returns the last address sampled on the bus (updated every PHI2 rising edge).

### peek

Read back bytes from the currently loaded `rom_image[]`. Useful for verifying that an
upload landed at the expected offsets before releasing RESET.

```json
{"v":1,"cmd":"peek","offset":28672,"count":16}
```

Response:

```json
{"v":1,"ok":true,"cmd":"peek","offset":28672,"count":16,"data":"A9...."}
```

`count` is capped at 64 bytes and clipped to the 32 KB ROM bounds.

### status

Returns a full hardware snapshot: clock frequency, reset state, ROM active flag,
whether the ASCII monitor is enabled, the last bus sample (`last_addr`, `last_data`,
`last_rw`), the active `drive` diagnostic state, and the raw pin levels (`resb`, `rwb`,
`a15`, `phi2`) in a single JSON response.

```json
{
  "v":1,"ok":true,"cmd":"status",
  "phi2_hz":1000.0,"rom_active":true,"reset_asserted":false,
  "last_addr":"8000","last_data":"18","last_rw":0,
  "read_active":false,"monitor_enabled":false,"upload_active":false,
  "drive_enabled":false,
  "resb":1,"rwb":1,"a15":1,"phi2":1
}
```

### monitor

Toggles the human-readable ASCII bus table (disabled by default). **Mutually exclusive
with scripted capture:** table rows contain `|` (`0x7C`) and other bytes that collide with
framed protocol traffic. Disable it before upload/read:

```python
api.monitor(enable=False)
```

Example output:

```
| 01 |  18  |  8000   |  0 | 1000.0 |
```

### clock

Sets the PHI2 clock frequency independently of starting a capture. Accepts `"hz"` as a
float in the range **0.1–1000.0**. The response echoes the actual frequency after rounding
to the nearest microsecond half-period.

```json
{"v":1,"cmd":"clock","hz":100}
```

Response:

```json
{"v":1,"ok":true,"cmd":"clock","hz":100}
```

### drive

Diagnostic command that forces the Pico to drive a byte onto D0–D7, or releases the bus
and returns to normal ROM emulation. The CPU should be held in reset before forcing the
data bus, otherwise the Pico and CPU contend.

Force the bus:

```json
{"v":1,"cmd":"drive","value":"EA"}
```

Release the bus:

```json
{"v":1,"cmd":"drive","enable":false}
```

Response:

```json
{"v":1,"ok":true,"cmd":"drive","enabled":true,"value":"EA"}
```

`drive` is automatically disabled by `upload_rom begin`, `read`, and `upload_rom abort`.

Use **`read`** (JSON cycle stream) for automated tests; reserve **`monitor`** for manual
breadboard observation.
