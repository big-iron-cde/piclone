# Host Tools

Host tools live in [`rom-builder/`](https://github.com/big-iron-cde/piclone/tree/main/rom-builder)
at the repository root. Install the one dependency once:

```bash
pip install --user pyserial
```

## Workflow

Build and upload a ROM:

```bash
cd rom-builder
python3 build-rom.py                     # → bin/rom.bin  (preferred)
python3 upload-rom.py                     # upload via Hardware API
python3 upload-rom.py --read-stp          # upload, disable monitor, reset, capture to STP
```

`build-rom.py` is the maintained ROM builder (CPU-address helpers, `STP` terminator for
automated capture). `main.py` is a minimal legacy example that also works.

```{note}
`rom_image[]` is lost on Pico power-cycle — re-run `upload-rom.py` after each reboot.
```

## Demo program (`build-rom.py`)

```
$8000: CLC
       LDA #$05
       STA $4000       ; visible via read / monitor
       ADC #$0F        ; A = 20
       STA $4000
       STP             ; stops read_until_stp capture (opcode $DB)
$FFFC: $8000           ; reset vector
```

## Python API reference

The client library is documented from source below. The `build-rom.py` and
`upload-rom.py` entry points use hyphenated filenames (not importable Python modules), so
they are summarized here with links to source.

### `hardware_api` — client library

```{eval-rst}
.. automodule:: hardware_api
   :members:
   :undoc-members:
   :show-inheritance:
```

### Scripts

- [`build-rom.py`](https://github.com/big-iron-cde/piclone/blob/main/rom-builder/build-rom.py)
  — assembles a 32 KB ROM image into `bin/rom.bin`. Key helpers: `cpu_to_offset()` maps a
  CPU address (`$8000–$FFFF`) to a file offset, and `write_bytes()` writes one or more
  bytes at a CPU address and returns the next address for chaining.
- [`upload-rom.py`](https://github.com/big-iron-cde/piclone/blob/main/rom-builder/upload-rom.py)
  — CLI wrapper: `python3 upload-rom.py [PORT] [BIN]`, with `--read-stp` to upload, reset,
  and capture bus cycles until `STP`.

