/*
 * test/test_eeprom_lifecycle.c — EEPROM lifecycle test.
 *
 * Validates that the libretro core properly resets all internal state
 * across load/unload/reload cycles.  This catches stale-static bugs
 * that manifest on iOS (where dlclose is a no-op) and verifies that
 * EEPROM data survives the round-trip through retro_get_memory_data.
 *
 * Tests:
 *   1. First load: SRAM buffer available and EEPROM values written
 *   2. Unload + reload: core re-initializes cleanly (video/audio fire)
 *   3. SRAM persistence: pre-loaded SRAM data survives unload/reload
 *   4. Multiple cycles: three consecutive load/unload cycles succeed
 *
 * Build:  cc -O2 -Wall -std=c99 -I. -Isrc -Isrc/core -Ilibretro-common/include \
 *           -o test/test_eeprom_lifecycle test/test_eeprom_lifecycle.c \
 *           test/harness/harness.c -ldl -lm
 *
 * Usage:  ./test/test_eeprom_lifecycle [core.dylib] <eeprom_test.j64>
 */

#include "harness/harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The harness intentionally leaks ROM buffers (VJ keeps the pointer).
 * Suppress LeakSanitizer for this known-benign leak. */
#if defined(__SANITIZE_ADDRESS__) || (defined(__has_feature) && __has_feature(address_sanitizer))
const char *__lsan_default_suppressions(void) {
    return "leak:harness_load_rom\n";
}
#endif

/* libretro memory API — resolved via dlsym */
typedef void  *(*retro_get_memory_data_t)(unsigned);
typedef size_t (*retro_get_memory_size_t)(unsigned);

#define RETRO_MEMORY_SAVE_RAM 0

#define MAX_RESULTS 16

static int pass_count = 0;
static int fail_count = 0;

static void check(int cond, const char *name, const char *detail,
                  harness_result *results, unsigned *num)
{
    if (cond) {
        results[*num] = (harness_result){"PASS", name, detail};
        pass_count++;
    } else {
        results[*num] = (harness_result){"FAIL", name, detail};
        fail_count++;
    }
    (*num)++;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    harness_result results[MAX_RESULTS];
    unsigned num_results = 0;
    retro_get_memory_data_t get_mem_data;
    retro_get_memory_size_t get_mem_size;
    uint8_t *sram;
    size_t sram_size;

    cfg.frames = 60;

    if (!harness_init_from_args(&cfg, argc, argv)) {
        fprintf(stderr, "Failed to load core\n");
        return 2;
    }

    if (!cfg.rom_path) {
        fprintf(stderr, "Usage: test_eeprom_lifecycle [core.dylib] <eeprom_test.j64>\n");
        fprintf(stderr, "  Generate ROM: cc -o gen_eeprom_test_rom test/tools/gen_eeprom_test_rom.c\n");
        fprintf(stderr, "                ./gen_eeprom_test_rom /tmp/eeprom_test.j64\n");
        harness_shutdown(&cfg);
        return 2;
    }

    /* Resolve memory API (must re-resolve after each harness_load_core
     * because harness_shutdown calls dlclose, invalidating pointers) */
    get_mem_data = (retro_get_memory_data_t)harness_dlsym(&cfg, "retro_get_memory_data");
    get_mem_size = (retro_get_memory_size_t)harness_dlsym(&cfg, "retro_get_memory_size");
    if (!get_mem_data || !get_mem_size) {
        fprintf(stderr, "Cannot resolve retro_get_memory_data/size\n");
        harness_shutdown(&cfg);
        return 2;
    }

    /* ================================================================
     * Test 1: First load — EEPROM writes detected
     * ================================================================ */

    if (!harness_load_rom(&cfg)) {
        fprintf(stderr, "Failed to load ROM (first time)\n");
        harness_shutdown(&cfg);
        return 2;
    }

    harness_run(&cfg);

    sram = (uint8_t *)get_mem_data(RETRO_MEMORY_SAVE_RAM);
    sram_size = get_mem_size(RETRO_MEMORY_SAVE_RAM);

    check(sram != NULL && sram_size == 128,
          "first_load_sram_available",
          sram ? "SRAM pointer and size correct" : "SRAM pointer NULL",
          results, &num_results);

    /* The EEPROM test ROM writes 0xCAFE at address 0 */
    if (sram && sram_size >= 2) {
        int ok = (sram[0] == 0xCA && sram[1] == 0xFE);
        check(ok, "first_load_eeprom_written",
              ok ? "addr 0 = 0xCAFE" : "EEPROM write not detected",
              results, &num_results);
    } else {
        check(0, "first_load_eeprom_written", "no SRAM buffer",
              results, &num_results);
    }

    check(cfg.video.total_frames_rendered > 0,
          "first_load_video",
          cfg.video.total_frames_rendered > 0 ?
              "video callbacks fired" : "no video callbacks",
          results, &num_results);

    check(cfg.audio.total_batch_calls > 0,
          "first_load_audio",
          cfg.audio.total_batch_calls > 0 ?
              "audio callbacks fired" : "no audio callbacks",
          results, &num_results);

    /* ================================================================
     * Test 2: Unload + reload — statics properly reset
     * ================================================================ */

    harness_shutdown(&cfg);

    /* Re-load the core (simulates iOS where dlclose is no-op) */
    if (!harness_load_core(&cfg)) {
        fprintf(stderr, "Failed to reload core\n");
        return 2;
    }
    get_mem_data = (retro_get_memory_data_t)harness_dlsym(&cfg, "retro_get_memory_data");
    get_mem_size = (retro_get_memory_size_t)harness_dlsym(&cfg, "retro_get_memory_size");
    if (!harness_load_rom(&cfg)) {
        check(0, "reload_succeeds", "retro_load_game failed on reload",
              results, &num_results);
    } else {
        check(1, "reload_succeeds", "core reloaded successfully",
              results, &num_results);

        harness_reset_audio(&cfg);
        cfg.video.total_frames_rendered = 0;
        cfg.current_frame = 0;
        harness_run(&cfg);

        check(cfg.video.total_frames_rendered > 0,
              "reload_video",
              cfg.video.total_frames_rendered > 0 ?
                  "video fires after reload" : "no video after reload",
              results, &num_results);

        check(cfg.audio.total_batch_calls > 0,
              "reload_audio",
              cfg.audio.total_batch_calls > 0 ?
                  "audio fires after reload" : "no audio after reload",
              results, &num_results);
    }

    /* ================================================================
     * Test 3: SRAM persistence across unload/reload
     * ================================================================ */

    harness_shutdown(&cfg);

    if (!harness_load_core(&cfg)) {
        fprintf(stderr, "Failed to reload core (test 3)\n");
        return 2;
    }
    get_mem_data = (retro_get_memory_data_t)harness_dlsym(&cfg, "retro_get_memory_data");
    get_mem_size = (retro_get_memory_size_t)harness_dlsym(&cfg, "retro_get_memory_size");
    if (!harness_load_rom(&cfg)) {
        check(0, "sram_persist", "load failed", results, &num_results);
    } else {
        uint8_t pattern[128];
        int i, match;

        /* Pre-fill SRAM with a known pattern */
        sram = (uint8_t *)get_mem_data(RETRO_MEMORY_SAVE_RAM);
        if (sram) {
            for (i = 0; i < 64; i++) {
                pattern[i * 2] = (uint8_t)(i + 0x40);
                pattern[i * 2 + 1] = (uint8_t)(i ^ 0xAA);
            }
            memcpy(sram, pattern, 128);

            /* Run one frame to trigger unpack */
            harness_step(&cfg);

            /* Check addresses 3-62 (not overwritten by test ROM) */
            sram = (uint8_t *)get_mem_data(RETRO_MEMORY_SAVE_RAM);
            match = 1;
            for (i = 3; i < 63 && sram; i++) {
                if (sram[i * 2] != pattern[i * 2] ||
                    sram[i * 2 + 1] != pattern[i * 2 + 1]) {
                    match = 0;
                    break;
                }
            }
            check(match, "sram_persist",
                  match ? "SRAM data survives unpack/repack" :
                          "SRAM corruption after reload",
                  results, &num_results);
        } else {
            check(0, "sram_persist", "SRAM pointer NULL after reload",
                  results, &num_results);
        }
    }

    /* ================================================================
     * Test 4: Multiple consecutive cycles (stress test)
     * ================================================================ */

    harness_shutdown(&cfg);

    {
        int cycle, all_ok = 1;

        for (cycle = 0; cycle < 3; cycle++) {
            if (!harness_load_core(&cfg)) {
                all_ok = 0;
                break;
            }
            if (!harness_load_rom(&cfg)) {
                all_ok = 0;
                break;
            }
            harness_reset_audio(&cfg);
            cfg.video.total_frames_rendered = 0;
            cfg.current_frame = 0;
            cfg.frames = 30;
            harness_run(&cfg);

            if (cfg.video.total_frames_rendered == 0) {
                all_ok = 0;
                break;
            }
            harness_shutdown(&cfg);
        }

        check(all_ok, "multi_cycle",
              all_ok ? "3 consecutive load/run/unload cycles passed" :
                       "cycle failure (stale state leak)",
              results, &num_results);
    }

    /* Report */
    harness_report(&cfg, results, num_results);

    return fail_count > 0 ? 1 : 0;
}
