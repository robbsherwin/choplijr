# Fight performance — where a tank battle spends its time

Scope: the code that runs while the chopper is trading shots with one or more
tanks, usually with hostages in the field and the camera following. Read on
`main` after the FIXES.md series (#9 to #24). Nothing here was built or run;
every figure marked *analysis* is derived from 8088 timings and the M10
hardware logs, not measured. The combat-window capture in `docs/TIMINGS.md`
(`M10-05.LOG`) is the measurement this leans on.

## What the hardware log already says

`M10-05.LOG`, real PCjr, flown into a fight: 381 sim ticks in 927 BIOS ticks,
which is **7.5 sim ticks a second against a 20 Hz target**. That is the bog.
Per sim tick, about 133 ms, split:

| phase | share | ms per sim tick (approx) |
|---|---:|---:|
| `present` (restore + scenery + sprites + HUD) | 58% | 77 |
| `idle` (the two retrace waits between sim ticks) | 25% | 33 |
| `flip` (wait for retrace after the present) | 8% | 11 |
| `sim_tick` (all game logic, tanks included) | 9% | 12 |

Two things follow. The tank AI, hostage AI, physics and collision are a
tenth of the frame; making them faster buys nothing visible. And a quarter
of every fight is spent waiting on purpose.

Inside `present`, `restore` was the largest slice while scrolling (30%),
`sprites` next (26%), `mountain` (28%) is gone since the band was removed.
Byte counts do not explain `present`'s 77 ms: a fight's whole working set is
on the order of 1,500 blit bytes and 1,500 restore bytes, which at the M1
rates is 10-12 ms. The other 60-odd ms is C overhead around the assembly,
and the items below go after that.

---

## 1. Stop waiting out retraces the present already missed — biggest single win

`run_viewer()`'s loop counts iterations, not retraces:

```c
for (i = 0; i < limit; i++) {
    if ((i % SIM_DIV) == 0U) { sim_tick(); /* present */ vid_wait_retrace(); flip; }
    else                     { vid_wait_retrace(); }
}
```

The two `else` iterations each wait a full retrace, whatever the sim
iteration cost. When sim + present fits in one frame that gives the intended
20 Hz. When it takes 77 ms, four and a half frames, the loop still burns two
more frames doing nothing before the next tick. In the combat log that is
the 25% `idle`: about 12 seconds of a 51-second flight.

**Fix.** Measure how long the sim iteration took and skip the idle waits it
has already overrun. The 8253 channel 0 count (port 40h, latch via 43h) is
0.84 µs per tick and wraps every 54.9 ms, so pair it with the BIOS tick at
0040:006C for the coarse part: read both before `sim_tick()` and after the
flip, compute elapsed retraces (16.7 ms each), and do `max(0, SIM_DIV - 1 -
overrun)` idle waits instead of a fixed two. No interrupt hook, no CLI, two
`IN`s and a BIOS read per sim tick.

*Analysis:* on the logged fight this alone takes the sim rate from 7.5 Hz to
about 9.5 Hz with no change to the draw path. In quiet flight, where sim +
present already fit in three frames, nothing changes.

**Related, optional:** the `flip` wait after the present averages half a
frame. When the present has already overrun, flipping at once instead of
waiting for retrace saves that at the cost of one torn frame. Worth trying
by eye; the PCjr page flip is a single `OUT`, so the tear is one scanline's
worth of mixed pages, not a smear.

## 2. `restore_rect()`: one `fill_rows_m8` per rect from a per-row pattern table

Today every dirty rect is restored by walking its rows in C: for each row,
`sky_grad_find_idx()` (a two-entry loop over a struct table) and
`band_solid()` (a six-way compare chain), both as calls, to find where the
band changes; then a `fill_rect_m8` per band segment, and a
`sky_grad_fill` with its own row loop for gradient rows.

Look at what that costs for the sprites a fight is made of:

| sprite | rows | bands crossed | asm calls per restore |
|---|---:|---|---:|
| hostage (`HOSTAGE_WORLD_Y` 33, 11 rows at 166-176) | 11 | haze gradient, haze solid, grey, highlight, ground | 5 |
| tank treads (rows ~170-182) | 6-13 | grey, highlight, ground | 3 |
| tank turret and cannon | 3 + 5 | haze solid / grey | 1-2 each |
| chopper body, rotor, tail | 13-18 | usually 1-2 | 1-2 each |
| house body + flame + debris | 11 + 5 + 2 | haze, grey, highlight, ground | 2-4 each |

With two tanks, eight hostages, the chopper, a few shells and booms and two
burning houses on a scroll tick, that is around 35 rects and 300-400 rows,
each row paying two calls and a dozen compares before any pixel moves.
*Analysis:* 300 rows x 60-80 instructions is 20,000-plus instructions per
present on a bus-bound 8088 — plausibly a third of the present, which is
what the log's 30% `restore` says.

**Fix.** Precompute two 200-entry word tables, `row_pat[2][200]`: the
fill word for every scanline, one table for an even starting byte column
and one for odd (the sky dither is the only thing that depends on parity,
and only through parity). `paint_world()` can build them from the same
`band_solid()` / `sky_grad_pattern()` it already uses. Then

```c
fill_rows_m8(seg, xb, y, rw, rows, &row_pat[xb & 1][y]);
```

is the whole of `restore_rect()`: one assembly call per rect, no row loop in
C, no band classification, no `sky_rowpat` staging. `fill_rows_m8` already
exists and already does exactly this walk. Cost: 800 bytes of DGROUP, which
#10 made room for. Byte counts in the WORKSET report stay identical; this
removes overhead, not bytes.

## 3. Retained sprites: do not restore and redraw what did not change

Every present restores every sprite on the page and draws it again, even
when the sprite is exactly where the page already shows it. In a fight
that happens constantly: a tank that is aiming (`tank_burst_wait`, or
`vy == 0`) sits still for ticks at a time; the chopper hovers; waving
hostages (`hostage_act == 0`) only change frame every four ticks; shells
in flight are the only things guaranteed to move every tick.

**Design.** Each page already has a list of the rects it drew last visit
(`lists[pg]`). Extend each entry with an identity: the sprite data pointer
and its screen `x`, `y`. In the draw pass, before blitting an entity,
compute its key; if the same key is in the page's previous list, and that
rect was not intersected by any rect restored this visit, and no sprite
drawn after it in z-order overlaps it, skip both the restore and the blit
and carry the entry forward. Everything else restores and redraws as now.

The overlap tests are the cost: with ~35 rects they are the same kind of
scan `dirty_hits_box` already does for scenery. Keep it simple by only
retaining entities whose rect intersects nothing else on the list; those
are the common case for a tank at range.

*Analysis:* in the moments the player feels most — hovering while a tank
lines up a shot — this removes most of the sprite and restore bytes and
all their call overhead for the chopper, the tanks and the waving hostages.
During a scroll it saves nothing, since every world sprite moves on screen.
It is the largest draw-path win available but also the most code; do 1 and
2 first and measure.

## 4. Pre-bake the fire colours into the explosion frames

Explosions and the dying chopper draw through `blit_rle_m8_fire_rows`,
which runs `lodsb / cs xlatb / stosb / loop` per byte instead of `REP
MOVSB`, because the yellow-to-fire remap (`fire_tab`) is applied at blit
time. `blit_fire` is only ever set around `explosion_e[]` and
`chop_rubble_e` (which contains no yellow and so is unchanged by the
remap), and it is always 1, so the remap is a fixed function of the
sprite bytes.

**Fix.** Apply `fire_tab` to the five explosion frames in
`tools/build_sprites.py` (or once at startup into the same arrays, since
they are not `const`) and drop `blit_fire` and the fire path from m10.
Same bytes written, same DGROUP size, plain `REP MOVSB`. A fight with
three booms on screen blits roughly 300 explosion bytes per present;
*analysis:* this takes each from about 10 instruction fetches to one.

## 5. One sprite for turret plus cannon

A tank is three blits and three dirty rects: treads (18x6), turret (8x3)
and cannon (8x4 or 8x5, five angles). The turret never moves relative to
the treads and the cannon pivots at a fixed point, so the turret and the
five cannon angles compose into five 8x8-ish sprites with no change to
what is drawn. That is one blit, one rect, one restore fewer per tank per
present, for roughly 200 bytes of DGROUP. Two tanks in view: four fewer
blit-plus-restore pairs a present. Treads stay separate; they animate.

## 6. The static-camera scenery hit test

On a tick where the camera did not move, `draw_scenery(full = 0)` probes
about twenty scenery parts against the page's sprite rects. The bounding
box pre-check rejects a probe outright only when the sprite rects are all
far from the scenery; in a fight next to the barracks they are not, and
each probe scans every rect: 20 x 35 is 700 rect tests a present.

**Fix, cheap.** Sort-free bucketing by row band: scenery lives on rows
159-199, so any sprite rect wholly above row 159 (jets, saucers, their
shots, the moon) can be dropped from the scenery hit list before the
probes start. Build that filtered list once per present in `run_viewer()`
next to the bbox it already computes. In a fight most of what is above
row 159 is exactly the stuff that moves every tick.

## 7. Make the WORKSET counters a compile-time choice

`restore_rect()` and `blit_at()` add 32-bit counters on every call, and
`ws_end_tick()` does six 32-bit compares a tick. It is small, but it is
paid on every present of every build. The pad benchmark depends on these
numbers, so do not remove them: wrap them in `#ifdef WORKSET` with the
makefile defining it, so a release build drops them and a benchmark build
keeps them, the same arrangement `DEBUG_KEYS` now uses.

## 8. Scroll-time scenery (the hard one, listed for completeness)

On every scroll tick the whole visible scenery is restored and redrawn:
two burning houses are about 220 restore bytes and 220 blit bytes plus
nine rects of overhead, every present, for as long as the camera moves.
Hardware scrolling was ruled out in `DESIGN.md` section 7 because the 6845
start address moves in 2-byte steps. The software alternative is to
restore only the sliver a sprite uncovered when it moved by k pixels and
redraw it in place, which is only correct where the sprite is opaque over
the overlap. The houses have a solid core; the fence and flag do not. A
per-sprite "opaque inner rect" from the generator would make it safe for
the houses and the base building, which are most of the bytes. Worth a
look only after 1-3 have landed and been measured.

## 9. Things checked and not worth touching

- `update_tank()`, `fire_tank_shell()`, `ordinance_check()`,
  `update_shell()`: a few dozen instructions each per tick, inside the 9%
  `sim_tick` slice. The hostage loop in `ordinance_check()` runs only on an
  impact.
- Tank shells are 2x2 sprites; each costs the same `blit_at` prologue and
  one restore call as a big sprite. Item 2 makes the restore one call;
  the blit prologue is not worth a special path for two bytes.
- Stars: eight `plot_px` calls a present, no change needed.

## Suggested order and how to check it

1. Item 1 (skip overrun idle waits). Measure with `M10 /phases` in a
   fight: `idle` should fall toward zero, sim rate should rise, `present`
   unchanged.
2. Item 2 (table-driven restore). WORKSET byte counts identical; `restore`
   share of `present` should drop by half or more.
3. Item 4 (pre-baked fire) and item 7 (compile-time counters): small,
   safe, do them while there.
4. Item 3 (retained sprites), then 5 and 6, each with a before/after phase
   log. Item 8 last, if the scroll ticks are still the limit.

Every expected gain above is analysis; the `/phases` table on real hardware
is the judge, as `AGENTS.md` requires.
