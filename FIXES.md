# Things to fix — code review of the M10 build

Scope: a read-through of `src/m10.c`, `src/snd.c`, `src/pcjr.h` and every file
under `src/asm/`, plus the sprite headers in `src/sprdata_*.c` where a claim
depended on a sprite's size. Nothing here was built or run: this review was done
on a Linux box with no Open Watcom, NASM or DOSBox-X, and per `AGENTS.md`
nothing was installed. So every item is a reading of the source. Where a cycle
or byte figure appears it is **analysis from 8088 instruction timings, not a
measurement**, and is labelled as such. The pad benchmark and real hardware
remain the judge.

Each item gives: what you would see, why, where, and a fix direction. Items are
ordered by how much they matter, highest first, within each section.

---

## 1. Bugs and glitches

### 1.1 Pressing SPACE on a logo screen hangs the game (needs reboot) — high confidence

**Symptom.** On any of the four logo screens, tap SPACE to skip. The screen
never advances and no key, including Esc, gets you out.

**Cause.** `kbd_is_down()` returns `kbd[] | kbd_pulse[]`, and `kbd_pulse[]` is
latched by every make code until somebody calls `kbd_clear_pulse()`
(`src/asm/stick.asm`, `kbd_apply`). `wait_title_hold()` (`src/m10.c:5782`)
waits for release with

```c
while (kbd_is_down(SCAN_SPACE))
    vid_wait_retrace();
```

and never clears the pulse, so after the make arrives the loop condition can
never go false again, whatever the key does. The same construct guards the
Esc/SPACE entry wait at the top of that function.

`main()` has the same pattern at `src/m10.c:6520`:
`while (kbd_is_down(SCAN_SPACE)) ;`. The pulse was cleared inside `run_title()`
just before it returned, so this one only hangs if a *second* make arrives while
the key is held, i.e. typematic repeat. DOSBox-X generates repeated makes for a
held key, so holding SPACE for half a second on "Press SPACE to play" should
reproduce it there. Whether the PCjr BIOS re-issues INT 48h/INT 9 for typematic
is worth checking on hardware; if it does, the hang is reachable there too.

**Fix.** In release-wait loops, poll `kbd_held()` (make/break array only), or
call `kbd_clear_pulse()` inside the loop the way `run_calibrate()`'s Esc
drain does (`src/m10.c:6361`). Audit every `while (kbd_is_down(...))` for the
same thing.

### 1.2 Intact barracks vanish while hovering/landed near a burning one — high confidence

**Symptom.** Camera static (hovering in the lead-window deadzone or landed),
a burning house anywhere in the level (house 3 burns from sortie 1, so this is
"always"), an unburnt house on screen. Every 4 sim ticks the unburnt house and
its sill disappear and stay gone until the camera next scrolls or a sprite
happens to pass over them.

**Cause.** The fire-flip branch in `run_viewer()` (`src/m10.c:5987-6014`)
restores the **whole** of `scenery_mark[back]` whenever the fire bit changes.
That list holds every house, the fence and the base from the last full draw.
`draw_scenery(full = 0, …)` then runs, and `draw_houses()`'s *unburnt* branch
redraws only on `full` or a hit against the sprite dirty list — it has no
`fire_changed` term (`src/m10.c`, `draw_houses`, the `else` branch). The
burning houses, fence and base were given a `fire_changed` redraw to patch
exactly this effect on the pad; the unburnt houses were missed.

`docs/M10.md` item 7 describes this fix as restoring "only the tracked flame
footprint". The code restores the entire scenery list. That is also why the
pad benchmark's restore bytes rose 872 → 1089: the base, pad, pole, flag and
all five fence towers are erased and redrawn every 4 ticks for the whole game,
even when no burning house is on screen (`any_house_burning()` is global).

**Fix (also the top performance item, see 2.1).** Track the flame blits in
their own tiny list (`flame_mark[2]`, 4 rects max) and restore only that on a
fire flip; drop the `fire_changed` redraw from `draw_fence()`/`draw_base()`,
since nothing of theirs was erased any more; gate the whole thing on a burning
house actually being on screen. If you prefer the one-line patch first: add
`|| fire_changed` to the unburnt-house condition so the symptom goes away, then
do the proper fix for the cost.

### 1.3 Joystick never calibrates if the stick was not live at SPACE — high confidence

**Symptom.** Plug the stick in after the title, or get a dead read during the
8-sample rest capture, and the axes do nothing for the rest of the run
(buttons still work). The comment in `read_controls()` promises "a stick
plugged in later is still found", but the mapping never comes alive.

**Cause.** `capture_stick_rest()` sets `calib_n = 8U` unconditionally
(`src/m10.c:6426`) even when `n0 == 0` and `stick_cx0`/`stick_cy0` stay 0.
The in-flight fallback at `src/m10.c:3185` is `if (live && calib_n < 8U)`, so
it can never run on the attended path. With `center == 0`,
`apple_span_from_raw()` returns `APPLE_CENTER` for every raw value.

**Fix.** Only set `calib_n = 8U` when `n0 != 0`; otherwise leave it 0 so the
in-flight average takes over on the first eight live samples.

### 1.4 Dead joystick probe stalls the game with interrupts off — medium confidence on the hardware behaviour, high on the code path

**Symptom.** Title left on the default "Joystick" with nothing plugged in
(a keyboard player who never pressed K): the first eight sim ticks each stall,
then there is a hitch once a second forever. The sim's retrace counting misses
frames during each stall and the BIOS clock loses ticks.

**Cause.** `stick_read_port` (`src/asm/stick.asm`) runs the Paku loop under
`CLI` to `STICK_TIMEOUT = 0x7FFF` iterations when the axis bits never clear.
`read_controls()` probes every tick for `STICK_DEAD_PROBES = 8` dead probes
and then every `SIM_HZ = 20` ticks (`src/m10.c:3182`). The code's own comment
agrees on what a dead read costs. `capture_stick_rest()` does eight more
unconditional reads at SPACE, and `print_stick_probe()` one more at startup.

*Analysis, not measurement:* the loop body is about 12 instruction bytes and
one `IN`, bus-bound on an 8088 at roughly 50 cycles per iteration, so a
timed-out axis read is on the order of 1.5 M cycles, about a third of a
second at 4.77 MHz, with interrupts masked. Measure it before trusting the
number; the real PCjr may read the unconnected axis bits low, in which case
the loop exits at once and this item is moot. `CLAUDE-THOUGHTS.md` finding 6
already flagged the marginal-connector variant of this.

**Fix options.** (a) When the title is on Joystick and the first probe is
dead, stop probing until a key or button says otherwise, or switch the
default to Keyboard for that run. (b) Lower the timeout: a real pot on the
558 times out in about a millisecond, which is a count in the low hundreds,
so a timeout of 0x0FFF is still far above any live reading and cuts the dead
cost by eight. `STICK_LIVE_MAX` and `apple_span_from_raw()`'s fallback both
key off the timeout constant, so change both files together as the comment
says. (c) Never probe inside `run_calibrate()` more than once per frame when
the stick is dead; today it is once per loop iteration.

### 1.5 Sprites pop out when their top row reaches the HUD, rather than clipping — medium confidence

**Symptom.** A climbing jet (or an explosion, missile or saucer up high)
disappears the instant its top row hits row 7, while 4-5 rows of it would
still be visible under the HUD band.

**Cause.** `blit_at()` rejects the whole sprite at `src/m10.c:2191` when
`y < HUD_ROWS`. Jets climb indefinitely until the X cull: in `update_jet()`'s
`fast_climb` state the `jet_yv3` records add +1 to both `y` and `ground`
every tick, so `world_to_sy(y + ground)` goes negative well before the jet
leaves the screen sideways.

**Fix.** Give the RLE blitter an entry that takes an explicit row count and a
pre-advanced stream pointer (`blit_rle_m8_rows(seg, xbyte, y, sseg, soff,
rows)`), and in `blit_at()` walk the stream past `HUD_ROWS - y` rows before
calling it. The same entry fixes 1.6.

### 1.6 Bottom-edge clipping is computed but not applied on the fast path — latent, high confidence in the reading

`blit_at()` clamps `h` at `src/m10.c:2199`, then calls `blit_rle_m8` /
`blit_rle_m8_fire`, which read the height from the sprite header and draw
every row. The dirty rect is recorded with the clamped `h`. Rows 200-203 land
in the unused gap between the two 8 KB banks (harmless); row 204 onward wraps
into the odd bank's rows 1, 3, 5… at the left edge, i.e. into the HUD, and
nothing ever restores it.

Checked against current geometry (hostages, tanks, ground explosions with a
tank's plane as `ground`, rubble, fence, sill, debris): nothing reaches row
200 today, so this is a trap rather than a live glitch. Any new ground sprite
taller than its world Y, or a `TANK_GROUND_BIAS` tweak, turns it live. Fix
together with 1.5 by passing `h` to the blitter.

### 1.7 Chopper rubble is drawn for every ground explosion — verify against the original

`init_explosion()` (`src/m10.c:3445`) turns any explosion with `y == 0` into
`ET_SINK`, and `draw_ents()` blits `chop_rubble_e` on `ET_SINK` frames 3-4
(`src/m10.c:4753`). Tank shells landing in the dirt, bombs, missiles and
destroyed tanks all have `y == 0`, so each shows the 8x8 grey
`chopperRubble_00` blob for two frames. `DESIGN.md` lists type 2 as "sinking
chopper". If the rubble is meant only for the player's wreck, key it off
`dying`/`crash_fx` (the `spawn_ground_crash()` entity) rather than off the
entity type.

### 1.8 Sound output mux is left pointing at the SN76496 on exit — medium confidence

`snd_gate_on()` (`src/snd.c:107`) ORs 0x60 into port 61h so the SN76496
reaches the speaker. Nothing in `cleanup()` or `snd_silence()` puts the
original bits back, so after the game DOS/BIOS beeps that go through the 8253
path are silent until another program or a reboot resets the mux. Save the
port 61h value in `snd_init()` and restore it in `cleanup()`.

### 1.9 Debug HUD `sprintf` buffer is exactly full — low severity

`draw_hud()` formats into `char buf[36]` (`src/m10.c:1431`). With velx at its
clamp (`X-5376`), tilt `T-5`, `Y-10`, `A15.1/1`, the string is 35 characters
plus the terminator: 36 bytes, no slack. It only holds because `vely`'s high
byte is bounded by `accel_y_table`. Make it `buf[48]`; it costs 12 bytes of
DGROUP (see 3.1 for where to find them).

### 1.10 House-ignition sound depends on which code path lit the house — low

A bullet that hits the barracks footprint from `check_bullet()`
(`src/m10.c:3914`) is turned into a boom *before* `ordinance_check()` runs,
so `ignite_from_ent()`'s `if (ents[ei].type == ET_BULLET) snd_play(SND_HOUSE)`
(`src/m10.c:3726`) never fires; a bullet that reaches the dirt via
`update_bullet()` calls `ordinance_check()` first and does play it. Pass an
explicit "player bullet" flag instead of testing the (already mutated) type.

### 1.11 Tank "shell still in flight" check can alias a reused slot — low

`update_tank()` decides a shell is still arcing with
`ents[live].type == ET_SHELL` (`src/m10.c:4160`). If that shell slot is freed
and immediately reallocated to another tank's shell, the first tank waits on
somebody else's shell. Store a generation counter with the slot, or clear
`tank_burst_shot[]` when the shell explodes (in `init_explosion()` via a
reverse lookup, or by having `update_shell()` clear it).

### 1.12 `dirty_add()` drops rects silently when a list is full — hardening

`DIRTY_MAX = 96` (`src/m10.c:2016`). Counting worst cases (4 tanks x 3 blits,
4 jets, 4 saucers x 2, 16 other entities, chopper x 3, 16 hostages, a banner)
gives about 60, so it does not overflow today, but a dropped rect is a
permanent ghost with no diagnostic. Add a counter that `print_workset()`
reports, or at minimum a debug `printf` after the run when it ever hit.

---

## 2. Performance

The hardware phase tables (`docs/TIMINGS.md`, `M10-05.LOG`) say `present` is
about 60% of the frame and, once the camera scrolls, `restore` is the biggest
slice of it. Items here go after restore first, then the blitter's fixed
per-row cost, then DGROUP, then small change. All cycle figures are analysis.

### 2.1 Stop restoring and redrawing the whole scenery every 4 ticks (same root as 1.2)

With any house burning, which is all of a normal game, the fire-flip branch
erases and redraws the base, pad, flagpole, flag and all five fence towers
every 4 sim ticks on the pad, and every house in view elsewhere. That is the
872 → 1089 restore-byte jump the pad log recorded, plus the matching re-blit
bytes. Restoring only the flame rects (two 8x5 blits per burning house, in
their own list) and gating on "burning house on screen" takes that back to
near zero on the pad and makes 1.2 impossible by construction. This is the one
item here that is both a visible bug and a measured cost.

### 2.2 `blit_rle_m8` / `_fire`: per-row `CALL`, and a `PUSH`/`POP DX` the row never needed

`src/asm/blit.asm` drives each row through `call .row`, and `.row` begins with
`push dx` and ends with `pop dx` — but the plain `.row` never touches DX
(the fire variant needs its `push bx` for the `xlatb` base, not the DX pair).
Per row that is a near call, a return and two stack operations, about 6
instruction bytes plus 8 bytes of stack traffic, which on a bus-bound 8088
with the PCjr's wait states is on the order of 70-90 cycles. A present blits
a few hundred sprite rows, so this is a few thousand cycles a frame: around
1-2% of a 60 ms frame by analysis. Inline the row body into the even/odd
walk (jump back to the row-advance label instead of `ret`) and delete the
DX save. Measure with the pad log before and after; byte counts must be
identical.

While in there, the skip/run pair can be fetched with one `lodsw` (AL = skip,
AH = run; `test ax,ax` is the end-of-row test) instead of two `lodsb` and a
`mov`.

### 2.3 Replace `MUL` in the five row-address prologues with shifts

`blit_mask_m8`, `blit_rle_m8`, `blit_rle_m8_fire`, `fill_rect_m8` and
`fill_rows_m8` all compute `(y >> 1) * 80` with `mul bx`. `MUL r16` is
118-133 cycles on an 8088; `80 = 64 + 16`, so `mov bx,ax ; shl ax,1 ; shl
ax,1 ; add ax,bx` (×5) then four `shl ax,1` (×16) is about 20 cycles. Roughly
100 cycles saved per call, and there are 50-100 such calls a present
(every blit, every restore band, every HUD fill). Small, safe, no DGROUP cost.

### 2.4 Pack `dirty_rect` into bytes: 1.5 KB of DGROUP back, and less traffic in every restore and hit-test loop

`dirty_rect` is four `unsigned` (8 bytes) at `src/m10.c:355`. Every field
fits a byte: `xbyte < 80`, `y < 200`, `wbytes <= 80`, `rows <= 200`.
`lists[2]` and `scenery_mark[2]` are 4 x (2 + 96 x 8) = 3080 bytes; as bytes
they are 1544. The DGROUP margin is 528 bytes (`docs/M10.md` item 6), the
palette-effect work still has to land, and a 98-byte addition once crashed
the binary (`CLAUDE-THOUGHTS.md`, DGROUP entry). This single change takes the
margin from about 0.5 KB to about 2 KB and halves the bytes `restore_list()`, `dirty_bbox_calc()` and
`dirty_hits_box()` walk. Widen to `unsigned` at the point of use inside
`restore_rect()`; nothing else needs to change.

### 2.5 The `CLI` joystick timeout (see 1.4)

If the hardware does time out on an unconnected axis, every dead probe is a
third of a second by analysis. Fixing 1.4 is also the biggest single frame-
time item in this list for a keyboard player on the Joystick default.

### 2.6 Small things, each a few hundred cycles a frame at most

- `draw_stars_and_moon()`: two `%` by 160 per present plus one per star
  (`src/m10.c:1727`, `1731`), and `% 3`, `% 4`, `% 5` in `draw_chopper()` and
  `draw_ents()` (`src/m10.c:5636`, `4762`, `4774`). Each is a `DIV`. A
  conditional subtract (`if (v >= 160) v -= 160`) or a 3/4/5-entry wrap
  counter does the same work in a few cycles.
- `fence_tower_x()` (`src/m10.c:5357`) does its arithmetic in `long`. The
  value range (`FENCE_X - scroll_x - 0x8C` is between about -290 and +3640,
  doubled still fits) is 16-bit; dropping to `int` removes the long shifts and
  masks, called five times a present.
- `blit_at()` calls `data_seg()` on every blit (`src/m10.c:2221`, `2223`).
  Cache it in a static at startup.
- `dirty_bbox_calc()` over `lists[back]` is computed in `draw_stars_and_moon()`
  and again in `draw_scenery()` for the same list. Compute once in
  `run_viewer()` and pass the pointer.
- The WORKSET counters (`ws_t_restore += …` in `restore_rect()`,
  `ws_add_blit()` per blit, the 32-bit min/max in `ws_end_tick()`) run in
  every build. They are cheap, but `/phases` already shows how to make
  diagnostics opt-in; `/noworkset` could be the default with `/workset` to
  turn it on.
- `hud_draw_bubble()` still plots rows 1 and 6 pixel by pixel. With `x0`
  even, pixels `x0+2 … x0+21` are ten whole bytes: one `fill_rect_m8` plus
  two `plot_px` per row. Rare (HUD repaints on change only), so last.

### 2.7 Worth testing, not obviously a win

Opaque runs are `REP MOVSB`. On an 8088 `MOVSW` moves two bytes in 25 cycles
against `MOVSB`'s 17 per byte, so an aligned even-length run would be about
30% cheaper per byte — but sprite runs here are mostly 2-9 bytes and the
parity test plus odd lead/tail byte eats most of that. Only worth it if the
pad log shows the blitter's run loop dominating after 2.2 and 2.3.

---

## 3. Cleanup

### 3.1 DGROUP relief beyond 2.4

- `opt_retrace` (`src/m10.c:138`) is the `W202` warning in `m10.err`: defined,
  never referenced. Delete it.
- `stick_read_both` in `src/asm/stick.asm` has no caller (code segment, so no
  DGROUP, but it is dead weight and a maintenance trap).
- `rle_run_bytes()` is now only reached from the clipped path; fine to keep,
  but its comment still describes the old per-blit re-walk.
- The `PASS by eye` paragraph and `usage()` text in `main()` are DGROUP
  literals. Every line trimmed there is a byte for the palette work.

### 3.2 Debug keys live in the release binary

`P` teleports to the first barracks, F1/`` ` `` swaps in the debug HUD, and
`/forceclip`, `/forcefire`, `/noworkset` are documented in `usage()`. Fine for
a beta; gate them behind a `#ifdef DEBUG_KEYS` before a release build so a
stray P mid-sortie cannot move the player.

### 3.3 Doc drift

- `docs/M10.md` item 7 says the fire-flip fix restores "only the tracked flame
  footprint"; the code restores the whole `scenery_mark` list (1.2 / 2.1).
  Update the doc when the code is fixed, either way.
- `src/m10.c`'s header comment still mentions mountain parallax
  ("ridge tiles"); the band was removed in M10.

### 3.4 Keyboard hook notes (no change needed, worth knowing)

- `new_int9` empties the BIOS type-ahead buffer on every key, so every
  `while (kbhit()) getch();` in the title code is a no-op while hooked.
  Harmless, but it means those drains are not doing what they look like.
- On a real PCjr both `new_int48` and `new_int9` apply the same key, the first
  with the raw NMI scan code and the second with BIOS's translated one. For
  the keys the game uses they are identical; a key whose raw and translated
  codes differ would leave a phantom entry in `kbd[]`. If a key ever "sticks"
  on hardware, look here first.

---

## 4. Suggested order

1. 1.1 (pulse hang) — a one-line fix per loop, and it is a hard hang.
2. 1.2 + 2.1 (flame list) — visible bug and a measured cost in one change.
3. 1.3 (calibration) — one `if`.
4. 2.4 (byte `dirty_rect`) — buys the DGROUP room everything else needs.
5. 1.4 / 2.5 (joystick timeout) — decide after one hardware measurement of an
   unconnected read.
6. 2.2 + 2.3 (blitter prologue/row loop) — pure assembly, verify byte-for-byte
   with the pad log.
7. 1.5 + 1.6 (clipping) — one new blitter entry, fixes both.
8. The rest as time allows.
