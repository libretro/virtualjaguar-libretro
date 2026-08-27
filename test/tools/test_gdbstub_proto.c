/*
 * test_gdbstub_proto.c -- unit tests for the GDB RSP protocol engine
 * (src/debug/gdbstub.c) against a fake in-memory transport and a fake
 * target. No emulator, no sockets.
 *
 * Design: docs/gdb-stub-design.md (issue #652).
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "../../src/debug/gdbstub.h"

/* ------------------------------------------------------------------ */
/* Minimal test runner (copied verbatim from test/test_boot_config.c) */
/* ------------------------------------------------------------------ */

static int tf_pass = 0, tf_fail = 0, tf_skip = 0;
static const char *tf_suite = "";
static const char *tf_name = "";
static bool tf_failed = false;

#define SUITE(n) do { tf_suite = (n); tf_pass = tf_fail = tf_skip = 0; \
    fprintf(stderr, "\n=== %s ===\n", tf_suite); } while(0)
#define TEST(n) static void test_##n(void)
#define RUN(n) do { tf_name = #n; tf_failed = false; test_##n(); \
    if (tf_failed) tf_fail++; \
    else { tf_pass++; fprintf(stderr, "  PASS  %s\n", #n); } } while(0)
#define SKIP(n, r) do { tf_skip++; fprintf(stderr, "  SKIP  %s (%s)\n", #n, r); } while(0)
#define REPORT() (fprintf(stderr, "\n--- %s: %d passed, %d failed, %d skipped ---\n\n", \
    tf_suite, tf_pass, tf_fail, tf_skip), tf_fail)
#define FAIL(fmt, ...) do { fprintf(stderr, "  FAIL  %s:%d: " fmt "\n", \
    tf_name, __LINE__, ##__VA_ARGS__); tf_failed = true; return; } while(0)
#define ASSERT(cond) do { if (!(cond)) FAIL("expected true: %s", #cond); } while(0)
#define ASSERT_EQ(a, b) do { int _a=(int)(a), _b=(int)(b); \
    if (_a != _b) FAIL("%s == %s: got %d, want %d", #a, #b, _a, _b); } while(0)
#define ASSERT_STR(a, b) do { if (strcmp((a),(b))!=0) \
    FAIL("%s == %s: got \"%s\"", #a, #b, (a)); } while(0)

TEST(checksum_is_modulo_256_sum) {
    /* 'O','K' = 0x4F + 0x4B = 0x9A */
    ASSERT_EQ(GDBChecksum("OK", 2), 0x9A);
}

TEST(checksum_wraps_at_256) {
    /* 0xFF + 0x02 = 0x101 -> 0x01 */
    char buf[2]; buf[0] = (char)0xFF; buf[1] = (char)0x02;
    ASSERT_EQ(GDBChecksum(buf, 2), 0x01);
}

TEST(decode_extracts_payload) {
    char out[64];
    int n = GDBDecodePacket("$OK#9a", 6, out, sizeof(out));
    ASSERT_EQ(n, 2);
    out[n] = '\0';
    ASSERT_STR(out, "OK");
}

TEST(decode_accepts_uppercase_checksum) {
    char out[64];
    ASSERT_EQ(GDBDecodePacket("$OK#9A", 6, out, sizeof(out)), 2);
}

TEST(decode_rejects_bad_checksum) {
    char out[64];
    ASSERT_EQ(GDBDecodePacket("$OK#00", 6, out, sizeof(out)), -2);
}

TEST(decode_rejects_missing_dollar) {
    char out[64];
    ASSERT_EQ(GDBDecodePacket("OK#9a", 5, out, sizeof(out)), -1);
}

TEST(decode_rejects_missing_hash) {
    char out[64];
    ASSERT_EQ(GDBDecodePacket("$OK9a", 5, out, sizeof(out)), -1);
}

TEST(decode_rejects_truncated_checksum) {
    char out[64];
    ASSERT_EQ(GDBDecodePacket("$OK#9", 5, out, sizeof(out)), -1);
}

TEST(decode_refuses_to_overflow_output) {
    char out[4];
    /* payload is 8 bytes, out holds 4 */
    ASSERT_EQ(GDBDecodePacket("$AAAAAAAA#c0", 12, out, sizeof(out)), -3);
}

TEST(encode_wraps_payload_with_checksum) {
    char out[64];
    int n = GDBEncodePacket("OK", 2, out, sizeof(out));
    ASSERT_EQ(n, 6);
    out[n] = '\0';
    ASSERT_STR(out, "$OK#9a");
}

TEST(encode_refuses_to_overflow_output) {
    char out[4];
    ASSERT_EQ(GDBEncodePacket("OK", 2, out, sizeof(out)), -1);
}

/* ------------------------------------------------------------------ */
/* Task 2: run-length decode and command dispatch                      */
/* ------------------------------------------------------------------ */

TEST(rle_expands_star_runs) {
    char out[64];
    /* '0' then '*' with ' ' (0x20 = 32) -> repeat previous 32-29 = 3 more */
    int n = GDBExpandRLE("0* ", 3, out, sizeof(out));
    ASSERT_EQ(n, 4);
    out[n] = '\0';
    ASSERT_STR(out, "0000");
}

TEST(rle_passes_through_plain_text) {
    char out[64];
    int n = GDBExpandRLE("qSupported", 10, out, sizeof(out));
    ASSERT_EQ(n, 10);
}

TEST(rle_rejects_leading_star) {
    char out[64];
    ASSERT_EQ(GDBExpandRLE("* ", 2, out, sizeof(out)), -1);
}

TEST(rle_refuses_to_overflow_output) {
    char out[4];
    ASSERT_EQ(GDBExpandRLE("0*~", 3, out, sizeof(out)), -1);
}

/* --- dispatch --- */
static struct GDBSession g_sess;

static void setup_session(void) {
    static struct GDBTargetOps ops;
    memset(&ops, 0, sizeof(ops));
    GDBSessionInit(&g_sess, &ops, NULL);
}

TEST(qsupported_advertises_packet_size) {
    char reply[256];
    int n;
    setup_session();
    n = GDBHandlePacket(&g_sess, "qSupported:multiprocess+", 24, reply, sizeof(reply));
    ASSERT(n > 0);
    reply[n] = '\0';
    ASSERT(strstr(reply, "PacketSize=") != NULL);
}

TEST(unknown_command_returns_empty_reply) {
    char reply[256];
    setup_session();
    ASSERT_EQ(GDBHandlePacket(&g_sess, "zzUnknown", 9, reply, sizeof(reply)), 0);
}

TEST(halt_reason_reports_sigtrap) {
    char reply[256];
    int n;
    setup_session();
    n = GDBHandlePacket(&g_sess, "?", 1, reply, sizeof(reply));
    ASSERT(n > 0);
    reply[n] = '\0';
    ASSERT_STR(reply, "S05");
}

int main(void) {
    SUITE("gdbstub framing");
    RUN(checksum_is_modulo_256_sum);
    RUN(checksum_wraps_at_256);
    RUN(decode_extracts_payload);
    RUN(decode_accepts_uppercase_checksum);
    RUN(decode_rejects_bad_checksum);
    RUN(decode_rejects_missing_dollar);
    RUN(decode_rejects_missing_hash);
    RUN(decode_rejects_truncated_checksum);
    RUN(decode_refuses_to_overflow_output);
    RUN(encode_wraps_payload_with_checksum);
    RUN(encode_refuses_to_overflow_output);
    RUN(rle_expands_star_runs);
    RUN(rle_passes_through_plain_text);
    RUN(rle_rejects_leading_star);
    RUN(rle_refuses_to_overflow_output);
    RUN(qsupported_advertises_packet_size);
    RUN(unknown_command_returns_empty_reply);
    RUN(halt_reason_reports_sigtrap);
    return REPORT();
}
