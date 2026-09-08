;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; blit.asm -- packed mode-8 masked blit and RLE sprite blit.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
;
; Choplifter! for the IBM PCjr.  DESIGN.md section 6.
;
; blit_mask_m8 (M2) draws a packed 4bpp sprite (two pixels per byte, left pixel
; in the high nibble).  Index 0 is transparent.  X is a byte column.  Odd pixel
; X is a second pre-shifted copy (leading transparent nibble) still drawn at
; xbyte = x_px / 2.
;
; blit_rle_m8 (M3) draws the product format: per-row skip/run commands, REP
; MOVSB for opaque runs, RMW for a one-byte mixed-nibble edge.  It never copies
; a whole screen -- only the opaque bytes of one sprite.  RAM->video MOVSB is
; 4515 ns/byte (72.2 ms/screen) on hardware; a full-screen copy does not fit.
;
; Row address is the same walk as fill_rect_m8: even scanlines in the first
; 8 KB, odd scanlines in the second, base advanced by 80 after each odd row.
; Source is linear (packed or RLE), not banked.
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
; void __cdecl blit_mask_m8(unsigned dseg, unsigned xbyte, unsigned y,
;                           unsigned wbytes, unsigned rows,
;                           unsigned sseg, unsigned soff)
;
; Caller guarantees: wbytes * 2 + xbyte*2 fits the 160-pixel width, y+rows
; fits 200, wbytes != 0.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _blit_mask_m8
_blit_mask_m8:
        push    bp
        mov     bp,sp
        push    di
        push    si
        push    ds
        push    es
        cld

        mov     es,[bp+4]               ; dseg
        mov     ax,[bp+8]               ; y
        mov     di,ax
        shr     ax,1
        mov     bx,M8_ROW_BYTES
        mul     bx                      ; (y >> 1) * 80
        add     ax,[bp+6]               ; + xbyte
        mov     bx,ax                   ; pair base

        mov     dx,[bp+12]              ; rows
        or      dx,dx
        jz      .done
        mov     si,[bp+16]              ; soff
        mov     ds,[bp+14]              ; sseg -- last

        test    di,1
        jnz     .start_odd
        jmp     short .even_row
.start_odd:
        jmp     short .odd_row

.even_row:
        mov     di,bx
        call    .row
        dec     dx
        jz      .done
.odd_row:
        lea     di,[bx+M8_BANK_STRIDE]
        call    .row
        add     bx,M8_ROW_BYTES
        dec     dx
        jnz     .even_row

.done:
        pop     es
        pop     ds
        pop     si
        pop     di
        pop     bp
        ret

; One packed row: DS:SI source, ES:DI dest, [bp+10] = wbytes.
; Transparent 00h skips the write.  A mixed byte (one nibble zero) is a
; read-modify-write on that dest byte; both nibbles set is a straight store.
.row:
        push    cx
        push    dx
        mov     cx,[bp+10]
.pix:
        lodsb
        test    al,al
        jz      .trans
        mov     ah,al
        and     ah,0x0f
        jz      .hi_only
        mov     ah,al
        and     ah,0xf0
        jz      .lo_only
        stosb
        loop    .pix
        jmp     short .row_done
.hi_only:
        mov     ah,[es:di]
        and     ah,0x0f
        and     al,0xf0
        or      al,ah
        stosb
        loop    .pix
        jmp     short .row_done
.lo_only:
        mov     ah,[es:di]
        and     ah,0xf0
        and     al,0x0f
        or      al,ah
        stosb
        loop    .pix
        jmp     short .row_done
.trans:
        inc     di
        loop    .pix
.row_done:
        pop     dx
        pop     cx
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl blit_rle_m8(unsigned dseg, unsigned xbyte, unsigned y,
;                          unsigned sseg, unsigned soff)
;
; soff -> { width_px, height_px, RLE rows }.  Each row is
;
;   { skip_bytes, run_bytes, data[run_bytes] }*  0x00, 0x00
;
; skip_bytes advances ES:DI (transparent).  run_bytes >= 2 is a fully opaque
; run (REP MOVSB).  run_bytes == 1 is one byte: both nibbles live is a store,
; a mixed nibble is a dest RMW, 00h is a skip.  0x00 0x00 ends the row;
; trailing transparency is implicit.
;
; Caller guarantees: the sprite's byte span fits the 160-pixel width,
; y+height fits 200, height != 0.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _blit_rle_m8
_blit_rle_m8:
        push    bp
        mov     bp,sp
        push    di
        push    si
        push    ds
        push    es
        cld

        mov     es,[bp+4]               ; dseg
        mov     ax,[bp+8]               ; y
        mov     di,ax
        shr     ax,1
        mov     bx,M8_ROW_BYTES
        mul     bx                      ; (y >> 1) * 80
        add     ax,[bp+6]               ; + xbyte
        mov     bx,ax                   ; pair base

        mov     si,[bp+12]              ; soff
        mov     ds,[bp+10]              ; sseg -- last

        lodsb                           ; width_px
        lodsb                           ; height_px
        xor     ah,ah
        mov     dx,ax                   ; rows
        or      dx,dx
        jz      .done

        test    di,1
        jnz     .start_odd
        jmp     short .even_row
.start_odd:
        jmp     short .odd_row

.even_row:
        mov     di,bx
        call    .row
        dec     dx
        jz      .done
.odd_row:
        lea     di,[bx+M8_BANK_STRIDE]
        call    .row
        add     bx,M8_ROW_BYTES
        dec     dx
        jnz     .even_row

.done:
        pop     es
        pop     ds
        pop     si
        pop     di
        pop     bp
        ret

; One RLE row: DS:SI command stream, ES:DI dest.  BX pair base, DX rows left.
.row:
        push    dx
.next_cmd:
        lodsb                           ; skip
        mov     ah,al
        lodsb                           ; run
        mov     cl,al
        or      al,ah
        jz      .row_done               ; skip==0 && run==0
        xor     ch,ch
        mov     al,ah
        xor     ah,ah
        add     di,ax                   ; transparent dest bytes
        jcxz    .next_cmd
        cmp     cx,1
        je      .one
        rep     movsb                   ; opaque run, both nibbles live
        jmp     short .next_cmd
.one:
        lodsb
        test    al,al
        jz      .trans
        mov     ah,al
        and     ah,0x0f
        jz      .hi_only
        mov     ah,al
        and     ah,0xf0
        jz      .lo_only
        stosb
        jmp     short .next_cmd
.hi_only:
        mov     ah,[es:di]
        and     ah,0x0f
        and     al,0xf0
        or      al,ah
        stosb
        jmp     short .next_cmd
.lo_only:
        mov     ah,[es:di]
        and     ah,0xf0
        and     al,0x0f
        or      al,ah
        stosb
        jmp     short .next_cmd
.trans:
        inc     di
        jmp     short .next_cmd
.row_done:
        pop     dx
        ret
