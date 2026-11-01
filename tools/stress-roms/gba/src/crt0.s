@ gbastress crt0: ROM header + start-up (own code). GPL-3.0.
	.section .crt0, "ax"
	.arm
	.global _start
_start:
	b	rom_start
	.fill	156, 1, 0		@ 0x04-0x9F: the logo area, left empty (emulators boot without it)
	.ascii	"GBASTRESS   "		@ 0xA0 title
	.ascii	"BSTE"			@ 0xAC game code
	.ascii	"00"			@ 0xB0 maker
	.byte	0x96, 0, 0		@ 0xB2 fixed, unit, device
	.fill	7, 1, 0
	.byte	0			@ 0xBC version
	.byte	0			@ 0xBD complement check (build.py)
	.byte	0, 0
rom_start:				@ 0xC0
	mov	r0, #0x12		@ IRQ mode
	msr	cpsr_c, r0
	ldr	sp, =0x03007FA0
	mov	r0, #0x13		@ SVC mode
	msr	cpsr_c, r0
	ldr	sp, =0x03007FE0
	mov	r0, #0x1F		@ System mode
	msr	cpsr_c, r0
	ldr	sp, =0x03007F00
	ldr	r0, =__iwram_lma
	ldr	r1, =__iwram_start
	ldr	r2, =__iwram_end
	bl	copy
	ldr	r0, =__ewram_lma
	ldr	r1, =__ewram_start
	ldr	r2, =__ewram_end
	bl	copy
	ldr	r1, =__bss_start
	ldr	r2, =__bss_end
	bl	zero
	ldr	r1, =__ewbss_start
	ldr	r2, =__ewbss_end
	bl	zero
	ldr	r0, =main
	mov	lr, pc
	bx	r0
	b	.
copy:	cmp	r1, r2
	ldrlo	r3, [r0], #4
	strlo	r3, [r1], #4
	blo	copy
	bx	lr
zero:	mov	r3, #0
1:	cmp	r1, r2
	strlo	r3, [r1], #4
	blo	1b
	bx	lr
	.ltorg

@ the EABI division helpers that return quotient and remainder (r0, r1): the quotient from the C helpers
	.arm
	.global __aeabi_uidivmod
__aeabi_uidivmod:
	push	{r0, r1, lr}
	bl	__aeabi_uidiv
	pop	{r1, r2}
	mul	r3, r0, r2
	sub	r1, r1, r3
	pop	{lr}
	bx	lr
	.global __aeabi_idivmod
__aeabi_idivmod:
	push	{r0, r1, lr}
	bl	__aeabi_idiv
	pop	{r1, r2}
	mul	r3, r0, r2
	sub	r1, r1, r3
	pop	{lr}
	bx	lr
