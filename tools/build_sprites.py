#!/usr/bin/env python3
"""Convert original Choplifter art to packed and RLE PCjr mode-8 data.

M2 (DESIGN.md sections 6, 12, 13): packed even/odd copies for blit_mask_m8,
emitted as src/sprdata.c.

M3: the same flying-chopper frames as RLE rows for blit_rle_m8, emitted as
src/sprdata_rle.c. Packed and RLE are separate objects so m2.exe does not
carry the RLE set. Side-view and head-on/rotating chopper frames also get
horizontally flipped even/odd RLE (`_fe` / `_fo`) so M10 can blit_rle_m8 a
nose-left body instead of unpacking and mirroring in C every present. The
original does this with a real-time signed shear (`jumpSetSpriteTilt` in
choplifter.s) on one bitmap per tilt; this port pre-renders instead, so the
mirror needs its own baked copy per frame -- chooseChopperSprite's "unify
left/right cases" EOR/negate on ACCELX before indexing the tilt table is
the same idea, just done once here instead of every present.

M10 also emits sheared main-rotor RLE into sprdata_rle.c only (not packed
sprdata.c): 3 ink frames × 11 tilts × flip, even/odd, cropped to ink.
m10.c blits those with hub-relative origins instead of plot_px.

M4: scenery (mountains, barracks, base, fence, flag) as RLE even/odd copies
in src/sprdata_world.c, linked only into m4.exe and later spikes.

M6: hostage run / wave / load frames as RLE even/odd copies in
src/sprdata_host.c, linked only into m6.exe.  Widened to 8x11 (DESIGN.md
section 6); index 15 (white).  Flipped even/odd (`_fe` / `_fo`) for
left-facing run/wave/load.

M7: combat art in src/sprdata_combat.c (jets even-only, tanks even-only,
bullets even/odd, explosions, burning house, saucer).  Linked into m7.exe.
Jets get a flipped even copy (`_fe`); the missile gets flipped even/odd.

M9: title / sortie / win-lose art in src/sprdata_title.c (even-X only,
index 15 white).  HUD digits and the 24x8 counter bubbles are new art in
m9.c / m10.c, not conversions (DESIGN.md section 6).

Reads CHOPGFX through extract_chopgfx.py (no PIL). Each sprite:
  - width_px, height_px, then payload
  - even-X copy and odd-X copy (one nibble shift, padded to even width)
  - index 0 transparent; chopper body 15 / topside 7; rotors 15
  - scenery is one palette-role index per sprite (section 5)

RLE row: { skip_bytes, run_bytes, data[run_bytes] }* 0x00 0x00
  skip = fully transparent dest bytes; trailing skip is implicit
  run >= 2: fully opaque bytes (both nibbles nonzero) — blit_rle uses
            rep movsb, never a full-screen copy
  run == 1: one byte, opaque or a mixed nibble edge (RMW in the blitter)
  0x00 0x00 ends the row

X scale is coverage-OR, not nearest-neighbour, so 1-pixel Apple features
survive 32→18. Widths follow section 6 (aspect-correct, even). Small-sprite
widen is for hostages/HUD, not scenery. HGR checkerboard / isolated pixels
flatten to pairs so they become flat colour, not stripes.

Regenerate after changing conversion rules:

    python tools/build_sprites.py
"""

from pathlib import Path

import extract_chopgfx as chopgfx

REPO = Path(__file__).resolve().parent.parent
OUT_C = REPO / "src" / "sprdata.c"
OUT_RLE = REPO / "src" / "sprdata_rle.c"
OUT_WORLD = REPO / "src" / "sprdata_world.c"
OUT_HOST = REPO / "src" / "sprdata_host.c"
OUT_COMBAT = REPO / "src" / "sprdata_combat.c"
OUT_TITLE = REPO / "src" / "sprdata_title.c"

IDX_BODY = 15       # chopper body (white)
IDX_HIGHLIGHT = 7   # chopper highlight / shadow (light grey)
IDX_ROTOR = 15      # rotor disc (white)
IDX_HOSTAGE = 15    # hostage figures (white; mode 8 IRGB 15)
IDX_TANK = 10       # tank body (light green)
IDX_TANK_HI = 2     # tank highlight / shadow (dark green)
IDX_TREAD = 7       # tank treads (light grey)
IDX_JET = 5         # jet body (palette 5 -> light cyan)
IDX_JET_HI = 3      # jet highlight / shadow (dark cyan)
IDX_SAUCER = 13     # alien saucer (light magenta)
IDX_EXPLODE = 14    # explosion (yellow)
IDX_BULLET = 15     # chopper bullets / muzzle (white)
IDX_SHELL = 15      # tank shell
IDX_MISSILE = 5     # jet missile / bomb (magenta)
IDX_FIRE = 4        # house fire (red)
IDX_RUBBLE = 7      # wreckage (light grey)

# Isolated-pixel art that must become flat colour (DESIGN.md section 6).
FLATTEN_DITHER = 0.50

GROUPS = (
    ("chopperSide",   11, "body",  "chopper_side"),
    ("chopperHeadOn",  5, "body",  "chopper_head"),
    ("mainRotor",      3, "rotor", "main_rotor"),
    ("tailRotor",      4, "rotor", "tail_rotor"),
)

# M4 scenery. Colour is the section 5 palette-role index. widen=False:
# aspect-correct even widths only (the <10 Apple-px widen is for hostages).
SCENERY = (
    ("mountain_00",         8,  "mountain_00"),
    ("mountain_01",         8,  "mountain_01"),
    ("mountain_02",         8,  "mountain_02"),
    ("mountain_03",         8,  "mountain_03"),
    ("house_00",            7,  "house"),
    ("houseSill",          15,  "house_sill"),
    ("baseBuilding",        7,  "base_building"),
    ("baseFlagpole",       15,  "base_flagpole"),
    ("baseFlag_00",         4,  "base_flag_00"),
    ("baseFlag_01",         4,  "base_flag_01"),
    ("fenceTowerSprite4",   7,  "fence_4"),
    ("fenceTowerSprite3",   7,  "fence_3"),
    ("fenceTowerSprite2",   7,  "fence_2"),
    ("fenceTowerSprite1",   7,  "fence_1"),
    ("fenceTowerSprite0",   7,  "fence_0"),
)

# M6 hostages.  Apple ~9x11; section 6 widens to 8x11 so they read as people.
# Colour is index 15 (white).  Even/odd pre-shifts: the eye tracks them.
HOSTAGES = (
    ("hostageRunning",  4, "hostage_run"),
    ("hostageWaving",   3, "hostage_wave"),
    ("hostageLoading",  2, "hostage_load"),
)

# M7 combat.  Jets and tanks are even-X only (DESIGN.md section 6).
# Bullets get even/odd because the eye tracks them.
COMBAT_EVEN = (
    ("jetMaster",     25, IDX_JET,     "jet",       True),
    ("tankCannon",     5, IDX_TANK,    "tank_cannon", True),
    ("explosion",      5, IDX_EXPLODE, "explosion", True),
    ("alien",          4, IDX_SAUCER,  "alien",     True),
    ("houseFireSprites", 2, IDX_FIRE,  "house_fire", True),
)
# (name, colour, c ident, widen, convert role: host=bitmap, flat=preshifted)
COMBAT_EVEN_ONE = (
    ("tank_00",           IDX_TANK,    "tank_turret", True,  "host"),
    ("tank_01",           IDX_TREAD,   "tank_tread_00", True,  "host"),
    ("tank_02",           IDX_TREAD,   "tank_tread_01", True,  "host"),
    ("house_01",          7,           "house_burn", False, "flat"),
    ("houseDebris",       7,           "house_debris", True,  "host"),
    ("chopperRubble_00",  IDX_RUBBLE,  "chop_rubble", True,  "host"),
    ("chopperRubble_01",  IDX_HOSTAGE, "hostage_die", True,  "host"),
)
COMBAT_BULLETS = (
    ("bullet_00", IDX_BULLET,  "bullet_chop"),
    ("bullet_01", IDX_SHELL,   "bullet_shell"),
    ("bullet_02", IDX_MISSILE, "bullet_missile"),
    ("bullet_03", IDX_MISSILE, "bullet_bomb"),
    ("bullet_04", IDX_BULLET,  "muzzle"),
)

# M9 presentation.  Even-X only, index 15 (white).  HUD digits / 24x8
# bubbles are new art in m9.c (DESIGN.md section 6).  xy_scale 1.5 is a
# readability bump for the small title/sortie text; the Choplifter logo
# and win/lose art stay at the aspect-correct 1.0.  Broderbund is Apple
# 220 px: 1.5× would clamp to 160, so it sits at 1.25× instead.
TITLE = (
    ("titleGraphics_00",  "title_mission",     1.5),
    ("titleGraphics_01",  "title_logo",        1.0),
    ("titleGraphics_02",  "title_broderbund",  1.25),
    ("titleGraphics_03",  "title_gorlin",      1.5),
    ("titleGraphics_04",  "title_the_end",     1.0),
    ("titleGraphics_05",  "title_crown",       1.0),
    ("sortieGraphics_00", "sortie_first",      1.5),
    ("sortieGraphics_01", "sortie_second",     1.5),
    ("sortieGraphics_02", "sortie_third",      1.5),
)


def find_entry(name):
    entries = chopgfx.parse_sprite_tables()
    counts = {}
    for e in entries:
        counts[e["table"]] = counts.get(e["table"], 0) + 1
    for e in entries:
        if chopgfx.make_name(e, counts) == name:
            return e
    raise SystemExit(f"sprite {name} not in choplifter.s pointer tables")


def jr_width_px(apple_w, widen=True):
    """Aspect-correct mode-8 width, even, with section 6's small-sprite widen."""
    w = int(round(apple_w * chopgfx.APPLE_TO_JR_X))
    if widen and apple_w < 10:
        w = max(w, 8)
    if w < 2:
        w = 2
    if w & 1:
        w += 1
    return w


def jr_title_size(apple_w, apple_h, scale):
    """Title/sortie text: ~scale on both axes, even width, fit 160 px."""
    w = int(apple_w * chopgfx.APPLE_TO_JR_X * scale + 0.5)
    if w < 2:
        w = 2
    if w & 1:
        w += 1
    if w > 160:
        w = 160
    h = int(apple_h * scale + 0.5)
    if h < 1:
        h = 1
    return w, h


def scale_row_or(src_row, src_w, dst_w):
    """Coverage-OR X scale: a dest column is ink if any source column in its
    bin is ink. Nearest-neighbour dropped every other Apple column at 32→18."""
    out = []
    for x in range(dst_w):
        s0 = x * src_w // dst_w
        s1 = (x + 1) * src_w // dst_w
        if s1 <= s0:
            s1 = s0 + 1
        if s1 > src_w:
            s1 = src_w
        ink = False
        for s in range(s0, s1):
            if src_row[s]:
                ink = True
                break
        out.append(ink)
    return out


def scale_grid_or(ink, src_w, src_h, dst_w, dst_h):
    """Coverage-OR scale in X and Y.  Used to fatten title/sortie text."""
    out = []
    for y in range(dst_h):
        y0 = y * src_h // dst_h
        y1 = (y + 1) * src_h // dst_h
        if y1 <= y0:
            y1 = y0 + 1
        if y1 > src_h:
            y1 = src_h
        row = [False] * dst_w
        for sy in range(y0, y1):
            xr = scale_row_or(ink[sy], src_w, dst_w)
            for x in range(dst_w):
                if xr[x]:
                    row[x] = True
        out.append(row)
    return out


def flatten_isolated(ink_rows):
    """Expand isolated HGR pixels to a 2-pixel pair (the artefact colour)."""
    h = len(ink_rows)
    w = len(ink_rows[0])
    out = [list(row) for row in ink_rows]
    for y in range(h):
        row = ink_rows[y]
        for x in range(w):
            if not row[x]:
                continue
            left = x > 0 and row[x - 1]
            right = x + 1 < w and row[x + 1]
            if left or right:
                continue
            if x + 1 < w:
                out[y][x + 1] = True
            elif x > 0:
                out[y][x - 1] = True
    return out


def colour_body(ink_rows):
    """1bpp ink -> mode-8 indices. Topside ink (no ink above) is highlight."""
    return colour_pair(ink_rows, IDX_BODY, IDX_HIGHLIGHT)


def colour_pair(ink_rows, body, hi):
    """Topside ink (no ink above) is highlight; the rest is body."""
    h = len(ink_rows)
    w = len(ink_rows[0])
    out = []
    for y in range(h):
        row = []
        for x in range(w):
            if not ink_rows[y][x]:
                row.append(0)
            elif y == 0 or not ink_rows[y - 1][x]:
                row.append(hi)
            else:
                row.append(body)
        out.append(row)
    return out


def colour_rotor(ink_rows):
    out = []
    for row in ink_rows:
        out.append([IDX_ROTOR if p else 0 for p in row])
    return out


def colour_flat(ink_rows, idx):
    """One palette-role index for every ink pixel (scenery)."""
    out = []
    for row in ink_rows:
        out.append([idx if p else 0 for p in row])
    return out


def pack_rows(pix):
    """Two pixels per byte, left pixel in the high nibble."""
    packed = []
    w = len(pix[0])
    if w & 1:
        raise SystemExit(f"width {w} must be even for mode-8 packing")
    for row in pix:
        for x in range(0, w, 2):
            packed.append(((row[x] & 0x0F) << 4) | (row[x + 1] & 0x0F))
    return packed


def _opaque_byte(b):
    """Both nibbles live — safe for a REP MOVSB run."""
    return (b & 0x0F) != 0 and (b & 0xF0) != 0


def encode_rle_row(row):
    """One scanline: skip/run commands, then 0x00 0x00.

    Mixed bytes (one nibble transparent) are always a 1-byte run so the
    blitter can RMW that dest byte. Consecutive fully-opaque bytes become
    one run for REP MOVSB. Trailing transparent bytes are omitted.
    """
    out = bytearray()
    i = 0
    n = len(row)
    while i < n:
        skip = 0
        while i < n and row[i] == 0:
            skip += 1
            i += 1
        if i >= n:
            break
        if skip > 255:
            raise SystemExit("RLE skip exceeds 255")
        if not _opaque_byte(row[i]):
            out.append(skip)
            out.append(1)
            out.append(row[i])
            i += 1
            continue
        j = i + 1
        while j < n and _opaque_byte(row[j]):
            j += 1
        run = j - i
        if run > 255:
            raise SystemExit("RLE run exceeds 255")
        out.append(skip)
        out.append(run)
        out.extend(row[i:j])
        i = j
    out.append(0)
    out.append(0)
    return bytes(out)


def decode_rle_row(stream, wbytes):
    """Expand one encoded row back to packed bytes. Returns (row, bytes_used)."""
    dest = bytearray(wbytes)
    i = 0
    di = 0
    n = len(stream)
    while i + 1 < n:
        skip = stream[i]
        run = stream[i + 1]
        i += 2
        if skip == 0 and run == 0:
            if di > wbytes:
                raise SystemExit("RLE decode walked past the row")
            return bytes(dest), i
        di += skip
        if di + run > wbytes:
            raise SystemExit("RLE run overruns the packed row")
        dest[di:di + run] = stream[i:i + run]
        i += run
        di += run
    raise SystemExit("RLE row missing 0x00 0x00 terminator")


def encode_sprite_rle(width_px, height, packed):
    wbytes = width_px // 2
    if wbytes * height != len(packed):
        raise SystemExit(
            f"packed length {len(packed)} != {wbytes}*{height}")
    out = bytearray([width_px & 0xFF, height & 0xFF])
    off = 0
    for y in range(height):
        row = packed[off:off + wbytes]
        enc = encode_rle_row(row)
        dec, used = decode_rle_row(enc, wbytes)
        if used != len(enc) or dec != bytes(row):
            raise SystemExit(f"RLE roundtrip failed at row {y}")
        out.extend(enc)
        off += wbytes
    return bytes(out)


def preshift_odd(pix):
    """Odd pixel-X copy: leading transparent nibble, trailing pad to even."""
    out = []
    for row in pix:
        out.append([0] + list(row) + [0])
    return out


def flip_pix(pix):
    """Horizontal mirror; even width stays even, then re-pack / re-RLE."""
    return [list(reversed(row)) for row in pix]


# Apple renderTiltedSprite shear, matching m10.c's former plot_px path.
# C toward-zero divide (Watcom / C99); Python // floors negatives.
ROTOR_SHEAR_W = 22
ROTOR_SHEAR_HUB = 11
ROTOR_SHEAR_SIGN = (1, 1, 1, 1, 1, 0, -1, -1, -1, -1, -1)
ROTOR_SHEAR_PERIOD = (3, 4, 5, 10, 13, 1, 13, 10, 5, 4, 3)
ROTOR_SHEAR_INK0 = (3, 9, 0)
ROTOR_SHEAR_INK1 = (14, 20, 22)


def c_div_toward_zero(a, b):
    if b == 0:
        return 0
    if a < 0:
        return -((-a) // b)
    return a // b


def shear_rotor_pix(frame, tilt_i, flip):
    """Ink-cropped rotor disc; ox/oy are pixel offsets from the hub."""
    i0 = ROTOR_SHEAR_INK0[frame]
    i1 = ROTOR_SHEAR_INK1[frame]
    t = tilt_i
    if flip:
        tmp = ROTOR_SHEAR_W - i1
        i1 = ROTOR_SHEAR_W - i0
        i0 = tmp
        t = 10 - tilt_i
    sign = ROTOR_SHEAR_SIGN[t]
    period = ROTOR_SHEAR_PERIOD[t]
    pts = []
    for sx in range(i0, i1):
        dx = sx - ROTOR_SHEAR_HUB
        dy = 0
        if sign != 0 and period != 0:
            dy = c_div_toward_zero(sign * dx, period)
        pts.append((ROTOR_SHEAR_HUB + dx, dy))
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    minx, maxx = min(xs), max(xs)
    miny, maxy = min(ys), max(ys)
    w = maxx - minx + 1
    if w & 1:
        w += 1
    h = maxy - miny + 1
    pix = [[0] * w for _ in range(h)]
    for x, y in pts:
        pix[y - miny][x - minx] = IDX_ROTOR
    ox = minx - ROTOR_SHEAR_HUB
    oy = miny
    return pix, ox, oy


def emit_sheared_rotor_rle():
    """3×11×flip even/odd RLE plus hub origins. sprdata_rle.c only."""
    chunks = []
    even_names = [[[None] * 11 for _ in range(3)] for _ in range(2)]
    odd_names = [[[None] * 11 for _ in range(3)] for _ in range(2)]
    ox_tab = [[[0] * 11 for _ in range(3)] for _ in range(2)]
    oy_tab = [[[0] * 11 for _ in range(3)] for _ in range(2)]
    total = 0
    chunks.append(
        "\n/* Sheared main rotor (M10).  Same ink spans and C toward-zero\n"
        " * shear as the old plot_px path.  Cropped to ink so blit_at's\n"
        " * dirty rect is tight.  ox/oy are signed pixel offsets from the\n"
        " * hub.  Packed sprdata.c does not carry these tables.\n"
        " */\n"
    )
    for flip in (0, 1):
        tag = "f" if flip else ""
        for frame in range(3):
            for tilt in range(11):
                pix, ox, oy = shear_rotor_pix(frame, tilt, flip)
                w = len(pix[0])
                h = len(pix)
                even_rle = encode_sprite_rle(w, h, pack_rows(pix))
                odd_pix = preshift_odd(pix)
                odd_rle = encode_sprite_rle(w + 2, h, pack_rows(odd_pix))
                ident_e = f"rotor_s_f{frame}_t{tilt:02d}_{tag}e"
                ident_o = f"rotor_s_f{frame}_t{tilt:02d}_{tag}o"
                even_names[flip][frame][tilt] = ident_e
                odd_names[flip][frame][tilt] = ident_o
                ox_tab[flip][frame][tilt] = ox
                oy_tab[flip][frame][tilt] = oy
                total += len(even_rle) + len(odd_rle)
                preview = ascii_preview(pix)
                preview_c = "\n".join(" *   " + line for line in preview)
                chunks.append(
                    f"/* sheared rotor frame {frame} tilt {tilt}"
                    f"{' flipped' if flip else ''}; "
                    f"{w}x{h}; hub ox={ox} oy={oy}.\n"
                    f"{preview_c}\n"
                    f" */\n"
                )
                chunks.append(emit_rle_array(ident_e, even_rle))
                chunks.append("\n")
                chunks.append(emit_rle_array(ident_o, odd_rle))
                chunks.append("\n")

    def ptr_table(ident, names):
        rows = []
        for frame in range(3):
            inner = ", ".join(names[frame])
            rows.append("    { " + inner + " }")
        return (
            f"unsigned char *{ident}[3][11] = {{\n"
            + ",\n".join(rows)
            + "\n};\n"
        )

    def origin_table(ident, tab):
        rows = []
        for frame in range(3):
            inner = ", ".join(str(v) for v in tab[frame])
            rows.append("    { " + inner + " }")
        return (
            f"signed char {ident}[3][11] = {{\n"
            + ",\n".join(rows)
            + "\n};\n"
        )

    chunks.append(ptr_table("rotor_tilt_e", even_names[0]))
    chunks.append(ptr_table("rotor_tilt_o", odd_names[0]))
    chunks.append(ptr_table("rotor_tilt_fe", even_names[1]))
    chunks.append(ptr_table("rotor_tilt_fo", odd_names[1]))
    chunks.append(origin_table("rotor_tilt_ox", ox_tab[0]))
    chunks.append(origin_table("rotor_tilt_oy", oy_tab[0]))
    chunks.append(origin_table("rotor_tilt_fox", ox_tab[1]))
    chunks.append(origin_table("rotor_tilt_foy", oy_tab[1]))
    return "".join(chunks), total


def ascii_preview(pix):
    glyphs = {0: ".", 2: "d", 3: "c", 4: "R", 5: "J", 7: "+", 8: "M",
              10: "#", 13: "A", 14: "Y", 15: "="}
    lines = []
    for row in pix:
        lines.append("".join(glyphs.get(p, "?") for p in row))
    return lines


def c_bytes(data, indent="    "):
    lines = []
    for i in range(0, len(data), 12):
        chunk = data[i:i + 12]
        lines.append(indent + ", ".join("0x%02X" % b for b in chunk) + ",")
    return "\n".join(lines)


def convert_one(mem, name, role, colour=0, widen=True, colour_hi=None,
                xy_scale=1.0):
    entry = find_entry(name)
    w, h = mem[entry["addr"]], mem[entry["addr"] + 1]
    if role == "flat":
        # Preshifted scenery (mountains, barracks, base) lives in CHOPGFX as
        # seven HGR copies; copy 0 is the unshifted shape we scale.
        entries = chopgfx.parse_sprite_tables()
        all_addrs = sorted({e["addr"] for e in entries})
        data_end = all_addrs[-1] + 8192
        next_addr = {a: (all_addrs[i + 1] if i + 1 < len(all_addrs) else data_end)
                     for i, a in enumerate(all_addrs)}
        gap = next_addr[entry["addr"]] - entry["addr"]
        fmt, stride, _size = chopgfx.classify(mem, entry["addr"], gap)
        rgb_rows, _pw, _ph = chopgfx.render(mem, entry["addr"], fmt, stride)
    else:
        stride = chopgfx.bitmap_stride(w)
        rgb_rows, _pw, _ph = chopgfx.render(mem, entry["addr"], "bitmap", stride)
    ink = [[px != chopgfx.BG for px in row[:w]] for row in rgb_rows]
    dither, _ink_n = chopgfx.dither_ratio(rgb_rows, w)
    if dither >= FLATTEN_DITHER:
        ink = flatten_isolated(ink)
    if xy_scale != 1.0:
        dst_w, dst_h = jr_title_size(w, h, xy_scale)
        scaled = scale_grid_or(ink, w, h, dst_w, dst_h)
    else:
        dst_w = jr_width_px(w, widen=widen)
        dst_h = h
        scaled = [scale_row_or(row, w, dst_w) for row in ink]
    if role == "rotor":
        pix = colour_rotor(scaled)
    elif colour_hi is not None:
        pix = colour_pair(scaled, colour, colour_hi)
    elif role == "flat" or role == "host":
        pix = colour_flat(scaled, colour)
    else:
        pix = colour_body(scaled)
    even_pack = pack_rows(pix)
    odd_pix = preshift_odd(pix)
    odd_pack = pack_rows(odd_pix)
    fpix = flip_pix(pix)
    even_pack_f = pack_rows(fpix)
    odd_pack_f = pack_rows(preshift_odd(fpix))
    return {
        "name": name,
        "apple_w": w,
        "apple_h": h,
        "pix": pix,
        "even_w": dst_w,
        "odd_w": dst_w + 2,
        "h": dst_h,
        "even_pack": even_pack,
        "odd_pack": odd_pack,
        "even_rle": encode_sprite_rle(dst_w, dst_h, even_pack),
        "odd_rle": encode_sprite_rle(dst_w + 2, dst_h, odd_pack),
        "even_rle_f": encode_sprite_rle(dst_w, dst_h, even_pack_f),
        "odd_rle_f": encode_sprite_rle(dst_w + 2, dst_h, odd_pack_f),
        "flattened": dither >= FLATTEN_DITHER,
        "dither": dither,
    }


def emit_array(ident, width, height, packed):
    return (
        f"unsigned char {ident}[] = {{\n"
        f"    {width}, {height},  /* width_px, height_px */\n"
        f"{c_bytes(packed)}\n"
        f"}};\n"
    )


def emit_rle_array(ident, rle):
    width, height = rle[0], rle[1]
    return (
        f"unsigned char {ident}[] = {{\n"
        f"    {width}, {height},  /* width_px, height_px; rest is RLE rows */\n"
        f"{c_bytes(rle[2:])}\n"
        f"}};\n"
    )


def main():
    gfx = REPO / "lib" / "repos" / "ChoplifterReverse" / "CHOPGFX"
    if not gfx.is_file():
        raise SystemExit(
            "CHOPGFX not found at lib/repos/ChoplifterReverse/CHOPGFX.\n"
            "That tree is local reference material (gitignored). "
            "Point it at ChoplifterReverse and re-run.")

    mem, _end = chopgfx.load_memory()
    converted = []
    for prefix, count, role, _cprefix in GROUPS:
        for i in range(count):
            name = f"{prefix}_{i:02d}"
            converted.append((role, convert_one(mem, name, role)))

    packed_chunks = []
    packed_chunks.append("""/* sprdata.c -- packed mode-8 sprite data.
 *
 * Generated by tools/build_sprites.py.  Do not hand-edit the arrays;
 * change the converter and re-run it.
 *
 * M2 flying chopper set (DESIGN.md sections 6 and 13): 11 side tilts,
 * 5 head-on, 3 main-rotor, 4 tail-rotor.  Each sprite has an even-X copy
 * and an odd-X pre-shift (leading transparent nibble).  Index 0 is
 * transparent, 15 body, 7 topside, 15 rotor.  RLE lives in sprdata_rle.c.
 */
""")

    rle_chunks = []
    rle_chunks.append("""/* sprdata_rle.c -- RLE mode-8 sprite data for blit_rle_m8.
 *
 * Generated by tools/build_sprites.py.  Do not hand-edit the arrays;
 * change the converter and re-run it.
 *
 * M3 flying chopper set: same frames as sprdata.c, even/odd pre-shifts,
 * encoded per DESIGN.md section 6:
 *
 *   row := { skip_bytes, run_bytes, data[run_bytes] } ... 0x00 0x00
 *
 * skip_bytes advances the dest (transparent).  run_bytes >= 2 is a fully
 * opaque run (REP MOVSB).  run_bytes == 1 is one byte, possibly a mixed
 * nibble edge.  Trailing transparency is implicit.  Index 0 transparent,
 * 15 body, 7 topside, 15 rotor.
 *
 * Side-view and head-on/rotating frames also have horizontally flipped
 * even/odd RLE (_fe / _fo) so a nose-left body is blit_rle_m8, not a
 * per-present unpack or mirror.
 *
 * M10 sheared main-rotor frames (3 ink x 11 tilt x flip, even/odd) follow
 * the pointer tables.  Packed sprdata.c does not get those arrays.
 */
""")

    tables = []
    rle_flip_tables = []

    idx = 0
    for prefix, count, role, cprefix in GROUPS:
        even_names = []
        odd_names = []
        fe_names = []
        fo_names = []
        emit_flip = cprefix in ("chopper_side", "chopper_head")
        for i in range(count):
            spr = converted[idx][1]
            idx += 1
            ident_e = f"{cprefix}_{i:02d}_e"
            ident_o = f"{cprefix}_{i:02d}_o"
            even_names.append(ident_e)
            odd_names.append(ident_o)
            preview = ascii_preview(spr["pix"])
            preview_c = "\n".join(" *   " + line for line in preview)
            extra = ""
            if spr["flattened"]:
                extra = " flattened isolated HGR pixels;"
            comment = (
                f"/* {spr['name']}: Apple {spr['apple_w']}x{spr['apple_h']} -> "
                f"PCjr {spr['even_w']}x{spr['h']} even / {spr['odd_w']}x{spr['h']}"
                f" odd;{extra} coverage-OR X.\n"
                f"{preview_c}\n"
                f" */\n"
            )
            packed_chunks.append(comment)
            packed_chunks.append(emit_array(ident_e, spr["even_w"], spr["h"],
                                            spr["even_pack"]))
            packed_chunks.append("\n")
            packed_chunks.append(emit_array(ident_o, spr["odd_w"], spr["h"],
                                            spr["odd_pack"]))
            packed_chunks.append("\n")

            rle_chunks.append(comment)
            rle_chunks.append(emit_rle_array(ident_e, spr["even_rle"]))
            rle_chunks.append("\n")
            rle_chunks.append(emit_rle_array(ident_o, spr["odd_rle"]))
            rle_chunks.append("\n")
            if emit_flip:
                ident_fe = f"{cprefix}_{i:02d}_fe"
                ident_fo = f"{cprefix}_{i:02d}_fo"
                fe_names.append(ident_fe)
                fo_names.append(ident_fo)
                rle_chunks.append(
                    f"/* {spr['name']} horizontally flipped; even/odd "
                    f"pre-shifts for blit_rle_m8. */\n"
                )
                rle_chunks.append(emit_rle_array(ident_fe, spr["even_rle_f"]))
                rle_chunks.append("\n")
                rle_chunks.append(emit_rle_array(ident_fo, spr["odd_rle_f"]))
                rle_chunks.append("\n")

        tables.append(
            f"unsigned char *{cprefix}_e[{count}] = {{\n"
            + ",\n".join(f"    {n}" for n in even_names)
            + "\n};\n"
        )
        tables.append(
            f"unsigned char *{cprefix}_o[{count}] = {{\n"
            + ",\n".join(f"    {n}" for n in odd_names)
            + "\n};\n"
        )
        if emit_flip:
            rle_flip_tables.append(
                f"unsigned char *{cprefix}_fe[{count}] = {{\n"
                + ",\n".join(f"    {n}" for n in fe_names)
                + "\n};\n"
            )
            rle_flip_tables.append(
                f"unsigned char *{cprefix}_fo[{count}] = {{\n"
                + ",\n".join(f"    {n}" for n in fo_names)
                + "\n};\n"
            )

    table_blob = "".join(tables) + "\n"
    packed_chunks.append(table_blob)
    rle_chunks.append(table_blob)
    if rle_flip_tables:
        rle_chunks.append("\n" + "".join(rle_flip_tables))
    shear_blob, shear_bytes = emit_sheared_rotor_rle()
    rle_chunks.append(shear_blob)

    OUT_C.write_text("".join(packed_chunks).replace("\r\n", "\n"), encoding="ascii")
    OUT_RLE.write_text("".join(rle_chunks).replace("\r\n", "\n"), encoding="ascii")

    total_pack = 0
    total_rle = 0
    print(f"wrote {OUT_C.relative_to(REPO)}")
    print(f"wrote {OUT_RLE.relative_to(REPO)}")
    for role, spr in converted:
        nbytes = 2 + len(spr["even_pack"]) + 2 + len(spr["odd_pack"])
        rbytes = len(spr["even_rle"]) + len(spr["odd_rle"])
        total_pack += nbytes
        total_rle += rbytes
        flag = " flatten" if spr["flattened"] else ""
        print(f"  {spr['name']:<18} Apple {spr['apple_w']:>2}x{spr['apple_h']:<2} "
              f"-> {spr['even_w']:>2}x{spr['h']:<2} even / {spr['odd_w']:>2} odd"
              f"  packed {nbytes:4} B  rle {rbytes:4} B{flag}")
    print(f"  total packed+headers {total_pack} bytes  rle {total_rle} bytes  "
          f"{len(converted)} sprites")
    print(f"  sheared rotor rle {shear_bytes} bytes  "
          f"66 frames x even/odd (sprdata_rle.c only)")

    world = []
    world.append("""/* sprdata_world.c -- RLE scenery for M4 (blit_rle_m8).
 *
 * Generated by tools/build_sprites.py.  Do not hand-edit the arrays;
 * change the converter and re-run it.
 *
 * Mountains (4-row ridge, index 8), intact barracks + sill, base building,
 * flagpole, two flag frames, five fence towers.  Even/odd pre-shifts.
 * Index 0 transparent.  HGR checkerboard flattened to flat colour.
 * Linked only into m4.exe.
 */
""")
    world_converted = []
    for name, colour, ident in SCENERY:
        spr = convert_one(mem, name, "flat", colour=colour, widen=False)
        world_converted.append((ident, spr))
        extra = ""
        if spr["flattened"]:
            extra = " flattened isolated HGR pixels;"
        preview = ascii_preview(spr["pix"])
        preview_c = "\n".join(" *   " + line for line in preview)
        comment = (
            f"/* {spr['name']}: Apple {spr['apple_w']}x{spr['apple_h']} -> "
            f"PCjr {spr['even_w']}x{spr['h']} even / {spr['odd_w']}x{spr['h']}"
            f" odd; index {colour};{extra} coverage-OR X.\n"
            f"{preview_c}\n"
            f" */\n"
        )
        world.append(comment)
        world.append(emit_rle_array(f"{ident}_e", spr["even_rle"]))
        world.append("\n")
        world.append(emit_rle_array(f"{ident}_o", spr["odd_rle"]))
        world.append("\n")

    world.append(
        "unsigned char *mountain_e[4] = {\n"
        "    mountain_00_e, mountain_01_e, mountain_02_e, mountain_03_e\n"
        "};\n"
        "unsigned char *mountain_o[4] = {\n"
        "    mountain_00_o, mountain_01_o, mountain_02_o, mountain_03_o\n"
        "};\n\n"
        "unsigned char *fence_e[5] = {\n"
        "    fence_0_e, fence_1_e, fence_2_e, fence_3_e, fence_4_e\n"
        "};\n"
        "unsigned char *fence_o[5] = {\n"
        "    fence_0_o, fence_1_o, fence_2_o, fence_3_o, fence_4_o\n"
        "};\n"
    )
    OUT_WORLD.write_text("".join(world).replace("\r\n", "\n"), encoding="ascii")
    print(f"wrote {OUT_WORLD.relative_to(REPO)}")
    total_w = 0
    for ident, spr in world_converted:
        rbytes = len(spr["even_rle"]) + len(spr["odd_rle"])
        total_w += rbytes
        flag = " flatten" if spr["flattened"] else ""
        print(f"  {spr['name']:<18} Apple {spr['apple_w']:>2}x{spr['apple_h']:<2} "
              f"-> {spr['even_w']:>2}x{spr['h']:<2} even / {spr['odd_w']:>2} odd"
              f"  rle {rbytes:4} B{flag}")
    print(f"  scenery rle {total_w} bytes  {len(world_converted)} sprites")

    host = []
    host.append("""/* sprdata_host.c -- RLE hostages for M6 (blit_rle_m8).
 *
 * Generated by tools/build_sprites.py.  Do not hand-edit the arrays;
 * change the converter and re-run it.
 *
 * Running (4), waving (3), boarding (2).  Even/odd pre-shifts.
 * Index 0 transparent, 15 white.  Widened to 8x11 (DESIGN.md
 * section 6).  Linked only into m6.exe.  Flipped even/odd (_fe / _fo)
 * for left-facing figures.
 */
""")
    host_converted = []
    host_tables = []
    for prefix, count, cprefix in HOSTAGES:
        even_names = []
        odd_names = []
        fe_names = []
        fo_names = []
        for i in range(count):
            name = f"{prefix}_{i:02d}"
            spr = convert_one(mem, name, "host", colour=IDX_HOSTAGE, widen=True)
            host_converted.append((name, spr))
            ident_e = f"{cprefix}_{i:02d}_e"
            ident_o = f"{cprefix}_{i:02d}_o"
            ident_fe = f"{cprefix}_{i:02d}_fe"
            ident_fo = f"{cprefix}_{i:02d}_fo"
            even_names.append(ident_e)
            odd_names.append(ident_o)
            fe_names.append(ident_fe)
            fo_names.append(ident_fo)
            extra = ""
            if spr["flattened"]:
                extra = " flattened isolated HGR pixels;"
            preview = ascii_preview(spr["pix"])
            preview_c = "\n".join(" *   " + line for line in preview)
            comment = (
                f"/* {spr['name']}: Apple {spr['apple_w']}x{spr['apple_h']} -> "
                f"PCjr {spr['even_w']}x{spr['h']} even / {spr['odd_w']}x{spr['h']}"
                f" odd; index {IDX_HOSTAGE};{extra} coverage-OR X, widened.\n"
                f"{preview_c}\n"
                f" */\n"
            )
            host.append(comment)
            host.append(emit_rle_array(ident_e, spr["even_rle"]))
            host.append("\n")
            host.append(emit_rle_array(ident_o, spr["odd_rle"]))
            host.append("\n")
            host.append(
                f"/* {spr['name']} horizontally flipped; even/odd "
                f"pre-shifts for blit_rle_m8. */\n"
            )
            host.append(emit_rle_array(ident_fe, spr["even_rle_f"]))
            host.append("\n")
            host.append(emit_rle_array(ident_fo, spr["odd_rle_f"]))
            host.append("\n")
        host_tables.append(
            f"unsigned char *{cprefix}_e[{count}] = {{\n"
            + ",\n".join(f"    {n}" for n in even_names)
            + "\n};\n"
        )
        host_tables.append(
            f"unsigned char *{cprefix}_o[{count}] = {{\n"
            + ",\n".join(f"    {n}" for n in odd_names)
            + "\n};\n"
        )
        host_tables.append(
            f"unsigned char *{cprefix}_fe[{count}] = {{\n"
            + ",\n".join(f"    {n}" for n in fe_names)
            + "\n};\n"
        )
        host_tables.append(
            f"unsigned char *{cprefix}_fo[{count}] = {{\n"
            + ",\n".join(f"    {n}" for n in fo_names)
            + "\n};\n"
        )
    host.append("".join(host_tables))
    OUT_HOST.write_text("".join(host).replace("\r\n", "\n"), encoding="ascii")
    print(f"wrote {OUT_HOST.relative_to(REPO)}")
    total_h = 0
    for name, spr in host_converted:
        rbytes = len(spr["even_rle"]) + len(spr["odd_rle"])
        total_h += rbytes
        flag = " flatten" if spr["flattened"] else ""
        print(f"  {spr['name']:<18} Apple {spr['apple_w']:>2}x{spr['apple_h']:<2} "
              f"-> {spr['even_w']:>2}x{spr['h']:<2} even / {spr['odd_w']:>2} odd"
              f"  rle {rbytes:4} B{flag}")
    print(f"  hostage rle {total_h} bytes  {len(host_converted)} sprites")

    combat = []
    combat.append("""/* sprdata_combat.c -- RLE combat art for M7 (blit_rle_m8).
 *
 * Generated by tools/build_sprites.py.  Do not hand-edit the arrays;
 * change the converter and re-run it.
 *
 * Jets (25) and tanks are even-X only (DESIGN.md section 6).  Bullets
 * have even/odd pre-shifts.  Index 0 transparent.  Linked into m7.exe.
 * Jets have a flipped even copy (_fe); the missile has flipped even/odd.
 */
""")
    combat_n = 0
    combat_b = 0

    for prefix, count, colour, cprefix, widen in COMBAT_EVEN:
        even_names = []
        fe_names = []
        hi = None
        if prefix == "jetMaster":
            hi = IDX_JET_HI
        elif prefix == "tankCannon":
            hi = IDX_TANK_HI
        emit_flip = (prefix == "jetMaster")
        for i in range(count):
            name = f"{prefix}_{i:02d}"
            spr = convert_one(mem, name, "host", colour=colour, widen=widen,
                              colour_hi=hi)
            ident = f"{cprefix}_{i:02d}_e"
            even_names.append(ident)
            extra = " even-only;"
            if spr["flattened"]:
                extra += " flattened isolated HGR pixels;"
            preview = ascii_preview(spr["pix"])
            preview_c = "\n".join(" *   " + line for line in preview)
            combat.append(
                f"/* {spr['name']}: Apple {spr['apple_w']}x{spr['apple_h']} -> "
                f"PCjr {spr['even_w']}x{spr['h']} even-only; index {colour};"
                f"{extra} coverage-OR X.\n"
                f"{preview_c}\n"
                f" */\n"
            )
            combat.append(emit_rle_array(ident, spr["even_rle"]))
            combat.append("\n")
            combat_b += len(spr["even_rle"])
            combat_n += 1
            if emit_flip:
                ident_fe = f"{cprefix}_{i:02d}_fe"
                fe_names.append(ident_fe)
                combat.append(
                    f"/* {spr['name']} horizontally flipped; even-only "
                    f"for blit_rle_m8. */\n"
                )
                combat.append(emit_rle_array(ident_fe, spr["even_rle_f"]))
                combat.append("\n")
                combat_b += len(spr["even_rle_f"])
        combat.append(
            f"unsigned char *{cprefix}_e[{count}] = {{\n"
            + ",\n".join(f"    {n}" for n in even_names)
            + "\n};\n\n"
        )
        if emit_flip:
            combat.append(
                f"unsigned char *{cprefix}_fe[{count}] = {{\n"
                + ",\n".join(f"    {n}" for n in fe_names)
                + "\n};\n\n"
            )

    for name, colour, ident, widen, role in COMBAT_EVEN_ONE:
        hi = IDX_TANK_HI if name == "tank_00" else None
        spr = convert_one(mem, name, role, colour=colour, widen=widen,
                          colour_hi=hi)
        extra = " even-only;"
        if spr["flattened"]:
            extra += " flattened isolated HGR pixels;"
        preview = ascii_preview(spr["pix"])
        preview_c = "\n".join(" *   " + line for line in preview)
        combat.append(
            f"/* {spr['name']}: Apple {spr['apple_w']}x{spr['apple_h']} -> "
            f"PCjr {spr['even_w']}x{spr['h']} even-only; index {colour};"
            f"{extra} coverage-OR X.\n"
            f"{preview_c}\n"
            f" */\n"
        )
        combat.append(emit_rle_array(f"{ident}_e", spr["even_rle"]))
        combat.append("\n")
        combat_b += len(spr["even_rle"])
        combat_n += 1

    combat.append(
        "unsigned char *tank_tread_e[2] = {\n"
        "    tank_tread_00_e, tank_tread_01_e\n"
        "};\n\n"
    )

    for name, colour, cprefix in COMBAT_BULLETS:
        spr = convert_one(mem, name, "host", colour=colour, widen=True)
        extra = " even/odd;"
        if spr["flattened"]:
            extra += " flattened isolated HGR pixels;"
        preview = ascii_preview(spr["pix"])
        preview_c = "\n".join(" *   " + line for line in preview)
        combat.append(
            f"/* {spr['name']}: Apple {spr['apple_w']}x{spr['apple_h']} -> "
            f"PCjr {spr['even_w']}x{spr['h']} even / {spr['odd_w']}x{spr['h']}"
            f" odd; index {colour};{extra} coverage-OR X.\n"
            f"{preview_c}\n"
            f" */\n"
        )
        combat.append(emit_rle_array(f"{cprefix}_e", spr["even_rle"]))
        combat.append("\n")
        combat.append(emit_rle_array(f"{cprefix}_o", spr["odd_rle"]))
        combat.append("\n")
        combat_b += len(spr["even_rle"]) + len(spr["odd_rle"])
        combat_n += 1
        if cprefix == "bullet_missile":
            combat.append(
                f"/* {spr['name']} horizontally flipped; even/odd "
                f"pre-shifts for blit_rle_m8. */\n"
            )
            combat.append(emit_rle_array(f"{cprefix}_fe", spr["even_rle_f"]))
            combat.append("\n")
            combat.append(emit_rle_array(f"{cprefix}_fo", spr["odd_rle_f"]))
            combat.append("\n")
            combat_b += len(spr["even_rle_f"]) + len(spr["odd_rle_f"])

    OUT_COMBAT.write_text("".join(combat).replace("\r\n", "\n"), encoding="ascii")
    print(f"wrote {OUT_COMBAT.relative_to(REPO)}")
    print(f"  combat rle {combat_b} bytes  {combat_n} sprites")

    title = []
    title.append("""/* sprdata_title.c -- RLE title / sortie / win-lose art for M9 (blit_rle_m8).
 *
 * Generated by tools/build_sprites.py.  Do not hand-edit the arrays;
 * change the converter and re-run it.
 *
 * Even-X only, index 15 (white).  Linked into m9.exe and m10.exe.  HUD
 * digits and the 24x8 counter bubbles are new art in m9.c / m10.c, not
 * conversions.
 */
""")
    title_b = 0
    title_n = 0
    for name, ident, scale in TITLE:
        spr = convert_one(mem, name, "host", colour=IDX_BODY, widen=False,
                          xy_scale=scale)
        extra = " even-only;"
        if scale != 1.0:
            extra += f" xy_scale {scale:g};"
        if spr["flattened"]:
            extra += " flattened isolated HGR pixels;"
        or_axes = "XY" if scale != 1.0 else "X"
        title.append(
            f"/* {spr['name']}: Apple {spr['apple_w']}x{spr['apple_h']} -> "
            f"PCjr {spr['even_w']}x{spr['h']} even-only; index 15;"
            f"{extra} coverage-OR {or_axes}. */\n"
        )
        title.append(emit_rle_array(ident, spr["even_rle"]))
        title.append("\n")
        title_b += len(spr["even_rle"])
        title_n += 1
        print(f"  {ident:<18} Apple {spr['apple_w']:>3}x{spr['apple_h']:<2} "
              f"-> {spr['even_w']:>3}x{spr['h']:<2}  scale {scale:g}")
    title.append(
        "unsigned char *sortie_banner_e[3] = {\n"
        "    sortie_first, sortie_second, sortie_third\n"
        "};\n"
    )
    OUT_TITLE.write_text("".join(title).replace("\r\n", "\n"), encoding="ascii")
    print(f"wrote {OUT_TITLE.relative_to(REPO)}")
    print(f"  title rle {title_b} bytes  {title_n} sprites")


if __name__ == "__main__":
    main()
