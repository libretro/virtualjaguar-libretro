#ifndef __JAGCD_BOOT_H__
#define __JAGCD_BOOT_H__

#include <stdint.h>
#include <stddef.h>
#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

struct retro_game_info;

typedef struct CDBootStrategy {
    const char *name;
    bool (*boot)(const struct retro_game_info *info);
    bool (*instruction_hook)(uint32_t pc);
    void (*reset)(void);
} CDBootStrategy;

extern const CDBootStrategy cd_boot_strategy_hle;
extern const CDBootStrategy cd_boot_strategy_bios;
extern const CDBootStrategy cd_boot_strategy_cart;

/* No-content boot (RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME): the frontend
 * launched the core with no cartridge or disc at all.  Real hardware still
 * runs the boot ROM off the reset vector with an empty cart slot -- it
 * shows the boot animation and then sits, because the ROM's own
 * cart-header check never finds anything to jump to.  This strategy
 * mirrors that: it clears the cart ROM window (so a previous title's image
 * can't survive a same-process reload) and always forces the real boot ROM
 * path, since there is no 68K program anywhere for HLE to jump into.
 * Selected directly in retro_load_game() when info == NULL; never produced
 * by ResolveBootConfig(), which only knows about cart/CD content. */
extern const CDBootStrategy cd_boot_strategy_none;

/* Savestate chunk for the real-BIOS boot path (#804): whether the boot stub
 * has been injected yet.  Appended strictly last to the state blob behind a
 * magic word.  retro_unserialize() only calls Load for v16+ states and
 * leaves the live value alone for older ones (forcing "not injected" would
 * re-arm the BIOS hooks over a mid-game state's RAM); a chunk whose magic
 * is wrong takes JaguarCDBiosStateReset().  Save/Load return the bytes
 * consumed. */
size_t JaguarCDBiosStateSize(void);
size_t JaguarCDBiosStateSave(uint8_t *buf);
size_t JaguarCDBiosStateLoad(const uint8_t *buf);
void   JaguarCDBiosStateReset(void);

#ifdef __cplusplus
}
#endif

#endif /* __JAGCD_BOOT_H__ */
