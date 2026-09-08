;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; dosmem.asm -- DOS memory enquiry, plus far peek and poke.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
;
; Choplifter! for the IBM PCjr, milestone M1.  DESIGN.md section 3, "Buffer
; allocation", and section 14's top-severity risk.
;
; The problem this file exists to answer: mode 8 needs 16 KB and we want two
; pages, which means owning two 16 KB-aligned blocks inside the low 128 KB --
; the only memory the Video Gate Array can address.  BIOS has already taken the
; top block for its active page and told DOS about less memory than the machine
; has.  Whether the block below it is actually ours to take is a question about
; DOS and BIOS behaviour, not about our code, so M1 asks DOS directly and
; reports what it says rather than assuming.
;
; Nothing here is PCjr-specific; it is plain DOS 2.x-compatible interrupt
; plumbing, deliberately hand-written so that no run-time library's idea of who
; owns memory can get in the way of the measurement.
;
; All routines are __cdecl and preserve BP, SI, DI, DS, ES and the direction
; flag.  Pointer arguments are near pointers into DGROUP (small or compact
; model).
;
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;

        bits    16
        cpu     8086

        segment _TEXT class=CODE align=2

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; unsigned __cdecl dos_get_psp(void)
;
; int 21h AH=51h.  The documented call is AH=62h, but that is DOS 3.0 and
; later; DESIGN.md section 2 targets PC-DOS 2.1.  AH=51h is the undocumented
; original and is present in every DOS from 2.0 onwards -- 62h was only ever a
; documented alias for it.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _dos_get_psp
_dos_get_psp:
        push    bp
        push    si
        push    di
        push    ds
        push    es

        mov     ah,0x51
        int     0x21                    ; bx = PSP segment
        mov     ax,bx

        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; unsigned __cdecl dos_first_mcb(void)
;
; int 21h AH=52h returns ES:BX pointing at the DOS "list of lists"; the word
; immediately below it is the segment of the first memory control block.
; Undocumented, but stable from DOS 2.0 through to the present day, and the
; only way to see the whole arena rather than just the part DOS is willing to
; hand out.  The caller checks the 'M'/'Z' signature before trusting it.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _dos_first_mcb
_dos_first_mcb:
        push    bp
        push    si
        push    di
        push    ds
        push    es

        mov     ah,0x52
        int     0x21
        mov     ax,[es:bx-2]

        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl dos_sysvars(unsigned *seg_out, unsigned *off_out)
;
; Same int 21h AH=52h as dos_first_mcb, but returns ES:BX itself -- the DOS
; list of lists.  M1 uses LoL+22h (DOS 3+) to walk the device chain for
; JRCONSYS.  DS is reloaded before the stores because we write through DGROUP
; pointers afterwards.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _dos_sysvars
_dos_sysvars:
        push    bp
        mov     bp,sp
        push    si
        push    di
        push    ds
        push    es

        mov     ah,0x52
        push    ds
        int     0x21
        pop     ds
        mov     si,[bp+4]               ; *seg_out = ES
        mov     [si],es
        mov     si,[bp+6]               ; *off_out = BX
        mov     [si],bx

        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; unsigned __cdecl dos_mem_size_kb(void)
;
; int 12h -- conventional memory in KB, as BIOS reports it.  On a 128 KB PCjr
; this should come back as 112, the 16 KB shortfall being the video page BIOS
; kept for itself.  jrIDE.html: the jrIDE BIOS sets this to 736 KB (608 KB of
; sidecar SRAM from 128 KB to 736 KB; the system BIOS only scans to 640 KB).
; That extra RAM is not a 3DF page.  Either way we would rather know.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _dos_mem_size_kb
_dos_mem_size_kb:
        push    bp
        push    si
        push    di
        push    ds
        push    es

        int     0x12                    ; ax = KB

        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl dos_break_off(void)
;
; int 21h AX=3301h DL=00h -- stop DOS checking for Ctrl-Break inside its own
; calls.  Without this, a keypress during the report could terminate us through
; int 23h, skipping the atexit handler and leaving the machine in mode 8 with
; the wrong page displayed.  DOS restores the flag itself when the process
; ends, so there is nothing to unhook.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _dos_break_off
_dos_break_off:
        push    bp
        push    si
        push    di
        push    ds
        push    es

        mov     ax,0x3301
        sub     dl,dl
        int     0x21

        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; unsigned __cdecl dos_alloc(unsigned paras, unsigned *seg_out,
;                            unsigned *max_paras)
;
; int 21h AH=48h.  Returns 0 and sets *seg_out on success; on failure returns
; the DOS error code and sets *max_paras to the largest block available.
;
; Calling this with paras = 0xFFFF is the standard way to ask "how much have
; you got?": it always fails with error 8 and reports the answer in BX.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _dos_alloc
_dos_alloc:
        push    bp
        mov     bp,sp
        push    si
        push    di
        push    ds
        push    es

        mov     bx,[bp+4]               ; paragraphs wanted
        mov     ah,0x48
        push    ds                      ; DOS is documented to preserve DS, but
        int     0x21                    ;  we are about to store through a
        pop     ds                      ;  DGROUP pointer, so do not rely on it
                                        ;  (POP does not disturb the carry flag)
        jc      .failed

        mov     si,[bp+6]               ; *seg_out = segment
        mov     [si],ax
        mov     si,[bp+8]               ; *max_paras = what we got
        mov     [si],bx
        sub     ax,ax                   ; return 0 == success
        jmp     short .out

.failed:
        mov     si,[bp+8]               ; *max_paras = largest available
        mov     [si],bx
        mov     si,[bp+6]               ; *seg_out = 0
        mov     word [si],0
                                        ; ax already holds the DOS error code
.out:
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; unsigned __cdecl dos_free(unsigned seg)
;
; int 21h AH=49h.  Returns 0 on success, else the DOS error code.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _dos_free
_dos_free:
        push    bp
        mov     bp,sp
        push    si
        push    di
        push    ds
        push    es

        mov     es,[bp+4]
        mov     ah,0x49
        int     0x21
        jc      .out
        sub     ax,ax
.out:
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; unsigned __cdecl dos_setblock(unsigned seg, unsigned paras,
;                               unsigned *max_paras)
;
; int 21h AH=4Ah -- resize an existing block.  This is the call DESIGN.md
; section 3 plans to use to shrink our own allocation and free the top of
; memory; here it exists so we can hand back the slack after the run.  Returns
; 0 on success, else the DOS error code with *max_paras set to the largest size
; the block could have been.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _dos_setblock
_dos_setblock:
        push    bp
        mov     bp,sp
        push    si
        push    di
        push    ds
        push    es

        mov     es,[bp+4]               ; block segment
        mov     bx,[bp+6]               ; new size in paragraphs
        mov     ah,0x4a
        push    ds                      ; as in dos_alloc: we store through a
        int     0x21                    ;  DGROUP pointer afterwards
        pop     ds
        jc      .failed
        mov     si,[bp+8]
        mov     [si],bx
        sub     ax,ax
        jmp     short .out
.failed:
        mov     si,[bp+8]               ; largest size available
        mov     [si],bx
.out:
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; Far peek and poke.
;
; Used for two things: walking the DOS memory control block chain, and reading
; a video page back from its physical address after writing it through the
; B8000 window.  That second use is the whole point -- it is what turns "the
; CPU page register aliases main memory into B8000" from an assumption into a
; measurement.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;

;-- unsigned char __cdecl peek_byte(unsigned seg, unsigned off) --------------;
        global  _peek_byte
_peek_byte:
        push    bp
        mov     bp,sp
        push    ds

        mov     ax,[bp+4]               ; seg
        mov     bx,[bp+6]               ; off
        mov     ds,ax
        mov     al,[bx]
        sub     ah,ah

        pop     ds
        pop     bp
        ret

;-- unsigned __cdecl peek_word(unsigned seg, unsigned off) ------------------;
        global  _peek_word
_peek_word:
        push    bp
        mov     bp,sp
        push    ds

        mov     ax,[bp+4]
        mov     bx,[bp+6]
        mov     ds,ax
        mov     ax,[bx]

        pop     ds
        pop     bp
        ret

;-- void __cdecl poke_byte(unsigned seg, unsigned off, unsigned val) --------;
        global  _poke_byte
_poke_byte:
        push    bp
        mov     bp,sp
        push    ds

        mov     ax,[bp+4]
        mov     bx,[bp+6]
        mov     cx,[bp+8]
        mov     ds,ax
        mov     [bx],cl

        pop     ds
        pop     bp
        ret

;-- void __cdecl poke_word(unsigned seg, unsigned off, unsigned val) --------;
        global  _poke_word
_poke_word:
        push    bp
        mov     bp,sp
        push    ds

        mov     ax,[bp+4]
        mov     bx,[bp+6]
        mov     cx,[bp+8]
        mov     ds,ax
        mov     [bx],cx

        pop     ds
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; unsigned __cdecl data_seg(void)
;
; Small / compact model: near pointers are offsets from DS.  Sprite data in
; DGROUP is passed to blit_mask_m8 / blit_rle_m8 as (data_seg(), offset).
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _data_seg
_data_seg:
        mov     ax,ds
        ret

