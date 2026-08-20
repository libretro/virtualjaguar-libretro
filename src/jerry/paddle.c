/*
 * src/jerry/paddle.c -- the $F17C00 motherboard ADC (#505).
 *
 * Every hardware fact, every citation and every deliberate simplification
 * is in paddle.h.  Read it first; this file is only the arithmetic.
 */

#include <string.h>

#include "paddle.h"
#include "state.h"

/* Value a read returns with no converter on the board.  A retail Jaguar
 * has none, its data lines float high, and software sees $FF (JANALOG;
 * see paddle.h).  This is what the core shipped in v3.4.0 for every read
 * of the range, and it stays the default. */
#define PADDLE_NOT_FITTED   0xFF

/* Value a channel reads when the converter IS on the board but nothing is
 * plugged into that pot line: the input sits at the bottom rail. */
#define PADDLE_NOT_PLUGGED  0x00

/* MUX address bits, off data bits 3..0 (ADC0844, see paddle.h).
 * PADDLE_MUX_MODE_MASK selects the mode field (MA3, MA2) and
 * PADDLE_MUX_SINGLE_ENDED is its single-ended encoding, MA3 = 0, MA2 = 1
 * -- the value all three known writers set. */
#define PADDLE_MUX_MASK           0x0F
#define PADDLE_MUX_MODE_MASK      0x0C
#define PADDLE_MUX_SINGLE_ENDED   0x04
#define PADDLE_MUX_CHANNEL_MASK   0x03

/* Live axis values, ADC counts, indexed by channel: 0 = port 1 X,
 * 1 = port 1 Y, 2 = port 2 X, 3 = port 2 Y (paddle.h, CHANNEL -> AXIS). */
static uint8_t paddle_axis[4];

/* Per-port attach flags, and the derived "a converter is on the board"
 * state.  Both option-derived; neither is serialized. */
static uint8_t paddle_attached[2];

/* Converter state, and the only two machine-visible bytes here: the
 * latched MUX address and the completed conversion a read hands back. */
static uint8_t paddle_mux;
static uint8_t paddle_result;

static void paddle_reset_converter(void)
{
   /* A converter that has been selected but never written has performed no
    * conversion, so its output register holds nothing: $00, the same
    * bottom-rail value an unplugged channel gives.  BattleSphere's ISR
    * reads before it writes, so this IS the first byte it stores -- and it
    * is overwritten four Timer 1 ticks later, once the round robin has
    * been through every channel. */
   paddle_mux    = 0;
   paddle_result = 0;
}

void PaddleInit(void)
{
   /* Centre every axis: an attached-but-unfed stick is at rest, not at a
    * rail (paddle.h, NO ENGAGEMENT LATCH). */
   paddle_axis[0] = 0x80;
   paddle_axis[1] = 0x80;
   paddle_axis[2] = 0x80;
   paddle_axis[3] = 0x80;

   paddle_reset_converter();
}

void PaddleReset(void)
{
   /* Converter state only.  Which sockets have a paddle plugged in is
    * option-derived and survives a machine reset, exactly as a physical
    * controller stays plugged in across one -- the same rule
    * InputDevReset() follows for the device type. */
   paddle_reset_converter();
}

void PaddleShutdown(void)
{
   /* iOS cannot dlclose a core, so every static goes back to its
    * load-time value here, the option-derived ones included. */
   memset(paddle_axis,     0, sizeof(paddle_axis));
   memset(paddle_attached, 0, sizeof(paddle_attached));
   paddle_mux    = 0;
   paddle_result = 0;
}

void PaddleSetAttached(int port, int attached)
{
   if (port < 0 || port > 1)
      return;

   attached = attached ? 1 : 0;

   if (paddle_attached[port] == (uint8_t)attached)
      return;

   paddle_attached[port] = (uint8_t)attached;

   /* A freshly plugged stick is centred, and a freshly unplugged one
    * leaves no stale deflection behind for the other port's channels to
    * be read against. */
   paddle_axis[port * 2 + 0] = 0x80;
   paddle_axis[port * 2 + 1] = 0x80;
}

int PaddleFitted(void)
{
   return (paddle_attached[0] || paddle_attached[1]) ? 1 : 0;
}

void PaddleFeed(int port, uint8_t x, uint8_t y)
{
   if (port < 0 || port > 1)
      return;
   if (!paddle_attached[port])
      return;

   paddle_axis[port * 2 + 0] = x;
   paddle_axis[port * 2 + 1] = y;
}

/* The conversion itself: MUX address in, eight bits out. */
static uint8_t paddle_convert(uint8_t mux)
{
   unsigned channel;

   /* A mode field other than single-ended addresses the part's
    * differential / pseudo-differential pairs, which the Jaguar's four
    * independent pot lines are not wired for and which no known writer
    * selects.  Read it as an unconnected input rather than inventing a
    * pairing. */
   if ((mux & PADDLE_MUX_MODE_MASK) != PADDLE_MUX_SINGLE_ENDED)
      return PADDLE_NOT_PLUGGED;

   channel = mux & PADDLE_MUX_CHANNEL_MASK;

   if (!paddle_attached[channel >> 1])
      return PADDLE_NOT_PLUGGED;

   return paddle_axis[channel];
}

uint16_t PaddleReadWord(uint32_t offset)
{
   (void)offset;

   if (!PaddleFitted())
      return PADDLE_NOT_FITTED;

   return paddle_result;
}

uint8_t PaddleReadByte(uint32_t offset)
{
   /* Data lines on the low byte (paddle.h): the even address is the high
    * half of the word and carries nothing. */
   if (!(offset & 1))
      return 0x00;

   return (uint8_t)PaddleReadWord(offset & ~(uint32_t)1);
}

void PaddleWriteWord(uint32_t offset, uint16_t data)
{
   (void)offset;

   /* With no converter on the board the write has nowhere to land.
    * Swallowed rather than passed on -- letting it reach the EEPROM
    * catch-all is the bug v3.4.0 fixed. */
   if (!PaddleFitted())
      return;

   paddle_mux    = (uint8_t)(data & PADDLE_MUX_MASK);
   paddle_result = paddle_convert(paddle_mux);
}

void PaddleWriteByte(uint32_t offset, uint8_t data)
{
   if (!(offset & 1))
      return;

   PaddleWriteWord(offset & ~(uint32_t)1, data);
}

size_t PaddleStateSave(uint8_t *buf)
{
   uint8_t *start = buf;

   STATE_SAVE_VAR(buf, paddle_mux);
   STATE_SAVE_VAR(buf, paddle_result);

   return (size_t)(buf - start);
}

size_t PaddleStateLoad(const uint8_t *buf)
{
   const uint8_t *start = buf;

   STATE_LOAD_VAR(buf, paddle_mux);
   STATE_LOAD_VAR(buf, paddle_result);

   /* A savestate is untrusted input.  The result byte is full-range by
    * construction; the MUX latch is four bits wide on the part, so the
    * upper nibble cannot have come from real hardware. */
   paddle_mux &= PADDLE_MUX_MASK;

   return (size_t)(buf - start);
}
