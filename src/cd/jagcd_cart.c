/*
 * jagcd_cart.c — Cart/ROM boot strategy
 *
 * Handles standard Jaguar cartridge ROM loading. Loads the ROM file into
 * memory and calls JaguarReset() to start execution.
 */

#include "jagcd_boot.h"
#include "file.h"
#include "jaggd.h"          /* JGD_AUTO_THRESHOLD -- size of the flat cart window */
#include "jaguar.h"
#include "log.h"
#include "vjag_memory.h"

#include <stdlib.h>
#include <string.h>
#include <streams/file_stream.h>

RFILE* rfopen(const char *path, const char *mode);
int rfclose(RFILE* stream);
int64_t rfseek(RFILE* stream, int64_t offset, int origin);
int64_t rftell(RFILE* stream);
int64_t rfread(void* buffer, size_t elem_size, size_t elem_count, RFILE* stream);

static bool cart_boot(const struct retro_game_info *info)
{
    bool loaded = false;

    SET32(jaguarMainRAM, 0, 0x00200000);

    if (info && info->data && info->size > 0)
    {
        loaded = JaguarLoadFile((uint8_t *)info->data, info->size);
    }
    else if (info && info->path)
    {
        RFILE *romFile = rfopen(info->path, "rb");
        if (romFile)
        {
            int64_t fileSize;
            uint8_t *romData;

            rfseek(romFile, 0, SEEK_END);
            fileSize = rftell(romFile);
            rfseek(romFile, 0, SEEK_SET);

            romData = (uint8_t *)malloc(fileSize);
            if (romData)
            {
                rfread(romData, 1, fileSize, romFile);
                loaded = JaguarLoadFile(romData, fileSize);
                free(romData);
            }
            rfclose(romFile);
        }
    }

    if (!loaded)
    {
        LOG_ERR("[CART] JaguarLoadFile rejected the content\n");
        return false;
    }

    JaguarReset();

    /* JaguarReset() randomizes RAM contents, which destroys RAM-loaded
     * executables (ABS, COFF, JAGSERVER formats).  Cart ROMs are safe
     * because they live at $800000+ which isn't touched by reset.
     * Re-load the file so the program data is back in place. */
    if (!jaguarCartInserted)
    {
        if (info && info->data && info->size > 0)
        {
            if (!JaguarLoadFile((uint8_t *)info->data, info->size))
            {
                LOG_ERR("[CART] Failed to reload RAM-loaded content\n");
                return false;
            }
        }
    }

    LOG_INF("[CART] Boot path: cartridge ROM\n");
    return true;
}

static void cart_reset(void)
{
}

/* instruction_hook is NULL -- cart boot never traps any PC, and jaguar.c's
 * M68KInstructionHook already NULL-checks bootConfig.strategy->instruction_hook
 * before calling it, so this is identical to the always-false stub it
 * replaces (see perf(68k) hook-chain guard, issue #569). */
const CDBootStrategy cd_boot_strategy_cart = {
    "cart",
    cart_boot,
    NULL,
    cart_reset
};

/* No-content boot -- see the declaration comment in jagcd_boot.h. */
static bool none_boot(const struct retro_game_info *info)
{
    (void)info;

    /* Clear the flat cart window ($800000-$DFFEFF) so a previous title's
     * image cannot survive a same-process reload (iOS never dlcloses the
     * core) and be mistaken for an inserted cartridge.  jaguarROMSize and
     * the CRC follow suit so titledb / enhancement-hook / Memory-Track
     * lookups all see "no content" instead of the last title's values. */
    memset(jaguarMainROM, 0, JGD_AUTO_THRESHOLD);
    jaguarROMSize      = 0;
    jaguarMainROMCRC32 = 0;

    /* jaguarCartInserted here means "JaguarReset() must take the real
     * boot-ROM vector path" (copy SSP/PC from the staged boot ROM and park
     * illegal-fetch traps there), not "there is a valid program at
     * $800000" -- the window above is zeroed, so the boot ROM's own
     * cart-header check finds nothing, exactly like a real console with an
     * empty cart slot. */
    jaguarCartInserted = true;

    JaguarReset();

    LOG_INF("[BOOT] Boot path: no cartridge (bare console boot)\n");
    return true;
}

static void none_reset(void)
{
}

/* instruction_hook is NULL for the same reason cart_boot's is: nothing to
 * trap on an empty cart slot, and jaguar.c already NULL-checks it. */
const CDBootStrategy cd_boot_strategy_none = {
    "none",
    none_boot,
    NULL,
    none_reset
};
