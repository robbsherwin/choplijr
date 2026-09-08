/* m6.c -- Choplifter! for the IBM PCjr, milestone M6.
 *
 * DESIGN.md section 13: hostages.  M5's world, physics, 11-step tilt and
 * joystick, plus spawn / wave-run AI / board (cap 16) / unload at the pad
 * / rescue counters.  Fire is stubbed (M7).  Not tanks, jets or the death
 * cycle.  No Playdate rope: boarding is land-and-run, as in choplifter.s.
 *
 *   - BIOS mode 3 title (80-col): Joystick / Calibrate / Keyboard, then
 *     mode 8 for flight.  C opens the stick box (rest snapshot).  Default
 *     is Joystick.  Calibrate is never cyan.
 *   - Original VX/VY tables, gravity, thrust 0-15, ZP_TURN_STATE -5..+5.
 *   - Joystick primary: port 201h on model FD (Paku Paku 1.6a loop).
 *     INT 15h AH=84h only via /int15 -- original PCjr BIOS lacks it.
 *     /port201 forces the counting loop.  Joystick 1 only (Paku
 *     joyStick1Axis: bits 0,1).  Do not read stick B (bits 2,3).
 *     Buttons from port 201h bits 4-5 (stick 1).
 *   - Keyboard title mode: no analog (no 201h / INT 15h axes).  Keys from
 *     jrpiano3 INT 9 (port 60h make/break) plus PCjr INT 48 (NMI scan in
 *     AL).  WASD digital extremes, '.' rotate, '/' fire.  Arrows extra.
 *     Joystick mode keeps Paku 201h; WASD still override analog.  Space
 *     starts and does not fire in Keyboard.  S/A/D latch until a break bit.
 *     S wins over W (dump thrust).  On the pad, S sits; no grounding hop.
 *   - HUD rows 0-7: killed / aboard / rescued.  F1 or ` toggles the M5
 *     debug HUD instead (off by default).
 *   - In flight only, P teleports just right of the first (rightmost)
 *     barracks with that house off the left edge.
 *   - Simulation at 20 Hz (every 3rd retrace).  Present can be 60 Hz.
 *   - No full-screen copy.  Video fill is 44.8 ms/screen (M1 hardware).
 *
 * Ground colour stays the BROWN #define (pcjr.h).  Do not launch this in
 * DOSBox unless someone is watching: it is a visual, and Esc is the escape.
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

extern unsigned char *hostage_run_e[4];
extern unsigned char *hostage_run_o[4];
extern unsigned char *hostage_wave_e[3];
extern unsigned char *hostage_wave_o[3];
extern unsigned char *hostage_load_e[2];
extern unsigned char *hostage_load_o[2];

static int      opt_bios_flip   = 0;
static int      opt_wait        = 1;
static int      opt_force       = 0;
static int      opt_page_a      = -1;
static int      opt_page_b      = -1;
static int      opt_port201     = 0;        /* force port 201h */
static int      opt_int15       = 0;        /* force INT 15h AH=84h */
static int      opt_stick       = 0;        /* print raw min/max/now */
static int      stick_force     = 0;        /* 0 auto, 1 port201, 2 int15 */
static unsigned opt_frames      = 0;        /* 0 = until Esc */

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
/* choplifter.s handleGround ($770d): on-pad iff
 *   (chop_x - BASE_X) high byte == 0 and low byte in [$0D, $40).
 * Unload (spawn_hostages) keys off landed_base; barracks boarding is the
 * separate hhi < BASE_X_H gate and is not this window. */
#define PAD_DX0         0x0DU
#define PAD_DX1         0x40U
#define PAD_ROWS        5U              /* original renderBaseGrassSprite H */
#define DOOR_X          4821U
#define FENCE_X         4468U
#define FARHOUSE_X      896U
#define HOUSE_SPACING   256U
#define N_HOUSES        4
#define FIRST_HOUSE_X   (FARHOUSE_X + 3U * HOUSE_SPACING)  /* 1664, pad-side */
#define SCROLL_LEAD_L   80U
#define SCROLL_LEAD_R   240U
#define VIEW_WORLD_W    320U
#define HOUSE_WORLD_Y   36U
#define SILL_WORLD_Y    25U
#define BASE_BUILD_Y    34U
#define FLAGPOLE_Y      46U
#define BOUNDS_TOP      112U
#define LAND_POSY       25U
#define CHOP_GROUND_INIT 22U
#define MAX_SINK        6U
#define SIM_DIV         3U              /* 20 Hz sim from 60 Hz retrace */
#define MAX_HOSTAGES    16
#define HST_FREE        0xFF
#define HOSTAGE_WORLD_Y (CHOP_GROUND_INIT + 0x0BU)
#define BASE_X_H        0x12
#define DOOR_X_H        0x12
#define DOOR_X_L        0xD5

#define N_SIDE          11
#define N_HEAD          5
#define TAIL_DX         (-7)
#define TAIL_DY         5
#define ROTOR_DY        (-1)

#define APPLE_CENTER    128
#define STICK_DEADZONE  16              /* ±16 on 0-255 ≈ 12.5% throw */

#define SCAN_ESC        0x01
#define SCAN_W          0x11
#define SCAN_CTRL       0x1D
#define SCAN_A          0x1E
#define SCAN_S          0x1F
#define SCAN_D          0x20
#define SCAN_J          0x24
#define SCAN_K          0x25
#define SCAN_LSHIFT     0x2A
#define SCAN_C          0x2E
#define SCAN_V          0x2F
#define SCAN_P          0x19            /* debug: first barracks */
#define SCAN_PERIOD     0x34
#define SCAN_SLASH      0x35
#define SCAN_ALT        0x38
#define SCAN_SPACE      0x39
#define SCAN_UP         0x48
#define SCAN_LEFT       0x4B
#define SCAN_RIGHT      0x4D
#define SCAN_DOWN       0x50
#define SCAN_F1         0x3B
#define SCAN_GRAVE      0x29            /* ` */

#define TEXT_MODE       3
#define TEXT_COLS       80U
#define TEXT_ROWS       25U
#define TEXT_SEG        0xB800U
#define ATTR_WHITE      0x0F
#define ATTR_CYAN       0x0B            /* light cyan on black */

/* Mode 3 rows (0-24).  Six-row title block centred: (25-6)/2 = 9. */
#define TITLE_ROW_NAME  9
#define TITLE_ROW_SUB   10
#define TITLE_ROW_MENU  12
#define TITLE_ROW_PLAY  14
#define HELP_ROW_W      8
#define HELP_ROW_ASD    9
#define HELP_ROW_MOVE   12
#define HELP_ROW_TURN   13
#define HELP_ROW_CONT   22

/* Calibrate box: 31×11 inner (odd so rest lands on a cell), 33×13 with
 * border, centred.  Inner origin is (CAL_BOX_COL+1, CAL_BOX_ROW+1). */
#define CAL_INNER_W     31U
#define CAL_INNER_H     11U
#define CAL_BOX_W       (CAL_INNER_W + 2U)
#define CAL_BOX_H       (CAL_INNER_H + 2U)
#define CAL_BOX_COL     ((TEXT_COLS - CAL_BOX_W) / 2U)  /* 23 */
#define CAL_BOX_ROW     6
#define CAL_ROW_HINT    20
#define CAL_ROW_ESC     21

/* Scenery + chopper (body, rotors) + up to 16 hostages + mountain tiles. */
#define DIRTY_MAX       64

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
static int      kbd_hooked;

/* Original helicopter state (choplifter.s / zeropage.s). */
static int              velx, vely;
static unsigned         chop_x;
static unsigned         chop_y;
static signed char      turn_state;
static signed char      accelx;
static signed char      stickx;
static unsigned char    accely;
static unsigned char    airborne;
static unsigned char    landed;
static unsigned char    landed_base;
static unsigned char    grounding;
static unsigned char    sink_y;
static unsigned char    dying;
static unsigned char    death_timer;
static signed char      chop_face;
static signed char      turn_request;
static unsigned char    btn1_down;
static unsigned char    prefs_joy_x;    /* 0 = regular (eor 255), 1 = invert */
static unsigned char    prefs_joy_y;
static unsigned char    fire_stub;
static unsigned char    chop_ground;
static unsigned         sim_frame;
static unsigned         stick_cx0, stick_cy0;   /* rest: stick 1 / bits 0,1 */
static unsigned         calib_n;
static unsigned long    calib_sx0, calib_sy0;
static int              prev_ctrl_a, prev_ctrl_v;
static int              stick_live;
static int              stick_source;   /* 0 none, 1 port201, 2 int15 */
static unsigned         dbg_xmin, dbg_xmax, dbg_ymin, dbg_ymax;
static unsigned         dbg_x, dbg_y;
static unsigned         throw_xmin = 0xFFFFU, throw_xmax;
static unsigned         throw_ymin = 0xFFFFU, throw_ymax;
static int              play_keyboard;  /* 0 joystick (default), 1 keyboard */

/* hostageTable: anim $ff = free; action $00 wave, $ff run L, $01 run R,
 * $fe board R, $02 board L.  X is 16-bit world. */
static unsigned char    hostage_anim[MAX_HOSTAGES];
static unsigned char    hostage_act[MAX_HOSTAGES];
static unsigned         hostage_x[MAX_HOSTAGES];
static unsigned char    hostages_left;
static unsigned char    total_rescues;
static unsigned char    hostages_killed;
static unsigned char    hostages_active;
static unsigned char    hostages_loaded;
static unsigned char    base_runners;
static unsigned char    house_states[N_HOUSES];
static unsigned char    hostages_in_houses[N_HOUSES];
static unsigned char    chop_loaded;
static unsigned char    curr_level;
static unsigned char    rnd_s, rnd_seed;

/* HUD rows 0-7: K/A/R counts.  F1 or ` swaps in the M5 debug line. */
static unsigned char    hud_on = 0;
static unsigned char    hud_prev_tog;
static unsigned char    dbg_prev_p;
static unsigned char    hud_w, hud_s, hud_a, hud_d, hud_dot, hud_slash;
static signed char      hud_tilt;
static int              hud_vx, hud_vy;
static unsigned char    hud_accely;
static unsigned char    latch_a, latch_s, latch_d;
static unsigned char    pend_a, pend_s, pend_d;

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
    if (kbd_hooked) {
        kbd_unhook();
        kbd_hooked = 0;
    }
    if (mode_changed) {
        vid_set_mode(orig_mode);
        mode_changed = 0;
    }
}

static void text_hide_cursor(void);
#pragma aux text_hide_cursor = \
    "mov ah,1" \
    "mov cx,2000h" \
    "int 10h" \
    modify [ax cx];

static unsigned text_center_col(unsigned len)
{
    return (TEXT_COLS - len) / 2U;
}

static void text_put(unsigned row, unsigned col, unsigned char ch,
                     unsigned char attr)
{
    poke_word(TEXT_SEG, (unsigned)((row * TEXT_COLS + col) * 2U),
              (unsigned)ch | ((unsigned)attr << 8));
}

static void text_puts(unsigned row, unsigned col, const char *s,
                      unsigned char attr)
{
    while (*s != '\0')
        text_put(row, col++, (unsigned char)*s++, attr);
}

static void text_puts_center(unsigned row, const char *s, unsigned char attr)
{
    unsigned n;

    n = (unsigned)strlen(s);
    text_puts(row, text_center_col(n), s, attr);
}

static void text_clear(void)
{
    fill_words(TEXT_SEG, 0, TEXT_COLS * TEXT_ROWS, 0x0F20U);
}

/* Menu: "Joystick   Calibrate   Keyboard" -- 31 chars, col 24.
 * Joystick col 24, Calibrate col 35, Keyboard col 47. */
#define MENU_COL_JOY        24
#define MENU_COL_CAL        35
#define MENU_COL_KBD        47

static void draw_title(void)
{
    unsigned char joy_j;
    unsigned char kbd_k;

    text_clear();
    text_hide_cursor();
    text_puts_center(TITLE_ROW_NAME, "CHOPLIFTER!", ATTR_WHITE);
    text_puts_center(TITLE_ROW_SUB, "for IBM PCjr", ATTR_WHITE);

    joy_j = play_keyboard ? ATTR_WHITE : ATTR_CYAN;
    kbd_k = play_keyboard ? ATTR_CYAN : ATTR_WHITE;
    text_put(TITLE_ROW_MENU, MENU_COL_JOY, 'J', joy_j);
    text_puts(TITLE_ROW_MENU, MENU_COL_JOY + 1U, "oystick", ATTR_WHITE);
    text_puts(TITLE_ROW_MENU, MENU_COL_JOY + 8U, "   ", ATTR_WHITE);
    text_puts(TITLE_ROW_MENU, MENU_COL_CAL, "Calibrate", ATTR_WHITE);
    text_puts(TITLE_ROW_MENU, MENU_COL_CAL + 9U, "   ", ATTR_WHITE);
    text_put(TITLE_ROW_MENU, MENU_COL_KBD, 'K', kbd_k);
    text_puts(TITLE_ROW_MENU, MENU_COL_KBD + 1U, "eyboard", ATTR_WHITE);

    text_puts_center(TITLE_ROW_PLAY, "Press SPACE to play", ATTR_WHITE);
}

static void draw_cal_box(void)
{
    unsigned r, c;
    unsigned last_col = CAL_BOX_COL + CAL_BOX_W - 1U;
    unsigned last_row = CAL_BOX_ROW + CAL_BOX_H - 1U;

    text_put(CAL_BOX_ROW, CAL_BOX_COL, '+', ATTR_WHITE);
    text_put(CAL_BOX_ROW, last_col, '+', ATTR_WHITE);
    text_put(last_row, CAL_BOX_COL, '+', ATTR_WHITE);
    text_put(last_row, last_col, '+', ATTR_WHITE);
    for (c = 1U; c < CAL_BOX_W - 1U; c++) {
        text_put(CAL_BOX_ROW, CAL_BOX_COL + c, '-', ATTR_WHITE);
        text_put(last_row, CAL_BOX_COL + c, '-', ATTR_WHITE);
    }
    for (r = 1U; r < CAL_BOX_H - 1U; r++) {
        text_put(CAL_BOX_ROW + r, CAL_BOX_COL, '|', ATTR_WHITE);
        text_put(CAL_BOX_ROW + r, last_col, '|', ATTR_WHITE);
        for (c = 1U; c < CAL_BOX_W - 1U; c++)
            text_put(CAL_BOX_ROW + r, CAL_BOX_COL + c, ' ', ATTR_WHITE);
    }
}

static void draw_calibrate(void)
{
    text_clear();
    text_hide_cursor();
    draw_cal_box();
    text_puts_center(CAL_ROW_HINT,
                     "Center the stick, then SPACE or button to save",
                     ATTR_WHITE);
    text_puts_center(CAL_ROW_ESC, "Esc cancels", ATTR_WHITE);
}

static void draw_help(void)
{
    unsigned asd_col;

    text_clear();
    text_hide_cursor();
    asd_col = text_center_col(5U);          /* "A S D" */
    text_put(HELP_ROW_W, asd_col + 2U, 'W', ATTR_WHITE);
    text_put(HELP_ROW_ASD, asd_col, 'A', ATTR_WHITE);
    text_put(HELP_ROW_ASD, asd_col + 2U, 'S', ATTR_WHITE);
    text_put(HELP_ROW_ASD, asd_col + 4U, 'D', ATTR_WHITE);
    text_puts_center(HELP_ROW_MOVE, "WASD moves the chopper", ATTR_WHITE);
    text_puts_center(HELP_ROW_TURN,
                     ". turns the chopper and / shoots", ATTR_WHITE);
    text_puts_center(HELP_ROW_CONT, "Press SPACE to continue", ATTR_WHITE);
}

static int run_calibrate(void);

/* 1 = start flight, 0 = Esc to DOS.  C opens calibrate (does not start). */
static int run_title(void)
{
    int on_help;
    int space_need_up;
    int esc_need_up;
    int down_j, down_k, down_c, down_space, down_esc;
    int prev_j, prev_k, prev_c, prev_space, prev_esc;
    int held_space, held_esc;

    play_keyboard = 0;
    vid_set_mode(TEXT_MODE);
    mode_changed = 1;
    draw_title();

    on_help = 0;
    space_need_up = 0;
    esc_need_up = 0;
    prev_j = prev_k = prev_c = prev_space = prev_esc = 1; /* already-down */

    for (;;) {
        down_j     = kbd_is_down(SCAN_J);
        down_k     = kbd_is_down(SCAN_K);
        down_c     = kbd_is_down(SCAN_C);
        down_space = kbd_is_down(SCAN_SPACE);
        down_esc   = kbd_is_down(SCAN_ESC);

        /* Pulse counts as down so a make+break in one retrace still fires.
         * After help or calibrate, that same Space can stay latched (held
         * bit, or a typematic make after the break).  Drop the pulse, then
         * held_space is the make/break array alone. */
        kbd_clear_pulse();
        held_space = kbd_is_down(SCAN_SPACE);
        held_esc   = kbd_is_down(SCAN_ESC);

        if (on_help) {
            if (down_space && !prev_space) {
                on_help = 0;
                draw_title();
                space_need_up = 1;      /* release, then a new make, starts */
                while (kbhit())
                    getch();
            } else if (down_esc && !prev_esc)
                return 0;
        } else {
            if (down_k && !prev_k) {
                play_keyboard = 1;
                on_help = 1;
                draw_help();
            } else if (down_j && !prev_j) {
                play_keyboard = 0;
                draw_title();
            } else if (down_c && !prev_c) {
                if (run_calibrate())
                    play_keyboard = 0;  /* saved: Joystick (cyan J) */
                draw_title();
                space_need_up = 1;
                esc_need_up = 1;        /* cal Esc must not become title quit */
                while (kbhit())
                    getch();
            } else if (esc_need_up) {
                if (!held_esc)
                    esc_need_up = 0;
            } else if (down_esc && !prev_esc)
                return 0;
            else if (space_need_up) {
                if (!held_space) {
                    space_need_up = 0;
                    if (down_space)
                        return 1;       /* pulse-only tap; latch never sat up */
                }
            } else if (down_space && !prev_space)
                return 1;
        }

        prev_j     = down_j;
        prev_k     = down_k;
        prev_c     = down_c;
        prev_space = down_space;
        prev_esc   = down_esc;
        vid_wait_retrace();
    }
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

/* 4x6 glyphs, bit 3 = leftmost pixel.  Grey HUD (7), ink black (0). */
#define HUD_INK         0
#define HUD_GLYPH_W     4
#define HUD_GLYPH_H     6
#define HUD_CELL_W      5               /* 1 px gap; 32 cells = 160 px */
#define HUD_TEXT_Y      1

static const unsigned char hud_font_digit[10][HUD_GLYPH_H] = {
    { 0x6, 0x9, 0x9, 0x9, 0x9, 0x6 },   /* 0 */
    { 0x2, 0x6, 0x2, 0x2, 0x2, 0x7 },   /* 1 */
    { 0x6, 0x9, 0x1, 0x2, 0x4, 0xF },   /* 2 */
    { 0xE, 0x1, 0x6, 0x1, 0x9, 0x6 },   /* 3 */
    { 0x2, 0x6, 0xA, 0xF, 0x2, 0x2 },   /* 4 */
    { 0xF, 0x8, 0xE, 0x1, 0x9, 0x6 },   /* 5 */
    { 0x6, 0x8, 0xE, 0x9, 0x9, 0x6 },   /* 6 */
    { 0xF, 0x1, 0x2, 0x4, 0x4, 0x4 },   /* 7 */
    { 0x6, 0x9, 0x6, 0x9, 0x9, 0x6 },   /* 8 */
    { 0x6, 0x9, 0x9, 0x7, 0x1, 0x6 }    /* 9 */
};

static void hud_plot_px(unsigned seg, unsigned x, unsigned y, unsigned char c)
{
    unsigned off;
    unsigned char b, nib;

    if (x >= M8_WIDTH_PX || y >= HUD_ROWS)
        return;
    off = M8_ROW_OFF(y) + x / 2U;
    b = peek_byte(seg, off);
    nib = (unsigned char)(c & 0x0F);
    if (x & 1U)
        poke_byte(seg, off, (unsigned)((b & 0xF0) | nib));
    else
        poke_byte(seg, off, (unsigned)((b & 0x0F) | (nib << 4)));
}

static const unsigned char *hud_glyph(char ch)
{
    static const unsigned char g_plus[HUD_GLYPH_H]  = { 0x0, 0x4, 0xE, 0x4, 0x0, 0x0 };
    static const unsigned char g_minus[HUD_GLYPH_H] = { 0x0, 0x0, 0xF, 0x0, 0x0, 0x0 };
    static const unsigned char g_dot[HUD_GLYPH_H]   = { 0x0, 0x0, 0x0, 0x0, 0x6, 0x6 };
    static const unsigned char g_slash[HUD_GLYPH_H] = { 0x1, 0x2, 0x2, 0x4, 0x4, 0x8 };
    static const unsigned char g_A[HUD_GLYPH_H]     = { 0x6, 0x9, 0x9, 0xF, 0x9, 0x9 };
    static const unsigned char g_D[HUD_GLYPH_H]     = { 0xE, 0x9, 0x9, 0x9, 0x9, 0xE };
    static const unsigned char g_K[HUD_GLYPH_H]     = { 0x9, 0xA, 0xC, 0xC, 0xA, 0x9 };
    static const unsigned char g_R[HUD_GLYPH_H]     = { 0xE, 0x9, 0x9, 0xE, 0xA, 0x9 };
    static const unsigned char g_S[HUD_GLYPH_H]     = { 0x7, 0x8, 0x6, 0x1, 0x9, 0x6 };
    static const unsigned char g_T[HUD_GLYPH_H]     = { 0xF, 0x4, 0x4, 0x4, 0x4, 0x4 };
    static const unsigned char g_V[HUD_GLYPH_H]     = { 0x9, 0x9, 0x9, 0x9, 0x6, 0x6 };
    static const unsigned char g_W[HUD_GLYPH_H]     = { 0x9, 0x9, 0x9, 0xF, 0xF, 0x9 };
    static const unsigned char g_X[HUD_GLYPH_H]     = { 0x9, 0x9, 0x6, 0x6, 0x9, 0x9 };
    static const unsigned char g_Y[HUD_GLYPH_H]     = { 0x9, 0x9, 0x6, 0x4, 0x4, 0x4 };
    static const unsigned char g_blank[HUD_GLYPH_H] = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };

    if (ch >= '0' && ch <= '9')
        return hud_font_digit[ch - '0'];
    if (ch == '+')
        return g_plus;
    if (ch == '-')
        return g_minus;
    if (ch == '.')
        return g_dot;
    if (ch == '/')
        return g_slash;
    if (ch == 'A')
        return g_A;
    if (ch == 'D')
        return g_D;
    if (ch == 'K')
        return g_K;
    if (ch == 'R')
        return g_R;
    if (ch == 'S')
        return g_S;
    if (ch == 'T')
        return g_T;
    if (ch == 'V')
        return g_V;
    if (ch == 'W')
        return g_W;
    if (ch == 'X')
        return g_X;
    if (ch == 'Y')
        return g_Y;
    return g_blank;
}

static void hud_draw_char(unsigned seg, unsigned cx, char ch)
{
    const unsigned char *g;
    unsigned r, c, x;

    g = hud_glyph(ch);
    for (r = 0; r < HUD_GLYPH_H; r++) {
        for (c = 0; c < HUD_GLYPH_W; c++) {
            if ((g[r] & (unsigned char)(8U >> c)) == 0)
                continue;
            x = cx * HUD_CELL_W + c;
            hud_plot_px(seg, x, HUD_TEXT_Y + r, HUD_INK);
        }
    }
}

static void hud_put_signed(char *dst, int v)
{
    unsigned mag;
    unsigned n;
    char     tmp[6];
    unsigned i;

    n = 0;
    if (v < 0) {
        dst[n++] = '-';
        mag = (unsigned)(-v);
    } else {
        dst[n++] = '+';
        mag = (unsigned)v;
    }
    if (mag == 0U) {
        dst[n++] = '0';
        dst[n] = '\0';
        return;
    }
    i = 0;
    while (mag != 0U && i < 5U) {
        tmp[i++] = (char)('0' + (mag % 10U));
        mag /= 10U;
    }
    while (i > 0U)
        dst[n++] = tmp[--i];
    dst[n] = '\0';
}

static void draw_hud(unsigned seg)
{
    char buf[36];
    char ts[8], xs[8], ys[8];
    unsigned i;

    fill_band_m8(seg, 0, HUD_ROWS, M8_SOLID(7));
    if (hud_on) {
        hud_put_signed(ts, (int)hud_tilt);
        hud_put_signed(xs, hud_vx);
        hud_put_signed(ys, hud_vy);
        sprintf(buf, "W%u S%u A%u D%u T%s X%s Y%s A%u.%u/%u",
                (unsigned)hud_w, (unsigned)hud_s, (unsigned)hud_a,
                (unsigned)hud_d, ts, xs, ys, (unsigned)hud_accely,
                (unsigned)hud_dot, (unsigned)hud_slash);
    } else {
        sprintf(buf, "K%02u A%02u R%02u",
                (unsigned)hostages_killed, (unsigned)hostages_loaded,
                (unsigned)total_rescues);
    }
    for (i = 0; buf[i] != '\0' && i < 32U; i++)
        hud_draw_char(seg, i, buf[i]);
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

/* Apple arithmeticShiftRight16: abs, logical shift, restore sign (toward 0). */
static int asr16(int v, unsigned n)
{
    unsigned mag;
    int      neg;

    if (n == 0U)
        return v;
    neg = v < 0;
    if (neg)
        mag = (unsigned)(-v);
    else
        mag = (unsigned)v;
    mag >>= n;
    if (neg)
        return -(int)mag;
    return (int)mag;
}

static int s8_clamp(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static unsigned apple_deadzone(unsigned v)
{
    int d;

    d = (int)v - APPLE_CENTER;
    if (d < 0)
        d = -d;
    if (d <= (int)STICK_DEADZONE)
        return APPLE_CENTER;
    return v;
}

/* Paku rest is the count at SPACE.  Scale each side of rest onto 0-255
 * separately: raw 0 -> Apple 0, raw STICK_LIVE_MAX -> Apple 255.  The old
 * (raw-center)*128/center map only had a full 128 Apple units toward 0;
 * the high side was (timeout-center)*128/center, often a handful of units,
 * then ±16 deadzone ate down/right. */
static unsigned apple_span_from_raw(unsigned raw, unsigned center)
{
    long     d;
    int      a;
    unsigned span;

    if (center == 0U)
        return APPLE_CENTER;
    if (raw >= center) {
        span = (STICK_LIVE_MAX > center) ? (unsigned)(STICK_LIVE_MAX - center)
                                         : 1U;
        if (span == 0U)
            span = 1U;
        d = ((long)raw - (long)center) * 127L;
        d /= (long)span;
    } else {
        d = ((long)raw - (long)center) * 128L;
        d /= (long)center;
    }
    a = (int)APPLE_CENTER + (int)d;
    if (a < 0)
        a = 0;
    if (a > 255)
        a = 255;
    return (unsigned)a;
}

static unsigned apple_from_raw(unsigned raw, unsigned center)
{
    return apple_deadzone(apple_span_from_raw(raw, center));
}

/* Map raw onto 0..inner-1 with rest at the middle cell.  Same high side
 * as apple_span_from_raw (STICK_LIVE_MAX).  No deadzone — the star shows
 * the pot, not the flight bucket. */
static unsigned box_from_raw(unsigned raw, unsigned center, unsigned inner)
{
    unsigned a;

    if (inner <= 1U)
        return 0;
    a = apple_span_from_raw(raw, center);
    return (unsigned)((unsigned long)a * (inner - 1U) / 255UL);
}

static int axis_live(unsigned x, unsigned y)
{
    int xok, yok;

    xok = (x > 0U && x < STICK_LIVE_MAX);
    yok = (y > 0U && y < STICK_LIVE_MAX);
    return xok || yok;
}

/* Port 201h on a PCjr (or /port201); INT 15h only if forced/auto-fallback.
 * Joystick 1 only (Paku joyStick1Axis: bits 0,1).  *buttons is stick-1
 * bits 4-5 of port 201h, already mapped to nibble bits 0-1 (1 = pressed). */
static int read_stick_hardware(unsigned *x, unsigned *y,
                               unsigned *buttons, int *used_port)
{
    int live;
    int port;

    *x = *y = *buttons = 0;
    live = 0;
    port = (stick_force != 2);

    if (stick_force == 2) {
        live = (int)stick_read_bios(x, y, buttons);
        if (!live || (*x == 0U && *y == 0U)) {
            port = 1;
            live = 0;
        }
    } else if (stick_force == 0) {
        live = (int)stick_read_bios(x, y, buttons);
        if (!live || (*x == 0U && *y == 0U)) {
            port = 1;
            live = 0;
        } else
            port = 0;
    }

    if (port)
        live = (int)stick_read_port(x, y, buttons);
    *used_port = port;
    return live;
}

static void capture_stick_rest(int verbose);

static void dbg_sample(unsigned x, unsigned y)
{
    dbg_x = x;
    dbg_y = y;
    if (x < dbg_xmin)
        dbg_xmin = x;
    if (x > dbg_xmax)
        dbg_xmax = x;
    if (y < dbg_ymin)
        dbg_ymin = y;
    if (y > dbg_ymax)
        dbg_ymax = y;
}

static const unsigned accel_x_table[6] = {
    0x0000, 0x0060, 0x00B4, 0x0108, 0x015C, 0x01A4
};

static const signed char accel_y_table[16] = {
    -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 6
};

static const signed char facing_turn_table[12] = {
    1, 1, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1
};

static const int ground_settle_table[11] = {
    6, 5, 3, 2, 0, 0, -1, -2, -4, -5, -7
};

static const unsigned ground_bottom_table[11] = {
    10, 9, 9, 8, 8, 7, 7, 7, 7, 8, 8
};

/* choplifter.s rest: table[level]=7, skids at world 0 (Apple row 191).
 * Mode 8 maps world 0 to row 199; DESIGN's BROWN highlight is LAND_POSY
 * (row 174, band ~170-199).  Shift so level skids sit on that line.
 * Pitch deltas in the table stay as in the original. */
#define GROUND_BOTTOM_LEVEL 7U
#define GROUND_Y_SHIFT      (LAND_POSY - GROUND_BOTTOM_LEVEL)

static unsigned skid_rest_y(unsigned idx)
{
    return ground_bottom_table[idx] + GROUND_Y_SHIFT;
}

static void init_helicopter(void)
{
    turn_state   = 5;
    chop_face    = 1;
    turn_request = 0;
    accelx       = 0;
    stickx       = 0;
    accely       = 0;
    airborne     = 0;
    landed       = 1;
    landed_base  = 1;
    sink_y       = 0;
    grounding    = 0;
    dying        = 0;
    death_timer  = 0;
    btn1_down    = 0;
    fire_stub    = 0;
    velx         = 0;
    vely         = 0;
    chop_x       = 0x1293U;             /* BASE pad, choplifter.s initHelicopter */
    chop_y       = LAND_POSY;           /* skids on highlight; Apple rest was 7 */
    chop_ground  = CHOP_GROUND_INIT;
    latch_a = latch_s = latch_d = 0;
    pend_a = pend_s = pend_d = 0;
}

static void integrate_acceleration(void)
{
    int      scratch;
    int      ax;
    unsigned idx;
    int      vyh;
    int      dy;
    int      mag;
    int      sign;

    scratch = asr16(velx, 4);
    velx -= scratch;

    ax = stickx;
    /* Displayed side pitch is accelx (draw_chopper).  Original $7418 is
     * indexed by |ZP_STICKX|; ACCELX chases it.  Digital A/D writes both
     * so the sprite nibble is the vx nibble.  Prefer accelx when it has
     * left centre so a held tilt still accelerates if stickx glitched to 0. */
    if (accelx != 0)
        ax = accelx;
    {
        int signed_ax = ax;

        if (ax < 0)
            ax = -ax;
        idx = (unsigned)ax;
        if (idx > 5U)
            idx = 5U;
        if (signed_ax >= 0)
            velx -= (int)accel_x_table[idx];
        else
            velx += (int)accel_x_table[idx];
    }

    vyh = (int)(signed char)((unsigned)vely >> 8);
    dy  = (int)accel_y_table[accely] - vyh;
    sign = 0;
    mag  = dy;
    if (mag < 0) {
        mag = -mag;
        sign = 1;
    }
    if (mag >= 2)
        mag >>= 1;
    if (sign)
        mag = -mag;
    vyh += mag;
    vely = ((int)(signed char)vyh) << 8;

    {
        unsigned vh = (unsigned char)((unsigned)velx >> 8);

        if (vh >= 0x15U && vh < 0xEBU) {
            if (velx < 0)
                velx = -0x1500;
            else
                velx = 0x1500;
        }
    }
}

static void integrate_velocity(void)
{
    int      delta;
    int      step;
    unsigned ypos;

    delta = (int)accelx - (int)stickx;
    if (delta > 0) {
        accelx--;
        if (chop_x > 0U)
            chop_x--;
    } else if (delta < 0) {
        accelx++;
        chop_x++;
    }

    step = asr16(velx, 8);
    chop_x = (unsigned)((int)chop_x + step);
    if (chop_x < BOUNDS_LEFT) {
        chop_x = BOUNDS_LEFT;
        velx = 0;
    } else if (chop_x > BOUNDS_RIGHT) {
        chop_x = BOUNDS_RIGHT;
        velx = 0;
    }

    step = asr16(vely, 8);
    ypos = chop_y;
    if (step < 0) {
        unsigned down = (unsigned)(-step);

        if (down > ypos) {
            chop_y = 0;
            return;
        }
        chop_y = ypos - down;
    } else {
        chop_y = ypos + (unsigned)step;
        if (chop_y > BOUNDS_TOP) {
            chop_y = BOUNDS_TOP;
            vely = 0;
        }
    }
}

static void bounce_thrust(void)
{
    /* choplifter.s bounceThrust: add $04B0.  The extra ASR when VY is
     * upward almost always shifts by 0 (RE comment). */
    vely += 0x04B0;
}

static void death_bounce_cancel(void)
{
    int n = (int)dying + 3;

    vely = -asr16(vely, (unsigned)n);
}

static void ground_settle(int touch)
{
    int tx = touch;
    int ay = accelx;
    int ti, ai;
    int dx, dy;

    if (turn_state < 0) {
        tx = -tx;
        ay = -ay;
    }
    ti = s8_clamp(tx + 5, 0, 10);
    ai = s8_clamp(ay + 5, 0, 10);
    dx = ground_settle_table[ti] - ground_settle_table[ai];
    if (turn_state < 0)
        dx = -dx;
    chop_x = (unsigned)((int)chop_x + dx);
    dy = (int)ground_bottom_table[ti] - (int)ground_bottom_table[ai];
    chop_y = (unsigned)((int)chop_y + dy);
    accelx = (signed char)s8_clamp((int)accelx + (touch - (int)accelx), -5, 5);
}

static unsigned abs_accel_for_ground(void)
{
    int a = accelx;

    if (turn_state < 0)
        a = -a;
    return (unsigned)s8_clamp(a + 5, 0, 10);
}

static void handle_ground(void)
{
    unsigned idx;
    int      bottom;
    int      vy_neg_mag;
    unsigned nh, nl;

    if ((chop_y & 0x80U) != 0U) {
        airborne = 1;
        landed = 0;
        landed_base = 0;
        return;
    }
    if (grounding) {
        airborne = 0;
        goto detected;
    }

    idx = abs_accel_for_ground();
    bottom = (int)chop_y - (int)skid_rest_y(idx);
    if (bottom < 0) {
        chop_y = skid_rest_y(idx);
        goto detected;
    }
    if (bottom == 0)
        goto detected;

    airborne = 1;
    landed = 0;
    landed_base = 0;
    return;

detected:
    airborne = 0;
    {
        int dx = (int)chop_x - (int)BASE_X;

        landed_base = 0;
        if (dx >= (int)PAD_DX0 && dx < (int)PAD_DX1)
            landed_base = 1;
    }

    if (sink_y != 0) {
        if (sink_y >= MAX_SINK) {
            if (death_timer == 0)
                death_timer = 1;
        } else {
            sink_y++;
        }
        return;
    }
    if (dying) {
        sink_y = 1;
        return;
    }

    if (velx == 0) {
        if (accelx == 0)
            landed = 1;
        else
            landed = 0;
        return;
    }

    vy_neg_mag = (velx < 0) ? velx : -velx;
    nh = (unsigned char)((unsigned)vy_neg_mag >> 8);
    nl = (unsigned)vy_neg_mag & 0xFFU;
    if (nh < 0xEBU || (nh == 0xEBU && nl < 0x01U)) {
        sink_y = 1;
        dying = 1;
        return;
    }
    if (accelx == 0)
        landed = 1;
    else
        landed = 0;
}

static void chopper_physics(void)
{
    unsigned minh, minl;
    unsigned vyh, vyl;

    integrate_acceleration();
    if (airborne) {
        integrate_velocity();
        handle_ground();
        return;
    }

    /* Pad + dump thrust (S / stick down, accely=0..7): skip the
     * grounding hop only while the skids are on the pad.  table[0] is
     * -10 and would trip 0xFBA0 every other tick (HUD Y -1 then 0).
     * The 37156-byte skip zeroed vy whenever !airborne; takeoff did
     * not set the flag, so S in the air froze Y at +0.  If chop_y is
     * already above the skids, fall through as airborne so vy can go
     * negative and the chopper moves down. */
    if (accely < 8U) {
        unsigned gidx = abs_accel_for_ground();
        int bottom = (int)chop_y - (int)skid_rest_y(gidx);

        if (bottom > 0) {
            airborne = 1;
            landed = 0;
            landed_base = 0;
            integrate_velocity();
            handle_ground();
            return;
        }
        vely = 0;
        if (grounding) {
            grounding = 0;
            chop_y++;
        }
        if (accelx != 0)
            goto handle_x;
        velx = asr16(velx, 1);
        goto ground_done;
    }

    if (grounding) {
        grounding = 0;
        chop_y++;
        death_bounce_cancel();
        goto decay;
    }

    if (accelx != 0)
        goto handle_x;

    vyh = (unsigned char)((unsigned)vely >> 8);
    vyl = (unsigned)vely & 0xFFU;
    if ((vely & 0x8000) != 0) {
        minh = 0xFBU;
        minl = 0xA0U;
        if (vyh < minh || (vyh == minh && vyl < minl)) {
            grounding = 1;
            if (chop_y > 0U)
                chop_y--;
            bounce_thrust();
            goto ground_done;
        }
        death_bounce_cancel();
        goto decay;
    }
    bounce_thrust();
    goto decay;

handle_x:
    if (((accelx ^ (signed char)((unsigned)velx >> 8)) & 0x80) == 0)
        goto launch_check;

decay:
    velx = asr16(velx, 1);

launch_check:
    vyh = (unsigned char)((unsigned)vely >> 8);
    vyl = (unsigned)vely & 0xFFU;
    if ((vely & 0x8000) != 0 || vyh < 1U) {
        int scratch;
        int a;

        scratch = velx;
        a = accelx;
        if (a == 0)
            a = ((velx & 0x8000) != 0) ? 1 : -1;
        if (a > 0)
            scratch -= vely;
        else
            scratch += vely;
        scratch = asr16(scratch, 9);
        scratch = (int)accelx - scratch;
        if (((scratch ^ (int)accelx) & 0x8000) != 0 && accelx != 0)
            scratch = 0;
        scratch = s8_clamp(scratch, -5, 5);
        ground_settle(scratch);
        goto ground_done;
    }
    integrate_velocity();

    /* Original: jsr chopperPhysicsGroundCheck / jsr handleGround.
     * Without this, airborne stays 0 after takeoff (landed stays 1). */
ground_done:
    handle_ground();
}

static void process_chop_turn(void)
{
    int target;
    int cur;
    int diff;

    if (chop_face == 0)
        target = facing_turn_table[s8_clamp((int)accelx + 5, 0, 11)];
    else if (chop_face > 0)
        target = 5;
    else
        target = -5;

    if (target == (int)turn_state)
        return;

    cur  = (int)turn_state + 5;
    diff = cur - (target + 5);
    if (diff >= 0) {
        if (diff < 2)
            turn_state--;
        else
            turn_state = (signed char)(turn_state - 2);
    } else {
        if (diff > -2)
            turn_state++;
        else
            turn_state = (signed char)(turn_state + 2);
    }
    turn_state = (signed char)s8_clamp((int)turn_state, -5, 5);
}

static void check_rotate_button(int down)
{
    if (down && airborne && !dying) {
        if (!btn1_down) {
            btn1_down = 1;
            if (turn_state == 0 || turn_state == 1 || turn_state == -1) {
                turn_request = 0;
                chop_face = (stickx > 0) ? -1 : 1;
            } else {
                turn_request = turn_state;
                chop_face = (turn_state < 0) ? 1 : -1;
            }
        }
    } else if (btn1_down) {
        btn1_down = 0;
        if (turn_state == 0 || turn_state == 1 || turn_state == -1)
            chop_face = 0;
        else if (turn_request == 0 ||
                 ((turn_request ^ turn_state) & 0x80) != 0) {
            chop_face = (turn_state < 0) ? -1 : 1;
        } else {
            chop_face = 0;
        }
    }
    process_chop_turn();
}

static int map_stick_x(unsigned apple)
{
    int v;

    if (!prefs_joy_x)
        apple ^= 0xFFU;
    v = (int)(apple >> 4) - 2;
    if (v < 0)
        v = 0;
    if (v > 10)
        v = 10;
    return v - 5;
}

static unsigned map_stick_y(unsigned apple)
{
    if (!prefs_joy_y)
        apple ^= 0xFFU;
    return apple >> 4;
}

/* XT / PCjr 83-key, same as jrpiano3.asm note_tab: W=11h A=1Eh S=1Fh
 * D=20h, arrows 48/4B/4D/50, period 34h, slash 35h. */

/* Hold S/A/D until the key is up for a full sim tick after a break.
 * Extra makes (jrpiano kbd[]) keep the latch; make+break in the same tick
 * still counts as down via pulse. */
static void latch_until_break(unsigned char *lat, unsigned char *pend,
                              unsigned scan)
{
    if (kbd_held(scan) || kbd_is_down(scan)) {
        *lat = 1;
        *pend = 0;
        return;
    }
    if (*pend || kbd_broke(scan)) {
        if (*pend)
            *lat = 0;
        *pend = 1;
        return;
    }
    *pend = 1;
}

static void snapshot_raw_wasd(void)
{
    hud_w = (unsigned char)(kbd_is_down(SCAN_W) ? 1 : 0);
    hud_s = (unsigned char)(kbd_is_down(SCAN_S) ? 1 : 0);
    hud_a = (unsigned char)((!kbd_is_down(SCAN_CTRL) && kbd_is_down(SCAN_A))
                            ? 1 : 0);
    hud_d = (unsigned char)(kbd_is_down(SCAN_D) ? 1 : 0);
    hud_dot = (unsigned char)(kbd_is_down(SCAN_PERIOD) ? 1 : 0);
    hud_slash = (unsigned char)(kbd_is_down(SCAN_SLASH) ? 1 : 0);
}

static void read_wasd_arrows(int *key_x, int *key_y)
{
    latch_until_break(&latch_a, &pend_a, SCAN_A);
    latch_until_break(&latch_s, &pend_s, SCAN_S);
    latch_until_break(&latch_d, &pend_d, SCAN_D);

    *key_x = 0;
    *key_y = 0;
    if (kbd_is_down(SCAN_LEFT) || latch_a)
        (*key_x)--;
    if (kbd_is_down(SCAN_RIGHT) || latch_d)
        (*key_x)++;
    /* Ctrl-A is invert, not left.  Latch A still counts unless Ctrl is down. */
    if (kbd_is_down(SCAN_CTRL) && latch_a)
        (*key_x)++;                 /* undo the A we added above */
    /* S wins over W: dump thrust even if W is still down or pulsed.
     * W last used to cancel key_y to 0 and leave accely at hover 8. */
    if (kbd_is_down(SCAN_DOWN) || latch_s)
        *key_y = 1;
    else if (kbd_is_down(SCAN_UP) || kbd_is_down(SCAN_W))
        *key_y = -1;
}

/* Digital extremes skip the nibble tables.  Signs match Apple 0:
 * W/up = accely 15, S/down = 0, A/left = stickx +5, D/right = -5.
 * Also write accelx so the side-view pitch nibble is the same one that
 * feeds vx (original analog still lets ACCELX chase STICKX). */
static void apply_digital_axes(int key_x, int key_y)
{
    if (key_x < 0) {
        stickx = 5;
        accelx = 5;
    } else if (key_x > 0) {
        stickx = -5;
        accelx = -5;
    }
    /* Latch S (or key_y down) always dumps thrust.  Do not leave the
     * hover-8 default in place when W is also down. */
    if (latch_s || key_y > 0)
        accely = 0;
    else if (key_y < 0)
        accely = 15;
}

static void read_controls(void)
{
    unsigned x, y, buttons, ax, ay;
    int      live;
    int      used_port;
    int      key_x, key_y;
    int      rotate;
    int      ctrl_a, ctrl_v;

    snapshot_raw_wasd();

    /* Keyboard title mode: never sample 201h / INT 15h.  Buttons unused
     * ('.' / '/' instead of stick buttons / Space). */
    if (play_keyboard) {
        stick_live = 0;
        stick_source = 0;
        stickx = 0;
        accely = 8;                 /* gravity; not map_stick_y(128) */
        read_wasd_arrows(&key_x, &key_y);
        apply_digital_axes(key_x, key_y);

        ctrl_a = kbd_is_down(SCAN_CTRL) && kbd_is_down(SCAN_A);
        ctrl_v = kbd_is_down(SCAN_CTRL) && kbd_is_down(SCAN_V);
        if (ctrl_a && !prev_ctrl_a)
            prefs_joy_x = (unsigned char)(prefs_joy_x ^ 1U);
        if (ctrl_v && !prev_ctrl_v)
            prefs_joy_y = (unsigned char)(prefs_joy_y ^ 1U);
        prev_ctrl_a = ctrl_a;
        prev_ctrl_v = ctrl_v;

        if (dying || death_timer) {
            accely = 2;
            stickx = 0;
            accelx = 0;
        }

        fire_stub = 0;
        rotate = 0;
        if (kbd_is_down(SCAN_SLASH))
            fire_stub = 1;
        rotate = kbd_is_down(SCAN_PERIOD) || kbd_is_down(SCAN_ALT) ||
                 kbd_is_down(SCAN_LSHIFT);
        check_rotate_button(rotate);
        kbd_clear_pulse();
        return;
    }

    x = y = buttons = 0;
    live = 0;
    ax = APPLE_CENTER;
    ay = APPLE_CENTER;
    used_port = 0;

    live = read_stick_hardware(&x, &y, &buttons, &used_port);
    if (!axis_live(x, y))
        live = 0;

    if (live && calib_n < 8U) {
        calib_sx0 += x;
        calib_sy0 += y;
        calib_n++;
        if (calib_n == 8U) {
            stick_cx0 = (unsigned)(calib_sx0 / 8UL);
            stick_cy0 = (unsigned)(calib_sy0 / 8UL);
        }
    }

    if (live)
        dbg_sample(x, y);

    stick_live = live;
    stick_source = live ? (used_port ? 1 : 2) : 0;

    ax = APPLE_CENTER;
    ay = APPLE_CENTER;
    if (live && calib_n >= 8U) {
        ax = apple_from_raw(x, stick_cx0);
        ay = apple_from_raw(y, stick_cy0);
    }

    /* WASD/arrows override analog.  Stick at rest stays 128 / deadzone. */
    read_wasd_arrows(&key_x, &key_y);
    if (key_x < 0)
        ax = 0;
    else if (key_x > 0)
        ax = 255;
    else if (!live)
        ax = APPLE_CENTER;
    if (key_y < 0)
        ay = 0;
    else if (key_y > 0)
        ay = 255;
    else if (!live)
        ay = APPLE_CENTER;

    ctrl_a = kbd_is_down(SCAN_CTRL) && kbd_is_down(SCAN_A);
    ctrl_v = kbd_is_down(SCAN_CTRL) && kbd_is_down(SCAN_V);
    if (ctrl_a && !prev_ctrl_a)
        prefs_joy_x = (unsigned char)(prefs_joy_x ^ 1U);
    if (ctrl_v && !prev_ctrl_v)
        prefs_joy_y = (unsigned char)(prefs_joy_y ^ 1U);
    prev_ctrl_a = ctrl_a;
    prev_ctrl_v = ctrl_v;

    stickx = (signed char)map_stick_x(ax);
    accely = (unsigned char)map_stick_y(ay);
    apply_digital_axes(key_x, key_y);

    if (dying || death_timer) {
        accely = 2;
        stickx = 0;
        accelx = 0;
    }

    fire_stub = 0;
    rotate = 0;
    if ((buttons & 1U) != 0U || kbd_is_down(SCAN_SPACE))
        fire_stub = 1;
    rotate = ((buttons & 2U) != 0U) || kbd_is_down(SCAN_ALT) ||
             kbd_is_down(SCAN_LSHIFT);
    check_rotate_button(rotate);
    kbd_clear_pulse();
}

static void scroll_clamp(void)
{
    if (scroll_x > SCROLL_START)
        scroll_x = SCROLL_START;
    if (scroll_x < SCROLL_END)
        scroll_x = SCROLL_END;
}

static void scroll_lead_window(void)
{
    unsigned rel;

    if (chop_x >= scroll_x)
        rel = chop_x - scroll_x;
    else
        rel = 0;
    if (rel > SCROLL_LEAD_R) {
        if (chop_x >= SCROLL_LEAD_R)
            scroll_x = chop_x - SCROLL_LEAD_R;
        else
            scroll_x = SCROLL_END;
    } else if (chop_x < scroll_x || rel < SCROLL_LEAD_L) {
        if (chop_x >= SCROLL_LEAD_L)
            scroll_x = chop_x - SCROLL_LEAD_L;
        else
            scroll_x = SCROLL_END;
    }
}

static void scroll_terrain(void)
{
    int vxh;

    if (landed_base) {
        scroll_x += 4U;
        scroll_clamp();
        return;
    }

    vxh = (int)(signed char)((unsigned)velx >> 8);
    if (!airborne ||
        (turn_state > 0 && vxh < 0) ||
        (turn_state < 0 && vxh >= 0)) {
        scroll_lead_window();
        scroll_clamp();
        return;
    }

    {
        int      proj;
        unsigned lead;
        unsigned target;
        unsigned step;
        unsigned mag;

        proj = (int)chop_x + asr16(velx, 5);
        lead = (velx < 0) ? SCROLL_LEAD_L : SCROLL_LEAD_R;
        if (proj >= (int)lead)
            target = (unsigned)(proj - (int)lead);
        else
            target = SCROLL_END;
        mag = (unsigned)asr16(velx < 0 ? -velx : velx, 8);
        step = mag + 4U;
        if (target > scroll_x) {
            if ((unsigned)(target - scroll_x) < step)
                scroll_x = target;
            else
                scroll_x += step;
        } else if (target < scroll_x) {
            if ((unsigned)(scroll_x - target) < step)
                scroll_x = target;
            else
                scroll_x -= step;
        }
        scroll_clamp();
    }
}

static unsigned char rnd8(void)
{
    rnd_s = (unsigned char)(rnd_s * 17U + 31U + rnd_seed);
    rnd_seed++;
    return rnd_s;
}

static int hostage_alloc(void)
{
    int i;

    for (i = MAX_HOSTAGES - 1; i >= 0; i--) {
        if (hostage_anim[i] == HST_FREE)
            return i;
    }
    return -1;
}

static void kill_hostage(int i)
{
    hostage_anim[i] = HST_FREE;
    if (hostages_killed < 255U)
        hostages_killed++;
    if (hostages_active != 0U)
        hostages_active--;
}

static void spawn_one_in_field(int house)
{
    int i;

    i = hostage_alloc();
    if (i < 0)
        return;
    if (hostages_in_houses[house] != 0U)
        hostages_in_houses[house]--;
    if (hostages_left != 0U)
        hostages_left--;
    hostages_active++;
    hostage_anim[i] = (unsigned char)(rnd8() & 3U);
    if (rnd8() & 1U)
        hostage_act[i] = 1;             /* run right */
    else
        hostage_act[i] = 0xFF;          /* run left */
    /* choplifter.s spawnNewHostageInField: FARHOUSE_X_L+$0A, house+FARHOUSE_X_H */
    hostage_x[i] = (unsigned)((FARHOUSE_X & 0xFFU) + 0x0AU) +
                   (((unsigned)house + (FARHOUSE_X >> 8)) << 8);
}

static void spawn_unload(void)
{
    int i;

    i = hostage_alloc();
    if (i < 0)
        return;
    if (total_rescues < 255U)
        total_rescues++;
    if (hostages_loaded != 0U)
        hostages_loaded--;
    base_runners++;
    if (chop_loaded != 0U) {
        chop_loaded = 0;
        if (curr_level < 3U)
            curr_level++;
    }
    hostage_anim[i] = (unsigned char)(rnd8() & 3U);
    hostage_act[i] = 1;                 /* run right to DOOR_X */
    hostage_x[i] = (chop_x + 4U) | 1U;
}

static void spawn_hostages(void)
{
    unsigned char dh;
    int house, start;

    if ((unsigned)base_runners + (unsigned)hostages_active >= 16U)
        return;
    if (landed_base) {
        if (hostages_loaded == 0U)
            return;
        spawn_unload();
        return;
    }
    if (hostages_left == 0U)
        return;
    if (hostages_active >= 5U)
        return;

    dh = (unsigned char)((chop_x >> 8) - (FARHOUSE_X >> 8));
    if (dh < 4U) {
        house = (int)dh;
        if (house_states[house] != 0U && hostages_in_houses[house] != 0U) {
            spawn_one_in_field(house);
            return;
        }
    }
    start = (int)(rnd8() & 3U);
    house = start;
    do {
        if (house_states[house] != 0U && hostages_in_houses[house] != 0U) {
            spawn_one_in_field(house);
            return;
        }
        house++;
        if (house >= (int)N_HOUSES)
            house = 0;
    } while (house != start);
}

static void hostage_board(int i, unsigned char act)
{
    hostage_act[i] = act;
    hostage_anim[i] = 0;
    hostages_loaded++;
    if (hostages_active != 0U)
        hostages_active--;
    chop_loaded = 0xFF;
}

static void update_one_hostage(int i)
{
    unsigned char hhi, chi, dhi, act, scratch;
    int           dx;

    if (hostage_anim[i] == HST_FREE)
        return;

    hhi = (unsigned char)(hostage_x[i] >> 8);
    chi = (unsigned char)(chop_x >> 8);
    dhi = (unsigned char)(hhi - chi);
    if (!(dhi == 0U || dhi < 3U || dhi >= 0xFEU)) {
        if (hhi >= BASE_X_H) {
            if (base_runners != 0U)
                base_runners--;
            hostage_anim[i] = HST_FREE;
        }
        return;
    }

    act = hostage_act[i];
    if (act == 0xFF)
        hostage_x[i] -= 2U;
    else if (act == 1U)
        hostage_x[i] += 2U;

    hostage_anim[i]++;
    if (hostage_anim[i] >= 4U) {
        hostage_anim[i] = 0;
        if (dying) {
            if (hhi >= BASE_X_H)
                hostage_act[i] = 1;
            else if ((int)hostage_x[i] - (int)chop_x < 0)
                hostage_act[i] = 0xFF;  /* run away left */
            else
                hostage_act[i] = 1;
        } else if (!airborne && hhi >= BASE_X_H) {
            if ((rnd8() & 0x29U) == 0U)
                hostage_act[i] = 0;
            else
                hostage_act[i] = 1;
        } else if (!airborne && !landed_base && hostages_loaded < 16U) {
            if ((int)hostage_x[i] - (int)chop_x >= 0)
                hostage_act[i] = 0xFF;
            else
                hostage_act[i] = 1;
        } else if ((rnd8() & 0x09U) == 0U) {
            if (hostage_act[i] == 0U) {
                if (hhi >= BASE_X_H || (rnd_s & 0x20U) == 0U)
                    hostage_act[i] = 1;
                else
                    hostage_act[i] = 0xFF;
            } else {
                hostage_act[i] = 0;
            }
        }
    }

    if (hhi == DOOR_X_H && (unsigned char)hostage_x[i] >= DOOR_X_L) {
        if (base_runners != 0U)
            base_runners--;
        hostage_anim[i] = HST_FREE;
        return;
    }

    if (!landed || dying)
        return;

    if (hhi >= BASE_X_H)
        return;
    if ((hostage_act[i] & 3U) == 2U)
        return;

    dx = (int)hostage_x[i] - (int)chop_x;
    if (dx >= 0 && dx <= 255) {
        scratch = (unsigned char)dx;
        if (scratch < 8U) {
            kill_hostage(i);
            return;
        }
        if (scratch < 10U) {
            if (hostages_loaded >= 16U) {
                hostage_anim[i] = 0;
                hostage_act[i] = 1;
            } else {
                hostage_board(i, 0xFE);
            }
        }
    } else if (dx < 0 && dx >= -256) {
        scratch = (unsigned char)dx;
        if (scratch >= 0xF7U) {
            kill_hostage(i);
            return;
        }
        if (scratch >= 0xF5U) {
            if (hostages_loaded >= 16U) {
                hostage_anim[i] = 0;
                hostage_act[i] = 0xFF;
            } else {
                hostage_board(i, 2);
            }
        }
    }
}

static void finish_boarding(int i)
{
    unsigned char act;

    if (hostage_anim[i] == HST_FREE)
        return;
    act = hostage_act[i];
    if (act == 0U || (act & 1U) != 0U)
        return;
    if (hostage_anim[i] == 3U || !landed)
        hostage_anim[i] = HST_FREE;
}

static void update_hostages(void)
{
    int i;

    for (i = 0; i < MAX_HOSTAGES; i++) {
        update_one_hostage(i);
        finish_boarding(i);
    }
}

static void init_hostages(void)
{
    int i;
    unsigned x;
    unsigned char n;

    total_rescues = 0;
    hostages_killed = 0;
    hostages_active = 0;
    hostages_loaded = 0;
    base_runners = 0;
    hostages_left = 0x40;
    curr_level = 0;
    chop_loaded = 0;
    rnd_s = 1;
    rnd_seed = 1;
    for (i = 0; i < (int)N_HOUSES; i++) {
        house_states[i] = 0;
        hostages_in_houses[i] = 0x10;
    }
    for (i = 0; i < MAX_HOSTAGES; i++)
        hostage_anim[i] = HST_FREE;

    hostages_active = 8;
    hostages_left = (unsigned char)(hostages_left - 8U);
    hostages_in_houses[3] = (unsigned char)(hostages_in_houses[3] - 8U);
    house_states[3] = 1;
    x = FARHOUSE_X + 0x03D0U;
    n = 0;
    for (i = 0; i < 8; i++) {
        hostage_anim[i] = n;
        hostage_act[i] = 0;
        hostage_x[i] = x;
        n++;
        if (n >= 4U)
            n = 0;
        x += (unsigned)((rnd8() & 0x7EU) + 0x20U);
    }
}

static void murder_aboard(void)
{
    unsigned n;

    n = hostages_loaded;
    if ((unsigned)hostages_killed + n > 255U)
        hostages_killed = 255;
    else
        hostages_killed = (unsigned char)(hostages_killed + n);
    hostages_loaded = 0;
    chop_loaded = 0;
}

/* Just right of the pad-side barracks: house entirely off the left
 * (x + blit_world_w <= scroll_x), chopper on-screen at left lead.
 * Face left so airborne velx=0 uses the lead window and the camera
 * does not walk toward right-lead (that would reveal the house). */
static void debug_teleport_first_barracks(void)
{
    unsigned house_w;

    house_w = (unsigned)house_e[0] << 1;    /* 16 screen px -> 32 world */
    scroll_x = FIRST_HOUSE_X + house_w;     /* 1696 */
    chop_x = scroll_x + SCROLL_LEAD_L;      /* 1776 */
    chop_y = LAND_POSY + 16U;
    velx = 0;
    vely = 0;
    accelx = 0;
    stickx = 0;
    airborne = 1;
    landed = 0;
    landed_base = 0;
    grounding = 0;
    sink_y = 0;
    dying = 0;
    death_timer = 0;
    turn_state = -5;
    chop_face = -1;
    turn_request = 0;
}

static void sim_tick(void)
{
    read_controls();
    chopper_physics();
    if (dying && sink_y >= MAX_SINK) {
        murder_aboard();
        init_helicopter();
        scroll_x = SCROLL_START;
    }
    scroll_terrain();
    if ((sim_frame & 3U) == 0U)
        spawn_hostages();
    update_hostages();
    sim_frame++;
    hud_tilt = accelx;
    hud_vx = velx;
    hud_vy = (int)(signed char)((unsigned)vely >> 8);
    hud_accely = accely;
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

/* Light-grey concrete band on the brown, same world X as landed_base.
 * CHOPGFX grass/sidewalk are not in sprdata_world (grass is a $2A/$55
 * blitRect at BASE_X+$07, sidewalks sit at +$3F / +$7B beside the
 * building).  Index 7 is the section 5 "base concrete" role. */
static void draw_pad(unsigned seg, dirty_list *list)
{
    int      sx0, sx1, vis0, vis1, xb, x1b, wb;
    unsigned rw, xbu;

    sx0 = world_to_sx(BASE_X + PAD_DX0);
    sx1 = world_to_sx(BASE_X + PAD_DX1);
    if (sx1 <= 0 || sx0 >= (int)M8_WIDTH_PX)
        return;
    vis0 = (sx0 < 0) ? 0 : sx0;
    vis1 = (sx1 > (int)M8_WIDTH_PX) ? (int)M8_WIDTH_PX : sx1;
    xb   = vis0 / 2;
    x1b  = (vis1 + 1) / 2;
    wb   = x1b - xb;
    if (wb <= 0)
        return;
    rw = (unsigned)((wb + 1) & ~1);
    xbu = (unsigned)xb;
    if ((unsigned)(xbu + rw) > M8_BYTES_PER_ROW) {
        if (rw > M8_BYTES_PER_ROW)
            return;
        xbu = (unsigned)(M8_BYTES_PER_ROW - rw);
    }
    fill_rect_m8(seg, xbu, GROUND_TOP_ROW, rw, PAD_ROWS, M8_SOLID(7));
    dirty_add(list, xbu, GROUND_TOP_ROW, rw, PAD_ROWS);
}

static void draw_base(unsigned seg, unsigned frame, dirty_list *list)
{
    unsigned bx = BASE_X + 0x49U;
    unsigned px = BASE_X + 0x71U;

    draw_pad(seg, list);
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

static void packed_row_flip(unsigned char *row, int wpx)
{
    int i, j;
    unsigned char tmp[16];
    unsigned char p;
    int wbytes;

    if (wpx <= 0 || wpx > 32)
        return;
    wbytes = (wpx + 1) / 2;
    memcpy(tmp, row, (unsigned)wbytes);
    memset(row, 0, (unsigned)wbytes);
    for (i = 0; i < wpx; i++) {
        j = wpx - 1 - i;
        if (j & 1)
            p = (unsigned char)(tmp[j / 2] & 0x0F);
        else
            p = (unsigned char)(tmp[j / 2] >> 4);
        if (i & 1)
            row[i / 2] = (unsigned char)((row[i / 2] & 0xF0) | p);
        else
            row[i / 2] = (unsigned char)((row[i / 2] & 0x0F) | (p << 4));
    }
}

static void blit_at_flip(unsigned seg, int x_px, int y,
                         unsigned char *even, unsigned char *odd,
                         dirty_list *list)
{
    unsigned char *s;
    const unsigned char *p;
    unsigned char row[16];
    int wpx, h, r, dest_b, vis0, vis1, xb, wb, x1;
    unsigned skip, run, col, off_row;

    if (y < (int)HUD_ROWS || y >= (int)M8_HEIGHT_PX)
        return;
    s = (x_px & 1) ? odd : even;
    wpx = (int)s[0];
    h   = (int)s[1];
    if (h <= 0 || wpx <= 0 || wpx > 32)
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

    p = s + 2;
    dest_b = xbyte_from_px(x_px);
    for (r = 0; r < h; r++) {
        memset(row, 0, sizeof(row));
        col = 0;
        for (;;) {
            skip = *p++;
            run  = *p++;
            if (skip == 0U && run == 0U)
                break;
            col += skip;
            while (run != 0U) {
                if (col < 16U)
                    row[col] = *p;
                p++;
                col++;
                run--;
            }
        }
        packed_row_flip(row, wpx);
        off_row = M8_ROW_OFF((unsigned)(y + r));
        for (col = 0; col < (unsigned)((wpx + 1) / 2); col++) {
            int db = dest_b + (int)col;

            if (db >= 0 && db < (int)M8_BYTES_PER_ROW)
                plot_m8_byte(seg, (unsigned)(off_row + (unsigned)db),
                             row[col]);
        }
    }
    dirty_add(list, (unsigned)xb, (unsigned)y, (unsigned)wb, (unsigned)h);
}

static void draw_chopper(unsigned seg, dirty_list *list)
{
    int            body_x, body_y, h, bw, rw, rdx, tdx;
    unsigned       main_i, tail_i;
    unsigned char *be;
    unsigned char *oe;
    int            side;
    int            abs_t;
    int            pitch;

    abs_t = turn_state;
    if (abs_t < 0)
        abs_t = -abs_t;

    h = 18;
    if (abs_t == 5) {
        pitch = accelx;
        if (turn_state < 0)
            pitch = -pitch;
        pitch = s8_clamp(pitch + 5, 0, 10);
        be = chopper_side_e[pitch];
        oe = chopper_side_o[pitch];
        h  = (int)be[1];
        side = 1;
    } else {
        unsigned hi = (unsigned)abs_t;
        if (hi > 4U)
            hi = 4U;
        be = chopper_head_e[hi];
        oe = chopper_head_o[hi];
        h  = (int)be[1];
        side = 0;
    }

    body_x = world_to_sx(chop_x);
    {
        unsigned top = chop_y + (unsigned)h;
        if (top > 0U)
            top--;
        if (sink_y != 0U && top > sink_y)
            top -= sink_y;
        body_y = world_to_sy(top);
    }

    main_i = (sim_frame / 2U) % 3U;
    tail_i = (sim_frame / 2U) % 4U;

    if (side && turn_state < 0)
        blit_at_flip(seg, body_x, body_y, be, oe, list);
    else
        blit_at(seg, body_x, body_y, be, oe, list);

    if (side) {
        bw  = (int)be[0];
        rw  = (int)main_rotor_e[main_i][0];
        rdx = (bw - rw) / 2;
        blit_at(seg, body_x + rdx, body_y + ROTOR_DY,
                main_rotor_e[main_i], main_rotor_o[main_i], list);
        tdx = TAIL_DX;
        if (turn_state < 0)
            tdx = bw - (int)tail_rotor_e[tail_i][0] - TAIL_DX;
        blit_at(seg, body_x + tdx, body_y + TAIL_DY,
                tail_rotor_e[tail_i], tail_rotor_o[tail_i], list);
    }
}

static unsigned wave_sprite(unsigned char anim)
{
    unsigned t = (unsigned)anim;
    unsigned y;

    y = ((t << 1) ^ 0xFFU) & t;
    if (y > 2U)
        y = 2U;
    return y;
}

static void draw_hostages(unsigned seg, dirty_list *list)
{
    int i, sx, sy;
    unsigned char act, anim, fi;
    unsigned char *ee;
    unsigned char *oo;
    int flip;

    sy = world_to_sy(HOSTAGE_WORLD_Y);
    for (i = 0; i < MAX_HOSTAGES; i++) {
        anim = hostage_anim[i];
        if (anim == HST_FREE)
            continue;
        act = hostage_act[i];
        flip = (act & 0x80U) != 0U;
        if (act == 0U) {
            fi = (unsigned char)wave_sprite(anim);
            ee = hostage_wave_e[fi];
            oo = hostage_wave_o[fi];
        } else if (act & 1U) {
            fi = (unsigned char)(anim & 3U);
            ee = hostage_run_e[fi];
            oo = hostage_run_o[fi];
        } else {
            fi = (unsigned char)((anim & 2U) ? 1U : 0U);
            ee = hostage_load_e[fi];
            oo = hostage_load_o[fi];
        }
        sx = world_to_sx(hostage_x[i] - 4U);
        if (flip)
            blit_at_flip(seg, sx, sy, ee, oo, list);
        else
            blit_at(seg, sx, sy, ee, oo, list);
    }
}

static void run_viewer(void)
{
    dirty_list lists[2];
    unsigned   last_scroll[2];
    int        back;
    unsigned   back_seg;
    unsigned   i;
    unsigned   limit;
    int        scrolled;

    paint_world(seg_a);
    paint_world(seg_b);

    lists[0].n = 0;
    lists[1].n = 0;
    last_scroll[0] = 0xFFFFU;
    last_scroll[1] = 0xFFFFU;
    scroll_x = SCROLL_START;
    init_helicopter();
    init_hostages();
    hud_prev_tog = 0;
    dbg_prev_p = 1;             /* ignore P held through SPACE */
    hud_on = 0;
    dbg_xmin = dbg_ymin = 0xFFFFU;
    dbg_xmax = dbg_ymax = 0;
    dbg_x = dbg_y = 0;
    sim_frame = 0;
    back = 0;

    crt_page = page_b;
    set_pages(page_b, page_a);

    limit = opt_frames;
    if (limit == 0U)
        limit = 0xFFFFU;

    for (i = 0; i < limit; i++) {
        back_seg = back ? seg_b : seg_a;

        {
            unsigned tog = 0;

            if (kbd_is_down(SCAN_F1) || kbd_is_down(SCAN_GRAVE))
                tog = 1;
            if (tog && !hud_prev_tog)
                hud_on = (unsigned char)(hud_on ^ 1U);
            hud_prev_tog = (unsigned char)tog;
            if (kbd_is_down(SCAN_P) && !dbg_prev_p)
                debug_teleport_first_barracks();
            dbg_prev_p = (unsigned char)(kbd_is_down(SCAN_P) ? 1 : 0);
        }

        if ((i % SIM_DIV) == 0U)
            sim_tick();

        restore_list(back_seg, &lists[back]);

        scrolled = (scroll_x != last_scroll[back]);
        if (scrolled) {
            fill_band_m8(back_seg, MOUNTAIN_ROW, 4, M8_SOLID(8));
        }
        last_scroll[back] = scroll_x;

        draw_scenery(back_seg, sim_frame, &lists[back]);
        draw_chopper(back_seg, &lists[back]);
        draw_hostages(back_seg, &lists[back]);
        draw_hud(back_seg);

        vid_wait_retrace();
        crt_page = back ? page_b : page_a;
        set_pages(crt_page, back ? page_a : page_b);

        back = !back;

        if (kbd_is_down(SCAN_ESC))
            break;
    }
}

static void usage(void)
{
    printf(
"Choplifter! PCjr -- M6 hostages.\n"
"\n"
"  M6 [options]\n"
"\n"
"  /bios        flip pages with int 10h AX=0583h instead of OUT to 0x3DF\n"
"  /port201     force port 201h axes (Paku loop).  Default on a PCjr.\n"
"  /int15       force INT 15h AH=84h (not in original PCjr BIOS)\n"
"  /stick       print raw joystick-1 counts, then min/max/now after Esc\n"
"  /batch       never wait for a keypress; run /frames then exit\n"
"  /frames=N    retraces then exit. 0 (default, attended) means until Esc.\n"
"               /batch with no /frames uses 1800 retraces.\n"
"  /pa=N /pb=N  force two page numbers in 1-7\n"
"  /force       run even if the BIOS model byte is not a PCjr's\n"
"  /?           this\n"
"\n"
"Stick: Paku Paku 1.6a port 201h loop (CLI, bits high, timeout 7FFFh).\n"
"Joystick 1 only (first connector / Paku stick A, bits 0,1).  Analog\n"
"mapped to the original Apple paddle tables.  Each side of rest is\n"
"scaled to the full 0-255 Apple range (raw 0 = 0, timeout = 255)\n"
"before a +/-16 deadzone around 128.  The X table then has a one-nibble\n"
"centre bucket.  Buttons are stick 1 only (port 201h bits 4-5).\n"
"\n"
"Title (BIOS mode 3): default Joystick (cyan J).  J selects Joystick,\n"
"K selects Keyboard and shows WASD help, C opens the calibrate box\n"
"(SPACE or stick-1 button snapshots rest; Esc cancels).  SPACE on the\n"
"title starts (mode 8).  Esc on the title quits to DOS.  Stick button\n"
"does not start from the title.  Keyboard flight: WASD (arrows extra),\n"
"'.' rotate, '/' fire.  Joystick flight: analog plus WASD/arrows,\n"
"button 0 fire stub, button 1 / Alt / Left Shift rotate.\n"
"Ctrl-A / Ctrl-V invert\n"
"axes.  Esc ends play.  HUD is Killed / Aboard / Rescued.  F1 or `\n"
"toggles the debug HUD (off by default).  Sim is 20 Hz; the screen may\n"
"flip faster.  Land next to waving hostages to board (cap 16); land on\n"
"the base pad to unload.  Eight start at the far barracks; more leave\n"
"the burning house.  Intact barracks wait for M7 tanks.\n");
}

static int parse_args(int argc, char **argv)
{
    int i;
    int saw_frames;

    saw_frames = 0;
    for (i = 1; i < argc; i++) {
        char *a = argv[i];

        if (*a == '/' || *a == '-')
            a++;

        if (strcmp(a, "bios") == 0)
            opt_bios_flip = 1;
        else if (strcmp(a, "port201") == 0) {
            opt_port201 = 1;
            stick_force = 1;
        } else if (strcmp(a, "int15") == 0) {
            opt_int15 = 1;
            stick_force = 2;
        } else if (strcmp(a, "stick") == 0)
            opt_stick = 1;
        else if (strcmp(a, "batch") == 0)
            opt_wait = 0;
        else if (strcmp(a, "force") == 0)
            opt_force = 1;
        else if (strncmp(a, "frames=", 7) == 0) {
            opt_frames = (unsigned)atoi(a + 7);
            saw_frames = 1;
        } else if (strncmp(a, "pa=", 3) == 0)
            opt_page_a = atoi(a + 3);
        else if (strncmp(a, "pb=", 3) == 0)
            opt_page_b = atoi(a + 3);
        else {
            usage();
            return 0;
        }
    }
    if (!opt_wait && !saw_frames)
        opt_frames = 1800U;
    return 1;
}

static void print_stick_probe(void)
{
    unsigned x, y, pb, bx, by, bb;
    int      bios_ok;
    const char *src;

    x = y = pb = bx = by = bb = 0;
    stick_read_port(&x, &y, &pb);
    bios_ok = (int)stick_read_bios(&bx, &by, &bb);
    if (stick_force == 1)
        src = "port 201h";
    else if (stick_force == 2)
        src = "INT 15h AH=84h";
    else
        src = "auto (port 201h on PCjr, else INT 15h then port)";
    printf("stick 1 x=%u y=%u %s; btn=%u; INT15 x=%u y=%u %s\n",
           x, y, axis_live(x, y) ? "live" : "idle",
           pb, bx, by, bios_ok ? "ok" : "CF");
    printf("axes %s; deadzone +/-%u/255 around 128; joystick 1 = bits 0,1\n",
           src, STICK_DEADZONE);
    printf("Plug joystick 1 (first connector).  Centre, then SPACE on the\n"
           "title.  C opens calibrate.  Up = thrust, down = descend,\n"
           "left/right = tilt and translate.  Ctrl-A / Ctrl-V invert X / Y.\n"
           "Esc on the title quits to DOS.\n");
}

/* 1 = 8-sample rest saved, 0 = Esc (rest unchanged).  Joystick 1 only
 * (bits 0,1).  Display rest is saved rest, or an 8-sample used only for
 * the star (not written) if they have never saved. */
static int run_calibrate(void)
{
    unsigned x, y, buttons;
    unsigned map_cx, map_cy;
    unsigned cx, cy;
    unsigned i, n0;
    unsigned long sx0, sy0;
    int      used_port, live;
    int      down_space, down_esc, btn;
    int      prev_space, prev_esc, prev_btn;
    int      space_need_up, held_space, save;
    int      star_col, star_row, prev_col, prev_row;
    unsigned inner_x, inner_y;

    draw_calibrate();

    map_cx = stick_cx0;
    map_cy = stick_cy0;
    if (map_cx == 0U && map_cy == 0U) {
        n0 = 0;
        sx0 = sy0 = 0;
        for (i = 0; i < 8U; i++) {
            read_stick_hardware(&x, &y, &buttons, &used_port);
            if (axis_live(x, y)) {
                sx0 += x;
                sy0 += y;
                n0++;
            }
        }
        if (n0 != 0U) {
            map_cx = (unsigned)(sx0 / n0);
            map_cy = (unsigned)(sy0 / n0);
        }
    }

    prev_col = (int)(CAL_BOX_COL + 1U + CAL_INNER_W / 2U);
    prev_row = (int)(CAL_BOX_ROW + 1U + CAL_INNER_H / 2U);
    text_put((unsigned)prev_row, (unsigned)prev_col, '*', ATTR_WHITE);

    prev_space = prev_esc = prev_btn = 1;
    space_need_up = 1;                  /* leftover latch: release, then tap */

    for (;;) {
        read_stick_hardware(&x, &y, &buttons, &used_port);
        live = axis_live(x, y);
        if (live) {
            if (x < throw_xmin)
                throw_xmin = x;
            if (x > throw_xmax)
                throw_xmax = x;
            if (y < throw_ymin)
                throw_ymin = y;
            if (y > throw_ymax)
                throw_ymax = y;
            if (map_cx == 0U && map_cy == 0U) {
                map_cx = x;
                map_cy = y;
            }
            cx = map_cx;
            cy = map_cy;
            inner_x = box_from_raw(x, cx, CAL_INNER_W);
            inner_y = box_from_raw(y, cy, CAL_INNER_H);
            star_col = (int)(CAL_BOX_COL + 1U + inner_x);
            star_row = (int)(CAL_BOX_ROW + 1U + inner_y);
            if (star_col != prev_col || star_row != prev_row) {
                text_put((unsigned)prev_row, (unsigned)prev_col, ' ',
                         ATTR_WHITE);
                text_put((unsigned)star_row, (unsigned)star_col, '*',
                         ATTR_WHITE);
                prev_col = star_col;
                prev_row = star_row;
            }
        }

        down_space = kbd_is_down(SCAN_SPACE);
        down_esc   = kbd_is_down(SCAN_ESC);
        btn        = (buttons != 0);
        kbd_clear_pulse();
        held_space = kbd_is_down(SCAN_SPACE);

        /* Esc here is cancel-to-title only.  Drain BIOS and wait for the
         * break so title Esc (exit) does not see the same make. */
        if (down_esc && !prev_esc) {
            while (kbhit())
                getch();
            for (;;) {
                kbd_clear_pulse();
                if (!kbd_is_down(SCAN_ESC))
                    break;
                vid_wait_retrace();
            }
            while (kbhit())
                getch();
            return 0;
        }

        save = 0;
        if (btn && !prev_btn)
            save = 1;
        else if (space_need_up) {
            if (!held_space) {
                space_need_up = 0;
                if (down_space)
                    save = 1;           /* pulse-only tap; latch never sat up */
            }
        } else if (down_space && !prev_space)
            save = 1;

        if (save) {
            capture_stick_rest(0);
            for (;;) {
                read_stick_hardware(&x, &y, &buttons, &used_port);
                kbd_clear_pulse();
                if (!kbd_is_down(SCAN_SPACE) && buttons == 0U)
                    break;
                vid_wait_retrace();
            }
            while (kbhit())
                getch();
            return 1;
        }

        prev_space = down_space;
        prev_esc   = down_esc;
        prev_btn   = btn;
        vid_wait_retrace();
    }
}

static void capture_stick_rest(int verbose)
{
    unsigned x, y, b;
    unsigned i, n0;

    n0 = 0;
    calib_sx0 = calib_sy0 = 0;
    for (i = 0; i < 8U; i++) {
        if (stick_force == 2)
            stick_read_bios(&x, &y, &b);
        else
            stick_read_port(&x, &y, &b);
        if (axis_live(x, y)) {
            calib_sx0 += x;
            calib_sy0 += y;
            n0++;
        }
    }
    if (n0 != 0U) {
        stick_cx0 = (unsigned)(calib_sx0 / n0);
        stick_cy0 = (unsigned)(calib_sy0 / n0);
    }
    calib_n = 8U;
    if (verbose)
        printf("  rest stick 1 %u,%u (%u)\n", stick_cx0, stick_cy0, n0);
}

int main(int argc, char **argv)
{
    unsigned char model;

    setbuf(stdout, NULL);

    if (!parse_args(argc, argv))
        return 1;

    printf("Choplifter! PCjr -- M6 hostages\n");
    printf("spawn, board 16, unload at base, rescue HUD  ground %s\n\n",
           GROUND_COLOUR_NAME);

    model = peek_byte(MODEL_BYTE_SEG, MODEL_BYTE_OFF);
    printf("BIOS model byte at F000:FFFE ... %02X (%s)\n", model,
           model == MODEL_PCJR ? "IBM PCjr" : "not a PCjr");
    if (model != MODEL_PCJR && !opt_force) {
        printf("Refusing to run.  Pass /force on an emulator you do not mind "
               "crashing.\n");
        return 3;
    }
    if (stick_force == 0 && model == MODEL_PCJR)
        stick_force = 1;

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

    print_stick_probe();

    while (kbhit())
        getch();

    kbd_hook();
    kbd_hooked = 1;

    if (opt_wait) {
        if (!run_title()) {
            vid_set_mode(orig_mode);
            mode_changed = 0;
            kbd_unhook();
            kbd_hooked = 0;
            printf("Quit from title (Esc).  Did not fly.\n");
            cleanup();
            return 0;
        }
        while (kbd_is_down(SCAN_SPACE))
            ;
        kbd_clear_pulse();
        if (!play_keyboard)
            capture_stick_rest(0);
    } else {
        play_keyboard = 0;
        capture_stick_rest(opt_stick);
    }

    vid_set_mode(M8_MODE);
    mode_changed = 1;
    vid_set_palette16(m4_palette);
    crt_page = page_a;
    set_pages(page_a, page_b);

    run_viewer();

    vid_set_mode(orig_mode);
    mode_changed = 0;
    kbd_unhook();
    kbd_hooked = 0;

    printf("\nFlew with blit_rle_m8 on pages %d/%d.  Sim 20 Hz (every %u "
           "retraces).  Controls: %s.  Stick source this run: %s",
           page_a, page_b, SIM_DIV,
           play_keyboard ? "Keyboard" : "Joystick",
           stick_source == 1 ? "port 201h" :
           stick_source == 2 ? "INT 15h" : "keys only");
    printf(".\n");
    if (opt_stick)
        printf("/stick 1 x min/max/now %u/%u/%u  y %u/%u/%u\n",
               dbg_xmin, dbg_xmax, dbg_x,
               dbg_ymin, dbg_ymax, dbg_y);
    printf("PASS by eye: title then chopper on the base pad, facing right;\n"
           "up/W lifts, down/S descends, left/A and right/D tilt and\n"
           "translate; camera lead 80/240; mountains parallax; BROWN\n"
           "ground.  HUD rows 0-7 show Killed / Aboard / Rescued (F1 or `\n"
           "toggles debug).  Land among the barracks hostages: they run to\n"
           "the skids and board (cap 16).  Land back on the pad: they run\n"
           "to the door and R counts up.  No full-screen copy, no trail,\n"
           "no flicker.  Fire is a stub.  Esc ends.\n");
    cleanup();
    return 0;
}
