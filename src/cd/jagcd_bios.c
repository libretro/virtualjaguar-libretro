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

                /* Populate the $2C00 track-info table.
                 *
                 * Layout is track-INDEXED to match the real CD BIOS's own TOC
                 * builder (disassembled at ROM $808BE8 in the retail CD
                 * BIOS: `movea.l #$2C00,a0; bra $808be8`).  Note: this is a
                 * 68K routine in the BIOS ROM that polls BUTCH DSA responses
                 * directly at $DFFF0A — NOT the DSP code the BIOS uploads to
                 * $F1B000 (that handles drive-level transport, a separate
                 * stage).  That routine clears
                 * $2C00..$2FFF, then for each full-TOC response word
                 * ($60nn=track#, $62nn=min, $63nn=sec, $64nn=frm) writes the
                 * entry for track nn at $2C00 + nn*8 ($808CB4:
                 * `move.w d2,d5; lsl.w #3,d5; move.b d2,(a0,d5.w)`), stamping
                 * the 0-based session number into byte[+4]
                 * ($808CC2: `move.b d7,$4(a0,d5.w)`).  $2C00 itself is a
                 * header, not track 1.
                 *
                 * Per-entry (track N at $2C00 + N*8):
                 *   +0 track number       +4 session number (0-based)
                 *   +1 start minute (MSF) +5..+7 track duration (unused here)
                 *   +2 start second
                 *   +3 start frame
                 *
                 * The game boot stubs read this table:
                 *   - Baldies $4E18 scans from $2C08 (track 1) in 8-byte steps,
                 *     terminating on a zero first longword and matching byte[+4]
                 *     against a session key of 1 (the data session).
                 *   - Primal Rage $0803E2 scans from $2C08 for the first
                 *     byte[+4]==1 entry, then reads the NEXT entry's MSF.
                 * Both require real track entries with a nonzero track# in
                 * byte[0] and the 0-based session in byte[4] — which the old
                 * sequential layout with a standalone zero-longword marker slot
                 * did not provide (Baldies' scan hit the zero terminator and
                 * ILLEGAL-halted; Primal Rage landed one track early). */
                {
                    uint32_t numTracks = CDIntfGetNumTracks();
                    uint32_t t;
                    uint8_t  maxTrack = 0;

                    memset(&jaguarMainRAM[0x2C00], 0, 0x400);

                    for (t = 1; t <= numTracks; t++)
                    {
                        uint32_t tocAddr = 0x2C00 + t * 8;
                        uint8_t  tsess;

                        if (tocAddr + 8 > 0x2C00 + 0x400)
                            break;   /* out of table space (max ~127 tracks) */

                        tsess = CDIntfGetTrackSession(t);

                        jaguarMainRAM[tocAddr + 0] = (uint8_t)t;
                        jaguarMainRAM[tocAddr + 1] = CDIntfGetTrackInfo(t, 0);
                        jaguarMainRAM[tocAddr + 2] = CDIntfGetTrackInfo(t, 1);
                        jaguarMainRAM[tocAddr + 3] = CDIntfGetTrackInfo(t, 2);
                        /* byte[4] = 0-based session number (BIOS writer stores
                         * d7, which counts sessions from 0); CDIntf reports
                         * 1-based sessions. */
                        jaguarMainRAM[tocAddr + 4] =
                            (uint8_t)((tsess >= 1) ? (tsess - 1) : 0);
                        /* bytes[5..7] = track duration as MSF.  Primal Rage's
                         * music player multiplies these into a sector count
                         * for its DSP playback countdown ($F1B278); zeroes
                         * here made every CD-audio track "finish" after one
                         * sector and the game silenced the mix instantly. */
                        jaguarMainRAM[tocAddr + 5] = CDIntfGetTrackDuration(t, 0);
                        jaguarMainRAM[tocAddr + 6] = CDIntfGetTrackDuration(t, 1);
                        jaguarMainRAM[tocAddr + 7] = CDIntfGetTrackDuration(t, 2);

                        maxTrack = (uint8_t)t;
                    }

                    /* Header at $2C00: bytes[0,1]=0, byte[2]=min track (1),
                     * byte[3]=max track.  The two boot-stub scanners start at
                     * $2C08 and never read the header; these cheap fields match
                     * the BIOS writer's header without reproducing its intricate
                     * leadout encoding, which nothing in scope consumes. */
                    if (maxTrack)
                    {
                        jaguarMainRAM[0x2C02] = 0x01;
                        jaguarMainRAM[0x2C03] = maxTrack;
                    }

                    LOG_INF("[CD-BOOTSTUB] Populated TOC at $2C00: %u tracks "
                            "(track-indexed, 0-based session in byte[4])\n",
                            numTracks);
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
