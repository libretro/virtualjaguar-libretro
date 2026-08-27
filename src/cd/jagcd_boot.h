#ifndef __JAGCD_BOOT_H__
#define __JAGCD_BOOT_H__

#include <stdint.h>
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

#ifdef __cplusplus
}
#endif

#endif /* __JAGCD_BOOT_H__ */
