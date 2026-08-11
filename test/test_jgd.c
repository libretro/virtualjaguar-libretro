/*
 * test/test_jgd.c — Jaguar GameDrive (JagGD) detection + banking test.
 *
 * Synthetic-only (no private ROMs needed, like test_cd_synth_*): builds a
 * probe cartridge image at runtime whose 68K program replays the exact
 * logic of RetroHQ's published gdbios_bindings.s — the GDWaitData
 * handshake, the HW-version command (12), the GDBIOS install command
 * ($80), then blob calls (GD_BIOSVersion / GD_ROMSetPages / GD_ROMSetPage
 * / GD_ROMWriteEnable) — and stores every intermediate result at fixed
 * main-RAM addresses this harness reads back via dlsym'd core internals.
 *
 * Unlike the real bindings, the probe's handshake spin loops carry a
 * bail-out counter: on a machine with no GD the real code spins forever
 * (GDWaitData .waitAck), so the "option disabled" phase asserts the
 * timeout marker — proof the wedge would have happened — instead of
 * hanging the suite.
 *
 * Phases (separate core lifecycles in one process, which also exercises
 * the iOS static-state reset contract):
 *   A) virtualjaguar_jgd=disabled + 16 MB image: detect must FAIL cleanly
 *   B) virtualjaguar_jgd=auto + 16 MB image: install + banking + straddle
 *      + write-enable, then savestate round-trip (v8) and the pre-v8
 *      degradation contract (v7-headered state loads, GD resets)
 *   C) virtualjaguar_jgd=enabled + 2 MB image: the ForceJGD path for
 *      GD-locked images smaller than the cart window
 *
 * Exit codes: 0 = pass, 1 = fail, 2 = harness error.
 *
 * Build:  cc -O2 -Wall -std=c99 $(INCFLAGS) -o test/test_jgd \
 *           test/test_jgd.c test/harness/harness.c -ldl -lm
 */

#include "harness/harness.h"
#include "state.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#define MAX_RESULTS 64

/* Probe result mailbox in Jaguar main RAM. */
#define R_FLAG      0x10000   /* progress / final magic */
#define R_ERR       0x10004   /* failure marker */
#define R_HWVER     0x10010   /* u32 from command 12 */
#define R_BLOBSIZE  0x10014   /* u16 size prefix from command $80 */
#define R_BLOBHDR   0x10018   /* blob version<<16 | function count */
#define R_BIOSVER   0x1001C   /* GD_BIOSVersion() */
#define R_MARKERS   0x10020   /* six u32 window markers after ROMSetPages */
#define R_STRADDLE  0x10040   /* u32 read straddling the page 0/1 boundary */
#define R_SETPAGE   0x10044   /* marker after ROMSetPage(2,3) */
#define R_WREN      0x10048   /* read-back of write-enabled cart write */
#define R_WRDIS     0x1004C   /* read-back after write disable (must miss) */

#define MAGIC_DONE     0xC0DE600Du
#define MAGIC_FAILED   0xDEADDEADu
#define ERR_TIMEOUT    0xDEAD0001u
#define ERR_VERSION    0xDEAD0002u

/* Blob served by jaggd.c (kept in sync with src/core/jaggd.c). */
#define EXPECT_BLOB_SIZE   208
#define EXPECT_BLOB_HDR    0x0100001Au  /* version $0100, 26 functions */
#define EXPECT_HWVER       0x03000102u  /* FW $03.00 (>= $01.11), ASIC $01.02 */
#define EXPECT_BIOSVER     0x00000100u
/* JGD chunk byte count (see JGDStateSave): 16 + 6*2 + 512. */
#define EXPECT_JGD_CHUNK   540

#define STATE_OFF_VERSION  4

typedef size_t (*jgd_state_save_fn)(uint8_t *buf);
typedef size_t (*serialize_size_fn)(void);
/* bool, not int: retro_serialize/unserialize return bool, and UBSan's
 * function-type-mismatch check (Linux CI) rejects calls through a
 * mistyped pointer even when the ABI happens to coincide.  Matches
 * test_state_compat.c. */
typedef bool   (*serialize_fn)(void *data, size_t size);
typedef bool   (*unserialize_fn)(const void *data, size_t size);
typedef void   (*reset_fn)(void);
typedef uint32_t (*read_long_fn)(uint32_t offset, uint32_t who);

static int pass_count = 0;
static int fail_count = 0;
static harness_result results[MAX_RESULTS];
static unsigned num_results = 0;
static char detail_store[MAX_RESULTS][192];

static void check(int cond, const char *name, const char *fmt, ...)
{
    va_list ap;

    if (num_results >= MAX_RESULTS)
        return;

    va_start(ap, fmt);
    vsnprintf(detail_store[num_results], sizeof(detail_store[0]), fmt, ap);
    va_end(ap);

    results[num_results].status = cond ? "PASS" : "FAIL";
    results[num_results].name   = name;
    results[num_results].detail = detail_store[num_results];
    num_results++;

    if (cond)
        pass_count++;
    else
        fail_count++;
}

/* ================================================================
 * Probe ROM builder — 68K hand-assembly (big-endian), same approach
 * as test_boot_patterns.c's ROM builder.
 * ================================================================ */

#define CODE_ADDR   0x00802000u
#define BLOB_BUF    0x00004000u   /* long-aligned RAM buffer for the blob */

#define GD_STATUS   0x00F16002u
#define GD_DATA     0x00F16004u
#define GD_DATAB    0x00F16005u

/* Bounded-spin iteration count for the disabled phase (the real
 * bindings spin forever; ~50k iterations is a dozen frames). */
#define SPIN_LIMIT  50000

static uint8_t *g_rom;
static uint32_t g_pos;         /* current emit offset within the image */

static void e16(uint16_t v)
{
    g_rom[g_pos++] = (uint8_t)(v >> 8);
    g_rom[g_pos++] = (uint8_t)(v & 0xFF);
}

static void e32(uint32_t v)
{
    e16((uint16_t)(v >> 16));
    e16((uint16_t)(v & 0xFFFF));
}

/* move.l #imm,abs.l */
static void e_movel_imm_abs(uint32_t imm, uint32_t addr)
{ e16(0x23FC); e32(imm); e32(addr); }
/* move.w #imm,abs.l */
static void e_movew_imm_abs(uint16_t imm, uint32_t addr)
{ e16(0x33FC); e16(imm); e32(addr); }
/* move.w abs.l,dn */
static void e_movew_abs_dn(uint32_t addr, int dn)
{ e16((uint16_t)(0x3039 | (dn << 9))); e32(addr); }
/* move.l abs.l,dn */
static void e_movel_abs_dn(uint32_t addr, int dn)
{ e16((uint16_t)(0x2039 | (dn << 9))); e32(addr); }
/* move.l dn,abs.l */
static void e_movel_dn_abs(int dn, uint32_t addr)
{ e16((uint16_t)(0x23C0 | dn)); e32(addr); }
/* move.w dn,abs.l */
static void e_movew_dn_abs(int dn, uint32_t addr)
{ e16((uint16_t)(0x33C0 | dn)); e32(addr); }
/* move.b abs.l,dn */
static void e_moveb_abs_dn(uint32_t addr, int dn)
{ e16((uint16_t)(0x1039 | (dn << 9))); e32(addr); }
/* move.b abs.l,(a0)+ */
static void e_moveb_abs_a0inc(uint32_t addr)
{ e16(0x10F9); e32(addr); }
/* move.l #imm,dn */
static void e_movel_imm_dn(int dn, uint32_t imm)
{ e16((uint16_t)(0x203C | (dn << 9))); e32(imm); }
/* move.w #imm,dn */
static void e_movew_imm_dn(int dn, uint16_t imm)
{ e16((uint16_t)(0x303C | (dn << 9))); e16(imm); }
/* movea.l #imm,an */
static void e_movea_imm(int an, uint32_t imm)
{ e16((uint16_t)(0x207C | (an << 9))); e32(imm); }
/* moveq #v,dn */
static void e_moveq(int dn, int v)
{ e16((uint16_t)(0x7000 | (dn << 9) | (v & 0xFF))); }
/* btst #bit,d0 */
static void e_btst_d0(int bit)
{ e16(0x0800); e16((uint16_t)bit); }
/* subq.l #1,dn */
static void e_subql_1(int dn)
{ e16((uint16_t)(0x5380 | dn)); }
/* subq.w #1,dn */
static void e_subqw_1(int dn)
{ e16((uint16_t)(0x5340 | dn)); }
/* andi.l #imm,d0 */
static void e_andil_d0(uint32_t imm)
{ e16(0x0280); e32(imm); }
/* cmpi.w #imm,d0 */
static void e_cmpiw_d0(uint16_t imm)
{ e16(0x0C40); e16(imm); }
/* clr.w abs.l */
static void e_clrw_abs(uint32_t addr)
{ e16(0x4279); e32(addr); }
/* swap d0 */
static void e_swap_d0(void)
{ e16(0x4840); }
/* move.w d0,d1 */
static void e_movew_d0_d1(void)
{ e16(0x3200); }
/* ror.w #8,d0 */
static void e_rorw8_d0(void)
{ e16(0xE058); }
/* jsr d16(a6) */
static void e_jsr_a6(int disp)
{ e16(0x4EAE); e16((uint16_t)disp); }
/* rts */
static void e_rts(void)
{ e16(0x4E75); }
/* bra.s self */
static void e_bra_self(void)
{ e16(0x60FE); }

/* Short conditional/unconditional branches to a KNOWN (backward) target,
 * and a forward-patch variant. */
static void e_bcc_s_to(uint16_t opcode, uint32_t target)
{
    int disp = (int)target - (int)(g_pos + 2);
    e16((uint16_t)(opcode | (disp & 0xFF)));
}

static uint32_t e_bcc_s_fwd(uint16_t opcode)   /* returns patch position */
{
    uint32_t at = g_pos;
    e16(opcode);   /* displacement patched later */
    return at;
}

static void patch_bcc_s(uint32_t at, uint32_t target)
{
    int disp = (int)target - (int)(at + 2);
    g_rom[at + 1] = (uint8_t)(disp & 0xFF);
}

/* bra.w / bsr.w to a known target */
static void e_braw_to(uint32_t target)
{
    e16(0x6000);
    e16((uint16_t)((int)target - (int)g_pos));
}

static void e_bsrw_to(uint32_t target)
{
    e16(0x6100);
    e16((uint16_t)((int)target - (int)g_pos));
}

/* dbra dn,<target> */
static void e_dbra_to(int dn, uint32_t target)
{
    e16((uint16_t)(0x51C8 | dn));
    e16((uint16_t)((int)target - (int)g_pos));
}

/*
 * Build the probe image.  size must be a multiple of 1 MB.  Every 1 MB
 * bank that exists in the image gets:
 *   +$00  u16 pattern (0x50+bank)<<8 | (0xA0+bank)   [straddle low half]
 *   +$10  u32 0xC0DE0000 | bank                      [window marker]
 * and bank 0 additionally carries the cart header, the code, and
 *   +$FFFFC u32 0xAA55A0A0                           [straddle high half]
 */
static void build_probe_rom(uint8_t *rom, uint32_t size)
{
    uint32_t banks = size >> 20;
    uint32_t b;
    uint32_t l_waitdata, l_xword, l_timeout, l_verfail, l_main;
    uint32_t l_wd1, l_wd2, l_drain, l_copy;
    uint32_t p_sel, p_ok, p_fwok, p_nodrain;

    memset(rom, 0, size);
    g_rom = rom;

    for (b = 0; b < banks; b++)
    {
        uint32_t base = b << 20;
        rom[base + 0] = (uint8_t)(0x50 + b);
        rom[base + 1] = (uint8_t)(0xA0 + b);
        rom[base + 0x10] = 0xC0;
        rom[base + 0x11] = 0xDE;
        rom[base + 0x12] = 0x00;
        rom[base + 0x13] = (uint8_t)b;
    }
    /* bank 0, last long before the page 0/1 boundary */
    rom[0xFFFFC] = 0xAA; rom[0xFFFFD] = 0x55;
    rom[0xFFFFE] = 0xA0; rom[0xFFFFF] = 0xA0;

    /* Cart header: universal marker + run address (patched to main below) */
    rom[0x400] = 0x04; rom[0x401] = 0x04; rom[0x402] = 0x04; rom[0x403] = 0x04;

    g_pos = 0x2000;

    /* ---- timeout_fail ---- */
    l_timeout = g_pos;
    e_movel_imm_abs(ERR_TIMEOUT, R_ERR);
    e_movel_imm_abs(MAGIC_FAILED, R_FLAG);
    e_bra_self();

    /* ---- version_fail ---- */
    l_verfail = g_pos;
    e_movel_imm_abs(ERR_VERSION, R_ERR);
    e_movel_imm_abs(MAGIC_FAILED, R_FLAG);
    e_bra_self();

    /* ---- gd_waitdata (bounded) ----
     * spin HAVE_DATA==0; write $0011; spin HAVE_DATA==1.  Real bindings
     * spin forever; the bail-out proves the no-GD wedge without hanging. */
    l_waitdata = g_pos;
    e_movel_imm_dn(7, SPIN_LIMIT);
    l_wd1 = g_pos;
    e_movew_abs_dn(GD_STATUS, 0);
    e_btst_d0(3);
    p_sel = e_bcc_s_fwd(0x6700);          /* beq.s .sel */
    e_subql_1(7);
    e_bcc_s_to(0x6600, l_wd1);            /* bne.s .wd1 */
    e_braw_to(l_timeout);
    patch_bcc_s(p_sel, g_pos);            /* .sel: */
    e_movew_imm_abs(0x0011, GD_STATUS);
    e_movel_imm_dn(7, SPIN_LIMIT);
    l_wd2 = g_pos;
    e_movew_abs_dn(GD_STATUS, 0);
    e_btst_d0(3);
    p_ok = e_bcc_s_fwd(0x6600);           /* bne.s .ok */
    e_subql_1(7);
    e_bcc_s_to(0x6600, l_wd2);            /* bne.s .wd2 */
    e_braw_to(l_timeout);
    patch_bcc_s(p_ok, g_pos);             /* .ok: */
    e_rts();

    /* ---- gd_xword: word exchange, tx low byte first, rx high first ---- */
    l_xword = g_pos;
    e_movew_dn_abs(0, GD_DATA);
    e_moveb_abs_dn(GD_DATAB, 0);
    e_rorw8_d0();
    e_movew_dn_abs(0, GD_DATA);
    e_moveb_abs_dn(GD_DATAB, 0);
    e_rts();

    /* ---- main ---- */
    l_main = g_pos;
    e_movel_imm_abs(1, R_FLAG);

    /* HW version (command 12) */
    e_movew_imm_abs(0x0010, GD_STATUS);
    e_bsrw_to(l_waitdata);
    e_movel_imm_abs(2, R_FLAG);
    e_movew_imm_dn(0, 12);
    e_bsrw_to(l_xword);
    e_moveq(0, 0);
    e_bsrw_to(l_xword);
    e_movew_imm_abs(0x0010, GD_STATUS);
    e_bsrw_to(l_waitdata);
    e_bsrw_to(l_xword);                   /* firmware word */
    e_swap_d0();
    e_bsrw_to(l_xword);                   /* ASIC word */
    e_movel_dn_abs(0, R_HWVER);
    /* GD_Install's firmware gate: FW must be >= $0111 */
    e_swap_d0();
    e_cmpiw_d0(0x0111);
    p_fwok = e_bcc_s_fwd(0x6C00);         /* bge.s .fwok */
    e_braw_to(l_verfail);
    patch_bcc_s(p_fwok, g_pos);           /* .fwok: */
    e_clrw_abs(GD_STATUS);

    /* Drain the rx latch ("in case of FIFO DMA termination") */
    l_drain = g_pos;
    e_movew_abs_dn(GD_STATUS, 0);
    e_btst_d0(5);
    p_nodrain = e_bcc_s_fwd(0x6700);      /* beq.s .nodrain */
    e_movew_abs_dn(GD_DATA, 1);           /* word read drains the latch */
    e_bcc_s_to(0x6000, l_drain);          /* bra.s .drain */
    patch_bcc_s(p_nodrain, g_pos);        /* .nodrain: */

    /* Install the GDBIOS blob (command $80) */
    e_movew_imm_abs(0x0010, GD_STATUS);
    e_bsrw_to(l_waitdata);
    e_movew_imm_dn(0, 0x80);
    e_bsrw_to(l_xword);
    e_moveq(0, 0);
    e_bsrw_to(l_xword);
    e_movew_imm_abs(0x0010, GD_STATUS);
    e_bsrw_to(l_waitdata);
    e_bsrw_to(l_xword);                   /* u16 blob size */
    e_movew_imm_abs(0x0010, GD_STATUS);   /* done with this block */
    e_andil_d0(0xFFFF);
    e_movel_dn_abs(0, R_BLOBSIZE);
    /* copy loop (our blob is 208 bytes: a single <=512 block) */
    e_movea_imm(0, BLOB_BUF);
    e_movew_d0_d1();
    e_bsrw_to(l_waitdata);
    e_subqw_1(1);
    l_copy = g_pos;
    e_movew_dn_abs(1, GD_DATA);           /* dummy tx clocks a byte out */
    e_moveb_abs_a0inc(GD_DATAB);
    e_dbra_to(1, l_copy);
    e_movew_imm_abs(0x0010, GD_STATUS);
    e_clrw_abs(GD_STATUS);
    e_movel_imm_abs(3, R_FLAG);

    /* Blob header: version + function count */
    e_movel_abs_dn(BLOB_BUF, 0);
    e_movel_dn_abs(0, R_BLOBHDR);

    /* a6 = blob base (GDFunc convention), call GD_BIOSVersion (3) */
    e_movea_imm(6, BLOB_BUF);
    e_jsr_a6(3 * 4);
    e_andil_d0(0xFFFF);
    e_movel_dn_abs(0, R_BIOSVER);

    /* GD_ROMSetPages($BCDEF0): page0=0, pages 1..5 = banks 15,14,13,12,11 */
    e_movel_imm_dn(0, 0x00BCDEF0);
    e_jsr_a6(6 * 4);

    /* Window markers */
    e_movel_abs_dn(0x800010, 0); e_movel_dn_abs(0, R_MARKERS + 0);
    e_movel_abs_dn(0x900010, 0); e_movel_dn_abs(0, R_MARKERS + 4);
    e_movel_abs_dn(0xA00010, 0); e_movel_dn_abs(0, R_MARKERS + 8);
    e_movel_abs_dn(0xB00010, 0); e_movel_dn_abs(0, R_MARKERS + 12);
    e_movel_abs_dn(0xC00010, 0); e_movel_dn_abs(0, R_MARKERS + 16);
    e_movel_abs_dn(0xD00010, 0); e_movel_dn_abs(0, R_MARKERS + 20);

    /* Long read straddling the page 0 / page 1 bank boundary */
    e_movel_abs_dn(0x8FFFFE, 0);
    e_movel_dn_abs(0, R_STRADDLE);

    /* GD_ROMSetPage(2, 3): d0 = page<<16 | bank */
    e_movel_imm_dn(0, 0x00020003);
    e_jsr_a6(5 * 4);
    e_movel_abs_dn(0xA00010, 0);
    e_movel_dn_abs(0, R_SETPAGE);

    /* GD_ROMWriteEnable(1): write through page 1 (bank 15), read back */
    e_moveq(0, 1);
    e_jsr_a6(4 * 4);
    e_movew_imm_abs(0x1234, 0x900100);
    e_movew_abs_dn(0x900100, 0);
    e_andil_d0(0xFFFF);
    e_movel_dn_abs(0, R_WREN);
    /* GD_ROMWriteEnable(0): the same write must now miss */
    e_moveq(0, 0);
    e_jsr_a6(4 * 4);
    e_movew_imm_abs(0x4321, 0x900104);
    e_movew_abs_dn(0x900104, 0);
    e_andil_d0(0xFFFF);
    e_movel_dn_abs(0, R_WRDIS);

    e_movel_imm_abs(MAGIC_DONE, R_FLAG);
    e_bra_self();

    /* Run address -> main */
    rom[0x404] = (uint8_t)((CODE_ADDR + (l_main - 0x2000)) >> 24);
    rom[0x405] = (uint8_t)((CODE_ADDR + (l_main - 0x2000)) >> 16);
    rom[0x406] = (uint8_t)((CODE_ADDR + (l_main - 0x2000)) >> 8);
    rom[0x407] = (uint8_t)((CODE_ADDR + (l_main - 0x2000)) & 0xFF);
}

/* ================================================================
 * Host-side helpers
 * ================================================================ */

static uint32_t ram32(uint8_t *ram, uint32_t addr)
{
    return ((uint32_t)ram[addr] << 24) | ((uint32_t)ram[addr + 1] << 16)
         | ((uint32_t)ram[addr + 2] << 8) | ram[addr + 3];
}

static int write_rom_file(const char *path, const uint8_t *data, uint32_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    if (fwrite(data, 1, size, f) != size)
    {
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

/* One core lifecycle: load the image with the given option value and run.
 * Returns the harness config through *cfg (caller shuts it down). */
static int run_phase(harness_config *cfg, int argc, char **argv,
                     const char *rom_path, const char *jgd_value,
                     unsigned frames)
{
    harness_config fresh = HARNESS_CONFIG_DEFAULT;
    *cfg = fresh;
    cfg->frames = frames;
    cfg->quiet = 1;

    if (!harness_init_from_args(cfg, argc, argv))
        return 0;
    cfg->rom_path = rom_path;
    harness_set_option(cfg, "virtualjaguar_jgd", jgd_value);
    if (!harness_load_rom(cfg))
        return 0;
    harness_run(cfg);
    return 1;
}

int main(int argc, char **argv)
{
    char rom16_path[512], rom2_path[512];
    const char *tmpdir;
    uint8_t *rom16, *rom2;
    harness_config cfg;
    uint8_t *ram;
    uint8_t **ram_ptr;
    uint8_t *active_ptr, *wren_ptr, *page_ptr;
    int rc;

    tmpdir = getenv("TMPDIR");
    if (!tmpdir || !tmpdir[0])
        tmpdir = "/tmp";
    snprintf(rom16_path, sizeof(rom16_path), "%s/vj_jgd_probe16_%ld.j64",
             tmpdir, (long)getpid());
    snprintf(rom2_path, sizeof(rom2_path), "%s/vj_jgd_probe2_%ld.j64",
             tmpdir, (long)getpid());

    rom16 = (uint8_t *)malloc(16u << 20);
    rom2  = (uint8_t *)malloc(2u << 20);
    if (!rom16 || !rom2)
    {
        fprintf(stderr, "Out of memory building probe images\n");
        return 2;
    }
    build_probe_rom(rom16, 16u << 20);
    build_probe_rom(rom2, 2u << 20);
    if (!write_rom_file(rom16_path, rom16, 16u << 20)
        || !write_rom_file(rom2_path, rom2, 2u << 20))
    {
        fprintf(stderr, "Cannot write probe images under %s\n", tmpdir);
        free(rom16); free(rom2);
        return 2;
    }

    /* ============================================================
     * Phase A: option disabled -> GD detect must FAIL cleanly.
     * The probe's bounded GDWaitData must hit its bail-out (the real
     * bindings would spin in .waitAck forever = the stock-console
     * hang), and the core must still be running frames.
     * ============================================================ */
    if (!run_phase(&cfg, argc, argv, rom16_path, "disabled", 90))
    {
        fprintf(stderr, "Phase A: harness init/load failed\n");
        rc = 2;
        goto cleanup_files;
    }

    ram_ptr    = (uint8_t **)harness_dlsym(&cfg, "jaguarMainRAM");
    active_ptr = (uint8_t *)harness_dlsym(&cfg, "jgdActive");
    if (!ram_ptr || !active_ptr)
    {
        fprintf(stderr, "Cannot resolve jaguarMainRAM/jgdActive "
                        "(needs a TEST_EXPORTS=1 build)\n");
        harness_shutdown(&cfg);
        rc = 2;
        goto cleanup_files;
    }
    ram = *ram_ptr;

    check(*active_ptr == 0, "disabled_not_active",
          "jgdActive=%u with option disabled", *active_ptr);
    check(ram32(ram, R_FLAG) == MAGIC_FAILED
          && ram32(ram, R_ERR) == ERR_TIMEOUT,
          "disabled_detect_times_out",
          "flag=$%08X err=$%08X (expect $%08X/$%08X: the handshake never "
          "acked, so unbounded GD code would have wedged)",
          ram32(ram, R_FLAG), ram32(ram, R_ERR), MAGIC_FAILED, ERR_TIMEOUT);
    check(ram32(ram, R_HWVER) == 0, "disabled_no_hwversion",
          "R_HWVER=$%08X (expect 0: transaction never reached command 12)",
          ram32(ram, R_HWVER));
    check(cfg.video.total_frames_rendered >= 90, "disabled_core_not_hung",
          "%u frames rendered (bail-out kept the suite alive)",
          cfg.video.total_frames_rendered);
    harness_shutdown(&cfg);

    /* ============================================================
     * Phase B: auto + 16 MB image -> full install + banking.
     * ============================================================ */
    if (!run_phase(&cfg, argc, argv, rom16_path, "auto", 30))
    {
        fprintf(stderr, "Phase B: harness init/load failed\n");
        rc = 2;
        goto cleanup_files;
    }

    ram_ptr    = (uint8_t **)harness_dlsym(&cfg, "jaguarMainRAM");
    active_ptr = (uint8_t *)harness_dlsym(&cfg, "jgdActive");
    wren_ptr   = (uint8_t *)harness_dlsym(&cfg, "jgdWriteEnabled");
    page_ptr   = (uint8_t *)harness_dlsym(&cfg, "jgdPage");
    if (!ram_ptr || !active_ptr || !wren_ptr || !page_ptr)
    {
        fprintf(stderr, "Cannot resolve JGD internals\n");
        harness_shutdown(&cfg);
        rc = 2;
        goto cleanup_files;
    }
    ram = *ram_ptr;

    check(*active_ptr == 1, "auto_active_over_6mb",
          "jgdActive=%u for a 16 MB image in auto mode", *active_ptr);
    check(ram32(ram, R_FLAG) == MAGIC_DONE, "probe_completed",
          "flag=$%08X err=$%08X (expect $%08X)",
          ram32(ram, R_FLAG), ram32(ram, R_ERR), MAGIC_DONE);
    check(ram32(ram, R_HWVER) == EXPECT_HWVER, "hw_version",
          "cmd 12 -> $%08X (expect $%08X; FW word must be >= $0111)",
          ram32(ram, R_HWVER), EXPECT_HWVER);
    check(ram32(ram, R_BLOBSIZE) == EXPECT_BLOB_SIZE, "blob_size",
          "cmd $80 size prefix = %u (expect %u)",
          ram32(ram, R_BLOBSIZE), EXPECT_BLOB_SIZE);
    check(ram32(ram, R_BLOBHDR) == EXPECT_BLOB_HDR, "blob_header",
          "blob version/funccount = $%08X (expect $%08X)",
          ram32(ram, R_BLOBHDR), EXPECT_BLOB_HDR);
    check(ram32(ram, R_BIOSVER) == EXPECT_BIOSVER, "blob_biosversion",
          "GD_BIOSVersion() = $%08X (expect $%08X)",
          ram32(ram, R_BIOSVER), EXPECT_BIOSVER);

    check(ram32(ram, R_MARKERS + 0)  == 0xC0DE0000u
          && ram32(ram, R_MARKERS + 4)  == 0xC0DE000Fu
          && ram32(ram, R_MARKERS + 8)  == 0xC0DE000Eu
          && ram32(ram, R_MARKERS + 12) == 0xC0DE000Du
          && ram32(ram, R_MARKERS + 16) == 0xC0DE000Cu
          && ram32(ram, R_MARKERS + 20) == 0xC0DE000Bu,
          "banking_markers",
          "windows after ROMSetPages($BCDEF0): %08X %08X %08X %08X %08X %08X",
          ram32(ram, R_MARKERS + 0), ram32(ram, R_MARKERS + 4),
          ram32(ram, R_MARKERS + 8), ram32(ram, R_MARKERS + 12),
          ram32(ram, R_MARKERS + 16), ram32(ram, R_MARKERS + 20));
    check(ram32(ram, R_STRADDLE) == 0xA0A05FAFu, "boundary_straddle",
          "long at $8FFFFE = $%08X (expect $A0A05FAF: bank 0 tail + "
          "bank 15 head)", ram32(ram, R_STRADDLE));
    check(ram32(ram, R_SETPAGE) == 0xC0DE0003u, "single_page_remap",
          "window 2 after ROMSetPage(2,3) = $%08X (expect $C0DE0003)",
          ram32(ram, R_SETPAGE));
    check(ram32(ram, R_WREN) == 0x1234u, "rom_write_enable",
          "write-enabled cart write reads back $%04X (expect $1234)",
          ram32(ram, R_WREN));
    check(ram32(ram, R_WRDIS) == 0u, "rom_write_disable",
          "write-protected cart write reads back $%04X (expect $0000)",
          ram32(ram, R_WRDIS));
    check(page_ptr[0] == 0 && page_ptr[1] == 15 && page_ptr[2] == 3
          && page_ptr[3] == 13 && page_ptr[4] == 12 && page_ptr[5] == 11,
          "page_table_state",
          "jgdPage = {%u,%u,%u,%u,%u,%u} (expect {0,15,3,13,12,11})",
          page_ptr[0], page_ptr[1], page_ptr[2],
          page_ptr[3], page_ptr[4], page_ptr[5]);
    check(*wren_ptr == 0, "write_enable_left_clear",
          "jgdWriteEnabled=%u after the probe disabled it", *wren_ptr);

    /* ---- Savestate round-trip (still phase B's session) ---- */
    {
        jgd_state_save_fn jgd_save =
            (jgd_state_save_fn)harness_dlsym(&cfg, "JGDStateSave");
        serialize_size_fn ser_size =
            (serialize_size_fn)harness_dlsym(&cfg, "retro_serialize_size");
        serialize_fn ser = (serialize_fn)harness_dlsym(&cfg, "retro_serialize");
        unserialize_fn unser =
            (unserialize_fn)harness_dlsym(&cfg, "retro_unserialize");
        reset_fn do_reset = (reset_fn)harness_dlsym(&cfg, "retro_reset");
        read_long_fn jag_read_long =
            (read_long_fn)harness_dlsym(&cfg, "JaguarReadLong");
        uint8_t *state = NULL, *state7 = NULL;
        uint8_t chunk[1024];
        size_t ssize;

        if (!jgd_save || !ser_size || !ser || !unser || !do_reset
            || !jag_read_long)
        {
            fprintf(stderr, "Cannot resolve savestate symbols\n");
            harness_shutdown(&cfg);
            rc = 2;
            goto cleanup_files;
        }

        check(jgd_save(chunk) == EXPECT_JGD_CHUNK, "jgd_chunk_size",
              "JGDStateSave wrote %lu bytes (expect %d; active and "
              "inactive layouts must match)",
              (unsigned long)jgd_save(chunk), EXPECT_JGD_CHUNK);

        ssize = ser_size();
        state  = (uint8_t *)malloc(ssize);
        state7 = (uint8_t *)malloc(ssize);
        if (!state || !state7)
        {
            fprintf(stderr, "Out of memory for states\n");
            free(state); free(state7);
            harness_shutdown(&cfg);
            rc = 2;
            goto cleanup_files;
        }

        check(ser(state, ssize) != 0, "v8_serialize",
              "retro_serialize of a banked GameDrive session");
        {
            uint32_t hdr_ver;
            memcpy(&hdr_ver, state + STATE_OFF_VERSION, 4);
            /* The JGD chunk arrived in v8; the header must carry the
             * current version, whatever that has grown to since. */
            check(hdr_ver == (uint32_t)STATE_VERSION
                  && STATE_VERSION >= STATE_VERSION_JAGGD,
                  "header_is_current_version",
                  "state header version=%u (expect %u, >= %u for the JGD chunk)",
                  hdr_ver, (uint32_t)STATE_VERSION,
                  (uint32_t)STATE_VERSION_JAGGD);
        }

        /* Console reset returns the mapping to identity... */
        do_reset();
        check(page_ptr[0] == 0 && page_ptr[1] == 1 && page_ptr[2] == 2
              && page_ptr[3] == 3 && page_ptr[4] == 4 && page_ptr[5] == 5,
              "reset_returns_identity",
              "jgdPage after retro_reset = {%u,%u,%u,%u,%u,%u}",
              page_ptr[0], page_ptr[1], page_ptr[2],
              page_ptr[3], page_ptr[4], page_ptr[5]);

        /* ...and the state load restores the remap. */
        check(unser(state, ssize) != 0, "v8_unserialize", "state loads back");
        check(page_ptr[0] == 0 && page_ptr[1] == 15 && page_ptr[2] == 3
              && page_ptr[3] == 13 && page_ptr[4] == 12 && page_ptr[5] == 11,
              "v8_pages_restored",
              "jgdPage after load = {%u,%u,%u,%u,%u,%u} (expect {0,15,3,13,12,11})",
              page_ptr[0], page_ptr[1], page_ptr[2],
              page_ptr[3], page_ptr[4], page_ptr[5]);
        check(jag_read_long(0xA00010, 0) == 0xC0DE0003u,
              "v8_banked_read_after_load",
              "JaguarReadLong($A00010) = $%08X (expect $C0DE0003)",
              jag_read_long(0xA00010, 0));

        /* Pre-v8 contract: a v7-headered state (no JGD chunk) must load,
         * with the GD falling back to its reset mapping. */
        memcpy(state7, state, ssize);
        {
            uint32_t v7 = 7;
            memcpy(state7 + STATE_OFF_VERSION, &v7, 4);
        }
        check(unser(state7, ssize) != 0, "v7_state_loads",
              "v7-headered state accepted");
        check(page_ptr[0] == 0 && page_ptr[1] == 1 && page_ptr[2] == 2
              && page_ptr[3] == 3 && page_ptr[4] == 4 && page_ptr[5] == 5,
              "v7_jgd_defaults_to_reset",
              "jgdPage after v7 load = {%u,%u,%u,%u,%u,%u} (expect identity)",
              page_ptr[0], page_ptr[1], page_ptr[2],
              page_ptr[3], page_ptr[4], page_ptr[5]);

        free(state);
        free(state7);
    }
    harness_shutdown(&cfg);

    /* ============================================================
     * Phase C: forced enable + 2 MB image (the "GD-locked homebrew
     * smaller than the cart window" / ForceJGD path).
     * ============================================================ */
    if (!run_phase(&cfg, argc, argv, rom2_path, "enabled", 30))
    {
        fprintf(stderr, "Phase C: harness init/load failed\n");
        rc = 2;
        goto cleanup_files;
    }

    ram_ptr    = (uint8_t **)harness_dlsym(&cfg, "jaguarMainRAM");
    active_ptr = (uint8_t *)harness_dlsym(&cfg, "jgdActive");
    if (!ram_ptr || !active_ptr)
    {
        fprintf(stderr, "Cannot resolve internals (phase C)\n");
        harness_shutdown(&cfg);
        rc = 2;
        goto cleanup_files;
    }
    ram = *ram_ptr;

    check(*active_ptr == 1, "forced_active_small_image",
          "jgdActive=%u for a 2 MB image with option enabled", *active_ptr);
    check(ram32(ram, R_FLAG) == MAGIC_DONE, "forced_probe_completed",
          "flag=$%08X err=$%08X (expect $%08X)",
          ram32(ram, R_FLAG), ram32(ram, R_ERR), MAGIC_DONE);
    check(ram32(ram, R_HWVER) == EXPECT_HWVER, "forced_hw_version",
          "cmd 12 -> $%08X (expect $%08X)", ram32(ram, R_HWVER), EXPECT_HWVER);
    /* Banks 2-15 don't exist in a 2 MB image: their SDRAM reads zeros. */
    check(ram32(ram, R_MARKERS + 0) == 0xC0DE0000u
          && ram32(ram, R_MARKERS + 4) == 0
          && ram32(ram, R_MARKERS + 20) == 0,
          "forced_missing_banks_zero",
          "window 0 = $%08X, windows 1/5 = $%08X/$%08X (missing banks read 0)",
          ram32(ram, R_MARKERS + 0), ram32(ram, R_MARKERS + 4),
          ram32(ram, R_MARKERS + 20));
    check(ram32(ram, R_STRADDLE) == 0xA0A00000u, "forced_straddle",
          "long at $8FFFFE = $%08X (expect $A0A00000)",
          ram32(ram, R_STRADDLE));
    check(ram32(ram, R_WREN) == 0x1234u, "forced_write_enable",
          "write-enabled write into a missing bank reads back $%04X",
          ram32(ram, R_WREN));

    harness_report(&cfg, results, num_results);
    if (!cfg.json_output)
        printf("  %d passed, %d failed\n", pass_count, fail_count);
    harness_shutdown(&cfg);

    rc = fail_count > 0 ? 1 : 0;

cleanup_files:
    remove(rom16_path);
    remove(rom2_path);
    free(rom16);
    free(rom2);
    return rc;
}
