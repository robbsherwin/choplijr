/* snd.c -- SN76496 effect scheduler with voice stealing (M8).
 *
 * Writes: out 0C0h.  Gate: port 61h | 60h.  No software delay between
 * outs (chip READY ~42 wait states).  See DESIGN.md section 9. */

#include "snd.h"

static void snd_psg(unsigned char v);
#pragma aux snd_psg = \
    "out 0C0h, al" \
    parm [al];

static unsigned char snd_ppi_in(void);
#pragma aux snd_ppi_in = \
    "in al, 61h" \
    value [al];

static void snd_ppi_out(unsigned char v);
#pragma aux snd_ppi_out = \
    "out 61h, al" \
    parm [al];

static unsigned char snd_on = 0xFF;     /* PREFS_SOUND: $FF on, $00 mute */
static unsigned char voice_next;
static unsigned char voice_owner[3];    /* 0 = free; else SND_* id */
static const unsigned char *voice_pat[3];
static unsigned char voice_off[3];
static unsigned char voice_ticks[3];
static unsigned char noise_owner;
static const unsigned char *noise_pat;
static unsigned char noise_off;
static unsigned char noise_ticks;
static unsigned char death_att;         /* 0-15 playing, 16 idle */

/* Pattern: repeating {n_lo, n_hi, atten, ticks}; ticks==0 ends.
 * n = 0xFFFF is a rest (atten 15, period unchanged). */

static const unsigned char pat_fire[] = {
    0xC0, 0x00, 0x00, 2,        /* X=$30 -> N=192 */
    0xC4, 0x00, 0x04, 2,        /* A=$01 sweep */
    0, 0, 0, 0
};
static const unsigned char pat_board[] = {
    0xFC, 0x03, 0x00, 2,        /* X=$FF */
    0x80, 0x03, 0x00, 2,        /* X=$E0 */
    0x60, 0x02, 0x00, 3,        /* X=$98 */
    0, 0, 0, 0
};
static const unsigned char pat_rescue[] = {
    0xA0, 0x00, 0x00, 3,        /* X=$28 */
    0, 0, 0, 0
};
static const unsigned char pat_kill[] = {
    0xC0, 0x00, 0x00, 3,        /* X=$30 */
    0x0C, 0x01, 0x00, 3,        /* X=$43 */
    0x84, 0x01, 0x00, 3,        /* X=$61 */
    0, 0, 0, 0
};
static const unsigned char pat_kill_many[] = {
    0x00, 0x01, 0x00, 3,        /* X=$40 */
    0x50, 0x01, 0x00, 3,        /* X=$54 */
    0x84, 0x01, 0x00, 2,        /* X=$61 */
    0x0C, 0x02, 0x00, 2,        /* X=$83 */
    0, 0, 0, 0
};
/* New-game medley: three phrases of (count<<5)+$30, then $43, $61. */
static const unsigned char pat_start[] = {
    0x40, 0x02, 0x00, 2,        /* X=$90 */
    0x0C, 0x01, 0x00, 2,
    0x84, 0x01, 0x00, 2,
    0xC0, 0x01, 0x00, 2,        /* X=$70 */
    0x0C, 0x01, 0x00, 2,
    0x84, 0x01, 0x00, 2,
    0x40, 0x01, 0x00, 2,        /* X=$50 */
    0x0C, 0x01, 0x00, 2,
    0x84, 0x01, 0x00, 3,
    0, 0, 0, 0
};
static const unsigned char pat_alien[] = {
    0x04, 0x00, 0x02, 1,        /* short crackle */
    0, 0, 0, 0
};
/* Noise patterns: {ctrl, unused, atten, ticks}; ticks==0 ends.
 * ctrl is the 4-bit noise latch nibble (FB + NF). */
static const unsigned char pat_shell[] = {
    0x06, 0x00, 0x00, 2,        /* white /2048 */
    0x06, 0x00, 0x08, 2,
    0, 0, 0, 0
};
static const unsigned char pat_missile[] = {
    0x04, 0x00, 0x00, 2,        /* white /512 */
    0x04, 0x00, 0x06, 2,
    0, 0, 0, 0
};
static const unsigned char pat_boom[] = {
    0x04, 0x00, 0x00, 2,
    0x04, 0x00, 0x05, 2,
    0x04, 0x00, 0x0A, 2,
    0, 0, 0, 0
};
static const unsigned char pat_house[] = {
    0x05, 0x00, 0x02, 2,
    0x05, 0x00, 0x08, 2,
    0, 0, 0, 0
};

static void snd_gate_on(void)
{
    snd_ppi_out((unsigned char)(snd_ppi_in() | 0x60U));
}

static void snd_tone(unsigned char ch, unsigned n, unsigned char att)
{
    unsigned char rr;

    if (n == 0U)
        n = 1U;
    if (n > 1023U)
        n = 1023U;
    rr = (unsigned char)(ch << 5);
    snd_psg((unsigned char)(0x80 | rr | (n & 0x0FU)));
    snd_psg((unsigned char)((n >> 4) & 0x3FU));
    snd_psg((unsigned char)(0x90 | rr | (att & 0x0FU)));
}

static void snd_tone_off(unsigned char ch)
{
    snd_psg((unsigned char)(0x90 | (ch << 5) | 0x0FU));
}

static void snd_noise(unsigned char ctrl, unsigned char att)
{
    snd_psg((unsigned char)(0xE0 | (ctrl & 0x0FU)));
    snd_psg((unsigned char)(0xF0 | (att & 0x0FU)));
}

static void snd_noise_off(void)
{
    snd_psg(0xFF);
}

static void voice_clear(unsigned char v)
{
    voice_owner[v] = 0;
    voice_pat[v] = 0;
    voice_ticks[v] = 0;
    snd_tone_off(v);
}

static void noise_clear(void)
{
    noise_owner = 0;
    noise_pat = 0;
    noise_ticks = 0;
    snd_noise_off();
}

static void load_tone_step(unsigned char v)
{
    const unsigned char *p;
    unsigned n;
    unsigned char att, t;

    p = voice_pat[v];
    if (p == 0)
        return;
    t = p[voice_off[v] + 3U];
    if (t == 0U) {
        voice_clear(v);
        return;
    }
    n = (unsigned)p[voice_off[v]] | ((unsigned)p[voice_off[v] + 1U] << 8);
    att = p[voice_off[v] + 2U];
    if (n == 0xFFFFU)
        snd_tone_off(v);
    else
        snd_tone(v, n, att);
    voice_ticks[v] = t;
}

static void load_noise_step(void)
{
    const unsigned char *p;
    unsigned char t, att, ctrl;

    p = noise_pat;
    if (p == 0)
        return;
    t = p[noise_off + 3U];
    if (t == 0U) {
        noise_clear();
        return;
    }
    ctrl = p[noise_off];
    att = p[noise_off + 2U];
    snd_noise(ctrl, att);
    noise_ticks = t;
}

static unsigned char alloc_voice(unsigned char owner)
{
    unsigned char v;

    for (v = 0; v < 3U; v++) {
        if (voice_owner[v] == owner)
            return v;
    }
    v = voice_next;
    voice_next = (unsigned char)((v + 1U) % 3U);
    return v;
}

static void play_tone_pat(unsigned char owner, const unsigned char *pat)
{
    unsigned char v;

    if (!snd_on || death_att <= 15U)
        return;
    v = alloc_voice(owner);
    voice_owner[v] = owner;
    voice_pat[v] = pat;
    voice_off[v] = 0;
    load_tone_step(v);
}

static void play_noise_pat(unsigned char owner, const unsigned char *pat)
{
    if (!snd_on || death_att <= 15U)
        return;
    noise_owner = owner;
    noise_pat = pat;
    noise_off = 0;
    load_noise_step();
}

static void death_start(void)
{
    unsigned char v;

    if (!snd_on)
        return;
    for (v = 0; v < 3U; v++)
        voice_clear(v);
    noise_clear();
    /* Periodic-looking rumble: low tone2 clocks noise, plus tone0. */
    snd_tone(2, 0x3A0U, 0);
    snd_noise(0x04, 0);
    snd_tone(0, 0x384U, 0);
    death_att = 0;
}

void snd_silence(void)
{
    unsigned char v;

    for (v = 0; v < 3U; v++) {
        voice_owner[v] = 0;
        voice_pat[v] = 0;
        voice_ticks[v] = 0;
    }
    noise_owner = 0;
    noise_pat = 0;
    noise_ticks = 0;
    death_att = 16;
    snd_psg(0x9F);
    snd_psg(0xBF);
    snd_psg(0xDF);
    snd_psg(0xFF);
}

void snd_init(void)
{
    snd_gate_on();
    voice_next = 0;
    snd_silence();
}

void snd_tick(void)
{
    unsigned char v, a;
    const unsigned char *p;

    if (death_att <= 15U) {
        a = death_att;
        snd_psg((unsigned char)(0xF0 | a));
        snd_psg((unsigned char)(0x90 | a));
        snd_psg((unsigned char)(0xD0 | a));
        if (a >= 15U)
            snd_silence();
        else
            death_att = (unsigned char)(a + 1U);
        return;
    }

    for (v = 0; v < 3U; v++) {
        if (voice_owner[v] == 0)
            continue;
        if (voice_ticks[v] != 0U)
            voice_ticks[v]--;
        if (voice_ticks[v] != 0U)
            continue;
        p = voice_pat[v];
        if (p == 0) {
            voice_clear(v);
            continue;
        }
        voice_off[v] = (unsigned char)(voice_off[v] + 4U);
        load_tone_step(v);
    }

    if (noise_owner == 0)
        return;
    if (noise_ticks != 0U)
        noise_ticks--;
    if (noise_ticks != 0U)
        return;
    if (noise_pat == 0) {
        noise_clear();
        return;
    }
    noise_off = (unsigned char)(noise_off + 4U);
    load_noise_step();
}

void snd_play(unsigned char id)
{
    if (!snd_on && id != SND_DEATH)
        return;
    switch (id) {
    case SND_FIRE:
        play_tone_pat(id, pat_fire);
        break;
    case SND_BOARD:
        play_tone_pat(id, pat_board);
        break;
    case SND_RESCUE:
        play_tone_pat(id, pat_rescue);
        break;
    case SND_KILL:
        play_tone_pat(id, pat_kill);
        break;
    case SND_KILL_MANY:
        play_tone_pat(id, pat_kill_many);
        break;
    case SND_START:
        play_tone_pat(id, pat_start);
        break;
    case SND_ALIEN:
        play_tone_pat(id, pat_alien);
        break;
    case SND_SHELL:
        play_noise_pat(id, pat_shell);
        break;
    case SND_MISSILE:
        play_noise_pat(id, pat_missile);
        break;
    case SND_BOOM:
        play_noise_pat(id, pat_boom);
        break;
    case SND_HOUSE:
        play_noise_pat(id, pat_house);
        break;
    case SND_DEATH:
        death_start();
        break;
    default:
        break;
    }
}

void snd_toggle(void)
{
    snd_on = (unsigned char)(snd_on ^ 0xFFU);
    if (!snd_on)
        snd_silence();
}

unsigned char snd_is_on(void)
{
    return snd_on;
}
