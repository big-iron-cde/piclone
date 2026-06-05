# pico-u2f — FIDO U2F Security Key for Raspberry Pi Pico (RP2040)

A minimal but functional **FIDO U2F** authenticator for the Raspberry Pi Pico 1 (RP2040). It implements the U2F HID protocol and raw message formats over USB, using:

- **TinyUSB** (via pico-sdk) for the USB HID device stack
- **micro-ecc** for ECDSA P-256 signatures and key generation
- A tiny **SHA-256** implementation for hashing
- A **hardcoded self-signed X.509 attestation certificate**

## What it does

- Responds to **U2F REGISTER** requests (generate a key pair, return attestation)
- Responds to **U2F AUTHENTICATE** requests (sign challenges with stored keys)
- Implements **U2FHID_INIT**, **U2FHID_PING**, **U2FHID_WINK**
- Uses **GP0** as a user-presence button (active LOW, internal pull-up)
- Blinks the on-board LED on **WINK**

## Build

```bash
cd pico-u2f
mkdir build && cd build
cmake ..
make -j$(nproc)
```

The firmware is output as `build/pico-u2f.uf2`. Flash it to the Pico in BOOTSEL mode.

## Wiring

- **GP0** → momentary push-button → **GND** (user presence)
- **LED** on-board (activity / WINK)
- **USB** to host

No other external hardware is required.

## Limitations (prototype)

- Keys are stored **in RAM only** — key handles contain the wrapped private key, but the counter resets on power cycle.
- No **persistent flash storage** — registrations are lost after reboot.
- The **attestation private key** is hardcoded in firmware — not secret.
- This is a **proof-of-concept**, not a production security key.

## Files

| File | Purpose |
|---|---|
| `main.c` | Entry point, TinyUSB task loop |
| `usb_descriptors.c` | USB device / config / HID report descriptors |
| `tusb_config.h` | TinyUSB device-only configuration |
| `u2f_hid.c/h` | U2F HID transport framing |
| `u2f.c/h` | U2F raw messages, crypto, attestation, user presence |
| `sha256.c/h` | SHA-256 implementation |
| `uECC.c/h` | micro-ecc P-256 library |

## Protocol compliance

Implements FIDO U2F HID Protocol v1.2 and FIDO U2F Raw Message Formats v1.2.
