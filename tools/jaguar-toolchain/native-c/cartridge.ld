ENTRY(_start)

MEMORY
{
	rom (rx) : ORIGIN = 0x800000, LENGTH = 0x5FFF00
	ram (rw) : ORIGIN = 0x004000, LENGTH = 0x1FC000
}

PHDRS
{
	image PT_LOAD FLAGS(5);
	bss PT_LOAD FLAGS(6);
}

SECTIONS
{
	.image : {
		/* Cartridge marker and entry vector read by the emulator's HLE boot. */
		. = 0x400;
		LONG(0x04040404)
		LONG(_start)
		. = 0x2000;
		KEEP(*(.text.start))
		*(.text .text.*)
		*(.rodata .rodata.*)
	} > rom :image

	.bss (NOLOAD) : {
		*(.bss .bss.*)
	} > ram :bss

	ASSERT(SIZEOF(.data) == 0, "Add startup copying before using initialized RAM")
	.data : { *(.data .data.*) } > ram :bss
}
