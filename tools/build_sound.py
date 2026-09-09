#!/usr/bin/env python3
"""Apple playSound X/Y/A -> SN76496 period N.

The original (choplifter.s playSound) is a blocking speaker-toggle loop:
X = delay between clicks (pitch), Y = click count, A = add to X each click.
There are no waveform tables.

M8 cannot stall the 20 Hz sim, so src/snd.c plays short period/atten/tick
streams.  Period N = X<<2, clamped to 1..1023.

    F = 3579545 / (32 * N)

This script prints the mapping for the call sites.  It does not rewrite
snd.c; those streams are checked in by hand so a Python bump cannot
silently retune the game.
"""

# (name, X, Y, A) from choplifter.s call sites
SITES = (
    ("fire", 0x30, 0x20, 0x01),
    ("board0", 0xFF, 0x0C, 0x00),
    ("board1", 0xE0, 0x0E, 0x00),
    ("board2", 0x98, 0x10, 0x00),
    ("rescue", 0x28, 0x28, 0x00),
    ("kill0", 0x30, 0x30, 0x00),
    ("kill1", 0x43, 0x30, 0x00),
    ("kill2", 0x61, 0x30, 0x00),
    ("kill_many0", 0x40, 0x50, 0x00),
    ("kill_many1", 0x54, 0x40, 0x00),
    ("kill_many2", 0x61, 0x30, 0x00),
    ("kill_many3", 0x83, 0x20, 0x00),
    ("start_hi", 0x90, 0x30, 0x00),
    ("start_mid", 0x70, 0x30, 0x00),
    ("start_lo", 0x50, 0x30, 0x00),
    ("start_43", 0x43, 0x30, 0x00),
    ("start_61", 0x61, 0x30, 0x00),
)


def n_from_x(x):
    n = x << 2
    if n < 1:
        n = 1
    if n > 1023:
        n = 1023
    return n


def freq_hz(n):
    return 3579545.0 / (32.0 * n)


def main():
    print("name       X    Y    A     N     F_Hz")
    for name, x, y, a in SITES:
        n = n_from_x(x)
        print(" %-10s %3d  %3d  %3d  %4d  %7.1f" % (
            name, x, y, a, n, freq_hz(n)))


if __name__ == "__main__":
    main()
