//
// TOM Processing
//
// Originally by David Raingeard (cal2)
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Caz (BeOS)
// Cleanups and endian wrongness amelioration by James Hammons
// (C) 2010 Underground Software
//
// JLH = James Hammons <jlhamm@acm.org>
//
// Who  When        What
// ---  ----------  -------------------------------------------------------------
// JLH  01/16/2010  Created this log ;-)
// JLH  01/20/2011  Change rendering to RGBA, removed unnecessary code
//
// Note: Endian wrongness probably stems from the MAME origins of this emu and
//       the braindead way in which MAME used to handle memory. :-}
//
// Note: TOM has only a 16K memory space
//
//	------------------------------------------------------------
//	TOM REGISTERS (Mapped by Aaron Giles)
//	------------------------------------------------------------
//	F00000-F0FFFF   R/W   xxxxxxxx xxxxxxxx   Internal Registers
//	F00000          R/W   -x-xx--- xxxxxxxx   MEMCON1 - memory config reg 1
//	                      -x------ --------      (CPU32 - is the CPU 32bits?)
//	                      ---xx--- --------      (IOSPEED - external I/O clock cycles)
//	                      -------- x-------      (FASTROM - reduces ROM clock cycles)
//	                      -------- -xx-----      (DRAMSPEED - sets RAM clock cycles)
//	                      -------- ---xx---      (ROMSPEED - sets ROM clock cycles)
//	                      -------- -----xx-      (ROMWIDTH - sets width of ROM: 8,16,32,64 bits)
//	                      -------- -------x      (ROMHI - controls ROM mapping)
//	F00002          R/W   --xxxxxx xxxxxxxx   MEMCON2 - memory config reg 2
//	                      --x----- --------      (HILO - image display bit order)
//	                      ---x---- --------      (BIGEND - big endian addressing?)
//	                      ----xxxx --------      (REFRATE - DRAM refresh rate)
//	                      -------- xx------      (DWIDTH1 - DRAM1 width: 8,16,32,64 bits)
//	                      -------- --xx----      (COLS1 - DRAM1 columns: 256,512,1024,2048)
//	                      -------- ----xx--      (DWIDTH0 - DRAM0 width: 8,16,32,64 bits)
//	                      -------- ------xx      (COLS0 - DRAM0 columns: 256,512,1024,2048)
//	F00004          R/W   -----xxx xxxxxxxx   HC - horizontal count
//	                      -----x-- --------      (which half of the display)
//	                      ------xx xxxxxxxx      (10-bit counter)
//	F00006          R/W   ----xxxx xxxxxxxx   VC - vertical count
//	                      ----x--- --------      (which field is being generated)
//	                      -----xxx xxxxxxxx      (11-bit counter)
//	F00008          R     -----xxx xxxxxxxx   LPH - light pen horizontal position
//	F0000A          R     -----xxx xxxxxxxx   LPV - light pen vertical position
//	F00010-F00017   R     xxxxxxxx xxxxxxxx   OB - current object code from the graphics processor
//	F00020-F00023     W   xxxxxxxx xxxxxxxx   OLP - start of the object list
//	F00026            W   -------- -------x   OBF - object processor flag
//	F00028            W   ----xxxx xxxxxxxx   VMODE - video mode
//	                  W   ----xxx- --------      (PWIDTH1-8 - width of pixel in video clock cycles)
//	                  W   -------x --------      (VARMOD - enable variable color resolution)
//	                  W   -------- x-------      (BGEN - clear line buffer to BG color)
//	                  W   -------- -x------      (CSYNC - enable composite sync on VSYNC)
//	                  W   -------- --x-----      (BINC - local border color if INCEN)
//	                  W   -------- ---x----      (INCEN - encrustation enable)
//	                  W   -------- ----x---      (GENLOCK - enable genlock)
//	                  W   -------- -----xx-      (MODE - CRY16,RGB24,DIRECT16,RGB16)
//	                  W   -------- -------x      (VIDEN - enables video)
//	F0002A            W   xxxxxxxx xxxxxxxx   BORD1 - border color (red/green)
//	F0002C            W   -------- xxxxxxxx   BORD2 - border color (blue)
//	F0002E            W   ------xx xxxxxxxx   HP - horizontal period
//	F00030            W   -----xxx xxxxxxxx   HBB - horizontal blanking begin
//	F00032            W   -----xxx xxxxxxxx   HBE - horizontal blanking end
//	F00034            W   -----xxx xxxxxxxx   HSYNC - horizontal sync
//	F00036            W   ------xx xxxxxxxx   HVS - horizontal vertical sync
//	F00038            W   -----xxx xxxxxxxx   HDB1 - horizontal display begin 1
//	F0003A            W   -----xxx xxxxxxxx   HDB2 - horizontal display begin 2
//	F0003C            W   -----xxx xxxxxxxx   HDE - horizontal display end
//	F0003E            W   -----xxx xxxxxxxx   VP - vertical period
//	F00040            W   -----xxx xxxxxxxx   VBB - vertical blanking begin
//	F00042            W   -----xxx xxxxxxxx   VBE - vertical blanking end
//	F00044            W   -----xxx xxxxxxxx   VS - vertical sync
//	F00046            W   -----xxx xxxxxxxx   VDB - vertical display begin
//	F00048            W   -----xxx xxxxxxxx   VDE - vertical display end
//	F0004A            W   -----xxx xxxxxxxx   VEB - vertical equalization begin
//	F0004C            W   -----xxx xxxxxxxx   VEE - vertical equalization end
//	F0004E            W   -----xxx xxxxxxxx   VI - vertical interrupt
//	F00050            W   xxxxxxxx xxxxxxxx   PIT0 - programmable interrupt timer 0
//	F00052            W   xxxxxxxx xxxxxxxx   PIT1 - programmable interrupt timer 1
//	F00054            W   ------xx xxxxxxxx   HEQ - horizontal equalization end
//	F00058            W   xxxxxxxx xxxxxxxx   BG - background color
//	F000E0          R/W   ---xxxxx ---xxxxx   INT1 - CPU interrupt control register
//	                      ---x---- --------      (C_JERCLR - clear pending Jerry ints)
//	                      ----x--- --------      (C_PITCLR - clear pending PIT ints)
//	                      -----x-- --------      (C_OPCLR - clear pending object processor ints)
//	                      ------x- --------      (C_GPUCLR - clear pending graphics processor ints)
//	                      -------x --------      (C_VIDCLR - clear pending video timebase ints)
//	                      -------- ---x----      (C_JERENA - enable Jerry ints)
//	                      -------- ----x---      (C_PITENA - enable PIT ints)
//	                      -------- -----x--      (C_OPENA - enable object processor ints)
//	                      -------- ------x-      (C_GPUENA - enable graphics processor ints)
//	                      -------- -------x      (C_VIDENA - enable video timebase ints)
//	F000E2            W   -------- --------   INT2 - CPU interrupt resume register
//	F00400-F005FF   R/W   xxxxxxxx xxxxxxxx   CLUT - color lookup table A
//	F00600-F007FF   R/W   xxxxxxxx xxxxxxxx   CLUT - color lookup table B
//	F00800-F00D9F   R/W   xxxxxxxx xxxxxxxx   LBUF - line buffer A
//	F01000-F0159F   R/W   xxxxxxxx xxxxxxxx   LBUF - line buffer B
//	F01800-F01D9F   R/W   xxxxxxxx xxxxxxxx   LBUF - line buffer currently selected
//	------------------------------------------------------------
//	F02000-F021FF   R/W   xxxxxxxx xxxxxxxx   GPU control registers
//	F02100          R/W   xxxxxxxx xxxxxxxx   G_FLAGS - GPU flags register
//	                R/W   x------- --------      (DMAEN - DMA enable)
//	                R/W   -x------ --------      (REGPAGE - register page)
//	                  W   --x----- --------      (G_BLITCLR - clear blitter interrupt)
//	                  W   ---x---- --------      (G_OPCLR - clear object processor int)
//	                  W   ----x--- --------      (G_PITCLR - clear PIT interrupt)
//	                  W   -----x-- --------      (G_JERCLR - clear Jerry interrupt)
//	                  W   ------x- --------      (G_CPUCLR - clear CPU interrupt)
//	                R/W   -------x --------      (G_BLITENA - enable blitter interrupt)
//	                R/W   -------- x-------      (G_OPENA - enable object processor int)
//	                R/W   -------- -x------      (G_PITENA - enable PIT interrupt)
//	                R/W   -------- --x-----      (G_JERENA - enable Jerry interrupt)
//	                R/W   -------- ---x----      (G_CPUENA - enable CPU interrupt)
//	                R/W   -------- ----x---      (IMASK - interrupt mask)
//	                R/W   -------- -----x--      (NEGA_FLAG - ALU negative)
//	                R/W   -------- ------x-      (CARRY_FLAG - ALU carry)
//	                R/W   -------- -------x      (ZERO_FLAG - ALU zero)
//	F02104            W   -------- ----xxxx   G_MTXC - matrix control register
//	                  W   -------- ----x---      (MATCOL - column/row major)
//	                  W   -------- -----xxx      (MATRIX3-15 - matrix width)
//	F02108            W   ----xxxx xxxxxx--   G_MTXA - matrix address register
//	F0210C            W   -------- -----xxx   G_END - data organization register
//	                  W   -------- -----x--      (BIG_INST - big endian instruction fetch)
//	                  W   -------- ------x-      (BIG_PIX - big endian pixels)
//	                  W   -------- -------x      (BIG_IO - big endian I/O)
//	F02110          R/W   xxxxxxxx xxxxxxxx   G_PC - GPU program counter
//	F02114          R/W   xxxxxxxx xx-xxxxx   G_CTRL - GPU control/status register
//	                R     xxxx---- --------      (VERSION - GPU version code)
//	                R/W   ----x--- --------      (BUS_HOG - hog the bus!)
//	                R/W   -----x-- --------      (G_BLITLAT - blitter interrupt latch)
//	                R/W   ------x- --------      (G_OPLAT - object processor int latch)
//	                R/W   -------x --------      (G_PITLAT - PIT interrupt latch)
//	                R/W   -------- x-------      (G_JERLAT - Jerry interrupt latch)
//	                R/W   -------- -x------      (G_CPULAT - CPU interrupt latch)
//	                R/W   -------- ---x----      (SINGLE_GO - single step one instruction)
//	                R/W   -------- ----x---      (SINGLE_STEP - single step mode)
//	                R/W   -------- -----x--      (FORCEINT0 - cause interrupt 0 on GPU)
//	                R/W   -------- ------x-      (CPUINT - send GPU interrupt to CPU)
//	                R/W   -------- -------x      (GPUGO - enable GPU execution)
//	F02118-F0211B   R/W   xxxxxxxx xxxxxxxx   G_HIDATA - high data register
//	F0211C-F0211F   R     xxxxxxxx xxxxxxxx   G_REMAIN - divide unit remainder
//	F0211C            W   -------- -------x   G_DIVCTRL - divide unit control
//	                  W   -------- -------x      (DIV_OFFSET - 1=16.16 divide, 0=32-bit divide)
//	------------------------------------------------------------
//	BLITTER REGISTERS
//	------------------------------------------------------------
//	F02200-F022FF   R/W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   Blitter registers
//	F02200            W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   A1_BASE - A1 base register
//	F02204            W   -------- ---xxxxx -xxxxxxx xxxxx-xx   A1_FLAGS - A1 flags register
//	                  W   -------- ---x---- -------- --------      (YSIGNSUB - invert sign of Y delta)
//	                  W   -------- ----x--- -------- --------      (XSIGNSUB - invert sign of X delta)
//	                  W   -------- -----x-- -------- --------      (Y add control)
//	                  W   -------- ------xx -------- --------      (X add control)
//	                  W   -------- -------- -xxxxxx- --------      (width in 6-bit floating point)
//	                  W   -------- -------- -------x xx------      (ZOFFS1-6 - Z data offset)
//	                  W   -------- -------- -------- --xxx---      (PIXEL - pixel size)
//	                  W   -------- -------- -------- ------xx      (PITCH1-4 - data phrase pitch)
//	F02208            W   -xxxxxxx xxxxxxxx -xxxxxxx xxxxxxxx   A1_CLIP - A1 clipping size
//	                  W   -xxxxxxx xxxxxxxx -------- --------      (height)
//	                  W   -------- -------- -xxxxxxx xxxxxxxx      (width)
//	F0220C          R/W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   A1_PIXEL - A1 pixel pointer
//	                R/W   xxxxxxxx xxxxxxxx -------- --------      (Y pixel value)
//	                R/W   -------- -------- xxxxxxxx xxxxxxxx      (X pixel value)
//	F02210            W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   A1_STEP - A1 step value
//	                  W   xxxxxxxx xxxxxxxx -------- --------      (Y step value)
//	                  W   -------- -------- xxxxxxxx xxxxxxxx      (X step value)
//	F02214            W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   A1_FSTEP - A1 step fraction value
//	                  W   xxxxxxxx xxxxxxxx -------- --------      (Y step fraction value)
//	                  W   -------- -------- xxxxxxxx xxxxxxxx      (X step fraction value)
//	F02218          R/W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   A1_FPIXEL - A1 pixel pointer fraction
//	                R/W   xxxxxxxx xxxxxxxx -------- --------      (Y pixel fraction value)
//	                R/W   -------- -------- xxxxxxxx xxxxxxxx      (X pixel fraction value)
//	F0221C            W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   A1_INC - A1 increment
//	                  W   xxxxxxxx xxxxxxxx -------- --------      (Y increment)
//	                  W   -------- -------- xxxxxxxx xxxxxxxx      (X increment)
//	F02220            W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   A1_FINC - A1 increment fraction
//	                  W   xxxxxxxx xxxxxxxx -------- --------      (Y increment fraction)
//	                  W   -------- -------- xxxxxxxx xxxxxxxx      (X increment fraction)
//	F02224            W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   A2_BASE - A2 base register
//	F02228            W   -------- ---xxxxx -xxxxxxx xxxxx-xx   A2_FLAGS - A2 flags register
//	                  W   -------- ---x---- -------- --------      (YSIGNSUB - invert sign of Y delta)
//	                  W   -------- ----x--- -------- --------      (XSIGNSUB - invert sign of X delta)
//	                  W   -------- -----x-- -------- --------      (Y add control)
//	                  W   -------- ------xx -------- --------      (X add control)
//	                  W   -------- -------- -xxxxxx- --------      (width in 6-bit floating point)
//	                  W   -------- -------- -------x xx------      (ZOFFS1-6 - Z data offset)
//	                  W   -------- -------- -------- --xxx---      (PIXEL - pixel size)
//	                  W   -------- -------- -------- ------xx      (PITCH1-4 - data phrase pitch)
//	F0222C            W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   A2_MASK - A2 window mask
//	F02230          R/W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   A2_PIXEL - A2 pixel pointer
//	                R/W   xxxxxxxx xxxxxxxx -------- --------      (Y pixel value)
//	                R/W   -------- -------- xxxxxxxx xxxxxxxx      (X pixel value)
//	F02234            W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   A2_STEP - A2 step value
//	                  W   xxxxxxxx xxxxxxxx -------- --------      (Y step value)
//	                  W   -------- -------- xxxxxxxx xxxxxxxx      (X step value)
//	F02238            W   -xxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   B_CMD - command register
//	                  W   -x------ -------- -------- --------      (SRCSHADE - modify source intensity)
//	                  W   --x----- -------- -------- --------      (BUSHI - hi priority bus)
//	                  W   ---x---- -------- -------- --------      (BKGWREN - writeback destination)
//	                  W   ----x--- -------- -------- --------      (DCOMPEN - write inhibit from data comparator)
//	                  W   -----x-- -------- -------- --------      (BCOMPEN - write inhibit from bit coparator)
//	                  W   ------x- -------- -------- --------      (CMPDST - compare dest instead of src)
//	                  W   -------x xxx----- -------- --------      (logical operation)
//	                  W   -------- ---xxx-- -------- --------      (ZMODE - Z comparator mode)
//	                  W   -------- ------x- -------- --------      (ADDDSEL - select sum of src & dst)
//	                  W   -------- -------x -------- --------      (PATDSEL - select pattern data)
//	                  W   -------- -------- x------- --------      (TOPNEN - enable carry into top intensity nibble)
//	                  W   -------- -------- -x------ --------      (TOPBEN - enable carry into top intensity byte)
//	                  W   -------- -------- --x----- --------      (ZBUFF - enable Z updates in inner loop)
//	                  W   -------- -------- ---x---- --------      (GOURD - enable gouraud shading in inner loop)
//	                  W   -------- -------- ----x--- --------      (DSTA2 - reverses A2/A1 roles)
//	                  W   -------- -------- -----x-- --------      (UPDA2 - add A2 step to A2 in outer loop)
//	                  W   -------- -------- ------x- --------      (UPDA1 - add A1 step to A1 in outer loop)
//	                  W   -------- -------- -------x --------      (UPDA1F - add A1 fraction step to A1 in outer loop)
//	                  W   -------- -------- -------- x-------      (diagnostic use)
//	                  W   -------- -------- -------- -x------      (CLIP_A1 - clip A1 to window)
//	                  W   -------- -------- -------- --x-----      (DSTWRZ - enable dest Z write in inner loop)
//	                  W   -------- -------- -------- ---x----      (DSTENZ - enable dest Z read in inner loop)
//	                  W   -------- -------- -------- ----x---      (DSTEN - enables dest data read in inner loop)
//	                  W   -------- -------- -------- -----x--      (SRCENX - enable extra src read at start of inner)
//	                  W   -------- -------- -------- ------x-      (SRCENZ - enables source Z read in inner loop)
//	                  W   -------- -------- -------- -------x      (SRCEN - enables source data read in inner loop)
//	F02238          R     xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   B_CMD - status register
//	                R     xxxxxxxx xxxxxxxx -------- --------      (inner count)
//	                R     -------- -------- xxxxxxxx xxxxxx--      (diagnostics)
//	                R     -------- -------- -------- ------x-      (STOPPED - when stopped in collision detect)
//	                R     -------- -------- -------- -------x      (IDLE - when idle)
//	F0223C            W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   B_COUNT - counters register
//	                  W   xxxxxxxx xxxxxxxx -------- --------      (outer loop count)
//	                  W   -------- -------- xxxxxxxx xxxxxxxx      (inner loop count)
//	F02240-F02247     W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   B_SRCD - source data register
//	F02248-F0224F     W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   B_DSTD - destination data register
//	F02250-F02257     W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   B_DSTZ - destination Z register
//	F02258-F0225F     W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   B_SRCZ1 - source Z register 1
//	F02260-F02267     W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   B_SRCZ2 - source Z register 2
//	F02268-F0226F     W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   B_PATD - pattern data register
//	F02270            W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   B_IINC - intensity increment
//	F02274            W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   B_ZINC - Z increment
//	F02278            W   -------- -------- -------- -----xxx   B_STOP - collision control
//	                  W   -------- -------- -------- -----x--      (STOPEN - enable blitter collision stops)
//	                  W   -------- -------- -------- ------x-      (ABORT - abort after stop)
//	                  W   -------- -------- -------- -------x      (RESUME - resume after stop)
//	F0227C            W   -------- xxxxxxxx xxxxxxxx xxxxxxxx   B_I3 - intensity 3
//	F02280            W   -------- xxxxxxxx xxxxxxxx xxxxxxxx   B_I2 - intensity 2
//	F02284            W   -------- xxxxxxxx xxxxxxxx xxxxxxxx   B_I1 - intensity 1
//	F02288            W   -------- xxxxxxxx xxxxxxxx xxxxxxxx   B_I0 - intensity 0
//	F0228C            W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   B_Z3 - Z3
//	F02290            W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   B_Z2 - Z2
//	F02294            W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   B_Z1 - Z1
//	F02298            W   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   B_Z0 - Z0
//	------------------------------------------------------------

#include "tom.h"

#include <string.h>								// For memset()
#include <stdlib.h>								// For rand()
#include "blitter.h"
#include "cry2rgb.h"
#include "event.h"
#include "gpu.h"
#include "jaguar.h"
#include "log.h"
#include "m68000/m68kinterface.h"
//#include "memory.h"
#include "op.h"
#include "settings.h"

#define NEW_TIMER_SYSTEM

// TOM registers (offset from $F00000)

#define MEMCON1		0x00
#define MEMCON2		0x02
#define HC			0x04
#define VC			0x06
#define OLP			0x20		// Object list pointer
#define OBF			0x26		// Object processor flag
#define VMODE		0x28
#define   MODE		0x0006		// Line buffer to video generator mode
#define   BGEN		0x0080		// Background enable (CRY & RGB16 only)
#define   VARMOD	0x0100		// Mixed CRY/RGB16 mode (only works in MODE 0!)
#define   PWIDTH	0x0E00		// Pixel width in video clock cycles (value written + 1)
#define BORD1		0x2A		// Border green/red values (8 BPP)
#define BORD2		0x2C		// Border blue value (8 BPP)
#define HP			0x2E		// Values range from 1 - 1024 (value written + 1)
#define HBB			0x30		// Horizontal blank begin
#define HBE			0x32
#define HS			0x34		// Horizontal sync
#define HVS			0x36		// Horizontal vertical sync
#define HDB1		0x38		// Horizontal display begin 1
#define HDB2		0x3A
#define HDE			0x3C
#define VP			0x3E		// Value ranges from 1 - 2048 (value written + 1)
#define VBB			0x40		// Vertical blank begin
#define VBE			0x42
#define VS			0x44		// Vertical sync
#define VDB			0x46		// Vertical display begin
#define VDE			0x48
#define VEB			0x4A		// Vertical equalization begin
#define VEE			0x4C		// Vertical equalization end
#define VI			0x4E		// Vertical interrupt
#define PIT0		0x50
#define PIT1		0x52
#define HEQ			0x54		// Horizontal equalization end
#define BG			0x58		// Background color
#define INT1		0xE0

//NOTE: These arbitrary cutoffs are NOT taken into account for PAL jaguar screens. !!! FIX !!! [DONE]

// Arbitrary video cutoff values (i.e., first/last visible spots on a TV, in HC ticks)
// Also note that VC is in *half* lines, i.e. divide by 2 to get the scanline
/*#define LEFT_VISIBLE_HC			208
#define RIGHT_VISIBLE_HC		1528//*/
// These were right for Rayman, but that one is offset on a real TV too.
//#define LEFT_VISIBLE_HC			208
//#define RIGHT_VISIBLE_HC		1488
// This is more like a real TV display...
//#define LEFT_VISIBLE_HC			(208 - 32)
//#define RIGHT_VISIBLE_HC		(1488 - 32)
// Split the difference? (Seems to be OK for the most part...)

// (-10 +10)*4 is for opening up the display by 16 pixels (may go to 20). Need to change VIRTUAL_SCREEN_WIDTH to match this as well (went from 320 to 340; this is 4 HCs per one of those pixels).
//NB: Went back to 330. May shrink more. :-)
//#define LEFT_VISIBLE_HC			(208 - 16 - (8 * 4))
//#define LEFT_VISIBLE_HC			(208 - 16 - (3 * 4))
#define LEFT_VISIBLE_HC			(208 - 16 - (1 * 4))
//#define RIGHT_VISIBLE_HC		(1488 - 16 + (10 * 4))
#define RIGHT_VISIBLE_HC		(LEFT_VISIBLE_HC + (VIRTUAL_SCREEN_WIDTH * 4))
//#define TOP_VISIBLE_VC		25
//#define BOTTOM_VISIBLE_VC		503
#define TOP_VISIBLE_VC			31
#define BOTTOM_VISIBLE_VC		511

//Are these PAL horizontals correct?
//They seem to be for the most part, but there are some games that seem to be
//shifted over to the right from this "window".
//#define LEFT_VISIBLE_HC_PAL		(208 - 16 - (4 * 4))
//#define LEFT_VISIBLE_HC_PAL		(208 - 16 - (-1 * 4))
#define LEFT_VISIBLE_HC_PAL		(208 - 16 - (-3 * 4))
//#define RIGHT_VISIBLE_HC_PAL	(1488 - 16 + (10 * 4))
#define RIGHT_VISIBLE_HC_PAL	(LEFT_VISIBLE_HC_PAL + (VIRTUAL_SCREEN_WIDTH * 4))
#define TOP_VISIBLE_VC_PAL		67
#define BOTTOM_VISIBLE_VC_PAL	579

//This can be defined in the makefile as well...
//(It's easier to do it here, though...)
//#define TOM_DEBUG

uint8 tomRam8[0x4000];
uint32 tomWidth, tomHeight;
uint32 tomTimerPrescaler;
uint32 tomTimerDivider;
int32 tomTimerCounter;
uint16 tom_jerry_int_pending, tom_timer_int_pending, tom_object_int_pending,
	tom_gpu_int_pending, tom_video_int_pending;

// These are set by the "user" of the Jaguar core lib, since these are
// OS/system dependent.
uint32 * screenBuffer;
uint32 screenPitch;

static const char * videoMode_to_str[8] =
	{ "16 BPP CRY", "24 BPP RGB", "16 BPP DIRECT", "16 BPP RGB",
	  "Mixed mode", "24 BPP RGB", "16 BPP DIRECT", "16 BPP RGB" };

typedef void (render_xxx_scanline_fn)(uint32 *);

// Private function prototypes

void tom_render_16bpp_cry_scanline(uint32 * backbuffer);
void tom_render_24bpp_scanline(uint32 * backbuffer);
void tom_render_16bpp_direct_scanline(uint32 * backbuffer);
void tom_render_16bpp_rgb_scanline(uint32 * backbuffer);
void tom_render_16bpp_cry_rgb_mix_scanline(uint32 * backbuffer);

//render_xxx_scanline_fn * scanline_render_normal[] =
render_xxx_scanline_fn * scanline_render[] =
{
	tom_render_16bpp_cry_scanline,
	tom_render_24bpp_scanline,
	tom_render_16bpp_direct_scanline,
	tom_render_16bpp_rgb_scanline,
	tom_render_16bpp_cry_rgb_mix_scanline,
	tom_render_24bpp_scanline,
	tom_render_16bpp_direct_scanline,
	tom_render_16bpp_rgb_scanline
};

// Screen info for various games [PAL]...
/*
BIOS
TOM: Horizontal Period written by M68K: 850 (+1*2 = 1702)
TOM: Horizontal Blank Begin written by M68K: 1711
TOM: Horizontal Blank End written by M68K: 158
TOM: Horizontal Display End written by M68K: 1696
TOM: Horizontal Display Begin 1 written by M68K: 166
TOM: Vertical Period written by M68K: 623 (non-interlaced)
TOM: Vertical Blank End written by M68K: 34
TOM: Vertical Display Begin written by M68K: 46
TOM: Vertical Display End written by M68K: 526
TOM: Vertical Blank Begin written by M68K: 600
TOM: Vertical Sync written by M68K: 618
TOM: Horizontal Display End written by M68K: 1665
TOM: Horizontal Display Begin 1 written by M68K: 203
TOM: Vertical Display Begin written by M68K: 38
TOM: Vertical Display End written by M68K: 518
TOM: Video Mode written by M68K: 06C1. PWIDTH = 4, MODE = 16 BPP CRY, flags: BGEN (VC = 151)
TOM: Horizontal Display End written by M68K: 1713
TOM: Horizontal Display Begin 1 written by M68K: 157
TOM: Vertical Display Begin written by M68K: 35
TOM: Vertical Display End written by M68K: 2047
Horizontal range: 157 - 1713 (width: 1557 / 4 = 389.25, / 5 = 315.4)

Asteroid
TOM: Horizontal Period written by M68K: 845 (+1*2 = 1692)
TOM: Horizontal Blank Begin written by M68K: 1700
TOM: Horizontal Blank End written by M68K: 122
TOM: Horizontal Display End written by M68K: 1600
TOM: Horizontal Display Begin 1 written by M68K: 268
TOM: Vertical Period written by M68K: 523 (non-interlaced)
TOM: Vertical Blank End written by M68K: 40
TOM: Vertical Display Begin written by M68K: 44
TOM: Vertical Display End written by M68K: 492
TOM: Vertical Blank Begin written by M68K: 532
TOM: Vertical Sync written by M68K: 513
TOM: Video Mode written by M68K: 04C7. PWIDTH = 3, MODE = 16 BPP RGB, flags: BGEN (VC = 461)

Rayman
TOM: Horizontal Display End written by M68K: 1713
TOM: Horizontal Display Begin 1 written by M68K: 157
TOM: Vertical Display Begin written by M68K: 35
TOM: Vertical Display End written by M68K: 2047
TOM: Video Mode written by M68K: 06C7. PWIDTH = 4, MODE = 16 BPP RGB, flags: BGEN (VC = 89)
TOM: Horizontal Display Begin 1 written by M68K: 208
TOM: Horizontal Display End written by M68K: 1662
TOM: Vertical Display Begin written by M68K: 100
TOM: Vertical Display End written by M68K: 2047
TOM: Video Mode written by M68K: 07C7. PWIDTH = 4, MODE = 16 BPP RGB, flags: BGEN VARMOD (VC = 205)
Horizontal range: 208 - 1662 (width: 1455 / 4 = 363.5)

Alien vs Predator
TOM: Vertical Display Begin written by M68K: 96
TOM: Vertical Display End written by M68K: 2047
TOM: Horizontal Display Begin 1 written by M68K: 239
TOM: Horizontal Display End written by M68K: 1692
TOM: Video Mode written by M68K: 06C1. PWIDTH = 4, MODE = 16 BPP CRY, flags: BGEN (VC = 378)
TOM: Vertical Display Begin written by M68K: 44
TOM: Vertical Display End written by M68K: 2047
TOM: Horizontal Display Begin 1 written by M68K: 239
TOM: Horizontal Display End written by M68K: 1692
TOM: Video Mode written by M68K: 06C7. PWIDTH = 4, MODE = 16 BPP RGB, flags: BGEN (VC = 559)
TOM: Vertical Display Begin written by M68K: 84
TOM: Vertical Display End written by M68K: 2047
TOM: Horizontal Display Begin 1 written by M68K: 239
TOM: Horizontal Display End written by M68K: 1692
TOM: Vertical Display Begin written by M68K: 44
TOM: Vertical Display End written by M68K: 2047
TOM: Horizontal Display Begin 1 written by M68K: 239
TOM: Horizontal Display End written by M68K: 1692
Horizontal range: 239 - 1692 (width: 1454 / 4 = 363.5)

*/

// Screen info for various games [NTSC]...
/*
Doom
TOM: Horizontal Display End written by M68K: 1727
TOM: Horizontal Display Begin 1 written by M68K: 123
TOM: Vertical Display Begin written by M68K: 25
TOM: Vertical Display End written by M68K: 2047
TOM: Video Mode written by M68K: 0EC1. PWIDTH = 8, MODE = 16 BPP CRY, flags: BGEN (VC = 5)
Also does PWIDTH = 4...
Vertical resolution: 238 lines

Rayman
TOM: Horizontal Display End written by M68K: 1727
TOM: Horizontal Display Begin 1 written by M68K: 123
TOM: Vertical Display Begin written by M68K: 25
TOM: Vertical Display End written by M68K: 2047
TOM: Vertical Interrupt written by M68K: 507
TOM: Video Mode written by M68K: 06C7. PWIDTH = 4, MODE = 16 BPP RGB, flags: BGEN (VC = 92)
TOM: Horizontal Display Begin 1 written by M68K: 208
TOM: Horizontal Display End written by M68K: 1670
Display starts at 31, then 52!
Vertical resolution: 238 lines

Atari Karts
TOM: Horizontal Display End written by M68K: 1727
TOM: Horizontal Display Begin 1 written by M68K: 123
TOM: Vertical Display Begin written by M68K: 25
TOM: Vertical Display End written by M68K: 2047
TOM: Video Mode written by GPU: 08C7. PWIDTH = 5, MODE = 16 BPP RGB, flags: BGEN (VC = 4)
TOM: Video Mode written by GPU: 06C7. PWIDTH = 4, MODE = 16 BPP RGB, flags: BGEN (VC = 508)
Display starts at 31 (PWIDTH = 4), 24 (PWIDTH = 5)

Iron Soldier
TOM: Vertical Interrupt written by M68K: 2047
TOM: Video Mode written by M68K: 06C1. PWIDTH = 4, MODE = 16 BPP CRY, flags: BGEN (VC = 0)
TOM: Horizontal Display End written by M68K: 1727
TOM: Horizontal Display Begin 1 written by M68K: 123
TOM: Vertical Display Begin written by M68K: 25
TOM: Vertical Display End written by M68K: 2047
TOM: Vertical Interrupt written by M68K: 507
TOM: Video Mode written by M68K: 06C1. PWIDTH = 4, MODE = 16 BPP CRY, flags: BGEN (VC = 369)
TOM: Video Mode written by M68K: 06C1. PWIDTH = 4, MODE = 16 BPP CRY, flags: BGEN (VC = 510)
TOM: Video Mode written by M68K: 06C3. PWIDTH = 4, MODE = 24 BPP RGB, flags: BGEN (VC = 510)
Display starts at 31
Vertical resolution: 238 lines
[Seems to be a problem between the horizontal positioning of the 16-bit CRY & 24-bit RGB]

JagMania
TOM: Horizontal Period written by M68K: 844 (+1*2 = 1690)
TOM: Horizontal Blank Begin written by M68K: 1713
TOM: Horizontal Blank End written by M68K: 125
TOM: Horizontal Display End written by M68K: 1696
TOM: Horizontal Display Begin 1 written by M68K: 166
TOM: Vertical Period written by M68K: 523 (non-interlaced)
TOM: Vertical Blank End written by M68K: 24
TOM: Vertical Display Begin written by M68K: 46
TOM: Vertical Display End written by M68K: 496
TOM: Vertical Blank Begin written by M68K: 500
TOM: Vertical Sync written by M68K: 517
TOM: Vertical Interrupt written by M68K: 497
TOM: Video Mode written by M68K: 04C1. PWIDTH = 3, MODE = 16 BPP CRY, flags: BGEN (VC = 270)
Display starts at 55

Double Dragon V
TOM: Horizontal Display End written by M68K: 1727
TOM: Horizontal Display Begin 1 written by M68K: 123
TOM: Vertical Display Begin written by M68K: 25
TOM: Vertical Display End written by M68K: 2047
TOM: Vertical Interrupt written by M68K: 507
TOM: Video Mode written by M68K: 06C7. PWIDTH = 4, MODE = 16 BPP RGB, flags: BGEN (VC = 9)

Dino Dudes
TOM: Horizontal Display End written by M68K: 1823
TOM: Horizontal Display Begin 1 written by M68K: 45
TOM: Vertical Display Begin written by M68K: 40
TOM: Vertical Display End written by M68K: 2047
TOM: Vertical Interrupt written by M68K: 491
TOM: Video Mode written by M68K: 06C1. PWIDTH = 4, MODE = 16 BPP CRY, flags: BGEN (VC = 398)
Display starts at 11 (123 - 45 = 78, 78 / 4 = 19 pixels to skip)
Width is 417, so maybe width of 379 would be good (starting at 123, ending at 1639)
Vertical resolution: 238 lines

Flashback
TOM: Horizontal Display End written by M68K: 1727
TOM: Horizontal Display Begin 1 written by M68K: 188
TOM: Vertical Display Begin written by M68K: 1
TOM: Vertical Display End written by M68K: 2047
TOM: Vertical Interrupt written by M68K: 483
TOM: Video Mode written by M68K: 08C7. PWIDTH = 5, MODE = 16 BPP RGB, flags: BGEN (VC = 99)
Width would be 303 with above scheme, but border width would be 13 pixels

Trevor McFur
Vertical resolution: 238 lines
*/

uint32 RGB16ToRGB32[0x10000];
uint32 CRY16ToRGB32[0x10000];
uint32 MIX16ToRGB32[0x10000];


#warning "This is not endian-safe. !!! FIX !!!"
void TOMFillLookupTables(void)
{
	// NOTE: Jaguar 16-bit (non-CRY) color is RBG 556 like so:
	//       RRRR RBBB BBGG GGGG
	for(uint32 i=0; i<0x10000; i++)
//hm.		RGB16ToRGB32[i] = 0xFF000000
//			| ((i & 0xF100) >> 8)  | ((i & 0xE000) >> 13)
//			| ((i & 0x07C0) << 13) | ((i & 0x0700) << 8)
//			| ((i & 0x003F) << 10) | ((i & 0x0030) << 4);
		RGB16ToRGB32[i] = 0x000000FF
			| ((i & 0xF100) << 16)					// Red
			| ((i & 0x003F) << 18)					// Green
			| ((i & 0x07C0) << 5);					// Blue

	for(uint32 i=0; i<0x10000; i++)
	{
		uint32 cyan = (i & 0xF000) >> 12,
			red = (i & 0x0F00) >> 8,
			intensity = (i & 0x00FF);

		uint32 r = (((uint32)redcv[cyan][red]) * intensity) >> 8,
			g = (((uint32)greencv[cyan][red]) * intensity) >> 8,
			b = (((uint32)bluecv[cyan][red]) * intensity) >> 8;

//hm.		CRY16ToRGB32[i] = 0xFF000000 | (b << 16) | (g << 8) | r;
		CRY16ToRGB32[i] = 0x000000FF | (r << 24) | (g << 16) | (b << 8);
		MIX16ToRGB32[i] = (i & 0x01 ? RGB16ToRGB32[i] : CRY16ToRGB32[i]);
	}
}


void TOMSetPendingJERRYInt(void)
{
	tom_jerry_int_pending = 1;
}


void TOMSetPendingTimerInt(void)
{
	tom_timer_int_pending = 1;
}


void TOMSetPendingObjectInt(void)
{
	tom_object_int_pending = 1;
}


void TOMSetPendingGPUInt(void)
{
	tom_gpu_int_pending = 1;
}


void TOMSetPendingVideoInt(void)
{
	tom_video_int_pending = 1;
}


uint8 * TOMGetRamPointer(void)
{
	return tomRam8;
}


uint8 TOMGetVideoMode(void)
{
	uint16 vmode = GET16(tomRam8, VMODE);
	return ((vmode & VARMOD) >> 6) | ((vmode & MODE) >> 1);
}


//Used in only one place (and for debug purposes): OBJECTP.CPP
#warning "Used in only one place (and for debug purposes): OBJECTP.CPP !!! FIX !!!"
uint16 TOMGetVDB(void)
{
	return GET16(tomRam8, VDB);
}


#define LEFT_BG_FIX
//
// 16 BPP CRY/RGB mixed mode rendering
//
void tom_render_16bpp_cry_rgb_mix_scanline(uint32 * backbuffer)
{
//CHANGED TO 32BPP RENDERING
	uint16 width = tomWidth;
	uint8 * current_line_buffer = (uint8 *)&tomRam8[0x1800];

	//New stuff--restrict our drawing...
	uint8 pwidth = ((GET16(tomRam8, VMODE) & PWIDTH) >> 9) + 1;
	//NOTE: May have to check HDB2 as well!
	// Get start position in HC ticks
	int16 startPos = GET16(tomRam8, HDB1) - (vjs.hardwareTypeNTSC ? LEFT_VISIBLE_HC : LEFT_VISIBLE_HC_PAL);
	// Convert to pixels
	startPos /= pwidth;

	if (startPos < 0)
		// This is x2 because current_line_buffer is uint8 & we're in a 16bpp mode
		current_line_buffer += 2 * -startPos;
	else
//This case doesn't properly handle the "start on the right side of virtual screen" case
//Dunno why--looks Ok...
//What *is* for sure wrong is that it doesn't copy the linebuffer's BG pixels... [FIXED NOW]
//This should likely be 4 instead of 2 (?--not sure)
// Actually, there should be NO multiplier, as startPos is expressed in PIXELS
// and so is the backbuffer.
#ifdef LEFT_BG_FIX
	{
		uint8 g = tomRam8[BORD1], r = tomRam8[BORD1 + 1], b = tomRam8[BORD2 + 1];
		uint32 pixel = 0x000000FF | (r << 24) | (g << 16) | (b << 8);

		for(int16 i=0; i<startPos; i++)
			*backbuffer++ = pixel;

		width -= startPos;
	}
#else
		backbuffer += 2 * startPos, width -= startPos;
#endif

	while (width)
	{
		uint16 color = (*current_line_buffer++) << 8;
		color |= *current_line_buffer++;
		*backbuffer++ = MIX16ToRGB32[color];
		width--;
	}
}


//
// 16 BPP CRY mode rendering
//
void tom_render_16bpp_cry_scanline(uint32 * backbuffer)
{
//CHANGED TO 32BPP RENDERING
	uint16 width = tomWidth;
	uint8 * current_line_buffer = (uint8 *)&tomRam8[0x1800];

	//New stuff--restrict our drawing...
	uint8 pwidth = ((GET16(tomRam8, VMODE) & PWIDTH) >> 9) + 1;
	//NOTE: May have to check HDB2 as well!
	int16 startPos = GET16(tomRam8, HDB1) - (vjs.hardwareTypeNTSC ? LEFT_VISIBLE_HC : LEFT_VISIBLE_HC_PAL);// Get start position in HC ticks
	startPos /= pwidth;
	if (startPos < 0)
		current_line_buffer += 2 * -startPos;
	else
#ifdef LEFT_BG_FIX
	{
		uint8 g = tomRam8[BORD1], r = tomRam8[BORD1 + 1], b = tomRam8[BORD2 + 1];
		uint32 pixel = 0x000000FF | (r << 24) | (g << 16) | (b << 8);

		for(int16 i=0; i<startPos; i++)
			*backbuffer++ = pixel;

		width -= startPos;
	}
#else
//This should likely be 4 instead of 2 (?--not sure)
		backbuffer += 2 * startPos, width -= startPos;
#endif

	while (width)
	{
		uint16 color = (*current_line_buffer++) << 8;
		color |= *current_line_buffer++;
		*backbuffer++ = CRY16ToRGB32[color];
		width--;
	}
}


//
// 24 BPP mode rendering
//
void tom_render_24bpp_scanline(uint32 * backbuffer)
{
//CHANGED TO 32BPP RENDERING
	uint16 width = tomWidth;
	uint8 * current_line_buffer = (uint8 *)&tomRam8[0x1800];

	//New stuff--restrict our drawing...
	uint8 pwidth = ((GET16(tomRam8, VMODE) & PWIDTH) >> 9) + 1;
	//NOTE: May have to check HDB2 as well!
	int16 startPos = GET16(tomRam8, HDB1) - (vjs.hardwareTypeNTSC ? LEFT_VISIBLE_HC : LEFT_VISIBLE_HC_PAL);	// Get start position in HC ticks
	startPos /= pwidth;
	if (startPos < 0)
		current_line_buffer += 4 * -startPos;
	else
#ifdef LEFT_BG_FIX
	{
		uint8 g = tomRam8[BORD1], r = tomRam8[BORD1 + 1], b = tomRam8[BORD2 + 1];
		uint32 pixel = 0x000000FF | (r << 24) | (g << 16) | (b << 8);

		for(int16 i=0; i<startPos; i++)
			*backbuffer++ = pixel;

		width -= startPos;
	}
#else
//This should likely be 4 instead of 2 (?--not sure)
		backbuffer += 2 * startPos, width -= startPos;
#endif

	while (width)
	{
		uint32 g = *current_line_buffer++;
		uint32 r = *current_line_buffer++;
		current_line_buffer++;
		uint32 b = *current_line_buffer++;
//hm.		*backbuffer++ = 0xFF000000 | (b << 16) | (g << 8) | r;
		*backbuffer++ = 0x000000FF | (r << 24) | (g << 16) | (b << 8);
		width--;
	}
}


//Seems to me that this is NOT a valid mode--the JTRM seems to imply that you would need
//extra hardware outside of the Jaguar console to support this!
//
// 16 BPP direct mode rendering
//
void tom_render_16bpp_direct_scanline(uint32 * backbuffer)
{
	uint16 width = tomWidth;
	uint8 * current_line_buffer = (uint8 *)&tomRam8[0x1800];

	while (width)
	{
		uint16 color = (*current_line_buffer++) << 8;
		color |= *current_line_buffer++;
		*backbuffer++ = color >> 1;
		width--;
	}
}


//
// 16 BPP RGB mode rendering
//
void tom_render_16bpp_rgb_scanline(uint32 * backbuffer)
{
//CHANGED TO 32BPP RENDERING
	// 16 BPP RGB: 0-5 green, 6-10 blue, 11-15 red

	uint16 width = tomWidth;
	uint8 * current_line_buffer = (uint8 *)&tomRam8[0x1800];

	//New stuff--restrict our drawing...
	uint8 pwidth = ((GET16(tomRam8, VMODE) & PWIDTH) >> 9) + 1;
	//NOTE: May have to check HDB2 as well!
	int16 startPos = GET16(tomRam8, HDB1) - (vjs.hardwareTypeNTSC ? LEFT_VISIBLE_HC : LEFT_VISIBLE_HC_PAL);	// Get start position in HC ticks
	startPos /= pwidth;

	if (startPos < 0)
		current_line_buffer += 2 * -startPos;
	else
#ifdef LEFT_BG_FIX
	{
		uint8 g = tomRam8[BORD1], r = tomRam8[BORD1 + 1], b = tomRam8[BORD2 + 1];
		uint32 pixel = 0x000000FF | (r << 24) | (g << 16) | (b << 8);

		for(int16 i=0; i<startPos; i++)
			*backbuffer++ = pixel;

		width -= startPos;
	}
#else
//This should likely be 4 instead of 2 (?--not sure)
		backbuffer += 2 * startPos, width -= startPos;
#endif

	while (width)
	{
		uint32 color = (*current_line_buffer++) << 8;
		color |= *current_line_buffer++;
		*backbuffer++ = RGB16ToRGB32[color];
		width--;
	}
}


//
// Process a single scanline
// (this is bad terminology; each tick of the VC is actually a half-line)
//
void TOMExecHalfline(uint16 halfline, bool render)
{
#warning "!!! Need to handle multiple fields properly !!!"
	// We ignore the problem for now
	halfline &= 0x7FF;

	bool inActiveDisplayArea = true;

//Interlacing is still not handled correctly here... !!! FIX !!!
	if (halfline & 0x01)							// Execute OP only on even halflines (non-interlaced only!)
		return;

//Hm, it seems that the OP needs to execute from zero, so let's try it:
// And it works! But need to do some optimizations in the OP to keep it from attempting
// to do a scanline render in the non-display area... [DONE]
//this seems to cause a regression in certain games, like rayman
//which means I have to dig thru the asic nets to see what's wrong...
/*
No, the OP doesn't start until VDB, that much is certain. The thing is, VDB is the
HALF line that the OP starts on--which means that it needs to start at VDB / 2!!!

Hrm, doesn't seem to be enough, though it should be... still sticks for 20 frames.


What triggers this is writing $FFFF to VDE. This causes the OP start signal in VID to 
latch on, which in effect sets VDB to zero. So that much is correct. But the thing with
Rayman is that it shouldn't cause the graphical glitches seen there, so still have to
investigate what's going on there. By all rights, it shouldn't glitch because:

00006C00: 0000000D 82008F73 (BRANCH) YPOS=494, CC=">", link=$00006C10
00006C08: 000003FF 00008173 (BRANCH) YPOS=46, CC=">", link=$001FF800
00006C10: 00000000 0000000C (STOP)
001FF800: 12FC2BFF 02380000 (BITMAP)
          00008004 8180CFF1

Even if the OP is running all the time, the link should tell it to stop at the right
place (which it seems to do). But we still get glitchy screen.

Seems the glitchy screen went away... Maybe the GPU alignment fixes fixed it???
Just need to add the proper checking here then.

Some numbers, courtesy of the Jaguar BIOS:
// NTSC:
VP, 523			// Vertical Period (1-based; in this case VP = 524)
VBE, 24			// Vertical Blank End
VDB, 38			// Vertical Display Begin
VDE, 518		// Vertical Display End
VBB, 500		// Vertical Blank Begin
VS, 517			// Vertical Sync

// PAL Jaguar
VP, 623			// Vertical Period (1-based; in this case VP = 624)
VBE, 34			// Vertical Blank End
VDB, 38			// Vertical Display Begin
VDE, 518		// Vertical Display End
VBB, 600		// Vertical Blank Begin
VS, 618			// Vertical Sync

Numbers for KM, NTSC:
KM: (Note that with VDE <= 507, the OP starts at VDB as expected)
TOM: Vertical Display Begin written by M68K: 41
TOM: Vertical Display End written by M68K: 2047
TOM: Vertical Interrupt written by M68K: 491
*/
#if 1
	// Initial values that "well behaved" programs use
	uint16 startingHalfline = GET16(tomRam8, VDB);
	uint16 endingHalfline = GET16(tomRam8, VDE);

	// Simulate the OP start bug here!
	// Really, this value is somewhere around 507 for an NTSC Jaguar. But this
	// should work in a majority of cases, at least until we can figure it out properly.
	if (endingHalfline > GET16(tomRam8, VP))
		startingHalfline = 0;

	if (halfline >= startingHalfline && halfline < endingHalfline)
//	if (halfline >= 0 && halfline < (uint16)GET16(tomRam8, VDE))
// 16 isn't enough, and neither is 32 for raptgun. 32 fucks up Rayman
//	if (halfline >= ((uint16)GET16(tomRam8, VDB) / 2) && halfline < ((uint16)GET16(tomRam8, VDE) / 2))
//	if (halfline >= ((uint16)GET16(tomRam8, VDB) - 16) && halfline < (uint16)GET16(tomRam8, VDE))
//	if (halfline >= 20 && halfline < (uint16)GET16(tomRam8, VDE))
//	if (halfline >= (uint16)GET16(tomRam8, VDB) && halfline < (uint16)GET16(tomRam8, VDE))
	{
		if (render)
		{
			uint8 * current_line_buffer = (uint8 *)&tomRam8[0x1800];
			uint8 bgHI = tomRam8[BG], bgLO = tomRam8[BG + 1];

			// Clear line buffer with BG
			if (GET16(tomRam8, VMODE) & BGEN) // && (CRY or RGB16)...
				for(uint32 i=0; i<720; i++)
					*current_line_buffer++ = bgHI, *current_line_buffer++ = bgLO;

			OPProcessList(halfline, render);
		}
	}
	else
		inActiveDisplayArea = false;
#else
	inActiveDisplayArea =
		(halfline >= (uint16)GET16(tomRam8, VDB) && halfline < (uint16)GET16(tomRam8, VDE)
			? true : false);

	if (halfline < (uint16)GET16(tomRam8, VDE))
	{
		if (render)//With JaguarExecuteNew() this is always true...
		{
			uint8 * current_line_buffer = (uint8 *)&tomRam8[0x1800];
			uint8 bgHI = tomRam8[BG], bgLO = tomRam8[BG + 1];

			// Clear line buffer with BG
			if (GET16(tomRam8, VMODE) & BGEN) // && (CRY or RGB16)...
				for(uint32 i=0; i<720; i++)
					*current_line_buffer++ = bgHI, *current_line_buffer++ = bgLO;

//			OPProcessList(halfline, render);
//This seems to take care of it...
			OPProcessList(halfline, inActiveDisplayArea);
		}
	}
#endif

	// Try to take PAL into account... [We do now!]

	uint16 topVisible = (vjs.hardwareTypeNTSC ? TOP_VISIBLE_VC : TOP_VISIBLE_VC_PAL),
		bottomVisible = (vjs.hardwareTypeNTSC ? BOTTOM_VISIBLE_VC : BOTTOM_VISIBLE_VC_PAL);
	uint32 * TOMCurrentLine = &(screenBuffer[((halfline - topVisible) / 2) * screenPitch]);

	// Here's our virtualized scanline code...

	if (halfline >= topVisible && halfline < bottomVisible)
	{
		if (inActiveDisplayArea)
		{
//NOTE: The following doesn't put BORDER color on the sides... !!! FIX !!!
#warning "The following doesn't put BORDER color on the sides... !!! FIX !!!"
			if (vjs.renderType == RT_NORMAL)
//				scanline_render[TOMGetVideoMode()](TOMBackbuffer);
				scanline_render[TOMGetVideoMode()](TOMCurrentLine);
			else//TV type render
			{
/*
	tom_render_16bpp_cry_scanline,
	tom_render_24bpp_scanline,
	tom_render_16bpp_direct_scanline,
	tom_render_16bpp_rgb_scanline,
	tom_render_16bpp_cry_rgb_mix_scanline,
	tom_render_24bpp_scanline,
	tom_render_16bpp_direct_scanline,
	tom_render_16bpp_rgb_scanline
#define VMODE		0x28
#define   MODE		0x0006		// Line buffer to video generator mode
#define   VARMOD	0x0100		// Mixed CRY/RGB16 mode (only works in MODE 0!)
*/
				uint8 pwidth = ((GET16(tomRam8, VMODE) & PWIDTH) >> 9) + 1;
				uint8 mode = ((GET16(tomRam8, VMODE) & MODE) >> 1);
				bool varmod = GET16(tomRam8, VMODE) & VARMOD;
//The video texture line buffer ranges from 0 to 1279, with its left edge starting at
//LEFT_VISIBLE_HC. So, we need to start writing into the backbuffer at HDB1, using pwidth
//as our scaling factor. The way it generates its image on a real TV!

//So, for example, if HDB1 is less than LEFT_VISIBLE_HC, then we have to figure out where
//in the VTLB that we start writing pixels from the Jaguar line buffer (VTLB start=0,
//JLB=something).
#if 0
//
// 24 BPP mode rendering
//
void tom_render_24bpp_scanline(uint32 * backbuffer)
{
//CHANGED TO 32BPP RENDERING
	uint16 width = tomWidth;
	uint8 * current_line_buffer = (uint8 *)&tomRam8[0x1800];

	//New stuff--restrict our drawing...
	uint8 pwidth = ((GET16(tomRam8, VMODE) & PWIDTH) >> 9) + 1;
	//NOTE: May have to check HDB2 as well!
	int16 startPos = GET16(tomRam8, HDB1) - (vjs.hardwareTypeNTSC ? LEFT_VISIBLE_HC : LEFT_VISIBLE_HC_PAL);	// Get start position in HC ticks
	startPos /= pwidth;
	if (startPos < 0)
		current_line_buffer += 4 * -startPos;
	else
//This should likely be 4 instead of 2 (?--not sure)
		backbuffer += 2 * startPos, width -= startPos;

	while (width)
	{
		uint32 g = *current_line_buffer++;
		uint32 r = *current_line_buffer++;
		current_line_buffer++;
		uint32 b = *current_line_buffer++;
		*backbuffer++ = 0xFF000000 | (b << 16) | (g << 8) | r;
		width--;
	}
}
#endif

			}
		}
		else
		{
			// If outside of VDB & VDE, then display the border color
			uint32 * currentLineBuffer = TOMCurrentLine;
			uint8 g = tomRam8[BORD1], r = tomRam8[BORD1 + 1], b = tomRam8[BORD2 + 1];
//Hm.			uint32 pixel = 0xFF000000 | (b << 16) | (g << 8) | r;
			uint32 pixel = 0x000000FF | (r << 24) | (g << 16) | (b << 8);

			for(uint32 i=0; i<tomWidth; i++)
				*currentLineBuffer++ = pixel;
		}
	}
}


//
// TOM initialization
//
void TOMInit(void)
{
	TOMFillLookupTables();
	OPInit();
	BlitterInit();
	TOMReset();
}


void TOMDone(void)
{
	OPDone();
	BlitterDone();
	WriteLog("TOM: Resolution %i x %i %s\n", TOMGetVideoModeWidth(), TOMGetVideoModeHeight(),
		videoMode_to_str[TOMGetVideoMode()]);
//	WriteLog("\ntom: object processor:\n");
//	WriteLog("tom: pointer to object list: 0x%.8x\n",op_get_list_pointer());
//	WriteLog("tom: INT1=0x%.2x%.2x\n",TOMReadByte(0xf000e0),TOMReadByte(0xf000e1));
//	gpu_done();
//	dsp_done();
//	memory_free(tomRam8);
//	memory_free(tom_cry_rgb_mix_lut);
}


uint32 TOMGetVideoModeWidth(void)
{
	//These widths are pretty bogus. Should use HDB1/2 & HDE/HBB & PWIDTH to calc the width...
//	uint32 width[8] = { 1330, 665, 443, 332, 266, 222, 190, 166 };
//Temporary, for testing Doom...
//	uint32 width[8] = { 1330, 665, 443, 332, 266, 222, 190, 332 };

	// Note that the following PWIDTH values have the following pixel aspect ratios:
	// PWIDTH = 1 -> 0.25:1 (1:4) pixels (X:Y ratio)
	// PWIDTH = 2 -> 0.50:1 (1:2) pixels
	// PWIDTH = 3 -> 0.75:1 (3:4) pixels
	// PWIDTH = 4 -> 1.00:1 (1:1) pixels
	// PWIDTH = 5 -> 1.25:1 (5:4) pixels
	// PWIDTH = 6 -> 1.50:1 (3:2) pixels
	// PWIDTH = 7 -> 1.75:1 (7:4) pixels
	// PWIDTH = 8 -> 2.00:1 (2:1) pixels

	// Also note that the JTRM says that PWIDTH of 4 gives pixels that are "about" square--
	// this implies that the other modes have pixels that are *not* square!
	// Also, I seriously doubt that you will see any games that use PWIDTH = 1!

	// NOTE: Even though the PWIDTH value is + 1, here we're using a zero-based index and
	//       so we don't bother to add one...
//	return width[(GET16(tomRam8, VMODE) & PWIDTH) >> 9];

	// Now, we just calculate it...
/*	uint16 hdb1 = GET16(tomRam8, HDB1), hde = GET16(tomRam8, HDE),
		hbb = GET16(tomRam8, HBB), pwidth = ((GET16(tomRam8, VMODE) & PWIDTH) >> 9) + 1;
//	return ((hbb < hde ? hbb : hde) - hdb1) / pwidth;
//Temporary, for testing Doom...
	return ((hbb < hde ? hbb : hde) - hdb1) / (pwidth == 8 ? 4 : pwidth);*/

	// To make it easier to make a quasi-fixed display size, we restrict the viewing
	// area to an arbitrary range of the Horizontal Count.
	uint16 pwidth = ((GET16(tomRam8, VMODE) & PWIDTH) >> 9) + 1;
	return (vjs.hardwareTypeNTSC ? RIGHT_VISIBLE_HC - LEFT_VISIBLE_HC : RIGHT_VISIBLE_HC_PAL - LEFT_VISIBLE_HC_PAL) / pwidth;
//Temporary, for testing Doom...
//	return (RIGHT_VISIBLE_HC - LEFT_VISIBLE_HC) / (pwidth == 8 ? 4 : pwidth);
////	return (RIGHT_VISIBLE_HC - LEFT_VISIBLE_HC) / (pwidth == 4 ? 8 : pwidth);

// More speculating...
// According to the JTRM, the number of potential pixels across is given by the
// Horizontal Period (HP - in NTSC this is 845). The Horizontal Count counts from
// zero to this value twice per scanline (the high bit is set on the second count).
// HBE and HBB define the absolute "black" limits of the screen, while HDB1/2 and
// HDE determine the extent of the OP "on" time. I.e., when the OP is turned on by
// HDB1, it starts fetching the line from position 0 in LBUF.

// The trick, it would seem, is to figure out how long the typical visible scanline
// of a TV is in HP ticks and limit the visible area to that (divided by PWIDTH, of
// course). Using that length, we can establish an "absolute left display limit" with
// which to measure HBB & HDB1/2 against when rendering LBUF (i.e., if HDB1 is 20 ticks
// to the right of the ALDL and PWIDTH is 4, then start writing the LBUF starting at
// backbuffer + 5 pixels).

// That's basically what we're doing now...!
}


// *** SPECULATION ***
// It might work better to virtualize the height settings, i.e., set the vertical
// height at 240 lines and clip using the VDB and VDE/VP registers...
// Same with the width... [Width is pretty much virtualized now.]

// Now that that the width is virtualized, let's virtualize the height. :-)
uint32 TOMGetVideoModeHeight(void)
{
//	uint16 vmode = GET16(tomRam8, VMODE);
//	uint16 vbe = GET16(tomRam8, VBE);
//	uint16 vbb = GET16(tomRam8, VBB);
//	uint16 vdb = GET16(tomRam8, VDB);
//	uint16 vde = GET16(tomRam8, VDE);
//	uint16 vp = GET16(tomRam8, VP);

/*	if (vde == 0xFFFF)
		vde = vbb;//*/

//	return 227;//WAS:(vde/*-vdb*/) >> 1;
	// The video mode height probably works this way:
	// VC counts from 0 to VP. VDB starts the OP. Either when
	// VDE is reached or VP, the OP is stopped. Let's try it...
	// Also note that we're conveniently ignoring interlaced display modes...!
//	return ((vde > vp ? vp : vde) - vdb) >> 1;
//	return ((vde > vbb ? vbb : vde) - vdb) >> 1;
//Let's try from the Vertical Blank interval...
//Seems to work OK!
//	return (vbb - vbe) >> 1;	// Again, doesn't take interlacing into account...
// This of course doesn't take interlacing into account. But I haven't seen any
// Jaguar software that takes advantage of it either...
//Also, doesn't reflect PAL Jaguar either... !!! FIX !!! [DONE]
//	return 240;										// Set virtual screen height to 240 lines...
	return (vjs.hardwareTypeNTSC ? 240 : 256);
}


//
// TOM reset code
// Now PAL friendly!
//
/*
The values in TOMReset come from the Jaguar BIOS.
These values are from BJL:

NSTC:
CLK2	 181
HP		 844
HBB		 1713
HBE		 125
HS		 1741
HVS		 651
HEQ		 784
HDE		 1696
HDB1	 166
HDB2	 166
VP		 523
VEE		 6
VBE		 24
VDB		 46
VDE		 496
VBB		 500
VEB		 511
VS		 517

PAL:
CLK2	 226
HP		 850
HBB		 1711
HBE		 158
HS		 1749
HVS		 601
HEQ		 787
HDE		 1696
HDB1	 166
HDB2	 166
VP		 625
VEE		 6
VBE		 34
VDB		 46
VDE		 429
VBB		 600
VEB		 613
VS		 618
*/
void TOMReset(void)
{
	OPReset();
	BlitterReset();
	memset(tomRam8, 0x00, 0x4000);

	if (vjs.hardwareTypeNTSC)
	{
		SET16(tomRam8, MEMCON1, 0x1861);
		SET16(tomRam8, MEMCON2, 0x35CC);
		SET16(tomRam8, HP, 844);			// Horizontal Period (1-based; HP=845)
		SET16(tomRam8, HBB, 1713);			// Horizontal Blank Begin
		SET16(tomRam8, HBE, 125);			// Horizontal Blank End
		SET16(tomRam8, HDE, 1665);			// Horizontal Display End
		SET16(tomRam8, HDB1, 203);			// Horizontal Display Begin 1
		SET16(tomRam8, VP, 523);			// Vertical Period (1-based; in this case VP = 524)
		SET16(tomRam8, VBE, 24);			// Vertical Blank End
		SET16(tomRam8, VDB, 38);			// Vertical Display Begin
		SET16(tomRam8, VDE, 518);			// Vertical Display End
		SET16(tomRam8, VBB, 500);			// Vertical Blank Begin
		SET16(tomRam8, VS, 517);			// Vertical Sync
		SET16(tomRam8, VMODE, 0x06C1);
	}
	else	// PAL Jaguar
	{
		SET16(tomRam8, MEMCON1, 0x1861);
		SET16(tomRam8, MEMCON2, 0x35CC);
		SET16(tomRam8, HP, 850);			// Horizontal Period
		SET16(tomRam8, HBB, 1711);			// Horizontal Blank Begin
		SET16(tomRam8, HBE, 158);			// Horizontal Blank End
		SET16(tomRam8, HDE, 1665);			// Horizontal Display End
		SET16(tomRam8, HDB1, 203);			// Horizontal Display Begin 1
		SET16(tomRam8, VP, 623);			// Vertical Period (1-based; in this case VP = 624)
		SET16(tomRam8, VBE, 34);			// Vertical Blank End
		SET16(tomRam8, VDB, 38);			// Vertical Display Begin
		SET16(tomRam8, VDE, 518);			// Vertical Display End
		SET16(tomRam8, VBB, 600);			// Vertical Blank Begin
		SET16(tomRam8, VS, 618);			// Vertical Sync
		SET16(tomRam8, VMODE, 0x06C1);
	}

	tomWidth = 0;
	tomHeight = 0;

	tom_jerry_int_pending = 0;
	tom_timer_int_pending = 0;
	tom_object_int_pending = 0;
	tom_gpu_int_pending = 0;
	tom_video_int_pending = 0;

	tomTimerPrescaler = 0;					// TOM PIT is disabled
	tomTimerDivider = 0;
	tomTimerCounter = 0;
}


//
// TOM byte access (read)
//
uint8 TOMReadByte(uint32 offset, uint32 who/*=UNKNOWN*/)
{
//???Is this needed???
// It seems so. Perhaps it's the +$8000 offset being written to (32-bit interface)?
// However, the 32-bit interface is WRITE ONLY, so that can't be it...
// Also, the 68K CANNOT make use of the 32-bit interface, since its bus width is only 16-bits...
//	offset &= 0xFF3FFF;

#ifdef TOM_DEBUG
	WriteLog("TOM: Reading byte at %06X for %s\n", offset, whoName[who]);
#endif

	if ((offset >= GPU_CONTROL_RAM_BASE) && (offset < GPU_CONTROL_RAM_BASE+0x20))
		return GPUReadByte(offset, who);
	else if ((offset >= GPU_WORK_RAM_BASE) && (offset < GPU_WORK_RAM_BASE+0x1000))
		return GPUReadByte(offset, who);
/*	else if ((offset >= 0xF00010) && (offset < 0xF00028))
		return OPReadByte(offset, who);*/
	else if ((offset >= 0xF02200) && (offset < 0xF022A0))
		return BlitterReadByte(offset, who);
	else if (offset == 0xF00050)
		return tomTimerPrescaler >> 8;
	else if (offset == 0xF00051)
		return tomTimerPrescaler & 0xFF;
	else if (offset == 0xF00052)
		return tomTimerDivider >> 8;
	else if (offset == 0xF00053)
		return tomTimerDivider & 0xFF;

	return tomRam8[offset & 0x3FFF];
}


//
// TOM word access (read)
//
uint16 TOMReadWord(uint32 offset, uint32 who/*=UNKNOWN*/)
{
//???Is this needed???
//	offset &= 0xFF3FFF;
#ifdef TOM_DEBUG
	WriteLog("TOM: Reading word at %06X for %s\n", offset, whoName[who]);
#endif
if (offset >= 0xF02000 && offset <= 0xF020FF)
	WriteLog("TOM: ReadWord attempted from GPU register file by %s (unimplemented)!\n", whoName[who]);

	if (offset == 0xF000E0)
	{
		// For reading, should only return the lower 5 bits...
		uint16 data = (tom_jerry_int_pending << 4) | (tom_timer_int_pending << 3)
			| (tom_object_int_pending << 2) | (tom_gpu_int_pending << 1)
			| (tom_video_int_pending << 0);
		//WriteLog("tom: interrupt status is 0x%.4x \n",data);
		return data;
	}
//Shoud be handled by the jaguar main loop now... And it is! ;-)
/*	else if (offset == 0xF00006)	// VC
	// What if we're in interlaced mode?
	// According to docs, in non-interlace mode VC is ALWAYS even...
//		return (tom_scanline << 1);// + 1;
//But it's causing Rayman to be fucked up... Why???
//Because VC is even in NI mode when calling the OP! That's why!
		return (tom_scanline << 1) + 1;//*/
/*
//	F00004          R/W   -----xxx xxxxxxxx   HC - horizontal count
//	                      -----x-- --------      (which half of the display)
//	                      ------xx xxxxxxxx      (10-bit counter)
*/
// This is a kludge to get the HC working somewhat... What we really should do here
// is check what the global time is at the time of the read and calculate the correct HC...
// !!! FIX !!!
	else if (offset == 0xF00004)
		return rand() & 0x03FF;
	else if ((offset >= GPU_CONTROL_RAM_BASE) && (offset < GPU_CONTROL_RAM_BASE + 0x20))
		return GPUReadWord(offset, who);
	else if ((offset >= GPU_WORK_RAM_BASE) && (offset < GPU_WORK_RAM_BASE + 0x1000))
		return GPUReadWord(offset, who);
/*	else if ((offset >= 0xF00010) && (offset < 0xF00028))
		return OPReadWord(offset, who);*/
	else if ((offset >= 0xF02200) && (offset < 0xF022A0))
		return BlitterReadWord(offset, who);
	else if (offset == 0xF00050)
		return tomTimerPrescaler;
	else if (offset == 0xF00052)
		return tomTimerDivider;

	offset &= 0x3FFF;
	return (TOMReadByte(offset, who) << 8) | TOMReadByte(offset + 1, who);
}


#define TOM_STRICT_MEMORY_ACCESS
//
// TOM byte access (write)
//
void TOMWriteByte(uint32 offset, uint8 data, uint32 who/*=UNKNOWN*/)
{
#ifdef TOM_DEBUG
	WriteLog("TOM: Writing byte %02X at %06X", data, offset);
#endif
//???Is this needed???
// Perhaps on the writes--32-bit writes that is! And masked with FF7FFF...
#ifndef TOM_STRICT_MEMORY_ACCESS
	offset &= 0xFF3FFF;
#else
	// "Fast" (32-bit only) write access to the GPU
//	if ((offset >= 0xF0A100) && (offset <= 0xF0BFFF))
	if ((offset >= 0xF08000) && (offset <= 0xF0BFFF))
		offset &= 0xFF7FFF;
#endif
#ifdef TOM_DEBUG
	WriteLog(" -->[%06X] by %s\n", offset, whoName[who]);
#endif

#ifdef TOM_STRICT_MEMORY_ACCESS
	// Sanity check ("Aww, there ain't no Sanity Clause...")
	if ((offset < 0xF00000) || (offset > 0xF03FFF))
		return;
#endif

	if ((offset >= GPU_CONTROL_RAM_BASE) && (offset < GPU_CONTROL_RAM_BASE+0x20))
	{
		GPUWriteByte(offset, data, who);
		return;
	}
	else if ((offset >= GPU_WORK_RAM_BASE) && (offset < GPU_WORK_RAM_BASE+0x1000))
	{
		GPUWriteByte(offset, data, who);
		return;
	}
/*	else if ((offset >= 0xF00010) && (offset < 0xF00028))
	{
		OPWriteByte(offset, data, who);
		return;
	}*/
	else if ((offset >= 0xF02200) && (offset < 0xF022A0))
	{
		BlitterWriteByte(offset, data, who);
		return;
	}
	else if (offset == 0xF00050)
	{
		tomTimerPrescaler = (tomTimerPrescaler & 0x00FF) | (data << 8);
		TOMResetPIT();
		return;
	}
	else if (offset == 0xF00051)
	{
		tomTimerPrescaler = (tomTimerPrescaler & 0xFF00) | data;
		TOMResetPIT();
		return;
	}
	else if (offset == 0xF00052)
	{
		tomTimerDivider = (tomTimerDivider & 0x00FF) | (data << 8);
		TOMResetPIT();
		return;
	}
	else if (offset == 0xF00053)
	{
		tomTimerDivider = (tomTimerDivider & 0xFF00) | data;
		TOMResetPIT();
		return;
	}
	else if (offset >= 0xF00400 && offset <= 0xF007FF)	// CLUT (A & B)
	{
		// Writing to one CLUT writes to the other
		offset &= 0x5FF;		// Mask out $F00600 (restrict to $F00400-5FF)
		tomRam8[offset] = data, tomRam8[offset + 0x200] = data;
	}

	tomRam8[offset & 0x3FFF] = data;
}


//
// TOM word access (write)
//
void TOMWriteWord(uint32 offset, uint16 data, uint32 who/*=UNKNOWN*/)
{
#ifdef TOM_DEBUG
	WriteLog("TOM: Writing byte %04X at %06X", data, offset);
#endif
//???Is this needed??? Yes, but we need to be more vigilant than this.
#ifndef TOM_STRICT_MEMORY_ACCESS
	offset &= 0xFF3FFF;
#else
	// "Fast" (32-bit only) write access to the GPU
//	if ((offset >= 0xF0A100) && (offset <= 0xF0BFFF))
	if ((offset >= 0xF08000) && (offset <= 0xF0BFFF))
		offset &= 0xFF7FFF;
#endif
#ifdef TOM_DEBUG
	WriteLog(" -->[%06X] by %s\n", offset, whoName[who]);
#endif

#ifdef TOM_STRICT_MEMORY_ACCESS
	// Sanity check
	if ((offset < 0xF00000) || (offset > 0xF03FFF))
		return;
#endif

//if (offset == 0xF00000 + MEMCON1)
//	WriteLog("TOM: Memory Configuration 1 written by %s: %04X\n", whoName[who], data);
//if (offset == 0xF00000 + MEMCON2)
//	WriteLog("TOM: Memory Configuration 2 written by %s: %04X\n", whoName[who], data);
if (offset >= 0xF02000 && offset <= 0xF020FF)
	WriteLog("TOM: WriteWord attempted to GPU register file by %s (unimplemented)!\n", whoName[who]);

	if ((offset >= GPU_CONTROL_RAM_BASE) && (offset < GPU_CONTROL_RAM_BASE+0x20))
	{
		GPUWriteWord(offset, data, who);
		return;
	}
	else if ((offset >= GPU_WORK_RAM_BASE) && (offset < GPU_WORK_RAM_BASE+0x1000))
	{
		GPUWriteWord(offset, data, who);
		return;
	}
//What's so special about this?
/*	else if ((offset >= 0xF00000) && (offset < 0xF00002))
	{
		TOMWriteByte(offset, data >> 8);
		TOMWriteByte(offset+1, data & 0xFF);
	}*/
/*	else if ((offset >= 0xF00010) && (offset < 0xF00028))
	{
		OPWriteWord(offset, data, who);
		return;
	}*/
	else if (offset == 0xF00050)
	{
		tomTimerPrescaler = data;
		TOMResetPIT();
		return;
	}
	else if (offset == 0xF00052)
	{
		tomTimerDivider = data;
		TOMResetPIT();
		return;
	}
	else if (offset == 0xF000E0)
	{
//Check this out...
		if (data & 0x0100)
			tom_video_int_pending = 0;
		if (data & 0x0200)
			tom_gpu_int_pending = 0;
		if (data & 0x0400)
			tom_object_int_pending = 0;
		if (data & 0x0800)
			tom_timer_int_pending = 0;
		if (data & 0x1000)
			tom_jerry_int_pending = 0;

//		return;
	}
	else if ((offset >= 0xF02200) && (offset <= 0xF0229F))
	{
		BlitterWriteWord(offset, data, who);
		return;
	}
	else if (offset >= 0xF00400 && offset <= 0xF007FE)	// CLUT (A & B)
	{
		// Writing to one CLUT writes to the other
		offset &= 0x5FF;		// Mask out $F00600 (restrict to $F00400-5FF)
// Watch out for unaligned writes here! (Not fixed yet)
#warning "!!! Watch out for unaligned writes here !!! FIX !!!"
		SET16(tomRam8, offset, data);
		SET16(tomRam8, offset + 0x200, data);
	}

	offset &= 0x3FFF;
	if (offset == 0x28)			// VMODE (Why? Why not OBF?)
//Actually, we should check to see if the Enable bit of VMODE is set before doing this... !!! FIX !!!
#warning "Actually, we should check to see if the Enable bit of VMODE is set before doing this... !!! FIX !!!"
		objectp_running = 1;

	if (offset >= 0x30 && offset <= 0x4E)
		data &= 0x07FF;			// These are (mostly) 11-bit registers
	if (offset == 0x2E || offset == 0x36 || offset == 0x54)
		data &= 0x03FF;			// These are all 10-bit registers

// Fix a lockup bug... :-P
	TOMWriteByte(0xF00000 | offset, data >> 8, who);
	TOMWriteByte(0xF00000 | (offset+1), data & 0xFF, who);

if (offset == MEMCON1)
	WriteLog("TOM: Memory Config 1 written by %s: $%04X\n", whoName[who], data);
if (offset == MEMCON2)
	WriteLog("TOM: Memory Config 2 written by %s: $%04X\n", whoName[who], data);
//if (offset == OLP)
//	WriteLog("TOM: Object List Pointer written by %s: $%04X\n", whoName[who], data);
//if (offset == OLP + 2)
//	WriteLog("TOM: Object List Pointer +2 written by %s: $%04X\n", whoName[who], data);
//if (offset == OBF)
//	WriteLog("TOM: Object Processor Flag written by %s: %u\n", whoName[who], data);
if (offset == VMODE)
	WriteLog("TOM: Video Mode written by %s: %04X. PWIDTH = %u, MODE = %s, flags:%s%s (VC = %u)\n", whoName[who], data, ((data >> 9) & 0x07) + 1, videoMode_to_str[(data & MODE) >> 1], (data & BGEN ? " BGEN" : ""), (data & VARMOD ? " VARMOD" : ""), GET16(tomRam8, VC));
if (offset == BORD1)
	WriteLog("TOM: Border 1 written by %s: $%04X\n", whoName[who], data);
if (offset == BORD2)
	WriteLog("TOM: Border 2 written by %s: $%04X\n", whoName[who], data);
if (offset == HP)
	WriteLog("TOM: Horizontal Period written by %s: %u (+1*2 = %u)\n", whoName[who], data, (data + 1) * 2);
if (offset == HBB)
	WriteLog("TOM: Horizontal Blank Begin written by %s: %u\n", whoName[who], data);
if (offset == HBE)
	WriteLog("TOM: Horizontal Blank End written by %s: %u\n", whoName[who], data);
if (offset == HS)
	WriteLog("TOM: Horizontal Sync written by %s: %u\n", whoName[who], data);
if (offset == HVS)
	WriteLog("TOM: Horizontal Vertical Sync written by %s: %u\n", whoName[who], data);
if (offset == HDB1)
	WriteLog("TOM: Horizontal Display Begin 1 written by %s: %u\n", whoName[who], data);
if (offset == HDB2)
	WriteLog("TOM: Horizontal Display Begin 2 written by %s: %u\n", whoName[who], data);
if (offset == HDE)
	WriteLog("TOM: Horizontal Display End written by %s: %u\n", whoName[who], data);
if (offset == VP)
	WriteLog("TOM: Vertical Period written by %s: %u (%sinterlaced)\n", whoName[who], data, (data & 0x01 ? "non-" : ""));
if (offset == VBB)
	WriteLog("TOM: Vertical Blank Begin written by %s: %u\n", whoName[who], data);
if (offset == VBE)
	WriteLog("TOM: Vertical Blank End written by %s: %u\n", whoName[who], data);
if (offset == VS)
	WriteLog("TOM: Vertical Sync written by %s: %u\n", whoName[who], data);
if (offset == VDB)
	WriteLog("TOM: Vertical Display Begin written by %s: %u\n", whoName[who], data);
if (offset == VDE)
	WriteLog("TOM: Vertical Display End written by %s: %u\n", whoName[who], data);
if (offset == VEB)
	WriteLog("TOM: Vertical Equalization Begin written by %s: %u\n", whoName[who], data);
if (offset == VEE)
	WriteLog("TOM: Vertical Equalization End written by %s: %u\n", whoName[who], data);
if (offset == VI)
	WriteLog("TOM: Vertical Interrupt written by %s: %u\n", whoName[who], data);
if (offset == PIT0)
	WriteLog("TOM: PIT0 written by %s: %u\n", whoName[who], data);
if (offset == PIT1)
	WriteLog("TOM: PIT1 written by %s: %u\n", whoName[who], data);
if (offset == HEQ)
	WriteLog("TOM: Horizontal Equalization End written by %s: %u\n", whoName[who], data);
//if (offset == BG)
//	WriteLog("TOM: Background written by %s: %u\n", whoName[who], data);
//if (offset == INT1)
//	WriteLog("TOM: CPU Interrupt Control written by %s: $%04X (%s%s%s%s%s)\n", whoName[who], data, (data & 0x01 ? "Video" : ""), (data & 0x02 ? " GPU" : ""), (data & 0x04 ? " OP" : ""), (data & 0x08 ? " TOMPIT" : ""), (data & 0x10 ? " Jerry" : ""));

	// detect screen resolution changes
//This may go away in the future, if we do the virtualized screen thing...
//This may go away soon!
// TOM Shouldn't be mucking around with this, it's up to the host system to properly
// handle this kind of crap.
// NOTE: This is needed somehow, need to get rid of the dependency on this crap.
#warning "!!! Need to get rid of this dependency !!!"
#if 1
	if ((offset >= 0x28) && (offset <= 0x4F))
	{
		uint32 width = TOMGetVideoModeWidth(), height = TOMGetVideoModeHeight();

		if ((width != tomWidth) || (height != tomHeight))
		{
			tomWidth = width, tomHeight = height;

#warning "!!! TOM: ResizeScreen commented out !!!"
// No need to resize anything, since we're prepared for this...
//			if (vjs.renderType == RT_NORMAL)
//				ResizeScreen(tomWidth, tomHeight);
		}
	}
#endif
}


int TOMIRQEnabled(int irq)
{
	// This is the correct byte in big endian... D'oh!
//	return jaguar_byte_read(0xF000E1) & (1 << irq);
	return tomRam8[INT1 + 1/*0xE1*/] & (1 << irq);
}


// NEW:
// TOM Programmable Interrupt Timer handler
// NOTE: TOM's PIT is only enabled if the prescaler is != 0
//       The PIT only generates an interrupt when it counts down to zero, not when loaded!

void TOMPITCallback(void);


void TOMResetPIT(void)
{
#ifndef NEW_TIMER_SYSTEM
//Probably should *add* this amount to the counter to retain cycle accuracy! !!! FIX !!! [DONE]
//Also, why +1??? 'Cause that's what it says in the JTRM...!
//There is a small problem with this approach: If both the prescaler and the divider are equal
//to $FFFF then the counter won't be large enough to handle it. !!! FIX !!!
	if (tom_timer_prescaler)
		tom_timer_counter += (1 + tom_timer_prescaler) * (1 + tom_timer_divider);
//	WriteLog("tom: reseting timer to 0x%.8x (%i)\n",tom_timer_counter,tom_timer_counter);
#else
	// Need to remove previous timer from the queue, if it exists...
	RemoveCallback(TOMPITCallback);

	if (tomTimerPrescaler)
	{
		double usecs = (float)(tomTimerPrescaler + 1) * (float)(tomTimerDivider + 1) * RISC_CYCLE_IN_USEC;
		SetCallbackTime(TOMPITCallback, usecs);
	}
#endif
}


//
// TOM Programmable Interrupt Timer handler
// NOTE: TOM's PIT is only enabled if the prescaler is != 0
//
//NOTE: This is only used by the old execution code... Safe to remove
//      once the timer system is stable.
void TOMExecPIT(uint32 cycles)
{
	if (tomTimerPrescaler)
	{
		tomTimerCounter -= cycles;

		if (tomTimerCounter <= 0)
		{
			TOMSetPendingTimerInt();
			GPUSetIRQLine(GPUIRQ_TIMER, ASSERT_LINE);	// GPUSetIRQLine does the 'IRQ enabled' checking

			if (TOMIRQEnabled(IRQ_TIMER))
				m68k_set_irq(2);				// Cause a 68000 IPL 2...

			TOMResetPIT();
		}
	}
}


void TOMPITCallback(void)
{
//	INT1_RREG |= 0x08;							// Set TOM PIT interrupt pending
	TOMSetPendingTimerInt();
    GPUSetIRQLine(GPUIRQ_TIMER, ASSERT_LINE);	// It does the 'IRQ enabled' checking

//	if (INT1_WREG & 0x08)
	if (TOMIRQEnabled(IRQ_TIMER))
		m68k_set_irq(2);						// Generate a 68K IPL 2...

	TOMResetPIT();
}

