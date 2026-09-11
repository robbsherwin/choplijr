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
; unsigned __cdecl blit_rle_m8(unsigned dseg, unsigned xbyte, unsigned y,
;                              unsigned sseg, unsigned soff)
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
; Returns the total run_bytes (opaque + mixed) copied -- what m10.c used to
; get by having blit_at call rle_run_bytes() to re-walk this same stream a
; second time in C, purely for the WORKSET/ZTIMER byte counts (measured at
; ~19% of one present's cost; see CLAUDE-THOUGHTS.md).  Every general
; register here is already committed (ES/DS/SI/DI/BX/DX/AX/CX), so the
; running total lives in a local stack slot at [bp-2] instead, shared with
; .row across calls since .row is a plain near CALL, not its own frame.
;
; Caller guarantees: the sprite's byte span fits the 160-pixel width,
; y+height fits 200, height != 0.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _blit_rle_m8
_blit_rle_m8:
        push    bp
        mov     bp,sp
        sub     sp,2                    ; local: running byte total, [bp-2]
        push    di
        push    si
        push    ds
        push    es
        cld

        mov     word [bp-2],0

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
        mov     ax,[bp-2]               ; return value: total bytes copied
        pop     es
        pop     ds
        pop     si
        pop     di
        add     sp,2
        pop     bp
        ret

; One RLE row: DS:SI command stream, ES:DI dest.  BX pair base, DX rows left.
; [bp-2] (outer frame, still valid: .row is a plain near CALL) accumulates
; the running byte total across every row of this blit.
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
        add     [bp-2],cx               ; count run bytes, opaque or mixed
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

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; unsigned __cdecl blit_rle_m8_fire(unsigned dseg, unsigned xbyte, unsigned y,
;                                   unsigned sseg, unsigned soff)
;
; Same RLE format and row walk as blit_rle_m8 above.  m10.c's fire remap
; (fire_byte/fire_nibble, called only with blit_fire == 1) only ever recolours
; a nibble that is already 0x0E to another nonzero value -- it never makes an
; opaque nibble transparent or a transparent one opaque -- so the skip /
; opaque-run / mixed-nibble structure of a row is identical to the plain
; blitter; only the byte value written differs.  That means an opaque run can
; still be a single loop instead of a per-byte C call into blit_rle_clip /
; plot_m8_byte (peek_byte/poke_byte far calls plus a real DIV in fire_nibble's
; %3, redone from scratch every byte) -- it just can't be REP MOVSB, since
; each byte needs an XLATB first.  fire_tab below is fixed for blit_fire == 1,
; the only value m10.c ever calls a fire blit with.
;
; Returns the total run_bytes (opaque + mixed) copied, same [bp-2] local-slot
; technique as blit_rle_m8 -- see that routine's comment.
;
; Caller guarantees: the sprite's byte span fits the 160-pixel width,
; y+height fits 200, height != 0.  Same as blit_rle_m8; not a clipped
; blitter -- callers still fall back to blit_rle_clip near a screen edge.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _blit_rle_m8_fire
_blit_rle_m8_fire:
        push    bp
        mov     bp,sp
        sub     sp,2                    ; local: running byte total, [bp-2]
        push    di
        push    si
        push    ds
        push    es
        cld

        mov     word [bp-2],0

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
        mov     ax,[bp-2]               ; return value: total bytes copied
        pop     es
        pop     ds
        pop     si
        pop     di
        add     sp,2
        pop     bp
        ret

; One RLE row, fire-remapped.  DS:SI command stream, ES:DI dest.  BX is the
; outer loop's pair base on entry -- saved and restored around this row's use
; of BX as the fire_tab base for CS XLATB.  [bp-2] (outer frame) accumulates
; the running byte total across every row of this blit.
.row:
        push    dx
        push    bx
        mov     bx,fire_tab
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
        add     [bp-2],cx               ; count run bytes, opaque or mixed
        cmp     cx,1
        je      .one
.run:
        lodsb
        cs      xlatb
        stosb
        loop    .run                    ; opaque run, remapped byte by byte
        jmp     short .next_cmd
.one:
        lodsb
        cs      xlatb
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
        pop     bx
        pop     dx
        ret

; fire_byte(src) for m10.c's fire_nibble/fire_byte with blit_fire fixed at 1
; (the only value it is ever called with): a 0x0E nibble becomes 4, 0Ch or
; 0Eh by (src+1)%3 / (src+2)%3; every other nibble passes through unchanged.
; Generated from the C source (script in CLAUDE-THOUGHTS.md's fire-remap
; entry), not hand-derived, and checked against fire_byte() for all 256
; inputs before being transcribed here.
fire_tab:
        db      0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0C,0x0F        ; 00h-0Fh
        db      0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F        ; 10h-1Fh
        db      0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x24,0x2F        ; 20h-2Fh
        db      0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3C,0x3F        ; 30h-3Fh
        db      0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F        ; 40h-4Fh
        db      0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x5B,0x5C,0x5D,0x54,0x5F        ; 50h-5Fh
        db      0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6A,0x6B,0x6C,0x6D,0x6C,0x6F        ; 60h-6Fh
        db      0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x7B,0x7C,0x7D,0x7E,0x7F        ; 70h-7Fh
        db      0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x84,0x8F        ; 80h-8Fh
        db      0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9C,0x9F        ; 90h-9Fh
        db      0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF        ; A0h-AFh
        db      0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xB4,0xBF        ; B0h-BFh
        db      0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCC,0xCF        ; C0h-CFh
        db      0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF        ; D0h-DFh
        db      0x40,0xC1,0xE2,0x43,0xC4,0xE5,0x46,0xC7,0xE8,0x49,0xCA,0xEB,0x4C,0xCD,0xE4,0x4F        ; E0h-EFh
        db      0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFC,0xFF        ; F0h-FFh

