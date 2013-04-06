//
// Jaguar memory and I/O physical (hosted!) memory
//
// by James Hammons
//
// JLH = James Hammons
//
// WHO  WHEN        WHAT
// ---  ----------  -----------------------------------------------------------
// JLH  12/10/2009  Repurposed this file. :-)
//

/*
$FFFFFF => 16,777,215
$A00000 => 10,485,760

Really, just six megabytes short of using the entire address space...
Why not? We could just allocate the entire space and then use the MMU code to do
things like call functions and whatnot...
In other words, read/write would just tuck the value into the host RAM space and
the I/O function would take care of any weird stuff...

Actually: writes would tuck in the value, but reads would have to be handled
correctly since some registers do not fall on the same address as far as reading
goes... Still completely doable though. :-)
*/

#include "memory.h"

uint8 jagMemSpace[0xF20000];					// The entire memory space of the Jaguar...!

uint8 * jaguarMainRAM = &jagMemSpace[0x000000];
uint8 * jaguarMainROM = &jagMemSpace[0x800000];
uint8 * cdRAM         = &jagMemSpace[0xDFFF00];
uint8 * gpuRAM        = &jagMemSpace[0xF03000];
uint8 * dspRAM        = &jagMemSpace[0xF1B000];

//uint8 jaguarBootROM[0x040000];					// 68K CPU BIOS ROM--uses only half of this!
//uint8 jaguarCDBootROM[0x040000];				// 68K CPU CD BIOS ROM (256K)
//uint8 jaguarDevBootROM1[0x040000];				// 68K CPU Stubulator 1 ROM--uses only half of this!
//uint8 jaguarDevBootROM2[0x040000];				// 68K CPU Stubulator 2 ROM--uses only half of this!
//uint8 jaguarDevCDBootROM[0x040000];				// 68K CPU Dev CD BIOS ROM (256K)

// This is an ORed value showing which BIOSes having been loaded into memory.
//int biosAvailable = 0;

#if 0
union Word
{
	uint16 word;
	struct {
		// This changes depending on endianness...
#ifdef __BIG_ENDIAN__
		uint8 hi, lo;							// Big endian
#else
		uint8 lo, hi;							// Little endian
#endif
	};
};
#endif

#if 0
union DWord
{
	uint32 dword;
	struct
	{
#ifdef __BIG_ENDIAN__
		uint16 hiw, low;
#else
		uint16 low, hiw;
#endif
	};
};
#endif

#if 0
static void test(void)
{
	Word reg;
	reg.word = 0x1234;
	reg.lo = 0xFF;
	reg.hi = 0xEE;

	DWord reg2;
	reg2.hiw = 0xFFFE;
	reg2.low = 0x3322;
	reg2.low.lo = 0x11;
}
#endif

// OR, we could do like so:
#if 0
#ifdef __BIG_ENDIAN__
#define DWORD_BYTE_HWORD_H 1
#define DWORD_BYTE_HWORD_L 2
#define DWORD_BYTE_LWORD_H 3
#define DWORD_BYTE_LWORD_L 4
#else
#define DWORD_BYTE_HWORD_H 4
#define DWORD_BYTE_HWORD_L 3
#define DWORD_BYTE_LWORD_H 2
#define DWORD_BYTE_LWORD_L 1
#endif
// But this starts to get cumbersome after a while... Is union really better?

//More union stuff...
unsigned long ByteSwap1 (unsigned long nLongNumber)
{
   union u {unsigned long vi; unsigned char c[sizeof(unsigned long)];};
   union v {unsigned long ni; unsigned char d[sizeof(unsigned long)];};
   union u un;
   union v vn;
   un.vi = nLongNumber;
   vn.d[0]=un.c[3];
   vn.d[1]=un.c[2];
   vn.d[2]=un.c[1];
   vn.d[3]=un.c[0];
   return (vn.ni);
}
#endif

//Not sure if this is a good approach yet...
//should be if we use proper aliasing, and htonl and friends...
#if 1
uint32 & butch     = *((uint32 *)&jagMemSpace[0xDFFF00]);	// base of Butch == interrupt control register, R/W
uint32 & dscntrl   = *((uint32 *)&jagMemSpace[0xDFFF04]);	// DSA control register, R/W
uint16 & ds_data   = *((uint16 *)&jagMemSpace[0xDFFF0A]);	// DSA TX/RX data, R/W
uint32 & i2cntrl   = *((uint32 *)&jagMemSpace[0xDFFF10]);	// i2s bus control register, R/W
uint32 & sbcntrl   = *((uint32 *)&jagMemSpace[0xDFFF14]);	// CD subcode control register, R/W
uint32 & subdata   = *((uint32 *)&jagMemSpace[0xDFFF18]);	// Subcode data register A
uint32 & subdatb   = *((uint32 *)&jagMemSpace[0xDFFF1C]);	// Subcode data register B
uint32 & sb_time   = *((uint32 *)&jagMemSpace[0xDFFF20]);	// Subcode time and compare enable (D24)
uint32 & fifo_data = *((uint32 *)&jagMemSpace[0xDFFF24]);	// i2s FIFO data
uint32 & i2sdat2   = *((uint32 *)&jagMemSpace[0xDFFF28]);	// i2s FIFO data (old)
uint32 & unknown   = *((uint32 *)&jagMemSpace[0xDFFF2C]);	// Seems to be some sort of I2S interface
#else
uint32 butch, dscntrl, ds_data, i2cntrl, sbcntrl, subdata, subdatb, sb_time, fifo_data, i2sdat2, unknown;
#endif

#warning "Need to separate out this stuff (or do we???)"
//if we use a contiguous memory space, we don't need this shit...
//err, maybe we do, let's not be so hasty now... :-)

//#define ENDIANSAFE(x) htonl(x)

// The nice thing about doing it this way is that on big endian machines, htons/l
// compile to nothing and on Intel machines, it compiles down to a single bswap instruction.
// So endianness issues go away nicely without a lot of drama. :-D

#define BSWAP16(x) (htons(x))
#define BSWAP32(x) (htonl(x))
//this isn't endian safe...
#define BSWAP64(x) ((htonl(x & 0xFFFFFFFF) << 32) | htonl(x >> 32))
// Actually, we use ESAFExx() macros instead of this, and we use GCC to check the endianness...
// Actually, considering that "byteswap.h" doesn't exist elsewhere, the above
// is probably our best bet here. Just need to rename them to ESAFExx().

// Look at <endian.h> and see if that header is portable or not.

uint16 & memcon1   = *((uint16 *)&jagMemSpace[0xF00000]);
uint16 & memcon2   = *((uint16 *)&jagMemSpace[0xF00002]);
uint16 & hc        = *((uint16 *)&jagMemSpace[0xF00004]);
uint16 & vc        = *((uint16 *)&jagMemSpace[0xF00006]);
uint16 & lph       = *((uint16 *)&jagMemSpace[0xF00008]);
uint16 & lpv       = *((uint16 *)&jagMemSpace[0xF0000A]);
uint64 & obData    = *((uint64 *)&jagMemSpace[0xF00010]);
uint32 & olp       = *((uint32 *)&jagMemSpace[0xF00020]);
uint16 & obf       = *((uint16 *)&jagMemSpace[0xF00026]);
uint16 & vmode     = *((uint16 *)&jagMemSpace[0xF00028]);
uint16 & bord1     = *((uint16 *)&jagMemSpace[0xF0002A]);
uint16 & bord2     = *((uint16 *)&jagMemSpace[0xF0002C]);
uint16 & hp        = *((uint16 *)&jagMemSpace[0xF0002E]);
uint16 & hbb       = *((uint16 *)&jagMemSpace[0xF00030]);
uint16 & hbe       = *((uint16 *)&jagMemSpace[0xF00032]);
uint16 & hs        = *((uint16 *)&jagMemSpace[0xF00034]);
uint16 & hvs       = *((uint16 *)&jagMemSpace[0xF00036]);
uint16 & hdb1      = *((uint16 *)&jagMemSpace[0xF00038]);
uint16 & hdb2      = *((uint16 *)&jagMemSpace[0xF0003A]);
uint16 & hde       = *((uint16 *)&jagMemSpace[0xF0003C]);
uint16 & vp        = *((uint16 *)&jagMemSpace[0xF0003E]);
uint16 & vbb       = *((uint16 *)&jagMemSpace[0xF00040]);
uint16 & vbe       = *((uint16 *)&jagMemSpace[0xF00042]);
uint16 & vs        = *((uint16 *)&jagMemSpace[0xF00044]);
uint16 & vdb       = *((uint16 *)&jagMemSpace[0xF00046]);
uint16 & vde       = *((uint16 *)&jagMemSpace[0xF00048]);
uint16 & veb       = *((uint16 *)&jagMemSpace[0xF0004A]);
uint16 & vee       = *((uint16 *)&jagMemSpace[0xF0004C]);
uint16 & vi        = *((uint16 *)&jagMemSpace[0xF0004E]);
uint16 & pit0      = *((uint16 *)&jagMemSpace[0xF00050]);
uint16 & pit1      = *((uint16 *)&jagMemSpace[0xF00052]);
uint16 & heq       = *((uint16 *)&jagMemSpace[0xF00054]);
uint32 & bg        = *((uint32 *)&jagMemSpace[0xF00058]);
uint16 & int1      = *((uint16 *)&jagMemSpace[0xF000E0]);
uint16 & int2      = *((uint16 *)&jagMemSpace[0xF000E2]);
uint8  * clut      =   (uint8 *) &jagMemSpace[0xF00400];
uint8  * lbuf      =   (uint8 *) &jagMemSpace[0xF00800];
uint32 & g_flags   = *((uint32 *)&jagMemSpace[0xF02100]);
uint32 & g_mtxc    = *((uint32 *)&jagMemSpace[0xF02104]);
uint32 & g_mtxa    = *((uint32 *)&jagMemSpace[0xF02108]);
uint32 & g_end     = *((uint32 *)&jagMemSpace[0xF0210C]);
uint32 & g_pc      = *((uint32 *)&jagMemSpace[0xF02110]);
uint32 & g_ctrl    = *((uint32 *)&jagMemSpace[0xF02114]);
uint32 & g_hidata  = *((uint32 *)&jagMemSpace[0xF02118]);
uint32 & g_divctrl = *((uint32 *)&jagMemSpace[0xF0211C]);
uint32 g_remain;								// Dual register with $F0211C
uint32 & a1_base   = *((uint32 *)&jagMemSpace[0xF02200]);
uint32 & a1_flags  = *((uint32 *)&jagMemSpace[0xF02204]);
uint32 & a1_clip   = *((uint32 *)&jagMemSpace[0xF02208]);
uint32 & a1_pixel  = *((uint32 *)&jagMemSpace[0xF0220C]);
uint32 & a1_step   = *((uint32 *)&jagMemSpace[0xF02210]);
uint32 & a1_fstep  = *((uint32 *)&jagMemSpace[0xF02214]);
uint32 & a1_fpixel = *((uint32 *)&jagMemSpace[0xF02218]);
uint32 & a1_inc    = *((uint32 *)&jagMemSpace[0xF0221C]);
uint32 & a1_finc   = *((uint32 *)&jagMemSpace[0xF02220]);
uint32 & a2_base   = *((uint32 *)&jagMemSpace[0xF02224]);
uint32 & a2_flags  = *((uint32 *)&jagMemSpace[0xF02228]);
uint32 & a2_mask   = *((uint32 *)&jagMemSpace[0xF0222C]);
uint32 & a2_pixel  = *((uint32 *)&jagMemSpace[0xF02230]);
uint32 & a2_step   = *((uint32 *)&jagMemSpace[0xF02234]);
uint32 & b_cmd     = *((uint32 *)&jagMemSpace[0xF02238]);
uint32 & b_count   = *((uint32 *)&jagMemSpace[0xF0223C]);
uint64 & b_srcd    = *((uint64 *)&jagMemSpace[0xF02240]);
uint64 & b_dstd    = *((uint64 *)&jagMemSpace[0xF02248]);
uint64 & b_dstz    = *((uint64 *)&jagMemSpace[0xF02250]);
uint64 & b_srcz1   = *((uint64 *)&jagMemSpace[0xF02258]);
uint64 & b_srcz2   = *((uint64 *)&jagMemSpace[0xF02260]);
uint64 & b_patd    = *((uint64 *)&jagMemSpace[0xF02268]);
uint32 & b_iinc    = *((uint32 *)&jagMemSpace[0xF02270]);
uint32 & b_zinc    = *((uint32 *)&jagMemSpace[0xF02274]);
uint32 & b_stop    = *((uint32 *)&jagMemSpace[0xF02278]);
uint32 & b_i3      = *((uint32 *)&jagMemSpace[0xF0227C]);
uint32 & b_i2      = *((uint32 *)&jagMemSpace[0xF02280]);
uint32 & b_i1      = *((uint32 *)&jagMemSpace[0xF02284]);
uint32 & b_i0      = *((uint32 *)&jagMemSpace[0xF02288]);
uint32 & b_z3      = *((uint32 *)&jagMemSpace[0xF0228C]);
uint32 & b_z2      = *((uint32 *)&jagMemSpace[0xF02290]);
uint32 & b_z1      = *((uint32 *)&jagMemSpace[0xF02294]);
uint32 & b_z0      = *((uint32 *)&jagMemSpace[0xF02298]);
uint16 & jpit1     = *((uint16 *)&jagMemSpace[0xF10000]);
uint16 & jpit2     = *((uint16 *)&jagMemSpace[0xF10002]);
uint16 & jpit3     = *((uint16 *)&jagMemSpace[0xF10004]);
uint16 & jpit4     = *((uint16 *)&jagMemSpace[0xF10006]);
uint16 & clk1      = *((uint16 *)&jagMemSpace[0xF10010]);
uint16 & clk2      = *((uint16 *)&jagMemSpace[0xF10012]);
uint16 & clk3      = *((uint16 *)&jagMemSpace[0xF10014]);
uint16 & j_int     = *((uint16 *)&jagMemSpace[0xF10020]);
uint16 & asidata   = *((uint16 *)&jagMemSpace[0xF10030]);
uint16 & asictrl   = *((uint16 *)&jagMemSpace[0xF10032]);
uint16 asistat;									// Dual register with $F10032
uint16 & asiclk    = *((uint16 *)&jagMemSpace[0xF10034]);
uint16 & joystick  = *((uint16 *)&jagMemSpace[0xF14000]);
uint16 & joybuts   = *((uint16 *)&jagMemSpace[0xF14002]);
uint32 & d_flags   = *((uint32 *)&jagMemSpace[0xF1A100]);
uint32 & d_mtxc    = *((uint32 *)&jagMemSpace[0xF1A104]);
uint32 & d_mtxa    = *((uint32 *)&jagMemSpace[0xF1A108]);
uint32 & d_end     = *((uint32 *)&jagMemSpace[0xF1A10C]);
uint32 & d_pc      = *((uint32 *)&jagMemSpace[0xF1A110]);
uint32 & d_ctrl    = *((uint32 *)&jagMemSpace[0xF1A114]);
uint32 & d_mod     = *((uint32 *)&jagMemSpace[0xF1A118]);
uint32 & d_divctrl = *((uint32 *)&jagMemSpace[0xF1A11C]);
uint32 d_remain;								// Dual register with $F0211C
uint32 & d_machi   = *((uint32 *)&jagMemSpace[0xF1A120]);
uint16 & ltxd      = *((uint16 *)&jagMemSpace[0xF1A148]);
uint16 lrxd;									// Dual register with $F1A148
uint16 & rtxd      = *((uint16 *)&jagMemSpace[0xF1A14C]);
uint16 rrxd;									// Dual register with $F1A14C
uint8  & sclk      = *((uint8 *) &jagMemSpace[0xF1A150]);
uint8 sstat;									// Dual register with $F1A150
uint32 & smode     = *((uint32 *)&jagMemSpace[0xF1A154]);

// Memory debugging identifiers

const char * whoName[9] =
	{ "Unknown", "Jaguar", "DSP", "GPU", "TOM", "JERRY", "M68K", "Blitter", "OP" };

