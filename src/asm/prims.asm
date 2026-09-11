;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; prims.asm -- fill and copy primitives.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
;
; Choplifter! for the IBM PCjr, milestone M1.  DESIGN.md sections 2, 6 and 7.
;
; These are the routines the frame budget is made of, and the numbers in
; section 2's throughput table are claims about precisely these instruction
; sequences.  M1 measures them.  Keep the inner loops bare: a REP STOSW with
; nothing around it is the fastest a 8088 can move bytes, and any cleverness
; added here would be measuring something other than the machine.
;
; The PCjr's Video Gate Array shares main memory with the processor, so RAM
; accesses average two wait states and a bus cycle takes six clocks instead of
; four.  That applies to instruction fetch as much as to data, which is why
; short loops matter and why the string instructions -- one fetch, many
; transfers -- win by so much here.
;
; All routines are __cdecl and preserve BP, SI, DI, DS, ES and the direction
; flag.
;
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;

        bits    16
        cpu     8086

M8_ROW_BYTES    equ     80
M8_BANK_STRIDE  equ     0x2000

        segment _TEXT class=CODE align=2

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl fill_words(unsigned dseg, unsigned doff, unsigned nwords,
;                         unsigned pattern)
;
; The REP STOSW path from section 2's table.  A flat run, no row arithmetic:
; this is the machine's raw fill ceiling.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _fill_words
_fill_words:
        push    bp
        mov     bp,sp
        push    di
        push    es
        cld

        mov     es,[bp+4]               ; dseg
        mov     di,[bp+6]               ; doff
        mov     cx,[bp+8]               ; nwords
        mov     ax,[bp+10]              ; pattern
        rep     stosw

        pop     es
        pop     di
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl fill_bytes(unsigned dseg, unsigned doff, unsigned nbytes,
;                         unsigned pattern)
;
; REP STOSB.  Not used by the renderer -- included because the byte/word gap is
; the clearest single demonstration of where the bus, rather than the CPU, is
; the limit.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _fill_bytes
_fill_bytes:
        push    bp
        mov     bp,sp
        push    di
        push    es
        cld

        mov     es,[bp+4]               ; dseg
        mov     di,[bp+6]               ; doff
        mov     cx,[bp+8]               ; nbytes
        mov     ax,[bp+10]              ; pattern (low byte used)
        rep     stosb

        pop     es
        pop     di
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl copy_words(unsigned dseg, unsigned doff,
;                         unsigned sseg, unsigned soff, unsigned nwords)
;
; The REP MOVSW path.  Note that BP-relative reads use SS by default, so
; loading DS does not disturb the argument fetches -- but we load it last
; anyway, because relying on that would be a trap for whoever edits this next.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _copy_words
_copy_words:
        push    bp
        mov     bp,sp
        push    di
        push    si
        push    ds
        push    es
        cld

        mov     es,[bp+4]               ; dseg
        mov     di,[bp+6]               ; doff
        mov     si,[bp+10]              ; soff
        mov     cx,[bp+12]              ; nwords
        mov     ds,[bp+8]               ; sseg -- last
        rep     movsw

        pop     es
        pop     ds
        pop     si
        pop     di
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl copy_bytes(unsigned dseg, unsigned doff,
;                         unsigned sseg, unsigned soff, unsigned nbytes)
;
; The REP MOVSB path.  This one matters: blit_rle's opaque runs (section 6) are
; MOVSB, because a run can start or end on an odd pixel and word alignment is
; not free to arrange.  Section 2 predicts MOVSB at roughly three quarters the
; cost per byte of MOVSW, so the sprite blitter pays for that convenience.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _copy_bytes
_copy_bytes:
        push    bp
        mov     bp,sp
        push    di
        push    si
        push    ds
        push    es
        cld

        mov     es,[bp+4]               ; dseg
        mov     di,[bp+6]               ; doff
        mov     si,[bp+10]              ; soff
        mov     cx,[bp+12]              ; nbytes
        mov     ds,[bp+8]               ; sseg -- last
        rep     movsb

        pop     es
        pop     ds
        pop     si
        pop     di
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl fill_rect_m8(unsigned dseg, unsigned xbyte, unsigned y,
;                           unsigned wbytes, unsigned rows, unsigned pattern)
;
; A mode 8 rectangle, so it pays the two-bank row arithmetic that fill_words
; does not:
;
;   row_offset(y) = (y & 1) * 0x2000 + (y >> 1) * 80
;
; Multiplying per row would cost more than the fill: MUL on an 8088 is around
; 120 clocks, and 200 rows of that is milliseconds of pure overhead.  So the
; offset is computed once and then walked -- alternate between the two banks,
; and advance the shared base by one row-stride after each odd scanline.
;
; Comparing this against fill_words over the same byte count is the useful
; measurement, because the difference is the per-row cost that section 7's
; dirty-rect budget assumes away.
;
; Caller guarantees: wbytes even, xbyte + wbytes <= 80, y + rows <= 200.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _fill_rect_m8
_fill_rect_m8:
        push    bp
        mov     bp,sp
        push    di
        push    si
        push    es
        cld

        mov     es,[bp+4]               ; dseg
        mov     ax,[bp+8]               ; y
        mov     di,ax                   ; stash y; MUL is about to eat DX
        shr     ax,1                    ; y >> 1, the row-pair index
        mov     bx,M8_ROW_BYTES
        mul     bx                      ; dx:ax = (y >> 1) * 80, dx = 0
        add     ax,[bp+6]               ; + xbyte
        mov     bx,ax                   ; bx = bank-relative base of this pair

        mov     si,[bp+10]              ; wbytes
        shr     si,1                    ; -> words per row
        mov     cx,[bp+12]              ; rows
        jcxz    .done
        mov     ax,[bp+14]              ; pattern

        test    di,1                    ; does the rectangle start on an odd
        jnz     .start_odd              ;  scanline, i.e. in the second bank?
        mov     dx,cx                   ; dx = rows remaining
        jmp     short .even_row
.start_odd:
        mov     dx,cx
        jmp     short .odd_row

.even_row:
        mov     di,bx                   ; first bank
        mov     cx,si
        rep     stosw
        dec     dx
        jz      .done
.odd_row:
        lea     di,[bx+M8_BANK_STRIDE]  ; second bank, same row-pair index
        mov     cx,si
        rep     stosw
        add     bx,M8_ROW_BYTES         ; both banks done: next row pair
        dec     dx
        jnz     .even_row

.done:
        pop     es
        pop     si
        pop     di
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl fill_band_m8(unsigned dseg, unsigned y, unsigned rows,
;                           unsigned pattern)
;
; Full-width horizontal band: the sky and ground primitive.  This is the one
; that makes DESIGN.md section 7 work, because a band like this is
; scroll-invariant -- move the camera and it does not change at all.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _fill_band_m8
_fill_band_m8:
        push    bp
        mov     bp,sp

        ; __cdecl: push right to left.  No PUSH immediate on an 8088, hence the
        ; register shuffle.
        push    word [bp+10]            ; pattern
        push    word [bp+8]             ; rows
        mov     ax,M8_ROW_BYTES
        push    ax                      ; wbytes = full 80-byte width
        push    word [bp+6]             ; y
        sub     ax,ax
        push    ax                      ; xbyte = 0
        push    word [bp+4]             ; dseg
        call    _fill_rect_m8
        add     sp,12

        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl fill_rows_m8(unsigned dseg, unsigned xbyte, unsigned y,
;                           unsigned wbytes, unsigned rows,
;                           const unsigned *patterns);
;
; Like fill_rect_m8, but each scanline takes the next word from `patterns`
; (DGROUP near pointer).  The sky dither is one repeating word per row;
; calling fill_rect_m8 once per row paid a MUL and a far call every scanline.
; wbytes must be even.  Caller guarantees the rect fits the screen.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _fill_rows_m8
_fill_rows_m8:
        push    bp
        mov     bp,sp
        push    di
        push    si
        push    es
        cld

        mov     es,[bp+4]               ; dseg
        mov     ax,[bp+8]               ; y
        mov     di,ax
        shr     ax,1
        mov     bx,M8_ROW_BYTES
        mul     bx
        add     ax,[bp+6]               ; + xbyte
        mov     bx,ax                   ; pair base

        mov     cx,[bp+12]              ; rows
        jcxz    .fr_done
        mov     si,[bp+14]              ; patterns
        mov     dx,cx                   ; rows remaining

        test    di,1
        jnz     .fr_odd

.fr_even:
        lodsw                           ; pattern for this scanline
        mov     di,bx
        mov     cx,[bp+10]
        shr     cx,1
        rep     stosw
        dec     dx
        jz      .fr_done
.fr_odd:
        lodsw
        lea     di,[bx+M8_BANK_STRIDE]
        mov     cx,[bp+10]
        shr     cx,1
        rep     stosw
        add     bx,M8_ROW_BYTES
        dec     dx
        jnz     .fr_even

.fr_done:
        pop     es
        pop     si
        pop     di
        pop     bp
        ret
