/* test_netlink_host.c — netlink TCP host resolution through the libretro
   option layer.  The virtualjaguar_netlink_host core option must:
     A. accept ANY string value verbatim (frontends with free-text option
        UIs, e.g. Provenance, can hand back values not in the preset list);
     B. treat the sentinel value "vj_netlink.txt" as "read the first line
        of <system_dir>/vj_netlink.txt" (legacy path, stock RetroArch);
     C. fall back to vj_netlink.txt-then-127.0.0.1 when the option is
        absent (frontend without core-option support);
     D. be overridden by the VJ_NETLINK_HOST environment variable.
   Requires the wide test ABI (TEST_EXPORTS=1) for JLinkGetTCPHost. */
#define _DEFAULT_SOURCE 1   /* glibc: expose setenv/unsetenv under c99 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "harness/harness.h"

typedef const char *(*get_host_t)(void);

#define ROM_SIZE 131072

static const char *make_synth_rom(void)
{
    static char path[256];
    static uint8_t rom_buf[ROM_SIZE];
    FILE *f;
    const char *tmp = getenv("TMPDIR");
    snprintf(path, sizeof(path), "%s/vj_netlink_host_stub.j64",
             tmp ? tmp : "/tmp");
    memset(rom_buf, 0, ROM_SIZE);
    rom_buf[0x404] = 0x00; rom_buf[0x405] = 0x80;
    rom_buf[0x406] = 0x20; rom_buf[0x407] = 0x00;
    rom_buf[0x2000] = 0x60; rom_buf[0x2001] = 0xFE;   /* bra.s * */
    f = fopen(path, "wb");
    if (!f) return NULL;
    fwrite(rom_buf, 1, ROM_SIZE, f);
    fclose(f);
    return path;
}

/* Fresh core load with the given host option (NULL = option not set) and
   system dir; returns 0 on harness failure, else compares the resolved
   host against expect. */
static int run_case(int argc, char **argv, const char *rom,
                    const char *sysdir, const char *host_opt,
                    const char *expect, const char *label)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    get_host_t get_host;
    const char *got;
    int ok;

    cfg.frames = 1;
    if (!harness_init_from_args(&cfg, argc, argv)) return 0;
    cfg.rom_path = rom;
    cfg.system_dir = sysdir;
    harness_set_option(&cfg, "virtualjaguar_netlink", "disabled");
    if (host_opt)
        harness_set_option(&cfg, "virtualjaguar_netlink_host", host_opt);

    if (!harness_load_rom(&cfg)) return 0;

    get_host = (get_host_t)harness_dlsym(&cfg, "JLinkGetTCPHost");
    if (!get_host)
    {
        fprintf(stderr, "JLinkGetTCPHost not exported — build with "
                        "TEST_EXPORTS=1\n");
        harness_shutdown(&cfg);
        return 0;
    }
    got = get_host();
    ok = (got && strcmp(got, expect) == 0);
    printf("%s %s: got \"%s\" want \"%s\"\n",
           ok ? "PASS" : "FAIL", label, got ? got : "(null)", expect);
    harness_shutdown(&cfg);
    return ok;
}

int main(int argc, char **argv)
{
    const char *rom = make_synth_rom();
    char dir_with_file[256], dir_empty[256], path[512];
    FILE *f;
    int pass = 0, total = 0;
    const char *tmp = getenv("TMPDIR");

    if (!rom)
    {
        fprintf(stderr, "cannot write synthetic ROM stub\n");
        return 1;
    }

    snprintf(dir_with_file, sizeof(dir_with_file),
             "%s/vj_nlh_sys_file", tmp ? tmp : "/tmp");
    snprintf(dir_empty, sizeof(dir_empty),
             "%s/vj_nlh_sys_empty", tmp ? tmp : "/tmp");
    mkdir(dir_with_file, 0755);
    mkdir(dir_empty, 0755);
    snprintf(path, sizeof(path), "%s/vj_netlink.txt", dir_with_file);
    f = fopen(path, "w");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return 1; }
    fputs("192.168.77.5\n", f);
    fclose(f);
    /* Make sure the empty dir really has no leftover file. */
    snprintf(path, sizeof(path), "%s/vj_netlink.txt", dir_empty);
    remove(path);

    unsetenv("VJ_NETLINK_HOST");

    /* A: arbitrary string not in the preset list is used verbatim. */
    total++; pass += run_case(argc, argv, rom, dir_empty,
                              "10.9.8.7", "10.9.8.7",
                              "A option accepts any string");

    /* B: sentinel reads the legacy file. */
    total++; pass += run_case(argc, argv, rom, dir_with_file,
                              "vj_netlink.txt", "192.168.77.5",
                              "B sentinel reads vj_netlink.txt");

    /* C: option absent -> file if present, else 127.0.0.1. */
    total++; pass += run_case(argc, argv, rom, dir_empty,
                              NULL, "127.0.0.1",
                              "C no option, no file -> localhost");
    total++; pass += run_case(argc, argv, rom, dir_with_file,
                              NULL, "192.168.77.5",
                              "C no option, file -> file");

    /* D: env var beats the option. */
    setenv("VJ_NETLINK_HOST", "172.16.0.9", 1);
    total++; pass += run_case(argc, argv, rom, dir_empty,
                              "10.9.8.7", "172.16.0.9",
                              "D env overrides option");
    unsetenv("VJ_NETLINK_HOST");

    printf("%d/%d netlink host cases passed\n", pass, total);
    return (pass == total) ? 0 : 1;
}
