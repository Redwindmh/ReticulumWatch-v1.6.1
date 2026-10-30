#!/usr/bin/env python3
"""Static sanity-check for Reticulum Watch partition tables."""
from pathlib import Path
import csv

ROOT = Path(__file__).resolve().parents[1]
TARGETS = {
    ROOT / "partitions" / "reticulum_watch_8mb.csv": 8 * 1024 * 1024,
    ROOT / "partitions" / "reticulum_watch_16mb.csv": 16 * 1024 * 1024,
}

def number(value: str) -> int:
    return int(value.strip(), 0)

for path, flash_size in TARGETS.items():
    end = 0
    previous_end = 0
    with path.open() as f:
        rows = csv.reader(line for line in f if not line.lstrip().startswith("#"))
        for row in rows:
            name, _type, _subtype, offset, size, *_ = [c.strip() for c in row]
            start = number(offset)
            length = number(size)
            assert start >= previous_end, f"{path.name}: {name} overlaps previous partition"
            end = start + length
            assert end <= flash_size, f"{path.name}: {name} exceeds flash"
            previous_end = end
    assert end == flash_size, f"{path.name}: layout ends at {end:#x}, expected {flash_size:#x}"
    print(f"OK {path.name}: ends exactly at {flash_size:#x}")
