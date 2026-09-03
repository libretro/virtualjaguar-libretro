/*
 * TITLEHOOK.C -- see titlehook.h for the design contract.
 *
 * Fences, in the order TitleHookApplyROM() applies them.  Each one
 * refuses, logs and returns 0; nothing is ever half-applied.
 *
 *   Gate off        virtualjaguar_enhancement_hooks != "enabled" (default)
 *   No match        TitleDBHooks() == NULL, or the row carries no hooks
 *   Not a cartridge !jaguarCartInserted -- .abs/.cof/JagServer RAM loads
 *                   have no ROM window, and neither does CD content
 *   CRC disagree    TitleDBContentCRC() != jaguarMainROMCRC32; a free
 *                   assert that the table key and the loaded image are
 *                   the same object
 *   GameDrive       jgdActive, or romSize > JGD_AUTO_THRESHOLD.  NOT
 *                   obvious and it matters: JaguarLoadFileInternal copies
 *                   the first <=6 MB flat into the cart window AND hands
 *                   the full image to JGDLoadROM, which mallocs a
 *                   SEPARATE 16 MB copy (jgdROM).  Bank switching then
 *                   serves reads out of jgdROM, so a patch to the flat
 *                   window is silently defeated the moment the game
 *                   switches banks.  v1 refuses GD content and says so.
 *                   (jaggd.c's JGDWriteROM8() is the extension point if
 *                   GD hooks are ever wanted.)
 *   Bad record      len==0, len>64, NULL expect/patch/name, unknown kind
 *   Out of range    offset + len > romSize
 *   Entry vector    [offset, offset+len) overlaps cart $400..$407.  This
 *                   guards the one failure expect[] CANNOT catch: a
 *                   silent no-op.  JaguarLoadFileInternal reads the entry
 *                   point out of ROM itself (GET32 at $800404) into
 *                   jaguarRunAddress, and JaguarReset() latches that into
 *                   RAM -- both inside the boot strategy, i.e. BEFORE our
 *                   call site.  retro_reset() then reuses the cached
 *                   variable rather than re-reading ROM, so a hook here
 *                   would pass expect[], write successfully, and do
 *                   nothing, forever.  Supporting it would mean
 *                   re-deriving jaguarRunAddress after the apply, which
 *                   is a change to boot ordering, not to this feature.
 *   Precondition    memcmp(rom + offset, expect, len) != 0
 *
 * jaguarMainROMCRC32 is deliberately left at its pre-patch value.  The
 * applier writes ROM bytes and does not recompute it: titledb/filedb row
 * identity, EepromInit's per-title naming, the Memory Track $FDF37F47
 * checks and any frontend content hashing must all keep seeing the dump
 * the user supplied.  A hook is an overlay on the loaded image, not a
 * different game.
 */

#include "titlehook.h"

#include <string.h>
#include <boolean.h>        /* project shim; bool / true / false */
#include <retro_inline.h>   /* log.h uses INLINE */
#include "log.h"
#include "titledb.h"
#include "jaggd.h"          /* JGD_AUTO_THRESHOLD, jgdActive */
#include "jaguar.h"         /* jaguarCartInserted, jaguarROMSize, ...CRC32 */
#include "vjag_memory.h"    /* jaguarMainROM */
#include "hookfile.h"       /* user-supplied hooks (#637) */

/* The 68K vector table a JST_ROM cartridge places after its $400-byte
 * header: SSP at cart+$400, PC at cart+$404.  Consumed by the loader
 * before hooks run -- see the entry-vector fence above. */
#define TITLEHOOK_VECTOR_LO 0x400u
#define TITLEHOOK_VECTOR_HI 0x408u   /* exclusive */

/* Gate, latched once at content load. */
static int hooks_enabled = 0;

void TitleHookSetEnabled(int enabled)
{
   hooks_enabled = enabled ? 1 : 0;
}

int TitleHookGetEnabled(void)
{
   return hooks_enabled;
}

/*
 * Validate one hook against the image.  Returns 1 when it may be
 * written, 0 after logging exactly why not.
 */
static int TitleHookValidate(const uint8_t *rom, uint32_t romSize,
                             const TitleDBHook *h, const char *title)
{
   const char *hookName = (h->name != NULL) ? h->name : "(unnamed)";

   if (h->kind != TITLEDB_HOOK_ROM_PATCH)
   {
      LOG_WRN("[hooks] %s: REFUSED %s -- unknown hook kind %u (table bug); "
              "no bytes written\n",
              title, hookName, (unsigned)h->kind);
      return 0;
   }

   if (h->len == 0 || h->len > TITLEDB_HOOK_MAX_BYTES)
   {
      LOG_WRN("[hooks] %s: REFUSED %s -- len %u out of range 1..%u "
              "(table bug); no bytes written\n",
              title, hookName, (unsigned)h->len,
              (unsigned)TITLEDB_HOOK_MAX_BYTES);
      return 0;
   }

   if (h->expect == NULL || h->patch == NULL || h->name == NULL)
   {
      LOG_WRN("[hooks] %s: REFUSED a hook at +$%06X -- expect[]/patch[]/name "
              "must all be non-NULL (table bug); no bytes written\n",
              title, (unsigned)h->offset);
      return 0;
   }

   /* Written so offset + len cannot wrap a uint32_t. */
   if (h->offset > romSize || (romSize - h->offset) < (uint32_t)h->len)
   {
      LOG_WRN("[hooks] %s: REFUSED %s -- +$%06X..+$%06X is outside the "
              "%u-byte image; no bytes written\n",
              title, hookName, (unsigned)h->offset,
              (unsigned)(h->offset + h->len - 1), (unsigned)romSize);
      return 0;
   }

   if (h->offset < TITLEHOOK_VECTOR_HI
       && (h->offset + (uint32_t)h->len) > TITLEHOOK_VECTOR_LO)
   {
      LOG_WRN("[hooks] %s: REFUSED %s -- +$%06X..+$%06X overlaps the cart "
              "entry vector ($400..$407), which is consumed before hooks "
              "run, so the patch would be a permanent silent no-op; no "
              "bytes written\n",
              title, hookName, (unsigned)h->offset,
              (unsigned)(h->offset + h->len - 1));
      return 0;
   }

   if (memcmp(rom + h->offset, h->expect, (size_t)h->len) != 0)
   {
      LOG_WRN("[hooks] %s: REFUSED %s @ +$%06X -- expected bytes not found "
              "(this is not the dump the hook was written for); no bytes "
              "written\n",
              title, hookName, (unsigned)h->offset);
      return 0;
   }

   return 1;
}

int TitleHookApplyToBuffer(uint8_t *rom, uint32_t romSize,
                           const TitleDBHook *hooks, int count,
                           const char *title, int gdActive)
{
   int i;
   int n = 0;

   if (title == NULL)
      title = "(unknown title)";

   if (rom == NULL || romSize == 0 || hooks == NULL || count <= 0)
      return 0;

   /* Count the hooks first so an all-empty array costs nothing and logs
    * nothing -- that is the state of every row shipping today. */
   for (i = 0; i < count && hooks[i].kind != TITLEDB_HOOK_NONE; i++)
      n++;
   if (n == 0)
      return 0;

   if (gdActive || romSize > (uint32_t)JGD_AUTO_THRESHOLD)
   {
      LOG_WRN("[hooks] %s: REFUSED all %d hook(s) -- Jaguar GameDrive "
              "content is served from a separate banked image, so a patch "
              "to the flat cart window would be defeated by bank "
              "switching; no bytes written\n", title, n);
      return 0;
   }

   /* Pass 1: validate every hook.  All-or-nothing -- a row is a single
    * intervention, and half-applying it produces a state no author ever
    * tested. */
   for (i = 0; i < n; i++)
   {
      if (!TitleHookValidate(rom, romSize, &hooks[i], title))
      {
         if (n > 1)
            LOG_WRN("[hooks] %s: all %d hook(s) in this entry skipped "
                    "(all-or-nothing)\n", title, n);
         return 0;
      }
   }

   /* Pass 2: write. */
   for (i = 0; i < n; i++)
   {
      memcpy(rom + hooks[i].offset, hooks[i].patch, (size_t)hooks[i].len);
      LOG_INF("[hooks] %s: applied %s @ $%06X +%u\n",
              title, hooks[i].name,
              (unsigned)(0x800000u + hooks[i].offset),
              (unsigned)hooks[i].len);
   }

   LOG_INF("[hooks] %s: applied %d/%d\n", title, n, n);
   return n;
}

/* <system_dir>/vj_hooks.txt, or empty when the frontend gave us no system
 * directory.  Static storage: TitleDBHook holds pointers into userSet, so
 * it must outlive the apply. */
static char        user_hook_path[1024];
static HookFileSet userSet;

void TitleHookSetUserFilePath(const char *path)
{
   if (!path || !*path)
   {
      user_hook_path[0] = '\0';
      return;
   }
   strncpy(user_hook_path, path, sizeof(user_hook_path) - 1);
   user_hook_path[sizeof(user_hook_path) - 1] = '\0';
}

int TitleHookApplyROM(void)
{
   const TitleDBHook *hooks;
   const char *title;
   int count = 0;
   int userCount = 0;

   if (!hooks_enabled)
      return 0;

   hooks = TitleDBHooks(&count);

   /* User file wins over a built-in row for the same CRC, matching the
    * per-title DB's one hard rule ("user-set values always win"), and the
    * override is logged naming both so a bug report starts from the right
    * hypothesis.  Parsed AFTER the gate: the gate is read raw via
    * environ_cb, so a file can never switch on its own gate. */
   userCount = HookFileLoad(user_hook_path, TitleDBContentCRC(), &userSet);
   if (userCount > 0)
   {
      if (hooks != NULL && count > 0 && hooks[0].kind != TITLEDB_HOOK_NONE)
         LOG_WRN("[hooks] %s: user hook file overrides the %d built-in "
                 "hook(s) for this title; the built-in row is NOT applied\n",
                 TitleDBTitleName() ? TitleDBTitleName() : "(unlisted title)",
                 count);
      hooks = userSet.hooks;
      count = userCount;
   }

   if (hooks == NULL || count <= 0 || hooks[0].kind == TITLEDB_HOOK_NONE)
      return 0;

   title = TitleDBTitleName();
   if (title == NULL)
      title = "(unlisted title)";

   if (!jaguarCartInserted)
   {
      LOG_WRN("[hooks] %s: REFUSED -- content is not a cartridge ROM image "
              "(RAM-loaded executable or CD); no bytes written\n", title);
      return 0;
   }

   if (TitleDBContentCRC() != jaguarMainROMCRC32)
   {
      LOG_WRN("[hooks] %s: REFUSED -- table key CRC32 $%08X does not match "
              "the loaded image's $%08X; no bytes written\n",
              title, (unsigned)TitleDBContentCRC(),
              (unsigned)jaguarMainROMCRC32);
      return 0;
   }

   return TitleHookApplyToBuffer(jaguarMainROM, jaguarROMSize,
                                 hooks, count, title, (int)jgdActive);
}
