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

| | First pad | After shrink | Pre-flipped RLE | Dirty scenery | Sheared rotor RLE | bbox + sky-gradient |
|---|---:|---:|---:|---:|---:|---:|
| Log | — | — | — | `M10.LOG` | `M10-01.LOG` | `M10-02.LOG` |
| BIOS ticks | 100 | 89 | 91 | 74 | 73 | 69 |
| Sim rate (target 20) | 3.64 Hz | 4.09 Hz | 4.00 Hz | 4.92 Hz | 4.98 Hz | 5.27 Hz |
| Restore bytes avg | 1151 | 1151 | 1151 | 872 | 872 | 872 |
| RLE blit bytes avg | 1074 | 1074 | 1074 | 851 | 859 | 859 |
| Video total avg | 2545 | 2545 | 2545 | 1755 | 1763 | 1763 |
| avg mountain | 607 | 607 | 607 | 126 | 126 | 126 |
| avg scenery | 197 | 197 | 197 | 167 | 167 | 167 |
| avg sprites | 589 | 589 | 589 | 589 | 597 | 597 |
| Zen sim_frame 4 | overflow | overflow | overflow | overflow | overflow (>54 ms) | overflow (>54 ms) |

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

- Present time in microseconds (Zen overflowed every pad sample).
- A barracks / scrolling workset. `/batch` sits on the pad; that is not
  typical play.
- Sky Bayer dither as its own A/B.
- Emulator frame times (smoke test only; do not file them here).
