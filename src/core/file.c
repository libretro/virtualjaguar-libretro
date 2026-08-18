//
// FILE.CPP
//
// File support
// by James Hammons
// (C) 2010 Underground Software
//
// JLH = James Hammons <jlhamm@acm.org>
//
// Who  When        What
// ---  ----------  -------------------------------------------------------------
// JLH  01/16/2010  Created this log ;-)
// JLH  02/28/2010  Added functions to look inside .ZIP files and handle contents
// JLH  06/01/2012  Added function to check ZIP file CRCs against file DB
//

#include "file.h"

#include <stdarg.h>
#include <string.h>
#include "crc32.h"
#include "filedb.h"
#include "eeprom.h"
#include "jaggd.h"
#include "jaguar.h"
#include "log.h"
#include "vjag_memory.h"

/* Some circulating cartridge dumps carry a copier/dumper header glued to the
 * front of an otherwise byte-perfect image.  "Brutal Sports Football (1994)
 * (Telegames).jag" is the known case: 2 MiB + 512 bytes, where the payload's
 * CRC32 ($BCB1A4BF) matches the FF_VERIFIED row in filedb.c exactly, while the
 * whole-file CRC ($0FDCEB66) is catalogued as FF_BAD_DUMP.  Nothing about the
 * image data is wrong, so skip the header instead of refusing the file.
 *
 * Detection is deliberately narrow.  The payload must carry the cartridge
 * universal-header marker ($04040404 at payload $400), AND the file must
 * shape-wise look like copier output:
 *
 *   - payload is a whole number of megabytes (the circulating Brutal Sports
 *     class), or
 *   - payload is smaller than 1 MiB (sub-1 MiB homebrew with the same 512-byte
 *     header; without this arm those files fell through to the headerless
 *     JST_ROM fallback and loaded skewed).
 *
 * An exact-megabyte file is never stripped (copier output is always
 * N MiB + 512), so an incidental $04040404 at file $600 on a full-size
 * native ROM cannot take the header off.  A native small cart already
 * has the marker at file $400 and is left alone.
 */
#define CART_HEADER_SKIP_SIZE   512
#define CART_UNIVERSAL_MARKER_OFFSET   0x400
#define CART_UNIVERSAL_MARKER          0x04040404

uint32_t DetectPrependedHeaderSize(uint8_t *buffer, uint32_t size)
{
   uint32_t payloadSize;

   /* Need room for the header plus the payload's $400 marker longword. */
   if (size < CART_HEADER_SKIP_SIZE + CART_UNIVERSAL_MARKER_OFFSET + 4)
      return 0;

   /* Exact-megabyte files are native dumps.  A copier-headered image is
    * always N MiB + 512, so this excludes the incidental-$600 case on
    * full-size commercial ROMs before any payload math runs. */
   if ((size % 1048576) == 0)
      return 0;

   payloadSize = size - CART_HEADER_SKIP_SIZE;

   if (GET32(buffer, CART_HEADER_SKIP_SIZE + CART_UNIVERSAL_MARKER_OFFSET)
         != CART_UNIVERSAL_MARKER)
      return 0;

   /* Classic copier dump (Brutal Sports: 2 MiB + 512). */
   if ((payloadSize % 1048576) == 0)
      return CART_HEADER_SKIP_SIZE;

   /* Sub-1 MiB homebrew with a copier header.  A native small cart has
    * the marker at file $400 already -- do not strip those. */
   if (payloadSize < 1048576
         && GET32(buffer, CART_UNIVERSAL_MARKER_OFFSET) != CART_UNIVERSAL_MARKER)
      return CART_HEADER_SKIP_SIZE;

   return 0;
}

/* Say what we were handed when ParseFileType came up empty. Several images in
 * the CRC database are known to be unloadable -- bad dumps, and dumps with a
 * header glued to the front -- so name the one in front of us instead of
 * leaving the caller to print a generic "invalid content" line. */
static void ReportUnrecognizedContent(uint32_t crc, uint32_t size)
{
   const struct RomIdentifier *entry = FindRomIdentifier(crc);
   const struct RomIdentifier *verified;

   if (!entry)
   {
      LOG_ERR("[CART] unrecognized content format: %u bytes, CRC32 $%08X (no matching row in the ROM database)\n",
            (unsigned)size, (unsigned)crc);
      return;
   }

   if (entry->flags & FF_BAD_DUMP)
      LOG_ERR("[CART] known bad dump: \"%s\" -- %u bytes, CRC32 $%08X\n",
            entry->name, (unsigned)size, (unsigned)crc);
   else
      LOG_ERR("[CART] \"%s\" (%u bytes, CRC32 $%08X) is a known image, but this container is not loadable\n",
            entry->name, (unsigned)size, (unsigned)crc);

   verified = FindVerifiedRomVariant(entry);

   if (verified)
      LOG_ERR("[CART] a verified dump of the same title exists (CRC32 $%08X) -- re-dump, or strip any header prepended to this image\n",
            (unsigned)verified->crc32);
}

/* A dump can be bad and still load: every FF_BAD_DUMP row in the database is
 * cart-sized, so ParseFileType takes it as a plain JST_ROM and the game runs
 * subtly wrong with nothing said. Say it, so a glitch report can be attributed to
 * the image rather than to the emulator. */
static void ReportKnownBadDumpLoaded(uint32_t crc)
{
   const struct RomIdentifier *entry = FindRomIdentifier(crc);
   const struct RomIdentifier *verified;

   if (!entry || !(entry->flags & FF_BAD_DUMP))
      return;

   LOG_WRN("[CART] this is a known bad dump: \"%s\" (CRC32 $%08X) -- it will run, but expect glitches\n",
         entry->name, (unsigned)crc);

   verified = FindVerifiedRomVariant(entry);

   if (verified)
      LOG_WRN("[CART] a verified dump of the same title exists (CRC32 $%08X)\n",
            (unsigned)verified->crc32);
}

static bool InferRawBinaryLoadAddress(uint8_t *buffer, uint32_t size, uint32_t *loadAddress)
{
   static const uint32_t candidates[] = { 0x00802000, 0x00020000, 0x00004000 };
   unsigned bestCandidate = 0;
   unsigned bestScore = 0;
   unsigned minScore;
   bool knownStartup;
   unsigned i;
   uint32_t offset;

   if (size < 16 || !loadAddress)
      return false;

   /* One known homebrew startup idiom writes big-endian mode before
    * touching TOM.  It used to be a hard REQUIREMENT, which rejected any
    * raw binary that opens differently -- the majority of the PD/BJL
    * corpus.  Measured over the local homebrew set: 56 images that this
    * gate refused score 17-56 on the absolute-reference test below (i.e.
    * overwhelmingly consistent with one candidate base), while genuine
    * non-binaries -- .zip files, headered dev images like the AvP alpha --
    * score 0-1.  So the idiom is now a confidence hint, not an entry
    * requirement: recognised startup keeps the permissive threshold, and
    * anything else has to clear a much higher bar of self-consistent
    * absolute references before we will claim to know its load address. */
   knownStartup = (GET16(buffer, 0) == 0x23FC
         && GET32(buffer, 2) == 0x00070007
         && GET32(buffer, 6) == 0x00F0210C);
   minScore = knownStartup ? 2 : 8;

   for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
   {
      unsigned score = 0;
      uint32_t base = candidates[i];

      for (offset = 0; offset + 6 <= size && offset < 2048; offset += 2)
      {
         uint16_t op = GET16(buffer, offset);
         uint32_t target;

         if (op != 0x4EB9 && op != 0x4EF9 && op != 0x41F9
               && op != 0x2039 && op != 0x2079 && op != 0x2279)
            continue;

         target = GET32(buffer, offset + 2);
         if (target >= base && target < base + size)
            score++;
      }

      if (score > bestScore)
      {
         bestScore = score;
         bestCandidate = i;
      }
   }

   if (bestScore < minScore)
      return false;

   *loadAddress = candidates[bestCandidate];
   return true;
}

/* Parse the file type based upon file size and/or headers. */
static uint32_t ParseFileType(uint8_t * buffer, uint32_t size)
{
   uint32_t rawLoadAddress;

   // Check headers first...

   // ABS/COFF type 1
   if (buffer[0] == 0x60 && buffer[1] == 0x1B)
      return JST_ABS_TYPE1;

   // ABS/COFF type 2
   if (buffer[0] == 0x01 && buffer[1] == 0x50)
      return JST_ABS_TYPE2;

   // Jag Server and older RAM-loaded formats
   if (buffer[0] == 0x60 && buffer[1] == 0x1A)
   {
      if (buffer[0x1C] == 'J' && buffer[0x1D] == 'A' && buffer[0x1E] == 'G')
         return JST_JAGSERVER;
      return JST_WTFOMGBBQ;
   }

   // And if that fails, try file sizes...

   // If the file size is divisible by 1M, we probably have a regular ROM.
   // We can also check our CRC32 against the internal ROM database to be sure.
   // (We also check for the Memory Track cartridge size here as well...)
   if ((size % 1048576) == 0 || size == 131072)
      return JST_ROM;

   /* Homebrew / bootintro carts are rarely a whole megabyte and are often
    * well under the Memory Track size of 128K.  The universal header at
    * $400 is the same marker DetectPrependedHeaderSize uses, and is
    * enough on its own to call the file a cartridge (issue #462). */
   if (size >= (CART_UNIVERSAL_MARKER_OFFSET + 8)
         && GET32(buffer, CART_UNIVERSAL_MARKER_OFFSET) == CART_UNIVERSAL_MARKER)
      return JST_ROM;

   // If the file size + 8192 bytes is divisible by 1M, we probably have an
   // Alpine format ROM.
   if (((size + 8192) % 1048576) == 0)
      return JST_ALPINE;

   if (InferRawBinaryLoadAddress(buffer, size, &rawLoadAddress))
      return JST_RAW_BINARY;

   /* Last resort for headerless bootintros smaller than 1 MiB: map them
    * as a cartridge so real-BIOS mode can boot them from $800000.  Must
    * be at least large enough to hold the $400 universal-header slot
    * (otherwise DetectPrependedHeaderSize's 512-byte floor is a cart).
    * RAM-load BJL binaries with recognizable absolute refs already
    * returned JST_RAW_BINARY above.  Files >= 1 MiB that are not an
    * exact megabyte multiple stay unrecognized (the bad-dump /
    * leftover-header class ReportUnrecognizedContent names). */
   if (size >= (CART_UNIVERSAL_MARKER_OFFSET + 8) && size < 1048576)
      return JST_ROM;

   // Headerless crap
   return JST_NONE;
}

static bool JaguarLoadFileInternal(uint8_t *buffer, size_t bufsize)
{
   int fileType;
   uint32_t headerSize;

   jaguarLoadedRAMStart = 0;
   jaguarLoadedRAMEnd = 0;

   /* Taken off before anything else looks at the image: the CRC below, the
    * EEPROM init, the type detection and every load path must all see the
    * payload rather than the header. */
   headerSize = DetectPrependedHeaderSize(buffer, (uint32_t)bufsize);

   if (headerSize != 0)
   {
      LOG_INF("[CART] skipping a %u-byte header prepended to a %u-byte cartridge image\n",
            (unsigned)headerSize, (unsigned)(bufsize - headerSize));
      buffer  += headerSize;
      bufsize -= headerSize;
   }

   jaguarROMSize = bufsize;

   if (jaguarROMSize == 0)
      return false;

   jaguarMainROMCRC32 = crc32_calcCheckSum(buffer, jaguarROMSize);
   EepromInit();
   jaguarRunAddress = 0x802000;					// For non-BIOS runs, this is true
   fileType           = ParseFileType(buffer, jaguarROMSize);
   jaguarCartInserted = false;

   if (fileType == JST_ROM)
   {
      /* The flat cart window holds 6 MB ($800000-$DFFFFF).  Larger
       * images are Jaguar GameDrive content: the first 6 MB fill the
       * flat window as the GD's identity page mapping would, and the
       * full image (up to 16 MB) goes to the banked path. */
      uint32_t flatSize = jaguarROMSize;

      if (jaguarROMSize > JGD_ROM_SIZE)
      {
         LOG_ERR("[CART] image is %u bytes; the largest supported cartridge is the 16 MB Jaguar GameDrive\n",
               (unsigned)jaguarROMSize);
         return false;
      }
      /* Cap the flat copy at the dispatchable cart window ($800000-
       * $DFFEFF): the final $100 bytes ($DFFF00-$DFFFFF) are the CDROM
       * overlay and must not receive ROM bytes.  Same constant the
       * auto-enable threshold uses, so loader and banking agree on
       * where the flat window ends. */
      if (flatSize > JGD_AUTO_THRESHOLD)
         flatSize = JGD_AUTO_THRESHOLD;

      jaguarCartInserted = true;
      memcpy(jagMemSpace + 0x800000, buffer, flatSize);
      JGDLoadROM(buffer, jaguarROMSize);

      /* The common cart layout places a 68K vector table right after a
       * $400-byte header: SSP at cart+$400, PC (the real entry point) at
       * cart+$404.  Most JST_ROM images follow this, but some (raw demo
       * builds, homebrew) do not and start executing at cart+0 instead --
       * for those, cart+$404 lands mid-instruction-stream and decodes to
       * whatever bytes happen to be there, not a vector.
       *
       * "Rayman Demo (1995) (UBI Soft).jag" is the measured case: bytes at
       * cart+$400 are real 68K code (a MOVE.L immediate), and cart+$404
       * happens to read as $3BE800F0, which masks to $E800F0 -- just past
       * the mapped boot-ROM window ($E00000-$E1FFFF).  In HLE (no real
       * BIOS to validate/redirect the cart), that garbage becomes the
       * initial PC and the 68K never reaches the game's own code.  Real
       * BIOS mode is unaffected: it runs its own boot code first and
       * ignores jaguarRunAddress entirely (JaguarReset() only consults it
       * for the non-BIOS path).
       *
       * A masked value outside every executable band (main RAM $000000-
       * $1FFFFF, cart ROM $800000-$DFFEFF, boot ROM $E00000-$E1FFFF) can
       * never be a genuine vector, so treat it as evidence the
       * header/vector-table convention isn't followed and fall back to the
       * cart base -- the other common convention, and where this demo's
       * real startup code (VMODE/VI register writes) actually lives.
       * Note: $DFFF00-$DFFFFF is the CDROM overlay, not cart ROM; the
       * cart window ends at $DFFEFF.
       *
       * An odd candidate is rejected for the same reason: the 68000 cannot
       * fetch an instruction from an odd PC, and JaguarExecute() bails out
       * immediately whenever (m68kPC & 1), so an in-range but misaligned
       * vector (say $800001 out of a raw byte stream) would leave the CPU
       * parked instead of running the game.  Those take the fallback too. */
      {
         uint32_t candidate = GET32(jagMemSpace, 0x800404);
         uint32_t masked = candidate & 0x00FFFFFF;
         bool validVector = ((masked & 1) == 0)
               && ((masked < 0x200000)
                  || (masked >= 0x800000 && masked <= 0xDFFEFF)
                  || (masked >= 0xE00000 && masked <= 0xE1FFFF));

         if (validVector)
            jaguarRunAddress = candidate;
         else
         {
            LOG_INF("[CART] cart+$404 ($%08X) is not a valid execute address; "
                  "falling back to cart base $800000 as the entry point\n",
                  candidate);
            jaguarRunAddress = 0x800000;
         }
      }
      return true;
   }
   else if (fileType == JST_ALPINE)
   {
      // File extension ".ROM": Alpine image that loads/runs at $802000
      memset(jagMemSpace + 0x800000, 0xFF, 0x2000);
      memcpy(jagMemSpace + 0x802000, buffer, jaguarROMSize);

      /* Alpine images do not provide a BIOS vector table. Point the illegal
       * instruction vector at a local infinite loop to keep accidental traps
       * inside mapped RAM. */
      SET32(jaguarMainRAM, 0x10, 0x00001000);
      SET16(jaguarMainRAM, 0x1000, 0x60FE);		// Here: bra Here
      return true;
   }
   else if (fileType == JST_ABS_TYPE1)
   {
      // For ABS type 1, run address == load address
      uint32_t loadAddress = GET32(buffer, 0x16),
               codeSize = GET32(buffer, 0x02) + GET32(buffer, 0x06);
      memcpy(jagMemSpace + loadAddress, buffer + 0x24, codeSize);
      jaguarRunAddress = loadAddress;
      jaguarLoadedRAMStart = loadAddress;
      jaguarLoadedRAMEnd = loadAddress + codeSize;
      return true;
   }
   else if (fileType == JST_ABS_TYPE2)
   {
      uint32_t loadAddress = GET32(buffer, 0x28), runAddress = GET32(buffer, 0x24),
               codeSize = GET32(buffer, 0x18) + GET32(buffer, 0x1C);
      memcpy(jagMemSpace + loadAddress, buffer + 0xA8, codeSize);
      jaguarRunAddress = runAddress;
      jaguarLoadedRAMStart = loadAddress;
      jaguarLoadedRAMEnd = loadAddress + codeSize;
      return true;
   }
   else if (fileType == JST_JAGSERVER)
   {
      /* Detection already verified the JAG header. This load path still assumes
       * JAGSERVER type 3 and long command sizes; type 2/JAGR needs hardware docs
       * or a repro before changing behavior. */
      uint32_t loadAddress = GET32(buffer, 0x22), runAddress = GET32(buffer, 0x2A);
      uint32_t codeSize = jaguarROMSize - 0x2E;
      memcpy(jagMemSpace + loadAddress, buffer + 0x2E, codeSize);
      jaguarRunAddress = runAddress;
      jaguarLoadedRAMStart = loadAddress;
      jaguarLoadedRAMEnd = loadAddress + codeSize;

      /* Match the Alpine trap guard used above for RAM-loaded server images. */
      SET32(jaguarMainRAM, 0x10, 0x00001000);		// Set Exception #4 (Illegal Instruction)
      SET16(jaguarMainRAM, 0x1000, 0x60FE);		// Here: bra Here

      return true;
   }
   else if (fileType == JST_WTFOMGBBQ)
   {
      uint32_t loadAddress = (buffer[0x1F] << 24) | (buffer[0x1E] << 16) | (buffer[0x1D] << 8) | buffer[0x1C];
      uint32_t codeSize = jaguarROMSize - 0x20;
      memcpy(jagMemSpace + loadAddress, buffer + 0x20, codeSize);
      jaguarRunAddress = loadAddress;
      jaguarLoadedRAMStart = loadAddress;
      jaguarLoadedRAMEnd = loadAddress + codeSize;
      return true;
   }
   else if (fileType == JST_RAW_BINARY)
   {
      uint32_t loadAddress;

      if (!InferRawBinaryLoadAddress(buffer, jaguarROMSize, &loadAddress))
         return false;

      memcpy(jagMemSpace + loadAddress, buffer, jaguarROMSize);
      jaguarRunAddress = loadAddress;

      if (loadAddress < 0x800000)
      {
         jaguarLoadedRAMStart = loadAddress;
         jaguarLoadedRAMEnd = loadAddress + jaguarROMSize;
      }

      return true;
   }

   // We can assume we have JST_NONE at this point. :-P
   ReportUnrecognizedContent(jaguarMainROMCRC32, jaguarROMSize);
   return false;
}

/* Cart entry at $802000 that is real 68K (HLE can jump there). */
static int cart_entry_looks_like_68k(uint16_t op)
{
   if (op == 0x46FC)
      return 1;
   if (op == 0x4E71)
      return 1;
   if (op == 0x23FC || op == 0x33FC)
      return 1;
   if (op == 0x2E7C)
      return 1;
   if ((op & 0xFF00) == 0x7000)
      return 1;
   if ((op & 0xF000) == 0x6000)
      return 1;
   if ((op & 0xF1FF) == 0x41F8 || (op & 0xF1FF) == 0x41F9)
      return 1;
   if (op == 0x4EF8 || op == 0x4EF9 || op == 0x4EB8 || op == 0x4EB9)
      return 1;
   return 0;
}

bool JaguarCartNeedsBIOS(const uint8_t *buffer, uint32_t size)
{
   uint32_t headerSize;
   uint32_t payload;
   const uint8_t *body;
   unsigned i;
   uint16_t op;

   if (!buffer || size < 2)
      return false;

   /* RAM-loaded formats have their own entry; do not force the boot ROM. */
   if (buffer[0] == 0x60 && buffer[1] == 0x1B)
      return false;
   if (buffer[0] == 0x01 && buffer[1] == 0x50)
      return false;
   if (buffer[0] == 0x60 && buffer[1] == 0x1A)
      return false;

   headerSize = DetectPrependedHeaderSize((uint8_t *)buffer, size);
   if (headerSize >= size)
      return false;
   body = buffer + headerSize;
   payload = size - headerSize;

   /* Too small to hold 68K at $802000: BootIntro / header-only. */
   if (payload < 0x2002u)
      return true;

   /* Blank cart entry at $802000 — typical BootIntro pad. Require a full
    * 256-byte window (no over-read) and skip megabyte-or-larger dumps:
    * Rayman Demo is a 2 MiB commercial cart with FF there but real 68K
    * elsewhere; HLE must keep jumping to $802000 for those. */
   if (payload >= 0x2100u && payload < 0x100000u)
   {
      for (i = 0; i < 256; i++)
      {
         if (body[0x2000 + i] != 0xFFu)
            break;
      }
      if (i == 256)
         return true;
   }

   op = (uint16_t)(((uint16_t)body[0x2000] << 8) | body[0x2001]);
   /* All-zero dummy carts used by HLE tests. */
   if (op == 0x0000)
      return false;
   if (cart_entry_looks_like_68k(op))
      return false;
   /* Tursi jagcrypt / GPU-only intros typically start 0xFC or 0xFE.
    * Do not treat every non-68K entry as BIOS: synthetic test ROMs
    * start with ADDQ and similar. */
   if (body[0] == 0xFC || body[0] == 0xFE)
      return true;
   return false;
}

bool JaguarLoadFile(uint8_t *buffer, size_t bufsize)
{
   bool loaded = JaguarLoadFileInternal(buffer, bufsize);

   /* Reported from here rather than from each of the load paths above, all of
    * which return directly. */
   if (loaded)
      ReportKnownBadDumpLoaded(jaguarMainROMCRC32);

   return loaded;
}
