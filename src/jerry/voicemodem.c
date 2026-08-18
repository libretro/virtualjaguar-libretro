/* voicemodem.c — Jaguar Voice Modem (JVM) emulation for Ultra Vortek.
 *
 * Protocol derived from the retail Ultra Vortek ROM's own 68K modem
 * driver ($80AF96-$80BA19) and DSP UART service loop; the full derivation
 * is docs/voice-modem.md.  Summary of the console-facing wire format:
 *
 *   console -> modem : 16-bit command words, LOW byte first
 *   modem  -> console: 3-byte messages: sync $FF, HIGH byte, LOW byte
 *
 * Every command word is echoed back unless noted.  Beyond echoes the
 * modem produces:
 *   $B800  reply to the $FFFF wake
 *   $FFFE  reply to a $6800 DTMF poll when no digit is queued
 *   $68nn  reply to $6800 when digit nn was heard from the far side
 *   $86xx  reply to $8100 when the call is up ($80xx = not yet)
 *   $A4FC  async event clearing the driver's $57F6 connect wait bits
 *   $A401  async "line dropped" (mask clear + bit0 -> driver error $FFF3)
 *   $B1xx  async ring indicate
 *   $F0xx  data byte from the far console, $F301 end-of-packet
 *
 * The two virtual modems talk to each other over the existing jlink
 * transport (TCP / netpacket / loopback) with 2-byte frames:
 *   [VM_FR_* type, value].  "Dial" resolves to the transport session; the
 * dialed number is consumed and ignored.
 *
 * iOS rule: every static is reset in VMReset(), reached from both
 * retro_init and retro_deinit paths via JLinkClose/UARTDone.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "voicemodem.h"
#include "jlink.h"

/* ---- inter-modem frame types (ours to define; the real line carried
 *      tones and a Phylon-proprietary carrier) ---- */
#define VM_FR_DTMF   0x01
#define VM_FR_DIAL   0x02
#define VM_FR_ANSWER 0x03
#define VM_FR_HANGUP 0x04
#define VM_FR_DATA   0x05
#define VM_FR_END    0x06

/* console-facing RX queue (3 bytes per message) */
#define VM_RXQ_SIZE 1024
static uint8_t vmRxQ[VM_RXQ_SIZE];
static uint32_t vmRxHead;
static uint32_t vmRxCount;

/* heard-DTMF digit queue, popped by $6800 */
#define VM_DIGQ_SIZE 32
static uint8_t vmDigQ[VM_DIGQ_SIZE];
static uint32_t vmDigHead;
static uint32_t vmDigCount;

/* answer-side probe digits buffered until the call is up */
#define VM_PENDQ_SIZE 16
static uint8_t vmPendDig[VM_PENDQ_SIZE];
static uint32_t vmPendCount;

/* console word assembly (low byte first) */
static int vmHaveLo;
static uint8_t vmLoByte;

/* wire frame assembly */
static int vmWireHaveType;
static uint8_t vmWireType;

/* call state */
static int vmAwake;           /* saw $FFFF wake                    */
static int vmModeAnswer;      /* last mode word: $2480=1, $2C80=0  */
static int vmConnected;       /* carrier up                        */
static int vmDialedOut;       /* we sent VM_FR_DIAL                */
static int vmSawDialDigit;    /* originate-side number in progress */
static int vmRingPending;     /* far side dialed us                */
static int vmDataInBurst;     /* DATA sent since last burst end    */
static unsigned vmRingTimer;  /* frames until next $B1xx           */

static int vmTrace = -1;

static void VMTrace(const char *dir, unsigned v)
{
   if (vmTrace < 0)
   {
      const char *e = getenv("VJ_VM_TRACE");
      vmTrace = (e && e[0] == '1') ? 1 : 0;
   }
   if (vmTrace)
      fprintf(stderr, "[vmodem] %s %04X\n", dir, v);
}

/* ---- console-facing message queue ---- */

static void VMQueueByte(uint8_t b)
{
   uint32_t tail;
   if (vmRxCount >= VM_RXQ_SIZE)
      return;                     /* full: drop (console will retry) */
   tail = (vmRxHead + vmRxCount) % VM_RXQ_SIZE;
   vmRxQ[tail] = b;
   vmRxCount++;
}

/* 3-byte message: sync $FF, high byte, low byte */
static void VMQueueMsg(uint16_t word)
{
   if (vmRxCount + 3 > VM_RXQ_SIZE)
      return;
   VMTrace("out", word);
   VMQueueByte(0xFF);
   VMQueueByte((uint8_t)(word >> 8));
   VMQueueByte((uint8_t)(word & 0xFF));
}

int VMConsoleRecv(uint8_t *b)
{
   if (vmRxCount == 0)
      return 0;
   *b = vmRxQ[vmRxHead];
   vmRxHead = (vmRxHead + 1) % VM_RXQ_SIZE;
   vmRxCount--;
   return 1;
}

int VMConsoleRxPending(void)
{
   return (int)vmRxCount;
}

/* ---- inter-modem wire ---- */

static void VMWireSend(uint8_t type, uint8_t val)
{
   if (!JLinkConnected())
      return;
   JLinkWireSendByte(type);
   JLinkWireSendByte(val);
}

static void VMHangupLocal(void)
{
   if (vmConnected || vmDialedOut)
      VMWireSend(VM_FR_HANGUP, 0);
   vmConnected = 0;
   vmDialedOut = 0;
   vmSawDialDigit = 0;
   vmRingPending = 0;
   vmDataInBurst = 0;
   vmPendCount = 0;
   vmDigHead = 0;
   vmDigCount = 0;
}

static void VMConnectNow(void)
{
   uint32_t i;
   vmConnected = 1;
   vmRingPending = 0;
   /* flush probe digits buffered while the call was still ringing */
   for (i = 0; i < vmPendCount; i++)
      VMWireSend(VM_FR_DTMF, vmPendDig[i]);
   vmPendCount = 0;
}

/* ---- console command words ---- */

static void VMCommand(uint16_t cmd)
{
   VMTrace("cmd", cmd);

   /* wake / detect: also a full call-state reset */
   if (cmd == 0xFFFF)
   {
      vmAwake = 1;
      VMHangupLocal();
      VMQueueMsg(0xB800);
      return;
   }
   /* line-rate switch: console changes ASICLK right after and reads no
    * reply; echoing would desynchronize the next transaction */
   if (cmd == 0xFFFE)
      return;

   /* DTMF digit $8A2n */
   if ((cmd & 0xFFF0) == 0x8A20)
   {
      uint8_t digit = (uint8_t)(cmd & 0x0F);
      VMQueueMsg(cmd);
      if (vmConnected)
         VMWireSend(VM_FR_DTMF, digit);
      else if (vmModeAnswer)
      {
         /* answer-side probe digits sent before the carrier settled:
          * hold them for VMConnectNow */
         if (vmPendCount < VM_PENDQ_SIZE)
            vmPendDig[vmPendCount++] = digit;
      }
      else
         vmSawDialDigit = 1;   /* originate: part of the phone number */
      return;
   }

   switch (cmd)
   {
      case 0x2480:              /* answer mode */
         vmModeAnswer = 1;
         VMQueueMsg(cmd);
         if (vmRingPending && !vmConnected)
         {
            VMWireSend(VM_FR_ANSWER, 0);
            VMConnectNow();
         }
         return;
      case 0x2C80:              /* originate mode */
         vmModeAnswer = 0;
         VMQueueMsg(cmd);
         return;
      case 0x8C01:              /* dial-tone check */
         VMQueueMsg(JLinkConnected() ? 0x8C01 : 0x8C00);
         return;
      case 0x8C00:              /* call progress: always idle (instant) */
         VMQueueMsg(0x8C00);
         return;
      case 0x6800:              /* poll heard DTMF digit */
         if (!vmDialedOut && !vmModeAnswer && vmSawDialDigit)
         {
            /* first $6800 after the number went in = dialing finished */
            vmSawDialDigit = 0;
            vmDialedOut = 1;
            VMWireSend(VM_FR_DIAL, 0);
         }
         if (vmDigCount)
         {
            uint8_t d = vmDigQ[vmDigHead];
            vmDigHead = (vmDigHead + 1) % VM_DIGQ_SIZE;
            vmDigCount--;
            VMQueueMsg((uint16_t)(0x6800 | d));
         }
         else
            VMQueueMsg(0xFFFE);
         return;
      case 0x8100:              /* carrier / connect-result query */
         if (vmConnected)
         {
            /* $86xx: connected.  Ultra Vortek requires (xx >> 4) >= 8
             * and maps nibbles 8..E to a speed display table
             * (9600,9600,12000,14400,16800,19200,57600); below 8 it
             * declares TOO MUCH TELEPHONE NOISE and hangs up.  $D =
             * 19200, the rate the UART is actually programmed to. */
            VMQueueMsg(0x86D0);
            VMQueueMsg(0xA4FC);   /* clear the $57F6 connect wait bits */
         }
         else
            VMQueueMsg(0x8000);   /* not yet: driver retries */
         return;
      case 0x9000:              /* hang up */
         VMHangupLocal();
         VMQueueMsg(cmd);
         return;
      default:
         break;
   }

   /* data byte to the far console: raw, never echoed */
   if ((cmd & 0xFF00) == 0xF000)
   {
      if (vmConnected)
      {
         VMWireSend(VM_FR_DATA, (uint8_t)(cmd & 0xFF));
         vmDataInBurst = 1;
      }
      return;
   }

   /* everything else ($0102 ident, $0501 + config words, $8000, $0002,
    * $A0xx audio path, ...) just wants its echo */
   VMQueueMsg(cmd);
}

void VMConsoleTx(uint8_t b)
{
   if (!vmHaveLo)
   {
      vmLoByte = b;
      vmHaveLo = 1;
      return;
   }
   vmHaveLo = 0;
   VMCommand((uint16_t)(((uint16_t)b << 8) | vmLoByte));
}

/* ---- bytes from the far modem ---- */

static void VMWireFrame(uint8_t type, uint8_t val)
{
   switch (type)
   {
      case VM_FR_DTMF:
         if (vmDigCount < VM_DIGQ_SIZE)
         {
            uint32_t tail = (vmDigHead + vmDigCount) % VM_DIGQ_SIZE;
            vmDigQ[tail] = (uint8_t)(val & 0x0F);
            vmDigCount++;
         }
         break;
      case VM_FR_DIAL:
         if (vmModeAnswer && !vmConnected)
         {
            /* already armed to answer: pick up immediately */
            VMWireSend(VM_FR_ANSWER, 0);
            VMConnectNow();
         }
         else if (!vmConnected)
         {
            vmRingPending = 1;
            vmRingTimer = 0;    /* ring on the next frame tick */
         }
         break;
      case VM_FR_ANSWER:
         VMConnectNow();
         break;
      case VM_FR_HANGUP:
         if (vmConnected && vmAwake)
            VMQueueMsg(0xA401);   /* mask clear + bit0: line dropped */
         vmConnected = 0;
         vmDialedOut = 0;
         vmRingPending = 0;
         vmDigHead = 0;
         vmDigCount = 0;
         break;
      case VM_FR_DATA:
         VMQueueMsg((uint16_t)(0xF000 | val));
         break;
      case VM_FR_END:
         VMQueueMsg(0xF301);
         break;
      default:
         break;                  /* unknown frame: skip */
   }
}

void VMWireInput(uint8_t b)
{
   if (!vmWireHaveType)
   {
      vmWireType = b;
      vmWireHaveType = 1;
      return;
   }
   vmWireHaveType = 0;
   VMWireFrame(vmWireType, b);
}

void VMTxBurstEnd(void)
{
   if (vmDataInBurst)
   {
      vmDataInBurst = 0;
      VMWireSend(VM_FR_END, 0);
   }
}

void VMFrameTick(void)
{
   if (vmRingPending && !vmConnected && vmAwake)
   {
      if (vmRingTimer == 0)
      {
         VMQueueMsg(0xB100);     /* RING */
         vmRingTimer = 60;       /* ~1/second */
      }
      else
         vmRingTimer--;
   }
}

void VMReset(void)
{
   vmRxHead = 0;
   vmRxCount = 0;
   vmDigHead = 0;
   vmDigCount = 0;
   vmPendCount = 0;
   vmHaveLo = 0;
   vmLoByte = 0;
   vmWireHaveType = 0;
   vmWireType = 0;
   vmAwake = 0;
   vmModeAnswer = 0;
   vmConnected = 0;
   vmDialedOut = 0;
   vmSawDialDigit = 0;
   vmRingPending = 0;
   vmDataInBurst = 0;
   vmRingTimer = 0;
   /* vmTrace deliberately kept: env doesn't change mid-process */
}
