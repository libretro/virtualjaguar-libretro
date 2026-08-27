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

#ifdef __cplusplus
}
#endif

#endif /* __GDBSTUB_H__ */
