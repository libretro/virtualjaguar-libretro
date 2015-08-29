//
// Inline functions used by cpuemu.c
//
// by Bernd Schmidt, Thomas Huth, and James Hammons
//
// Since inline functions have to be in a header, we have them all defined
// here, in one place to make finding them easy.
//

#ifndef __INLINES_H__
#define __INLINES_H__

#include "cpudefs.h"
#include "m68kinterface.h"

STATIC_INLINE int cctrue(const int cc)
{
	switch (cc)
	{
		case 0:  return 1;                       /* T */
		case 1:  return 0;                       /* F */
		case 2:  return !CFLG && !ZFLG;          /* HI */
		case 3:  return CFLG || ZFLG;            /* LS */
		case 4:  return !CFLG;                   /* CC */
		case 5:  return CFLG;                    /* CS */
		case 6:  return !ZFLG;                   /* NE */
		case 7:  return ZFLG;                    /* EQ */
		case 8:  return !VFLG;                   /* VC */
		case 9:  return VFLG;                    /* VS */
		case 10: return !NFLG;                   /* PL */
		case 11: return NFLG;                    /* MI */
		case 12: return NFLG == VFLG;            /* GE */
		case 13: return NFLG != VFLG;            /* LT */
		case 14: return !ZFLG && (NFLG == VFLG); /* GT */
		case 15: return ZFLG || (NFLG != VFLG);  /* LE */
	}

	abort();
	return 0;
}

//no #define m68k_incpc(o) (regs.pc_p += (o))
#define m68k_incpc(o) (regs.pc += (o))

STATIC_INLINE void m68k_setpc(uint32_t newpc)
{
	//This is only done here... (get_real_address())
//	regs.pc_p = regs.pc_oldp = get_real_address(newpc);
	regs.pc = newpc;
}

#define m68k_setpc_rte  m68k_setpc

STATIC_INLINE uint32_t m68k_getpc(void)
{
//	return regs.pc + ((char *)regs.pc_p - (char *)regs.pc_oldp);
	return regs.pc;
}

STATIC_INLINE void m68k_setstopped(int stop)
{
	regs.stopped = stop;
	regs.remainingCycles = 0;
}

STATIC_INLINE void m68k_do_rts(void)
{
	m68k_setpc(m68k_read_memory_32(m68k_areg(regs, 7)));
	m68k_areg(regs, 7) += 4;
}

STATIC_INLINE void m68k_do_bsr(uint32_t oldpc, int32_t offset)
{
	m68k_areg(regs, 7) -= 4;
	m68k_write_memory_32(m68k_areg(regs, 7), oldpc);
	m68k_incpc(offset);
}

STATIC_INLINE void m68k_do_jsr(uint32_t oldpc, uint32_t dest)
{
	m68k_areg(regs, 7) -= 4;
	m68k_write_memory_32(m68k_areg(regs, 7), oldpc);
	m68k_setpc(dest);
}

// For now, we'll punt this crap...
// (Also, notice that the byte read is at address + 1...)
#define get_ibyte(o)	m68k_read_memory_8(regs.pc + (o) + 1)
#define get_iword(o)	m68k_read_memory_16(regs.pc + (o))
#define get_ilong(o)	m68k_read_memory_32(regs.pc + (o))

// We don't use this crap, so let's comment out for now...
STATIC_INLINE void refill_prefetch(uint32_t currpc, uint32_t offs)
{
}

STATIC_INLINE uint32_t get_ibyte_prefetch(int32_t o)
{
	return get_ibyte(o);
}

STATIC_INLINE uint32_t get_iword_prefetch(int32_t o)
{
	return get_iword(o);
}

STATIC_INLINE uint32_t get_ilong_prefetch(int32_t o)
{
	return get_ilong(o);
}

STATIC_INLINE void fill_prefetch_0(void)
{
}

#define fill_prefetch_2 fill_prefetch_0

#endif	// __INLINES_H__
