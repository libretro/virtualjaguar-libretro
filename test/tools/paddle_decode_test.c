/*
 * test/tools/paddle_decode_test.c -- register-level test for the $F17C00
 * motherboard paddle ADC (#505).
 *
 * WHAT THIS PINS, AND WHY EACH ITEM IS HERE
 * =========================================
 * The hardware model, its citations and its deliberate simplifications
 * are in src/jerry/paddle.h.  This suite asserts the parts of it that a
 * future edit could plausibly get wrong:
 *
 *   - THE THREE STATES.  No paddle selected -> $FF (a retail console with
 *     no converter fitted); selected, but this channel's port has nothing
 *     plugged in -> $00; selected -> the live axis.  The middle one is the
 *     case a naive implementation gets wrong by returning $FF or leaking
 *     the other port's value, and it is reachable in a real session (a
 *     paddle on port 1 while BattleSphere reads channels 2 and 3).
 *
 *   - THE DEFAULT IS INERT.  With no paddle selected the register is
 *     byte-for-byte what the core returned before this file existed,
 *     writes included.  Club Drive writes the channel select and never
 *     reads it, so the write path has to be inert too, not just the read.
 *
 *   - THE PROTOCOL, driven as BattleSphere's Timer 1 ISR at $83E75C
 *     actually drives it: read the completed conversion, store it, then
 *     write (channel | 4) to start the next.  A round robin over all four
 *     channels must land the right sample in the right slot, which is the
 *     one-behind pipelining the model reproduces.
 *
 *   - THE POLARITY.  A rising channel-3 count is DOWNWARD stick motion,
 *     because BattleSphere's calibrator ($827DDC..$827E14) uses that byte
 *     directly as a screen Y coordinate.  This is the assertion that
 *     stops someone "fixing" the feed into agreement with #437's
 *     TR10-mandated Y inversion, which belongs to a different device.
 *
 *   - THE MATRIX IS UNTOUCHED.  A paddle is pots on separate connector
 *     pins; the port's digital pad stays live.  Every row of
 *     $F14000/$F14002 must read exactly as it does with a plain pad --
 *     the opposite of the analog controller, which takes the port over.
 *
 *   - THE SAVESTATE.  The latched conversion is what the next read
 *     returns, so it has to survive a rollback (#400).
 *
 * SYNTHETIC, but not for #437's reason.  A released consumer does exist
 * (BattleSphere, behind its own Gameplay Options > 2nd Controller menu),
 * and this suite drives the register the way that consumer's ISR does.
 * What it cannot do is press through the game's menus, so the game-level
 * half is manual.
 *
 * USAGE
 *   ./test/tools/paddle_decode_test ./virtualjaguar_libretro.dylib [rom]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../harness/harness.h"

/* Mirrors src/jerry/inputdev.h -- restated by hand on purpose, so a
 * renumbering there cannot silently propagate here (same reasoning as
 * mouse_decode_test.c and analog_decode_test.c). */
typedef enum {
   DEV_PAD                 = 0,
   DEV_MOUSE_ST            = 1,
   DEV_MOUSE_AMIGA_ADAPTER = 2,
   DEV_MOUSE_AMIGA_ON_ST   = 3,
   DEV_ROTARY              = 4,
   DEV_ANALOG              = 5,
   DEV_DRIVING             = 6,
   DEV_LIGHTGUN            = 7,
   DEV_PADDLE              = 8
} InputDevType;

#define AXIS_X 0
#define AXIS_Y 1

#define PADDLE_BASE  0xF17C00u
#define PADDLE_LAST  0xF17FFEu

/* The value a retail console returns: no converter on the board. */
#define NOT_FITTED   0x00FFu
/* Converter fitted, nothing plugged into that pot line. */
#define NOT_PLUGGED  0x0000u

/* Socket-0 row-select words with the output enable set, as in
 * analog_decode_test.c: port 1 rows live in the low nibble, port 2 rows in
 * the high nibble, and the unused port's nibble is F (no row). */
static const uint16_t row_word_p1[4] = { 0x81FEu, 0x81FDu, 0x81FBu, 0x81F7u };
static const uint16_t row_word_p2[4] = { 0x817Fu, 0x81BFu, 0x81DFu, 0x81EFu };

static uint16_t (*p_JERRYReadWord)(uint32_t, uint32_t);
static void     (*p_JERRYWriteWord)(uint32_t, uint16_t, uint32_t);
static uint8_t  (*p_JERRYReadByte)(uint32_t, uint32_t);
static void     (*p_JERRYWriteByte)(uint32_t, uint8_t, uint32_t);
static uint16_t (*p_JoyReadWord)(uint32_t);
static void     (*p_JoyWriteWord)(uint32_t, uint16_t);
static void     (*p_SetType)(int, InputDevType);
static InputDevType (*p_GetType)(int);
static void     (*p_SetTune)(int, int, int32_t, int32_t, int32_t);
static void     (*p_FeedPaddle)(int, int32_t, int32_t);

static size_t (*p_serialize_size)(void);
static bool   (*p_serialize)(void *, size_t);
static bool   (*p_unserialize)(const void *, size_t);

/* ---- reporting ------------------------------------------------------- */

static int  failures;
static char detail[320];

static void report(int cond, const char *what)
{
   if (cond)
      printf("  ok   %s\n", what);
   else
   {
      printf("  FAIL %s\n", what);
      failures++;
   }
}

/* ---- the BattleSphere driver ----------------------------------------- */

/* MUX word the ROM writes to select a channel and start a conversion:
 * `addq.w #4,d1` on a channel counter masked to 0..3. */
static uint16_t mux_word(int channel)
{
   return (uint16_t)((channel & 3) + 4);
}

static uint16_t adc_read(void)
{
   return p_JERRYReadWord(PADDLE_BASE, 0);
}

static void adc_select(int channel)
{
   p_JERRYWriteWord(PADDLE_BASE, mux_word(channel), 0);
}

/* One turn of BattleSphere's Timer 1 handler: read the completed
 * conversion into the caller's buffer slot, advance the channel counter,
 * start the next conversion.  Faithful to $83E75C, read-before-write and
 * all -- which is exactly why a conversion is one tick behind its select. */
static void isr_tick(uint8_t buf[4], int *channel)
{
   buf[*channel] = (uint8_t)(adc_read() & 0xFF);
   *channel      = (*channel + 1) & 3;
   adc_select(*channel);
}

/* Run the round robin long enough for every slot to hold a settled
 * sample: two full laps from a cold converter. */
static void isr_run(uint8_t buf[4])
{
   int channel = 0;
   int i;

   memset(buf, 0xAA, 4);

   adc_select(channel);
   for (i = 0; i < 9; i++)
      isr_tick(buf, &channel);
}

static void attach(int port, InputDevType t)
{
   p_SetType(port, t);
}

static void detach_all(void)
{
   p_SetType(0, DEV_PAD);
   p_SetType(1, DEV_PAD);
}

/* Identity tuning, so a case that is not about tuning cannot be perturbed
 * by a previous one that was. */
static void tune_identity(int port)
{
   p_SetTune(port, AXIS_X, 0, 0, 256);
   p_SetTune(port, AXIS_Y, 0, 0, 256);
}

/* ---- cases ----------------------------------------------------------- */

/* A session that never selects a paddle must be indistinguishable from a
 * core built before #505 existed: $00FF from every read, and writes that
 * change nothing.  This is the whole safety argument for shipping the
 * feature enabled-by-selection rather than always-on. */
static void test_not_fitted(void)
{
   int      ch;
   int      inert = 1;
   uint32_t addr;

   detach_all();

   report(adc_read() == NOT_FITTED,
          "no paddle selected: $F17C00 reads $00FF (no converter fitted)");

   for (ch = 0; ch < 4; ch++)
   {
      adc_select(ch);
      if (adc_read() != NOT_FITTED)
         inert = 0;
   }
   report(inert,
          "no paddle selected: a channel select is swallowed, every "
          "channel still reads $00FF (Club Drive writes and never reads)");

   /* GPIO5's chip select covers the whole range, so the far end of it
    * decodes to the same converter and must give the same answer. */
   inert = 1;
   for (addr = PADDLE_BASE; addr <= PADDLE_LAST; addr += 0x100)
      if (p_JERRYReadWord(addr, 0) != NOT_FITTED)
         inert = 0;
   report(inert,
          "no paddle selected: the whole $F17C00-$F17FFF decode reads "
          "$00FF, not just the first word");

   report(p_JERRYReadByte(PADDLE_BASE + 1, 0) == 0xFF
          && p_JERRYReadByte(PADDLE_BASE, 0) == 0x00,
          "byte access follows the word: data lines on the low byte, so "
          "the odd address carries $FF and the even one $00");
}

/* Selecting a paddle fits the converter.  Unselecting it unfits it again
 * -- a user who tries the device and switches back must get the retail
 * console's $FF, not a converter stuck on the board. */
static void test_fit_unfit(int port)
{
   detach_all();
   tune_identity(port);

   attach(port, DEV_PADDLE);
   sprintf(detail, "InputDevSetType accepts a paddle on port %d", port + 1);
   report(p_GetType(port) == DEV_PADDLE, detail);

   /* Assert the VALUE, not merely "not $FF".  A freshly attached stick
    * reads CENTRE, and that is load-bearing rather than cosmetic: it is
    * the reason this device has no engagement latch (an un-engaged one
    * would sit at the $00 rail, which BattleSphere's "align the crosshair
    * with the stick centred" screen could never be satisfied from).  A
    * bare != $FF would pass on a converter that had regressed to the
    * rail. */
   adc_select(port * 2);
   sprintf(detail,
           "port %d paddle: fitted, and an attached but untouched stick "
           "converts to centre ($80) -- not a rail", port + 1);
   report(adc_read() == 0x0080u, detail);

   attach(port, DEV_PAD);
   adc_select(port * 2);
   sprintf(detail,
           "port %d paddle removed: $F17C00 is back to $00FF", port + 1);
   report(adc_read() == NOT_FITTED, detail);

   detach_all();
}

/* Converter fitted, but only one socket has a stick in it.  The other
 * socket's two channels are unconnected pot lines sitting at the bottom
 * rail -- $00, NOT $FF (that would claim no converter) and NOT the
 * attached port's value (that would be a channel-decode bug). */
static void test_unplugged_channels(void)
{
   uint8_t buf[4];

   detach_all();
   tune_identity(0);
   tune_identity(1);

   attach(0, DEV_PADDLE);
   p_FeedPaddle(0, 32767, -32768);   /* hard right, hard up */
   isr_run(buf);

   report(buf[0] == 0xFF && buf[1] == 0x01,
          "paddle on port 1 only: channels 0/1 carry port 1's axes");
   report(buf[2] == 0x00 && buf[3] == 0x00,
          "paddle on port 1 only: channels 2/3 read $00 -- converter "
          "fitted, nothing plugged into port 2's pot lines");

   detach_all();
}

/* BattleSphere's own consumption pattern: a paddle on port 2, its ISR
 * round robin, and the two channels the game reads.  This is the case the
 * feature exists for. */
static void test_battlesphere_shape(void)
{
   uint8_t buf[4];

   detach_all();
   tune_identity(1);
   attach(1, DEV_PADDLE);

   p_FeedPaddle(1, 0, 0);
   isr_run(buf);
   report(buf[2] == 0x80 && buf[3] == 0x80,
          "port 2 paddle, stick centred: channels 2 and 3 read $80 -- "
          "what BattleSphere's calibrator is asked to align on");
   report(buf[0] == 0x00 && buf[1] == 0x00,
          "port 2 paddle: port 1's channels stay at the unplugged $00");

   p_FeedPaddle(1, 32767, 0);
   isr_run(buf);
   report(buf[2] == 0xFF, "stick hard right: channel 2 reads full scale");

   p_FeedPaddle(1, -32768, 0);
   isr_run(buf);
   report(buf[2] == 0x01, "stick hard left: channel 2 reads bottom of scale");

   /* THE POLARITY ASSERTION.  BattleSphere's calibrator loads $1F9317
    * (channel 3) and uses it directly as the crosshair's SCREEN Y, which
    * grows downward -- so libretro's +Y (down) must be the HIGH count.
    * #437's device inverts here; this one must not. */
   p_FeedPaddle(1, 0, 32767);
   isr_run(buf);
   report(buf[3] == 0xFF,
          "stick pushed DOWN (+Y): channel 3 reads full scale -- not "
          "inverted, per BattleSphere's calibrator using it as screen Y");

   p_FeedPaddle(1, 0, -32768);
   isr_run(buf);
   report(buf[3] == 0x01, "stick pushed UP (-Y): channel 3 reads bottom");

   detach_all();
}

/* The one-behind pipeline.  A read hands back the conversion started by
 * the PREVIOUS write, which is the whole reason BattleSphere's ISR stores
 * into the slot named by the counter it has not yet advanced.  Drive four
 * distinct values through the four channels and check every slot at once:
 * an off-by-one in the model puts each sample in the wrong place. */
static void test_channel_round_robin(void)
{
   uint8_t buf[4];

   detach_all();
   tune_identity(0);
   tune_identity(1);
   attach(0, DEV_PADDLE);
   attach(1, DEV_PADDLE);

   /* Four values that cannot be confused with each other, one per
    * channel: port 1 = (right, up), port 2 = (left, down). */
   p_FeedPaddle(0, 32767, -32768);
   p_FeedPaddle(1, -32768, 32767);

   isr_run(buf);

   sprintf(detail,
           "round robin lands each channel in its own slot "
           "(got %02X %02X %02X %02X)", buf[0], buf[1], buf[2], buf[3]);
   report(buf[0] == 0xFF && buf[1] == 0x01
          && buf[2] == 0x01 && buf[3] == 0xFF, detail);

   /* A read with no intervening write repeats the same completed
    * conversion -- the part holds its output register until the next one. */
   adc_select(2);
   report(adc_read() == adc_read(),
          "a repeated read with no new select returns the same completed "
          "conversion");

   detach_all();
}

/* A MUX word outside the single-ended encoding addresses the part's
 * differential pairs, which the Jaguar's four independent pot lines are
 * not wired for.  Read as unconnected rather than silently aliased onto a
 * channel -- otherwise a stray write would look like stick data. */
static void test_mux_mode_field(void)
{
   detach_all();
   tune_identity(1);
   attach(1, DEV_PADDLE);
   /* Two values that are neither $FF nor $00, so no assertion below can
    * pass by accidentally matching the not-fitted or unplugged reading. */
   p_FeedPaddle(1, 16384, -16384);             /* X -> $BF, Y -> $41 */

   p_JERRYWriteWord(PADDLE_BASE, 0x0002, 0);   /* MA3 MA2 = 0 0 */
   report(adc_read() == NOT_PLUGGED,
          "a differential MUX address converts nothing: reads $00, not a "
          "channel aliased off the low two bits");

   p_JERRYWriteWord(PADDLE_BASE, 0x0006, 0);   /* single-ended, channel 2 */
   report(adc_read() == 0x00BFu,
          "the single-ended encoding (channel | 4) is what selects a "
          "channel");

   /* Only the low byte reaches the MUX latch: the converter's data lines
    * are the bus's low half. */
   p_JERRYWriteWord(PADDLE_BASE, 0xFF07, 0);
   report(adc_read() == 0x0041u,
          "the high byte of a write is ignored -- MA3..MA0 come off data "
          "bits 3..0");

   /* Same rule at byte width: the odd address is the low half and is the
    * one that latches. */
   p_JERRYWriteByte(PADDLE_BASE + 1, 0x06, 0);
   report(adc_read() == 0x00BFu,
          "an odd-address byte write latches the MUX just as a word write "
          "does");
   p_JERRYWriteByte(PADDLE_BASE, 0x07, 0);
   report(adc_read() == 0x00BFu,
          "an even-address byte write is the high half and latches "
          "nothing");

   detach_all();
}

/* A paddle must be invisible to the joystick matrix.  Scan every row of
 * both ports with a paddle attached and with a plain pad, and require the
 * two scans to be word-identical: the pots are separate connector pins,
 * so the digital pad on a paddle port keeps working. */
static void test_matrix_untouched(void)
{
   uint16_t with_pad[2][4][2];
   uint16_t with_paddle[2][4][2];
   int      port, row;
   int      same = 1;

   detach_all();

   for (port = 0; port < 2; port++)
      for (row = 0; row < 4; row++)
      {
         p_JoyWriteWord(0, port ? row_word_p2[row] : row_word_p1[row]);
         with_pad[port][row][0] = p_JoyReadWord(0);
         with_pad[port][row][1] = p_JoyReadWord(2);
      }

   attach(0, DEV_PADDLE);
   attach(1, DEV_PADDLE);
   tune_identity(0);
   tune_identity(1);
   p_FeedPaddle(0, 32767, -32768);
   p_FeedPaddle(1, -32768, 32767);

   for (port = 0; port < 2; port++)
      for (row = 0; row < 4; row++)
      {
         p_JoyWriteWord(0, port ? row_word_p2[row] : row_word_p1[row]);
         with_paddle[port][row][0] = p_JoyReadWord(0);
         with_paddle[port][row][1] = p_JoyReadWord(2);
      }

   for (port = 0; port < 2; port++)
      for (row = 0; row < 4; row++)
         if (with_paddle[port][row][0] != with_pad[port][row][0]
             || with_paddle[port][row][1] != with_pad[port][row][1])
            same = 0;

   report(same,
          "a deflected paddle on BOTH ports leaves every row of "
          "$F14000/$F14002 bit-identical to a plain pad");

   detach_all();
}

/* Tuning goes through the shared absolute path (#439), the same one #437
 * uses, so the user's dead zone / offset means the same thing on both
 * devices.  Two cases are enough to prove the layer is reached at all and
 * that it is reached in the right orientation. */
static void test_tuning(void)
{
   uint8_t buf[4];

   detach_all();
   attach(1, DEV_PADDLE);
   tune_identity(1);

   /* Absolute dead zone RE-BASES: just inside it reads centre, and full
    * deflection still reads full scale (axistune.h). */
   p_SetTune(1, AXIS_X, 32, 0, 256);
   p_FeedPaddle(1, 32767 / 8, 0);       /* ~16 counts, inside a 32 dz */
   isr_run(buf);
   report(buf[2] == 0x80, "dead zone: a deflection inside it reads centre");

   p_FeedPaddle(1, 32767, 0);
   isr_run(buf);
   report(buf[2] == 0xFF,
          "dead zone re-bases rather than gates: full deflection still "
          "reads full scale");

   /* Offset moves the rest position, which is its entire job: a centred
    * stick must read -offset. */
   p_SetTune(1, AXIS_X, 0, 16, 256);
   p_FeedPaddle(1, 0, 0);
   isr_run(buf);
   report(buf[2] == (uint8_t)(0x80 - 16),
          "offset biases the rest position: a centred stick reads "
          "centre - offset");

   tune_identity(1);
   detach_all();
}

/* The latched conversion decides what the next read returns, so a
 * rollback that loses it replays a different byte into the game's sample
 * buffer -- issue #400's class of bug. */
static void test_savestate(void)
{
   uint8_t *snap;
   size_t   size;
   uint16_t before, after;

   detach_all();
   attach(1, DEV_PADDLE);
   tune_identity(1);

   if (!p_serialize_size || !p_serialize || !p_unserialize)
   {
      report(0, "savestate symbols available");
      detach_all();
      return;
   }

   size = p_serialize_size();
   snap = (uint8_t *)malloc(size);
   if (!snap)
   {
      report(0, "savestate buffer allocated");
      detach_all();
      return;
   }

   /* Latch a conversion of a hard-right stick, then save. */
   p_FeedPaddle(1, 32767, 0);
   adc_select(2);
   before = adc_read();

   report(p_serialize(snap, size), "retro_serialize with a paddle attached");

   /* Move the stick and convert again, so the live latch no longer
    * matches the saved one. */
   p_FeedPaddle(1, -32768, 0);
   adc_select(2);
   report(adc_read() != before,
          "a fresh conversion after moving the stick differs from the "
          "saved one (the test would be vacuous otherwise)");

   report(p_unserialize(snap, size), "retro_unserialize");

   after = adc_read();
   sprintf(detail,
           "the restored state replays the saved conversion "
           "($%04X, got $%04X)", before, after);
   report(after == before, detail);

   free(snap);
   detach_all();
}

int main(int argc, char **argv)
{
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   harness_result result;
   char           summary[128];
   int            port;

   cfg.frames = 0;
   if (!harness_init_from_args(&cfg, argc, argv))
      return 1;

   if (!cfg.rom_path)
      cfg.rom_path = "test/roms/yarc.j64";
   if (!harness_load_rom(&cfg))
      return 1;

   p_JERRYReadWord  = (uint16_t (*)(uint32_t, uint32_t))
                         harness_dlsym(&cfg, "JERRYReadWord");
   p_JERRYWriteWord = (void (*)(uint32_t, uint16_t, uint32_t))
                         harness_dlsym(&cfg, "JERRYWriteWord");
   p_JERRYReadByte  = (uint8_t (*)(uint32_t, uint32_t))
                         harness_dlsym(&cfg, "JERRYReadByte");
   p_JERRYWriteByte = (void (*)(uint32_t, uint8_t, uint32_t))
                         harness_dlsym(&cfg, "JERRYWriteByte");
   p_JoyReadWord    = (uint16_t (*)(uint32_t))
                         harness_dlsym(&cfg, "JoystickReadWord");
   p_JoyWriteWord   = (void (*)(uint32_t, uint16_t))
                         harness_dlsym(&cfg, "JoystickWriteWord");
   p_SetType        = (void (*)(int, InputDevType))
                         harness_dlsym(&cfg, "InputDevSetType");
   p_GetType        = (InputDevType (*)(int))
                         harness_dlsym(&cfg, "InputDevGetType");
   p_SetTune        = (void (*)(int, int, int32_t, int32_t, int32_t))
                         harness_dlsym(&cfg, "InputDevSetTune");
   p_FeedPaddle     = (void (*)(int, int32_t, int32_t))
                         harness_dlsym(&cfg, "InputDevFeedPaddle");

   p_serialize_size = (size_t (*)(void))
                         harness_dlsym(&cfg, "retro_serialize_size");
   p_serialize      = (bool (*)(void *, size_t))
                         harness_dlsym(&cfg, "retro_serialize");
   p_unserialize    = (bool (*)(const void *, size_t))
                         harness_dlsym(&cfg, "retro_unserialize");

   if (!p_JERRYReadWord || !p_JERRYWriteWord || !p_JERRYReadByte
       || !p_JERRYWriteByte || !p_JoyReadWord || !p_JoyWriteWord
       || !p_SetType || !p_GetType || !p_SetTune || !p_FeedPaddle)
   {
      fprintf(stderr,
              "paddle_decode_test: missing test-ABI symbols.  The wide "
              "test ABI must export JERRY*, Joystick* and InputDev* "
              "(exports-test.list and link-test.T).  Build with "
              "TEST_EXPORTS=1.\n");
      harness_shutdown(&cfg);
      return 1;
   }

   printf("paddle ADC ($F17C00) register-level test (#505)\n");

   test_not_fitted();

   for (port = 0; port < 2; port++)
      test_fit_unfit(port);

   test_unplugged_channels();
   test_battlesphere_shape();
   test_channel_round_robin();
   test_mux_mode_field();
   test_matrix_untouched();
   test_tuning();
   test_savestate();

   /* Leave the machine as we found it: every later test in the suite
    * shares this process only through the core's statics, but a harness
    * run that ends with a device attached is a trap for the next one. */
   detach_all();

   sprintf(summary, "%d failure(s)", failures);
   result.status = failures ? "FAIL" : "PASS";
   result.name   = "paddle_decode";
   result.detail = summary;
   harness_report(&cfg, &result, 1);

   harness_shutdown(&cfg);
   return failures ? 1 : 0;
}
