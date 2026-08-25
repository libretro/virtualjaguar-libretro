/* test/test_voicechat.c — pure-logic unit tests for voicechat (#485).
 *
 * Compiles voicechat.c against stubs for the jlink/discovery seams so
 * codec, framing, jitter, VAD energy, and mix saturation need no sockets.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "voicechat.h"

/* ---- Stubs for symbols voicechat.c pulls from jlink / discover ------ */

int JLinkMode(void) { return 0; }
int JLinkDiscActive(void) { return 0; }
int JLinkDiscPort(void) { return 42170; }
const char *JLinkGetTCPHost(void) { return "127.0.0.1"; }
static uint32_t g_now_ms = 1000;
uint32_t JLinkNowMs(void) { return g_now_ms; }
int JLinkDiscSendTo(const uint8_t *buf, size_t len,
                    const char *to_addr, int to_port)
{
   (void)buf; (void)len; (void)to_addr; (void)to_port;
   return 0;
}

static int failures = 0;

static void check(int cond, const char *what)
{
   if (!cond) { printf("FAIL: %s\n", what); failures++; }
   else        printf("  ok: %s\n", what);
}

static void test_mulaw_roundtrip(void)
{
   static const int16_t samples[] = {
      0, 100, -100, 1000, -1000, 8000, -8000, 16000, -16000, 30000, -30000
   };
   unsigned i;

   for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++)
   {
      uint8_t e = VoiceChatMuLawEncode(samples[i]);
      int16_t d = VoiceChatMuLawDecode(e);
      int err = (int)d - (int)samples[i];
      if (err < 0)
         err = -err;
      /* G.711 is lossy; allow ~2% of full scale for large samples, and
       * a floor for near-zero values. */
      check(err < 600 || err < (int)(abs(samples[i]) / 8 + 1),
            "mulaw round-trip within bound");
   }
}

static void test_pkt_roundtrip(void)
{
   uint8_t mulaw[VC_FRAME_SAMPLES];
   uint8_t out[VC_FRAME_SAMPLES];
   uint8_t pkt[VC_PKT_LEN];
   uint8_t flags = 0;
   uint16_t seq = 0;
   uint32_t sid = 0;
   size_t n;
   unsigned i;

   for (i = 0; i < VC_FRAME_SAMPLES; i++)
      mulaw[i] = (uint8_t)(i & 0xFF);

   n = VoiceChatEncodePkt(pkt, sizeof(pkt), VC_FLAG_KEEPALIVE, 0x1234,
                          0xA1B2C3D4u, mulaw);
   check(n == VC_PKT_LEN, "encode writes VC_PKT_LEN");
   check(VoiceChatDecodePkt(pkt, n, &flags, &seq, &sid, out) == 1,
         "decode accepts own packet");
   check(flags == VC_FLAG_KEEPALIVE, "flags survive");
   check(seq == 0x1234, "seq survives");
   check(sid == 0xA1B2C3D4u, "senderId survives");
   check(memcmp(mulaw, out, VC_FRAME_SAMPLES) == 0, "payload survives");
}

static void test_pkt_rejects(void)
{
   uint8_t mulaw[VC_FRAME_SAMPLES];
   uint8_t pkt[VC_PKT_LEN];
   uint8_t flags;
   uint16_t seq;
   uint32_t sid;

   memset(mulaw, 0, sizeof(mulaw));
   VoiceChatEncodePkt(pkt, sizeof(pkt), 0, 1, 2, mulaw);

   check(VoiceChatDecodePkt(pkt, VC_PKT_LEN - 1, &flags, &seq, &sid, mulaw) == 0,
         "truncated rejected");
   check(VoiceChatDecodePkt(pkt, VC_PKT_LEN + 1, &flags, &seq, &sid, mulaw) == 0,
         "oversized rejected");
   pkt[0] = 'X';
   check(VoiceChatDecodePkt(pkt, VC_PKT_LEN, &flags, &seq, &sid, mulaw) == 0,
         "wrong magic rejected");
   pkt[0] = VC_MAGIC_0;
   pkt[4] = 99;
   check(VoiceChatDecodePkt(pkt, VC_PKT_LEN, &flags, &seq, &sid, mulaw) == 0,
         "wrong version rejected");
}

static void test_jitter(void)
{
   int16_t frame[VC_FRAME_SAMPLES];
   int16_t s;
   unsigned i;

   VoiceChatReset();
   VoiceChatSetEnabled(1);

   for (i = 0; i < VC_FRAME_SAMPLES; i++)
      frame[i] = (int16_t)(i + 1);

   VoiceChatJitterPush(frame, 0);
   check(VoiceChatJitterCount() == VC_FRAME_SAMPLES, "jitter holds one frame");
   check(VoiceChatJitterPop(&s) == 1 && s == 1, "pop first sample");
   check(VoiceChatJitterCount() == VC_FRAME_SAMPLES - 1, "count decrements");

   /* Push next seq; should append without gap fill. */
   for (i = 0; i < VC_FRAME_SAMPLES; i++)
      frame[i] = (int16_t)(1000 + i);
   VoiceChatJitterPush(frame, 1);
   check(VoiceChatJitterCount() == VC_FRAME_SAMPLES * 2 - 1,
         "second frame appended");

   /* Skip ahead to force gap fill (seq 3 after expect 2). */
   VoiceChatReset();
   VoiceChatSetEnabled(1);
   VoiceChatJitterPush(frame, 0);
   while (VoiceChatJitterPop(&s))
      ;
   VoiceChatJitterPush(frame, 2); /* gap of one frame of silence */
   check(VoiceChatJitterCount() == VC_FRAME_SAMPLES * 2,
         "gap fill inserts silence frame");
}

static void test_keepalive_seq_continuity(void)
{
   /* Models the RX side of the keepalive fix: keepalives are dropped
    * before VoiceChatJitterPush, so consecutive voice frames must arrive
    * with unbroken seq (TX must not consume seq for keepalives).  Pushing
    * 0 then 1 must not insert a silence gap. */
   int16_t frame[VC_FRAME_SAMPLES];
   int16_t s;
   unsigned i;
   unsigned silence = 0;

   VoiceChatReset();
   VoiceChatSetEnabled(1);
   for (i = 0; i < VC_FRAME_SAMPLES; i++)
      frame[i] = 1000;
   VoiceChatJitterPush(frame, 0);
   /* A keepalive would be dropped here — no JitterPush. */
   for (i = 0; i < VC_FRAME_SAMPLES; i++)
      frame[i] = 2000;
   VoiceChatJitterPush(frame, 1);

   check(VoiceChatJitterCount() == VC_FRAME_SAMPLES * 2,
         "keepalive does not open a voice seq gap");
   while (VoiceChatJitterPop(&s))
   {
      if (s == 0)
         silence++;
   }
   check(silence == 0, "no silence inserted between contiguous voice seqs");
}

static void test_energy_and_mix(void)
{
   int16_t loud[VC_FRAME_SAMPLES];
   int16_t quiet[VC_FRAME_SAMPLES];
   uint16_t buf[1600]; /* 800 stereo pairs */
   unsigned i;
   int clipped;

   for (i = 0; i < VC_FRAME_SAMPLES; i++)
   {
      loud[i] = 8000;
      quiet[i] = 10;
   }
   check(VoiceChatFrameEnergy(loud, VC_FRAME_SAMPLES) > 7000,
         "loud energy high");
   check(VoiceChatFrameEnergy(quiet, VC_FRAME_SAMPLES) < 50,
         "quiet energy low");

   VoiceChatReset();
   VoiceChatSetEnabled(1);
   VoiceChatSetVolume(100);
   VoiceChatJitterPush(loud, 0);

   /* Fill buffer near positive full scale so mix must clamp. */
   for (i = 0; i < 1600; i++)
      buf[i] = (uint16_t)30000; /* as int16_t = 30000 */

   VoiceChatMixInto(buf, 800);
   clipped = 0;
   for (i = 0; i < 1600; i++)
   {
      int16_t v = (int16_t)buf[i];
      if (v > 32767 || v < -32768) /* unreachable for int16_t store */
         clipped = -1;
      if (v == 32767)
         clipped = 1;
   }
   check(clipped == 1, "saturates near full scale without wrap");
}

static void test_multi_speaker(void)
{
   int16_t frame_a[VC_FRAME_SAMPLES];
   int16_t frame_b[VC_FRAME_SAMPLES];
   int16_t frame_c[VC_FRAME_SAMPLES];
   int16_t frame_d[VC_FRAME_SAMPLES];
   uint16_t buf[1600];
   unsigned i;
   long sum;

   for (i = 0; i < VC_FRAME_SAMPLES; i++)
   {
      frame_a[i] = 1000;
      frame_b[i] = 2000;
      frame_c[i] = 3000;
      frame_d[i] = 4000;
   }

   VoiceChatReset();
   VoiceChatSetEnabled(1);
   VoiceChatSetVolume(100);

   /* Independent gap-fill: A seq 0 then 2; B seq 0 continuous. */
   VoiceChatJitterPushFrom(0x11111111u, frame_a, 0);
   VoiceChatJitterPushFrom(0x22222222u, frame_b, 0);
   VoiceChatJitterPushFrom(0x11111111u, frame_a, 2); /* gap of 1 */
   check(VoiceChatActiveSpeakers() == 2, "two speakers claimed");
   check(VoiceChatJitterCount() == VC_FRAME_SAMPLES * 4,
         "A gap-fill + B frame counted (3+1 frames)");

   VoiceChatReset();
   VoiceChatSetEnabled(1);
   VoiceChatSetVolume(100);
   VoiceChatJitterPushFrom(1, frame_a, 0);
   VoiceChatJitterPushFrom(2, frame_b, 0);
   VoiceChatJitterPushFrom(3, frame_c, 0);
   check(VoiceChatActiveSpeakers() == 3, "three speakers at cap");

   for (i = 0; i < 1600; i++)
      buf[i] = 0;
   VoiceChatMixInto(buf, 800);
   sum = 0;
   for (i = 0; i < 1600; i++)
      sum += (int16_t)buf[i];
   check(sum > 0, "three speakers mix into output");

   /* Fourth speaker reclaims the least-recently-active slot. Advance
    * the stub clock between claims so lastMs differs; sender 10 is
    * oldest and must be the one evicted. */
   VoiceChatReset();
   VoiceChatSetEnabled(1);
   g_now_ms = 1000;
   VoiceChatJitterPushFrom(10, frame_a, 0);
   g_now_ms = 2000;
   VoiceChatJitterPushFrom(20, frame_b, 0);
   g_now_ms = 3000;
   VoiceChatJitterPushFrom(30, frame_c, 0);
   g_now_ms = 4000;
   VoiceChatJitterPushFrom(40, frame_d, 0);
   check(VoiceChatActiveSpeakers() == 3,
         "fourth speaker reclaims; still at cap of 3");
   check(!VoiceChatHasSpeaker(10), "oldest speaker (10) was reclaimed");
   check(VoiceChatHasSpeaker(20) && VoiceChatHasSpeaker(30)
             && VoiceChatHasSpeaker(40),
         "newer speakers 20/30/40 kept");
}

int main(void)
{
   printf("test_voicechat\n");
   test_mulaw_roundtrip();
   test_pkt_roundtrip();
   test_pkt_rejects();
   test_jitter();
   test_keepalive_seq_continuity();
   test_energy_and_mix();
   test_multi_speaker();
   if (failures)
   {
      printf("%d FAILURES\n", failures);
      return 1;
   }
   printf("ALL PASS\n");
   return 0;
}
