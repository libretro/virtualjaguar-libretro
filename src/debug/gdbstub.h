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

/*
 * What the engine needs from a target. Phase 1 uses only readRegisters
 * and readMemory; later phases add the rest. Every function may be NULL,
 * in which case the engine replies "unsupported" rather than crashing.
 */
struct GDBTargetOps
{
   /* Serialise the register file as GDB expects it, hex, no separators.
    * Returns chars written, or -1. */
   int (*readRegisters)(void *user, char *out, int outMax);

   /* Read len guest bytes at addr into out as hex. MUST bounds-check addr
    * against the emulated map and return -1 on any out-of-range access. */
   int (*readMemory)(void *user, unsigned int addr, int len,
                     char *out, int outMax);
};

struct GDBSession
{
   const struct GDBTargetOps *ops;
   void *user;
   int noAckMode;
};

void GDBSessionInit(struct GDBSession *s, const struct GDBTargetOps *ops,
                    void *user);

int GDBExpandRLE(const char *in, int inLen, char *out, int outMax);

int GDBHandlePacket(struct GDBSession *s, const char *pay, int payLen,
                    char *reply, int replyMax);

/* Implemented in gdbtarget.c (Task 4). Declared here so libretro.c needs
 * only this one header. */
const struct GDBTargetOps *GDBJaguarOps(void);

#ifdef __cplusplus
}
#endif

#endif /* __GDBSTUB_H__ */
