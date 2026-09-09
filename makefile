# Choplifter! for the IBM PCjr -- build file for M1 (video spike), M2
# (chopper on screen), M3 (blit_rle + per-buffer dirty lists), M4
# (scrolling world), M5 (flight: physics, 11-step tilt, joystick), M6
# (hostages: spawn, board, unload, rescue counter), M7 (combat:
# tanks, jets, saucers, bullets, death and sortie cycle), M8
# (SN76496 effects with voice stealing) and M9 (HUD, title, sortie
# banners, win and lose).
#
# For Open Watcom's wmake.  Toolchain per DESIGN.md section 12: Open Watcom
# C/C++ V2 16-bit for logic, NASM for the primitives, wlink to put them
# together, wmake to drive it.
#
#   wmake              build build\m1.exe through build\m9.exe
#   wmake run          build, then launch DOSBox-X with a 128 KB PCjr config
#   wmake run-dev      as above but with a roomier machine, for quick iteration
#   wmake run-batch    128 KB PCjr, /batch /nogfx, captured to build\M1.LOG,
#                      and DOSBox-X exits by itself
#   wmake clean        remove build products
#
# Do not add a run-m2 ... run-m9 target that launches
# the emulator unannounced.  M2-M9 are attended visuals; compile-only until
# someone is watching.
#
# Override any tool path on the command line, e.g.
#
#   wmake DOSBOX="C:\Program Files\DOSBox-X\dosbox-x.exe" run
#
# Nothing here needs anything on the global PATH.  setenv.bat (cmd) and
# setenv.ps1 (PowerShell) record where the three tools were found on this
# machine and set WATCOM, INCLUDE, PATH, NASM and DOSBOX for one shell only;
# source one of them and then wmake needs no arguments.  Without that, pass
# the paths in:
#
#   wmake NASM=lib\repos\nasm\nasm-3.02\nasm.exe
#
# NASM's Windows installer does not put nasm.exe on the PATH, so that one
# almost always has to be given.  Josh Foster's pcjr-asm-game Makefile expects
# it at %LOCALAPPDATA%\bin\NASM\nasm.exe, which is where the installer puts
# it; this checkout also carries a copy at lib\repos\nasm\nasm-3.02\.

!ifndef CC
CC      = wcc
!endif
!ifndef NASM
NASM    = nasm
!endif
!ifndef LINK
LINK    = wlink
!endif
!ifndef DOSBOX
DOSBOX  = dosbox-x
!endif

# Ground colour, DESIGN.md section 5.  Brown and pink/violet are both
# supported and the choice is still open, so it is a build-time constant
# rather than a decision baked into the source:
#
#   wmake clean
#   wmake GROUND=PINK          pink/violet, as the Apple original reads
#   wmake GROUND=BROWN         brown (the default)
#
# Clean first: nothing in the dependency graph knows GROUND changed.
!ifndef GROUND
GROUND  = BROWN
!endif

BUILD   = build
SRC     = src
ASMDIR  = src\asm
CONF    = conf

M1      = $(BUILD)\m1.exe
M2      = $(BUILD)\m2.exe
M3      = $(BUILD)\m3.exe
M4      = $(BUILD)\m4.exe
M5      = $(BUILD)\m5.exe
M6      = $(BUILD)\m6.exe
M7      = $(BUILD)\m7.exe
M8      = $(BUILD)\m8.exe
M9      = $(BUILD)\m9.exe

# -0     genuine 8086/8088 code generation.  The PCjr is an 8088; anything
#        later would assemble instructions the machine does not have.
# -ms    small model.  Required, not preferred: every asm routine is reached
#        by a NEAR call, and the primitives take explicit segment/offset pairs
#        rather than far pointers so the data model cannot change the ABI.
# -os    optimise for size.  Nothing that gets timed is written in C, so there
#        is no reason to trade size for speed here.
# -w4    warnings on.  Worth reading: this code cannot be run on the machine
#        it was written on, so the compiler is the only reviewer available.
CFLAGS  = -0 -ms -os -bt=dos -zq -w4 -i=$(SRC) -dGROUND_COLOUR=GROUND_$(GROUND)

# -f obj is the OMF object format wlink reads directly (DESIGN.md section 12).
# Listings are kept because the assembly here has to be verified by reading,
# not by running.
AFLAGS  = -f obj

M1OBJS  = $(BUILD)\m1.obj $(BUILD)\pztimer.obj $(BUILD)\pcjrvid.obj $(BUILD)\prims.obj $(BUILD)\dosmem.obj
M2OBJS  = $(BUILD)\m2.obj $(BUILD)\sprdata.obj $(BUILD)\pcjrvid.obj $(BUILD)\prims.obj $(BUILD)\dosmem.obj $(BUILD)\blit.obj
M3OBJS  = $(BUILD)\m3.obj $(BUILD)\sprdata_rle.obj $(BUILD)\pcjrvid.obj $(BUILD)\prims.obj $(BUILD)\dosmem.obj $(BUILD)\blit.obj
M4OBJS  = $(BUILD)\m4.obj $(BUILD)\sprdata_rle.obj $(BUILD)\sprdata_world.obj $(BUILD)\pcjrvid.obj $(BUILD)\prims.obj $(BUILD)\dosmem.obj $(BUILD)\blit.obj
M5OBJS  = $(BUILD)\m5.obj $(BUILD)\sprdata_rle.obj $(BUILD)\sprdata_world.obj $(BUILD)\pcjrvid.obj $(BUILD)\prims.obj $(BUILD)\dosmem.obj $(BUILD)\blit.obj $(BUILD)\stick.obj
M6OBJS  = $(BUILD)\m6.obj $(BUILD)\sprdata_rle.obj $(BUILD)\sprdata_world.obj $(BUILD)\sprdata_host.obj $(BUILD)\pcjrvid.obj $(BUILD)\prims.obj $(BUILD)\dosmem.obj $(BUILD)\blit.obj $(BUILD)\stick.obj
M7OBJS  = $(BUILD)\m7.obj $(BUILD)\sprdata_rle.obj $(BUILD)\sprdata_world.obj $(BUILD)\sprdata_host.obj $(BUILD)\sprdata_combat.obj $(BUILD)\pcjrvid.obj $(BUILD)\prims.obj $(BUILD)\dosmem.obj $(BUILD)\blit.obj $(BUILD)\stick.obj
M8OBJS  = $(BUILD)\m8.obj $(BUILD)\snd.obj $(BUILD)\sprdata_rle.obj $(BUILD)\sprdata_world.obj $(BUILD)\sprdata_host.obj $(BUILD)\sprdata_combat.obj $(BUILD)\pcjrvid.obj $(BUILD)\prims.obj $(BUILD)\dosmem.obj $(BUILD)\blit.obj $(BUILD)\stick.obj
M9OBJS  = $(BUILD)\m9.obj $(BUILD)\snd.obj $(BUILD)\sprdata_rle.obj $(BUILD)\sprdata_world.obj $(BUILD)\sprdata_host.obj $(BUILD)\sprdata_combat.obj $(BUILD)\sprdata_title.obj $(BUILD)\pcjrvid.obj $(BUILD)\prims.obj $(BUILD)\dosmem.obj $(BUILD)\blit.obj $(BUILD)\stick.obj

all : $(M1) $(M2) $(M3) $(M4) $(M5) $(M6) $(M7) $(M8) $(M9) .SYMBOLIC

$(M1) : $(M1OBJS)
	$(LINK) system dos name $(M1) option quiet option stack=8192 option map=$(BUILD)\m1.map file { $(M1OBJS) }

$(M2) : $(M2OBJS)
	$(LINK) system dos name $(M2) option quiet option stack=8192 option map=$(BUILD)\m2.map file { $(M2OBJS) }

$(M3) : $(M3OBJS)
	$(LINK) system dos name $(M3) option quiet option stack=8192 option map=$(BUILD)\m3.map file { $(M3OBJS) }

$(M4) : $(M4OBJS)
	$(LINK) system dos name $(M4) option quiet option stack=8192 option map=$(BUILD)\m4.map file { $(M4OBJS) }

$(M5) : $(M5OBJS)
	$(LINK) system dos name $(M5) option quiet option stack=8192 option map=$(BUILD)\m5.map file { $(M5OBJS) }

$(M6) : $(M6OBJS)
	$(LINK) system dos name $(M6) option quiet option stack=8192 option map=$(BUILD)\m6.map file { $(M6OBJS) }

$(M7) : $(M7OBJS)
	$(LINK) system dos name $(M7) option quiet option stack=8192 option map=$(BUILD)\m7.map file { $(M7OBJS) }

$(M8) : $(M8OBJS)
	$(LINK) system dos name $(M8) option quiet option stack=8192 option map=$(BUILD)\m8.map file { $(M8OBJS) }

$(M9) : $(M9OBJS)
	$(LINK) system dos name $(M9) option quiet option stack=8192 option map=$(BUILD)\m9.map file { $(M9OBJS) }

$(BUILD)\m1.obj : $(SRC)\m1.c $(SRC)\pcjr.h
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\m1.obj $(SRC)\m1.c

$(BUILD)\m2.obj : $(SRC)\m2.c $(SRC)\pcjr.h
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\m2.obj $(SRC)\m2.c

$(BUILD)\m3.obj : $(SRC)\m3.c $(SRC)\pcjr.h
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\m3.obj $(SRC)\m3.c

$(BUILD)\m4.obj : $(SRC)\m4.c $(SRC)\pcjr.h
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\m4.obj $(SRC)\m4.c

$(BUILD)\m5.obj : $(SRC)\m5.c $(SRC)\pcjr.h
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\m5.obj $(SRC)\m5.c

$(BUILD)\m6.obj : $(SRC)\m6.c $(SRC)\pcjr.h
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\m6.obj $(SRC)\m6.c

$(BUILD)\m7.obj : $(SRC)\m7.c $(SRC)\pcjr.h
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\m7.obj $(SRC)\m7.c

$(BUILD)\m8.obj : $(SRC)\m8.c $(SRC)\pcjr.h $(SRC)\snd.h
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\m8.obj $(SRC)\m8.c

$(BUILD)\m9.obj : $(SRC)\m9.c $(SRC)\pcjr.h $(SRC)\snd.h
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\m9.obj $(SRC)\m9.c

$(BUILD)\snd.obj : $(SRC)\snd.c $(SRC)\snd.h
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\snd.obj $(SRC)\snd.c

$(BUILD)\sprdata.obj : $(SRC)\sprdata.c
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\sprdata.obj $(SRC)\sprdata.c

$(BUILD)\sprdata_rle.obj : $(SRC)\sprdata_rle.c
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\sprdata_rle.obj $(SRC)\sprdata_rle.c

$(BUILD)\sprdata_world.obj : $(SRC)\sprdata_world.c
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\sprdata_world.obj $(SRC)\sprdata_world.c

$(BUILD)\sprdata_host.obj : $(SRC)\sprdata_host.c
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\sprdata_host.obj $(SRC)\sprdata_host.c

$(BUILD)\sprdata_combat.obj : $(SRC)\sprdata_combat.c
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\sprdata_combat.obj $(SRC)\sprdata_combat.c

$(BUILD)\sprdata_title.obj : $(SRC)\sprdata_title.c
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\sprdata_title.obj $(SRC)\sprdata_title.c

$(BUILD)\pztimer.obj : $(ASMDIR)\pztimer.asm
	@if not exist $(BUILD) mkdir $(BUILD)
	$(NASM) $(AFLAGS) -l $(BUILD)\pztimer.lst -o $(BUILD)\pztimer.obj $(ASMDIR)\pztimer.asm

$(BUILD)\pcjrvid.obj : $(ASMDIR)\pcjrvid.asm
	@if not exist $(BUILD) mkdir $(BUILD)
	$(NASM) $(AFLAGS) -l $(BUILD)\pcjrvid.lst -o $(BUILD)\pcjrvid.obj $(ASMDIR)\pcjrvid.asm

$(BUILD)\prims.obj : $(ASMDIR)\prims.asm
	@if not exist $(BUILD) mkdir $(BUILD)
	$(NASM) $(AFLAGS) -l $(BUILD)\prims.lst -o $(BUILD)\prims.obj $(ASMDIR)\prims.asm

$(BUILD)\dosmem.obj : $(ASMDIR)\dosmem.asm
	@if not exist $(BUILD) mkdir $(BUILD)
	$(NASM) $(AFLAGS) -l $(BUILD)\dosmem.lst -o $(BUILD)\dosmem.obj $(ASMDIR)\dosmem.asm

$(BUILD)\blit.obj : $(ASMDIR)\blit.asm
	@if not exist $(BUILD) mkdir $(BUILD)
	$(NASM) $(AFLAGS) -l $(BUILD)\blit.lst -o $(BUILD)\blit.obj $(ASMDIR)\blit.asm

$(BUILD)\stick.obj : $(ASMDIR)\stick.asm
	@if not exist $(BUILD) mkdir $(BUILD)
	$(NASM) $(AFLAGS) -l $(BUILD)\stick.lst -o $(BUILD)\stick.obj $(ASMDIR)\stick.asm

# Product target is jrIDE-class sidecar RAM (see DESIGN.md).  run-dev is the
# closer smoke test.  run / run-batch still use the 128 KB configs as a
# historical check.  Neither DOSBox nor DOSBox-X models PCjr memory
# contention, so timings from here are a smoke test, not a validation.
run : $(M1) .SYMBOLIC
	$(DOSBOX) -conf $(CONF)\pcjr-128k.conf

run-dev : $(M1) .SYMBOLIC
	$(DOSBOX) -conf $(CONF)\pcjr-dev.conf

# Unattended capture.  /nogfx because the visual test only means anything to
# somebody looking at it, and it breaks early only on a keypress -- left in a
# redirected run it draws to nobody for the whole of its duration.  The config
# ends its autoexec with `exit`, so DOSBox-X shuts down and the log is closed.
run-batch : $(M1) .SYMBOLIC
	$(DOSBOX) -conf $(CONF)\pcjr-128k-batch.conf
	@if exist $(BUILD)\M1.LOG type $(BUILD)\M1.LOG

clean : .SYMBOLIC
	@if exist $(BUILD)\*.obj del /q $(BUILD)\*.obj
	@if exist $(BUILD)\*.lst del /q $(BUILD)\*.lst
	@if exist $(BUILD)\*.map del /q $(BUILD)\*.map
	@if exist $(BUILD)\*.exe del /q $(BUILD)\*.exe
	@if exist $(BUILD)\*.log del /q $(BUILD)\*.log
