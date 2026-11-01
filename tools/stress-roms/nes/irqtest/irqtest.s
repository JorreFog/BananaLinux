; irqtest: mapper 73 (VRC3) cycle-counting IRQ test ROM for BananaNES (GPL-3.0-or-later). The IRQ fires every ~6000 CPU
; cycles, i.e. at a different scanline and dot each frame; its handler changes the background colour mid-frame, so the
; picture depends on the exact cycle the emulator raises the IRQ: a differential test of the mapper-IRQ hook path.
.p02
.segment "HEADER"
  .byte "NES", $1A, 2, 0, $91, $40, 0
  .res 7, 0
.segment "ZEROPAGE"
col: .res 1
fcnt: .res 1
.segment "BSS"
.segment "PRG0SEG"
  .byte "IRQTEST"
.segment "CODE"
reset:
  sei
  cld
  ldx #$FF
  txs
  inx
  stx $2000
  stx $2001
  stx $4015
  lda #$40
  sta $4017
: bit $2002
  bpl :-
: bit $2002
  bpl :-
  ; palette: bg colour 0 and a few
  lda #$3F
  sta $2006
  lda #0
  sta $2006
  ldx #0
: lda pal,x
  sta $2007
  inx
  cpx #8
  bne :-
  ; a few tiles into CHR-RAM so the screen is not empty: tile 1 = solid colour 1, tile 2 = checker
  lda #0
  sta $2006
  sta $2006
  ldx #16
  lda #0
: sta $2007
  dex
  bne :-
  ldx #8
  lda #$00
: sta $2007
  dex
  bne :-
  ldx #8
  lda #0
: sta $2007
  dex
  bne :-
  ldx #8
: lda #$AA
  sta $2007
  lda #$55
  sta $2007
  dex
  bne :-
  ldx #8
  lda #0
: sta $2007
  dex
  bne :-
  ; nametable: columns of tiles 1/2
  lda #$20
  sta $2006
  lda #0
  sta $2006
  ldy #30
@row:
  ldx #32
@col:
  txa
  and #1
  clc
  adc #1
  sta $2007
  dex
  bne @col
  dey
  bne @row
  ; VRC3 IRQ: 16-bit mode, reload $E8A0 (about 5984 cycles between IRQs), enabled
  lda #$00
  sta $8000
  lda #$0A
  sta $9000
  lda #$08
  sta $A000
  lda #$0E
  sta $B000
  lda #3
  sta $C000
  lda #0
  sta $2005
  sta $2005
  lda #$80
  sta $2000
  lda #$0A
  sta $2001
  cli
main:
  jmp main

nmi:
  pha
  inc fcnt
  ; colour emphasis bits cycle every 16 frames (0 = none for 1/8 of the time): the emphasis path of the PPU
  lda fcnt
  lsr
  lsr
  lsr
  lsr
  and #7
  asl
  asl
  asl
  asl
  asl
  ora #$0A
  sta $2001
  lda #0
  sta $2005
  sta $2005
  ; a sound blip every 32 frames (exercises the APU hook next to the mapper hook)
  lda fcnt
  and #$1F
  bne :+
  lda #$0F
  sta $4015
  lda #$9F
  sta $4000
  lda fcnt
  sta $4002
  lda #$08
  sta $4003
:
  pla
  rti

irq:
  pha
  sta $D000          ; acknowledge (autoenable bit set: counting goes on)
  lda #$3F
  sta $2006
  lda #0
  sta $2006
  inc col
  lda col
  and #$0F
  clc
  adc #$11
  sta $2007
  lda #$20           ; mid-frame address back into the (uniform) name table: the picture goes on
  sta $2006
  lda #0
  sta $2006
  pla
  rti

pal: .byte $0F, $30, $16, $27, $0F, $12, $1A, $28

.segment "VECTORS"
  .word nmi, reset, irq
