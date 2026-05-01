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
#include "jaguar.h"
#include "vjag_memory.h"

static bool InferRawBinaryLoadAddress(uint8_t *buffer, uint32_t size, uint32_t *loadAddress)
{
   static const uint32_t candidates[] = { 0x00802000, 0x00020000, 0x00004000 };
   unsigned bestCandidate = 0;
   unsigned bestScore = 0;
   unsigned i;
   uint32_t offset;

   if (size < 16 || !loadAddress)
      return false;

   /* Known raw homebrew startup writes big-endian mode before touching TOM. */
   if (GET16(buffer, 0) != 0x23FC || GET32(buffer, 2) != 0x00070007
         || GET32(buffer, 6) != 0x00F0210C)
      return false;

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

   if (bestScore < 2)
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

   // If the file size + 8192 bytes is divisible by 1M, we probably have an
   // Alpine format ROM.
   if (((size + 8192) % 1048576) == 0)
      return JST_ALPINE;

   if (InferRawBinaryLoadAddress(buffer, size, &rawLoadAddress))
      return JST_RAW_BINARY;

   // Headerless crap
   return JST_NONE;
}

bool JaguarLoadFile(uint8_t *buffer, size_t bufsize)
{
   int fileType;
   jaguarROMSize = bufsize;
   jaguarLoadedRAMStart = 0;
   jaguarLoadedRAMEnd = 0;

   if (jaguarROMSize == 0)
      return false;

   jaguarMainROMCRC32 = crc32_calcCheckSum(buffer, jaguarROMSize);
   EepromInit();
   jaguarRunAddress = 0x802000;					// For non-BIOS runs, this is true
   fileType           = ParseFileType(buffer, jaguarROMSize);
   jaguarCartInserted = false;

   if (fileType == JST_ROM)
   {
      jaguarCartInserted = true;
      memcpy(jagMemSpace + 0x800000, buffer, jaguarROMSize);
      // Checking something...
      jaguarRunAddress = GET32(jagMemSpace, 0x800404);
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
   return false;
}
