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
    SET32(jaguarMainRAM, 0, 0x00200000);

    if (info->data && info->size > 0)
    {
        JaguarLoadFile((uint8_t *)info->data, info->size);
    }
    else if (info->path)
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
                JaguarLoadFile(romData, fileSize);
                free(romData);
            }
            rfclose(romFile);
        }
    }

    JaguarReset();
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
