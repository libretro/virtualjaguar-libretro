/* jlink_netpacket.c — libretro netpacket transport for the JagLink seam.
 *
 * UART TX bytes accumulate in a small per-frame batch and go out as one
 * RELIABLE broadcast packet (flushed from the frontend's per-frame poll
 * callback, from retro_run via JLinkPoll, or when the batch fills).
 * Received packet payloads feed the shared jlink RX ring byte-for-byte.
 */
#include <string.h>
#include "jlink.h"
#include "jlink_netpacket.h"

#define JLINK_NP_TXBUF_SIZE 512

static retro_netpacket_send_t npSend = NULL;
static retro_netpacket_poll_receive_t npPollReceive = NULL;
static int npActive = 0;
static int npPrevMode = JLINK_MODE_DISABLED;
static uint8_t npTxBuf[JLINK_NP_TXBUF_SIZE];
static uint32_t npTxLen = 0;

void JLinkNPStart(uint16_t client_id, retro_netpacket_send_t send_fn,
                  retro_netpacket_poll_receive_t poll_receive_fn)
{
   (void)client_id;
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
   npActive = 1;
   JLinkOpen(JLINK_MODE_NETPACKET);
}

void JLinkNPReceive(const void *buf, size_t len, uint16_t client_id)
{
   (void)client_id;
   if (npActive && buf)
      JLinkNPDeliver((const uint8_t *)buf, len);
}

void JLinkNPStop(void)
{
   npActive = 0;
   npSend = NULL;
   npPollReceive = NULL;
   npTxLen = 0;
   JLinkClose();
   if (npPrevMode != JLINK_MODE_DISABLED
       && npPrevMode != JLINK_MODE_NETPACKET)
      JLinkOpen(npPrevMode);
   npPrevMode = JLINK_MODE_DISABLED;
}

void JLinkNPPoll(void)
{
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
   if (npTxLen >= JLINK_NP_TXBUF_SIZE)
      return;   /* flush unavailable: drop rather than overflow */
   npTxBuf[npTxLen++] = b;
   /* No flush here: the UART flushes at burst end (transmit shift
      register drains with nothing queued), which keeps mid-frame tic
      exchanges sub-millisecond WITHOUT emitting one reliable packet
      per byte.  Per-byte packets were fine on localhost but caused
      visible lag over real Wi-Fi (per-packet overhead on every byte
      of every tic).  Safety nets: buffer-full flush below, and the
      per-frame flush in JLinkPoll/JLinkNPPoll. */
   if (npTxLen >= JLINK_NP_TXBUF_SIZE)
      JLinkNPFlush();
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
   npSend(RETRO_NETPACKET_RELIABLE | RETRO_NETPACKET_FLUSH_HINT,
          npTxBuf, npTxLen, RETRO_NETPACKET_BROADCAST);
   npTxLen = 0;
}
