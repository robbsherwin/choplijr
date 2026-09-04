;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; pcjrvid.asm -- PCjr mode, page register, palette and retrace.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
;
; Choplifter! for the IBM PCjr, milestone M1.  DESIGN.md sections 3 and 5.
;
; Borrowed, with thanks, from Pungas de Villa Martelli's Flashparty 2018 demo
; (lib/repos/pcjr-flashparty-2018/, code by riq):
;
;   - the retrace waits are utils.asm:22 and :38, reworked into callable
;     routines with the port number as an argument-free constant;
;   - the mode-8 plus page-register sequence is part1/part1.asm:56-62;
;   - the page register bit layout is part3/part3.asm:1111 onwards;
;   - the palette write sequence is part1/part1.asm:478 (gfx_init) and
;     part3/part3.asm:707 (change_palette).
;
; All routines are __cdecl and preserve BP, SI, DI, DS, ES and the direction
; flag.  That is stricter than any of Watcom's conventions require; it costs a
; few pushes and removes a category of untestable failure.
;
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;

        bits    16
        cpu     8086

STATUS_PORT     equ     0x03da          ; display status; also the palette
                                        ;  address/data register
PAGE_PORT       equ     0x03df          ; CRT / Processor Page Register
PAL_FIRST_REG   equ     0x10            ; palette register 0 is index 0x10

        segment _TEXT class=CODE align=2

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; unsigned __cdecl vid_get_mode(void)
;
; int 10h AH=0Fh.  Returns the current BIOS mode number so we can put it back.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _vid_get_mode
_vid_get_mode:
        push    bp
        push    si
        push    di
        push    ds
        push    es

        mov     ah,0x0f
        int     0x10                    ; al = mode, ah = columns, bh = page
        mov     ah,0
                                        ; ah cleared: we want just the mode

        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl vid_set_mode(unsigned mode)
;
; int 10h AX=00mm.  Clears the screen, resets the palette registers to the
; BIOS defaults, and resets both page fields -- which is exactly what we want
; on the way out.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _vid_set_mode
_vid_set_mode:
        push    bp
        mov     bp,sp
        push    si
        push    di
        push    ds
        push    es

        mov     ax,[bp+4]
        mov     ah,0                    ; AH=00h: set mode, AL = mode number
        int     0x10

        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; unsigned __cdecl vid_get_pages(void)
;
; int 10h AH=05h AL=80h -- read the CRT and CPU page registers.
;
; BH comes back as the CRT page and BL as the CPU page.  This is the opposite
; assignment from the one DESIGN.md section 3 gave, and the evidence for it is
; part3/part3.asm:1113-1123, which reads the pair back with AL=80h and then
; shifts BL into bits 3-5 of the page register -- the CPU page field -- while
; OR-ing BH into bits 0-2, the CRT page field.  That code runs on real
; hardware.  m1.c proves it independently by writing distinct signatures
; through the B8000 window at two CPU page settings and reading them back from
; the physical addresses.
;
; Returns (crt << 8) | cpu.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _vid_get_pages
_vid_get_pages:
        push    bp
        push    si
        push    di
        push    ds
        push    es

        mov     ax,0x0580
        sub     bx,bx
        int     0x10                    ; bh = CRT page, bl = CPU page
        mov     ax,bx                   ; already (crt << 8) | cpu

        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl vid_set_pages_bios(unsigned crt_page, unsigned cpu_page)
;
; int 10h AH=05h AL=83h -- set both page registers.  BIOS works out the
; address-mode bits for the current mode and keeps its own video variables
; consistent, at the cost of a BIOS call inside the retrace window.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _vid_set_pages_bios
_vid_set_pages_bios:
        push    bp
        mov     bp,sp
        push    si
        push    di
        push    ds
        push    es

        mov     bh,[bp+4]               ; crt_page  -> BH
        mov     bl,[bp+6]               ; cpu_page  -> BL
        and     bh,7
        and     bl,7
        mov     ax,0x0583
        int     0x10

        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl vid_set_pages_port(unsigned crt_page, unsigned cpu_page,
;                                 unsigned addr_mode)
;
; Raw write to the CRT / Processor Page Register:
;
;   bits 0-2  CRT page -- which 16 KB block the CRTC displays
;   bits 3-5  CPU page -- which 16 KB block appears at B800:0000
;   bits 6-7  video address mode (1 = 16 KB graphics, which mode 8 is)
;
; This is the whole of page_flip: one OUT, no buffer copy.  Port 0x3DF is
; write-only, so the caller has to supply bits 6-7 rather than preserving
; whatever BIOS left there -- which is the reason vid_set_pages_bios exists
; alongside this.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _vid_set_pages_port
_vid_set_pages_port:
        push    bp
        mov     bp,sp

        mov     al,[bp+8]               ; addr_mode
        and     al,3
        mov     cl,6
        shl     al,cl                   ; -> bits 7-6
        mov     ah,[bp+6]               ; cpu_page
        and     ah,7
        mov     cl,3
        shl     ah,cl                   ; -> bits 5-3
        or      al,ah
        mov     ah,[bp+4]               ; crt_page
        and     ah,7                    ; -> bits 2-0
        or      al,ah

        mov     dx,PAGE_PORT
        out     dx,al

        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl vid_wait_retrace(void)
;
; Returns with vertical retrace just beginning, which is the moment to flip.
; If we are already inside retrace we wait it out first, so that back-to-back
; calls cannot both return inside the same blanking interval -- otherwise a
; frame counter built on this would run at whatever rate the loop happens to
; poll at.  (utils.asm:22, WAIT_VERTICAL_RETRACE.)
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _vid_wait_retrace
_vid_wait_retrace:
        mov     dx,STATUS_PORT
.wait_end:
        in      al,dx                   ; wait for any retrace in progress
        test    al,8                    ;  to finish
        jnz     .wait_end
.wait_start:
        in      al,dx                   ; wait for the next one to start
        test    al,8
        jz      .wait_start
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl vid_wait_hretrace(void)
;
; Horizontal equivalent, for palette writes that must not be seen.
; (utils.asm:38, WAIT_HORIZONTAL_RETRACE.)
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _vid_wait_hretrace
_vid_wait_hretrace:
        mov     dx,STATUS_PORT
.wait_end:
        in      al,dx
        ror     al,1                    ; bit 0 -> carry: display enable
        jc      .wait_end
.wait_start:
        in      al,dx
        ror     al,1
        jnc     .wait_start
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl vid_set_palette(unsigned index, unsigned colour)
;
; Writes one of the sixteen palette registers.  Port 0x3DA is a two-state
; register: reading it forces the address/data flip-flop back to "address", the
; first write selects a register, the second supplies its value.  Leaving the
; flip-flop in the data state would corrupt the next access, hence the closing
; read.  (gfx_init, part1/part1.asm:481-497.)
;
; The caller is responsible for being inside a blanking interval.  Sixteen
; registers is about 48 OUTs, which fits comfortably; DESIGN.md section 5
; treats whole-palette rewrites as effectively free for exactly this reason.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _vid_set_palette
_vid_set_palette:
        push    bp
        mov     bp,sp

        mov     dx,STATUS_PORT
        in      al,dx                   ; flip-flop -> address state

        mov     al,[bp+4]               ; index
        and     al,0x0f
        or      al,PAL_FIRST_REG        ; palette registers start at 0x10
        out     dx,al                   ; select

        mov     al,[bp+6]               ; colour
        and     al,0x0f
        out     dx,al                   ; set

        sub     al,al
        out     dx,al                   ; leave a benign register selected
        in      al,dx                   ; flip-flop -> address state

        pop     bp
        ret

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; void __cdecl vid_set_palette16(const unsigned char *pal16)
;
; All sixteen registers from a near pointer (small/compact model, so DS
; already addresses it).  Waits for vertical retrace once, up front, rather
; than per register.
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
        global  _vid_set_palette16
_vid_set_palette16:
        push    bp
        mov     bp,sp
        push    si

        call    _vid_wait_retrace

        mov     si,[bp+4]
        mov     dx,STATUS_PORT
        in      al,dx                   ; flip-flop -> address state
        mov     bl,PAL_FIRST_REG
        mov     cx,16
.next:
        mov     al,bl
        out     dx,al                   ; select register bl
        mov     al,[si]
        inc     si
        and     al,0x0f
        out     dx,al                   ; set its colour
        inc     bl
        loop    .next

        sub     al,al
        out     dx,al                   ; leave a benign register selected
        in      al,dx                   ; flip-flop -> address state

        pop     si
        pop     bp
        ret
