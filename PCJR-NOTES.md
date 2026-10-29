# PCjr development notes, carried over from Choplifter!

Everything below was learned (often the hard way, on real jrIDE-class
hardware) building a native PCjr port of Choplifter. It's written for
whatever the next PCjr project is — a Super Star Trek clone was the one
in mind when this was written, hence the aside below about text mode, but
none of this is Choplifter-specific otherwise.

**One framing note before the rest:** Choplifter is real-time arcade
action, so a lot of what's below (dirty-rect redraw, double-buffered
video pages, sub-pixel sprite tricks) exists to hit a 20 Hz sim loop.
The original Super Star Trek is turn-based text. A native PCjr version
of it may not need mode 8 graphics, double buffering, or any of the
dirty-rect machinery at all — plain BIOS text mode (used successfully in
this project just for the title/menu screens, see "Text mode" below) may
be the entire video layer it needs, which sidesteps most of section 3's
complexity outright. Read the graphics sections as "here if you want a
graphical map/sensor display," not as a mandatory starting point.

## Target hardware: 640 KB is the right spec

Use a **jrIDE-class PCjr, 640 KB minimum** (typically 736 KB with a real
jrIDE sidecar) as the target, not a stock 128 KB machine. This was an
explicit, deliberate decision on Choplifter (`DESIGN.md` section 2), not
a limitation discovered late — a 128 KB stock PCjr is useful only as a
worst-case *gate test* (e.g. "can I even reserve two video pages on the
smallest possible box"), never the ship target. 640 KB is comfortably
achievable on real 2020s-era PCjr hardware via a jrIDE or equivalent IDE
sidecar, and it means you aren't fighting the small-model 64 KB `DGROUP`
cap (see below) *and* a tiny overall RAM budget at the same time.

## Reference repos worth bringing along

All of these lived in `lib/repos/` for Choplifter. Which ones are
actually relevant depends on whether the new project ends up needing PCjr
graphics/sound at all (see the text-mode note above), but this is the
full list and what each is for:

| Repo | What it's for |
|---|---|
| `jrIDE` | Brutman's jrIDE hardware spec (`jrIDE.html`). The authority on real memory layout: 128 KB motherboard + sidecar SRAM, how the jrIDE BIOS reports total RAM, how `JrConfig` reserves a video buffer. Essential if the new project touches PCjr video hardware at all. |
| `pcjr-flashparty-2018` | PCjr video mode setup, palette programming during retrace, a working Zen-timer (`pztimer.asm`), LZ4, raster IRQ tricks. |
| `pcjr-asm-game` | Dirty-rect pipeline example, bank arithmetic for a different video mode, SN76496 streaming, real-hardware timing notes. Most relevant if the new project *does* end up doing real-time graphics. |
| `jrpiano` | A working SN76496 sound driver, including the port `0x61` gating gotcha (below) and an INT 9 keyboard hook. Bring this even for a turn-based game if it wants sound effects. |
| `PAKU_16A` | Paku Paku 1.6a source — has the port `201h` joystick polling loop this project used. Only relevant if the new game wants joystick input. |
| `ChoplifterReverse` | Quinn Dunki's clean-room 6502 disassembly of the *original Apple II Choplifter*, with extensive comments. Obviously not relevant to Super Star Trek's own logic, but a good example of "how much detail a clean-room RE-and-comment pass can recover" if the new project is also porting/recreating an existing game from disassembly or an old BASIC listing. |
| `open-watcom-v2` | Open Watcom C source — useful to have local if you ever need to check a compiler quirk against its actual source rather than guessing. |
| `nasm` | NASM source. Only needed if the pinned NASM binary (see toolchain below) ever needs rebuilding. |
| `MASM5`, `watcom` (installers), `scubaventure`, `dosbox` | Lower-value/reference: old Microsoft Macro Assembler docs, Watcom installer binaries, a real commercial PCjr game disk image, and a mainline (non-X) DOSBox checkout. Kept around but not things this project actively drew from day to day. |

## Toolchain

- **Open Watcom C**, 16-bit, targeting genuine 8086/8088 code (`-0` — the
  PCjr is an 8088; never let the compiler target anything later).
- **Small model** (`-ms`) is close to mandatory for a PCjr DOS `.exe`
  this size — see the DGROUP section below for what that costs you.
- **NASM** for hot-path assembly (blitters, the Zen timer, low-level
  port/memory primitives), linked via `wlink` reading OMF (`-f obj`).
  Near calls only in small model — a NASM routine can't be reached with
  a far call from small-model C without extra plumbing.
- **`-ot`** (optimize for speed) mattered more than `-os` once profiling
  showed the hot path was CPU-bound in C (dirty-rect bookkeeping, sprite
  dispatch) rather than memory-bandwidth-bound. Re-check the `.map`
  file's DGROUP size after flipping this — it costs code size.
- **`-w4`** (warnings on) is worth it specifically because you cannot run
  the code on the machine you wrote it on to sanity-check it by eye —
  the compiler is one of the only reviewers you have before hardware.
- **DOSBox-X**, not mainline DOSBox, for day-to-day development testing:
  `machine=pcjr` is meaningfully better supported (mainline DOSBox
  supports `machine=pcjr` too, but DOSBox-X's PCjr emulation and conf
  options go further). A *plain* DOSBox download is still fine for just
  running the finished game — see this project's own `README.txt`.

## The small-model 64 KB DGROUP cap — the single biggest recurring risk

Small-model Watcom C puts **every** `static`/global variable, every
string literal, and the runtime stack into one 64 KB segment (`DGROUP`).
This project hit this wall repeatedly and it is worth budgeting for from
day one on the next one too:

- **Check the linker's `.map` file's `DGROUP` size after every feature**,
  not just when something crashes. `65536 - DGROUP size` is your real
  margin, and it can go from "comfortable" to "under 1 KB" over the
  course of a single session of small, individually-reasonable changes.
- **String literals are not free.** `printf`/usage-text/debug strings go
  into `CONST`, which is part of `DGROUP`. A handful of verbose
  multi-line help/diagnostic strings cost real, permanent bytes out of
  the same 64 KB budget as your game state. Keep printed/usage text
  terse; put the verbose explanation in a source comment instead (the
  compiler strips comments for free).
- **Sprite/asset data does *not* automatically avoid this.** Plain
  `unsigned char foo[] = {...}` initialized arrays land in `DATA`
  (part of DGROUP) same as anything else — Choplifter's sprite art
  stayed affordable purely because RLE-compressing small retro sprites
  makes the *aggregate* small (tens of KB across the whole game, not
  hundreds), not because of any special segment placement.
- **A build that compiles clean can still crash at runtime with no
  output at all** if DGROUP (plus the stack sitting right next to it)
  overflows — there was no source-level debugger available for this
  target, so the only reliable diagnosis method was comparing the
  linker `.map` file's segment sizes between a known-good build and the
  one that broke, byte-for-byte, rather than guessing.
- When you do need to recover margin, look for wins with **zero runtime
  cost** first: dead code, verbose diagnostic strings, redundant
  static tables — before trading away a feature or switching models.

## PCjr video hardware, if you need it

- **Mode 8**: 160×200, 16 colors, 2 pixels per byte (4 bits/pixel,
  packed two-to-a-byte). If the new project wants any kind of graphical
  display (a starmap, a viewscreen), this is the mode to reach for.
- **Mode-8 pixels are not square** — roughly **1.667:1** width:height on
  a real CRT (derived from a 160×200 buffer filling a standard 4:3
  screen: `(4/3)/(160/200) = 1.667`). Any art authored as "square" pixel
  data will render as a wide oval/rectangle unless you compensate —
  either enable your emulator's aspect-correction option (DOSBox-X:
  `aspect=true`) for testing, and/or size new art with narrower width
  than height by that same ~0.6 ratio so it reads correctly on real
  hardware regardless of aspect-correction settings.
- **Double buffering needs two hardware video pages, and hardware pages
  only exist in the motherboard's first 128 KB** (port `0x3DF`'s page
  fields are 3 bits, addressing physical pages 0–7 only — sidecar RAM
  above 128 KB cannot be a hardware video page no matter how much of it
  you have). On a stock 128 KB machine, DOS itself typically already
  occupies every page in that window, so a naive "find a free page"
  scan reports failure — this is not "the PCjr can't double-buffer," it
  means the machine needs to be told to leave a hole.
- **The fix is a PCjr memory manager (`JrConfig`/`JRCONFIG.SYS`)** that
  relocates the video buffer, carving a 32 KB (or 64 KB with `/V64`)
  reserve inside the first 128 KB while still leaving 640 KB+ free for
  the program above it. Detect it by looking for the resident
  `JRCONSYS` device and reading its `/V` size and start page from its
  resident data. **Never force page numbers that overlap DOS-occupied
  memory** — page 0 is the interrupt vector table and BIOS data area,
  and the PCjr keyboard is NMI-driven, so `CLI` does *not* make it safe
  to scribble over a live kernel page.
- **A real CRT and an emulator can disagree about which physical pages
  are free** and about palette-register timing — validate page
  selection logic on real hardware, not just in DOSBox-X.

## Sound: SN76496

- Three tone voices + one noise channel, register writes at port `0xC0`.
- **Port `0x61` bits 5 and 6 must be set to route the chip to the
  speaker**, or the program runs silently on real hardware while
  sounding completely normal in DOSBox — **DOSBox does not enforce this
  gate**, so this bug is invisible until you're on real hardware. If a
  new project adds sound and it "works in the emulator but not on the
  machine," check this first.
- Back-to-back `OUT`s to the chip need no software delay of their own;
  the chip's own READY pin already inserts about 42 wait states.
- Effects only, no background music, is a reasonable default if you're
  recreating an old text/terminal game — the "silence except for
  effects" convention read as period-authentic for Choplifter and would
  likely suit a 1970s-style game just as well.

## Joystick, if wanted

Port `201h`, Paku Paku 1.6a's polling loop (interrupts off, count while
the axis bits stay high, with a timeout). INT 15h AH=84h is not present
in the original PCjr BIOS and should be an explicit opt-in override, not
the default path, if you support it at all.

## Watcom C quirks worth knowing up front

- **`#pragma aux` with literal 8-bit register names (`al`/`bl`/`cl`) in a
  `parm` clause has known bugs** in this Watcom version — it can produce
  silently-wrong code generation. Always use the full 16-bit register
  slot (`ax`/`bx`/`cx`) even for a byte-sized parameter. This is exactly
  the kind of bug a source-level debugger would catch immediately and
  you won't have one, so avoid the risk rather than chase it after the
  fact.
- `#pragma aux` register-parameter-passing is a real, measurable win for
  small, extremely hot leaf functions (cuts `__cdecl` push/pop
  overhead), but is easy to over-apply — it was tried once on a
  function that turned out not to be hot, added complexity, and was
  reverted with no measured benefit. Profile first.

## Text mode (may be all you need)

BIOS text mode is available and simple: write `(char, attribute)` word
pairs directly into the text-mode video segment (`0xB800` for
CGA-compatible text modes, which the PCjr supports). Choplifter used
this for its title/menu/calibration screens — centered text, a simple
box-drawing helper, blinking/highlighted menu letters — entirely via
direct memory pokes, no BIOS teletype calls needed per character. For a
turn-based, text-heavy game like Super Star Trek, this alone might be
the whole video layer: no mode switching, no dirty-rect bookkeeping, no
double buffering, and none of the DGROUP pressure that sprite/graphics
data creates.

## DOSBox-X conf-file gotchas (for day-to-day dev testing)

- DOSBox-X **rewrites a minimal conf file into its full, ~1500-line
  fully-commented form on exit**, and folds in whatever settings were
  active at the time (notably `automount`) — regardless of whether your
  autoexec succeeded. Expect this and restore your minimal conf
  afterward rather than treating the rewrite as an edit worth keeping.
  If you share a DOSBox-X install across projects, set `automount =
  false` explicitly in every project conf, or another project's
  `automount = true` will get folded in and DOSBox-X will prompt to
  mount real host drives at startup.
- **A relative `mount` target in `[autoexec]` resolves against
  DOSBox-X's own program directory, not the shell's working
  directory.** If DOSBox-X itself lives in a different folder than your
  project (e.g. a shared/vendored copy), a relative `mount c: build`
  will look in the wrong place; use an absolute path in that case.
- `cycles = fixed N` with `core = normal` (not `dynamic`) is what gives
  a Zen-timer-style benchmark inside the emulator something meaningful
  to measure — cycle-counted, not wall-clock/dynamic-recompiled.
- **Emulator timings are a smoke test, not a hardware prediction.** No
  DOSBox variant models the PCjr's real memory contention (the Video
  Gate Array shares the bus with the CPU, so real RAM accesses average
  two wait states — a bus cycle is six clocks instead of four on real
  hardware). A frame budget that looks comfortable in DOSBox-X can be
  meaningfully tighter on the real machine. Treat emulator numbers as
  "is this change correct and roughly directionally cheaper/costlier,"
  and validate anything performance-critical on real hardware before
  trusting it.

## General hardware-validation workflow that worked well

1. Make one change at a time; rebuild everything the change could
   affect, not just the one file.
2. Check the linker `.map` file's DGROUP margin after every change that
   touches static/global state, not only when something breaks.
3. Run a deterministic, scripted batch-mode smoke test (`/batch
   /frames=N`-style flags, redirected to a log file) and diff key
   numbers (byte counts moved, tick counts) against the last known-good
   run, to catch regressions a human eyeballing the screen would miss.
4. Only after the batch smoke test passes, do an attended, interactive
   run to visually/aurally confirm the change actually looks and sounds
   right — the two checks catch different classes of bug.
5. For anything performance-sensitive, get it onto real hardware
   eventually and treat that number as authoritative over anything the
   emulator reported, per the note above.
6. Add debug/instrumentation flags as **opt-in**, off by default — this
   project once had phase-timing instrumentation running unconditionally
   on every build, which was itself a measurable, wasted per-tick cost
   in ordinary play.
7. When converting outside art (a photo, a hand-drawn asset) into game
   sprites: if the source has a flat, single-color background you want
   to key out, a **flood fill from the image border** (not a blanket
   color-key) correctly separates "background" from "an interior pixel
   that happens to share that color" (e.g. a dark shadow inside the
   subject) — this avoided needing the source art to have real alpha
   transparency at all.
