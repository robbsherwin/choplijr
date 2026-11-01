@echo off
rem ---------------------------------------------------------------------------
rem  Choplifter! for the IBM PCjr -- M10 build, without wmake.
rem
rem  The makefile is the real build.  This exists because wmake, NASM and
rem  wlink can each be missing independently, and when something does not work
rem  it is useful to have a version that runs the three commands in the open
rem  where you can see exactly what was invoked and what it said.
rem
rem  Usage:  build            build build\m10.exe
rem          build clean      delete build products
rem
rem  Tool paths come from setenv.bat, which this script calls if the caller
rem  has not already set WATCOM.  CC, NASM and LINKER can each be overridden
rem  in the environment first.
rem
rem  GROUND=BROWN or GROUND=PINK selects the ground colour (DESIGN.md
rem  section 5); RELEASE=1 drops DEBUG_KEYS and WORKSET, as `wmake
rem  RELEASE=1` does.  This script always rebuilds everything, so no clean
rem  is needed when changing either.  Compiler flags match the makefile.
rem ---------------------------------------------------------------------------

setlocal

if "%1"=="clean" goto clean

rem -- tool locations -------------------------------------------------------
rem  setenv.bat records where Open Watcom, NASM and DOSBox-X were found on
rem  this machine and sets WATCOM, INCLUDE, PATH, NASM and DOSBOX for this
rem  shell only.  Nothing here touches the global PATH.  Skipped if the caller
rem  has already set things up, so `set NASM=... & build` still works.
if exist "%~dp0setenv.bat" if "%WATCOM%"=="" call "%~dp0setenv.bat" >nul

if "%CC%"==""   set CC=wcc
if "%NASM%"==""  set NASM=nasm
if "%LINKER%"=="" set LINKER=wlink

rem -- ground colour, DESIGN.md section 5 -----------------------------------
rem  Brown and pink/violet are both supported; the choice is still open, so
rem  it is a build-time constant.  `build clean` first -- nothing here knows
rem  GROUND changed.
if "%GROUND%"=="" set GROUND=BROWN
if /i not "%GROUND%"=="BROWN" if /i not "%GROUND%"=="PINK" (
  echo ERROR: GROUND must be BROWN or PINK, not "%GROUND%".
  goto fail
)
if "%RELEASE%"=="" (set DEVFLAGS=-dDEBUG_KEYS -dWORKSET) else (set DEVFLAGS=)

rem -- tool check, so a missing tool is reported rather than half a build -----
where %CC% >nul 2>nul
if errorlevel 1 (
  echo ERROR: Open Watcom's 16-bit C compiler "%CC%" is not on the PATH.
  echo        Install Open Watcom C/C^+^+ V2 and run its owsetenv.bat, or set
  echo        CC to its full path.
  goto fail
)
rem  setenv.bat gives NASM as a full path, which `where` cannot look up.
if exist "%NASM%" goto nasm_ok
where %NASM% >nul 2>nul
if errorlevel 1 (
  echo ERROR: NASM "%NASM%" is not on the PATH.
  echo        Set NASM to its full path, e.g.
  echo          set NASM=%%LOCALAPPDATA%%\bin\NASM\nasm.exe
  goto fail
)
:nasm_ok
where %LINKER% >nul 2>nul
if errorlevel 1 (
  echo ERROR: Open Watcom's linker "%LINKER%" is not on the PATH.
  goto fail
)

if not exist build mkdir build

rem -- C: 8086 code generation, small model, DOS target ----------------------
echo [cc ]  ground colour %GROUND%, flags: %DEVFLAGS%
for %%F in (m10 snd sprdata_rle sprdata_world sprdata_host sprdata_combat sprdata_title sprdata_moon sprdata_tank) do (
  echo [cc ]  src\%%F.c
  %CC% -0 -ms -ot -bt=dos -zq -w4 -i=src -dGROUND_COLOUR=GROUND_%GROUND% %DEVFLAGS% ^
    -fo=build\%%F.obj src\%%F.c
  if errorlevel 1 goto fail
)

rem -- NASM: OMF objects that wlink reads directly ---------------------------
for %%F in (pcjrvid prims dosmem blit stick pztimer) do (
  echo [asm]  src\asm\%%F.asm
  %NASM% -f obj -l build\%%F.lst -o build\%%F.obj src\asm\%%F.asm
  if errorlevel 1 goto fail
)

echo [link] build\m10.exe
%LINKER% system dos name build\m10.exe option quiet option stack=8192 ^
  option map=build\m10.map ^
  file { build\m10.obj build\snd.obj build\sprdata_rle.obj build\sprdata_world.obj build\sprdata_host.obj build\sprdata_combat.obj build\sprdata_title.obj build\sprdata_moon.obj build\sprdata_tank.obj build\pcjrvid.obj build\prims.obj build\dosmem.obj build\blit.obj build\stick.obj build\pztimer.obj }
if errorlevel 1 goto fail

echo.
echo Built build\m10.exe
dir /b build\m10.exe
endlocal
exit /b 0

:clean
if exist build\*.obj del /q build\*.obj
if exist build\*.lst del /q build\*.lst
if exist build\*.map del /q build\*.map
if exist build\*.exe del /q build\*.exe
echo Cleaned.
endlocal
exit /b 0

:fail
echo.
echo Build failed.
endlocal
exit /b 1
