/*
 * GPU/DSP DIV unit (TOM and JERRY share one design).
 *
 * jag_sim netlists/tom/DIVIDE.NET and netlists/jerry/DIVIDE.NET (logic
 * identical; only the module name differs): an unsigned non-restoring
 * divider, two quotient bits per tick for 16 ticks.  The working value
 * is the 32-bit `dividend_hi` register plus the `last_neg` sign flop, so
 * every add/subtract is 33 bits wide ("The additions / subtractions
 * actually have to be performed at 33 bits of precision to correctly
 * determine the sign, as the arguments are 32-bit unsigned values",
 * DIVIDE.NET header; Adder1_33/Adder2_33 and Lnegt, lines 100-166).
 * Step: shift the working value left one, shifting in the dividend's
 * top bit (Dend1/Dend2), add the divisor if the last result was
 * negative, else subtract it (Divr1/Divr2 = divisor XOR last_neg\),
 * and shift the inverted 33rd bit into the quotient (Quosh/Quoti).
 * div_offset (DIVCTRL bit 0) preloads the working value with the top 16
 * dividend bits and the dividend with the low 16 bits shifted up
 * (Divhit/Divein, lines 176-215).  The remainder register (D_REMAIN /
 * G_REMAIN, Remrd) reads the low 32 bits of the final working value: the
 * remainder, or the remainder minus the divisor when negative.
 *
 * The result is exact for every divisor, including those with bit 31
 * set, where a 32-bit working value takes the sign from the wrong bit.
 * A zero divisor is not special-cased in the hardware: every step
 * subtracts 0, so an integer divide by zero returns $FFFFFFFF with the
 * dividend in the remainder.
 */

#ifndef RISC_DIV_H
#define RISC_DIV_H

#include <stdint.h>

#ifndef RISC_DIV_INLINE
#if defined(_MSC_VER)
#define RISC_DIV_INLINE static __inline
#else
#define RISC_DIV_INLINE static inline
#endif
#endif

#define RISC_DIV_MASK33 UINT64_C(0x1FFFFFFFF)

RISC_DIV_INLINE uint32_t risc_divide(uint32_t dividend, uint32_t divisor,
                                     int offset, uint32_t *remain)
{
	uint64_t wk, sub;
	uint32_t q;
	uint32_t neg;
	unsigned i;

	wk  = offset ? (uint64_t)(dividend >> 16) : 0;
	q   = offset ? (dividend << 16) : dividend;
	sub = (UINT64_C(1) << 33) - (uint64_t)divisor;   /* -divisor mod 2^33 */
	neg = 0;
	for (i = 0; i < 32; i++)
	{
		wk  = ((wk << 1) | (q >> 31)) & RISC_DIV_MASK33;
		q <<= 1;
		wk  = (wk + (neg ? (uint64_t)divisor : sub)) & RISC_DIV_MASK33;
		neg = (uint32_t)(wk >> 32) & 1;
		q  |= neg ^ 1;
	}
	*remain = (uint32_t)wk;
	return q;
}

#endif /* RISC_DIV_H */
