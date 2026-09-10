# choplijr

A native IBM PCjr port of Dan Gorlin's 1982 Apple II game *Choplifter!*, built
to use the machine's 16-colour graphics, three-voice sound chip and joysticks.

Faithful recreation rather than a remake: the original's tuning constants are
used verbatim, so it plays like *Choplifter* rather than like something
inspired by it. Targets a **640 KB minimum** jrIDE-class PCjr with an RGB
monitor (typically 736 KB as the jrIDE BIOS reports), ships as a DOS `.EXE`,
written in Open Watcom C with NASM for the handful of routines where the
cycles actually live.

**[DESIGN.md](DESIGN.md) is the authoritative spec.** Read that first; it
covers the video mode, the rendering pipeline and frame budget, the palette,
the sprite format, the game model ported from the reverse-engineered original,
the memory budget, the toolchain and the milestone plan. A sibling Playdate
port (same sim, crank-driven rope, 1-bit presentation) is specified in
[DESIGN-PLAYDATE.md](DESIGN-PLAYDATE.md).

## Where things are

| Path | What |
|---|---|
| `DESIGN.md` | the PCjr design, in fifteen sections |
| `DESIGN-PLAYDATE.md` | sibling Playdate design (crank/rope, 1-bit, C SDK) |
| `docs/M1.md` | milestone M1: how to build and run it, and what its numbers do and do not prove |
| `docs/M3.md` | milestone M3: blit_rle, dirty lists, attended pass criteria |
| `docs/M4.md` | milestone M4: scrolling world, camera lead, scenery |
| `docs/M5.md` | milestone M5: flight, 11-step tilt, joystick |
| `docs/M6.md` | milestone M6: hostages, boarding, rescue counter |
| `docs/M8.md` | milestone M8: SN76496 effects, voice stealing |
| `docs/M9.md` | milestone M9: HUD bubbles, title logos, sortie banners, win/lose |
| `docs/M10.md` | milestone M10: hardware validation, optimisation, palette-effect tuning |
| `docs/TIMINGS.md` | measured M1 rates and M10 pad logs; analysis stays labeled analysis |
| `src/` | C driver and the NASM primitives |
| `makefile`, `build.bat` | the build; `wmake` is the real one, the batch file is for when it misbehaves |
| `conf/` | DOSBox-X configurations, `machine=pcjr` |
| `tools/` | the sprite extractor and its catalogue of all 128 original sprites |
| `lib/repos/` | reference material: the 6502 reverse engineering, and three PCjr codebases we borrow from |

## Building

Needs Open Watcom C/C++ V2 (16-bit) and NASM; DOSBox-X to smoke-test without
hardware. Paths for this machine are in `setenv.bat` / `setenv.ps1`. See
[docs/M1.md](docs/M1.md) for how to build, how to run attended vs `/nogfx`,
and what the numbers do and do not prove.

```
wmake            build build\m1.exe through build\m10.exe
wmake run        launch M1 in DOSBox-X as a 128 KB PCjr (attended; do not start this until someone is watching)
wmake run-batch  128 KB, /batch /nogfx, captured to build\M1.LOG
wmake clean
```

M2 through M10 are compile-only visuals until someone is watching. `build\m2.exe`
is packed `blit_mask` of the flying set. `build\m3.exe` is the sprite engine:
`blit_rle` plus per-buffer dirty lists, same bounce and pose cycle. `build\m4.exe`
is the scrolling world (camera lead, mountains, barracks, base, fence).
`build\m5.exe` is flight: original physics, 11-step tilt, joystick (arrows as
a DOSBox fallback). `build\m6.exe` is hostages: spawn, land-and-board (cap 16),
unload at the pad, K/A/R HUD. `build\m7.exe` is combat: tanks, jets, saucers,
bullets, house fires, explosion/sink, three sorties. `build\m8.exe` is that
plus SN76496 effects with voice stealing (Ctrl-S mutes). `build\m9.exe` adds
HUD bubbles, title logos, sortie-banner art, and win/lose overlays.
`build\m10.exe` is that game as the polish spike (hardware validation,
optimisation, palette effects). Do not launch DOSBox for these without an
attended visual pass. See `docs/M1.md` through `docs/M10.md`.

## Progress

`DESIGN.md` section 13 lists eleven milestones. M1 is **GO** on real hardware
(JrConfig `/V64` at 1000h, pages 6 and 7). Section 2 keeps the analysis
predictions and records the measured table from `J:\GAMES\CHOPLIJR\M1.LOG`.
Sidecar RAM is not a hardware video page. Emulator `build\M1.LOG` is not
hardware. See `docs/M1.md`.

| Milestone | State |
|---|---|
| M0 Toolchain | **in place** — Watcom, NASM 3.02 and DOSBox-X via `setenv.*` |
| M1 Video spike | **GO** on jrIDE-class hardware — pages 6 and 7, visuals judged correct, fill/copy rates in `DESIGN.md` section 2 and `docs/M1.md` |
| M2 Asset pipeline | **in place** — `blit_mask` + even/odd flying chopper set (11 side, 5 head-on, rotors), stub viewer `build\m2.exe`. DOSBox visual accepted; hardware copy of that build optional. |
| M3 Sprite engine | **accepted** on DOSBox and real PCjr — `blit_rle` + per-buffer dirty lists, viewer `build\m3.exe`. Same bounce/pose as M2. |
| M4 World | **accepted** (attended visual) — scrolling playfield, camera lead, mountain parallax, barracks/base/fence, viewer `build\m4.exe`. Owner noted the demo is slower than real gameplay; accepted for M4. |
| M5 Flight | **compiling increment** — original physics, 11-step tilt, joystick (INT 15h / port 201h), viewer `build\m5.exe`. Keyboard arrows for DOSBox. Hardware stick still to verify. |
| M6 Hostages | **compiling increment** — spawn / wave-run / board 16 / unload at pad / K·A·R HUD, viewer `build\m6.exe`. Intact barracks wait for M7 tanks. Attended visual pending. |
| M7 Combat | **compiling increment** — tanks / jets / saucers / bullets / house fire / explosion-sink / three sorties, viewer `build\m7.exe`. Attended visual pending. |
| M8 Sound | **compiling increment** — SN76496 effects with voice stealing, viewer `build\m8.exe`. Ctrl-S mutes. Attended audio pending. |
| M9 Presentation | **compiling increment** — HUD bubbles, title logos, sortie banners, win/lose, viewer `build\m9.exe`. Attended visual pending. |
| M10 Polish | **compiling increment** — five pad logs; dirty-scenery restamp **4.92 Hz**; sheared rotor RLE sprites 589→597, restore unchanged, Zen still overflowed. |

Section 2's ~1500 / ~3500 / ~4600 ns/byte lines are still analysis. The
measured video `stosw` is 2803 ns/byte (44.8 ms/screen). Do not treat an
emulator timing as a hardware figure.
