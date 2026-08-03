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

#ifdef __cplusplus
}
#endif

#endif /* __JAGCD_BOOT_H__ */
