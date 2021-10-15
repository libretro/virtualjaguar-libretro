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
#include "event.h"
#include "gpu.h"
#include "jaguar.h"
#include "m68000/m68kinterface.h"
#include "op.h"
#include "settings.h"

// Red Color Values for CrY<->RGB Color Conversion
uint8_t redcv[16][16] = {
   //  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
   // ----------------------------------------------------------------------
	{  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},    // 0
	{  34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 19, 0},    // 1
	{  68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 64, 43, 21, 0},    // 2
	{  102,102,102,102,102,102,102,102,102,102,102,95, 71, 47, 23, 0},    // 3
	{  135,135,135,135,135,135,135,135,135,135,130,104,78, 52, 26, 0},    // 4
	{  169,169,169,169,169,169,169,169,169,170,141,113,85, 56, 28, 0},    // 5
	{  203,203,203,203,203,203,203,203,203,183,153,122,91, 61, 30, 0},    // 6
	{  237,237,237,237,237,237,237,237,230,197,164,131,98, 65, 32, 0},    // 7
	{  255,255,255,255,255,255,255,255,247,214,181,148,15, 82, 49, 7},    // 8
	{  255,255,255,255,255,255,255,255,255,235,204,173,143,112,81, 51},   // 9
	{  255,255,255,255,255,255,255,255,255,255,227,198,170,141,113,85},   // A
	{  255,255,255,255,255,255,255,255,255,255,249,223,197,171,145,119},  // B
	{  255,255,255,255,255,255,255,255,255,255,255,248,224,200,177,153},  // C
	{  255,255,255,255,255,255,255,255,255,255,255,255,252,230,208,187},  // D
	{  255,255,255,255,255,255,255,255,255,255,255,255,255,255,240,221},  // E
	{  255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255}   // F
};

// Green Color Values for CrY<->RGB Color Conversion
uint8_t greencv[16][16] = {
   //  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
   // ----------------------------------------------------------------------
	{  0,  17, 34, 51,68, 85, 102,119,136,153,170,187,204,221,238,255},   // 0
	{  0,  19, 38, 57,77, 96, 115,134,154,173,192,211,231,250,255,255},   // 1
	{  0,  21, 43, 64,86, 107,129,150,172,193,215,236,255,255,255,255},   // 2
	{  0,  23, 47, 71,95, 119,142,166,190,214,238,255,255,255,255,255},   // 3
	{  0,  26, 52, 78,104,130,156,182,208,234,255,255,255,255,255,255},   // 4
	{  0,  28, 56, 85,113,141,170,198,226,255,255,255,255,255,255,255},   // 5
	{  0,  30, 61, 91,122,153,183,214,244,255,255,255,255,255,255,255},   // 6
	{  0,  32, 65, 98,131,164,197,230,255,255,255,255,255,255,255,255},   // 7
	{  0,  32, 65, 98,131,164,197,230,255,255,255,255,255,255,255,255},   // 8
	{  0,  30, 61, 91,122,153,183,214,244,255,255,255,255,255,255,255},   // 9
	{  0,  28, 56, 85,113,141,170,198,226,255,255,255,255,255,255,255},   // A
	{  0,  26, 52, 78,104,130,156,182,208,234,255,255,255,255,255,255},   // B
	{  0,  23, 47, 71,95, 119,142,166,190,214,238,255,255,255,255,255},   // C
	{  0,  21, 43, 64,86, 107,129,150,172,193,215,236,255,255,255,255},   // D
	{  0,  19, 38, 57,77, 96, 115,134,154,173,192,211,231,250,255,255},   // E
	{  0,  17, 34, 51,68, 85, 102,119,136,153,170,187,204,221,238,255}    // F
};
   
// Blue Color Values for CrY<->RGB Color Conversion
uint8_t bluecv[16][16] = {
   //  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
   // ----------------------------------------------------------------------
	{  255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255},  // 0
	{  255,255,255,255,255,255,255,255,255,255,255,255,255,255,240,221},  // 1
	{  255,255,255,255,255,255,255,255,255,255,255,255,252,230,208,187},  // 2
	{  255,255,255,255,255,255,255,255,255,255,255,248,224,200,177,153},  // 3
	{  255,255,255,255,255,255,255,255,255,255,249,223,197,171,145,119},  // 4
	{  255,255,255,255,255,255,255,255,255,255,227,198,170,141,113,85},   // 5
	{  255,255,255,255,255,255,255,255,255,235,204,173,143,112,81, 51},   // 6
	{  255,255,255,255,255,255,255,255,247,214,181,148,115,82, 49, 17},   // 7
	{  237,237,237,237,237,237,237,237,230,197,164,131,98, 65, 32, 0},    // 8
	{  203,203,203,203,203,203,203,203,203,183,153,122,91, 61, 30, 0},    // 9
	{  169,169,169,169,169,169,169,169,169,170,141,113,85, 56, 28, 0},    // A
	{  135,135,135,135,135,135,135,135,135,135,130,104,78, 52, 26, 0},    // B
	{  102,102,102,102,102,102,102,102,102,102,102,95, 71, 47, 23, 0},    // C
	{  68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 64, 43, 21, 0},    // D
	{  34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 19, 0},    // E
	{  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}     // F
};

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

#define LEFT_VISIBLE_HC			(208 - 16 - (1 * 4))
#define RIGHT_VISIBLE_HC		(LEFT_VISIBLE_HC + (VIRTUAL_SCREEN_WIDTH * 4))
#define TOP_VISIBLE_VC			31
#define BOTTOM_VISIBLE_VC		511

//Are these PAL horizontals correct?
//They seem to be for the most part, but there are some games that seem to be
//shifted over to the right from this "window".
#define LEFT_VISIBLE_HC_PAL	(208 - 16 - (-3 * 4))
#define RIGHT_VISIBLE_HC_PAL	(LEFT_VISIBLE_HC_PAL + (VIRTUAL_SCREEN_WIDTH * 4))
#define TOP_VISIBLE_VC_PAL		67
#define BOTTOM_VISIBLE_VC_PAL	579

#ifdef __LIBRETRO__
extern int doom_res_hack;
#endif

uint8_t tomRam8[0x4000];
uint32_t tomWidth, tomHeight;
uint32_t tomTimerPrescaler;
uint32_t tomTimerDivider;
int32_t tomTimerCounter;
uint16_t tom_jerry_int_pending, tom_timer_int_pending, tom_object_int_pending,
         tom_gpu_int_pending, tom_video_int_pending;

// These are set by the "user" of the Jaguar core lib, since these are
// OS/system dependent.
uint32_t * screenBuffer;
uint32_t screenPitch;

typedef void (render_xxx_scanline_fn)(uint32_t *);

// Private function prototypes

void tom_render_16bpp_cry_scanline(uint32_t * backbuffer);
void tom_render_24bpp_scanline(uint32_t * backbuffer);
void tom_render_16bpp_direct_scanline(uint32_t * backbuffer);
void tom_render_16bpp_rgb_scanline(uint32_t * backbuffer);
void tom_render_16bpp_cry_rgb_mix_scanline(uint32_t * backbuffer);

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

uint32_t RGB16ToRGB32[0x10000];
uint32_t CRY16ToRGB32[0x10000];
uint32_t MIX16ToRGB32[0x10000];

//#warning "This is not endian-safe. !!! FIX !!!"
void TOMFillLookupTables(void)
{
   // NOTE: Jaguar 16-bit (non-CRY) color is RBG 556 like so:
   //       RRRR RBBB BBGG GGGG
  
   unsigned i;
   for(i=0; i<0x10000; i++)
      RGB16ToRGB32[i] = 0xFF000000
         | ((i & 0xF800) << 8)					// Red
         | ((i & 0x003F) << 10)					// Green
         | ((i & 0x07C0) >> 3);					// Blue

   for(i=0; i<0x10000; i++)
   {
      uint32_t cyan = (i & 0xF000) >> 12,
               red = (i & 0x0F00) >> 8,
               intensity = (i & 0x00FF);

      uint32_t r = (((uint32_t)redcv[cyan][red]) * intensity) >> 8,
               g = (((uint32_t)greencv[cyan][red]) * intensity) >> 8,
               b = (((uint32_t)bluecv[cyan][red]) * intensity) >> 8;

      CRY16ToRGB32[i] = 0xFF000000 | (r << 16) | (g << 8) | (b << 0);
      MIX16ToRGB32[i] = ((i & 0x01) ? RGB16ToRGB32[i] : CRY16ToRGB32[i]);
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


uint8_t * TOMGetRamPointer(void)
{
   return tomRam8;
}


uint8_t TOMGetVideoMode(void)
{
   uint16_t vmode = GET16(tomRam8, VMODE);
   return ((vmode & VARMOD) >> 6) | ((vmode & MODE) >> 1);
}

uint16_t TOMGetVDB(void)
{
   return GET16(tomRam8, VDB);
}

uint16_t TOMGetHC(void)
{
   return GET16(tomRam8, HC);
}


uint16_t TOMGetVP(void)
{
   return GET16(tomRam8, VP);
}

uint16_t TOMGetMEMCON1(void)
{
   return GET16(tomRam8, MEMCON1);
}

#define LEFT_BG_FIX
// 16 BPP CRY/RGB mixed mode rendering
void tom_render_16bpp_cry_rgb_mix_scanline(uint32_t * backbuffer)
{
   //CHANGED TO 32BPP RENDERING
   uint16_t width = tomWidth;
   uint8_t * current_line_buffer = (uint8_t *)&tomRam8[0x1800];

   //New stuff--restrict our drawing...
   uint8_t pwidth = ((GET16(tomRam8, VMODE) & PWIDTH) >> 9) + 1;
   //NOTE: May have to check HDB2 as well!
   // Get start position in HC ticks
   int16_t startPos = GET16(tomRam8, HDB1) - (vjs.hardwareTypeNTSC ? LEFT_VISIBLE_HC : LEFT_VISIBLE_HC_PAL);
   // Convert to pixels
   startPos /= pwidth;

   if (startPos < 0)
      // This is x2 because current_line_buffer is uint8_t & we're in a 16bpp mode
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
      unsigned i;
      uint8_t g = tomRam8[BORD1], r = tomRam8[BORD1 + 1], b = tomRam8[BORD2 + 1];
      uint32_t pixel = 0xFF000000 | (r << 16) | (g << 8) | (b << 0);

      for(i=0; i<startPos; i++)
         *backbuffer++ = pixel;

      width -= startPos;
   }
#else
   backbuffer += 2 * startPos, width -= startPos;
#endif

   while (width)
   {
      uint16_t color = (*current_line_buffer++) << 8;
      color |= *current_line_buffer++;
      *backbuffer++ = MIX16ToRGB32[color];
      width--;
   }
}

// 16 BPP CRY mode rendering
void tom_render_16bpp_cry_scanline(uint32_t * backbuffer)
{
   unsigned i;
   //CHANGED TO 32BPP RENDERING
   uint16_t width = tomWidth;
   uint8_t * current_line_buffer = (uint8_t *)&tomRam8[0x1800];

   //New stuff--restrict our drawing...
   uint8_t pwidth = ((GET16(tomRam8, VMODE) & PWIDTH) >> 9) + 1;
   //NOTE: May have to check HDB2 as well!
   int16_t startPos = GET16(tomRam8, HDB1) - (vjs.hardwareTypeNTSC ? LEFT_VISIBLE_HC : LEFT_VISIBLE_HC_PAL);// Get start position in HC ticks
   startPos /= pwidth;
   if (startPos < 0)
      current_line_buffer += 2 * -startPos;
   else
#ifdef LEFT_BG_FIX
   {
      uint8_t g = tomRam8[BORD1], r = tomRam8[BORD1 + 1], b = tomRam8[BORD2 + 1];
      uint32_t pixel = 0xFF000000 | (r << 16) | (g << 8) | (b << 0);

      for(i=0; i<startPos; i++)
         *backbuffer++ = pixel;

      width -= startPos;
   }
#else
   //This should likely be 4 instead of 2 (?--not sure)
   backbuffer += 2 * startPos, width -= startPos;
#endif

   while (width)
   {
      uint16_t color = (*current_line_buffer++) << 8;
      color |= *current_line_buffer++;
      *backbuffer++ = CRY16ToRGB32[color];
#ifdef __LIBRETRO__
      //Double pixel screen on doom if pwidth=8 -> (163*2)
      if(doom_res_hack==1)
         if(pwidth==8)*backbuffer++ = CRY16ToRGB32[color];
#endif
      width--;
   }
}

// 24 BPP mode rendering
void tom_render_24bpp_scanline(uint32_t * backbuffer)
{
   unsigned i;
   //CHANGED TO 32BPP RENDERING
   uint16_t width = tomWidth;
   uint8_t * current_line_buffer = (uint8_t *)&tomRam8[0x1800];

   //New stuff--restrict our drawing...
   uint8_t pwidth = ((GET16(tomRam8, VMODE) & PWIDTH) >> 9) + 1;
   //NOTE: May have to check HDB2 as well!
   int16_t startPos = GET16(tomRam8, HDB1) - (vjs.hardwareTypeNTSC ? LEFT_VISIBLE_HC : LEFT_VISIBLE_HC_PAL);	// Get start position in HC ticks
   startPos /= pwidth;
   if (startPos < 0)
      current_line_buffer += 4 * -startPos;
   else
#ifdef LEFT_BG_FIX
   {
      uint8_t g = tomRam8[BORD1], r = tomRam8[BORD1 + 1], b = tomRam8[BORD2 + 1];
      uint32_t pixel = 0xFF000000 | (r << 16) | (g << 8) | (b << 0);

      for(i=0; i<startPos; i++)
         *backbuffer++ = pixel;

      width -= startPos;
   }
#else
   //This should likely be 4 instead of 2 (?--not sure)
   backbuffer += 2 * startPos, width -= startPos;
#endif

   while (width)
   {
      uint32_t b;
      uint32_t g = *current_line_buffer++;
      uint32_t r = *current_line_buffer++;
      current_line_buffer++;
      b = *current_line_buffer++;
      //hm.		*backbuffer++ = 0xFF000000 | (b << 16) | (g << 8) | r;
      *backbuffer++ = 0xFF000000 | (r << 16) | (g << 8) | (b << 0);
      width--;
   }
}

//Seems to me that this is NOT a valid mode--the JTRM seems to imply that you would need
//extra hardware outside of the Jaguar console to support this!
// 16 BPP direct mode rendering
void tom_render_16bpp_direct_scanline(uint32_t * backbuffer)
{
   uint16_t width = tomWidth;
   uint8_t * current_line_buffer = (uint8_t *)&tomRam8[0x1800];

   while (width)
   {
      uint16_t color = (*current_line_buffer++) << 8;
      color |= *current_line_buffer++;
      *backbuffer++ = color >> 1;
      width--;
   }
}


// 16 BPP RGB mode rendering
void tom_render_16bpp_rgb_scanline(uint32_t * backbuffer)
{
   unsigned i;
   //CHANGED TO 32BPP RENDERING
   // 16 BPP RGB: 0-5 green, 6-10 blue, 11-15 red

   uint16_t width = tomWidth;
   uint8_t * current_line_buffer = (uint8_t *)&tomRam8[0x1800];

   //New stuff--restrict our drawing...
   uint8_t pwidth = ((GET16(tomRam8, VMODE) & PWIDTH) >> 9) + 1;
   //NOTE: May have to check HDB2 as well!
   int16_t startPos = GET16(tomRam8, HDB1) - (vjs.hardwareTypeNTSC ? LEFT_VISIBLE_HC : LEFT_VISIBLE_HC_PAL);	// Get start position in HC ticks
   startPos /= pwidth;

   if (startPos < 0)
      current_line_buffer += 2 * -startPos;
   else
#ifdef LEFT_BG_FIX
   {
      uint8_t g = tomRam8[BORD1], r = tomRam8[BORD1 + 1], b = tomRam8[BORD2 + 1];
      uint32_t pixel = 0xFF000000 | (r << 16) | (g << 8) | (b << 0);

      for(i=0; i<startPos; i++)
         *backbuffer++ = pixel;

      width -= startPos;
   }
#else
   //This should likely be 4 instead of 2 (?--not sure)
   backbuffer += 2 * startPos, width -= startPos;
#endif

   while (width)
   {
      uint32_t color = (*current_line_buffer++) << 8;
      color |= *current_line_buffer++;
      *backbuffer++ = RGB16ToRGB32[color];
      width--;
   }
}

// Process a single scanline
void TOMExecHalfline(uint16_t halfline, bool render)
{
   unsigned i;
   uint16_t field2 = halfline & 0x0800;
   bool inActiveDisplayArea = true;
   uint16_t startingHalfline;
   uint16_t endingHalfline;
   uint32_t * TOMCurrentLine = 0;
   uint16_t topVisible;
   uint16_t bottomVisible;

   halfline &= 0x07FF;

   if (halfline & 0x01)							// Execute OP only on even halflines (non-interlaced only!)
      // Execute OP only on even halflines (skip higher resolutions for now...)
      return;

   // Initial values that "well behaved" programs use
   startingHalfline = GET16(tomRam8, VDB);
   endingHalfline = GET16(tomRam8, VDE);

   // Simulate the OP start bug here!
   // Really, this value is somewhere around 507 for an NTSC Jaguar. But this
   // should work in a majority of cases, at least until we can figure it out properly.
   if (endingHalfline > GET16(tomRam8, VP))
      startingHalfline = 0;

   if ((halfline >= startingHalfline) && (halfline < endingHalfline))
   {
      if (render)
      {
         uint8_t * current_line_buffer = (uint8_t *)&tomRam8[0x1800];
         uint8_t bgHI = tomRam8[BG], bgLO = tomRam8[BG + 1];

         // Clear line buffer with BG
         if (GET16(tomRam8, VMODE) & BGEN) // && (CRY or RGB16)...
            for(i=0; i<720; i++)
               *current_line_buffer++ = bgHI, *current_line_buffer++ = bgLO;

         OPProcessList(halfline, render);
      }
   }
   else
      inActiveDisplayArea = false;

   // Take PAL into account...

   topVisible = (vjs.hardwareTypeNTSC ? TOP_VISIBLE_VC : TOP_VISIBLE_VC_PAL);
   bottomVisible = (vjs.hardwareTypeNTSC ? BOTTOM_VISIBLE_VC : BOTTOM_VISIBLE_VC_PAL);

   // Bit 0 in VP is interlace flag. 0 = interlace, 1 = non-interlaced
   if (tomRam8[VP + 1] & 0x01)
      TOMCurrentLine = &(screenBuffer[((halfline - topVisible) / 2) * screenPitch]);//non-interlace
   else
      TOMCurrentLine = &(screenBuffer[(((halfline - topVisible) / 2) * screenPitch * 2) + (field2 ? 0 : screenPitch)]);//interlace

   // Here's our virtualized scanline code...

   if ((halfline >= topVisible) && (halfline < bottomVisible))
   {
      if (inActiveDisplayArea)
         scanline_render[TOMGetVideoMode()](TOMCurrentLine);
      else
      {
         // If outside of VDB & VDE, then display the border color
         uint32_t * currentLineBuffer = TOMCurrentLine;
         uint8_t      g = tomRam8[BORD1], r = tomRam8[BORD1 + 1], b = tomRam8[BORD2 + 1];
         uint32_t pixel = 0xFF000000 | (r << 16) | (g << 8) | (b << 0);

         for(i=0; i<tomWidth; i++)
            *currentLineBuffer++ = pixel;
      }
   }
}

// TOM initialization
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
}


uint32_t TOMGetVideoModeWidth(void)
{
   uint16_t pwidth = ((GET16(tomRam8, VMODE) & PWIDTH) >> 9) + 1;
   return (vjs.hardwareTypeNTSC ? RIGHT_VISIBLE_HC - LEFT_VISIBLE_HC : RIGHT_VISIBLE_HC_PAL - LEFT_VISIBLE_HC_PAL) / pwidth;
}

uint32_t TOMGetVideoModeHeight(void)
{
   return (vjs.hardwareTypeNTSC ? 240 : 256);
}

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

uint8_t TOMReadByte(uint32_t offset, uint32_t who)
{
   //???Is this needed???
   // It seems so. Perhaps it's the +$8000 offset being written to (32-bit interface)?
   // However, the 32-bit interface is WRITE ONLY, so that can't be it...
   // Also, the 68K CANNOT make use of the 32-bit interface, since its bus width is only 16-bits...
   //	offset &= 0xFF3FFF;

   if ((offset >= GPU_CONTROL_RAM_BASE) && (offset < GPU_CONTROL_RAM_BASE+0x20))
      return GPUReadByte(offset, who);
   else if ((offset >= GPU_WORK_RAM_BASE) && (offset < GPU_WORK_RAM_BASE+0x1000))
      return GPUReadByte(offset, who);
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


// TOM word access (read)
uint16_t TOMReadWord(uint32_t offset, uint32_t who)
{
   if (offset == 0xF000E0)
   {
      // For reading, should only return the lower 5 bits...
      uint16_t data = (tom_jerry_int_pending << 4) | (tom_timer_int_pending << 3)
         | (tom_object_int_pending << 2) | (tom_gpu_int_pending << 1)
         | (tom_video_int_pending << 0);
      return data;
   }
   else if (offset == 0xF00004)
      return rand() & 0x03FF;
   else if ((offset >= GPU_CONTROL_RAM_BASE) && (offset < GPU_CONTROL_RAM_BASE + 0x20))
      return GPUReadWord(offset, who);
   else if ((offset >= GPU_WORK_RAM_BASE) && (offset < GPU_WORK_RAM_BASE + 0x1000))
      return GPUReadWord(offset, who);
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
// TOM byte access (write)
void TOMWriteByte(uint32_t offset, uint8_t data, uint32_t who)
{
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

// TOM word access (write)
void TOMWriteWord(uint32_t offset, uint16_t data, uint32_t who)
{
   //???Is this needed??? Yes, but we need to be more vigilant than this.
#ifndef TOM_STRICT_MEMORY_ACCESS
   offset &= 0xFF3FFF;
#else
   // "Fast" (32-bit only) write access to the GPU
   //	if ((offset >= 0xF0A100) && (offset <= 0xF0BFFF))
   if ((offset >= 0xF08000) && (offset <= 0xF0BFFF))
      offset &= 0xFF7FFF;
#endif

#ifdef TOM_STRICT_MEMORY_ACCESS
   // Sanity check
   if ((offset < 0xF00000) || (offset > 0xF03FFF))
      return;
#endif

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
//#warning "!!! Watch out for unaligned writes here !!! FIX !!!"
      SET16(tomRam8, offset, data);
      SET16(tomRam8, offset + 0x200, data);
   }

   offset &= 0x3FFF;
   if (offset == 0x28)			// VMODE (Why? Why not OBF?)
      //Actually, we should check to see if the Enable bit of VMODE is set before doing this... !!! FIX !!!
//#warning "Actually, we should check to see if the Enable bit of VMODE is set before doing this... !!! FIX !!!"
      objectp_running = 1;

   if (offset >= 0x30 && offset <= 0x4E)
      data &= 0x07FF;			// These are (mostly) 11-bit registers
   if (offset == 0x2E || offset == 0x36 || offset == 0x54)
      data &= 0x03FF;			// These are all 10-bit registers

   // Fix a lockup bug... :-P
   TOMWriteByte(0xF00000 | offset, data >> 8, who);
   TOMWriteByte(0xF00000 | (offset+1), data & 0xFF, who);

   // detect screen resolution changes
   //This may go away in the future, if we do the virtualized screen thing...
   //This may go away soon!
   // TOM Shouldn't be mucking around with this, it's up to the host system to properly
   // handle this kind of crap.
   // NOTE: This is needed somehow, need to get rid of the dependency on this crap.
//#warning "!!! Need to get rid of this dependency !!!"
   if ((offset >= 0x28) && (offset <= 0x4F))
   {
      uint32_t width = TOMGetVideoModeWidth(), height = TOMGetVideoModeHeight();

      if ((width != tomWidth) || (height != tomHeight))
      {
         tomWidth = width, tomHeight = height;
      }
   }
}


int TOMIRQEnabled(int irq)
{
   // This is the correct byte in big endian... D'oh!
   //	return jaguar_byte_read(0xF000E1) & (1 << irq);
   return tomRam8[INT1 + 1] & (1 << irq);
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
#else
   // Need to remove previous timer from the queue, if it exists...
   RemoveCallback(TOMPITCallback);

   if (tomTimerPrescaler)
   {
      double usecs = (float)(tomTimerPrescaler + 1) * (float)(tomTimerDivider + 1) * RISC_CYCLE_IN_USEC;
      SetCallbackTime(TOMPITCallback, usecs, EVENT_MAIN);
   }
#endif
}

// TOM Programmable Interrupt Timer handler
// NOTE: TOM's PIT is only enabled if the prescaler is != 0
//
//NOTE: This is only used by the old execution code... Safe to remove
//      once the timer system is stable.
void TOMExecPIT(uint32_t cycles)
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
   TOMSetPendingTimerInt();                  // Set TOM PIT interrupt pending
   GPUSetIRQLine(GPUIRQ_TIMER, ASSERT_LINE); // It does the 'IRQ enabled' checking

   if (TOMIRQEnabled(IRQ_TIMER))
      m68k_set_irq(2); // Generate a 68K IPL 2...

   TOMResetPIT();
}
