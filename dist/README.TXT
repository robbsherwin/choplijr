Choplifter! PCjr -- M10 test build 20261101-01, version 0.90.0
M10.EXE built 2026-11-01 on branch 20261101-01: main at 62efb4d plus
the removal of the stale root m10.exe, the M10-only makefile, and the
removal of code only M4-M9 used (the second flag frame and the old
SND_SHELL sound; M10 played neither).  Same flags as before: -0 -ms -ot,
DEBUG_KEYS and WORKSET on, so PAD.BAT and the P teleport still work.

New since the d9b5c11 cut -- check these by eye:
  - "0.90.0" shows on the title screen and at the top of the report
    (PR #44).
  - Base flag is one still red frame, no waving (PR #42).
  - Tanks sit on their ground plane, where their explosion lands
    (PR #43).
  - Fire flickers by palette cycling: explosions and barracks flames
    step yellow -> light red -> red with no redraw; the jet shadow is a
    different colour; the saucer shot glows (PR #45).
  - Hostages: no more than 5 active at once again, burning house or
    not, as in the Apple original (PR #46).
  - Retrace totals and /frames now count the retraces a tick really
    used (PR #46), so the BIOS-tick and retrace figures in step 1 may
    read differently from the 10/29 run.

Nobody has run this build yet, on hardware or in DOSBox-X.  The byte
counts in step 1 are from the d9b5c11 build.  If this build's differ,
that is a finding to report, not automatically a fault: the palette
work changed how flames are baked.

Earlier notes (d9b5c11 cut, 2026-10-30):
Built with Open Watcom V2, -0 -ms -ot, DEBUG_KEYS
and WORKSET on: the fight performance series (PRs #26-#31), the
animated-scenery restore fix, tank shell sounds, the M10RUN.LOG copy
of everything the program prints (PRs #34-#36), and the base/barracks
flicker fix (PR #39: each page redraws its scenery when the camera is
not where that page last drew it).  Full checklist:
"PCjr hardware test checklist: fight performance series" (claude.ai doc).

Files
  M10.EXE    the game, 20261101-01 build (0.90.0).  Self-contained.
  M10OLD.EXE the 2026-10-28 build (PR #24), for the before/after fight.
  PAD.BAT    pad benchmark:  M10 /ztimer /batch /frames=60 > M10-05.LOG
             PAD OTHER.LOG  writes a different log name.
  STICK.BAT  joystick raw-count check:  M10 /stick /batch /frames=60
             > M10-STK.LOG

Copy everything to J:\GAMES\CHOPLIJR\ and run from there.  Same machine
setup as every prior cut: JrConfig loaded (it is auto-detected), pages
6 and 7, stick 1 plugged in and centred.  Note the DOS TIME first.

1. Pad benchmark (unattended, about 10 s).  Run PAD M10-06.LOG.  Do not
   touch the keyboard until the prompt returns.  Pass: byte counts
   identical to the 10/29 hardware run:

                   hardware    emulator
                   10/29       d9b5c11
     restore avg      874         874
     rle blit avg     764         764
     scenery          167         167
     sprites          597         597
     mountain           0           0
     BIOS ticks        49          38 (smoke test only)

   Fewer than 20 sim ticks for 60 retraces is not a fault: overrun
   ticks now count the retraces they really used.
   "DIRTY LIST FULL" in the table means rects were dropped: report it.

2. Fight with /phases (attended), same route both times, under 15 min:
     M10OLD /phases > M10-07A.LOG
     M10 /phases > M10-07B.LOG
   Fly to a barracks (P teleports there), free hostages, fight a tank
   for about a minute, fly back, Esc.  Compare the COMBAT tables:
   idle share should fall, restore share of present should drop.

3. By eye during 07B: tank cannon stays fixed on the turret while
   scrolling; explosions and the dying chopper look as before; no
   trails in the sky or ground; no scenery left erased near sprites.

4. Leave M10 in play on the pad for 20 minutes: it must not end by
   itself.  Esc, then check TIME against a wall clock (within a
   second or two), then run PAD M10-08.LOG (same counts as step 1).

Bring back the .LOG files.
