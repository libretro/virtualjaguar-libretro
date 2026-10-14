/* Thin wrapper over the vendored Musashi disassembler: raw-buffer mode only.
 * Compiled as C by the app (not part of the C89 core build). */
#include <string.h>

#include "m68k.h"
#include "vj_m68kdasm.h"

/* Required by m68kdasm.c's non-raw path; unused here (m68k_disassemble_raw reads the buffer). */
unsigned int m68k_read_disassembler_8(unsigned int address)  { (void)address; return 0; }
unsigned int m68k_read_disassembler_16(unsigned int address) { (void)address; return 0; }
unsigned int m68k_read_disassembler_32(unsigned int address) { (void)address; return 0; }

unsigned vj_m68k_disasm(const unsigned char* bytes, unsigned pc, char* out, int max)
{
	char tmp[256];
	unsigned len;

	if (!out || max <= 0)
		return 2;
	tmp[0] = '\0';
	len = m68k_disassemble_raw(tmp, pc & 0x00FFFFFFu, bytes, bytes, M68K_CPU_TYPE_68000);
	if (len < 2 || len > 10)
		len = 2;
	strncpy(out, tmp, (size_t)max - 1);
	out[max - 1] = '\0';
	return len;
}
