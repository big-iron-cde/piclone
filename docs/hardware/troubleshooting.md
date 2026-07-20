# Troubleshooting

Symptoms seen during bring-up and their likely causes.

## Hardware / bus symptoms

| Symptom | Likely cause |
|---|---|
| Address bus stuck at one value, never changes | No clock on PHI2 (check GP28 → CPU pin 37) or power fight (external 3.3 V on Pico VSYS while USB connected) |
| 65C02 address bus doesn't change | RDY floating (verify R1) or clock not reaching the CPU |
| Address bus stuck at `$FFFE` forever | CPU in reset (RESB low, check R5 + Pico GP27 should be INPUT after boot) |
| Address goes `$FFFC`, `$FFFD`, then random garbage | D0–D7 / address lines mis-wired, or ROM image wasn't uploaded correctly |
| Reset vector fetches correct bytes but jumps to wrong address | Endianness, reset vector low byte at `$FFFC`, high byte at `$FFFD` |
| Watch shows `$50` at `$5000` or `$40` at `$4000` | Address-line crosstalk onto the data bus, use `$4000` as the virtual print port |
| Watch shows `$05` but never `$14`, or CPU crashes quickly | RAM OE# still on GND, move it to +3.3 V |
| RAM reads return garbage (writes seem to work) | HM62256 at 3.3 V is out of spec, replace with 3.3 V SRAM for production |
| Random behavior when touching the breadboard | Loose wire or missing decoupling, re-seat connections; a 100 nF cap helps |
| Pico USB disconnects when the 65C02 boots | Brownout, the 3.3 V supply can't deliver enough current |

## Hardware API / host symptoms

| Symptom | Likely cause |
|---|---|
| Upload: Device or resource busy | Another program holds the serial port, close serial monitors first |
| `ProtocolError: expected ACK … got 0x7C` | Legacy ASCII **monitor** still enabled (old firmware), run `api.monitor(enable=False)` or start a `read` capture (auto-disables it); reflash if firmware is stale — current firmware emits JSON monitor lines and suppresses them mid-exchange |
| `ProtocolError: device NACK after EOT (0x15)` | Stale or mismatched firmware, rebuild and reflash `piclone.uf2` (`picotool load -f`) |
| `ProtocolError` / timeout (other) | Wrong port, no firmware flashed, or a plain serial monitor is open on the port |
| `TimeoutError` during capture | ROM missing **`STP` (`0xDB`)** at end of program, USB noise, or host `--timeout` too low — rebuild the ROM or raise `--timeout` / `frame_timeout` |
| `unexpected frame during read` (single `cycle` event) | Host/firmware protocol mismatch: the Romulan branch expects batched `cycles` events, but the firmware returned a legacy single-`cycle` frame — rebuild/reflash Piclone from the matching source |
| `ModuleNotFoundError: romulan` | Run host commands from the **Romulan** checkout (`~/Downloads/romulan`) with `uv run`, not from `src/build/` |
