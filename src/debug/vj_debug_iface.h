/*
 * vj_debug_iface.h -- core <-> app debug-tools interface (version 1).
 *
 * Shared, verbatim, by the core (strict C89) and the standalone app (C++17).
 * The core fills a static `struct vj_debug_iface` (src/debug/vjdebug.c) and
 * hands the app a pointer to it ONCE, through the private frontend channel
 * (VJ_ENV_SET_DEBUG_IFACE in src/core/vj_frontend_ext.h), but only when the
 * frontend advertised VJ_CAP_DEBUG in its HELLO.  RetroArch and every other
 * frontend never gets it.  See docs/app-frontend-extension.md and
 * docs/app-debug-tools.md.
 *
 * THE RULES
 *
 *   - Single thread.  Every call, in both directions, is made on the thread
 *     that runs retro_run(): either between two retro_run() calls, or from
 *     inside vj_debug_host.halt_pump while the core is parked in GDBHalt().
 *     Nothing here is thread safe and nothing may be called from a helper.
 *   - Static lifetime.  The vj_debug_iface the core passes, and the strings
 *     it returns (region names), stay valid until retro_deinit().  The
 *     vj_debug_host the app passes to attach() must stay valid until detach().
 *   - Append-only.  `size` is the sizeof the producer built against.  A new
 *     function pointer is appended at the END of vj_debug_iface / vj_debug_host,
 *     and a consumer must check `size` covers the member before using it.
 *     Anything else bumps VJ_DEBUG_IFACE_VERSION.
 *   - Addresses are the 24-bit Jaguar bus ($000000-$FFFFFF); all multi-byte
 *     values in this header are host `unsigned` (32 bit) in host order; memory
 *     image bytes are big-endian bus order, exactly as the 68000 sees them.
 *   - peek() never has a side effect on the emulated machine (it is the same
 *     byte path the GDB stub uses, who == DEBUGGER).  poke() only ever writes
 *     where the policy documented at VJ_DBG_POKE_* allows.
 *   - Detached cost is zero: attach() stores a pointer; nothing on a per
 *     instruction or per pixel path looks at it.
 */
#ifndef VJ_DEBUG_IFACE_H
#define VJ_DEBUG_IFACE_H

#ifdef __cplusplus
extern "C" {
#endif

#define VJ_DEBUG_IFACE_VERSION 1u

/* ---- targets (same values as GDB_TGT_* in src/debug/gdbstub.h) ------------ */
#define VJ_DBG_TARGET_M68K  0
#define VJ_DBG_TARGET_GPU   1
#define VJ_DBG_TARGET_DSP   2
#define VJ_DBG_NUM_TARGETS  3

/* ---- halt_pump verdicts --------------------------------------------------- */
#define VJ_DBG_RESUME_NONE      0   /* stay halted; call me again                       */
#define VJ_DBG_RESUME_CONTINUE  1   /* run until the next breakpoint                    */
#define VJ_DBG_RESUME_STEP      2   /* run exactly one instruction of the halted target */
#define VJ_DBG_RESUME_DETACH    3   /* resume AND detach (all breakpoints cleared)      */

/* ---- why the core halted (halt_reason) ------------------------------------ */
#define VJ_DBG_STOP_NONE        0   /* not halted                                       */
#define VJ_DBG_STOP_BREAKPOINT  1
#define VJ_DBG_STOP_STEP        2
#define VJ_DBG_STOP_USER        3   /* halt_next() / break-at-boot                      */

/* ---- region flags ---------------------------------------------------------- */
#define VJ_DBG_REGION_RAM       0x01u  /* plain read/write storage (DRAM, GPU/DSP RAM, CLUT) */
#define VJ_DBG_REGION_ROM       0x02u  /* read-only (cartridge, BIOS); poke refuses            */
#define VJ_DBG_REGION_REG       0x04u  /* hardware registers; poke needs VJ_DBG_POKE_REGS     */
#define VJ_DBG_REGION_BLOCKED   0x08u  /* never poked (BUTCH $DFFF00-$DFFFFF); peek is fine   */

/* ---- poke flags and results ------------------------------------------------ */
/* Without flags, poke() writes only VJ_DBG_REGION_RAM bytes.  VJ_DBG_POKE_REGS
 * additionally allows register space, EXCEPT the side-effecting set below which
 * also needs VJ_DBG_POKE_DANGEROUS (writing it starts or stops a processor, a
 * blit, or the serial port): B_CMD $F02238, G_CTRL $F02114, D_CTRL $F1A114,
 * OLP $F00020, LTXD $F1A148, RTXD $F1A14C.  BUTCH ($DFFF00-$DFFFFF) is refused
 * regardless.  A poke is all-or-nothing: if any byte of the range is refused,
 * no byte is written. */
#define VJ_DBG_POKE_REGS        0x1u
#define VJ_DBG_POKE_DANGEROUS   0x2u

#define VJ_DBG_ERR_RANGE      (-1)  /* n == 0, leaves $000000-$FFFFFF, or (poke) touches no region */
#define VJ_DBG_ERR_POLICY     (-2)  /* register/blocked write not allowed by the flags     */
#define VJ_DBG_ERR_READONLY   (-3)  /* ROM (cartridge/BIOS)                                */
#define VJ_DBG_ERR_STATE      (-4)  /* no content / core not initialised                   */

struct vj_debug_region
{
   const char *name;     /* static, e.g. "DRAM", "Cartridge", "TOM", "JERRY" */
   unsigned    start;    /* first bus address                                 */
   unsigned    len;      /* bytes                                             */
   unsigned    flags;    /* VJ_DBG_REGION_*                                   */
};

/* ---- registers -------------------------------------------------------------- */
/* set_reg() / reg numbers.
 *   68K : 0-7 D0-D7, 8-15 A0-A7, 16 PC, 17 SR, 18 USP, 19 SSP
 *   RISC: 0-31 bank 0 r0-r31, 32-63 bank 1 r0-r31, 64 PC, 65 FLAGS
 * (the RISC numbering is bank-absolute, NOT "current bank": set_reg(…, 5, v)
 * always writes bank 0 r5.  The active bank is vj_debug_regs.cur_bank.) */
#define VJ_DBG_REG_68K_D0     0
#define VJ_DBG_REG_68K_A0     8
#define VJ_DBG_REG_68K_PC     16
#define VJ_DBG_REG_68K_SR     17
#define VJ_DBG_REG_68K_USP    18
#define VJ_DBG_REG_68K_SSP    19
#define VJ_DBG_REG_RISC_BANK0 0
#define VJ_DBG_REG_RISC_BANK1 32
#define VJ_DBG_REG_RISC_PC    64
#define VJ_DBG_REG_RISC_FLAGS 65

struct vj_debug_regs
{
   /* 68000 (valid for VJ_DBG_TARGET_M68K) */
   unsigned d[8];
   unsigned a[8];        /* a[7] is the CURRENT stack pointer (USP or SSP per SR.S) */
   unsigned pc;
   unsigned sr;
   unsigned usp;
   unsigned ssp;
   /* GPU / DSP (valid for VJ_DBG_TARGET_GPU / _DSP) */
   unsigned bank0[32];
   unsigned bank1[32];
   unsigned risc_pc;
   unsigned flags;       /* G_FLAGS / D_FLAGS (live N/C/Z bits merged)                */
   unsigned ctrl;        /* G_CTRL / D_CTRL                                           */
   unsigned cur_bank;    /* 0 or 1: the register bank the processor is using now      */
};

/* ---- Object Processor snapshot ----------------------------------------------- */
#define VJ_DBG_OP_MAX_OBJECTS 1024

struct vj_debug_op_obj
{
   unsigned address;     /* bus address of the object's first phrase                  */
   unsigned type;        /* p0 low 3 bits: 0 bitmap, 1 scaled, 2 GPU, 3 branch, 4 stop */
   unsigned phrases;     /* how many phrases were captured: 1, 2 (bitmap) or 3 (scaled)*/
   unsigned p0_hi, p0_lo;/* each phrase as two host-order 32-bit halves (hi = bits 63-32) */
   unsigned p1_hi, p1_lo;
   unsigned p2_hi, p2_lo;
};

/* ---- blitter launch log ------------------------------------------------------- */
#define VJ_DBG_BLIT_LOG_SIZE 256   /* launches kept; older ones are overwritten */

struct vj_debug_blit
{
   unsigned seq;         /* launch number since the log was enabled (0-based)         */
   unsigned frame;       /* frame_number() at launch                                  */
   unsigned cmd;         /* B_CMD                                                     */
   unsigned a1_base, a1_flags, a1_pixel, a1_step;
   unsigned a2_base, a2_flags, a2_pixel, a2_step;
   unsigned count;       /* B_COUNT (inner | outer << 16)                             */
   unsigned engine;      /* 0 fast, 1 accurate                                        */
};

/* ---- the host (app) side ------------------------------------------------------- */
struct vj_debug_host
{
   unsigned size;        /* sizeof(struct vj_debug_host)                              */
   void    *user;
   /* Called repeatedly, on the emulation thread, while a processor is halted
    * (breakpoint, step, halt_next).  `target` is VJ_DBG_TARGET_*, `pc` the
    * address it stopped at.  The host may run its UI between calls (one nested
    * frame per call) and must NOT run the emulation.  The core resumes on the
    * first verdict other than VJ_DBG_RESUME_NONE. */
   int (*halt_pump)(void *user, int target, unsigned pc);
};

/* ---- the core side --------------------------------------------------------------- */
struct vj_debug_iface
{
   unsigned size;        /* sizeof(struct vj_debug_iface) the core was built with     */
   unsigned version;     /* VJ_DEBUG_IFACE_VERSION                                    */

   /* memory ------------------------------------------------------------------- */
   /* idx 0..n-1; returns 1 and fills *o, or 0 past the end. */
   int (*region)(int idx, struct vj_debug_region *o);
   /* Reads n bytes (side-effect free, byte path).  Returns n, or VJ_DBG_ERR_*. */
   int (*peek)(unsigned addr, unsigned char *dst, unsigned n);
   /* Writes n bytes per the VJ_DBG_POKE_* policy.  Returns n, or VJ_DBG_ERR_*. */
   int (*poke)(unsigned addr, const unsigned char *src, unsigned n, unsigned flags);

   /* registers / code ------------------------------------------------------------ */
   int (*get_regs)(int target, struct vj_debug_regs *o);          /* 1 ok, 0 no       */
   int (*set_reg)(int target, int reg, unsigned v);                /* 1 ok, 0 refused  */
   /* Disassembles ONE GPU/DSP instruction at `addr` (RISC targets only) into
    * out[max]; *len = its size in bytes (2, or 6 for MOVEI).  Returns 1 or 0. */
   int (*disasm_risc)(int target, unsigned addr, char *out, int max, unsigned *len);

   /* halt / breakpoints ------------------------------------------------------------ */
   /* Registers the host and initialises the breakpoint tables WITHOUT a socket.
    * Returns 1, or 0 if a host is already attached.  The GDB TCP stub (if the
    * option is on) keeps working; either side may resume the halted core. */
   int  (*attach)(const struct vj_debug_host *h);
   /* Clears every breakpoint this interface armed and forgets the host. */
   void (*detach)(void);
   int  (*break_set)(int target, unsigned addr);     /* 1 ok, 0 table full / bad target */
   int  (*break_clear)(int target, unsigned addr);   /* 1 ok (idempotent)               */
   /* Arms a halt on that target's very next instruction. */
   void (*halt_next)(int target);
   /* The target currently parked in halt_pump, or -1.  Only meaningful inside it. */
   int  (*halted_target)(void);

   /* Object Processor ----------------------------------------------------------------- */
   /* Arms a one-shot capture: the next display-list walk records each object's
    * raw phrases BEFORE the OP rewrites them (HEIGHT/DATA/REMAINDER update per
    * halfline), up to VJ_DBG_OP_MAX_OBJECTS. */
   void (*op_snapshot_arm)(void);
   /* Returns the number of objects captured (0..max), or -1 while armed but no
    * walk has completed yet (or never armed).  *halfline is the halfline the
    * captured walk ran on.  Stays readable until the next arm. */
   int  (*op_snapshot_get)(struct vj_debug_op_obj *o, int max, unsigned *halfline);

   /* video -------------------------------------------------------------------------------- */
   /* 16-bit CRY/RGB16-as-CRY pixel -> 0x00RRGGBB, via the same table the renderer uses. */
   unsigned (*cry_to_rgb)(unsigned v16);

   /* blitter ------------------------------------------------------------------------------ */
   void     (*blit_log_enable)(int on);
   /* Total launches since enable (monotonic).  Entry `i` is available while
    * head - VJ_DBG_BLIT_LOG_SIZE <= i < head. */
   unsigned (*blit_log_head)(void);
   int      (*blit_log_get)(unsigned i, struct vj_debug_blit *o);   /* 1 ok, 0 gone/none */

   /* textures / misc ------------------------------------------------------------------------ */
   /* Re-reads the texture-replacement packs (TexReplaceShutdown + ContentLoaded).
    * Returns 1 if the replacement feature is active afterwards, else 0. */
   int      (*texreplace_reload)(void);
   /* Emulated frame counter (retro_run calls since content load). */
   unsigned (*frame_number)(void);

   /* ---- append new members below this line ---- */
   int      (*halt_reason)(void);   /* VJ_DBG_STOP_* of the current halt, else NONE */
};

#ifdef __cplusplus
}
#endif

#endif /* VJ_DEBUG_IFACE_H */
