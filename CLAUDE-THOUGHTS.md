# Architecture review: where the frame budget is actually going

Written after reading `DESIGN.md`, `docs/TIMINGS.md`, `docs/M10.md`, `src/m10.c`,
and `src/asm/{blit,prims,dosmem,stick}.asm`. This started as a code-reading
exercise, not a hardware measurement, and every number quoted inline below is
still an emulator smoke test (DOSBox-X, fixed-cycle 8086 core, no PCjr wait-state
model) — a relative A/B between builds, not a replacement for a hardware figure.
Where an 8088 instruction cost is cited (DIV, a far call, a segment-register
load), that is a documented property of the CPU, not a measurement of this
program.

**Update:** all three findings below were since taken to real hardware
(`docs/claude-logs/M10-02.LOG`, `M10-FORC.LOG`, `M10-CLIP.LOG`; transcribed into
`docs/TIMINGS.md` and `docs/M10.md`, which are the citable numbers from here on).
The bbox + sky-gradient cut moved 73→69 BIOS ticks (4.98→5.27 Hz) on the standard
pad log — smaller than either flag alone predicted, in the same direction as
predicted. The fire-remap fix is confirmed dramatically: `/forceclip` (old path)
nearly doubles present cost on real hardware (+99%, bigger than the emulator's
+90%), `/forcefire` (new path) costs +5.8% (versus the emulator's +3.7%) — real
wait states make the old per-byte `peek_byte`/`poke_byte` fallback even more
expensive relative to `rep`-adjacent assembly than the emulator's fixed-cycle
core showed. The present at `sim_frame 4` still overflows the Zen timer
identically in every one of these hardware logs; nothing below closes that gap
on its own. Treat everything past this point as the reasoning that produced
those results, not as a live number in its own right — the hardware logs above
are authoritative where they overlap with anything quoted below.

## ⚠ DGROUP margin: a near-miss, read this before adding any more static data

Continuing the investigation (phase-timing instrumentation, see the present-
sub-phase breakdown below) turned up a real crash, caused by this review's own
changes, and a more important discovery underneath it.

The breakdown pointed at `blit_at`'s `ws_add_blit(rle_run_bytes(s, h))` —
`rle_run_bytes` re-walks a sprite's entire RLE stream a second time, purely to
produce the WORKSET byte count, redundant with the blit that is about to walk
the same bytes. Measured cost: skipping it (`/noworkset`, a debug flag added
alongside `/forceclip`/`/forcefire`) took a reproducible, deterministic
84→68 BIOS ticks on the standard pad scene — about 19% of present's total
cost, paid on every blit, the entire time this investigation has been running.

The fix attempted was a small round-robin cache (16 entries, ~98 bytes of
static arrays) so the common case — the same handful of sprite pointers
(chopper frames, mountain tiles, scenery) repeating every tick — hits a cheap
pointer-compare loop instead of a full re-parse. It compiled clean and **it
crashed on real DOSBox-X: no output at all, reproducibly, 3/3 runs**, even
with the cache *function* provably unused (dead code, flagged by the compiler
warning) — reverting only the call site did not fix it; removing the arrays
and the function did.

The actual cause: this is an Open Watcom **small model** build (`-ms`),
which caps all near static data — every `static`/global variable plus the
runtime stack — at 64 KB total (`DGROUP`). Checking the link map across this
session's builds:

| Build | DGROUP+stack | Margin to 64 KB |
|---|---:|---:|
| Original, before this review (`m10-os.map`) | 63,152 B | 2,384 B |
| + bbox pre-check, sky-gradient table, phase timing, debug flags | 65,312 B | 224 B |
| + the byte-count cache (crashed) | 65,408 B | **128 B** |

**This review's own fixes consumed 2,160 of the original 2,384-byte margin.**
The cache's extra ~98 bytes crossing some threshold near the absolute limit is
the most likely proximate cause, though the exact failure mode (stack growing
into `_BSS`, which the map shows sitting immediately adjacent with no gap, is
the leading theory) was not root-caused further — there is no source-level
debugger available in this loop, and guessing at a second fix without one
would repeat the same mistake. The cache is reverted; current state (bbox +
sky-gradient + fire-remap + phase timing + `/forceclip`/`/forcefire`/
`/noworkset`, no cache) is back to a 224-byte margin and confirmed stable
(3/3 DOSBox-X runs after the revert, matching pre-crash numbers exactly).

**224 bytes is not a safe margin.** M10's own remaining scope item —
palette-effect tuning (cycling registers 3/12 for rotor/muzzle/explosion/fire,
`docs/M10.md` item 3) — will want new static state (timers, cycle tables) and
could exhaust this without warning, the same way the cache just did.

**Audited and fixed, without needing the debugger.** Comparing the linker map
segment-by-segment against the pre-review build (`m10-os.map`) rather than
guessing: of the 2,160 bytes consumed, only 292 were `_BSS` (the actual new
variables — `dirty_bbox` helpers, `sky_pat_tab`, the `ph_*` accumulators, the
three debug flags). **1,866 bytes were `CONST`** — string literals, almost
entirely the verbose multi-paragraph explanations this review's own phase-
timing printf output and `/forceclip`/`/forcefire`/`/noworkset` usage() text
had accumulated. Moving those explanations into source comments (free — the
compiler strips them) and cutting the printed and usage text to one terse
line each recovered 1,278 bytes with zero functional change (confirmed:
identical BIOS-tick count and byte counts before/after on the standard pad
scene). **Current margin: 1,504 bytes**, not back to the original 2,384 but
a real, verified, low-risk recovery — done without touching any game logic
and without needing to choose between trimming further or moving off small
model. That choice is deferred, not resolved; 1,504 bytes should comfortably
survive M10's palette-tuning item, but re-check the `.map` file after adding
its static state rather than assuming.

The lesson generalises: **printf/usage text in this codebase is not free**,
the same way a `DIV` or a far call is not free elsewhere in this document —
it is real, permanent `CONST` space in a segment that is now known to be
tight. Verbose runtime diagnostics belong in source comments; printed output
should stay as terse as `docs/M10.md`'s own prose already is.

**Fixed, the zero-storage way.** `blit_rle_m8`/`blit_rle_m8_fire` (NASM) now
return the run-byte total (opaque + mixed) they copy, counted as a side
effect of the blit's own row walk — `add [bp-2],cx` right where `.row`
already knows a command's `run` length, accumulated in a local stack slot
(`[bp-2]`) since every general register in that routine — ES, DS, SI, DI,
BX, DX, AX, CX — was already committed to something else. `blit_at`
(`src/m10.c`) uses that return value instead of a second `rle_run_bytes`
pass on the fast path; the rare edge-of-screen `blit_rle_clip` path still
does its own count, unchanged. Confirmed on the standard pad scene: **84→69
BIOS ticks (4.33→5.27 Hz)** — matching the `/noworkset` prediction almost
exactly (84→68) — with WORKSET byte counts identical before and after
(872/859/1763, same mountain/scenery/sprites breakdown) and, per the map
file, **zero new DGROUP bytes** (0xfa20 both before and after this change).
Correctness, performance and memory margin all land clean simultaneously —
the outcome the cache attempt was reaching for without needing new storage
to get there.

**A fair question surfaced a real bug: the phase-timing instrumentation
itself was never free, and it was running unconditionally.** Each of the
eight `bios_ticks()` calls per sim tick (added earlier this session to
produce the present-sub-phase breakdown above) is a far call into
`dosmem.asm` -- real cost, paid every tick, in *every* run including
ordinary attended play with no debug flags at all, not just `/ztimer`
batch tests. Made it opt-in (`/phases`, off by default, mirroring how
`/ztimer` already works) rather than opt-out. Confirmed: 68 BIOS ticks with
no flags (one better than the 69 measured with instrumentation always on),
69 with `/phases` (identical breakdown to before), same WORKSET bytes
either way. **A normal, flagless `M10` run now carries none of this
session's diagnostic overhead** -- the byte-counting is already
near-free via the asm return-value fix above, and the phase timing no
longer runs unless explicitly asked for. This is the build worth trying on
real hardware to judge actual playability, not a synthetic benchmark.

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

**Applied.** `makefile`'s `CFLAGS` now uses `-ot` in place of `-os` for every
module (2026-09-10) — this was tested and left uncommitted for a while;
nothing else changed. Re-checked the DGROUP-margin concern from the section
above (this codebase's small-model near-data segment was down to a
previously-recovered 1,504-byte margin) since `-ot` trades size for speed and
the earlier scare was exactly a size regression eating that margin: `m10.map`
shows DGROUP at `0xfb10` (64,272 B) vs `0xfb00` (64,256 B) before, a **16-byte**
growth, even though `m10.exe` itself grew by about 1,250 bytes. The two flags
mostly move code-segment size, which small-model DOS puts outside `DGROUP`;
the near-data segment barely felt it. Margin to the 64 KB cap after this
change: **1,264 bytes**.

**Confirmed on real hardware, `docs/claude-logs/M10-03.LOG`.** 69→61 BIOS
ticks, 5.27→5.97 Hz — **+13.3%**, bigger than the DOSBox-X prediction
(+3.7%). All byte counts identical to `M10-02.LOG`. Worth keeping.

This same log carried `/phases` for the first time, finally answering "what
I'd do next" step 2 below: **`present` is 65.6% of the frame, `sim_tick`
9.8%, `flip` 8.2%, `idle` 16.4%.** The draw path dominates, game logic
doesn't — this document's central hypothesis, now with a real number behind
it instead of a code-reading argument. Inside `present`: `sprites` 37.5%,
`restore` and `hud` tied at 22.5% each, `scenery` 12.5%, `mountain` 5%.
`sprites` leading supports finding #5's per-call-overhead hypothesis for
the blit hot path. `hud` tying `restore` is new and NOT explained by
anything in this document — `draw_hud()` (`src/m10.c:1403`) already has a
per-page dirty check that should make 18 of this run's 20 ticks near-free
once both video pages have painted the (unchanging, in this static scene)
counters once. Whether that's two genuinely expensive first-draws or the
dirty check not skipping as often as intended is not yet distinguished; a
run-summed BIOS-tick total can't tell those apart, only a per-tick count
can. Worth resolving before spending effort on finding #5, since if it's
the latter, it's a cheap, high-value fix in its own right.

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

**Tried, partially — `sky_grad_mix` dropped from this list first.** By the
time this item came up, finding #3 had already cut `sky_grad_mix`'s call
count from once-per-pixel-pair-per-row to 108 calls for the game's entire
lifetime (a precomputed table replaced the per-call math). Applying a
register calling convention to a function called 108 times total isn't
worth the risk, so it's excluded. `blit_at`'s own dispatch (mentioned in
passing when this was revisited after `sprites` showed up as the largest
`present` sub-phase) was also excluded on inspection: it's a branchy
dispatcher that calls into `blit_rle_m8`/`blit_rle_clip`, not a small leaf
— exactly the shape this technique doesn't suit, and a poor fit for
hand-picking a register/`modify` set with confidence.

Applied `#pragma aux ... parm [...] value [...] modify [ax bx cx dx]` to
the three that remained genuine hot leaves: `dirty_hits_px` (hit→`ax`,
nhit→`dx`, rest on the stack), `dirty_hits_box` (same two), and
`plot_m8_byte` (all three params — `seg`→`ax`, `off`→`dx`, `src`→`cx` as a
full 16-bit slot, not a byte sub-register: Open Watcom 16-bit has a known
bug with literal 8-bit register names like `al`/`bl`/`cl` in `parm`
clauses, and this has no source-level debugger to catch a silent
miscompile if that bug were hit). `modify` is left as the full
general-purpose set on all three since the bodies are ordinary
compiler-generated C, not hand-written asm — the point is only to remove
argument push/pop overhead, not to keep anything alive across the call.

Verified correct (DOSBox-X, static pad scene, `/phases`): WORKSET byte
counts identical (872/859/1763, mountain 126/scenery 167/sprites 597) to
every prior cut, across four repeated runs. **No measurable speedup on this
scene**: total BIOS ticks landed at 65 in every run, matching the pre-change
HUD-fix baseline exactly, with `present`/`sprites`/`sim_tick` bouncing by
±1 tick run-to-run the same way they did before this change — ordinary
DOSBox-X timer jitter, not a regression or a win. Consistent with finding
#2's own conclusion: this specific static, no-scrolling, no-combat scene
has a small dirty list and rarely reaches the clipped-blit fallback
(`plot_m8_byte`'s only caller), so there isn't much fixed call overhead
here to remove in the first place. Whether this helps in a denser scene
(scrolling, combat, more dirty rects, more clipped edge blits) is untested
— kept because it is verified byte-identical and free (zero DGROUP cost),
the same disposition finding #2's bbox pre-check got.

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

### 7. `draw_hud`'s counter bubbles plot one pixel at a time — confirmed and fixed

`M10-03.LOG`'s `/phases` breakdown showed `hud` tied `restore` at 22.5% of
`present`, surprising for a static scene where the hostage counters never
change and `draw_hud()` (`src/m10.c:1403`) already has a per-page dirty
check meant to skip the redraw once each of the two video pages has painted
the (unchanging) counters once.

Traced it: the dirty check is correct, not the problem. Over a 20-sim-tick
run, `back` (the page index) toggles 0/1/0/1.../ each tick
(`src/m10.c:5837`), so exactly 2 of the 20 ticks (one per page) do a real
redraw and the other 18 hit the early-return. The cost is concentrated in
those 2 real draws, and it's the same bug class as finding #1: `hud_draw_bubble()`
plotted its 24x8 counter well **one pixel at a time** via `hud_plot_px()` →
`peek_byte()`/`poke_byte()` (`src/m10.c:1229`), both `__cdecl` far calls
(`pcjr.h:256/258`) — the identical per-call overhead finding #1 already
named as expensive, just in the HUD renderer instead of the sprite blitter.
~180 pixels x 2 far calls per bubble, x3 bubbles (killed/loaded/rescued) x2
real draws (both pages) is on the order of 2,500+ far calls concentrated
into 2 of the run's 20 ticks — consistent with a lopsided per-tick cost a
run-summed total can't see directly, but a two-run isolation (see below)
confirms.

**Fixed.** `hud_draw_bubble`'s x0 is always even (22/68/114, the three
counter slots), so 6 of its 8 rows land on byte boundaries: row 0/7 (2px
inset each side, byte-aligned since the inset is even) and rows 2-5 (full
24px width) now go through `fill_rect_m8` — the same fast assembly
rectangle-fill dirty-rect restore already uses — one far call per row-band
instead of one `hud_plot_px` far-call pair per pixel. Only rows 1 and 6 (an
odd 1px inset, not byte-aligned) still plot pixel by pixel, about 90 far
calls left out of the original ~360 per bubble.

Isolated exactly like findings #1-#3: rebuilt with the fix reverted, ran the
same DOSBox-X smoke test (`/force /ztimer /phases /batch /frames=60`,
static pad scene), then rebuilt with the fix restored and re-ran. **69→65
BIOS ticks (5.27→5.60 Hz)**, `hud` 9→3 ticks (494→165 ms, a 67% cut),
`present` 50→46. WORKSET byte counts identical in both runs (872/859/1763,
mountain 126/scenery 167/sprites 597) — same pixels, only how they're drawn
changed.

**Confirmed on real hardware, `docs/claude-logs/M10-04.LOG`.** Same command
as `M10-03.LOG` (the pre-fix baseline, already carrying `-ot`): **61→56
BIOS ticks, 5.97→6.50 Hz (+8.9%)**, `hud` 9→4 BIOS ticks (494→220 ms) —
smaller than the DOSBox-X isolation's 9→3 but the same direction and order
of magnitude. Byte counts identical (872/859/1763, mountain 126/scenery
167/sprites 597). `sim_tick` (6→3), `idle` (10→13) and `flip` (5→3) also
moved a few ticks on this run; none of those phases were touched by this
fix, so read that as ordinary jitter on a ~1 s real-hardware sample, not a
second effect.

## What I'd do next, in order

Status (2026-09-10): every item below has now been tried. 1, 2, 4 (this
session's HUD fix) are hardware-confirmed wins. 5 and 7 (bbox pre-check,
`#pragma aux`) were tried and verified correct but showed no measurable win
on the static pad scene — both kept as free, harmless, untested-in-denser-
scenes. 6 is done. 3 was superseded (no longer needed once finding #1 was
actually fixed). Nothing is left un-investigated from this list; the next
open question is what a scrolling/combat scene's phase breakdown looks
like, not another item from this list.

1. ~~Rebuild with `-ot` instead of `-os`.~~ **Done, hardware-confirmed:**
   +13.3% (`M10-03.LOG`).
2. ~~Measure the phases separately with `bios_ticks()` deltas.~~ **Done,
   hardware-confirmed:** `present` is 65.6% of the frame, `sim_tick` 9.8%
   (`M10-03.LOG`, `docs/TIMINGS.md`). The draw path is confirmed as the
   target, not game logic.
3. ~~Separate clipped/fire-path bytes from fast-path bytes in the WORKSET
   counters (finding #1).~~ **Superseded, not needed.** The actual fix
   (`blit_rle_m8_fire`, an assembly fire-remap that stays on the fast path)
   made the byte-count blind spot moot — fire sprites are counted correctly
   by the same return-value mechanism as everything else now (see finding
   #1's "Fixed, the zero-storage way" note above); `blit_rle_clip` is only
   reached for genuine edge-of-screen clipping.
4. ~~Find out whether `hud`'s 22.5%-of-`present` share (`M10-03.LOG`) is two
   expensive first-draws or the dirty check not skipping as often as it
   should.~~ **Done (finding #7): the dirty check is fine, the cost was
   `hud_draw_bubble`'s per-pixel far calls.** Fixed with `fill_rect_m8`;
   DOSBox-X isolation showed `hud` cut 9→3 ticks (67%), `present` 50→46.
   **Hardware-confirmed, `M10-04.LOG`:** `hud` 9→4, run total 61→56
   (5.97→6.50 Hz, +8.9%), byte counts identical.
5. ~~Add a bounding-box pre-check to `dirty_hits_px`/`dirty_hits_box`
   (finding #2).~~ **Tried, did not help on the static pad scene (kept,
   harmless, may help in denser scenes — untested there).**
6. ~~Precompute the sky-gradient row table (finding #3).~~ **Done:** ~2.4%
   on the DOSBox-X pad log, byte-identical output.
7. ~~`#pragma aux` for the hot leaf functions named in finding #5.~~ **Done
   for `dirty_hits_px`/`dirty_hits_box`/`plot_m8_byte`** (`sky_grad_mix`
   dropped — finding #3 already made it cold; `blit_at`'s dispatch dropped —
   not a small leaf, poor fit for hand-picked registers). **Verified
   correct, no measurable win**: byte-identical WORKSET across 4 DOSBox-X
   runs, total ticks unchanged from the pre-change baseline within normal
   jitter. Kept (free, zero DGROUP cost) pending a denser/scrolling/combat
   benchmark or a real hardware pass to see if it moves anything there.

Everything above except the two hardware-confirmed items (`-ot`, the HUD
fix) is still a reading of the source plus well-established 8088 timing
facts (DIV is slow, far calls aren't free, unindexed linear scans cost what
they cost) validated only against the DOSBox-X smoke test, not real
hardware. The `#pragma aux` item above is the first one where the DOSBox-X
smoke test itself came back negative (no measured change) rather than
positive-but-emulator-only — worth remembering before spending more effort
generalizing this technique to other functions.

## Combat-window measurement (2026-09-10)

Every `/phases` capture in this document is the static pad scene: 0 tanks,
0 hostages, chopper parked. That scene told us `present` dominates
`sim_tick` overall, but it cannot say anything about combat specifically --
`update_tank`'s AI, hostage AI, more entities in `draw_ents`, more dirty
rects. The `#pragma aux` result above is a concrete example of why that
gap matters: it targeted functions whose call frequency was reasoned about
from the code, not measured in the scene that would actually exercise them
densely, and it needs `plot_m8_byte` (the clipped-blit fallback) called
enough to matter, which a static, no-combat scene doesn't force at all.

`/phases` now also tracks a second set of the same eight counters, gated on
`combat_tick_p()` (`src/m10.c`, next to `run_viewer`): a sim tick counts as
"combat" if a tank is alive and at least one hostage is spawned. This is an
existence check, not a screen-rectangle overlap test -- deliberately simple,
since the chopper is the player and is always on screen, and a live tank
plus a spawned hostage are usually nearby each other in practice. Printed
as a "COMBAT ticks" table after the existing whole-run one, or "COMBAT
ticks: none this run" if the flight never reached one. Needs an attended
run (`M10 /force /phases`, fly into a fight, then Esc) -- `/batch
/frames=60` never leaves the pad, so it will always read zero. Verified on
the static pad smoke test: correctly reports zero combat ticks, whole-run
table numbers unaffected, DGROUP cost minimal (a shared `print_phase_table`
helper avoids paying for the format strings twice -- 1,056 bytes of margin
left of the 64 KB cap after this addition, down from 1,264 before it, of
which most of that delta is the ten new `unsigned long`/`unsigned`
counters, not the printf text). `/force` is emulator-only (bypasses the
BIOS model-byte check DOSBox-X fails); real hardware just needs
`M10 /phases`.

**Run in an actual firefight, `docs/claude-logs/M10-05.LOG`.** Real PCjr,
attended, 381 sim ticks, 316 (83%) classified combat, 268 involved
scrolling. The result: combat does not shift the cost profile.
`sim_tick`/`present`/`flip`/`idle` land within a point of each other
whole-run vs. combat-window (8.2% vs 8.7%, 60.4% vs 58.5%, etc.) — see
`docs/TIMINGS.md` for the full table. `sim_tick` (all of `update_tank`,
`update_hostages`, physics, collision) stays ~8% in this real flight, same
order of magnitude as the static pad scene (6-10%, M10-03/04). Three
different scenes now agree on this. **This closes the question that
started this section: entity-AI C code is not a productive assembly-
conversion target for combat** — it was never more than a tenth of the
frame, combat or not.

What this run actually shows moving is scrolling, not combat: `mountain`
jumped from 5% of `present` (static) to 28.2% here, `restore` from 22.5%
to 30.5% — both driven by `shift != last_shift` parallax retiling and
bigger dirty rects while the camera is moving (268/381 ticks), not by
entity count. `hud` fell to 1.6% (vs 22.5% static), confirming the HUD fix
lands even better in real play than the static number suggested. `sprites`
is still meaningful (26.0%) but is no longer alone at the top the way the
static scene made it look (37.5%) — mountain and restore now take a bigger
bite once scrolling is real. If there's a next present-side target worth
hand-optimizing, this data points at the mountain-retile and dirty-rect-
restore C orchestration around the existing `fill_rect_m8`/`blit_rle_m8`
assembly, not entity AI.
