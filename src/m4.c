/* m4.c -- Choplifter! for the IBM PCjr, milestone M4.
 *
 * DESIGN.md section 13: scrolling, camera lead, mountain parallax, ground,
 * barracks, base, fence.  This program does that and stops.  It is not
 * joystick/flight (M5), not hostages, and not the game.
 *
 *   - JrConfig page pick, same rule as M1-M3: highest two usable pages in
 *     1-7 from the /V window (hardware GO is pages 6 and 7).
 *   - Mode 8, section 5 palette, scroll-invariant sky/ground bands.
 *   - Camera lead (SCROLL_LEAD_L=80, SCROLL_LEAD_R=240) on a stub chopper
 *     that flies the world; no joystick.
 *   - Mountain strip: 4-row fill_band when this buffer's scroll changes
 *     (320 bytes, not a full-screen copy), then RLE mountain tiles at
 *     half-rate parallax.
 *   - Barracks, base, fence, flag from CHOPGFX via blit_rle; dirty lists
 *     with even byte widths.
 *   - Chopper from M3 rides the world in original coordinate space.
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

extern unsigned char *mountain_e[4];
extern unsigned char *mountain_o[4];
extern unsigned char *fence_e[5];
extern unsigned char *fence_o[5];
extern unsigned char  house_e[];
extern unsigned char  house_o[];
extern unsigned char  house_sill_e[];
extern unsigned char  house_sill_o[];
extern unsigned char  base_building_e[];
extern unsigned char  base_building_o[];
extern unsigned char  base_flagpole_e[];
extern unsigned char  base_flagpole_o[];
extern unsigned char  base_flag_00_e[];
extern unsigned char  base_flag_00_o[];
extern unsigned char  base_flag_01_e[];
extern unsigned char  base_flag_01_o[];

static int      opt_bios_flip   = 0;
static int      opt_wait        = 1;
static int      opt_force       = 0;
static int      opt_page_a      = -1;
static int      opt_page_b      = -1;
static unsigned opt_frames      = 2400U;    /* ~40 s at 60 Hz; a round trip */

#define MODEL_BYTE_SEG  0xF000U
#define MODEL_BYTE_OFF  0xFFFEU
#define MODEL_PCJR      0xFDU

#define HUD_ROWS        8
#define GROUND_TOP_ROW  (199 - 25)
#define MOUNTAIN_ROW    (199 - 29)

/* DESIGN.md section 4, verbatim except the 320/280 lead scale. */
#define BOUNDS_LEFT     720U
#define BOUNDS_RIGHT    4864U
#define SCROLL_END      688U
#define SCROLL_START    4616U
#define BASE_X          4724U
#define FENCE_X         4468U
#define FARHOUSE_X      896U
#define HOUSE_SPACING   256U
#define N_HOUSES        4
#define SCROLL_LEAD_L   80U
#define SCROLL_LEAD_R   240U
#define VIEW_WORLD_W    320U
#define HOUSE_WORLD_Y   36U
#define SILL_WORLD_Y    25U
#define BASE_BUILD_Y    34U
#define FLAGPOLE_Y      46U
#define CHOP_WORLD_Y    109U            /* screen row 90, mid-sky */
#define CHOP_SPEED      4U              /* world px / retrace; 2 screen px */

#define N_SIDE          11
#define N_HEAD          5
#define N_POSE          (N_SIDE + N_HEAD)
#define POSE_HOLD       10              /* retraces per body frame */
#define TAIL_DX         (-7)            /* screen px, left-facing stub */
#define TAIL_DY         5
#define ROTOR_DY        (-1)

/* Scenery + chopper (body, rotors) + a few extra mountain tiles. */
#define DIRTY_MAX       40

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
static unsigned scroll_x;

static const unsigned char fence_world_y[5] = { 17, 22, 25, 27, 28 };

static const unsigned char m4_palette[16] = {
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

static int xbyte_from_px(int x)
{
    if (x >= 0)
        return x / 2;
    return (x - 1) / 2;
}

static void plot_m8_byte(unsigned seg, unsigned off, unsigned char src)
{
    unsigned char d;

    if (src == 0)
        return;
    if ((src & 0x0F) != 0 && (src & 0xF0) != 0) {
        poke_byte(seg, off, src);
        return;
    }
    d = peek_byte(seg, off);
    if ((src & 0xF0) == 0)
        poke_byte(seg, off, (unsigned char)((d & 0xF0) | (src & 0x0F)));
    else
        poke_byte(seg, off, (unsigned char)((d & 0x0F) | (src & 0xF0)));
}

/* Walk one RLE row, writing only dest bytes in 0..79.  Returns the next row. */
static const unsigned char *blit_rle_row_clip(unsigned seg, unsigned y,
                                              int dest_b,
                                              const unsigned char *p)
{
    unsigned off_row;

    off_row = M8_ROW_OFF(y);
    for (;;) {
        unsigned skip = *p++;
        unsigned run  = *p++;

        if (skip == 0U && run == 0U)
            return p;
        dest_b += (int)skip;
        while (run != 0U) {
            unsigned char b = *p++;

            run--;
            if (dest_b >= 0 && dest_b < (int)M8_BYTES_PER_ROW)
                plot_m8_byte(seg, (unsigned)(off_row + (unsigned)dest_b), b);
            dest_b++;
        }
    }
}

static void blit_rle_clip(unsigned seg, int x_px, int y, unsigned char *s,
                          unsigned rows)
{
    const unsigned char *p;
    unsigned              r;
    int                   dest_b;

    p = s + 2;
    dest_b = xbyte_from_px(x_px);
    for (r = 0; r < rows; r++)
        p = blit_rle_row_clip(seg, (unsigned)(y + (int)r), dest_b, p);
}

static void blit_at(unsigned seg, int x_px, int y,
                    unsigned char *even, unsigned char *odd,
                    dirty_list *list)
{
    unsigned char *s;
    int            wpx, h, x1, vis0, vis1, xb, wb, xbyte0;

    if (y < (int)HUD_ROWS || y >= (int)M8_HEIGHT_PX)
        return;
    s = (x_px & 1) ? odd : even;
    wpx = (int)s[0];
    h   = (int)s[1];
    if (h <= 0 || wpx <= 0)
        return;
    if ((unsigned)(y + h) > M8_HEIGHT_PX)
        h = (int)M8_HEIGHT_PX - y;
    x1 = x_px + wpx;
    if (x1 <= 0 || x_px >= (int)M8_WIDTH_PX)
        return;

    vis0 = (x_px < 0) ? 0 : x_px;
    vis1 = (x1 > (int)M8_WIDTH_PX) ? (int)M8_WIDTH_PX : x1;
    xb   = vis0 / 2;
    wb   = (vis1 + 1) / 2 - xb;
    if (wb <= 0)
        return;

    xbyte0 = xbyte_from_px(x_px);
    if (xbyte0 >= 0 && (unsigned)(xbyte0 + wpx / 2) <= M8_BYTES_PER_ROW)
        blit_rle_m8(seg, (unsigned)xbyte0, (unsigned)y, data_seg(),
                    (unsigned)s);
    else
        blit_rle_clip(seg, x_px, y, s, (unsigned)h);

    dirty_add(list, (unsigned)xb, (unsigned)y, (unsigned)wb, (unsigned)h);
}

static int world_to_sx(unsigned wx)
{
    if (wx >= scroll_x)
        return (int)((wx - scroll_x) >> 1);
    return -(int)((scroll_x - wx) >> 1);
}

static int world_to_sy(unsigned wy)
{
    return (int)(199U - wy);
}

static void update_camera(unsigned chop_x, int dir)
{
    unsigned rel;

    if (chop_x >= scroll_x)
        rel = chop_x - scroll_x;
    else
        rel = 0;

    if (dir > 0) {
        if (rel > SCROLL_LEAD_R)
            scroll_x = chop_x - SCROLL_LEAD_R;
    } else {
        if (chop_x < scroll_x || rel < SCROLL_LEAD_L) {
            if (chop_x >= SCROLL_LEAD_L)
                scroll_x = chop_x - SCROLL_LEAD_L;
            else
                scroll_x = SCROLL_END;
        }
    }
    if (scroll_x > SCROLL_START)
        scroll_x = SCROLL_START;
    if (scroll_x < SCROLL_END)
        scroll_x = SCROLL_END;
}

static void blit_world(unsigned seg, unsigned wx, unsigned wy,
                       unsigned char *even, unsigned char *odd,
                       dirty_list *list)
{
    blit_at(seg, world_to_sx(wx), world_to_sy(wy), even, odd, list);
}

static void draw_mountains(unsigned seg, dirty_list *list)
{
    /* Half-rate parallax: one screen pixel of ridge per two of camera.
     * Tile the four 4-row CHOPGFX patterns across the 160-px window. */
    unsigned period;
    unsigned shift;
    int      x;
    int      i;
    int      w;

    period = 0;
    for (i = 0; i < 4; i++)
        period += mountain_e[i][0];
    if (period == 0U)
        return;
    shift = (scroll_x >> 2) % period;
    x = -(int)shift;
    i = 0;
    while (x < (int)M8_WIDTH_PX) {
        w = (int)mountain_e[i][0];
        blit_at(seg, x, (int)MOUNTAIN_ROW, mountain_e[i], mountain_o[i], list);
        x += w;
        i++;
        if (i >= 4)
            i = 0;
    }
}

static void draw_houses(unsigned seg, dirty_list *list)
{
    unsigned i;
    unsigned wx;

    for (i = 0; i < N_HOUSES; i++) {
        wx = FARHOUSE_X + i * HOUSE_SPACING;
        blit_world(seg, wx, HOUSE_WORLD_Y, house_e, house_o, list);
        blit_world(seg, wx, SILL_WORLD_Y, house_sill_e, house_sill_o, list);
    }
}

static unsigned fence_tower_x(int tower)
{
    long delta;
    long d;

    delta = (long)FENCE_X - (long)scroll_x - 0x8CL;
    if (delta & 1L)
        delta--;
    if (tower == 0)
        return (unsigned)((long)FENCE_X + (delta << 1));
    if (tower == 1)
        return (unsigned)((long)FENCE_X + delta);
    d = delta >> 1;
    if (tower == 2)
        return (unsigned)((long)FENCE_X + (d & ~1L));
    d >>= 1;
    if (tower == 3)
        return (unsigned)((long)FENCE_X + (d & ~1L));
    d >>= 1;
    return (unsigned)((long)FENCE_X + (d & ~1L));
}

static void draw_fence(unsigned seg, dirty_list *list)
{
    int t;

    /* Furthest (smallest) first, closest last -- original draw order. */
    for (t = 4; t >= 0; t--) {
        blit_world(seg, fence_tower_x(t), fence_world_y[t],
                   fence_e[t], fence_o[t], list);
    }
}

static void draw_base(unsigned seg, unsigned frame, dirty_list *list)
{
    unsigned bx = BASE_X + 0x49U;
    unsigned px = BASE_X + 0x71U;

    blit_world(seg, bx, BASE_BUILD_Y, base_building_e, base_building_o, list);
    blit_world(seg, px, FLAGPOLE_Y, base_flagpole_e, base_flagpole_o, list);
    if ((frame / 8U) & 1U)
        blit_world(seg, px + 2U, FLAGPOLE_Y, base_flag_01_e, base_flag_01_o,
                   list);
    else
        blit_world(seg, px + 2U, FLAGPOLE_Y, base_flag_00_e, base_flag_00_o,
                   list);
}

static void draw_scenery(unsigned seg, unsigned frame, dirty_list *list)
{
    draw_mountains(seg, list);
    draw_houses(seg, list);
    draw_fence(seg, list);
    draw_base(seg, frame, list);
}

static void draw_chopper(unsigned seg, int body_x, int body_y, unsigned pose,
                         unsigned main_i, unsigned tail_i, dirty_list *list)
{
    if (pose < (unsigned)N_SIDE) {
        unsigned char *be = chopper_side_e[pose];
        int bw, rw, rdx;

        blit_at(seg, body_x, body_y, be, chopper_side_o[pose], list);
        bw  = (int)be[0];
        rw  = (int)main_rotor_e[main_i][0];
        rdx = (bw - rw) / 2;
        blit_at(seg, body_x + rdx, body_y + ROTOR_DY,
                main_rotor_e[main_i], main_rotor_o[main_i], list);
        blit_at(seg, body_x + TAIL_DX, body_y + TAIL_DY,
                tail_rotor_e[tail_i], tail_rotor_o[tail_i], list);
    } else {
        unsigned hi = pose - (unsigned)N_SIDE;

        blit_at(seg, body_x, body_y,
                chopper_head_e[hi], chopper_head_o[hi], list);
    }
}

static void run_viewer(void)
{
    dirty_list lists[2];
    unsigned   last_scroll[2];
    unsigned   chop_x;
    int        dir;
    int        back;
    unsigned   back_seg;
    unsigned   i;
    unsigned   pose;
    unsigned   main_i, tail_i;
    int        sx, sy;
    int        scrolled;

    paint_world(seg_a);
    paint_world(seg_b);

    lists[0].n = 0;
    lists[1].n = 0;
    last_scroll[0] = 0xFFFFU;
    last_scroll[1] = 0xFFFFU;
    scroll_x = SCROLL_START;
    chop_x   = SCROLL_START + SCROLL_LEAD_R;
    dir      = -1;
    back     = 0;

    crt_page = page_b;
    set_pages(page_b, page_a);

    for (i = 0; i < opt_frames; i++) {
        back_seg = back ? seg_b : seg_a;

        if (dir < 0) {
            if (chop_x > BOUNDS_LEFT + CHOP_SPEED)
                chop_x -= CHOP_SPEED;
            else
                chop_x = BOUNDS_LEFT;
        } else {
            if (chop_x < BOUNDS_RIGHT - CHOP_SPEED)
                chop_x += CHOP_SPEED;
            else
                chop_x = BOUNDS_RIGHT;
        }
        if (dir < 0 && chop_x <= FARHOUSE_X)
            dir = 1;
        if (dir > 0 && chop_x >= BASE_X)
            dir = -1;

        update_camera(chop_x, dir);

        restore_list(back_seg, &lists[back]);

        scrolled = (scroll_x != last_scroll[back]);
        if (scrolled) {
            /* DESIGN.md section 7: 4 rows x 80 B mountain strip, not a
             * full-screen copy.  Sky and ground bands are scroll-invariant. */
            fill_band_m8(back_seg, MOUNTAIN_ROW, 4, M8_SOLID(8));
        }
        last_scroll[back] = scroll_x;

        pose   = (i / (unsigned)POSE_HOLD) % (unsigned)N_POSE;
        main_i = (i / 2U) % 3U;
        tail_i = (i / 2U) % 4U;

        draw_scenery(back_seg, i, &lists[back]);

        sx = world_to_sx(chop_x);
        sy = world_to_sy(CHOP_WORLD_Y);
        draw_chopper(back_seg, sx, sy, pose, main_i, tail_i, &lists[back]);

        vid_wait_retrace();
        crt_page = back ? page_b : page_a;
        set_pages(crt_page, back ? page_a : page_b);

        back = !back;

        if (kbhit()) {
            getch();
            break;
        }
    }
}

static void usage(void)
{
    printf(
"Choplifter! PCjr -- M4 world scroller.\n"
"\n"
"  M4 [options]\n"
"\n"
"  /bios        flip pages with int 10h AX=0583h instead of OUT to 0x3DF\n"
"  /batch       never wait for a keypress; run /frames then exit\n"
"  /frames=N    frames of motion, default 2400. Floor is 2.\n"
"  /pa=N /pb=N  force two page numbers in 1-7\n"
"  /force       run even if the BIOS model byte is not a PCjr's\n"
"  /?           this\n"
"\n"
"Attended visual: the chopper flies left from the base to the barracks\n"
"and back.  Camera lead (80 left / 240 right in world px).  Mountain\n"
"ridge parallaxes at half rate.  BROWN ground band, HUD in rows 0-7.\n"
"Barracks, fence towers, base and flag.  No full-screen copy.  Any key\n"
"skips the rest.\n");
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

    printf("Choplifter! PCjr -- M4 world scroller\n");
    printf("camera lead, mountain parallax, barracks/base/fence  ground %s\n\n",
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

    pause_for_key("About to set mode 8 and scroll the world");

    vid_set_mode(M8_MODE);
    mode_changed = 1;
    vid_set_palette16(m4_palette);
    crt_page = page_a;
    set_pages(page_a, page_b);

    run_viewer();

    vid_set_mode(orig_mode);
    mode_changed = 0;

    printf("\nDrew the scrolling world with blit_rle_m8 on pages %d/%d.\n",
           page_a, page_b);
    printf("PASS by eye: chopper flies left from the base (flag, concrete) "
           "with camera lead -- it sits right of centre, then the world "
           "scrolls; fence towers then four barracks; mountains parallax "
           "slower than the ground line; BROWN ground, HUD band untouched; "
           "turns around at the far house and flies home. No full-screen "
           "copy, no trail, no flicker, no venetian-blind bank error.\n");
    cleanup();
    return 0;
}
