/* m1.c -- Choplifter! for the IBM PCjr, milestone M1: the video spike.
 *
 * DESIGN.md section 13 made M1 a go/no-go gate on two assumptions.  Hardware
 * closed both: JrConfig /V64 at 1000h, pages 6 and 7, attended visuals
 * judged correct, fill/copy rates recorded in DESIGN.md section 2 from
 * J:\GAMES\CHOPLIJR\M1.LOG.  This program is still the instrument.
 *
 *   1. Two 16 KB video pages in physical 00000-1FFFF (port 0x3DF pages 0-7
 *      only) on a jrIDE-class machine, so page flipping is a single OUT.
 *      Sidecar RAM is not a hardware page.  The product path is JrConfig's
 *      video window in the first 128 KB (DEVICE=JRCONFIG.SYS /V32 or /V64,
 *      with /L on a 736 KB box).  That window is often still inside the
 *      owner-0008 system MCB -- it is not always an MCB hole.
 *
 *   2. Section 2's analysis table (~1500 / ~3500 / ~4600 ns/byte) versus
 *      what the Zen timer measures on this machine.  Those analysis figures
 *      matched sidecar RAM; video RAM is slower (2803 ns/byte stosw).
 *
 * So this program does exactly three things:
 *
 *   - sets up mode 8 and works out, by asking DOS and then by writing and
 *     reading memory, which 16 KB pages are actually ours;
 *   - flips between two of them at vertical retrace with visibly different
 *     content, so a human can confirm the flip is real and tear-free;
 *   - times the fill and copy primitives with Abrash's Zen timer and reports
 *     what it measured, labelled as measured.
 *
 * It does not draw a sprite, read a joystick or make a sound.  That is the
 * point of a gate.  M2 is the chopper viewer.
 *
 * IMPORTANT, and the reason to be careful reading the output: no number this
 * program prints is a guess.  Anything under "measured" was timed on the
 * machine it ran on.  Anything under "predicted" is quoted from DESIGN.md
 * section 2's analysis table.  Comparing the two is the milestone.
 *
 * Equally important: an emulator cannot settle question 2.  DOSBox and
 * DOSBox-X do not model the PCjr's memory contention -- the Video Gate Array
 * stealing bus cycles from the CPU is the whole reason the numbers are low --
 * so emulator timings are a smoke test that the code runs and the loops are
 * the right shape, nothing more.  Only a real PCjr is hardware.  Do not treat
 * build\M1.LOG from DOSBox-X as J:\GAMES\CHOPLIJR\M1.LOG.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>

#include "pcjr.h"

/* ------------------------------------------------------------------ options */

static int      opt_flip        = 1;    /* run the visual page-flip test */
static int      opt_bench       = 1;    /* run the benchmarks */
static int      opt_bios_flip   = 0;    /* flip via int 10h instead of OUT */
static int      opt_wait        = 1;    /* pause for keypresses */
static unsigned opt_reps        = 8;    /* timed repetitions per primitive */
static int      opt_page_a      = -1;   /* force page numbers */
static int      opt_page_b      = -1;
static int      opt_force       = 0;    /* run on a machine that is not a PCjr */

/* Frames the two visual phases run for, at 60 Hz.  These are wall-clock costs
 * paid by whoever is watching, so they are short by default and only the
 * motion phase -- the one worth staring at -- can be lengthened.  Both phases
 * also break on a keypress, which is no use at all in a redirected run: hence
 * /nogfx, which skips them outright. */
#define IDENTITY_FRAMES      360U       /* 6 s: twelve half-second swaps */
#define IDENTITY_SWAP_FRAMES  30U       /* swap the CRT page twice a second */
#define MOTION_FRAMES_DEF    480U       /* 8 s, and /frames=N for longer */

static unsigned opt_motion_frames = MOTION_FRAMES_DEF;

/* The BIOS model byte at F000:FFFE.  0xFD is the PCjr; 0xFF is the original
 * PC and also what a Tandy 1000 reports. */
#define MODEL_BYTE_SEG  0xF000U
#define MODEL_BYTE_OFF  0xFFFEU
#define MODEL_PCJR      0xFDU

/* -------------------------------------------------------------------- state */

static unsigned orig_mode;
static unsigned orig_pages;
static int      mode_changed;
static unsigned owned_block;            /* our big DOS allocation, 0 if none */

static int      page_a = -1;            /* the two video pages we settled on */
static int      page_b = -1;
static unsigned seg_a, seg_b;           /* their physical segments */
static unsigned scratch_seg;            /* plain RAM, not a video page */
static unsigned scratch_bytes;
static unsigned r2r_dst_off;            /* RAM-to-RAM copy destination */
static unsigned r2r_bytes;

static int      crt_page;               /* what the CRTC is showing right now */

/* ------------------------------------------------------------------ palette */

/* DESIGN.md section 5 assigns palette indices by role rather than by hue, and
 * for most roles it picks the IRGB colour of the same number, so the mapping
 * is nearly the identity.  Writing it out anyway, because the next milestone
 * that wants palette cycling on indices 3 and 12 will want somewhere to
 * change it -- and because the ground role is already an exception: which
 * colour register 6 holds is a build-time choice, see GROUND_COLOUR in
 * pcjr.h. */
static const unsigned char m1_palette[16] = {
     0,     /*  0  black        sky top / sprite transparency key */
     1,     /*  1  blue         sky upper band */
     2,     /*  2  green        chopper body (olive) */
     3,     /*  3  cyan         reserved for palette cycling */
     4,     /*  4  red          enemy tanks */
     5,     /*  5  magenta      enemy jets */
    GROUND_COLOUR,
            /*  6  ground       brown or pink/violet -- build-time choice */
     7,     /*  7  light grey   near mountains, base concrete, treads */
     8,     /*  8  dark grey    far mountain ridge, sprite outlines */
     9,     /*  9  light blue   sky lower band, horizon haze */
    10,     /* 10  light green  chopper highlight */
    11,     /* 11  light cyan   canopy glass, rotor blur */
    12,     /* 12  light red    reserved for palette cycling */
    13,     /* 13  light magenta alien saucer */
    14,     /* 14  yellow       ground highlight, muzzle flash, fire core */
    15      /* 15  white        rotor disc, highlights, HUD text, hostages */
};

/* ------------------------------------------------------- screen composition */

/* Straight from DESIGN.md section 4: world Y is bottom-relative, screen_y =
 * 199 - world_y, and the top eight rows are the HUD band that the play field
 * never touches.  LAND_POSY is 25 and MOUNTAIN_Y is 29. */
#define HUD_ROWS        8
#define GROUND_TOP_ROW  (199 - 25)      /* 174 */
#define MOUNTAIN_ROW    (199 - 29)      /* 170 */

/* ---------------------------------------------------------------- memory map */

typedef struct {
    unsigned mem_kb;
    unsigned psp;
    unsigned block_seg;
    unsigned block_paras;
    unsigned first_mcb;                 /* head of the MCB chain */
    unsigned arena_top;                 /* first paragraph past the last MCB */
    unsigned owned_mask;                /* pages wholly inside our allocation */
    unsigned bios_mask;                 /* pages above the arena: BIOS's video
                                         *  reserve, and therefore ours to use */
    unsigned hole_mask;                 /* pages in 0-7 that no MCB covers */
    unsigned jr_mask;                   /* pages inside JrConfig's /V window */
    unsigned jr_seg;                    /* video buffer segment, 0 if unknown */
    unsigned jr_paras;                  /* window length in paragraphs */
    unsigned jr_kb;                     /* /V size in KB, 0 if unknown */
    int      jr_found;                  /* JRCONSYS device header located */
    int      jr_sig;                    /* 5AA5h present at 2000:0200 */
    int      mcb_ok;
} mem_report;

static mem_report mem;

/* ------------------------------------------------------------- benchmarking */

enum {
    K_FILL_STOSW_WIN,
    K_FILL_STOSW_DIRECT,
    K_FILL_STOSW_RAM,
    K_FILL_STOSB_DIRECT,
    K_FILL_RECT_M8,
    K_COPY_MOVSW_R2V,
    K_COPY_MOVSB_R2V,
    K_COPY_MOVSW_V2V,
    K_COPY_MOVSW_R2R,
    K_COUNT
};

#define ST_OK           0
#define ST_OVERFLOW     1
#define ST_SKIPPED      2

typedef struct {
    const char   *name;
    const char   *target;
    unsigned      want_bytes;           /* what we asked for */
    unsigned      bytes;                /* what we could actually time */
    unsigned long us_min;
    unsigned long us_avg;
    unsigned      reps;
    int           status;
    const char   *skip_why;
} bench_result;

static bench_result results[K_COUNT];

/* The precision timer wraps at about 54.9 ms, so a primitive that is slower
 * than predicted can run off the end of it.  A full-screen MOVSB copy is
 * predicted at 73.7 ms and would overflow even if the prediction is right, so
 * the harness starts each primitive at a size chosen to fit and halves it if
 * the timer wraps anyway.  When the size ends up below a full screen the
 * per-screen figure is extrapolated from the measured rate, and the report
 * says so. */
#define BENCH_FLOOR_BYTES  512U

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

/* JrConfig 3.10 (lib/repos/jrIDE/JRCONFIG.SYS, JRCONFIG.DOC).  The resident
 * character device is named JRCONSYS.  Offsets are from that header:
 *   +72h  word  video start paragraph (computed at init; /S moves it)
 *   +74h  word  video size in paragraphs
 *   +76h  byte  starting 16 KB page (start_seg >> 10)
 *   +77h  byte  /V size in KB (default 16, range 5-96)
 * First-boot signature: word 5AA5h at 2000:0200, written before the reboot.
 * Without /S the buffer sits at the top of the first 128 KB:
 *   /V16 -> 1C00h (page 7), /V32 -> 1800h (6-7), /V64 -> 1000h (4-7). */
#define JR_DEV_NAME     "JRCONSYS"
#define JR_NUL_NAME     "NUL     "
#define JR_OFF_START    0x72U
#define JR_OFF_PARAS    0x74U
#define JR_OFF_PAGE     0x76U
#define JR_OFF_KB       0x77U
#define JR_SIG_SEG      0x2000U
#define JR_SIG_OFF      0x0200U
#define JR_SIG_WORD     0x5AA5U

static unsigned long ticks_to_us(unsigned ticks)
{
    /* One 8253 tick is 1 / (14.31818 MHz / 12) = 838.1 ns.  Doing this in
     * 32-bit integers rather than the original ZTimerReport's 16-bit
     * fixed-point keeps it exact to the tick and avoids linking the floating
     * point library into a 128 KB machine. */
    return ((unsigned long)ticks * 8381UL + 5000UL) / 10000UL;
}

static unsigned long bytes_per_sec(unsigned bytes, unsigned long us)
{
    /* bytes * 1000000 / us would overflow 32 bits above 4,294 bytes, so scale
     * in two steps.  Costs us the last digit, which we were never going to
     * believe anyway. */
    if (us == 0UL)
        return 0UL;
    return ((unsigned long)bytes * 100000UL / us) * 10UL;
}

static unsigned long ns_per_byte(unsigned bytes, unsigned long us)
{
    if (bytes == 0U)
        return 0UL;
    return us * 1000UL / (unsigned long)bytes;
}

static unsigned long us_per_screen(unsigned bytes, unsigned long us)
{
    if (bytes == 0U)
        return 0UL;
    return us * (unsigned long)M8_SCREEN_BYTES / (unsigned long)bytes;
}

/* Print an unsigned long as milliseconds with one decimal place. */
static void print_ms(unsigned long us)
{
    printf("%5lu.%01lu", us / 1000UL, (us % 1000UL) / 100UL);
}

static void set_pages(int crt, int cpu)
{
    if (opt_bios_flip)
        vid_set_pages_bios((unsigned)crt, (unsigned)cpu);
    else
        vid_set_pages_port((unsigned)crt, (unsigned)cpu, PCJR_ADDR_16K);
}

/* ==========================================================================
 * Part 1 -- where is the memory?
 * ========================================================================== */

static void walk_mcbs(void)
{
    unsigned      m;
    unsigned      owner, size;
    unsigned char sig;
    int           n;

    mem.mcb_ok    = 0;
    mem.arena_top = 0;
    mem.first_mcb = dos_first_mcb();

    m = mem.first_mcb;

    printf("  DOS memory arena (int 21h AH=52h chain):\n");
    printf("    MCB    owner   paragraphs      bytes  physical span\n");
    printf("    -----  -----   ----------  ---------  ---------------\n");

    for (n = 0; n < 64; n++) {
        sig   = peek_byte(m, 0);
        owner = peek_word(m, 1);
        size  = peek_word(m, 3);

        if (sig != 'M' && sig != 'Z') {
            printf("    %04X   signature %02X is neither 'M' nor 'Z' --"
                   " chain not walkable\n", m, sig);
            return;
        }

        printf("    %04X   %04X    %10u  %9lu  %05lX-%05lX%s\n",
               m, owner, size,
               (unsigned long)size * 16UL,
               (unsigned long)(m + 1) * 16UL,
               (unsigned long)(m + 1 + size) * 16UL - 1UL,
               (owner == mem.psp) ? "  <- us" :
               (owner == 0U)      ? "  free"  : "");

        mem.arena_top = m + 1 + size;

        if (sig == 'Z') {
            mem.mcb_ok = 1;
            printf("    arena ends at paragraph %04X (physical %05lX)\n",
                   mem.arena_top, (unsigned long)mem.arena_top * 16UL);
            return;
        }
        m = (unsigned)(m + 1 + size);
    }
    printf("    (more than 64 blocks -- giving up on the walk)\n");
}

static int name8_eq(unsigned seg, unsigned off, const char *n)
{
    int i;

    for (i = 0; i < 8; i++) {
        if (peek_byte(seg, (unsigned)(off + (unsigned)i)) != (unsigned char)n[i])
            return 0;
    }
    return 1;
}

static int jr_header_at(unsigned seg, unsigned off)
{
    unsigned attr;

    attr = peek_word(seg, (unsigned)(off + 4U));
    if ((attr & 0x8000U) == 0U)
        return 0;
    return name8_eq(seg, (unsigned)(off + 0x0AU), JR_DEV_NAME);
}

/* Walk the DOS device chain from NUL (LoL+22h, DOS 3+) or CON (LoL+0Ch). */
static int jr_from_device_chain(unsigned *seg_out, unsigned *off_out)
{
    unsigned lol_seg, lol_off, dseg, doff, next_seg, next_off;
    int      n;

    dos_sysvars(&lol_seg, &lol_off);
    doff = peek_word(lol_seg, (unsigned)(lol_off + 0x22U));
    dseg = peek_word(lol_seg, (unsigned)(lol_off + 0x24U));
    if (!name8_eq(dseg, (unsigned)(doff + 0x0AU), JR_NUL_NAME)) {
        doff = peek_word(lol_seg, (unsigned)(lol_off + 0x0CU));
        dseg = peek_word(lol_seg, (unsigned)(lol_off + 0x0EU));
    }

    for (n = 0; n < 64; n++) {
        if (jr_header_at(dseg, doff)) {
            *seg_out = dseg;
            *off_out = doff;
            return 1;
        }
        next_off = peek_word(dseg, doff);
        next_seg = peek_word(dseg, (unsigned)(doff + 2U));
        if (next_off == 0xFFFFU && next_seg == 0xFFFFU)
            return 0;
        dseg = next_seg;
        doff = next_off;
    }
    return 0;
}

/* DOS 2 has no NUL pointer in the list of lists; CONFIG.SYS drivers live in
 * MCB-owned blocks, so scan those paragraphs for a JRCONSYS header. */
static int jr_from_mcb_scan(unsigned *seg_out, unsigned *off_out)
{
    unsigned      m, size, s, last;
    unsigned char sig;
    int           n;

    if (!mem.mcb_ok || mem.first_mcb == 0U)
        return 0;

    m = mem.first_mcb;
    for (n = 0; n < 64; n++) {
        sig  = peek_byte(m, 0);
        size = peek_word(m, 3);
        if (sig != 'M' && sig != 'Z')
            return 0;
        s = (unsigned)(m + 1U);
        for (last = 0; last < size; last++) {
            if (jr_header_at((unsigned)(s + last), 0)) {
                *seg_out = (unsigned)(s + last);
                *off_out = 0;
                return 1;
            }
        }
        if (sig == 'Z')
            return 0;
        m = (unsigned)(m + 1U + size);
    }
    return 0;
}

static unsigned jr_round16_kb(unsigned kb)
{
    if (kb < 16U)
        kb = 16U;
    return (unsigned)((kb + 15U) & ~15U);
}

/* Fill jr_seg / jr_paras from the resident header.  Prefer the start page
 * (+76h) and /V size (+77h): those follow /S.  A 16 KB-aligned +72h is a
 * corroborating start.  Last resort, no /S: park the rounded size at 2000h. */
static int jr_read_window(unsigned dseg, unsigned doff)
{
    unsigned kb, page, start, paras, aligned;

    kb    = peek_byte(dseg, (unsigned)(doff + JR_OFF_KB));
    page  = peek_byte(dseg, (unsigned)(doff + JR_OFF_PAGE));
    start = peek_word(dseg, (unsigned)(doff + JR_OFF_START));
    paras = peek_word(dseg, (unsigned)(doff + JR_OFF_PARAS));

    if (kb < 5U || kb > 96U)
        return 0;

    mem.jr_kb = kb;
    if (paras == 0U || paras > 96U * 64U)
        paras = (unsigned)(kb * 64U);

    if (page >= 1U && page <= 7U)
        aligned = PAGE_SEG(page);
    else if ((start & 0x03FFU) == 0U && start >= PAGE_SEG(1) && start < 0x2000U)
        aligned = start;
    else {
        unsigned round_paras = (unsigned)(jr_round16_kb(kb) * 64U);
        if (round_paras >= 0x2000U)
            return 0;
        aligned = (unsigned)(0x2000U - round_paras);
        if (aligned < PAGE_SEG(1))
            return 0;
    }

    if ((unsigned long)aligned + (unsigned long)paras > 0x2000UL)
        paras = (unsigned)(0x2000U - aligned);
    if (paras < PAGE_PARAS)
        return 0;

    mem.jr_seg   = aligned;
    mem.jr_paras = paras;
    return 1;
}

static void jr_build_mask(void)
{
    unsigned p, pstart, pend, end;

    mem.jr_mask = 0;
    if (mem.jr_seg == 0U || mem.jr_paras < PAGE_PARAS)
        return;
    end = (unsigned)(mem.jr_seg + mem.jr_paras);
    for (p = 1; p < PCJR_PAGE_COUNT; p++) {
        pstart = PAGE_SEG(p);
        pend   = (unsigned)(pstart + PAGE_PARAS);
        if (pstart >= mem.jr_seg && pend <= end)
            mem.jr_mask |= (1U << p);
    }
}

static void detect_jrconfig(void)
{
    unsigned dseg, doff;
    int      got_hdr, got_win;

    dseg    = 0;
    doff    = 0;
    got_hdr = 0;
    got_win = 0;

    mem.jr_found = 0;
    mem.jr_sig   = (peek_word(JR_SIG_SEG, JR_SIG_OFF) == JR_SIG_WORD);
    mem.jr_seg   = 0;
    mem.jr_paras = 0;
    mem.jr_kb    = 0;
    mem.jr_mask  = 0;

    if (jr_from_device_chain(&dseg, &doff) || jr_from_mcb_scan(&dseg, &doff)) {
        got_hdr = 1;
        mem.jr_found = 1;
        got_win = jr_read_window(dseg, doff);
    }

    /* Signature says JrConfig completed its two-pass boot, but the device
     * header was not readable.  The owner's CONFIG is /V64 without /S, which
     * JRCONFIG.DOC places at segment 1000h (pages 4-7). */
    if (!got_win && mem.jr_sig) {
        mem.jr_kb    = 64U;
        mem.jr_seg   = 0x1000U;
        mem.jr_paras = 64U * 64U;
        got_win      = 1;
        printf("  JrConfig boot signature 5AA5h at 2000:0200, but JRCONSYS was\n"
               "  not readable -- assuming no-/S /V64 at segment 1000h.\n");
    }

    if (got_win)
        jr_build_mask();

    printf("  JrConfig: ");
    if (got_hdr) {
        printf("JRCONSYS at %04X:%04X", dseg, doff);
        if (mem.jr_sig)
            printf(", boot signature 5AA5h at 2000:0200");
        printf("\n");
    } else if (mem.jr_sig) {
        printf("boot signature 5AA5h at 2000:0200; JRCONSYS not in the device chain\n");
    } else {
        printf("not found (no JRCONSYS, no 2000:0200 signature)\n");
    }

    if (got_win) {
        unsigned p, first, last;

        first = last = 0xFFFFU;
        for (p = 1; p < PCJR_PAGE_COUNT; p++) {
            if ((mem.jr_mask & (1U << p)) == 0U)
                continue;
            if (first == 0xFFFFU)
                first = p;
            last = p;
        }
        printf("  video window %u KB at segment %04Xh (physical %05lX-%05lX)",
               mem.jr_kb, mem.jr_seg,
               (unsigned long)mem.jr_seg * 16UL,
               (unsigned long)(mem.jr_seg + mem.jr_paras) * 16UL - 1UL);
        if (first != 0xFFFFU)
            printf(", pages %u-%u", first, last);
        printf("\n");
        printf("  those pages are the video buffer even when an owner-0008 MCB\n"
               "  covers them -- they are not program RAM.\n");
    } else if (got_hdr) {
        printf("  JRCONSYS found, but /V size and start page did not make a\n"
               "  window in pages 1-7.\n");
    }
}

static void claim_memory(void)
{
    unsigned err, seg, avail, got;

    seg   = 0;
    avail = 0;

    /* Asking for 0xFFFF paragraphs always fails; the point is that DOS reports
     * the largest block it has in BX. */
    err = dos_alloc(0xFFFFU, &seg, &avail);
    printf("  largest free block DOS will admit to: %u paragraphs"
           " (%lu bytes)%s\n",
           avail, (unsigned long)avail * 16UL,
           (err == 8U) ? "" : " [unexpected error code]");
    if (err != 8U)
        printf("  note: int 21h AH=48h with BX=FFFFh returned %u,"
               " expected 8\n", err);

    if (avail == 0U) {
        printf("  nothing free -- cannot claim a second video page.\n");
        return;
    }

    got = avail;
    err = dos_alloc(got, &seg, &avail);
    if (err != 0U) {
        printf("  claiming it failed with DOS error %u.\n", err);
        return;
    }

    owned_block      = seg;
    mem.block_seg    = seg;
    mem.block_paras  = got;

    printf("  claimed %u paragraphs at %04X:0000 (physical %05lX-%05lX)\n",
           got, seg,
           (unsigned long)seg * 16UL,
           (unsigned long)(seg + got) * 16UL - 1UL);
}

/* Does any MCB, or the pre-arena DOS/BIOS region, overlap [p0, p1)? */
static int page_is_mcb_covered(unsigned p0, unsigned p1, unsigned *owner_out)
{
    unsigned      m, owner, size, b0, b1;
    unsigned char sig;
    int           n;

    *owner_out = 0xFFFFU;

    /* Bytes below the first MCB are IVT, BDA and the DOS kernel -- not a
     * JrConfig hole. */
    if (mem.first_mcb != 0U && p1 <= mem.first_mcb)
        return 1;
    if (mem.first_mcb != 0U && p0 < mem.first_mcb && p1 > mem.first_mcb) {
        *owner_out = 8U;
        return 1;
    }

    if (!mem.mcb_ok)
        return 1;

    m = mem.first_mcb;
    for (n = 0; n < 64; n++) {
        sig   = peek_byte(m, 0);
        owner = peek_word(m, 1);
        size  = peek_word(m, 3);
        if (sig != 'M' && sig != 'Z')
            return 1;
        b0 = m;
        b1 = (unsigned)(m + 1U + size);
        if (p0 < b1 && p1 > b0) {
            *owner_out = owner;
            return 1;
        }
        if (sig == 'Z')
            return 0;
        m = (unsigned)(m + 1 + size);
    }
    return 1;
}

static void classify_pages(void)
{
    unsigned p, pstart, pend, owner;
    unsigned entry_crt;

    mem.owned_mask = 0;
    mem.bios_mask  = 0;
    mem.hole_mask  = 0;
    entry_crt      = (orig_pages >> 8) & 7U;

    for (p = 0; p < PCJR_PAGE_COUNT; p++) {
        pstart = PAGE_SEG(p);
        pend   = (unsigned)(pstart + PAGE_PARAS);

        if (mem.block_paras != 0U &&
            pstart >= mem.block_seg &&
            pend   <= (unsigned)(mem.block_seg + mem.block_paras)) {
            mem.owned_mask |= (1U << p);
        } else if ((mem.jr_mask & (1U << p)) == 0U) {
            /* JrConfig pages stay off owned/bios/hole: they are often still
             * inside the owner-0008 system MCB. */
            if (mem.mcb_ok && pstart >= mem.arena_top)
                mem.bios_mask |= (1U << p);
            else if (mem.mcb_ok && !page_is_mcb_covered(pstart, pend, &owner))
                mem.hole_mask |= (1U << p);
        }
    }

    printf("\n  16 KB page map (port 0x3DF pages 0-7 = first 128 KB only):\n");
    printf("    page  physical         status\n");
    printf("    ----  ---------------  ----------------------------------\n");
    for (p = 0; p < PCJR_PAGE_COUNT; p++) {
        const char *status;

        if (mem.owned_mask & (1U << p))
            status = "ours (claimed from DOS)";
        else if (mem.jr_mask & (1U << p))
            status = "JrConfig video reserve";
        else if (mem.bios_mask & (1U << p))
            status = "above the DOS arena -- BIOS video reserve";
        else if (mem.hole_mask & (1U << p))
            status = "MCB hole -- video reserve";
        else if (p == 0U)
            status = "in use (IVT / BDA / DOS -- never a video page)";
        else {
            owner = 0xFFFFU;
            page_is_mcb_covered(PAGE_SEG(p),
                                (unsigned)(PAGE_SEG(p) + PAGE_PARAS),
                                &owner);
            if (owner == 0xFFFFU)
                status = "in use by DOS, by us, or absent";
            else {
                /* Printed in two steps so we can show the MCB owner. */
                status = 0;
            }
        }

        printf("    %u     %05lX-%05lX    ",
               p,
               (unsigned long)p * PAGE_BYTES,
               (unsigned long)(p + 1) * PAGE_BYTES - 1UL);
        if (status)
            printf("%s", status);
        else
            printf("in DOS arena, MCB owner %04X", owner);
        if (p == entry_crt)
            printf("  [entry CRT]");
        if (p == (orig_pages & 7U) && (orig_pages & 7U) != entry_crt)
            printf("  [entry CPU]");
        printf("\n");
    }
}

/* Write signatures to two pages by their physical addresses and read them
 * back, to prove they are two distinct pieces of RAM.  On a machine where the
 * address decode wraps, both "pages" would be the same memory and the flip
 * would appear to work while doing nothing. */
static int pages_are_distinct(int pa, int pb)
{
    unsigned a_sig = 0xA5A5U;
    unsigned b_sig = 0x5A5AU;

    poke_word(PAGE_SEG(pa), 0x0000U, a_sig);
    poke_word(PAGE_SEG(pb), 0x0000U, b_sig);
    poke_word(PAGE_SEG(pa), 0x3FFEU, (unsigned)~a_sig);
    poke_word(PAGE_SEG(pb), 0x3FFEU, (unsigned)~b_sig);

    return peek_word(PAGE_SEG(pa), 0x0000U) == a_sig &&
           peek_word(PAGE_SEG(pb), 0x0000U) == b_sig &&
           peek_word(PAGE_SEG(pa), 0x3FFEU) == (unsigned)~a_sig &&
           peek_word(PAGE_SEG(pb), 0x3FFEU) == (unsigned)~b_sig;
}

/* Does the CPU page field really alias 16 KB of main memory into B800:0000?
 * DESIGN.md section 3 states it as fact.  It is the reason we can write to a
 * fixed segment and let a page register decide where the bytes land, so it is
 * worth one paragraph of code to check rather than assume. */
static int aperture_maps_page(int p)
{
    unsigned sig = (unsigned)(0x5A00U | (unsigned)p);
    int      ok  = 1;

    /* Always the raw port here, never the BIOS call, even when /bios is in
     * effect.  The bit layout of 0x3DF is not in doubt; which of BH and BL
     * int 10h AX=0583h wants is, and probe_bios_page_order() below is what
     * answers that.  Mixing the two would make each test depend on the other
     * being right. */
    vid_set_pages_port((unsigned)crt_page, (unsigned)p, PCJR_ADDR_16K);

    /* window -> RAM */
    poke_word(PCJR_APERTURE_SEG, 0x0000U, sig);
    poke_word(PCJR_APERTURE_SEG, 0x3FFEU, (unsigned)~sig);
    if (peek_word(PAGE_SEG(p), 0x0000U) != sig)
        ok = 0;
    if (peek_word(PAGE_SEG(p), 0x3FFEU) != (unsigned)~sig)
        ok = 0;

    /* RAM -> window */
    poke_word(PAGE_SEG(p), 0x0100U, (unsigned)(sig ^ 0xFFFFU));
    if (peek_word(PCJR_APERTURE_SEG, 0x0100U) != (unsigned)(sig ^ 0xFFFFU))
        ok = 0;

    return ok;
}

/* Which of BH and BL does int 10h AH=05h AL=83h take as the CPU page?
 *
 * DESIGN.md section 3 says BX = (cpu << 8) | crt.  Flashparty's
 * set_vid_160_100_16 implies the opposite, and Ralf Brown agrees with
 * Flashparty.  Rather than pick a side, put a distinct signature in each page,
 * ask BIOS for a specific pairing, and see which one shows up in the window.
 *
 * Returns  1  BL is the CPU page (what this program assumes)
 *         -1  BH is the CPU page (DESIGN.md section 3 is right, we are wrong)
 *          0  neither -- the call did something else entirely */
static int probe_bios_page_order(int pa, int pb)
{
    unsigned sig_a = (unsigned)(0xAA00U | (unsigned)pa);
    unsigned sig_b = (unsigned)(0xAA00U | (unsigned)pb);
    unsigned seen;

    poke_word(PAGE_SEG(pa), 0x0000U, sig_a);
    poke_word(PAGE_SEG(pb), 0x0000U, sig_b);

    /* Ask for: CRT shows pa, CPU window sees pb. */
    vid_set_pages_bios((unsigned)pa, (unsigned)pb);
    seen = peek_word(PCJR_APERTURE_SEG, 0x0000U);

    if (seen == sig_b)
        return 1;
    if (seen == sig_a)
        return -1;
    return 0;
}

static int choose_pages(void)
{
    int      p;
    unsigned usable = mem.owned_mask | mem.bios_mask | mem.hole_mask |
                      mem.jr_mask;

    if (opt_page_a >= 0 && opt_page_b >= 0) {
        if (opt_page_a < 1 || opt_page_b < 1 ||
            opt_page_a > 7 || opt_page_b > 7 || opt_page_a == opt_page_b) {
            printf("\n  /pa and /pb must be two different pages in 1-7.\n"
                   "  Page 0 is the IVT and BIOS data area -- never force it.\n");
            return 0;
        }
        page_a = opt_page_a;
        page_b = opt_page_b;
        printf("\n  pages forced from the command line: A=%d B=%d\n",
               page_a, page_b);
        if (((usable >> page_a) & 1U) == 0U || ((usable >> page_b) & 1U) == 0U)
            printf("  one or both of those is not a reserved video page.\n"
                   "  Continuing because that is what /pa /pb are for -- but\n"
                   "  this can trash a resident driver or the kernel.\n"
                   "  On jrIDE, do not use /pa as a substitute for JrConfig's\n"
                   "  /V window (lib/repos/jrIDE/jrIDE.html).\n");
    } else {
        /* Highest two usable pages in 1-7.  /V64 at 1000h yields 6 and 7;
         * /V32 at 1800h is the same pair.  Page 0 is never a candidate. */
        page_a = page_b = -1;
        for (p = (int)PCJR_PAGE_COUNT - 1; p >= 1; p--) {
            if ((usable & (1U << p)) == 0U)
                continue;
            if (page_b < 0)
                page_b = p;
            else if (page_a < 0)
                page_a = p;
        }
        if (page_a < 0 || page_b < 0) {
            printf("\n  *** could not find two usable 16 KB pages in 0-7. ***\n");
            if (mem.mem_kb > 128U) {
                printf("  This is the expected jrIDE-class map, not a small CONFIG.\n");
                printf("  int 12h is %u KB -- jrIDE.html: the jrIDE BIOS sets\n"
                       "  detected memory to 736 KB (system BIOS only scans to\n"
                       "  640 KB).  608 KB of sidecar SRAM fills 128 KB to 736 KB.\n",
                       mem.mem_kb);
                printf("  Port 0x3DF can only select pages 0-7 (first 128 KB).\n"
                       "  The free block above page 7 is not video-capable.\n");
                if (mem.jr_found || mem.jr_sig) {
                    printf("  JrConfig is loaded, but its /V window does not contain\n"
                           "  two full 16 KB pages in 1-7.  /V16 is one page; /V32 or\n"
                           "  /V64 is what mode 8 flipping needs.\n");
                } else {
                    printf("  jrIDE.html: use JrConfig (or similar) to move the video\n"
                           "  buffer and reserve 32 KB or 64 KB so two pages in 0-7\n"
                           "  are the video buffer.  DOS still sees over 640 KB.\n");
                    printf("  M1 looks for JRCONSYS and for JrConfig's 5AA5h at\n"
                           "  2000:0200 -- an MCB hole is not required.\n");
                }
                printf("  Do not /pa=/pb a DOS-occupied page -- that can trash\n"
                       "  the kernel.  Entry CRT page is %u (live BIOS video).\n",
                       (orig_pages >> 8) & 7U);
            } else {
                printf("  The usual causes on a 128 KB box (not the ship target):\n");
                printf("   - the C run-time did not hand its slack back to DOS,\n"
                       "     so the largest free block stops short of page 6.\n");
                printf("   - DOS itself is large enough that the free block starts\n"
                       "     above page 6.  Try a smaller CONFIG.SYS.\n");
                printf("   - the machine has less than 128 KB.\n");
            }
            return 0;
        }
        if (page_a > page_b) {
            p = page_a; page_a = page_b; page_b = p;
        }
        printf("\n  chose pages %d and %d (physical %05lX and %05lX)\n",
               page_a, page_b,
               (unsigned long)page_a * PAGE_BYTES,
               (unsigned long)page_b * PAGE_BYTES);
    }

    seg_a = PAGE_SEG(page_a);
    seg_b = PAGE_SEG(page_b);
    return 1;
}

/* Carve a scratch buffer out of the low end of our claim, for the copy
 * benchmarks' source.  It must not overlap either video page, or we would be
 * measuring a video-to-video copy and calling it RAM-to-video. */
static int page_overlaps_claim(int p)
{
    unsigned p0, p1, c0, c1;

    if (p < 0 || mem.block_paras == 0U)
        return 0;
    p0 = PAGE_SEG(p);
    p1 = (unsigned)(p0 + PAGE_PARAS);
    c0 = mem.block_seg;
    c1 = (unsigned)(mem.block_seg + mem.block_paras);
    return (p0 < c1) && (p1 > c0);
}

static void carve_scratch(void)
{
    unsigned avail_paras;

    scratch_seg   = 0;
    scratch_bytes = 0;

    if (mem.block_paras == 0U)
        return;

    /* On jrIDE the claim is sidecar RAM above 128 KB -- it never contains a
     * 3DF page, so the whole block is plain RAM.  On a 128 KB layout the
     * claim may include a video page; only the run below it is scratch. */
    if (page_overlaps_claim(page_a) || page_overlaps_claim(page_b)) {
        unsigned first_page_para = PAGE_SEG(page_a < page_b ? page_a : page_b);
        if (first_page_para <= mem.block_seg)
            return;
        avail_paras = (unsigned)(first_page_para - mem.block_seg);
        if (avail_paras > mem.block_paras)
            avail_paras = mem.block_paras;
    } else {
        avail_paras = mem.block_paras;
    }
    if (avail_paras > PAGE_PARAS)
        avail_paras = PAGE_PARAS;

    scratch_seg   = mem.block_seg;
    scratch_bytes = (unsigned)(avail_paras * 16U);

    /* RAM-to-RAM copies go from the bottom half of the scratch to the top,
     * so both ends stay inside it whatever size we ended up with. */
    r2r_dst_off = scratch_bytes / 2U;
    r2r_bytes   = scratch_bytes / 2U;
    if (r2r_bytes > 8000U)
        r2r_bytes = 8000U;
    r2r_bytes &= ~1U;
}

/* ==========================================================================
 * Part 2 -- the visual test
 * ========================================================================== */

/* Paint the sky, mountain strip and ground into one page, as flat horizontal
 * bands.  This is not decoration: bands like these are what makes DESIGN.md
 * section 7's budget work, because a solid horizontal band is scroll-invariant
 * and costs nothing to maintain while the camera moves. */
static void paint_world(unsigned seg, unsigned hud_colour)
{
    fill_band_m8(seg, 0,                HUD_ROWS,       M8_SOLID(hud_colour));
    fill_band_m8(seg, HUD_ROWS,         56,             M8_SOLID(0));   /* sky top */
    fill_band_m8(seg, 64,               56,             M8_SOLID(1));   /* sky mid */
    fill_band_m8(seg, 120,              MOUNTAIN_ROW - 120, M8_SOLID(9)); /* haze */
    fill_band_m8(seg, MOUNTAIN_ROW,     4,              M8_SOLID(8));   /* ridge */
    fill_band_m8(seg, GROUND_TOP_ROW,   1,
                                        M8_SOLID(M8_IDX_GROUND_HI));    /* highlight */
    fill_band_m8(seg, GROUND_TOP_ROW+1, 199 - GROUND_TOP_ROW,
                                        M8_SOLID(M8_IDX_GROUND));       /* ground */
}

/* Phase one: prove the CRT page field actually changes what is on the glass.
 * The two pages get different HUD-band colours and a marker block at different
 * heights, and we swap at half-second intervals.  If the flip is not working
 * the screen simply will not change. */
static void flip_test_identity(void)
{
    unsigned frame;
    int      showing;

    paint_world(seg_a, 2);              /* green HUD band */
    paint_world(seg_b, 4);              /* red HUD band */

    /* A block low on page A, high on page B: unmistakable even if someone is
     * colour-blind to the band. */
    fill_rect_m8(seg_a, 30, 130, 20, 24, M8_SOLID(15));
    fill_rect_m8(seg_b, 30,  40, 20, 24, M8_SOLID(15));

    showing = page_a;
    for (frame = 0; frame < IDENTITY_FRAMES; frame++) {
        vid_wait_retrace();
        if ((frame % IDENTITY_SWAP_FRAMES) == 0) {
            showing  = (showing == page_a) ? page_b : page_a;
            crt_page = showing;
            /* CPU window follows the page we are *not* showing, which is the
             * arrangement the real renderer uses. */
            set_pages(showing, (showing == page_a) ? page_b : page_a);
        }
        if (kbhit()) {
            getch();
            break;
        }
    }
}

/* Phase two: prove the flip is tear-free.  Identical backgrounds on both
 * pages and a bar swept across, redrawn into the back page every frame with
 * its previous position restored first -- a one-object version of the
 * dirty-rect pipeline in section 7.  Any tearing, and the bar will show a
 * horizontal seam; any missed retrace, and it will strobe.
 *
 * This one only pays for itself if somebody is watching it, so it runs for
 * MOTION_FRAMES_DEF frames and stops.  /frames=N makes it longer when there
 * is a marginal artefact to stare at; /nogfx skips both phases, which is what
 * a redirected batch run wants. */
#define BAR_TOP     28                  /* rows 28..127, so the bar crosses */
#define BAR_ROWS    100                 /*  all three sky bands */
#define BAR_WBYTES  4                   /* 8 pixels wide */
#define BAR_MAX_X   (M8_BYTES_PER_ROW - BAR_WBYTES)

/* Repaint the background under one bar position, from the band model rather
 * than from a saved copy.  Three fills, because the bar spans the boundary
 * between sky band 0 (rows 8-63), band 1 (64-119) and the haze band
 * (120-169). */
static void restore_bar(unsigned seg, unsigned x)
{
    fill_rect_m8(seg, x, BAR_TOP, BAR_WBYTES,  64 - BAR_TOP, M8_SOLID(0));
    fill_rect_m8(seg, x, 64,      BAR_WBYTES,  56,           M8_SOLID(1));
    fill_rect_m8(seg, x, 120,     BAR_WBYTES,
                 (BAR_TOP + BAR_ROWS) - 120,                 M8_SOLID(9));
}

static void flip_test_motion(void)
{
    int      x[2];                      /* bar column last drawn, per page */
    int      bar;
    int      back;                      /* 0 = page_a is the back buffer */
    int      dir;
    unsigned back_seg;
    unsigned i;

    paint_world(seg_a, 7);
    paint_world(seg_b, 7);

    x[0] = x[1] = -1;
    bar  = 0;
    dir  = 1;
    back = 0;

    crt_page = page_b;
    set_pages(page_b, page_a);

    for (i = 0; i < opt_motion_frames; i++) {
        back_seg = back ? seg_b : seg_a;

        /* This buffer is one frame stale, so what needs restoring is what
         * *this* page dirtied two frames ago -- the per-buffer dirty list
         * DESIGN.md section 7 describes, with a list of one. */
        if (x[back] >= 0)
            restore_bar(back_seg, (unsigned)x[back]);

        fill_rect_m8(back_seg, (unsigned)bar, BAR_TOP, BAR_WBYTES, BAR_ROWS,
                     M8_SOLID(15));
        x[back] = bar;

        /* The flip: one write, inside blanking.  We address the pages by
         * their physical segments here, so the CPU page field is along for the
         * ride -- but the renderer will want it pointing at the back buffer,
         * so keep it honest. */
        vid_wait_retrace();
        crt_page = back ? page_b : page_a;
        set_pages(crt_page, back ? page_a : page_b);

        back = !back;
        bar += dir;
        if (bar >= BAR_MAX_X) { bar = BAR_MAX_X; dir = -1; }
        if (bar <= 0)         { bar = 0;         dir =  1; }

        if (kbhit()) {
            getch();
            break;
        }
    }
}

/* ==========================================================================
 * Part 3 -- the benchmarks
 * ========================================================================== */

static void run_kernel(int k, unsigned bytes)
{
    unsigned pattern = M8_SOLID(6);

    switch (k) {
    case K_FILL_STOSW_WIN:
        fill_words(PCJR_APERTURE_SEG, 0, bytes >> 1, pattern);
        break;
    case K_FILL_STOSW_DIRECT:
        fill_words(seg_b, 0, bytes >> 1, pattern);
        break;
    case K_FILL_STOSW_RAM:
        fill_words(scratch_seg, 0, bytes >> 1, pattern);
        break;
    case K_FILL_STOSB_DIRECT:
        fill_bytes(seg_b, 0, bytes, pattern);
        break;
    case K_FILL_RECT_M8:
        fill_rect_m8(seg_b, 0, 0, M8_BYTES_PER_ROW,
                     bytes / M8_BYTES_PER_ROW, pattern);
        break;
    case K_COPY_MOVSW_R2V:
        copy_words(seg_b, 0, scratch_seg, 0, bytes >> 1);
        break;
    case K_COPY_MOVSB_R2V:
        copy_bytes(seg_b, 0, scratch_seg, 0, bytes);
        break;
    case K_COPY_MOVSW_V2V:
        copy_words(seg_b, 0, seg_a, 0, bytes >> 1);
        break;
    case K_COPY_MOVSW_R2R:
        copy_words(scratch_seg, r2r_dst_off, scratch_seg, 0, bytes >> 1);
        break;
    default:
        break;
    }
}

/* One primitive, timed opt_reps times.
 *
 * Each run is started immediately after vertical retrace begins, so that every
 * repetition sees the same phase of the video gate array's memory traffic.
 * Without that, a 23 ms fill starting at a random point in a 16.7 ms frame
 * would straddle a different mix of active-display and blanking each time and
 * the spread would be noise rather than signal. */
static void bench_one(int k, const char *name, const char *target,
                      unsigned want_bytes)
{
    bench_result *r = &results[k];
    unsigned      bytes = want_bytes;
    unsigned      i;
    unsigned long us, sum;

    r->name       = name;
    r->target     = target;
    r->want_bytes = want_bytes;
    r->bytes      = 0;
    r->reps       = 0;
    r->status     = ST_OK;
    r->skip_why   = 0;

    /* Find a size that fits inside the timer's ~54 ms window. */
    for (;;) {
        vid_wait_retrace();
        ztimer_on();
        run_kernel(k, bytes);
        ztimer_off();
        if (ztimer_overflow() == 0U)
            break;
        if (bytes <= BENCH_FLOOR_BYTES) {
            r->status   = ST_OVERFLOW;
            r->skip_why = "timer overflowed even at the floor size";
            return;
        }
        bytes = (bytes >> 1) & ~1U;
        if (k == K_FILL_RECT_M8)
            bytes = (bytes / M8_BYTES_PER_ROW) * M8_BYTES_PER_ROW;
    }

    sum      = 0UL;
    r->us_min = 0xFFFFFFFFUL;

    for (i = 0; i < opt_reps; i++) {
        vid_wait_retrace();
        ztimer_on();
        run_kernel(k, bytes);
        ztimer_off();
        if (ztimer_overflow() != 0U)
            continue;                   /* a stray NMI; drop the sample */
        us = ticks_to_us(ztimer_count());
        if (us < r->us_min)
            r->us_min = us;
        sum += us;
        r->reps++;
    }

    if (r->reps == 0U) {
        r->status   = ST_OVERFLOW;
        r->skip_why = "every repetition overflowed the timer";
        return;
    }

    r->bytes  = bytes;
    r->us_avg = sum / (unsigned long)r->reps;
}

static void bench_skip(int k, const char *name, const char *target,
                       const char *why)
{
    results[k].name     = name;
    results[k].target   = target;
    results[k].status   = ST_SKIPPED;
    results[k].skip_why = why;
}

static void run_benchmarks(void)
{
    const char *no_scratch = "no scratch RAM outside the video pages";
    int         have_scratch;

    /* Draw into the page the CRTC is *not* showing, which is what the renderer
     * does, and leave the display on something.  The video gate array is
     * fetching 16 KB per frame throughout, which is the contention we are
     * trying to measure. */
    crt_page = page_a;
    set_pages(page_a, page_b);

    have_scratch = (scratch_seg != 0U && scratch_bytes >= 8192U);

    /* Fills: predicted at ~1.5 us/byte, so a full 16,000-byte screen should be
     * about 23.5 ms and fits inside the timer's window directly. */
    bench_one(K_FILL_STOSW_WIN,    "fill  rep stosw", "video via B8000",
              M8_SCREEN_BYTES);
    bench_one(K_FILL_STOSW_DIRECT, "fill  rep stosw", "video, direct seg",
              M8_SCREEN_BYTES);
    if (have_scratch)
        bench_one(K_FILL_STOSW_RAM, "fill  rep stosw", "plain RAM",
                  scratch_bytes < M8_SCREEN_BYTES ? scratch_bytes
                                                  : M8_SCREEN_BYTES);
    else
        bench_skip(K_FILL_STOSW_RAM, "fill  rep stosw", "plain RAM",
                   no_scratch);
    bench_one(K_FILL_STOSB_DIRECT, "fill  rep stosb", "video, direct seg",
              M8_SCREEN_BYTES);
    bench_one(K_FILL_RECT_M8,      "fill_rect_m8 80x200", "video, 2-bank rows",
              M8_SCREEN_BYTES);

    /* Copies: predicted at 3.5 us/byte for MOVSW and 4.6 for MOVSB, so a full
     * screen would be 55 ms and 74 ms -- at and past the timer's ceiling.
     * Start at half and a quarter of a screen. */
    if (have_scratch) {
        bench_one(K_COPY_MOVSW_R2V, "copy  rep movsw", "RAM -> video", 8000U);
        bench_one(K_COPY_MOVSB_R2V, "copy  rep movsb", "RAM -> video", 4000U);
        bench_one(K_COPY_MOVSW_R2R, "copy  rep movsw", "RAM -> RAM",   r2r_bytes);
    } else {
        bench_skip(K_COPY_MOVSW_R2V, "copy  rep movsw", "RAM -> video", no_scratch);
        bench_skip(K_COPY_MOVSB_R2V, "copy  rep movsb", "RAM -> video", no_scratch);
        bench_skip(K_COPY_MOVSW_R2R, "copy  rep movsw", "RAM -> RAM",   no_scratch);
    }
    bench_one(K_COPY_MOVSW_V2V,     "copy  rep movsw", "video -> video", 8000U);
}

static void report_benchmarks(void)
{
    int           k;
    bench_result *r;
    unsigned long full;

    printf("\nMEASURED on this machine, display active in mode 8, interrupts\n");
    printf("disabled.  Each run starts at the beginning of vertical retrace so\n");
    printf("that every repetition sees the same phase of video memory traffic.\n");
    printf("%u repetitions requested per primitive.\n\n", opt_reps);

    printf("Primitive             Target              Bytes   us min   us avg  runs\n");
    printf("--------------------  ------------------  -----  -------  -------  ----\n");
    for (k = 0; k < K_COUNT; k++) {
        r = &results[k];
        if (r->name == 0)
            continue;
        if (r->status != ST_OK) {
            printf("%-20s  %-18s  not measured\n", r->name, r->target);
            printf("                      reason: %s\n",
                   r->skip_why ? r->skip_why : "unknown");
            continue;
        }
        printf("%-20s  %-18s  %5u  %7lu  %7lu  %4u\n",
               r->name, r->target, r->bytes, r->us_min, r->us_avg, r->reps);
    }

    printf("\nDERIVED from those measurements -- arithmetic only, no new\n");
    printf("assumptions:\n\n");
    printf("Primitive             Target              ns/byte    bytes/s  ms/screen\n");
    printf("--------------------  ------------------  -------  ---------  ---------\n");
    for (k = 0; k < K_COUNT; k++) {
        r = &results[k];
        if (r->name == 0 || r->status != ST_OK)
            continue;
        full = us_per_screen(r->bytes, r->us_avg);
        printf("%-20s  %-18s  %7lu  %9lu  ",
               r->name, r->target,
               ns_per_byte(r->bytes, r->us_avg),
               bytes_per_sec(r->bytes, r->us_avg));
        print_ms(full);
        printf("%s\n", (r->bytes == M8_SCREEN_BYTES) ? "" : " *");
    }
    printf("\n  ms/screen is the cost of 16,000 bytes: one full mode 8 frame.\n");
    printf("  Rows marked * were timed over fewer bytes than that, because a\n");
    printf("  full screen would have run past the Zen timer's 54 ms ceiling.\n");
    printf("  For those the per-byte rate is measured and the screen figure is\n");
    printf("  that rate multiplied out.\n");

    printf("\nPREDICTED by DESIGN.md section 2 -- analytical, from the"
           " six-clock bus\n");
    printf("cycle in the PCjr Technical Reference.  NOT measured, by us or"
           " anyone:\n\n");
    printf("  rep stosw fill     ~1500 ns/byte    ~680 KB/s     ~23.5 ms/screen\n");
    printf("  rep movsw copy     ~3500 ns/byte    ~290 KB/s     ~55.2 ms/screen\n");
    printf("  rep movsb copy     ~4600 ns/byte    ~217 KB/s     ~73.7 ms/screen\n");
    printf("  ceiling on all memory traffic: ~795,000 byte accesses/second\n");
}

/* ==========================================================================
 * Setup, teardown, command line
 * ========================================================================== */

static void cleanup(void)
{
    if (mode_changed) {
        vid_set_mode(orig_mode);
        mode_changed = 0;
    }
    if (owned_block != 0U) {
        dos_free(owned_block);
        owned_block = 0U;
    }
}

static void usage(void)
{
    printf(
"Choplifter! PCjr -- M1 video spike.\n"
"\n"
"  M1 [options]\n"
"\n"
"  /nobench     skip the timing runs\n"
"  /nogfx       skip the visual page-flip test (/noflip is the same thing)\n"
"  /frames=N    frames of the smooth-motion phase, default 480 (8 s at 60 Hz)\n"
"  /bios        flip pages with int 10h AX=0583h instead of OUT to 0x3DF\n"
"  /batch       never wait for a keypress (use with > to capture a log)\n"
"  /reps=N      timed repetitions per primitive, default 8\n"
"  /pa=N /pb=N  force two page numbers in 1-7 (unsafe if those pages hold DOS)\n"
"  /force       run even if the BIOS model byte is not a PCjr's (unsafe)\n"
"  /?           this\n"
"\n"
"To capture the numbers:   M1 /batch /nogfx > M1.LOG\n"
"\n"
"The visual test has to be looked at to mean anything -- it is a human\n"
"judging tearing, strobing and which page is on the glass -- and it breaks\n"
"early only on a keypress.  So a redirected run should always pass /nogfx;\n"
"otherwise it spends the whole visual phase drawing to nobody.\n");
}

static int parse_args(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        char *a = argv[i];

        if (*a == '/' || *a == '-')
            a++;

        if (strcmp(a, "nobench") == 0)
            opt_bench = 0;
        else if (strcmp(a, "noflip") == 0 || strcmp(a, "nogfx") == 0)
            opt_flip = 0;
        else if (strcmp(a, "bios") == 0)
            opt_bios_flip = 1;
        else if (strcmp(a, "batch") == 0)
            opt_wait = 0;
        else if (strcmp(a, "force") == 0)
            opt_force = 1;
        else if (strncmp(a, "reps=", 5) == 0)
            opt_reps = (unsigned)atoi(a + 5);
        else if (strncmp(a, "frames=", 7) == 0)
            opt_motion_frames = (unsigned)atoi(a + 7);
        else if (strncmp(a, "pa=", 3) == 0)
            opt_page_a = atoi(a + 3);
        else if (strncmp(a, "pb=", 3) == 0)
            opt_page_b = atoi(a + 3);
        else {
            usage();
            return 0;
        }
    }
    if (opt_reps < 1U)  opt_reps = 1U;
    if (opt_reps > 64U) opt_reps = 64U;
    if (opt_motion_frames < 2U)
        opt_motion_frames = 2U;
    return 1;
}

static void pause_for_key(const char *what)
{
    if (!opt_wait)
        return;
    printf("\n%s -- press a key.\n", what);
    getch();
}

int main(int argc, char **argv)
{
    int           bios_order;
    unsigned char model;

    /* Unbuffered from the first character, for two reasons that both bite in
     * a redirected run.  A block-buffered stdout holds the entire report
     * until the program exits normally, so a log captured from a run that
     * dies -- or that is still going -- is empty, which is exactly the case
     * where the report was worth having.  And the buffer would otherwise be
     * allocated on first use, which is after we have taken every free byte
     * DOS has. */
    setbuf(stdout, NULL);

    if (!parse_args(argc, argv))
        return 1;

    printf("Choplifter! for the IBM PCjr -- M1 video spike\n");
    printf("=============================================\n\n");

    /* Which ground colour this .EXE was built with.  DESIGN.md section 5
     * leaves the choice open pending a look at both on a real RGB monitor,
     * and the only way to tell two builds apart afterwards is if each says
     * so. */
    printf("Built with ground colour %s: palette register %u holds IRGB %u.\n",
           GROUND_COLOUR_NAME, (unsigned)M8_IDX_GROUND,
           (unsigned)GROUND_COLOUR);
    printf("  Rebuild with GROUND=%s for the other one (docs/M1.md).\n\n",
           GROUND_OTHER_NAME);

    /* Refuse to run anywhere but a PCjr unless told otherwise.  This program
     * writes to fixed physical addresses in the low 128 KB on the strength of
     * a page register that only the PCjr has.  On any other machine those
     * addresses belong to DOS or to us, and the writes would corrupt them. */
    model = peek_byte(MODEL_BYTE_SEG, MODEL_BYTE_OFF);
    printf("BIOS model byte at F000:FFFE ... %02X (%s)\n", model,
           model == MODEL_PCJR ? "IBM PCjr" : "not a PCjr");
    if (model != MODEL_PCJR && !opt_force) {
        printf("\nRefusing to run.  M1 pokes fixed physical addresses in the\n"
               "low 128 KB, which is only safe because the PCjr's page\n"
               "register puts video memory there.  Elsewhere those bytes\n"
               "belong to DOS.\n\n"
               "In DOSBox-X, check that the config has machine=pcjr.\n"
               "To override anyway -- on an emulator you do not mind\n"
               "crashing -- pass /force.\n");
        return 3;
    }
    if (model != MODEL_PCJR)
        printf("Running anyway because /force was given.  Nothing below is\n"
               "meaningful as a PCjr measurement.\n");
    printf("\n");

    dos_break_off();
    atexit(cleanup);

    orig_mode  = vid_get_mode();
    orig_pages = vid_get_pages();
    mem.psp    = dos_get_psp();
    mem.mem_kb = dos_mem_size_kb();

    printf("Machine as reported:\n");
    printf("  int 12h memory size .......... %u KB\n", mem.mem_kb);
    printf("  our PSP ...................... %04X\n", mem.psp);
    printf("  video mode on entry .......... %u\n", orig_mode);
    printf("  page register on entry ....... CRT=%u CPU=%u\n",
           (orig_pages >> 8) & 7U, orig_pages & 7U);
    if (mem.mem_kb < PRODUCT_MIN_KB)
        printf("  DESIGN.md requires %u KB (jrIDE-class).  This report is %u KB;\n"
               "  it is not the ship configuration.\n",
               PRODUCT_MIN_KB, mem.mem_kb);
    if (mem.mem_kb == 112U)
        printf("  112 KB is what a 128 KB PCjr should report: BIOS has kept\n"
               "  the top 16 KB for the active video page.  Historical; not\n"
               "  the 640 KB jrIDE target.\n");
    else if (mem.mem_kb == 128U)
        printf("  128 KB with nothing withheld -- BIOS has not reserved a\n"
               "  video page, so page 7 may be inside the DOS arena.\n");
    else if (mem.mem_kb > 128U)
        printf("  more than 128 KB: jrIDE-class sidecar.  jrIDE.html: 608 KB of\n"
               "  SRAM fills 128 KB to 736 KB; the jrIDE BIOS sets int 12h to\n"
               "  736 KB.  Those extra bytes are not 3DF pages.  Two video pages\n"
               "  must be JrConfig's /V window in 0-7.\n");
    printf("\n");

    walk_mcbs();
    printf("\n");
    detect_jrconfig();
    printf("\n");
    claim_memory();
    classify_pages();

    if (!choose_pages()) {
        printf("\n  NO-GO on the two-page assumption: DESIGN.md section 3\n"
               "  needs two pages in 0-7.  On jrIDE that is JrConfig's /V\n"
               "  window (often still inside owner 0008, not an MCB hole).\n"
               "  See section 14's fallbacks only if that window cannot be had.\n");
        return 2;
    }
    carve_scratch();
    printf("  scratch RAM for copy sources: ");
    if (scratch_seg)
        printf("%u bytes at %04X:0000\n", scratch_bytes, scratch_seg);
    else
        printf("none available\n");

    pause_for_key("About to set mode 8");

    vid_set_mode(M8_MODE);
    mode_changed = 1;
    vid_set_palette16(m1_palette);
    crt_page = page_a;
    set_pages(page_a, page_b);

    /* Verification, done in mode 8 because that is when the page register is
     * in the state we care about.  It scribbles on both pages; the visual test
     * repaints them. */
    {
        int distinct = pages_are_distinct(page_a, page_b);
        int win_a    = aperture_maps_page(page_a);
        int win_b    = aperture_maps_page(page_b);

        bios_order = probe_bios_page_order(page_a, page_b);

        set_pages(page_a, page_b);
        crt_page = page_a;

        vid_set_mode(orig_mode);
        mode_changed = 0;

        printf("\nPage verification, performed in mode 8:\n");
        printf("  pages %d and %d are distinct RAM ......... %s\n",
               page_a, page_b, distinct ? "yes" : "NO");
        printf("  B8000 window follows the CPU page field .. %s (page %d), %s (page %d)\n",
               win_a ? "yes" : "NO", page_a, win_b ? "yes" : "NO", page_b);
        printf("  int 10h AX=0583h page order .............. ");
        if (bios_order > 0)
            printf("BL = CPU page, BH = CRT page\n"
                   "      (matches Ralf Brown and part3.asm; DESIGN.md\n"
                   "       section 3 had these the other way round)\n");
        else if (bios_order < 0)
            printf("BH = CPU page, BL = CRT page\n"
                   "      (DESIGN.md section 3 is right and this program's\n"
                   "       vid_set_pages_bios has the arguments swapped)\n");
        else
            printf("inconclusive -- neither page's signature\n"
                   "      appeared in the window\n");

        if (!distinct || !win_a || !win_b) {
            printf("\n  NO-GO on the two-page assumption.  One of the checks\n"
                   "  above failed, so the flip and the benchmarks below would\n"
                   "  be measuring something other than what they claim.\n");
            return 2;
        }
        printf("  GO on the two-page assumption, on this machine.\n");
    }

    if (opt_flip) {
        pause_for_key("Visual test: page identity, then smooth motion");
        vid_set_mode(M8_MODE);
        mode_changed = 1;
        vid_set_palette16(m1_palette);
        crt_page = page_a;
        set_pages(page_a, page_b);

        flip_test_identity();
        flip_test_motion();

        vid_set_mode(orig_mode);
        mode_changed = 0;
        printf("\nVisual test done.  What you should have seen:\n");
        printf("  1. the top band alternating green/red twice a second, with a\n");
        printf("     white block jumping between low and high -- that is the\n");
        printf("     CRT page field changing what the CRTC displays;\n");
        printf("  2. a white bar sweeping smoothly left and right with no seam\n");
        printf("     and no strobe -- that is the flip landing inside vertical\n");
        printf("     retrace, and the back page being redrawn while hidden.\n");
        printf("  3. the bottom 25 rows in %s, under a yellow highlight row.\n",
               GROUND_COLOUR_NAME);
        printf("     That is the open question in DESIGN.md section 5.  Look at\n");
        printf("     it against the sky bands and the grey ridge, then build the\n");
        printf("     other variant and look again -- on the RGB monitor, since\n");
        printf("     that is the only place the comparison means anything.\n");
        printf("  Pages were flipped with %s.\n",
               opt_bios_flip ? "int 10h AX=0583h" : "a single OUT to 0x3DF");
    }

    if (opt_bench) {
        pause_for_key("About to run the timing loop (a few seconds, no keys)");
        vid_set_mode(M8_MODE);
        mode_changed = 1;
        vid_set_palette16(m1_palette);
        run_benchmarks();
        vid_set_mode(orig_mode);
        mode_changed = 0;

        report_benchmarks();

        printf("\nWhere these numbers came from matters:\n");
        printf("  If this ran under DOSBox or DOSBox-X, the timings above are\n");
        printf("  NOT a validation of DESIGN.md section 2.  Neither emulator\n");
        printf("  models the PCjr's memory contention -- the Video Gate Array\n");
        printf("  stealing bus cycles from the CPU is the entire reason the\n");
        printf("  predicted throughput is as low as it is.  Under emulation\n");
        printf("  this is a smoke test: it says the loops run, the page\n");
        printf("  register behaves and the timer works.  Only a real PCjr\n");
        printf("  settles whether section 7's frame budget is affordable.\n");
    }

    cleanup();
    return 0;
}
