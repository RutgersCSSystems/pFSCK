#include "config.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define ARCH_HAVE_SSE4_2

/*
 * Based on a posting to lkml by Austin Zhang <austin.zhang@intel.com>
 *
 * Using hardware provided CRC32 instruction to accelerate the CRC32 disposal.
 * CRC32C polynomial:0x1EDC6F41(BE)/0x82F63B78(LE)
 * CRC32 is a new instruction in Intel SSE4.2, the reference can be found at:
 * http://www.intel.com/products/processor/manuals/
 * Intel(R) 64 and IA-32 Architectures Software Developer's Manual
 * Volume 2A: Instruction Set Reference, A-M
 */

int crc32c_intel_available = 0;

#ifdef ARCH_HAVE_SSE4_2
#if BITS_PER_LONG == 64
#define REX_PRE "0x48, "
#define SCALE_F 8
#else
#define REX_PRE
#define SCALE_F 4
#endif

static int crc32c_probed;

static uint32_t crc32c_intel_le_hw_byte(uint32_t crc, unsigned char const *data,
					unsigned long length)
{
	while (length--) {
		__asm__ __volatile__(
			".byte 0xf2, 0xf, 0x38, 0xf0, 0xf1"
			:"=S"(crc)
			:"0"(crc), "c"(*data)
		);
		data++;
	}

	return crc;
}

/*
 * Steps through buffer one byte at at time, calculates reflected 
 * crc using table.
 */
uint32_t crc32c_intel(uint32_t crc_in, unsigned char const *data, unsigned long length)
{
	unsigned int iquotient = length / SCALE_F;
	unsigned int iremainder = length % SCALE_F;


#if BITS_PER_LONG == 64
	uint64_t *ptmp = (uint64_t *) data;
#else
	uint32_t *ptmp = (uint32_t *) data;
#endif
	uint32_t crc = crc_in;

	while (iquotient--) {
		__asm__ __volatile__(
			".byte 0xf2, " REX_PRE "0xf, 0x38, 0xf1, 0xf1;"
			:"=S"(crc)
			:"0"(crc), "c"(*ptmp)
		);
		ptmp++;
	}

	if (iremainder)
		crc = crc32c_intel_le_hw_byte(crc, (unsigned char *)ptmp,
				 iremainder);

	return crc;
}

static inline void do_cpuid(unsigned int *eax, unsigned int *ebx,
			    unsigned int *ecx, unsigned int *edx)
{
	asm volatile("xchgl %%ebx, %1\ncpuid\nxchgl %%ebx, %1"
		: "=a" (*eax), "=r" (*ebx), "=c" (*ecx), "=d" (*edx)
		: "0" (*eax)
		: "memory");
}

int crc32c_intel_probe(void)
{
	if (!crc32c_probed) {
		unsigned int eax, ebx, ecx = 0, edx;

		eax = 1;

		do_cpuid(&eax, &ebx, &ecx, &edx);
		crc32c_intel_available = (ecx & (1 << 20)) != 0;
		crc32c_probed = 1;
	}
	return crc32c_intel_available;
}

#endif /* ARCH_HAVE_SSE4_2 */
