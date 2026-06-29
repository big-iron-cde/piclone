"""Resolve the Romulan package source tree (sibling repo in ~/Downloads/romulan)."""

from __future__ import annotations

import os
from pathlib import Path


def romulan_src_path() -> Path:
    """Return path to Romulan's src/ directory."""
    env = os.environ.get("ROMULAN_PATH")
    if env:
        root = Path(env).expanduser().resolve()
        src = root / "src" if (root / "src" / "romulan").is_dir() else root
        if (src / "romulan").is_dir() or src.name == "romulan":
            return src if (src / "romulan").is_dir() else src.parent / "src"
        raise RuntimeError(f"ROMULAN_PATH does not contain romulan package: {root}")

    # Default: sibling of PICO-ROM under Downloads (~/Downloads/romulan)
    pico_rom = Path(__file__).resolve().parents[1]
    sibling = pico_rom.parent / "romulan" / "src"
    if sibling.is_dir():
        return sibling

    raise RuntimeError(
        "Romulan not found. Clone https://github.com/big-iron-cde/romulan "
        "next to PICO-ROM (e.g. ~/Downloads/romulan) or set ROMULAN_PATH."
    )


def ensure_romulan_on_path() -> Path:
    src = romulan_src_path()
    import sys

    src_str = str(src)
    if src_str not in sys.path:
        sys.path.insert(0, src_str)
    return src
