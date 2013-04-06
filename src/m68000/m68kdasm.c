//
// m68kdasm.c: 68000 instruction disassembly
//
// Originally part of the UAE 68000 cpu core
// by Bernd Schmidt
//
// Adapted to Virtual Jaguar by James Hammons
//
// This file is distributed under the GNU Public License, version 3 or at your
// option any later version. Read the file GPLv3 for details.
//

#include <string.h>
#include "cpudefs.h"
#include "cpuextra.h"
#include "inlines.h"
#include "readcpu.h"


// Stuff from m68kinterface.c
extern unsigned long IllegalOpcode(uint32_t opcode);
extern cpuop_func * cpuFunctionTable[65536];

// Local "global" variables
static long int m68kpc_offset;

#if 0
#define get_ibyte_1(o) get_byte(regs.pc + (regs.pc_p - regs.pc_oldp) + (o) + 1)
#define get_iword_1(o) get_word(regs.pc + (regs.pc_p - regs.pc_oldp) + (o))
#define get_ilong_1(o) get_long(regs.pc + (regs.pc_p - regs.pc_oldp) + (o))
#else
#define get_ibyte_1(o) m68k_read_memory_8(regs.pc + (o) + 1)
#define get_iword_1(o) m68k_read_memory_16(regs.pc + (o))
#define get_ilong_1(o) m68k_read_memory_32(regs.pc + (o))
#endif


int32_t ShowEA(FILE * f, int reg, amodes mode, wordsizes size, char * buf)
{
	uint16_t dp;
	int8_t disp8;
	int16_t disp16;
	int r;
	uint32_t dispreg;
	uint32_t addr;
	int32_t offset = 0;
	char buffer[80];

	switch (mode)
	{
	case Dreg:
		sprintf(buffer,"D%d", reg);
		break;
	case Areg:
		sprintf(buffer,"A%d", reg);
		break;
	case Aind:
		sprintf(buffer,"(A%d)", reg);
		break;
	case Aipi:
		sprintf(buffer,"(A%d)+", reg);
		break;
	case Apdi:
		sprintf(buffer,"-(A%d)", reg);
		break;
	case Ad16:
		disp16 = get_iword_1(m68kpc_offset); m68kpc_offset += 2;
		addr = m68k_areg(regs,reg) + (int16_t)disp16;
		sprintf(buffer,"(A%d,$%04X) == $%08lX", reg, disp16 & 0xFFFF,
			(unsigned long)addr);
		break;
	case Ad8r:
		dp = get_iword_1(m68kpc_offset); m68kpc_offset += 2;
		disp8 = dp & 0xFF;
		r = (dp & 0x7000) >> 12;
		dispreg = (dp & 0x8000 ? m68k_areg(regs,r) : m68k_dreg(regs,r));

		if (!(dp & 0x800))
			dispreg = (int32_t)(int16_t)(dispreg);

		dispreg <<= (dp >> 9) & 3;

		if (dp & 0x100)
		{
			int32_t outer = 0, disp = 0;
			int32_t base = m68k_areg(regs,reg);
			char name[10];
			sprintf (name,"A%d, ",reg);
			if (dp & 0x80) { base = 0; name[0] = 0; }
			if (dp & 0x40) dispreg = 0;
			if ((dp & 0x30) == 0x20) { disp = (int32_t)(int16_t)get_iword_1(m68kpc_offset); m68kpc_offset += 2; }
			if ((dp & 0x30) == 0x30) { disp = get_ilong_1(m68kpc_offset); m68kpc_offset += 4; }
			base += disp;

			if ((dp & 0x3) == 0x2) { outer = (int32_t)(int16_t)get_iword_1(m68kpc_offset); m68kpc_offset += 2; }
			if ((dp & 0x3) == 0x3) { outer = get_ilong_1(m68kpc_offset); m68kpc_offset += 4; }

			if (!(dp & 4)) base += dispreg;
			if (dp & 3) base = m68k_read_memory_32(base);
			if (dp & 4) base += dispreg;

			addr = base + outer;
			sprintf(buffer,"(%s%c%d.%c*%d+%ld)+%ld == $%08lX", name,
				dp & 0x8000 ? 'A' : 'D', (int)r, dp & 0x800 ? 'L' : 'W',
				1 << ((dp >> 9) & 3),
				(long)disp, (long)outer, (unsigned long)addr);
		}
		else
		{
			addr = m68k_areg(regs,reg) + (int32_t)((int8_t)disp8) + dispreg;
			sprintf (buffer,"(A%d, %c%d.%c*%d, $%02X) == $%08lX", reg,
				dp & 0x8000 ? 'A' : 'D', (int)r, dp & 0x800 ? 'L' : 'W',
				1 << ((dp >> 9) & 3), disp8, (unsigned long)addr);
		}
		break;
	case PC16:
		addr = m68k_getpc() + m68kpc_offset;
		disp16 = get_iword_1(m68kpc_offset); m68kpc_offset += 2;
		addr += (int16_t)disp16;
		sprintf(buffer,"(PC,$%04X) == $%08lX", disp16 & 0xffff,(unsigned long)addr);
		break;
	case PC8r:
		addr = m68k_getpc() + m68kpc_offset;
		dp = get_iword_1(m68kpc_offset); m68kpc_offset += 2;
		disp8 = dp & 0xFF;
		r = (dp & 0x7000) >> 12;
		dispreg = dp & 0x8000 ? m68k_areg(regs,r) : m68k_dreg(regs,r);

		if (!(dp & 0x800))
			dispreg = (int32_t)(int16_t)(dispreg);

		dispreg <<= (dp >> 9) & 3;

		if (dp & 0x100)
		{
			int32_t outer = 0,disp = 0;
			int32_t base = addr;
			char name[10];
			sprintf (name,"PC, ");
			if (dp & 0x80) { base = 0; name[0] = 0; }
			if (dp & 0x40) dispreg = 0;
			if ((dp & 0x30) == 0x20) { disp = (int32_t)(int16_t)get_iword_1(m68kpc_offset); m68kpc_offset += 2; }
			if ((dp & 0x30) == 0x30) { disp = get_ilong_1(m68kpc_offset); m68kpc_offset += 4; }
			base += disp;

			if ((dp & 0x3) == 0x2)
			{
				outer = (int32_t)(int16_t)get_iword_1(m68kpc_offset);
				m68kpc_offset += 2;
			}

			if ((dp & 0x3) == 0x3)
			{
				outer = get_ilong_1(m68kpc_offset);
				m68kpc_offset += 4;
			}

			if (!(dp & 4)) base += dispreg;
			if (dp & 3) base = m68k_read_memory_32(base);
			if (dp & 4) base += dispreg;

			addr = base + outer;
			sprintf(buffer,"(%s%c%d.%c*%d+%ld)+%ld == $%08lX", name,
				dp & 0x8000 ? 'A' : 'D', (int)r, dp & 0x800 ? 'L' : 'W',
				1 << ((dp >> 9) & 3), (long)disp, (long)outer, (unsigned long)addr);
		}
		else
		{
			addr += (int32_t)((int8_t)disp8) + dispreg;
			sprintf(buffer,"(PC, %c%d.%c*%d, $%02X) == $%08lX", dp & 0x8000 ? 'A' : 'D',
				(int)r, dp & 0x800 ? 'L' : 'W',  1 << ((dp >> 9) & 3),
				disp8, (unsigned long)addr);
		}
		break;
	case absw:
		sprintf(buffer,"$%08lX", (unsigned long)(int32_t)(int16_t)get_iword_1(m68kpc_offset));
		m68kpc_offset += 2;
		break;
	case absl:
		sprintf(buffer,"$%08lX", (unsigned long)get_ilong_1(m68kpc_offset));
		m68kpc_offset += 4;
		break;
	case imm:
		switch (size)
		{
		case sz_byte:
			sprintf(buffer,"#$%02X", (unsigned int)(get_iword_1(m68kpc_offset) & 0xFF));
			m68kpc_offset += 2;
			break;
		case sz_word:
			sprintf(buffer,"#$%04X", (unsigned int)(get_iword_1(m68kpc_offset) & 0xFFFF));
			m68kpc_offset += 2;
			break;
		case sz_long:
			sprintf(buffer,"#$%08lX", (unsigned long)(get_ilong_1(m68kpc_offset)));
			m68kpc_offset += 4;
			break;
		default:
			break;
		}
		break;
	case imm0:
		offset = (int32_t)(int8_t)get_iword_1(m68kpc_offset);
		m68kpc_offset += 2;
		sprintf (buffer,"#$%02X", (unsigned int)(offset & 0xFF));
		break;
	case imm1:
		offset = (int32_t)(int16_t)get_iword_1(m68kpc_offset);
		m68kpc_offset += 2;
		sprintf (buffer,"#$%04X", (unsigned int)(offset & 0xFFFF));
		break;
	case imm2:
		offset = (int32_t)get_ilong_1(m68kpc_offset);
		m68kpc_offset += 4;
		sprintf(buffer,"#$%08lX", (unsigned long)offset);
		break;
	case immi:
		offset = (int32_t)(int8_t)(reg & 0xFF);
		sprintf(buffer,"#$%08lX", (unsigned long)offset);
		break;
	default:
		break;
	}

	if (buf == 0)
		fprintf(f, "%s", buffer);
	else
		strcat(buf, buffer);

	return offset;
}


//void m68k_disasm(FILE * f, uint32_t addr, uint32_t * nextpc, int cnt)
unsigned int m68k_disasm(char * output, uint32_t addr, uint32_t * nextpc, int cnt)
{
	char f[256], str[256];
	static const char * const ccnames[] =
		{ "RA","RN","HI","LS","CC","CS","NE","EQ",
		  "VC","VS","PL","MI","GE","LT","GT","LE" };

	str[0] = 0;
	output[0] = 0;
	uint32_t newpc = 0;
	m68kpc_offset = addr - m68k_getpc();
	long int pcOffsetSave = m68kpc_offset;
	int opwords;

	while (cnt-- > 0)
	{
		char instrname[20], * ccpt;
		uint32_t opcode;
		const struct mnemolookup * lookup;
		struct instr * dp;
//		sprintf(f, "%06lX: ", m68k_getpc() + m68kpc_offset);
//		strcat(str, f);

#if 0
		for(opwords=0; opwords<5; opwords++)
		{
			sprintf(f, "%04X ", get_iword_1(m68kpc_offset + opwords * 2));
			strcat(str, f);
		}
#endif

		opcode = get_iword_1(m68kpc_offset);
		m68kpc_offset += 2;

		if (cpuFunctionTable[opcode] == IllegalOpcode)
		{
			opcode = 0x4AFC;
		}

		dp = table68k + opcode;

		for(lookup=lookuptab; lookup->mnemo!=dp->mnemo; lookup++)
			;

		strcpy(instrname, lookup->name);
		ccpt = strstr(instrname, "cc");

		if (ccpt != 0)
		{
			strncpy(ccpt, ccnames[dp->cc], 2);
		}

		sprintf(f, "%s", instrname);
		strcat(str, f);

#if 0
		switch (dp->size)
		{
			case sz_byte: sprintf(f, ".B "); break;
			case sz_word: sprintf(f, ".W "); break;
			case sz_long: sprintf(f, ".L "); break;
			default: sprintf(f, "   "); break;
		}
		strcat(str, f);
#else
		switch (dp->size)
		{
			case sz_byte: strcat(str, ".B\t"); break;
			case sz_word: strcat(str, ".W\t"); break;
			case sz_long: strcat(str, ".L\t"); break;
			default: strcat(str, "\t"); break;
		}
#endif

		if (dp->suse)
		{
			f[0] = 0;
			newpc = m68k_getpc() + m68kpc_offset;
			newpc += ShowEA(0, dp->sreg, dp->smode, dp->size, f);
			strcat(str, f);
		}

#if 1
		if (dp->suse && dp->duse)
		{
//			sprintf(f, ",");
//			strcat(str, f);
			strcat(str, ", ");
		}
#endif

		if (dp->duse)
		{
			f[0] = 0;
			newpc = m68k_getpc() + m68kpc_offset;
			newpc += ShowEA(0, dp->dreg, dp->dmode, dp->size, f);
			strcat(str, f);
		}

		if (ccpt != 0)
		{
			if (cctrue(dp->cc))
				sprintf(f, " == %08lX (TRUE)", (long)newpc);
			else
				sprintf(f, " == %08lX (FALSE)", (long)newpc);

			strcat(str, f);
		}
		else if ((opcode & 0xFF00) == 0x6100) // BSR
		{
			sprintf(f, " == %08lX", (long)newpc);
			strcat(str, f);
		}

//		fprintf(f, "\n");
	}

//	if (nextpc)
//		*nextpc = m68k_getpc() + m68kpc_offset;

	long int numberOfBytes = m68kpc_offset - pcOffsetSave;

	for(opwords=0; opwords<5; opwords++)
	{
		if (((opwords + 1) * 2) <= numberOfBytes)
			sprintf(f, "%04X ", get_iword_1(pcOffsetSave + opwords * 2));
		else
			sprintf(f, "     ");

		strcat(output, f);
	}

	strcat(output, str);

	return numberOfBytes;
}


//
// Disasemble one instruction at pc and store in str_buff
//
unsigned int m68k_disassemble(char * str_buff, unsigned int pc, unsigned int cpu_type)
{
	return m68k_disasm(str_buff, pc, 0, 1);
}

