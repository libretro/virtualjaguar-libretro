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

/* Phase 2 added two out-parameters to GDBHandlePacket (resumeRequested/
 * resumeIsStep, for c/s/vCont -- see gdbstub.h). Every pre-existing test
 * below only cares about the reply, so this thin wrapper keeps them
 * readable; the new tests that DO care about resume signalling call
 * GDBHandlePacket directly. */
static int TF_Handle(struct GDBSession *s, const char *pay, int payLen,
                      char *reply, int replyMax) {
    int resumeRequested = 0, resumeIsStep = 0;
    return GDBHandlePacket(s, pay, payLen, reply, replyMax,
                           &resumeRequested, &resumeIsStep);
}

TEST(qsupported_advertises_packet_size) {
    char reply[256];
    int n;
    setup_session();
    n = TF_Handle(&g_sess, "qSupported:multiprocess+", 24, reply, sizeof(reply));
    ASSERT(n > 0);
    reply[n] = '\0';
    ASSERT(strstr(reply, "PacketSize=") != NULL);
}

TEST(unknown_command_returns_empty_reply) {
    char reply[256];
    setup_session();
    ASSERT_EQ(TF_Handle(&g_sess, "zzUnknown", 9, reply, sizeof(reply)), 0);
}

TEST(halt_reason_reports_sigtrap) {
    char reply[256];
    int n;
    setup_session();
    n = TF_Handle(&g_sess, "?", 1, reply, sizeof(reply));
    ASSERT(n > 0);
    reply[n] = '\0';
    ASSERT_STR(reply, "S05");
}

/* ------------------------------------------------------------------ */
/* Task 3: bounds-checked memory reads                                 */
/* ------------------------------------------------------------------ */

static unsigned char fake_mem[256];

static int fake_read_mem(void *user, unsigned int addr, int len,
                         char *out, int outMax) {
    static const char hx[] = "0123456789abcdef";
    int i;
    (void)user;
    if (addr > sizeof(fake_mem) || len < 0) return -1;
    if ((addr + (unsigned int)len) > sizeof(fake_mem)) return -1;  /* the invariant */
    if ((len * 2) > outMax) return -1;
    for (i = 0; i < len; i++) {
        out[i*2]   = hx[(fake_mem[addr+i] >> 4) & 0xF];
        out[i*2+1] = hx[fake_mem[addr+i] & 0xF];
    }
    return len * 2;
}

static void setup_mem_session(void) {
    static struct GDBTargetOps ops;
    memset(&ops, 0, sizeof(ops));
    ops.readMemory = fake_read_mem;
    GDBSessionInit(&g_sess, &ops, NULL);
    fake_mem[0] = 0xDE; fake_mem[1] = 0xAD;
}

TEST(parse_hex_u32_reads_value) {
    unsigned int v = 0;
    ASSERT_EQ(GDBParseHexU32("f00d", 4, &v), 4);
    ASSERT_EQ((int)v, 0xF00D);
}

TEST(parse_hex_u32_rejects_non_hex) {
    unsigned int v = 0;
    ASSERT_EQ(GDBParseHexU32("zz", 2, &v), -1);
}

TEST(read_memory_returns_hex) {
    char reply[256]; int n;
    setup_mem_session();
    n = TF_Handle(&g_sess, "m0,2", 4, reply, sizeof(reply));
    ASSERT_EQ(n, 4);
    reply[n] = '\0';
    ASSERT_STR(reply, "dead");
}

TEST(read_memory_past_end_is_refused) {
    char reply[256]; int n;
    setup_mem_session();
    /* 0xFF + 4 bytes overruns the 256-byte fake guest */
    n = TF_Handle(&g_sess, "mff,4", 5, reply, sizeof(reply));
    ASSERT(n > 0);
    reply[n] = '\0';
    ASSERT_STR(reply, "E01");
}

TEST(read_memory_wild_address_is_refused) {
    char reply[256]; int n;
    setup_mem_session();
    n = TF_Handle(&g_sess, "mffffffff,4", 11, reply, sizeof(reply));
    ASSERT(n > 0);
    reply[n] = '\0';
    ASSERT_STR(reply, "E01");
}

TEST(read_memory_absurd_length_is_refused) {
    char reply[64]; int n;
    setup_mem_session();
    /* len would overflow the caller's reply buffer even if in range */
    n = TF_Handle(&g_sess, "m0,1000", 7, reply, sizeof(reply));
    ASSERT(n > 0);
    reply[n] = '\0';
    ASSERT_STR(reply, "E01");
}

TEST(read_memory_without_target_op_is_unsupported) {
    char reply[256];
    setup_session();   /* ops.readMemory == NULL */
    ASSERT_EQ(TF_Handle(&g_sess, "m0,2", 4, reply, sizeof(reply)), 0);
}

/* ------------------------------------------------------------------ */
/* Task 4: 68000 register serialisation                                */
/* ------------------------------------------------------------------ */

static int fake_read_regs(void *user, char *out, int outMax) {
    /* d0=1, everything else 0, sr=0x2700, pc=0x802000 */
    static const char *s =
      "00000001" "00000000" "00000000" "00000000"
      "00000000" "00000000" "00000000" "00000000"
      "00000000" "00000000" "00000000" "00000000"
      "00000000" "00000000" "00000000" "00000000"
      "00002700" "00802000";
    int n = (int)strlen(s);
    (void)user;
    if (n > outMax) return -1;
    memcpy(out, s, (size_t)n);
    return n;
}

TEST(read_registers_returns_144_hex_chars) {
    char reply[512]; int n;
    static struct GDBTargetOps ops;
    memset(&ops, 0, sizeof(ops));
    ops.readRegisters = fake_read_regs;
    GDBSessionInit(&g_sess, &ops, NULL);
    n = TF_Handle(&g_sess, "g", 1, reply, sizeof(reply));
    ASSERT_EQ(n, 144);
    ASSERT_EQ(memcmp(reply, "00000001", 8), 0);          /* d0 first */
    ASSERT_EQ(memcmp(reply + 136, "00802000", 8), 0);    /* pc last  */
}

TEST(read_registers_without_target_op_is_unsupported) {
    char reply[512];
    setup_session();
    ASSERT_EQ(TF_Handle(&g_sess, "g", 1, reply, sizeof(reply)), 0);
}

/* ------------------------------------------------------------------ */
/* Phase 2: register/memory writes, Z/z, c/s/vCont, qRcmd, threads     */
/* ------------------------------------------------------------------ */

static char last_written_hex[512];
static int last_written_hex_len;

static int fake_write_regs(void *user, const char *hex, int hexLen) {
    (void)user;
    if (hexLen < 144) return -1;
    memcpy(last_written_hex, hex, (size_t)hexLen);
    last_written_hex_len = hexLen;
    return 0;
}

TEST(write_registers_calls_ops_and_replies_ok) {
    char reply[64]; int n;
    static struct GDBTargetOps ops;
    char pay[1 + 144];
    memset(&ops, 0, sizeof(ops));
    ops.writeRegisters = fake_write_regs;
    GDBSessionInit(&g_sess, &ops, NULL);
    pay[0] = 'G';
    memset(pay + 1, '0', 144);
    last_written_hex_len = 0;
    n = TF_Handle(&g_sess, pay, (int)sizeof(pay), reply, sizeof(reply));
    ASSERT_EQ(n, 2);
    reply[n] = '\0';
    ASSERT_STR(reply, "OK");
    ASSERT_EQ(last_written_hex_len, 144);
}

TEST(write_registers_without_target_op_is_unsupported) {
    char reply[64];
    char pay[1 + 144];
    setup_session();
    pay[0] = 'G';
    memset(pay + 1, '0', 144);
    ASSERT_EQ(TF_Handle(&g_sess, pay, (int)sizeof(pay), reply, sizeof(reply)), 0);
}

static unsigned char fake_write_mem_target[256];

static int fake_write_mem(void *user, unsigned int addr, int len,
                          const char *hex, int hexLen) {
    static const char hx[] = "0123456789abcdef";
    int i;
    (void)user;
    if (addr > sizeof(fake_write_mem_target) || len < 0) return -1;
    if ((addr + (unsigned int)len) > sizeof(fake_write_mem_target)) return -1;
    if (hexLen != len * 2) return -1;
    for (i = 0; i < len; i++) {
        const char *hi = strchr(hx, hex[i*2]);
        const char *lo = strchr(hx, hex[i*2+1]);
        if (!hi || !lo) return -1;
        fake_write_mem_target[addr+i] = (unsigned char)(((hi - hx) << 4) | (lo - hx));
    }
    return 0;
}

static void setup_write_mem_session(void) {
    static struct GDBTargetOps ops;
    memset(&ops, 0, sizeof(ops));
    ops.writeMemory = fake_write_mem;
    GDBSessionInit(&g_sess, &ops, NULL);
    memset(fake_write_mem_target, 0, sizeof(fake_write_mem_target));
}

TEST(write_memory_writes_and_replies_ok) {
    char reply[64]; int n;
    setup_write_mem_session();
    n = TF_Handle(&g_sess, "M0,2:dead", 9, reply, sizeof(reply));
    ASSERT_EQ(n, 2);
    reply[n] = '\0';
    ASSERT_STR(reply, "OK");
    ASSERT_EQ(fake_write_mem_target[0], 0xDE);
    ASSERT_EQ(fake_write_mem_target[1], 0xAD);
}

TEST(write_memory_length_mismatch_is_refused) {
    char reply[64]; int n;
    setup_write_mem_session();
    /* len=2 promised, only 1 byte (2 hex chars) supplied */
    n = TF_Handle(&g_sess, "M0,2:de", 7, reply, sizeof(reply));
    ASSERT(n > 0);
    reply[n] = '\0';
    ASSERT_STR(reply, "E01");
}

static int fake_insert_break_calls;
static int fake_insert_break_result = 0;

static int fake_insert_break(void *user, int type, unsigned int addr, unsigned int kind) {
    (void)user; (void)type; (void)addr; (void)kind;
    fake_insert_break_calls++;
    return fake_insert_break_result;
}

static int fake_remove_break_calls;

static int fake_remove_break(void *user, int type, unsigned int addr, unsigned int kind) {
    (void)user; (void)type; (void)addr; (void)kind;
    fake_remove_break_calls++;
    return 0;
}

static void setup_break_session(void) {
    static struct GDBTargetOps ops;
    memset(&ops, 0, sizeof(ops));
    ops.insertBreak = fake_insert_break;
    ops.removeBreak = fake_remove_break;
    GDBSessionInit(&g_sess, &ops, NULL);
    fake_insert_break_calls = 0;
    fake_remove_break_calls = 0;
    fake_insert_break_result = 0;
}

TEST(insert_breakpoint_replies_ok) {
    char reply[64]; int n;
    setup_break_session();
    n = TF_Handle(&g_sess, "Z0,1000,2", 9, reply, sizeof(reply));
    ASSERT_EQ(n, 2);
    reply[n] = '\0';
    ASSERT_STR(reply, "OK");
    ASSERT_EQ(fake_insert_break_calls, 1);
}

TEST(insert_breakpoint_out_of_resources_is_e01) {
    char reply[64]; int n;
    setup_break_session();
    fake_insert_break_result = -2;
    n = TF_Handle(&g_sess, "Z1,2000,2", 9, reply, sizeof(reply));
    ASSERT(n > 0);
    reply[n] = '\0';
    ASSERT_STR(reply, "E01");
}

TEST(remove_breakpoint_replies_ok) {
    char reply[64]; int n;
    setup_break_session();
    n = TF_Handle(&g_sess, "z0,1000,2", 9, reply, sizeof(reply));
    ASSERT_EQ(n, 2);
    reply[n] = '\0';
    ASSERT_STR(reply, "OK");
    ASSERT_EQ(fake_remove_break_calls, 1);
}

TEST(watchpoint_type_reaches_ops_too) {
    char reply[64]; int n;
    setup_break_session();
    n = TF_Handle(&g_sess, "Z2,3000,4", 9, reply, sizeof(reply));
    ASSERT_EQ(n, 2);
    ASSERT_EQ(fake_insert_break_calls, 1);
}

TEST(breakpoint_without_target_op_is_unsupported) {
    char reply[64];
    setup_session();
    ASSERT_EQ(TF_Handle(&g_sess, "Z0,1000,2", 9, reply, sizeof(reply)), 0);
}

TEST(continue_sets_resume_and_suppresses_reply) {
    char reply[64];
    int resumeRequested = 0, resumeIsStep = 1;
    int n;
    setup_session();
    n = GDBHandlePacket(&g_sess, "c", 1, reply, sizeof(reply),
                        &resumeRequested, &resumeIsStep);
    ASSERT_EQ(n, 0);
    ASSERT_EQ(resumeRequested, 1);
    ASSERT_EQ(resumeIsStep, 0);
}

TEST(step_sets_resume_and_is_step) {
    char reply[64];
    int resumeRequested = 0, resumeIsStep = 0;
    int n;
    setup_session();
    n = GDBHandlePacket(&g_sess, "s", 1, reply, sizeof(reply),
                        &resumeRequested, &resumeIsStep);
    ASSERT_EQ(n, 0);
    ASSERT_EQ(resumeRequested, 1);
    ASSERT_EQ(resumeIsStep, 1);
}

TEST(vcont_query_lists_continue_and_step) {
    char reply[64]; int n;
    setup_session();
    n = TF_Handle(&g_sess, "vCont?", 6, reply, sizeof(reply));
    ASSERT(n > 0);
    reply[n] = '\0';
    ASSERT_STR(reply, "vCont;c;s");
}

TEST(vcont_step_action_sets_resume_step) {
    char reply[64];
    int resumeRequested = 0, resumeIsStep = 0;
    int n;
    setup_session();
    n = GDBHandlePacket(&g_sess, "vCont;s:1", 9, reply, sizeof(reply),
                        &resumeRequested, &resumeIsStep);
    ASSERT_EQ(n, 0);
    ASSERT_EQ(resumeRequested, 1);
    ASSERT_EQ(resumeIsStep, 1);
}

TEST(vcont_continue_action_sets_resume_not_step) {
    char reply[64];
    int resumeRequested = 0, resumeIsStep = 1;
    int n;
    setup_session();
    n = GDBHandlePacket(&g_sess, "vCont;c", 7, reply, sizeof(reply),
                        &resumeRequested, &resumeIsStep);
    ASSERT_EQ(n, 0);
    ASSERT_EQ(resumeRequested, 1);
    ASSERT_EQ(resumeIsStep, 0);
}

static int fake_monitor_cmd(void *user, const char *cmd, char *out, int outMax) {
    (void)user;
    return snprintf(out, (size_t)outMax, "you said: %s", cmd);
}

TEST(qrcmd_decodes_hex_and_encodes_reply) {
    /* "hi" hex-encoded is "6869" */
    char reply[128]; int n;
    static struct GDBTargetOps ops;
    memset(&ops, 0, sizeof(ops));
    ops.monitorCmd = fake_monitor_cmd;
    GDBSessionInit(&g_sess, &ops, NULL);
    n = TF_Handle(&g_sess, "qRcmd,6869", 10, reply, sizeof(reply));
    ASSERT(n > 0);
    reply[n] = '\0';
    /* Reply is hex-encoded text "you said: hi" -- spot-check it decodes
     * to something containing "hi" without writing a second hex decoder
     * in the test: just check the known hex prefix for "you said: h". */
    ASSERT(strncmp(reply, "796f7520736169643a2068", 22) == 0);
}

TEST(qrcmd_without_monitor_op_is_unsupported) {
    char reply[128];
    setup_session();
    ASSERT_EQ(TF_Handle(&g_sess, "qRcmd,6869", 10, reply, sizeof(reply)), 0);
}

TEST(thread_roster_is_fixed_at_three) {
    char reply[64]; int n;
    setup_session();
    n = TF_Handle(&g_sess, "qfThreadInfo", 12, reply, sizeof(reply));
    ASSERT(n > 0);
    reply[n] = '\0';
    ASSERT_STR(reply, "m1,2,3");
    n = TF_Handle(&g_sess, "qsThreadInfo", 12, reply, sizeof(reply));
    reply[n] = '\0';
    ASSERT_STR(reply, "l");
}

TEST(hg_selects_thread_and_qc_reports_it) {
    char reply[64]; int n;
    setup_session();
    n = TF_Handle(&g_sess, "Hg2", 3, reply, sizeof(reply));
    reply[n] = '\0';
    ASSERT_STR(reply, "OK");
    n = TF_Handle(&g_sess, "qC", 2, reply, sizeof(reply));
    reply[n] = '\0';
    ASSERT_STR(reply, "QC2");
}

int main(void) {
    int total_fail = 0;

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
    RUN(parse_hex_u32_reads_value);
    RUN(parse_hex_u32_rejects_non_hex);
    RUN(read_memory_returns_hex);
    RUN(read_memory_past_end_is_refused);
    RUN(read_memory_wild_address_is_refused);
    RUN(read_memory_absurd_length_is_refused);
    RUN(read_memory_without_target_op_is_unsupported);
    RUN(read_registers_returns_144_hex_chars);
    RUN(read_registers_without_target_op_is_unsupported);
    total_fail += REPORT();

    SUITE("gdbstub phase 2: writes, breakpoints, resume, monitor, threads");
    RUN(write_registers_calls_ops_and_replies_ok);
    RUN(write_registers_without_target_op_is_unsupported);
    RUN(write_memory_writes_and_replies_ok);
    RUN(write_memory_length_mismatch_is_refused);
    RUN(insert_breakpoint_replies_ok);
    RUN(insert_breakpoint_out_of_resources_is_e01);
    RUN(remove_breakpoint_replies_ok);
    RUN(watchpoint_type_reaches_ops_too);
    RUN(breakpoint_without_target_op_is_unsupported);
    RUN(continue_sets_resume_and_suppresses_reply);
    RUN(step_sets_resume_and_is_step);
    RUN(vcont_query_lists_continue_and_step);
    RUN(vcont_step_action_sets_resume_step);
    RUN(vcont_continue_action_sets_resume_not_step);
    RUN(qrcmd_decodes_hex_and_encodes_reply);
    RUN(qrcmd_without_monitor_op_is_unsupported);
    RUN(thread_roster_is_fixed_at_three);
    RUN(hg_selects_thread_and_qc_reports_it);
    total_fail += REPORT();

    return total_fail;
}
