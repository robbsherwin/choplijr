# Working agreements for this repo

## Tooling: ask, don't install

Never download or install software, and never use a package manager. If something
is missing or the wrong version, **stop and report it** with the exact tool name
and version needed. The owner installs it and points you at the path. This is
faster than having an agent hunt for it, and it keeps the machine's global
environment under human control.

The same applies to a wrong-looking shell, runtime, or SDK version: say so
rather than working around it.

## Shell

Windows PowerShell 5.1. It does **not** accept `&&` as a statement separator --
use `;` between commands.

## Don't open an emulator unannounced

DOSBox-X sessions need a human in front of them: M1's visual tests are judged by
eye, and the escape hatch out of a timed loop is a keypress. Before running
anything that launches an emulator, stop and explain what the run is testing and
what a pass looks like, then wait for a go-ahead.

## Git is driven by the owner

Do not run state-mutating git commands -- no `add`, `commit`, `checkout`,
`branch`, `stash`, `push`, `reset`, or `clean` -- unless explicitly asked. There
is usually uncommitted work in the tree. Read-only commands are fine.

## Measurements are not to be invented

Never fabricate, estimate, or extrapolate a performance figure. Emulator timings
are a smoke test only: DOSBox-X does not model the PCjr Video Gate Array wait
states that slow CPU access to shared RAM in 16-colour modes, so its numbers run
optimistic. Label them as such. Real PCjr hardware is available for timings that
need to be trusted, so prefer it over emulation for anything performance-related.

Predictions in `DESIGN.md` are analysis, not measurement. Keep the distinction
visible when reporting.

## Toolchain locations

- NASM 3.02: `lib/repos/nasm/nasm-3.02/nasm.exe`
- Open Watcom 16-bit: see `setenv.bat` / `setenv.ps1` in the repo root
- `Y:/github/open-watcom-v2` is the Watcom **source tree**, not an installation

Build with `wmake NASM=lib\repos\nasm\nasm-3.02\nasm.exe`, or `build.bat` as a
fallback.
