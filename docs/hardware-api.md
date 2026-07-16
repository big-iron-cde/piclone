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
| **read** | `{"v":1,"cmd":"read","until":"stp","max_cycles":10000,"phi2_hz":1000}` | ack, then poll `read_event` for cycle/`done` events |
| **read_event** | `{"v":1,"cmd":"read_event"}` | `cycle` / `done` / `none` while capture is armed |
| **request_addr** | `{"v":1,"cmd":"request_addr"}` | `{"v":1,"ok":true,"cmd":"request_addr","addr":"4000","phi2_hz":1000}` |
| **peek** | `{"v":1,"cmd":"peek","addr":"4000"}` | `{"v":1,"ok":true,"cmd":"peek","addr":"4000","data":"14"}` |
| **monitor** | `{"v":1,"cmd":"monitor","enable":true}` | enables/disables the ASCII bus table (off by default) |
| **status** | `{"v":1,"cmd":"status"}` | full hardware snapshot (clock, reset, ROM, monitor) |

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

Captures bus activity as JSON. Streams one event per PHI2 rising edge until the CPU
**fetches STP** (`0xDB` on a read cycle) or `max_cycles` is reached. Each cycle event:

```json
{"v":1,"type":"event","event":"cycle","seq":1,"addr":"8000","data":"18","rw":0}
```

`rw` is **0 = read**, **1 = write**. On this build it is **inferred from A15** (ROM
`$8000–$FFFF` → read, RAM `$0000–$7FFF` → write) because Pico 2 GP23 cannot sense CPU
RWB on the header. STP stop still uses a ROM read of `$DB`.

Final event:

```json
{"v":1,"type":"event","event":"done","ok":true,"reason":"stp","cycles":14,"addr":"800D"}
```

To use this in automated tests, end your ROM with a **`STP` (`0xDB`)** instruction (not
`BRK`, that opcode is `0x00`). Starting a `read` automatically disables the ASCII monitor
on the Pico.

At the default **1 kHz** clock (~1 ms per PHI2 cycle), cycle events are polled quickly over
USB. The Romulan host client uses a serial/frame timeout (default **30 s**, configurable)
while draining `read_event` responses. A full capture from reset through STP is typically
under a second for short demo programs.

### request_addr

Returns the last address sampled on the bus (updated every PHI2 rising edge).

### peek

Live bus/RAM peek: asserts RESET, patches `LDA $addr` / `STP` at `$8000`, releases
RESET, samples the data byte on the bus cycle whose address matches `addr`, then
re-asserts RESET and restores the previous ROM bytes. `addr` is a hex string
(`0000`–`FFFF`). Errors: `timeout` (STP not seen), `no_cycle` (no matching address),
`busy` (capture/upload in progress).

This is distinct from reading the Pico's `rom_image[]` buffer — it observes a real
CPU read cycle (e.g. RAM at `$4000` after an STA).

### status

Returns a full hardware snapshot: clock frequency, reset state, ROM active flag, and
whether the ASCII monitor is enabled, in a single JSON response.

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

Use **`read`** (JSON cycle stream) for automated tests; reserve **`monitor`** for manual
breadboard observation.
