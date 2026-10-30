#!/usr/bin/env python3
"""Regenerates the hand-assembled known-answer ROMs in mayhem/fuzz_<core>/testsuite/.

They are both the fuzz seeds and the inputs of the functional oracle (mayhem/oracle/oracle.c).
Every program computes 10+9+...+1 = 55 with a counting loop, stores it to RAM, then spins.
"""
import os
import struct
import sys

root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def write(core, ext, data):
    path = os.path.join(root, "fuzz_" + core, "testsuite", "oracle." + ext)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def arm(words):
    return b"".join(struct.pack("<I", w) for w in words)


# Game Boy: 32 KiB, entry at 0x100 -> jp 0x150, result in WRAM 0xC000
gb = bytearray(32 * 1024)
gb[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])
gb[0x134:0x13A] = b"ORACLE"
code = bytes([
    0x3E, 0x00,        # ld a,0
    0x06, 0x0A,        # ld b,10
    0x80,              # loop: add a,b
    0x05,              # dec b
    0x20, 0xFC,        # jr nz,loop
    0xEA, 0x00, 0xC0,  # ld (0xC000),a
    0x18, 0xFE,        # jr $
])
gb[0x150:0x150 + len(code)] = code
write("gb", "gb", gb)

# Game Boy Advance: result in EWRAM 0x02000000
loop = [
    0xE3A00000,  # mov r0,#0
    0xE3A0100A,  # mov r1,#10
    0xE0800001,  # loop: add r0,r0,r1
    0xE2511001,  # subs r1,r1,#1
    0x1AFFFFFC,  # bne loop
    0xE3A02402,  # mov r2,#0x02000000
]
body_gba = loop + [
    0xE5820000,  # str r0,[r2]
    0xEAFFFFFE,  # b .
]
gba = bytearray(256)
gba[0:4] = arm([0xEA00002E])  # b 0xC0
gba[0xC0:0xC0 + len(body_gba) * 4] = arm(body_gba)
write("gba", "gba", gba)

# Nintendo DS: the ARM9 stores the sum to main RAM 0x02001000; the ARM7 spins.
body_nds = loop + [
    0xE2822A01,  # add r2,r2,#0x1000
    0xE5820000,  # str r0,[r2]
    0xEAFFFFFE,  # b .
]
nds = bytearray(0x600)
nds[0:6] = b"ORACLE"
nds[0x20:0x40] = struct.pack("<8I", 0x200, 0x02000000, 0x02000000, len(body_nds) * 4,
                             0x400, 0x02300000, 0x02300000, 4)
nds[0x200:0x200 + len(body_nds) * 4] = arm(body_nds)
nds[0x400:0x404] = arm([0xEAFFFFFE])
write("nds", "nds", nds)
