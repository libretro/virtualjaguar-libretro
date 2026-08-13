/* test/tools/vjtrace_smoke.c -- minimal driver for the shared vjtrace
 * flight-recorder flags.
 *
 * It adds nothing of its own: it is the harness quick-start template
 * plus trace_probe_attach(), so whatever it produces is exactly what
 * every other harness tool gets for free by calling attach.  Useful as
 * a smoke test that the recorder is wired end to end, and as a generic
 * "just record this ROM" driver.
 *
 * Not part of `make test` (like test/tools/test_blitter_compare, build
 * it by hand):
 *
 *   cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
 *      -o /tmp/vjt_smoke test/tools/vjtrace_smoke.c \
 *      test/harness/harness.c test/harness/trace_probe.c -ldl -lm
 *
 *   VJ_EXPECT_BUILD=$(./scripts/build-id.sh) /tmp/vjt_smoke \
 *      ./virtualjaguar_libretro.dylib test/roms/yarc.j64 --frames 300 \
 *      --field-csv /tmp/f.csv --trace-out /tmp/t.ring --watch 0x0:0x1000:w
 */
#include <stdio.h>
#include "../harness/harness.h"
#include "../harness/trace_probe.h"

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    harness_result res;
    int ok;

    cfg.frames = 300;

    if (!harness_init_from_args(&cfg, argc, argv)) return 1;
    if (!cfg.rom_path) {
        fprintf(stderr,
                "usage: vjtrace_smoke [core] <rom> [--frames N] "
                "[--field-csv F] [--trace-out F] [--watch A[:LEN][:r|w|rw]] "
                "[--snap FRAME] [--snap-prefix BASE] [--mark FRAME:TAG]\n");
        return 1;
    }
    if (!harness_load_rom(&cfg))   return 1;
    if (!trace_probe_attach(&cfg)) return 1;

    harness_run(&cfg);
    trace_probe_finish(&cfg);

    ok = (cfg.video.total_frames_rendered > 0);
    res.status = ok ? "PASS" : "FAIL";
    res.name   = "vjtrace_smoke";
    res.detail = ok ? "core ran and the recorder was attached"
                    : "core presented no frames";
    harness_report(&cfg, &res, 1);
    harness_shutdown(&cfg);
    return ok ? 0 : 1;
}
