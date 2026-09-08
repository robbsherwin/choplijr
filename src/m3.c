/* m3.c -- Choplifter! for the IBM PCjr, milestone M3.
 *
 * DESIGN.md section 13: blit_rle plus per-buffer dirty rects; chopper moves
 * cleanly with no flicker.  This program does that and stops.  It is not the
 * world scroller (M4), not joystick/flight (M5), and not the game.
 *
 *   - JrConfig page pick, same rule as M1/M2: highest two usable pages in
 *     1-7 from the /V window (hardware GO is pages 6 and 7).
 *   - Mode 8, section 5 palette, scroll-invariant sky/ground bands.
 *   - blit_rle_m8 of the flying chopper set (RLE rows, REP MOVSB opaque
 *     runs, mixed-nibble edges), even and odd pixel-X pre-shifts.
 *   - Per-buffer dirty *list* (one rect per blit, not a union bounce hack).
 *     Restore from the band model with word-padded widths (fill_rect_m8
 *     must not see an odd wbytes -- the 9->8 trail bug).
 *   - Same 1-pixel bounce + pose cycle as M2 so the engine change is
 *     visible as "same picture, cleaner path".
 *
 * Ground colour stays the BROWN #define (pcjr.h).  Do not launch this in
 * DOSBox unless someone is watching: it is a visual, and a keypress is the
 * escape.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>

#include "pcjr.h"

extern unsigned char *chopper_side_e[11];
extern unsigned char *chopper_side_o[11];
extern unsigned char *chopper_head_e[5];
extern unsigned char *chopper_head_o[5];
extern unsigned char *main_rotor_e[3];
extern unsigned char *main_rotor_o[3];
extern unsigned char *tail_rotor_e[4];
extern unsigned char *tail_rotor_o[4];

static int      opt_bios_flip   = 0;
static int      opt_wait        = 1;
static int      opt_force       = 0;
static int      opt_page_a      = -1;
static int      opt_page_b      = -1;
static unsigned opt_frames      = 360U;     /* 6 s at 60 Hz */

#define MODEL_BYTE_SEG  0xF000U
#define MODEL_BYTE_OFF  0xFFFEU
#define MODEL_PCJR      0xFDU

#define HUD_ROWS        8
#define GROUND_TOP_ROW  (199 - 25)
#define MOUNTAIN_ROW    (199 - 29)

#define SPR_Y           90              /* fully inside the mid-sky band */
#define N_SIDE          11
#define N_HEAD          5
#define N_POSE          (N_SIDE + N_HEAD)
#define POSE_HOLD       10              /* retraces per body frame */
#define TAIL_DX         (-7)            /* screen px, left-facing stub */
#define TAIL_DY         5
#define ROTOR_DY        (-1)
#define BOUNCE_LEFT     8
#define BOUNCE_RIGHT    24              /* keeps odd rotor pad on-screen */

/* Product dirty list: one rect per blit, kept per back buffer.  M3 draws
 * at most three sprites (body, main rotor, tail).  16 leaves room for a
 * couple of extra entities without pretending to be the M7 pool. */
#define DIRTY_MAX       16

typedef struct {
    unsigned xbyte;
    unsigned y;
    unsigned wbytes;
    unsigned rows;
} dirty_rect;

typedef struct {
    unsigned n;
    dirty_rect r[DIRTY_MAX];
} dirty_list;

static unsigned orig_mode;
static unsigned orig_pages;
static int      mode_changed;
static int      page_a = -1;
static int      page_b = -1;
static unsigned seg_a, seg_b;
static int      crt_page;

static const unsigned char m3_palette[16] = {
     0, 1, 2, 3, 4, 5,
    GROUND_COLOUR,
     7, 8, 9, 10, 11, 12, 13, 14, 15
};

#define JR_DEV_NAME     "JRCONSYS"
#define JR_NUL_NAME     "NUL     "
#define JR_OFF_START    0x72U
#define JR_OFF_PARAS    0x74U
#define JR_OFF_PAGE     0x76U
#define JR_OFF_KB       0x77U
#define JR_SIG_SEG      0x2000U
#define JR_SIG_OFF      0x0200U
#define JR_SIG_WORD     0x5AA5U

typedef struct {
    unsigned mem_kb;
    unsigned first_mcb;
    unsigned arena_top;
    unsigned jr_mask;
    unsigned jr_seg;
    unsigned jr_paras;
    unsigned jr_kb;
    int      jr_found;
    int      jr_sig;
    int      mcb_ok;
} mem_report;

static mem_report mem;

static void set_pages(int crt, int cpu)
{
    if (opt_bios_flip)
        vid_set_pages_bios((unsigned)crt, (unsigned)cpu);
    else
        vid_set_pages_port((unsigned)crt, (unsigned)cpu, PCJR_ADDR_16K);
}

static void cleanup(void)
{
    if (mode_changed) {
        vid_set_mode(orig_mode);
        mode_changed = 0;
    }
}

static void pause_for_key(const char *what)
{
    if (!opt_wait)
        return;
    printf("  %s -- press a key\n", what);
    getch();
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

static void quiet_mcb_walk(void)
{
    unsigned      m;
    unsigned char sig;
    unsigned      size;
    int           n;

    mem.mcb_ok    = 0;
    mem.arena_top = 0;
    mem.first_mcb = dos_first_mcb();
    m = mem.first_mcb;
    for (n = 0; n < 64; n++) {
        sig  = peek_byte(m, 0);
        size = peek_word(m, 3);
        if (sig != 'M' && sig != 'Z')
            return;
        mem.arena_top = m + 1 + size;
        if (sig == 'Z') {
            mem.mcb_ok = 1;
            return;
        }
        m = (unsigned)(m + 1 + size);
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

    if (!got_win && mem.jr_sig) {
        mem.jr_kb    = 64U;
        mem.jr_seg   = 0x1000U;
        mem.jr_paras = 64U * 64U;
        got_win      = 1;
        printf("  JrConfig signature at 2000:0200, JRCONSYS unread -- "
               "/V64 at 1000h assumed.\n");
    }

    if (got_win)
        jr_build_mask();

    printf("  JrConfig: ");
    if (got_hdr)
        printf("JRCONSYS at %04X:%04X", dseg, doff);
    else if (mem.jr_sig)
        printf("boot signature only");
    else
        printf("not found");
    if (got_win)
        printf(", %u KB at %04Xh (mask %02X)", mem.jr_kb, mem.jr_seg,
               mem.jr_mask);
    printf("\n");
}

static int choose_pages(void)
{
    int      p;
    unsigned usable;

    usable = mem.jr_mask;

    if (opt_page_a >= 0 && opt_page_b >= 0) {
        if (opt_page_a < 1 || opt_page_b < 1 ||
            opt_page_a > 7 || opt_page_b > 7 || opt_page_a == opt_page_b) {
            printf("  /pa and /pb must be two different pages in 1-7.\n");
            return 0;
        }
        page_a = opt_page_a;
        page_b = opt_page_b;
        printf("  pages forced: A=%d B=%d\n", page_a, page_b);
    } else {
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
            /* Hardware GO is 6 and 7.  Emulator configs often have no
             * JrConfig; still the pair DESIGN.md names. */
            page_a = 6;
            page_b = 7;
            printf("  no JrConfig /V window -- using pages 6 and 7 "
                   "(the /V64 pair).\n");
            printf("  On jrIDE, load DEVICE=JRCONFIG.SYS /V64 /L instead of "
                   "forcing DOS pages.\n");
        }
        if (page_a > page_b) {
            p = page_a;
            page_a = page_b;
            page_b = p;
        }
        printf("  video pages A=%d B=%d\n", page_a, page_b);
    }

    seg_a = PAGE_SEG(page_a);
    seg_b = PAGE_SEG(page_b);
    return 1;
}

static void paint_world(unsigned seg)
{
    fill_band_m8(seg, 0,                HUD_ROWS,       M8_SOLID(7));
    fill_band_m8(seg, HUD_ROWS,         56,             M8_SOLID(0));
    fill_band_m8(seg, 64,               56,             M8_SOLID(1));
    fill_band_m8(seg, 120,              MOUNTAIN_ROW - 120, M8_SOLID(9));
    fill_band_m8(seg, MOUNTAIN_ROW,     4,              M8_SOLID(8));
    fill_band_m8(seg, GROUND_TOP_ROW,   1,
                                        M8_SOLID(M8_IDX_GROUND_HI));
    fill_band_m8(seg, GROUND_TOP_ROW+1, 199 - GROUND_TOP_ROW,
                                        M8_SOLID(M8_IDX_GROUND));
}

/* Scroll-invariant band colour for one row.  Matches paint_world. */
static unsigned band_solid(unsigned y)
{
    if (y < HUD_ROWS)
        return M8_SOLID(7);
    if (y < 64U)
        return M8_SOLID(0);
    if (y < 120U)
        return M8_SOLID(1);
    if (y < MOUNTAIN_ROW)
        return M8_SOLID(9);
    if (y < GROUND_TOP_ROW)
        return M8_SOLID(8);
    if (y == GROUND_TOP_ROW)
        return M8_SOLID(M8_IDX_GROUND_HI);
    return M8_SOLID(M8_IDX_GROUND);
}

static void restore_rect(unsigned seg, const dirty_rect *d)
{
    unsigned y, y1, run_y, rw, xb, solid;

    /* fill_rect_m8 fills words: wbytes must be even.  An 18 px sprite is 9
     * packed bytes; passing 9 SHRs to 8 and leaves the last byte standing.
     * Going left those crumbs accumulate.  The recorder already pads to a
     * word; belt-and-suspenders here, and shift left at the right edge so
     * the extra byte still sits on this scanline. */
    rw = (d->wbytes + 1U) & ~1U;
    if (rw == 0U || d->rows == 0U)
        return;
    xb = d->xbyte;
    if ((unsigned)(xb + rw) > M8_BYTES_PER_ROW)
        xb = (unsigned)(M8_BYTES_PER_ROW - rw);

    y  = d->y;
    y1 = (unsigned)(y + d->rows);
    if (y1 > M8_HEIGHT_PX)
        y1 = M8_HEIGHT_PX;

    while (y < y1) {
        solid = band_solid(y);
        run_y = y + 1U;
        while (run_y < y1 && band_solid(run_y) == solid)
            run_y++;
        fill_rect_m8(seg, xb, y, rw, (unsigned)(run_y - y), solid);
        y = run_y;
    }
}

static void restore_list(unsigned seg, dirty_list *list)
{
    unsigned i;

    for (i = 0; i < list->n; i++)
        restore_rect(seg, &list->r[i]);
    list->n = 0;
}

static void dirty_add(dirty_list *list, unsigned xbyte, unsigned y,
                      unsigned wbytes, unsigned rows)
{
    dirty_rect *d;
    unsigned    rw, xb;

    if (list->n >= DIRTY_MAX || wbytes == 0U || rows == 0U)
        return;
    rw = (wbytes + 1U) & ~1U;
    xb = xbyte;
    if ((unsigned)(xb + rw) > M8_BYTES_PER_ROW)
        xb = (unsigned)(M8_BYTES_PER_ROW - rw);
    d = &list->r[list->n];
    d->xbyte  = xb;
    d->y      = y;
    d->wbytes = rw;
    d->rows   = rows;
    list->n++;
}

static void blit_at(unsigned seg, int x_px, int y,
                    unsigned char *even, unsigned char *odd,
                    dirty_list *list)
{
    unsigned char *s;
    unsigned wpx, h, wbytes, xbyte;

    if (y < 0)
        return;
    if (x_px < 0)
        x_px = 0;
    s = (x_px & 1) ? odd : even;
    wpx    = s[0];
    h      = s[1];
    wbytes = wpx / 2U;
    xbyte  = (unsigned)(x_px / 2);
    if (wbytes == 0U || xbyte + wbytes > M8_BYTES_PER_ROW)
        return;
    blit_rle_m8(seg, xbyte, (unsigned)y, data_seg(), (unsigned)s);
    /* Dirty the bytes we actually touched.  The odd copy's leading pad lives
     * in xbyte's high nibble (pixel xbyte*2), one pixel left of x_px. */
    dirty_add(list, xbyte, (unsigned)y, wbytes, h);
}

static void draw_chopper(unsigned seg, int body_x, unsigned pose,
                         unsigned main_i, unsigned tail_i, dirty_list *list)
{
    if (pose < (unsigned)N_SIDE) {
        unsigned char *be = chopper_side_e[pose];
        int bw, rw, rdx;

        blit_at(seg, body_x, SPR_Y, be, chopper_side_o[pose], list);
        bw  = (int)be[0];
        rw  = (int)main_rotor_e[main_i][0];
        rdx = (bw - rw) / 2;
        blit_at(seg, body_x + rdx, SPR_Y + ROTOR_DY,
                main_rotor_e[main_i], main_rotor_o[main_i], list);
        blit_at(seg, body_x + TAIL_DX, SPR_Y + TAIL_DY,
                tail_rotor_e[tail_i], tail_rotor_o[tail_i], list);
    } else {
        unsigned hi = pose - (unsigned)N_SIDE;

        blit_at(seg, body_x, SPR_Y,
                chopper_head_e[hi], chopper_head_o[hi], list);
    }
}

static void run_viewer(void)
{
    dirty_list lists[2];
    int        x;
    int        dir;
    int        back;
    unsigned   back_seg;
    unsigned   i;
    unsigned   pose;
    unsigned   main_i, tail_i;

    paint_world(seg_a);
    paint_world(seg_b);

    lists[0].n = 0;
    lists[1].n = 0;
    x    = BOUNCE_LEFT + 8;
    dir  = 1;
    back = 0;

    crt_page = page_b;
    set_pages(page_b, page_a);

    for (i = 0; i < opt_frames; i++) {
        back_seg = back ? seg_b : seg_a;

        /* This buffer is one frame stale: restore the rects it dirtied two
         * frames ago, from the band model, then blit and record a new list. */
        restore_list(back_seg, &lists[back]);

        pose   = (i / (unsigned)POSE_HOLD) % (unsigned)N_POSE;
        main_i = (i / 2U) % 3U;
        tail_i = (i / 2U) % 4U;
        draw_chopper(back_seg, x, pose, main_i, tail_i, &lists[back]);

        vid_wait_retrace();
        crt_page = back ? page_b : page_a;
        set_pages(crt_page, back ? page_a : page_b);

        back = !back;
        x += dir;
        if (x >= (int)(M8_WIDTH_PX - BOUNCE_RIGHT)) {
            x = (int)(M8_WIDTH_PX - BOUNCE_RIGHT);
            dir = -1;
        }
        if (x <= BOUNCE_LEFT) {
            x = BOUNCE_LEFT;
            dir = 1;
        }

        if (kbhit()) {
            getch();
            break;
        }
    }
}

static void usage(void)
{
    printf(
"Choplifter! PCjr -- M3 sprite engine.\n"
"\n"
"  M3 [options]\n"
"\n"
"  /bios        flip pages with int 10h AX=0583h instead of OUT to 0x3DF\n"
"  /batch       never wait for a keypress; run /frames then exit\n"
"  /frames=N    frames of motion, default 360 (6 s at 60 Hz). Floor is 2.\n"
"  /pa=N /pb=N  force two page numbers in 1-7\n"
"  /force       run even if the BIOS model byte is not a PCjr's\n"
"  /?           this\n"
"\n"
"Attended visual: olive chopper (index 2) with a light-green topside and\n"
"white rotors on the mode-8 sky/ground bands, sweeping in 1-pixel steps\n"
"(odd X), cycling the 11 side tilts then 5 head-on frames.  Same picture\n"
"as M2; the path is blit_rle plus a per-buffer dirty list restored from\n"
"the band model.  Any key skips the rest.\n");
}

static int parse_args(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        char *a = argv[i];

        if (*a == '/' || *a == '-')
            a++;

        if (strcmp(a, "bios") == 0)
            opt_bios_flip = 1;
        else if (strcmp(a, "batch") == 0)
            opt_wait = 0;
        else if (strcmp(a, "force") == 0)
            opt_force = 1;
        else if (strncmp(a, "frames=", 7) == 0)
            opt_frames = (unsigned)atoi(a + 7);
        else if (strncmp(a, "pa=", 3) == 0)
            opt_page_a = atoi(a + 3);
        else if (strncmp(a, "pb=", 3) == 0)
            opt_page_b = atoi(a + 3);
        else {
            usage();
            return 0;
        }
    }
    if (opt_frames < 2U)
        opt_frames = 2U;
    return 1;
}

int main(int argc, char **argv)
{
    unsigned char model;

    setbuf(stdout, NULL);

    if (!parse_args(argc, argv))
        return 1;

    printf("Choplifter! PCjr -- M3 sprite engine\n");
    printf("blit_rle + dirty lists, 11 side + 5 head-on + rotors  ground %s\n\n",
           GROUND_COLOUR_NAME);

    model = peek_byte(MODEL_BYTE_SEG, MODEL_BYTE_OFF);
    printf("BIOS model byte at F000:FFFE ... %02X (%s)\n", model,
           model == MODEL_PCJR ? "IBM PCjr" : "not a PCjr");
    if (model != MODEL_PCJR && !opt_force) {
        printf("Refusing to run.  Pass /force on an emulator you do not mind "
               "crashing.\n");
        return 3;
    }

    dos_break_off();
    atexit(cleanup);

    orig_mode  = vid_get_mode();
    orig_pages = vid_get_pages();
    mem.mem_kb = dos_mem_size_kb();

    printf("int 12h %u KB, pages on entry CRT=%u CPU=%u\n",
           mem.mem_kb, (orig_pages >> 8) & 7U, orig_pages & 7U);
    if (mem.mem_kb < PRODUCT_MIN_KB)
        printf("DESIGN.md wants %u KB; this is not the ship configuration.\n",
               PRODUCT_MIN_KB);

    quiet_mcb_walk();
    detect_jrconfig();
    if (!choose_pages())
        return 2;

    pause_for_key("About to set mode 8 and draw the chopper");

    vid_set_mode(M8_MODE);
    mode_changed = 1;
    vid_set_palette16(m3_palette);
    crt_page = page_a;
    set_pages(page_a, page_b);

    run_viewer();

    vid_set_mode(orig_mode);
    mode_changed = 0;

    printf("\nDrew the flying chopper set with blit_rle_m8 on pages %d/%d.\n",
           page_a, page_b);
    printf("PASS by eye: olive helicopter cycling tilts then head-on, white "
           "rotors on the side views, 1-pixel bounce (not locked to bytes), "
           "skids toward the ground, no venetian-blind bank error, "
           "no trail either direction, no flicker between pages.\n");
    cleanup();
    return 0;
}
