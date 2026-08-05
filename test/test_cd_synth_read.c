/*
 * test_cd_synth_read.c -- HLE CD_read delivery semantics, pinned against a
 * SYNTHETIC disc image and a SYNTHETIC 68K boot stub.
 *
 * Every other CD test in this tree needs test/roms/private (gitignored,
 * absent in CI and on a fresh clone), so the HLE CD read path -- the code
 * that actually moves disc bytes into Jaguar RAM -- had zero committed
 * regression coverage.  This test builds its own disc and its own boot ROM
 * at runtime, so it runs anywhere.
 *
 * The disc is a two-session CUE/BIN pair (same shape test_cd_pregap.c
 * synthesises, which the boot-stub extractor is known to accept):
 *
 *   track01  4 sectors, session 1, filler
 *   track02  session 2:
 *              sector 0..7  Atari universal boot header + 68K boot stub
 *              sector 8..39 PAYLOAD: a deterministic, position-derived byte
 *                           pattern in 1..251 (never $00, never $FC, never
 *                           $FF) so "delivered", "left untouched", "zeroed"
 *                           and "overwritten by the completion pad" are four
 *                           distinguishable outcomes.
 *
 * The boot stub is real 68K code:
 *     movea.l #dest,a0 / movea.l #end,a1 / move.l #msf,d0 / move.l #1,d1
 *     jsr $303C            ; CD_read, through the HLE jump table
 *     bra.s *
 *
 * D1 = 1 puts CD_read on its "D1 is a counter/ID" path: no sentinel scan,
 * raw stream from the requested LBA at offset 0.  That makes the source
 * position exactly determined by D0, which is what lets every byte be
 * predicted.  ($3072 bit 7 is left at the $FF the HLE boot writes, i.e.
 * match-ISR mode, so the non-match raw-alignment scan is not involved.)
 *
 * Invariants pinned (each has a documented negative control -- see the
 * per-test comments):
 *
 *   1. LBA addressing            -- a read at LBA L returns the bytes
 *                                   written at L in the image.
 *   2. Long-rounded tail         -- total = (count+3)&~3 while the game
 *                                   asked for `count`; the tail of the last
 *                                   longword must hold the NEXT REAL DISC
 *                                   BYTES, not zeros and not the $FF pad.
 *                                   (Iron Soldier 2 checksums through it.)
 *   3. Streamed, not instant     -- CD_read itself copies nothing; bytes
 *                                   arrive over many ticks; the far end of
 *                                   the buffer is still untouched after one
 *                                   tick.  (Instant delivery stomped
 *                                   in-flight poll code: Hover Strike.)
 *   4. Arm over in-flight        -- the previous transfer's tail is dropped
 *                                   and its completion side effects (pad +
 *                                   ATRI block) never fire.
 *   5. End-to-end through 68K    -- the same read driven by the synthetic
 *                                   boot stub via retro_run, not by poking
 *                                   the hook directly.
 *
 * Build: make test/test_cd_synth_read     (needs TEST_EXPORTS=1)
 * Run:   DYLD_LIBRARY_PATH=. test/test_cd_synth_read
 */

#include "cd_assertions.h"
#include "../libretro-common/include/libretro.h"

#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

static struct vj_core C;

/* ------------------------------------------------------------------ */
/* Core internals resolved by dlsym                                     */
/* ------------------------------------------------------------------ */

static bool     (*p_hle_hook)(uint32_t);
static void     (*p_hle_tick)(void);
static bool     (*p_hle_active)(void);
static uint32_t (*p_hle_dest)(void);
static uint32_t (*p_hle_bytes)(void);
static uint32_t (*p_hle_arms)(void);
static bool     (*p_cd_read_block)(uint32_t, uint8_t *);
static uint32_t (*p_cd_s2_first)(void);
static uint32_t (*p_cd_total)(void);
static bool     (*p_load_game)(const struct retro_game_info *);
static void     (*p_unload_game)(void);
static void     (*p_run)(void);

static uint8_t *ram;

/* ------------------------------------------------------------------ */
/* Synthetic disc geometry                                              */
/* ------------------------------------------------------------------ */

#define SECTOR_BYTES      2352u
#define S1_SECTORS        4u
#define PAYLOAD_SECTOR    8u      /* payload starts this many sectors into track 02 */
#define PAYLOAD_SECTORS   32u
#define T2_SECTORS        (PAYLOAD_SECTOR + PAYLOAD_SECTORS)
#define PAYLOAD_BASE_OFF  (PAYLOAD_SECTOR * SECTOR_BYTES)

/* Atari universal boot header (offsets as used by CDIntfExtractBootStub). */
#define HDR_MAGIC_OFF     0x42u
#define HDR_LOAD_OFF      0x62u
#define HDR_LEN_OFF       0x66u
#define HDR_PAYLOAD       0x6Au
#define STUB_LOAD_ADDR    0x004000u
#define STUB_LENGTH       0x100u

static const char BOOT_MAGIC[32] = "ATARI APPROVED DATA HEADER ATRI ";

/* Destination buffers in main RAM.  Clear of the 68K vector table, the
 * $400 RTE stub, the TOC at $2C00, the jump table at $3000, the injected
 * boot stub at $4000, the CD-ready flag at $3727C and SP at $200000. */
#define DEST_A            0x100000u
#define DEST_B            0x140000u

/* Filler written into the destination before every read.  Outside the
 * payload pattern's 1..251 range, and distinct from both $00 (a zeroing
 * bug) and $FF (the completion pad). */
#define FILLER            0xFCu

/* The stub's own read, for the end-to-end test: deliberately not a
 * multiple of 4 so it doubles as a tail check. */
#define STUB_READ_BYTES   0x1005u

/* ------------------------------------------------------------------ */
/* Deterministic payload pattern                                        */
/* ------------------------------------------------------------------ */

/* Raw byte stored at track-02 file offset `off`.  Always in 1..251. */
static uint8_t disc_pat(uint32_t off)
{
    return (uint8_t)(1u + ((off * 37u + (off >> 8) * 11u) % 251u));
}

/* Byte the streamer must land at dest + i for a read that starts at the
 * payload LBA, offset 0.
 *
 * The streamer un-does the I2S word swap: within each 2352-byte sector it
 * exchanges byte pairs (0,1), (2,3), ...  PAYLOAD_BASE_OFF and SECTOR_BYTES
 * are both even, so sector-local parity is global parity and the swap is
 * just `^1` on the file offset. */
static uint8_t expect_ram(uint32_t i)
{
    return disc_pat(PAYLOAD_BASE_OFF + (i ^ 1u));
}

/* ------------------------------------------------------------------ */
/* Byte helpers                                                         */
/* ------------------------------------------------------------------ */

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* LBA -> the packed MSF CD_read expects in D0: (min<<16)|(sec<<8)|frm,
 * with the 150-sector lead-in added back (CD_read subtracts it). */
static uint32_t lba_to_msf(uint32_t lba)
{
    uint32_t tf  = lba + 150u;
    uint32_t min = tf / 4500u;
    uint32_t sec = (tf / 75u) % 60u;
    uint32_t frm = tf % 75u;
    return (min << 16) | (sec << 8) | frm;
}

/* ------------------------------------------------------------------ */
/* Synthetic ROM: the 68K boot stub                                     */
/* ------------------------------------------------------------------ */

/* Emits, at `out` (STUB_LENGTH bytes, logical/unswapped order):
 *   207C dddddddd  movea.l #dest,a0
 *   227C eeeeeeee  movea.l #dest+size,a1
 *   203C mmmmmmmm  move.l  #msf,d0
 *   223C 00000001  move.l  #1,d1
 *   4EB9 0000303C  jsr     $0000303C        ; CD_read
 *   60FE           bra.s   *                ; park
 * with the remainder filled with `bra.s *` so any stray entry parks too. */
static void build_stub(uint8_t *out, uint32_t msf, uint32_t dest, uint32_t size)
{
    uint32_t o = 0;

    put16(out + o, 0x207C); put32(out + o + 2, dest);         o += 6;
    put16(out + o, 0x227C); put32(out + o + 2, dest + size);  o += 6;
    put16(out + o, 0x203C); put32(out + o + 2, msf);          o += 6;
    put16(out + o, 0x223C); put32(out + o + 2, 0x00000001u);  o += 6;
    put16(out + o, 0x4EB9); put32(out + o + 2, 0x0000303Cu);  o += 6;

    while (o + 1u < STUB_LENGTH)
    {
        put16(out + o, 0x60FE);
        o += 2;
    }
}

/* ------------------------------------------------------------------ */
/* Synthetic disc construction                                          */
/* ------------------------------------------------------------------ */

static bool write_file(const char *path, const uint8_t *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    size_t n;

    if (!f)
        return false;
    n = len ? fwrite(data, 1, len, f) : 0;
    fclose(f);
    return n == len;
}

/* Writes <dir>/disc.cue + track01.bin + track02.bin.
 *
 * `stubMsf` is baked into the boot stub's `move.l #msf,d0`.  The payload
 * LBA is only known after the image loader has told us where session 2
 * starts, so the disc is built twice: once with a placeholder, then again
 * with the real value.  Track sizes never change, so the geometry the
 * loader reports is identical for both builds. */
static bool make_disc(const char *dir, uint32_t stubMsf,
                      char *cueOut, size_t cueOutLen)
{
    char path[1024];
    uint8_t *t1 = NULL;
    uint8_t *t2 = NULL;
    uint8_t *logical = NULL;
    size_t t1Len = (size_t)S1_SECTORS * SECTOR_BYTES;
    size_t t2Len = (size_t)T2_SECTORS * SECTOR_BYTES;
    size_t i;
    FILE *cue;
    bool ok = false;

    t1      = (uint8_t *)calloc(1, t1Len);
    t2      = (uint8_t *)calloc(1, t2Len);
    logical = (uint8_t *)calloc(1, PAYLOAD_BASE_OFF);
    if (!t1 || !t2 || !logical)
        goto done;

    /* Header + stub, built in logical order then I2S-swapped onto the disc. */
    memcpy(logical + HDR_MAGIC_OFF, BOOT_MAGIC, sizeof(BOOT_MAGIC));
    put32(logical + HDR_LOAD_OFF, STUB_LOAD_ADDR);
    put32(logical + HDR_LEN_OFF,  STUB_LENGTH);
    build_stub(logical + HDR_PAYLOAD, stubMsf, DEST_A, STUB_READ_BYTES);

    for (i = 0; i + 1 < PAYLOAD_BASE_OFF; i += 2)
    {
        t2[i]     = logical[i + 1];
        t2[i + 1] = logical[i];
    }

    /* Payload region: raw disc bytes, straight from the pattern. */
    for (i = PAYLOAD_BASE_OFF; i < t2Len; i++)
        t2[i] = disc_pat((uint32_t)i);

    snprintf(path, sizeof(path), "%s/track01.bin", dir);
    if (!write_file(path, t1, t1Len))
        goto done;
    snprintf(path, sizeof(path), "%s/track02.bin", dir);
    if (!write_file(path, t2, t2Len))
        goto done;

    snprintf(cueOut, cueOutLen, "%s/disc.cue", dir);
    cue = fopen(cueOut, "wb");
    if (!cue)
        goto done;
    fprintf(cue,
            "REM SESSION 01\n"
            "FILE \"track01.bin\" BINARY\n"
            "  TRACK 01 AUDIO\n"
            "    INDEX 01 00:00:00\n"
            "REM SESSION 02\n"
            "FILE \"track02.bin\" BINARY\n"
            "  TRACK 02 AUDIO\n"
            "    INDEX 01 00:00:00\n");
    fclose(cue);
    ok = true;

done:
    free(t1);
    free(t2);
    free(logical);
    return ok;
}

/* ------------------------------------------------------------------ */
/* libretro plumbing                                                    */
/* ------------------------------------------------------------------ */

static bool env_cb(unsigned cmd, void *data)
{
    if (cmd == RETRO_ENVIRONMENT_GET_VARIABLE)
    {
        struct retro_variable *var = (struct retro_variable *)data;
        if (strcmp(var->key, "virtualjaguar_cd_boot_mode") == 0)
        {
            var->value = "hle";
            return true;
        }
        /* Everything else defaults -- notably virtualjaguar_cd_read_speed,
         * whose default is CDSPEED_2X (paced).  CDSPEED_INSTANT would make
         * the streaming test vacuous. */
        return false;
    }
    if (cmd == RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY)
    {
        *(const char **)data = ".";
        return true;
    }
    if (cmd == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT)
        return true;
    return false;
}

static void vcb(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static void acb(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t abcb(const int16_t *d, size_t f) { (void)d; return f; }
static void ipcb(void) {}
static int16_t iscb(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }

/* Remove the scratch disc.  Called on every exit path -- an aborted run
 * must not leave ~94 KB of synthetic BIN behind. */
static void scrub_scratch(const char *base)
{
    char path[1024];

    snprintf(path, sizeof(path), "%s/track01.bin", base); remove(path);
    snprintf(path, sizeof(path), "%s/track02.bin", base); remove(path);
    snprintf(path, sizeof(path), "%s/disc.cue",    base); remove(path);
    rmdir(base);
}

static bool load_disc(const char *cue)
{
    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    info.path = cue;
    return p_load_game(&info);
}

/* ------------------------------------------------------------------ */
/* Test helpers                                                         */
/* ------------------------------------------------------------------ */

static uint32_t payloadLBA;   /* resolved after the first load */
static uint32_t s2FirstLBA;
static uint32_t discSectors;

/* Arm a CD_read exactly as the 68K entry does: registers, then the HLE
 * jump-table hook at $303C. */
static void arm_read(uint32_t lba, uint32_t dest, uint32_t bytes)
{
    C.m68k_set_reg(M68K_REG_D0, lba_to_msf(lba));
    C.m68k_set_reg(M68K_REG_D1, 0x00000001u);
    C.m68k_set_reg(M68K_REG_A0, dest);
    C.m68k_set_reg(M68K_REG_A1, dest + bytes);
    p_hle_hook(0x0000303Cu);
}

static void fill_dest(uint32_t dest, uint32_t len)
{
    memset(ram + dest, FILLER, len);
}

static uint32_t drive_stream(uint32_t maxTicks)
{
    uint32_t n = 0;
    while (p_hle_active() && n < maxTicks)
    {
        p_hle_tick();
        n++;
    }
    return n;
}

/* Index of the first byte in [0,len) that differs from expect_ram(), or
 * len when the whole range matches. */
static uint32_t first_bad(uint32_t dest, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++)
        if (ram[dest + i] != expect_ram(i))
            return i;
    return len;
}

/* HLEStreamFinish's end-of-transfer marker: 8 bytes of $FF immediately
 * overwritten by 16 "ATRI" longwords, both starting at dest + total.  Its
 * position is the observable that says where the transfer was considered to
 * end -- at the long-rounded end, never at the requested end. */
static bool is_atri(uint32_t addr)
{
    return ram[addr + 0] == 0x41u && ram[addr + 1] == 0x54u &&
           ram[addr + 2] == 0x52u && ram[addr + 3] == 0x49u;
}

static bool range_is(uint32_t addr, uint32_t len, uint8_t v)
{
    uint32_t i;
    for (i = 0; i < len; i++)
        if (ram[addr + i] != v)
            return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* 1. LBA addressing / image model                                      */
/* ------------------------------------------------------------------ */

/* Pins that a given absolute LBA hands back exactly the bytes written at
 * that position in the synthetic image, and establishes the preconditions
 * every later test relies on (payload inside the session-2 range, so
 * CD_read's out-of-range redirect never fires, and inside the disc, so
 * CDIntfReadBlock never fails into a zero-filled buffer).
 *
 * Negative control: `hleStream.lba = scanLBA + 1` in HLEHandleCDRead makes
 * test 2 fail at offset 0 -- this test localises whether such a failure is
 * the streamer or the image loader. */
TEST(synth_disc_lba_addressing)
{
    static uint8_t buf[SECTOR_BYTES];
    uint32_t k;
    uint32_t s;

    ASSERT_TRUE(s2FirstLBA > 0);
    ASSERT_TRUE(discSectors > 0);
    /* Payload must sit strictly inside [s2first, discTotal): outside it,
     * CD_read redirects to the session-2 game-data LBA and reads something
     * else entirely. */
    ASSERT_TRUE(payloadLBA >= s2FirstLBA);
    ASSERT_TRUE(payloadLBA + PAYLOAD_SECTORS <= discSectors);

    for (s = 0; s < 4; s++)
    {
        ASSERT_TRUE(p_cd_read_block(payloadLBA + s, buf));
        for (k = 0; k < SECTOR_BYTES; k++)
        {
            if (buf[k] != disc_pat(PAYLOAD_BASE_OFF + s * SECTOR_BYTES + k))
                FAIL("LBA %u byte %u: got $%02X, expected $%02X",
                     payloadLBA + s, k, buf[k],
                     disc_pat(PAYLOAD_BASE_OFF + s * SECTOR_BYTES + k));
        }
    }
}

/* ------------------------------------------------------------------ */
/* 2. Long-rounded tail delivery                                        */
/* ------------------------------------------------------------------ */

/* hleStream.total = (count+3)&~3 while reqTotal = count.  The bytes in
 * [count, total) are NOT padding: the real GPU CD ISR stores whole
 * longwords, so those positions hold the next real bytes off the disc.
 * Iron Soldier 2's boot stub checksums its $14FE-byte load in ADD.L steps
 * and sums 2 bytes past its own end address; delivering exact-size (and
 * therefore putting the $FF pad at the odd end) made every validation fail
 * and the stub re-issue the read forever.
 *
 * Negative control (verified): change
 *     hleStream.total = (byteCount + 3u) & ~3u;
 * to
 *     hleStream.total = byteCount;
 * -- the tail then reads $FF (the completion pad now starts at reqTotal)
 * and this test goes red on every unaligned size. */
TEST(long_rounded_tail_holds_real_disc_bytes)
{
    static const uint32_t sizes[7] = { 1u, 2u, 3u, 5u, 7u, 4093u, 4096u };
    uint32_t n;

    for (n = 0; n < 7; n++)
    {
        uint32_t size  = sizes[n];
        uint32_t total = (size + 3u) & ~3u;
        uint32_t bad;
        uint32_t i;

        /* Filler across the request, its long-rounded tail, and the pad +
         * ATRI region beyond, so nothing is inherited from the last pass. */
        fill_dest(DEST_A, total + 128u);

        arm_read(payloadLBA, DEST_A, size);
        ASSERT_TRUE(p_hle_active());
        drive_stream(200000u);
        if (p_hle_active())
            FAIL("size %u: stream never completed", size);

        bad = first_bad(DEST_A, total);
        if (bad != total)
            FAIL("size %u: byte %u of %u: got $%02X, expected $%02X",
                 size, bad, total, ram[DEST_A + bad], expect_ram(bad));

        /* Restate the tail explicitly -- this is the load-bearing part and
         * it must not be silently covered by the loop above. */
        for (i = size; i < total; i++)
        {
            if (ram[DEST_A + i] == 0xFF)
                FAIL("size %u: tail byte %u is the $FF completion pad, "
                     "not disc data", size, i);
            if (ram[DEST_A + i] == 0x00)
                FAIL("size %u: tail byte %u is zero, not disc data", size, i);
            if (ram[DEST_A + i] == FILLER)
                FAIL("size %u: tail byte %u was never written", size, i);
            ASSERT_EQ_U8(ram[DEST_A + i], expect_ram(i));
        }

        /* The wire count itself is long-rounded. */
        ASSERT_EQ_U32(p_hle_bytes(), total);

        /* And the end-of-transfer marker starts exactly AT the long-rounded
         * end, so it can never eat a delivered byte.  (Delivering exact-size
         * put it at the odd end address and clobbered the checksummed tail.) */
        if (!is_atri(DEST_A + total))
            FAIL("size %u: no end marker at the long-rounded end $%06X "
                 "(got $%02X%02X%02X%02X)", size, DEST_A + total,
                 ram[DEST_A + total], ram[DEST_A + total + 1],
                 ram[DEST_A + total + 2], ram[DEST_A + total + 3]);
    }
}

/* ------------------------------------------------------------------ */
/* 3. Streamed, not instant                                            */
/* ------------------------------------------------------------------ */

/* Games issue overlay loads whose destination covers the very code that
 * polls for completion (Hover Strike's LVL loads span $05D340-$1F0000 with
 * its poll loop at $1B4xxx).  On hardware the drive streams at 352,800 B/s,
 * giving the game time to jump away.  An instant CD_read stomps the running
 * poll loop and the 68K double-faults.
 *
 * Deliberately no predicted per-tick byte count (that depends on the
 * halfline period and the NTSC/PAL flag): the assertions are two-sided and
 * model-free -- the front of the buffer has moved, the far end has not.
 *
 * Negative control (verified): force `hleStream.speedMult = CDSPEED_INSTANT`
 * at arm time -- the whole transfer lands in the first tick, the far byte is
 * no longer filler, and this test goes red. */
TEST(delivery_is_streamed_not_instant)
{
    uint32_t size  = 0x8000u;
    uint32_t ticks;
    uint32_t bad;

    fill_dest(DEST_A, size + 128u);
    arm_read(payloadLBA, DEST_A, size);

    /* CD_read itself must copy nothing -- it only arms. */
    ASSERT_TRUE(p_hle_active());
    ASSERT_EQ_U32(p_hle_dest(), DEST_A);
    if (!range_is(DEST_A, size, FILLER))
        FAIL("CD_read wrote to the destination before any tick");

    /* One tick: the front has advanced, the far end has not. */
    p_hle_tick();
    ASSERT_EQ_U8(ram[DEST_A], expect_ram(0));
    ASSERT_EQ_U8(ram[DEST_A + size - 1u], FILLER);
    ASSERT_TRUE(p_hle_active());

    /* And it takes many more ticks to finish -- a single-tick transfer is
     * exactly the instant delivery this pins against. */
    ticks = 1u + drive_stream(200000u);
    if (p_hle_active())
        FAIL("stream never completed (%u ticks)", ticks);
    if (ticks < 100u)
        FAIL("%u-byte transfer completed in %u ticks -- not drive-paced",
             size, ticks);

    bad = first_bad(DEST_A, size);
    if (bad != size)
        FAIL("byte %u: got $%02X, expected $%02X",
             bad, ram[DEST_A + bad], expect_ram(bad));
}

/* ------------------------------------------------------------------ */
/* 4. Arming over an in-flight stream                                   */
/* ------------------------------------------------------------------ */

/* A CD_read armed while another is still streaming abandons the first one:
 * its undelivered tail never arrives and -- the part that matters for
 * anything downstream -- HLEStreamFinish never runs for it, so neither the
 * $FF pad nor the ATRI sync block that overwrites it appears at its end.  A regression that quietly
 * "finished" the abandoned transfer would publish a completion the game
 * never earned.
 *
 * The second read differs in A0/A1, so it does not take CD_read's
 * identical-re-issue path (which deliberately keeps the in-flight transfer).
 *
 * Negative control (verified): call HLEStreamFinish() just before re-arming
 * in HLEHandleCDRead -- the pad + ATRI block appear at dest_A + total_A and
 * this test goes red. */
TEST(arming_over_inflight_drops_tail_and_completion)
{
    uint32_t sizeA  = 0x8000u;
    uint32_t sizeB  = 0x400u;
    uint32_t totalA = (sizeA + 3u) & ~3u;
    uint32_t totalB = (sizeB + 3u) & ~3u;
    uint32_t arms0  = p_hle_arms();
    uint32_t i;
    uint32_t bad;
    uint32_t delivered = 0;

    fill_dest(DEST_A, sizeA + 128u);
    fill_dest(DEST_B, sizeB + 128u);

    arm_read(payloadLBA, DEST_A, sizeA);
    for (i = 0; i < 5u; i++)
        p_hle_tick();
    ASSERT_TRUE(p_hle_active());

    /* Partially delivered: some bytes in, most still filler. */
    for (i = 0; i < totalA; i++)
    {
        if (ram[DEST_A + i] == FILLER)
            break;
        delivered++;
    }
    if (delivered == 0)
        FAIL("stream A delivered nothing in 5 ticks");
    if (delivered >= totalA)
        FAIL("stream A already complete after 5 ticks -- cannot test abandon");

    /* Arm B over it. */
    arm_read(payloadLBA, DEST_B, sizeB);
    ASSERT_EQ_U32(p_hle_arms() - arms0, 2u);
    ASSERT_EQ_U32(p_hle_dest(), DEST_B);
    ASSERT_EQ_U32(p_hle_bytes(), totalB);

    /* A's tail was dropped: the last byte it would have delivered is still
     * filler, and no completion side effects ran at its end. */
    ASSERT_EQ_U8(ram[DEST_A + totalA - 1u], FILLER);
    if (!range_is(DEST_A + totalA, 8u, FILLER))
        FAIL("abandoned transfer A published completion side effects "
             "($FF pad / ATRI block) at $%06X", DEST_A + totalA);

    /* B completes normally and is byte-exact. */
    drive_stream(200000u);
    if (p_hle_active())
        FAIL("stream B never completed");
    bad = first_bad(DEST_B, totalB);
    if (bad != totalB)
        FAIL("B byte %u: got $%02X, expected $%02X",
             bad, ram[DEST_B + bad], expect_ram(bad));
    if (!is_atri(DEST_B + totalB))
        FAIL("stream B did not publish its end marker at $%06X",
             DEST_B + totalB);

    /* A's abandoned region stayed abandoned while B ran. */
    ASSERT_EQ_U8(ram[DEST_A + totalA - 1u], FILLER);
}

/* ------------------------------------------------------------------ */
/* 5. End-to-end through the synthetic ROM                              */
/* ------------------------------------------------------------------ */

/* Everything above drives JaguarCDHLEHook() directly.  This one proves the
 * whole chain is wired: the boot-stub extractor pulls our 68K program off
 * the synthetic disc, M68KInstructionHook dispatches its `jsr $303C` into
 * the HLE CD_read, and HalflineCallback's per-halfline StreamTick delivers
 * the bytes across retro_run() frames.
 *
 * The stub's request is $1005 bytes -- not a multiple of 4 -- so the tail
 * invariant is re-checked end-to-end.
 *
 * Negative control (verified): the same `hleStream.total = byteCount`
 * sabotage as test 2 turns this red too. */
TEST(stub_driven_read_end_to_end)
{
    uint32_t total = (STUB_READ_BYTES + 3u) & ~3u;
    uint32_t arms0;
    uint32_t f;
    uint32_t bad;
    uint32_t i;

    /* Boot stub injected and entered at its load address. */
    ASSERT_EQ_U32(C.m68k_get_reg(NULL, M68K_REG_PC), STUB_LOAD_ADDR);
    /* Our program, not somebody's zero fill: movea.l #DEST_A,a0. */
    ASSERT_EQ_U8(ram[STUB_LOAD_ADDR + 0], 0x20);
    ASSERT_EQ_U8(ram[STUB_LOAD_ADDR + 1], 0x7C);

    arms0 = p_hle_arms();
    fill_dest(DEST_A, total + 128u);

    for (f = 0; f < 30u; f++)
        p_run();

    if (p_hle_arms() - arms0 != 1u)
        FAIL("stub issued %u CD_reads in 30 frames, expected 1",
             p_hle_arms() - arms0);
    if (p_hle_active())
        FAIL("stub's read never completed in 30 frames");

    bad = first_bad(DEST_A, total);
    if (bad != total)
        FAIL("byte %u of %u: got $%02X, expected $%02X",
             bad, total, ram[DEST_A + bad], expect_ram(bad));

    for (i = STUB_READ_BYTES; i < total; i++)
    {
        if (ram[DEST_A + i] == 0xFF || ram[DEST_A + i] == FILLER)
            FAIL("tail byte %u not delivered from disc (got $%02X)",
                 i, ram[DEST_A + i]);
        ASSERT_EQ_U8(ram[DEST_A + i], expect_ram(i));
    }
}

/* ------------------------------------------------------------------ */

static bool resolve_symbols(void)
{
    p_hle_hook      = (bool (*)(uint32_t))dlsym(C.handle, "JaguarCDHLEHook");
    p_hle_tick      = (void (*)(void))dlsym(C.handle, "JaguarCDHLEStreamTick");
    p_hle_active    = (bool (*)(void))dlsym(C.handle, "JaguarCDHLEStreamActive");
    p_hle_dest      = (uint32_t (*)(void))dlsym(C.handle, "JaguarCDHLEStreamDest");
    p_hle_bytes     = (uint32_t (*)(void))dlsym(C.handle, "JaguarCDHLEStreamBytes");
    p_hle_arms      = (uint32_t (*)(void))dlsym(C.handle, "JaguarCDHLEStreamArmCount");
    p_cd_read_block = (bool (*)(uint32_t, uint8_t *))dlsym(C.handle, "CDIntfReadBlock");
    p_cd_s2_first   = (uint32_t (*)(void))dlsym(C.handle, "CDIntfGetSession2FirstTrackLBA");
    p_cd_total      = (uint32_t (*)(void))dlsym(C.handle, "CDIntfGetDiscTotalSectors");
    p_load_game     = (bool (*)(const struct retro_game_info *))dlsym(C.handle, "retro_load_game");
    p_unload_game   = (void (*)(void))dlsym(C.handle, "retro_unload_game");
    p_run           = (void (*)(void))dlsym(C.handle, "retro_run");

    return p_hle_hook && p_hle_tick && p_hle_active && p_hle_dest &&
           p_hle_bytes && p_hle_arms && p_cd_read_block && p_cd_s2_first &&
           p_cd_total && p_load_game && p_unload_game && p_run &&
           C.m68k_set_reg && C.m68k_get_reg && C.GetRamPtr;
}

int main(int argc, char *argv[])
{
    char base[512];
    char cue[1024];
    int rc;

    (void)argc; (void)argv;

    TEST_INIT("CD HLE read semantics (synthetic disc + ROM)");

    if (!vj_core_load(&C))
    {
        fprintf(stderr, "FATAL: failed to load core\n");
        return 1;
    }

    C.retro_set_environment(env_cb);
    C.retro_set_video_refresh(vcb);
    C.retro_set_audio_sample(acb);
    C.retro_set_audio_sample_batch(abcb);
    C.retro_set_input_poll(ipcb);
    C.retro_set_input_state(iscb);
    C.retro_init();

    if (!resolve_symbols())
    {
        fprintf(stderr, "  FAIL  missing test exports "
                        "(build with `make TEST_EXPORTS=1`)\n");
        dlclose(C.handle);
        return 1;
    }
    ram = C.GetRamPtr();
    if (!ram)
    {
        fprintf(stderr, "  FAIL  GetRamPtr() returned NULL\n");
        dlclose(C.handle);
        return 1;
    }

    /* Deterministic per-process scratch dir (mkdtemp sits behind different
     * feature-test macros on Darwin vs glibc, and per-process is unique
     * enough).  Everything below is synthetic -- no test/roms/private. */
    snprintf(base, sizeof(base), "/tmp/vj_cd_synth_%ld", (long)getpid());
    if (mkdir(base, 0755) != 0 && errno != EEXIST)
    {
        fprintf(stderr, "  SKIP  cannot create scratch dir %s\n", base);
        C.retro_deinit();
        dlclose(C.handle);
        return 0;
    }

    /* Pass 1: placeholder MSF, just to learn the image geometry. */
    if (!make_disc(base, 0, cue, sizeof(cue)))
    {
        fprintf(stderr, "  SKIP  cannot write synthetic disc under %s\n", base);
        scrub_scratch(base);
        C.retro_deinit();
        dlclose(C.handle);
        return 0;
    }
    if (!load_disc(cue))
    {
        fprintf(stderr, "  FAIL  synthetic disc did not load\n");
        scrub_scratch(base);
        C.retro_deinit();
        dlclose(C.handle);
        return 1;
    }
    s2FirstLBA  = p_cd_s2_first();
    discSectors = p_cd_total();
    payloadLBA  = s2FirstLBA + PAYLOAD_SECTOR;
    p_unload_game();

    /* Pass 2: same geometry, real MSF baked into the stub. */
    if (!make_disc(base, lba_to_msf(payloadLBA), cue, sizeof(cue)))
    {
        fprintf(stderr, "  FAIL  cannot rewrite synthetic disc\n");
        scrub_scratch(base);
        C.retro_deinit();
        dlclose(C.handle);
        return 1;
    }
    if (!load_disc(cue))
    {
        fprintf(stderr, "  FAIL  synthetic disc did not reload\n");
        scrub_scratch(base);
        C.retro_deinit();
        dlclose(C.handle);
        return 1;
    }
    ram = C.GetRamPtr();   /* re-latch: a reload may re-seat main RAM */

    fprintf(stderr, "synthetic disc: session2 LBA %u, %u sectors, "
                    "payload LBA %u\n", s2FirstLBA, discSectors, payloadLBA);

    RUN_TEST(synth_disc_lba_addressing);
    RUN_TEST(long_rounded_tail_holds_real_disc_bytes);
    RUN_TEST(delivery_is_streamed_not_instant);
    RUN_TEST(arming_over_inflight_drops_tail_and_completion);

    /* Fresh boot for the end-to-end run so the 68K starts at the stub. */
    p_unload_game();
    if (!load_disc(cue))
    {
        fprintf(stderr, "  FAIL  synthetic disc did not reload for e2e\n");
        rc = 1;
        goto out;
    }
    ram = C.GetRamPtr();
    RUN_TEST(stub_driven_read_end_to_end);

    rc = TEST_REPORT();
out:
    p_unload_game();
    C.retro_deinit();
    dlclose(C.handle);

    scrub_scratch(base);

    return rc;
}
