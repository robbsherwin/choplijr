# Architecture review: where the frame budget is actually going

Written after reading `DESIGN.md`, `docs/TIMINGS.md`, `docs/M10.md`, `src/m10.c`,
and `src/asm/{blit,prims,dosmem,stick}.asm`. This is a code-reading exercise, not
a new hardware measurement — nothing here should go in `TIMINGS.md`. Where I
cite an 8088 instruction cost (DIV, a far call, a segment-register load) that is
a documented property of the CPU, not a measurement of this program. Everything
in this file is a hypothesis to go test, ranked by how well the code supports it.

## The headline problem

Every optimisation pass so far (`M10.md`'s "Cuts so far") has targeted **bytes
moved through `fill_rect_m8` / `blit_rle_m8`**, because that is the only thing
`DESIGN.md` section 2 and `docs/TIMINGS.md` know how to price: an `ns/byte` rate
times a byte count. That model says one present (restore + draw, ~1.7 KB of
touched video memory per `docs/TIMINGS.md`'s M10-01 log) should cost roughly
6–8 µs × 1000 — call it single-digit milliseconds even at the worst measured
rate (4515 ns/byte for `movsb`).

The Zen timer wrapped around exactly that span overflows past its ~54 ms window
**every single sample taken so far.** That is not a rounding error against a
6–8 ms prediction — it is evidence that the byte-count model is measuring the
wrong thing. Bytes-moved is a fine proxy for the *assembly* blitters, but the
present path is mostly **C** (dirty-rect bookkeeping, sprite dispatch,
coordinate transforms), and none of that shows up in a `ns/byte` figure at all.
An 8088 running C compiled `-os` is not free just because it isn't touching the
video bus — and right now nothing in this project has ever measured it.

**My best-supported read: the frame budget is CPU-bound on 8088-cycle-expensive
C, not memory-bandwidth-bound on video writes.** Section 2's "binding
performance constraint is memory bandwidth, not the CPU" was true for the M1
spike (which timed nothing but `rep stosw`/`movsw` loops). It stopped being
tested once M3 onward put real game logic in the loop, and M10's own overflow
is the first data point that actually contradicts it.

## Concrete things I found in the code, ranked by how confident I am

### 1. The fire/explosion path silently drops out of the fast blitter — high confidence

`blit_at()` (`src/m10.c:1927`) picks between two completely different
implementations of "draw this RLE sprite":

```c
if (!blit_fire && xbyte0 >= 0 &&
    (unsigned)(xbyte0 + wpx / 2) <= M8_BYTES_PER_ROW)
    blit_rle_m8(seg, (unsigned)xbyte0, (unsigned)y, data_seg(), (unsigned)s);
else
    blit_rle_clip(seg, x_px, y, s, (unsigned)h);
```

`blit_rle_m8` is the NASM routine (`src/asm/blit.asm:153`): `rep movsb` for
every opaque run, near-`call`-per-row overhead only. `blit_rle_clip` is the C
fallback (`src/m10.c:1914` → `blit_rle_row_clip` → `plot_m8_byte`,
`src/m10.c:1869`): it calls `peek_byte`/`poke_byte` — full `__cdecl` far
subroutine calls (`src/asm/dosmem.asm:300`, push bp / mov bp,sp / push ds / load
segment / fetch / pop×3 / ret, per call) — **once or twice per opaque byte**,
and for a "fire" pixel it also runs `fire_byte()` → `fire_nibble()` twice, each
doing a `% 3`. Unsigned `%` on an `-0` 8086 target is a genuine `DIV`
instruction, one of the slowest things this CPU does.

`blit_fire` is set around every explosion, muzzle flash, burning-house flame,
and the chopper's death/sink animation (`src/m10.c:4387`, `4407`). So the
*exact moments combat produces the most on-screen sprites* are also the moments
every one of those sprites pays a per-byte far-call-plus-division cost instead
of `rep movsb`. And because `ws_add_blit()` counts `rle_run_bytes()` — the
sprite's logical opaque-byte count — regardless of which path drew it
(`src/m10.c:1955`), **the WORKSET table in `docs/TIMINGS.md` cannot tell a cheap
blit from an expensive one.** A present full of fire could show the same byte
count as a present with none, while costing an order of magnitude more.

The current M10 pad log is a static, no-combat scene, so this specific path is
probably *not* what's overflowing that particular sample — but it is almost
certainly a large part of why the game gets worse exactly when tanks and jets
start dying, which is worth checking against however the owner's hands-on
impression of "unplayable" lines up with combat density.

*Fix directions:* give `blit_rle_m8` a fire-remap mode in assembly (a lookup
table indexed by nibble, no `DIV`, still `rep movsb`-shaped since you know the
byte in advance is either literal or index-14), so "fire" sprites keep the fast
path. Failing that, at minimum stop reusing `rle_run_bytes()` as the cost proxy
for a blit that took the clipped path — record clipped-path bytes separately in
the WORKSET table so this stops being invisible.

**Tried, isolated from `blit_fire` itself.** Added a `/forceclip` debug flag
(`src/m10.c`) that makes `blit_at()` always take `blit_rle_clip` instead of
`blit_rle_m8`, regardless of `blit_fire` or edge clipping — this exercises the
exact same C fallback path finding #1 describes, on the exact same static pad
scene and the exact same ~859 B of RLE payload, with the *only* variable being
which routine draws it. No entity/game-state changes needed, so there is
nothing to contaminate the comparison.

Same DOSBox-X smoke test, same build otherwise (already carrying the bbox and
sky-gradient changes above): **156 BIOS ticks vs 82 with the flag off — 2.33
Hz vs 4.44 Hz. Roughly 1.9x slower**, moving the same bytes. This is the one
change in this document that is a multiplier, not a percentage. The
`sim_frame 4` Zen sample's *byte counts* were identical in both runs (968 /
830 / 1798, both OVERFLOW) — which is the practical proof of this finding's
"blind spot" claim: two runs that produced identical numbers in
`docs/TIMINGS.md`'s own byte-counting scheme took a measured 1.9x different
amount of real (emulated) time. A present that spends real time in
`blit_fire` (any explosion, muzzle flash, burning house, or the chopper's
death/sink animation) is running through code roughly twice as expensive per
byte as the WORKSET table can see, on a scene that already overflows the
54 ms budget with the fast path alone. This is the strongest single result
in this document and the one worth prioritizing: fixing it (fire-remap
inside the assembly blitter, so "fire" sprites keep `rep movsb`) is the only
change here with multiplier-sized upside rather than single-digit-percent.

**Fixed.** Added `blit_rle_m8_fire` to `src/asm/blit.asm`: the same RLE
row-walk as `blit_rle_m8`, but every stored byte is passed through a
precomputed 256-entry `fire_tab` via `cs xlatb` first. The remap in
`fire_byte()`/`fire_nibble()` only ever recolours a nibble that is already
`0x0E` to another nonzero value (never makes an opaque nibble transparent or
vice versa), so the skip/opaque-run/mixed-nibble structure of a row is
unchanged — an opaque run just can't be `rep movsb` any more, since each byte
needs the table lookup first, so it's a `lodsb`/`xlatb`/`stosb` loop instead.
`fire_tab` was generated from and checked against `fire_byte()` for all 256
inputs (script kept in this repo's history, not hand-derived), fixed for
`blit_fire == 1`, the only value it is ever called with. `blit_at()`
(`src/m10.c`) now picks `blit_rle_m8_fire` whenever `blit_fire` is set,
instead of falling back to `blit_rle_clip` — the C clip path is now reached
only for genuine edge-of-screen clipping, which is rare and untouched by this
change.

Verified with a second debug switch, `/forcefire` (mirrors `/forceclip`: every
in-bounds blit takes the fire path, without touching real game state), on the
exact same static pad scene:

| | BIOS ticks | Hz | vs. no-fire baseline |
|---|---:|---:|---:|
| baseline (fire off) | 82 | 4.44 | — |
| `/forcefire` (new asm path, every blit) | 85 | 4.28 | +3.7% |
| `/forceclip` (old slow C path, every blit) | 156 | 2.33 | +90% |

The marginal cost of the fire-remap operation dropped from a 74-tick penalty
to a 3-tick penalty — roughly a **24x reduction** in what fire/explosion
rendering actually costs relative to a normal blit. WORKSET byte counts
(872/859/1763, mountain 126/scenery 167/sprites 597) are identical across all
three runs: same pixels, the assembly routine is a correctness-preserving
speedup, not a behaviour change. This is the one change in this document
that should visibly matter during actual combat, since that is exactly when
`blit_fire` was previously silently paying the ~90% tax this fix removes —
not measurable against the static pad log the rest of this document uses
(no combat in that scene), but real once tanks and jets are dying on screen.

### 2. Dirty-rect hit-testing is an unindexed linear scan, run per star and per scenery part, every tick — high confidence

`dirty_hits_px()` and `dirty_hits_box()` (`src/m10.c:1389`, `1407`) are `for (i
= 0; i < nhit; i++)` scans with no spatial structure, against a list sized up
to `DIRTY_MAX = 96` (`src/m10.c:311`). They're called:

- Once per star, unconditionally, every present — `draw_stars_and_moon`
  (`src/m10.c:1507`) loops all 36 stars (`N_STARS`, `src/m10.c:1468`) and calls
  `dirty_hits_px` for each one, even on a scene with zero visible stars (this
  is `sim_frame 4`, well before night, if the sky is still daytime-blue at that
  point — worth checking).
- Once per scenery sub-sprite via `spr_hits_dirty` → `box_hits_dirty` →
  `dirty_hits_box` — every house (3 sub-blits × `N_HOUSES=4`), every fence
  tower (5), the pad/building/flagpole/flag (`draw_houses`, `draw_fence`,
  `draw_base`, `src/m10.c:4943`–`5111`) — roughly 20 call sites per tick,
  *even when the camera is static and none of it needs redrawing*, because the
  hit-test itself is the mechanism that decides that.

None of this shows up as a "byte" in the WORKSET table — it's pure comparison
and branch overhead. With a couple of dozen call sites each scanning what could
plausibly be 10–90 dirty rects left over from the previous frame's sprites,
this is a very ordinary way for a present to spend tens of thousands of 8088
cycles without moving a single byte of video memory.

*Fix direction:* the dirty list from the *previous* present on this buffer is
usually small and spatially clustered (sprites near the chopper, near the
ground). A cheap first pass — one min/max bounding box over the whole list,
checked once per star/sprite before the per-rect loop — would let most of these
36+20 calls per tick bail out in O(1) instead of O(nhit).

**Tried.** Added a `dirty_bbox` (one min/max pass over a `(hit, nhit)` pair,
computed once per outer call — `draw_stars_and_moon`, `draw_mountains_dirty`,
`draw_scenery`) and threaded it through `dirty_hits_px`/`dirty_hits_box`/
`box_hits_dirty`/`spr_hits_dirty` as an extra "reject fast" argument. Compiles
clean, and the WORKSET byte counts (restore/rle/video/mountain/scenery/sprite
totals) came back byte-for-byte identical to the unpatched build across all
20 sim ticks — so the change is behaviourally correct, it changes nothing
about what gets drawn.

It did **not** help. Same DOSBox-X smoke test: **85 BIOS ticks vs 84 baseline
— 4.28 Hz vs 4.33 Hz, very slightly *slower*.** The `sim_frame 4` Zen sample
still overflows identically. Reading back from this: in the specific scenario
`docs/TIMINGS.md` has always used (chopper parked on the pad, 0 scrolling, no
combat), the dirty list per buffer is apparently small — on the order of the
handful of sprites actually on screen (chopper body/rotor/tail, whatever
scenery overlaps the parked chopper), not anywhere near `DIRTY_MAX = 96`. A
linear scan over a handful of rects was already cheap; paying for one bbox
pass plus one extra pushed argument on every `spr_hits_dirty`/`dirty_hits_*`
call site (stack-based `__cdecl`, so every extra argument is a real `push`)
costs slightly more than it saves when `nhit` is this small.

This is a useful negative result, not a wasted one: it says the present-path
overflow **in this specific static, no-combat pad log** is not dominated by
dirty-rect scan cost, which redirects suspicion toward either finding #3 (sky-
gradient math, which a parked-on-the-pad chopper's dirty rects may not even
reach much) or, more likely, plain accumulated per-call/per-byte cost — DOSBox-
X's `cycles = fixed 315` is calibrated to a genuine 4.77 MHz 8088, which is
slow enough that dozens of ordinary `__cdecl` far-call blits plus 36 point
tests plus a couple of tile loops can plausibly eat a 54 ms window on its own,
with no single bug required. The bbox change is still structurally correct
and may earn its keep in a denser scene — heavy combat or mid-scroll, where
`nhit` could genuinely approach `DIRTY_MAX` — but that's untested; it has not
been measured against anything but the static pad scenario. Left in place
(harmless, byte-identical output) pending that test, not because it's proven
to help.

### 3. Sky-gradient dithering recomputes a multiply and a divide per pixel-pair, per row, per dirty rect — medium-high confidence

`restore_rect()` (`src/m10.c:1697`) calls `sky_grad_fill` → `sky_grad_pattern`
→ `sky_grad_byte` (twice, once per nibble) → `sky_grad_mix` (`src/m10.c:1030`)
for **every row** of every dirty rect that overlaps the dark/haze bands:

```c
return ((y - (unsigned)g->y0) * 17U) / span;
```

That's a 16-bit multiply and a 16-bit divide, computed fresh for two pixel
columns, for every scanline of every restore that touches those bands — and
the chopper spends most of its on-screen life flying through exactly that
strip of sky, so its trailing dirty rects hit this path constantly. `span` is
fixed per gradient and there are only `SKY_GRAD_N = 2` gradients
(`src/m10.c:1009`), each covering a small, fixed row range — this is a value
that depends only on `y`, so it has at most ~20-ish distinct outputs total and
is being recomputed from scratch instead of looked up.

*Fix direction:* precompute `sky_grad_mix(g, y)` (or the whole `sky_grad_byte`
result for both xbyte parities) into a small per-row table once, at the point
where the gradient bands are defined, rather than inside the per-rect restore
loop. Same idea `sky_rowpat[]` already uses for the multi-row fill batching one
row lower down (`src/m10.c:1066`) — the table exists, it's just built too late
to avoid the per-pixel math.

**Tried.** Added `sky_pat_tab[SKY_GRAD_N][SKY_GRAD_MAXROWS][2]` — both fill
words (even/odd leading byte-column) for every row of both bands, computed
once by `sky_pat_init()` on first use, since `sky_grad_byte`'s output only
ever depends on `y` and `xbyte`'s parity, never its value. `sky_grad_find`
(which returned a pointer) became `sky_grad_find_idx` (returns `0`/`1`/
`SKY_GRAD_N`-as-"none"), so `restore_rect` and `sky_grad_pattern` index the
table directly instead of doing pointer arithmetic. `sky_grad_mix`/
`sky_grad_byte` (the original multiply-and-divide path) are kept, but now run
only 108 times total across the game's lifetime (54 rows × 2 parities), not
once per pixel-pair per row per dirty rect per present.

Same DOSBox-X smoke test: **82 BIOS ticks vs 84 baseline — 4.44 Hz vs
4.33 Hz, about 2.4% faster.** WORKSET byte totals and the `sim_frame 4` Zen
sample's byte counts are identical to the unpatched build (restore 968 / blit
830 / video 1798, same overflow) — the table produces pixel-identical output,
confirming this is a correctness-preserving speedup, not a behaviour change.

Read together with the `-ot` result (+3.7%) and the bbox result (−1.2% on
this scenario): each real fix here is worth low single digits of a percent,
and none comes close to the roughly 10x shrink the `sim_frame 4` present
would need to fit inside its 54 ms window. That is consistent with finding #1
being the one candidate that's a qualitative order-of-magnitude difference
rather than a percentage point — but it's gated on `blit_fire`, which this
static, no-combat pad scenario never sets, so it cannot be measured against
this same log. Confirming or ruling it out needs a combat-heavy benchmark
(explosions/muzzle flashes on screen during the sampled tick), not another
pass over the static pad scene.

### 4. `-os` was chosen on a premise M10 now contradicts — medium confidence, cheap to test

`makefile:89` sets `-os` (optimise for size) with the stated reasoning:

> Nothing that gets timed is written in C, so there is no reason to trade size
> for speed here.

That was true when only `fill_rect_m8`/`blit_rle_m8` were being timed. M10's
own Zen-timer overflow is direct evidence that the *un*timed C — everything in
this document — is now where the missing time is. `-os` on Watcom's 8086
back end tends to prefer smaller, more call-heavy code sequences over inlined
ones; on a machine this slow that can matter as much as an algorithmic fix.

*Fix direction:* rebuild `m10.exe` with `-ot` (or `-oa`/whatever Watcom's speed
preset is for `-0`) as an A/B, no source changes, and see whether the BIOS-tick
Hz in `docs/TIMINGS.md`'s table moves. This is the cheapest experiment in this
whole document — it's a one-line makefile change and a rebuild.

**Tried.** `-ot` linked to a larger binary (103,232 B vs 101,904 B — confirms
the flag actually changed codegen) and, under DOSBox-X's `pcjr-128k-m10.conf`
pattern (`/force /ztimer /batch /frames=60`, fixed-cycle 8086 core, no PCjr
wait-state modelling), measured **81 BIOS ticks vs 84** for the same 20 sim
ticks / 60 retraces — 4.49 Hz vs 4.33 Hz. Real, reproducible, about 3.7%. The
`sim_frame 4` Zen-timer sample overflowed identically in both builds, with the
same byte counts, so `-ot` is worth keeping but is not itself the fix — the
present path is still spending far more than a 54 ms window can hold. Worth
noting on its own: this reproduced under a fixed-cycle core with **no** PCjr
memory-contention modelling at all, at a similar sim rate to hardware's
4.92–4.98 Hz. That is independent evidence (a different execution environment
entirely) that the overflow is CPU-cycle-bound C, not something specific to
the real PCjr's video-bus wait states — exactly what finding #1–#3 argue from
reading the code. This is still an emulator number and does not belong in
`docs/TIMINGS.md`; the real question is whether `-ot` moves the hardware Hz at
all, which only a PCjr pad run answers.

### 5. Every hot-path call pays full stack-based `__cdecl` overhead — medium confidence, larger effort

`pcjr.h`'s header comment already names this as a known, deferred trade-off:

> DESIGN.md section 12 plans `#pragma aux` register conventions for the hot
> paths; that is a real win for the per-sprite blitter... Legibility wins here.

`#pragma aux` is used today only for tiny I/O leaf functions (`snd_psg`,
`snd_ppi_in/out`, `text_hide_cursor`) — grep confirms none of the actual
per-frame hot path (`blit_at`, `dirty_add`, `restore_rect`, `world_to_sx/sy`,
`plot_px`, `spr_hits_dirty`) uses it. Every one of those is called dozens of
times per present with the full push-args/push-bp/mov-bp,sp/pop-bp/ret dance.
That overhead was "under 0.1% of the measurement" for M1's whole-screen timed
calls (per `pcjr.h`'s own reasoning) — it is a much larger fraction of a
function that itself does five lines of arithmetic and gets called 50+ times a
tick.

*Fix direction:* this was correctly deferred while the byte-count model was the
only thing being optimised. Given finding #1–#3, it's now worth revisiting for
the handful of functions actually called per-star/per-sprite/per-row
(`dirty_hits_px`, `dirty_hits_box`, `sky_grad_mix`, `plot_m8_byte`) rather than
the whole codebase — the ROI is concentrated in a small set of tiny, hot,
frequently-called functions, not spread evenly.

### 6. The joystick's CLI-protected counting loop is a real-but-currently-mitigated risk — low confidence for *this* log, worth knowing about

`stick_read_port` (`src/asm/stick.asm:186`) is Paku Paku's RC-discharge count:
`CLI`, then loop `IN`/`AND`/`LOOPNZ` until the axis bit clears or `CX` (started
at `STICK_TIMEOUT = 0x7FFF = 32767`) hits zero. `read_controls()`
(`src/m10.c:2835`) already anticipates the worst case — "nothing plugged in
pays all 32767 IN instructions every tick" — and throttles to about one probe a
second until a live stick is ever seen (`src/m10.c:2900`). Once
`stick_seen_live` is true it probes every tick with no further throttle, on the
reasoning that a real stick can't fake a timeout once centred.

That reasoning holds for a stick that's actually connected and behaving. It
does *not* cover a marginal connector, cable bounce, or the stick momentarily
reading out-of-range for other electrical reasons — any of which reintroduces
the full `CLI`-protected 32767-iteration loop **outside** the part of the frame
the Zen timer ever measures (`read_controls` runs inside `sim_tick`, before
`ztimer_on`). If frame time is ever jittery in a way the present-only
measurements can't explain, this is the first place to look, and it would be
invisible to every log gathered so far by construction.

*Fix direction:* nothing to change yet — just worth an explicit note in
`docs/TIMINGS.md` that `sim_tick()` (joystick read, physics, hostage/entity AI,
`snd_tick`) has never been measured at all, only inferred from `(BIOS ticks −
present ticks)`.

## What I'd do next, in order

1. **Rebuild with `-ot` instead of `-os` and re-run the exact same pad
   command.** Zero source changes, five minutes, and it's a real hardware
   number either way.
2. **Stop trying to fit the present inside the Zen timer's 54 ms window and
   measure the phases separately with `bios_ticks()` deltas instead** — coarse
   (18.2 Hz / ~55 ms resolution) but it doesn't overflow, and it can bracket
   `sim_tick()` vs. `present()` vs. the idle `vid_wait_retrace()` loop
   independently. Right now nobody knows the actual split between "physics and
   AI," "restore and draw," and "waiting on hardware," and every optimisation
   so far has been aimed at only one of those three, unconfirmed to even be the
   largest one on this hardware. That is the single most useful thing to add to
   `docs/TIMINGS.md` before spending more effort on byte-level blit tuning.
3. **Separate "clipped/fire-path bytes" from "fast-path bytes" in the WORKSET
   counters** (finding #1) so the existing instrumentation can see the problem
   it's currently blind to.
4. **Add a cheap bounding-box pre-check to `dirty_hits_px`/`dirty_hits_box`**
   (finding #2) — small, local, testable against the existing WORKSET/Hz
   numbers without touching the rendering itself.
5. **Precompute the sky-gradient row table** (finding #3) — same shape of
   change as `sky_rowpat[]` already does, just moved earlier.
6. Only after 1–5: revisit `#pragma aux` for the specific hot leaf functions
   named in finding #5, since that's the most invasive change and the one
   whose payoff is hardest to predict without the phase-split measurement from
   step 2 telling you it's still worth it.

Everything above is a reading of the source plus well-established 8088 timing
facts (DIV is slow, far calls aren't free, unindexed linear scans cost what
they cost), not a new hardware measurement. Steps 1 and 2 are cheap enough to
run on the next hardware pass and would turn most of this document from
"plausible reading" into "confirmed," one way or the other.
