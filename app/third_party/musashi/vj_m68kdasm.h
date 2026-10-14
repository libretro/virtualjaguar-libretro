/* The one function the app calls into the vendored Musashi disassembler (m68kdasm.c).
 * It reads from the buffer you give it, never from emulated memory and never from any core
 * global, so it is safe to call whenever the bytes are at hand. */
#ifndef VJ_M68KDASM_H
#define VJ_M68KDASM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Disassembles ONE 68000 instruction whose first byte is bytes[0] (big-endian memory order) and
 * whose address is `pc`.  `bytes` must hold at least 16 readable bytes (zero-padded past the end
 * of memory).  Writes the text to out[max] (e.g. "move.w  D0, ($1234).l") and returns the
 * instruction length in bytes (2..10), or 2 for data it cannot decode ("dc.w $xxxx"). */
unsigned vj_m68k_disasm(const unsigned char* bytes, unsigned pc, char* out, int max);

#ifdef __cplusplus
}
#endif

#endif
