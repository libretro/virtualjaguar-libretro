//
// MEMORY.H: Header file
//
// All Jaguar related memory and I/O locations are contained in this file
//

#ifndef __MEMORY_H__
#define __MEMORY_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)
    typedef union Bits64 {
        uint64_t DATA;
        struct Bytes8 {
#ifdef LITTLE_ENDIAN
            uint8_t b0;
            uint8_t b1;
            uint8_t b2;
            uint8_t b3;
            uint8_t b4;
            uint8_t b5;
            uint8_t b6;
            uint8_t b7;
#else
            uint8_t b7;
            uint8_t b6;
            uint8_t b5;
            uint8_t b4;
            uint8_t b3;
            uint8_t b2;
            uint8_t b1;
            uint8_t b0;
#endif
        } bytes;
    } Bits64;
#pragma pack(pop)
typedef union Bits32 {
    uint32_t WORD;
    struct Words {
#ifdef LITTLE_ENDIAN
        uint16_t LWORD;
        uint16_t UWORD;
#else
        uint16_t UWORD;
        uint16_t LWORD;
#endif
    } words;
    struct Bits {
#ifdef LITTLE_ENDIAN
        unsigned int b0: 1;
        unsigned int b1: 1;
        unsigned int b2: 1;
        unsigned int b3: 1;
        unsigned int b4: 1;
        unsigned int b5: 1;
        unsigned int b6: 1;
        unsigned int b7: 1;
        unsigned int b8: 1;
        unsigned int b9: 1;
        unsigned int b10: 1;
        unsigned int b11: 1;
        unsigned int b12: 1;
        unsigned int b13: 1;
        unsigned int b14: 1;
        unsigned int b15: 1;
        unsigned int b16: 1;
        unsigned int b17: 1;
        unsigned int b18: 1;
        unsigned int b19: 1;
        unsigned int b20: 1;
        unsigned int b21: 1;
        unsigned int b22: 1;
        unsigned int b23: 1;
        unsigned int b24: 1;
        unsigned int b25: 1;
        unsigned int b26: 1;
        unsigned int b27: 1;
        unsigned int b28: 1;
        unsigned int b29: 1;
        unsigned int b30: 1;
        unsigned int b31: 1;
#else
        // reverse the order of the bit fields.
        unsigned int b31: 1;
        unsigned int b30: 1;
        unsigned int b29: 1;
        unsigned int b28: 1;
        unsigned int b27: 1;
        unsigned int b26: 1;
        unsigned int b25: 1;
        unsigned int b24: 1;
        unsigned int b23: 1;
        unsigned int b22: 1;
        unsigned int b21: 1;
        unsigned int b20: 1;
        unsigned int b19: 1;
        unsigned int b18: 1;
        unsigned int b17: 1;
        unsigned int b16: 1;
        unsigned int b15: 1;
        unsigned int b14: 1;
        unsigned int b13: 1;
        unsigned int b12: 1;
        unsigned int b11: 1;
        unsigned int b10: 1;
        unsigned int b9: 1;
        unsigned int b8: 1;
        unsigned int b7: 1;
        unsigned int b6: 1;
        unsigned int b5: 1;
        unsigned int b4: 1;
        unsigned int b3: 1;
        unsigned int b2: 1;
        unsigned int b1: 1;
        unsigned int b0: 1;
#endif
    } bits;
} Bits32;
    
typedef union GPUControl {
    uint32_t WORD;
    struct Words words;
    struct Bits bits;
    struct  __attribute__ ((__packed__)) {
#ifdef LITTLE_ENDIAN
        unsigned int : 6;
        unsigned int irqMask: 5;
        unsigned int : 21;
#else
        unsigned int : 21;
        unsigned int irqMask: 5;
        unsigned int : 6;
#endif
    } gpuIRQ;
    
} GPUControl;
    
#ifdef USE_STRUCTS
#pragma pack(push, 1)
    typedef union OpCode {
        uint16_t WORD;
        struct Bytes {
#ifdef LITTLE_ENDIAN
            uint8_t LBYTE;
            uint8_t UBYTE;
#else
            uint8_t UBYTE;
            uint8_t LBYTE;
#endif
        } Bytes;
        struct Codes {
#ifdef LITTLE_ENDIAN
            unsigned int second : 5;
            unsigned int first : 5;
            unsigned int index : 6;
#else
            unsigned int index : 6;
            unsigned int first : 5;
            unsigned int second : 5;
#endif
        } Codes;
    } OpCode;
#pragma pack(pop)
    
    typedef OpCode U16Union;
#endif //USE_STRUCTS

#ifdef USE_STRUCTS
typedef union Offset {
    uint32_t LONG;
#pragma pack(push, 1)
    struct Members {
#ifdef LITTLE_ENDIAN
        unsigned int offset : 31;
        unsigned int bit : 1;
#else
        unsigned int bit : 1;
        unsigned int offset : 31;
#endif
    } Members;
#pragma pack(pop)
} Offset;
#endif //USE_STRUCTS
    
typedef union DSPLong {
    uint32_t LONG;
    struct Data {
#ifdef LITTLE_ENDIAN
        uint16_t LWORD;
        uint16_t UWORD;
#else
        uint16_t UWORD;
        uint16_t LWORD;
#endif
    } Data;
} DSPLong;
    
extern uint8_t jagMemSpace[];

extern uint8_t * jaguarMainRAM;
extern uint8_t * jaguarMainROM;
extern uint8_t * gpuRAM;
extern uint8_t * dspRAM;

extern uint32_t * butch, * dscntrl;
extern uint16_t * ds_data;
extern uint32_t * i2cntrl, * sbcntrl, * subdata, * subdatb, * sb_time, * fifo_data, * i2sdat2, * unknown;

extern uint16_t * memcon1, * memcon2, * hc, * vc, * lph, * lpv;
extern uint64_t * obData;
extern uint32_t * olp;
extern uint16_t * obf, * vmode, * bord1, * bord2, * hp, * hbb, * hbe, * hs,
	* hvs, * hdb1, * hdb2, * hde, * vp, * vbb, * vbe, * vs, * vdb, * vde,
	* veb, * vee, * vi, * pit0, * pit1, * heq;
extern uint32_t * bg;
extern uint16_t * int1, * int2;
extern uint8_t * clut, * lbuf;
extern uint32_t * g_flags, * g_mtxc, * g_mtxa, * g_end, * g_pc, * g_ctrl,
	* g_hidata, * g_divctrl;
extern uint32_t g_remain;
extern uint32_t * a1_base, * a1_flags, * a1_clip, * a1_pixel, * a1_step,
	* a1_fstep, * a1_fpixel, * a1_inc, * a1_finc, * a2_base, * a2_flags,
	* a2_mask, * a2_pixel, * a2_step, * b_cmd, * b_count;
extern uint64_t * b_srcd, * b_dstd, * b_dstz, * b_srcz1, * b_srcz2, * b_patd;
extern uint32_t * b_iinc, * b_zinc, * b_stop, * b_i3, * b_i2, * b_i1, * b_i0, * b_z3,
	* b_z2, * b_z1, * b_z0;
extern uint16_t * jpit1, * jpit2, * jpit3, * jpit4, * clk1, * clk2, * clk3, * j_int,
	* asidata, * asictrl;
extern uint16_t asistat;
extern uint16_t * asiclk, * joystick, * joybuts;
extern uint32_t * d_flags, * d_mtxc, * d_mtxa, * d_end, * d_pc, * d_ctrl,
	* d_mod, * d_divctrl;
extern uint32_t d_remain;
extern uint32_t * d_machi;
extern uint16_t * ltxd, lrxd, * rtxd, rrxd;
extern uint8_t * sclk, sstat;
extern uint32_t * smode;

// Read/write tracing enumeration

enum { UNKNOWN, JAGUAR, DSP, GPU, TOM, JERRY, M68K, BLITTER, OP, DEBUG };
extern const char * whoName[10];

// BIOS identification enum

// Some handy macros to help converting native endian to big endian (jaguar native)
// & vice versa

#define SET64(r, a, v) 	r[(a)] = ((v) & 0xFF00000000000000) >> 56, r[(a)+1] = ((v) & 0x00FF000000000000) >> 48, \
						r[(a)+2] = ((v) & 0x0000FF0000000000) >> 40, r[(a)+3] = ((v) & 0x000000FF00000000) >> 32, \
						r[(a)+4] = ((v) & 0xFF000000) >> 24, r[(a)+5] = ((v) & 0x00FF0000) >> 16, \
						r[(a)+6] = ((v) & 0x0000FF00) >> 8, r[(a)+7] = (v) & 0x000000FF
#define GET64(r, a)		(((uint64_t)r[(a)] << 56) | ((uint64_t)r[(a)+1] << 48) | \
						((uint64_t)r[(a)+2] << 40) | ((uint64_t)r[(a)+3] << 32) | \
						((uint64_t)r[(a)+4] << 24) | ((uint64_t)r[(a)+5] << 16) | \
						((uint64_t)r[(a)+6] << 8) | (uint64_t)r[(a)+7])
#define SET32(r, a, v)	r[(a)] = ((v) & 0xFF000000) >> 24, r[(a)+1] = ((v) & 0x00FF0000) >> 16, \
						r[(a)+2] = ((v) & 0x0000FF00) >> 8, r[(a)+3] = (v) & 0x000000FF
#define GET32(r, a)		((r[(a)] << 24) | (r[(a)+1] << 16) | (r[(a)+2] << 8) | r[(a)+3])
#define SET16(r, a, v)	r[(a)] = ((v) & 0xFF00) >> 8, r[(a)+1] = (v) & 0xFF


//#ifdef USE_STRUCTS
//    INLINE static uint16_t GET16(uint8_t* r,uint32_t a) {
//        U16Union u16;
//        u16.Bytes.UBYTE = r[a];
//        u16.Bytes.LBYTE = r[a+1];
//        return u16.WORD;
//    }
//#else
    #define GET16(r, a)        ((r[(a)] << 8) | r[(a)+1])
//#endif

#ifdef __cplusplus
}
#endif

#endif	// __MEMORY_H__
