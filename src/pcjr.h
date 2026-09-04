/* pcjr.h -- PCjr video, timing and DOS-memory primitives.
 *
 * Choplifter! for the IBM PCjr, milestone M1 (video spike).
 * See DESIGN.md sections 3, 7, 11 and 13.
 *
 * Everything declared here is implemented in NASM under src/asm/.
 *
 * ABI notes
 * ---------
 * All routines are __cdecl, so the C-to-asm boundary is the plain stack
 * convention.  DESIGN.md section 12 plans #pragma aux register conventions for
 * the hot paths; that is a real win for M3's per-sprite blitter, but M1 times
 * whole calls of 4,000-16,000 bytes, where a stack frame is under 0.1% of the
 * measurement.  Legibility wins here.
 *
 * Buffers are passed as explicit (segment, offset) pairs rather than as far
 * pointers, so the data model cannot silently change the ABI.
 *
 * Build model must be small or compact: every routine is reached by a NEAR
 * call.
 */

#ifndef PCJR_H
#define PCJR_H

/* ------------------------------------------------------------------ mode 8 */

/* BIOS mode 8: 160x200, 16 colours, 80 bytes/row, 2 px/byte, leftmost pixel
 * in the high nibble.  Even scanlines live in the first 8 KB, odd scanlines in
 * the second -- the same two-bank split CGA graphics modes use. */
#define M8_MODE            0x08
#define M8_WIDTH_PX        160
#define M8_HEIGHT_PX       200
#define M8_BYTES_PER_ROW   80
#define M8_BANK_STRIDE     0x2000U
#define M8_SCREEN_BYTES    16000U

/* row_offset(y) = (y & 1) * 0x2000 + (y >> 1) * 80   (DESIGN.md section 3) */
#define M8_ROW_OFF(y)   ((unsigned)(((y) & 1) * M8_BANK_STRIDE) \
                         + (unsigned)(((y) >> 1) * M8_BYTES_PER_ROW))

/* A solid colour fills both nibbles of every byte. */
#define M8_SOLID(c)     ((unsigned)(((c) & 0x0F) * 0x1111U))

/* --------------------------------------------------------- ground colour */

/* DESIGN.md section 5 gives palette index 6 the "ground" role but leaves the
 * colour that register holds deliberately open, because the two candidates
 * want comparing on a real RGB monitor before either is committed to:
 *
 *   GROUND_BROWN   IRGB 6, brown.  What section 5's table first proposed.
 *   GROUND_PINK    IRGB 13, light magenta.  What the Apple original's terrain
 *                  actually reads as: landBackground at choplifter.s:10987 is
 *                  a $55,$2A,$55,$2A pseudo-sprite, and the reverse
 *                  engineering's own comment on that fill describes the
 *                  resulting terrain as pink/violet.  Reproducing the hue
 *                  rather than the brown is arguably the more faithful of the
 *                  two, which is why it is a real question and not a
 *                  preference.
 *
 * Because section 5 assigns indices by role and not by hue, switching between
 * them is one byte of the palette table and nothing else -- no pixel value
 * moves, no sprite data changes, no other index is disturbed.  That is the
 * PCjr's palette indirection doing the job it exists for.
 *
 * Set at build time; the makefile and build.bat both take GROUND=BROWN or
 * GROUND=PINK and pass it through as -dGROUND_COLOUR=GROUND_<name>. */
#define GROUND_BROWN    6
#define GROUND_PINK     13

#ifndef GROUND_COLOUR
#define GROUND_COLOUR   GROUND_BROWN
#endif

#if GROUND_COLOUR == GROUND_BROWN
#define GROUND_COLOUR_NAME  "BROWN"
#define GROUND_OTHER_NAME   "PINK"
#elif GROUND_COLOUR == GROUND_PINK
#define GROUND_COLOUR_NAME  "PINK"
#define GROUND_OTHER_NAME   "BROWN"
#else
#error GROUND_COLOUR must be GROUND_BROWN or GROUND_PINK
#endif

/* The palette registers the ground band is drawn in.  These are pixel values
 * and they never change: only what colour register M8_IDX_GROUND holds does.
 * The highlight row above the ground (DESIGN.md section 15) stays yellow in
 * both variants -- it reads as a lit edge against either. */
#define M8_IDX_GROUND       6
#define M8_IDX_GROUND_HI    14

/* ------------------------------------------------------- pages and windows */

/* The CRT/Processor Page Register at port 0x3DF selects, independently, which
 * 16 KB block the CRTC displays (bits 0-2) and which block the CPU sees at
 * B800:0000 (bits 3-5).  Bits 6-7 are the video address mode.  Because the two
 * page fields are independent, flipping pages is a single OUT -- no buffer copy
 * ever.  That is the capability the whole of DESIGN.md section 7 rests on. */
#define PCJR_PAGE_PORT     0x3DFU
#define PCJR_STATUS_PORT   0x3DAU

/* Bits 6-7, the video address mode.  DESIGN.md section 3 documents 1 as the
 * 16 KB graphics setting, which is what mode 8 is.  The 32 KB modes' encoding
 * is not written down anywhere we have checked, so it is not guessed at here.
 * Port 0x3DF is write-only, which is why we have to supply this at all rather
 * than preserving whatever BIOS left in place -- and why m1.c can also flip
 * through int 10h, where BIOS works it out for us. */
#define PCJR_ADDR_16K      1u

#define PCJR_PAGE_COUNT    8       /* eight 16 KB pages in the low 128 KB */
#define PAGE_BYTES         0x4000UL
#define PAGE_PARAS         0x0400U             /* 16 KB expressed in paragraphs */
#define PAGE_SEG(p)        ((unsigned)((p) * PAGE_PARAS))   /* page 6 -> 0x1800 */

/* The B8000 aperture.  On the PCjr this is not separate video RAM: it is a
 * window onto whichever 16 KB block of main memory the CPU page field selects.
 * M1 verifies that claim rather than assuming it. */
#define PCJR_APERTURE_SEG  0xB800U

/* ------------------------------------------------------------- src/asm/pcjrvid.asm */

unsigned __cdecl vid_get_mode(void);            /* int 10h AH=0Fh -> AL */
void     __cdecl vid_set_mode(unsigned mode);   /* int 10h AX=00mm, clears screen */

/* int 10h AH=05h AL=80h/83h.  BH = CRT page, BL = CPU page.
 *
 * That order follows Ralf Brown's interrupt list and, more usefully,
 * lib/repos/pcjr-flashparty-2018/part3/part3.asm:1113 (set_vid_160_100_16),
 * which reads the pair back with AL=80h and then shifts BL into bits 3-5 --
 * the CPU page field.  That code is known to work on real hardware, so BL is
 * the CPU page.  DESIGN.md section 3 originally had it the other way round;
 * probe_bios_page_order() in m1.c settles it empirically regardless. */
unsigned __cdecl vid_get_pages(void);           /* -> (crt << 8) | cpu */
void     __cdecl vid_set_pages_bios(unsigned crt_page, unsigned cpu_page);

/* Raw OUT to 0x3DF.  Faster and retrace-precise, but the port is write-only,
 * so we have to supply the address-mode bits ourselves rather than preserving
 * whatever BIOS put there. */
void     __cdecl vid_set_pages_port(unsigned crt_page, unsigned cpu_page,
                                    unsigned addr_mode);

void     __cdecl vid_wait_retrace(void);        /* return with VR just starting */
void     __cdecl vid_wait_hretrace(void);

void     __cdecl vid_set_palette(unsigned index, unsigned colour);
void     __cdecl vid_set_palette16(const unsigned char *pal16);

/* ------------------------------------------------------------- src/asm/prims.asm */

/* The primitives DESIGN.md section 2's throughput table is about. */
void __cdecl fill_words(unsigned dseg, unsigned doff, unsigned nwords,
                        unsigned pattern);              /* rep stosw */
void __cdecl fill_bytes(unsigned dseg, unsigned doff, unsigned nbytes,
                        unsigned pattern);              /* rep stosb */
void __cdecl copy_words(unsigned dseg, unsigned doff,
                        unsigned sseg, unsigned soff, unsigned nwords);  /* movsw */
void __cdecl copy_bytes(unsigned dseg, unsigned doff,
                        unsigned sseg, unsigned soff, unsigned nbytes);  /* movsb */

/* fill_rect as DESIGN.md section 6 lists it: a mode 8 rectangle, so it pays
 * the two-bank row arithmetic that a flat fill_words does not.  Measuring both
 * is the point -- the difference is the per-row overhead the frame budget in
 * section 7 quietly assumes away.
 *
 * xbyte and wbytes are byte columns (2 px each); wbytes must be even. */
void __cdecl fill_rect_m8(unsigned dseg, unsigned xbyte, unsigned y,
                          unsigned wbytes, unsigned rows, unsigned pattern);

/* Full-width horizontal band, the sky and ground primitive (section 6). */
void __cdecl fill_band_m8(unsigned dseg, unsigned y, unsigned rows,
                          unsigned pattern);

/* ------------------------------------------------------------- src/asm/pztimer.asm */

/* Michael Abrash's precision Zen timer, adapted from
 * lib/repos/pcjr-flashparty-2018/common/pztimer.asm.  Resolution is one 8253
 * tick, 0.8381 us; the usable window is about 54 ms before timer 0 wraps.
 *
 * Interrupts are off between ztimer_on() and ztimer_off().  Do not call DOS or
 * BIOS in between, and do not touch the keyboard: the PCjr scans it from the
 * non-maskable interrupt, which CLI cannot hold off and which would land
 * inside the measurement. */
void     __cdecl ztimer_on(void);
void     __cdecl ztimer_off(void);
unsigned __cdecl ztimer_count(void);      /* net 8253 ticks, overhead removed */
unsigned __cdecl ztimer_overflow(void);   /* nonzero => interval exceeded ~54 ms */

#define ZTIMER_NS_PER_TICK  838L          /* 1 / 1.193182 MHz = 838.1 ns */

/* --------------------------------------------------------- src/asm/dosmem.asm */

unsigned __cdecl dos_get_psp(void);             /* int 21h AH=51h */
unsigned __cdecl dos_first_mcb(void);           /* int 21h AH=52h, [es:bx-2] */
unsigned __cdecl dos_mem_size_kb(void);         /* int 12h */
void     __cdecl dos_break_off(void);           /* int 21h AX=3301h DL=0 */

/* int 21h AH=48h / 49h / 4Ah.  Each returns 0 on success or the DOS error
 * code; *max_paras receives the largest available block on error 8. */
unsigned __cdecl dos_alloc(unsigned paras, unsigned *seg_out, unsigned *max_paras);
unsigned __cdecl dos_free(unsigned seg);
unsigned __cdecl dos_setblock(unsigned seg, unsigned paras, unsigned *max_paras);

/* Far peek/poke, so the C side can read the DOS memory-control-block chain and
 * physical video pages without depending on the data model. */
unsigned char __cdecl peek_byte(unsigned seg, unsigned off);
unsigned      __cdecl peek_word(unsigned seg, unsigned off);
void          __cdecl poke_byte(unsigned seg, unsigned off, unsigned val);
void          __cdecl poke_word(unsigned seg, unsigned off, unsigned val);

#endif /* PCJR_H */
