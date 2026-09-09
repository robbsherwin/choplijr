/* snd.h -- SN76496 effects for M8.
 *
 * DESIGN.md section 9: three tone voices plus noise, port 0C0h, port 61h
 * bits 5-6 to un-gate (jrpiano3; DOSBox ignores the gate).  Voice stealing
 * matches jrpiano3 voice_next / voice_sc: retrigger the same owner, else
 * round-robin steal.  Noise is a fourth channel, not in that trio.
 *
 * Apple playSound is X=pitch delay, Y=clicks, A=decay.  N = X<<2
 * (clamp 1..1023).  Streams run on the 20 Hz sim tick, not blocking. */

#ifndef SND_H
#define SND_H

#define SND_FIRE        1
#define SND_SHELL       2
#define SND_MISSILE     3
#define SND_BOARD       4
#define SND_RESCUE      5
#define SND_KILL        6
#define SND_KILL_MANY   7
#define SND_START       8
#define SND_DEATH       9
#define SND_BOOM        10
#define SND_HOUSE       11
#define SND_ALIEN       12

void snd_init(void);
void snd_silence(void);
void snd_tick(void);
void snd_play(unsigned char id);
void snd_toggle(void);
unsigned char snd_is_on(void);

#endif
