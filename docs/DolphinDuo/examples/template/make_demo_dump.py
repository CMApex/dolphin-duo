#!/usr/bin/env python3
# Copyright 2026 Dolphin Duo Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Writes a FAKE GameCube MEM1 dump laid out for the template companion, so you can try

    dolphin-tool duo preview docs/DolphinDuo/examples/template --mem1 demo-mem1.raw

without running a game. Nothing here comes from a real game.
"""

import struct
import sys

MEM1_BASE = 0x80000000
MEM1_SIZE = 24 * 1024 * 1024


def main() -> int:
    out = sys.argv[1] if len(sys.argv) > 1 else "demo-mem1.raw"
    mem = bytearray(MEM1_SIZE)

    def put(address: int, fmt: str, *values) -> None:
        struct.pack_into(">" + fmt, mem, address - MEM1_BASE, *values)

    put(0x80003000, "I", 12_345)       # gold
    put(0x80003004, "B", 7)            # chapter
    put(0x80003005, "B", 1)            # in_battle
    put(0x80003006, "H", 3)            # turn
    put(0x80003008, "B", 5)            # unit_count
    put(0x8000300C, "f", 6.5)          # cursor_x
    put(0x80003010, "f", 9.0)          # cursor_y
    mem[0x3020:0x3020 + 8] = b"Duo Demo"  # save_name

    # boss_hp: *(*(0x80003040) + 0x8) + 0x4
    put(0x80003040, "I", 0x80005000)
    put(0x80005008, "I", 0x80005100)
    put(0x80005104, "HH", 180, 640)    # boss_hp, boss_max_hp

    units = [
        ("Aria", 28, 30, 0, 12, 3, 4),
        ("Bram", 9, 26, 1, 10, 5, 4),
        ("Cael", 0, 22, 2, 9, 0, 0),    # defeated: hidden by "where": "hp > 0"
        ("Dara", 20, 21, 0x10, 15, 8, 12),
        ("Eli", 6, 18, 3, 8, 13, 2),
    ]
    for slot, (name, hp, max_hp, cls, level, x, y) in enumerate(units):
        base = 0x80004000 + slot * 0x20
        raw = name.encode("ascii")
        mem[base - MEM1_BASE:base - MEM1_BASE + len(raw)] = raw
        put(base + 0x10, "BBBB", hp, max_hp, cls, level)
        put(base + 0x14, "hh", x, y)

    with open(out, "wb") as f:
        f.write(mem)
    print(f"wrote {out} ({MEM1_SIZE // (1024 * 1024)} MiB of fake MEM1)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
