; Fixture for app/tests/debug_symbols_test.cpp (#1195): one symbol of every kind rln emits.
	.68000
	.globl	_start
	.globl	main_loop
	.globl	BORDER_COLOR
	.globl	frame_table
	.globl	frame_count
	.globl	gpu_blit
	.globl	gpu_end
	.globl	a_rather_long_symbol_name_past_eight_chars

BORDER_COLOR	.equ	$F0002A

	.text
_start:
	move.w	#$2700,sr
	lea	frame_table,a0
main_loop:
	addq.l	#1,frame_count
local_label:
	bra.s	main_loop
a_rather_long_symbol_name_past_eight_chars:
	rts

	.gpu
	.org	$F03000
gpu_blit:
	moveq	#1,r0
	nop
gpu_end:
	.68000

	.data
frame_table:
	.dc.l	1,2,3,4

	.bss
frame_count:
	.ds.l	1
	.end
