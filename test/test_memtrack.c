/*
 * test_memtrack.c — Memory Track cartridge protocol.
 *
 * No title we have exercises the Memory Track (see #258), so this test drives
 * the device directly against the protocol documented by the MiSTer Atari
 * Jaguar core (https://github.com/MiSTer-devel/Jaguar_MiSTer, Jaguar.sv) —
 * the only known-working open-source implementation, and effectively the
 * spec, since neither the Jaguar TRM nor the official SDK covers this part.
 *
 * Guards the three things that were wrong before:
 *   1. the NVRAM window is $900000, not the $8xxxxx ROM window;
 *   2. the device claims ONLY its own addresses, so a CD BIOS at $800000
 *      survives;
 *   3. unlock -> ID mode reports ATMEL AT29C010 at $800000 / $800004.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 $(INCFLAGS) -o test/test_memtrack \
 *      test/test_memtrack.c src/core/memtrack.c -lm
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "memtrack.h"

static int failures;
static int checks;

static void ok(const char *what, int cond)
{
   checks++;
   if (cond)
   {
      printf("  PASS  %s\n", what);
   }
   else
   {
      printf("  FAIL  %s\n", what);
      failures++;
   }
}

/* The unlock sequence: $AA -> $815554, $55 -> $80AAA8, <cmd> -> $815554. */
static void unlock(uint16_t cmd)
{
   MTWriteWord(MT_CMD_UNLOCK1, 0x00AA);
   MTWriteWord(MT_CMD_UNLOCK2, 0x0055);
   MTWriteWord(MT_CMD_UNLOCK1, cmd);
}

int main(void)
{
   printf("=== Memory Track ===\n");

   MTInit();
   MTReset();

   /* --- 1. address claims: the device must not swallow cart/CD-BIOS space --- */
   ok("does not claim $800000 before unlock (CD BIOS lives there)",
      !MTClaimsRead(0x800000));
   ok("does not claim $804000 (arbitrary cart ROM)",
      !MTClaimsRead(0x804000));
   ok("does not claim $880000 (above the command window)",
      !MTClaimsRead(0x880000));
   ok("claims the NVRAM window base $900000", MTClaimsRead(MT_DATA_BASE));
   ok("claims the NVRAM window top  $91FFFE", MTClaimsRead(MT_DATA_END - 2));
   ok("does not claim $920000 (past the NVRAM window)",
      !MTClaimsRead(MT_DATA_END));
   ok("accepts command writes at $815554", MTClaimsWrite(MT_CMD_UNLOCK1));
   ok("accepts command writes at $80AAA8", MTClaimsWrite(MT_CMD_UNLOCK2));

   /* --- 2. ID mode: ATMEL AT29C010 --- */
   unlock(0x0090);
   ok("ID mode claims $800000", MTClaimsRead(MT_ID_MANUF));
   ok("manufacturer ID = $1F (ATMEL)", MTReadWord(MT_ID_MANUF) == 0x001F);
   ok("device ID       = $D5 (AT29C010)", MTReadWord(MT_ID_DEVICE) == 0x00D5);

   /* $80AAA8 reads back $0055 after the unlock write (ROMULATOR probe). */
   ok("$80AAA8 reads back $0055", MTReadWord(MT_CMD_UNLOCK2) == 0x0055);

   unlock(0x00F0);   /* undo ID mode */
   ok("after $F0, $800000 is no longer claimed", !MTClaimsRead(MT_ID_MANUF));

   /* --- 3. NVRAM read/write --- */
   /* Writes must be ignored until the part is write-enabled. */
   memset(mtMem, 0xFF, MT_MEM_SIZE);
   MTWriteWord(MT_DATA_BASE, 0x1234);
   ok("write ignored without write-enable",
      MTReadWord(MT_DATA_BASE) == 0xFFFF);

   unlock(0x00A0);   /* write enable */
   MTWriteWord(MT_DATA_BASE, 0x1234);
   ok("write lands after write-enable",
      MTReadWord(MT_DATA_BASE) == 0x1234);
   ok("stored big-endian in mtMem",
      mtMem[0] == 0x12 && mtMem[1] == 0x34);

   MTWriteWord(MT_DATA_BASE + 2, 0xABCD);
   ok("adjacent word independent",
      MTReadWord(MT_DATA_BASE) == 0x1234 &&
      MTReadWord(MT_DATA_BASE + 2) == 0xABCD);

   ok("long read composes two words",
      MTReadLong(MT_DATA_BASE) == 0x1234ABCDu);

   /* Top of the window is reachable — a 128K device, not a 512K stride. */
   MTWriteWord(MT_DATA_END - 2, 0x5A5A);
   ok("top of window writable",
      MTReadWord(MT_DATA_END - 2) == 0x5A5A);
   ok("top of window maps to the last mtMem bytes",
      mtMem[MT_MEM_SIZE - 2] == 0x5A && mtMem[MT_MEM_SIZE - 1] == 0x5A);

   /* The whole 128K must be addressable: the old mapping folded four
    * addresses onto one byte, so a 128K span would have aliased. */
   unlock(0x00A0);
   MTWriteWord(MT_DATA_BASE + 0x10000, 0x0F0F);
   ok("mid-window write does not alias the base",
      MTReadWord(MT_DATA_BASE) == 0x1234 &&
      MTReadWord(MT_DATA_BASE + 0x10000) == 0x0F0F);

   /* --- 3b. byte accessors (the 68K reaches the device with move.b too;
    * Copilot review on #259 caught these bypassing the device entirely) --- */
   unlock(0x00A0);
   MTWriteByte(MT_DATA_BASE + 4, 0xDE);
   MTWriteByte(MT_DATA_BASE + 5, 0xAD);
   ok("byte writes land in the NVRAM window",
      MTReadWord(MT_DATA_BASE + 4) == 0xDEAD);
   ok("byte reads pick the right half of the word",
      MTReadByte(MT_DATA_BASE + 4) == 0xDE
      && MTReadByte(MT_DATA_BASE + 5) == 0xAD);
   ok("byte write to the last NVRAM byte is in range",
      (MTWriteByte(MT_DATA_END - 1, 0x77), mtMem[MT_MEM_SIZE - 1] == 0x77));

   /* A byte write without write-enable must be ignored, like the word path. */
   MTReset();
   MTWriteByte(MT_DATA_BASE + 4, 0x00);
   ok("byte write ignored without write-enable",
      MTReadByte(MT_DATA_BASE + 4) == 0xDE);

   /* Byte writes to the command window must still drive the state machine. */
   MTWriteByte(MT_CMD_UNLOCK1, 0xAA);
   MTWriteByte(MT_CMD_UNLOCK2, 0x55);
   MTWriteByte(MT_CMD_UNLOCK1, 0x90);
   ok("byte-written unlock reaches ID mode",
      MTReadWord(MT_ID_MANUF) == 0x001F);
   unlock(0x00F0);

   /* --- 4. reset keeps contents, clears command state --- */
   MTReset();
   ok("soft reset preserves NVRAM contents",
      MTReadWord(MT_DATA_BASE) == 0x1234);
   MTWriteWord(MT_DATA_BASE, 0x9999);
   ok("soft reset cleared write-enable",
      MTReadWord(MT_DATA_BASE) == 0x1234);

   printf("--- Memory Track: %d checks, %d failed ---\n", checks, failures);
   return failures ? 1 : 0;
}
