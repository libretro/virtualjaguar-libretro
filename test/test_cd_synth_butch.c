/*
 * test_cd_synth_butch.c -- REAL-BIOS CD path (BUTCH registers, DSA serial
 * link, I2S FIFO) pinned against a SYNTHETIC disc image.
 *
 * test_cd_synth_read.c does this for the HLE CD_read path.  The real-BIOS
 * path -- src/cd/cdrom.c, the code that answers DSA commands, runs the seek
 * state machine and hands disc bytes to the CD BIOS's GPU ISR through the
 * BUTCH FIFO -- had no committed coverage at all: every other CD test that
 * touches it boots a commercial disc out of test/roms/private, which is
 * gitignored and absent in CI and on a fresh clone.
 *
 * HOW THE DISC DRIVES THE REAL-BIOS PATH
 * --------------------------------------
 * These tests do not enter a CD *boot mode* and never call retro_load_game,
 * so the `virtualjaguar_cd_boot_mode` option is not involved.  They open the
 * synthetic image with CDIntfOpenImage() and then play the part the CD BIOS
 * plays on hardware: writing DSA commands to DS_DATA, polling BUTCH+2 for
 * the RX-full / FIFO-half-full status bits, ticking BUTCHExec() (which
 * HalflineCallback drives once per halfline in the real core) and draining
 * FIFO_DATA the way the BIOS's GPU CD ISR does.  That is the entire
 * real-BIOS data path; nothing about it is simulated by the harness.
 *
 * The disc is a single-file, single-track CUE/BIN of DISC_SECTORS sectors
 * filled with a deterministic, position-derived pattern in 1..251 (never
 * $00, so "silence returned because the read fell off the disc or into an
 * inter-session gap" is distinguishable from real data).  Adjacent bytes
 * always differ -- asserted at runtime, because the byte-order assertions
 * below are only meaningful if they do.
 *
 * INVARIANTS PINNED (each with a documented negative control)
 * ----------------------------------------------------------
 *   1. FIFO byte-exactness -- after a DSA seek to LBA L, FIFO_DATA hands
 *      back the bytes stored at L: the MSF->LBA conversion (incl. the 150
 *      sector lead-in), the one-word capture skew (the stream starts at
 *      byte 2 of the sector, not byte 0), the little-endian word assembly,
 *      and the roll to L+1 at the sector boundary.
 *   2. FIFO half-full accounting -- a drain takes several reads, clears the
 *      half-full flag exactly once, and the flag comes back only after
 *      elapsed BUTCHExec ticks, never from reading harder.
 *   3. DSA responses are never instant, and each command gets its documented
 *      response word.
 *   4. Seek bookkeeping -- one SEEK_DONE per SEEK_START, never in the same
 *      instant; STOP does not cancel an in-flight seek; a redundant seek
 *      does NOT restart the state machine but IS still answered with Found.
 *   5. Drive-speed latch ($15nn) -- single speed paces the FIFO refill at
 *      exactly half the double-speed rate, decoded from the one-based speed
 *      CODE in the payload's low bits and not from bit 0.
 *
 * Timing CONSTANTS are deliberately never asserted (SEEK_DELAY_TICKS,
 * DSA_RESPONSE_DELAY_TICKS, FIFO_DRAIN_READS, FIFO_REFILL_PERIOD_X100 may
 * legitimately move).  Every timing claim here is relational: ">= 1 tick"
 * for the non-instant invariants, "exactly 2x" for the speed ratio.
 *
 * Build: make test/test_cd_synth_butch     (needs TEST_EXPORTS=1)
 * Run:   DYLD_LIBRARY_PATH=. test/test_cd_synth_butch
 */

#include "test_framework.h"

#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

static struct vj_core C;

/* ------------------------------------------------------------------ */
/* Core internals resolved by dlsym                                     */
/* ------------------------------------------------------------------ */

static bool     (*p_open_image)(const char *);
static void     (*p_close_image)(void);
static bool     (*p_read_block)(uint32_t, uint8_t *);
static uint32_t (*p_num_sessions)(void);
static uint32_t (*p_disc_sectors)(void);
static void     (*p_butch_exec)(uint32_t);
static void     (*p_seek_state)(uint32_t *, uint32_t *, uint32_t *);

/* ------------------------------------------------------------------ */
/* BUTCH register offsets (CDROMReadWord/CDROMWriteWord mask to & $FF)   */
/* ------------------------------------------------------------------ */

#define R_BUTCH_HI     0x00u
#define R_BUTCH_LO     0x02u   /* status word: bits 9..14 read-only        */
#define R_DS_DATA      0x0Au   /* DSA TX/RX data                            */
#define R_I2CNTRL_LO   0x12u   /* low word of I2CNTRL ($10)                 */
#define R_FIFO_DATA    0x24u   /* i2s FIFO data                             */

#define ST_FIFO_HALF   (1u << 9)    /* FIFO half-full pending               */
#define ST_FRAME       (1u << 10)   /* set while the drive is playing       */
#define ST_SUBCODE     (1u << 11)   /* ditto                                */
#define ST_TX_EMPTY    (1u << 12)
#define ST_RX_FULL     (1u << 13)   /* DSA response available               */

#define I2S_DATA_ENABLE 0x0004u     /* I2CNTRL bit 2: I2S data enable       */

#define CALLER 0u                   /* `who` argument -- UNKNOWN            */

/* ------------------------------------------------------------------ */
/* Synthetic disc geometry                                              */
/* ------------------------------------------------------------------ */

#define SECTOR_BYTES   2352u
#define DISC_SECTORS   64u
#define TARGET_LBA     20u     /* well inside the single track              */

/* Words the FIFO yields out of one sector once the stream is running:
 * the first sector after a seek starts at byte 2, later sectors at byte 0. */
#define WORDS_FIRST_SECTOR   ((SECTOR_BYTES - 2u) / 2u)   /* 1175 */
#define WORDS_FULL_SECTOR    (SECTOR_BYTES / 2u)          /* 1176 */

/* Bounds.  Generous enough that a legitimate constant change cannot trip
 * them, tight enough that a hang is reported instead of spinning. */
#define MAX_SEEK_TICKS   200000u
#define MAX_DSA_TICKS      4096u
#define MAX_REFILL_TICKS   4096u
#define MAX_DRAIN_READS      256u

/* ------------------------------------------------------------------ */
/* Deterministic disc pattern                                           */
/* ------------------------------------------------------------------ */

/* Byte stored at absolute file offset `off`.  Always in 1..251, and -- the
 * property the byte-order assertions depend on -- consecutive offsets never
 * hold the same value, because the step (37) is non-zero mod 251. */
static uint8_t disc_pat(uint32_t off)
{
    return (uint8_t)(1u + ((off * 37u + (off >> 8) * 11u) % 251u));
}

/* ------------------------------------------------------------------ */
/* Synthetic disc construction                                          */
/* ------------------------------------------------------------------ */

static bool make_disc(const char *dir, char *cueOut, size_t cueOutLen)
{
    char path[1024];
    uint8_t *bin = NULL;
    size_t binLen = (size_t)DISC_SECTORS * SECTOR_BYTES;
    size_t i;
    size_t written;
    FILE *f;
    bool ok = false;

    bin = (uint8_t *)malloc(binLen);
    if (!bin)
        return false;
    for (i = 0; i < binLen; i++)
        bin[i] = disc_pat((uint32_t)i);

    snprintf(path, sizeof(path), "%s/track01.bin", dir);
    f = fopen(path, "wb");
    if (!f)
        goto done;
    written = fwrite(bin, 1, binLen, f);
    fclose(f);
    if (written != binLen)
        goto done;

    snprintf(cueOut, cueOutLen, "%s/disc.cue", dir);
    f = fopen(cueOut, "wb");
    if (!f)
        goto done;
    fprintf(f,
            "FILE \"track01.bin\" BINARY\n"
            "  TRACK 01 AUDIO\n"
            "    INDEX 01 00:00:00\n");
    fclose(f);
    ok = true;

done:
    free(bin);
    return ok;
}

/* Remove the scratch disc.  Called on every exit path. */
static void scrub_scratch(const char *base)
{
    char path[1024];

    snprintf(path, sizeof(path), "%s/track01.bin", base); remove(path);
    snprintf(path, sizeof(path), "%s/disc.cue",    base); remove(path);
    rmdir(base);
}

/* ------------------------------------------------------------------ */
/* BUTCH driving helpers -- these are the CD BIOS's moves                */
/* ------------------------------------------------------------------ */

static uint16_t butch_status(void)
{
    /* Pure read: BUTCH+2 has no side effects, unlike DSCNTRL (which acks
     * the DSA latch) and DS_DATA / FIFO_DATA (which consume). */
    return C.CDROMReadWord(R_BUTCH_LO, CALLER);
}

static void butch_tick(uint32_t n)
{
    while (n--)
        p_butch_exec(0);
}

static void dsa_send(uint16_t cmd)
{
    C.CDROMWriteWord(R_DS_DATA, cmd, CALLER);
}

static uint16_t dsa_recv(void)
{
    return C.CDROMReadWord(R_DS_DATA, CALLER);
}

static uint16_t fifo_word(void)
{
    return C.CDROMReadWord(R_FIFO_DATA, CALLER);
}

static void i2s_data_enable(void)
{
    C.CDROMWriteWord(R_I2CNTRL_LO, I2S_DATA_ENABLE, CALLER);
}

/* Ticks BUTCHExec until the DSA RX-full status bit rises.  Returns the
 * number of ticks that were needed (0 = already up before any tick), or
 * 0xFFFFFFFF if it never rose within `max`. */
static uint32_t wait_rx_full(uint32_t max)
{
    uint32_t n = 0;

    while (n <= max)
    {
        if (butch_status() & ST_RX_FULL)
            return n;
        p_butch_exec(0);
        n++;
    }
    return 0xFFFFFFFFu;
}

/* Issue the three-command DSA seek the BIOS issues: Goto Min / Goto Sec /
 * Goto Frame.  Only $12xx arms the seek; the MSF carries the 150-sector
 * lead-in that the $12xx handler subtracts back off. */
static void dsa_seek(uint32_t lba)
{
    uint32_t tf = lba + 150u;

    dsa_send((uint16_t)(0x1000u | (tf / 4500u)));
    dsa_send((uint16_t)(0x1100u | ((tf / 75u) % 60u)));
    dsa_send((uint16_t)(0x1200u | (tf % 75u)));
}

static uint32_t seek_dones(void)
{
    uint32_t dones = 0;
    p_seek_state(NULL, &dones, NULL);
    return dones;
}

static uint32_t seek_starts(void)
{
    uint32_t starts = 0;
    p_seek_state(&starts, NULL, NULL);
    return starts;
}

static uint32_t fifo_drains(void)
{
    uint32_t drains = 0;
    p_seek_state(NULL, NULL, &drains);
    return drains;
}

/* Ticks until the seek-done counter moves past `before`.  Returns ticks
 * used, or 0xFFFFFFFF on timeout. */
static uint32_t wait_seek_done(uint32_t before, uint32_t max)
{
    uint32_t n = 0;

    while (n <= max)
    {
        if (seek_dones() != before)
            return n;
        p_butch_exec(0);
        n++;
    }
    return 0xFFFFFFFFu;
}

/* Reset + I2S data enable + seek to `lba` + consume the Found response.
 * Leaves the drive playing with the FIFO primed at `lba` byte 2 -- exactly
 * the state the CD BIOS's GPU ISR starts draining from.
 * Returns false (having printed why) if any step did not happen. */
static bool prime_stream(uint32_t lba)
{
    uint32_t before;
    uint32_t t;
    uint16_t r;

    C.CDROMReset();
    i2s_data_enable();

    before = seek_dones();
    dsa_seek(lba);
    if (wait_seek_done(before, MAX_SEEK_TICKS) == 0xFFFFFFFFu)
    {
        fprintf(stderr, "        (seek to LBA %u never completed)\n", lba);
        return false;
    }
    t = wait_rx_full(MAX_DSA_TICKS);
    if (t == 0xFFFFFFFFu)
    {
        fprintf(stderr, "        (seek response never became visible)\n");
        return false;
    }
    r = dsa_recv();
    if (r != 0x0100)
    {
        fprintf(stderr, "        (seek response was $%04X, expected $0100)\n", r);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Ground truth straight off the image                                  */
/* ------------------------------------------------------------------ */

static uint8_t sect[3][SECTOR_BYTES];

static bool load_ground_truth(uint32_t lba)
{
    uint32_t s;

    for (s = 0; s < 3; s++)
        if (!p_read_block(lba + s, sect[s]))
            return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* 0. Preflight: the disc is live and BUTCH is actually running          */
/* ------------------------------------------------------------------ */

/* Everything below silently no-ops if haveCDGoodness is false: BUTCHExec
 * returns immediately, DS_DATA answers $0400, FIFO_DATA falls through to a
 * raw cdRam read.  A setup failure would then look like a behaviour
 * failure, so it gets its own test that fails first and unambiguously.
 *
 * Also asserts the disc pattern property the byte-order checks rely on
 * (adjacent bytes differ), which is what makes the word-assembly negative
 * control able to go red at all. */
TEST(preflight_disc_and_butch_are_live)
{
    uint32_t total;
    uint32_t before;
    uint32_t k;

    total = p_disc_sectors();
    if (total < TARGET_LBA + 3u)
        FAIL("disc reports %u sectors, need at least %u",
             total, TARGET_LBA + 3u);

    if (!load_ground_truth(TARGET_LBA))
        FAIL("CDIntfReadBlock failed inside the synthetic track");

    /* Real data, not the silence CDIntfReadBlock returns for an
     * out-of-track / inter-session read. */
    for (k = 0; k < SECTOR_BYTES; k++)
    {
        if (sect[0][k] != disc_pat(TARGET_LBA * SECTOR_BYTES + k))
            FAIL("LBA %u byte %u: image holds $%02X, expected $%02X",
                 TARGET_LBA, k, sect[0][k],
                 disc_pat(TARGET_LBA * SECTOR_BYTES + k));
    }
    for (k = 0; k + 1u < SECTOR_BYTES; k++)
    {
        if (sect[0][k] == sect[0][k + 1u])
            FAIL("pattern degenerate at byte %u ($%02X twice): the byte-order "
                 "assertions below would be vacuous", k, sect[0][k]);
    }

    /* BUTCHExec is live: a seek must actually complete. */
    C.CDROMReset();
    i2s_data_enable();
    before = seek_dones();
    dsa_seek(TARGET_LBA);
    if (wait_seek_done(before, MAX_SEEK_TICKS) == 0xFFFFFFFFu)
        FAIL("seek never completed -- BUTCHExec is not running or the disc "
             "is not attached (haveCDGoodness false?)");
}

/* ------------------------------------------------------------------ */
/* 1. FIFO drain delivers the disc bytes at the seek LBA                 */
/* ------------------------------------------------------------------ */

/* The real-BIOS analogue of test_cd_synth_read's byte-exactness check.
 * Four separate behaviours have to be right for this to pass, and each has
 * its own one-line sabotage:
 *
 *   MSF -> LBA        drop the `- 150` lead-in adjustment in the $12xx
 *                     handler: every word comes from LBA+150.
 *   capture skew      `cdBufPtr = 2` -> `0` after the seek: the stream
 *                     starts one word early, so word 0 mismatches.
 *   word assembly     swap `(cdBuf[p+1] << 8) | cdBuf[p]`: every word's
 *                     halves trade places (only detectable because adjacent
 *                     disc bytes differ -- see the preflight test).
 *   sector roll       break the `cdBufPtr >= 2352` refill: the stream
 *                     stalls or repeats at the 1175-word boundary.
 *
 * Not a single BUTCHExec tick is issued during the read loop, so the
 * comparison is fully deterministic: FIFO_DATA delivers while the drive is
 * playing, and pacing (fifoDataReady) is test 2's business. */
TEST(fifo_stream_matches_disc_bytes_at_seek_lba)
{
    uint32_t totalWords = WORDS_FIRST_SECTOR + WORDS_FULL_SECTOR + 8u;
    uint32_t i;
    uint32_t s = 0;
    uint32_t off = 2u;      /* the one-word capture skew */
    uint16_t got;
    uint16_t want;

    if (!load_ground_truth(TARGET_LBA))
        FAIL("could not read ground truth off the image");
    if (!prime_stream(TARGET_LBA))
        FAIL("could not prime the stream at LBA %u", TARGET_LBA);

    /* Stated separately from the loop because it is the load-bearing bit:
     * the stream starts ONE WORD into the sector.  Jaguar CD discs are
     * mastered for BUTCH's one-word FIFO capture skew, and starting at byte
     * 0 is the "streaming wall" regression. */
    got  = fifo_word();
    want = (uint16_t)((sect[0][3] << 8) | sect[0][2]);
    if (got != want)
        FAIL("first word after seek: got $%04X, expected $%04X (bytes 2..3 "
             "of LBA %u).  $%04X would be bytes 0..1 -- capture skew lost",
             got, want, TARGET_LBA,
             (uint16_t)((sect[0][1] << 8) | sect[0][0]));
    off = 4u;

    for (i = 1; i < totalWords; i++)
    {
        if (off >= SECTOR_BYTES)
        {
            s++;
            off = 0u;
            if (s >= 3u)
                FAIL("read ran past the ground truth window at word %u", i);
        }
        got  = fifo_word();
        want = (uint16_t)((sect[s][off + 1u] << 8) | sect[s][off]);
        if (got != want)
            FAIL("word %u (LBA %u byte %u): got $%04X, expected $%04X",
                 i, TARGET_LBA + s, off, got, want);
        /* Restate the sector-boundary word: the first word of a new sector
         * must be its bytes 0..1, i.e. the roll to LBA+1 happened and did
         * not re-apply the skew. */
        if (off == 0u && got != (uint16_t)((sect[s][1] << 8) | sect[s][0]))
            FAIL("sector roll at word %u did not land on byte 0 of LBA %u",
                 i, TARGET_LBA + s);
        off += 2u;
    }

    /* The drive is still playing -- nothing above stopped it. */
    ASSERT_TRUE((butch_status() & ST_FRAME) != 0);
}

/* ------------------------------------------------------------------ */
/* 2. FIFO half-full drain / refill accounting                           */
/* ------------------------------------------------------------------ */

/* The half-full flag is what paces the CD BIOS's GPU ISR.  Three claims,
 * none of which names a constant:
 *
 *   a) a drain takes MORE THAN ONE read (hardware: 16 word reads = the
 *      8 longwords the ISR loads) and bumps the drain counter exactly once;
 *   b) reading past the drain point does NOT re-raise the flag or count
 *      more drains -- refill is a drive-side event, not a side effect of
 *      the host reading harder;
 *   c) the flag comes back only after >= 1 elapsed BUTCHExec tick.
 *
 * Negative controls (both verified against this file):
 *   (a) in CDROMReadWord's FIFO_DATA branch replace
 *         fifoFillDelay = CDROMNextRefillDelay();
 *       with
 *         fifoFillDelay = 0; fifoDataReady = true;
 *       -- the flag never drops and the drain bound trips.
 *   (b) re-raise the flag on any host read (`if (!fifoDataReady) {
 *       fifoDataReady = true; fifoReadCount = 0; }` ahead of the drain
 *       counter) -- (b) trips on the first extra read. */
TEST(fifo_half_full_drain_and_refill_accounting)
{
    uint32_t drains0;
    uint32_t reads = 0;
    uint32_t i;
    uint32_t ticks = 0;

    if (!prime_stream(TARGET_LBA))
        FAIL("could not prime the stream at LBA %u", TARGET_LBA);

    /* Seek completion with I2S data enabled primes the FIFO. */
    if (!(butch_status() & ST_FIFO_HALF))
        FAIL("FIFO half-full not set after a completed seek with I2S enabled");

    drains0 = fifo_drains();
    while (butch_status() & ST_FIFO_HALF)
    {
        (void)fifo_word();
        reads++;
        if (reads > MAX_DRAIN_READS)
            FAIL("half-full flag still set after %u reads -- the FIFO never "
                 "drains", reads);
    }
    if (reads < 2u)
        FAIL("half-full flag cleared after %u read(s) -- a FIFO batch is "
             "several host reads deep", reads);
    ASSERT_EQ_U32(fifo_drains() - drains0, 1u);

    /* (b) Reading harder does not refill. */
    for (i = 0; i < 200u; i++)
    {
        (void)fifo_word();
        if (butch_status() & ST_FIFO_HALF)
            FAIL("half-full flag re-raised by host reads alone (after %u "
                 "extra reads, no BUTCHExec tick)", i + 1u);
    }
    ASSERT_EQ_U32(fifo_drains() - drains0, 1u);

    /* (c) It comes back on drive time. */
    while (!(butch_status() & ST_FIFO_HALF))
    {
        p_butch_exec(0);
        ticks++;
        if (ticks > MAX_REFILL_TICKS)
            FAIL("FIFO never refilled in %u ticks", ticks);
    }
    if (ticks < 1u)
        FAIL("FIFO refilled without a single elapsed tick");
}

/* ------------------------------------------------------------------ */
/* 3. DSA request/response sequencing                                    */
/* ------------------------------------------------------------------ */

/* Two things at once.
 *
 * NEVER INSTANT: on hardware a response word arrives over the DSA serial
 * link hundreds of microseconds after the command; it is never already
 * pending in the same instant the 68K writes DS_DATA.  Making queued
 * responses visible synchronously created a real steal race -- if the 68K's
 * timeslice ended between writing the command and its first BUTCH bit-13
 * poll, the game's GPU CD ISR (entered constantly for FIFO service) saw
 * bit 13 already up, acked and consumed the response, and the 68K polled
 * forever.  Device-traced on Primal Rage: the attract loop's CDDA hand-off
 * blacked out ~103 s in, permanently.
 *
 * CORRECT WORD: each command's documented answer.  $04nn is the generic
 * "done, no error" ack in this DSA dialect, not an error.
 *
 * Negative control (verified): set `dsaResponseReady = true` directly in
 * DSAQueuePush() -- every case fails the "not instant" assertion. */
TEST(dsa_responses_are_delayed_and_correct)
{
    uint32_t sessions = p_num_sessions();
    uint16_t cmds[9];
    uint16_t want[9];
    uint32_t n;

    cmds[0] = 0x5400; want[0] = (uint16_t)(0x5400u | (sessions & 0xFFu));
    cmds[1] = 0x5000; want[1] = (uint16_t)(0x0300u | (sessions & 0xFFu));
    cmds[2] = 0x1800; want[2] = 0x0143;                  /* Spun Up        */
    cmds[3] = 0x150A; want[3] = 0x170A;                  /* Mode Status    */
    cmds[4] = 0x1509; want[4] = 0x1709;
    cmds[5] = 0x7001; want[5] = 0x7001;                  /* DAC-mode echo  */
    cmds[6] = 0x0400; want[6] = 0x0400;                  /* Pause done     */
    cmds[7] = 0x0500; want[7] = 0x0400;                  /* Unpause done   */
    cmds[8] = 0x0200; want[8] = 0x0200;                  /* Stopped        */

    for (n = 0; n < 9u; n++)
    {
        uint32_t t;
        uint16_t got;

        C.CDROMReset();
        dsa_send(cmds[n]);

        if (butch_status() & ST_RX_FULL)
            FAIL("$%04X: response already visible in the same instant the "
                 "command was written -- the DSA steal race is back",
                 cmds[n]);

        t = wait_rx_full(MAX_DSA_TICKS);
        if (t == 0xFFFFFFFFu)
            FAIL("$%04X: no response within %u ticks", cmds[n], MAX_DSA_TICKS);

        got = dsa_recv();
        if (got != want[n])
            FAIL("$%04X: got $%04X, expected $%04X", cmds[n], got, want[n]);
    }
}

/* ------------------------------------------------------------------ */
/* 4. Seek start/completion bookkeeping                                  */
/* ------------------------------------------------------------------ */

/* (a) One SEEK_DONE per SEEK_START, and never in the same instant -- the
 *     BIOS polls BUTCH+2 once right after $12xx and expects NOTHING yet; a
 *     response already pending there makes it send STOP and give up.
 *
 * Negative control (verified): complete the seek inline in the $12xx
 * handler -- `seekDelay = 0; cdSeekDoneCount++; DSAQueuePush(0x0100);`
 * instead of arming seekDelay -- and the "completed in the same instant it
 * was armed" assertion fires.  Shortening SEEK_DELAY_TICKS (verified at 3
 * instead of 100) leaves this green: the invariant is deliberately
 * relational, so it does not pin the constant. */
TEST(seek_start_and_done_accounting)
{
    uint32_t starts0;
    uint32_t dones0;
    uint32_t t;

    C.CDROMReset();
    i2s_data_enable();

    starts0 = seek_starts();
    dones0  = seek_dones();

    dsa_seek(TARGET_LBA);
    ASSERT_EQ_U32(seek_starts() - starts0, 1u);
    if (seek_dones() != dones0)
        FAIL("seek completed in the same instant it was armed");
    if (butch_status() & ST_RX_FULL)
        FAIL("seek response visible before the seek completed");

    t = wait_seek_done(dones0, MAX_SEEK_TICKS);
    if (t == 0xFFFFFFFFu)
        FAIL("seek never completed in %u ticks", MAX_SEEK_TICKS);

    /* Exactly one completion for exactly one start. */
    ASSERT_EQ_U32(seek_dones() - dones0, 1u);
    ASSERT_EQ_U32(seek_starts() - starts0, 1u);

    if (wait_rx_full(MAX_DSA_TICKS) == 0xFFFFFFFFu)
        FAIL("seek Found response never became visible");
    ASSERT_EQ_U16(dsa_recv(), 0x0100);
}

/* (b) STOP halts playback but does NOT cancel an in-flight seek: on real
 *     hardware the drive keeps seeking and queues $0100 when it arrives.
 *     The CD BIOS boot sequence depends on it -- it sends seek, then STOP,
 *     then waits for the seek response in its main loop.  Cancelling the
 *     seek on STOP means the formatter never starts and the disc never
 *     boots.
 *
 * Negative control (verified): add `seekDelay = 0;` to the $0200 handler in
 * CDROMWriteWord -- the seek never completes and this fails on the
 * seek-done timeout. */
TEST(stop_does_not_cancel_an_inflight_seek)
{
    uint32_t dones0;
    uint16_t resp[4];
    uint32_t got = 0;
    uint32_t i;
    bool sawFound = false;
    bool sawStop  = false;

    C.CDROMReset();
    i2s_data_enable();

    dones0 = seek_dones();
    dsa_seek(TARGET_LBA);
    /* No tick in between: the seek is armed and demonstrably unfinished, so
     * STOP lands strictly mid-seek however short SEEK_DELAY_TICKS gets. */
    if (seek_dones() != dones0)
        FAIL("seek finished before STOP was sent -- cannot test STOP "
             "arriving mid-seek");

    dsa_send(0x0200);                 /* STOP, mid-seek */

    if (wait_seek_done(dones0, MAX_SEEK_TICKS) == 0xFFFFFFFFu)
        FAIL("STOP cancelled the in-flight seek -- it never completed");
    ASSERT_EQ_U32(seek_dones() - dones0, 1u);

    /* Both answers are still delivered; the RX buffer is a queue, so a
     * later command does not discard an unread earlier response. */
    for (i = 0; i < 4u; i++)
    {
        if (wait_rx_full(MAX_DSA_TICKS) == 0xFFFFFFFFu)
            break;
        resp[got++] = dsa_recv();
    }
    for (i = 0; i < got; i++)
    {
        if (resp[i] == 0x0100) sawFound = true;
        if (resp[i] == 0x0200) sawStop  = true;
    }
    if (!sawFound)
        FAIL("seek Found ($0100) was never delivered after a mid-seek STOP "
             "(%u responses seen)", got);
    if (!sawStop)
        FAIL("STOP acknowledgement ($0200) was lost (%u responses seen)", got);
}

/* (c) A seek to the block the drive is ALREADY streaming must not restart
 *     the seek state machine -- that would cycle the RX-full bit and mask
 *     the FIFO half-full bit the ISR is waiting on, and would re-frame the
 *     stream back to byte 2 mid-transfer.  But it MUST still be answered
 *     with Found: every $12xx elicits a response on hardware, and drivers
 *     that wait for the seek-complete DSA interrupt hang without it
 *     (device-traced on Philia: seeks to the LBA it is already streaming,
 *     never drains the FIFO again, freezes).
 *
 * Negative controls (both verified): delete the `DSAQueuePush(0x0100);`
 * from the redundant-seek branch -- the Found response never arrives and
 * this fails on the RX-full timeout.  Falsify the guard so every $12xx
 * arms a real seek -- the suppression assertion fails.
 *
 * NOT ASSERTED, deliberately: that the FIFO stream keeps its position
 * across the redundant seek.  It does not, today.  The $12xx handler
 * carries the same guard TWICE -- once in the response block and once in
 * the side-effect block -- and both spell it `... && dsaQueueCount == 0`.
 * The first one now ends in `DSAQueuePush(0x0100)` (the Philia fix), which
 * leaves dsaQueueCount == 1, so the SECOND guard can never match: the
 * side-effect block falls into the full-seek path and re-reads the sector,
 * resetting cdBufPtr to 2 and the SSI head to 0.  Measured here: with the
 * stream parked at byte 10 the next FIFO word came back as bytes 2..3.
 * The first guard's own comment says its purpose ("don't disturb the
 * in-flight stream, keep cdBufPtr") is preserved -- that is no longer
 * true.  Pinning it would pin the bug, so this test stops at the two
 * halves that are correct; the position invariant belongs in the PR that
 * fixes the guard. */
TEST(redundant_seek_is_suppressed_but_still_answered)
{
    uint32_t starts1;
    uint32_t dones1;
    uint32_t i;

    if (!load_ground_truth(TARGET_LBA))
        FAIL("could not read ground truth off the image");
    if (!prime_stream(TARGET_LBA))
        FAIL("could not prime the stream at LBA %u", TARGET_LBA);

    /* Four words in, so the drive is demonstrably mid-stream.  The stream
     * starts at byte 2 (capture skew), so this leaves it parked at byte 10. */
    for (i = 0; i < 4u; i++)
        (void)fifo_word();
    ASSERT_TRUE((butch_status() & ST_FRAME) != 0);

    starts1 = seek_starts();
    dones1  = seek_dones();

    dsa_seek(TARGET_LBA);                  /* same block, already playing */
    if (seek_starts() != starts1)
        FAIL("redundant seek to the block already playing restarted the "
             "seek state machine");

    if (wait_rx_full(MAX_DSA_TICKS) == 0xFFFFFFFFu)
        FAIL("redundant seek was never answered -- drivers waiting on the "
             "seek-complete response hang here");
    ASSERT_EQ_U16(dsa_recv(), 0x0100);

    /* Independent witness that no seek was armed: a restarted seek would
     * eventually produce its own SEEK_DONE.  Give it well past a seek's
     * worth of ticks and require none. */
    butch_tick(MAX_SEEK_TICKS / 10u);
    if (seek_dones() != dones1)
        FAIL("a redundant seek produced a seek completion -- the state "
             "machine was restarted after all");

    /* Position continuity -- the actual point of suppressing a redundant
     * seek.  Four words were consumed above, so the stream is parked at byte
     * 10; the next four must continue at 10/12/14/16, not restart at byte 2.
     * Before #306 this restarted: the side-effect block's guard could never
     * match (the response block's DSAQueuePush had made dsaQueueCount
     * non-zero), so it re-read the sector and rewound cdBufPtr to 2 and the
     * SSI audio head to 0 -- up to 2352 bytes of replayed CD-DA. */
    for (i = 0; i < 4u; i++)
    {
        uint32_t off  = 10u + (i * 2u);
        uint16_t got  = fifo_word();
        uint16_t want = (uint16_t)((sect[0][off + 1u] << 8) | sect[0][off]);

        if (got != want)
            FAIL("word %u after the redundant seek: got $%04X, expected $%04X "
                 "(bytes %u..%u of LBA %u).  $%04X would be bytes 2..3 -- the "
                 "redundant seek re-framed the in-flight stream (#306)",
                 i, got, want, off, off + 1u, TARGET_LBA,
                 (uint16_t)((sect[0][3] << 8) | sect[0][2]));
    }
}

/* ------------------------------------------------------------------ */
/* 5. Drive-speed latch ($15nn Set Mode)                                 */
/* ------------------------------------------------------------------ */

/* Total BUTCHExec ticks spent waiting for the FIFO to refill across
 * `cycles` drain/refill cycles, with the drive speed set by `setMode`.
 * Returns 0xFFFFFFFF (having printed why) on any anomaly. */
static uint32_t measure_refill_ticks(uint16_t setMode, uint32_t cycles)
{
    uint32_t total = 0;
    uint32_t c;
    uint16_t echo;

    C.CDROMReset();                  /* also zeroes the refill accumulator */
    i2s_data_enable();

    dsa_send(setMode);
    if (wait_rx_full(MAX_DSA_TICKS) == 0xFFFFFFFFu)
    {
        fprintf(stderr, "        (Set Mode $%04X was never answered)\n", setMode);
        return 0xFFFFFFFFu;
    }
    echo = dsa_recv();
    if (echo != (uint16_t)(0x1700u | (setMode & 0xFFu)))
    {
        fprintf(stderr, "        (Set Mode $%04X echoed $%04X)\n", setMode, echo);
        return 0xFFFFFFFFu;
    }

    {
        uint32_t before = seek_dones();
        dsa_seek(TARGET_LBA);
        if (wait_seek_done(before, MAX_SEEK_TICKS) == 0xFFFFFFFFu)
        {
            fprintf(stderr, "        (seek never completed)\n");
            return 0xFFFFFFFFu;
        }
        if (wait_rx_full(MAX_DSA_TICKS) == 0xFFFFFFFFu)
        {
            fprintf(stderr, "        (Found response never arrived)\n");
            return 0xFFFFFFFFu;
        }
        (void)dsa_recv();
    }

    for (c = 0; c < cycles; c++)
    {
        uint32_t reads = 0;
        uint32_t ticks = 0;

        while (butch_status() & ST_FIFO_HALF)
        {
            (void)fifo_word();
            if (++reads > MAX_DRAIN_READS)
            {
                fprintf(stderr, "        (cycle %u never drained)\n", c);
                return 0xFFFFFFFFu;
            }
        }
        while (!(butch_status() & ST_FIFO_HALF))
        {
            p_butch_exec(0);
            if (++ticks > MAX_REFILL_TICKS)
            {
                fprintf(stderr, "        (cycle %u never refilled)\n", c);
                return 0xFFFFFFFFu;
            }
        }
        total += ticks;
    }

    /* If the drive stopped playing partway the refill loop takes its
     * one-tick retry path and the counts are meaningless. */
    if (!(butch_status() & ST_FRAME))
    {
        fprintf(stderr, "        (drive stopped playing during the window)\n");
        return 0xFFFFFFFFu;
    }
    return total;
}

/* The $15nn payload's low bits are a ONE-BASED SPEED CODE (1 = single,
 * 2 = double), not a speed bit; bit 3 separately selects data(1)/audio(0).
 * The two payloads the retail CD BIOS emits in data mode are $1509
 * (single) and $150A (double) -- note $150A has bit 0 CLEAR and is the
 * FAST one, so reading bit 0 as "the speed bit" inverts them.
 *
 * The observable is FIFO refill pacing: single speed streams at half the
 * byte rate, so a fixed number of drain/refill cycles must cost exactly
 * twice as many ticks.  Measured over exactly 100 cycles, which makes the
 * error-diffused fractional period land back on a whole number for both
 * speeds -- so the 2:1 result holds whatever the period constant is, and
 * this test does not pin that constant.
 *
 * Negative controls (both verified): make the $15nn handler ignore `code`
 * and leave cdDriveSpeed alone -- both measurements come out at 285 ticks
 * and the exact-2x assertion fails.  Decode the speed from bit 0 instead
 * (`code = (data & 1) ? DOUBLE : SINGLE`) -- the measurements invert to
 * single 285 / double 570 and it fails the other way.  Changing
 * FIFO_REFILL_PERIOD_X100 (verified at 437 instead of 285) leaves this
 * green: only the ratio is pinned. */
TEST(set_mode_latches_drive_speed)
{
    uint32_t cycles = 100u;
    uint32_t dbl;
    uint32_t single;

    dbl = measure_refill_ticks(0x150A, cycles);      /* double / data */
    if (dbl == 0xFFFFFFFFu)
        FAIL("double-speed measurement did not complete");
    single = measure_refill_ticks(0x1509, cycles);   /* single / data */
    if (single == 0xFFFFFFFFu)
        FAIL("single-speed measurement did not complete");

    if (dbl < cycles)
        FAIL("double speed: %u ticks over %u refills -- the FIFO is not "
             "drive-paced at all", dbl, cycles);
    if (single != dbl * 2u)
        FAIL("single speed cost %u ticks over %u refills, double speed %u: "
             "expected exactly 2x.  $1509 must select SINGLE and $150A "
             "DOUBLE (one-based code in the low bits, not bit 0)",
             single, cycles, dbl);
}

/* ------------------------------------------------------------------ */

static bool resolve_symbols(void)
{
    p_open_image   = (bool (*)(const char *))dlsym(C.handle, "CDIntfOpenImage");
    p_close_image  = (void (*)(void))dlsym(C.handle, "CDIntfCloseImage");
    p_read_block   = (bool (*)(uint32_t, uint8_t *))dlsym(C.handle, "CDIntfReadBlock");
    p_num_sessions = (uint32_t (*)(void))dlsym(C.handle, "CDIntfGetNumSessions");
    p_disc_sectors = (uint32_t (*)(void))dlsym(C.handle, "CDIntfGetDiscTotalSectors");
    p_butch_exec   = (void (*)(uint32_t))dlsym(C.handle, "BUTCHExec");
    p_seek_state   = (void (*)(uint32_t *, uint32_t *, uint32_t *))
                     dlsym(C.handle, "CDROMDiagGetSeekWedgeState");

    return p_open_image && p_close_image && p_read_block && p_num_sessions &&
           p_disc_sectors && p_butch_exec && p_seek_state &&
           C.CDROMInit && C.CDROMReset && C.CDROMReadWord && C.CDROMWriteWord;
}

int main(int argc, char *argv[])
{
    char base[512];
    char cue[1024];
    int rc = 1;
    bool opened = false;

    (void)argc; (void)argv;

    TEST_INIT("CD real-BIOS BUTCH/DSA/FIFO path (synthetic disc)");

    if (!vj_core_load(&C))
    {
        fprintf(stderr, "FATAL: failed to load core\n");
        return 1;
    }
    vj_core_init(&C);

    if (!resolve_symbols())
    {
        fprintf(stderr, "  FAIL  missing test exports "
                        "(build with `make TEST_EXPORTS=1`)\n");
        vj_core_unload(&C);
        return 1;
    }

    /* Deterministic per-process scratch dir (mkdtemp sits behind different
     * feature-test macros on Darwin vs glibc, and per-process is unique
     * enough).  Everything below is synthetic -- no test/roms/private. */
    snprintf(base, sizeof(base), "/tmp/vj_cd_butch_%ld", (long)getpid());
    if (mkdir(base, 0755) != 0 && errno != EEXIST)
    {
        fprintf(stderr, "  SKIP  cannot create scratch dir %s\n", base);
        vj_core_unload(&C);
        return 0;
    }
    if (!make_disc(base, cue, sizeof(cue)))
    {
        fprintf(stderr, "  SKIP  cannot write synthetic disc under %s\n", base);
        goto out;
    }

    if (!p_open_image(cue))
    {
        fprintf(stderr, "  FAIL  synthetic disc did not load\n");
        goto out;
    }
    opened = true;

    /* CDROMInit latches haveCDGoodness from the now-open image; without it
     * every path under test silently no-ops. */
    C.CDROMInit();
    C.CDROMReset();

    fprintf(stderr, "synthetic disc: %u sectors, %u session(s), target LBA %u\n",
            p_disc_sectors(), p_num_sessions(), TARGET_LBA);

    RUN_TEST(preflight_disc_and_butch_are_live);
    RUN_TEST(fifo_stream_matches_disc_bytes_at_seek_lba);
    RUN_TEST(fifo_half_full_drain_and_refill_accounting);
    RUN_TEST(dsa_responses_are_delayed_and_correct);
    RUN_TEST(seek_start_and_done_accounting);
    RUN_TEST(stop_does_not_cancel_an_inflight_seek);
    RUN_TEST(redundant_seek_is_suppressed_but_still_answered);
    RUN_TEST(set_mode_latches_drive_speed);

    rc = TEST_REPORT();

out:
    if (opened)
        p_close_image();
    vj_core_unload(&C);
    scrub_scratch(base);
    return rc;
}
