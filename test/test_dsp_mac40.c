/*
 * Unit tests for src/dsp_acc40.h (Jaguar DSP 40-bit MAC semantics).
 * Build: cc -O2 -Wall -I../src -o test_dsp_mac40 test/test_dsp_mac40.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "dsp_acc40.h"

int main(void)
{
	uint64_t acc;
	int i;
	int failed = 0;

	/* Only 40 physical bits; high 24 must stay clear */
	acc = 0;
	for (i = 0; i < 50000; i++)
		dsp_acc_mac_apply(&acc, 7777);
	if (acc & ~(DSP_ACC_U40_MASK))
	{
		fprintf(stderr, "FAIL: bits above 39 set after MAC loop (acc=%llx)\n",
				(unsigned long long)acc);
		failed++;
	}

	/* RESMAC-style low 32 must be stable across wraps */
	acc = DSP_ACC_U40_MASK;
	dsp_acc_mac_apply(&acc, 1);
	if ((uint32_t)acc != 0)
	{
		fprintf(stderr, "FAIL: wrap +1 from 40-bit all-ones (got low32=%x)\n",
				(unsigned int)(uint32_t)acc);
		failed++;
	}

	/* Load int32 -1 into acc (sign-extended in 40-bit domain) */
	dsp_acc_set_from_i32(&acc, -1);
	if (acc != DSP_ACC_U40_MASK)
	{
		fprintf(stderr, "FAIL: set -1 (acc=%llx expected %llx)\n",
				(unsigned long long)acc, (unsigned long long)DSP_ACC_U40_MASK);
		failed++;
	}

	if (failed)
	{
		fprintf(stderr, "test_dsp_mac40: %d failure(s)\n", failed);
		return 1;
	}

	printf("test_dsp_mac40: OK\n");
	return 0;
}
