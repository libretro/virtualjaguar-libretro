/* test_uart_loopback.c — unit test for JERRY UART emulation over the
   loopback transport, driving the real event queue. */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "src/core/event.h"
#include "src/core/settings.h"
#include "src/jerry/jerry.h"
#include "src/jerry/uart.h"
#include "src/jerry/jlink.h"

/* ---- stubs required by event.c's savestate callback registry ---- */
void HalflineCallback(void) {}
void TOMPITCallback(void) {}
void JERRYPIT1Callback(void) {}
void JERRYPIT2Callback(void) {}
void JERRYI2SCallback(void) {}
void DSPSampleCallback(void) {}
void GPUCPUINTCallback(void) {}

/* ---- stubs required by uart.c ---- */
struct VJSettings vjs;
static int stubIrqMask = 0;
static int stubPending = 0;
static int stubIpl = -1;
bool JERRYIRQEnabled(int irq) { return (stubIrqMask & irq) != 0; }
void JERRYSetPendingIRQ(int irq) { stubPending |= irq; }
void m68k_set_irq(unsigned int level) { stubIpl = (int)level; }
/* TOM INT1 enable byte: JERRY's interrupt output reaches the 68K only
   through bit 4 (C_JERENA, IRQ_DSP).  Stubbed rather than pulling in
   tom.c, which this test deliberately does not link. */
#define STUB_IRQ_DSP 4
static int stubTomInt1 = 0;
int TOMIRQEnabled(int irq) { return stubTomInt1 & (1 << irq); }

#define ASIDATA 0xF10030u
#define ASISTAT 0xF10032u
#define ASICTRL 0xF10032u
#define ASICLK  0xF10034u

#define ST_RBF   0x0080
#define ST_TBE   0x0100
#define ST_OE    0x0800
#define ST_SERIN 0x2000
#define ST_ERROR 0x8000
#define CT_TINTEN 0x0010
#define CT_RINTEN 0x0020
#define CT_CLRERR 0x0040

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (cond) printf("PASS %s\n", msg); \
         else { printf("FAIL %s\n", msg); failures++; } } while (0)

static void fresh(void)
{
    InitializeEventList();
    UARTReset();
    UARTSetWireSpeedup(1);        /* #498 enhancement off unless a test sets it */
    UARTSetLinkMode(JLINK_MODE_LOOPBACK);
    stubIrqMask = IRQ2_ASI;   /* J_ASYNENA on unless a test clears it */
    stubPending = 0;
    stubIpl = -1;
    stubTomInt1 = 1 << STUB_IRQ_DSP;   /* C_JERENA on unless a test clears it */
}

/* Fire JERRY events one at a time, mirroring the production loop:
   HandleNextEvent() dispatches the slot located by the LAST
   GetTimeToNextEvent() call, so the pair must always run together. */
static void pump(int max_events)
{
    int i;
    for (i = 0; i < max_events; i++)
    {
        (void)GetTimeToNextEvent(EVENT_JERRY);
        HandleNextEvent(EVENT_JERRY);
    }
}

static void test_reset_defaults(void)
{
    uint16_t st;
    fresh();
    st = UARTReadWord(ASISTAT);
    CHECK((st & ST_TBE) != 0,   "reset: TBE set");
    CHECK((st & ST_RBF) == 0,   "reset: RBF clear");
    CHECK((st & ST_ERROR) == 0, "reset: no error");
    CHECK((st & ST_SERIN) != 0, "reset: SERIN idles at mark");
    CHECK(UARTReadWord(ASICLK) == 0, "reset: ASICLK zero");
}

static void test_baud_timing(void)
{
    double t, expect;
    fresh();
    UARTWriteWord(ASICLK, 0);              /* N=0: frame = 11*16*1 clocks */
    UARTWriteWord(ASIDATA, 0x41);
    t = GetTimeToNextEvent(EVENT_JERRY);
    expect = 11.0 * 16.0 * 1.0 * RISC_CYCLE_IN_USEC;
    CHECK(fabs(t - expect) < 0.001, "N=0 frame time = 176 clocks");

    fresh();
    UARTWriteWord(ASICLK, 171);            /* ~9600 baud on NTSC clock */
    UARTWriteWord(ASIDATA, 0x41);
    t = GetTimeToNextEvent(EVENT_JERRY);
    expect = 11.0 * 16.0 * 172.0 * RISC_CYCLE_IN_USEC;
    CHECK(fabs(t - expect) < 0.01, "N=171 frame time = 302,720 clocks");
}

static void test_loopback_roundtrip(void)
{
    uint16_t st;
    fresh();
    UARTWriteWord(ASICLK, 0);
    UARTWriteWord(ASIDATA, 0x5A);
    st = UARTReadWord(ASISTAT);
    CHECK((st & ST_TBE) != 0, "straight-to-shift write keeps TBE");
    pump(1);                               /* TX completes -> byte to jlink */
    pump(1);                               /* RX frame completes -> RBF */
    st = UARTReadWord(ASISTAT);
    CHECK((st & ST_RBF) != 0, "byte looped back: RBF set");
    CHECK((UARTReadWord(ASIDATA) & 0xFF) == 0x5A, "ASIDATA returns byte");
    st = UARTReadWord(ASISTAT);
    CHECK((st & ST_RBF) == 0, "ASIDATA read clears RBF");
}

static void test_double_buffering(void)
{
    uint16_t st;
    fresh();
    UARTWriteWord(ASICLK, 0);
    UARTWriteWord(ASIDATA, 0x01);          /* -> shift */
    UARTWriteWord(ASIDATA, 0x02);          /* -> holding, TBE drops */
    st = UARTReadWord(ASISTAT);
    CHECK((st & ST_TBE) == 0, "holding full: TBE clear");
    pump(1);                               /* TX#1: byte out, holding -> shift */
    st = UARTReadWord(ASISTAT);
    CHECK((st & ST_TBE) != 0, "holding drained: TBE set");
    pump(2);                               /* TX#2 + RX#1 (tie order converges) */
    CHECK((UARTReadWord(ASIDATA) & 0xFF) == 0x01, "1st byte received first");
    pump(1);                               /* RX#2 delivers into now-empty RBF */
    CHECK((UARTReadWord(ASIDATA) & 0xFF) == 0x02, "2nd byte follows");
    /* NOTE: pump counts are exact (4 events scheduled in total) because
       HandleNextEvent() re-fires a stale slot if called on an empty list. */
}

static void test_overrun(void)
{
    uint16_t st;
    fresh();
    UARTWriteWord(ASICLK, 0);
    UARTWriteWord(ASIDATA, 0x01);
    UARTWriteWord(ASIDATA, 0x02);
    pump(4);                               /* TX#1, TX#2, RX#1, RX#2 — no reads */
    st = UARTReadWord(ASISTAT);
    CHECK((st & ST_RBF) != 0, "first byte still buffered");
    CHECK((st & ST_OE) != 0,  "second byte overruns: OE");
    CHECK((st & ST_ERROR) != 0, "ERROR mirrors OE");
    CHECK((UARTReadWord(ASIDATA) & 0xFF) == 0x01, "old data kept on overrun");
    UARTWriteWord(ASICTRL, CT_CLRERR);
    st = UARTReadWord(ASISTAT);
    CHECK((st & ST_OE) == 0 && (st & ST_ERROR) == 0, "CLRERR clears OE");
}

static void test_disabled_link(void)
{
    fresh();
    UARTSetLinkMode(JLINK_MODE_DISABLED);
    UARTWriteWord(ASICLK, 0);
    UARTWriteWord(ASIDATA, 0x7E);
    pump(1);                               /* TX only; nothing echoes back */
    CHECK((UARTReadWord(ASISTAT) & ST_RBF) == 0, "disabled: nothing echoes");
    CHECK((UARTReadWord(ASISTAT) & ST_TBE) != 0, "disabled: TX still drains");
}

static void test_rx_interrupt(void)
{
    fresh();
    UARTWriteWord(ASICLK, 0);
    UARTWriteWord(ASICTRL, CT_RINTEN);
    UARTWriteWord(ASIDATA, 0x99);
    pump(2);                               /* TX + RX frames */
    CHECK((stubPending & IRQ2_ASI) != 0, "RINTEN: RX raises IRQ2_ASI");
    CHECK(stubIpl == 2, "RX interrupt asserts 68K IPL2");
}

static void test_rx_interrupt_masked(void)
{
    fresh();
    stubIrqMask = 0;                       /* J_ASYNENA off */
    UARTWriteWord(ASICLK, 0);
    UARTWriteWord(ASICTRL, CT_RINTEN);
    UARTWriteWord(ASIDATA, 0x99);
    pump(2);
    CHECK(stubPending == 0, "JINTCTRL gate blocks RX IRQ");
    CHECK(stubIpl == -1, "no IPL2 when masked");
}

/* Regression (#UV voice-modem video corruption): Ultra Vortek enables
   RINTEN and JINTCTRL bit 4 but leaves TOM INT1 = $01 (video only), so
   C_JERENA is clear and the ASI interrupt must never reach the 68K.
   Without this gate every received byte took a spurious level-2
   exception, re-entering the game's display handler mid-field. */
static void test_rx_interrupt_tom_gate(void)
{
    fresh();
    stubTomInt1 = 0x01;                    /* video only: C_JERENA clear */
    UARTWriteWord(ASICLK, 0);
    UARTWriteWord(ASICTRL, CT_RINTEN);
    UARTWriteWord(ASIDATA, 0x99);
    pump(2);
    CHECK((stubPending & IRQ2_ASI) != 0,
          "C_JERENA clear: JERRY still latches ASI pending");
    CHECK(stubIpl == -1, "C_JERENA clear: no 68K IPL2 from UART RX");
}

static void test_rx_interrupt_disabled(void)
{
    fresh();                               /* mask on, RINTEN off */
    UARTWriteWord(ASICLK, 0);
    UARTWriteWord(ASIDATA, 0x99);
    pump(2);
    CHECK(stubPending == 0, "RINTEN off: RX raises nothing");
}

static void test_tx_interrupt_on_holding_drain(void)
{
    fresh();
    UARTWriteWord(ASICLK, 0);
    UARTWriteWord(ASICTRL, CT_TINTEN);
    UARTWriteWord(ASIDATA, 0x01);          /* straight to shift: no IRQ */
    CHECK(stubPending == 0, "straight-to-shift write raises no TX IRQ");
    UARTWriteWord(ASIDATA, 0x02);          /* parks in holding */
    pump(1);                               /* holding drains -> TBE edge */
    CHECK((stubPending & IRQ2_ASI) != 0, "TINTEN: holding drain raises IRQ");
}

/* ---- #498: opt-in netlink wire-latency enhancement ----
   UARTFrameUsec() is the single choke point for BOTH the TX drain and the
   RX arrival, so these cases pin the whole feature: the divisor applies,
   it only applies on an active link, and it changes nothing but time. */

/* Emulated microseconds until the pending UART character completes, for
   one byte written at the given ASICLK.  Uses ASICLK $56 (86) -- the
   value Ultra Vortek's Voice Modem driver settles at (docs/voice-modem.md),
   i.e. the case the enhancement exists for. */
static double frame_usec_at(unsigned speedup, int link_mode)
{
    fresh();
    UARTSetLinkMode(link_mode);
    UARTSetWireSpeedup(speedup);
    UARTWriteWord(ASICLK, 0x56);
    UARTWriteWord(ASIDATA, 0x41);
    return GetTimeToNextEvent(EVENT_JERRY);
}

static void test_wire_speedup_scales_frame_time(void)
{
    /* Stock reference computed the way uart.c's untouched branch does. */
    double stock = 11.0 * 16.0 * 87.0 * RISC_CYCLE_IN_USEC;
    double t;

    fresh();
    CHECK(UARTWireSpeedup() == 1, "wire speedup defaults to 1 (off)");

    t = frame_usec_at(1, JLINK_MODE_LOOPBACK);
    CHECK(fabs(t - stock) < 0.001, "speedup off: stock 576 us character frame");

    t = frame_usec_at(2, JLINK_MODE_LOOPBACK);
    CHECK(fabs(t - stock / 2.0) < 0.001, "speedup 2x halves the character frame");

    t = frame_usec_at(4, JLINK_MODE_LOOPBACK);
    CHECK(fabs(t - stock / 4.0) < 0.001, "speedup 4x quarters the character frame");
}

/* The gate that keeps every non-link user on stock timing: with no
   transport selected the divisor must be ignored outright, so a stale
   "4x" left in a config can never perturb a single-player session. */
static void test_wire_speedup_ignored_without_link(void)
{
    double stock = 11.0 * 16.0 * 87.0 * RISC_CYCLE_IN_USEC;
    double t = frame_usec_at(4, JLINK_MODE_DISABLED);
    CHECK(fabs(t - stock) < 0.001, "link disabled: 4x ignored, stock timing");
}

static void test_wire_speedup_clamps(void)
{
    fresh();
    UARTSetWireSpeedup(0);
    CHECK(UARTWireSpeedup() == 1, "divisor 0 clamps to 1");
    /* A RetroArch .cfg is a text file: the option layer never produces
       more than the option's largest value, but a hand-edited config can,
       and an unbounded divisor drives the character time toward zero. */
    UARTSetWireSpeedup(100000);
    CHECK(UARTWireSpeedup() == UART_WIRE_SPEEDUP_MAX,
          "absurd divisor clamps to UART_WIRE_SPEEDUP_MAX");
}

/* "Nothing game-observable changes except timing": the same bytes arrive
   in the same order with the same status-register transitions at 4x as at
   1x.  Only the emulated interval between them differs. */
static void test_wire_speedup_stream_identical(void)
{
    uint16_t st;
    fresh();
    UARTSetWireSpeedup(4);
    UARTWriteWord(ASICLK, 0x56);
    UARTWriteWord(ASIDATA, 0x01);          /* -> shift */
    UARTWriteWord(ASIDATA, 0x02);          /* -> holding */
    st = UARTReadWord(ASISTAT);
    CHECK((st & ST_TBE) == 0, "4x: holding full still clears TBE");
    pump(1);
    st = UARTReadWord(ASISTAT);
    CHECK((st & ST_TBE) != 0, "4x: holding drained still sets TBE");
    pump(2);
    CHECK((UARTReadWord(ASIDATA) & 0xFF) == 0x01, "4x: 1st byte received first");
    pump(1);
    CHECK((UARTReadWord(ASIDATA) & 0xFF) == 0x02, "4x: 2nd byte follows");
    CHECK((UARTReadWord(ASISTAT) & ST_OE) == 0, "4x: no overrun on read-per-byte");
}

/* The hazard that decides how far the enhancement may go, and the reason
   the accelerated receiver applies back-pressure.  test_overrun() above
   pins the stock behaviour: a character completing into a full RBF is an
   overrun and the NEW byte is lost.  That is correct hardware, but the
   reader's polling rate belongs to the game, so dividing the character
   time divides the reader's budget with it -- and a dropped byte in a
   framed lockstep protocol is a desync, i.e. a game-observable change.
   Measured, not theorised: at 4x with the plain hardware rule, Ultra
   Vortek's DSP-poll driver lost the second byte of the $B800 wake reply
   and the modem handshake never completed (0 pad words vs 7044 stock).
   Accelerated, the byte waits on the wire instead. */
static void test_wire_speedup_no_overrun_loss(void)
{
    uint16_t st;
    fresh();
    UARTSetWireSpeedup(4);
    UARTWriteWord(ASICLK, 0x56);
    UARTWriteWord(ASIDATA, 0x01);
    UARTWriteWord(ASIDATA, 0x02);
    pump(4);                               /* TX#1, TX#2, RX#1 -- no reads */
    st = UARTReadWord(ASISTAT);
    CHECK((st & ST_RBF) != 0, "4x: first byte buffered");
    CHECK((st & ST_OE) == 0,  "4x: unread RBF holds the wire, no overrun");
    CHECK((UARTReadWord(ASIDATA) & 0xFF) == 0x01, "4x: first byte intact");
    pump(1);                               /* the held byte now clocks in */
    CHECK((UARTReadWord(ASISTAT) & ST_RBF) != 0, "4x: held byte then arrives");
    CHECK((UARTReadWord(ASIDATA) & 0xFF) == 0x02,
          "4x: second byte DELIVERED, not dropped");
}

/* The invariant that lets UARTKickRx()'s back-pressure test omit a
   JLinkMode() term: dropping the link drains the RX ring, so "speedup set
   but no transport selected" can never coexist with a byte pending on the
   wire.  If a future transport change stops clearing the ring on close,
   this fails here rather than silently applying accelerated back-pressure
   while UARTFrameUsec() is on its stock branch. */
static void test_wire_speedup_link_drop_drains(void)
{
    fresh();
    UARTSetWireSpeedup(4);
    UARTWriteWord(ASICLK, 0x56);
    UARTWriteWord(ASIDATA, 0x11);
    UARTWriteWord(ASIDATA, 0x22);
    pump(2);                               /* both TX frames -> loopback ring */
    UARTSetLinkMode(JLINK_MODE_DISABLED);  /* pull the cable */
    pump(2);
    CHECK((UARTReadWord(ASISTAT) & ST_RBF) == 0,
          "link dropped: RX ring drained, nothing pending to hold");
}

int main(void)
{
    vjs.hardwareTypeNTSC = true;
    test_reset_defaults();
    test_baud_timing();
    test_loopback_roundtrip();
    test_double_buffering();
    test_overrun();
    test_disabled_link();
    test_rx_interrupt();
    test_rx_interrupt_masked();
    test_rx_interrupt_tom_gate();
    test_rx_interrupt_disabled();
    test_tx_interrupt_on_holding_drain();
    test_wire_speedup_scales_frame_time();
    test_wire_speedup_ignored_without_link();
    test_wire_speedup_clamps();
    test_wire_speedup_stream_identical();
    test_wire_speedup_no_overrun_loss();
    test_wire_speedup_link_drop_drains();
    printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
