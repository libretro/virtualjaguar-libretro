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

#include <libretro.h>
#include "../../src/debug/gdbstub.h"

/* ------------------------------------------------------------------ */
/* Minimal test runner (copied verbatim from test/test_boot_config.c) */
/* ------------------------------------------------------------------ */

static int tf_pass = 0, tf_fail = 0, tf_skip = 0;
/* gdbsock.c logs a refused non-private peer (#652) through src/core/log.h.
 * NULL makes VJ_LOG fall back to its vj_log_stderr path, so the warning
 * still works standalone instead of the include becoming a link error --
 * same stub test_blitter_mmio.c and test_titlehook.c already use. */
retro_log_printf_t vj_log_cb = NULL;

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

TEST(thread_roster_is_68k_only) {
    /* #936: gdb applies ONE tdesc (the 68K's) to every thread it knows, so
     * enumerating the GPU/DSP made `info threads` fail on a 72 vs 136
     * byte 'g'.  Hg2/Hg3 still work for raw clients (next test). */
    char reply[64]; int n;
    setup_session();
    n = TF_Handle(&g_sess, "qfThreadInfo", 12, reply, sizeof(reply));
    ASSERT(n > 0);
    reply[n] = '\0';
    ASSERT_STR(reply, "m1");
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

/* ------------------------------------------------------------------ */
/* Issue #936: stock-gdb compatibility                                  */
/* ------------------------------------------------------------------ */

/* Reads the whole qXfer document the way gdb does: repeated
 * qXfer:features:read:target.xml:<off>,<len> until an 'l' reply. */
static int read_target_xml(char *xml, int xmlMax, int chunk) {
    char pay[64], reply[GDB_PACKET_MAX];
    int off = 0, n;
    for (;;) {
        int pl = sprintf(pay, "qXfer:features:read:target.xml:%x,%x", off, chunk);
        n = TF_Handle(&g_sess, pay, pl, reply, sizeof(reply));
        if (n <= 0) return -1;
        if (off + n - 1 >= xmlMax) return -2;
        memcpy(xml + off, reply + 1, (size_t)(n - 1));
        off += n - 1;
        if (reply[0] == 'l') break;
        if (reply[0] != 'm') return -3;
    }
    xml[off] = '\0';
    return off;
}

TEST(m68k_target_xml_is_served_without_target_op) {
    /* The advertised qXfer:features:read used to answer the 68K with an
     * empty packet, which made stock gdb abort with "conflicting enabled
     * responses".  The 68K ops carry no targetXML and must still get the
     * engine's description. */
    static char xml[4096];
    int n;
    setup_session();
    n = read_target_xml(xml, (int)sizeof(xml), 0x100);
    ASSERT(n > 0);
    ASSERT(strstr(xml, "<architecture>m68k</architecture>") != NULL);
    ASSERT(strstr(xml, "org.gnu.gdb.m68k.core") != NULL);
    /* No FP feature: the 'g' reply is 18 registers. */
    ASSERT(strstr(xml, "fp0") == NULL);
    /* One-shot read in a single large request too. */
    n = read_target_xml(xml, (int)sizeof(xml), 0xffb);
    ASSERT(n > 0);
}

TEST(m68k_target_xml_matches_g_layout_order) {
    /* d0-d7, a0-a5, fp, sp, ps, pc in that order == readRegisters'
     * D0-D7, A0-A7, SR, PC; 18 registers, all 32-bit. */
    static const char *want[] = { "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
        "a0", "a1", "a2", "a3", "a4", "a5", "fp", "sp", "ps", "pc" };
    static char xml[4096];
    const char *p;
    char needle[48];
    int i, n;
    setup_session();
    n = read_target_xml(xml, (int)sizeof(xml), 0xffb);
    ASSERT(n > 0);
    p = xml;
    for (i = 0; i < 18; i++) {
        sprintf(needle, "<reg name=\"%s\" bitsize=\"32\"", want[i]);
        p = strstr(p, needle);
        if (!p) FAIL("register %s missing or out of order", want[i]);
        p += strlen(needle);
    }
    ASSERT(strstr(p, "<reg name=") == NULL);   /* nothing after pc */
}

TEST(m68k_target_xml_has_no_rsp_special_characters) {
    /* qXfer data is binary-escaped on the wire; keeping $ # } * out of
     * the document means it needs no escaping at all. */
    static char xml[4096];
    int n;
    setup_session();
    n = read_target_xml(xml, (int)sizeof(xml), 0xffb);
    ASSERT(n > 0);
    ASSERT(strpbrk(xml, "$#}*") == NULL);
}

TEST(detach_replies_ok_and_requests_session_end) {
    char reply[64]; int n;
    setup_session();
    ASSERT_EQ(g_sess.detachRequested, 0);
    n = TF_Handle(&g_sess, "D", 1, reply, sizeof(reply));
    reply[n] = '\0';
    ASSERT_STR(reply, "OK");
    ASSERT_EQ(g_sess.detachRequested, GDB_END_REPLY);
}

TEST(detach_with_pid_form_is_accepted) {
    char reply[64]; int n;
    setup_session();
    n = TF_Handle(&g_sess, "D;1", 3, reply, sizeof(reply));
    reply[n] = '\0';
    ASSERT_STR(reply, "OK");
    ASSERT_EQ(g_sess.detachRequested, GDB_END_REPLY);
}

TEST(vkill_replies_ok_and_requests_session_end) {
    char reply[64]; int n;
    setup_session();
    n = TF_Handle(&g_sess, "vKill;a410", 10, reply, sizeof(reply));
    reply[n] = '\0';
    ASSERT_STR(reply, "OK");
    ASSERT_EQ(g_sess.detachRequested, GDB_END_REPLY);
}

TEST(kill_requests_session_end_with_no_reply) {
    char reply[64];
    setup_session();
    ASSERT_EQ(TF_Handle(&g_sess, "k", 1, reply, sizeof(reply)), 0);
    ASSERT_EQ(g_sess.detachRequested, GDB_END_NOREPLY);
}

TEST(other_d_or_v_packets_do_not_detach) {
    char reply[64];
    setup_session();
    TF_Handle(&g_sess, "vMustReplyEmpty", 15, reply, sizeof(reply));
    TF_Handle(&g_sess, "vKillX", 6, reply, sizeof(reply));
    TF_Handle(&g_sess, "Dx", 2, reply, sizeof(reply));
    ASSERT_EQ(g_sess.detachRequested, 0);
}

/* Long monitor output: the engine used to give the target a 512-byte
 * buffer and one hex reply.  It now hands the target GDB_MONITOR_MAX and
 * streams the text as 'O' packets of GDB_MONITOR_CHUNK bytes, then OK. */
static int fake_long_monitor(void *user, const char *cmd, char *out, int outMax) {
    int i, n = 0;
    (void)user; (void)cmd;
    for (i = 0; i < 700; i++)            /* ~7 KB of "line NNN\n" */
        n += snprintf(out + n, (size_t)(outMax - n), "line %03d\n", i);
    return n;
}

static int fake_oversized_monitor(void *user, const char *cmd, char *out, int outMax) {
    (void)user; (void)cmd;
    memset(out, 'x', (size_t)outMax);
    return outMax + 5000;                /* snprintf-style "would have written" */
}

static char o_text[GDB_MONITOR_MAX + 64];
static int  o_text_len, o_packets, o_max_payload;

static int collect_o_packet(void *ctx, const char *payload, int len) {
    int i;
    (void)ctx;
    if (payload[0] != 'O') return -1;
    if (len > o_max_payload) o_max_payload = len;
    for (i = 1; i + 1 < len + 1 && o_text_len < (int)sizeof(o_text) - 1; i += 2) {
        unsigned int v;
        if (GDBParseHexU32(payload + i, 2, &v) < 0) return -1;
        o_text[o_text_len++] = (char)v;
    }
    o_packets++;
    return len;
}

static void setup_long_monitor(int (*fn)(void *, const char *, char *, int)) {
    static struct GDBTargetOps ops;
    memset(&ops, 0, sizeof(ops));
    ops.monitorCmd = fn;
    GDBSessionInit(&g_sess, &ops, NULL);
    g_sess.sendPacket = collect_o_packet;
    o_text_len = o_packets = o_max_payload = 0;
}

TEST(long_monitor_output_streams_as_o_packets) {
    char reply[64]; int n;
    setup_long_monitor(fake_long_monitor);
    n = TF_Handle(&g_sess, "qRcmd,6869", 10, reply, sizeof(reply));
    reply[n] = '\0';
    ASSERT_STR(reply, "OK");
    ASSERT(o_packets > 1);
    ASSERT_EQ(o_text_len, 700 * 9);                /* nothing lost */
    o_text[o_text_len] = '\0';
    ASSERT(strncmp(o_text, "line 000\n", 9) == 0);
    ASSERT(strstr(o_text, "line 699\n") != NULL);   /* the TAIL survives */
    ASSERT(o_max_payload <= GDB_MONITOR_CHUNK * 2 + 1);
}

TEST(oversized_monitor_return_is_clamped_not_overrun) {
    char reply[64]; int n;
    setup_long_monitor(fake_oversized_monitor);
    n = TF_Handle(&g_sess, "qRcmd,6869", 10, reply, sizeof(reply));
    reply[n] = '\0';
    ASSERT_STR(reply, "OK");
    ASSERT_EQ(o_text_len, GDB_MONITOR_MAX - 1);
}

TEST(monitor_without_sender_truncates_to_one_reply) {
    char reply[256]; int n;
    setup_long_monitor(fake_long_monitor);
    g_sess.sendPacket = NULL;
    n = TF_Handle(&g_sess, "qRcmd,6869", 10, reply, sizeof(reply));
    ASSERT(n > 0 && n <= (int)sizeof(reply));
    ASSERT(strncmp(reply, "6c696e652030303", 15) == 0);   /* "line 00" */
}


/* Bind scope (issue #652).  The DEFAULT is the whole point: an open GDB
 * stub is unauthenticated read/write of the emulated machine, so the safe
 * value must be what you get from doing nothing, from an absent option,
 * and from any value we do not recognise.  These four assertions are what
 * stop that default drifting in a later refactor. */
TEST(bind_mode_defaults_to_loopback) {
    /* No setter call -- a frontend that never heard of this option, or a
     * test, must still get loopback. */
    ASSERT_EQ(GDBSockGetBindMode(), GDB_BIND_LOOPBACK);
}

TEST(bind_mode_lan_is_settable) {
    GDBSockSetBindMode(GDB_BIND_LAN);
    ASSERT_EQ(GDBSockGetBindMode(), GDB_BIND_LAN);
    GDBSockSetBindMode(GDB_BIND_LOOPBACK);
}

TEST(bind_mode_unknown_value_fails_closed) {
    GDBSockSetBindMode(GDB_BIND_LAN);
    GDBSockSetBindMode(99);          /* garbage / future value */
    ASSERT_EQ(GDBSockGetBindMode(), GDB_BIND_LOOPBACK);
}

TEST(bind_mode_returns_to_loopback) {
    GDBSockSetBindMode(GDB_BIND_LAN);
    GDBSockSetBindMode(GDB_BIND_LOOPBACK);
    ASSERT_EQ(GDBSockGetBindMode(), GDB_BIND_LOOPBACK);
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
    RUN(thread_roster_is_68k_only);
    RUN(hg_selects_thread_and_qc_reports_it);
    RUN(m68k_target_xml_is_served_without_target_op);
    RUN(m68k_target_xml_matches_g_layout_order);
    RUN(m68k_target_xml_has_no_rsp_special_characters);
    RUN(detach_replies_ok_and_requests_session_end);
    RUN(detach_with_pid_form_is_accepted);
    RUN(vkill_replies_ok_and_requests_session_end);
    RUN(kill_requests_session_end_with_no_reply);
    RUN(other_d_or_v_packets_do_not_detach);
    RUN(long_monitor_output_streams_as_o_packets);
    RUN(oversized_monitor_return_is_clamped_not_overrun);
    RUN(monitor_without_sender_truncates_to_one_reply);
    RUN(bind_mode_defaults_to_loopback);
    RUN(bind_mode_lan_is_settable);
    RUN(bind_mode_unknown_value_fails_closed);
    RUN(bind_mode_returns_to_loopback);
    total_fail += REPORT();

    return total_fail;
}
