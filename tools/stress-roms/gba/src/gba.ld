/* gbastress link map. IWRAM above 0x03007000 = stacks; EWRAM above 0x0203FE00 = results + companion probe block. */
ENTRY(_start)
MEMORY {
	rom   : ORIGIN = 0x08000000, LENGTH = 32M
	iwram : ORIGIN = 0x03000000, LENGTH = 0x7000
	ewram : ORIGIN = 0x02000000, LENGTH = 0x3FE00
}
SECTIONS {
	.crt0 : { KEEP(*(.crt0)) } > rom
	.text : { *(.text*) } > rom
	.rodata : { *(.rodata*) . = ALIGN(4); } > rom
	.ovl_a : { __ovl_a_start = .; KEEP(*(.ovl_a*)) . = ALIGN(4); __ovl_a_end = .; } > rom
	.ovl_b : { __ovl_b_start = .; KEEP(*(.ovl_b*)) . = ALIGN(4); __ovl_b_end = .; } > rom
	.iwram : { __iwram_start = .; *(.iwram*) *(.data*) . = ALIGN(4); __iwram_end = .; } > iwram AT > rom
	__iwram_lma = LOADADDR(.iwram);
	.bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > iwram
	.ewram : { __ewram_start = .; *(.ewram*) . = ALIGN(4); __ewram_end = .; } > ewram AT > rom
	__ewram_lma = LOADADDR(.ewram);
	.ewram_bss (NOLOAD) : { __ewbss_start = .; *(.ewram_bss*) . = ALIGN(4); __ewbss_end = .; } > ewram
	/DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
