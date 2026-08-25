/* voicechat.c — host-side voice over the discovery UDP socket (#485).
 *
 * Never touches jlinkRing / UART / savestate. See docs/voice-chat-design.md.
 */
#include "voicechat.h"
#include "jlink.h"
#include "jlink_discover.h"

#include <string.h>

#define VC_JITTER_FRAMES   8
#define VC_JITTER_SAMPLES  (VC_JITTER_FRAMES * VC_FRAME_SAMPLES)
#define VC_KEEPALIVE_MS    500
#define VC_VAD_HANG_FRAMES 8
#define VC_MIC_PULL_MAX    320   /* up to 40 ms of 8 kHz per video frame */

typedef struct
{
   uint32_t senderId;
   int      inUse;
   uint32_t lastMs;
   int16_t  samples[VC_JITTER_SAMPLES];
   unsigned head;
   unsigned count;
   uint16_t expectSeq;
   int      haveExpect;
} VoiceChatStream;

static int vcEnabled = 0;
static int vcGate = VC_GATE_OPEN_MIC;
static unsigned vcPttKey = 0;
static unsigned vcVolume = 50;
static unsigned vcVadThresh = 400;
static int vcMonitor = 0;
static VoiceChatMicReadFn vcMicRead = NULL;
static VoiceChatNetSendFn vcNetSend = NULL;

static uint32_t vcSenderId = 0;
static int vcSenderIdReady = 0;
static uint16_t vcTxSeq = 0;
static char vcPeerAddr[VC_PEER_ADDR_MAX];
static uint32_t vcLastKeepaliveMs = 0;
static uint32_t vcLastVoiceMs = 0;

static VoiceChatStream vcStreams[VC_MAX_SPEAKERS];

static int16_t vcMonRing[VC_FRAME_SAMPLES];
static unsigned vcMonCount = 0;

static int vcVadOpen = 0;
static unsigned vcVadHang = 0;

static int16_t vcMicAccum[VC_FRAME_SAMPLES];
static unsigned vcMicAccumN = 0;

/* ---- µ-law (ITU-T G.711) --------------------------------------------- */

uint8_t VoiceChatMuLawEncode(int16_t pcm)
{
   int sign, exp, mantissa;
   unsigned mag;
   unsigned mask;
   int sample = pcm;

   sign = (sample >> 8) & 0x80;
   if (sign)
      sample = -sample;
   if (sample > 32635)
      sample = 32635;
   sample += 0x84;
   mag = (unsigned)sample;
   mask = 0x4000;
   exp = 7;
   while (exp > 0)
   {
      if (mag & mask)
         break;
      mask >>= 1;
      exp--;
   }
   mantissa = (int)((mag >> (exp + 3)) & 0x0F);
   return (uint8_t)(~(sign | (exp << 4) | mantissa));
}

int16_t VoiceChatMuLawDecode(uint8_t mulaw)
{
   int sign, exp, data;
   int sample;

   mulaw = (uint8_t)~mulaw;
   sign = mulaw & 0x80;
   exp = (mulaw >> 4) & 0x07;
   data = mulaw & 0x0F;
   sample = ((data << 3) + 0x84) << exp;
   sample -= 0x84;
   return (int16_t)(sign ? -sample : sample);
}

/* ---- Packet codec ---------------------------------------------------- */

size_t VoiceChatEncodePkt(uint8_t *buf, size_t cap,
                          uint8_t flags, uint16_t seq, uint32_t senderId,
                          const uint8_t mulaw[VC_FRAME_SAMPLES])
{
   if (!buf || cap < VC_PKT_LEN || !mulaw)
      return 0;
   buf[0] = VC_MAGIC_0;
   buf[1] = VC_MAGIC_1;
   buf[2] = VC_MAGIC_2;
   buf[3] = VC_MAGIC_3;
   buf[4] = VC_VERSION;
   buf[5] = flags;
   buf[6] = (uint8_t)((seq >> 8) & 0xFF);
   buf[7] = (uint8_t)(seq & 0xFF);
   buf[8] = (uint8_t)((senderId >> 24) & 0xFF);
   buf[9] = (uint8_t)((senderId >> 16) & 0xFF);
   buf[10] = (uint8_t)((senderId >> 8) & 0xFF);
   buf[11] = (uint8_t)(senderId & 0xFF);
   memcpy(buf + VC_HDR_LEN, mulaw, VC_FRAME_SAMPLES);
   return VC_PKT_LEN;
}

int VoiceChatDecodePkt(const uint8_t *buf, size_t len,
                       uint8_t *flags, uint16_t *seq, uint32_t *senderId,
                       uint8_t mulaw[VC_FRAME_SAMPLES])
{
   if (!buf || len != VC_PKT_LEN)
      return 0;
   if (buf[0] != VC_MAGIC_0 || buf[1] != VC_MAGIC_1
       || buf[2] != VC_MAGIC_2 || buf[3] != VC_MAGIC_3)
      return 0;
   if (buf[4] != VC_VERSION)
      return 0;
   if (flags)
      *flags = buf[5];
   if (seq)
      *seq = (uint16_t)(((uint16_t)buf[6] << 8) | buf[7]);
   if (senderId)
      *senderId = ((uint32_t)buf[8] << 24) | ((uint32_t)buf[9] << 16)
                | ((uint32_t)buf[10] << 8) | (uint32_t)buf[11];
   if (mulaw)
      memcpy(mulaw, buf + VC_HDR_LEN, VC_FRAME_SAMPLES);
   return 1;
}

unsigned VoiceChatFrameEnergy(const int16_t *pcm, unsigned n)
{
   unsigned i;
   unsigned long sum = 0;

   if (!pcm || n == 0)
      return 0;
   for (i = 0; i < n; i++)
   {
      int v = pcm[i];
      if (v < 0)
         v = -v;
      sum += (unsigned)v;
   }
   return (unsigned)(sum / n);
}

/* ---- Config ---------------------------------------------------------- */

/* Local xorshift32 — never touch the process-global C PRNG (srand/rand),
 * which the rest of the core / frontend may use for anything observable. */
static uint32_t VoiceChatEnsureSenderId(void)
{
   if (!vcSenderIdReady)
   {
      uint32_t x = JLinkNowMs() ^ (uint32_t)(size_t)&vcSenderId;
      if (x == 0)
         x = 0xA5A5A5A5u;
      x ^= x << 13;
      x ^= x >> 17;
      x ^= x << 5;
      vcSenderId = x ? x : 1u;
      vcSenderIdReady = 1;
   }
   return vcSenderId;
}

void VoiceChatReset(void)
{
   unsigned i;

   vcEnabled = 0;
   vcGate = VC_GATE_OPEN_MIC;
   vcPttKey = 0;
   vcVolume = 50;
   vcVadThresh = 400;
   vcMonitor = 0;
   vcMicRead = NULL;
   vcNetSend = NULL;
   vcSenderId = 0;
   vcSenderIdReady = 0;
   vcTxSeq = 0;
   vcPeerAddr[0] = '\0';
   vcLastKeepaliveMs = 0;
   vcLastVoiceMs = 0;
   vcMonCount = 0;
   vcVadOpen = 0;
   vcVadHang = 0;
   vcMicAccumN = 0;
   memset(vcStreams, 0, sizeof(vcStreams));
   memset(vcMonRing, 0, sizeof(vcMonRing));
   memset(vcMicAccum, 0, sizeof(vcMicAccum));
   for (i = 0; i < VC_MAX_SPEAKERS; i++)
      vcStreams[i].inUse = 0;
}

void VoiceChatSetEnabled(int enabled)
{
   unsigned i;

   vcEnabled = enabled ? 1 : 0;
   if (!vcEnabled)
   {
      for (i = 0; i < VC_MAX_SPEAKERS; i++)
      {
         vcStreams[i].inUse = 0;
         vcStreams[i].head = 0;
         vcStreams[i].count = 0;
         vcStreams[i].haveExpect = 0;
      }
      vcMicAccumN = 0;
      vcMonCount = 0;
      vcVadOpen = 0;
      vcVadHang = 0;
   }
}

int VoiceChatEnabled(void)
{
   return vcEnabled;
}

void VoiceChatSetGate(int gate)
{
   vcGate = (gate == VC_GATE_PTT) ? VC_GATE_PTT : VC_GATE_OPEN_MIC;
}

void VoiceChatSetPTTKey(unsigned retrok)
{
   vcPttKey = retrok;
}

unsigned VoiceChatPTTKey(void)
{
   return vcPttKey;
}

void VoiceChatSetVolume(unsigned pct)
{
   if (pct > 100)
      pct = 100;
   vcVolume = pct;
}

void VoiceChatSetVadThreshold(unsigned thresh)
{
   vcVadThresh = thresh;
}

void VoiceChatSetMonitor(int enabled)
{
   vcMonitor = enabled ? 1 : 0;
}

void VoiceChatSetMicRead(VoiceChatMicReadFn fn)
{
   vcMicRead = fn;
}

void VoiceChatSetNetSend(VoiceChatNetSendFn fn)
{
   vcNetSend = fn;
}

void VoiceChatSetSenderId(uint32_t id)
{
   vcSenderId = id ? id : 1;
   vcSenderIdReady = 1;
}

void VoiceChatSetPeerAddr(const char *addr)
{
   if (!addr || !addr[0])
   {
      vcPeerAddr[0] = '\0';
      return;
   }
   strncpy(vcPeerAddr, addr, VC_PEER_ADDR_MAX - 1);
   vcPeerAddr[VC_PEER_ADDR_MAX - 1] = '\0';
}

const char *VoiceChatPeerAddr(void)
{
   return vcPeerAddr;
}

/* ---- Jitter buffer (per-sender, up to VC_MAX_SPEAKERS) ---------------- */

static VoiceChatStream *VoiceChatFindStream(uint32_t senderId, int claim)
{
   unsigned i;
   unsigned freeIdx;
   unsigned staleIdx;
   uint32_t staleMs;
   uint32_t now;

   freeIdx = VC_MAX_SPEAKERS;
   staleIdx = 0;
   staleMs = 0xFFFFFFFFu;
   now = JLinkNowMs();

   for (i = 0; i < VC_MAX_SPEAKERS; i++)
   {
      if (vcStreams[i].inUse && vcStreams[i].senderId == senderId)
      {
         vcStreams[i].lastMs = now;
         return &vcStreams[i];
      }
      if (!vcStreams[i].inUse && freeIdx == VC_MAX_SPEAKERS)
         freeIdx = i;
      if (vcStreams[i].inUse && vcStreams[i].lastMs < staleMs)
      {
         staleMs = vcStreams[i].lastMs;
         staleIdx = i;
      }
   }

   if (!claim)
      return NULL;

   if (freeIdx < VC_MAX_SPEAKERS)
      i = freeIdx;
   else
      i = staleIdx; /* reclaim least-recently-active */

   memset(&vcStreams[i], 0, sizeof(vcStreams[i]));
   vcStreams[i].inUse = 1;
   vcStreams[i].senderId = senderId;
   vcStreams[i].lastMs = now;
   return &vcStreams[i];
}

static void VoiceChatStreamPush(VoiceChatStream *st,
                                const int16_t pcm[VC_FRAME_SAMPLES],
                                uint16_t seq)
{
   unsigned i;

   if (!st || !pcm)
      return;

   if (st->haveExpect)
   {
      int16_t delta = (int16_t)(seq - st->expectSeq);
      if (delta < 0)
         return;
      while (delta > 0 && st->count + VC_FRAME_SAMPLES <= VC_JITTER_SAMPLES)
      {
         for (i = 0; i < VC_FRAME_SAMPLES; i++)
         {
            unsigned idx = (st->head + st->count) % VC_JITTER_SAMPLES;
            st->samples[idx] = 0;
            st->count++;
         }
         st->expectSeq++;
         delta--;
      }
   }

   if (st->count + VC_FRAME_SAMPLES > VC_JITTER_SAMPLES)
   {
      st->head = (st->head + VC_FRAME_SAMPLES) % VC_JITTER_SAMPLES;
      st->count -= VC_FRAME_SAMPLES;
   }

   for (i = 0; i < VC_FRAME_SAMPLES; i++)
   {
      unsigned idx = (st->head + st->count) % VC_JITTER_SAMPLES;
      st->samples[idx] = pcm[i];
      st->count++;
   }
   st->expectSeq = (uint16_t)(seq + 1);
   st->haveExpect = 1;
}

static int VoiceChatStreamPop(VoiceChatStream *st, int16_t *out)
{
   if (!st || !st->inUse || st->count == 0 || !out)
      return 0;
   *out = st->samples[st->head];
   st->head = (st->head + 1) % VC_JITTER_SAMPLES;
   st->count--;
   return 1;
}

void VoiceChatJitterPushFrom(uint32_t senderId,
                             const int16_t pcm[VC_FRAME_SAMPLES],
                             uint16_t seq)
{
   VoiceChatStream *st = VoiceChatFindStream(senderId, 1);
   VoiceChatStreamPush(st, pcm, seq);
}

void VoiceChatJitterPush(const int16_t pcm[VC_FRAME_SAMPLES], uint16_t seq)
{
   /* Default stream (senderId 0) for unit tests / single-peer helpers. */
   VoiceChatJitterPushFrom(0, pcm, seq);
}

int VoiceChatJitterPop(int16_t *out)
{
   VoiceChatStream *st = VoiceChatFindStream(0, 0);
   if (!st)
      return 0;
   return VoiceChatStreamPop(st, out);
}

unsigned VoiceChatJitterCount(void)
{
   unsigned i;
   unsigned total = 0;
   for (i = 0; i < VC_MAX_SPEAKERS; i++)
   {
      if (vcStreams[i].inUse)
         total += vcStreams[i].count;
   }
   return total;
}

unsigned VoiceChatActiveSpeakers(void)
{
   unsigned i;
   unsigned n = 0;
   for (i = 0; i < VC_MAX_SPEAKERS; i++)
   {
      if (vcStreams[i].inUse)
         n++;
   }
   return n;
}

/* ---- TX helpers ------------------------------------------------------ */

static void VoiceChatSendFrame(uint8_t flags, const int16_t pcm[VC_FRAME_SAMPLES])
{
   uint8_t mulaw[VC_FRAME_SAMPLES];
   uint8_t pkt[VC_PKT_LEN];
   size_t n;
   unsigned i;
   const char *peer;
   uint16_t seq;
   int isKeepalive;

   if (!pcm)
      return;
   for (i = 0; i < VC_FRAME_SAMPLES; i++)
      mulaw[i] = VoiceChatMuLawEncode(pcm[i]);

   /* Keepalives must NOT consume the voice sequence: the receiver drops
    * them before VoiceChatJitterPush, so advancing vcTxSeq here would
    * open a one-frame gap the jitter filler turns into a 20 ms silence
    * hole every keepalive interval during active speech. */
   isKeepalive = (flags & VC_FLAG_KEEPALIVE) ? 1 : 0;
   seq = vcTxSeq;
   if (!isKeepalive)
      vcTxSeq++;

   n = VoiceChatEncodePkt(pkt, sizeof(pkt), flags, seq,
                          VoiceChatEnsureSenderId(), mulaw);
   if (!n)
      return;

   /* Netpacket path (#585): registered sink + netplay mode. No peer
    * address needed — the frontend delivers by client_id. */
   if (vcNetSend && JLinkMode() == JLINK_MODE_NETPACKET)
   {
      if (vcNetSend(pkt, n) && !isKeepalive)
         vcLastVoiceMs = JLinkNowMs();
      return;
   }

   peer = vcPeerAddr;
   if (!peer[0] && JLinkMode() == JLINK_MODE_TCP_CLIENT)
      peer = JLinkGetTCPHost();
   if (!peer || !peer[0])
      return;
   if (!JLinkDiscActive())
      return;
   JLinkDiscSendTo(pkt, n, peer, JLinkDiscPort());

   if (!isKeepalive)
      vcLastVoiceMs = JLinkNowMs();
}

static int VoiceChatGateOpen(int ptt_down, unsigned energy)
{
   if (vcGate == VC_GATE_PTT)
      return ptt_down ? 1 : 0;

   if (energy >= vcVadThresh)
   {
      vcVadOpen = 1;
      vcVadHang = VC_VAD_HANG_FRAMES;
      return 1;
   }
   if (vcVadHang > 0)
   {
      vcVadHang--;
      return 1;
   }
   vcVadOpen = 0;
   return 0;
}

static void VoiceChatProcessMicFrame(const int16_t pcm[VC_FRAME_SAMPLES],
                                     int ptt_down)
{
   unsigned energy;
   int open;
   unsigned i;

   energy = VoiceChatFrameEnergy(pcm, VC_FRAME_SAMPLES);
   open = VoiceChatGateOpen(ptt_down, energy);

   if (vcMonitor)
   {
      for (i = 0; i < VC_FRAME_SAMPLES; i++)
         vcMonRing[i] = pcm[i];
      vcMonCount = VC_FRAME_SAMPLES;
   }

   if (open)
      VoiceChatSendFrame(0, pcm);
}

/* ---- Per-frame / wire ------------------------------------------------ */

void VoiceChatFrameTick(int ptt_down)
{
   int16_t pull[VC_MIC_PULL_MAX];
   int got;
   unsigned i;
   uint32_t now;
   int16_t silence[VC_FRAME_SAMPLES];

   if (!vcEnabled)
      return;

   /* Seed peer from TCP client host when discovery is up. */
   if (!vcPeerAddr[0] && JLinkMode() == JLINK_MODE_TCP_CLIENT
       && JLinkGetTCPHost() && JLinkGetTCPHost()[0])
      VoiceChatSetPeerAddr(JLinkGetTCPHost());

   if (vcMicRead)
   {
      got = vcMicRead(pull, VC_MIC_PULL_MAX);
      if (got > 0)
      {
         for (i = 0; i < (unsigned)got; i++)
         {
            vcMicAccum[vcMicAccumN++] = pull[i];
            if (vcMicAccumN >= VC_FRAME_SAMPLES)
            {
               VoiceChatProcessMicFrame(vcMicAccum, ptt_down);
               vcMicAccumN = 0;
            }
         }
      }
   }

   /* Presence keepalive so a TCP server learns the client address even
    * when the gate is closed.  Suppressed when a real voice frame went
    * out recently — voice packets already teach the peer address, and
    * firing keepalives during speech is pointless. */
   now = JLinkNowMs();
   if (JLinkDiscActive()
       && (JLinkMode() == JLINK_MODE_TCP_CLIENT
           || JLinkMode() == JLINK_MODE_TCP_SERVER)
       && (vcLastVoiceMs == 0
           || (uint32_t)(now - vcLastVoiceMs) >= VC_KEEPALIVE_MS)
       && (vcLastKeepaliveMs == 0
           || (uint32_t)(now - vcLastKeepaliveMs) >= VC_KEEPALIVE_MS))
   {
      memset(silence, 0, sizeof(silence));
      VoiceChatSendFrame(VC_FLAG_KEEPALIVE, silence);
      vcLastKeepaliveMs = now;
   }
}

void VoiceChatOnRaw(const uint8_t *buf, size_t len,
                    const char *from_addr, int from_port)
{
   uint8_t flags;
   uint16_t seq;
   uint32_t senderId;
   uint8_t mulaw[VC_FRAME_SAMPLES];
   int16_t pcm[VC_FRAME_SAMPLES];
   unsigned i;

   (void)from_port;

   if (!vcEnabled)
      return;
   if (!VoiceChatDecodePkt(buf, len, &flags, &seq, &senderId, mulaw))
      return;
   if (senderId == VoiceChatEnsureSenderId())
      return; /* own packet looped via SO_REUSEPORT */

   if (from_addr && from_addr[0])
      VoiceChatSetPeerAddr(from_addr);

   if (flags & VC_FLAG_KEEPALIVE)
      return; /* presence only */

   for (i = 0; i < VC_FRAME_SAMPLES; i++)
      pcm[i] = VoiceChatMuLawDecode(mulaw[i]);
   VoiceChatJitterPushFrom(senderId, pcm, seq);
}

void VoiceChatMixInto(uint16_t *buf, unsigned pairs)
{
   unsigned i;
   unsigned s;
   int vol;
   int mixed;
   int16_t *stereo;
   int left, right;
   int16_t holdFar[VC_MAX_SPEAKERS];
   int16_t holdMon;
   unsigned monIdx;

   if (!vcEnabled || !buf || pairs == 0)
      return;
   if (vcVolume == 0 && !vcMonitor)
      return;

   vol = (int)vcVolume;
   stereo = (int16_t *)buf;
   for (s = 0; s < VC_MAX_SPEAKERS; s++)
      holdFar[s] = 0;
   holdMon = 0;
   monIdx = 0;

   for (i = 0; i < pairs; i++)
   {
      /* 6× upsample: one 8 kHz sample every VC_UPSAMPLE output pairs. */
      if ((i % VC_UPSAMPLE) == 0)
      {
         for (s = 0; s < VC_MAX_SPEAKERS; s++)
         {
            if (!vcStreams[s].inUse)
            {
               holdFar[s] = 0;
               continue;
            }
            if (!VoiceChatStreamPop(&vcStreams[s], &holdFar[s]))
               holdFar[s] = 0;
         }
         if (vcMonitor && monIdx < vcMonCount)
         {
            holdMon = vcMonRing[monIdx];
            monIdx++;
         }
         else
            holdMon = 0;
      }

      mixed = 0;
      for (s = 0; s < VC_MAX_SPEAKERS; s++)
         mixed += ((int)holdFar[s] * vol) / 100;
      if (vcMonitor)
         mixed += ((int)holdMon * vol) / 100;

      left = (int)stereo[i * 2 + 0] + mixed;
      right = (int)stereo[i * 2 + 1] + mixed;
      if (left > 32767)
         left = 32767;
      if (left < -32768)
         left = -32768;
      if (right > 32767)
         right = 32767;
      if (right < -32768)
         right = -32768;
      stereo[i * 2 + 0] = (int16_t)left;
      stereo[i * 2 + 1] = (int16_t)right;
   }
   if (vcMonitor)
      vcMonCount = 0;
}
