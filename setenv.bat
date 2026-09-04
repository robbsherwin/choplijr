@echo off
rem ---------------------------------------------------------------------------
rem  Choplifter! for the IBM PCjr -- toolchain paths for one shell.
rem
rem  Where the three tools DESIGN.md section 12 calls for were installed on
rem  this machine.  Nothing here is written to the registry and the global
rem  PATH is not touched: these variables live only in the shell that calls
rem  this script, so a wrong path here cannot break anything else.
rem
rem    call setenv.bat
rem    wmake
rem
rem  If you move a tool, edit the three paths below.  build.bat calls this
rem  automatically when WATCOM is not already set.
rem ---------------------------------------------------------------------------

rem -- Open Watcom C/C++ V2, 16-bit ------------------------------------------
rem  The maintained fork (open-watcom/open-watcom-v2), not the 1.9 release.
rem  Installed from lib\repos\watcom\open-watcom-2_0-c-win-x64.exe.  Windows
rem  x64 host, cross-compiling to 16-bit DOS.  binnt64 holds the 64-bit host
rem  build of the tools, binnt the 32-bit one, binw the DOS-hosted ones; the
rem  16-bit compiler is "wcc" in all three.  The path has no spaces in it,
rem  which Open Watcom has a long history of caring about.
if "%WATCOM%"=="" set WATCOM=C:\Users\icj\AppData\Local\watcom
set INCLUDE=%WATCOM%\h
set EDPATH=%WATCOM%\eddat
set PATH=%WATCOM%\binnt64;%WATCOM%\binnt;%PATH%

rem -- NASM ------------------------------------------------------------------
rem  Two copies exist on this machine and they are the same build, 3.02:
rem  one in this checkout, one where NASM's own installer puts it
rem  (%LOCALAPPDATA%\bin\NASM, which is also where Josh Foster's
rem  pcjr-asm-game Makefile looks).  Prefer the in-tree one, so a fresh clone
rem  builds without an install step.
if exist "%~dp0lib\repos\nasm\nasm-3.02\nasm.exe" (
  set NASM=%~dp0lib\repos\nasm\nasm-3.02\nasm.exe
) else (
  set NASM=%LOCALAPPDATA%\bin\NASM\nasm.exe
)

rem -- DOSBox-X --------------------------------------------------------------
rem  Needed rather than plain DOSBox: machine=pcjr is better supported, and
rem  plain DOSBox cannot be configured down to 128 KB.  Remember that no
rem  DOSBox models the PCjr's memory contention, so timings from here are a
rem  smoke test and not a measurement -- see docs/M1.md.
if "%DOSBOX%"=="" set DOSBOX=F:\_GAMES\Wizardry 6 in 7 Engine\dosbox-x.exe

echo WATCOM = %WATCOM%
echo NASM   = %NASM%
echo DOSBOX = %DOSBOX%
