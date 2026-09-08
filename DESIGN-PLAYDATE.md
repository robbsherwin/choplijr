# Choplifter! for Playdate — Design

A sibling port of the same game specified in [DESIGN.md](DESIGN.md): Dan Gorlin's
1982 Apple II *Choplifter!*, using the original tuning constants, in original
Apple world space. The PCjr product is a 128 KB DOS `.EXE` in mode 8. This
product is a Playdate `.pdx`. They share a simulation. They do not share a
display, an input device, a sound chip, a memory budget, or a toolchain.

**Decisions already made:** faithful recreation of the *game model*; Playdate-native
presentation; the crank lowers and raises a rope that hostages can climb (this
mechanic is new — the Apple original has no rope); C for the sim and the game
loop, not Lua.

**This file does not replace DESIGN.md.** Entity tables, hostage AI, helicopter
physics, spawn cadence, world constants and scoring rules live there. Duplicate
here only what Playdate changes.

**Reference material**

| Source | What we take from it |
|---|---|
| [DESIGN.md](DESIGN.md) | Authoritative game model for both ports. |
| `lib/repos/ChoplifterReverse` | Quinn Dunki's 6502 RE. Same role as on the PCjr track: constants, AI, art. Not a source of video code. |
| `tools/extract_chopgfx.py` | Sprite catalogue (128 sprites, two blit formats, `dither_pct`). Input to a 1-bit redraw, not a converter we ship. |
| Panic SDK docs | Display, crank, audio, `pdc`. Linked where a fact is used. |
| `lib/repos/jrpiano`, Flashparty, `pcjr-asm-game` | **PCjr only.** Do not port. |

The Playdate SDK is **not in this tree yet**. Layout in section 9 is a proposal
for when the owner fetches it.

---

## 1. Purpose and relationship to the PCjr port

**Goals.** The same mission, the same 64 hostages, the same three sorties, the
same tanks/jets/saucers, the same crash rules, the same counters (killed /
aboard / rescued — still no score). It should play like *Choplifter* with one
Playdate-native verb grafted on: a crank-driven rope.

**Non-goals.** No colour. No SN76496. No DOS. No joystick. No PCjr video pages.
No attract loop (same relaxation as DESIGN.md §15). No rewriting the PCjr plan
to accommodate this port.

### Shared sim, divergent presentation

| Layer | Shared? | Notes |
|---|---|---|
| World constants, entity rules, hostage AI, physics tables | **Yes, by spec** | Verbatim from `choplifter.s` / DESIGN.md §4 and §8. |
| Rope + crank | Playdate only | Section 5. Does not exist on Apple or PCjr. |
| Rendering | No | 400×240 1-bit vs mode 8 160×200×16. |
| Input | No | D-pad + A/B + crank vs PCjr joystick. |
| Audio | No | SDK synth/samples vs SN76496. |
| Delivery | No | `.pdx` vs DOS `.EXE`. |

### Can the game logic be one portable C library?

**Recommend: two codebases, one mental model, portable sim as a later extract —
not a v1 build product.**

Playdate's C API is 32-bit ARM (device) plus a host dylib (Simulator), heap via
`playdate->system->realloc`, and a frame callback. The PCjr port is Open Watcom
16-bit real mode, small/compact model, `#pragma aux`, NASM for blitters. Those
are not the same C dialect. A file that `#include`s `pd_api.h` will never compile
with Watcom; a file full of `far` pointers and `int` as 16-bit will not compile
cleanly for the Playdate.

Trying to force a single `sim.c` through both compilers in the first milestone
will make both ports worse: the PCjr side cannot afford 32-bit `int` habits or
floating-point crank angles, and the Playdate side cannot afford 8088 calling
conventions.

What we *do* share from day one:

- The same named constants (`BOUNDS_LEFT`, `LAND_POSY`, `SCROLL_LEAD_*` after
  viewport scaling, boarding capacity 16, …).
- The same state machines described in DESIGN.md §8, plus the rope states in
  section 5 of this file.
- C as the language of the sim on **both** products, so a reader of either
  codebase is looking at the same kind of code.

**Lua vs C on Playdate.** Lua is the SDK's default and is fine for many games.
This repo already chose C for the PCjr sim because `choplifter.s` is eleven
thousand lines of bookkeeping, not of blitters. That argument is stronger on
Playdate, not weaker: the crank/rope is more state, and DESIGN.md's constants
should not be transcribed into Lua tables "for convenience." Use the C API
end-to-end (`eventHandler` + `setUpdateCallback`). Lua stays available as an
escape hatch for UI experiments; it is not the architecture.

If both ports mature, extract a `sim/` directory of `stdint.h` C with no video
and no input, and wrap it. That is a refactor with two working games, not a
prerequisite.

---

## 2. Target hardware

Playdate, current retail hardware. Official specs from Panic:

| | Playdate | PCjr (DESIGN.md §2) |
|---|---|---|
| Display | 400 × 240, 1-bit Sharp Memory LCD, 173 ppi | BIOS mode 8: 160 × 200 × 16 |
| CPU | 168 MHz Cortex-M7 | 8088, ~4.77 MHz, VGA wait states |
| RAM | 16 MB | 128 KB (two 16 KB video pages eat 32 KB) |
| Storage | 4 GB flash | Floppy / DOS |
| Input | D-pad, A, B, Menu, Lock, crank, accelerometer | Joystick + keyboard |
| Sound | Mono speaker, headphone jack, 44.1 kHz | TI SN76496 at port `0xC0` |
| Refresh (nominal) | **30 fps default, max 50 fps**; `setRefreshRate(0)` runs the update callback as soon as possible | 60 Hz CRTC; sim capped at 20 Hz |

Sources:

- Hardware list: <https://help.play.date/hardware/the-specs/>
- Screen, 173 ppi, crank-with-buttons guidance: <https://help.play.date/developer/designing-for-playdate/>
- 30 fps default / 50 fps max, crank API, audio sample rate: [Inside Playdate 3.1.1](https://sdk.play.date/3.1.1/Inside%20Playdate.html)
- C display, `getFrame` stride, crank, Windows CMake: [Inside Playdate with C 3.1.1](https://sdk.play.date/3.1.1/Inside%20Playdate%20with%20C.html)

**SoC part number.** Panic's public specs page names a 168 MHz Cortex-M7, not a
chip. Rev A units are widely identified as STM32F746; from OS 2.0 onward Panic
documents **Rev A vs Rev B** and requires C games to be built with SDK 2.0+ so
one `pdex.bin` relocates on both (<https://help.play.date/developer/os-2.0/>).
Treat "STM32F746" as a Rev A shorthand until the checked-in SDK headers or
hardware notes name the parts. Do not write Rev-A-only linker scripts.

**What this contrast means.** The PCjr design is a fight against ~795,000 byte
accesses per second of shared RAM (DESIGN.md §2 — analytical, not measured).
Playdate has two orders of magnitude more CPU clock and two orders more RAM.
The binding constraint here is **1-bit readability at 173 ppi** and **crank
ergonomics**, not page flips. We still avoid full-screen redraw every frame
because the Memory LCD updates line-by-line and battery life is real — but we
do not inherit PCjr frame-budget folklore.

**Simulation rate.** Keep the **20 Hz sim** from DESIGN.md §7 so velocity and
gravity tables stay verbatim. Drive it from a fixed timestep inside the
Playdate update callback. The display may run at the SDK default 30 fps; that
is a *present* rate, not a retuning of `ZP_ACCEL*`. Interpolating drawn
positions between sim ticks is optional polish (PD-M10), not a requirement for
feel. If calibration against the original later wants a different sim rate, it
is still one divider — same rule as the PCjr port.

No fps "budget" table appears in this document. We have not measured a Playdate,
and DESIGN.md's millisecond lines are not transferable.

---

## 3. What 1-bit does to Choplifter

The Apple original is monochrome bitmaps plus HGR artifact colour (a sign bit
and pixel parity). The PCjr port *invents* a 16-colour reading of that (DESIGN.md
§5): sky bands, olive chopper, red tanks, magenta jets, palette-cycled fire.
None of that survives here. Playdate is black, white, and the dither patterns
you choose to author.

### Readability, not palette

Panic's design guide: a reasonable minimum for a *player* sprite is around
32 × 32; the screen is physically small at 173 ppi; HUD digits should not drop
below ~10 px cap height; 8 px is a floor for anything meant to be read.
<https://help.play.date/developer/designing-for-playdate/>

Apple sprite sizes from DESIGN.md §6 / `extract_chopgfx.py`:

| Entity | Apple px | 1:1 on Playdate | Problem |
|---|---|---|---|
| Helicopter | 32 × 18 | 32 × 18 | Width hits Panic's player minimum; height is a squat silhouette. Rotor disc has to carry identity. |
| Enemy jet | 35 × 18 | 35 × 18 | Same. Banking turn is a silhouette animation, not a colour. |
| Tank | 31 × 6 | 31 × 6 | A 6-px-tall tank at 173 ppi is a speck with a stick. |
| Barracks | 27 × 11 | 27 × 11 | Building mass is OK; windows/door need a hard outline. |
| Explosion | 24 × 12 | 24 × 12 | No palette flicker. Needs extra frames or an invert flash. |
| Hostage | 9 × 11 | 9 × 11 | **Unreadable.** This is the same crisis DESIGN.md solved by widening; on Playdate we enlarge, we do not squeeze. |
| HUD digit | 6 × 7 | 6 × 7 | Below Panic's HUD floor. New font, ~10–12 px. |

PCjr widened hostages from an aspect-correct 5 px to **8 × 11** because mode 8
pixels are fat. Playdate pixels are *tiny*. Aspect-correct 1:1 is the wrong
default for anything smaller than the chopper.

**Authoring rule.** Large entities (heli, jet, barracks, base) start from the
extracted 1-bpp shapes at 1:1 or a modest upscale (see camera, below) and are
cleaned by hand for silhouette. Hostages, tanks, bullets, HUD, and the rope
are **new 1-bit art**, sized to read at arm's length on device — not in the
Simulator window, which Panic warns is larger than the hardware.

Proposed starting sizes (playtest on device; these are composition targets,
not a claim about performance):

| Entity | Playdate art | Rationale |
|---|---|---|
| Helicopter | ~40 × 24 plus rotor | Keep it the biggest moving thing. |
| Jet | ~40 × 22, 25 rotation frames | See §11. |
| Tank | ~36 × 12 | Double the Apple height so the cannon angle reads. |
| Hostage | **~16 × 20** | Must parse as a person while running and while climbing the rope. |
| HUD digits | 10–12 px cap height | Panic HUD floor. |

### Dither, explosions, distinguishing factions

The catalogue's `dither_pct` column flags Apple art that is alternating pixels
meant to read as a *solid artefact hue*. On Playdate that art will read as
texture or as noise. Flatten it to a silhouette (body black or white against
sky) the same way PCjr flattens it to a palette index.

**Do not 2×2-checker the scrolling sky or ground.** Panic documents that a
checkered pattern scrolled by one pixel inverts every frame and flashes the
Memory LCD. If we dither at all: horizontal hatches on the ground band
(scroll-invariant along X if the pattern is rows, not a 2×2), or dither only
on static HUD chrome.

Faction identity without colour:

| Role | 1-bit cue |
|---|---|
| Heli | Rotor disc + tail boom; largest airborne sprite |
| Jet | Swept silhouette, 25-frame bank; faster, higher |
| Tank | Grounded, long, cannon elevation |
| Saucer | Disk, no tail; rarer |
| Hostage | Biped, wave cycle, climbs rope |
| Barracks | Building with door; burning = extra frames, not a red palette |

Hit feedback that PCjr got from palette flash: brief `display->setInverted(1)`
on chopper death or a local white burst. Invert is a documented C API
(`playdate->display->setInverted`). Use it sparingly; it is a whole-screen
effect.

### Camera and world mapping

Simulation stays in Apple world pixels. DESIGN.md §4 constants are unchanged.
Scaling is render-time only.

Apple viewport is 280 px wide. PCjr chose 2 world px per mode-8 px, i.e. a
**320 world-px** window, and scaled `SCROLL_LEAD_L/R` by 320/280. Playdate is
400 px wide. Integer options:

| Mapping | World px visible | vs Apple 280 | vs PCjr 320 |
|---|---:|---|---|
| 1 screen px = 1 world px | 400 | 1.43× wider | 1.25× wider |
| 1 screen px = 1 world px, side panels totalling 120 px | 280 | match | narrower than PCjr |
| `display->setScale(2)` (200×120 buffer) | 200 if 1:1 | narrower, chunkier | — |

**Default: 1:1, full 400-wide playfield**, leads scaled like PCjr:

```
SCROLL_LEAD_L = 70 * 400/280 = 100
SCROLL_LEAD_R = 210 * 400/280 = 300
```

```
screen_x = world_x - scroll_x
screen_y = PLAYFIELD_BOTTOM - world_y
```

Apple Y is bottom-relative, 0–191. Playdate origin is top-left. Spare vertical
pixels: 240 − 192 = 48. Spend them on a **HUD band of 16–24 rows at the top**
(readable digits) and the rest as extra sky or a thicker ground band. The
playfield never draws through the HUD (same isolation as DESIGN.md §15).

Y mapping is 1:1 inside the 192-row Apple range so `BOUNDS_TOP`, `LAND_POSY`
and `MOUNTAIN_Y` do not need a second scale factor.

**FOV consequence.** A wider window makes incoming tanks and jets visible
sooner. That is a difficulty *decrease*. Combined with the rope (another
potential ease), this is a playtest item, not something we "fix" by shrinking
sprites. Fallback if the game feels slack: letterbox to 280 world px and put
the three counters in the side margins. That preserves Apple FOV exactly and
is a Playdate-shaped HUD. Do not do it until PD-M6/M7 can be felt.

`playdate->graphics->setDrawOffset` can implement the camera. Fine for
scenery; the HUD sprites must `setIgnoresDrawOffset` (C sprite API) so
counters do not scroll.

---

## 4. Input (flight) — then the rope

The original and the PCjr design assume a **joystick**: analog X/Y into the
acceleration tables, button 0 fire, button 1 rotate (DESIGN.md §10). Playdate
has no analog stick. The crank is reserved for the rope (section 5), not for
cyclic/collective. Mapping stick onto crank would steal the only analog axis
we have for the unique mechanic.

### Recommended scheme

Left thumb on the d-pad (flight). Right hand on the crank when the rope is in
play; on A/B when it is not.

| Control | Action | Original analog |
|---|---|---|
| D-pad left / right | Horizontal accel | Stick X → `ZP_STICKX` / `ZP_ACCELX` |
| D-pad up | Thrust | Stick Y up → `ZP_ACCELY` (0–15) |
| D-pad down | Reduce thrust / descend | Stick Y down |
| **A** | Fire | Joystick button 0 |
| **B** | Rotate / face | Joystick button 1 |
| Crank | Rope length (section 5) | *(none)* |
| Crank docked | Retract rope fully | *(none)* |
| Menu | Pause / system menu | Esc on PCjr |
| System menu item | Sound on/off | Ctrl-S |

D-pad-to-stick: treat each axis as a digital three-position stick (−1 / 0 / +1)
fed into the **same acceleration tables**. We are not inventing a new flight
model. Diagonal d-pad is valid (up-right = thrust + right). Repeat-rate and
whether held d-pad should ramp to max thrust over N sim ticks the way a
centered stick sits at rest — playtest; default is "held = full deflection on
that axis," which is harsher than analog but honest.

**A vs B with the crank.** Panic recommends pairing the crank with **B** rather
than A when a game needs one extra button, because A+crank is a stretch for a
right-handed grip
(<https://help.play.date/developer/designing-for-playdate/>). We still put
**fire on A** (primary) and **rotate on B**, matching the original's button 0/1
priority: you fire more often than you turn. During a rope rescue you are
mostly hovering (d-pad) and cranking; firing is the rare interruption. During
combat the crank should be **docked** (rope up) and the right hand free for
A/B. That is the intended posture, not a workaround.

**Accessibility.** Panic asks for a d-pad alternative to the crank. **Hold
Menu?** No — Menu is system. Use: **crank docked + B held + d-pad up/down** as
a slow rope deploy/retract, or a system-menu toggle "rope on d-pad up/down,
B is rotate only when crank out." Default off. PD-M5 implements the crank
path first; the alternative is PD-M10.

Accelerometer: unused for v1. Lock button: OS sleep, not ours.

---

## 5. Crank / rope

The Apple game loads hostages by **landing**. They run to the helicopter and
board. There is no rope, no winch, no climb. This section is a Playdate-native
addition, not a reconstruction.

**Why it belongs.** The crank is one analog axis the Apple II and the PCjr
never had. A rope is still *Choplifter*: same heli, same hostages, same
capacity, same counters, same enemies. It is not a swinging-physics toy and
not a different genre. Landing remains the bulk pickup. The rope is a second
way to board, paid for with time-on-station and a new hitbox.

### Mapping: relative delta, not absolute angle

`playdate->system->getCrankAngle` is 0–360°, zero = up, increasing clockwise
as viewed from the right edge
([C API §7.10](https://sdk.play.date/3.1.1/Inside%20Playdate%20with%20C.html)).
`getCrankChange` is degrees since last call (negative = anti-clockwise).
Angle **wraps**. Docked rest position is not "rope length zero" in angle
space.

**Absolute angle is rejected.** Wrapping would slam the rope from max to min.
Docking would jump. Players think in "reel in / pay out," which is a delta.

**Default mapping**

| Rule | Default | Playtest |
|---|---|---|
| Sense | Clockwise (forward) **pays out** (lengthens); anti-clockwise **reels in** | Swap if it feels backwards on device |
| Gain | **2° of crank → 1 world pixel** of length | Try 1° and 4°/px |
| Integration | `length += crank_change / 2`, then clamp | — |
| Docked | `isCrankDocked()` → length goes to 0 over a few sim ticks (forced retract), not a teleport if climbers are on the line — see "reeling with climbers" | Instant vs animated retract |
| Wrap | Irrelevant; we never store angle as length | — |

At 2°/px, one full turn is 180 world px, which is more than the useful drop
from `BOUNDS_TOP` (112) to `LAND_POSY` (25) = 87 px. So a hover near the
ceiling is about **half a turn** to the ground. That is the feel we want:
deliberate, not a sewing machine. If it is too slow in playtest, drop to 1°/px.

**Max length**

```
length ∈ [0, min(ROPE_MAX, hitch_world_y - LAND_POSY)]
```

`ROPE_MAX` default **96** world px (a little more than the 87 px flight column,
so the altitude clamp always wins in normal flight). The tip cannot go below
the top of the ground band. Extra clockwise crank at maximum is ignored (hard
stop, no wrap, no wind-up).

The rope is a **vertical kinematic segment** from a hitch point under the
chopper (use the same per-tilt offset idea as the rotor tables in DESIGN.md
§8 — a small lookup, not a physics constraint). No pendulum, no stretch, no
mass. If the heli translates, the hitch and tip move with it; hostages on the
rope keep their **offset from the hitch**, not a world-X of their own.

### Relationship to landing pickup

**Both systems exist. Landing is unchanged. The rope does not replace it and
does not require landing to deploy.**

| Situation | What hostages do |
|---|---|
| Heli landed, speed below crash threshold | Original AI: run to the heli, board. Rope ignored (forced length 0 while landed). |
| Heli airborne, rope tip in the ground pickup band | Waving / running hostages may treat the **tip X** as the attractor instead of the heli. |
| Heli airborne, rope up | Original: they wave; they do not board. |
| Both possible | **Landing wins.** If the heli is on the ground, never send anyone to a rope. |

Deploying only after landing would waste the crank in the air and turn the
rope into a gimmick attached to a mechanic the player already has. Replacing
landing would delete the original's central skill (touch down without crushing
anyone, sit still while tanks shoot). Side-by-side keeps the game's identity
and gives the crank a job that analog sticks never had: **hover-rescue**.

While landed, crank input does not pay out. You cannot sit on the pad with a
rope out; that state is nonsense and would confuse the crush check.

### Climb, capacity, cranking while occupied

New hostage states, in addition to DESIGN.md §8 (waving → running L/R →
boarding → aboard):

| State | Meaning |
|---|---|
| `TO_ROPE` | Running to the tip's world X (same 2 px/frame as a ground run) |
| `CLIMBING` | On the rope; Y moves toward the hitch |
| `ABOARD` | Unchanged |

**Capacity.** `aboard + climbing + (boarding from the ground)` ≤ **16**. The
original cap is "16 aboard"; climbers occupy a seat as soon as they leave the
ground. A 17th hostage at the tip stays in waving/running. They do not stack
past 16 on the line.

**Climb speed.** Default **2 world px per sim tick**, matching ground run
speed, so a climb from `LAND_POSY` to a hitch at Y=80 is 28 ticks ≈ 1.4 s at
20 Hz. Slow enough that hover-rescue is not a free "vacuum the map." Playtest
1 and 3 px/tick.

**Pickup band.** Tip is "live" when `tip_y <= LAND_POSY + ROPE_PICKUP_SLOP`
with `ROPE_PICKUP_SLOP` default **8**. Hostages more than a barracks-width
away should not all redirect; use the same sort of proximity the original uses
to start a run toward the heli (confirm exact trigger in `choplifter.s` when
implementing — DESIGN.md does not quote a pixel radius). Until that number is
read from the RE, treat "on screen and in the waving/running states, same as
landing pickup" as the working rule, then match the original's gate.

**Climb animation.** New frames: 2–4 poses (hands up, body vertical). Do not
reuse the run cycle on a rope; it will read as a bug. Face toward the heli
centerline. One hitch X for all climbers; they are stacked by offset along Y
(`hitch_y - (i+1) * CLIMB_SPACING`, `CLIMB_SPACING` default 10) so 16 bodies
do not occupy one pixel.

**Cranking while people are on the rope**

| Player action | Default rule |
|---|---|
| Reel in | Climbers **ride the rope**: each climber's offset is preserved, so their world Y rises as the hitch's distance to ground shrinks. They do not fall. When offset ≤ boarding radius (`BOARD_R` default 8), they become `ABOARD` (same as finishing a ground board). |
| Pay out | Climbers keep their offset from the hitch (they go down with the tip if you pay out past them — they stay on the line). They continue climbing at climb speed toward the hitch as well. Net: you can drop the extra slack without throwing them off. |
| Dock crank | Animated retract as "reel in" at a fixed px/tick (default 8 px/tick), still converting to aboard at `BOARD_R`. Not an instant cut. |
| Fly away horizontally | Kinematic: the whole line translates with the heli. Ground hostages in `TO_ROPE` abort back to waving if the tip leaves the pickup band. Climbers stay on until aboard, drop, or death. |
| Fly up with slack still out | Tip lifts off the pickup band; no new boarders; climbers keep climbing. |

**Partial climb + high speed.** Default: **no slip.** We are not simulating
grip. The skill is "don't get shot while the line is out," not "don't go
above 4 px/frame." If playtest shows people yo-yoing across the map with a
necklace of hostages and no risk, add a slip threshold later (open question).

### Collision, death, scoring

The original: crushed by a landing chopper; killed by ordnance; aboard count;
rescued at `DOOR_X`; killed/loaded/rescued HUD; lose at 64 dead or death on
sortie 3 (DESIGN.md §8).

| Event | Rule |
|---|---|
| Ordnance vs climber | Climber dies; `killed++`; removed from the line. Remaining climbers keep their offsets (gap closes on next tick, or leave a hole — **leave a hole** so the animation does not magically stack). |
| Ordnance vs rope segment, no climber hit | **Severs:** all climbers drop and die; length → 0. Makes the rope a liability near tanks, which is the difficulty conservation. Playtest "sever vs ignore" — see §11. |
| Tank *body* vs rope | Treat as solid: same as sever. Tanks are the reason you wanted to hover in the first place. |
| Tip vs ground | Stops at `LAND_POSY`. Not lethal. |
| Tip / line vs mountains | Mountains are a backdrop in the original (parallax strip), not a heli collider. **No new mountain collision.** |
| Crash / type-1/2 death with climbers | Climbers die (`killed++`), same as aboard hostages lost with the chopper. Confirm original aboard-loss-on-crash in the RE when implementing; DESIGN.md's death sequence is chopper-centric. |
| Land on ground hostages | Unchanged crush. Rope is already 0 while landed. |
| Unload at base | Unchanged: on the pad they run to `DOOR_X`. Rope not involved. |
| Scoring | Still not a score. Climbing is not "aboard" until `ABOARD`. HUD aboard count includes only `ABOARD`. Optional: a fourth icon for "on rope" — nice, not v1. |

### What this is not

Not a grappling hook. Not a wrecking ball. Not a way to pick up tanks. Not a
second weapon. Not in the Apple or PCjr products.

---

## 6. Rendering

DESIGN.md §7 is a PCjr dirty-rect pipeline aimed at VGA wait states and two
16 KB pages. **Do not copy it.** Playdate's display model is different.

### What the SDK actually does

Documented in [Inside Playdate with C 3.1.1](https://sdk.play.date/3.1.1/Inside%20Playdate%20with%20C.html):

- The update callback runs at the nominal refresh rate (default **30 fps**).
  Returning non-zero from the C callback asks the system to push the frame.
- `playdate->graphics->display()` flushes the working buffer; it already runs
  after the run loop, so game code rarely calls it.
- `getFrame()` returns the **working** 1-bit buffer. **Rows are 32-bit aligned:
  stride is 52 bytes** — 400 px = 50 bytes of pixels, plus 2 bytes ignored.
  MSB-first: column 0 is bit `0x80` of byte 0. (The 50-byte figure is the pixel
  payload; blitters must use 52.)
- After poking `getFrame()`, call `markUpdatedRows(start, end)` (inclusive).
  Unmarked rows are not sent to the Memory LCD.
- `getDisplayFrame()` is the *last completed* frame, useful for debug, not for
  drawing.
- `setDrawOffset` moves the drawing origin (camera).
- The **sprite system** tracks dirty rects, z-order, and optional collisions;
  `updateAndDrawSprites()` is the usual pump. `setAlwaysRedraw(1)` exists if
  dirty tracking itself is more expensive than redrawing everything — a
  documented escape hatch, not a guess about our CPU.

Lua `playdate.update` is the same cadence; we are not using it.

There is no CRT page register and no retrace `out`. There is no second 16 KB
bank. Double-buffering is the SDK's working vs display frame, not something we
allocate.

### Recommended pipeline

**Use SDK sprites for entities and HUD; use `getFrame` / `markUpdatedRows`
only if a custom blitter earns its keep.**

Rationale: 16 MB and a 168 MHz M7 are not the PCjr. The original already
thought in "erase sprite rect, draw sprite" (DESIGN.md §15 on
`eraseAllSprites`). The Playdate sprite list *is* that idea, with row-level
LCD updates underneath. Reimplementing RLE nibble blitters would be copying
PCjr lore into a machine that stores 1-bit LCDBitmaps natively.

Per update (30 Hz present, 20 Hz sim):

1. Read buttons + crank (`getButtonState`, `getCrankChange`, `isCrankDocked`).
2. Accrue sim time; step the 20 Hz world zero or more times (usually 0 or 1;
   never invent a catch-up cap — if we hitch, we hitch; measure later).
3. `setDrawOffset` from `scroll_x` (and Y if any).
4. Let sprites represent heli, rope (custom draw), hostages, tanks, jets,
   scenery. HUD sprites ignore draw offset.
5. `sprite->updateAndDrawSprites()`.
6. Return 1.

The **rope** is a custom sprite draw: a 1-px or 2-px vertical line plus a hook
at the tip. Dirty rect = hitch-to-tip bounds, which changes every crank tick.

**Sky and ground** can be a full-width background sprite or a tilemap; they are
still conceptually DESIGN.md's scroll-invariant bands. Horizontal-only camera
means a flat sky does not need to move. Mountains: a wide 1-bit strip scrolled
at parallax (original has a mountain baseline `MOUNTAIN_Y` = 29).

**Do not** enable `setAlwaysRedraw` until a Simulator "Highlight Screen
Updates" pass (Panic's own advice) shows dirty tracking misbehaving with our
sprite count. That pass is PD-M3/M4 work on hardware, not a paper budget.

### Framebuffer facts to code against

```
LCD width           400
LCD height          240
Bits per pixel      1
Pixel bytes / row   50
Row stride          52   /* 32-bit align; last 2 bytes ignored */
```

Constants `LCD_COLUMNS`, `LCD_ROWS`, `LCD_ROWSIZE` live in the SDK headers
once the SDK is in tree — use those, do not duplicate magic numbers.

---

## 7. Audio

PCjr: SN76496, three tones + noise, effects only during play, Ctrl-S mute,
optional title music (DESIGN.md §9). `jrpiano` exists to hit that chip
correctly, including port `61h` gating. **None of that ports.**

Playdate: 44,100 Hz, not user-changeable
([Inside Playdate](https://sdk.play.date/3.1.1/Inside%20Playdate.html) —
`playdate.sound.getSampleRate()`). C API: `sampleplayer`, `fileplayer`,
`synth` (waveforms, noise, envelopes, optional sample-as-synth), mixer
channels. Speaker is small and mono; Panic asks to test on device and
headphones.

**Intent that survives:** the original's *effect list* and the "no in-game BGM"
rule.

| Original cue (DESIGN.md §9) | Playdate approach |
|---|---|
| Cannon / tank shell / boarding triplet / rescue chime / death / house events / three-note start | `PDSynth` square/triangle/noise with short envelopes, tuned by ear to the original in an Apple emulator — not SN76496 register dumps |
| Explosion / crash rumble | Noise synth, longer decay |
| Title / sortie banners | Optional; if added, a short sample or sequence. Not PVM, not Flashparty `music_player.asm` |
| Mute | System menu checkbox, plus respect `getSystemVolume() == 0` |

Do not chase a 76496 emulation. Do not import `jrpiano3.asm`. Samples are
allowed where a synth is a poor match (rescue chime); keep them short. Asset
pipeline can wait until PD-M8; PD-M1 is silent.

---

## 8. Memory and assets

16 MB RAM vs ~105 KB on the PCjr (DESIGN.md §11) means **art direction is the
constraint**, not packing two video pages. The 25 jet frames that threaten the
PCjr budget are cheap here (1-bit, small rectangles). Keep them (section 11).

### Layout (proposed)

```
pd/
  CMakeLists.txt          /* SDK C example as the starting point */
  Source/                 /* pdc input */
    pdxinfo
    images/               /* PNG, 1-bit or converted by pdc */
    sounds/
  src/                    /* C: eventHandler, sim, draw, input, rope */
tools/pd_sprites.py       /* later: catalogue → Playdate-sized PNG sheets */
```

`src/` at repo root remains the **PCjr** tree. Do not mix.

### Asset flow

1. Extract reference PNGs with `tools/extract_chopgfx.py` (already designed;
   writes `tools/out/`, gitignored).
2. Author Playdate sheets by hand (or a future script) at the sizes in §3:
   1-bit, no half-tone unless it is a static HUD pattern.
3. Drop PNGs in `pd/Source/images/`. **`pdc` compiles images to `.pdi`**
   (tables to `.pdt`). Code loads with no extension or `.pdi`
   ([Inside Playdate §4](https://sdk.play.date/3.1.1/Inside%20Playdate.html)).
4. Ship the `.pdx`; do not commit baked `.pdi` if PNG sources are in tree —
   owner's call. Prefer PNG as source.

`pdc` is not a replacement for thinking about silhouettes. Garbage-in 8-bit
colour PNGs will dither poorly; author in 1-bit.

Title and sortie art from the catalogue can be redrawn at 400×240 (launcher
card is also 400×240, no transparency on the card image — SDK launcher rules).
That is presentation work (PD-M9), not a memory emergency.

---

## 9. Language, SDK, build

**Language: C.** `eventHandler` + `playdate->system->setUpdateCallback`. On
Windows the handler needs `__declspec(dllexport)` (SDK C docs §6).

**SDK: not present.** Owner fetches it. Official download: <https://play.date/dev/>
Windows is a first-class SDK host: installer, Simulator, `pdc`, CMake.
Make-based examples are **not** supported on Windows; CMake is
([Inside Playdate with C §5](https://sdk.play.date/3.1.1/Inside%20Playdate%20with%20C.html)).

**Proposed on-disk location:** `lib/repos/PlaydateSDK/` — same pattern as
NASM and the RE repos, and already covered by `.gitignore`'s `lib/repos/*`.
Alternatively wherever the installer wants, with `PLAYDATE_SDK_PATH` set.
Do not assume the folder exists until the owner says so.

Also required for a **device** C build, per Panic (owner installs, we do not):

- Visual Studio 2019 or 2022 with C tools (Simulator dylib)
- GNU Arm Embedded Toolchain `gcc-arm-none-eabi` (device `pdex.bin`)
- CMake
- `PLAYDATE_SDK_PATH` pointing at the SDK
- SDK `bin/` on `PATH` is convenient for `pdc` / `pdutil`

**Do not install any of that from this task.** When it is present, PD-M0 is
"hello `.pdx` in the Simulator."

`pdc` invocation (Lua-shaped, also used under CMake for the bundle):

```
pdc pd/Source pd/build/choplifter.pdx
```

CMake from an SDK `C_API/Examples` clone will call `pdc` for you. Device
builds need the ARM toolchain file
`%PLAYDATE_SDK_PATH%\C_API\buildsupport\arm.cmake` and SDK 2.0+ relocations
so Rev B hardware loads the binary.

**Simulator vs hardware.** Panic: the Simulator is faster and larger on a PC
monitor than the device; crank feel and 1-bit contrast are wrong there.
<https://help.play.date/developer/designing-for-playdate/>
Same project rule as DOSBox-X: Simulator is the fast loop; ship decisions
(rope gain, hostage size, jet silhouettes) wait for a real Playdate. Do not
launch the Simulator from an agent session without a go-ahead (AGENTS.md).

---

## 10. Milestones (PD-M*)

Numbered apart from PCjr M1–M11 so nobody says "M1" and means both. Exit
criteria are observable; none of them is a fabricated fps number.

| # | Milestone | Exit criteria |
|---|---|---|
| **PD-M0** | Toolchain | SDK on disk, `PLAYDATE_SDK_PATH` set, CMake produces a `.pdx`. Owner has installed what §9 lists. |
| **PD-M1** | **Crank spike** | Simulator (or device) window: 1-bit ground band, a hitch, a rope whose length follows the crank with the §5 defaults, docked crank retracts. No mode-8, no page flip. **Validates crank polarity, gain, and the 52-byte stride if we poke `getFrame`.** |
| **PD-M2** | Art path | Chopper (or stand-in) as a 1-bit LCDBitmap from PNG via `pdc`, on screen. |
| **PD-M3** | Sprites | SDK sprite list, camera offset, heli moves on d-pad without leaving trails. Highlight Screen Updates used to see dirty rows. |
| **PD-M4** | World | Scroll, scaled leads, mountains, ground, barracks, base, fence in 1-bit. |
| **PD-M5** | Flight | Original physics tables, 11-step tilt, A fire / B rotate. Still no hostages. |
| **PD-M6** | Hostages + rope | Spawn, original landing board, **and** §5 climb path. Capacity 16. Crush + ordnance kills. |
| **PD-M7** | Combat | Tanks, jets, saucers, bullets, rope sever, death and sortie cycle. |
| **PD-M8** | Sound | Synth effects; menu mute. |
| **PD-M9** | Presentation | HUD, titles, sortie banners, difficulty, win/lose, launcher card. |
| **PD-M10** | Device polish | Rope gain, hostage size, FOV/side-panel call, crank accessibility, invert-flash — on hardware. |

PD-M1 is the go/no-go for *feel*, not for memory allocation. If the crank-to-
length mapping is miserable, we change §5 before building AI on it. If the
SDK is missing, PD-M0 is blocked the same way PCjr M0 is blocked on Watcom.

PCjr M1 and PD-M1 can proceed in parallel. They do not share binaries.

---

## 11. Risks and open questions

| Risk | Severity | Mitigation |
|---|---|---|
| Rope makes the game easier (hover out of tank range, vacuum hostages) | **High** | Keep landing as the fast path; slow climb; rope is a hitbox (sever default); wider FOV called out in §3. Playtest PD-M7 before adding slip physics. |
| Crank + d-pad + fire is an awkward grip | **High** | Intended postures in §4; Panic's B-with-crank note. Playtest on device, not Simulator. |
| Hostages unreadable at 16×20 (or still huge vs world) | Medium | Same class of risk as PCjr 8×11. Review as soon as PD-M2/M6 can show them on hardware. |
| 1-bit jets: 25 frames vs fewer | Medium | **Keep all 25.** Argument below. |
| Sharing sim with Watcom too early | Medium | §1: don't. Copy constants; extract `sim/` later. |
| Simulator lies about speed and pixel size | Medium | Panic's own warning; AGENTS.md emulator rule. |
| C binary Rev A vs Rev B | Medium | Build with SDK 2.0+ CMake/ARM file so `pdc` emits relocations. |
| 20 Hz sim on a 30 fps callback feels stepped | Low | Interpolate in PD-M10; do not retune tables to 30 Hz. |
| Sever-on-tank too punishing / too weak | Low | Flag in playtest; default is sever. |

### Jet frames on 1-bit

DESIGN.md §15 keeps all 25 `jetMaster` frames because the bank is the
signature animation and the PCjr memory math still fitted. On Playdate the
memory argument is gone. The remaining question is **readability**.

Runtime rotation of a 1-bit jet will sparkle (Panic: pre-transform, don't
`drawRotated` for detailed sprites). Twenty-five authored silhouettes are
cheap and are the only way the bank reads as a bank rather than as a noisy
smear. Dropping to 8 frames would save nothing we care about and would make
jets look cheaper than the chopper's 11 tilts. **Keep 25. Revisit only if
authoring time, not RAM, becomes the bottleneck.**

### Rope vs original difficulty

Levers, in order we should try them if PD-M7 is too easy:

1. Climb speed 2 → 1 px/tick.
2. Sever on any ordnance vs the segment (already default).
3. Jets spawn while you hover (they already do at level 1+; do not nerf).
4. Narrow the camera to 280 world px (side HUD).
5. Last resort: slip at high VX.

Do not speed up tanks or change `CURR_LEVEL` caps to "balance the rope." That
would be a different game on both ports.

### Still open (need the SDK or a Playdate)

- Exact `LCD_ROWSIZE` / row-bit order: documented as 52 and MSB-first; confirm
  against `pd_api_gfx.h` when the SDK lands.
- Whether `setUpdateCallback` at 30 fps with an internal 20 Hz step wants
  `setRefreshRate(20)` instead. Default is leave present at 30; decide in
  PD-M5 by looking at tilt animation, not by guessing milliseconds.
- Hostage-to-heli attraction radius in `choplifter.s` (not copied into
  DESIGN.md). Match it for `TO_ROPE`.
- Aboard hostages on chopper crash: confirm kill accounting in the RE.
- Chip IDs for Rev A/B in SDK notes vs community names.

---

## 12. What we are not doing

- Colour, IRGB palettes, palette cycling, mid-frame palette IRQs.
- BIOS mode 8, port `0x3DF` page flips, `B800` windows, DOS memory shrinking.
- SN76496, port `0xC0`, port `0x61` speaker gate, `jrpiano`.
- PCjr joystick adapter, `int 15h` / port `0x201`.
- A DOS `.EXE`, Watcom in the Playdate tree, NASM blitters for nibble RLE.
- Changing DESIGN.md, `src/`, the PCjr makefile, or M1 to "support" this port.
- Claiming the original had a rope.
- Invented blit rates, crank latencies, or frame budgets. When PD-M1 runs,
  label Simulator timings as Simulator timings.

The two products can live in one git repo. They should still feel like two
machines' ideas of the same game.
