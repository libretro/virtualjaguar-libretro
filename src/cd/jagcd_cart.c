/*
 * jagcd_cart.c — Cart/ROM boot strategy
 *
 * Handles standard Jaguar cartridge ROM loading. Loads the ROM file into
 * memory and calls JaguarReset() to start execution.
 */

#include "jagcd_boot.h"
#include "file.h"
#include "jaguar.h"
#include "log.h"
#include "vjag_memory.h"

#include <stdlib.h>
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

static bool cart_instruction_hook(uint32_t pc)
{
    (void)pc;
    return false;
}

static void cart_reset(void)
{
}

const CDBootStrategy cd_boot_strategy_cart = {
    "cart",
    cart_boot,
    cart_instruction_hook,
    cart_reset
};
