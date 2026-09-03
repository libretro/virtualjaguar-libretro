/*
 * gdbdisasm.h -- the one and only GPU/DSP RISC disassembler in this tree.
 *
 * Header-only and self-contained (no Jaguar globals, no I/O) so it can be
 * included both by src/debug/gdbtarget.c (`monitor disasm`, live, in the
 * running core) and by test/tools/gpu_disasm_dump.c (offline, dlsym-based)
 * without either one inventing its own copy. Design: docs/gdb-stub-design.md
 * (issue #652) -- "reuse our own disassembly tooling... do not write a
 * second disassembler."
 *
 * GPU and DSP share the RISC core for opcodes 0-31 (arithmetic/logic/shift/
 * compare) but diverge from opcode 32 onward: the GPU has pixel ops
 * (LOADP/STOREP) the DSP has no use for, and the DSP has audio-saturation
 * ops (SAT16S/SAT32S/SUBQMOD/ADDQMOD/MIRROR) the GPU has no use for. The
 * mnemonic tables below are transcribed directly from the two dispatch
 * tables that actually execute the opcodes -- src/tom/gpu.c's executeOpcode
 * switch and src/jerry/dsp.c's dsp_dispatch[] in dsp_executeOpcode() -- so a
 * future opcode change there must be mirrored here by hand; nothing here is
 * derived automatically.
 *
 * C89/GNU89, like the rest of the core. Every accumulating snprintf() below
 * clamps its running offset to outMax immediately, so out+n is never
 * advanced past one-past-the-end even if the caller hands in a tiny buffer
 * -- pointer arithmetic past that is undefined behaviour in C regardless of
 * whether the result is ever dereferenced.
 */
#ifndef __GDBDISASM_H__
#define __GDBDISASM_H__

#include <stdio.h>

static const char * const GDBDisasmGPUMnemonics[64] =
{
   "add",       "addc",      "addq",      "addqt",
   "sub",       "subc",      "subq",      "subqt",
   "neg",       "and",       "or",        "xor",
   "not",       "btst",      "bset",      "bclr",
   "mult",      "imult",     "imultn",    "resmac",
   "imacn",     "div",       "abs",       "sh",
   "shlq",      "shrq",      "sha",       "sharq",
   "ror",       "rorq",      "cmp",       "cmpq",
   "sat8",      "sat16",     "move",      "moveq",
   "moveta",    "movefa",    "movei",     "loadb",
   "loadw",     "load",      "loadp",     "load(r14+n)",
   "load(r15+n)","storeb",   "storew",    "store",
   "storep",    "store(r14+n)","store(r15+n)","move pc",
   "jump",      "jr",        "mmult",     "mtoi",
   "normi",     "nop",       "load(r14+rn)","load(r15+rn)",
   "store(r14+rn)","store(r15+rn)","sat24","pack"
};

static const char * const GDBDisasmDSPMnemonics[64] =
{
   "add",       "addc",      "addq",      "addqt",
   "sub",       "subc",      "subq",      "subqt",
   "neg",       "and",       "or",        "xor",
   "not",       "btst",      "bset",      "bclr",
   "mult",      "imult",     "imultn",    "resmac",
   "imacn",     "div",       "abs",       "sh",
   "shlq",      "shrq",      "sha",       "sharq",
   "ror",       "rorq",      "cmp",       "cmpq",
   "subqmod",   "sat16s",    "move",      "moveq",
   "moveta",    "movefa",    "movei",     "loadb",
   "loadw",     "load",      "sat32s",    "load(r14+n)",
   "load(r15+n)","storeb",   "storew",    "store",
   "mirror",    "store(r14+n)","store(r15+n)","move pc",
   "jump",      "jr",        "mmult",     "mtoi",
   "normi",     "nop",       "load(r14+rn)","load(r15+rn)",
   "store(r14+rn)","store(r15+rn)","illegal","addqmod"
};

/* Clamp n into [0, cap] -- shared by GDBDisasmCC and GDBDisasmOne so an
 * accumulated snprintf() offset can never walk out+n past one-past-end. */
static int GDBDisasmClamp(int n, int cap)
{
   if (n < 0)
      return 0;
   if (n > cap)
      return cap;
   return n;
}

/* jr/jump condition-code decode -- mirrors build_branch_condition_table()
 * in src/tom/gpu.c (shared by the DSP, same encoding). The 5-bit field is
 * NOT simply "cc[n&7]": bit 4 selects the N flag instead of C for the
 * carry-style tests in bits 2-3 (issue #406). Writes into out, returns
 * bytes written (never NUL-terminates; caller owns termination). */
static int GDBDisasmCC(unsigned j, char *out, int outMax)
{
   int n = 0;
   int needSep = 0;

   if (outMax <= 0)
      return 0;

   if (j == 0)
   {
      out[0] = 'T';
      return 1;
   }

   if (j & 1)
   {
      n += snprintf(out + n, (size_t)(outMax - n), "NZ");
      n = GDBDisasmClamp(n, outMax);
      needSep = 1;
   }
   if (j & 2)
   {
      n += snprintf(out + n, (size_t)(outMax - n), "%sZ", needSep ? " " : "");
      n = GDBDisasmClamp(n, outMax);
      needSep = 1;
   }
   if (j & 4)
   {
      n += snprintf(out + n, (size_t)(outMax - n), "%s%s",
                    needSep ? " " : "", (j & 0x10) ? "NN" : "NC");
      n = GDBDisasmClamp(n, outMax);
      needSep = 1;
   }
   if (j & 8)
   {
      n += snprintf(out + n, (size_t)(outMax - n), "%s%s",
                    needSep ? " " : "", (j & 0x10) ? "N" : "C");
      n = GDBDisasmClamp(n, outMax);
      needSep = 1;
   }
   if (!needSep)
      n = GDBDisasmClamp(snprintf(out, (size_t)outMax, "cc%u", j), outMax);

   return n;
}

/*
 * Decode one RISC instruction at `addr` (word w, plus the two following
 * words w2/w3 for the movei 32-bit-immediate case) into out as a single
 * line of text, no trailing newline. `mnemonics` is one of the two tables
 * above. Returns bytes written, clamped to outMax (may fill the buffer
 * completely with no room for a NUL if outMax is small -- callers that
 * need a C string reserve one extra byte, same convention as GDBDisasmCC).
 */
static int GDBDisasmOne(const char * const mnemonics[64], unsigned int addr,
                        unsigned short w, unsigned short w2, unsigned short w3,
                        char *out, int outMax)
{
   unsigned op  = (unsigned)(w >> 10);
   unsigned r1  = (unsigned)((w >> 5) & 0x1F);
   unsigned r2  = (unsigned)(w & 0x1F);
   int n;

   if (outMax <= 0)
      return 0;

   n = GDBDisasmClamp(
          snprintf(out, (size_t)outMax, "$%06X: %04X  %-14s", addr, w, mnemonics[op]),
          outMax);

   if (op == 53)                        /* jr */
   {
      int disp = (int)((r1 & 0x10) ? (int)r1 - 32 : (int)r1);
      n += snprintf(out + n, (size_t)(outMax - n), " ");
      n = GDBDisasmClamp(n, outMax);
      n += GDBDisasmCC(r2, out + n, outMax - n);
      n = GDBDisasmClamp(n, outMax);
      n += snprintf(out + n, (size_t)(outMax - n), ", %+d -> $%06X",
                    disp, addr + 2 + disp * 2);
      n = GDBDisasmClamp(n, outMax);
   }
   else if (op == 52)                   /* jump */
   {
      n += snprintf(out + n, (size_t)(outMax - n), " ");
      n = GDBDisasmClamp(n, outMax);
      n += GDBDisasmCC(r2, out + n, outMax - n);
      n = GDBDisasmClamp(n, outMax);
      n += snprintf(out + n, (size_t)(outMax - n), ", (r%u)", r1);
      n = GDBDisasmClamp(n, outMax);
   }
   else if (op == 38)                   /* movei -- 32-bit immediate follows */
   {
      n += snprintf(out + n, (size_t)(outMax - n), " #$%08X, r%u",
                    ((unsigned int)w3 << 16) | (unsigned int)w2, r2);
      n = GDBDisasmClamp(n, outMax);
   }
   else if (op == 2 || op == 6 || op == 31 || op == 35 || op == 24 || op == 25)
   {
      /* addq/subq/cmpq/moveq/shlq/shrq: r1 is a 5-bit immediate, 0 means 32 */
      n += snprintf(out + n, (size_t)(outMax - n), " #%u, r%u", r1 ? r1 : 32, r2);
      n = GDBDisasmClamp(n, outMax);
   }
   else
   {
      n += snprintf(out + n, (size_t)(outMax - n), " r%u, r%u", r1, r2);
      n = GDBDisasmClamp(n, outMax);
   }

   return n;
}

#endif /* __GDBDISASM_H__ */
