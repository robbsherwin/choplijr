#!/usr/bin/env python3
"""Compose the tank turret and the five cannon angles into one RLE sprite each.

FIGHT_PERFORMANCE.md item 5.  m10.c drew a tank as three blits (treads,
turret, cannon), each with its own dirty rect and restore.  The turret never
moves relative to the treads and the cannon pivots at a fixed point, so turret
plus cannon is one sprite per angle.

Layout: m10.c drew both parts with blit_aligned(), which rounds each x to an
even column separately.  The turret's offset (+4) is even and every cannon
offset is odd, so on an even tank x the cannon landed at x + (off & ~1) and on
an odd x at x + off + 1: a 2 px wobble against the turret as the camera
scrolled.  The composites keep the even-x layout exactly (cannon at off & ~1,
turret at +4), so even-x frames are pixel-identical to before and odd-x frames
lose the wobble.

Reads src/sprdata_combat.c (tank_turret_e, tank_cannon_0N_e) and
cannon_angle_x from src/m10.c; writes src/sprdata_tank.c.  Before writing it
renders the old three-step drawing and the new composite with blit_rle_m8's
semantics and checks they agree, as described above.

    python3 tools/compose_tank.py
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMBAT = os.path.join(ROOT, "src", "sprdata_combat.c")
M10 = os.path.join(ROOT, "src", "m10.c")
OUT = os.path.join(ROOT, "src", "sprdata_tank.c")

TURRET_DX, TURRET_DY = 4, -3       # m10.c draw_ents: sx + 4, sy - 3
CANNON_DY = -7                     # sy - 7


def c_array(src, name):
    m = re.search(r"unsigned char %s\[\] = \{(.*?)\};" % re.escape(name), src, re.S)
    if not m:
        sys.exit("missing array %s" % name)
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    return [int(t, 0) for t in re.findall(r"0x[0-9A-Fa-f]+|\d+", body)]


def decode(rle):
    """RLE -> rows of byte cells; each cell is (hi, lo), None = transparent.

    Mirrors blit_rle_m8: runs of >= 2 bytes are REP MOVSB (both nibbles
    written, even a zero one); a 1-byte run writes only its non-zero nibbles.
    """
    w, h = rle[0], rle[1]
    rows, p = [], 2
    for _ in range(h):
        row = [(None, None)] * (w // 2)
        col = 0
        while True:
            skip, run = rle[p], rle[p + 1]
            p += 2
            if skip == 0 and run == 0:
                break
            col += skip
            for k in range(run):
                b = rle[p + k]
                hi, lo = b >> 4, b & 15
                if run >= 2:
                    row[col + k] = (hi, lo)
                else:
                    row[col + k] = (hi if hi else None, lo if lo else None)
            p += run
            col += run
        rows.append(row)
    return w, h, rows


def encode(w, rows):
    out = [w, len(rows)]
    for row in rows:
        col, n = 0, len(row)
        pending_skip = 0
        while col < n:
            hi, lo = row[col]
            if hi is None and lo is None:
                pending_skip += 1
                col += 1
                continue
            if hi is not None and lo is not None:
                end = col
                while end < n and row[end][0] is not None and row[end][1] is not None:
                    end += 1
                run = row[col:end]
                if len(run) == 1 and (hi == 0 or lo == 0):
                    sys.exit("opaque black in a 1-byte run cannot be encoded")
                out += [pending_skip, len(run)] + [(a << 4) | b for a, b in run]
                pending_skip = 0
                col = end
                continue
            v = hi if hi is not None else lo
            if v == 0:
                sys.exit("opaque black in a mixed byte cannot be encoded")
            out += [pending_skip, 1, ((hi or 0) << 4) | (lo or 0)]
            pending_skip = 0
            col += 1
        out += [0, 0]
    return out


def stamp(canvas, x_px, y, sprite):
    """Draw a decoded sprite at a pixel x (must be even, like blit_aligned)."""
    _, _, rows = sprite
    assert x_px % 2 == 0
    for r, row in enumerate(rows):
        for c, (hi, lo) in enumerate(row):
            px = x_px + 2 * c
            if hi is not None:
                canvas[(px, y + r)] = hi
            if lo is not None:
                canvas[(px + 1, y + r)] = lo


def main():
    combat = open(COMBAT).read()
    m10 = open(M10).read()
    m = re.search(r"cannon_angle_x\[5\] = \{([^}]*)\}", m10)
    offs = [int(t, 0) for t in re.findall(r"0x[0-9A-Fa-f]+", m.group(1))]
    offs = [o - 256 if o > 127 else o for o in offs]

    turret = decode(c_array(combat, "tank_turret_e"))
    cannons = [decode(c_array(combat, "tank_cannon_%02d_e" % k)) for k in range(5)]

    arrays, oxs = [], []
    for a in range(5):
        cdx = offs[a] & ~1                  # even-x layout, see docstring
        ox = min(TURRET_DX, cdx)
        right = max(TURRET_DX + turret[0], cdx + cannons[a][0])
        width = right - ox
        assert width % 2 == 0 and ox % 2 == 0
        canvas = {}
        stamp(canvas, TURRET_DX - ox, TURRET_DY - CANNON_DY, turret)
        stamp(canvas, cdx - ox, 0, cannons[a])
        height = max(TURRET_DY - CANNON_DY + turret[1], cannons[a][1])
        rows = []
        for y in range(height):
            row = []
            for c in range(width // 2):
                row.append((canvas.get((2 * c, y)), canvas.get((2 * c + 1, y))))
            rows.append(row)
        rle = encode(width, rows)

        # Check: old drawing at an even and an odd tank x vs the composite.
        comp = decode(rle)
        for sx in (40, 41):
            old = {}
            stamp(old, (sx + TURRET_DX) & ~1, TURRET_DY, turret)
            stamp(old, (sx + offs[a]) & ~1, CANNON_DY, cannons[a])
            new = {}
            stamp(new, (sx & ~1) + ox, CANNON_DY, comp)
            ref = old
            if sx & 1:                      # odd x: should equal even x - 1
                ref = {}
                e = sx - 1
                stamp(ref, (e + TURRET_DX) & ~1, TURRET_DY, turret)
                stamp(ref, (e + offs[a]) & ~1, CANNON_DY, cannons[a])
            if new != ref:
                sys.exit("composite %d does not match at sx=%d" % (a, sx))
        arrays.append(rle)
        oxs.append(ox)

    lines = [
        "/* sprdata_tank.c -- tank turret + cannon composites (M10).",
        " *",
        " * Generated by tools/compose_tank.py from tank_turret_e and",
        " * tank_cannon_0N_e in sprdata_combat.c; do not edit by hand.  One RLE",
        " * sprite per cannon angle, top row at the tank's sy - 7, left edge at",
        " * sx + tank_tc_ox[angle].  Indexed like tank_cannon_e (angle 0..4). */",
        "",
    ]
    for a, rle in enumerate(arrays):
        lines.append("unsigned char tank_tc_%02d_e[] = {" % a)
        lines.append("    %d, %d,  /* width_px, height_px; rest is RLE rows */" % (rle[0], rle[1]))
        body = rle[2:]
        for i in range(0, len(body), 12):
            lines.append("    " + ", ".join("0x%02X" % b for b in body[i:i + 12]) + ",")
        lines.append("};")
        lines.append("")
    lines.append("unsigned char *tank_tc_e[5] = {")
    lines.append(",\n".join("    tank_tc_%02d_e" % a for a in range(5)))
    lines.append("};")
    lines.append("")
    lines.append("signed char tank_tc_ox[5] = { %s };" % ", ".join(str(o) for o in oxs))
    lines.append("")
    open(OUT, "w").write("\n".join(lines))
    total = sum(len(r) for r in arrays) + 5 * 2 + 5
    print("wrote %s: 5 composites, %d bytes of DGROUP; checks passed" % (OUT, total))


if __name__ == "__main__":
    main()
