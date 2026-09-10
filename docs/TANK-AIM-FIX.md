# Tank aim/approach bug — diagnosis and fix plan

Written to pick back up after a break. Two symptoms reported from play:

1. A grounded, stationary chopper can sit for 5+ minutes without a tank
   successfully bombing it — tanks seem to have "trouble targeting."
2. On the first sortie, after the first tank is destroyed, it took a long
   time for a replacement tank to spawn, approach, and finish off a
   non-moving chopper on the ground.

Both trace to the same root cause, in `update_tank()`, `src/m10.c:3945`.

## What the code is supposed to do

The tank's cannon-aim and movement-direction decisions both key off one
byte, called `scratch` in the port (`ZP_SCRATCH64` in the original). It is
meant to represent "how far right/left the chopper is relative to this
tank," folded into a single unsigned byte where `0x80` means "centered,"
values near `0x00` mean "far left," and values near `0xFF` mean "far
right." `tank_aim_table` (`{0x00,0x40,0x70,0x90,0xC0,0xFF}`, `src/m10.c:2327`)
buckets that byte into one of 5 cannon angles; `update_tank` nudges
`e->dir` one step per tick toward whichever bucket `scratch` currently
falls in.

## The original (6502) computation

Reference: `lib/repos/ChoplifterReverse/choplifter.s`, `updateTank` at
`$65c3`, specifically `updateTankOffscreen` through `updateTankClose`
(around line 4236–4293 in that file).

The original does a **true 16-bit subtraction with borrow**:

```asm
sec
lda CHOP_POS_X_L
sbc ENTITY_X_L,x
sta ZP_SCRATCH64        ; low byte of (chop_x - tank_x), borrow-correct
lda CHOP_POS_X_H
sbc ENTITY_X_H,x
sta ZP_SCRATCH65        ; high byte of the SAME 16-bit subtraction
```

Then it classifies by `ZP_SCRATCH65` into **three cases**, each of which
treats `ZP_SCRATCH64` differently:

- `SCRATCH65 == 0x00` ("under," chopper same page, at or right of tank):
  test bit 7 of the *raw* `SCRATCH64`. If clear (chopper is at/right of
  tank on this page): tank is "ready to move," and `SCRATCH64 += 0x80`
  (this is the value later used for aim/fire). If set: **skip aim and
  fire entirely this tick** — the tank just idles toward home
  (`updateTankGoLeft`).
- `SCRATCH65 == 0xFF` ("get closer," chopper same page, at or left of
  tank): same idea, mirrored — ready-to-move only if bit 7 of raw
  `SCRATCH64` is *set*.
- `SCRATCH65 ∈ {0x01, 0x02, 0xFD, 0xFE}` ("close," chopper one page away
  either direction): `SCRATCH64 := SCRATCH65 XOR 0x80` — the *low* byte
  is thrown away and the aim value is derived from the high byte instead.
  **This case also never aims or fires** — it falls straight through to
  the same idle/home-movement path as the "not ready" sub-cases above.
- Anything else (`SCRATCH65` outside `{0xFD..0x02}`, i.e. more than ~2
  pages / ~512-768 world units away): despawn the tank.

So in the original, **aiming and firing only ever happen when the chopper
is on the same 256-unit "page" as the tank** (the `SCRATCH65 == 0x00` or
`0xFF` cases), and even then only on the sub-case where the sign of the
raw low-byte delta says the tank is roughly facing the right way. The
"close" (one page off) and "far" (despawn) cases are movement/lifecycle
only.

## What the port actually does

`src/m10.c:3951-3959`:

```c
dxh = (int)(chop_x >> 8) - (int)(e->x >> 8);
if (dxh != 0 && dxh != -1 && (dxh < -2 || dxh > 2)) {
    compact_ids(tank_ids, &num_tanks, (unsigned char)i);
    ent_free(i);
    return;
}
scratch = (int)(unsigned char)(chop_x - e->x);
if (dxh != 0) scratch ^= 0x80;
```

Two problems:

1. **`dxh` is computed wrong.** It's `(chop_x>>8) - (e->x>>8)` — the
   difference of the two high bytes, computed *independently* of whether
   the low-byte subtraction would borrow. This is **not** the same value
   as the original's `SCRATCH65` (the high byte of the *combined* 16-bit
   subtraction). They agree most of the time, but disagree exactly when
   `chop_x` and `e->x` straddle a 256-unit boundary.

   Concrete example: `chop_x = 0x1005`, `e->x = 0x0FFA`. The chopper is
   only **11 world units to the right** — about as close as it gets.
   - Original: `chop_x - e->x = 0x000B` (true 16-bit subtract) →
     `SCRATCH65 = 0x00` → "under" case, `SCRATCH64` starts at `0x0B`.
   - Port: `dxh = 0x10 - 0x0F = 1` (nonzero!) → applies the XOR:
     `scratch = (unsigned char)(0x1005-0x0FFA) = 0x0B`, then
     `0x0B ^ 0x80 = 0x8B`.

   `0x0B` buckets as strongly-right (bucket 0, `[0x00,0x40)`); `0x8B`
   buckets as dead-center (bucket 2, `[0x70,0x90)`). The tank's aim/
   movement logic sees "chopper is right in front of me" when the
   chopper is actually well off to the right. This isn't a rare edge
   case — it happens any time the two positions straddle a 256-unit
   boundary, which is essentially arbitrary as both move around, so it
   fires unpredictably during ordinary play.

2. **The three-way gating is gone.** The port applies its (already
   miscalculated) `scratch` uniformly and runs the aim-swivel
   (`src/m10.c:4000-4009`) **every tick**, regardless of classification.
   The original explicitly does *not* aim or fire in its "close" (one
   page off) case, or in the "wrong-facing" sub-case of "under"/"get
   closer" — it only ever fires when genuinely lined up on the same page
   *and* facing the right way. The port has no equivalent of "don't even
   try yet."

## Why this explains both symptoms

- **Erratic aim / tank never fires at a stationary target:** direct
  consequence of finding 1 — `scratch` is frequently corrupted toward
  center or the wrong side by the spurious XOR, so the cannon swivels
  toward the wrong angle, or toward a middling angle that never lines up
  with `tank_aim_table` for the tank's actual position.
- **Slow spawn-approach-kill cycle on sortie 1:** the *same* corrupted
  `scratch` feeds the tank's movement decision at `src/m10.c:3971-3999`
  (which way to drive, via the `(e->vy ^ (signed char)scratch) & 0x80`
  checks). A tank that thinks the chopper is roughly centered/behind it
  when it's actually off to one side can walk the wrong way, wander far
  enough to trip the (also slightly-off) despawn check, vanish, and force
  a fresh tank to spawn 432 world units away and start over. Each cycle
  costs real time; only when a freshly-spawned tank's position happens
  not to straddle a page boundary in the first place does it approach and
  fire cleanly, which is luck-of-the-draw rather than reliable behavior.

## Proposed fix

Replace lines `src/m10.c:3951-3959` with a faithful port of
`updateTankOffscreen`'s true 16-bit-subtraction classification. Rough
shape (needs care in the actual patch, this is the plan not final code):

```c
unsigned diff  = chop_x - e->x;      /* natural 16-bit wraparound; this
                                       * is bit-for-bit what SEC/SBC does */
unsigned char diff_hi = (unsigned char)(diff >> 8);   /* == SCRATCH65 */
unsigned char diff_lo = (unsigned char)diff;          /* == SCRATCH64 (raw) */
int aim_ready = 0;   /* does this tick even get to swivel/fire? */

if (diff_hi == 0x00U) {
    if ((diff_lo & 0x80U) == 0U) {          /* "under", facing right way */
        scratch = (unsigned char)(diff_lo + 0x80U);
        aim_ready = 1;
    }
    /* else: idle only, no aim/fire this tick */
} else if (diff_hi == 0xFFU) {
    if ((diff_lo & 0x80U) != 0U) {          /* "get closer", facing right way */
        scratch = (unsigned char)(diff_lo + 0x80U);
        aim_ready = 1;
    }
} else if (diff_hi == 0x01U || diff_hi == 0x02U ||
           diff_hi == 0xFDU || diff_hi == 0xFEU) {
    /* "close": one page off either way -- idle only, matches original */
} else {
    compact_ids(tank_ids, &num_tanks, (unsigned char)i);
    ent_free(i);
    return;
}
```

Then gate the existing movement-decision block (`e->vx--; if (e->vx==0)
{...}`) and the aim-swivel block (`if (scratch < tank_aim_table[ang])
...`) so they only run `if (aim_ready)` — matching the original routing
everything else through `updateTankGoLeft`/`updateTankGoLeft2` (idle
walk toward base) when not ready. Need to check what the port currently
uses for that idle case (it may already have a `tank_step_right`/
`e->x -= 4` fallback that's close enough, or may need a small addition)
before finalizing — that's the one piece not fully traced yet.

## Risk and validation

This changes core combat balance/feel, not just a rendering bug, so:

- **Scope:** touches only `update_tank()`'s first ~15 lines plus gating
  the two blocks below it. Does not touch spawn logic, shell physics, or
  jet/alien AI.
- **Validate with the existing debug hooks** (`F1`/backtick debug HUD
  already shows chopper velocity/tilt; consider a similar temporary print
  of a tank's `scratch`/`diff_hi`/aim bucket while testing) rather than
  guessing from feel alone.
- **Compare before/after on a fixed scenario:** stationary chopper, one
  tank spawned at a known offset, count ticks until first shell fires and
  whether it lands on-target. This is easy to script under `/batch` with
  a debug print, without needing a human to fly.
- Since this is a from-first-principles port of the *original's* logic
  (not a novel design), the main risk is a transcription error in the
  three-way branch, not a design question — worth a careful second read
  of `choplifter.s:4236-4293` against whatever patch actually lands
  before considering it done.

## Status

Implemented in `update_tank()` (`src/m10.c:3945`), 2026-09-10. Builds
clean with `wmake` (no new warnings).

The implementation differs from the rough pseudocode above in one way:
`scratch` is now computed as a valid raw value (diff_lo, or
`diff_hi ^ 0x80` in the "close" case) in *every* non-despawn branch, not
just the `aim_ready` ones, and only gets the `+0x80` readiness bump when
`aim_ready` is true. This was needed so the idle path has a real value to
test — traced from `updateTankGoLeft`/`updateTankGoLeft2` in
`choplifter.s:4420-4427`, which use the *unadjusted* `ZP_SCRATCH64` (bit 7
only) to decide whether to step right or left while not aiming. That idle
fallback ("Need to check what the port currently uses for that idle
case," from the original writeup) is now implemented directly in
`update_tank()` as an `if (!aim_ready) { ...; return; }` block, gating
both the movement-decision and aim-swivel/fire blocks in one early
return, matching the original's control flow (`updateTankDeathCheck` →
`updateTankGoLeft`/`updateTankGoLeft2` → `rts`, never reaching
`updateTankReadyToMove`/`updateTankActionChosen`).

The pre-existing top-of-function `dying`-global short-circuit block was
left untouched, per the stated scope — it does not correspond 1:1 to the
original's `ZP_DYING` handling (which, in the `aim_ready` case, still
swivels the cannon via `updateTankSkip`/`updateTankActionChosen` while
suppressing fire, rather than returning immediately), but reconciling
that is a separate, un-scoped change.

Also fixed as a side effect of the correct three-way classification: the
despawn range was off by one on the negative side (previously despawned
at `diff_hi == 0xFD`/-3 pages, which the original's "close" case treats
as alive-but-idle; the corrected branching keeps `{0xFD, 0xFE, 0x00, 0xFF,
0x01, 0x02}` alive, matching `choplifter.s:4251-4262`).

Not yet validated against real play — needs a DOSBox-X / hardware pass
(stationary chopper, single tank at a known offset, count ticks to first
shell and whether it lands on-target) before this is considered done.
