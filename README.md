# choplijr

A native IBM PCjr port of Dan Gorlin's 1982 Apple II game *Choplifter!*, built
to use the machine's 16-colour graphics, three-voice sound chip and joysticks.

Faithful recreation rather than a remake: the original's tuning constants are
used verbatim, so it plays like *Choplifter* rather than like something
inspired by it. Targets a 128 KB PCjr with an RGB monitor, ships as a DOS
`.EXE`, written in Open Watcom C with NASM for the handful of routines where
the cycles actually live.

**[DESIGN.md](DESIGN.md) is the authoritative spec.** Read that first; it
covers the video mode, the rendering pipeline and frame budget, the palette,
the sprite format, the game model ported from the reverse-engineered original,
the memory budget, the toolchain and the milestone plan.

## Where things are

| Path | What |
|---|---|
| `DESIGN.md` | the design, in fifteen sections |
| `docs/M1.md` | milestone M1: how to build and run it, and what its numbers do and do not prove |
| `src/` | C driver and the NASM primitives |
| `makefile`, `build.bat` | the build; `wmake` is the real one, the batch file is for when it misbehaves |
| `conf/` | DOSBox-X configurations, `machine=pcjr` |
| `tools/` | the sprite extractor and its catalogue of all 128 original sprites |
| `lib/repos/` | reference material: the 6502 reverse engineering, and three PCjr codebases we borrow from |

## Building

Needs Open Watcom C/C++ V2 (16-bit) and NASM; DOSBox-X to run it without
hardware. **None of those is installed in this checkout** — see
[docs/M1.md](docs/M1.md) for where to get them.

```
wmake            build build\m1.exe
wmake run        launch it in DOSBox-X as a 128 KB PCjr
wmake clean
```

## Progress

`DESIGN.md` section 13 lists eleven milestones. M1 is a deliberate go/no-go
gate, because two things the whole rendering design rests on have never been
tested: whether two 16 KB video pages can be had under DOS on a 128 KB
machine, and whether the machine's memory throughput is what the design
assumes. Section 2's throughput figures are analytical, derived from the PCjr
Technical Reference's six-clock bus cycle, and nobody has measured them.

| Milestone | State |
|---|---|
| M0 Toolchain | **blocked** — Watcom, NASM and DOSBox-X are not installed |
| M1 Video spike | **written, unbuilt, unmeasured** — see `docs/M1.md` |
| M2 onwards | not started |

Anything presented as a throughput number anywhere in this repository is still
a prediction. M1 exists to replace it with a measurement, and it has to be a
measurement taken on real hardware: no emulator models the PCjr's memory
contention, which is the entire reason the predicted numbers are as low as
they are.
