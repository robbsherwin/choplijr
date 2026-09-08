;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; stick.asm -- PCjr joystick (INT 15h AH=84h and port 201h) plus INT 9 keys.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
;
; Choplifter! for the IBM PCjr, milestone M5.  DESIGN.md sections 6, 10, 13.
;
; Axes drive the original Apple paddle mapping in C (0-255, then the $7000
; acceleration tables).  This file only samples the hardware.
;
; Port 201h is the PCjr path.  The counting loop is copied from Paku Paku
; 1.6a SOURCE/JOYSTICK.PAS (joyStick1Axis / joyStick2Axis): CLI, OUT 201h,
; count while the axis bits stay high, timeout CX=$7FFF.  Trixter's 1.6a
; note: CLI stops twitchy counts on a PCjr.  M5 uses joystick 1 only
; (Paku joyStick1Axis, bits 0,1; IBM first connector).  Stick B (bits 2,3)
; is implemented in stick_read_both but M5 does not call it.  INT 15h
; AH=84h is an override only -- original PCjr BIOS does not implement it.
; Buttons: port 201h active low; stick 1 is bits 4-5 (Paku nibble bits 0-1).
;
; Keyboard follows jrpiano3.asm (new_int9 at line 432) and KEY_INT\KEYB_OBJ.PAS.
; BIOS int 16h cannot report simultaneous or held keys.
;
; PCjr keyboard is NMI, not IRQ1.  BIOS NMI calls INT 48h with the scan in AL,
; then the default INT 48 translates and issues INT 9.  jrpiano hooks INT 9,
; reads port 60h (BIOS leaves the translated set-1 byte on Port A so PC-style
; IN 60h still works), make/break into kbd[], chains the original handler, and
; empties the BIOS type-ahead buffer.  We do that, and also hook INT 48h so
; AL from the NMI path is recorded even if INT 9 is entered as IRQ1 with a
; leftover AL (a PC-style stack-AL read is not the scan code on DOSBox).
;
; Do not treat INT 9's AL as a scan code: that register is only the key on
; the software INT 48 path.  A second 128-byte pulse array latches makes
; until kbd_clear_pulse, so a make+break in one retrace still counts as down.
; kbd_broke[] latches the break bit until the same clear, so C can hold S/A/D
; until a real break and ignore extra typematic makes.
;
; All routines are __cdecl and preserve BP, SI, DI, DS, ES and the direction
; flag, except the interrupt handler which preserves what it uses.
;
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;

        bits    16
        cpu     8086

GAMEPORT        equ     0x0201
STICK_TIMEOUT   equ     0x7FFF          ; Paku JOYSTICK.PAS CX=$7FFF; must match pcjr.h
STICK_LIVE_MAX  equ     (STICK_TIMEOUT - 16)

        segment _TEXT class=CODE align=2

; CS-relative DGROUP snapshot, set by kbd_hook.  ISRs cannot assume DS.
dgroup_seg:
        dw      0
old_int9:
        dw      0, 0                    ; offset, segment
old_int48:
        dw      0, 0                    ; PCjr NMI keyboard intercept

; Make/break and pulse arrays live in CS so INT 9 and C both see them
; without DGROUP games.  kbd_is_down reads CS:kbd | CS:kbd_pulse.
kbd:
        times   128 db 0
kbd_pulse:
        times   128 db 0
kbd_broke:
        times   128 db 0

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; unsigned __cdecl stick_read_both(
;     unsigned *x0, unsigned *y0, unsigned *x1, unsigned *y1,
;     unsigned *buttons)
;
; Paku Paku 1.6a joyStick1Axis then joyStick2Axis, verbatim counting:
;   xor ax/bx/di, CX=$7FFF, CLI, OUT 201h, then
;     SHR AL,1 / ADC BX,0 / ADD DI,AX / IN AL,DX / AND mask / LOOPNZ
; Stick A mask $03 (bits 0,1).  Stick B AND $0F then two SHRs (bits 2,3).
; Buttons: (~port >> 4) & 15, so bits 0-1 stick A, 2-3 stick B, 1 = pressed.
; AX = 1 if either stick has an axis inside (0, timeout).
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _stick_read_both
_stick_read_both:
        push    bp
        mov     bp,sp
        push    di
        push    si
        push    ds
        push    es

        xor     ax,ax
        xor     bx,bx
        mov     cx,STICK_TIMEOUT
        xor     di,di
        mov     dx,GAMEPORT
        cli
        out     dx,al
.a_loop:
        shr     al,1
        adc     bx,0
        add     di,ax
        in      al,dx
        and     al,0x03
        loopnz  .a_loop
        sti

        mov     si,[bp+4]
        mov     [si],bx
        mov     si,[bp+6]
        mov     [si],di

        xor     ax,ax
        xor     bx,bx
        mov     cx,STICK_TIMEOUT
        xor     di,di
        mov     dx,GAMEPORT
        cli
        out     dx,al
.b_loop:
        shr     al,1
        adc     bx,0
        add     di,ax
        in      al,dx
        and     al,0x0F
        shr     al,1
        shr     al,1
        loopnz  .b_loop
        sti

        mov     si,[bp+8]
        mov     [si],bx
        mov     si,[bp+10]
        mov     [si],di

        in      al,dx
        not     al
        mov     cl,4
        shr     al,cl
        and     ax,0x000F
        mov     si,[bp+12]
        mov     [si],ax

        mov     ax,1
        mov     si,[bp+4]
        mov     bx,[si]
        mov     si,[bp+6]
        mov     cx,[si]
        call    .pair_live
        jc      .both_done
        mov     si,[bp+8]
        mov     bx,[si]
        mov     si,[bp+10]
        mov     cx,[si]
        call    .pair_live
        jc      .both_done
        xor     ax,ax
.both_done:
        pop     es
        pop     ds
        pop     si
        pop     di
        pop     bp
        ret

; BX=x CX=y.  CF=1 if an axis is inside (0, STICK_TIMEOUT).
.pair_live:
        cmp     bx,0
        je      .x_edge
        cmp     bx,STICK_LIVE_MAX
        jae     .x_edge
        stc
        ret
.x_edge:
        cmp     cx,0
        je      .pair_dead
        cmp     cx,STICK_LIVE_MAX
        jae     .pair_dead
        stc
        ret
.pair_dead:
        clc
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; unsigned __cdecl stick_read_port(unsigned *x, unsigned *y, unsigned *buttons)
;
; Stick A only (Paku joyStick1Axis).  Buttons bits 0-1 of the nibble.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _stick_read_port
_stick_read_port:
        push    bp
        mov     bp,sp
        push    di
        push    si
        push    ds
        push    es

        xor     ax,ax
        xor     bx,bx
        mov     cx,STICK_TIMEOUT
        xor     di,di
        mov     dx,GAMEPORT
        cli
        out     dx,al
.port_loop:
        shr     al,1
        adc     bx,0
        add     di,ax
        in      al,dx
        and     al,0x03
        loopnz  .port_loop
        sti

        mov     si,bx                   ; X
        ; DI = Y

        in      al,dx
        not     al
        mov     cl,4
        shr     al,cl
        and     ax,3
        mov     ah,0

        mov     bx,[bp+4]
        mov     [bx],si
        mov     bx,[bp+6]
        mov     [bx],di
        mov     bx,[bp+8]
        mov     [bx],ax

        mov     ax,1
        cmp     si,0
        je      .p_x_edge
        cmp     si,STICK_LIVE_MAX
        jae     .p_x_edge
        jmp     short .p_live
.p_x_edge:
        cmp     di,0
        je      .p_dead
        cmp     di,STICK_LIVE_MAX
        jae     .p_dead
.p_live:
        mov     ax,1
        jmp     short .p_done
.p_dead:
        xor     ax,ax
.p_done:
        pop     es
        pop     ds
        pop     si
        pop     di
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; unsigned __cdecl stick_read_bios(unsigned *x, unsigned *y, unsigned *buttons)
;
; INT 15h AH=84h DX=1: AX=A(X), BX=A(Y).  CF set => not supported, AX=0.
; Buttons still come from port 201h (same mapping as stick_read_port).
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _stick_read_bios
_stick_read_bios:
        push    bp
        mov     bp,sp
        push    di
        push    si
        push    ds
        push    es

        mov     dx,1
        mov     ah,0x84
        stc                             ; unimplemented BIOS leaves CF set
        int     0x15
        jc      .bios_fail
        ; AX = X, BX = Y.  DX was the BIOS Y2 and is now trash.
        mov     si,ax
        mov     di,bx

        mov     dx,GAMEPORT
        in      al,dx
        not     al
        mov     cl,4
        shr     al,cl
        and     ax,3
        mov     ah,0

        mov     bx,[bp+4]
        mov     [bx],si
        mov     bx,[bp+6]
        mov     [bx],di
        mov     bx,[bp+8]
        mov     [bx],ax
        mov     ax,1
        jmp     short .bios_done

.bios_fail:
        xor     ax,ax
        mov     bx,[bp+4]
        mov     word [bx],0
        mov     bx,[bp+6]
        mov     word [bx],0
        mov     bx,[bp+8]
        mov     word [bx],0

.bios_done:
        pop     es
        pop     ds
        pop     si
        pop     di
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; unsigned __cdecl kbd_is_down(unsigned scan)
; scan is 0..127 (make code).  Returns 0 or 1.  Held (kbd[]) or pulsed
; since the last kbd_clear_pulse both count.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _kbd_is_down
_kbd_is_down:
        push    bp
        mov     bp,sp
        push    bx
        mov     bx,[bp+4]
        and     bx,0x007F
        mov     al,[cs:kbd+bx]
        or      al,[cs:kbd_pulse+bx]
        mov     ah,0
        pop     bx
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; unsigned __cdecl kbd_held(unsigned scan)
; kbd[] only (last make/break).  Extra makes stay 1; a break stores 0.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _kbd_held
_kbd_held:
        push    bp
        mov     bp,sp
        push    bx
        mov     bx,[bp+4]
        and     bx,0x007F
        mov     al,[cs:kbd+bx]
        mov     ah,0
        pop     bx
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; unsigned __cdecl kbd_broke(unsigned scan)
; 1 if a break bit was seen since kbd_clear_pulse.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _kbd_broke
_kbd_broke:
        push    bp
        mov     bp,sp
        push    bx
        mov     bx,[bp+4]
        and     bx,0x007F
        mov     al,[cs:kbd_broke+bx]
        mov     ah,0
        pop     bx
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl kbd_clear_pulse(void)
; Zero the one-tick make and break latches.  Call once per sim/title frame
; after reading kbd_is_down / kbd_broke.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _kbd_clear_pulse
_kbd_clear_pulse:
        push    di
        push    es
        push    cs
        pop     es
        mov     di,kbd_pulse
        xor     ax,ax
        mov     cx,128                  ; pulse[] + broke[]
        cld
        rep     stosw
        pop     es
        pop     di
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl kbd_hook(void)
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _kbd_hook
_kbd_hook:
        push    bp
        push    si
        push    di
        push    ds
        push    es

        mov     [cs:dgroup_seg],ds

        push    cs
        pop     es
        mov     di,kbd
        xor     ax,ax
        mov     cx,192                  ; kbd[] + pulse[] + broke[]
        cld
        rep     stosw

        mov     ax,0x3509
        int     0x21
        mov     [cs:old_int9],bx
        mov     [cs:old_int9+2],es
        mov     ax,0x3548
        int     0x21
        mov     [cs:old_int48],bx
        mov     [cs:old_int48+2],es
        push    cs
        pop     ds
        mov     dx,new_int9
        mov     ax,0x2509
        int     0x21
        mov     dx,new_int48
        mov     ax,0x2548
        int     0x21

        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl kbd_unhook(void)
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _kbd_unhook
_kbd_unhook:
        push    bp
        push    si
        push    di
        push    ds
        push    es

        mov     ax,[cs:old_int48+2]
        or      ax,[cs:old_int48]
        jz      .no48
        lds     dx,[cs:old_int48]
        mov     ax,0x2548
        int     0x21
        xor     ax,ax
        mov     [cs:old_int48],ax
        mov     [cs:old_int48+2],ax
.no48:
        mov     ax,[cs:old_int9+2]
        or      ax,[cs:old_int9]
        jz      .nohook
        lds     dx,[cs:old_int9]
        mov     ax,0x2509
        int     0x21
        xor     ax,ax
        mov     [cs:old_int9],ax
        mov     [cs:old_int9+2],ax
.nohook:
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; INT 48h: PCjr NMI keyboard intercept.  AL is the scan (make/break).  Record
; it, then chain so BIOS can translate and INT 9.  jrpiano does not hook this;
; we do because INT 9's AL is not the key when IRQ1 (DOSBox) delivers INT 9.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
new_int48:
        push    ax
        push    bx
        push    ds
        push    cs
        pop     ds                      ; DS = CODE, as jrpiano3 new_int9
        call    kbd_apply               ; AL = NMI scan; BIOS still needs it
        pop     ds
        pop     bx
        pop     ax
        pushf
        call    far [cs:old_int48]
        iret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; INT 9: port 60h make/break, chain, empty BIOS buffer.  jrpiano3.asm:432.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
new_int9:
        push    ax
        push    bx
        push    ds
        push    cs
        pop     ds                      ; DS = CODE (kbd[] and old_int9)

        in      al,0x60                 ; raw scan byte (jrpiano3.asm:439)
        call    kbd_apply

        pushf
        call    far [cs:old_int9]

        ; 0040:001A = buffer head, 0040:001C = buffer tail.  Equal = empty.
        cli
        mov     ax,0x40
        mov     ds,ax
        mov     ax,[0x1C]
        mov     [0x1A],ax
        sti

        pop     ds
        pop     bx
        pop     ax
        iret

; AL = raw make/break.  kbd[] is CS-relative.  Scan 0 ignored.
kbd_apply:
        mov     ah,al
        and     al,0x7F
        jz      .apply_done
        xor     bh,bh
        mov     bl,al
        xor     al,al
        test    ah,0x80
        jnz     .apply_break
        inc     al
        mov     [cs:kbd_pulse+bx],al
        jmp     .apply_store
.apply_break:
        mov     byte [cs:kbd_broke+bx],1
.apply_store:
        mov     [cs:kbd+bx],al
.apply_done:
        ret
