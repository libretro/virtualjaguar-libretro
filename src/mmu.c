//
// mmu.cpp
//
// Jaguar Memory Manager Unit
//
// by James Hammons
//
// JLH = James Hammons <jlhamm@acm.org>
//
// WHO  WHEN        WHAT
// ---  ----------  -----------------------------------------------------------
// JLH  11/25/2009  Created this file. :-)
//

#include <stdlib.h>								// For NULL definition

#include <boolean.h>

#include "mmu.h"

#include "jagbios.h"
#include "wavetable.h"

/*
   Addresses to be handled:

   SYSTEM SETUP REGISTERS

 *MEMCON1	Memory Control Register 1			F00000		RW
 *MEMCON2	Memory Control Register 2			F00002		RW
 HC			Horizontal Count					F00004		RW
 VC			Vertical Count						F00006		RW
 LPH			Light Pen Horizontal				F00008		RO
 LPV			Light Pen Vertical					F0000A		RO
 OB[0-3]		Object Data Field					F00010-16	RO
 OLP			Object List Pointer					F00020-23	WO
 OBF			Object Flag							F00026		WO
 VMODE		Video Mode							F00028		WO
 BORD1		Border Colour (Red & Green)			F0002A		WO
 BORD2		Border Colour (Blue)				F0002C		WO
 *HP			Horizontal Period					F0002E		WO
 *HBB		Horizontal Blank Begin				F00030		WO
 *HBE		Horizontal Blank End				F00032		WO
 *HS			Horizontal Sync						F00034		WO
 *HVS		Horizontal Vertical Sync			F00036		WO
 HDB1		Horizontal Display Begin 1			F00038		WO
 HDB2		Horizontal Display Begin 2			F0003A		WO
 HDE			Horizontal Display End				F0003C		WO
 *VP			Vertical Period						F0003E		WO
 *VBB		Vertical Blank Begin				F00040		WO
 *VBE		Vertical Blank End					F00042		WO
 *VS			Vertical Sync						F00044		WO
 VDB			Vertical Display Begin				F00046		WO
 VDE			Vertical Display End				F00048		WO
 *VEB		Vertical Equalization Begin			F0004A		WO
 *VEE		Vertical Equalization End			F0004C		WO
 VI			Vertical Interrupt					F0004E		WO
 PIT[0-1]	Programmable Interrupt Timer		F00050-52	WO
 *HEQ		Horizontal Equalization End			F00054		WO
 BG			Background Colour					F00058		WO
 INT1		CPU Interrupt Control Register		F000E0		RW
 INT2		CPU Interrupt Resume Register		F000E2		WO
 CLUT		Colour Look-Up Table				F00400-7FE	RW
 LBUF		Line Buffer							F00800-1D9E	RW

 GPU REGISTERS

 G_FLAGS		GPU Flags Register					F02100		RW
 G_MTXC		Matrix Control Register				F02104		WO
 G_MTXA		Matrix Address Register				F02108		WO
 G_END		Data Organization Register			F0210C		WO
 G_PC		GPU Program Counter					F02110		RW
 G_CTRL		GPU Control/Status Register			F02114		RW
 G_HIDATA	High Data Register					F02118		RW
 G_REMAIN	Divide Unit Remainder				F0211C		RO
 G_DIVCTRL	Divide Unit Control					F0211C		WO

 BLITTER REGISTERS

 A1_BASE		A1 Base Register					F02200		WO
 A1_FLAGS	Flags Register						F02204		WO
 A1_CLIP		A1 Clipping Size					F02208		WO
 A1_PIXEL	A1 Pixel Pointer					F0220C		WO
 F02204		RO
 A1_STEP		A1 Step Value						F02210		WO
 A1_FSTEP	A1 Step Fraction Value				F02214		WO
 A1_FPIXEL	A1 Pixel Pointer Fraction			F02218		RW
 A1_INC		A1 Increment						F0221C		WO
 A1_FINC		A1 Increment Fraction				F02220		WO
 A2_BASE		A2 Base Register					F02224		WO
 A2_FLAGS	A2 Flags Register					F02228		WO
 A2_MASK		A2 Window Mask						F0222C		WO
 A2_PIXEL	A2 Pixel Pointer					F02230		WO
 F0222C		RO
A2_STEP		A2 Step Value						F02234		WO
B_CMD		Command/Status Register				F02238		RW
B_COUNT		Counters Register					F0223C		WO
B_SRCD		Source Data Register				F02240		WO
B_DSTD		Destination Data Register			F02248		WO
B_DSTZ		Destination Z Register				F02250		WO
B_SRCZ1		Source Z Register 1					F02258		WO
B_SRCZ2		Source Z Register 2					F02260		WO
B_PATD		Pattern Data Register				F02268		WO
B_IINC		Intensity Increment					F02270		WO
B_ZINC		Z Increment							F02274		WO
B_STOP		Collision Control					F02278		WO
B_I3		Intensity 3							F0227C		WO
B_I2		Intensity 2							F02280		WO
B_I1		Intensity 1							F02284		WO
B_I0		Intensity 0							F02288		WO
B_Z3		Z 3									F0228C		WO
B_Z2		Z 2									F02290		WO
B_Z1		Z 1									F02294		WO
B_Z0		Z 0									F02298		WO

JERRY REGISTERS

*CLK1		Processor Clock Divider				F10010		WO
*CLK2		Video Clock Divider					F10012		WO
*CLK3		Chroma Clock Divider				F10014		WO
JPIT1		Timer 1 Pre-scaler					F10000		WO
JPIT3		Timer 2 Pre-scaler					F10004		WO
JPIT2		Timer 1 Divider						F10002		WO
JPIT4		Timer 2 Divider						F10006		WO
J_INT		Interrup Control Register			F10020		RW
SCLK		Serial Clock Frequency				F1A150		WO
SMODE		Serial Mode							F1A154		WO
LTXD		Left Transmit Data					F1A148		WO
RTXD		Right Transmit Data					F1A14C		WO
LRXD		Left Receive Data					F1A148		RO
RRXD		Right Receive Data					F1A14C		RO
L_I2S		Left I2S Serial Interface			F1A148		RW
R_I2S		Right I2S Serial Interface			F1A14C		RW
SSTAT		Serial Status						F1A150		RO
ASICLK		Asynchronous Serial Interface Clock	F10034		RW
ASICTRL		Asynchronous Serial Control			F10032		WO
ASISTAT		Asynchronous Serial Status			F10032		RO
ASIDATA		Asynchronous Serial Data			F10030		RW

JOYSTICK REGISTERS

JOYSTICK	Joystick Register					F14000		RW
JOYBUTS		Button Register						F14002		RW

DSP REGISTERS

D_FLAGS		DSP Flags Register					F1A100		RW
D_MTXC		DSP Matrix Control Register			F1A104		WO
D_MTXA		DSP Matrix Address Register			F1A108		WO
D_END		DSP Data Organization Register		F1A10C		WO
D_PC		DSP Program Counter					F1A110		RW
D_CTRL		DSP Control/Status Register			F1A114		RW
D_MOD		Modulo Instruction Mask				F1A118		WO
D_REMAIN	Divide Unit Remainder				F1A11C		RO
D_DIVCTRL	Divide Unit Control					F1A11C		WO
D_MACHI		MAC High Result Bits				F1A120		RO
*/

enum MemType { MM_NOP = 0, MM_RAM = 1, MM_ROM = 2, MM_IO_R = 4, MM_IO_W = 8, MM_IO = 12 };

struct MemDesc {
   uint32_t startAddr;
   uint32_t endAddr;
   enum MemType type;
   //	(void (* ioFunc)(uint32, uint32)); // <-- could also be a pointer to RAM...
   void * readFunc;					// This is read & write with MM_IO
   void * writeFunc;
   uint32_t mask;
};


struct MemDesc memoryMap[] = {
   { 0x000000, 0x3FFFFF, MM_RAM,  &jaguarMainRAM },
   { 0x800000, 0xDFFEFF, MM_ROM,  &jaguarMainROM },

   { 0xDFFF00, 0xDFFF03, MM_IO,   &butch }, // base of Butch == interrupt control register, R/W
   { 0xDFFF04, 0xDFFF07, MM_IO,   &dscntrl }, // DSA control register, R/W
   { 0xDFFF0A, 0xDFFF0B, MM_IO,   &ds_data }, // DSA TX/RX data, R/W
   { 0xDFFF10, 0xDFFF13, MM_IO,   &i2cntrl }, // i2s bus control register, R/W
   { 0xDFFF14, 0xDFFF17, MM_IO,   &sbcntrl }, // CD subcode control register, R/W
   { 0xDFFF18, 0xDFFF1B, MM_IO,   &subdata }, // Subcode data register A
   { 0xDFFF1C, 0xDFFF1F, MM_IO,   &subdatb }, // Subcode data register B
   { 0xDFFF20, 0xDFFF23, MM_IO,   &sb_time }, // Subcode time and compare enable (D24)
   { 0xDFFF24, 0xDFFF27, MM_IO,   &fifo_data }, // i2s FIFO data
   { 0xDFFF28, 0xDFFF2B, MM_IO,   &i2sdat2 }, // i2s FIFO data (old)
   { 0xDFFF2C, 0xDFFF2F, MM_IO,   &unknown }, // Seems to be some sort of I2S interface

   { 0xE00000, 0xE1FFFF, MM_ROM,  jaguarBootROM },

   // TOM REGISTERS

   { 0xF00000, 0xF00001, MM_IO,   &memcon1 }, // *MEMCON1	Memory Control Register 1			F00000		RW
   { 0xF00002, 0xF00003, MM_IO,   &memcon2 }, // *MEMCON2	Memory Control Register 2			F00002		RW
   { 0xF00004, 0xF00005, MM_IO,   &hc }, // HC			Horizontal Count					F00004		RW
   { 0xF00006, 0xF00007, MM_IO,   &vc }, // VC			Vertical Count						F00006		RW
   { 0xF00008, 0xF00009, MM_IO_R, &lph }, // LPH			Light Pen Horizontal				F00008		RO
   { 0xF0000A, 0xF0000B, MM_IO_R, &lpv }, // LPV			Light Pen Vertical					F0000A		RO
   { 0xF00010, 0xF00017, MM_IO_R, &obData }, // OB[0-3]		Object Data Field					F00010-16	RO
   { 0xF00020, 0xF00023, MM_IO_W, &olp }, // OLP			Object List Pointer					F00020-23	WO
   { 0xF00026, 0xF00027, MM_IO_W, &obf }, // OBF			Object Flag							F00026		WO
   { 0xF00028, 0xF00029, MM_IO_W, &vmode }, // VMODE		Video Mode							F00028		WO
   { 0xF0002A, 0xF0002B, MM_IO_W, &bord1 }, // BORD1		Border Colour (Red & Green)			F0002A		WO
   { 0xF0002C, 0xF0002D, MM_IO_W, &bord2 }, // BORD2		Border Colour (Blue)				F0002C		WO
   { 0xF0002E, 0xF0002F, MM_IO_W, &hp }, // *HP			Horizontal Period					F0002E		WO
   { 0xF00030, 0xF00031, MM_IO_W, &hbb }, // *HBB		Horizontal Blank Begin				F00030		WO
   { 0xF00032, 0xF00033, MM_IO_W, &hbe }, // *HBE		Horizontal Blank End				F00032		WO
   { 0xF00034, 0xF00035, MM_IO_W, &hs }, // *HS			Horizontal Sync						F00034		WO
   { 0xF00036, 0xF00037, MM_IO_W, &hvs }, // *HVS		Horizontal Vertical Sync			F00036		WO
   { 0xF00038, 0xF00039, MM_IO_W, &hdb1 }, // HDB1		Horizontal Display Begin 1			F00038		WO
   { 0xF0003A, 0xF0003B, MM_IO_W, &hdb2 }, // HDB2		Horizontal Display Begin 2			F0003A		WO
   { 0xF0003C, 0xF0003D, MM_IO_W, &hde }, // HDE			Horizontal Display End				F0003C		WO
   { 0xF0003E, 0xF0003F, MM_IO_W, &vp }, // *VP			Vertical Period						F0003E		WO
   { 0xF00040, 0xF00041, MM_IO_W, &vbb }, // *VBB		Vertical Blank Begin				F00040		WO
   { 0xF00042, 0xF00043, MM_IO_W, &vbe }, // *VBE		Vertical Blank End					F00042		WO
   { 0xF00044, 0xF00045, MM_IO_W, &vs }, // *VS			Vertical Sync						F00044		WO
   { 0xF00046, 0xF00047, MM_IO_W, &vdb }, // VDB			Vertical Display Begin				F00046		WO
   { 0xF00048, 0xF00049, MM_IO_W, &vde }, // VDE			Vertical Display End				F00048		WO
   { 0xF0004A, 0xF0004B, MM_IO_W, &veb }, // *VEB		Vertical Equalization Begin			F0004A		WO
   { 0xF0004C, 0xF0004D, MM_IO_W, &vee }, // *VEE		Vertical Equalization End			F0004C		WO
   { 0xF0004E, 0xF0004F, MM_IO_W, &vi }, // VI			Vertical Interrupt					F0004E		WO
   { 0xF00050, 0xF00051, MM_IO_W, &pit0 }, // PIT[0-1]	Programmable Interrupt Timer		F00050-52	WO
   { 0xF00052, 0xF00053, MM_IO_W, &pit1 },
   { 0xF00054, 0xF00055, MM_IO_W, &heq }, // *HEQ		Horizontal Equalization End			F00054		WO
   { 0xF00058, 0xF0005B, MM_IO_W, &bg }, // BG			Background Colour					F00058		WO
   { 0xF000E0, 0xF000E1, MM_IO,   &int1 }, // INT1		CPU Interrupt Control Register		F000E0		RW
   { 0xF000E2, 0xF000E3, MM_IO_W, &int2 }, // INT2		CPU Interrupt Resume Register		F000E2		WO
   //Some of these RAM spaces may be 16- or 32-bit only... in which case, we need
   //to cast appropriately (in memory.cpp, that is)...
   { 0xF00400, 0xF005FF, MM_RAM,  &clut }, // CLUT		Colour Look-Up Table				F00400-7FE	RW
   { 0xF00600, 0xF007FF, MM_RAM,  &clut },
   { 0xF00800, 0xF01D9F, MM_RAM,  &lbuf }, // LBUF		Line Buffer							F00800-1D9E	RW
   //Need high speed RAM interface for GPU & DSP (we have it now...)

   // GPU REGISTERS

   { 0xF02100, 0xF02103, MM_IO,   &g_flags }, // G_FLAGS		GPU Flags Register					F02100		RW
   { 0xF02104, 0xF02107, MM_IO_W, &g_mtxc }, // G_MTXC		Matrix Control Register				F02104		WO
   { 0xF02108, 0xF0210B, MM_IO_W, &g_mtxa }, // G_MTXA		Matrix Address Register				F02108		WO
   { 0xF0210C, 0xF0210F, MM_IO_W, &g_end }, // G_END		Data Organization Register			F0210C		WO
   { 0xF02110, 0xF02113, MM_IO,   &g_pc }, // G_PC		GPU Program Counter					F02110		RW
   { 0xF02114, 0xF02117, MM_IO,   &g_ctrl }, // G_CTRL		GPU Control/Status Register			F02114		RW
   { 0xF02118, 0xF0211B, MM_IO,   &g_hidata }, // G_HIDATA	High Data Register					F02118		RW
   { 0xF0211C, 0xF0211F, MM_IO,   &g_remain, &g_divctrl }, // G_REMAIN	Divide Unit Remainder				F0211C		RO
   // G_DIVCTRL	Divide Unit Control					F0211C		WO
   { 0xF03000, 0xF03FFF, MM_RAM,  &gpuRAM },

   // BLITTER REGISTERS

   { 0xF02200, 0xF02203, MM_IO_W, &a1_base }, // A1_BASE		A1 Base Register					F02200		WO
   { 0xF02204, 0xF02207, MM_IO,   &a1_pixel, &a1_flags }, // A1_FLAGS	Flags Register						F02204		WO
   { 0xF02208, 0xF0220B, MM_IO_W, &a1_clip }, // A1_CLIP		A1 Clipping Size					F02208		WO
   { 0xF0220C, 0xF0220F, MM_IO_W, &a1_pixel }, // A1_PIXEL	A1 Pixel Pointer					F0220C		WO
   //												F02204		RO
   { 0xF02210, 0xF02213, MM_IO_W, &a1_step }, // A1_STEP		A1 Step Value						F02210		WO
   { 0xF02214, 0xF02217, MM_IO_W, &a1_fstep }, // A1_FSTEP	A1 Step Fraction Value				F02214		WO
   { 0xF02218, 0xF0221B, MM_IO,   &a1_fpixel }, // A1_FPIXEL	A1 Pixel Pointer Fraction			F02218		RW
   { 0xF0221C, 0xF0221F, MM_IO_W, &a1_inc }, // A1_INC		A1 Increment						F0221C		WO
   { 0xF02220, 0xF02223, MM_IO_W, &a1_finc }, // A1_FINC		A1 Increment Fraction				F02220		WO
   { 0xF02224, 0xF02227, MM_IO_W, &a2_base }, // A2_BASE		A2 Base Register					F02224		WO
   { 0xF02228, 0xF0222B, MM_IO_W, &a2_flags }, // A2_FLAGS	A2 Flags Register					F02228		WO
   { 0xF0222C, 0xF0222F, MM_IO,   &a2_pixel, &a2_mask }, // A2_MASK		A2 Window Mask						F0222C		WO
   { 0xF02230, 0xF02233, MM_IO_W, &a2_pixel }, // A2_PIXEL	A2 Pixel Pointer					F02230		WO
   //												F0222C		RO
   { 0xF02234, 0xF02237, MM_IO_W, &a2_step }, // A2_STEP		A2 Step Value						F02234		WO
   { 0xF02238, 0xF0223B, MM_IO,   &b_cmd }, // B_CMD		Command/Status Register				F02238		RW
   { 0xF0223C, 0xF0223F, MM_IO_W, &b_count }, // B_COUNT		Counters Register					F0223C		WO
   { 0xF02240, 0xF02247, MM_IO_W, &b_srcd }, // B_SRCD		Source Data Register				F02240		WO
   { 0xF02248, 0xF0224F, MM_IO_W, &b_dstd }, // B_DSTD		Destination Data Register			F02248		WO
   { 0xF02250, 0xF02258, MM_IO_W, &b_dstz }, // B_DSTZ		Destination Z Register				F02250		WO
   { 0xF02258, 0xF0225F, MM_IO_W, &b_srcz1 }, // B_SRCZ1		Source Z Register 1					F02258		WO
   { 0xF02260, 0xF02267, MM_IO_W, &b_srcz2 }, // B_SRCZ2		Source Z Register 2					F02260		WO
   { 0xF02268, 0xF0226F, MM_IO_W, &b_patd }, // B_PATD		Pattern Data Register				F02268		WO
   { 0xF02270, 0xF02273, MM_IO_W, &b_iinc }, // B_IINC		Intensity Increment					F02270		WO
   { 0xF02274, 0xF02277, MM_IO_W, &b_zinc }, // B_ZINC		Z Increment							F02274		WO
   { 0xF02278, 0xF0227B, MM_IO_W, &b_stop }, // B_STOP		Collision Control					F02278		WO
   { 0xF0227C, 0xF0227F, MM_IO_W, &b_i3 }, // B_I3		Intensity 3							F0227C		WO
   { 0xF02280, 0xF02283, MM_IO_W, &b_i2 }, // B_I2		Intensity 2							F02280		WO
   { 0xF02284, 0xF02287, MM_IO_W, &b_i1 }, // B_I1		Intensity 1							F02284		WO
   { 0xF02288, 0xF0228B, MM_IO_W, &b_i0 }, // B_I0		Intensity 0							F02288		WO
   { 0xF0228C, 0xF0228F, MM_IO_W, &b_z3 }, // B_Z3		Z 3									F0228C		WO
   { 0xF02290, 0xF02293, MM_IO_W, &b_z2 }, // B_Z2		Z 2									F02290		WO
   { 0xF02294, 0xF02297, MM_IO_W, &b_z1 }, // B_Z1		Z 1									F02294		WO
   { 0xF02298, 0xF0229B, MM_IO_W, &b_z0 }, // B_Z0		Z 0									F02298		WO

   // JTRM sez ALL GPU address space is accessible from $8000 offset as "fast" 32-bit WO access
   // Dunno if anything actually USED it tho... :-P
   { 0xF0A100, 0xF0A103, MM_IO_W, &g_flags }, // G_FLAGS		GPU Flags Register					F02100		RW
   { 0xF0A104, 0xF0A107, MM_IO_W, &g_mtxc }, // G_MTXC		Matrix Control Register				F02104		WO
   { 0xF0A108, 0xF0A10B, MM_IO_W, &g_mtxa }, // G_MTXA		Matrix Address Register				F02108		WO
   { 0xF0A10C, 0xF0A10F, MM_IO_W, &g_end }, // G_END		Data Organization Register			F0210C		WO
   { 0xF0A110, 0xF0A113, MM_IO_W, &g_pc }, // G_PC		GPU Program Counter					F02110		RW
   { 0xF0A114, 0xF0A117, MM_IO_W, &g_ctrl }, // G_CTRL		GPU Control/Status Register			F02114		RW
   { 0xF0A118, 0xF0A11B, MM_IO_W, &g_hidata }, // G_HIDATA	High Data Register					F02118		RW
   { 0xF0A11C, 0xF0A11F, MM_IO_W, &g_divctrl }, // G_REMAIN	Divide Unit Remainder				F0211C		RO
   { 0xF0B000, 0xF0BFFF, MM_IO_W, &gpuRAM }, // "Fast" interface to GPU RAM

   // JERRY REGISTERS

   { 0xF10000, 0xF10001, MM_IO_W, &jpit1 }, // JPIT1		Timer 1 Pre-scaler					F10000		WO
   { 0xF10002, 0xF10003, MM_IO_W, &jpit2 }, // JPIT2		Timer 1 Divider						F10002		WO
   { 0xF10004, 0xF10005, MM_IO_W, &jpit3 }, // JPIT3		Timer 2 Pre-scaler					F10004		WO
   { 0xF10006, 0xF10007, MM_IO_W, &jpit4 }, // JPIT4		Timer 2 Divider						F10006		WO
   { 0xF10010, 0xF10011, MM_IO_W, &clk1 }, // *CLK1		Processor Clock Divider				F10010		WO
   { 0xF10012, 0xF10013, MM_IO_W, &clk2 }, // *CLK2		Video Clock Divider					F10012		WO
   { 0xF10014, 0xF10015, MM_IO_W, &clk3 }, // *CLK3		Chroma Clock Divider				F10014		WO
   { 0xF10020, 0xF10021, MM_IO,   &j_int }, // J_INT		Interrup Control Register			F10020		RW
   { 0xF10030, 0xF10031, MM_IO,   &asidata }, // ASIDATA		Asynchronous Serial Data			F10030		RW
   { 0xF10032, 0xF10033, MM_IO,   &asistat, &asictrl }, // ASICTRL		Asynchronous Serial Control			F10032		WO
   // ASISTAT		Asynchronous Serial Status			F10032		RO
   { 0xF10034, 0xF10035, MM_IO,   &asiclk }, // ASICLK		Asynchronous Serial Interface Clock	F10034		RW
   { 0xF10036, 0xF10037, MM_IO_R, &jpit1 }, // JPIT1		Timer 1 Pre-scaler					F10036		RO
   { 0xF10038, 0xF10039, MM_IO_R, &jpit2 }, // JPIT2		Timer 1 Divider						F10038		RO
   { 0xF1003A, 0xF1003B, MM_IO_R, &jpit3 }, // JPIT3		Timer 2 Pre-scaler					F1003A		RO
   { 0xF1003C, 0xF1003D, MM_IO_R, &jpit4 }, // JPIT4		Timer 2 Divider						F1003C		RO

   { 0xF14000, 0xF14001, MM_IO,   &joystick }, // JOYSTICK	Joystick Register					F14000		RW
   { 0xF14002, 0xF14003, MM_IO,   &joybuts }, // JOYBUTS		Button Register						F14002		RW

   // DSP REGISTERS

   { 0xF1A100, 0xF1A103, MM_IO,   &d_flags }, // D_FLAGS		DSP Flags Register					F1A100		RW
   { 0xF1A104, 0xF1A107, MM_IO_W, &d_mtxc }, // D_MTXC		DSP Matrix Control Register			F1A104		WO
   { 0xF1A108, 0xF1A10B, MM_IO_W, &d_mtxa }, // D_MTXA		DSP Matrix Address Register			F1A108		WO
   { 0xF1A10C, 0xF1A10F, MM_IO_W, &d_end }, // D_END		DSP Data Organization Register		F1A10C		WO
   { 0xF1A110, 0xF1A113, MM_IO,   &d_pc }, // D_PC		DSP Program Counter					F1A110		RW
   { 0xF1A114, 0xF1A117, MM_IO,   &d_ctrl }, // D_CTRL		DSP Control/Status Register			F1A114		RW
   { 0xF1A118, 0xF1A11B, MM_IO_W, &d_mod }, // D_MOD		Modulo Instruction Mask				F1A118		WO
   { 0xF1A11C, 0xF1A11F, MM_IO_W, &d_remain, &d_divctrl }, // D_REMAIN	Divide Unit Remainder				F1A11C		RO
   // D_DIVCTRL	Divide Unit Control					F1A11C		WO
   { 0xF1A120, 0xF1A123, MM_IO_R, &d_machi }, // D_MACHI		MAC High Result Bits				F1A120		RO


   { 0xF1A148, 0xF1A149, MM_IO,   &lrxd, &ltxd }, // LTXD		Left Transmit Data					F1A148		WO
   // LRXD		Left Receive Data					F1A148		RO
   // L_I2S		Left I2S Serial Interface			F1A148		RW
   { 0xF1A14C, 0xF1A14D, MM_IO,   &rrxd, &rtxd }, // RTXD		Right Transmit Data					F1A14C		WO
   // RRXD		Right Receive Data					F1A14C		RO
   // R_I2S		Right I2S Serial Interface			F1A14C		RW
   { 0xF1A150, 0xF1A150, MM_IO,   &sstat, &sclk }, // SCLK		Serial Clock Frequency				F1A150		WO
   // SSTAT		Serial Status						F1A150		RO
   { 0xF1A154, 0xF1A157, MM_IO_W, &smode }, // SMODE		Serial Mode							F1A154		WO

   { 0xF1B000, 0xF1CFFF, MM_RAM,  &dspRAM }, // F1B000-F1CFFF   R/W   xxxxxxxx xxxxxxxx   Local DSP RAM
   { 0xF1D000, 0xF1DFFF, MM_ROM,  waveTableROM },
   // hi-speed interface for DSP??? Ain't no such thang...
   { 0xFFFFFF, 0xFFFFFF, MM_NOP } // End of memory address sentinel
};

void MMUWrite8(uint32_t address, uint8_t data, uint32_t who/*= UNKNOWN*/)
{
}

void MMUWrite16(uint32_t address, uint16_t data, uint32_t who/*= UNKNOWN*/)
{
}

void MMUWrite32(uint32_t address, uint32_t data, uint32_t who/*= UNKNOWN*/)
{
}

void MMUWrite64(uint32_t address, uint64_t data, uint32_t who/*= UNKNOWN*/)
{
}

uint8_t MMURead8(uint32_t address, uint32_t who/*= UNKNOWN*/)
{
   // Search for address in the memory map
   // NOTE: This assumes that all entries are linear and sorted in ascending order!

   struct MemDesc memory;
   uint8_t byte = 0xFE;

   uint32_t i = 0;
   while (true)
   {
      if (address <= memoryMap[i].endAddr)
      {
         if (address >= memoryMap[i].startAddr)
         {
            memory = memoryMap[i];
            break;
         }
         return 0xFF;	// Wasn't found...
      }

      i++;

      if (memoryMap[i].startAddr == 0xFFFFFF)
         return 0xFF;		// Exhausted the list, so bail!
   }

   uint32_t offset = address - memory.startAddr;
   uint32_t size = memory.endAddr - memory.startAddr + 1;
   uint8_t byteShift[8] = { 0, 8, 16, 24, 32, 40, 48, 56 };

   if (memory.type == MM_RAM || memory.type == MM_ROM)
      byte = ((uint8_t *)memory.readFunc)[offset];
   else if (memory.type == MM_IO_R || memory.type == MM_IO)
   {
#define FUNC_CAST(retVal, function, params) (*(retVal(*)(params))function)
      uint64_t retVal = FUNC_CAST(uint64_t, memory.readFunc, uint32_t)(offset);
      byte = (retVal >> byteShift[offset]) & 0xFF;
   }
   else if (memory.type == MM_IO_W)
      byte = 0xFF;		// Write only, what do we return? A fixed value?

   return byte;
}

uint16_t MMURead16(uint32_t address, uint32_t who)
{
   return 0;
}

uint32_t MMURead32(uint32_t address, uint32_t who)
{
   return 0;
}

uint64_t MMURead64(uint32_t address, uint32_t who)
{
   return 0;
}

