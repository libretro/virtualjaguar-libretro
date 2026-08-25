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
uint32_t JLinkNowMs(void) { return 1000; }
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

int main(void)
{
   printf("test_voicechat\n");
   test_mulaw_roundtrip();
   test_pkt_roundtrip();
   test_pkt_rejects();
   test_jitter();
   test_energy_and_mix();
   if (failures)
   {
      printf("%d FAILURES\n", failures);
      return 1;
   }
   printf("ALL PASS\n");
   return 0;
}
