	.section .text.start,"ax"
	.globl _start
_start:
	move.w	#0x2700,%sr
	move.l	#0x200000,%sp
	jsr	sample_main
1:	bra.s	1b

	.section .note.GNU-stack,""
