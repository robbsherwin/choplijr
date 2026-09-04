;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; pztimer.asm -- Michael Abrash's precision Zen timer, Watcom-callable.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
;
; Provenance:
;   Listing 2-1, "Zen of Assembly Language", Michael Abrash, 26 Apr 1989.
;   MASM -> NASM port by riq (Pungas de Villa Martelli), taken from
;   lib/repos/pcjr-flashparty-2018/common/pztimer.asm.
;
; Changes made here:
;
;   1. Segmented .EXE instead of .COM.  Code goes in _TEXT, data in _DATA, and
;      _DATA is placed in DGROUP so DS-relative addressing resolves.  The
;      original used cs: overrides on every data reference because a .COM has
;      CS = DS = SS; that does not hold in an .EXE.
;
;   2. ZTimerReport is dropped.  It built a $-terminated string and called
;      int 21h AH=09h with "push cs / pop ds", which cannot work once the
;      strings live in _DATA rather than alongside the code.  C prints instead,
;      which also means we can report bytes/second and per-byte cost rather
;      than a bare tick count.
;
;   3. ztimer_count / ztimer_overflow expose the raw result.  The original
;      folded the tick-to-microsecond conversion (x 0.8381) into 16-bit
;      arithmetic inside ZTimerReport; doing it in 32-bit C is both clearer and
;      exact enough to derive throughput from.
;
;   4. BUG FIX, and it is a real one.  The upstream port restores the caller's
;      interrupt flag in ZTimerOff with
;
;          and     ch,! 0fdh
;
;      In MASM, "NOT 0FDH" is 02h -- the mask that isolates IF in the high byte
;      of FLAGS.  In NASM, "!" is *logical* not, so "!0fdh" evaluates to 0, CH
;      is zeroed, and the subsequent "or ah,ch" contributes nothing.  The
;      caller's interrupt flag is therefore always restored as *clear*, and
;      interrupts stay disabled after every ZTimerOff.  NASM spells bitwise not
;      "~".  We write the mask literally as 0x02 so it cannot be misread again.
;
;      This is latent in the demo it came from, which re-enables interrupts
;      elsewhere, but it would have silently stopped the system clock and the
;      keyboard for the rest of M1's run.
;
; Resolution is one 8253 tick: 1 / (14.31818 MHz / 12) = 838.1 ns.  Timer 0 is
; 16 bits, so the measurable interval tops out near 54.9 ms; past that
; ztimer_overflow() reports true and the count is meaningless.
;
; Interrupts are disabled from ztimer_on() until ztimer_off().  On the PCjr the
; keyboard is scanned from the non-maskable interrupt, which CLI cannot hold
; off, so do not press keys while a measurement is running.
;
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;

        bits    16
        cpu     8086

BASE_8253       equ     0x40
TIMER_0_8253    equ     BASE_8253 + 0
MODE_8253       equ     BASE_8253 + 3
OCW3            equ     0x20            ; 8259 Operation Command Word 3 (write)
IRR             equ     0x20            ; 8259 Interrupt Request register (read)

FLAGS_IF_HI     equ     0x02            ; IF is bit 9 of FLAGS = bit 1 of the
                                        ;  high byte

; Emulate POPF, working around the 80286 erratum that lets an interrupt occur
; during POPF even when the popped IF is clear.
%macro MPOPF 0
        jmp     short %%p2
%%p1:   iret                            ; jump to pushed address & pop flags
%%p2:   push    cs                      ; construct far return address to
        call    %%p1                    ;  the next instruction
%endmacro

; Delay long enough between successive accesses to the same I/O device that it
; can respond to both, even on a fast machine.
%macro DELAY 0
        jmp     $+2
        jmp     $+2
        jmp     $+2
%endmacro

        segment _DATA class=DATA align=2
        group   DGROUP _DATA

; Initialised rather than reserved: six bytes in the .EXE image buys us
; not having to care how the object writer treats uninitialised space in a
; DGROUP segment.
OriginalFlags:  db      0               ; high byte of FLAGS when ztimer_on ran
TimedCount:     dw      0               ; timer 0 count at ztimer_off
ReferenceCount: dw      0               ; ticks consumed by the timer overhead
OverflowFlag:   db      0               ; nonzero if timer 0 wrapped

        segment _TEXT class=CODE align=2

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl ztimer_on(void)
;
; Starts the timer.  Returns with interrupts disabled regardless of how it was
; called; ztimer_off restores the caller's interrupt flag.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _ztimer_on
_ztimer_on:
        push    ax
        pushf
        pop     ax                      ; grab the flags so we can leave
                                        ;  interrupts off on the way out
        mov     [OriginalFlags],ah      ; remember the caller's IF
        and     ah,0fdh                 ; the copy we push back has IF clear
        push    ax

        sti                             ; let any pending timer interrupt fire

        ; Put timer 0 in mode 2 (divide-by-N) so it counts linearly rather than
        ; by twos, and leave it waiting for an initial count.
        mov     al,00110100b
        out     MODE_8253,al

        ; Zero the count so we do not take another timer interrupt immediately.
        ; This can cost the system clock up to 54 ms each time it runs.
        DELAY
        sub     al,al
        out     TIMER_0_8253,al         ; lsb
        DELAY
        out     TIMER_0_8253,al         ; msb

        ; Give the mode 3 -> mode 2 transition interrupt time to be recognised
        ; (needs >= 210 ns; ten jumps is plenty on anything).
%rep    10
        jmp     $+2
%endrep

        cli                             ; from here on the count is ours

        mov     al,00110100b            ; reload, starting the interval
        out     MODE_8253,al
        DELAY
        sub     al,al
        out     TIMER_0_8253,al         ; lsb
        DELAY
        out     TIMER_0_8253,al         ; msb

        MPOPF                           ; keeps interrupts off
        pop     ax
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl ztimer_off(void)
;
; Stops the timer, latches the count, checks for wrap, measures its own
; overhead, and restores the caller's interrupt flag.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _ztimer_off
_ztimer_off:
        push    ax
        push    cx
        pushf

        mov     al,00000000b            ; latch timer 0
        out     MODE_8253,al

        ; A pending IRQ0 means timer 0 wrapped and the count is worthless.
        mov     al,00001010b            ; OCW3: select the Interrupt Request
        out     OCW3,al                 ;  register for reading
        DELAY
        in      al,IRR
        and     al,1                    ; 1 if IRQ0 (timer) is pending
        mov     [OverflowFlag],al

        sti                             ; interrupts may happen again

        in      al,TIMER_0_8253         ; lsb
        DELAY
        mov     ah,al
        in      al,TIMER_0_8253         ; msb
        xchg    ah,al
        neg     ax                      ; countdown remaining -> elapsed
        mov     [TimedCount],ax

        ; Time an empty interval sixteen times and average it, so the caller's
        ; figure has this routine's own cost removed.
        mov     word [ReferenceCount],0
        mov     cx,16
        cli                             ; off, for a precise reference count
.ref_loop:
        call    ReferenceZTimerOn
        call    ReferenceZTimerOff
        loop    .ref_loop
        sti
        add     word [ReferenceCount],8 ; total + (0.5 * 16), i.e. round
        mov     cl,4
        shr     word [ReferenceCount],cl

        ; Restore the caller's interrupt flag.
        pop     ax                      ; flags as they were on entry
        mov     ch,[OriginalFlags]      ; high FLAGS byte from ztimer_on
        and     ch,FLAGS_IF_HI          ; keep only the original IF
                                        ;  (upstream had "and ch,! 0fdh",
                                        ;   which NASM evaluates to 0)
        and     ah,0fdh                 ; drop the current IF, keep the rest
        or      ah,ch                   ; splice the original IF back in
        push    ax

        MPOPF
        pop     cx
        pop     ax
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; unsigned __cdecl ztimer_count(void)
;
; Net ticks between ztimer_on and ztimer_off, with the timer's own overhead
; subtracted.  Multiply by 838 ns for wall time.  Meaningless if
; ztimer_overflow() is true.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _ztimer_count
_ztimer_count:
        mov     ax,[TimedCount]
        sub     ax,[ReferenceCount]
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; unsigned __cdecl ztimer_overflow(void)
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _ztimer_overflow
_ztimer_overflow:
        sub     ax,ax
        mov     al,[OverflowFlag]
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; Overhead-reference helpers.  Called only from ztimer_off, with interrupts
; already off.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
ReferenceZTimerOn:
        push    ax
        pushf                           ; interrupts are already off

        mov     al,00110100b            ; mode 2, load initial count
        out     MODE_8253,al
        DELAY
        sub     al,al
        out     TIMER_0_8253,al         ; lsb
        DELAY
        out     TIMER_0_8253,al         ; msb

        MPOPF
        pop     ax
        ret

ReferenceZTimerOff:
        push    ax
        push    cx
        pushf

        mov     al,00000000b            ; latch timer 0
        out     MODE_8253,al
        DELAY
        in      al,TIMER_0_8253         ; lsb
        DELAY
        mov     ah,al
        in      al,TIMER_0_8253         ; msb
        xchg    ah,al
        neg     ax
        add     [ReferenceCount],ax

        MPOPF
        pop     cx
        pop     ax
        ret
