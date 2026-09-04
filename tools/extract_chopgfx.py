#!/usr/bin/env python3
"""Extract the original Apple II Choplifter sprites as reference art.

Reads the sprite pointer tables out of ChoplifterReverse's choplifter.s and
decodes the sprite structs from the CHOPGFX / CHOPGFXHI binaries.

Every sprite shares the same two-byte header (choplifter.s:11469):

    .byte widthInPixels
    .byte heightInPixels
    .byte pixelData...

but the pixel data comes in two formats, because the game has two blitters.

BITMAP (renderSprite, choplifter.s:2287)
    A plain 1bpp bitmap: 8 pixels per byte, MSB = leftmost pixel, rows padded
    to whole bytes. The blitter repacks these 8-bit groups into 7-pixel hi-res
    bytes at draw time and ORs in ZP_PALETTE, so the stored art is monochrome
    and its colour is chosen per draw call by the game logic.

PRESHIFTED (blitAlignedImage, choplifter.s:2028)
    Already in hi-res screen format -- 7 pixels per byte, bit 0 leftmost, bit 7
    is the palette colour -- and stored as SEVEN copies, one pre-shifted to each
    bit position within a hi-res byte. alignImg (:2084) sets the row stride to
    ceil(width / 7) + 1, and the blitter then copies bytes straight to the
    screen with no shifting at all. Used for the wide scenery: mountains, the
    barracks, the base building.

Note that the preshifted trick is the same one worth using for the PCjr port,
just with 2 copies instead of 7, since mode 8 packs 2 pixels per byte.

Outputs to out/:
    catalogue.csv        every sprite with format, dimensions and validation
    png/<name>.png       one image per sprite
    preshifted/<name>_shiftN.png   the other six copies of each scenery sprite
    atlas.png            contact sheet of everything
"""

import csv
import re
import struct
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "lib" / "repos" / "ChoplifterReverse"
OUT = REPO / "tools" / "out"

# Where the loader places each blob (loader.s).
CHOPGFX_ORG = 0xA102
CHOPGFXHI_ORG = 0xBEF0

HGR_PIXELS_PER_BYTE = 7
BITMAP_PIXELS_PER_BYTE = 8
PRESHIFT_COPIES = 7

# Apple HGR is ~0.914:1 pixel aspect on a 4:3 screen; PCjr 160x200x16 is
# ~1.667:1. Scaling widths by this ratio preserves on-screen proportions.
APPLE_TO_JR_X = 0.914 / 1.667

BG = (20, 22, 30)
INK = (232, 240, 255)
# Rough stand-ins for the two HGR palettes, just so shapes read clearly.
PAL0 = (96, 255, 128)
PAL1 = (255, 150, 64)


def load_memory():
    """Build a 64K image with the graphics blobs at their load addresses."""
    mem = bytearray(0x10000)
    end = 0
    for name, org in (("CHOPGFX", CHOPGFX_ORG), ("CHOPGFXHI", CHOPGFXHI_ORG)):
        blob = (SRC / name).read_bytes()
        mem[org:org + len(blob)] = blob
        end = max(end, org + len(blob))
    return mem, end


def parse_sprite_tables():
    """Pull (table, index, address, description) out of the $a000 pointer tables."""
    lines = (SRC / "choplifter.s").read_text(encoding="utf-8",
                                             errors="replace").splitlines()
    start = next(i for i, l in enumerate(lines)
                 if re.match(r"^\.org\s+\$a000\s*$", l.strip(), re.I))
    end = next(i for i, l in enumerate(lines[start:], start)
               if re.match(r"^\.org\s+\$a100\s*$", l.strip(), re.I))

    label_re = re.compile(r"^([A-Za-z_]\w*):")
    word_re = re.compile(r"^\s*\.word\s+\$([0-9a-fA-F]{1,4})\s*(?:;\s*(.*))?$")

    entries = []
    label, index = "unnamed", 0
    for line in lines[start:end]:
        m = label_re.match(line)
        if m:
            label, index = m.group(1), 0
            continue
        m = word_re.match(line)
        if not m:
            continue
        desc = re.sub(r"\s*\$[0-9a-fA-F]{4}\s*$", "", (m.group(2) or "")).strip()
        entries.append({"table": label, "index": index,
                        "addr": int(m.group(1), 16), "desc": desc})
        index += 1
    return entries


def make_name(entry, table_counts):
    base = re.sub(r"(Sprite)?(Animation)?Table$", "", entry["table"])
    base = re.sub(r"Sprite$", "", base) or entry["table"]
    if table_counts[entry["table"]] > 1:
        return f"{base}_{entry['index']:02d}"
    return base


def bitmap_stride(width):
    return (width + BITMAP_PIXELS_PER_BYTE - 1) // BITMAP_PIXELS_PER_BYTE


def preshift_stride(width):
    """alignImg: ceil(width / 7) + 1."""
    bw, rem = divmod(width, HGR_PIXELS_PER_BYTE)
    return bw + (1 if rem else 0) + 1


def classify(mem, addr, gap):
    """Decide which format this sprite uses by seeing which size fits the gap."""
    w, h = mem[addr], mem[addr + 1]
    if w == 0 or h == 0:
        return "empty", 0, 0

    bmp = 2 + bitmap_stride(w) * h
    pre = 2 + preshift_stride(w) * h * PRESHIFT_COPIES
    if bmp == gap:
        return "bitmap", bitmap_stride(w), bmp
    if pre == gap:
        return "preshifted", preshift_stride(w), pre
    # Slot has slack. Prefer whichever fits; bitmap is by far the common case.
    if pre <= gap and bmp < pre:
        return "preshifted", preshift_stride(w), pre
    return "bitmap", bitmap_stride(w), bmp


def decode_bitmap(mem, addr, w, h, stride):
    """1bpp bitmap, 8 pixels per byte, MSB leftmost. Returns rows of RGB."""
    rows = []
    for row in range(h):
        base = addr + 2 + row * stride
        out = []
        for b in range(stride):
            byte = mem[base + b]
            for bit in range(7, -1, -1):
                if len(out) < w:
                    out.append(INK if (byte >> bit) & 1 else BG)
        rows.append(out)
    return rows


def decode_preshifted(mem, addr, w, h, stride, copy=0):
    """HGR format, 7 pixels per byte, bit 0 leftmost, bit 7 = palette colour."""
    base0 = addr + 2 + copy * h * stride
    rows = []
    for row in range(h):
        base = base0 + row * stride
        out = []
        for b in range(stride):
            byte = mem[base + b]
            colour = PAL1 if (byte >> 7) & 1 else PAL0
            for bit in range(HGR_PIXELS_PER_BYTE):
                out.append(colour if (byte >> bit) & 1 else BG)
        rows.append(out)
    return rows


def render(mem, addr, fmt, stride, copy=0):
    """Decode a sprite to RGB rows plus its true rendered pixel size."""
    w, h = mem[addr], mem[addr + 1]
    if fmt == "preshifted":
        rows = decode_preshifted(mem, addr, w, h, stride, copy)
        return rows, stride * HGR_PIXELS_PER_BYTE, h
    return decode_bitmap(mem, addr, w, h, stride), w, h


def write_png(path, rows_rgb, width, height, scale=4):
    """Write an RGB PNG. rows_rgb is height x width of (r, g, b) tuples."""
    raw = bytearray()
    for row in rows_rgb:
        for _ in range(scale):
            raw.append(0)  # filter type 0 (none)
            for (r, g, b) in row:
                raw.extend((r, g, b) * scale)

    def chunk(tag, payload):
        body = tag + payload
        return (struct.pack(">I", len(payload)) + body
                + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width * scale, height * scale,
                                      8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def dither_ratio(rows, width):
    """Fraction of set pixels with no set horizontal neighbour.

    High values mean the artwork uses alternating-pixel patterns, which on a
    real Apple II read as a solid artefact colour rather than as texture. Those
    regions should become flat PCjr colour, not stripes.
    """
    ink = isolated = 0
    for row in rows:
        for x in range(width):
            if row[x] == BG:
                continue
            ink += 1
            left = x > 0 and row[x - 1] != BG
            right = x + 1 < width and row[x + 1] != BG
            if not left and not right:
                isolated += 1
    return (isolated / ink) if ink else 0.0, ink


def main():
    mem, data_end = load_memory()
    entries = parse_sprite_tables()

    table_counts = {}
    for e in entries:
        table_counts[e["table"]] = table_counts.get(e["table"], 0) + 1

    # The gap to the next distinct sprite address gives the true struct size,
    # which is what lets us tell the two formats apart.
    addrs = sorted({e["addr"] for e in entries})
    next_addr = {a: (addrs[i + 1] if i + 1 < len(addrs) else data_end)
                 for i, a in enumerate(addrs)}

    (OUT / "png").mkdir(parents=True, exist_ok=True)
    (OUT / "preshifted").mkdir(parents=True, exist_ok=True)

    records, cells = [], []
    for e in entries:
        addr = e["addr"]
        name = make_name(e, table_counts)
        gap = next_addr[addr] - addr
        fmt, stride, size = classify(mem, addr, gap)
        if fmt == "empty":
            continue

        rows, px_w, px_h = render(mem, addr, fmt, stride)
        write_png(OUT / "png" / f"{name}.png", rows, px_w, px_h)
        cells.append((name, px_w, px_h, rows))

        if fmt == "preshifted":
            for c in range(1, PRESHIFT_COPIES):
                extra, ew, eh = render(mem, addr, fmt, stride, copy=c)
                write_png(OUT / "preshifted" / f"{name}_shift{c}.png",
                          extra, ew, eh)

        dither, ink = dither_ratio(rows, px_w)
        records.append({
            "name": name,
            "format": fmt,
            "table": e["table"],
            "index": e["index"],
            "addr": f"${addr:04x}",
            "width_px": mem[addr],
            "height_px": mem[addr + 1],
            "row_stride": stride,
            "struct_bytes": size,
            "gap_bytes": gap,
            "exact": size == gap,
            "ink_pixels": ink,
            "fill_pct": round(100.0 * ink / (px_w * px_h), 1),
            "dither_pct": round(100.0 * dither, 1),
            "jr_width_px": round(mem[addr] * APPLE_TO_JR_X),
            "jr_height_px": mem[addr + 1],
            "desc": e["desc"],
        })

    with open(OUT / "catalogue.csv", "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(records[0].keys()))
        writer.writeheader()
        writer.writerows(records)

    build_atlas(cells)
    report(records)


def build_atlas(cells):
    """Contact sheet: every sprite on one canvas, shelf-packed."""
    PAD, MAXW = 3, 620
    shelves, cur, cur_w = [], [], 0
    for cell in cells:
        if cur and cur_w + cell[1] + PAD > MAXW:
            shelves.append(cur)
            cur, cur_w = [], 0
        cur.append(cell)
        cur_w += cell[1] + PAD
    if cur:
        shelves.append(cur)

    total_h = sum(max(c[2] for c in s) + PAD for s in shelves) + PAD
    canvas = [[BG] * MAXW for _ in range(total_h)]

    y = PAD
    for shelf in shelves:
        x = PAD
        for _name, w, h, rows in shelf:
            for dy, row in enumerate(rows):
                for dx, px in enumerate(row):
                    if y + dy < total_h and x + dx < MAXW:
                        canvas[y + dy][x + dx] = px
            x += w + PAD
        y += max(c[2] for c in shelf) + PAD

    write_png(OUT / "atlas.png", canvas, MAXW, total_h, scale=2)


def report(records):
    n = len(records)
    exact = sum(1 for r in records if r["exact"])
    pre = [r for r in records if r["format"] == "preshifted"]
    art_bytes = sum(r["struct_bytes"] for r in records)

    print(f"sprites extracted   : {n}")
    print(f"size matches gap    : {exact}/{n}")
    print(f"bitmap / preshifted : {n - len(pre)} / {len(pre)}")
    print(f"total art bytes     : {art_bytes}")

    print("\npreshifted scenery (7 copies each):")
    for r in pre:
        print(f"   {r['name']:<16} {r['width_px']:>3}x{r['height_px']:<3}"
              f" stride {r['row_stride']}  {r['struct_bytes']:>4} bytes"
              f"   {r['desc']}")

    print("\nheavily dithered (alternating pixels = solid HGR colour,"
          " flatten these for PCjr):")
    for r in sorted(records, key=lambda r: -r["dither_pct"])[:10]:
        print(f"   {r['name']:<20} {r['dither_pct']:>5}% isolated"
              f"   {r['width_px']:>3}x{r['height_px']:<3} {r['desc']}")

    print("\nper-entity art budget (Apple px -> PCjr 160x200 px):")
    groups = {}
    for r in records:
        groups.setdefault(r["table"], []).append(r)
    for table, rs in sorted(groups.items(), key=lambda kv: -sum(
            x["struct_bytes"] for x in kv[1]))[:16]:
        tot = sum(x["struct_bytes"] for x in rs)
        big = max(rs, key=lambda x: x["width_px"] * x["height_px"])
        print(f"   {table:<28} {len(rs):>2} frames {tot:>5} B"
              f"   largest {big['width_px']}x{big['height_px']}"
              f" -> {big['jr_width_px']}x{big['jr_height_px']}")


if __name__ == "__main__":
    main()
