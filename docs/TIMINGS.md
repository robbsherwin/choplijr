# Hardware timings

Measured numbers from the real PCjr. Analysis stays analysis. Emulator
clocks are a smoke test only — DOSBox-X does not model Video Gate Array
wait states, so it runs optimistic. Do not invent, estimate, or
extrapolate a figure into this file.

**Machine (every row below):** BIOS `FDh`, 736 KB, JrConfig `/V64` at
`1000h`, pages **6 and 7**, RGB. Live `J:\GAMES\CHOPLIJR\M1.LOG` is gone
(an M10 pad report overwrote it; no copy). M1 rates are the GO
transcription in `DESIGN.md` section 2.

---

## M1 throughput (GO)

Zen timer, mode 8, writes to the hidden page. Average of 8 runs.
`ns/byte` and `ms/screen` are arithmetic on the timed average.

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

`stosb` has no derived line: the log's byte count for that row is not
quoted. Sidecar `stosw` matches the section 2 prediction (~1500 ns/byte).
Video `stosw` does not (2803). A 16,000-byte video fill is 44.8 ms of a
50 ms 20 Hz frame.

The working set is counted at `fill_rect_m8` **2935 ns/byte** (restore)
and RAM→video `movsb` **4515 ns/byte** (`blit_rle` opaque + mixed bytes).

---

## M10 pad present

Same command every cut: `M10 /ztimer /batch /frames=60`, chopper on the
pad, 0 scrolling, stick 1 live, no keys during the sample. 20 sim ticks /
60 retraces. Sim target is 20 Hz. `/ztimer` wraps one present (restore +
draw, not the HUD) at sim_frame 4.

Latest capture: `docs/claude-logs/M10-02.LOG` (dirty-rect bbox pre-check +
sky-gradient precompute, see `CLAUDE-THOUGHTS.md`). Machine this round:
**736 KB**, JrConfig auto-detected (`JRCONSYS` at `09C4:0000`, 64 KB at
`1000h`), pages 6/7 — same pair as every prior cut, found automatically
rather than falling back to the `/V64` guess. Sheared rotor RLE (previous
latest) is `J:\GAMES\CHOPLIJR\M10-01.LOG`; dirty-scenery is `M10.LOG`.

| | First pad | After shrink | Pre-flipped RLE | Dirty scenery | Sheared rotor RLE | bbox + sky-gradient | `-ot` | HUD fill_rect |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Log | — | — | — | `M10.LOG` | `M10-01.LOG` | `M10-02.LOG` | `M10-03.LOG` | `M10-04.LOG` |
| BIOS ticks | 100 | 89 | 91 | 74 | 73 | 69 | 61 | 56 |
| Sim rate (target 20) | 3.64 Hz | 4.09 Hz | 4.00 Hz | 4.92 Hz | 4.98 Hz | 5.27 Hz | 5.97 Hz | 6.50 Hz |
| Restore bytes avg | 1151 | 1151 | 1151 | 872 | 872 | 872 | 872 | 872 |
| RLE blit bytes avg | 1074 | 1074 | 1074 | 851 | 859 | 859 | 859 | 859 |
| Video total avg | 2545 | 2545 | 2545 | 1755 | 1763 | 1763 | 1763 | 1763 |
| avg mountain | 607 | 607 | 607 | 126 | 126 | 126 | 126 | 126 |
| avg scenery | 197 | 197 | 197 | 167 | 167 | 167 | 167 | 167 |
| avg sprites | 589 | 589 | 589 | 589 | 597 | 597 | 597 | 597 |
| Zen sim_frame 4 | overflow | overflow | overflow | overflow | overflow (>54 ms) | overflow (>54 ms) | overflow (>54 ms) | overflow (>54 ms) |

`hud_draw_bubble` via `fill_rect_m8` (`CLAUDE-THOUGHTS.md` finding #7):
**61→56 BIOS ticks, 5.97→6.50 Hz, +8.9% on real hardware**; `hud` phase
9→4 BIOS ticks. Byte counts identical to `M10-03.LOG`. See the phase
breakdown section below for the full before/after.

`-ot` (`makefile` `CFLAGS`, replacing `-os`; `CLAUDE-THOUGHTS.md` finding #4):
**69→61 BIOS ticks, 5.27→5.97 Hz, +13.3% on real hardware** — bigger than
the DOSBox-X smoke test's +3.7% prediction. Every byte count (restore, RLE,
mountain, scenery, sprites) is identical to `M10-02.LOG`: same pixels, only
the compiled code changed. Zen `sim_frame 4` still overflows with the same
byte counts (968/830/1798) as every prior cut.

### Phase breakdown (`M10-03.LOG`, first capture with `/phases`)

Same command plus `/phases`: `M10 /ztimer /phases /batch /frames=60`. Sums
BIOS ticks over the whole 20-sim-tick run, not per-tick averages. This is
the first hardware evidence of where the frame budget actually goes,
answering `CLAUDE-THOUGHTS.md`'s open question of whether the cost is in
`sim_tick` (physics/AI) or `present` (draw).

| Phase | BIOS ticks | ms | Share of run |
|---|---:|---:|---:|
| `sim_tick` | 6 | 330 | 9.8% |
| `present` | 40 | 2197 | 65.6% |
| `flip` | 5 | 275 | 8.2% |
| `idle` | 10 | 549 | 16.4% |
| **sum** | **61** | | matches `run_bios_ticks` 61 |

`present`'s own five sub-phases:

| Sub-phase | BIOS ticks | ms | Share of `present` |
|---|---:|---:|---:|
| `sprites` | 15 | 824 | 37.5% |
| `restore` | 9 | 494 | 22.5% |
| `hud` | 9 | 494 | 22.5% |
| `scenery` | 5 | 275 | 12.5% |
| `mountain` | 2 | 110 | 5.0% |
| **sub-sum** | **40** | | matches `present` 40 |

`present` is nearly two-thirds of the frame; `sim_tick` (all game logic —
physics, controls, hostage/tank/jet AI) is under 10%. This confirms
`CLAUDE-THOUGHTS.md`'s central hypothesis (CPU-bound C in the draw path, not
game logic) with a real hardware number for the first time.

`sprites` being the largest single sub-phase supports finding #5's
`__cdecl` per-call-overhead hypothesis for the hot blit path. `hud` tying
`restore` at 22.5% each is a new, unexplained data point: this is a static
pad scene where the hostage/kill/rescue counters never change, and
`draw_hud()` (`src/m10.c:1403`) already has a per-page dirty check meant to
skip the redraw once both video pages have painted the counters once — so
9 BIOS ticks (494 ms) of HUD cost over a 20-tick static run is either two
genuinely expensive first-draws (plausible) or the dirty check not
skipping as often as it should (not yet distinguished; needs a tick-by-tick
count, not just a run-summed total).

**Resolved and fixed.** The dirty check is correct (exactly 2 of the 20
ticks do a real redraw, one per video page); the cost was concentrated in
those 2 draws because `hud_draw_bubble()` plotted its 24x8 counter well one
pixel at a time via `peek_byte`/`poke_byte` far calls — the same bug class
as finding #1, just in the HUD renderer. Rewritten to use `fill_rect_m8`
(the same fast rectangle-fill dirty-rect restore already uses) for the six
of eight rows that land on byte boundaries. DOSBox-X isolation (fix
reverted vs restored, same smoke test): **`hud` 9→3 BIOS ticks (494→165 ms,
-67%), `present` 50→46, run total 69→65 (5.27→5.60 Hz)**, WORKSET byte
counts identical in both runs. See `CLAUDE-THOUGHTS.md` finding #7.

**Hardware-confirmed, `M10-04.LOG`.** Same command
(`M10 /ztimer /phases /batch /frames=60`), real PCjr, compared against
`M10-03.LOG` (the pre-fix hardware baseline, already carrying `-ot`):
**`hud` 9→4 BIOS ticks (494→220 ms), run total 61→56 (5.97→6.50 Hz,
+8.9%)**, WORKSET bytes identical (872/859/1763, mountain 126/scenery
167/sprites 597). `hud`'s cut is smaller on hardware than DOSBox-X predicted
(9→4 vs 9→3) but the same direction and order of magnitude. `sim_tick`
(6→3), `idle` (10→13) and `flip` (5→3) also moved a few ticks each; none of
those phases were touched by this fix, so treat that as ordinary jitter on
a ~1 s real-hardware sample, the same way `docs/M10.md` already treats a
one-tick PIT jitter elsewhere in this table.

**`#pragma aux` on `dirty_hits_px`/`dirty_hits_box`/`plot_m8_byte`**
(`CLAUDE-THOUGHTS.md` finding #5's follow-up): register-based argument
passing instead of `__cdecl` stack push/pop, for the three functions that
were still genuinely hot leaves (`sky_grad_mix` was excluded — finding #3
already made it cold; `blit_at`'s dispatch was excluded as too large/branchy
for hand-picked registers to be safe). Verified correct across 4 repeated
DOSBox-X runs on the static pad scene: WORKSET bytes identical
(872/859/1763, mountain 126/scenery 167/sprites 597) every time, but total
BIOS ticks landed at 65 every run — unchanged from the pre-change baseline,
with `present`/`sprites`/`sim_tick` bouncing ±1 tick run-to-run the same way
they did before this change. **No measurable win on this scene.** Kept
(free: byte-identical, zero DGROUP cost) since this static, no-scrolling,
no-combat scene has a small dirty list per finding #2 and rarely reaches
the clipped-blit fallback `plot_m8_byte` is only called from — whether it
helps in a denser scene, or on real hardware, is untested.

### Combat-window capture (`M10-05.LOG`), first real firefight data

Real PCjr, attended (`M10 /phases`, no `/batch`/`/frames`/`/ztimer`), flown
into a fight and back: 381 sim ticks, 927 BIOS ticks (7.48 Hz), 316/381
ticks (83%) classified combat (`combat_tick_p`: a tank alive and a hostage
spawned), 268/381 involved scrolling.

| phase | whole-run (927 BIOS ticks) | combat-window (733) |
|---|---:|---:|
| `sim_tick` | 75 (8.2%) | 64 (8.7%) |
| `present` | 554 (60.4%) | 429 (58.5%) |
| `flip` | 67 (7.3%) | 59 (8.0%) |
| `idle` | 221 (24.1%) | 181 (24.7%) |

**Combat does not shift the cost profile.** `sim_tick` — all of `update_tank`,
`update_hostages`, physics, collision — stays ~8% whether or not a fight is
happening, matching the static pad scene (M10-03/04: 6-10%). Three
different scenes now agree: the AI/physics C code has never been more than
about a tenth of the frame. This directly answers whether entity-AI C code
is worth converting to assembly for combat: **no, that was never where the
time goes.**

What *did* change relative to the static pad benchmark is scrolling, not
combat: `present`'s own sub-phases (`mountain`/`restore`/`scenery`/`sprites`/
`hud`, summing to 554 whole-run / 429 combat-window) shifted hard toward
`mountain` (156/554 = 28.2% here vs 5% static — parallax retile only fires
when the camera moves, and 268/381 ticks did) and `restore` (169/554 = 30.5%
vs 22.5% static — bigger dirty rects while scrolling). `hud` fell to
9/554 = 1.6% (vs 22.5% static) — the HUD fix from this session paying off
in real play even better than the static-pad number suggested, since
counters change occasionally rather than never. `sprites` (144/554 = 26.0%)
is still meaningful but no longer the single dominant sub-phase the static
scene showed (37.5%) now that scrolling gives mountain/restore a bigger
share to compete for.

If a scrolling/present-side function is worth hand-optimizing next, this
run points at the mountain-retile and dirty-rect-restore C orchestration
(`draw_mountains_dirty`, `restore_list`) around the existing assembly
primitives (`fill_rect_m8`, `blit_rle_m8`), not the entity-AI code the
combat-window question set out to test.

`sum 917 vs run_bios_ticks 927`: a ~1% gap on this 927-tick sample, smaller
proportionally than the single-digit-tick gaps already seen on the ~60-70
tick static-pad samples — consistent with ordinary unaccounted overhead
(e.g. the F1/P debug-key polling outside the phase brackets), not a defect.

73 vs 74 BIOS ticks is one PIT tick on a ~1 s sample. Do not treat
**4.98 Hz** as a speedup over **4.92 Hz**. 73→69 (**4.98→5.27 Hz**) is a
real four-tick move, not PIT jitter, and every WORKSET byte count stayed
identical to the sheared-rotor cut — same pixels, less C-level cost to draw
them. Zen still overflowed at the same sampled tick with the same byte
counts (restore 968, blit 830, video 1798); this cut did not touch whatever
is costing that present the rest of its time.

Dirty-scenery restamp is the cut that moved the pad workset (mountain
607→126, restore 1151→872). Sheared rotor RLE put the disc in the RLE
total (sprites 589→597); restore stayed 872. bbox + sky-gradient did not
change the workset at all (see `CLAUDE-THOUGHTS.md`'s dirty-rect and
sky-gradient entries) — it removed C-level cost around the same bytes,
which is a different kind of cut than the ones above it.

### Fire-remap isolation: `/forcefire` vs `/forceclip`

Same pad command, same machine, plus one of two debug-only switches added
this session (`src/m10.c`, not a play mode): `/forceclip` routes every RLE
blit through the old per-byte C fallback (`blit_rle_clip`), the path
`blit_fire` used to take for every explosion, muzzle flash, burning house
and chopper death; `/forcefire` routes every blit through the new
`blit_rle_m8_fire` (NASM, `src/asm/blit.asm`), which keeps a `rep`-free but
still-assembly `lodsb`/`xlatb`/`stosb` loop instead of falling back to C.
Neither flag touches game state — same static pad scene, same bytes, only
the routine drawing them changes. This static scene never sets `blit_fire`
itself, so the plain `M10-02.LOG` baseline above is the "fire off" row here.

| | fire off (`M10-02.LOG`) | `/forcefire` (`M10-FORC.LOG`) | `/forceclip` (`M10-CLIP.LOG`) |
|---|---:|---:|---:|
| BIOS ticks | 69 | 73 | 137 |
| Sim rate (target 20) | 5.27 Hz | 4.98 Hz | 2.65 Hz |
| vs. fire-off baseline | — | +4 ticks (+5.8%) | +68 ticks (+99%) |
| WORKSET bytes | 872/859/1763 | 872/859/1763 | 872/859/1763 |
| Zen sim_frame 4 | overflow, 968/830/1798 | overflow, 968/830/1798 | overflow, 968/830/1798 |

Real hardware, not the DOSBox-X smoke test: forcing every blit onto the old
C fallback very nearly doubles the frame's cost (+99%) for the same pixels;
the new assembly fire path costs a 5.8% tax instead. That is roughly a 17x
reduction in what a fire/explosion blit actually costs relative to a normal
one. `blit_fire` is set for every explosion, muzzle flash, burning barracks
and chopper death (`src/m10.c`, `draw_ents`) — moments this pad scene never
reaches, which is why the plain M10-01/M10-02 pad logs could never show this
gap. See `CLAUDE-THOUGHTS.md` for the emulator prediction (+3.7%/+90%) this
confirms and the reasoning for why the remap can stay in assembly at all
(it only ever recolours an already-opaque nibble, never changes what's
transparent).

### Latest log detail (`M10-01.LOG`)

20 sim ticks, 0 scrolling, 20 static.

| | min | avg | max |
|---|---:|---:|---:|
| restore | 0 | 872 | 976 |
| rle blit | 824 | 859 | 1080 |
| video total | 1399 | 1763 | 1832 |

avg mountain 126, scenery 167, sprites 597.

Zen sample (sim_frame 4): restore 968, blit 830, video 1798.
**OVERFLOW** — interval exceeded ~54 ms; the tick count is meaningless.
There is still no microsecond present time.

---

## Analysis (not a new hardware total)

M1 GO rates × latest pad averages. Do not add these into a replacement
for `DESIGN.md` section 7.

| | avg bytes | rate | product |
|---|---:|---|---:|
| restore | 872 | 2935 ns/byte (`fill_rect_m8`) | 2559 µs |
| rle blit | 859 | 4515 ns/byte (RAM→video `movsb`) | 3878 µs |

Dirty-scenery avgs at the same rates: restore 2559 µs, rle 3842 µs.

Section 7's scrolling budget (~26 ms / ~20 ms static) used a sidecar-like
fill (~1500 ns/byte). It is still analysis.

| Item (section 7) | Bytes | Cost |
|---|---:|---:|
| Dirty-rect restore | ~3,000 | ~7 ms |
| Mountain strip (scrolling only) | 320 | ~1 ms |
| Scenery re-blit (scrolling only) | ~1,500 | ~5 ms |
| Sprite blits | ~3,000 | ~8 ms |
| Logic, input, sound, palette | — | ~5 ms |
| **Total while scrolling** | | **~26 ms** |
| **Total while static** | | **~20 ms** |

---

## Not measured

- Present time in microseconds (Zen overflowed every pad sample). The
  `/phases` breakdown above answers the coarser question (which phase, at
  ~54.6 ms BIOS-tick resolution, summed over a run) but not a per-tick
  microsecond figure.
- The HUD fix (`hud_draw_bubble` via `fill_rect_m8`) on real hardware — only
  DOSBox-X-confirmed so far. See the phase breakdown section above.
- A barracks / scrolling workset. `/batch` sits on the pad; that is not
  typical play.
- Sky Bayer dither as its own A/B.
- Emulator frame times (smoke test only; do not file them here).
