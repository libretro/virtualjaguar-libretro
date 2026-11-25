ENTRY(_start)

MEMORY
{
	ram (rwx) : ORIGIN = 0x4000, LENGTH = 0x1FC000
}

PHDRS
{
	image PT_LOAD FLAGS(5);
	bss PT_LOAD FLAGS(6);
}

SECTIONS
{
	.image : {
		KEEP(*(.text.start))
		*(.text .text.*)
		*(.rodata .rodata.*)
		*(.data .data.*)
	} > ram :image

	.bss (NOLOAD) : {
		*(.bss .bss.*)
		*(COMMON)
	} > ram :bss
}
