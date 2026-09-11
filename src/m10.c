/* m10.c -- Choplifter! for the IBM PCjr, milestone M10.
 *
 * DESIGN.md section 13: polish.  M9 presentation plus hardware
 * validation (workset byte counts, optional Zen-timer of one present).
 * Present path: flipped sprites are pre-encoded RLE (_fe / _fo) so they
 * go through blit_rle_m8; sheared main-rotor frames are the same (3 ink
 * x 11 tilt x flip, cropped to ink); sky dither is one fill_rows_m8 walk;
 * stars/moon only redraw in restored rects.  Static camera restamps scenery
 * and ridge tiles only where restore punched holes; a scroll still reblits
 * visible scenery and records it for the next restore.
 * Palette cycling is still ahead.  No attract loop (section 15).
 * Title PVM music is still optional and not here.
 * No Playdate rope: boarding is land-and-run, as in choplifter.s.
 *
 *   - BIOS mode 3 title (80-col): Joystick / Calibrate / Keyboard, then
 *     mode 8 logos (Broderbund, Choplifter, Dan Gorlin, mission), then
 *     flight.  C opens the stick box (rest snapshot).  Default is
 *     Joystick.  Calibrate is never cyan.
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
 *   - HUD rows 0-7: 24x8 BCD bubbles (killed / aboard / rescued).  F1
 *     or ` toggles the M5 debug HUD instead (off by default).
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
#include "snd.h"

extern unsigned char *chopper_side_e[11];
extern unsigned char *chopper_side_o[11];
extern unsigned char *chopper_side_fe[11];
extern unsigned char *chopper_side_fo[11];
extern unsigned char *chopper_head_e[5];
extern unsigned char *chopper_head_o[5];
extern unsigned char *chopper_head_fe[5];
extern unsigned char *chopper_head_fo[5];
extern unsigned char *rotor_tilt_e[3][11];
extern unsigned char *rotor_tilt_o[3][11];
extern unsigned char *rotor_tilt_fe[3][11];
extern unsigned char *rotor_tilt_fo[3][11];
extern signed char    rotor_tilt_ox[3][11];
extern signed char    rotor_tilt_oy[3][11];
extern signed char    rotor_tilt_fox[3][11];
extern signed char    rotor_tilt_foy[3][11];
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
extern unsigned char *hostage_run_fe[4];
extern unsigned char *hostage_run_fo[4];
extern unsigned char *hostage_wave_e[3];
extern unsigned char *hostage_wave_o[3];
extern unsigned char *hostage_wave_fe[3];
extern unsigned char *hostage_wave_fo[3];
extern unsigned char *hostage_load_e[2];
extern unsigned char *hostage_load_o[2];
extern unsigned char *hostage_load_fe[2];
extern unsigned char *hostage_load_fo[2];

extern unsigned char *jet_e[25];
extern unsigned char *jet_fe[25];
extern unsigned char *tank_cannon_e[5];
extern unsigned char *explosion_e[5];
extern unsigned char *alien_e[4];
extern unsigned char *house_fire_e[2];
extern unsigned char  tank_turret_e[];
extern unsigned char  house_burn_e[];
extern unsigned char  house_debris_e[];
extern unsigned char  chop_rubble_e[];
extern unsigned char  hostage_die_e[];
extern unsigned char  bullet_chop_e[];
extern unsigned char  bullet_chop_o[];
extern unsigned char  bullet_missile_e[];
extern unsigned char  bullet_missile_o[];
extern unsigned char  bullet_missile_fe[];
extern unsigned char  bullet_missile_fo[];
extern unsigned char  bullet_bomb_e[];
extern unsigned char  bullet_bomb_o[];
extern unsigned char  muzzle_e[];
extern unsigned char  muzzle_o[];

extern unsigned char  title_mission[];
extern unsigned char  title_logo[];
extern unsigned char  title_broderbund[];
extern unsigned char  title_gorlin[];
extern unsigned char  title_the_end[];
extern unsigned char  title_crown[];
extern unsigned char *sortie_banner_e[3];

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
static unsigned opt_ztimer      = 0;        /* sim_frame to sample; 0 = off */
static unsigned opt_retrace     = 0;        /* retrace-probe length; 0 = off */
static int      opt_forceclip   = 0;        /* debug: every blit_at takes
                                              * blit_rle_clip, not blit_rle_m8.
                                              * Not a real mode -- isolates the
                                              * fast-vs-slow blit path cost. */
static int      opt_forcefire   = 0;        /* debug: every in-bounds blit_at
                                              * takes blit_rle_m8_fire, as if
                                              * blit_fire were set, without
                                              * touching real game state.
                                              * Not a real mode -- isolates
                                              * the fire-remap fast path. */
static int      opt_noworkset   = 0;        /* debug: skip ws_add_blit's
                                              * rle_run_bytes() re-parse of
                                              * every blitted sprite -- pure
                                              * diagnostic cost, redundant
                                              * with the blit itself.  Not a
                                              * real mode; WORKSET/ZTIMER
                                              * byte counts print as 0. */
static int      opt_phases      = 0;        /* off by default: the ph_*
                                              * phase-timing brackets each
                                              * call bios_ticks() (a far call
                                              * into dosmem.asm), real cost
                                              * on every sim tick whether or
                                              * not anyone reads the report.
                                              * Diagnostic only; pass this to
                                              * get the PHASE ticks report
                                              * back, otherwise none of that
                                              * overhead runs at all -- not
                                              * even during ordinary play. */

#define RETRACE_DEFAULT 180U            /* 3 s if a field costs one field */

/* Workset (DESIGN.md section 7 / 14).  Byte counts every sim present.
 * /ztimer wraps restore+draw of one present with the Zen timer.  Derived
 * microseconds use M1.LOG rates and are analysis, not a new measurement. */
#define WS_K_SPRITE     0
#define WS_K_MTN        1
#define WS_K_SCENERY    2
#define M1_NS_FILL_RECT 2935UL          /* fill_rect_m8, DESIGN.md section 2 */
#define M1_NS_MOVSB     4515UL          /* RAM->video movsb (blit_rle) */
#define ZTIMER_DEFAULT  4U              /* first present with a real restore */

static unsigned char    ws_kind;
static unsigned long    ws_t_restore, ws_t_mtn_fill, ws_t_mtn_blit;
static unsigned long    ws_t_scenery, ws_t_sprite;
static unsigned         ws_n, ws_n_scroll;
static unsigned long    ws_sum_restore, ws_sum_blit, ws_sum_mtn, ws_sum_scenery;
static unsigned long    ws_sum_sprite, ws_sum_video;
static unsigned long    ws_min_restore, ws_max_restore;
static unsigned long    ws_min_blit, ws_max_blit;
static unsigned long    ws_min_video, ws_max_video;
static unsigned         ws_z_did, ws_z_ovf, ws_z_frame, ws_z_ticks;
static unsigned long    ws_z_restore, ws_z_blit, ws_z_video;

#define MODEL_BYTE_SEG  0xF000U
#define MODEL_BYTE_OFF  0xFFFEU
#define MODEL_PCJR      0xFDU

#define HUD_ROWS        8
#define HUD_FIELD       10              /* light green; Apple HUD is green */
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
/* Tread bottom = LAND_POSY + GROUND - this.  8 sat them on the
 * highlight; 13 is two rows under the first "a little high" nudge. */
#define TANK_GROUND_BIAS 13U
/* One shell in the air at a time (Atari 8-bit feel: ~2 shots/s).  After
 * impact the boom plays, then a beat, then the next shot.  Four shots
 * make a volley; then 2 s at 20 Hz before another.  Apple was one shot
 * every 8–23 ticks with no volley rest. */
#define TANK_BURST_N        4U
#define TANK_SHOT_BEAT      5U          /* ticks after impact; boom is 5 */
#define TANK_BURST_COOL     40U         /* 2.0 s */
#define TANK_SHOT_NONE      0xFFU
#define CHOP_GROUND_INIT 22U
#define MAX_SINK        6U
#define SIM_DIV         3U              /* 20 Hz sim from 60 Hz retrace */
#define SIM_HZ          20U
#define STICK_DEAD_PROBES 8U            /* dead probes before backing off */
#define MAX_HOSTAGES    16
#define MAX_ENT         28
#define MAX_SHOTS       5
#define ET_FREE         0
#define ET_BOOM         1
#define ET_SINK         2
#define ET_TANK         3
#define ET_MISSILE      4
#define ET_BOMB         5
#define ET_BULLET       6
#define ET_SHELL        8
#define ET_JET          9
#define ET_ALIEN        10
#define MAX_SORTIE      3
#define HST_FREE        0xFF
#define HST_DIE         0x40            /* crush flash; choplifter.s $40 */
#define CRUSH_TICKS     2U              /* die sprite + blood, then free */
#define HOSTAGE_WORLD_Y (CHOP_GROUND_INIT + 0x0BU)
#define BASE_X_H        0x12
#define DOOR_X_H        0x12
#define DOOR_X_L        0xD5

#define N_SIDE          11
#define N_HEAD          5
#define TAIL_DX         (-3)            /* hub on the tail boom, not past it */
#define TAIL_DY         1               /* boom is ~row 4 of the 13-row side */
#define ROTOR_DY        (-1)
/* Tank / saucer shot: 2x2 screen px.  Index 7 is light grey; 6 is the
 * CGA brown that reads orange on a CRT (same register as the ground). */
#define SHELL_PX        2U
#define SHELL_WORLD_W   (SHELL_PX * 2U)
#define SHELL_C_GREY    7
#define SHELL_C_ORANGE  6
#define CHOP_HIT_W      18U             /* screen px; 2 world px each */
#define CHOP_HIT_H      18U
#define CHOP_WORLD_W    (CHOP_HIT_W * 2U)
#define ROTOR_FORWARD   3               /* nose-ward of body centre */

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

/* Scenery + chopper + hostages + tanks/jets/shots/explosions. */
#define DIRTY_MAX       96

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
static unsigned char    fire_down;
static unsigned char    fire_held;
static unsigned char    chop_ground;
static unsigned         sim_frame;
static unsigned         stick_cx0, stick_cy0;   /* rest: stick 1 / bits 0,1 */
static unsigned         calib_n;
static unsigned long    calib_sx0, calib_sy0;
static int              prev_ctrl_a, prev_ctrl_v, prev_ctrl_s;
static int              stick_live;
static int              stick_source;   /* 0 none, 1 port201, 2 int15 */
static unsigned char    stick_seen_live;        /* a live sample ever arrived */
static unsigned char    stick_dead_run;         /* consecutive dead probes */
static unsigned char    stick_skip;             /* ticks left before re-probe */
static unsigned char    blit_fire;              /* remap yellow 14 -> fire */
static unsigned char    snd_did_board, snd_did_kill, snd_did_boom;
static unsigned         dbg_xmin, dbg_xmax, dbg_ymin, dbg_ymax;
static unsigned         dbg_x, dbg_y;
static unsigned         throw_xmin = 0xFFFFU, throw_xmax;
static unsigned         throw_ymin = 0xFFFFU, throw_ymax;
static int              play_keyboard;  /* 0 joystick (default), 1 keyboard */

/* hostageTable: anim $ff = free; action $00 wave, $ff run L, $01 run R,
 * $fe board R, $02 board L, $40 crush (dying sprite one tick).  X is 16-bit. */
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
static unsigned char    scenery_full[2];
static unsigned char    last_flag_bit[2];
static unsigned char    last_fire_bit[2];
/* Tentative redeclarations: next_sortie() (below) needs to reset these the
 * same way run_viewer()'s setup does, but the real definitions sit next to
 * the rest of run_viewer()'s per-page state, later in this file. */
static dirty_list        lists[2];
static dirty_list        scenery_mark[2];
static unsigned char    chop_loaded;
static unsigned char    curr_level;
static unsigned char    curr_shots;
static unsigned char    sortie;
static unsigned         banner_left;
static unsigned char    game_over;
static unsigned char    end_kind;       /* 0 none, 1 win, 2 lose */
static unsigned char    end_left;       /* overlay ticks left */
static unsigned char    viewer_quit;    /* 1 = Esc from play or logos */
static unsigned char    crash_fx;
static unsigned char    num_tanks, num_jets, num_aliens;
static unsigned char    tank_ids[4], jet_ids[4], alien_ids[4];
/* Per entity slot; only tanks use these.  left = shells still to fire
 * in the current volley; wait = beat after impact, or the 2 s rest;
 * shot = live ET_SHELL index, or TANK_SHOT_NONE. */
static unsigned char    tank_burst_left[MAX_ENT];
static unsigned char    tank_burst_wait[MAX_ENT];
static unsigned char    tank_burst_shot[MAX_ENT];
/* 1 while a tank shell's dirt burst is in ordinance_check: chopper
 * overlap is the 2x2, not Apple's ±12 world-px pad. */
static unsigned char    shell_chop_aabb;
static unsigned char    alien_odd;
static unsigned char    rnd_s, rnd_seed;

typedef struct {
    unsigned char type;
    signed char   vx;
    signed char   vy;
    signed char   dir;
    unsigned      x;
    unsigned char y;
    unsigned char ground;
} entity_t;

static entity_t         ents[MAX_ENT];

/* HUD rows 0-7: K/A/R counts.  F1 or ` swaps in the M5 debug line. */
static unsigned char    hud_on = 0;
static unsigned char    hud_prev_tog;
static unsigned char    dbg_prev_p;
/* What each page's HUD band already shows.  Repainting it is 640 bytes of
 * video plus per-pixel glyph RMW; the counts change a few times a sortie. */
static unsigned char    hud_pg_mode[2];         /* 0 = unknown/debug, 1 = counts */
static unsigned char    hud_pg_k[2], hud_pg_a[2], hud_pg_r[2];
static unsigned char    hud_w, hud_s, hud_a, hud_d, hud_dot, hud_slash;
static signed char      hud_tilt;
static int              hud_vx, hud_vy;
static unsigned char    hud_accely;
static unsigned char    latch_a, latch_s, latch_d;
static unsigned char    pend_a, pend_s, pend_d;

static const unsigned char fence_world_y[5] = { 17, 22, 25, 27, 28 };

/*  0 black, 1 blue, 2 green (tank treads, turret shadow), 3 cyan (jet
 *  shadow), 4 red (fire), 5 -> light cyan (jet body), 6 ground, 7 light
 *  grey (chopper shadow, mountains).  Chopper body is pixel 15 (white). */
static const unsigned char m4_palette[16] = {
     0, 1, 2, 3, 4, 11,
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

static void begin_death(void);

static void cleanup(void)
{
    snd_silence();
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

/* Ordered-dither night sky.  Black used to butt straight into the dark blue
 * band at row 64 and the seam read as a drawn line.  A 4x4 Bayer matrix fades
 * one into the other instead, weighted low so the top of the sky stays solid.
 *
 * The result is still scroll-invariant: every pixel is a function of screen
 * (x, y) only, so restore_rect can reproduce it under a sprite and the frame
 * budget in DESIGN.md section 7 is unchanged.
 *
 * y0/y1 are the first dithered row and the first solid row below it; move
 * them to shift or widen the fade.  Ordered, not random: a restored rect has
 * to land on the same pixels the paint did. */
#define SKY_C_BLACK     0
#define SKY_C_DARK      1               /* dark blue */
#define SKY_C_HAZE      9               /* light blue, horizon glow */

/* Bottom-up: 3 solid haze rows at the horizon, a fade up into dark blue,
 * 3 solid dark rows, then a fade up into black that everything above
 * inherits.  Only the two SOLID_ runs are flat; move any of these four to
 * re-balance how much sky is night. */
#define SKY_HAZE_SOLID  3U
#define SKY_DARK_SOLID  3U
#define SKY_HAZE_Y1     (MOUNTAIN_ROW - SKY_HAZE_SOLID)     /* 167 */
#define SKY_HAZE_Y0     (SKY_HAZE_Y1 - 24U)                 /* 143 */
#define SKY_DARK_Y1     (SKY_HAZE_Y0 - SKY_DARK_SOLID)      /* 140 */
#define SKY_DARK_Y0     (SKY_DARK_Y1 - 30U)                 /* 110 */

typedef struct {
    unsigned char        y0, y1;
    unsigned char        top, bot;
    const unsigned char *bayer;         /* 4x4 row-major, values 0-15 */
} sky_gradient;

static const unsigned char bayer_4x4[16] = {
     0,  8,  2, 10,
    12,  4, 14,  6,
     3, 11,  1,  9,
    15,  7, 13,  5
};

#define SKY_GRAD_N      2

/* Both bands together span 30 + 24 rows (section 4's row map).  32 is a
 * power of 2 so the table index below is a shift, not a multiply. */
#define SKY_GRAD_MAXROWS 32

static const sky_gradient sky_grads[SKY_GRAD_N] = {
    { (unsigned char)SKY_DARK_Y0, (unsigned char)SKY_DARK_Y1,
      SKY_C_BLACK, SKY_C_DARK, bayer_4x4 },
    { (unsigned char)SKY_HAZE_Y0, (unsigned char)SKY_HAZE_Y1,
      SKY_C_DARK,  SKY_C_HAZE, bayer_4x4 }
};

/* SKY_GRAD_N (2) on no match, not a pointer, so restore_rect's hot path never
 * carries a pointer that still needs turning back into a table index. */
static unsigned sky_grad_find_idx(unsigned y)
{
    unsigned i;

    for (i = 0; i < SKY_GRAD_N; i++) {
        if (y >= (unsigned)sky_grads[i].y0 && y < (unsigned)sky_grads[i].y1)
            return i;
    }
    return SKY_GRAD_N;
}

/* 0 = all top colour, 16 = all bottom colour (every Bayer cell is < 16).
 * Used only by sky_pat_init below, once per row, not per present. */
static unsigned sky_grad_mix(const sky_gradient *g, unsigned y)
{
    unsigned span;

    span = (unsigned)g->y1 - (unsigned)g->y0;
    if (span == 0U || y <= (unsigned)g->y0)
        return 0U;
    if (y >= (unsigned)g->y1)
        return 16U;
    return ((y - (unsigned)g->y0) * 17U) / span;
}

static unsigned char sky_grad_byte(const sky_gradient *g, unsigned xbyte,
                                   unsigned y)
{
    unsigned      mix, cell, xl;
    unsigned char l, r;

    mix  = sky_grad_mix(g, y);
    cell = (y & 3U) << 2;
    xl   = (xbyte << 1) & 3U;           /* 2 px per byte: 0,1 or 2,3 */
    l = ((unsigned)g->bayer[cell + xl] < mix) ? g->bot : g->top;
    r = ((unsigned)g->bayer[cell + ((xl + 1U) & 3U)] < mix) ? g->bot : g->top;
    return (unsigned char)((l << 4) | (r & 0x0FU));
}

/* sky_grad_byte(g, xbyte, y) depends on xbyte only through its parity (the
 * 4x4 Bayer cell is two bytes wide), so a row has exactly two possible fill
 * words for a given (gradient, y): one starting on an even byte column, one
 * on odd.  restore_rect can re-enter the same row many times a present (once
 * per dirty rect that touches it), and every entry used to redo a 16-bit
 * multiply and divide (sky_grad_mix) twice -- once per nibble-pair -- to
 * regenerate a value that depends on nothing but y.  Precompute both words
 * for every row of both bands once (self-triggering on first use) into
 * sky_pat_tab, and turn the per-row cost into a table lookup. */
static unsigned sky_pat_tab[SKY_GRAD_N][SKY_GRAD_MAXROWS][2];
static unsigned char sky_pat_ready = 0;

static unsigned sky_grad_pattern_calc(const sky_gradient *g, unsigned xbyte,
                                      unsigned y)
{
    return ((unsigned)sky_grad_byte(g, xbyte + 1U, y) << 8)
           | (unsigned)sky_grad_byte(g, xbyte, y);
}

static void sky_pat_init(void)
{
    unsigned gi, row, n, y;
    const sky_gradient *g;

    for (gi = 0; gi < SKY_GRAD_N; gi++) {
        g = &sky_grads[gi];
        n = (unsigned)g->y1 - (unsigned)g->y0;
        if (n > SKY_GRAD_MAXROWS)
            n = SKY_GRAD_MAXROWS;       /* cannot happen; guards the table */
        for (row = 0; row < n; row++) {
            y = (unsigned)g->y0 + row;
            sky_pat_tab[gi][row][0] = sky_grad_pattern_calc(g, 0U, y);
            sky_pat_tab[gi][row][1] = sky_grad_pattern_calc(g, 1U, y);
        }
    }
    sky_pat_ready = 1;
}

static unsigned sky_grad_pattern(unsigned gi, unsigned xbyte, unsigned y)
{
    if (!sky_pat_ready)
        sky_pat_init();
    return sky_pat_tab[gi][y - (unsigned)sky_grads[gi].y0][xbyte & 1U];
}

#define SKY_PAT_MAX     40
static unsigned sky_rowpat[SKY_PAT_MAX];

static void sky_grad_fill(unsigned seg, unsigned gi, unsigned xbyte,
                          unsigned y, unsigned wbytes, unsigned rows)
{
    unsigned i, n, off;

    off = 0;
    while (off < rows) {
        n = rows - off;
        if (n > SKY_PAT_MAX)
            n = SKY_PAT_MAX;
        for (i = 0; i < n; i++)
            sky_rowpat[i] = sky_grad_pattern(gi, xbyte, y + off + i);
        fill_rows_m8(seg, xbyte, y + off, wbytes, n, sky_rowpat);
        off += n;
    }
}

static void draw_sky_gradient(unsigned seg, unsigned gi)
{
    const sky_gradient *g = &sky_grads[gi];

    sky_grad_fill(seg, gi, 0U, (unsigned)g->y0, M8_BYTES_PER_ROW,
                  (unsigned)g->y1 - (unsigned)g->y0);
}

static void draw_stars_and_moon(unsigned seg, const dirty_rect *hit,
                                unsigned nhit);

static void paint_world(unsigned seg)
{
    fill_band_m8(seg, 0,                HUD_ROWS,       M8_SOLID(HUD_FIELD));
    fill_band_m8(seg, HUD_ROWS,         SKY_DARK_Y0 - HUD_ROWS,
                                        M8_SOLID(SKY_C_BLACK));
    draw_sky_gradient(seg, 0U);
    fill_band_m8(seg, SKY_DARK_Y1,      SKY_DARK_SOLID, M8_SOLID(SKY_C_DARK));
    draw_sky_gradient(seg, 1U);
    fill_band_m8(seg, SKY_HAZE_Y1,      SKY_HAZE_SOLID, M8_SOLID(SKY_C_HAZE));
    fill_band_m8(seg, MOUNTAIN_ROW,     4,              M8_SOLID(8));
    fill_band_m8(seg, GROUND_TOP_ROW,   1,
                                        M8_SOLID(M8_IDX_GROUND_HI));
    fill_band_m8(seg, GROUND_TOP_ROW+1, 199 - GROUND_TOP_ROW,
                                        M8_SOLID(M8_IDX_GROUND));
    draw_stars_and_moon(seg, 0, 0);
}

/* Debug HUD: 4x6 glyphs, bit 3 = leftmost pixel.  Grey band, ink black. */
#define HUD_INK         0
#define HUD_GLYPH_W     4
#define HUD_GLYPH_H     6
#define HUD_CELL_W      5               /* 1 px gap; 32 cells = 160 px */
#define HUD_TEXT_Y      1
#define HUD_BUBBLE      0               /* black 24x8 counter wells */
#define HUD_DIGIT       15              /* white 5x7, new art (section 6) */
#define HUD_BUBBLE_W    24U
#define HUD_DIGIT_W     5U
#define HUD_DIGIT_H     7U
#define BANNER_TICKS    48U             /* ~2.4 s at 20 Hz sim */
#define BANNER_Y        92              /* 199 - $6B; original 15-px top */
#define BANNER_H_ORIG   15              /* Apple height before 1.5x bump */
#define END_TICKS       32U             /* Apple FRAME_COUNT cmp #$20 */
#define END_Y           87              /* 199 - $70 */
#define TITLE_HOLD      90U             /* ~1.5 s at 60 Hz retrace */

/* 5x7 digits, bit 4 = leftmost pixel.  New art, not the Apple 6x7 font. */
static const unsigned char hud_digit5[10][HUD_DIGIT_H] = {
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },   /* 0 */
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },   /* 1 */
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },   /* 2 */
    { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E },   /* 3 */
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },   /* 4 */
    { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },   /* 5 */
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E },   /* 6 */
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },   /* 7 */
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E },   /* 8 */
    { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C }    /* 9 */
};

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
    static const unsigned char g_E[HUD_GLYPH_H]     = { 0xF, 0x8, 0xE, 0x8, 0x8, 0xF };
    static const unsigned char g_F[HUD_GLYPH_H]     = { 0xF, 0x8, 0xE, 0x8, 0x8, 0x8 };
    static const unsigned char g_I[HUD_GLYPH_H]     = { 0xE, 0x4, 0x4, 0x4, 0x4, 0xE };
    static const unsigned char g_K[HUD_GLYPH_H]     = { 0x9, 0xA, 0xC, 0xC, 0xA, 0x9 };
    static const unsigned char g_O[HUD_GLYPH_H]     = { 0x6, 0x9, 0x9, 0x9, 0x9, 0x6 };
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
    if (ch == 'E')
        return g_E;
    if (ch == 'F')
        return g_F;
    if (ch == 'I')
        return g_I;
    if (ch == 'K')
        return g_K;
    if (ch == 'O')
        return g_O;
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

static void hud_draw_digit5(unsigned seg, unsigned x, unsigned d)
{
    const unsigned char *g;
    unsigned r, c;

    if (d > 9U)
        d = 9U;
    g = hud_digit5[d];
    for (r = 0; r < HUD_DIGIT_H; r++) {
        for (c = 0; c < HUD_DIGIT_W; c++) {
            if ((g[r] & (unsigned char)(0x10U >> c)) == 0)
                continue;
            hud_plot_px(seg, x + c, HUD_TEXT_Y + r, HUD_DIGIT);
        }
    }
}

/* 24x8 well, inset corners.  DESIGN.md section 6: not a scaled 43x9.
 *
 * x0 is always even (22/68/114, the three counter slots), so six of the
 * eight rows land on byte boundaries: row 0/7 (2px inset each side) and
 * rows 2-5 (full width) go through fill_rect_m8 -- one far call per
 * row-band instead of one hud_plot_px (peek_byte+poke_byte far-call pair)
 * per pixel, cutting this bubble from ~360 far calls to ~90. Only rows 1
 * and 6 (odd 1px inset, not byte-aligned) still plot pixel by pixel. */
static void hud_draw_bubble(unsigned seg, unsigned x0)
{
    unsigned x, y, xa, xb, xbyte0;

    xbyte0 = x0 / 2U;
    fill_rect_m8(seg, xbyte0 + 1U, 0U, (HUD_BUBBLE_W - 4U) / 2U, 1U,
                 M8_SOLID(HUD_BUBBLE));
    fill_rect_m8(seg, xbyte0, 2U, HUD_BUBBLE_W / 2U, 4U,
                 M8_SOLID(HUD_BUBBLE));
    fill_rect_m8(seg, xbyte0 + 1U, 7U, (HUD_BUBBLE_W - 4U) / 2U, 1U,
                 M8_SOLID(HUD_BUBBLE));

    for (y = 1U; y <= 6U; y += 5U) {
        xa = x0 + 1U;
        xb = x0 + HUD_BUBBLE_W - 1U;
        for (x = xa; x < xb; x++)
            hud_plot_px(seg, x, y, HUD_BUBBLE);
    }
}

static void hud_draw_counter(unsigned seg, unsigned x0, unsigned char v)
{
    unsigned tens, ones;

    hud_draw_bubble(seg, x0);
    tens = (unsigned)v / 10U;
    ones = (unsigned)v % 10U;
    hud_draw_digit5(seg, x0 + 6U, tens);
    hud_draw_digit5(seg, x0 + 12U, ones);
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

static void draw_hud(unsigned seg, int pg)
{
    char buf[36];
    char ts[8], xs[8], ys[8];
    unsigned i;

    /* The debug line changes every tick, so it always repaints. */
    if (!hud_on && hud_pg_mode[pg] == 1U
        && hud_pg_k[pg] == hostages_killed
        && hud_pg_a[pg] == hostages_loaded
        && hud_pg_r[pg] == total_rescues)
        return;

    if (hud_on) {
        fill_band_m8(seg, 0, HUD_ROWS, M8_SOLID(7));
        hud_pg_mode[pg] = 0;
        hud_put_signed(ts, (int)hud_tilt);
        hud_put_signed(xs, hud_vx);
        hud_put_signed(ys, hud_vy);
        sprintf(buf, "W%u S%u A%u D%u T%s X%s Y%s A%u.%u/%u",
                (unsigned)hud_w, (unsigned)hud_s, (unsigned)hud_a,
                (unsigned)hud_d, ts, xs, ys, (unsigned)hud_accely,
                (unsigned)hud_dot, (unsigned)hud_slash);
        for (i = 0; buf[i] != '\0' && i < 32U; i++)
            hud_draw_char(seg, i, buf[i]);
        return;
    }
    fill_band_m8(seg, 0, HUD_ROWS, M8_SOLID(HUD_FIELD));
    hud_draw_counter(seg, 22U, hostages_killed);
    hud_draw_counter(seg, 68U, hostages_loaded);
    hud_draw_counter(seg, 114U, total_rescues);
    hud_pg_mode[pg] = 1;
    hud_pg_k[pg] = hostages_killed;
    hud_pg_a[pg] = hostages_loaded;
    hud_pg_r[pg] = total_rescues;
}

/* Scroll-invariant band colour for one row.  Matches paint_world.  Rows
 * inside the sky gradient are not solid; restore_rect handles those before
 * it asks. */
static unsigned band_solid(unsigned y)
{
    if (y < HUD_ROWS)
        return M8_SOLID(HUD_FIELD);
    if (y < SKY_DARK_Y0)
        return M8_SOLID(SKY_C_BLACK);
    if (y < SKY_HAZE_Y0)
        return M8_SOLID(SKY_C_DARK);
    if (y < MOUNTAIN_ROW)
        return M8_SOLID(SKY_C_HAZE);
    if (y < GROUND_TOP_ROW)
        return M8_SOLID(8);
    if (y == GROUND_TOP_ROW)
        return M8_SOLID(M8_IDX_GROUND_HI);
    return M8_SOLID(M8_IDX_GROUND);
}

static void plot_px(unsigned seg, unsigned x, unsigned y, unsigned char c);

/* One min/max pass over a dirty list, byte columns for X.  Callers that
 * probe the same (hit, nhit) pair many times in a row (draw_stars_and_moon
 * over 36 stars, draw_scenery over every house/fence/base part) compute this
 * once and reuse it, so dirty_hits_px/dirty_hits_box can reject most probes
 * in O(1) instead of re-scanning up to DIRTY_MAX rects every time.  Valid
 * only when nhit != 0; callers must not build or consult one otherwise. */
typedef struct {
    unsigned xb0, xb1;      /* byte-column span [xb0, xb1) */
    unsigned y0, y1;        /* row span [y0, y1) */
} dirty_bbox;

static void dirty_bbox_calc(dirty_bbox *bb, const dirty_rect *hit,
                            unsigned nhit)
{
    unsigned i, xe, ye;

    bb->xb0 = hit[0].xbyte;
    bb->xb1 = (unsigned)(hit[0].xbyte + hit[0].wbytes);
    bb->y0  = hit[0].y;
    bb->y1  = (unsigned)(hit[0].y + hit[0].rows);
    for (i = 1; i < nhit; i++) {
        xe = (unsigned)(hit[i].xbyte + hit[i].wbytes);
        ye = (unsigned)(hit[i].y + hit[i].rows);
        if (hit[i].xbyte < bb->xb0)
            bb->xb0 = hit[i].xbyte;
        if (xe > bb->xb1)
            bb->xb1 = xe;
        if (hit[i].y < bb->y0)
            bb->y0 = hit[i].y;
        if (ye > bb->y1)
            bb->y1 = ye;
    }
}

/* Called ~36+ times per present (once per star, more via dirty_hits_box)
 * with a small nhit each -- CLAUDE-THOUGHTS.md finding #2 found the O(n)
 * scan itself is already cheap at this list size, so the fixed __cdecl
 * push/pop-per-call overhead is the actual cost worth cutting here
 * (finding #5). hit/nhit are the two fields every call site has in hand
 * first; bb/x/y stay on the stack, same as before. modify is the full
 * register set since the body is ordinary C, not hand-written asm -- this
 * only removes argument-passing overhead, it does not try to keep
 * anything alive across the call. */
static int dirty_hits_px(const dirty_rect *hit, unsigned nhit,
                         const dirty_bbox *bb, unsigned x, unsigned y);
#pragma aux dirty_hits_px parm [ax] [dx] value [ax] modify [ax bx cx dx];

static int dirty_hits_px(const dirty_rect *hit, unsigned nhit,
                         const dirty_bbox *bb, unsigned x, unsigned y)
{
    unsigned i, xb;

    if (hit == 0)
        return 1;
    if (nhit == 0U)
        return 0;
    xb = x / 2U;
    if (bb != 0 && (xb < bb->xb0 || xb >= bb->xb1
                    || y < bb->y0 || y >= bb->y1))
        return 0;
    for (i = 0; i < nhit; i++) {
        if (y < hit[i].y || y >= hit[i].y + hit[i].rows)
            continue;
        if (xb < hit[i].xbyte || xb >= hit[i].xbyte + hit[i].wbytes)
            continue;
        return 1;
    }
    return 0;
}

/* Same reasoning as dirty_hits_px above: called ~20 times per present
 * (once per house/fence-tower/base sub-blit via box_hits_dirty), small
 * nhit, fixed call overhead dominates. */
static int dirty_hits_box(const dirty_rect *hit, unsigned nhit,
                          const dirty_bbox *bb,
                          unsigned x0, unsigned y0, unsigned x1, unsigned y1);
#pragma aux dirty_hits_box parm [ax] [dx] value [ax] modify [ax bx cx dx];

static int dirty_hits_box(const dirty_rect *hit, unsigned nhit,
                          const dirty_bbox *bb,
                          unsigned x0, unsigned y0, unsigned x1, unsigned y1)
{
    unsigned i;

    if (hit == 0)
        return 1;
    if (nhit == 0U)
        return 0;
    if (bb != 0 && (x1 < bb->xb0 * 2U || x0 >= bb->xb1 * 2U
                    || y1 < bb->y0 || y0 >= bb->y1))
        return 0;
    for (i = 0; i < nhit; i++) {
        unsigned ry1 = hit[i].y + hit[i].rows;
        unsigned rx0 = hit[i].xbyte * 2U;
        unsigned rx1 = rx0 + hit[i].wbytes * 2U;

        if (y1 < hit[i].y || y0 >= ry1)
            continue;
        if (x1 < rx0 || x0 >= rx1)
            continue;
        return 1;
    }
    return 0;
}

static int box_hits_dirty(int x, int y, int w, int h,
                          const dirty_rect *hit, unsigned nhit,
                          const dirty_bbox *bb)
{
    int x1, y1;

    if (hit == 0 || nhit == 0U || w <= 0 || h <= 0)
        return 0;
    x1 = x + w - 1;
    y1 = y + h - 1;
    if (x1 < 0 || y1 < (int)HUD_ROWS)
        return 0;
    if (x < 0)
        x = 0;
    if (y < (int)HUD_ROWS)
        y = (int)HUD_ROWS;
    return dirty_hits_box(hit, nhit, bb, (unsigned)x, (unsigned)y,
                          (unsigned)x1, (unsigned)y1);
}

static int spr_hits_dirty(int x, int y, unsigned char *spr,
                          const dirty_rect *hit, unsigned nhit,
                          const dirty_bbox *bb)
{
    if (spr == 0)
        return 0;
    return box_hits_dirty(x, y, (int)spr[0], (int)spr[1], hit, nhit, bb);
}

static void scenery_invalidate(void)
{
    scenery_full[0] = 1;
    scenery_full[1] = 1;
}

/* Apple renderStars / renderMoon: compiled HGR dots in the night sky, plus
 * a fixed moon.  Stars twinkle by rewriting bit patterns; here a few 1-px
 * dots in the upper sky dim or skip.  The second dozen sit in the solid
 * black the chopper can actually reach (ceiling ~row 70–87, black to 110).
 * Not added to the dirty list — restore_rect wipes them, then we redraw.
 * Callers must run this after the sky gradient, or the dither paints over
 * the stars. */
#define N_STARS         36U
static const unsigned char star_x[N_STARS] = {
    8, 22, 35, 48, 61, 74, 88, 97, 110, 14,
    29, 41, 55, 69, 82, 101, 115, 145, 151, 6,
    138, 90, 52, 160 - 18,
    18, 40, 63, 85, 102, 124, 147, 9,
    33, 77, 118, 153
};
static const unsigned char star_y[N_STARS] = {
    12, 18, 11, 28, 15, 22, 14, 31, 19, 38,
    44, 36, 51, 42, 48, 39, 52, 33, 24, 55,
    46, 58, 21, 16,
    64, 71, 67, 78, 73, 81, 69, 86,
    92, 96, 83, 91
};

static void draw_moon(unsigned seg)
{
    int dx, dy, r2;
    unsigned char c;

    for (dy = -4; dy <= 4; dy++) {
        for (dx = -4; dx <= 4; dx++) {
            r2 = dx * dx + dy * dy;
            if (r2 > 18)
                continue;
            if (dx <= -2)
                c = 7;
            else if (r2 > 12 && dx < 0)
                c = 7;
            else
                c = 15;
            plot_px(seg, (unsigned)(126 + dx), (unsigned)(20 + dy), c);
        }
    }
}

static const unsigned char star_cycle[4] = { 15, 8, 0, 14 };

static void draw_stars_and_moon(unsigned seg, const dirty_rect *hit,
                                unsigned nhit)
{
    unsigned i, ph;
    unsigned char c;
    dirty_bbox bb;
    const dirty_bbox *bbp;

    if (hit != 0 && nhit == 0U)
        return;

    bbp = 0;
    if (hit != 0) {
        dirty_bbox_calc(&bb, hit, nhit);
        bbp = &bb;
    }

    if (dirty_hits_box(hit, nhit, bbp, 122U, 16U, 131U, 25U))
        draw_moon(seg);

    for (i = 0; i < N_STARS; i++) {
        if (!dirty_hits_px(hit, nhit, bbp, star_x[i], star_y[i]))
            continue;
        if (i & 1U) {
            /* White, dark grey, black, yellow.  4 sim ticks per colour. */
            ph = ((sim_frame / 4U) + i) & 3U;
            c = star_cycle[ph];
            if (c == 0U)
                continue;
        } else {
            ph = (sim_frame + i * 5U) & 7U;
            if (ph == 0U)
                continue;
            c = (ph < 3U) ? 7 : 15;
        }
        plot_px(seg, (unsigned)star_x[i], (unsigned)star_y[i], c);
    }
}

static unsigned rle_run_bytes(unsigned char *s, unsigned rows)
{
    const unsigned char *p;
    unsigned              r, n, skip, run;

    if (s == 0 || rows == 0U)
        return 0U;
    p = s + 2;
    n = 0U;
    for (r = 0; r < rows; r++) {
        for (;;) {
            skip = *p++;
            run  = *p++;
            if (skip == 0U && run == 0U)
                break;
            p += run;
            n += run;
        }
    }
    return n;
}

/* A round-robin byte-count cache here (attempted this session) reliably
 * crashed on real DOSBox-X: DGROUP was already at 65408/65536 bytes before
 * it (see CLAUDE-THOUGHTS.md's DGROUP-margin entry), and adding ~98 more
 * bytes of static cache state was enough to tip something over, even with
 * the cache function provably unused (dead code, confirmed by the compiler
 * warning) -- reverting the *call* was not enough to un-crash it; only
 * removing the arrays and the function did. Do not re-add without first
 * bringing DGROUP margin back to at least several hundred bytes. */

static void ws_add_blit(unsigned n)
{
    if (n == 0U)
        return;
    if (ws_kind == WS_K_MTN)
        ws_t_mtn_blit += n;
    else if (ws_kind == WS_K_SCENERY)
        ws_t_scenery += n;
    else
        ws_t_sprite += n;
}

static void ws_reset(void)
{
    ws_kind = WS_K_SPRITE;
    ws_n = ws_n_scroll = 0;
    ws_sum_restore = ws_sum_blit = ws_sum_mtn = 0UL;
    ws_sum_scenery = ws_sum_sprite = ws_sum_video = 0UL;
    ws_min_restore = ws_min_blit = ws_min_video = 0xFFFFFFFFUL;
    ws_max_restore = ws_max_blit = ws_max_video = 0UL;
    ws_z_did = ws_z_ovf = ws_z_frame = ws_z_ticks = 0;
    ws_z_restore = ws_z_blit = ws_z_video = 0UL;
}

static void ws_begin_tick(void)
{
    ws_t_restore = ws_t_mtn_fill = ws_t_mtn_blit = 0UL;
    ws_t_scenery = ws_t_sprite = 0UL;
    ws_kind = WS_K_SPRITE;
}

static void ws_end_tick(int scrolled)
{
    unsigned long blit, video;

    blit = ws_t_mtn_blit + ws_t_scenery + ws_t_sprite;
    video = ws_t_restore + ws_t_mtn_fill + blit;
    if (ws_t_restore < ws_min_restore)
        ws_min_restore = ws_t_restore;
    if (ws_t_restore > ws_max_restore)
        ws_max_restore = ws_t_restore;
    if (blit < ws_min_blit)
        ws_min_blit = blit;
    if (blit > ws_max_blit)
        ws_max_blit = blit;
    if (video < ws_min_video)
        ws_min_video = video;
    if (video > ws_max_video)
        ws_max_video = video;
    ws_sum_restore += ws_t_restore;
    ws_sum_blit += blit;
    ws_sum_mtn += ws_t_mtn_fill + ws_t_mtn_blit;
    ws_sum_scenery += ws_t_scenery;
    ws_sum_sprite += ws_t_sprite;
    ws_sum_video += video;
    ws_n++;
    if (scrolled)
        ws_n_scroll++;
}

static unsigned long ws_avg(unsigned long sum, unsigned n)
{
    if (n == 0U)
        return 0UL;
    return sum / n;
}

static unsigned long us_from_nsbyte(unsigned long bytes, unsigned long ns_per)
{
    /* bytes * ns / 1000.  Typical working set is a few thousand bytes. */
    return (bytes * ns_per + 500UL) / 1000UL;
}

static unsigned long ticks_to_us(unsigned ticks)
{
    return ((unsigned long)ticks * 8381UL + 5000UL) / 10000UL;
}

/* BIOS tick (0040:006C) at 18.2065 Hz -> ms, integer only.  Used only for
 * the ph_* phase sums, which are already a sum over many sim ticks -- a
 * single tick's true duration is far finer than this counter's resolution,
 * the same way run_bios_ticks already is. */
static unsigned long bios_ticks_to_ms(unsigned long ticks)
{
    return (ticks * 54925UL + 500UL) / 1000UL;
}

static void print_workset(void)
{
    unsigned long ar, ab, av, am, asce, asp;
    unsigned long us_r, us_b;

    printf("\nWORKSET byte counts (every sim present this run).  Not times.\n");
    if (ws_n == 0U) {
        printf("  no sim presents.\n");
        return;
    }
    ar = ws_avg(ws_sum_restore, ws_n);
    ab = ws_avg(ws_sum_blit, ws_n);
    av = ws_avg(ws_sum_video, ws_n);
    am = ws_avg(ws_sum_mtn, ws_n);
    asce = ws_avg(ws_sum_scenery, ws_n);
    asp = ws_avg(ws_sum_sprite, ws_n);
    printf("  sim ticks %u  scrolling %u  static %u\n",
           ws_n, ws_n_scroll, ws_n - ws_n_scroll);
    printf("                    min     avg     max\n");
    printf("  restore        %6lu  %6lu  %6lu\n",
           ws_min_restore, ar, ws_max_restore);
    printf("  rle blit       %6lu  %6lu  %6lu\n",
           ws_min_blit, ab, ws_max_blit);
    printf("  video total    %6lu  %6lu  %6lu\n",
           ws_min_video, av, ws_max_video);
    printf("  avg mountain %lu  scenery %lu  sprites %lu\n",
           am, asce, asp);
    printf("  restore is even-padded dirty-rect fill_rect bytes.\n");
    printf("  rle blit is skip-stripped payload (opaque + mixed bytes).\n");
    printf("  mountain includes the 320-byte band fill when the ridge\n");
    printf("  is repainted.  DESIGN.md section 7 ~3000/~320/~1500/~3000\n");
    printf("  is analysis; this table is what this run actually wrote.\n");

    us_r = us_from_nsbyte(ar, M1_NS_FILL_RECT);
    us_b = us_from_nsbyte(ab, M1_NS_MOVSB);
    printf("\nANALYSIS -- M1.LOG rates x avg bytes, not a new hardware total.\n");
    printf("  restore  %lu B * 2935 ns (fill_rect) = %lu us\n", ar, us_r);
    printf("  rle blit %lu B * 4515 ns (movsb)     = %lu us\n", ab, us_b);
    printf("  Do not add these into a replacement for section 7's ~26 ms.\n");

    if (opt_ztimer == 0U) {
        printf("\n  /ztimer not used.  On hardware: M10 /ztimer /batch /frames=60\n");
        printf("  with no keys during the sample (CLI; PCjr kbd is NMI).\n");
        return;
    }
    if (ws_z_did == 0U) {
        printf("\nZTIMER: no sample (sim_frame never hit %u).\n", opt_ztimer);
        return;
    }
    printf("\nZTIMER one present (restore+draw, HUD not included).\n");
    printf("  sim_frame %u  bytes restore %lu  blit %lu  video %lu\n",
           ws_z_frame, ws_z_restore, ws_z_blit, ws_z_video);
    if (ws_z_ovf)
        printf("  OVERFLOW -- interval exceeded ~54 ms; count is meaningless.\n");
    else
        printf("  %u ticks = %lu us.  Emulator clocks are a smoke test.\n",
               ws_z_ticks, ticks_to_us(ws_z_ticks));
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

    if (y1 > y)
        ws_t_restore += (unsigned long)rw * (unsigned long)(y1 - y);

    while (y < y1) {
        unsigned gi = sky_grad_find_idx(y);

        if (gi < SKY_GRAD_N) {
            run_y = (unsigned)sky_grads[gi].y1;
            if (run_y > y1)
                run_y = y1;
            sky_grad_fill(seg, gi, xb, y, rw, (unsigned)(run_y - y));
            y = run_y;
            continue;
        }
        solid = band_solid(y);
        run_y = y + 1U;
        while (run_y < y1 && sky_grad_find_idx(run_y) == SKY_GRAD_N
               && band_solid(run_y) == solid)
            run_y++;
        fill_rect_m8(seg, xb, y, rw, (unsigned)(run_y - y), solid);
        y = run_y;
    }
}

/* Returns non-zero if any restored rect overlapped the mountain band, which
 * is the only reason to repaint mountains when the camera has not moved. */
static int restore_list(unsigned seg, dirty_list *list)
{
    unsigned i;
    int      band_hit = 0;

    for (i = 0; i < list->n; i++) {
        const dirty_rect *d = &list->r[i];

        if (d->y < (unsigned)(MOUNTAIN_ROW + 4)
            && (unsigned)(d->y + d->rows) > (unsigned)MOUNTAIN_ROW)
            band_hit = 1;
        restore_rect(seg, d);
    }
    list->n = 0;
    return band_hit;
}

/* A null list means "this blit is not restored per-rect": the mountain band
 * is cleared wholesale instead. */
static void dirty_add(dirty_list *list, unsigned xbyte, unsigned y,
                      unsigned wbytes, unsigned rows)
{
    dirty_rect *d;
    unsigned    rw, xb;

    if (list == 0 || list->n >= DIRTY_MAX || wbytes == 0U || rows == 0U)
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

static void plot_px(unsigned seg, unsigned x, unsigned y, unsigned char c)
{
    unsigned off;
    unsigned char b, nib;

    if (x >= M8_WIDTH_PX || y >= M8_HEIGHT_PX)
        return;
    off = M8_ROW_OFF(y) + x / 2U;
    b = peek_byte(seg, off);
    nib = (unsigned char)(c & 0x0F);
    if (x & 1U)
        poke_byte(seg, off, (unsigned)((b & 0xF0) | nib));
    else
        poke_byte(seg, off, (unsigned)((b & 0x0F) | (nib << 4)));
}

static void blit_at(unsigned seg, int x_px, int y,
                    unsigned char *even, unsigned char *odd,
                    dirty_list *list);

static void blit_centered(unsigned seg, unsigned char *spr, int y,
                          dirty_list *list)
{
    int wpx, x;

    if (spr == 0)
        return;
    wpx = (int)spr[0];
    x = ((int)M8_WIDTH_PX - wpx) / 2;
    if (x & 1)
        x--;
    if (x < 0)
        x = 0;
    blit_at(seg, x, y, spr, spr, list);
}

static void draw_sortie_banner(unsigned seg, dirty_list *list)
{
    unsigned char *spr;
    int            h, y;

    if (banner_left == 0U || sortie >= MAX_SORTIE)
        return;
    spr = sortie_banner_e[sortie];
    h = (int)spr[1];
    y = BANNER_Y + BANNER_H_ORIG / 2 - h / 2;
    if (y < (int)HUD_ROWS)
        y = (int)HUD_ROWS;
    blit_centered(seg, spr, y, list);
}

static void draw_end_banner(unsigned seg, dirty_list *list)
{
    if (end_kind == 0U)
        return;
    if (end_kind == 1U)
        blit_centered(seg, title_crown, END_Y, list);
    else
        blit_centered(seg, title_the_end, END_Y, list);
}

static int xbyte_from_px(int x)
{
    if (x >= 0)
        return x / 2;
    return (x - 1) / 2;
}

static unsigned char fire_nibble(unsigned char n, unsigned mix)
{
    static const unsigned char c[3] = { 4, 12, 14 };

    if (n != 0x0E)
        return n;
    return c[mix % 3U];
}

static unsigned char fire_byte(unsigned char src)
{
    unsigned mix;

    mix = (unsigned)src + (unsigned)blit_fire;
    return (unsigned char)((fire_nibble((unsigned char)(src >> 4), mix) << 4) |
                           fire_nibble((unsigned char)(src & 0x0F), mix + 1U));
}

/* Called once per opaque/mixed byte on the clipped blit fallback's row walk
 * (blit_rle_row_clip) -- all three arguments fit in registers, so this
 * drops the __cdecl push/pop entirely rather than just partially. src is
 * still a full 16-bit register slot (cx), not a byte sub-register: Watcom
 * 16-bit #pragma aux has known bugs with literal 8-bit register names
 * (al/bl/cl) in parm clauses, so this avoids that path rather than risk
 * silently-wrong codegen with no source-level debugger to catch it. */
static void plot_m8_byte(unsigned seg, unsigned off, unsigned char src);
#pragma aux plot_m8_byte parm [ax] [dx] [cx] modify [ax bx cx dx];

static void plot_m8_byte(unsigned seg, unsigned off, unsigned char src)
{
    unsigned char d;

    if (blit_fire)
        src = fire_byte(src);
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
    if (!opt_forceclip && xbyte0 >= 0 &&
        (unsigned)(xbyte0 + wpx / 2) <= M8_BYTES_PER_ROW) {
        /* The fast blitters return the run bytes (opaque + mixed) they
         * just copied, counted as a side effect of the blit itself -- no
         * second C-side pass over the same RLE stream needed here. */
        unsigned n;

        if (blit_fire || opt_forcefire)
            n = blit_rle_m8_fire(seg, (unsigned)xbyte0, (unsigned)y,
                                 data_seg(), (unsigned)s);
        else
            n = blit_rle_m8(seg, (unsigned)xbyte0, (unsigned)y, data_seg(),
                            (unsigned)s);
        if (!opt_noworkset)
            ws_add_blit(n);
    } else {
        /* Rare edge-of-screen path; still needs its own count. */
        if (!opt_noworkset)
            ws_add_blit(rle_run_bytes(s, (unsigned)h));
        blit_rle_clip(seg, x_px, y, s, (unsigned)h);
    }

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
 * separately, against the ends the stick has actually been seen to reach.
 *
 * The high side used to scale against STICK_LIVE_MAX, which cannot be right:
 * axis_live() treats a count at STICK_LIVE_MAX as an *unconnected* axis, so
 * the raw value mapped to full deflection was the one that means "no stick".
 * A real pot counts far below that, so pushing right or down moved Apple by
 * a unit or two and the ±16 deadzone ate it, while left and up — scaled
 * against rest, a real number — worked.
 *
 * lo/hi are the throw recorded by the calibrate screen.  Until the stick has
 * been that way, mirror rest: assume full scale sits as far above rest as 0
 * is below it.  Erring small is the safe direction — it saturates early,
 * where erring large is what killed the axis. */
static unsigned apple_span_from_raw(unsigned raw, unsigned center,
                                    unsigned lo, unsigned hi)
{
    long     d;
    int      a;
    unsigned span;

    if (center == 0U)
        return APPLE_CENTER;
    if (raw >= center) {
        if (hi <= center)
            hi = (center < 0x4000U) ? (unsigned)(center * 2U) : STICK_LIVE_MAX;
        span = (hi > center) ? (unsigned)(hi - center) : 1U;
        d = ((long)raw - (long)center) * 127L;
        d /= (long)span;
    } else {
        if (lo >= center)
            lo = 0U;
        span = (center > lo) ? (unsigned)(center - lo) : 1U;
        d = ((long)raw - (long)center) * 128L;
        d /= (long)span;
    }
    a = (int)APPLE_CENTER + (int)d;
    if (a < 0)
        a = 0;
    if (a > 255)
        a = 255;
    return (unsigned)a;
}

static unsigned apple_from_raw(unsigned raw, unsigned center,
                               unsigned lo, unsigned hi)
{
    return apple_deadzone(apple_span_from_raw(raw, center, lo, hi));
}

/* Map raw onto 0..inner-1 with rest at the middle cell.  No deadzone — the
 * star shows the pot, not the flight bucket.  Deliberately the mirror-of-rest
 * scale rather than the recorded throw, so the box stays a fixed ruler: a
 * stick that only reaches 60% of the way right shows that, instead of the
 * scale stretching to meet it and pinning the star to the edge. */
static unsigned box_from_raw(unsigned raw, unsigned center, unsigned inner)
{
    unsigned a;

    if (inner <= 1U)
        return 0;
    a = apple_span_from_raw(raw, center, 0U, 0U);
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

static const unsigned char tank_spawn_rate[4] = { 1, 2, 2, 2 };
static const unsigned char jet_spawn_rate[4]  = { 0, 1, 2, 2 };
static const unsigned char alien_spawn_rate[4] = { 0, 0, 1, 2 };
static const unsigned char jet_difficulty[4] = { 0, 0, 0x2C, 0x44 };
static const unsigned char cannon_angle_x[5] = { 0x0B, 0x07, 0x01, 0xFF, 0xFB };
static const unsigned char tank_aim_table[6] = { 0x00, 0x40, 0x70, 0x90, 0xC0, 0xFF };
static const unsigned char shell_launch_x[5] = { 0x00, 0x06, 0x10, 0x19, 0x1F };
static const signed char   shell_launch_vx[5] = { -12, -7, 0, 7, 12 };
static const unsigned char shell_launch_dir[5] = { 2, 3, 4, 3, 2 };
/* 2x2 tank shell.  Even: GG / GO.  Odd: one-nibble shift for odd X. */
static unsigned char tank_shell_e[] = {
    2, 2,
    0x00, 0x01, 0x77, 0x00, 0x00,
    0x00, 0x01, 0x76, 0x00, 0x00
};
static unsigned char tank_shell_o[] = {
    4, 2,
    0x00, 0x01, 0x07, 0x00, 0x01, 0x70, 0x00, 0x00,
    0x00, 0x01, 0x07, 0x00, 0x01, 0x60, 0x00, 0x00
};
/* Dark green treads (index 2) with white studs on the rim.  Same 18x6
 * silhouette as the Apple frames; the dots crawl when the two frames swap. */
static unsigned char tank_tread_m9_00[] = {
    18, 6,
    0x01, 0x07, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0x00, 0x00, 0x00,
    0x09, 0x2F, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x2F, 0x00, 0x00,
    0x00, 0x09, 0xF2, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x00,
    0x00, 0x00, 0x09, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x2F,
    0x00, 0x00, 0x00, 0x09, 0xF2, 0x22, 0x22, 0x22, 0x22, 0xF2, 0x22, 0x22,
    0x22, 0x00, 0x00, 0x00, 0x01, 0x0F, 0x00, 0x01, 0x20, 0x00, 0x03, 0x2F,
    0x2F, 0x2F, 0x00, 0x01, 0x0F, 0x00, 0x02, 0x2F, 0x2F, 0x00, 0x01, 0x20,
    0x00, 0x00
};
static unsigned char tank_tread_m9_01[] = {
    18, 6,
    0x00, 0x01, 0x0F, 0x00, 0x01, 0x20, 0x00, 0x03, 0x2F, 0x2F, 0x2F, 0x00,
    0x01, 0x0F, 0x00, 0x02, 0x2F, 0x2F, 0x00, 0x01, 0x20, 0x00, 0x00, 0x00,
    0x09, 0xF2, 0x22, 0x22, 0x22, 0x22, 0xF2, 0x22, 0x22, 0x22, 0x00, 0x00,
    0x00, 0x09, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x2F, 0x00,
    0x00, 0x00, 0x09, 0xF2, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
    0x00, 0x00, 0x00, 0x09, 0x2F, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
    0x2F, 0x00, 0x00, 0x01, 0x07, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2,
    0x00, 0x00
};
static unsigned char *tank_tread_m9[2] = {
    tank_tread_m9_00, tank_tread_m9_01
};
static const unsigned char jet_climb_table[5] = { 0, 0x12, 0x0F, 0x18, 0x18 };
/* choplifter.s $6c00 jetYVelocityTables: 4-byte {sprite_off, dx, dy, dground}.
 * VY is the record index, not a Y speed.  jetXVelocityTable: VX 0->$6c00,
 * 1->$6c04 (18), 2->$6c4c (15), 3/4->$6c88 (24). */
static const unsigned char jet_yv0[] = {
    0x00,0x08,0x00,0x00
};
static const unsigned char jet_yv1[] = {
    0x00,0x06,0x01,0xFF, 0x00,0x06,0x00,0xFF, 0x00,0x06,0x00,0xFF,
    0x00,0x07,0x00,0xFE, 0x00,0x07,0x00,0xFE, 0x01,0x08,0xFF,0xFE,
    0x01,0x09,0xFF,0xFE, 0x01,0x09,0xFF,0xFE, 0x01,0x09,0xFF,0xFE,
    0x02,0x08,0xFF,0xFE, 0x02,0x08,0xFF,0xFE, 0x02,0x05,0xFF,0xFE,
    0x03,0x02,0xFF,0xFE, 0x04,0xFC,0xFF,0xFE, 0x04,0xF8,0xFF,0xFE,
    0x05,0xF6,0xFF,0xFE, 0x06,0xF4,0xFF,0xFE, 0x06,0xF4,0x00,0xFE
};
static const unsigned char jet_yv2[] = {
    0x00,0x04,0x01,0xFF, 0x00,0x04,0x00,0xFF, 0x00,0x03,0x00,0xFF,
    0x00,0x02,0x00,0xFE, 0x00,0x01,0xFF,0xFE, 0x00,0x01,0xFF,0xFE,
    0x00,0x01,0xFF,0xFD, 0x01,0x01,0xFF,0xFD, 0x01,0x01,0xFF,0xFD,
    0x01,0x01,0xFF,0xFD, 0x01,0x02,0xFF,0xFD, 0x01,0x03,0xFF,0xFD,
    0x02,0x05,0xFF,0xFD, 0x03,0x08,0xFF,0xFF, 0x04,0x0B,0xFF,0x00
};
static const unsigned char jet_yv3[] = {
    0x00,0x0C,0x00,0x00, 0x00,0x0C,0x00,0x00, 0x00,0x0C,0x00,0x00,
    0x00,0x0C,0x00,0x00, 0x00,0x0C,0x00,0x00, 0x01,0x0C,0x00,0x01,
    0x01,0x0C,0x00,0x01, 0x01,0x0C,0x01,0x01, 0x01,0x0C,0x01,0x01,
    0x02,0x0C,0x01,0x01, 0x02,0x0C,0x01,0x01, 0x02,0x0C,0x01,0x01,
    0x02,0x0C,0x01,0x01, 0x03,0x0C,0x01,0x01, 0x03,0x0C,0x01,0x01,
    0x03,0x0B,0x01,0x01, 0x03,0x0B,0x01,0x01, 0x04,0x0B,0x01,0x01,
    0x04,0x0B,0x01,0x01, 0x04,0x0B,0x01,0x01, 0x04,0x0B,0x01,0x01,
    0x04,0x0B,0x01,0x01, 0x04,0x0B,0x01,0x01, 0x05,0x0A,0x01,0x01
};
static const unsigned char * const jet_yv_tab[5] = {
    jet_yv0, jet_yv1, jet_yv2, jet_yv3, jet_yv3
};
static const unsigned char jet_yv_n[5] = { 1, 18, 15, 24, 24 };
static const unsigned char bullet_grav_table[6] = { 0xFE, 0xFE, 0xFF, 0xFF, 0xFF, 0 };
/* choplifter.s $6f8d: pairs (vx,vy) indexed by abs(turn)*12 + abs(accelx)*2 */
static const unsigned char bullet_vel_table[72] = {
    0x00,0x00, 0x00,0x00, 0x01,0x00, 0x02,0x00, 0x02,0x00, 0x02,0x00,
    0x03,0x00, 0x03,0x00, 0x03,0x00, 0x03,0x00, 0x03,0x01, 0x04,0x02,
    0x08,0x00, 0x08,0x00, 0x08,0x00, 0x08,0x01, 0x08,0x02, 0x08,0x02,
    0x0C,0x00, 0x0C,0x00, 0x0C,0x01, 0x0C,0x02, 0x0B,0x02, 0x0A,0x02,
    0x10,0x00, 0x10,0x01, 0x10,0x01, 0x10,0x01, 0x0F,0x02, 0x0E,0x02,
    0x1A,0x00, 0x1A,0x02, 0x1A,0x04, 0x1A,0x06, 0x19,0x08, 0x19,0x0A
};
/* chopperFrontOffsetTable pairs; index (accelx+5)*22 + (turn+5)*2 */
static const signed char chop_front_xy[242] = {
    14,3, 12,3, 10,2, 7,0, 4,0, 3,0, 2,-1, -1,-2,
    -5,-3, -7,-4, -11,-5, 14,2, 12,1, 10,1, 7,0, 4,-1,
    3,-1, 1,-2, -2,-3, -6,-4, -9,-4, -12,-4, 15,1, 13,1,
    11,1, 7,-1, 4,-1, 2,-2, 0,-2, -3,-3, -7,-3, -10,-3,
    -13,-4, 15,0, 13,0, 11,0, 6,0, 3,-1, 1,-1, -1,-1,
    -4,-2, -8,-2, -11,-3, -14,-3, 15,-1, 13,-1, 11,-1, 6,-1,
    3,-1, 1,-1, -1,-1, -4,-2, -9,-2, -12,-2, -15,-2, 15,-2,
    14,-2, 11,-2, 7,-2, 3,-2, 0,-2, -2,-2, -6,-2, -10,-2,
    -13,-2, -15,-2, 15,-2, 14,-2, 11,-2, 7,-2, 3,-1, 0,-1,
    -2,-1, -6,-1, -10,-1, -12,-1, -15,-1, 15,-3, 13,-3, 10,-2,
    6,-2, 3,-1, 1,-1, -2,-1, -6,0, -11,0, -13,0, -15,0,
    14,-4, 12,-4, 8,-4, 4,-3, 2,-2, 0,-1, -2,-1, -6,-1,
    -10,1, -12,1, -14,1, 13,-4, 10,-4, 8,-4, 4,-3, 1,-2,
    -1,-1, -3,0, -6,0, -10,1, -12,1, -14,2, 12,-5, 8,-4,
    5,-3, 2,-3, 0,-2, -1,-1, -3,0, -6,0, -8,2, -11,3,
    -14,3
};
static const unsigned char jet_frame_of_vx[5] = { 0, 1, 8, 13, 19 };

/* choplifter.s rest: table[level]=7, skids at world 0 (Apple row 191).
 * Mode 8 maps world 0 to row 199; DESIGN's BROWN highlight is LAND_POSY
 * (row 174, band ~170-199).  Shift so level skids sit on that line.
 * Pitch deltas in the table stay as in the original. */
#define GROUND_BOTTOM_LEVEL 7U
#define GROUND_Y_SHIFT      (LAND_POSY - GROUND_BOTTOM_LEVEL)
/* Apple Y<4 with rest 7.  Same 3 px into the dirt after the shift. */
#define SHOT_GROUND_Y       (4U + GROUND_Y_SHIFT)

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
    crash_fx     = 0;
    btn1_down    = 0;
    fire_down    = 0;
    fire_held    = 0;
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

/* Apple: accelx != 0 on the ground is "angled, not landed" and blocks
 * boarding.  That pose is the side view.  Head-on / mid-rotate does not
 * show that roll, so stick X used to leave landed=0 while the craft
 * looked planted and facing the camera. */
static int tilt_blocks_landing(void)
{
    int t = (int)turn_state;

    if (t < 0)
        t = -t;
    if (t != 5)
        return 0;
    return accelx != 0;
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
        landed = (unsigned char)(tilt_blocks_landing() ? 0 : 1);
        return;
    }

    vy_neg_mag = (velx < 0) ? velx : -velx;
    nh = (unsigned char)((unsigned)vy_neg_mag >> 8);
    nl = (unsigned)vy_neg_mag & 0xFFU;
    if (nh < 0xEBU || (nh == 0xEBU && nl < 0x01U)) {
        sink_y = 1;
        begin_death();
        return;
    }
    landed = (unsigned char)(tilt_blocks_landing() ? 0 : 1);
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

/* '.' is the documented rotate key.  Joystick mode used to omit it, so a
 * tap of '.' while the title was on J (the default) did nothing. */
static int rotate_is_down(unsigned buttons)
{
    if (kbd_is_down(SCAN_PERIOD) || kbd_is_down(SCAN_ALT) ||
        kbd_is_down(SCAN_LSHIFT))
        return 1;
    if ((buttons & 2U) != 0U)
        return 1;
    return 0;
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
    /* Ctrl-S is mute, not dump-thrust. */
    if ((latch_s || key_y > 0) && !kbd_is_down(SCAN_CTRL))
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
    int      ctrl_a, ctrl_v;

    snapshot_raw_wasd();
    {
        int ctrl_s;

        ctrl_s = kbd_is_down(SCAN_CTRL) && kbd_is_down(SCAN_S);
        if (ctrl_s && !prev_ctrl_s)
            snd_toggle();
        prev_ctrl_s = ctrl_s;
    }

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

        fire_down = 0;
        if (kbd_is_down(SCAN_SLASH))
            fire_down = 1;
        check_rotate_button(rotate_is_down(0));
        kbd_clear_pulse();
        return;
    }

    x = y = buttons = 0;
    live = 0;
    ax = APPLE_CENTER;
    ay = APPLE_CENTER;
    used_port = 0;

    /* The Paku 201h loop counts to STICK_TIMEOUT (0x7FFF) with interrupts
     * off.  A present stick discharges its one-shots and the loop leaves
     * early; with nothing plugged in the axis bits never clear and every
     * tick pays all 32767 IN instructions.  So: probe each tick until a
     * live sample turns up, then keep probing forever.  If none ever does,
     * fall back to about one probe a second so a stick plugged in later is
     * still found.  A real stick held in a corner cannot trip this -- it
     * had to pass through centre to get airborne. */
    if (!stick_seen_live && stick_skip != 0U) {
        stick_skip--;
    } else {
        live = read_stick_hardware(&x, &y, &buttons, &used_port);
        if (!axis_live(x, y))
            live = 0;
        if (live)
            stick_seen_live = 1;
        else if (stick_dead_run < STICK_DEAD_PROBES)
            stick_dead_run++;
        else
            stick_skip = SIM_HZ;
    }

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
        ax = apple_from_raw(x, stick_cx0, throw_xmin, throw_xmax);
        ay = apple_from_raw(y, stick_cy0, throw_ymin, throw_ymax);
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

    fire_down = 0;
    if ((buttons & 1U) != 0U || kbd_is_down(SCAN_SPACE) ||
        kbd_is_down(SCAN_SLASH))
        fire_down = 1;
    check_rotate_button(rotate_is_down(buttons));
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
    if (!snd_did_kill) {
        snd_play(SND_KILL);
        snd_did_kill = 1;
    }
}

/* Landed on them.  Original killHostage blits $a092 once then frees.
 * Hold a couple of ticks so the red pixels on the corpse can register. */
static void crush_hostage(int i)
{
    hostage_act[i] = HST_DIE;
    hostage_anim[i] = (unsigned char)CRUSH_TICKS;
    if (hostages_killed < 255U)
        hostages_killed++;
    if (hostages_active != 0U)
        hostages_active--;
    if (!snd_did_kill) {
        snd_play(SND_KILL);
        snd_did_kill = 1;
    }
}

static int ent_alloc(void)
{
    int i;

    for (i = 0; i < MAX_ENT; i++) {
        if (ents[i].type == ET_FREE)
            return i;
    }
    return -1;
}

static void ent_free(int i)
{
    if (i >= 0 && i < MAX_ENT)
        ents[i].type = ET_FREE;
}

static void compact_ids(unsigned char *tab, unsigned char *n, unsigned char id)
{
    int i, j;

    j = 0;
    for (i = 0; i < (int)*n; i++) {
        if (tab[i] != id)
            tab[j++] = tab[i];
    }
    *n = (unsigned char)j;
}

static void init_ents(void)
{
    int i;

    for (i = 0; i < MAX_ENT; i++) {
        ents[i].type = ET_FREE;
        tank_burst_left[i] = 0;
        tank_burst_wait[i] = 0;
        tank_burst_shot[i] = (unsigned char)TANK_SHOT_NONE;
    }
    num_tanks = num_jets = num_aliens = 0;
    curr_shots = 0;
    alien_odd = 0;
}

static unsigned add16_s8(unsigned x, signed char d)
{
    /* Sign-extend, matching choplifter.s updateBasicPhysics (ADC #0 / ADC
     * #$FF on the high byte).  Zero-extending made -16 into +240, so a
     * left-facing shot spawned a page to the right and vanished. */
    return (unsigned)(x + (unsigned)(int)d);
}

static void ent_basic_phys(int i, unsigned char grav)
{
    entity_t *e = &ents[i];

    e->x = add16_s8(e->x, e->vx);
    e->y = (unsigned char)(e->y + (unsigned char)e->vy);
    e->ground = (unsigned char)(e->ground + (unsigned char)e->dir);
    e->vy = (signed char)(e->vy - (signed char)grav);
}

static void init_explosion(int i)
{
    unsigned char was_shell = (ents[i].type == ET_SHELL);

    ents[i].vx = 0;
    ents[i].vy = 0;
    if (ents[i].y == 0)
        ents[i].type = ET_SINK;
    else
        ents[i].type = ET_BOOM;
    /* Tank shells (and saucer shots, same type) were still booming after
     * the launch sound was muted. */
    if (!was_shell && !snd_did_boom) {
        snd_play(SND_BOOM);
        snd_did_boom = 1;
    }
}

static void chopper_front(signed char *ox, signed char *oy)
{
    int ax, ts, idx;

    ax = (int)accelx + 5;
    ts = (int)turn_state + 5;
    if (ax < 0)
        ax = 0;
    if (ax > 10)
        ax = 10;
    if (ts < 0)
        ts = 0;
    if (ts > 10)
        ts = 10;
    idx = ax * 22 + ts * 2;
    if (idx < 0)
        idx = 0;
    if (idx > 240)
        idx = 240;
    *ox = chop_front_xy[idx];
    *oy = chop_front_xy[idx + 1];
}

static void init_bullet_phys(int i)
{
    int ts, ax, yoff;
    unsigned char vx, vy, g;
    entity_t *e = &ents[i];

    ts = (int)turn_state;
    if (ts < 0)
        ts = -ts;
    ax = (int)accelx;
    if (ax < 0)
        ax = -ax;
    yoff = ts * 12 + ax * 2;
    if (yoff < 0)
        yoff = 0;
    if (yoff > 70)
        yoff = 70;
    vx = bullet_vel_table[yoff];
    vy = bullet_vel_table[yoff + 1];
    if (turn_state < 0)
        vx = (unsigned char)(-(signed char)vx);
    if (velx < 0)
        vx = (unsigned char)((signed char)vx + 1);
    /* original adds VELX_H (high byte of 16-bit vx, already signed-ish) */
    vx = (unsigned char)((signed char)vx + (signed char)((unsigned)velx >> 8));
    if ((turn_state ^ accelx) < 0)
        vy = (unsigned char)(-(signed char)vy);
    e->vx = (signed char)vx;
    e->vy = (signed char)vy;
    g = 0;
    if (ts <= 5)
        g = bullet_grav_table[ts];
    e->dir = (signed char)g;
}

static void try_fire(void)
{
    int i;
    signed char ox, oy;

    if (fire_held || !fire_down)
        goto done;
    if (!airborne || dying || death_timer)
        goto done;
    if (curr_shots >= MAX_SHOTS)
        goto done;
    i = ent_alloc();
    if (i < 0)
        goto done;
    chopper_front(&ox, &oy);
    ox = (signed char)(ox + 1);
    ents[i].type = ET_BULLET;
    ents[i].x = add16_s8(chop_x, (signed char)(-ox));
    /* Apple ADC of signed oy onto CHOP_POS_Y.  chop_y is already the
     * shifted rest line, so this is muzzle altitude in skid space. */
    ents[i].y = (unsigned char)((int)chop_y + (int)oy);
    ents[i].ground = (unsigned char)chop_ground;
    init_bullet_phys(i);
    curr_shots++;
    snd_play(SND_FIRE);
done:
    fire_held = fire_down;
}

static void spawn_tank(void)
{
    int i, plane, y, clash;
    unsigned vxh;
    entity_t *e;

    if ((int)((int)FENCE_X - (int)chop_x) < 0x200)
        return;
    if (num_tanks >= 4U)
        return;
    i = ent_alloc();
    if (i < 0)
        return;
    e = &ents[i];
    vxh = (unsigned char)((unsigned)velx >> 8);
    if (vxh >= 0x11U && vxh < 0xEFU) {
        if (velx < 0)
            goto left;
        goto right;
    }
    if (rnd8() & 8U)
        goto right;
left:
    e->x = chop_x - 0x1B0U;
    e->dir = 4;
    goto plane;
right:
    e->x = chop_x + 0x1B0U;
    e->dir = 0;
plane:
    e->y = 6;
    e->vx = 10;
    e->vy = 0;
    e->type = ET_TANK;
    for (;;) {
        plane = (int)((rnd8() & 7U) + 5U);
        clash = 0;
        for (y = 0; y < (int)num_tanks; y++) {
            if (ents[tank_ids[y]].ground == (unsigned char)plane)
                clash = 1;
        }
        if (!clash)
            break;
    }
    e->ground = (unsigned char)plane;
    tank_burst_left[i] = 0;
    tank_burst_wait[i] = 0;
    tank_burst_shot[i] = (unsigned char)TANK_SHOT_NONE;
    tank_ids[num_tanks++] = (unsigned char)i;
}

static void spawn_jet(void)
{
    int i;
    unsigned vxh;
    entity_t *e;

    /* Tanks/aliens skip the safe zone; spawnJet does not, so a jet can
     * appear at chop_x±$1B8 on the pad.  updateJet still treats the
     * fence page as out of bounds. */
    if ((int)((int)FENCE_X - (int)chop_x) < 0x200)
        return;
    if (num_jets >= 4U)
        return;
    i = ent_alloc();
    if (i < 0)
        return;
    e = &ents[i];
    e->type = ET_JET;
    e->vy = 0;
    e->y = (unsigned char)((chop_y >> 1) + 0x30U);
    if (e->y > 0x68U)
        e->y = 0x68U;
    e->ground = (unsigned char)(chop_ground + 0x20U);
    e->vx = 0;
    if (curr_level >= 2U && (rnd8() & jet_difficulty[curr_level]) != 0U)
        goto neg;
    vxh = (unsigned char)((unsigned)velx >> 8);
    if (vxh < 0x11U || vxh >= 0xEFU) {
        if (rnd8() & 0x10U)
            goto pos;
        goto neg;
    }
    if (velx < 0)
        goto neg;
pos:
    e->dir = (signed char)0xFF;
    e->x = chop_x + 0x1B8U;
    goto done;
neg:
    e->dir = 0;
    e->x = chop_x - 0x1B8U;
done:
    if ((e->x >> 8) >= (FENCE_X >> 8)) {
        ent_free(i);
        return;
    }
    jet_ids[num_jets++] = (unsigned char)i;
}

static void spawn_alien(void)
{
    int i;
    unsigned vxh;
    entity_t *e;

    if ((int)((int)FENCE_X - (int)chop_x) < 0x200)
        return;
    if (num_aliens >= 4U)
        return;
    i = ent_alloc();
    if (i < 0)
        return;
    e = &ents[i];
    e->type = ET_ALIEN;
    e->vx = e->vy = e->dir = 0;
    e->y = 0x70;
    e->ground = (unsigned char)(chop_ground - 1);
    vxh = (unsigned char)((unsigned)velx >> 8);
    if (vxh < 0x11U || vxh >= 0xEFU) {
        if (rnd8() & 0x10U)
            e->x = chop_x + 0x1B8U;
        else
            e->x = chop_x - 0x1B8U;
    } else if (velx < 0)
        e->x = chop_x - 0x1B8U;
    else
        e->x = chop_x + 0x1B8U;
    alien_ids[num_aliens++] = (unsigned char)i;
}

static void spawn_enemies(void)
{
    unsigned char r, lv;

    if ((sim_frame & 0x2FU) != 0U)
        return;
    r = rnd8();
    lv = curr_level;
    if (lv > 3U)
        lv = 3U;
    if (lv == 0U)
        goto tanks;
    if (lv == 1U)
        goto jets;
    if ((r & 0x28U) == 0U && alien_spawn_rate[lv] > num_aliens) {
        spawn_alien();
        return;
    }
jets:
    if ((r & 0x80U) == 0U && jet_spawn_rate[lv] > num_jets) {
        spawn_jet();
        return;
    }
tanks:
    if (tank_spawn_rate[lv] > num_tanks)
        spawn_tank();
}

static void ignite_from_ent(int ei)
{
    unsigned x = ents[ei].x;
    unsigned char dl, dh;
    unsigned hw;

    dl = (unsigned char)((unsigned char)x - (unsigned char)FARHOUSE_X);
    if ((signed char)dl < 0)
        return;
    /* Apple window is $18 world px at each house origin.  Our barracks are
     * 16 screen px (32 world); keep the full footprint hittable. */
    hw = (unsigned)house_e[0] << 1;
    if (hw < 0x18U)
        hw = 0x18U;
    if ((unsigned)dl >= hw)
        return;
    dh = (unsigned char)((x >> 8) - (FARHOUSE_X >> 8));
    if (dh >= 3U)
        return;
    if (house_states[dh] == 0U) {
        house_states[dh] = 1;
        scenery_invalidate();
        /* Player bombs are still ET_BULLET here; tank shells have already
         * become BOOM.  Don't let off-screen shells chirp the house. */
        if (ents[ei].type == ET_BULLET)
            snd_play(SND_HOUSE);
    }
}

static void begin_death(void)
{
    if (!dying)
        snd_play(SND_DEATH);
    dying = 1;
}

static void chopper_hit(int ei)
{
    begin_death();
    ents[ei].vy = 4;
}

static int dx16(unsigned a, unsigned b)
{
    return (int)a - (int)b;
}

/* 2x2 screen shell vs the chopper body.  X is 2 world px per screen px;
 * Y is 1:1.  Origin of the shell is the top-left, same as the blit. */
static int shell_overlaps_chopper(int ei, int check_y)
{
    entity_t *e = &ents[ei];
    int dx, shell_top, shell_bot, chop_bot, chop_top;

    dx = dx16(e->x, chop_x);
    if (dx >= (int)CHOP_WORLD_W || dx <= -(int)SHELL_WORLD_W)
        return 0;
    if (!check_y)
        return 1;
    shell_top = (int)e->y + (int)e->ground;
    shell_bot = shell_top - ((int)SHELL_PX - 1);
    chop_bot = (int)chop_y;
    chop_top = chop_bot + (int)CHOP_HIT_H - 1;
    if (shell_bot > chop_top || shell_top < chop_bot)
        return 0;
    return 1;
}

static void ordinance_check(int ei)
{
    entity_t *e = &ents[ei];
    int i, d;
    unsigned char act;
    int dirt;

    if ((e->x >> 8) >= BASE_X_H)
        return;
    /* Chopper bombs explode at SHOT_GROUND_Y after a couple of ticks;
     * DIR has only dropped GROUND from 22 toward 20, while tanks live
     * in planes 5-12.  Apple's bombs fell through Y=7..4 so the planes
     * met.  A dirt burst therefore hits tanks by X only. */
    dirt = (e->type == ET_BULLET);
    if (e->ground >= chop_ground) {
        if (!airborne) {
            if (shell_chop_aabb) {
                if (shell_overlaps_chopper(ei, 0))
                    chopper_hit(ei);
            } else {
                d = dx16(e->x, chop_x);
                if (d >= 0 && d < 12)
                    chopper_hit(ei);
                else if (d < 0 && d > -11)
                    chopper_hit(ei);
            }
        }
        for (i = 0; i < MAX_HOSTAGES; i++) {
            if (hostage_anim[i] == HST_FREE)
                continue;
            act = hostage_act[i];
            if (act == 2U || act == 0xFEU || act == HST_DIE)
                continue;
            d = dx16(e->x, hostage_x[i]);
            if ((d >= 0 && d < 10) || (d < 0 && d > -11))
                kill_hostage(i);
        }
        ignite_from_ent(ei);
        return;
    }
    for (i = 0; i < (int)num_tanks; i++) {
        int ti = tank_ids[i];
        unsigned char gd;

        if (!dirt) {
            gd = (unsigned char)(e->ground - ents[ti].ground);
            if (gd >= 0x0AU && gd <= 0xF5U)
                continue;
        }
        d = dx16(e->x, ents[ti].x);
        if ((d >= 0 && d < 14) || (d < 0 && d > -13)) {
            ents[ei].x = ents[ti].x - 3U;
            ents[ei].y = 0;
            ents[ei].ground = ents[ti].ground;
            ents[ti].x += 3U;
            ents[ti].y = 0;
            init_explosion(ti);
            init_explosion(ei);
            compact_ids(tank_ids, &num_tanks, (unsigned char)ti);
            return;
        }
    }
    /* Bombs spend a couple of ticks with DIR dropping GROUND below
     * chop_ground, so they used to skip the house branch.  A dirt burst
     * still lights barracks by X. */
    ignite_from_ent(ei);
}

static void destroy_jet(int ji, int bullet)
{
    int ti = jet_ids[ji];
    int boom;
    unsigned x, y;

    x = ents[ti].x;
    y = ents[ti].y;
    ents[ti].ground = (unsigned char)chop_ground;
    ents[ti].y = (unsigned char)y;
    init_explosion(ti);
    ents[ti].vy = 1;
    boom = ent_alloc();
    if (boom >= 0) {
        ents[boom].type = ET_BOOM;
        ents[boom].x = x - 4U;
        ents[boom].y = (unsigned char)y;
        ents[boom].ground = (unsigned char)chop_ground;
        ents[boom].vx = 1;
        ents[boom].vy = 1;
        init_explosion(boom);
        ents[boom].vx = 1;
        ents[boom].vy = 1;
    }
    if (bullet >= 0) {
        ents[bullet].x = x + 4U;
        ents[bullet].y = (unsigned char)y;
        ents[bullet].ground = (unsigned char)chop_ground;
        init_explosion(bullet);
        ents[bullet].vx = 1;
        ents[bullet].vy = 1;
    }
    compact_ids(jet_ids, &num_jets, (unsigned char)ti);
}

static void destroy_alien(int ji, int bullet)
{
    int ti = alien_ids[ji];
    int boom;
    unsigned x, y;

    x = ents[ti].x;
    y = ents[ti].y;
    ents[ti].ground = (unsigned char)chop_ground;
    init_explosion(ti);
    ents[ti].vy = 2;
    boom = ent_alloc();
    if (boom >= 0) {
        ents[boom].x = x - 4U;
        ents[boom].y = (unsigned char)y;
        ents[boom].ground = (unsigned char)chop_ground;
        init_explosion(boom);
        ents[boom].vx = 1;
        ents[boom].vy = 2;
    }
    if (bullet >= 0) {
        ents[bullet].x = x + 4U;
        ents[bullet].y = (unsigned char)y;
        ents[bullet].ground = (unsigned char)chop_ground;
        init_explosion(bullet);
        ents[bullet].vx = 1;
        ents[bullet].vy = 2;
    }
    compact_ids(alien_ids, &num_aliens, (unsigned char)ti);
}

static void check_chopper_hit_air(int ei)
{
    int dy, d;

    dy = (int)ents[ei].y - (int)chop_y;
    if (dy >= 10 || dy <= -9)
        return;
    d = dx16(ents[ei].x, chop_x);
    if ((d >= 0 && d < 11) || (d < 0 && d > -10)) {
        ents[ei].ground = (unsigned char)(chop_ground - 1);
        init_explosion(ei);
        chopper_hit(ei);
    }
}

static void check_bullet(int ei)
{
    entity_t *e = &ents[ei];
    int i, d, dy;
    unsigned char gd;

    if (e->ground != chop_ground)
        return;
    if (e->y < 0x0EU) {
        unsigned char dl, dh;

        dl = (unsigned char)((unsigned char)e->x - (unsigned char)FARHOUSE_X);
        dh = (unsigned char)((e->x >> 8) - (FARHOUSE_X >> 8));
        if ((signed char)dl >= 0 && dl < 0x18U && dh < 4U) {
            e->y = 0;
            init_explosion(ei);
            ordinance_check(ei);
            if (curr_shots)
                curr_shots--;
            return;
        }
    }
    if (e->y >= 0x18U) {
        for (i = 0; i < (int)num_jets; i++) {
            int ji = jet_ids[i];

            gd = (unsigned char)(ents[ji].ground - chop_ground);
            if (gd >= 8U)
                continue;
            dy = (int)e->y - (int)ents[ji].y;
            if (dy >= 9 || dy <= -8)
                continue;
            d = dx16(e->x, ents[ji].x);
            if ((d >= 0 && d < 20) || (d < 0 && d > -19)) {
                init_explosion(ei);
                destroy_jet(i, ei);
                if (curr_shots)
                    curr_shots--;
                return;
            }
        }
        for (i = 0; i < (int)num_aliens; i++) {
            int ji = alien_ids[i];

            dy = (int)e->y - (int)ents[ji].y;
            if (dy >= 9 || dy <= -8)
                continue;
            d = dx16(e->x, ents[ji].x);
            if ((d >= 0 && d < 17) || (d < 0 && d > -16)) {
                init_explosion(ei);
                destroy_alien(i, ei);
                if (curr_shots)
                    curr_shots--;
                return;
            }
        }
    }
}

/* Kept for when tank shells are unmuted: only play if some of the tank
 * overlaps the 160 px window. */
#if 0
static int tank_on_screen(const entity_t *e)
{
    int sx, w;

    sx = world_to_sx(e->x);
    w = (int)tank_tread_m9[0][0];
    if (w <= 0)
        w = 18;
    /* Cannon can stick ~12 px past the treads. */
    if (sx + w + 12 <= 0)
        return 0;
    if (sx - 12 >= (int)M8_WIDTH_PX)
        return 0;
    return 1;
}
#endif

/* choplifter.s updateTankGoRight: do not walk up to FENCE_X.  Same-page
 * keep-out is $C0 + ~(GROUND<<3), ~95-151 world px by plane. */
static int tank_can_go_right(const entity_t *e)
{
    unsigned diff;
    unsigned char dh, dl, lim, g8;

    diff = FENCE_X - e->x;
    dh = (unsigned char)(diff >> 8);
    dl = (unsigned char)diff;
    if (dh == 0U) {
        g8 = (unsigned char)((unsigned)e->ground << 3);
        lim = (unsigned char)(0xC0U + (unsigned char)~g8);
        if (lim < dl)
            return 1;
        return 0;
    }
    if ((signed char)dh > 0)
        return 1;
    return 0;
}

static void tank_step_right(entity_t *e)
{
    if (tank_can_go_right(e))
        e->x += 4U;
}

static int fire_tank_shell(int ti)
{
    int si;
    entity_t *t, *s;
    signed char off;
    unsigned char ang;

    si = ent_alloc();
    if (si < 0)
        return -1;
    t = &ents[ti];
    s = &ents[si];
    ang = (unsigned char)t->dir;
    if (ang > 4U)
        ang = 4U;
    s->type = ET_SHELL;
    s->x = t->x - 0x10U;
    off = (signed char)shell_launch_x[ang];
    off = (signed char)(off + t->vy);
    s->x = add16_s8(s->x, off);
    s->y = (unsigned char)(t->y + 9U);
    s->ground = (unsigned char)(t->ground + 1U);
    s->vx = (signed char)(shell_launch_vx[ang] + t->vy);
    s->vy = 3;
    s->dir = (signed char)shell_launch_dir[ang];
    /* Tank shells muted: off-screen fire was jarring.  Restore later. */
    /* if (tank_on_screen(t))
        snd_play(SND_SHELL); */
    return si;
}

static void update_tank(int i)
{
    entity_t *e = &ents[i];
    int scratch, aim_ready, si;
    unsigned diff;
    unsigned char ang, live, diff_hi, diff_lo;

    /* True 16-bit subtraction with borrow, matching the original's
     * SEC/SBC pair on CHOP_POS_X/ENTITY_X -- NOT independent high-byte
     * and low-byte subtraction, which disagrees with this exactly when
     * chop_x and e->x straddle a 256-unit boundary. */
    diff = (chop_x - e->x) & 0xFFFFU;
    diff_hi = (unsigned char)(diff >> 8);
    diff_lo = (unsigned char)diff;

    if (diff_hi == 0x00U) {                 /* "under": chopper at/right, same page */
        aim_ready = ((diff_lo & 0x80U) == 0U);
        scratch = (int)diff_lo;
    } else if (diff_hi == 0xFFU) {           /* "get closer": chopper at/left, same page */
        aim_ready = ((diff_lo & 0x80U) != 0U);
        scratch = (int)diff_lo;
    } else if (diff_hi == 0x01U || diff_hi == 0x02U ||
               diff_hi == 0xFDU || diff_hi == 0xFEU) {
        aim_ready = 0;                       /* "close": one page off, idle only */
        scratch = (int)(unsigned char)(diff_hi ^ 0x80U);
    } else {                                 /* too far away: despawn */
        compact_ids(tank_ids, &num_tanks, (unsigned char)i);
        ent_free(i);
        return;
    }
    if (aim_ready)
        scratch = (int)(unsigned char)(scratch + 0x80);

    if (dying) {
        tank_burst_left[i] = 0;
        tank_burst_wait[i] = 0;
        tank_burst_shot[i] = (unsigned char)TANK_SHOT_NONE;
        if ((scratch & 0x80) == 0) {
            if (e->x > 4U)
                e->x -= 4U;
            e->vy = (signed char)0xFC;
        }
        return;
    }
    if (!aim_ready) {
        /* Not lined up on the chopper's page yet, or facing the wrong
         * way -- move toward the chopper, same as the original's
         * updateTankGoLeft (bit 7 set means the chopper is to the
         * right, by construction of the diff/scratch math above). No
         * aim swivel or firing this tick. */
        if ((scratch & 0x80) != 0) {
            tank_step_right(e);
            e->vy = 4;
        } else {
            if (e->x > 4U)
                e->x -= 4U;
            e->vy = (signed char)0xFC;
        }
        return;
    }
    e->vx--;
    if (e->vx == 0) {
        e->vx = (signed char)((rnd8() & 0x0FU) + 8U);
        if (e->vy == 0) {
            if ((rnd8() & 0x17U) == 0)
                e->vy = (signed char)0xFC;
            else if (rnd8() & 0x10U) {
                tank_step_right(e);
                e->vy = 4;
            }
        } else if (((e->vy ^ (signed char)scratch) & 0x80) != 0) {
            if (rnd8() & 5U)
                e->vy = 0;
            else if (scratch & 0x80) {
                tank_step_right(e);
                e->vy = 4;
            } else {
                e->x -= 4U;
                e->vy = (signed char)0xFC;
            }
        }
    } else {
        if (e->vy > 0)
            tank_step_right(e);
        else if (e->vy < 0) {
            if (e->x > 4U)
                e->x -= 4U;
        }
    }
    ang = (unsigned char)e->dir;
    if (ang > 4U)
        ang = 4U;
    if ((unsigned char)scratch < tank_aim_table[ang]) {
        if (ang > 0U)
            e->dir--;
    } else if (tank_aim_table[ang + 1] < (unsigned char)scratch) {
        if (ang < 4U)
            e->dir++;
    }

    /* One shell in flight at a time: wait for impact, then a beat while
     * the boom is on the dirt.  After four, a 2 s rest.  A volley already
     * started finishes even if aim walks off. */
    live = tank_burst_shot[i];
    if (live != (unsigned char)TANK_SHOT_NONE &&
        (unsigned)live < (unsigned)MAX_ENT &&
        ents[live].type == ET_SHELL) {
        /* still arcing */
    } else if (live != (unsigned char)TANK_SHOT_NONE) {
        tank_burst_shot[i] = (unsigned char)TANK_SHOT_NONE;
        if (tank_burst_left[i] != 0)
            tank_burst_wait[i] = (unsigned char)TANK_SHOT_BEAT;
        else
            tank_burst_wait[i] = (unsigned char)TANK_BURST_COOL;
    } else if (tank_burst_wait[i] != 0) {
        tank_burst_wait[i]--;
    } else if (tank_burst_left[i] != 0) {
        si = fire_tank_shell(i);
        if (si >= 0) {
            tank_burst_shot[i] = (unsigned char)si;
            tank_burst_left[i]--;
        }
    } else if ((unsigned char)scratch >= tank_aim_table[ang] &&
               tank_aim_table[ang + 1] >= (unsigned char)scratch) {
        si = fire_tank_shell(i);
        if (si >= 0) {
            tank_burst_shot[i] = (unsigned char)si;
            tank_burst_left[i] = (unsigned char)(TANK_BURST_N - 1U);
        }
    }
}

static void fire_jet_missiles(int ji)
{
    int a, b;
    signed char vx;

    a = ent_alloc();
    b = ent_alloc();
    if (a < 0)
        return;
    vx = 0x18;
    if (ents[ji].dir < 0)
        vx = (signed char)(-vx);
    ents[a].type = ET_MISSILE;
    ents[a].x = ents[ji].x;
    ents[a].y = (unsigned char)(ents[ji].y + 8U);
    ents[a].ground = (unsigned char)chop_ground;
    ents[a].vx = vx;
    ents[a].vy = 0;
    ents[a].dir = 0;
    if (b >= 0) {
        ents[b] = ents[a];
        ents[b].y = (unsigned char)(ents[ji].y - 9U);
    }
    snd_play(SND_MISSILE);
}

static void drop_jet_bomb(int ji)
{
    int b;
    signed char vx;

    b = ent_alloc();
    if (b < 0)
        return;
    vx = 0x14;
    if (ents[ji].dir < 0)
        vx = (signed char)(-vx);
    ents[b].type = ET_BOMB;
    ents[b].x = ents[ji].x;
    ents[b].y = (unsigned char)(ents[ji].y - 1U);
    ents[b].ground = (unsigned char)chop_ground;
    ents[b].vx = vx;
    ents[b].vy = (signed char)0xFC;
    ents[b].dir = 0;
}

static const unsigned char *jet_yv_rec(const entity_t *e)
{
    unsigned vx, vy, n;

    vx = (unsigned char)e->vx;
    if (vx > 4U)
        vx = 4U;
    n = jet_yv_n[vx];
    if (n == 0U)
        n = 1U;
    vy = (unsigned char)e->vy;
    if (vy >= n)
        vy = (unsigned char)(n - 1U);
    return jet_yv_tab[vx] + (unsigned)vy * 4U;
}

static int jet_sprite_frame(const entity_t *e)
{
    unsigned vx, fi;
    const unsigned char *p;

    vx = (unsigned char)e->vx;
    if (vx > 4U)
        vx = 4U;
    p = jet_yv_rec(e);
    fi = (unsigned)jet_frame_of_vx[vx] + (unsigned)p[0];
    if (fi > 24U)
        fi = 24U;
    return (int)fi;
}

static void jet_apply_record(int i)
{
    entity_t *e = &ents[i];
    const unsigned char *p;
    signed char dx, dy, dg;

    p = jet_yv_rec(e);
    dx = (signed char)p[1];
    dy = (signed char)p[2];
    dg = (signed char)p[3];
    if (e->dir < 0)
        dx = (signed char)(-dx);
    e->x = add16_s8(e->x, dx);
    e->y = (unsigned char)(e->y + (unsigned char)dy);
    e->ground = (unsigned char)(e->ground + (unsigned char)dg);
}

static void update_jet(int i)
{
    entity_t *e = &ents[i];
    unsigned char vx, vy, dh, dl, abs_pvx, pvxh;
    int dxh;

    jet_apply_record(i);

    dxh = (int)(e->x >> 8) - (int)(chop_x >> 8);
    if (dxh > 2 || dxh < -2) {
        compact_ids(jet_ids, &num_jets, (unsigned char)i);
        ent_free(i);
        return;
    }

    vx = (unsigned char)e->vx;
    vy = (unsigned char)e->vy;

    /* choplifter.s: altitude match only when 0 < VX < 3 */
    if (vx != 0U && vx < 3U) {
        if ((int)e->y < (int)chop_y)
            e->y++;
        else if ((int)e->y - (int)chop_y >= 0x1E)
            e->y--;
    }

    /* Apple only tests the fence on VX==0 / $80.  VX 1–4 (swoop, shots,
     * climb) still fly +12/tick facing right, so they crossed.  Stay on
     * the combat side; face left so climb does not carry them through. */
    if (e->x >= FENCE_X) {
        e->x = FENCE_X - 1U;
        e->dir = (signed char)0xFF;
        goto fast_climb;
    }
    if (vx == 0U) {
        if (dying || (e->x >> 8) >= (FENCE_X >> 8))
            goto fast_climb;
        goto in_bounds;
    }
    if (vx >= 3U)
        goto shot_avail;

    /* VX 1 or 2: swoop through the Y-velocity table, then bank. */
    e->vy = (signed char)(vy + 1U);
    if ((unsigned char)e->vy != jet_climb_table[vx])
        return;
    if (vx == 1U)
        e->dir = (signed char)((unsigned char)e->dir ^ 0xFFU);
    e->vy = 0;
    if (airborne)
        e->vx = 3;
    else
        e->vx = 4;
    return;

shot_avail:
    if (vy == 0U) {
        if (vx == 3U)
            fire_jet_missiles(i);
        else
            drop_jet_bomb(i);
    }
    vx = (unsigned char)e->vx;
    if (vx > 4U)
        vx = 4U;
    /* Original INCs then CMPs climb; on match it does not STA, so VY
     * stays at climb-1 until the X cull.  Storing climb and freeing
     * here made on-screen jets vanish after one pass. */
    if ((unsigned char)(vy + 1U) == jet_climb_table[vx])
        return;
    e->vy = (signed char)(vy + 1U);
    return;

fast_climb:
    e->vx = 4;
    e->vy = (signed char)0x13;
    return;

in_bounds:
    pvxh = (unsigned char)((unsigned)velx >> 8);
    if ((signed char)pvxh < 0)
        pvxh ^= 0xFFU;
    abs_pvx = pvxh;
    dl = (unsigned char)(e->x - chop_x);
    dh = (unsigned char)((e->x - chop_x) >> 8);
    if (dh == 0U)
        goto close;
    if (dh == 1U || dh == 0xFEU)
        goto sorta;
    if (dh != 0xFFU)
        goto fast_climb;
    dl ^= 0xFFU;
close:
    if ((unsigned char)((curr_level << 2) + 0x98U) >= dl)
        goto check_sign;
sorta:
    if (((unsigned char)((unsigned)velx >> 8) ^ (unsigned char)e->dir) & 0x80U) {
        /* Opposite heading. */
        if ((signed char)e->vx < 0) {
            if (abs_pvx >= 0x0FU)
                goto med;
        } else if (abs_pvx >= 0x14U)
            goto slow;
    }
    return;

check_sign:
    if ((signed char)e->vx < 0)
        goto med;
    if (!((dh ^ (unsigned char)e->dir) & 0x80U))
        goto slow;
    if (dl >= 0x40U)
        return;
    if (abs_pvx < 0x09U)
        return;
    if (!(((unsigned char)((unsigned)velx >> 8) ^ (unsigned char)e->dir) & 0x80U))
        goto med;
slow:
    e->vy = 0;
    e->vx = 1;
    return;
med:
    e->vy = 0;
    e->vx = 2;
}

static void update_alien(int i)
{
    entity_t *e = &ents[i];
    int dxh, dxl;

    dxh = (int)(chop_x >> 8) - (int)(e->x >> 8);
    if (dxh < -2 || dxh > 2) {
        compact_ids(alien_ids, &num_aliens, (unsigned char)i);
        ent_free(i);
        return;
    }
    dxl = dx16(chop_x, e->x);
    if (dxl >= 0) {
        if (e->vx < 5)
            e->vx++;
    } else {
        if (e->vx > -5)
            e->vx--;
    }
    if ((int)chop_y >= (int)e->y) {
        if (e->vy < 1)
            e->vy++;
    } else {
        if (e->vy > -1)
            e->vy--;
    }
    e->dir = (signed char)(((unsigned char)e->dir + 1U) % 3U);
    ent_basic_phys(i, 0);
    e->dir = (signed char)(((unsigned char)e->dir) % 3U);
    {
        int dy, d;

        dy = (int)e->y - (int)chop_y;
        d = dx16(e->x, chop_x);
        if (dy > -8 && dy < 8 && ((d >= 0 && d < 19) || (d < 0 && d > -18))) {
            compact_ids(alien_ids, &num_aliens, (unsigned char)i);
            init_explosion(i);
            chopper_hit(i);
            return;
        }
    }
    if (curr_level >= 3U && e->y < 0x58U && (rnd8() & 0x48U) == 0U) {
        int b = ent_alloc();
        signed char vx = 10;

        if (b < 0)
            return;
        if (rnd8() & 0x80U)
            vx = (signed char)(-vx);
        ents[b].type = ET_SHELL;
        ents[b].x = e->x;
        ents[b].y = (unsigned char)(e->y + (unsigned char)e->vy);
        ents[b].ground = (unsigned char)chop_ground;
        ents[b].vx = vx;
        ents[b].vy = (signed char)0xFC;
        ents[b].dir = 3;
        snd_play(SND_ALIEN);
    }
}

static void update_bullet(int i)
{
    entity_t *e = &ents[i];
    unsigned char grav;
    int dxh;

    /* choplifter.s updateBullet: DIR 0 is a side shot (no gravity).
     * Any other DIR is a bomb; physics A is 2, not the DIR byte.
     * DIR still adds into GROUND (tank plane).  We draw at ENTITY_Y, same
     * as the chopper, so that drop must not be folded into sprite Y or
     * facing-camera bombs vanish in a couple of frames. */
    grav = 2;
    if (e->dir == 0)
        grav = 0;
    ent_basic_phys(i, grav);
    dxh = (int)(e->x >> 8) - (int)(chop_x >> 8);
    if (dxh >= 2 || dxh < -1)
        goto gone;
    if ((signed char)e->ground < 3)
        e->ground = 3;
    /* Apple: Y $E0+ or Y<4 is a ground hit, with rest CHOP_POS_Y=7 so the
     * dirt is 3 px under the skids.  Our skids sit at LAND_POSY (25);
     * SHOT_GROUND_Y is that same 3 px.  Keeping Apple's Y<4 after the
     * shift left side-on tracers in the brown (spawn ~23) and let a
     * nose-down burst tunnel 18 px of dirt before colliding — it looked
     * like horizontal fire did nothing.  Do not delete on GROUND==3. */
    if (e->y >= 0xE0U || e->y < SHOT_GROUND_Y) {
        ordinance_check(i);
        if (e->type == ET_BULLET) {
            e->y = (unsigned char)LAND_POSY;
            e->ground = 0;
            init_explosion(i);
        }
        goto shot;
    }
    if (e->y >= 0x90U)
        goto gone;
    check_bullet(i);
    return;
gone:
    ent_free(i);
shot:
    if (curr_shots)
        curr_shots--;
}

static void update_shell(int i)
{
    entity_t *e = &ents[i];

    ent_basic_phys(i, 3);
    if (e->y != 0 && (signed char)e->y > 0) {
        if (shell_overlaps_chopper(i, 1)) {
            init_explosion(i);
            chopper_hit(i);
        }
        return;
    }
    e->y = 0;
    e->ground = (unsigned char)chop_ground;
    init_explosion(i);
    shell_chop_aabb = 1;
    ordinance_check(i);
    shell_chop_aabb = 0;
}

static void update_missile(int i)
{
    entity_t *e = &ents[i];

    ent_basic_phys(i, 1);
    if (e->y >= 0xA0U || e->y < 3U) {
        e->y = 0;
        init_explosion(i);
        ordinance_check(i);
        return;
    }
    check_chopper_hit_air(i);
}

static void update_bomb(int i)
{
    entity_t *e = &ents[i];

    ent_basic_phys(i, 4);
    if (e->y < 0xA0U)
        return;
    e->y = 0;
    init_explosion(i);
    ordinance_check(i);
}

static void update_boom(int i)
{
    ents[i].vx++;
    if ((unsigned char)ents[i].vx >= 5U)
        ent_free(i);
}

static void spawn_ground_crash(void)
{
    int i;

    i = ent_alloc();
    if (i < 0)
        return;
    ents[i].x = chop_x - 8U + (rnd8() & 0x0FU);
    ents[i].y = 0;
    ents[i].ground = (unsigned char)chop_ground;
    init_explosion(i);
    ents[i].vy = 4;
}

static void update_ents(void)
{
    int i;
    unsigned char t;

    for (i = 0; i < MAX_ENT; i++) {
        t = ents[i].type;
        if (t == ET_FREE)
            continue;
        if (t == ET_TANK)
            update_tank(i);
        else if (t == ET_JET)
            update_jet(i);
        else if (t == ET_ALIEN)
            update_alien(i);
        else if (t == ET_BULLET)
            update_bullet(i);
        else if (t == ET_SHELL)
            update_shell(i);
        else if (t == ET_MISSILE)
            update_missile(i);
        else if (t == ET_BOMB)
            update_bomb(i);
        else if (t == ET_BOOM || t == ET_SINK)
            update_boom(i);
    }
}

static void blit_aligned(unsigned seg, int x_px, int y, unsigned char *spr,
                         dirty_list *list)
{
    x_px &= ~1;
    blit_at(seg, x_px, y, spr, spr, list);
}

static void draw_ents(unsigned seg, dirty_list *list)
{
    int i, sx, sy, fi;
    entity_t *e;
    unsigned char *spr;

    for (i = 0; i < MAX_ENT; i++) {
        e = &ents[i];
        if (e->type == ET_FREE)
            continue;
        sx = world_to_sx(e->x);
        /* Chopper blit origin is the *bottom* (skids at chop_y).  Shots
         * spawn in that space; world_to_sy(e->y) would put the *top* of a
         * 3 px tracer at the skids and hang it into the brown. */
        if (e->type == ET_BULLET) {
            unsigned bh = (unsigned)bullet_chop_e[1];

            if (bh == 0U)
                bh = 1U;
            sy = world_to_sy((unsigned)e->y + bh - 1U);
        } else
            sy = world_to_sy((unsigned)e->y + (unsigned)e->ground);
        if (e->type == ET_TANK) {
            unsigned th;
            unsigned wy;

            /* Chopper skids live at LAND_POSY; tanks were Y+GROUND in Apple
             * space (~11-18) and sat in the brown under the bombs.  Plane
             * 5-12 is a few pixels of field depth; BIAS keeps treads in
             * the dirt under the highlight rather than on it. */
            wy = (unsigned)LAND_POSY + (unsigned)e->ground - TANK_GROUND_BIAS;
            th = (unsigned)tank_tread_m9[0][1];
            if (th == 0U)
                th = 1U;
            if (wy + th > 0U)
                sy = world_to_sy(wy + th - 1U);
            else
                sy = world_to_sy(wy);
            fi = (e->x & 4U) ? 0 : 1;
            blit_aligned(seg, sx, sy, tank_tread_m9[fi], list);
            blit_aligned(seg, sx + 4, sy - 3, tank_turret_e, list);
            fi = (int)(unsigned char)e->dir;
            if (fi > 4)
                fi = 4;
            if (fi < 0)
                fi = 0;
            blit_aligned(seg, sx + (signed char)cannon_angle_x[fi],
                         sy - 7, tank_cannon_e[fi], list);
        } else if (e->type == ET_JET) {
            fi = jet_sprite_frame(e);
            spr = (e->dir < 0) ? jet_fe[fi] : jet_e[fi];
            blit_aligned(seg, sx, sy, spr, list);
        } else if (e->type == ET_ALIEN) {
            blit_aligned(seg, sx, sy, alien_e[0], list);
            fi = (int)((unsigned char)e->dir % 3U);
            blit_aligned(seg, sx + 2, sy - 6, alien_e[1 + fi], list);
        } else if (e->type == ET_BULLET)
            blit_at(seg, sx, sy, bullet_chop_e, bullet_chop_o, list);
        else if (e->type == ET_SHELL)
            blit_at(seg, sx, sy, tank_shell_e, tank_shell_o, list);
        else if (e->type == ET_MISSILE) {
            if (e->vx < 0)
                blit_at(seg, sx, sy, bullet_missile_fe, bullet_missile_fo,
                        list);
            else
                blit_at(seg, sx, sy, bullet_missile_e, bullet_missile_o, list);
        } else if (e->type == ET_BOMB)
            blit_at(seg, sx, sy, bullet_bomb_e, bullet_bomb_o, list);
        else if (e->type == ET_BOOM || e->type == ET_SINK) {
            unsigned eh;

            fi = (int)((unsigned char)e->vx % 5U);
            eh = (unsigned)explosion_e[fi][1];
            if (eh == 0U)
                eh = 1U;
            sy = world_to_sy((unsigned)e->y + (unsigned)e->ground + eh - 1U);
            blit_fire = 1;
            blit_aligned(seg, sx, sy, explosion_e[fi], list);
            if (e->type == ET_SINK && fi >= 3)
                blit_aligned(seg, sx, sy, chop_rubble_e, list);
            blit_fire = 0;
        }
    }
    if (dying) {
        unsigned eh, wy;
        int      fi2, k, dx;

        fi2 = (int)((sim_frame / 2U) % 5U);
        eh = (unsigned)explosion_e[fi2][1];
        if (eh == 0U)
            eh = 1U;
        wy = chop_y;
        if (sink_y != 0U && wy > sink_y)
            wy -= sink_y;
        sy = world_to_sy(wy + eh - 1U);
        sx = world_to_sx(chop_x);
        blit_fire = 1;
        for (k = 0; k < 3; k++) {
            dx = (k - 1) * 10;
            fi2 = (int)((sim_frame / 2U + (unsigned)k) % 5U);
            blit_aligned(seg, sx + dx, sy - ((k * 3) & 3),
                         explosion_e[fi2], list);
        }
        blit_fire = 0;
    }
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
    snd_play(SND_RESCUE);
}

static void spawn_hostages(void)
{
    unsigned char dh;
    int house, start, best, best_d, dx, adx;
    unsigned hx;

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

    /* Apple: HOSTAGES_ACTIVE >= 5 skips all field spawns.  The game
     * starts with 8 already out of house 3, so a barracks you just
     * ignited never emptied until four of those eight were gone.
     * Ignite only sets house_states; people still trickle one per 4
     * ticks.  A burning house on this screen releases even if the
     * starting eight are still in the field.  House 3 keeps the 5-cap
     * so the opening group does not immediately double. */
    best = -1;
    best_d = 32767;
    for (house = 0; house < (int)N_HOUSES; house++) {
        if (house_states[house] == 0U || hostages_in_houses[house] == 0U)
            continue;
        hx = FARHOUSE_X + (unsigned)house * HOUSE_SPACING;
        dx = (int)chop_x - (int)hx;
        adx = (dx < 0) ? -dx : dx;
        if (adx <= (int)VIEW_WORLD_W && adx < best_d) {
            best_d = adx;
            best = house;
        }
    }
    if (best >= 0 && (best < 3 || hostages_active < 5U)) {
        spawn_one_in_field(best);
        return;
    }
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
    if (!snd_did_board) {
        snd_play(SND_BOARD);
        snd_did_board = 1;
    }
}

static void update_one_hostage(int i)
{
    unsigned char hhi, chi, dhi, act, scratch;
    int           dx;

    if (hostage_anim[i] == HST_FREE)
        return;
    if (hostage_act[i] == HST_DIE) {
        if (hostage_anim[i] <= 1U)
            hostage_anim[i] = HST_FREE;
        else
            hostage_anim[i]--;
        return;
    }

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
    {
        int ts = (int)turn_state;
        int head;

        if (ts < 0)
            ts = -ts;
        /* Side-on: Apple 8-9 world px right / 10-11 left, crush closer.
         * Head-on body is ~16 world px at chop_x.  Board at 6-11 / 12-7
         * so they reach the fuselage, not the empty air beside it. */
        head = (ts != 5);
        if (dx >= 0 && dx <= 255) {
            scratch = (unsigned char)dx;
            if (head) {
                if (scratch < 6U) {
                    crush_hostage(i);
                    return;
                }
                if (scratch < 12U) {
                    if (hostages_loaded >= 16U) {
                        hostage_anim[i] = 0;
                        hostage_act[i] = 1;
                    } else {
                        hostage_board(i, 0xFE);
                    }
                }
            } else if (scratch < 8U) {
                crush_hostage(i);
                return;
            } else if (scratch < 10U) {
                if (hostages_loaded >= 16U) {
                    hostage_anim[i] = 0;
                    hostage_act[i] = 1;
                } else {
                    hostage_board(i, 0xFE);
                }
            }
        } else if (dx < 0 && dx >= -256) {
            scratch = (unsigned char)dx;
            if (head) {
                if (scratch >= 0xFAU) {
                    crush_hostage(i);
                    return;
                }
                if (scratch >= 0xF4U) {
                    if (hostages_loaded >= 16U) {
                        hostage_anim[i] = 0;
                        hostage_act[i] = 0xFF;
                    } else {
                        hostage_board(i, 2);
                    }
                }
            } else if (scratch >= 0xF7U) {
                crush_hostage(i);
                return;
            } else if (scratch >= 0xF5U) {
                if (hostages_loaded >= 16U) {
                    hostage_anim[i] = 0;
                    hostage_act[i] = 0xFF;
                } else {
                    hostage_board(i, 2);
                }
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
    if (act == HST_DIE)
        return;
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
    init_ents();
    sortie = 0;
    game_over = 0;
    end_kind = 0;
    end_left = 0;
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
    crash_fx = 0;
    turn_state = -5;
    chop_face = -1;
    turn_request = 0;
}

static void next_sortie(void)
{
    murder_aboard();
    sortie++;
    if (sortie >= MAX_SORTIE) {
        if (end_kind == 0U) {
            end_kind = 2;
            end_left = (unsigned char)END_TICKS;
        }
        return;
    }
    init_helicopter();
    init_ents();
    scroll_x = SCROLL_START;
    death_timer = 0;
    crash_fx = 0;
    banner_left = BANNER_TICKS;
    snd_silence();
    /* scroll_x just jumped discontinuously to SCROLL_START, but the
     * per-tick dirty-rect system only knows how to erase what a sprite's
     * own dirty rect says changed -- it has no notion of "the whole scene
     * just changed," so anything drawn at the old scroll position (a
     * burning house, a tank, whatever was on screen) has no dirty rect
     * pointing at it any more and is never scheduled for a restore. It
     * just sits there. A per-tracker reset (scenery_mark/fire-bit state)
     * caught the narrower case docs/M10.md item 5 originally described but
     * not this one -- confirmed still visible by eye (2026-09-10),
     * barracks *and* a tank both surviving into the next sortie.
     *
     * The robust fix is the one run_viewer()'s own setup already uses at
     * game start, before its loop begins: repaint both video pages'
     * background layer from scratch (paint_world does the whole 0-199
     * band -- sky, mountain placeholder, ground -- as unconditional fills,
     * wiping every sprite pixel regardless of what dirty-tracked it),
     * then force everything else to redraw on top of that clean
     * background on the very next present, the same way a fresh game's
     * first tick does. This costs a full-screen fill on both pages once,
     * at a sortie boundary the player is not otherwise interacting with
     * (still inside the death animation / banner pause) -- not a per-tick
     * cost, so not worth trading correctness for here. */
    paint_world(seg_a);
    paint_world(seg_b);
    lists[0].n = lists[1].n = 0;
    scenery_mark[0].n = scenery_mark[1].n = 0;
    scenery_invalidate();          /* forces a full houses/fence/base redraw
                                     * next present, onto the wiped
                                     * background above -- without this,
                                     * only sprites the next tick's own
                                     * dirty list happens to hit would
                                     * redraw, leaving the rest as bare
                                     * ground. */
    last_flag_bit[0] = last_flag_bit[1] = 0xFF;
    last_fire_bit[0] = last_fire_bit[1] = 0xFF;
}

static void sim_tick(void)
{
    snd_did_board = snd_did_kill = snd_did_boom = 0;
    read_controls();
    chopper_physics();
    if (dying && airborne == 0 && crash_fx == 0) {
        spawn_ground_crash();
        crash_fx = 1;
    }
    if (dying && sink_y >= MAX_SINK) {
        if (death_timer == 0)
            death_timer = 1;
    }
    if (death_timer) {
        death_timer = (unsigned char)(death_timer + 6U);
        if ((signed char)death_timer < 0)
            next_sortie();
    }
    if (end_kind == 0U) {
        /* choplifter.s:599-608: win is TOTAL_RESCUES==64; otherwise the
         * game ends (loss) once TOTAL_RESCUES+HOSTAGES_KILLED==64 -- all
         * 64 hostages processed, not 64 killed outright. */
        if (total_rescues >= 64U)
            end_kind = 1;
        else if ((unsigned)total_rescues + (unsigned)hostages_killed >= 64U)
            end_kind = 2;
        if (end_kind != 0U)
            end_left = (unsigned char)END_TICKS;
    }
    if (end_kind != 0U && end_left != 0U) {
        end_left = (unsigned char)(end_left - 1U);
        if (end_left == 0U)
            game_over = 1;
    }
    scroll_terrain();
    if ((sim_frame & 3U) == 0U)
        spawn_hostages();
    spawn_enemies();
    update_hostages();
    update_ents();
    /* Apple draws the new round in updateChopper *before* its first
     * updateBullet.  Physics-before-draw ate a nose-down burst on the
     * spawn tick and slid level tracers 13 px under the fuselage. */
    try_fire();
    snd_tick();
    if (banner_left != 0U)
        banner_left--;
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

/* Half-rate parallax: one screen pixel of ridge per two of camera. */
static unsigned mountain_shift(void)
{
    unsigned period;
    int      i;

    period = 0;
    for (i = 0; i < 4; i++)
        period += mountain_e[i][0];
    if (period == 0U)
        return 0xFFFFU;
    return (scroll_x >> 2) % period;
}

/* Tile the four 4-row CHOPGFX patterns across the 160-px window.  The ridge
 * spans the whole band, so a camera move clears the band wholesale; these
 * blits stay out of the sprite dirty list. */
static void draw_mountains(unsigned seg, unsigned shift)
{
    int x, i, w;
    unsigned n;

    if (shift == 0xFFFFU)
        return;
    ws_kind = WS_K_MTN;
    ws_t_mtn_fill += 4UL * (unsigned long)M8_BYTES_PER_ROW;
    fill_band_m8(seg, MOUNTAIN_ROW, 4, M8_SOLID(8));
    x = -(int)shift;
    i = 0;
    while (x < (int)M8_WIDTH_PX) {
        w = (int)mountain_e[i][0];
        /* blit_mtn_fast (src/asm/blit.asm) skips blit_at's C-side dispatch
         * for the common on-screen case; !opt_forceclip/!opt_forcefire keep
         * both debug flags exercising blit_at's real clip/fire paths the
         * way they always have.  0xFFFF means "needs the slow path" (edge
         * tile, or a flag forced it) -- blit_at still does that, unchanged,
         * so the one clipping implementation is never duplicated. */
        n = 0xFFFFU;
        if (!opt_forceclip && !opt_forcefire)
            n = blit_mtn_fast(seg, x, (unsigned)MOUNTAIN_ROW,
                              (unsigned)mountain_e[i], (unsigned)mountain_o[i]);
        if (n != 0xFFFFU) {
            if (!opt_noworkset)
                ws_add_blit(n);
        } else {
            blit_at(seg, x, (int)MOUNTAIN_ROW, mountain_e[i], mountain_o[i], 0);
        }
        x += w;
        i++;
        if (i >= 4)
            i = 0;
    }
    ws_kind = WS_K_SPRITE;
}

/* Still camera: restore already filled the dirty span.  Retile only tiles
 * that overlap it; do not wipe the whole 160x4 band. */
static void draw_mountains_dirty(unsigned seg, unsigned shift,
                                 const dirty_rect *hit, unsigned nhit)
{
    int x, i, w, sy;
    dirty_bbox bb;

    if (shift == 0xFFFFU || nhit == 0U)
        return;
    ws_kind = WS_K_MTN;
    sy = (int)MOUNTAIN_ROW;
    dirty_bbox_calc(&bb, hit, nhit);
    x = -(int)shift;
    i = 0;
    while (x < (int)M8_WIDTH_PX) {
        w = (int)mountain_e[i][0];
        if (spr_hits_dirty(x, sy, mountain_e[i], hit, nhit, &bb))
            blit_at(seg, x, sy, mountain_e[i], mountain_o[i], 0);
        x += w;
        i++;
        if (i >= 4)
            i = 0;
    }
    ws_kind = WS_K_SPRITE;
}

static void draw_houses(unsigned seg, dirty_list *list, int full,
                        const dirty_rect *hit, unsigned nhit,
                        const dirty_bbox *bb, unsigned pg)
{
    unsigned i;
    unsigned wx;
    int      sx, sy, fy;
    unsigned char fire_bit, fire_changed;
    unsigned char *hs, *hi, *de;

    fire_bit = (unsigned char)((sim_frame / 4U) & 1U);
    fire_changed = (unsigned char)(fire_bit != last_fire_bit[pg]);
    last_fire_bit[pg] = fire_bit;

    for (i = 0; i < N_HOUSES; i++) {
        wx = FARHOUSE_X + i * HOUSE_SPACING;
        sx = world_to_sx(wx);
        sy = world_to_sy(HOUSE_WORLD_Y);
        if (house_states[i]) {
            hs = house_burn_e;
            hi = house_fire_e[fire_bit];
            de = house_debris_e;
            fy = world_to_sy(HOUSE_WORLD_Y + 4U);
            if (full
                || spr_hits_dirty(sx, sy, hs, hit, nhit, bb)
                || spr_hits_dirty(sx + 4, fy, hi, hit, nhit, bb)
                || spr_hits_dirty(world_to_sx(wx + 2U),
                                  world_to_sy(SILL_WORLD_Y), de, hit, nhit, bb)
                || fire_changed) {
                blit_aligned(seg, sx, sy, hs, list);
                blit_aligned(seg, sx + 4, fy, hi, list);
                blit_aligned(seg, world_to_sx(wx + 2U),
                             world_to_sy(SILL_WORLD_Y), de, list);
            }
        } else {
            if (full
                || spr_hits_dirty(sx, sy, house_e, hit, nhit, bb)
                || spr_hits_dirty(world_to_sx(wx), world_to_sy(SILL_WORLD_Y),
                                  house_sill_e, hit, nhit, bb)) {
                blit_world(seg, wx, HOUSE_WORLD_Y, house_e, house_o, list);
                blit_world(seg, wx, SILL_WORLD_Y, house_sill_e, house_sill_o,
                           list);
            }
        }
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

static void draw_fence(unsigned seg, dirty_list *list, int full,
                       const dirty_rect *hit, unsigned nhit,
                       const dirty_bbox *bb)
{
    int t;
    unsigned wx;
    int sx, sy;

    /* Furthest (smallest) first, closest last -- original draw order. */
    for (t = 4; t >= 0; t--) {
        wx = fence_tower_x(t);
        sx = world_to_sx(wx);
        sy = world_to_sy(fence_world_y[t]);
        if (full || spr_hits_dirty(sx, sy, fence_e[t], hit, nhit, bb))
            blit_world(seg, wx, fence_world_y[t], fence_e[t], fence_o[t],
                       list);
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

static int pad_hits_dirty(const dirty_rect *hit, unsigned nhit,
                          const dirty_bbox *bb)
{
    int sx0, sx1, vis0, vis1;

    sx0 = world_to_sx(BASE_X + PAD_DX0);
    sx1 = world_to_sx(BASE_X + PAD_DX1);
    if (sx1 <= 0 || sx0 >= (int)M8_WIDTH_PX)
        return 0;
    vis0 = (sx0 < 0) ? 0 : sx0;
    vis1 = (sx1 > (int)M8_WIDTH_PX) ? (int)M8_WIDTH_PX : sx1;
    return box_hits_dirty(vis0, (int)GROUND_TOP_ROW, vis1 - vis0,
                          (int)PAD_ROWS, hit, nhit, bb);
}

static void draw_base(unsigned seg, unsigned frame, dirty_list *list,
                      int full, const dirty_rect *hit, unsigned nhit,
                      const dirty_bbox *bb, unsigned pg)
{
    unsigned bx = BASE_X + 0x49U;
    unsigned px = BASE_X + 0x71U;
    unsigned char flag_bit, flag_changed;
    unsigned char *flag;

    if (full || pad_hits_dirty(hit, nhit, bb))
        draw_pad(seg, list);

    if (full
        || spr_hits_dirty(world_to_sx(bx), world_to_sy(BASE_BUILD_Y),
                          base_building_e, hit, nhit, bb))
        blit_world(seg, bx, BASE_BUILD_Y, base_building_e, base_building_o,
                   list);
    if (full
        || spr_hits_dirty(world_to_sx(px), world_to_sy(FLAGPOLE_Y),
                          base_flagpole_e, hit, nhit, bb))
        blit_world(seg, px, FLAGPOLE_Y, base_flagpole_e, base_flagpole_o,
                   list);

    flag_bit = (unsigned char)((frame / 8U) & 1U);
    flag_changed = (unsigned char)(flag_bit != last_flag_bit[pg]);
    last_flag_bit[pg] = flag_bit;
    flag = flag_bit ? base_flag_01_e : base_flag_00_e;
    if (full || flag_changed
        || spr_hits_dirty(world_to_sx(px + 2U), world_to_sy(FLAGPOLE_Y),
                          flag, hit, nhit, bb)) {
        if (flag_bit)
            blit_world(seg, px + 2U, FLAGPOLE_Y, base_flag_01_e,
                       base_flag_01_o, list);
        else
            blit_world(seg, px + 2U, FLAGPOLE_Y, base_flag_00_e,
                       base_flag_00_o, list);
    }
}

/* Mountains are handled by the caller.  list is the scenery mark (full
 * redraw) or 0 (static hole-fill; do not dirty_add). */
static void draw_scenery(unsigned seg, unsigned frame, dirty_list *list,
                         int full, const dirty_rect *hit, unsigned nhit,
                         unsigned pg)
{
    dirty_bbox bb;
    const dirty_bbox *bbp;

    ws_kind = WS_K_SCENERY;
    bbp = 0;
    if (!full && hit != 0 && nhit != 0U) {
        dirty_bbox_calc(&bb, hit, nhit);
        bbp = &bb;
    }
    draw_houses(seg, list, full, hit, nhit, bbp, pg);
    draw_fence(seg, list, full, hit, nhit, bbp);
    draw_base(seg, frame, list, full, hit, nhit, bbp, pg);
    ws_kind = WS_K_SPRITE;
}

/*
 * Apple renderTiltedSprite shears the 1-px main rotor with spriteTiltTable
 * (choplifter.s:1399).  The body here is already one of 11 side frames, so
 * the slope follows that silhouette (pitch 0 = high-left, 10 = high-right),
 * pivoted on the cabin (body centre plus ROTOR_FORWARD toward the
 * nose).  Head-on stays level: the disc faces the camera.
 *
 * Rotor frames (22 px ink): 0 left-ish bar, 1 right-ish, 2 full disc.
 * Sheared copies are pre-encoded RLE (sprdata_rle.c); blit_at takes the
 * cropped sprite plus hub-relative origin.
 */
static void draw_main_rotor(unsigned seg, dirty_list *list,
                            int hub_x, int hub_y, int tilt_i,
                            int flip, unsigned frame)
{
    unsigned char *e;
    unsigned char *o;
    int            ox, oy;

    if (frame > 2U)
        frame = 2U;
    if (tilt_i < 0)
        tilt_i = 0;
    if (tilt_i > 10)
        tilt_i = 10;
    if (flip) {
        e  = rotor_tilt_fe[frame][tilt_i];
        o  = rotor_tilt_fo[frame][tilt_i];
        ox = (int)rotor_tilt_fox[frame][tilt_i];
        oy = (int)rotor_tilt_foy[frame][tilt_i];
    } else {
        e  = rotor_tilt_e[frame][tilt_i];
        o  = rotor_tilt_o[frame][tilt_i];
        ox = (int)rotor_tilt_ox[frame][tilt_i];
        oy = (int)rotor_tilt_oy[frame][tilt_i];
    }
    blit_at(seg, hub_x + ox, hub_y + oy, e, o, list);
}

static void draw_chopper(unsigned seg, dirty_list *list)
{
    int            body_x, body_y, h, bw, tdx, hub_x, hub_y, mast;
    unsigned       main_i, tail_i;
    unsigned char *be;
    unsigned char *oe;
    unsigned char *bfe;
    unsigned char *bfo;
    int            side;
    int            abs_t;
    int            pitch;
    int            flip;

    if (dying)
        return;

    abs_t = turn_state;
    if (abs_t < 0)
        abs_t = -abs_t;

    pitch = accelx;
    if (turn_state < 0)
        pitch = -pitch;
    pitch = s8_clamp(pitch + 5, 0, 10);

    h = 18;
    if (abs_t == 5) {
        be  = chopper_side_e[pitch];
        oe  = chopper_side_o[pitch];
        bfe = chopper_side_fe[pitch];
        bfo = chopper_side_fo[pitch];
        h   = (int)be[1];
        side = 1;
    } else {
        /* chopper_head_fe/fo (tools/build_sprites.py) mirror the same way
         * chopper_side_fe/fo already did -- until this fix, every
         * mid-turn frame here drew the same un-mirrored body regardless of
         * turn direction, visible on the frame closest to a full turn
         * where the art's own asymmetry is strongest. The original does
         * this with a real-time signed shear (choplifter.s,
         * chooseChopperSprite/jumpSetSpriteTilt) on one bitmap; this port
         * pre-renders, so it needs the baked mirror instead. */
        unsigned hi = (unsigned)abs_t;
        if (hi > 4U)
            hi = 4U;
        be  = chopper_head_e[hi];
        oe  = chopper_head_o[hi];
        bfe = chopper_head_fe[hi];
        bfo = chopper_head_fo[hi];
        h   = (int)be[1];
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
    flip = (turn_state < 0) ? 1 : 0;
    bw = (int)be[0];

    if (flip)
        blit_at(seg, body_x, body_y, bfe, bfo, list);
    else
        blit_at(seg, body_x, body_y, be, oe, list);

    hub_y = body_y + ROTOR_DY;
    if (side) {
        /* Cabin centre, then a few pixels toward the nose so the disc
         * sits on the fuselage rather than the tail stub. */
        mast = bw / 2 + ROTOR_FORWARD;
        if (mast > bw - 1)
            mast = bw - 1;
        if (flip)
            mast = bw - 1 - mast;
        hub_x = body_x + mast;
        draw_main_rotor(seg, list, hub_x, hub_y, pitch, flip, main_i);
        tdx = TAIL_DX;
        if (turn_state < 0)
            tdx = bw - (int)tail_rotor_e[tail_i][0] - TAIL_DX;
        blit_at(seg, body_x + tdx, body_y + TAIL_DY,
                tail_rotor_e[tail_i], tail_rotor_o[tail_i], list);
    } else {
        hub_x = body_x + bw / 2;
        draw_main_rotor(seg, list, hub_x, hub_y, 5, 0, main_i);
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

/* Six red pixels on the lower 6x11 die sprite (palette index 4). */
static void draw_crush_blood(unsigned seg, int sx, int sy)
{
    static const unsigned char ox[6] = { 2, 3, 1, 4, 2, 5 };
    static const unsigned char oy[6] = { 7, 8, 9, 9, 10, 8 };
    unsigned i;
    int      x, y;

    for (i = 0; i < 6U; i++) {
        x = sx + (int)ox[i];
        y = sy + (int)oy[i];
        if (x < 0 || y < (int)HUD_ROWS)
            continue;
        plot_px(seg, (unsigned)x, (unsigned)y, 4);
    }
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
        sx = world_to_sx(hostage_x[i] - 4U);
        if (act == HST_DIE) {
            blit_aligned(seg, sx, sy, hostage_die_e, list);
            draw_crush_blood(seg, sx, sy);
            continue;
        }
        if (act == 0U) {
            fi = (unsigned char)wave_sprite(anim);
            ee = flip ? hostage_wave_fe[fi] : hostage_wave_e[fi];
            oo = flip ? hostage_wave_fo[fi] : hostage_wave_o[fi];
        } else if (act & 1U) {
            fi = (unsigned char)(anim & 3U);
            ee = flip ? hostage_run_fe[fi] : hostage_run_e[fi];
            oo = flip ? hostage_run_fo[fi] : hostage_run_o[fi];
        } else {
            fi = (unsigned char)((anim & 2U) ? 1U : 0U);
            ee = flip ? hostage_load_fe[fi] : hostage_load_e[fi];
            oo = flip ? hostage_load_fo[fi] : hostage_load_o[fi];
        }
        blit_at(seg, sx, sy, ee, oo, list);
    }
}

/* Sprite dirty lists, plus last full scenery footprints (scroll restore). */
static dirty_list lists[2];
static dirty_list scenery_mark[2];

/* Achieved rate, so the sim Hz reported after a sortie is measured rather
 * than assumed.  BIOS tick at 0040:006C runs at 18.2065 Hz. */
static unsigned long run_bios_ticks;
static unsigned      run_sim_ticks;
static unsigned      run_retraces;

/* Phase breakdown, BIOS ticks summed over the whole run (18.2065 Hz, so a
 * single sim_tick() or present() is usually well under one tick -- these are
 * only meaningful as sums across every sim tick in the run, the same way
 * run_bios_ticks/run_sim_ticks already are).  Added because /ztimer keeps
 * overflowing its ~54 ms window on the present span, so there has never been
 * a real number for how that time splits between sim_tick() (physics, AI,
 * joystick, sound), present (restore + draw + HUD) and the two hardware
 * retrace waits: the one after a sim tick's own present, and the two idle
 * ticks between sim ticks (SIM_DIV).  ph_sim + ph_present + ph_flip +
 * ph_idle should sum to ~run_bios_ticks; if it does not, something in the
 * loop is spending time outside all four brackets. */
static unsigned long ph_sim, ph_present, ph_flip, ph_idle;
/* present's own five natural sub-phases; should sum to ~ph_present. */
static unsigned long ph_restore, ph_mountain, ph_scenery, ph_sprites, ph_hud;

/* Combat-window phase totals: the same eight counters above, but only for
 * sim ticks classified as "combat" (see combat_tick_p below).  Every
 * /phases capture so far has been the static empty pad -- these exist so a
 * real firefight (tank + hostages + chopper on screen) can be measured
 * separately from an otherwise-quiet flight, instead of one run-wide
 * average diluting whatever the combat cost actually is. */
static unsigned long cph_sim, cph_present, cph_flip, cph_idle;
static unsigned long cph_restore, cph_mountain, cph_scenery, cph_sprites,
                      cph_hud;
static unsigned long combat_ticks;

static unsigned long bios_ticks(void)
{
    unsigned lo, hi, lo2;

    do {
        lo  = peek_word(0x0040U, 0x006CU);
        hi  = peek_word(0x0040U, 0x006EU);
        lo2 = peek_word(0x0040U, 0x006CU);
    } while (lo != lo2);
    return ((unsigned long)hi << 16) | (unsigned long)lo;
}

static int wait_title_hold(void)
{
    unsigned i;

    while (kbd_is_down(SCAN_SPACE) || kbd_is_down(SCAN_ESC))
        vid_wait_retrace();
    for (i = 0; i < TITLE_HOLD; i++) {
        vid_wait_retrace();
        if (kbd_is_down(SCAN_ESC))
            return 0;
        if (kbd_is_down(SCAN_SPACE)) {
            while (kbd_is_down(SCAN_SPACE))
                vid_wait_retrace();
            return 1;
        }
    }
    return 1;
}

static void paint_logo_screen(unsigned seg, unsigned char *spr, int y)
{
    paint_world(seg);
    fill_band_m8(seg, 0, HUD_ROWS, M8_SOLID(0));
    blit_centered(seg, spr, y, 0);
}

static int show_one_logo(unsigned char *spr, int y)
{
    paint_logo_screen(seg_a, spr, y);
    paint_logo_screen(seg_b, spr, y);
    crt_page = page_a;
    set_pages(page_a, page_b);
    return wait_title_hold();
}

/* Keep the 1.0-scale midline so 1.5x title text does not jump. */
static int title_y_keep_mid(unsigned char *spr, int old_y, int old_h)
{
    int h, y;

    h = (int)spr[1];
    y = old_y + old_h / 2 - h / 2;
    if (y < (int)HUD_ROWS)
        y = (int)HUD_ROWS;
    if (y + h > (int)M8_HEIGHT_PX)
        y = (int)M8_HEIGHT_PX - h;
    return y;
}

/* Broderbund, Choplifter logo, Dan Gorlin, mission.  SPACE skips one
 * screen; Esc quits.  No attract loop (DESIGN.md section 15).  The
 * Choplifter wordmark stays at the original Y; the other three are
 * 1.5x Apple art recentred on their old midlines. */
static int show_presentation(void)
{
    if (!show_one_logo(title_broderbund,
                       title_y_keep_mid(title_broderbund, 59, 11)))
        return 0;
    if (!show_one_logo(title_logo, 94))
        return 0;
    if (!show_one_logo(title_gorlin,
                       title_y_keep_mid(title_gorlin, 131, 17)))
        return 0;
    if (!show_one_logo(title_mission,
                       title_y_keep_mid(title_mission, 89, 27)))
        return 0;
    return 1;
}

/* Approximate "chopper, tank and hostages on screen together": at least
 * one live tank plus at least one spawned (not yet boarded/rescued/HST_FREE)
 * hostage.  This is existence, not a screen-rectangle overlap check -- the
 * chopper is the player and always present, and in practice a live tank and
 * a spawned hostage are usually visible together or a few chopper-lengths
 * apart, so it is a reasonable proxy for "a firefight is happening" without
 * the cost or complexity of real on-screen bounds checks every tick. */
static int combat_tick_p(void)
{
    unsigned h;

    if (num_tanks == 0U)
        return 0;
    for (h = 0; h < MAX_HOSTAGES; h++)
        if (hostage_anim[h] != HST_FREE)
            return 1;
    return 0;
}

static void run_viewer(void)
{
    unsigned long t0;
    unsigned   last_shift[2];
    int        back;
    unsigned   back_seg;
    unsigned   i;
    unsigned   limit;
    int        in_combat = 0;  /* combat_tick_p() as of the last sim tick;
                                 * also read on the two idle retraces
                                 * between sim ticks, so cph_idle reflects
                                 * the state those retraces were waiting on. */

    paint_world(seg_a);
    paint_world(seg_b);
    snd_init();
    snd_play(SND_START);

    lists[0].n = 0;
    lists[1].n = 0;
    scenery_mark[0].n = 0;
    scenery_mark[1].n = 0;
    scenery_invalidate();
    last_flag_bit[0] = last_flag_bit[1] = 0xFF;
    last_fire_bit[0] = last_fire_bit[1] = 0xFF;
    last_shift[0] = 0xFFFEU;
    last_shift[1] = 0xFFFEU;
    hud_pg_mode[0] = hud_pg_mode[1] = 0;
    scroll_x = SCROLL_START;
    init_helicopter();
    init_hostages();
    banner_left = BANNER_TICKS;
    hud_prev_tog = 0;
    dbg_prev_p = 1;             /* ignore P held through SPACE */
    hud_on = 0;
    dbg_xmin = dbg_ymin = 0xFFFFU;
    dbg_xmax = dbg_ymax = 0;
    dbg_x = dbg_y = 0;
    sim_frame = 0;
    back = 0;
    ws_reset();
    ph_sim = ph_present = ph_flip = ph_idle = 0UL;
    ph_restore = ph_mountain = ph_scenery = ph_sprites = ph_hud = 0UL;
    cph_sim = cph_present = cph_flip = cph_idle = 0UL;
    cph_restore = cph_mountain = cph_scenery = cph_sprites = cph_hud = 0UL;
    combat_ticks = 0UL;

    crt_page = page_b;
    set_pages(page_b, page_a);

    limit = opt_frames;
    if (limit == 0U)
        limit = 0xFFFFU;

    t0 = bios_ticks();

    /* i counts retraces, so /frames keeps its meaning.  Every pixel we draw
     * is a function of simulation state, so the two retraces between ticks
     * would redraw an identical scene into the other page: three times the
     * video traffic for one visible update.  Draw on the tick only. */
    for (i = 0; i < limit; i++) {
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

        if ((i % SIM_DIV) == 0U) {
            unsigned shift;
            unsigned prev_scroll;
            unsigned old_n;
            unsigned old_sc;
            int      band_hit;
            int      scrolled;
            int      full_sc;
            int      do_z;
            unsigned long tp0, tp1, t_simend;

            back_seg = back ? seg_b : seg_a;
            prev_scroll = scroll_x;
            if (opt_phases)
                tp0 = bios_ticks();
            sim_tick();
            in_combat = opt_phases && combat_tick_p();
            if (opt_phases) {
                tp1 = bios_ticks();
                ph_sim += tp1 - tp0;
                if (in_combat) {
                    cph_sim += tp1 - tp0;
                    combat_ticks++;
                }
                t_simend = tp1;         /* present's span starts here; tp0/
                                         * tp1 are reused below as rolling
                                         * checkpoints for the sub-phases. */
            }
            scrolled = (scroll_x != prev_scroll);
            ws_begin_tick();

            /* Sample after both pages have a dirty list.  Wait retrace first
             * so the Zen window sees the same VGA phase as M1.  Do not touch
             * the keyboard: PCjr scan is NMI and CLI cannot hold it off. */
            do_z = (opt_ztimer != 0U && ws_z_did == 0U
                    && sim_frame == opt_ztimer);
            if (do_z)
                vid_wait_retrace();
            if (do_z)
                ztimer_on();

            old_n = lists[back].n;
            old_sc = scenery_mark[back].n;
            band_hit = restore_list(back_seg, &lists[back]);
            draw_stars_and_moon(back_seg, lists[back].r, old_n);

            full_sc = scrolled || scenery_full[back];
            if (full_sc)
                band_hit |= restore_list(back_seg, &scenery_mark[back]);

            if (opt_phases) {
                tp0 = bios_ticks();
                ph_restore += tp0 - tp1;
                if (in_combat)
                    cph_restore += tp0 - tp1;
            }

            /* Static camera and nothing erased over the ridge: the band on
             * this page is already right.  A camera move that changes
             * parallax still retile-fills the whole strip. */
            shift = mountain_shift();
            if (shift != last_shift[back]) {
                draw_mountains(back_seg, shift);
                last_shift[back] = shift;
            } else if (band_hit) {
                if (old_n != 0U)
                    draw_mountains_dirty(back_seg, shift, lists[back].r,
                                         old_n);
                if (full_sc && old_sc != 0U)
                    draw_mountains_dirty(back_seg, shift,
                                         scenery_mark[back].r, old_sc);
            }

            if (opt_phases) {
                tp1 = bios_ticks();
                ph_mountain += tp1 - tp0;
                if (in_combat)
                    cph_mountain += tp1 - tp0;
            }

            if (full_sc) {
                draw_scenery(back_seg, sim_frame, &scenery_mark[back], 1,
                             0, 0U, (unsigned)back);
                scenery_full[back] = 0;
            } else {
                /* full=0 so this only draws what hit-tests dirty or has an
                 * animation-state change (fire_changed / flag_changed) --
                 * but list must still be real, not 0: a redraw here isn't
                 * preceded by a restore (that only happens on a full_sc
                 * pass), so an animated sprite whose frames don't share the
                 * same opaque silhouette (house_fire_00_e vs _01_e, e.g.)
                 * can leave a stale pixel where the old frame was opaque
                 * and the new one is transparent -- RLE transparency means
                 * "don't touch dest", so that pixel is never overwritten.
                 * Tracking the footprint here (in scenery_mark, not lists,
                 * so this can never alias the hit list being iterated over)
                 * means the next full_sc pass's restore_list will erase it
                 * before redrawing, closing that gap. */
                draw_scenery(back_seg, sim_frame, &scenery_mark[back], 0,
                             lists[back].r, old_n, (unsigned)back);
            }

            if (opt_phases) {
                tp0 = bios_ticks();
                ph_scenery += tp0 - tp1;
                if (in_combat)
                    cph_scenery += tp0 - tp1;
            }

            draw_ents(back_seg, &lists[back]);
            draw_chopper(back_seg, &lists[back]);
            draw_hostages(back_seg, &lists[back]);
            draw_sortie_banner(back_seg, &lists[back]);
            draw_end_banner(back_seg, &lists[back]);

            if (opt_phases) {
                tp1 = bios_ticks();
                ph_sprites += tp1 - tp0;
                if (in_combat)
                    cph_sprites += tp1 - tp0;
            }

            if (do_z) {
                ztimer_off();
                ws_z_did = 1;
                ws_z_ovf = ztimer_overflow();
                ws_z_ticks = ztimer_count();
                ws_z_frame = sim_frame;
                ws_z_restore = ws_t_restore;
                ws_z_blit = ws_t_mtn_blit + ws_t_scenery + ws_t_sprite;
                ws_z_video = ws_t_restore + ws_t_mtn_fill + ws_z_blit;
            }

            draw_hud(back_seg, back);
            ws_end_tick(scrolled);

            if (opt_phases) {
                tp0 = bios_ticks();
                ph_hud += tp0 - tp1;
                ph_present += tp0 - t_simend;  /* cross-check: should equal
                                                 * ph_restore + ph_mountain +
                                                 * ph_scenery + ph_sprites +
                                                 * ph_hud. */
                if (in_combat) {
                    cph_hud += tp0 - tp1;
                    cph_present += tp0 - t_simend;
                }
            }

            vid_wait_retrace();
            crt_page = back ? page_b : page_a;
            set_pages(crt_page, back ? page_a : page_b);

            if (opt_phases) {
                tp1 = bios_ticks();
                ph_flip += tp1 - tp0;
                if (in_combat)
                    cph_flip += tp1 - tp0;
            }

            back = !back;
        } else {
            unsigned long ti0;

            if (opt_phases)
                ti0 = bios_ticks();
            vid_wait_retrace();
            if (opt_phases) {
                unsigned long dt = bios_ticks() - ti0;

                ph_idle += dt;
                if (in_combat)
                    cph_idle += dt;
            }
        }

        if (kbd_is_down(SCAN_ESC)) {
            viewer_quit = 1;
            break;
        }
        if (game_over)
            break;
    }

    snd_silence();
    run_bios_ticks = bios_ticks() - t0;
    run_sim_ticks  = sim_frame;
    run_retraces   = i;
}

static void usage(void)
{
    printf(
"Choplifter! PCjr -- M10 polish.\n"
"\n"
"  M10 [options]\n"
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
"  /ztimer      Zen-timer one present at sim_frame 4 (pad, dirty list live)\n"
"  /ztimer=N    same, at sim_frame N.  Interrupts off; do not touch keys.\n"
"  /forceclip   debug: every RLE blit takes blit_rle_clip, not blit_rle_m8\n"
"  /forcefire   debug: every in-bounds blit takes blit_rle_m8_fire\n"
"  /noworkset   debug: skip WORKSET/ZTIMER byte counting (prints as 0)\n"
"  /phases      debug: bios_ticks() phase breakdown, whole-run and again\n"
"               for tank+hostage ticks.  Off by default: real cost else.\n"
"  /?           this\n"
"\n"
"Stick: Paku Paku 1.6a port 201h loop (CLI, bits high, timeout 7FFFh).\n"
"Joystick 1 only (first connector / Paku stick A, bits 0,1).  Analog\n"
"mapped to the original Apple paddle tables.  Each side of rest is\n"
"scaled to the full 0-255 Apple range (raw 0 = 0, timeout = 255)\n"
"before a +/-16 deadzone around 128.  The X table then has a one-nibble\n"
"centre bucket.  Buttons are stick 1 only (port 201h bits 4-5).\n"
"\n"
"Title: J/K pick Joystick/Keyboard, C calibrates, SPACE flies, Esc quits.\n"
"Keyboard: WASD/arrows, '.' rotate, '/' fire.  Joystick: analog plus\n"
"WASD/arrows, button 0 fire, button 1/'.'/Alt/LShift rotate.  Ctrl-A/V\n"
"invert axes, Ctrl-S mutes.  docs/M5.md and docs/M10.md cover the rest\n"
"(HUD layout, sortie flow, win/lose).\n");
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
        else if (strcmp(a, "ztimer") == 0)
            opt_ztimer = ZTIMER_DEFAULT;
        else if (strncmp(a, "ztimer=", 7) == 0)
            opt_ztimer = (unsigned)atoi(a + 7);
        else if (strcmp(a, "forceclip") == 0)
            opt_forceclip = 1;
        else if (strcmp(a, "forcefire") == 0)
            opt_forcefire = 1;
        else if (strcmp(a, "noworkset") == 0)
            opt_noworkset = 1;
        else if (strcmp(a, "phases") == 0)
            opt_phases = 1;
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

/* Shared by the whole-run and combat-window /phases tables below: printing
 * the same five lines twice from two literal printf call sites would double
 * their format-string CONST cost for no reason (CLAUDE-THOUGHTS.md already
 * paid for that lesson once with verbose diagnostic text). */
static void print_phase_table(unsigned long ps, unsigned long pp,
                              unsigned long pf, unsigned long pi,
                              unsigned long pr, unsigned long pm,
                              unsigned long psc, unsigned long psp,
                              unsigned long ph)
{
    printf("  sim_tick %5lu (%5lu ms)   present %5lu (%5lu ms)\n",
           ps, bios_ticks_to_ms(ps), pp, bios_ticks_to_ms(pp));
    printf("  flip     %5lu (%5lu ms)   idle    %5lu (%5lu ms)\n",
           pf, bios_ticks_to_ms(pf), pi, bios_ticks_to_ms(pi));
    printf("  restore  %5lu (%5lu ms)   mountain %5lu (%5lu ms)\n",
           pr, bios_ticks_to_ms(pr), pm, bios_ticks_to_ms(pm));
    printf("  scenery  %5lu (%5lu ms)   sprites  %5lu (%5lu ms)\n",
           psc, bios_ticks_to_ms(psc), psp, bios_ticks_to_ms(psp));
    printf("  hud      %5lu (%5lu ms)   sub-sum  %5lu (present %5lu)\n",
           ph, bios_ticks_to_ms(ph), pr + pm + psc + psp + ph, pp);
}

int main(int argc, char **argv)
{
    unsigned char model;
    int           flew;

    setbuf(stdout, NULL);

    if (!parse_args(argc, argv))
        return 1;

    printf("Choplifter! PCjr -- M10 polish\n");
    printf("hardware validation (workset bytes, /ztimer)  ground %s\n\n",
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

    flew = 0;
    viewer_quit = 0;
    for (;;) {
        if (opt_wait) {
            if (!run_title()) {
                vid_set_mode(orig_mode);
                mode_changed = 0;
                kbd_unhook();
                kbd_hooked = 0;
                if (!flew)
                    printf("Quit from title (Esc).  Did not fly.\n");
                else
                    printf("Quit from title (Esc).\n");
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

        if (opt_wait) {
            if (!show_presentation()) {
                viewer_quit = 1;
                break;
            }
        }

        run_viewer();
        flew = 1;
        if (!opt_wait || viewer_quit)
            break;
    }

    vid_set_mode(orig_mode);
    mode_changed = 0;
    kbd_unhook();
    kbd_hooked = 0;

    if (!flew) {
        printf("Quit from logos (Esc).  Did not fly.\n");
        cleanup();
        return 0;
    }

    printf("\nFlew with blit_rle_m8 + SN76496 + M9 presentation (M10 polish) on pages %d/%d.  Sim 20 Hz (every %u "
           "retraces).  Controls: %s.  Stick source this run: %s",
           page_a, page_b, SIM_DIV,
           play_keyboard ? "Keyboard" : "Joystick",
           stick_source == 1 ? "port 201h" :
           stick_source == 2 ? "INT 15h" : "keys only");
    printf(".\n");
    if (run_bios_ticks != 0UL) {
        unsigned long hz100;

        /* 18.2065 Hz BIOS tick; x100 to keep it in integers. */
        hz100 = (unsigned long)run_sim_ticks * 1821UL / run_bios_ticks;
        printf("Measured: %u sim ticks / %u retraces in %lu BIOS ticks"
               " = %lu.%02lu Hz sim (target 20.00).\n",
               run_sim_ticks, run_retraces, run_bios_ticks,
               hz100 / 100UL, hz100 % 100UL);
    }
    /* BIOS ticks (18.2065 Hz), summed over the whole run -- a single sim
     * tick is far finer than this counter, only the sum means anything,
     * same caveat as the Measured line above.  sim_tick = physics/AI/
     * joystick/sound.  present = restore+draw+HUD, the span /ztimer samples
     * once and keeps overflowing.  flip = the sim tick's own retrace wait +
     * page flip.  idle = the two non-sim retraces per SIM_DIV, hardware-
     * timed dead time, not CPU cost.  present's own five sub-phases should
     * sum to ~present: restore = restore_list + stars/moon, mountain =
     * parallax retile, scenery = houses/fence/base, sprites = entities +
     * chopper + hostages + banners, hud = draw_hud (cached: near 0 once
     * both pages have drawn once, unless a counter changed this run). */
    if (opt_phases) {
        unsigned long ph_sum = ph_sim + ph_present + ph_flip + ph_idle;

        printf("\nPHASE ticks (BIOS, sum over run; ms approx):\n");
        print_phase_table(ph_sim, ph_present, ph_flip, ph_idle,
                          ph_restore, ph_mountain, ph_scenery, ph_sprites,
                          ph_hud);
        printf("  sum %lu vs run_bios_ticks %lu\n", ph_sum, run_bios_ticks);

        /* Same counters, only for sim ticks a tank and a spawned hostage
         * both existed (combat_tick_p) -- does a firefight's cost shape
         * differ from the whole-run average above? combat_ticks counts sim
         * ticks, the sample size behind this table, not BIOS ticks. */
        if (combat_ticks != 0UL) {
            unsigned long cph_sum = cph_sim + cph_present + cph_flip +
                                     cph_idle;

            printf("\nCOMBAT ticks (%lu/%u sim ticks, tank+hostage on "
                   "screen):\n", combat_ticks, run_sim_ticks);
            print_phase_table(cph_sim, cph_present, cph_flip, cph_idle,
                              cph_restore, cph_mountain, cph_scenery,
                              cph_sprites, cph_hud);
            printf("  sum %lu\n", cph_sum);
        } else {
            printf("\nCOMBAT ticks: none this run.\n");
        }
    }
    print_workset();
    if (opt_stick) {
        printf("/stick 1 x min/max/now %u/%u/%u  y %u/%u/%u\n",
               dbg_xmin, dbg_xmax, dbg_x,
               dbg_ymin, dbg_ymax, dbg_y);
        /* Rest and recorded throw are what the 0-255 map is built from.
         * If a throw end reads 0/65535 the calibrate screen never saw that
         * direction and the axis fell back to mirroring rest. */
        printf("/stick 1 rest %u,%u  throw x %u..%u  y %u..%u"
               "  (timeout %u)\n",
               stick_cx0, stick_cy0, throw_xmin, throw_xmax,
               throw_ymin, throw_ymax, (unsigned)STICK_LIVE_MAX);
    }
    printf("PASS by eye: BIOS title, then Broderbund / logo / Gorlin /\n"
           "mission screens (SPACE skips), then chopper on the base pad.\n"
           "HUD rows 0-7: three 24x8 bubbles, two 5x7 digits each.  F1 or `\n"
           "toggles debug.  FIRST/SECOND/THIRD SORTIE art at sortie start.\n"
           "64 rescued: crown overlay then title.  64 killed or third crash:\n"
           "The End then title.  Esc on play or logos quits to DOS.  No\n"
           "full-screen copy in the sim loop, no trail, no flicker.\n"
           "WORKSET byte counts print after every flight.  On hardware add\n"
           "/ztimer /batch /frames=60 and do not touch the keyboard during\n"
           "the sample.  Stick 1 analog and SN76496 (port 61h gate) are\n"
           "the rest of the hardware pass; see docs/M10.md.\n");
    cleanup();
    return 0;
}
