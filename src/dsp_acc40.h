/*
 * Jaguar DSP MAC accumulator is 40-bit signed two's complement.
 * Store only bits 39..0 in the low bits of uint64_t (bits 63..40 must stay zero)
 * so RESMAC and control-register reads (dsp_acc >> 32) stay correct.
 */

#ifndef DSP_ACC40_H
#define DSP_ACC40_H

#include <stdint.h>

#ifndef DSP_ACC40_INLINE
#if defined(_MSC_VER)
#define DSP_ACC40_INLINE static __inline
#else
#define DSP_ACC40_INLINE static inline
#endif
#endif

#define DSP_ACC_U40_MASK UINT64_C(0xFFFFFFFFFF)

DSP_ACC40_INLINE int64_t dsp_acc_i40_signed(uint64_t raw)
{
	uint64_t u = raw & DSP_ACC_U40_MASK;
	if (u & (UINT64_C(1) << 39))
		return (int64_t)(u | UINT64_C(0xFFFFFF0000000000));
	return (int64_t)u;
}

DSP_ACC40_INLINE uint64_t dsp_acc_wrap_store_i40(int64_t v)
{
	int64_t t;

	t = v & (((int64_t)1 << 40) - 1);
	if (t & ((int64_t)1 << 39))
		t |= ~(((int64_t)1 << 40) - 1);
	return (uint64_t)t & DSP_ACC_U40_MASK;
}

DSP_ACC40_INLINE void dsp_acc_mac_apply(uint64_t *acc, int32_t prod)
{
	int64_t a;

	a = dsp_acc_i40_signed(*acc);
	*acc = dsp_acc_wrap_store_i40(a + (int64_t)prod);
}

DSP_ACC40_INLINE void dsp_acc_set_from_i32(uint64_t *acc, int32_t res)
{
	*acc = dsp_acc_wrap_store_i40((int64_t)res);
}

#endif
