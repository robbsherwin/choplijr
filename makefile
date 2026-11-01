# Choplifter! for the IBM PCjr -- build file for M10, the game.
#
# M1-M9 (video spike through HUD/title) are no longer built: each one was
# a step toward M10, and M10 carries everything they proved.  Their
# sources stay in src\ and their write-ups in docs\ as history; git has
# the makefile rules that built them (removed 2026-11-01).
#
# For Open Watcom's wmake.  Toolchain per DESIGN.md section 12: Open Watcom
# C/C++ V2 16-bit for logic, NASM for the primitives, wlink to put them
# together, wmake to drive it.
#
#   wmake              build build\m10.exe
#   wmake clean        remove build products
#
# Do not add a run target that launches the emulator unannounced.  M10 is
# an attended visual; compile-only until someone is watching.
#
# Override any tool path on the command line, e.g.
#
#   wmake CC="C:\WATCOM\binnt64\wcc.exe"
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

M10     = $(BUILD)\m10.exe

# -0     genuine 8086/8088 code generation.  The PCjr is an 8088; anything
#        later would assemble instructions the machine does not have.
# -ms    small model.  Required, not preferred: every asm routine is reached
#        by a NEAR call, and the primitives take explicit segment/offset pairs
#        rather than far pointers so the data model cannot change the ABI.
# -ot    optimise for speed, not size.  M10's own Zen-timer overflow showed
#        the present path is CPU-bound in C (dirty-rect bookkeeping, sprite
#        dispatch, coordinate transforms), not memory-bandwidth-bound the way
#        the M1 spike (only rep stosw/movsw) was -- see CLAUDE-THOUGHTS.md
#        finding #4.  Measured +3.7% (81 vs 84 BIOS ticks) on the standard
#        DOSBox-X pad log with zero source changes; not yet confirmed on real
#        hardware.  Costs binary size (small-model DGROUP is a tight 64 KB
#        cap) -- re-check the .map file's DGROUP size after touching this.
# -w4    warnings on.  Worth reading: this code cannot be run on the machine
#        it was written on, so the compiler is the only reviewer available.
# Development builds (the default) carry two extras a player never asked
# for:
#
# -dDEBUG_KEYS  the in-play debug keys (P = teleport to the first barracks)
#        and the /debug=N test rigs.
# -dWORKSET     the WORKSET byte counters (restore/blit bytes per present,
#        printed after a flight).  The pad benchmark depends on them; a
#        build without them skips their per-blit cost.
#
# A release build drops both:
#
#   wmake clean
#   wmake RELEASE=1
#
# Clean first, as with GROUND: nothing in the dependency graph knows the
# flags changed.  The F1/` debug HUD and the /force* switches stay in every
# build; they are harmless to a player who does not ask for them.  The
# version printed on the title and in the report is CHOPLIJR_VERSION in
# src/m10.c.
!ifdef RELEASE
DEVFLAGS =
!else
DEVFLAGS = -dDEBUG_KEYS -dWORKSET
!endif
CFLAGS  = -0 -ms -ot -bt=dos -zq -w4 -i=$(SRC) -dGROUND_COLOUR=GROUND_$(GROUND) $(DEVFLAGS)

# -f obj is the OMF object format wlink reads directly (DESIGN.md section 12).
# Listings are kept because the assembly here has to be verified by reading,
# not by running.
AFLAGS  = -f obj

M10OBJS = $(BUILD)\m10.obj $(BUILD)\snd.obj $(BUILD)\sprdata_rle.obj $(BUILD)\sprdata_world.obj $(BUILD)\sprdata_host.obj $(BUILD)\sprdata_combat.obj $(BUILD)\sprdata_title.obj $(BUILD)\sprdata_moon.obj $(BUILD)\sprdata_tank.obj $(BUILD)\pcjrvid.obj $(BUILD)\prims.obj $(BUILD)\dosmem.obj $(BUILD)\blit.obj $(BUILD)\stick.obj $(BUILD)\pztimer.obj

all : $(M10) .SYMBOLIC

$(M10) : $(M10OBJS)
	$(LINK) system dos name $(M10) option quiet option stack=8192 option map=$(BUILD)\m10.map file { $(M10OBJS) }

$(BUILD)\m10.obj : $(SRC)\m10.c $(SRC)\pcjr.h $(SRC)\snd.h
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\m10.obj $(SRC)\m10.c

$(BUILD)\snd.obj : $(SRC)\snd.c $(SRC)\snd.h
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\snd.obj $(SRC)\snd.c

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

$(BUILD)\sprdata_moon.obj : $(SRC)\sprdata_moon.c
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\sprdata_moon.obj $(SRC)\sprdata_moon.c

# Generated by tools/compose_tank.py (turret + cannon composites, M10).
$(BUILD)\sprdata_tank.obj : $(SRC)\sprdata_tank.c
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\sprdata_tank.obj $(SRC)\sprdata_tank.c

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

clean : .SYMBOLIC
	@if exist $(BUILD)\*.obj del /q $(BUILD)\*.obj
	@if exist $(BUILD)\*.lst del /q $(BUILD)\*.lst
	@if exist $(BUILD)\*.map del /q $(BUILD)\*.map
	@if exist $(BUILD)\*.exe del /q $(BUILD)\*.exe
	@if exist $(BUILD)\*.log del /q $(BUILD)\*.log
