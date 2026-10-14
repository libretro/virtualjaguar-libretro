/* Minimal stand-in for Musashi's m68k.h, for m68kdasm.c ONLY.
 *
 * The real m68k.h declares the whole 68000 emulator (m68k_get_reg, m68k_set_reg,
 * m68k_register_t, the callbacks, ...), which would collide with the core's own
 * src/m68000/m68kinterface.h.  The disassembler needs only the CPU-type enum,
 * M68K_ARRAY_LENGTH, the three memory-read hooks (never called by our wrapper,
 * which uses m68k_disassemble_raw; stubs live in vj_m68kdasm.c) and the
 * disassembler entry points.  Not part of upstream; see README.md.
 *
 * Musashi is Copyright Karl Stenerud, MIT license (LICENSE in this directory).
 */
#ifndef M68K__HEADER
#define M68K__HEADER

#ifdef __cplusplus
extern "C" {
#endif

#ifndef M68K_ARRAY_LENGTH
#define M68K_ARRAY_LENGTH(x) (sizeof(x) / sizeof(x[0]))
#endif

#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif

/* m68kdasm.c tests "M68K_COMPILE_FOR_MAME == M68K_OPT_ON": both must be defined
 * or the undefined-macro rule makes the test true.  Off = plain byte lengths. */
/* The core (src/core/jaguar.c, src/m68000/m68kinterface.c) defines functions with Musashi's names --
 * m68k_read_disassembler_*, which read emulated memory, and m68k_is_valid_instruction -- and both end up in
 * the same executable.  Every exported symbol of the vendored disassembler is therefore renamed here, in the
 * header m68kdasm.c includes first, so nothing of ours can clash with or be mistaken for the core's. */
#define m68k_disassemble          vj_dasm_m68k_disassemble
#define m68k_disassemble_raw      vj_dasm_m68k_disassemble_raw
#define m68k_is_valid_instruction vj_dasm_m68k_is_valid_instruction
#define m68ki_disassemble_quick   vj_dasm_m68ki_disassemble_quick
#define m68k_read_disassembler_8  vj_dasm_read_8
#define m68k_read_disassembler_16 vj_dasm_read_16
#define m68k_read_disassembler_32 vj_dasm_read_32

#define M68K_OPT_OFF 0
#define M68K_OPT_ON  1
#define M68K_COMPILE_FOR_MAME M68K_OPT_OFF

typedef enum
{
	M68K_CPU_TYPE_INVALID,
	M68K_CPU_TYPE_68000,
	M68K_CPU_TYPE_68010,
	M68K_CPU_TYPE_68EC020,
	M68K_CPU_TYPE_68020,
	M68K_CPU_TYPE_68EC030,
	M68K_CPU_TYPE_68030,
	M68K_CPU_TYPE_68EC040,
	M68K_CPU_TYPE_68LC040,
	M68K_CPU_TYPE_68040,
	M68K_CPU_TYPE_SCC68070
} m68k_cpu_type;

unsigned int m68k_read_disassembler_8  (unsigned int address);
unsigned int m68k_read_disassembler_16 (unsigned int address);
unsigned int m68k_read_disassembler_32 (unsigned int address);

unsigned int m68k_disassemble(char* str_buff, unsigned int pc, unsigned int cpu_type);
unsigned int m68k_disassemble_raw(char* str_buff, unsigned int pc, const unsigned char* opdata, const unsigned char* argdata, unsigned int cpu_type);
unsigned int m68k_is_valid_instruction(unsigned int instruction, unsigned int cpu_type);

#ifdef __cplusplus
}
#endif

#endif /* M68K__HEADER */
