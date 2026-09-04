# Choplifter! for the IBM PCjr -- build file for milestone M1.
#
# For Open Watcom's wmake.  Toolchain per DESIGN.md section 12: Open Watcom
# C/C++ V2 16-bit for logic, NASM for the primitives, wlink to put them
# together, wmake to drive it.
#
#   wmake              build build\m1.exe
#   wmake run          build, then launch DOSBox-X with a 128 KB PCjr config
#   wmake run-dev      as above but with a roomier machine, for quick iteration
#   wmake run-batch    128 KB PCjr, /batch /nogfx, captured to build\M1.LOG,
#                      and DOSBox-X exits by itself
#   wmake clean        remove build products
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

TARGET  = $(BUILD)\m1.exe

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

OBJS    = $(BUILD)\m1.obj $(BUILD)\pztimer.obj $(BUILD)\pcjrvid.obj $(BUILD)\prims.obj $(BUILD)\dosmem.obj

all : $(TARGET) .SYMBOLIC

$(TARGET) : $(OBJS)
	$(LINK) system dos name $(TARGET) option quiet option stack=8192 option map=$(BUILD)\m1.map file { $(OBJS) }

$(BUILD)\m1.obj : $(SRC)\m1.c $(SRC)\pcjr.h
	@if not exist $(BUILD) mkdir $(BUILD)
	$(CC) $(CFLAGS) -fo=$(BUILD)\m1.obj $(SRC)\m1.c

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

# The 128 KB config is the one that matters, because the whole question M1 asks
# is whether two video pages fit on a 128 KB machine.  Note the caveat in
# docs/M1.md: neither DOSBox nor DOSBox-X models PCjr memory contention, so
# timings from here are a smoke test, not a validation.
run : $(TARGET) .SYMBOLIC
	$(DOSBOX) -conf $(CONF)\pcjr-128k.conf

run-dev : $(TARGET) .SYMBOLIC
	$(DOSBOX) -conf $(CONF)\pcjr-dev.conf

# Unattended capture.  /nogfx because the visual test only means anything to
# somebody looking at it, and it breaks early only on a keypress -- left in a
# redirected run it draws to nobody for the whole of its duration.  The config
# ends its autoexec with `exit`, so DOSBox-X shuts down and the log is closed.
run-batch : $(TARGET) .SYMBOLIC
	$(DOSBOX) -conf $(CONF)\pcjr-128k-batch.conf
	@if exist $(BUILD)\M1.LOG type $(BUILD)\M1.LOG

clean : .SYMBOLIC
	@if exist $(BUILD)\*.obj del /q $(BUILD)\*.obj
	@if exist $(BUILD)\*.lst del /q $(BUILD)\*.lst
	@if exist $(BUILD)\*.map del /q $(BUILD)\*.map
	@if exist $(BUILD)\*.exe del /q $(BUILD)\*.exe
	@if exist $(BUILD)\*.log del /q $(BUILD)\*.log
