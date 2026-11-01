; BananaNES stress ROM - realistic NES scenes on an MMC3 board (mapper 4). GPL-3.0-or-later. ca65.
; Scenes (1500 frames each, 7 scenes = one ~175 s loop): see README.md.
.p02
.include "data.inc"
.ifndef LEVEL
LEVEL = 5
.endif

PPUCTRL = $2000
PPUMASK = $2001
PPUSTATUS = $2002
OAMADDR = $2003
PPUSCROLL = $2005
PPUADDR = $2006
PPUDATA = $2007
OAMDMA = $4014
APU_STATUS = $4015
APU_FRAME = $4017
M3_SEL = $8000
M3_DAT = $8001
M3_MIRR = $A000
M3_WRAM = $A001
M3_LATCH = $C000
M3_RELOAD = $C001
M3_DIS = $E000
M3_EN = $E001
SRAM = $6000
PHASE_FRAMES = 1500
NPHASE = 9
P3_BUL_END = .min(LEVEL + 3 + LEVEL * 5, 56 - 16)
OAMBUF = $0200
PROBE = $07E0

.segment "HEADER"
  .byte "NES", $1A
  .byte 4              ; 4 x 16 KiB PRG = 64 KiB
  .byte 8              ; 8 x 8 KiB CHR = 64 KiB
  .byte $42            ; mapper 4 low nibble, battery
  .byte $00
  .byte 1              ; 8 KiB PRG-RAM
  .res 7, 0

.segment "ZEROPAGE"
frame:      .res 2
cam:        .res 2
nmi_flag:   .res 1
phase:      .res 1
ptimer:     .res 2
scx:        .res 2        ; camera x (world px)
scy:        .res 1
rng:        .res 2
hash:       .res 2
lagcnt:     .res 2
oamstart:   .res 1
oamidx:     .res 1
sprcnt:     .res 1
vlen:       .res 1
render_on:  .res 1
ppuctrl_b:  .res 1        ; base PPUCTRL without the nametable bit (sprite size etc.)
use_s0:     .res 1
bsel:       .res 1
irqidx:     .res 1
nbands:     .res 1
pal_dirty:  .res 1
pal_ptr:    .res 2
s0wait:     .res 2
snd_tick:   .res 1
snd_row:    .res 1
snd_mask:   .res 1
snd_speed:  .res 1
snd_vol1:   .res 1
snd_vol2:   .res 1
snd_nvol:   .res 1
snd_per1:   .res 2
snd_t:      .res 1
lastcol:    .res 2
nent:       .res 1
chr_r:      .res 6        ; shadows of R0..R5
prg6:       .res 1
t0:         .res 1
t1:         .res 1
t2:         .res 1
t3:         .res 1
t4:         .res 1
t5:         .res 1
t6:         .res 1
t7:         .res 1
ptr:        .res 2
ptr2:       .res 2
spx:        .res 1
spy:        .res 1
spt:        .res 1
spa:        .res 1
hudtick:    .res 1
score:      .res 3
gstate:     .res 1
dmc_idx:    .res 1
sub:        .res 1
wave_amp:   .res 1
txtpos:     .res 1
irq_mode:   .res 1
vpos:       .res 2
poll_spin:  .res 1
col_rows:   .res 1
attr_end:   .res 1
dbg:        .res 1

.segment "OAM"
oam:        .res 256

.segment "BSS"
MAXENT = 56
ent_xf:     .res MAXENT
ent_xl:     .res MAXENT
ent_xh:     .res MAXENT
ent_yf:     .res MAXENT
ent_y:      .res MAXENT
ent_vxf:    .res MAXENT
ent_vxi:    .res MAXENT
ent_vyf:    .res MAXENT
ent_vyi:    .res MAXENT
ent_type:   .res MAXENT
ent_spr:    .res MAXENT   ; sprite frame (creature) index 0..15 in its bank + bank selector bits
ent_attr:   .res MAXENT
ent_aux:    .res MAXENT
gh:         .res 64       ; ground height ring (px) per column
vbuf:       .res 256
colbuf:     .res 32
band_x:     .res 40
band_ctrl:  .res 40
band_chr:   .res 40
band_len:   .res 40
band_yl:    .res 40

; ======================================================================= CODE
.segment "CODE"

.macro M3 reg, val
  lda #reg
  sta bsel
  sta M3_SEL
  lda val
  sta M3_DAT
.endmacro

; --- constants ---
T_WALK = 0
T_FLY = 1
T_BULLET = 2
T_PART = 3
T_NPC = 4
T_ENEMY = 5
T_DIVE = 6

reset:
  sei
  cld
  ldx #$40
  stx APU_FRAME
  ldx #$FF
  txs
  inx
  stx PPUCTRL
  stx PPUMASK
  stx $4010
  stx APU_STATUS
: bit PPUSTATUS
  bpl :-
  ; clear RAM
  lda #0
  tax
: sta $00,x
  sta $0100,x
  sta $0300,x
  sta $0400,x
  sta $0500,x
  sta $0600,x
  sta $0700,x
  inx
  bne :-
  lda #$F8
: sta oam,x
  inx
  bne :-
: bit PPUSTATUS
  bpl :-
  ; MMC3: vertical mirroring, WRAM on
  lda #0
  sta M3_MIRR
  lda #$80
  sta M3_WRAM
  sta M3_DIS
  ; signature in SRAM
  ldx #5
: lda sigstr,x
  sta SRAM,x
  dex
  bpl :-
  ; companion probe block at $07E0 (spec 7): "BNPROBE1", frame u32, scene u16, phase u16, checksum u32, 12 x $A5
  ldx #7
: lda probestr,x
  sta PROBE,x
  dex
  bpl :-
  ldx #19
  lda #$A5
: sta PROBE+12,x
  dex
  cpx #7
  bne :-
  lda #$A5
  sta rng
  lda #$5A
  sta rng+1
  lda #1
  sta hash
  lda #0
  sta phase
  jsr phase_init
  lda #$18             ; sprites + bg on, left column shown
  sta PPUMASK
  lda #1
  sta render_on
  lda #$88
  ora ppuctrl_b
  sta PPUCTRL
  cli
main:
  lda nmi_flag
  bne @late
@wait:
  lda poll_spin
  beq @nopoll
  bit PPUSTATUS          ; games that busy-wait on $2002 instead of sleeping until the NMI: a status read every few cycles
  bit PPUSTATUS
@nopoll:
  lda nmi_flag
  beq @wait
  jmp @go
@late:
  inc lagcnt
  bne @go
  inc lagcnt+1
@go:
  lda #0
  sta nmi_flag
  jsr phase_update
  jsr snd_update
  ; fold game state into the checksum
  lda hash
  asl
  rol hash+1
  bcc :+
  ora #1
: eor scx
  eor rng
  eor oam+4
  eor sprcnt
  eor s0wait
  sta hash
  lda hash+1
  eor frame
  eor ent_xl
  sta hash+1
  ; probe block: frame counter, scene/phase, checksum of the last frame (the running hash)
  lda frame
  sta PROBE+8
  lda frame+1
  sta PROBE+9
  lda phase
  sta PROBE+12
  sta PROBE+14
  lda hash
  sta PROBE+16
  lda hash+1
  sta PROBE+17
  jmp main

sigstr: .byte "BSTRES"
probestr: .byte "BNPROBE1"

; ---------------------------------------------------------------- RNG (16-bit xorshift-ish)
rand:
  lda rng
  asl
  rol rng+1
  bcc :+
  eor #$2D
: sta rng
  eor rng+1
  rts

; ---------------------------------------------------------------- NMI
nmi:
  pha
  txa
  pha
  tya
  pha
  lda bsel
  pha
  lda render_on
  bne @gfx
  jmp @nogfx
@gfx:
  lda #0
  sta OAMADDR
  lda #>oam
  sta OAMDMA
  jsr vbuf_flush
  jsr chr_apply
  ; PPUCTRL / scroll for the top of the screen
  lda scx+1
  and #1
  ora ppuctrl_b
  ora #$88
  sta PPUCTRL
  lda use_s0
  beq @noS0
  ; status bar: scroll 0 until the sprite-0 hit, then the camera
  lda ppuctrl_b
  ora #$88
  sta PPUCTRL
  lda #0
  sta PPUSCROLL
  sta PPUSCROLL
  lda #0
  sta s0wait
  sta s0wait+1
: bit PPUSTATUS
  bvc @hit0               ; flag already clear
  inc s0wait
  bne :-
  inc s0wait+1
  jmp :-
@hit0:
  lda #0
  sta s0wait
  sta s0wait+1
@wh:
  bit PPUSTATUS
  bvs @gotit
  inc s0wait
  bne @wh
  inc s0wait+1
  lda s0wait+1
  cmp #$08
  bcc @wh
  bcs @gotit
@gotit:
  lda scx+1
  and #1
  ora ppuctrl_b
  ora #$88
  sta PPUCTRL
  lda scx
  sta PPUSCROLL
  lda scy
  sta PPUSCROLL
  jmp @gfxdone
@noS0:
  lda scx
  sta PPUSCROLL
  lda scy
  sta PPUSCROLL
@gfxdone:
  ; MMC3 IRQ splits for this frame
  lda nbands
  cmp #2
  bcc @nogfx
  lda #0
  sta irqidx
  sta M3_DIS
  lda band_len
  sta M3_LATCH
  sta M3_RELOAD
  sta M3_EN
@nogfx:
  inc frame
  bne :+
  inc frame+1
: lda #1
  sta nmi_flag
  pla
  sta bsel
  sta M3_SEL
  pla
  tay
  pla
  tax
  pla
  rti

irq:
  pha
  txa
  pha
  sta M3_DIS            ; acknowledge
  ldx irqidx            ; IRQ i starts band i+1
  lda irq_mode
  beq @m1
  ; the four-write mid-frame scroll ($2006 / $2005 / $2005 / $2006), as split-screen games do it: the band's first
  ; line is row band_yl of the nametable
  lda band_ctrl+1,x
  and #1
  asl
  asl
  sta PPUADDR
  lda band_yl+1,x
  sta PPUSCROLL
  lda band_x+1,x
  sta PPUSCROLL
  lda band_yl+1,x
  and #$38
  asl
  asl
  sta t6
  lda band_x+1,x
  lsr
  lsr
  lsr
  ora t6
  sta PPUADDR
  jmp @bank
@m1:
  lda band_ctrl+1,x
  ora #$88
  sta PPUCTRL
  lda band_x+1,x
  sta PPUSCROLL
  lda #0
  sta PPUSCROLL
@bank:
  lda bsel
  pha
  lda #0
  sta M3_SEL
  lda band_chr+1,x
  sta M3_DAT
  pla
  sta M3_SEL
  inx
  stx irqidx
  txa
  clc
  adc #1
  cmp nbands
  bcs @done
  lda band_len,x
  sta M3_LATCH
  sta M3_RELOAD
  sta M3_EN
@done:
  pla
  tax
  pla
  rti

; vbuf: [hi][lo][len | $80 = +32] data..., terminated by hi=0
vbuf_flush:
  ldx #0
  lda vlen
  beq @end
@cmd:
  lda vbuf,x
  beq @end
  sta PPUADDR
  inx
  lda vbuf,x
  sta PPUADDR
  inx
  lda vbuf,x
  inx
  tay
  and #$7F
  sta t0
  tya
  and #$80
  beq :+
  lda #4
: sta t1
  lda ppuctrl_b
  ora #$88
  ora t1
  sta PPUCTRL
  ldy t0
@copy:
  lda vbuf,x
  sta PPUDATA
  inx
  dey
  bne @copy
  cpx vlen
  bcc @cmd
@end:
  lda #0
  sta vlen
  sta vbuf
  rts

; vq_begin: A=hi X=lo Y=len(|$80) -> appends header, X=write index for data (vq_put)
vq_begin:
  sta t5
  stx t6
  ldx vlen
  lda t5
  sta vbuf,x
  inx
  lda t6
  sta vbuf,x
  inx
  tya
  sta vbuf,x
  inx
  rts
; vq_end: X = new length
vq_end:
  stx vlen
  lda #0
  sta vbuf,x
  rts

; chr_apply: R0..R5 from shadows (BG 2 KiB banks at $0000/$0800, sprites 4 x 1 KiB at $1000)
chr_apply:
  ldx #0
: stx M3_SEL
  stx bsel
  lda chr_r,x
  sta M3_DAT
  inx
  cpx #6
  bcc :-
  rts

; PRG bank into $8000 (R6)
prg_bank:
  ldx #6
  stx bsel
  stx M3_SEL
  sta M3_DAT
  sta prg6
  rts

; ---------------------------------------------------------------- sprites
; oam_begin: clear the table, choose the rotating start slot (stride 7 sprites keeps all 64 reachable)
oam_begin:
  lda #$F8
  ldx #0
: sta oam,x
  sta oam+4,x
  sta oam+8,x
  sta oam+12,x
  inx
  inx
  inx
  inx
  inx
  inx
  inx
  inx
  inx
  inx
  inx
  inx
  inx
  inx
  inx
  inx
  bne :-
  lda frame
  asl
  asl
  asl
  sec
  sbc frame
  asl
  asl                    ; frame*28 (mod 256)
  sta oamstart
  sta oamidx
  lda use_s0
  beq :+
  ; sprite 0 fixed: opaque tile in the HUD border row
  lda #26
  sta oam
  lda #63
  sta oam+1
  lda #$20
  sta oam+2
  lda #24
  sta oam+3
  lda oamstart
  and #$FC
  bne :+
  lda #4
  sta oamstart
  sta oamidx
: lda #0
  sta sprcnt
  rts

; put_spr: spy,spt,spa,spx -> next OAM slot (rotating, slot 0 reserved when sprite 0 is used)
put_spr:
  lda sprcnt
  cmp #64
  bcs @full
  inc sprcnt
  ldx oamidx
  bne :+
  lda use_s0
  beq :+
  ldx #4
: lda spy
  sta oam,x
  lda spt
  sta oam+1,x
  lda spa
  sta oam+2,x
  lda spx
  sta oam+3,x
  inx
  inx
  inx
  inx
  stx oamidx
@full:
  rts

; ---------------------------------------------------------------- column streaming
; make_col: A = column number low 8 bits of (col & 1023) from lastcol; builds colbuf rows 4..29, sets gh
; in: ptr = map pointer to (h,d) already in t2 (h) t3 (d), t4 = col&63 ; out: colbuf[0..25]
col_fill:
  ; t2 = ground row, t3 = deco, t4 = column (0..255) for sky pattern
  ldx #0                  ; x = row-4
@row:
  txa
  clc
  adc #4
  sta t5                  ; row
  cmp t2
  beq @top
  bcs @fill
  ; above ground
  lda t2
  sec
  sbc t5
  sec
  sbc #1                  ; k = h-1-row
  cmp #8
  bcs @sky
  sta t6
  lda t3
  asl
  asl
  asl
  ora t6
  tay
  lda rel_tile,y
  jmp @store
@sky:
  ; decoration clouds on rows 6..9 depending on column
  lda t5
  cmp #6
  bcc @s0
  cmp #8
  bcs @s0
  lda t4
  and #7
  cmp #3
  bcs @s0
  clc
  adc #5                  ; cloud tiles 5,6,7 (shared with deco tiles in the set)
  jmp @store
@s0:
  lda #0
  jmp @store
@top:
  lda #1
  jmp @store
@fill:
  lda t5
  cmp #28
  bcs @water
  lda #2
  jmp @store
@water:
  lda #11
@store:
  sta colbuf,x
  inx
  cpx #26
  bcc @row
  rts

; fetch_col: A/X = column (16-bit in lastcol) -> t2 = h, t3 = d via map banks; also gh[] ring
fetch_col:
  lda lastcol
  sta t4
  lda lastcol+1
  and #3
  tay
  lda t4                   ; low byte of column
  asl                      ; *2 -> offset (carry = bit 7)
  tax                      ; offset low
  lda #0
  rol
  sta t7                   ; high bit of offset
  ; map bank = lastcol>>8 & 3 -> PRG bank number
  tya
  jsr prg_bank
  lda t7
  clc
  adc #$80
  sta ptr+1
  stx ptr
  ldy #0
  lda (ptr),y
  sta t2
  iny
  lda (ptr),y
  sta t3
  ; ground height ring
  lda lastcol
  and #63
  tax
  lda t2
  asl
  asl
  asl
  sta gh,x
  rts

; queue one new column (the one entering at the right edge) in vbuf (rows 4..29) + attributes
queue_col:
  jsr fetch_col
  jsr col_fill
  ; nametable address: $2000 | (col&32 ? $400) | (col&31) + row 4 * 32
  lda lastcol
  and #$20
  beq :+
  lda #$04
: ora #$20
  sta t0                   ; hi without row offset
  lda lastcol
  and #$1F
  clc
  adc #$80                 ; + 4*32 = $80
  tax
  lda t0
  adc #0
  lda col_rows
  ora #$80
  tay
  jsr vq_begin
  ldy #0
: lda colbuf,y
  sta vbuf,x
  inx
  iny
  cpy col_rows
  bcc :-
  jsr vq_end
  ; attributes every 4th column: 8 bytes at $23C0 + (col&31)/4 + row*8
  lda lastcol
  and #3
  cmp #3
  bne @noattr
  lda lastcol
  and #$1F
  lsr
  lsr
  sta t1
  lda t3
  asl
  asl
  asl
  sta t2                   ; deco*8
  ldy #1
@attr:
  ; byte for attribute row y (1..7): address $23C0 + y*8 + t1
  tya
  asl
  asl
  asl
  clc
  adc t1
  adc #$C0
  sta t6
  lda lastcol
  and #$20
  beq :+
  lda #$04
: ora #$23
  tax                      ; hi
  stx t0
  ldx vlen
  lda t0
  sta vbuf,x
  inx
  lda t6
  sta vbuf,x
  inx
  lda #1
  sta vbuf,x
  inx
  tya
  ora t2
  stx t7
  tax
  lda attr_tpl,x
  ldx t7
  sta vbuf,x
  inx
  stx vlen
  iny
  cpy attr_end
  bcc @attr
  lda #0
  ldx vlen
  sta vbuf,x
@noattr:
  rts

; stream columns for the camera: keep the right edge column (scx>>3)+33 queued
stream_cols:
  lda scx
  lsr
  sta t0
  lda scx+1
  lsr
  ror t0
  lsr
  ror t0                   ; t0 = scx/8 low (9 bits ok: high bit in carry chain)
  ; column index = scx/8 + 33 (16 bit)
  lda scx
  sta ptr
  lda scx+1
  sta ptr+1
  lsr ptr+1
  ror ptr
  lsr ptr+1
  ror ptr
  lsr ptr+1
  ror ptr
  lda ptr
  clc
  adc #33
  sta ptr
  bcc :+
  inc ptr+1
: lda ptr
  cmp lastcol
  bne @new
  lda ptr+1
  cmp lastcol+1
  beq @none
@new:
  lda ptr
  sta lastcol
  lda ptr+1
  and #3
  sta lastcol+1
  jsr queue_col
@none:
  rts

; fill both nametables at init (rendering is off): columns 0..63, rows 4..29, attributes
init_world:
  lda #0
  sta lastcol
  sta lastcol+1
@c:
  jsr fetch_col
  jsr col_fill
  lda lastcol
  and #$20
  beq :+
  lda #$04
: ora #$20
  sta t0
  lda lastcol
  and #$1F
  clc
  adc #$80
  tax
  lda t0
  adc #0
  sta PPUADDR
  stx PPUADDR
  lda #4
  sta PPUCTRL
  ldy #0
: lda colbuf,y
  sta PPUDATA
  iny
  cpy #26
  bcc :-
  ; attribute bytes
  lda lastcol
  and #3
  cmp #3
  bne @na
  lda lastcol
  and #$1F
  lsr
  lsr
  sta t1
  lda t3
  asl
  asl
  asl
  sta t2
  ldy #1
@a:
  tya
  asl
  asl
  asl
  clc
  adc t1
  adc #$C0
  tax
  lda lastcol
  and #$20
  beq :+
  lda #$04
: ora #$23
  sta PPUADDR
  stx PPUADDR
  tya
  ora t2
  tax
  lda attr_tpl,x
  sta PPUDATA
  iny
  cpy #8
  bcc @a
@na:
  inc lastcol
  lda lastcol
  cmp #64
  bcc @c
  lda #0
  sta lastcol
  lda #31
  sta lastcol            ; next streamed column handled by stream_cols (scx/8+33 = 33)
  lda #0
  sta lastcol+1
  rts

; HUD in both nametables rows 0..3 (rendering off): text on row 1, border (opaque tile 192) on row 3
init_hud:
  lda #0
  sta PPUCTRL
  ldy #0                    ; nametable index 0..1
@nt:
  lda hud_hi,y
  sta PPUADDR
  lda #0
  sta PPUADDR
  ; rows 0..3: 128 tiles
  ldx #0
@r0:
  lda #128 + 0              ; blank font tile (space) = 128
  sta PPUDATA
  inx
  cpx #32
  bne @r0
  ; row 1: the HUD string
  ldx #0
  lda #128
  sta PPUDATA
  lda #0
  sta t0
@txt:
  ldx t0
  lda str_hud,x
  beq @txtend
  clc
  adc #96                    ; ASCII -> font tile (128 + c - 32)
  sta PPUDATA
  inc t0
  bne @txt
@txtend:
  ; pad row 1 to 32 tiles
  lda t0
  clc
  adc #1
  tax
@pad:
  cpx #32
  bcs @row2
  lda #128
  sta PPUDATA
  inx
  bne @pad
@row2:
  ldx #0
: lda #128
  sta PPUDATA
  inx
  cpx #32
  bne :-
  ldx #0
: lda #192
  sta PPUDATA
  inx
  cpx #32
  bne :-
  iny
  cpy #2
  bcc @nt
  rts
hud_hi: .byte $20, $24

; palette: A/X = pointer lo/hi -> write all 32 bytes (rendering off) at init
load_pal:
  sta ptr
  stx ptr+1
  lda PPUSTATUS
  lda #$3F
  sta PPUADDR
  lda #0
  sta PPUADDR
  ldy #0
: lda (ptr),y
  sta PPUDATA
  iny
  cpy #32
  bcc :-
  rts

; queue a palette update of n bytes from (ptr) to $3F00+x (via vbuf)
; text helper: print zero-terminated string at A=hi X=lo with rendering off
print_str:
  ; ptr = string, A/X = PPU address
  sta PPUADDR
  stx PPUADDR
  ldy #0
: lda (ptr),y
  beq @e
  clc
  adc #96
  sta PPUDATA
  iny
  bne :-
@e:
  rts

; clear both nametables + attributes (rendering off)
clear_nt:
  lda PPUSTATUS
  lda #0
  sta PPUCTRL
  lda #$20
  sta PPUADDR
  lda #0
  sta PPUADDR
  ldy #0
  ldx #8
  lda #0
: sta PPUDATA
  iny
  bne :-
  dex
  bne :-
  rts

; ---------------------------------------------------------------- entities
; spawn one entity: type in t0, x (16-bit px) t1/t2, y in t3, returns X index; uses next free slot (nent)
ent_new:
  ldx nent
  cpx #MAXENT
  bcc :+
  ldx #MAXENT-1
  rts
: inc nent
  rts

; random helper: A = rand() & mask (mask in A on entry)
rand_mask:
  sta t7
  jsr rand
  and t7
  rts

; update_ents: dispatch on type for every entity
update_ents:
  ldx #0
ue_loop:
  cpx nent
  bcs ue_done
  lda ent_type,x
  asl
  tay
  lda etab+1,y
  pha
  lda etab,y
  pha
  rts                       ; RTS-dispatch to etab[type]-1 (see etab)
ue_next:
  inx
  jmp ue_loop
ue_done:
  rts

etab:
  .word e_walk-1, e_fly-1, e_bullet-1, e_part-1, e_npc-1, e_enemy-1, e_dive-1

; common: x += v (16.8), y += v (8.8)
move_ent:
  clc
  lda ent_xf,x
  adc ent_vxf,x
  sta ent_xf,x
  lda ent_xl,x
  adc ent_vxi,x
  sta ent_xl,x
  ldy #0
  lda ent_vxi,x
  bpl :+
  dey
: tya
  adc ent_xh,x
  sta ent_xh,x
  clc
  lda ent_yf,x
  adc ent_vyf,x
  sta ent_yf,x
  lda ent_y,x
  adc ent_vyi,x
  sta ent_y,x
  rts

; platformer walker with gravity and ground collision
e_walk:
  jsr move_ent
  ; gravity
  clc
  lda ent_vyf,x
  adc #$30
  sta ent_vyf,x
  lda ent_vyi,x
  adc #0
  sta ent_vyi,x
  ; ground at column of x
  lda ent_xl,x
  lsr
  lsr
  lsr
  and #63
  tay
  lda gh,y
  sec
  sbc #16
  cmp ent_y,x
  bcs @air
  sta ent_y,x
  lda #0
  sta ent_vyi,x
  sta ent_vyf,x
  ; occasionally jump
  jsr rand
  and #$3F
  bne @air
  lda #$FC
  sta ent_vyi,x
@air:
  ; turn at times
  lda frame
  and #$7F
  bne @nt
  lda ent_vxi,x
  eor #$FF
  clc
  adc #1
  sta ent_vxi,x
@nt:
  jmp ue_next

e_fly:
  jsr move_ent
  lda frame
  clc
  adc ent_aux,x
  tay
  lda sine,y
  cmp #$80
  ror
  cmp #$80
  ror                       ; /4 signed
  clc
  adc #96
  sta ent_y,x
  jmp ue_next

e_bullet:
  jsr move_ent
  lda ent_xh,x
  bne @respawn
  lda ent_y,x
  cmp #232
  bcs @respawn
  jmp ue_next
@respawn:
  lda scx                   ; screen-space entities: re-fire from the ship
  jsr rand
  sta ent_y,x
  and #3
  sta ent_vyi,x
  lda #0
  sta ent_xl,x
  sta ent_xh,x
  jmp ue_next

e_part:
  jsr move_ent
  clc
  lda ent_vyf,x
  adc #$40
  sta ent_vyf,x
  lda ent_vyi,x
  adc #0
  sta ent_vyi,x
  dec ent_aux,x
  bne @a
  ; respawn at the burst point
  jsr rand
  sta ent_vxi,x
  and #3
  sta ent_vyi,x
  lda #0
  sta ent_vyf,x
  lda #60
  sta ent_aux,x
  lda #120
  sta ent_y,x
@a:
  jmp ue_next

e_npc:
  dec ent_aux,x
  bne @mv
  jsr rand
  and #3
  tay
  lda npc_vx,y
  sta ent_vxi,x
  lda npc_vy,y
  sta ent_vyi,x
  jsr rand
  and #$1F
  clc
  adc #16
  sta ent_aux,x
@mv:
  jsr move_ent
  jmp ue_next
npc_vx: .byte 1, $FF, 0, 0
npc_vy: .byte 0, 0, 1, $FF

; vertical scroller dive-bombers: fall, drift, come back at the top
e_dive:
  jsr move_ent
  lda ent_y,x
  cmp #232
  bcc @ok
  lda #0
  sta ent_y,x
  jsr rand
  sta ent_xl,x
  lda #0
  sta ent_xh,x
  jsr rand
  and #1
  sec
  sbc #1
  sta ent_vxi,x
@ok:
  jmp ue_next

e_enemy:
  jsr move_ent
  lda frame
  clc
  adc ent_aux,x
  tay
  lda sine,y
  cmp #$80
  ror
  cmp #$80
  ror
  cmp #$80
  ror
  clc
  adc ent_aux,x
  adc #40
  sta ent_y,x
  lda ent_xl,x
  cmp #8
  bcs :+
  lda ent_xh,x
  bne :+
  lda #255
  sta ent_xl,x
:
  jmp ue_next

; draw every entity as a 2x2 metasprite. world entities relative to scx (cam=1), others screen space
; mode 8x16 (ppuctrl_b & $20): 2 sprites per entity, else 4
draw_ents:
  ldx #0
@e:
  cpx nent
  bcc :+
  rts
: stx t0
  ; screen x = x - scx (16-bit); visible if hi == 0
  sec
  lda ent_xl,x
  sbc cam
  sta spx
  lda ent_xh,x
  sbc cam+1
  bne @skip
  lda spx
  cmp #241
  bcs @skip
  lda ent_y,x
  sta spy
  lda ent_attr,x
  and #$EF
  sta spa
  lda ent_spr,x
  asl
  asl                      ; creature index * 4 = tile base within the bank (bank offset via ent_aux high? kept in spr high bits)
  sta t1
  lda ppuctrl_b
  and #$20
  bne @big
  ; 8x8: LT, LB, RT, RB (small: LT only)
  lda t1
  sta spt
  jsr put_spr
  ldx t0
  lda ent_attr,x
  and #$10
  bne @skip
  lda spy
  clc
  adc #8
  sta spy
  inc spt
  jsr put_spr
  lda spx
  clc
  adc #8
  sta spx
  inc spt
  jsr put_spr
  lda spy
  sec
  sbc #8
  sta spy
  inc spt
  jsr put_spr
  jmp @skip
@big:
  lda t1
  ora #1
  sta spt
  jsr put_spr
  lda ent_attr,x
  and #$10
  bne @skip                ; small: one 8x16 sprite
  lda spx
  clc
  adc #8
  sta spx
  lda t1
  clc
  adc #2
  ora #1
  sta spt
  jsr put_spr
@skip:
  ldx t0
  inx
  jmp @e

; ---------------------------------------------------------------- sound engine (5 channels)
snd_init:
  lda #$0F
  sta APU_STATUS
  lda #$30
  sta $4000
  sta $4004
  sta $400C
  lda #$08
  sta $4001
  sta $4005
  lda #6
  sta snd_speed
  lda #1
  sta snd_tick
  lda #$1F
  sta snd_mask
  rts

snd_update:
  inc snd_t
  dec snd_tick
  beq @row
  jmp @env
@row:
  lda snd_speed
  sta snd_tick
  ; next row
  lda snd_row
  sta t0
  lda #0
  sta t1
  lda snd_row
  asl
  rol t1
  asl
  rol t1
  clc
  adc snd_row
  bcc :+
  inc t1
: sta ptr
  lda t1
  adc #>song
  sta ptr+1
  lda ptr
  clc
  adc #<song
  sta ptr
  bcc :+
  inc ptr+1
: ldy #0
  lda snd_row
  clc
  adc #1
  sta snd_row
  ; pulse 1
  lda (ptr),y
  beq @p2
  tax
  lda snd_mask
  and #1
  beq @p2
  lda pulse_lo,x
  sta snd_per1
  sta $4002
  lda pulse_hi,x
  sta snd_per1+1
  ora #$08
  sta $4003
  lda #15
  sta snd_vol1
@p2:
  iny
  lda (ptr),y
  beq @tri
  tax
  lda snd_mask
  and #2
  beq @tri
  lda pulse_lo,x
  sta $4006
  lda pulse_hi,x
  ora #$08
  sta $4007
  lda #12
  sta snd_vol2
@tri:
  iny
  lda (ptr),y
  beq @noise
  cmp #$FF
  beq @trioff
  tax
  lda snd_mask
  and #4
  beq @noise
  lda #$FF
  sta $4008
  lda tri_lo,x
  sta $400A
  lda tri_hi,x
  ora #$08
  sta $400B
  jmp @noise
@trioff:
  lda #$80
  sta $4008
@noise:
  iny
  lda (ptr),y
  beq @dmc
  tax
  lda snd_mask
  and #8
  beq @dmc
  lda noise_per-1,x
  sta $400E
  lda #8
  sta $400F
  lda #15
  sta snd_nvol
@dmc:
  iny
  lda (ptr),y
  beq @env
  tax
  lda snd_mask
  and #16
  beq @env
  dex
  lda dpcm_rate,x
  sta $4010
  lda dpcm_addr,x
  sta $4012
  lda dpcm_len,x
  sta $4013
  lda #$0F
  sta APU_STATUS
  lda #$1F
  sta APU_STATUS
@env:
  ; pulse 1: envelope + vibrato
  lda snd_vol1
  beq @e2
  lda snd_t
  and #1
  bne :+
  dec snd_vol1
: lda snd_vol1
  ora #$B0
  sta $4000
  lda snd_t
  and #$1F
  tax
  lda sine,x
  cmp #$80
  ror
  cmp #$80
  ror
  cmp #$80
  ror
  cmp #$80
  ror
  clc
  adc snd_per1
  sta $4002
@e2:
  lda snd_vol2
  beq @e3
  lda snd_t
  and #3
  bne :+
  dec snd_vol2
: lda snd_vol2
  ora #$70
  sta $4004
@e3:
  lda snd_nvol
  beq @sw
  lda snd_t
  and #1
  bne :+
  dec snd_nvol
: lda snd_nvol
  ora #$30
  sta $400C
@sw:
  lda snd_t
  and #7
  bne @x
  lda snd_t
  and #8
  beq :+
  lda #$A9
  .byte $2C
: lda #$08
  sta $4001
@x:
  rts
noise_per: .byte $04, $08, $0D

; ---------------------------------------------------------------- checksum / phase bookkeeping
phase_end:
  ; SRAM record: SRAM+8+phase*8 = hash lo/hi, frame lo/hi, lag lo/hi, s0wait lo, sprcnt
  lda phase
  asl
  asl
  asl
  clc
  adc #8
  tax
  lda hash
  sta SRAM,x
  lda hash+1
  sta SRAM+1,x
  lda frame
  sta SRAM+2,x
  lda frame+1
  sta SRAM+3,x
  lda lagcnt
  sta SRAM+4,x
  lda lagcnt+1
  sta SRAM+5,x
  lda s0wait
  sta SRAM+6,x
  lda sprcnt
  sta SRAM+7,x
  rts

; ---------------------------------------------------------------- phase dispatch
phase_update:
  lda ptimer
  bne @t
  lda ptimer+1
  bne @t1
  ; end of phase
  jsr phase_end
  ldx phase
  inx
  cpx #NPHASE
  bcc :+
  ldx #0
: stx phase
  jmp phase_init
@t1:
  dec ptimer+1
@t:
  dec ptimer
  lda phase
  asl
  tay
  lda ph_upd,y
  sta ptr2
  lda ph_upd+1,y
  sta ptr2+1
  jmp (ptr2)

ph_init:  .word p0_init, p1_init, p2_init, p3_init, p4_init, p5_init, p6_init, p7_init, p8_init
ph_upd:   .word p0_upd, p1_upd, p2_upd, p3_upd, p4_upd, p5_upd, p6_upd, p7_upd, p8_upd

; phase_init: rendering off during the setup (a scene load), then back on
phase_init:
  lda #0
  sta M3_MIRR
  sta PPUMASK
  sta render_on
  sta nbands
  sta vlen
  sta use_s0
  sta scx
  sta scx+1
  sta scy
  sta nent
  sta irq_mode
  sta poll_spin
  sta APU_STATUS
  lda #26
  sta col_rows
  lda #8
  sta attr_end
  lda #<PHASE_FRAMES
  sta ptimer
  lda #>PHASE_FRAMES
  sta ptimer+1
  jsr snd_init
  lda PPUSTATUS
  lda phase
  asl
  tay
  lda ph_init,y
  sta ptr2
  lda ph_init+1,y
  sta ptr2+1
  jsr @call
  lda #99
  sta dbg
  ; the phase name small in the corner is drawn by each init (str_phaseN)
  jsr oam_begin
  lda ppuctrl_b
  ora #$88
  sta PPUCTRL
  lda #1
  sta render_on
  lda #$1E
  sta PPUMASK
  rts
@call:
  jmp (ptr2)

; =========================================================== PHASE 0: TITLE / ATTRACT (light)
p0_init:
  lda #1
  sta poll_spin
  lda #0
  sta ppuctrl_b
  sta cam
  sta cam+1
  lda #<pal_plat
  ldx #>pal_plat
  jsr load_pal
  lda #0
  ldx #8
  jsr set_chr_bg
  lda #22
  ldx #26
  ldy #30
  sty t0
  ldy #34
  jsr set_chr_spr
  lda #$1F
  sta snd_mask
  jsr clear_nt
  ; big title text
  lda #<str_title1
  sta ptr
  lda #>str_title1
  sta ptr+1
  lda #$21
  ldx #$CA
  jsr print_str
  lda #<str_title2
  sta ptr
  lda #>str_title2
  sta ptr+1
  lda #$22
  ldx #$0A
  jsr print_str
  lda #<str_phase0
  sta ptr
  lda #>str_phase0
  sta ptr+1
  lda #$23
  ldx #$A2
  jsr print_str
  ; 6 bouncing sprites
  lda #6
  sta nent
  ldx #0
: lda #T_FLY
  sta ent_type,x
  txa
  asl
  asl
  asl
  asl
  sta ent_xl,x
  lda #0
  sta ent_xh,x
  sta ent_vxf,x
  sta ent_vxi,x
  txa
  asl
  asl
  asl
  sta ent_aux,x
  txa
  sta ent_spr,x
  lda #0
  sta ent_attr,x
  inx
  cpx #6
  bcc :-
  lda #$28
  sta ppuctrl_b          ; 8x16 sprites... bit5; bit3 sprites at $1000
  rts

p0_upd:
  jsr update_ents
  jsr oam_begin
  jsr draw_ents
  ; palette cycling: two colours every 4 frames
  lda frame
  and #3
  bne @n
  ldx vlen
  lda #$3F
  sta vbuf,x
  inx
  lda #$01
  sta vbuf,x
  inx
  lda #3
  sta vbuf,x
  inx
  lda frame
  lsr
  lsr
  and #15
  ora #$10
  sta vbuf,x
  inx
  eor #$0F
  sta vbuf,x
  inx
  adc #3
  sta vbuf,x
  inx
  stx vlen
  lda #0
  sta vbuf,x
@n:
  rts

; ---------------------------------------------------------------- CHR helpers
; set_chr_bg: A = R0 (2 KiB bank, even 1 KiB number), X = R1
set_chr_bg:
  sta chr_r
  stx chr_r+1
  rts
; set_chr_spr: A, X, Y, t0 = sprite banks for $1000, $1400, $1800, $1C00
set_chr_spr:
  sta chr_r+2
  stx chr_r+3
  sty chr_r+4
  lda t0
  sta chr_r+5
  rts

; ent_init_one: X = index, t0 = type, A = sprite (slot*16+creature), t1 = attr, t2:t3 = x lo/hi, t4 = y, t5 = vx, t6 = vy
ent_set:
  sta ent_spr,x
  lda t0
  sta ent_type,x
  lda t1
  sta ent_attr,x
  lda t2
  sta ent_xl,x
  lda t3
  sta ent_xh,x
  lda t4
  sta ent_y,x
  lda t5
  sta ent_vxi,x
  lda t6
  sta ent_vyi,x
  lda #0
  sta ent_xf,x
  sta ent_yf,x
  sta ent_vxf,x
  sta ent_vyf,x
  jsr rand
  sta ent_aux,x
  rts

; ent_col: Y = ground-ring index of entity X (world column & 63)
ent_col:
  lda ent_xl,x
  lsr
  lsr
  lsr
  sta t7
  lda ent_xh,x
  asl
  asl
  asl
  asl
  asl
  ora t7
  and #63
  tay
  rts

; scroll camera by A pixels (A = 0..3) and stream columns
cam_scroll:
  clc
  adc scx
  sta scx
  bcc :+
  inc scx+1
: lda scx
  sta cam
  lda scx+1
  sta cam+1
  rts

; HUD digits (score) written at $2027, 6 digits, every 6 frames
hud_update:
  lda frame
  and #3
  bne @n
  inc score
  bne :+
  inc score+1
: ldx vlen
  lda #$20
  sta vbuf,x
  inx
  lda #$27
  sta vbuf,x
  inx
  lda #6
  sta vbuf,x
  inx
  lda score
  jsr hex_lo
  sta t0
  lda score
  lsr
  lsr
  lsr
  lsr
  jsr hex_lo
  sta t1
  lda score+1
  jsr hex_lo
  sta t2
  lda score+1
  lsr
  lsr
  lsr
  lsr
  jsr hex_lo
  sta t3
  lda #128+16
  sta vbuf,x
  inx
  lda #128+16
  sta vbuf,x
  inx
  lda t3
  sta vbuf,x
  inx
  lda t2
  sta vbuf,x
  inx
  lda t1
  sta vbuf,x
  inx
  lda t0
  sta vbuf,x
  inx
  stx vlen
  lda #0
  sta vbuf,x
@n:
  rts
hex_lo:
  and #$0F
  cmp #10
  bcc :+
  adc #6                 ; A-F, carry set
: clc
  adc #128+16            ; '0' = ASCII 48 -> font tile 128 + 48 - 32
  rts

; world-phase helper: respawn walkers that left the screen on the left
respawn_left:
  ldx #1
@l:
  cpx nent
  bcs @d
  lda ent_type,x
  cmp #T_WALK
  beq :+
  cmp #T_NPC
  bne @nx
: sec
  lda ent_xl,x
  sbc scx
  lda ent_xh,x
  sbc scx+1
  bpl @nx                ; still ahead
  ; far behind: respawn ahead of the camera
  jsr rand
  and #$3F
  clc
  adc scx
  sta ent_xl,x
  lda scx+1
  adc #1
  sta ent_xh,x
  lda #20
  sta ent_y,x
@nx:
  inx
  bne @l
@d:
  rts

; ===========================================================  PHASE 1: PLATFORMER (SMB3 / Mega Man pattern)
p1_init:
  lda #$20
  sta ppuctrl_b          ; 8x16 sprites
  lda #<pal_plat
  ldx #>pal_plat
  jsr load_pal
  lda #0
  ldx #8
  jsr set_chr_bg
  lda #22
  ldx #26
  ldy #30
  sty t0
  ldy #34
  jsr set_chr_spr
  lda #11
  sta dbg
  jsr clear_nt
  lda #12
  sta dbg
  jsr init_world
  lda #13
  sta dbg
  jsr init_hud
  lda #14
  sta dbg
  lda #<str_phase1
  sta ptr
  lda #>str_phase1
  sta ptr+1
  lda #$20
  ldx #$7A
  jsr print_str
  lda #15
  sta dbg
  lda #1
  sta use_s0
  lda #$1F
  sta snd_mask
  lda #32
  sta snd_row
  ; entities: player + enemies (walk) + flyers
  ldx #0
  lda #T_WALK
  sta t0
  lda #$00
  sta t1
  lda #100
  sta t2
  lda #0
  sta t3
  lda #100
  sta t4
  lda #1
  sta t5
  lda #0
  sta t6
  lda #0
  jsr ent_set
  ldx #1
@e:
  stx t7
  lda #T_WALK
  sta t0
  lda #1
  sta t1
  jsr rand
  sta t2
  and #1
  sta t3
  lda #40
  sta t4
  jsr rand
  and #1
  clc
  adc #$FF
  sta t5
  lda #0
  sta t6
  txa
  and #7
  clc
  adc #16
  ldx t7
  jsr ent_set
  inx
  cpx #(4 + LEVEL * 2)
  bcc @e
  ; flyers
@f:
  stx t7
  lda #T_FLY
  sta t0
  lda #2
  sta t1
  jsr rand
  sta t2
  lda #1
  sta t3
  lda #60
  sta t4
  lda #$FF
  sta t5
  lda #0
  sta t6
  txa
  and #3
  clc
  adc #32
  ldx t7
  jsr ent_set
  inx
  cpx #(6 + LEVEL * 2 + 4)
  bcc @f
  stx nent
  lda #16
  sta dbg
  rts

p1_upd:
  lda frame+1
  and #1
  clc
  adc #1
  jsr cam_scroll
  jsr stream_cols
  ; player follows the camera
  lda scx
  clc
  adc #96
  sta ent_xl
  lda scx+1
  adc #0
  sta ent_xh
  jsr update_ents
  jsr respawn_left
  ; animation banks
  lda frame
  lsr
  lsr
  lsr
  and #3
  clc
  adc #26
  sta chr_r+3
  adc #4
  sta chr_r+4
  jsr oam_begin
  jsr draw_ents
  jsr hud_update
  ; question-block colour cycling every 8 frames (palette byte)
  lda frame
  and #7
  bne @n
  ldx vlen
  lda #$3F
  sta vbuf,x
  inx
  lda #$0A
  sta vbuf,x
  inx
  lda #1
  sta vbuf,x
  inx
  lda frame
  lsr
  lsr
  lsr
  and #3
  tay
  lda qcol,y
  sta vbuf,x
  inx
  stx vlen
  lda #0
  sta vbuf,x
@n:
  rts
qcol: .byte $27, $17, $07, $17

; ===========================================================  PHASE 2: MMC3 PARALLAX + WAVY WATER (Kirby / SMB3 / Crystalis pattern)
p2_init:
  lda #$20
  sta ppuctrl_b
  lda #<pal_para
  ldx #>pal_para
  jsr load_pal
  lda #10
  ldx #8
  jsr set_chr_bg
  lda #22
  ldx #26
  ldy #30
  sty t0
  ldy #34
  jsr set_chr_spr
  jsr clear_nt
  ; both nametables: rows 0..29
  lda #0
  sta sub
  ldy #0
@nt:
  lda #0
  sta PPUCTRL
  lda ptnt,y
  sta PPUADDR
  lda #0
  sta PPUADDR
  ldx #0                 ; row
@row:
  stx t0
  ; first row of a band: silhouette tile (col&15), else fill tile 16 (band 0: all silhouettes = sky)
  lda #0
  sta t1                 ; col
  lda t0
  beq @sil
  cmp #6
  beq @sil
  cmp #11
  beq @sil
  cmp #16
  beq @sil
  cmp #21
  beq @sil
  cmp #26
  beq @sil
  cmp #1
  bcc @sil
  cmp #6
  bcc @sil
  lda #16
  sta t2
  jmp @fillrow
@sil:
  lda #$FF
  sta t2
@fillrow:
  ldx #0
@col:
  lda t2
  bpl :+
  txa
  and #15
  jmp @put
: lda t2
@put:
  sta PPUDATA
  inx
  cpx #32
  bcc @col
  ldx t0
  inx
  cpx #30
  bcc @row
  ; attribute table: palette per 32px row
  ldx #0
@at:
  lda atpal,x
  ldy #8
: sta PPUDATA
  dey
  bne :-
  inx
  cpx #8
  bcc @at
  inc sub
  ldy sub
  cpy #2
  bcs @ntdone
  jmp @nt
@ntdone:
  lda #0
  sta cam
  sta cam+1
  lda #$0F
  sta snd_mask
  lda #64
  sta snd_row
  ; sprites: fliers in screen space
  ldx #0
@s:
  stx t7
  lda #T_FLY
  sta t0
  lda #(LEVEL * 2 + 6)
  lda #0
  sta t1
  jsr rand
  sta t2
  lda #0
  sta t3
  lda #0
  sta t4
  lda #$FF
  sta t5
  lda #0
  sta t6
  txa
  and #15
  ldx t7
  jsr ent_set
  inx
  cpx #(LEVEL + 6)
  bcc @s
  stx nent
  rts
ptnt: .byte $20, $24
para_yl: .byte 0, 48, 88, 128, 168, 208
atpal: .byte $00, $00, $55, $55, $AA, $AA, $FF, $FF

p2_upd:
  lda #2
  jsr cam_scroll
  lda #0
  sta cam
  sta cam+1
  jsr update_ents_wrap
  lda irq_mode
  bne @normal
  lda ptimer+1
  cmp #>750
  bcc @wavy
  bne @normal
  lda ptimer
  cmp #<750
  bcc @wavy
@normal:
  lda #6
  sta nbands
  ldx #0
@b:
  ; x16 = scx >> speed[x]
  lda scx
  sta t0
  lda scx+1
  sta t1
  ldy para_speed,x
  beq @sh
: lsr t1
  ror t0
  dey
  bne :-
@sh:
  lda t0
  sta band_x,x
  lda t1
  and #1
  ora ppuctrl_b
  ora #$88
  sta band_ctrl,x
  lda para_chr,x
  sta band_chr,x
  lda para_len,x
  sta band_len,x
  lda para_yl,x
  sta band_yl,x
  inx
  cpx #6
  bcc @b
  jmp @top
@wavy:
  ; 28 bands of 8 lines, x wobbles with a sine, sets cycle
  lda #28
  sta nbands
  ldx #0
@w:
  txa
  asl
  asl
  asl
  asl
  clc
  adc frame
  adc frame
  adc frame
  tay
  lda sine,y
  cmp #$80
  ror
  cmp #$80
  ror
  cmp #$80
  ror
  cmp #$80
  ror
  sta t2
  clc
  adc scx
  sta band_x,x
  lda scx+1
  adc #0
  and #1
  ora ppuctrl_b
  ora #$88
  sta band_ctrl,x
  txa
  lsr
  lsr
  tay
  lda para_chr,y
  sta band_chr,x
  lda #7
  sta band_len,x
  inx
  cpx #28
  bcc @w
@top:
  lda band_chr
  sta chr_r
  jsr oam_begin
  jsr draw_ents
  rts

; fliers wrapping at the screen edges
update_ents_wrap:
  jsr update_ents
  ldx #0
@l:
  cpx nent
  bcs @d
  lda ent_xh,x
  beq @nx
  lda ent_vxi,x
  bmi @left
  lda #0
  sta ent_xl,x
  sta ent_xh,x
  jmp @nx
@left:
  lda #240
  sta ent_xl,x
  lda #0
  sta ent_xh,x
@nx:
  inx
  jmp @l
@d:
  rts

; ===========================================================  PHASE 7: SPLIT SCROLL WITH $2006 (the parallax bands, the four-write scroll trick)
p7_init:
  jsr p2_init
  lda #1
  sta irq_mode
  lda #$0F
  sta snd_mask
  lda #224
  sta snd_row
  rts
p7_upd:
  jmp p2_upd

; ===========================================================  PHASE 8: VERTICAL SCROLLER (1943 / Ikari Warriors / Kid Icarus pattern)
; horizontal mirroring (MMC3 $A000 = 1), the Y scroll walks through two name tables, one new 32-tile row (+ attributes every
; fourth) streamed in below the screen every four frames, dive-bombers and bullets over it
p8_init:
  lda #1
  sta M3_MIRR
  lda #$00
  sta ppuctrl_b
  lda #<pal_shoot
  ldx #>pal_shoot
  jsr load_pal
  lda #6
  ldx #8
  jsr set_chr_bg
  lda #36
  ldx #40
  ldy #44
  sty t0
  ldy #48
  jsr set_chr_spr
  jsr clear_nt
  ; all 60 rows (two screens) of the first stretch of the map, rendering off
  lda #0
  sta lastcol
@rows:
  lda lastcol
  jsr vrow_make
  lda lastcol
  jsr vrow_addr          ; A = hi, X = lo
  sta PPUADDR
  stx PPUADDR
  lda #0
  sta PPUCTRL
  ldy #0
: lda colbuf,y
  sta PPUDATA
  iny
  cpy #32
  bcc :-
  inc lastcol
  lda lastcol
  cmp #60
  bcc @rows
  lda #0
  sta vpos
  sta vpos+1
  sta scx
  sta scx+1
  sta scy
  lda #$1F
  sta snd_mask
  lda #240
  sta snd_row
  lda #0
  sta cam
  sta cam+1
  ; 12 dive-bombers (2x2) then 12 small bullets
  ldx #0
@d:
  stx t7
  lda #T_DIVE
  sta t0
  lda #1
  sta t1
  jsr rand
  sta t2
  lda #0
  sta t3
  jsr rand
  and #$7F
  sta t4
  lda #0
  sta t5
  lda #2
  sta t6
  txa
  and #7
  clc
  adc #16
  ldx t7
  jsr ent_set
  inx
  cpx #(LEVEL + 7)
  bcc @d
@b:
  stx t7
  lda #T_DIVE
  sta t0
  lda #$11
  sta t1
  jsr rand
  sta t2
  lda #0
  sta t3
  jsr rand
  sta t4
  lda #0
  sta t5
  lda #4
  sta t6
  lda #32
  ldx t7
  jsr ent_set
  inx
  cpx #(LEVEL + 7 + LEVEL * 2)
  bcc @b
  stx nent
  rts

; vrow_make: A = world row -> colbuf[0..31] (32 tiles)
vrow_make:
  sta t0
  asl
  asl
  sta t1                 ; row * 4
  lda t0
  lsr
  lsr
  lsr
  sta t2                 ; row / 8
  ldy #0
@c:
  tya
  asl
  sta t3
  tya
  clc
  adc t3                 ; col * 3
  clc
  adc t1
  adc t0                 ; + row * 5
  sta t3
  tya
  ldx t2
  beq :+
  clc
  adc t2                 ; + (row/8) * col (cheap)
: eor t3
  and #$1F
  tax
  lda vtab,x
  sta colbuf,y
  iny
  cpy #32
  bcc @c
  rts
vtab: .byte 2,2,2,0,2,2,3,2,2,2,0,0,4,2,2,2,2,5,2,2,2,2,0,2,2,6,2,2,2,2,2,2

; vrow_addr: A = world row -> A:X = PPU address of that row (rows 0..29 nametable 0 at $2000, 30..59 nametable 2 at $2800)
vrow_addr:
@m:
  cmp #60
  bcc :+
  sec
  sbc #60
  jmp @m
: cmp #30
  bcc @top
  sec
  sbc #30
  jsr @a
  clc
  adc #$08
  rts
@top:
@a:
  sta t5
  lda #0
  sta t4
  lda t5
  asl
  asl
  asl
  asl
  asl
  tax
  lda t5
  lsr
  lsr
  lsr
  clc
  adc #$20
  rts

p8_upd:
  ; position: 2 px a frame, wrapping at 480
  clc
  lda vpos
  adc #2
  sta vpos
  bcc :+
  inc vpos+1
: lda vpos+1
  cmp #1
  bcc @nowrap
  lda vpos
  cmp #$E0
  bcc @nowrap
  lda #0
  sta vpos
  sta vpos+1
@nowrap:
  ; scy / nametable select from the position (0..479)
  lda vpos+1
  beq @n0
  lda vpos
  clc
  adc #16
  sta scy
  lda #2
  sta ppuctrl_b
  jmp @post
@n0:
  lda vpos
  cmp #240
  bcc @lo
  sec
  sbc #240
  sta scy
  lda #2
  sta ppuctrl_b
  jmp @post
@lo:
  sta scy
  lda #0
  sta ppuctrl_b
@post:
  ; a new row below the screen every 8 px (every 4th frame)
  lda frame
  and #3
  bne @nr
  lda vpos
  lsr
  lsr
  lsr
  sta t0                 ; row index (low part)
  lda vpos+1
  beq :+
  lda t0
  clc
  adc #32
  sta t0
: lda t0
  clc
  adc #30
  sta lastcol            ; world row to write (mod 60 inside vrow_addr)
  jsr vrow_make
  lda lastcol
  jsr vrow_addr
  sta t0
  stx t1
  ldx vlen
  lda t0
  sta vbuf,x
  inx
  lda t1
  sta vbuf,x
  inx
  lda #32
  sta vbuf,x
  inx
  ldy #0
: lda colbuf,y
  sta vbuf,x
  inx
  iny
  cpy #32
  bcc :-
  stx vlen
  lda #0
  sta vbuf,x
@nr:
  jsr update_ents
  jsr oam_begin
  jsr draw_ents
  rts

; ===========================================================  PHASE 3: SHOOTER (Gradius / R-Type pattern): 64 sprites, 8 per line overflow, bursts
p3_init:
  lda #$00
  sta ppuctrl_b          ; 8x8 sprites at $1000
  lda #<pal_shoot
  ldx #>pal_shoot
  jsr load_pal
  lda #2
  ldx #8
  jsr set_chr_bg
  lda #36
  ldx #40
  ldy #44
  sty t0
  ldy #48
  jsr set_chr_spr
  jsr clear_nt
  jsr init_world
  lda #<str_phase3
  sta ptr
  lda #>str_phase3
  sta ptr+1
  lda #$20
  ldx #$82
  jsr print_str
  lda #$1F
  sta snd_mask
  lda #96
  sta snd_row
  lda #0
  sta cam
  sta cam+1
  ; entity 0 = ship; 1..12 enemies; 13..(13+L*3) bullets; 16 particles last
  ldx #0
  lda #T_ENEMY
  sta t0
  lda #0
  sta t1
  lda #40
  sta t2
  lda #0
  sta t3
  lda #100
  sta t4
  lda #0
  sta t5
  sta t6
  lda #0
  jsr ent_set
  lda #T_PART
  ldx #1
@en:
  stx t7
  lda #T_ENEMY
  sta t0
  lda #1
  sta t1
  jsr rand
  ora #$80
  sta t2
  lda #0
  sta t3
  lda #60
  sta t4
  lda #$FF
  sta t5
  lda #0
  sta t6
  txa
  and #7
  clc
  adc #16
  ldx t7
  jsr ent_set
  txa
  and #7
  sta ent_aux,x
  asl
  asl
  asl
  asl
  sta ent_aux,x
  inx
  cpx #(LEVEL + 3)
  bcc @en
@bu:
  stx t7
  lda #T_BULLET
  sta t0
  lda #$11               ; small
  sta t1
  jsr rand
  and #$7F
  sta t2
  lda #0
  sta t3
  jsr rand
  and #$7F
  clc
  adc #40
  sta t4
  lda #5
  sta t5
  lda #0
  sta t6
  lda #32
  ldx t7
  jsr ent_set
  inx
  cpx #P3_BUL_END
  bcc @bu
  ldy #0
@pa:
  stx t7
  lda #T_PART
  sta t0
  lda #$10
  sta t1
  lda #0
  sta t2
  sta t3
  lda #$F8
  sta t4
  lda #0
  sta t5
  sta t6
  lda #33
  ldx t7
  jsr ent_set
  lda #0
  sta ent_aux,x
  inx
  iny
  cpy #16
  bcc @pa
  stx nent
  rts

p3_upd:
  lda #2
  jsr cam_scroll
  lda #0
  sta cam
  sta cam+1
  jsr stream_cols
  ; ship follows a sine path
  lda frame
  asl
  tay
  lda sine,y
  clc
  adc #128
  lsr
  clc
  adc #30
  sta ent_y
  jsr update_ents
  ; screen-space wrap for enemies
  ; burst every 90 frames
  lda frame
  and #$7F
  cmp #$40
  bne @nb
  jsr burst
@nb:
  lda frame
  and #3
  clc
  adc #36
  sta chr_r+2
  jsr oam_begin
  jsr draw_ents
  rts

burst:
  ; random burst point; 16 particles
  jsr rand
  and #$7F
  clc
  adc #80
  sta t2
  jsr rand
  and #$3F
  clc
  adc #60
  sta t4
  ldx #P3_BUL_END
@p:
  cpx nent
  bcs @d
  lda t2
  sta ent_xl,x
  lda #0
  sta ent_xh,x
  lda t4
  sta ent_y,x
  jsr rand
  and #7
  sec
  sbc #4
  sta ent_vxi,x
  jsr rand
  and #7
  sec
  sbc #5
  sta ent_vyi,x
  lda #0
  sta ent_vxf,x
  sta ent_vyf,x
  lda #40
  sta ent_aux,x
  inx
  jmp @p
@d:
  ; sfx: noise + DMC crash
  lda #$3F
  sta $400C
  lda #$0A
  sta $400E
  lda #$F8
  sta $400F
  lda #15
  sta snd_nvol
  lda dpcm_rate+2
  sta $4010
  lda dpcm_addr+2
  sta $4012
  lda dpcm_len+2
  sta $4013
  lda #$0F
  sta APU_STATUS
  lda #$1F
  sta APU_STATUS
  rts

; ===========================================================  PHASE 4: TOWN RPG (Dragon Quest / Zelda 2 pattern): slow scroll, NPCs, text box + IRQ split
p4_init:
  lda #1
  sta poll_spin
  lda #20
  sta col_rows           ; the text window (rows 24..29 of nametable 0) is not streamed over
  lda #6
  sta attr_end
  lda #$20
  sta ppuctrl_b
  lda #<pal_rpg
  ldx #>pal_rpg
  jsr load_pal
  lda #4
  ldx #8
  jsr set_chr_bg
  lda #22
  ldx #26
  ldy #30
  sty t0
  ldy #34
  jsr set_chr_spr
  jsr clear_nt
  jsr init_world
  ; text window frame rows 24..29 of nametable 0 (font tiles from the common set)
  lda #0
  sta PPUCTRL
  lda #$23
  sta PPUADDR
  lda #$00
  sta PPUADDR
  ldx #0
  lda #193
: sta PPUDATA
  inx
  cpx #32
  bcc :-
  ldy #4
@rows:
  lda #193
  sta PPUDATA
  ldx #0
  lda #128
: sta PPUDATA
  inx
  cpx #30
  bcc :-
  lda #193
  sta PPUDATA
  dey
  bne @rows
  ldx #0
  lda #193
: sta PPUDATA
  inx
  cpx #32
  bcc :-
  lda #<str_phase4
  sta ptr
  lda #>str_phase4
  sta ptr+1
  lda #$20
  ldx #$82
  jsr print_str
  lda #$07
  sta snd_mask
  lda #128
  sta snd_row
  lda #0
  sta txtpos
  sta gstate
  ; NPCs
  ldx #0
@n:
  stx t7
  lda #T_NPC
  sta t0
  lda #0
  sta t1
  txa
  asl
  asl
  asl
  asl
  sta t2
  txa
  clc
  adc t2
  adc t2
  sta t2
  lda #0
  sta t3
  lda #100
  sta t4
  lda #0
  sta t5
  sta t6
  txa
  and #15
  ldx t7
  jsr ent_set
  lda #8
  sta ent_aux,x
  inx
  cpx #(LEVEL * 2 + 4)
  bcc @n
  stx nent
  rts

p4_upd:
  lda frame
  and #1
  jsr cam_scroll
  jsr stream_cols
  jsr update_ents
  jsr respawn_left
  jsr oam_begin
  jsr draw_ents
  ; IRQ split for the text window at scanline 191
  lda #190
  sta band_len
  lda #0
  sta band_x+1
  lda ppuctrl_b
  ora #$88
  sta band_ctrl+1
  lda chr_r
  sta band_chr+1
  lda #2
  sta nbands
  ; typed text, one character per 4 frames
  lda frame
  and #3
  beq @tx
  jmp @pal
@tx:
  lda gstate
  bne @hold
  ldx txtpos
  lda str_dialog,x
  bne @put
  lda #1
  sta gstate
  lda #0
  sta sub
  jmp @pal
@put:
  pha
  ; row = 25 + pos/28, col = 2 + pos%28
  txa
  ldy #0
: cmp #28
  bcc :+
  sbc #28
  iny
  bne :-
: clc
  adc #$22 + 32             ; $2322 = row 25 col 2... computed below with y rows
  sta t0
  tya
  asl
  asl
  asl
  asl
  asl
  clc
  adc t0
  sta t0
  lda #$23
  adc #0
  ldx vlen
  sta vbuf,x
  inx
  lda t0
  sta vbuf,x
  inx
  lda #1
  sta vbuf,x
  inx
  pla
  clc
  adc #96
  sta vbuf,x
  inx
  stx vlen
  lda #0
  sta vbuf,x
  inc txtpos
  jmp @pal
@hold:
  inc sub
  lda sub
  cmp #20
  bcc @pal
  ; clear the window text (4 rows x 28) and start over
  lda #0
  sta gstate
  sta txtpos
  ldy #0
@clr:
  ldx vlen
  lda #$23
  sta vbuf,x
  inx
  tya
  asl
  asl
  asl
  asl
  asl
  clc
  adc #$22 + 32
  sta vbuf,x
  inx
  lda #28
  sta vbuf,x
  inx
  lda #128
  sta t1
  ldy #28
: lda t1
  sta vbuf,x
  inx
  dey
  bne :-
  stx vlen
  lda #0
  sta vbuf,x
  ; the loop variable lives in t3
  inc t3
  ldy t3
  cpy #4
  bcc @clr
  lda #0
  sta t3
@pal:
  ; day/night: background colour every 8 frames
  lda frame
  and #7
  bne @x
  ldx vlen
  lda #$3F
  sta vbuf,x
  inx
  lda #$00
  sta vbuf,x
  inx
  lda #1
  sta vbuf,x
  inx
  lda frame+1
  and #3
  tay
  lda ptimer
  lda daycol,y
  sta vbuf,x
  inx
  stx vlen
  lda #0
  sta vbuf,x
@x:
  rts
daycol: .byte $29, $2A, $19, $1A

; ===========================================================  PHASE 5: FIGHTER (digitised-drum pattern): DMC streaming, $4011 writes, bank-animated sprites
p5_init:
  lda #$00
  sta ppuctrl_b
  lda #<pal_fight
  ldx #>pal_fight
  jsr load_pal
  lda #6
  ldx #8
  jsr set_chr_bg
  lda #38
  ldx #42
  ldy #46
  sty t0
  ldy #50
  jsr set_chr_spr
  jsr clear_nt
  jsr init_world
  lda #<str_phase5
  sta ptr
  lda #>str_phase5
  sta ptr+1
  lda #$20
  ldx #$82
  jsr print_str
  lda #$1F
  sta snd_mask
  lda #160
  sta snd_row
  ; two fighters, each three stacked 2x2 parts, plus 8 sparks
  ldx #0
@f:
  stx t7
  lda #T_ENEMY
  sta t0
  lda #0
  sta t1
  txa
  and #1
  asl
  asl
  asl
  asl
  asl
  asl
  clc
  adc #70
  sta t2
  lda #0
  sta t3
  txa
  lsr
  clc
  adc #1
  asl
  asl
  asl
  asl
  asl
  clc
  adc #60
  sta t4
  lda #0
  sta t5
  sta t6
  txa
  ldx t7
  jsr ent_set
  lda #0
  sta ent_aux,x
  inx
  cpx #6
  bcc @f
  stx nent
  lda #$4F
  sta $4010                 ; DMC looping at rate F? (loop flag)
  lda #0
  sta dmc_idx
  rts

p5_upd:
  lda #1
  jsr cam_scroll
  lda #0
  sta cam
  sta cam+1
  jsr stream_cols
  ; stack positions
  ldx #0
@pos:
  lda ent_aux,x
  inx
  cpx #6
  bcc @pos
  jsr update_ents
  ; sprite banks as animation frames
  lda frame
  lsr
  lsr
  and #7
  asl
  clc
  adc #38
  sta chr_r+2
  clc
  adc #1
  sta chr_r+3
  ; direct-load PCM writes ($4011): a short pattern every frame, and status polling
  ldx #0
@pcm:
  lda frame
  clc
  adc dmc_idx
  tay
  lda sine,y
  lsr
  lsr
  clc
  adc #30
  sta $4011
  inc dmc_idx
  inx
  cpx #(LEVEL + 3)
  bcc @pcm
  lda $4015
  sta t0
  ; PRG bank animation reads (8 swaps per frame)
  ldx #0
@bk:
  stx t1
  txa
  and #3
  sta t2
  lda #7
  sta M3_SEL
  sta bsel
  lda t2
  sta M3_DAT
  lda $A000,x
  eor hash
  sta t3
  inx
  cpx #8
  bcc @bk
  ; screen shake on hits
  lda frame
  and #$1F
  cmp #4
  bcs @ns
  and #1
  sta scy
  jmp @dr
@ns:
  lda #0
  sta scy
@dr:
  jsr oam_begin
  jsr draw_ents
  rts

; ===========================================================  PHASE 6: LOADS (scene transitions, render off, bulk uploads, RAM-executed code, SRAM writes)
p6_init:
  lda #1
  sta poll_spin
  lda #$00
  sta ppuctrl_b
  lda #<pal_plat
  ldx #>pal_plat
  jsr load_pal
  lda #0
  ldx #8
  jsr set_chr_bg
  lda #22
  ldx #26
  ldy #30
  sty t0
  ldy #34
  jsr set_chr_spr
  jsr clear_nt
  jsr init_world
  lda #<str_phase6
  sta ptr
  lda #>str_phase6
  sta ptr+1
  lda #$20
  ldx #$82
  jsr print_str
  lda #$0F
  sta snd_mask
  lda #192
  sta snd_row
  lda #0
  sta gstate
  sta sub
  ldx #0
@e:
  stx t7
  lda #T_FLY
  sta t0
  lda #0
  sta t1
  jsr rand
  sta t2
  lda #0
  sta t3
  sta t4
  lda #$FF
  sta t5
  lda #0
  sta t6
  txa
  and #15
  ldx t7
  jsr ent_set
  inx
  cpx #8
  bcc @e
  stx nent
  rts

p6_upd:
  inc sub
  lda gstate
  bne @fade
  lda sub
  cmp #200
  bcc @run
  ; --- a scene load: render off, bulk upload, RAM code ---
  lda #0
  sta sub
  lda #1
  sta gstate
  jsr scene_load
  rts
@fade:
  ; fade-in: gstate 1..4 brightness steps, 2 frames each
  lda sub
  lsr
  clc
  adc #1
  cmp #5
  bcs @fdone
  sta t0
  jsr fade_pal
  jmp @run
@fdone:
  lda #0
  sta gstate
  sta sub
@run:
  lda #1
  jsr cam_scroll
  lda #0
  sta cam
  sta cam+1
  jsr stream_cols
  jsr update_ents_wrap
  jsr oam_begin
  jsr draw_ents
  rts

; fade_pal: t0 = step 1..4 (4 = full): write plat palette dimmed by (4-t0)*$10 through the update buffer
fade_pal:
  ldx vlen
  lda #$3F
  sta vbuf,x
  inx
  lda #0
  sta vbuf,x
  inx
  lda #32
  sta vbuf,x
  inx
  lda #4
  sec
  sbc t0
  asl
  asl
  asl
  asl
  sta t1
  ldy #0
@p:
  lda pal_plat,y
  sec
  sbc t1
  bcs :+
  lda #$0F
: and #$3F
  sta vbuf,x
  inx
  iny
  cpy #32
  bcc @p
  stx vlen
  lda #0
  sta vbuf,x
  rts

scene_load:
  lda #0
  sta PPUMASK
  sta render_on
  sta PPUCTRL
  ; a new random stretch of the world
  jsr rand
  and #$0F
  sta t0
  lda #<(pal_plat)
  ldx #>(pal_plat)
  jsr load_pal
  jsr clear_nt
  ; bulk nametable uploads with tile-by-tile loops (two nametables, 1 KiB each)
  lda #$20
  sta PPUADDR
  lda #0
  sta PPUADDR
  ldx #0
  ldy #4
: lda t0
  clc
  adc sine,x
  sta PPUDATA
  inx
  bne :-
  dey
  bne :-
  lda #$24
  sta PPUADDR
  lda #0
  sta PPUADDR
  ldx #0
  ldy #4
: txa
  eor t0
  and #3
  sta PPUDATA
  inx
  bne :-
  dey
  bne :-
  jsr init_world
  ; RAM-executed routine in PRG-RAM: copy, patch, call, then SRAM table writes
  ldx #14
: lda ramcode,x
  sta SRAM+$100,x
  dex
  bpl :-
  lda #<sine
  sta SRAM+$100+3
  lda #>sine
  sta SRAM+$100+4
  lda t0
  sta SRAM+$100+7
  lda #$00
  sta SRAM+$100+9
  lda #$63
  sta SRAM+$100+10
  jsr SRAM+$100
  lda SRAM+$300
  eor hash
  sta hash
  ; the fade-in follows
  lda ppuctrl_b
  ora #$88
  sta PPUCTRL
  lda #1
  sta render_on
  lda #$1E
  sta PPUMASK
  lda #1
  sta t0
  jsr fade_pal
  rts
ramcode: .byte $A0,$00,$B9,$00,$00,$18,$69,$00,$99,$00,$00,$C8,$D0,$F4,$60

; ---------------------------------------------------------------- vectors
.segment "VECTORS"
  .word nmi, reset, irq

; ---------------------------------------------------------------- CHR
.segment "CHRROM"
  .incbin "chr.bin"
.segment "DPCM"
  .incbin "dpcm.bin"
