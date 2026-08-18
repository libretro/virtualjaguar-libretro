/* uart.c — JERRY asynchronous serial interface (ComLynx / JagLink UART).
 *
 * Register behavior per the Jaguar Technical Reference Manual Rev 8,
 * pp. 93-95 (docs/atari-jaguar-1999/jag_v8.pdf):
 *   - baud = SystemClock / (16 * (ASICLK+1))
 *   - a character frame is 11 bit times: start + 8 data + parity slot
 *     (always transmitted) + stop
 *   - transmitter and receiver are each double buffered
 *   - JINTCTRL bit 4 (IRQ2_ASI) gates both interrupt classes; interrupts
 *     reach the 68K only (IPL2)
 *
 * The wire itself is src/jerry/jlink.c; this file never touches a
 * transport directly.
 */
#include <string.h>
#include "uart.h"
#include "jerry.h"
#include "event.h"
#include "settings.h"
#include "state.h"
#include "jlink.h"
#include "m68000/m68kinterface.h"

/* ASICTRL write bits */
#define ASICTRL_TINTEN  0x0010
#define ASICTRL_RINTEN  0x0020
#define ASICTRL_CLRERR  0x0040
#define ASICTRL_TXBRK   0x4000
#define ASICTRL_CFGMASK 0x403F   /* stored bits; CLRERR is an action */

/* ASISTAT read bits */
#define ASISTAT_RBF     0x0080
#define ASISTAT_TBE     0x0100
#define ASISTAT_OE      0x0800
#define ASISTAT_SERIN   0x2000
#define ASISTAT_TXBRK   0x4000
#define ASISTAT_ERROR   0x8000

static uint16_t asiCtrl;
static uint16_t asiClk;
static uint16_t asiErr;         /* PE/FE/OE latches, ASISTAT positions */
static uint8_t uartRxData;
static uint8_t uartRxFull;      /* RBF */
static uint8_t uartTxHold;
static uint8_t uartTxHoldFull;  /* inverse of TBE */
static uint8_t uartTxShift;
static uint8_t uartTxBusy;      /* shift register clocking out */
static uint8_t uartRxBusy;      /* an RX frame is in flight */

/* One character frame in microseconds at the current divider. */
static double UARTFrameUsec(void)
{
   double cyc = 11.0 * 16.0 * (double)((uint32_t)asiClk + 1u);
   return cyc * (vjs.hardwareTypeNTSC ? RISC_CYCLE_IN_USEC
                                      : RISC_CYCLE_PAL_IN_USEC);
}

static void UARTRaiseIRQ(void)
{
   if (JERRYIRQEnabled(IRQ2_ASI))
   {
      JERRYSetPendingIRQ(IRQ2_ASI);
      m68k_set_irq(2);
   }
}

/* Start an RX frame if the transport has data and none is in flight. */
static void UARTKickRx(void)
{
   if (!uartRxBusy && JLinkRxPending())
   {
      uartRxBusy = 1;
      SetCallbackTime(UARTRXCallback, UARTFrameUsec(), EVENT_JERRY);
   }
}

void UARTTXCallback(void)
{
   uartTxBusy = 0;
   JLinkSendByte(uartTxShift);
   if (uartTxHoldFull)
   {
      uartTxShift = uartTxHold;
      uartTxHoldFull = 0;       /* TBE edge: transmitter interrupt */
      uartTxBusy = 1;
      SetCallbackTime(UARTTXCallback, UARTFrameUsec(), EVENT_JERRY);
      if (asiCtrl & ASICTRL_TINTEN)
         UARTRaiseIRQ();
   }
   else
   {
      /* Burst finished (shift drained, nothing queued): push the
         batched transport bytes out NOW so a mid-frame tic exchange
         gets sub-millisecond latency as one packet per burst.  The
         voice modem also emits its end-of-packet marker here. */
      JLinkTxBurstEnd();
   }
   UARTKickRx();
}

void UARTRXCallback(void)
{
   uint8_t b;
   uartRxBusy = 0;
   if (JLinkRecvByte(&b))
   {
      if (uartRxFull)
      {
         /* Overrun: newest byte lost, buffered data kept.  OE is a
            receiver-class interrupt source. */
         asiErr |= ASISTAT_OE;
         if (asiCtrl & ASICTRL_RINTEN)
            UARTRaiseIRQ();
      }
      else
      {
         uartRxData = b;
         uartRxFull = 1;
         if (asiCtrl & ASICTRL_RINTEN)
            UARTRaiseIRQ();
      }
   }
   UARTKickRx();
}

uint16_t UARTReadWord(uint32_t offset)
{
   uint16_t v = 0;
   switch (offset)
   {
      case 0xF10030:            /* ASIDATA */
         v = uartRxData;
         uartRxFull = 0;
         UARTKickRx();
         break;
      case 0xF10032:            /* ASISTAT */
         /* Link games spin on this register waiting for the partner's
            reply mid-frame.  If nothing is buffered anywhere, pump the
            transport so a reply that is already on the network can be
            delivered THIS frame instead of next (rate-limited: a
            polling loop iterates every few emulated microseconds). */
         if (!uartRxFull && !uartRxBusy && JLinkConnected()
             && !JLinkRxPending())
         {
            static unsigned pumpGate = 0;
            /* An unanswered TX burst: wait in wall-clock (bounded per
               frame) so the reply lands THIS frame instead of
               quantizing to the next retro_run. */
            JLinkAwaitReply();
            if (JLinkRxPending())
               UARTKickRx();
            else if ((++pumpGate & 15u) == 0)
            {
               JLinkPump();
               UARTKickRx();
            }
         }
         v = (uint16_t)(asiCtrl & 0x003F);
         if (uartRxFull)
            v |= ASISTAT_RBF;
         if (!uartTxHoldFull)
            v |= ASISTAT_TBE;
         v |= asiErr;
         v |= ASISTAT_SERIN;    /* phase 1: line idles at mark */
         if (asiCtrl & ASICTRL_TXBRK)
            v |= ASISTAT_TXBRK;
         if (asiErr)
            v |= ASISTAT_ERROR;
         break;
      case 0xF10034:            /* ASICLK */
         v = asiClk;
         break;
      default:
         break;
   }
   return v;
}

void UARTWriteWord(uint32_t offset, uint16_t data)
{
   switch (offset)
   {
      case 0xF10030:            /* ASIDATA */
         if (!uartTxBusy)
         {
            uartTxShift = (uint8_t)(data & 0xFF);
            uartTxBusy = 1;
            SetCallbackTime(UARTTXCallback, UARTFrameUsec(), EVENT_JERRY);
         }
         else
         {
            uartTxHold = (uint8_t)(data & 0xFF);
            uartTxHoldFull = 1;
         }
         break;
      case 0xF10032:            /* ASICTRL */
         asiCtrl = (uint16_t)(data & ASICTRL_CFGMASK);
         if (data & ASICTRL_CLRERR)
            asiErr = 0;
         break;
      case 0xF10034:            /* ASICLK */
         asiClk = data;
         break;
      default:
         break;
   }
}

/* Per-frame service from retro_run, after JLinkPoll has drained the
   socket: start an RX frame for any newly arrived transport bytes. */
void UARTPoll(void)
{
   UARTKickRx();
}

void UARTInit(void)
{
   UARTReset();
}

void UARTReset(void)
{
   RemoveCallback(UARTTXCallback);
   RemoveCallback(UARTRXCallback);
   asiCtrl = 0;
   asiClk = 0;
   asiErr = 0;
   uartRxData = 0;
   uartRxFull = 0;
   uartTxHold = 0;
   uartTxHoldFull = 0;
   uartTxShift = 0;
   uartTxBusy = 0;
   uartRxBusy = 0;
}

void UARTDone(void)
{
   UARTReset();
   JLinkClose();
}

void UARTSetLinkMode(int mode)
{
   if (mode != JLinkMode())
   {
      JLinkClose();
      if (mode != JLINK_MODE_DISABLED)
         JLinkOpen(mode);
   }
}

size_t UARTStateSave(uint8_t *buf)
{
   uint8_t *start = buf;
   STATE_SAVE_VAR(buf, asiCtrl);
   STATE_SAVE_VAR(buf, asiClk);
   STATE_SAVE_VAR(buf, asiErr);
   STATE_SAVE_VAR(buf, uartRxData);
   STATE_SAVE_VAR(buf, uartRxFull);
   STATE_SAVE_VAR(buf, uartTxHold);
   STATE_SAVE_VAR(buf, uartTxHoldFull);
   STATE_SAVE_VAR(buf, uartTxShift);
   STATE_SAVE_VAR(buf, uartTxBusy);
   STATE_SAVE_VAR(buf, uartRxBusy);
   buf += JLinkStateSave(buf);
   return (size_t)(buf - start);
}

size_t UARTStateLoad(const uint8_t *buf, uint32_t stateVersion)
{
   const uint8_t *start = buf;
   (void)stateVersion;   /* whole chunk is gated by the caller */
   STATE_LOAD_VAR(buf, asiCtrl);
   STATE_LOAD_VAR(buf, asiClk);
   STATE_LOAD_VAR(buf, asiErr);
   STATE_LOAD_VAR(buf, uartRxData);
   STATE_LOAD_VAR(buf, uartRxFull);
   STATE_LOAD_VAR(buf, uartTxHold);
   STATE_LOAD_VAR(buf, uartTxHoldFull);
   STATE_LOAD_VAR(buf, uartTxShift);
   STATE_LOAD_VAR(buf, uartTxBusy);
   STATE_LOAD_VAR(buf, uartRxBusy);
   buf += JLinkStateLoad(buf);
   return (size_t)(buf - start);
}
