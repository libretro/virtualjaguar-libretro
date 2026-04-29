/*
 * jagcd_bios.c — Real CD BIOS boot strategy
 *
 * Handles the real Atari Jaguar CD BIOS path: loads the external BIOS ROM
 * as a "cartridge" at $800000, patches GPU authentication, and provides
 * 68K instruction hooks for CD authentication bypass, boot stub injection,
 * and DSP completion flag management.
 */

#include "jagcd_boot.h"
#include "cdintf.h"
#include "cdrom.h"
#include "dsp.h"
#include "gpu.h"
#include "jaguar.h"
#include "log.h"
#include "settings.h"
#include "vjag_memory.h"
#include "m68000/m68kinterface.h"

#include <string.h>

/* External CD BIOS data loaded by libretro.c.  Tier 2 will define these in
 * libretro.c; for Tier 1 we provide weak fallback definitions so the .dylib
 * links cleanly even though no path activates this strategy
 * (bootConfig.strategy == NULL until libretro.c populates it). */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) uint8_t external_cd_bios[0x40000];
__attribute__((weak)) bool cd_bios_loaded_externally = false;
#else
uint8_t external_cd_bios[0x40000];
bool cd_bios_loaded_externally = false;
#endif

static bool cdBootStubInjected = false;

static void bios_reset(void)
{
    cdBootStubInjected = false;
}

static bool bios_instruction_hook(uint32_t m68kPC)
{
    /* GPU auth magic — boot ROM checks this to verify GPU ran auth code.
     * Empirically still load-bearing: removing it makes every BIOS disc
     * loop at $0050B6 because the GPU never naturally writes the magic. */
    if (m68kPC == 0x005E40)
    {
        GPUWriteLong(0xF03000, 0x03D0DEAD, 0);
        return true;
    }

    /* Boot stub injection — triggered when BIOS is ready to jump to game code */
    if (m68kPC == 0x050176)
    {
        if (!cdBootStubInjected)
        {
            static uint8_t stub[600 * 1024];
            uint32_t loadAddr = 0, length = 0;
            if (CDIntfExtractBootStub(stub, sizeof(stub), &loadAddr, &length))
            {
                uint32_t i;
                for (i = 0; i < length && (loadAddr + i) < 0x200000; i++)
                    jaguarMainRAM[loadAddr + i] = stub[i];
                LOG_INF("[CD-BOOTSTUB] Injected $%X bytes at $%06X\n",
                        length, loadAddr);

                LOG_INF("[CD-BOOTSTUB] Bytes at PC=$050176: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                        jaguarMainRAM[0x050176], jaguarMainRAM[0x050177],
                        jaguarMainRAM[0x050178], jaguarMainRAM[0x050179],
                        jaguarMainRAM[0x05017A], jaguarMainRAM[0x05017B],
                        jaguarMainRAM[0x05017C], jaguarMainRAM[0x05017D]);
                LOG_INF("[CD-BOOTSTUB] JSR target at $050178 = $%02X%02X%02X%02X\n",
                        jaguarMainRAM[0x050178], jaguarMainRAM[0x050179],
                        jaguarMainRAM[0x05017A], jaguarMainRAM[0x05017B]);

                if (loadAddr != 0x080000)
                {
                    LOG_INF("[CD-BOOTSTUB] Boot stub loads at $%06X, not $080000 — "
                            "installing trampoline at $080000\n", loadAddr);
                    /* JMP loadAddr (4EF9 xxxx xxxx) */
                    jaguarMainRAM[0x080000] = 0x4E;
                    jaguarMainRAM[0x080001] = 0xF9;
                    jaguarMainRAM[0x080002] = (loadAddr >> 24) & 0xFF;
                    jaguarMainRAM[0x080003] = (loadAddr >> 16) & 0xFF;
                    jaguarMainRAM[0x080004] = (loadAddr >>  8) & 0xFF;
                    jaguarMainRAM[0x080005] = (loadAddr >>  0) & 0xFF;
                }

                /* Populate TOC at $2C00 */
                {
                    uint32_t numTracks = CDIntfGetNumTracks();
                    uint32_t t, tocAddr = 0x2C00;
                    bool wroteMarker = false;

                    memset(&jaguarMainRAM[0x2C00], 0, 0x400);

                    for (t = 1; t <= numTracks && tocAddr < 0x2C00 + 0x3F8; t++)
                    {
                        uint8_t tmin  = CDIntfGetTrackInfo(t, 0);
                        uint8_t tsec  = CDIntfGetTrackInfo(t, 1);
                        uint8_t tfrm  = CDIntfGetTrackInfo(t, 2);
                        uint8_t tsess = CDIntfGetTrackSession(t);

                        if (tsess >= 2 && !wroteMarker)
                        {
                            jaguarMainRAM[tocAddr + 4] = 0x01;
                            tocAddr += 8;
                            wroteMarker = true;
                        }

                        jaguarMainRAM[tocAddr + 0] = (uint8_t)t;
                        jaguarMainRAM[tocAddr + 1] = tmin;
                        jaguarMainRAM[tocAddr + 2] = tsec;
                        jaguarMainRAM[tocAddr + 3] = tfrm;
                        tocAddr += 8;
                    }
                    LOG_INF("[CD-BOOTSTUB] Populated TOC at $2C00: %u tracks, "
                            "session marker=%s\n", numTracks,
                            wroteMarker ? "yes" : "no");
                }
                cdBootStubInjected = true;
            }
            else
            {
                LOG_INF("[CD-BOOTSTUB] CDIntfExtractBootStub failed\n");
            }
        }
        return true;
    }

    return false;
}

static bool bios_boot(const struct retro_game_info *info)
{
    const uint8_t *cdBiosData = external_cd_bios;
    size_t cdBiosSize = 0x40000;

    memcpy(jagMemSpace + 0x800000, cdBiosData, cdBiosSize);
    jaguarRunAddress = GET32(jagMemSpace, 0x800404);
    jaguarCartInserted = true;
    jaguarROMSize = cdBiosSize;

    /* Skip the boot ROM's GPU-based cart authentication check */
    jagMemSpace[0x80040B] &= 0xFE;

    JaguarReset();
    LOG_INF("[CD] Boot path: REAL BIOS at $%06X (CD BIOS loaded as cart)\n",
            jaguarRunAddress);
    return true;
}

const CDBootStrategy cd_boot_strategy_bios = {
    "bios",
    bios_boot,
    bios_instruction_hook,
    bios_reset
};
