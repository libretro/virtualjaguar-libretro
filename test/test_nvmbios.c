/*
 * test_nvmbios.c — Memory Track NVM BIOS filesystem semantics.
 *
 * Drives the C implementation of the module's eleven calls directly
 * (nvmbios.c), with the 68K stubbed out.  Covers the specified behaviour
 * plus the quirks games depend on; see nvmbios.c's header comment and the
 * Atari NVM BIOS v1.01 source in https://github.com/cubanismo/skunk_mtrk.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 $(INCFLAGS) -o test/test_nvmbios \
 *      test/test_nvmbios.c src/core/nvmbios.c src/core/memtrack.c -lm
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "nvmbios.h"
#include "memtrack.h"

/* ---- 68K / RAM stubs so nvmbios.c links standalone ---- */
static uint8_t ram_backing[0x200000];
uint8_t *jaguarMainRAM = ram_backing;
bool jaguarMemTrackInserted = true;
unsigned int m68k_get_reg(void *c, int r) { (void)c; (void)r; return 0; }
void m68k_set_reg(int r, unsigned int v) { (void)r; (void)v; }
unsigned int m68k_read_memory_8(unsigned int a) { (void)a; return 0; }
unsigned int m68k_read_memory_16(unsigned int a) { (void)a; return 0; }
unsigned int m68k_read_memory_32(unsigned int a) { (void)a; return 0; }
void m68k_write_memory_8(unsigned int a, unsigned int v) { (void)a; (void)v; }
void m68k_write_memory_32(unsigned int a, unsigned int v) { (void)a; (void)v; }

static int failures, checks;

static void ok(const char *what, int cond)
{
   checks++;
   printf("  %s  %s\n", cond ? "PASS" : "FAIL", what);
   if (!cond)
      failures++;
}

int main(void)
{
   uint8_t buf[2048];
   uint8_t sbuf[NVM_SEARCHBUFSIZE];
   uint32_t total = 0, freeb = 0;
   int32_t h, h2, r;
   int i;

   printf("=== NVM BIOS (Memory Track filesystem) ===\n");

   MTInit();
   NVMBiosReset();

   /* --- pre-init gating --- */
   ok("calls before Initialize return ENOINIT",
      NVMOpen("SAVE") == NVM_ENOINIT
      && NVMCreate("SAVE", 100) == NVM_ENOINIT
      && NVMInquire(&total, &freeb) == NVM_ENOINIT);

   /* --- blank cart self-formats on Initialize --- */
   memset(mtMem, 0xFF, MT_MEM_SIZE);
   ok("Initialize on blank cart returns 0", NVMInitialize("VID GRID") == 0);
   ok("blank cart was formatted (FAT+dir zeroed)",
      mtMem[0] == 0 && mtMem[3] == 0 && mtMem[512] == 0);

   /* --- create / write / read round trip --- */
   h = NVMCreate("HISCORES", 1000);
   ok("Create returns a handle", h >= 0 && h < 3);
   for (i = 0; i < 1000; i++)
      buf[i] = (uint8_t)(i * 7);
   ok("Write returns count", NVMWriteFile((int16_t)h, buf, 1000) == 1000);
   ok("Seek to 0 returns 0", NVMSeek((int16_t)h, 0, 0) == 0);
   memset(buf, 0, sizeof(buf));
   ok("Read returns count", NVMReadFile((int16_t)h, buf, 1000) == 1000);
   r = 1;
   for (i = 0; i < 1000; i++)
      if (buf[i] != (uint8_t)(i * 7))
         r = 0;
   ok("data round-trips", r == 1);
   ok("Close returns 0", NVMClose((int16_t)h) == 0);

   /* --- reopen finds the file --- */
   h = NVMOpen("HISCORES");
   ok("Open finds the file", h >= 0);
   memset(buf, 0, sizeof(buf));
   ok("read after open starts at offset 0",
      NVMReadFile((int16_t)h, buf, 4) == 4 && buf[0] == 0 && buf[1] == 7);
   NVMClose((int16_t)h);

   /* --- checksum is maintained: re-Initialize must NOT reformat --- */
   ok("re-Initialize preserves files", NVMInitialize("VID GRID") == 0
      && NVMOpen("HISCORES") >= 0);
   NVMClose(0);

   /* --- missing file --- */
   ok("Open of missing file is EFILNF", NVMOpen("NOPE") == NVM_EFILNF);

   /* --- create replaces an existing file of the same name --- */
   h = NVMCreate("HISCORES", 512);
   ok("Create over existing succeeds", h >= 0);
   ok("size rounded to blocks: seek to 511 ok, 512 ERANGE",
      NVMSeek((int16_t)h, 511, 0) == 511
      && NVMSeek((int16_t)h, 512, 0) == NVM_ERANGE);
   NVMClose((int16_t)h);

   /* --- relative seek --- */
   h = NVMOpen("HISCORES");
   NVMSeek((int16_t)h, 100, 0);
   ok("relative seek adds", NVMSeek((int16_t)h, 50, 1) == 150);
   ok("bad seek flag is EINVFN", NVMSeek((int16_t)h, 0, 2) == NVM_EINVFN);
   NVMClose((int16_t)h);

   /* --- handle exhaustion: 3 handles max --- */
   {
      int32_t hs[3];
      for (i = 0; i < 3; i++)
         hs[i] = NVMOpen("HISCORES");
      ok("three handles open", hs[0] >= 0 && hs[1] >= 0 && hs[2] >= 0);
      ok("fourth is ENFILES", NVMOpen("HISCORES") == NVM_ENFILES);
      for (i = 0; i < 3; i++)
         NVMClose((int16_t)hs[i]);
   }

   /* --- bad handle --- */
   ok("read on closed handle is EIHNDL",
      NVMReadFile(0, buf, 4) == NVM_EIHNDL);

   /* --- second app, search semantics --- */
   NVMInitialize("OTHER APP");
   h2 = NVMCreate("SETTINGS", 100);
   ok("second app creates its own file", h2 >= 0);
   NVMClose((int16_t)h2);
   ok("open of other app's file by name fails (app-scoped)",
      NVMOpen("HISCORES") == NVM_EFILNF);

   ok("SearchFirst(all) finds a file", NVMSearchFirst(sbuf, 0) == 0);
   ok("SearchNext finds the second", NVMSearchNext(sbuf) == 0);
   ok("SearchNext exhausts with EFILNF", NVMSearchNext(sbuf) == NVM_EFILNF);
   ok("SearchFirst(current app) finds exactly one",
      NVMSearchFirst(sbuf, 1) == 0 && NVMSearchNext(sbuf) == NVM_EFILNF);
   ok("search buffer carries app name",
      memcmp(sbuf + 4, "OTHER APP      ", 15) == 0);
   ok("search buffer carries file name",
      memcmp(sbuf + 4 + 16, "SETTINGS ", 9) == 0);
   ok("bad search flags are EINVFN", NVMSearchFirst(sbuf, 5) == NVM_EINVFN);

   /* --- inquire --- */
   NVMInquire(&total, &freeb);
   ok("Inquire totals: 248 blocks", total == 248u * 512u);
   /* HISCORES was re-created at 512 bytes (1 block) + SETTINGS (1 block). */
   ok("Inquire free reflects use: 2 blocks used",
      freeb == (248u - 2u) * 512u);

   /* --- delete (cross-app, and the always-success quirk) --- */
   ok("Delete other app's file by explicit appname",
      NVMDelete("VID GRID", "HISCORES") == 0);
   ok("deleted file is gone", NVMOpen("HISCORES") == NVM_EFILNF
      /* still OTHER APP's context, so also check via search-all */
      && (NVMSearchFirst(sbuf, 0) == 0
          && memcmp(sbuf + 4 + 16, "SETTINGS ", 9) == 0
          && NVMSearchNext(sbuf) == NVM_EFILNF));
   ok("Delete of missing file still reports success (module quirk)",
      NVMDelete(NULL, "NOPE") == 0);
   NVMInquire(&total, &freeb);
   ok("freed blocks return to the pool", freeb == (248u - 1u) * 512u);

   /* --- ENOSPC --- */
   ok("Create larger than the cart is ENOSPC",
      NVMCreate("HUGE", 248 * 512) == NVM_ENOSPC);

   /* --- zero-length file --- */
   h = NVMCreate("EMPTY", 0);
   ok("zero-length create succeeds", h >= 0);
   NVMClose((int16_t)h);

   /* --- persistence: state save/load round-trip of runtime state --- */
   {
      uint8_t st[128];
      size_t n = NVMBiosStateSave(st);
      NVMBiosReset();
      ok("after reset, calls need Initialize again",
         NVMOpen("SETTINGS") == NVM_ENOINIT);
      NVMBiosStateLoad(st);
      ok("state restore brings back the session",
         NVMOpen("SETTINGS") >= 0);
      ok("state block size is stable", NVMBiosStateSave(st) == n);
   }

   printf("--- NVM BIOS: %d checks, %d failed ---\n", checks, failures);
   return failures ? 1 : 0;
}
