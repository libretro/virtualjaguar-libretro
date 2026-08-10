/*
 * test/test_state_compat.c — Save-state version-gate regression test.
 *
 * Guards the compatibility contract in retro_unserialize():
 *
 *   1. A state written with an OLDER layout (>= STATE_MIN_VERSION) must
 *      still load, and the core must still run afterwards.  f334d81 added
 *      DAC i2sNonZeroCount and bumped STATE_VERSION 2 -> 3 while
 *      retro_unserialize still demanded an exact version match, which
 *      silently invalidated every state written by v2.3.0 / v2.3.1.
 *      Every version a RELEASED core ever wrote is covered: 1 (v2.2.0),
 *      2 (v2.3.0 / v2.3.1), 3 (v2.3.2) and the current 7 (v3.0.0).
 *      Issue #268.
 *   2. A state OLDER than STATE_MIN_VERSION (v0) must be refused.
 *   3. A state NEWER than STATE_VERSION must be refused — we cannot know
 *      what fields it carries.
 *   4. A bad magic must be refused.
 *   5. A same-version round trip must succeed (positive control, so the
 *      rejections above can't pass vacuously via a core that refuses
 *      everything).
 *
 * The fix has two coupled halves and they need separate assertions:
 *
 *   - libretro.c widens the gate to STATE_MIN_VERSION <= v <= STATE_VERSION.
 *     Covered by "v2_state_loads".
 *   - dac.c only consumes i2sNonZeroCount when the header version is
 *     >= STATE_VERSION_DAC_I2S_NONZEROCOUNT, so the fields after it stay
 *     aligned.  Covered ONLY by "v2_dac_block_realigned".  DAC is the last
 *     module in the load sequence, so an unconditional consume still
 *     returns true from retro_unserialize and still leaves a live core —
 *     neither the load result nor the framebuffer check can detect it.
 *
 * Locating the DAC block without hardcoding the whole state layout: the
 * test dlsym's DACStateSave (exposed by the TEST_EXPORTS symbol lists),
 * calls it into a scratch buffer to get the exact v3 DAC bytes, then finds
 * that byte pattern inside the freshly serialized state.  Two guards make
 * the match trustworthy: the pattern must occur exactly once, and — since
 * DAC is serialized last and retro_serialize zero-fills the tail —
 * everything after the block must be zero.  No 2.4 MB binary fixture is
 * committed; the v1/v2/v3 states are synthesized at runtime from a current
 * one by synth_legacy_state().
 *
 * That synthesis has to rebuild the CDROM chunk, not just trim fields off
 * the end of the state.  The CD-support work replaced the old block's two
 * cdBuf2/cdBuf3 staging buffers with the BUTCH/FIFO/DSA/SSI working set,
 * shrinking the chunk by 2627 bytes, and only the last 28 of those bytes
 * were version-gated.  A fixture built by trimming alone is not a v1/v2/v3
 * state, and a test built on one reports PASS while every released state
 * silently mis-parses from the CDROM chunk onward.
 *
 * Exit codes: 0 = pass, 1 = fail, 2 = harness error, 77 = skip (ROM absent)
 *
 * Build:  cc -O2 -Wall -std=c99 $(INCFLAGS) -o test/test_state_compat \
 *           test/test_state_compat.c test/harness/harness.c \
 *           $(if $(filter Linux,$(shell uname -s)),-ldl) -lm
 *
 * Usage:  ./test/test_state_compat [core.dylib] [rom.j64]
 *         Default ROM: test/roms/yarc.j64 (committed in-tree)
 */

#include "harness/harness.h"
#include "state.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Suppress LeakSanitizer for the known-benign ROM buffer leak. */
#if defined(__SANITIZE_ADDRESS__)
#define SC_TEST_HAS_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SC_TEST_HAS_ASAN 1
#endif
#endif
#ifdef SC_TEST_HAS_ASAN
const char *__lsan_default_suppressions(void) {
    return "leak:harness_load_rom\n";
}
#endif

#define MAX_RESULTS 24
#define DEFAULT_ROM "test/roms/yarc.j64"
#define DEFAULT_FRAMES 120
/* Frames run between capturing the v3 state and loading the v2 fixture,
 * and again after the v2 load to prove the core is still alive. */
#define DRIFT_FRAMES 60

/* Fraction of pixels that must be non-black after loading the v2 state and
 * running DRIFT_FRAMES more frames.  yarc.j64 measures ~78% here.  Note this
 * is the WEAKER of the two post-load liveness checks: yarc's screen is
 * near-static, so the count is bit-identical whether the load succeeded or
 * was refused.  "core_advances_after_v2_load" is the one with teeth. */
#define MIN_NONBLACK_FRACTION 0.01

/* Layout of the DAC state block, verified against DACStateSave() in
 * src/jerry/dac.c: bufferIndex (int, 4), numberOfSamples (int, 4),
 * bufferDone (bool, 1), i2sWritePos (uint32, 4), i2sWriteCount (uint32, 4),
 * i2sNonZeroCount (uint32, 4), i2sPhase (double, 8), i2sRateRatio
 * (double, 8) = 37 bytes, with i2sNonZeroCount at offset 17.
 *
 * STATE_VERSION_DAC_REGISTERS (v8) appends the I2S hardware registers
 * themselves: *ltxd (uint16, 2), *rtxd (uint16, 2), *sclk (uint8, 1),
 * *smode (uint32, 4), lrxd (uint16, 2), rrxd (uint16, 2), sstat (uint8,
 * 1) = 14 more, for 51.  They live in jagMemSpace at $F1A148-$F1A157,
 * which no STATE_SAVE_BUF covers, so before v8 they were restored as
 * whatever the previous run left behind -- see
 * test/tools/test_runahead_determinism.c.  The v1/v2/v3 fixture offsets
 * below are unaffected: the new fields are appended, not interleaved.
 *
 * The size is asserted deliberately: if someone adds or reorders a DAC
 * field, THIS TEST FAILS LOUDLY rather than silently splicing the wrong
 * four bytes out of the fixture and "passing".  To update, re-derive both
 * numbers from DACStateSave's field order and sizes. */
/* STATE_VERSION_DAC_I2S_RING (v9) appends the resample ring contents:
 * i2sRingL + i2sRingR, 16384 entries x int16 each = 65536 bytes, for
 * 65587.  The ring and its read/write cursors persist across frames
 * since the 60 Hz-step fix, so replay determinism needs the data
 * itself in the state.  Appended after sstat: every pre-v9 offset
 * below is unaffected. */
#define EXPECTED_DAC_BLOCK_SIZE       65587
#define EXPECTED_DAC_BLOCK_SIZE_V8    51
#define DAC_I2S_NONZEROCOUNT_OFFSET   17
#define DAC_I2S_NONZEROCOUNT_SIZE     4

/* The v1 DAC block (release v2.2.0) is just bufferIndex + numberOfSamples
 * + bufferDone = 9 bytes.  In the v2 layout the four resampler fields
 * (i2sWritePos 4, i2sWriteCount 4, i2sPhase 8, i2sRateRatio 8 = 24 bytes)
 * sit contiguously right after that prefix (i2sNonZeroCount is v3+). */
#define DAC_V1_PREFIX_SIZE            9
#define DAC_I2S_RESAMPLER_SIZE        24
/* Offsets of the resampler fields inside the CURRENT block, used to build
 * the expected post-v1-load block.  Offsets, not sizes, so appending the
 * v8 register tail does not move them. */
#define DAC_I2S_WRITEPOS_OFFSET       9    /* writePos+writeCount, 8 bytes */
#define DAC_I2S_PHASE_OFFSET          21   /* phase+ratio, 16 bytes */

/* Trailing CDROM-block fields a v2/v3 state does not carry, verified
 * against the end of CDROMStateSave() in src/cd/cdrom.c:
 *   - STATE_VERSION_CDROM_DSA_QUEUE (v4): dsaQueue (DSA_QUEUE_SIZE=4 x
 *     uint16, 8) + dsaQueueHead/Tail/Count + dsaResponseDelay
 *     (4 x uint32, 16) = 24 bytes
 *   - STATE_VERSION_CDROM_DRIVE_SPEED (v5): cdDriveSpeed (uint32, 4)
 * = 28 bytes total.  If this constant goes stale the fixture misaligns
 * everything after the CDROM block and "v2_dac_block_realigned" fails
 * loudly — re-derive it from the field order there.  The CDROM block's
 * position is computed structurally (dac_off minus the Joystick and MT
 * block sizes, per the module order in retro_serialize) because for a
 * cartridge ROM the CDROM block is zero-heavy and a byte-pattern search
 * would not be unique. */
#define CDROM_DSA_TAIL_SIZE           28

/* The pre-CD-support CDROM block (STATE_VERSION_CDROM_RESTRUCTURE), i.e.
 * what releases v2.2.0 / v2.3.0 / v2.3.1 / v2.3.2 wrote:
 *
 *   cdRam 256 + cdCmd/cdPtr 4 + haveCDGoodness 1 + min/sec/frm/block 16
 *   + cdBuf 2448 + cdBufPtr 4 + trackNum/minTrack/maxTrack 3
 *   + currentState 4 + counter 2 + cmdTx 1 + busCmd 2 + rxData/txData 4
 *   + rxDataBit 2 + firstTime 1                                  = 2748
 *   + cdBuf2 (2532+96) + cdBuf3 (2532+96)                        = 5256
 *                                                          total = 8004
 *
 * The prefix is byte-identical in every version; everything after it was
 * replaced wholesale by the BUTCH/FIFO/DSA/SSI working set.  A fixture that
 * keeps the CURRENT tail is not a v1/v2/v3 state at all — it is 2627 bytes
 * short — so synthesizing one means swapping the tail, not just trimming
 * the version-gated fields off the end. */
#define CDROM_LEGACY_PREFIX_SIZE      2748
#define CDROM_LEGACY_STAGING_SIZE     5256
#define CDROM_LEGACY_BLOCK_SIZE       (CDROM_LEGACY_PREFIX_SIZE \
                                       + CDROM_LEGACY_STAGING_SIZE)

/* Trailing v7 additions a v2..v6 state does not carry: the MT block's
 * latched $80AAA8 override flag (1 byte) plus the entire NVM BIOS block
 * that follows it (nvm_size, probed at runtime).  Together they sit
 * immediately before the DAC block. */
#define MEMTRACK_OVERRIDE_SIZE        1

/* Header field offsets (see retro_serialize in libretro.c) */
#define STATE_OFF_MAGIC    0
#define STATE_OFF_VERSION  4

typedef size_t (*dac_state_save_fn)(uint8_t *buf);
typedef size_t (*serialize_size_fn)(void);
typedef bool   (*serialize_fn)(void *data, size_t size);
typedef bool   (*unserialize_fn)(const void *data, size_t size);

static int pass_count = 0;
static int fail_count = 0;
static harness_result results[MAX_RESULTS];
static unsigned num_results = 0;
/* Details are formatted into per-result storage because harness_result
 * keeps the pointer rather than a copy. */
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

static void put_u32(uint8_t *buf, size_t off, uint32_t v)
{
    memcpy(buf + off, &v, sizeof(v));
}

static uint32_t get_u32(const uint8_t *buf, size_t off)
{
    uint32_t v;
    memcpy(&v, buf + off, sizeof(v));
    return v;
}

/* Count occurrences of needle in haystack; records the first offset.
 * Hand-rolled rather than memmem(), which needs _GNU_SOURCE under
 * -std=c99 on glibc. */
static unsigned find_pattern(const uint8_t *hay, size_t hay_len,
                             const uint8_t *needle, size_t needle_len,
                             size_t *first_off)
{
    unsigned count = 0;
    size_t i;

    if (needle_len == 0 || hay_len < needle_len)
        return 0;

    for (i = 0; i + needle_len <= hay_len; i++) {
        if (hay[i] == needle[0] && memcmp(hay + i, needle, needle_len) == 0) {
            if (count == 0 && first_off)
                *first_off = i;
            count++;
        }
    }
    return count;
}

/* Compose a genuine pre-CD-support fixture (version 1, 2 or 3) from the
 * current-version state in `src`.
 *
 * Rebuilt piecewise rather than spliced in place because the old layout is
 * both shorter (no MT override flag, no NVM block, no DSA tail, a smaller
 * DAC block) and LONGER (5256 bytes of cdBuf2/cdBuf3 where the current
 * block carries 2601 bytes of BUTCH/FIFO/DSA/SSI state) than the current
 * one.  A run of trim-only memmoves cannot express that.
 *
 * Writes the header version, returns the payload length, and reports where
 * the DAC block landed via *dac_off_out. */
static size_t synth_legacy_state(uint8_t *dst, const uint8_t *src,
                                 size_t state_size, size_t cdrom_start,
                                 size_t cdrom_end, size_t v7_tail,
                                 size_t dac_off, unsigned version,
                                 size_t *dac_off_out)
{
    size_t pos = 0;
    size_t n;

    /* Header through the CDROM block's version-invariant prefix. */
    n = cdrom_start + CDROM_LEGACY_PREFIX_SIZE;
    memcpy(dst, src, n);
    pos = n;

    /* cdBuf2 + cdBuf3.  Their contents are irrelevant to the loader (it
     * skips them by byte count); zeros are what a cartridge title's stub
     * BUTCH would have held anyway. */
    memset(dst + pos, 0, CDROM_LEGACY_STAGING_SIZE);
    pos += CDROM_LEGACY_STAGING_SIZE;

    /* Joystick + Memory Track, minus the v7 MT override flag and the whole
     * NVM BIOS block that follows it. */
    n = (dac_off - v7_tail) - cdrom_end;
    memcpy(dst + pos, src + cdrom_end, n);
    pos += n;

    if (dac_off_out)
        *dac_off_out = pos;

    /* DAC, in the layout that version carried. */
    if (version >= 3) {
        memcpy(dst + pos, src + dac_off, EXPECTED_DAC_BLOCK_SIZE);
        pos += EXPECTED_DAC_BLOCK_SIZE;
    } else if (version == 2) {
        /* Everything up to i2sNonZeroCount, then phase + rateRatio. */
        memcpy(dst + pos, src + dac_off, DAC_I2S_NONZEROCOUNT_OFFSET);
        pos += DAC_I2S_NONZEROCOUNT_OFFSET;
        memcpy(dst + pos,
               src + dac_off + DAC_I2S_NONZEROCOUNT_OFFSET
                   + DAC_I2S_NONZEROCOUNT_SIZE,
               EXPECTED_DAC_BLOCK_SIZE - DAC_I2S_NONZEROCOUNT_OFFSET
                   - DAC_I2S_NONZEROCOUNT_SIZE);
        pos += EXPECTED_DAC_BLOCK_SIZE - DAC_I2S_NONZEROCOUNT_OFFSET
               - DAC_I2S_NONZEROCOUNT_SIZE;
    } else {
        memcpy(dst + pos, src + dac_off, DAC_V1_PREFIX_SIZE);
        pos += DAC_V1_PREFIX_SIZE;
    }

    /* retro_unserialize checks size >= STATE_SIZE, so the fixture keeps the
     * full allocation with a zero tail, exactly like a real one. */
    memset(dst + pos, 0, state_size - pos);
    put_u32(dst, STATE_OFF_VERSION, (uint32_t)version);
    return pos;
}

/* Patch the header of a copy of `src` and try to load it. */
static bool try_load_patched(unserialize_fn unser, const uint8_t *src,
                             size_t len, size_t off, uint32_t value,
                             uint8_t *scratch)
{
    memcpy(scratch, src, len);
    put_u32(scratch, off, value);
    return unser(scratch, len);
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    dac_state_save_fn dac_save;
    dac_state_save_fn cdrom_save, joy_save, mt_save, nvm_save;
    serialize_size_fn ser_size_fn;
    serialize_fn ser;
    unserialize_fn unser;
    uint32_t **fb_ptr;
    int *width_ptr, *height_ptr;
    uint8_t *state_v3 = NULL, *state_v2 = NULL, *state_v1 = NULL,
            *state_v3l = NULL, *scratch = NULL;
    /* Static: the v9 DAC block is ~64 KB (ring contents) -- five of
     * them on the stack was a segfault, and did in fact segfault when
     * the ring landed and these were 256-byte automatics. */
    static uint8_t dac_v3[EXPECTED_DAC_BLOCK_SIZE + 16];
    static uint8_t dac_now[EXPECTED_DAC_BLOCK_SIZE + 16];
    static uint8_t dac_expect[EXPECTED_DAC_BLOCK_SIZE + 16];
    static uint8_t dac_post[EXPECTED_DAC_BLOCK_SIZE + 16];
    static uint8_t dac_expect_v1[EXPECTED_DAC_BLOCK_SIZE + 16];
    size_t state_size, dac_size, dac_size_now, dac_off = 0;
    size_t dac_off_v2 = 0;
    size_t cdrom_size, joy_size, mt_size, nvm_size, cdrom_end = 0;
    size_t tail_nonzero = 0, i;
    unsigned matches;
    unsigned frame;
    int rc = 2;

    cfg.frames = DEFAULT_FRAMES;

    if (!harness_init_from_args(&cfg, argc, argv)) {
        fprintf(stderr, "Failed to load core\n");
        return 2;
    }

    if (!cfg.rom_path)
        cfg.rom_path = DEFAULT_ROM;

    {
        FILE *f = fopen(cfg.rom_path, "rb");
        if (!f) {
            /* Loud and distinct from a pass: the Makefile invokes this test
             * unguarded, so exit 77 stops the suite instead of being
             * swallowed as "skipped, therefore fine". */
            fprintf(stderr,
                    "==== SKIP (test_state_compat): ROM '%s' not found ====\n"
                    "     yarc.j64 is committed in-tree; a missing ROM is a\n"
                    "     broken checkout, not a reason to pass.\n",
                    cfg.rom_path);
            harness_shutdown(&cfg);
            return 77;
        }
        fclose(f);
    }

    dac_save    = (dac_state_save_fn)harness_dlsym(&cfg, "DACStateSave");
    cdrom_save  = (dac_state_save_fn)harness_dlsym(&cfg, "CDROMStateSave");
    joy_save    = (dac_state_save_fn)harness_dlsym(&cfg, "JoystickStateSave");
    mt_save     = (dac_state_save_fn)harness_dlsym(&cfg, "MTStateSave");
    nvm_save    = (dac_state_save_fn)harness_dlsym(&cfg, "NVMBiosStateSave");
    ser_size_fn = (serialize_size_fn)harness_dlsym(&cfg, "retro_serialize_size");
    ser         = (serialize_fn)harness_dlsym(&cfg, "retro_serialize");
    unser       = (unserialize_fn)harness_dlsym(&cfg, "retro_unserialize");
    fb_ptr      = (uint32_t **)harness_dlsym(&cfg, "videoBuffer");
    width_ptr   = (int *)harness_dlsym(&cfg, "game_width");
    height_ptr  = (int *)harness_dlsym(&cfg, "game_height");

    if (!dac_save || !cdrom_save || !joy_save || !mt_save
        || !ser_size_fn || !ser || !unser
        || !fb_ptr || !width_ptr || !height_ptr) {
        fprintf(stderr,
                "Cannot resolve required symbols (DACStateSave/CDROMStateSave/\n"
                "JoystickStateSave/MTStateSave need DAC*/CDROM*/JoystickState*/\n"
                "MTState* in exports-test.list / link-test.T and a\n"
                "TEST_EXPORTS=1 build)\n");
        harness_shutdown(&cfg);
        return 2;
    }

    if (!harness_load_rom(&cfg)) {
        fprintf(stderr, "Failed to load ROM '%s'\n", cfg.rom_path);
        harness_shutdown(&cfg);
        return 2;
    }

    /* Run frames so the DAC fields hold non-trivial values — the block is
     * used as a search pattern below and needs entropy. */
    harness_run(&cfg);

    state_size = ser_size_fn();
    if (state_size != (size_t)STATE_SIZE || state_size < 16) {
        fprintf(stderr, "Unexpected retro_serialize_size(): %lu\n",
                (unsigned long)state_size);
        harness_shutdown(&cfg);
        return 2;
    }

    state_v3 = (uint8_t *)malloc(state_size);
    state_v2 = (uint8_t *)malloc(state_size);
    state_v1 = (uint8_t *)malloc(state_size);
    state_v3l = (uint8_t *)malloc(state_size);
    scratch  = (uint8_t *)malloc(state_size);
    if (!state_v3 || !state_v2 || !state_v1 || !state_v3l || !scratch) {
        fprintf(stderr, "Out of memory\n");
        goto done;
    }

    /* ---- 1. DAC block size guard ------------------------------------ */
    dac_size = dac_save(dac_v3);
    check(dac_size == EXPECTED_DAC_BLOCK_SIZE, "dac_block_size",
          "DACStateSave wrote %lu bytes, expected %d "
          "(a DAC field was added/reordered -- re-derive the size and the "
          "i2sNonZeroCount offset in this test)",
          (unsigned long)dac_size, EXPECTED_DAC_BLOCK_SIZE);
    if (dac_size != EXPECTED_DAC_BLOCK_SIZE) {
        rc = 1;
        goto report;
    }

    /* ---- 2. Serialize a current-version state ----------------------- */
    if (!ser(state_v3, state_size)) {
        check(0, "serialize", "retro_serialize() returned false");
        rc = 1;
        goto report;
    }
    check(get_u32(state_v3, STATE_OFF_MAGIC) == (uint32_t)STATE_MAGIC
          && get_u32(state_v3, STATE_OFF_VERSION) == (uint32_t)STATE_VERSION,
          "serialize_writes_current_version",
          "header magic=0x%08X version=%u (expect 0x%08X / %d)",
          get_u32(state_v3, STATE_OFF_MAGIC),
          get_u32(state_v3, STATE_OFF_VERSION),
          (uint32_t)STATE_MAGIC, STATE_VERSION);

    /* ---- 3. Locate the DAC block ------------------------------------ */
    matches = find_pattern(state_v3, state_size, dac_v3, dac_size, &dac_off);
    check(matches == 1, "dac_block_located",
          "DAC byte pattern occurs %u time(s) in the state, need exactly 1",
          matches);
    if (matches != 1) {
        rc = 1;
        goto report;
    }

    /* DAC is serialized last and retro_serialize zero-fills the tail, so a
     * correct offset implies nothing but padding follows.  This corroborates
     * the pattern match structurally. */
    for (i = dac_off + dac_size; i < state_size; i++) {
        if (state_v3[i] != 0)
            tail_nonzero++;
    }
    check(tail_nonzero == 0, "dac_block_is_last",
          "offset %lu + %lu bytes, then %lu non-zero tail byte(s) "
          "(expected pure zero padding)",
          (unsigned long)dac_off, (unsigned long)dac_size,
          (unsigned long)tail_nonzero);
    if (tail_nonzero != 0) {
        rc = 1;
        goto report;
    }

    /* ---- 3b. Locate the CDROM block structurally --------------------- */
    /* Module order in retro_serialize: ... CDROM, Joystick, MT, DAC.  No
     * frames run between the serialize above and these probes, so the
     * standalone block dumps are byte-identical to what the state holds. */
    joy_size   = joy_save(scratch);
    mt_size    = mt_save(scratch);
    nvm_size   = nvm_save(scratch);
    cdrom_size = cdrom_save(scratch);
    cdrom_end  = dac_off - nvm_size - mt_size - joy_size;
    check(cdrom_end > cdrom_size && cdrom_size > CDROM_DSA_TAIL_SIZE
          && memcmp(state_v3 + cdrom_end - cdrom_size, scratch,
                    cdrom_size) == 0,
          "cdrom_block_located",
          "CDROM block (%lu bytes) ends at %lu (dac_off %lu - joy %lu - "
          "mt %lu) and matches a standalone CDROMStateSave dump",
          (unsigned long)cdrom_size, (unsigned long)cdrom_end,
          (unsigned long)dac_off, (unsigned long)joy_size,
          (unsigned long)mt_size);
    if (cdrom_end <= cdrom_size || cdrom_size <= CDROM_DSA_TAIL_SIZE
        || memcmp(state_v3 + cdrom_end - cdrom_size, scratch,
                  cdrom_size) != 0) {
        rc = 1;
        goto report;
    }

    /* ---- 4. Same-version round trip (positive control) -------------- */
    check(unser(state_v3, state_size), "v3_round_trip",
          "retro_unserialize() of a freshly written v%d state", STATE_VERSION);
    dac_size_now = dac_save(dac_now);
    check(dac_size_now == dac_size
          && memcmp(dac_now, dac_v3, dac_size) == 0,
          "v3_dac_block_round_trip",
          "DAC block after same-version load is byte-identical to the saved one");

    /* ---- 5. Synthesize a genuine v2-layout state -------------------- */
    /* The CDROM prefix constant has to agree with the block the current
     * core writes, or every fixture below is nonsense.  Assert it rather
     * than trusting the arithmetic in the comment. */
    check(cdrom_size > CDROM_LEGACY_PREFIX_SIZE + CDROM_DSA_TAIL_SIZE
          && CDROM_LEGACY_BLOCK_SIZE > cdrom_size,
          "cdrom_legacy_constants_sane",
          "current CDROM block %lu bytes, legacy %d (prefix %d + staging %d)",
          (unsigned long)cdrom_size, CDROM_LEGACY_BLOCK_SIZE,
          CDROM_LEGACY_PREFIX_SIZE, CDROM_LEGACY_STAGING_SIZE);

    synth_legacy_state(state_v2, state_v3, state_size,
                       cdrom_end - cdrom_size, cdrom_end,
                       MEMTRACK_OVERRIDE_SIZE + nvm_size, dac_off, 2u,
                       &dac_off_v2);
    (void)dac_off_v2;

    /* What the DAC block must look like after loading that fixture: every
     * field as saved, except i2sNonZeroCount which a v2 state cannot carry
     * and which DACStateLoad therefore zeroes. */
    memcpy(dac_expect, dac_v3, dac_size);
    memset(dac_expect + DAC_I2S_NONZEROCOUNT_OFFSET, 0,
           DAC_I2S_NONZEROCOUNT_SIZE);

    /* Drift away from the snapshot so neither the load nor the block
     * comparison below can pass by doing nothing at all. */
    for (frame = 0; frame < (unsigned)DRIFT_FRAMES; frame++)
        harness_step(&cfg);
    dac_size_now = dac_save(dac_now);
    check(dac_size_now == dac_size
          && memcmp(dac_now, dac_v3, dac_size) != 0,
          "dac_state_drifted",
          "DAC block changed over %d frames, so the v2 load below is not "
          "vacuous", DRIFT_FRAMES);

    /* ---- 6. The v2 state must load (libretro.c half of the fix) ----- */
    check(unser(state_v2, state_size), "v2_state_loads",
          "retro_unserialize() of a v2-layout state (pre-fix: refused "
          "because the gate demanded version == %d)",
          STATE_VERSION);

    /* ---- 7. ...with the DAC block still aligned (dac.c half) -------- */
    /* This is the only assertion that catches an unconditional consume of
     * i2sNonZeroCount: DAC is the last module, so reading four bytes too
     * many still leaves retro_unserialize returning true and the core
     * running — only the field values give it away. */
    dac_size_now = dac_save(dac_now);
    check(dac_size_now == dac_size
          && memcmp(dac_now, dac_expect, dac_size) == 0,
          "v2_dac_block_realigned",
          "post-load DAC block matches the saved block with i2sNonZeroCount "
          "zeroed (fields after the skipped one must not shift)");

    /* ---- 8. ...and the core must still be alive --------------------- */
    /* Emulation has to keep ADVANCING, which is the part a framebuffer
     * check cannot establish: yarc's screen is near-static, so its
     * non-black pixel count comes out bit-identical whether the load took
     * or was refused (measured: 61071/78566 either way).  Comparing the
     * DAC block against its immediately-post-load value is what actually
     * distinguishes a live core from a frozen one. */
    memcpy(dac_post, dac_now, dac_size);   /* immediately-post-load snapshot */
    for (frame = 0; frame < (unsigned)DRIFT_FRAMES; frame++)
        harness_step(&cfg);
    dac_size_now = dac_save(dac_now);
    check(dac_size_now == dac_size
          && memcmp(dac_now, dac_post, dac_size) != 0,
          "core_advances_after_v2_load",
          "DAC state advanced over %d frames following the v2 load",
          DRIFT_FRAMES);
    {
        uint32_t *fb = *fb_ptr;
        int w = *width_ptr, h = *height_ptr;
        unsigned total = 0, nonblack = 0;
        double frac = 0.0;

        if (fb && w > 0 && h > 0) {
            total = (unsigned)(w * h);
            for (i = 0; i < total; i++) {
                if (fb[i] & 0x00FFFFFFu)
                    nonblack++;
            }
            frac = (double)nonblack / (double)total;
        }
        check(total > 0 && frac > MIN_NONBLACK_FRACTION,
              "core_live_after_v2_load",
              "%dx%d, %.1f%% non-black (%u/%u) after %d frames, need >%.0f%%",
              w, h, frac * 100.0, nonblack, total, DRIFT_FRAMES,
              MIN_NONBLACK_FRACTION * 100.0);
    }

    /* ---- 8b. Synthesize and load a genuine v1-layout state ---------- */
    /* v1 is what release v2.2.0 wrote (the only released core that ever
     * wrote version 1): the v2 layout minus the four DAC resampler fields,
     * which sit contiguously after the 9-byte DAC prefix (issue #268). */
    synth_legacy_state(state_v1, state_v3, state_size,
                       cdrom_end - cdrom_size, cdrom_end,
                       MEMTRACK_OVERRIDE_SIZE + nvm_size, dac_off, 1u, NULL);

    /* Expected DAC block after the v1 load: the saved prefix, every
     * resampler field at its DACInit() default (writePos/writeCount/
     * nonZeroCount 0, phase 0.0, rateRatio 1.0 — DACPrepareFrame
     * re-derives all of them at the next frame boundary). */
    memcpy(dac_expect_v1, dac_v3, dac_size);
    memset(dac_expect_v1 + DAC_I2S_WRITEPOS_OFFSET, 0,
           DAC_I2S_PHASE_OFFSET + 8 - DAC_I2S_WRITEPOS_OFFSET);
    {
        double one = 1.0;
        memcpy(dac_expect_v1 + DAC_I2S_PHASE_OFFSET + 8, &one, sizeof(one));
    }

    check(unser(state_v1, state_size), "v1_state_loads",
          "retro_unserialize() of a v1-layout state (release v2.2.0's "
          "format; pre-#268 fix: refused because STATE_MIN_VERSION was 2)");

    check((dac_size_now = dac_save(dac_now)) == dac_size
          && memcmp(dac_now, dac_expect_v1, dac_size) == 0,
          "v1_dac_block_defaulted",
          "post-load DAC block matches the saved prefix with the resampler "
          "fields at their init defaults (fields must not shift)");

    memcpy(dac_post, dac_now, dac_size);
    for (frame = 0; frame < (unsigned)DRIFT_FRAMES; frame++)
        harness_step(&cfg);
    dac_size_now = dac_save(dac_now);
    check(dac_size_now == dac_size
          && memcmp(dac_now, dac_post, dac_size) != 0,
          "core_advances_after_v1_load",
          "DAC state advanced over %d frames following the v1 load",
          DRIFT_FRAMES);

    /* ---- 8c. v3 layout (release v2.3.2) ---------------------------- */
    /* The strictest alignment assertion in the file: a v3 state carries the
     * DAC block in full, so after the load every one of its bytes must
     * come back byte-identical.  Any mis-sized chunk anywhere upstream —
     * the CDROM restructure was exactly that — shifts the DAC read and this
     * fails, while retro_unserialize still returns true. */
    synth_legacy_state(state_v3l, state_v3, state_size,
                       cdrom_end - cdrom_size, cdrom_end,
                       MEMTRACK_OVERRIDE_SIZE + nvm_size, dac_off, 3u, NULL);

    for (frame = 0; frame < (unsigned)DRIFT_FRAMES; frame++)
        harness_step(&cfg);

    check(unser(state_v3l, state_size), "v3_state_loads",
          "retro_unserialize() of a v3-layout state (release v2.3.2's "
          "format)");

    check((dac_size_now = dac_save(dac_now)) == dac_size
          && memcmp(dac_now, dac_v3, dac_size) == 0,
          "v3_dac_block_realigned",
          "post-load DAC block is byte-identical to the saved one -- proves "
          "every chunk before it was read in the v3 layout, not the v7 one");

    /* ---- 9. Rejections -------------------------------------------- */
    check(try_load_patched(unser, state_v3, state_size, STATE_OFF_VERSION,
                           0u, scratch) == false,
          "v0_state_rejected",
          "version 0 is below STATE_MIN_VERSION (%d) -- no released core "
          "ever wrote it", STATE_MIN_VERSION);

    check(try_load_patched(unser, state_v3, state_size, STATE_OFF_VERSION,
                           (uint32_t)STATE_VERSION + 1u, scratch) == false,
          "future_state_rejected",
          "version %d is newer than STATE_VERSION (%d)",
          STATE_VERSION + 1, STATE_VERSION);

    check(try_load_patched(unser, state_v3, state_size, STATE_OFF_MAGIC,
                           0xDEADBEEFu, scratch) == false,
          "bad_magic_rejected", "magic 0xDEADBEEF is not 0x%08X",
          (uint32_t)STATE_MAGIC);

    rc = fail_count > 0 ? 1 : 0;

report:
    harness_report(&cfg, results, num_results);
    if (!cfg.json_output)
        printf("  DAC block: %lu bytes at state offset %lu; %d passed, %d failed\n",
               (unsigned long)dac_size, (unsigned long)dac_off,
               pass_count, fail_count);

done:
    free(state_v3);
    free(state_v2);
    free(state_v1);
    free(state_v3l);
    free(scratch);
    harness_shutdown(&cfg);
    return rc;
}
