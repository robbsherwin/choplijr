# Choplifter! for the IBM PCjr — Design

A native PCjr recreation of Dan Gorlin's 1982 Apple II game, built to use the
machine's 16-colour graphics, three-voice sound chip and joysticks.

**Decisions already made:** faithful recreation (not a remake); **640 KB
minimum**, jrIDE-class PCjr with an RGB monitor; Open Watcom C for logic with
NASM for hot paths; shipped as a DOS `.EXE`. A 128 KB stock machine is
historical analysis, not the ship target.

**Reference material** lives in `lib/repos/`:

| Repo | What we take from it |
|---|---|
| `jrIDE` | Brutman's jrIDE spec (`jrIDE.html`): 128 KB motherboard + 608 KB SRAM = 736 KB; jrIDE BIOS sets int 12h to 736 KB; JrConfig reserves a 32 KB video buffer in the first 128 KB. |
| `ChoplifterReverse` | Quinn Dunki's clean-room 6502 RE of the original. The authoritative design doc: tuning constants, entity model, AI states, sprite art. |
| `jrpiano` | Our own SN76496 code, including the port `61h` routing gotcha and the INT 9 key-state hook. |
| `pcjr-flashparty-2018` | PCjr video mode setup, palette programming during retrace, `pztimer.asm`, LZ4, raster IRQ. |
| `pcjr-asm-game` | Mode 9 bank arithmetic, dirty-rect pipeline, SN76496 streaming, real-hardware timing notes. |

---

## 1. Goals and non-goals

**Goals.** Reproduce the original's gameplay and feel exactly, including its
tuning constants, so it plays like Choplifter rather than like something
inspired by it. Look like a PCjr game, not a port — 16 colours used
deliberately. Support the PCjr joystick as the primary control.

**Non-goals.** No scoring system (the original has none — it counts hostages
killed, loaded and rescued). No extra waves, enemies or mechanics. No CGA,
EGA or VGA support. No 64 KB PCjr support. No composite-monitor tuning. No
demo-mode attract loop — the titles and mission screen stay, the scripted
playback does not (section 15).

**Explicitly deferred.** Self-booting disk and cartridge builds; Tandy 1000
compatibility (mode 8 and the SN76496 are common to both, so this stays cheap
to add later, but it is not a v1.0 requirement).

---

## 2. Target hardware

IBM PCjr, **640 KB minimum**, RGB monitor, one or two joysticks, PC-DOS 2.1 or
later. The 2026 machine is jrIDE-class: an IDE sidecar with 1 MB of SRAM, of
which **608 KB fills conventional memory from 128 KB to 736 KB**
(`lib/repos/jrIDE/jrIDE.html`, Hardware). The jrIDE BIOS **sets detected
memory to 736 KB** because the system BIOS only scans to 640 KB (same page,
BIOS). Their hardware reporting 736 KB via int 12h is that BIOS, not a
misconfigure.

The 640 KB floor is the amount **left for DOS programs** after a 32 KB video
reserve: jrIDE.html says that with JrConfig and jrIDE you can boot DOS, have
a 32 KB video buffer, and still see over 640 KB for programs. We require that
configuration. We do **not** ship for a stock 128 KB box, and we do not ask
anyone to pull the sidecar.

Double buffering still needs two 16 KB pages. Those pages can only live in
the **motherboard** 128 KB. jrIDE SRAM does not overlay, relocate, or become
video RAM — see section 3.

**The binding performance constraint** is memory bandwidth, not the CPU. From
the PCjr Technical Reference: RAM reads and writes average two wait states
because the Video Gate Array shares main memory with the processor, so a bus
cycle takes six clocks instead of four. That leaves roughly **795,000 byte
accesses per second** for everything — instruction fetch, game logic and
pixels.

### Analysis (six-clock bus cycle)

These figures are derived from the Technical Reference, not from a timer.
They are **predictions**. M1's hardware run showed they describe **sidecar
RAM** much more closely than video RAM — the Video Gate Array shares the bus
only in the first 128 KB. jrIDE.html notes that the sidecar SRAMs do not need
DRAM refresh; that path is not a substitute for filling a hardware page.

| Operation | Cost per byte | Throughput | Per 16,000-byte screen |
|---|---|---|---|
| `rep stosw` (fill) | ~1500 ns | ~680 KB/s | ~23.5 ms |
| `rep movsw` (copy) | ~3500 ns | ~290 KB/s | ~55.2 ms |
| `rep movsb` (copy) | ~4600 ns | ~217 KB/s | ~73.7 ms |

### Measured on real PCjr (M1, GO)

Source: the M1 GO log (5,947 bytes) transcribed here. The live file at
`J:\GAMES\CHOPLIJR\M1.LOG` is gone (an M10 pad report overwrote it; no
copy remains). These rates are the GO transcription, not a reread.
JrConfig
`DEVICE=JRCONFIG.SYS /V64 /L`, 64 KB video at segment `1000h`, pages **6 and
7**. Average of 8 runs, Zen timer, mode 8, writes to the hidden page. Owner
judged attended visuals correct; page verification all yes. Emulator
`build\M1.LOG` is **not** this log and is not hardware.

| Operation | Bytes timed | avg µs | ns/byte | ms/screen |
|---|---:|---:|---:|---:|
| `rep stosw` video via `B8000` | 16,000 | 44853 | 2803 | 44.8 |
| `rep stosw` video, direct segment | 16,000 | 44860 | 2803 | 44.8 |
| `rep stosw` plain RAM (sidecar) | 16,000 | 23629 | 1476 | 23.6 |
| `rep stosb` video | — | 53798 | — | — |
| `fill_rect_m8` | 16,000 | 46965 | 2935 | 46.9 |
| `rep movsw` RAM→video | 8,000 | 31466 | 3933 | 62.9 |
| `rep movsb` RAM→video | 4,000 | 18060 | 4515 | 72.2 |
| `rep movsw` video→video | 8,000 | 40409 | 5051 | 80.8 |
| `rep movsw` RAM→RAM | 8,000 | 21138 | 2642 | 42.2 |

`ns/byte` and `ms/screen` are **derived** from the timed average, as quoted.
They add no new assumptions. `stosb` has no derived line because the log's
byte count for that row is not quoted here. M10 pad workset and Hz are
collected in `docs/TIMINGS.md`.

Video `stosw` is 2803 ns/byte against the 1500 ns/byte prediction. Sidecar
`stosw` is 1476 ns/byte, which is the prediction. The analysis table was a
sidecar-like rate applied to video. Section 7's implications are under that
heading; emulator timings remain a smoke test only.

---

## 3. Video mode

**BIOS mode 8: 160 × 200, 16 colours.** 80 bytes per row, 4 bits per pixel,
two pixels per byte with the **leftmost pixel in the high nibble**. Total
16,000 bytes.

Memory is split into two banks exactly like CGA graphics modes: even scanlines
in the first 8 KB, odd scanlines in the second. So:

```
row_offset(y) = (y & 1) * 0x2000 + (y >> 1) * 80
```

### Why mode 8 rather than mode 9

Mode 9 (320 × 200 × 16) doubles the resolution and doubles every pixel cost.
A full-screen fill in mode 9 takes ~47 ms and a full-screen copy ~110 ms,
which consumes the entire frame before a single sprite is drawn. The
historical record is unanimous: Boulder Dash, Jumpman, Pitfall II, River Raid,
Mouser, Microsurgeon, ScubaVenture, Oil's Well, Flight Simulator 2.0 and
Murder on the Zinderneuf all shipped in 160 × 200 × 16. Even Sierra's Troll's
Tale, which sets mode 9, draws its art at an effective 160 × 200.

Mode 8 also happens to preserve Choplifter's composition. Apple HGR pixels are
~0.914:1 on a 4:3 screen and mode 8 pixels are ~1.667:1, so a 160-pixel-wide
window covers about 292 Apple pixels of world — almost exactly the Apple's own
280-pixel viewport. Field of view, camera lead distance and how much of the
battlefield you can see all transfer directly.

Finally, mode 8 is a 16 KB mode, which is what makes free page flipping
possible, and it avoids the high-bandwidth-mode hardware quirks that the
Flashparty demo documented in its "Embarrassing bug" section.

### Mode and page setup

```
mov ax, 0x0008          ; BIOS set mode 8
int 0x10
```

Page selection uses the **CRT/Processor Page Register at port `0x3DF`**:

| Bits | Meaning |
|---|---|
| 0–2 | CRT page — which 16 KB block the CRTC displays |
| 3–5 | CPU page — which 16 KB block appears at `B800:0000` |
| 6–7 | Display mode: 1 = 16 KB graphics mode |

Because the CRT page and CPU page are independent, **page flipping is free**:
draw into the page the CRTC is not showing, then swap both fields with a single
`out` at vertical retrace. No buffer copy, ever. This is a real PCjr capability
that plain CGA does not have, and it is the single most important thing that
makes this design fit in the frame budget.

Both fields can also be set through BIOS, with `int 10h AH=05h AL=83h` and
**`BH` = CRT page, `BL` = CPU page** — which is what Flashparty's parts use.
Note the register order: an earlier draft of this section had it as
`BX = (cpu_page << 8) | crt_page`, i.e. the other way round. `AL=80h` reads the
pair back the same way, and `set_vid_160_100_16` in `part3/part3.asm:1113`
reads it and then shifts `BL` into bits 3–5 — the CPU page field — which
settles it, since that code runs on real hardware. M1 confirms it empirically
rather than taking either source's word for it, by writing a distinct signature
into each page, asking BIOS for a specific pairing and seeing which one appears
in the `B800` window.

### Buffer allocation

We need two 16 KB pages. Port `0x3DF` page fields are three bits each:
**pages 0–7 = physical `00000`–`1FFFF` only**. That is the motherboard 128 KB.
jrIDE SRAM starts at 128 KB and runs to 736 KB (`jrIDE.html`: 608 KB of the
card's 1 MB SRAM). Extra RAM does **not** 3DF-map and cannot be a hardware
video page. Copying a sidecar back-buffer into one hardware page is a
different design with a different (copy-bound) budget; it is not the plan.

jrIDE itself does not reserve, relocate, or overlay video RAM. Its BIOS sets
int 12h to 736 KB and leaves the video buffer where the system put it. On
that map DOS occupies the first 128 KB, the large free block sits above
page 7, and M1's old "usable = overlaps a DOS free block" test reports every
page 0–7 in use — which is what the first hardware run showed (int 12h
736 KB, claim at `2DF60`–`B7FFF`, CRT=CPU=4). That is not "PCjr cannot
double-buffer". It is "this CONFIG has no free 16 KB page in 0–7".

**The required fix is the one jrIDE.html already names:** a memory manager
like **JrConfig** that moves the video buffer so DOS can use memory above
128 KB, with a **32 KB video buffer**, and still over 640 KB for programs.
Those 32 KB (or 64 KB with `/V64`) are two or more mode-8 pages in the first
128 KB. JrConfig 3.10 often accounts that window **inside the owner-0008
system MCB** rather than leaving an MCB gap. M1 finds `JRCONSYS`, reads the
resident `/V` size and start page (which follow `/S` if the buffer moved),
and treats whole pages in that window as the video reserve. An MCB hole and
a classic BIOS withhold are still accepted. Page 0 is never used.

Do not force DOS-occupied pages with `/pa` / `/pb`. Page 0 is IVT and BDA.
The PCjr keyboard is NMI, so `CLI` does not make overlaying a live kernel
safe. A game that never returns to DOS may overwrite unused low DOS after
init; M1 returns to DOS and will not. Save/restore of pages that hold
resident drivers is not the product path — JrConfig is.

The EXE, simulation and assets live in sidecar RAM. Video front/back stay
in the 32 KB hole.

---

## 4. Coordinate system

The simulation runs in the **original's coordinate space**, unscaled. Every
tuning constant from the `$7000` block in `choplifter.s` is used verbatim, so
the physics, camera lead and world layout need no retuning and accumulate no
rounding drift. Scaling happens only at render time:

```
screen_x = (world_x - scroll_x) >> 1     /* 2 world px per screen px */
screen_y = 199 - world_y                 /* Y is 1:1, bottom-relative */
```

Apple Y coordinates are bottom-relative and span 0–191. Mode 8 gives us 200
rows, so world Y maps to screen rows 8–199 and the top 8 rows are the HUD
band, which the play field never touches (section 15).

### World constants (verbatim from `choplifter.s:4760`)

| Constant | Value | Meaning |
|---|---|---|
| `BOUNDS_LEFT` | 720 | Left world edge |
| `BOUNDS_RIGHT` | 4864 | Right world edge |
| `SCROLL_END` | 688 | Leftmost camera position |
| `SCROLL_START` | 4616 | Rightmost camera position |
| `BASE_X` | 4724 | Base and landing pad |
| `DOOR_X` | 4821 | Base door — rescue trigger |
| `FENCE_X` | 4468 | Safe zone; no tanks spawn right of here |
| `FARHOUSE_X` | 896 | Rightmost barracks; others at +256 px steps |
| `BOUNDS_TOP` | 112 | Flight ceiling |
| `MOUNTAIN_Y` | 29 | Mountain baseline |
| `LAND_POSY` | 25 | Top of the ground band |
| `SKY_HEIGHT` | 166 | Sky band height |
| `CHOP_GROUND_INIT` | 22 | Chopper's initial depth plane |
| `MAX_SINK` | 6 | How far a burning chopper sinks |

World width is 4144 px, or about 13 screens at 320 world px per screen.

**One adjustment is required.** The camera lead constants `SCROLL_LEAD_L` = 70
and `SCROLL_LEAD_R` = 210 sum to 280, exactly the Apple viewport width. Our
viewport is 320 world px, so they scale proportionally by 320/280:

```
SCROLL_LEAD_L = 80      SCROLL_LEAD_R = 240
```

---

## 5. Palette

The PCjr has 16 palette registers, each selecting one of the fixed 16 IRGB
colours, with **no CGA-style restrictions** — all 16 can be on screen at once.
Registers are written through port `0x3DA` as an index byte then a data byte;
see Flashparty's `change_palette` in `part3/part3.asm:708` for the exact
sequence including the retrace wait.

The original's art carries **no colour at all**. `setPalette` keeps only the
sign bit of the accumulator, so `ZP_PALETTE` is a single bit OR'd into each
byte at draw time; combined with pixel parity inside the monochrome bitmap that
yields the four HGR artifact hues plus black and white. Recolouring for the
PCjr is therefore not a conversion problem — it is choosing a palette index per
entity in the draw call, structurally the same thing the original does, with 16
options instead of 2.

### Index budget

Indices are assigned by role rather than by hue, so that the palette-animation
tricks below stay available.

| Index | IRGB colour | Role |
|---:|---|---|
| 0 | Black | Sky top / transparent |
| 1 | Blue | Sky upper band |
| 9 | Light blue | Sky lower band, horizon haze |
| 8 | Dark grey | Far mountain ridge (parallax layer) |
| 7 | Light grey | Near mountains, base concrete |
| 6 | Brown | Ground |
| 14 | Yellow | Ground highlight, muzzle flash, fire core |
| 2 | Green | Tank treads, turret shadow |
| 10 | Light green | Chopper highlight / topside |
| 11 | Light cyan | Canopy glass, rotor blur |
| 15 | White | Rotor disc, highlights, HUD text, hostages |
| 4 | Red | Enemy tanks |
| 5 | Magenta | Enemy jets |
| 13 | Light magenta | Alien saucer |
| 3 | *cycled* | Reserved for palette animation |
| 12 | *cycled* | Reserved for palette animation |

Index 0 doubles as the sprite transparency key. Drawn black outlines therefore
need a second register mapped to black; index 8 is close enough to serve as an
outline colour without spending another index.

### Techniques, in order of value per cycle

**A gradient sky is free.** The background is already painted as horizontal
`rep stosw` runs, so using indices 0, 1 and 9 in bands instead of one flat
colour costs nothing. This is the single change that will make a screenshot
read instantly as "not the Apple version".

**Palette cycling replaces redrawing.** The rotor disc, muzzle flashes,
explosion flicker and the burning barracks' flames are drawn once in indices 3
and 12 and animated by rewriting those two palette registers each frame — a
handful of `out` instructions instead of hundreds of blitted bytes. This
spends the resource we have (I/O) instead of the one we do not (memory
bandwidth), and it is where most of the game's visual life will come from.

**Flash and fade.** Rewriting all 16 registers is ~48 `out`s, effectively
free: flash white on an explosion, push toward red when the chopper is hit,
fade to black between sorties and at game over. Flashparty's
`common/fadeout16.asm` already has the ramp tables.

**Mid-frame palette swaps — stretch goal only.** A raster-timed interrupt
(Flashparty's `irq_8_init` in `common/utils.asm`) can reprogram registers at
the horizon scanline so the same pixel value means sky above and ground below,
buying more than 16 simultaneous colours. It costs real CPU and is
timing-fragile. The game must not depend on it.

**Dither only the sky bands.** At 160 × 200 the pixels are wide enough that a
checkerboard reads as vertical stripes rather than a blend, so sprites stay
flat colours with hard outlines. The exception is the sky, where a 4 × 4
Bayer matrix over the band fills does read as a blend: `src/m9.c` and
`src/m10.c` fade haze into dark blue and dark blue into black that way. It stays free because the
pattern is a pure function of (x, y), so each row is still one `rep stosw` of
a precomputed word and `restore_rect` can repaint under a sprite without
tracking state. This has only been judged on a rendered preview, not on a
real PCjr display.

Note the corollary for section 7: colour should be spent on horizontal bands,
which are scroll-invariant and therefore free, rather than on scroll-varying
detail, which is expensive.

---

## 6. Sprite format and blitters

### Source art

Extracted and catalogued by `tools/extract_chopgfx.py` into `tools/out/`. 128
sprites, 7,773 bytes, in two formats:

**Bitmap** (121 sprites, drawn by `renderSprite`): a plain 1bpp bitmap at 8
pixels per byte, MSB leftmost, rows padded to whole bytes. Not in screen
format — the blitter repacks 8-bit groups into 7-pixel HGR bytes at draw time.

**Pre-shifted** (7 scenery sprites, drawn by `blitAlignedImage`): already in
HGR format at 7 pixels per byte with bit 7 as the palette colour, stored as
**seven copies pre-shifted to each bit position within a byte**. `alignImg`
sets the row stride to `ceil(width/7) + 1` and the blitter copies bytes
straight to screen with no shifting at all.

That second format is the technique we adopt below, with two copies instead of
seven. Dan Gorlin reached for it for the same reason we do.

The catalogue's `dither_pct` column flags art drawn in alternating pixels,
which reads as a solid artifact colour on real hardware rather than as texture.
The tank body, all five cannon angles, the tail rotor frames and the muzzle
flash are **100% isolated pixels** — these must become flat PCjr colour.
Reproducing the stripes would look like a decoding bug.

### Target format

4 bits per pixel, two pixels per byte, leftmost pixel in the high nibble.
Index 0 is transparent. Each row is **run-length encoded**:

```
row := { skip_bytes, run_bytes, data[run_bytes] } ... 0x00 0x00
```

`skip_bytes` advances the dest (transparent). `run_bytes` of 0 with skip 0
ends the row; trailing transparency is implicit. Choplifter's sprites are
mostly air, so transparent gaps cost nothing and opaque runs become
`rep movsb`. Half-byte edges (a run starting or ending on an odd pixel)
are a 1-byte run: a masked read-modify-write on the one boundary byte.

**Pre-shifting** stores two variants of each sprite, for even and for odd pixel
X, which removes all runtime nibble shifting. This doubles the data, so it is
applied selectively: the chopper, hostages and bullets get both variants
because the eye tracks them; the 25 jet rotation frames and the tanks are
byte-aligned only, since 2-pixel positional quantisation is invisible on
fast-moving or ground-locked objects.

### Sprite sizes

Scaling aspect-correctly works for the large entities but breaks the small
ones, so below roughly 10 Apple pixels we widen deliberately and accept
slightly stout proportions:

| Entity | Apple | PCjr mode 8 | Note |
|---|---|---|---|
| Helicopter | 32×18 | 18×18 | aspect-correct |
| Enemy jet | 35×18 | 19×18 | aspect-correct |
| Tank | 31×6 | 17×6 | aspect-correct |
| Barracks | 27×11 | 15×11 | aspect-correct |
| Explosion | 24×12 | 13×12 | aspect-correct |
| Hostage | 9×11 | **8×11** | widened from 5 — must read as a person |
| HUD digit | 6×7 | **5×7** | new font, not a conversion |
| HUD counter bubble | 43×9 | **24×8** | one row under aspect-correct — the HUD band is 8 rows |

The small sprites are therefore new art rather than conversions.

### Routines (NASM)

| Routine | Purpose |
|---|---|
| `blit_mask` | M2: packed mode-8 sprite, index 0 transparent; even/odd X via two pre-shifted copies, byte-column blit |
| `blit_rle` | Product sprite blitter (M3): pre-shifted, RLE rows, `rep movsb` runs, masked edges |
| `blit_opaque` | Solid rectangular art (base building interior) |
| `fill_rect` | `rep stosw`, for background bands and dirty-rect clears |
| `fill_band` | Full-width horizontal band, the sky and ground primitive |
| `page_flip` | Single `out` to `0x3DF` at vertical retrace |
| `wait_retrace` | Poll `0x3DA` bit 3 (from Flashparty's `common/utils.asm`) |
| `set_palette` | Index/data pair to `0x3DA` |
| `read_stick` | Joystick axes and buttons |
| `snd_tick` | SN76496 effect scheduler |

---

## 7. Rendering pipeline and frame budget

Two hardware pages, flipped each frame, with **per-buffer dirty-rectangle
restore**. There is no full-screen repaint and no buffer copy.

The insight that makes this cheap: **a solid horizontal band is
scroll-invariant.** Because the sky and ground are flat colour bands — a
decision, not an accident; see section 15 — scrolling does not change them at
all. Only the parallax mountain strip, the scenery
sprites and the entity dirty rects need touching when the camera moves.

Because each buffer is one frame stale, the dirty list is kept per buffer and
what gets restored is the rects that were dirty in *this* buffer two frames
ago, plus anything invalidated by scroll delta accumulated over those two
frames.

### Per frame

1. Read joystick and keyboard
2. Update camera (`scrollTerrain` lead logic), compute scroll delta
3. Restore this buffer's dirty rects from the background model
4. If scrolled: repaint the mountain strip (4 rows × 80 B) and re-blit visible
   scenery — barracks, base, fence, flag
5. Update entities and hostages
6. Blit all sprites, recording a new dirty list
7. Update animated palette registers
8. Wait for vertical retrace, flip the page

### Budget

| Item | Bytes | Cost |
|---|---:|---:|
| Dirty-rect restore | ~3,000 | ~7 ms |
| Mountain strip (scrolling only) | 320 | ~1 ms |
| Scenery re-blit (scrolling only) | ~1,500 | ~5 ms |
| Sprite blits | ~3,000 | ~8 ms |
| Logic, input, sound, palette | — | ~5 ms |
| **Total while scrolling** | | **~26 ms** |
| **Total while static** | | **~20 ms** |

That table is **analysis**. The ~26 ms scrolling total used a sidecar-like
fill rate (~1500 ns/byte, ~23.5 ms/screen in section 2). It is **not**
replaced by a new invented total.

**Hardware rates, quoted from M1.** A full-screen video `stosw` is **44.8 ms**;
`fill_rect_m8` is **46.9 ms**. RAM→video `movsw` is **62.9 ms/screen**, `movsb`
**72.2 ms**, video→video `movsw` **80.8 ms**. The 20 Hz frame is 50 ms, so a
full-page fill or copy does not fit. Sidecar `stosw` is **23.6 ms/screen** —
that is the neighbourhood the ~26 ms line was in, and it is the wrong RAM.

The pipeline still has **no full-screen repaint and no buffer copy**. Dirty
rects and sprite blits are a few thousand bytes, not 16,000; sky and ground
bands are scroll-invariant and are not paid again when the camera moves.
`fill_rect_m8` is 2935 ns/byte and RAM→video `movsb` (what `blit_rle` will
pay) is 4515 ns/byte — those are the rates the working set is counted in.
Multiplying them into a new "total while scrolling" is a later measurement,
not this paragraph.

### Hardware scrolling — deliberately not used

The 6845 start-address registers (`0x3D4` index `0x0C`/`0x0D`) can pan the
display, which would make scrolling nearly free. But the granularity is one
memory word, roughly 4 screen pixels in mode 8, or 8 world pixels. The camera
tracks the chopper at 1–2 screen pixels per frame, finer than that step, so
hardware scrolling alone would judder badly. Since the scroll-invariant band
design already fits the budget with smooth single-pixel scrolling, this stays
in the optimisation drawer.

### Timing

The original has no frame limiter and no retrace sync — it runs flat out, which
on a 1 MHz 6502 lands somewhere around 15–20 fps. Its per-frame velocity and
gravity constants are calibrated to that rate.

We therefore **cap the simulation at exactly 20 Hz** (every third vertical
retrace from the PCjr's 60 Hz) and use the constants verbatim. Running at 30 Hz
would need every velocity constant scaled by 0.6, which is exactly the sort of
retuning that "faithful" is meant to avoid. The divider is a single constant, so
if calibration against the original in an emulator suggests a different rate,
it is a one-line change.

---

## 8. Game model

Ported structurally from `choplifter.s`, which is the design document for all
of this. Logic lives in C; only the blitters, sound and input are assembly.

### Entities

A pool of 30 records kept as a linked list sorted by depth plane
(`ENTITY_GROUND`), matching `entityTable` at `choplifter.s:4940`.

| Type | ID | Max active |
|---|---:|---:|
| Helicopter | 0 | 1 |
| Crashing chopper | 1 | 1 |
| Sinking chopper | 2 | 1 |
| Tank | 3 | 4 |
| Jet missile | 4 | — |
| Jet bomb | 5 | — |
| Chopper bullet | 6 | 5 |
| Tank shell | 8 | — |
| Jet | 9 | 4 |
| Alien saucer | 10 | 4 |

Enemy spawn is attempted every 48th frame, hostage spawn every 3rd, matching
the original's cadence.

### Helicopter

`ZP_TURN_STATE` runs −5 (full left tilt) through 0 (head-on) to +5 (full right
tilt), animating one or two steps per frame toward the target. `CHOP_FACE` is
the instant logical facing: 0 camera, −1 left, +1 right. Physics is 16-bit
VX/VY with stick-driven acceleration tables, gravity, and thrust up to 15.
Landing above `CRASHSPEED` crashes. Death runs chopper → type 1 (air
explosion) → type 2 (sink, `MAX_SINK` = 6) → next sortie.

Rendering composites three sprites: body (one of 11 side tilts or 5 head-on
frames), main rotor (3 frames), tail rotor (4 frames), each with per-tilt
offset tables.

### Hostages

16 slots of 4 bytes (`hostageTable`, `choplifter.s:5000`). States: waving
(4-frame cycle) → running left or right → boarding → aboard. Movement is 2
world pixels per frame. Maximum 16 aboard. They are crushed if the chopper
lands on them and killed by ordnance. On the landing pad they run to
`DOOR_X` and increment `TOTAL_RESCUES`.

### Rules

64 hostages total, 16 in each of 4 barracks. Three sorties. Win at 64 rescued;
lose at 64 killed or on death with `SORTIE` already at 3. Difficulty
`CURR_LEVEL` rises when all loaded hostages are unloaded at the base, capping
at 3: level 0 tanks only, level 1 adds jets, level 2+ adds saucers.

The HUD shows three BCD counters — killed, aboard, rescued. There is no score.

---

## 9. Sound

TI SN76496 at port `0xC0`, three tone voices plus one noise channel. Start from
`lib/repos/jrpiano/jrpiano3.asm`, which already documents the register format
and two things that are only learned the hard way:

- Port `0x61` bits 5 and 6 must be set to route the chip to the speaker.
  jrpiano's comment: *"DOSBox does not enforce this gating, which is why the
  program appeared to work in the emulator but produced complete silence on
  real hardware."*
- Back-to-back `out`s need no software delay; the chip's READY pin inserts
  about 42 wait states.

jrpiano3 also has the three-voice allocation with voice stealing (`voice_next`,
`voice_sc`) that a game needs for overlapping effects.

Faithful means **effects only during play, no background music** — the original
has none. Effects to reproduce: cannon fire, tank shell launch, hostage
boarding triplet, rescue chime, death triplet, house and hostage events, and
the three-note game-start medley. Explosions and crash rumble use the **noise
channel**, a straight upgrade on the Apple's speaker-toggle white noise.

Ctrl-S toggles sound, as in the original.

Title and sortie-banner music is optional and, if added, uses Flashparty's
`common/music_player.asm` PVM streams.

---

## 10. Input

**Joystick is primary.** The original has no keyboard flight controls, and its
two paddle buttons map exactly onto the PCjr's two: **button 0 fires, button 1
rotates**. Axes drive `ZP_STICKX`/`ZP_ACCELX` and `ZP_ACCELY` (thrust, 0–15)
through the original's acceleration tables. Ctrl-A and Ctrl-V invert the axes,
as in the original.

`int 15h AH=84h` is the portable read but slow; direct reads of port `0x201`
with a counting loop are faster. **M5 default on a PCjr (model `FDh`) is
port 201h**, using Paku Paku 1.6a’s loop (CLI, count while bits stay high,
timeout `$7FFF`, both connectors). Original PCjr BIOS does not implement
AH=84h. `/int15` and `/port201` are overrides. Buttons are always port
`201h`. Analog values are scaled from the rest count to the Apple 0–255
paddle scale, then the original nibble tables, with a **±16 deadzone
around 128** first. Ctrl-A and Ctrl-V invert the axes, as in the original.

Keyboard uses the **INT 9 hook and key-state array from `jrpiano3.asm`**, since
BIOS `int 16h` cannot report simultaneous keys. Bindings: any key starts from
the title, Esc pauses, Ctrl-S toggles sound.

---

## 11. Memory budget

jrIDE-class, 640 KB minimum for the program after a 32 KB video reserve.
Typical hardware is 736 KB conventional (jrIDE BIOS). jrIDE.html's remaining
SRAM in the upper-memory holes is **not enabled yet** — we do not plan on
UMBs.

| Region | Where | Size |
|---|---|---:|
| DOS + jrIDE BIOS resident | first 128 KB, minus the video hole | varies with CONFIG.SYS |
| Code (C + asm) | sidecar | ~30 KB |
| Sprite art (room to pre-shift more than the 128 KB plan) | sidecar | ~16–32 KB |
| Working RAM | sidecar | ~2 KB |
| Video buffer A | page in 0–7 (JrConfig 32 KB hole) | 16 KB |
| Video buffer B | page in 0–7 (JrConfig 32 KB hole) | 16 KB |
| Spare | sidecar | hundreds of KB |

The 128 KB packing exercise (~105 KB used, ~20 KB spare) is historical. Art
pressure is gone as a ship-stopper; keep selective pre-shifting because it
is the right trade for the eye, not because we are out of RAM. If something
overflows the *video* hole, that is a page-reserve problem (section 3), not
an art-budget problem.

**A different, tighter ceiling this table does not show: small model's 64 KB
DGROUP.** "Hundreds of KB spare" is true of total sidecar RAM and irrelevant
to it. Small model (section 12) puts every `static`/global variable, every
`const` array (sprite art included — it is read through `data_seg()`, DGROUP,
not a separate segment), and the runtime stack in one 64 KB near-addressable
group, independent of how much RAM the machine has. `CLAUDE-THOUGHTS.md`'s
DGROUP-margin entry found `m10.exe` at 63,152 of 65,536 bytes (2,384 spare)
before a round of hot-path fixes and instrumentation — none of it large
individually — brought it to 65,312 (224 spare), and one more small addition
(a ~98-byte cache) crashed the program outright on real DOSBox-X. Watcom did
not error at 65,312/65,536, so a clean link is not proof of margin. Auditing
the `.map` file segment-by-segment against the pre-review build (not
guessing) found 1,866 of the 2,160 consumed bytes were `CONST` — printf and
usage() string literals, not the 292 bytes of actual new variables; cutting
that text to source comments plus terse output recovered 1,278 bytes at zero
functional cost, back to 1,504 spare. Section 13's still-open palette-effect
tuning will want new static state; re-check the `.map` file's DGROUP total
after adding it, the same way, rather than assuming 1,504 bytes is enough.
Section 12's "small **or** compact" already names the fallback if trimming
cannot keep pace: compact model's data far pointers remove this ceiling, at
the cost of `pcjr.h`'s ABI comment, which currently commits to "small or
compact" — that door was left open on purpose.

---

## 12. Toolchain

**Open Watcom C/C++ V2** for game logic, 16-bit, small or compact model,
`-0` for genuine 8086/8088 codegen. It runs natively on Windows, `wlink` links
NASM `-f obj` output directly, and `#pragma aux` declares register-based
calling conventions so C-to-asm calls avoid stack-frame overhead. It is proven
on this machine — Brutman's mTCP is built with Watcom and supports the PCjr.

**NASM** for the routines in section 6. **`wmake`** to drive it. **Python** for
the asset pipeline, following both reference repos.

Rationale for splitting the languages: `choplifter.s` is over 11,000 lines of
6502 for exactly this game, and nearly all of it is entity bookkeeping, AI
states and spawn tables that consume almost no CPU. Writing that in assembly is
how a project like this stalls at 40% done. The cycles live in about nine small,
well-defined routines, and those are hand-written.

### Testing

DOSBox-X with `machine=pcjr` for the fast loop, using Flashparty's
`conf/dosbox-x_pcjr.conf` as a starting point. **Real hardware for anything
touching video pages, palette timing, joystick or sound** — both reference
repos document emulator-versus-hardware divergence, and Foster's `TODO.md`
records timings that differ from DOSBox by enough to matter.

### Asset pipeline

`tools/extract_chopgfx.py` already decodes the original art and is the
reference for shape and dimension. To be built:

- `tools/build_sprites.py` — flying chopper set from CHOPGFX → packed mode-8
  nibbles in `src/sprdata.c` (M2 `blit_mask`) and RLE rows in
  `src/sprdata_rle.c` (M3 `blit_rle`; skip/run commands, `rep movsb` opaque
  runs). 11 side tilts, 5 head-on, 3 main-rotor, 4 tail-rotor; even and odd
  pixel-X copies; index 0 transparent. Coverage-OR X scale, not
  nearest-neighbour. M4/M5 scenery (mountains, barracks, base, fence, flag) is
  the same RLE in `src/sprdata_world.c`, linked into `m4.exe` and later
  spikes. M6 hostages (run / wave / board, 8×11, index 15) are RLE in
  `src/sprdata_host.c`, linked into `m6.exe`. Combat art (tanks, 25 jet
  frames even-only, saucers, bullets even/odd, explosions, burning house) is
  RLE in `src/sprdata_combat.c`, linked into `m7.exe` and later spikes.
  Title, sortie, and win/lose art is even-X RLE in `src/sprdata_title.c`,
  linked into `m9.exe` and `m10.exe`. Broderbund / Gorlin / mission / sortie banners are
  ~1.5× from the Apple bitmaps (Broderbund 1.25× so it does not fill
  160 px) as a
  readability trial of the crushed X scale; the Choplifter logo and
  win/lose art stay aspect-correct. HUD digits and the 24×8 bubbles are
  new art in `src/m9.c` / `src/m10.c`.
  Later:
  PNG sheets, generated NASM include. Flashparty's
  `lib/repos/pcjr-flashparty-2018/tools/convert_gfx_to_bios_format.py` handles
  mode 8 packing and is worth cribbing. No PIL; zlib PNG write stays in
  `extract_chopgfx.py`.
- `tools/build_sound.py` — prints Apple `playSound` X/Y/A → SN76496 `N=X<<2`.
  Streams live in `src/snd.c` (checked in). Foster's `fosquesttools/sound.py`
  is the tagged F/V/W model if we grow a generator later.

---

## 13. Milestones

| # | Milestone | Exit criteria |
|---|---|---|
| M0 | Toolchain | Watcom + NASM + `wmake` produce a `.EXE` that runs in DOSBox-X and on hardware |
| M1 | **Video spike** | Mode 8, two pages flipping at retrace, palette set, `fill_rect` benchmarked with `pztimer` on **jrIDE-class hardware** (640 KB min, typically 736 KB). **Validates section 2 and 7 and the section 3 JrConfig 32 KB hole.** **GO** on real hardware: pages 6 and 7, visuals correct, section 2 table filled from `J:\GAMES\CHOPLIJR\M1.LOG`. |
| M2 | Asset pipeline | Chopper art authored, converted, and on screen. `blit_mask` plus even/odd pre-shifts of the side, head-on and rotor frames; stub viewer `m2.exe`. |
| M3 | Sprite engine | `blit_rle` plus per-buffer dirty rects; chopper moves cleanly with no flicker |
| M4 | World | Scrolling, camera lead, mountain parallax, ground, barracks, base, fence |
| M5 | Flight | Chopper physics, 11-step tilt state machine, joystick control |
| M6 | Hostages | Spawn, AI states, boarding, unloading, rescue counter |
| M7 | Combat | Tanks, jets, saucers, bullets, collisions, death and sortie cycle |
| M8 | Sound | SN76496 effects with voice stealing |
| M9 | Presentation | HUD, title, sortie banners, difficulty progression, win and lose |
| M10 | Polish | Hardware validation pass, optimisation, palette-effect tuning |

M1 is closed **GO**. Two pages in 0–7 work with JrConfig `/V64` at `1000h`
(highest pair 6 and 7). Video `stosw` came in at 2803 ns/byte against the
1500 ns/byte analysis figure, which matched sidecar RAM instead. That does
not abandon page flipping. It confirms section 7's rule: no full-screen copy
and no full-screen fill — a 16,000-byte video fill is 44.8 ms of a 50 ms
frame. The 128 KB "can DOS even give us two pages on a stock machine?"
question stays closed as a product gate: jrIDE-class RAM and JrConfig's
video reserve.

M3 is **accepted** on DOSBox and real PCjr: `blit_rle_m8` plus per-buffer
dirty lists, even byte widths, no trail, no flicker.

M4 is **accepted** on attended visual (owner): scrolling world, camera
lead, mountain parallax, BROWN ground, scenery. Demo was slower than real
gameplay; accepted for that milestone. No joystick (that is M5). It does
not time dirty-rect or blit byte counts; that measurement is still the
section 14 item.

M5 is a **compiling increment**, not a hardware GO. `build\m5.exe` is the
M4 world plus original helicopter physics, 11-step tilt, and joystick
(port `201h` Paku loop by default on model `FDh`; `/int15` / `/port201`
overrides; either stick; Apple paddle tables after a ±16/255 deadzone).
Keyboard arrows are a DOSBox fallback. Sim is 20 Hz; present may be
faster. Fire is stubbed (M7). Real-hardware stick verification is still
the section 14 item.

M6 is a **compiling increment**, not a hardware GO. `build\m6.exe` is M5
flight plus original hostage spawn and AI (`choplifter.s` `hostageTable`:
wave / run L/R / board, 2 px per tick, cap 16 aboard), unload at the pad
toward `DOOR_X`, and a Killed / Aboard / Rescued count on HUD rows 0–7.
Eight start in the field at the far barracks; only the burning house
releases more until M7 tanks fire the rest. No Playdate rope. Fire is
still stubbed. The F1 debug HUD stays off by default.

M7 is a **compiling increment**, not a hardware GO. `build\m7.exe` is M6
hostages plus tanks, jets, saucers, chopper bullets, collisions, barracks
fires, the type 1/2 explosion and sink cycle, and three sorties. Fire is
no longer a stub. Sound is M8. Sortie banners and win/lose art are
M9. The F1 debug HUD stays off by default.

M8 is a **compiling increment**, not a hardware GO. `build\m8.exe` is M7
combat plus SN76496 effects (`src/snd.c`): three tone voices with
jrpiano3 round-robin steal, noise for shells/explosions/crash, Ctrl-S
mute, port `61h` gate. Apple `playSound` does not block the sim. Title
PVM music is still optional and not here.

M9 is a **compiling increment**, not a hardware GO. `build\m9.exe` is M8
sound plus presentation: 24×8 HUD bubbles with 5×7 digits, converted
title and sortie art (`src/sprdata_title.c`), Broderbund / logo / Gorlin
/ mission screens after the BIOS menu, and the crown / The End overlays.
No attract loop (section 15). Title PVM music is still optional.

M10 is a **compiling increment**, not a hardware GO. Eight pad logs
(same `/ztimer /batch /frames=60` command): pages 6/7 and stick 1 GO.
Workset and Hz were flat through pre-flipped RLE (**4.00 Hz**, ~1151+1074
bytes). Dirty-scenery restamp: restore/rle **872/851**, mountain **126**,
sim **4.92 Hz** (74 BIOS ticks). Sheared-rotor RLE: restore still
**872**, sprites **589→597**, **73** BIOS ticks (**4.98 Hz**); one PIT
tick, not a win. A code-reading pass (`CLAUDE-THOUGHTS.md`) argued the
overflow was C-level CPU cost around the blits rather than memory
bandwidth; a dirty-rect bounding-box pre-check plus a sky-gradient lookup
table cut it further to **69 BIOS ticks (5.27 Hz)**, byte-for-byte the
same pixels. Zen still overflowed the same way at the same sampled tick
(`docs/claude-logs/M10-02.LOG`). Isolated separately (debug-only
`/forcefire` / `/forceclip` switches, same pad scene): the C fallback
`blit_fire` used for every explosion, muzzle flash, burning house and
chopper death nearly doubles present cost (**+99%**,
`docs/claude-logs/M10-CLIP.LOG`) for the same pixels a new assembly
routine (`blit_rle_m8_fire`) draws for **+5.8%**
(`docs/claude-logs/M10-FORC.LOG`) — a ~17x reduction this static pad scene
could not have shown on its own, since it never sets `blit_fire`. The M1
GO rates live in section 2; the live `M1.LOG` is gone. Section 7's ~26 ms
line stays analysis. See `docs/M10.md` and `CLAUDE-THOUGHTS.md`.

---

## 14. Risks

| Risk | Severity | Mitigation |
|---|---|---|
| Two pages in 0–7 cannot be reserved (no JrConfig 32 KB hole, or the hole is only 16 KB) | **High** | M1 gate. First fix: JrConfig (or equivalent) as jrIDE.html specifies — 32 KB video buffer, DOS still >640 KB. Do not overlay DOS-resident pages. Fallbacks only if a hole truly cannot be had: single-page dirty-rect with retrace-timed updates, or a self-booting build that bypasses DOS. |
| Frame budget working-set size is still analysis | **High** | Per-byte rates are the M1 GO table. Pad logs measured **bytes** and a **3.64 / 4.09 / 4.00 / 4.92 / 4.98 Hz** sim; the last step is one PIT tick. The Zen sample overflowed every time. Do not replace the ~26 ms line with those figures. |
| Emulator behaviour diverges from hardware on page registers, palette timing, sound gating | Medium | Test on hardware from M1 onward. jrpiano and Foster's TODO both document specific divergences. |
| Art overflows the memory budget | Medium | Selective pre-shifting; LZ4 the title art; reduce jet frames. |
| Hostages unreadable at 8×11 | Medium | Deliberately widened past aspect-correct; review as soon as M2 can display them. |
| PCjr joystick interface differs from the PC game adapter | Low | M5 uses Paku Paku 1.6a's port 201h loop on model `FDh`; INT 15h is an override only. |
| 20 Hz simulation rate does not match the original's feel | Low | Single-constant divider; calibrate against the original in an emulator. |

---

## 15. Resolved decisions

The four questions this section used to hold open are settled. They are
recorded here with their reasoning, and their consequences are folded into the
sections they affect.

### Ground: flat colour bands and one static highlight line

No scroll-varying ground texture. The ground is a flat `rep stosw` band with a
single static highlight row above it, and it stays byte-identical wherever the
camera is.

This is the decision that keeps section 7 honest. That frame budget rests
entirely on the scroll-invariant band: a ground pattern that moved with the
camera would have to be repainted across the full 80-byte width of every
ground row on every scrolling frame, and the "total while scrolling" line
would grow by most of a full-screen fill.

Nothing real is given up, because the original has no ground texture to give
up. `landBackground` at `choplifter.s:10987` is a four-byte pseudo-sprite,
`$55,$2A,$55,$2A`, stretched to whatever rectangle needs erasing — the same
alternating-pixel fill the catalogue's `dither_pct` column flags across the
tank and cannon art, and it reads as one solid artifact colour on real Apple
hardware rather than as pattern. Flat PCjr colour is the faithful rendering of
it, not a simplification. Depth comes from the parallax mountain strip and the
horizon banding instead, and both of those are already paid for.

Worth noting while we are in that routine: `eraseAllSprites` restores each
sprite's rectangle from exactly two flat pseudo-sprites, `skyBackground`
(`$80` × 4, black) and `landBackground`. The original is already doing
per-sprite dirty-rect restore against a flat background model, which is
section 7's pipeline. We are not inventing that structure, we are inheriting
it.

### HUD in the 8 spare rows

The HUD occupies screen rows 0–7 — the rows mode 8's 200-line frame has spare
over the Apple's 192 — and never overlaps the play field. Section 4's mapping
already assumes it: world Y lands on rows 8–199.

The payoff is that the HUD sits outside the scrolling world and no entity can
ever be drawn over it, so it is not part of the dirty-rect system at all. It
is painted at sortie start and repainted only when one of the three counters
changes: a few bytes a second instead of a few thousand a frame. It also
avoids the one case where a static overlay would have to be tracked in both
video pages' dirty lists.

The cost is one piece of new art. `hudBackgroundBubbleSprite` is 43 × 9 px on
the Apple, which scales to 24 × 9 — one row taller than the eight rows
available. The counter bubbles are therefore **redrawn at 24 × 8**, not merely
relocated. The HUD digits are new art anyway (section 6: 6 × 7 → 5 × 7), so
this is an addition to work already scheduled for M9 rather than a new
problem.

### Titles and mission screen, but no attract loop

We keep the presentation screens: the Broderbund and Dan Gorlin logos, the
"Your Mission: Rescue Hostages" screen, the Choplifter logo and the three
sortie banners. The art is already extracted — `titleGraphicsTable` and
`sortieGraphicsTable` together are 1,944 bytes of the 7,773 total, a quarter
of the whole art budget — and it is what makes the thing feel like a product
rather than a tech demo.

We drop the original's demo-mode attract loop, which plays a scripted 224
frames out and 224 frames back. It is a poor trade: it needs a third game
state that drives the full simulation from a canned input script, so the
entity update path has to run correctly against a non-player input source and
every subsystem has to be resettable mid-flight. That is real complexity in
the most timing-sensitive part of the codebase, spent on something the player
sees only while not playing. This is the one place the faithful-recreation
rule is knowingly relaxed, and the reason is that an attract loop is an
arcade-cabinet convention rather than part of how Choplifter plays.

### All 25 jet rotation frames are kept

The banking turn is a signature moment of the original and the one animation
in the game with real weight to it. It also costs more than anything else: at
1,220 bytes the 25 `jetMaster` frames are the largest single art group, half
again the size of all 11 chopper side tilts put together (793 bytes).

We keep all 25, byte-aligned only. Section 6 already excludes the jets from
pre-shifting, so they cost 1,220 bytes rather than 2,440, and section 11
projects spare sidecar RAM rather than a 20 KB squeeze. Revisit only if the
video hole is smaller than 32 KB; the section 14 fallback stands.

### Still open

- **How the page register gets written each frame.** A raw `out` to `0x3DF`
  needs us to supply the addressing-mode bits in 6–7 ourselves, because the
  port is write-only and cannot be read back to preserve what BIOS put there.
  `int 10h AX=0583h` lets BIOS compute them and keeps its own video variables
  consistent, at the cost of a BIOS call inside the retrace window. M1
  implements both and switches between them at run time, so hardware decides.
- **JrConfig's exact switches for a 32 KB hole.** jrIDE.html names JrConfig
  and the 32 KB video buffer. `JRCONFIG.DOC`: default `/V16` at `1C00h`,
  `/V32` at `1800h`, `/V64` at `1000h` (no `/S`; `/S` moves the buffer and
  the resident start page follows). M1 reads `JRCONSYS` rather than assuming
  an MCB hole. `DEVICE=JRCONFIG.SYS /V64 /L` is the line the hardware boot
  printed as `64KB of PCjr video memory located at segment 1000h`.
