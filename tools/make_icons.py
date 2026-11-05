#!/usr/bin/env python3
"""Draw the launcher tile (99x99, transparent, like the stock tiles): a music note with light rays.

    tools/make_icons.py <out dir>     writes tile.png and tile_disabled.png
"""
import math, os, struct, sys, zlib

S = 99
SS = 4
TEAL, BLUE, GREY = (42, 170, 185), (74, 92, 208), (92, 96, 102)


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


def inside(x, y):
    # note heads
    for cx, cy in ((35, 72), (66, 64)):
        if ((x - cx) / 10.5) ** 2 + ((y - cy) / 8.5) ** 2 <= 1:
            return True
    # stems and beam
    if 41 <= x <= 45.5 and 32 <= y <= 72:
        return True
    if 71.5 <= x <= 76 and 24 <= y <= 64:
        return True
    top = 32 - (x - 41) * 8 / 35
    if 41 <= x <= 76 and top <= y <= top + 10:
        return True
    # rays
    for ax, ay, bx, by in ((22, 26, 30, 34), (14, 44, 25, 46), (34, 11, 37, 21)):
        if seg_dist(x, y, ax, ay, bx, by) <= 2.2:
            return True
    return False


def draw(top, bottom):
    rows = []
    for y in range(S):
        t = y / (S - 1)
        c = [round(top[i] + (bottom[i] - top[i]) * t) for i in range(3)]
        row = bytearray([0])
        for x in range(S):
            hits = sum(inside(x + (i + .5) / SS, y + (j + .5) / SS) for i in range(SS) for j in range(SS))
            row += bytes(c + [round(255 * hits / SS ** 2)])
        rows.append(bytes(row))
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", S, S, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))


out = sys.argv[1]
os.makedirs(out, exist_ok=True)
open(os.path.join(out, "tile.png"), "wb").write(draw(TEAL, BLUE))
open(os.path.join(out, "tile_disabled.png"), "wb").write(draw(GREY, GREY))
