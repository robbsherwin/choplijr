# ---------------------------------------------------------------------------
#  Choplifter! for the IBM PCjr -- toolchain paths for one PowerShell session.
#
#  The PowerShell twin of setenv.bat.  Same three tools, same reasoning: the
#  variables are set for this session only, the registry is not touched and
#  the global PATH is left alone.
#
#    . .\setenv.ps1
#    wmake NASM=$env:NASM
#
#  Note the leading dot: without it the script runs in a child scope and the
#  variables vanish when it returns.
#
#  wmake itself is a DOS-descended tool and does not like PowerShell's quoting
#  of arguments containing '=' in every version, so the plain
#
#    cmd /c "wmake"
#
#  after sourcing this is the reliable way to drive the build from PowerShell.
# ---------------------------------------------------------------------------

$repo = Split-Path -Parent $MyInvocation.MyCommand.Path

# -- Open Watcom C/C++ V2, 16-bit -------------------------------------------
# The maintained fork (open-watcom/open-watcom-v2), not the 1.9 release.
# Installed from lib\repos\watcom\open-watcom-2_0-c-win-x64.exe: Windows x64
# host, cross-compiling to 16-bit DOS.  No spaces in the path, which Open
# Watcom has a long history of caring about.
$env:WATCOM  = 'C:\Users\icj\AppData\Local\watcom'
$env:INCLUDE = "$env:WATCOM\h"
$env:EDPATH  = "$env:WATCOM\eddat"
$env:PATH    = "$env:WATCOM\binnt64;$env:WATCOM\binnt;$env:PATH"

# -- NASM --------------------------------------------------------------------
# Prefer the copy in this checkout so a fresh clone builds without installing
# anything; fall back to where NASM's own installer puts it, which is also
# where Josh Foster's pcjr-asm-game Makefile looks.  Both are 3.02.
$nasm_local = Join-Path $repo 'lib\repos\nasm\nasm-3.02\nasm.exe'
if (Test-Path $nasm_local) {
    $env:NASM = $nasm_local
} else {
    $env:NASM = "$env:LOCALAPPDATA\bin\NASM\nasm.exe"
}

# -- DOSBox-X ----------------------------------------------------------------
# machine=pcjr is better supported here than in plain DOSBox, and plain DOSBox
# cannot be configured down to 128 KB.  No DOSBox models the PCjr's memory
# contention, so timings from it are a smoke test only -- see docs/M1.md.
$env:DOSBOX = 'F:\_GAMES\Wizardry 6 in 7 Engine\dosbox-x.exe'

Write-Output "WATCOM = $env:WATCOM"
Write-Output "NASM   = $env:NASM"
Write-Output "DOSBOX = $env:DOSBOX"
