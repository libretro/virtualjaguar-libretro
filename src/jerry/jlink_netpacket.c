/* jlink_netpacket.c — libretro netpacket transport for the JagLink seam.
 *
 * UART TX bytes accumulate in a small per-frame batch and go out as one
 * RELIABLE broadcast packet (flushed from the frontend's per-frame poll
 * callback, from retro_run via JLinkPoll, or when the batch fills).
 * Received packet payloads feed the shared jlink RX ring byte-for-byte.
 *
 * vjag-netlink-2 (#585): every payload is prefixed with a type byte
 * (NP_UART / NP_VOICE / NP_VOICE_HELLO).  Voice stays host-side; only
 * NP_UART bytes enter the UART ring.
 */
#include <stdio.h>
#include <string.h>
#include "jlink.h"
#include "jlink_netpacket.h"
#include "voicechat.h"

/* UART batch capacity (unchanged from vjag-netlink-1) + 1 type byte. */
#define JLINK_NP_UART_CAP   512
#define JLINK_NP_TXBUF_SIZE (JLINK_NP_UART_CAP + 1)

#define JLINK_NP_HELLO_RETRY_MS   500
#define JLINK_NP_HELLO_TIMEOUT_MS 5000
#define JLINK_NP_VOICE_PEERS_MAX  8

static retro_netpacket_send_t npSend = NULL;
static retro_netpacket_poll_receive_t npPollReceive = NULL;
static int npActive = 0;
static int npPrevMode = JLINK_MODE_DISABLED;
static uint8_t npTxBuf[JLINK_NP_TXBUF_SIZE];
static uint32_t npTxLen = 0;   /* UART payload bytes (excludes type) */

/* Voice negotiate state. */
static int npVoiceWant = 0;
static int npVoiceReady = 0;
static int npVoiceDataOnly = 0;
static int npVoiceLogged = 0;
static uint32_t npHelloStartMs = 0;
static uint32_t npHelloLastMs = 0;
static uint32_t npLocalSenderId = 0;
static int npLocalSenderReady = 0;

static uint16_t npAckedClients[JLINK_NP_VOICE_PEERS_MAX];
static unsigned npAckedCount = 0;

static uint32_t JLinkNPEnsureSenderId(void)
{
   if (!npLocalSenderReady)
   {
      uint32_t x = JLinkNowMs() ^ (uint32_t)(size_t)&npLocalSenderId;
      if (x == 0)
         x = 0xA5A5A5A5u;
      x ^= x << 13;
      x ^= x >> 17;
      x ^= x << 5;
      npLocalSenderId = x ? x : 1u;
      npLocalSenderReady = 1;
   }
   return npLocalSenderId;
}

static void JLinkNPVoiceReset(void)
{
   npVoiceReady = 0;
   npVoiceDataOnly = 0;
   npVoiceLogged = 0;
   npHelloStartMs = 0;
   npHelloLastMs = 0;
   npAckedCount = 0;
   memset(npAckedClients, 0, sizeof(npAckedClients));
}

static int JLinkNPAckedHas(uint16_t client_id)
{
   unsigned i;
   for (i = 0; i < npAckedCount; i++)
   {
      if (npAckedClients[i] == client_id)
         return 1;
   }
   return 0;
}

static void JLinkNPAckedAdd(uint16_t client_id)
{
   if (JLinkNPAckedHas(client_id))
      return;
   if (npAckedCount >= JLINK_NP_VOICE_PEERS_MAX)
      return;
   npAckedClients[npAckedCount++] = client_id;
}

static void JLinkNPSendHello(int ack)
{
   uint8_t pkt[JLINK_NP_HELLO_PKT_LEN];
   uint32_t sid;
   uint8_t flags;

   if (!npActive || !npSend || !npVoiceWant)
      return;

   sid = JLinkNPEnsureSenderId();
   flags = (uint8_t)(JLINK_NP_HELLO_FLAG_WANT | (ack ? JLINK_NP_HELLO_FLAG_ACK : 0));
   pkt[0] = JLINK_NP_TYPE_VOICE_HELLO;
   pkt[1] = 'V';
   pkt[2] = 'C';
   pkt[3] = JLINK_NP_HELLO_VERSION;
   pkt[4] = flags;
   pkt[5] = (uint8_t)((sid >> 24) & 0xFF);
   pkt[6] = (uint8_t)((sid >> 16) & 0xFF);
   pkt[7] = (uint8_t)((sid >> 8) & 0xFF);
   pkt[8] = (uint8_t)(sid & 0xFF);
   npSend(RETRO_NETPACKET_RELIABLE | RETRO_NETPACKET_FLUSH_HINT,
          pkt, JLINK_NP_HELLO_PKT_LEN, RETRO_NETPACKET_BROADCAST);
}

static void JLinkNPHandleHello(const uint8_t *body, size_t len,
                               uint16_t client_id)
{
   uint8_t flags;
   uint32_t sid;

   if (!body || len < JLINK_NP_HELLO_BODY_LEN)
      return;
   if (body[0] != 'V' || body[1] != 'C')
      return;
   if (body[2] != JLINK_NP_HELLO_VERSION)
      return;

   flags = body[3];
   sid = ((uint32_t)body[4] << 24) | ((uint32_t)body[5] << 16)
       | ((uint32_t)body[6] << 8) | (uint32_t)body[7];
   if (sid == 0 || sid == JLinkNPEnsureSenderId())
      return; /* ignore echo / invalid */

   if (!npVoiceWant)
      return;

   /* Answer offers only — never ACK an ACK, or both sides keep
    * reflecting hellos forever. */
   if (!(flags & JLINK_NP_HELLO_FLAG_ACK))
      JLinkNPSendHello(1);

   if (flags & JLINK_NP_HELLO_FLAG_WANT)
   {
      /* Any WANT from a peer is enough to arm: we already replied with a
         reliable ACK (above) or they already ACKed us. Waiting for an
         extra ACK bit would only delay mic open. */
      JLinkNPAckedAdd(client_id);
      npVoiceReady = 1;
      npVoiceDataOnly = 0;
   }
}

static int JLinkNPVoiceSendThunk(const uint8_t *pkt, size_t len)
{
   return JLinkNPSendVoice(pkt, len);
}

void JLinkNPStart(uint16_t client_id, retro_netpacket_send_t send_fn,
                  retro_netpacket_poll_receive_t poll_receive_fn)
{
   /* The libretro contract guarantees a valid send_fn, but a NULL here
      would otherwise leave the link claiming to be connected while
      JLinkNPFlush can never drain the TX buffer. */
   if (!send_fn)
      return;
   npPrevMode = JLinkMode();
   JLinkClose();
   npSend = send_fn;
   npPollReceive = poll_receive_fn;
   npTxLen = 0;
   npTxBuf[0] = JLINK_NP_TYPE_UART;
   npActive = 1;
   (void)client_id;
   JLinkNPVoiceReset();
   VoiceChatSetNetSend(JLinkNPVoiceSendThunk);
   JLinkOpen(JLINK_MODE_NETPACKET);
   if (npVoiceWant)
   {
      npHelloStartMs = JLinkNowMs();
      npHelloLastMs = 0;
      JLinkNPSendHello(0);
      npHelloLastMs = npHelloStartMs;
   }
}

void JLinkNPReceive(const void *buf, size_t len, uint16_t client_id)
{
   const uint8_t *p;

   if (!npActive || !buf || len < 1)
      return;
   p = (const uint8_t *)buf;
   switch (p[0])
   {
      case JLINK_NP_TYPE_UART:
         if (len > 1)
            JLinkNPDeliver(p + 1, len - 1);
         break;
      case JLINK_NP_TYPE_VOICE:
         if (len > 1)
            VoiceChatOnRaw(p + 1, len - 1, NULL, 0);
         break;
      case JLINK_NP_TYPE_VOICE_HELLO:
         JLinkNPHandleHello(p + 1, len - 1, client_id);
         break;
      default:
         /* Unknown type: drop. Do not push into the UART ring. */
         break;
   }
}

void JLinkNPStop(void)
{
   npActive = 0;
   npSend = NULL;
   npPollReceive = NULL;
   npTxLen = 0;
   VoiceChatSetNetSend(NULL);
   JLinkNPVoiceReset();
   JLinkClose();
   if (npPrevMode != JLINK_MODE_DISABLED
       && npPrevMode != JLINK_MODE_NETPACKET)
      JLinkOpen(npPrevMode);
   npPrevMode = JLINK_MODE_DISABLED;
}

void JLinkNPPoll(void)
{
   JLinkNPVoiceTick();
   JLinkNPFlush();
}

int JLinkNPActive(void)
{
   return npActive;
}

void JLinkNPQueueByte(uint8_t b)
{
   if (!npActive)
      return;
   if (npTxLen >= JLINK_NP_UART_CAP)
   {
      /* Full: flush to make room — dropping link bytes risks a game
         desync.  Only drop if the flush couldn't drain (inconsistent
         state; cannot happen for an active session). */
      JLinkNPFlush();
      if (npTxLen >= JLINK_NP_UART_CAP)
         return;
   }
   npTxBuf[1 + npTxLen] = b;
   npTxLen++;
   /* No flush here: the UART flushes at burst end (transmit shift
      register drains with nothing queued), which keeps mid-frame tic
      exchanges sub-millisecond WITHOUT emitting one reliable packet
      per byte.  Per-byte packets were fine on localhost but caused
      visible lag over real Wi-Fi (per-packet overhead on every byte
      of every tic).  Safety nets: the flush-on-full above and the
      per-frame flush in JLinkPoll/JLinkNPPoll. */
}

/* Pump the frontend for incoming packets mid-frame (the receive
   callback fires reentrantly and feeds the ring). */
void JLinkNPPumpReceive(void)
{
   if (npActive && npPollReceive)
      npPollReceive();
}

void JLinkNPFlush(void)
{
   if (!npActive || !npSend || npTxLen == 0)
      return;
   npTxBuf[0] = JLINK_NP_TYPE_UART;
   npSend(RETRO_NETPACKET_RELIABLE | RETRO_NETPACKET_FLUSH_HINT,
          npTxBuf, (size_t)npTxLen + 1, RETRO_NETPACKET_BROADCAST);
   npTxLen = 0;
}

void JLinkNPSetVoiceWant(int want)
{
   uint32_t now;

   npVoiceWant = want ? 1 : 0;
   if (!npVoiceWant)
   {
      JLinkNPVoiceReset();
      return;
   }
   /* check_variables may call this often — respect the same 500 ms
      hello throttle as JLinkNPVoiceTick so option re-reads cannot spam
      reliable broadcasts during the negotiate window. */
   if (npActive && !npVoiceReady && !npVoiceDataOnly)
   {
      now = JLinkNowMs();
      if (npHelloStartMs == 0)
         npHelloStartMs = now;
      if (npHelloLastMs == 0
          || (uint32_t)(now - npHelloLastMs) >= JLINK_NP_HELLO_RETRY_MS)
      {
         JLinkNPSendHello(0);
         npHelloLastMs = now;
      }
   }
}

int JLinkNPVoiceReady(void)
{
   return npActive && npVoiceWant && npVoiceReady;
}

void JLinkNPVoiceTick(void)
{
   uint32_t now;

   if (!npActive || !npVoiceWant)
      return;
   if (npVoiceReady)
      return;

   now = JLinkNowMs();
   if (npHelloStartMs == 0)
      npHelloStartMs = now;

   if (!npVoiceDataOnly
       && (uint32_t)(now - npHelloStartMs) >= JLINK_NP_HELLO_TIMEOUT_MS)
   {
      npVoiceDataOnly = 1;
      if (!npVoiceLogged)
      {
         fprintf(stderr, "[VOICE] peer has no voice chat -- data-only\n");
         npVoiceLogged = 1;
      }
      return;
   }

   if (npVoiceDataOnly)
      return;

   if (npHelloLastMs == 0
       || (uint32_t)(now - npHelloLastMs) >= JLINK_NP_HELLO_RETRY_MS)
   {
      JLinkNPSendHello(0);
      npHelloLastMs = now;
   }
}

int JLinkNPSendVoice(const uint8_t *pkt, size_t len)
{
   uint8_t frame[1 + VC_PKT_LEN];

   if (!npActive || !npSend || !pkt || len == 0)
      return 0;
   if (!npVoiceReady)
      return 0;
   if (len > VC_PKT_LEN)
      return 0;
   frame[0] = JLINK_NP_TYPE_VOICE;
   memcpy(frame + 1, pkt, len);
   /* Best-effort: must not block lockstep UART. Frontends without
      unreliable support silently promote to reliable (libretro.h). */
   npSend(RETRO_NETPACKET_UNRELIABLE | RETRO_NETPACKET_UNSEQUENCED,
          frame, len + 1, RETRO_NETPACKET_BROADCAST);
   return 1;
}
