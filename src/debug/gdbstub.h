/*
 * gdbstub.h -- GDB Remote Serial Protocol engine.
 *
 * This translation unit is deliberately free of sockets and of every
 * Jaguar global, so test/tools/test_gdbstub_proto.c can link it alone.
 * Design: docs/gdb-stub-design.md (issue #652).
 */
#ifndef __GDBSTUB_H__
#define __GDBSTUB_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Modulo-256 sum of payload, as RSP defines it. */
int GDBChecksum(const char *payload, int len);

/*
 * Decode "$payload#cs" from raw. Returns payload length written to out,
 * or: -1 malformed framing, -2 checksum mismatch, -3 would overflow out.
 * Never writes more than outMax bytes and never NUL-terminates.
 */
int GDBDecodePacket(const char *raw, int rawLen, char *out, int outMax);

/*
 * Encode payload as "$payload#cs" into out. Returns bytes written, or -1
 * if out is too small. Never NUL-terminates.
 */
int GDBEncodePacket(const char *payload, int len, char *out, int outMax);

#define GDB_PACKET_MAX 4096

/* GDB thread numbers (1-based, RSP convention) map 1:1 to processors.
 * Fixed forever -- clients cache thread identity across a session.
 * GDB_TGT_* below are the 0-based equivalents used for arrays/indexing;
 * thread number == GDB_TGT_* + 1. */
#define GDB_TGT_68K       0
#define GDB_TGT_GPU       1
#define GDB_TGT_DSP       2
#define GDB_NUM_TARGETS   3

/* Register numbering within the GPU/DSP custom target description
 * (org.atari.jaguar.risc): R0-R31, then PC, then FLAGS. Fixed once
 * shipped -- see docs/gdb-stub-design.md Open Question 3. */
#define GDB_RISC_REG_PC     32
#define GDB_RISC_REG_FLAGS  33
#define GDB_RISC_NUM_REGS   34

/* Stop reasons, used to build the stop-reply and for logging. */
#define GDB_STOP_BREAKPOINT 0
#define GDB_STOP_WATCHPOINT 1
#define GDB_STOP_STEP       2
#define GDB_STOP_USER       3

/* Z/z packet break/watch types, as GDB defines them on the wire. */
#define GDB_BP_SOFTWARE   0
#define GDB_BP_HARDWARE   1
#define GDB_BP_WATCH_WR   2
#define GDB_BP_WATCH_RD   3
#define GDB_BP_WATCH_RW   4

/*
 * What the engine needs from a target. Every function pointer may be
 * NULL, in which case the engine replies "unsupported" (or the
 * packet-specific error) rather than crashing. One instance per
 * processor (68K/GPU/DSP); register a target with
 * GDBSessionSetTargetOps() below.
 */
struct GDBTargetOps
{
   /* Serialise the register file as GDB expects it, hex, no separators.
    * Returns chars written, or -1. */
   int (*readRegisters)(void *user, char *out, int outMax);

   /* Parse GDB's "G" payload (hex, no separators, same layout as
    * readRegisters's output) and apply it. Returns 0 on success, -1 on
    * malformed input. */
   int (*writeRegisters)(void *user, const char *hex, int hexLen);

   /* Read len guest bytes at addr into out as hex. MUST bounds-check addr
    * against the emulated map and return -1 on any out-of-range access. */
   int (*readMemory)(void *user, unsigned int addr, int len,
                     char *out, int outMax);

   /* Write len guest bytes at addr from hex (2*len hex chars). MUST
    * bounds-check identically to readMemory. Returns 0 on success, -1 on
    * any out-of-range access or malformed hex. */
   int (*writeMemory)(void *user, unsigned int addr, int len,
                      const char *hex, int hexLen);

   /* Z/z packet handling. type is one of GDB_BP_*. Returns 0 (OK), -1
    * (unsupported -- empty reply), or -2 (out of resources -- E01, e.g.
    * the breakpoint table is full or a watchpoint slot is already
    * taken). insert/remove are idempotent: removing an address that was
    * never inserted is not an error. */
   int (*insertBreak)(void *user, int type, unsigned int addr, unsigned int kind);
   int (*removeBreak)(void *user, int type, unsigned int addr, unsigned int kind);

   /* qXfer:features:read target: return a pointer to a static,
    * NUL-terminated XML blob (annex is ignored -- each target has
    * exactly one description) and its length. NULL (xmlLen set to 0)
    * means "this target has no custom description" (thread 1 / 68K: GDB
    * already knows the m68k architecture natively). */
   const char * (*targetXML)(void *user, int *xmlLen);

   /* qRcmd ("monitor ..."), cmd already hex-decoded and NUL-terminated.
    * Appends plain (not hex-encoded -- the engine does that) text to
    * out. Returns bytes written. */
   int (*monitorCmd)(void *user, const char *cmd, char *out, int outMax);
};

struct GDBSession
{
   const struct GDBTargetOps *ops[GDB_NUM_TARGETS];
   void *user[GDB_NUM_TARGETS];
   int noAckMode;
   /* Hg/Hc thread selection, RSP thread numbers (1-based); 0 means
    * "any"/unset and is treated as thread 1. */
   int threadG;
   int threadC;
};

void GDBSessionInit(struct GDBSession *s, const struct GDBTargetOps *ops68k,
                    void *user68k);

/* Registers (or clears, with ops==NULL) the ops/user pair for one
 * non-68K target. Safe to call before any packet is processed. */
void GDBSessionSetTargetOps(struct GDBSession *s, int target,
                            const struct GDBTargetOps *ops, void *user);

int GDBExpandRLE(const char *in, int inLen, char *out, int outMax);

/*
 * Handle one decoded RSP payload and produce a reply payload (NOT yet
 * framed with $...#cs -- the caller encodes it). Returns the reply
 * length, which may legitimately be 0 (RSP's "I don't implement this"
 * convention for most packets, but ALSO the correct reply-suppression
 * for 'c'/'s'/vCont: those never get an immediate reply, the eventual
 * stop-reply is sent later, out of band, by GDBHalt()). Check
 * *resumeRequested after every call: nonzero means the caller (the halt
 * loop) should stop servicing packets and let the halted processor run;
 * *resumeIsStep distinguishes step from continue. Both are 0 on entry
 * and only ever set, never read, by this function -- caller clears them
 * before the next call.
 */
int GDBHandlePacket(struct GDBSession *s, const char *pay, int payLen,
                    char *reply, int replyMax,
                    int *resumeRequested, int *resumeIsStep);

int GDBParseHexU32(const char *s, int len, unsigned int *out);

/* Implemented in gdbtarget.c. Declared here so libretro.c needs only this
 * one header. */
const struct GDBTargetOps *GDBJaguarOps(void);
const struct GDBTargetOps *GDBGpuOps(void);
const struct GDBTargetOps *GDBDspOps(void);

/*
 * Per-target armed-breakpoint counters -- the whole "near zero cost when
 * disabled" premise (docs/gdb-stub-design.md, "Breakpoint detection").
 * Defined in gdbtarget.c; read by the hot paths in src/core/jaguar.c,
 * src/tom/gpu.c, src/jerry/dsp.c. Each is the count of armed execution
 * breakpoints (Z0/Z1) for that processor PLUS 1 while a one-shot single
 * step is pending for it -- either way, nonzero means "call GDBCheckPC",
 * zero means "one load, one never-taken branch, done".
 */
extern int gdbArmed68K;
extern int gdbArmedGPU;
extern int gdbArmedDSP;

/* Called only when the matching gdbArmedXXX counter above is nonzero.
 * Consults a 256-entry direct-mapped cache first; a miss falls through
 * to a linear scan of the (small, capped) breakpoint table. Returns
 * nonzero if pc is an armed breakpoint or the pending one-shot step
 * target for that processor. */
int GDBCheckPC(int target, unsigned int pc);

/*
 * Blocks. Freezes the calling processor's forward progress by servicing
 * RSP packets in a loop until a continue/step is issued or the client
 * disconnects (which disarms every breakpoint/watchpoint and resumes
 * immediately -- a dead debugger must never leave the machine wedged).
 * Called from inside M68KInstructionHook()/GPUExec()/DSPExec() --
 * deliberately does NOT unwind the call stack, so every processor's
 * state is frozen exactly where it stopped. pc is the address to report
 * in the stop reply.
 */
void GDBHalt(int target, int reason, unsigned int pc);

/*
 * Called from the memory-access breakpoint sites in src/core/jaguar.c
 * (m68k_read/write_memory_{8,16,32}), guarded there by "if (bpmActive)"
 * -- bpmActive/bpmAddress1 (src/core/jaguar.c) are owned exclusively by
 * this module; nothing else in the tree ever sets them. isWrite is 1 for
 * a write access, 0 for a read. A no-op unless the single armed
 * watchpoint's kind (write/read/access) matches and the address matches.
 */
void GDBMemWatchHit(unsigned int address, int isWrite);

/* Disarms every breakpoint/watchpoint/step and resets thread/halt state,
 * without touching the socket. Called on client disconnect (mid-halt or
 * not), on halt-timeout auto-continue, and from the reset paths so a
 * fresh content load never inherits a previous session's armed state. */
void GDBTargetResetState(void);

/* Called once, right after GDBSockOpen() succeeds: builds the session
 * and registers all three targets' ops, then resets armed state. */
void GDBTargetOpen(void);

/* Called on content unload/deinit (or a bind failure that leaves the
 * stub disabled for this session): resets all Jaguar-side state. Does
 * NOT touch the socket -- callers close that separately with
 * GDBSockClose() first, matching the existing Phase 1 lifecycle. */
void GDBTargetClose(void);

/* Edge-triggered: true the first time this is called after a new client
 * has accepted, false otherwise. Lets libretro.c fire the "client
 * attached" OSD banner (docs/gdb-stub-design.md "The halt loop and
 * retro_run()") from retro_run() without this module needing to reach
 * environ_cb itself. */
int GDBSockHasClientAttachEvent(void);

/*
 * Loopback-only TCP transport, implemented in gdbsock.c (Task 5). Binds
 * 127.0.0.1 only -- never INADDR_ANY -- and accepts a single client.
 * On platforms without BSD sockets, GDBSockOpen() always fails and the
 * rest are inert, so the caller degrades to "stub unavailable" rather
 * than failing content load.
 */
int GDBSockOpen(int port);
void GDBSockClose(void);
int GDBSockPoll(void);
int GDBSockRecv(char *buf, int max);
int GDBSockSend(const char *buf, int len);
/* True iff a client is currently connected (accept()ed, not yet
 * dropped). Lets libretro.c fire the "client attached" OSD banner on
 * the accept edge without owning any socket state itself. */
int GDBSockHasClient(void);

/*
 * Non-blocking, called once per retro_run() frame while the stub is
 * enabled. Drains whatever the socket has, dispatches every complete
 * packet currently buffered, and returns -- never loops waiting for
 * more data. This is also the low-level pump GDBHalt() calls
 * repeatedly (with a short sleep between calls) while blocked.
 */
void GDBTargetServicePoll(void);

/* Optional halt timeout in seconds (virtualjaguar_gdb_halt_timeout); 0
 * means "off" (the documented default -- silently resuming a debugged
 * machine is worse than a freeze for this audience). Set by libretro.c
 * from the core option. */
void GDBTargetSetHaltTimeout(int seconds);

/* Halt-at-boot support (virtualjaguar_gdb_wait): when armed, the very
 * first M68KInstructionHook() call after content load halts
 * unconditionally, exactly like a breakpoint at the reset vector, so a
 * developer can attach before anything has executed. Self-clearing:
 * fires at most once per content load. */
void GDBTargetArmWaitAtBoot(void);

#ifdef __cplusplus
}
#endif

#endif /* __GDBSTUB_H__ */
