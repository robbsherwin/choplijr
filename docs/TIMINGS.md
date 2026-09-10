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

Latest capture: `J:\GAMES\CHOPLIJR\M10-01.LOG` (sheared rotor).
Dirty-scenery is `M10.LOG`.

| | First pad | After shrink | Pre-flipped RLE | Dirty scenery | Sheared rotor RLE |
|---|---:|---:|---:|---:|---:|
| Log | — | — | — | `M10.LOG` | `M10-01.LOG` |
| BIOS ticks | 100 | 89 | 91 | 74 | 73 |
| Sim rate (target 20) | 3.64 Hz | 4.09 Hz | 4.00 Hz | 4.92 Hz | 4.98 Hz |
| Restore bytes avg | 1151 | 1151 | 1151 | 872 | 872 |
| RLE blit bytes avg | 1074 | 1074 | 1074 | 851 | 859 |
| Video total avg | 2545 | 2545 | 2545 | 1755 | 1763 |
| avg mountain | 607 | 607 | 607 | 126 | 126 |
| avg scenery | 197 | 197 | 197 | 167 | 167 |
| avg sprites | 589 | 589 | 589 | 589 | 597 |
| Zen sim_frame 4 | overflow | overflow | overflow | overflow | overflow (>54 ms) |

73 vs 74 BIOS ticks is one PIT tick on a ~1 s sample. Do not treat
**4.98 Hz** as a speedup over **4.92 Hz**.

Dirty-scenery restamp is the cut that moved the pad workset (mountain
607→126, restore 1151→872). Sheared rotor RLE put the disc in the RLE
total (sprites 589→597); restore stayed 872.

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
