Choplifter! PCjr -- M10 test build, 2026-10-30 (late)
Built from main at d9b5c11 with Open Watcom V2, -0 -ms -ot, DEBUG_KEYS
and WORKSET on: the fight performance series (PRs #26-#31), the
animated-scenery restore fix, tank shell sounds, the M10RUN.LOG copy
of everything the program prints (PRs #34-#36), and the base/barracks
flicker fix (PR #39: each page redraws its scenery when the camera is
not where that page last drew it).  Full checklist:
"PCjr hardware test checklist: fight performance series" (claude.ai doc).

Files
  M10.EXE    the game, d9b5c11 build.  Self-contained.
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
                   10/29       this build
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
