/*
 * test_cd_synth_cdda.c -- CD-DA (Red Book audio) delivery over the SSI,
 * pinned against a SYNTHETIC multi-track audio disc.
 *
 * test/test_cd_ssi_stream.c already covers this contract, but it SKIPs
 * unless VJ_SSI_DISC points at a commercial image under
 * test/roms/private -- gitignored, absent in CI and on a fresh clone -- so
 * in practice nothing guards the CD-audio path.  This test builds its own
 * disc in a scratch dir and therefore always runs.
 *
 * THE DISC IS THE ORACLE
 * ----------------------
 * The PCM is generated from a closed-form integer phase accumulator, so
 * the decoded AUDIO ITSELF says what is playing and where -- the audio
 * analogue of test_cd_synth_butch.c's disc_pat() byte pattern:
 *
 *   - Base frequency encodes the TRACK.  Track t starts at 882*t Hz, and
 *     a zero-crossing count over any window identifies t on its own.
 *   - A linear chirp inside each track (+441 Hz over its 2 s) encodes the
 *     POSITION within the track: instantaneous frequency rises
 *     monotonically, so a rewound or re-framed head shows up as a
 *     frequency that goes backwards -- in the audio domain, without
 *     needing to know any LBA.
 *   - LEFT and RIGHT differ: right is the second harmonic at a different
 *     amplitude.  A channel swap is invisible on a mono disc and glaring
 *     here.  (This repo has real SSI channel/alignment history.)
 *   - Amplitude is a known constant (square wave, never zero), so
 *     "silence where there should be tone" is unambiguous.
 *   - The only silence on the disc is the 150-sector INDEX 00 pregap in
 *     front of tracks 2..4.  Silence anywhere else is a failure.
 *
 * The disc is a MULTI-FILE CUE (one BIN per track) with real pregaps --
 * the exact shape every CUE in the Jaguar CD corpus uses.  That makes
 * dataLBA != startLBA, so the loader's pregap handling is under test
 * rather than bypassed (a single-file no-pregap disc has them equal and
 * hides that whole class).
 *
 * INVARIANTS PINNED (each with a documented negative control)
 * ----------------------------------------------------------
 *   1. Channel order and sample framing -- LRXD carries the LEFT channel
 *      (little-endian bytes [4i+0..1]) and RRXD the RIGHT ([4i+2..3]),
 *      sample-aligned from byte 0 of the seek target sector, crossing
 *      sector boundaries linearly.
 *      NEGATIVE CONTROL: swap the two reads in
 *      SetSSIWordsXmittedFromButch() (src/cd/cdrom.c) -> test 2 fails.
 *   2. Track identity from the audio -- a seek to track t's INDEX 01
 *      delivers audio whose measured frequency is in track t's band and
 *      in no other track's.
 *      NEGATIVE CONTROL: make CDIntfGetTrackInfo() use startLBA instead
 *      of dataLBA (src/cd/cdintf.c), i.e. drop the pregap from the TOC ->
 *      the seek lands 150 sectors early, in silence -> test 3 fails.
 *   3. Stream continuity across a redundant seek -- re-issuing the seek
 *      the drive is already serving must not rewind the SSI head.  The
 *      chirp makes this an audio-domain assertion: instantaneous
 *      frequency never goes backwards.  (Regression guard for #306/#307,
 *      where a redundant seek replayed up to ~13 ms of CD-DA.)
 *      NEGATIVE CONTROL: restore `ssiBufPtr = 0;` on the redundant-seek
 *      branch of the $12xx handler (src/cd/cdrom.c) -> test 4 fails.
 *   4. Pregap is silence and the tone starts exactly at INDEX 01 --
 *      seeking two sectors before track t's data start yields exactly
 *      1176 zero samples and then phase 0 of track t.
 *      NEGATIVE CONTROL: same dataLBA/startLBA sabotage as 2 -> test 5
 *      fails.
 *
 * No timing constant is asserted; every timing claim is relational.
 *
 * Build: make test/test_cd_synth_cdda      (needs TEST_EXPORTS=1)
 * Run:   DYLD_LIBRARY_PATH=. test/test_cd_synth_cdda
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
static uint32_t (*p_num_tracks)(void);
static uint8_t  (*p_track_info)(uint32_t, uint32_t);
static void     (*p_butch_exec)(uint32_t);
static void     (*p_seek_state)(uint32_t *, uint32_t *, uint32_t *);
static void     (*p_ssi_xmit)(void);
static uint16_t *p_lrxd;
static uint16_t *p_rrxd;

/* ------------------------------------------------------------------ */
/* BUTCH register offsets (CDROMReadWord/CDROMWriteWord mask to & $FF)   */
/* ------------------------------------------------------------------ */

#define R_BUTCH_LO    0x02u
#define R_DS_DATA     0x0Au
#define R_I2CNTRL_LO  0x12u

#define ST_RX_FULL       0x2000u
#define I2S_DATA_ENABLE  0x0001u

#define CALLER 0u                   /* `who` argument -- UNKNOWN; the enum in
                                     * vjag_memory.h is { UNKNOWN, JAGUAR, DSP,
                                     * GPU, TOM, JERRY, M68K, ... }, so 0 is
                                     * UNKNOWN and M68K would be 6.           */

/* ------------------------------------------------------------------ */
/* Synthetic disc geometry                                              */
/* ------------------------------------------------------------------ */

#define SECTOR_BYTES      2352u
#define SAMPLES_PER_SECT  (SECTOR_BYTES / 4u)      /* 588 stereo frames    */
#define NUM_TRACKS        4u
#define AUDIO_SECTORS     150u                     /* 2 s per track        */
#define PREGAP_SECTORS    150u                     /* 2 s, Red Book gap    */
#define TRACK_SAMPLES     (AUDIO_SECTORS * SAMPLES_PER_SECT)   /* 88200    */

/* Bounds: generous enough that a legitimate constant change cannot trip
 * them, tight enough that a hang is reported instead of spinning. */
#define MAX_SEEK_TICKS   200000u
#define MAX_DSA_TICKS      4096u

/* ------------------------------------------------------------------ */
/* Deterministic PCM: a per-track chirp                                 */
/* ------------------------------------------------------------------ */

/* Phase increment per sample for 882.0 Hz at 44100 Hz, in 32-bit phase
 * units: 882/44100 * 2^32 = 0.02 * 2^32.  Track t starts at 882*t Hz. */
#define BASE_INC     85899346u
/* Chirp rate: +441 Hz spread over one track's 88200 samples.  Track t
 * therefore sweeps 882*t -> 882*t + 441 Hz, leaving a 441 Hz gap between
 * adjacent tracks' bands -- wide enough that a windowed frequency
 * measurement identifies the track with no ambiguity. */
#define CHIRP_DINC   487u

#define AMP_LEFT     20000
#define AMP_RIGHT    12000

/* Phase after `n` samples of track `track` (1-based).
 *
 *   phase(n) = n*inc0 + DINC * (n*(n-1)/2)      (mod 2^32)
 *
 * Closed form, so any sample is reproducible without generating the ones
 * before it.  All arithmetic is uint32 and wrapping is intended. */
static uint32_t pcm_phase(uint32_t track, uint32_t n)
{
    uint32_t inc0 = track * BASE_INC;
    uint32_t tri;

    /* n*(n-1)/2 without overflowing before the (intended) mod-2^32 wrap:
     * exactly one of n, n-1 is even, so halve that one first. */
    if (n & 1u)
        tri = ((n - 1u) / 2u) * n;
    else
        tri = (n / 2u) * (n - 1u);

    return n * inc0 + CHIRP_DINC * tri;
}

/* Sample `n` of track `track`.  Square waves: the amplitude is an exact
 * known constant and the value is never zero, so a zero sample always
 * means "no disc audio here". */
static void pcm_sample(uint32_t track, uint32_t n, int16_t *l, int16_t *r)
{
    uint32_t ph = pcm_phase(track, n);

    *l = (ph & 0x80000000u) ? (int16_t)(-AMP_LEFT) : (int16_t)AMP_LEFT;
    /* Right channel is the second harmonic (phase doubled) at a different
     * amplitude -- L and R never agree in both sign pattern and level. */
    *r = (ph & 0x40000000u) ? (int16_t)(-AMP_RIGHT) : (int16_t)AMP_RIGHT;
}

/* ------------------------------------------------------------------ */
/* Synthetic disc construction                                          */
/* ------------------------------------------------------------------ */

static void track_bin_name(char *out, size_t outLen, const char *dir,
                           uint32_t track)
{
    snprintf(out, outLen, "%s/track%02u.bin", dir, (unsigned)track);
}

static bool write_track_bin(const char *dir, uint32_t track)
{
    char path[1024];
    uint8_t *bin = NULL;
    uint32_t pregap = (track == 1u) ? 0u : PREGAP_SECTORS;
    size_t binLen = (size_t)(pregap + AUDIO_SECTORS) * SECTOR_BYTES;
    size_t gapLen = (size_t)pregap * SECTOR_BYTES;
    uint32_t n;
    size_t written;
    FILE *f;
    bool ok = false;

    bin = (uint8_t *)calloc(1, binLen);
    if (!bin)
        return false;

    /* [0, gapLen) stays zero: the INDEX 00 pregap.  Audio follows. */
    for (n = 0; n < TRACK_SAMPLES; n++)
    {
        size_t o = gapLen + (size_t)n * 4u;
        int16_t l, r;

        pcm_sample(track, n, &l, &r);
        bin[o + 0] = (uint8_t)((uint16_t)l & 0xFFu);
        bin[o + 1] = (uint8_t)(((uint16_t)l >> 8) & 0xFFu);
        bin[o + 2] = (uint8_t)((uint16_t)r & 0xFFu);
        bin[o + 3] = (uint8_t)(((uint16_t)r >> 8) & 0xFFu);
    }

    track_bin_name(path, sizeof(path), dir, track);
    f = fopen(path, "wb");
    if (!f)
        goto done;
    written = fwrite(bin, 1, binLen, f);
    fclose(f);
    ok = (written == binLen);

done:
    free(bin);
    return ok;
}

static bool make_disc(const char *dir, char *cueOut, size_t cueOutLen)
{
    uint32_t t;
    FILE *f;

    for (t = 1u; t <= NUM_TRACKS; t++)
        if (!write_track_bin(dir, t))
            return false;

    snprintf(cueOut, cueOutLen, "%s/musiccd.cue", dir);
    f = fopen(cueOut, "wb");
    if (!f)
        return false;

    /* Multi-file CUE, file-relative INDEX MSF -- the corpus shape.  Tracks
     * 2..4 carry a real 150-sector (00:02:00) INDEX 00 pregap. */
    fprintf(f, "REM SESSION 01\n");
    for (t = 1u; t <= NUM_TRACKS; t++)
    {
        fprintf(f, "FILE \"track%02u.bin\" BINARY\n", (unsigned)t);
        fprintf(f, "  TRACK %02u AUDIO\n", (unsigned)t);
        if (t == 1u)
            fprintf(f, "    INDEX 01 00:00:00\n");
        else
            fprintf(f, "    INDEX 00 00:00:00\n"
                       "    INDEX 01 00:02:00\n");
    }
    fclose(f);
    return true;
}

static void scrub_scratch(const char *base)
{
    char path[1024];
    uint32_t t;

    for (t = 1u; t <= NUM_TRACKS; t++)
    {
        track_bin_name(path, sizeof(path), base, t);
        remove(path);
    }
    snprintf(path, sizeof(path), "%s/musiccd.cue", base);
    remove(path);
    rmdir(base);
}

/* ------------------------------------------------------------------ */
/* BUTCH driving helpers -- these are the CD BIOS's moves                */
/* ------------------------------------------------------------------ */

static uint16_t butch_status(void)
{
    return C.CDROMReadWord(R_BUTCH_LO, CALLER);
}

static void dsa_send(uint16_t cmd)
{
    C.CDROMWriteWord(R_DS_DATA, cmd, CALLER);
}

static uint16_t dsa_recv(void)
{
    return C.CDROMReadWord(R_DS_DATA, CALLER);
}

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

/* Reset + I2S enable + seek to `lba` + consume the Found response, leaving
 * the drive playing with the SSI head parked at byte 0 of `lba`. */
static bool prime_stream(uint32_t lba)
{
    uint32_t before;
    uint16_t r;

    C.CDROMReset();
    C.CDROMWriteWord(R_I2CNTRL_LO, I2S_DATA_ENABLE, CALLER);

    before = seek_dones();
    dsa_seek(lba);
    if (wait_seek_done(before, MAX_SEEK_TICKS) == 0xFFFFFFFFu)
    {
        fprintf(stderr, "        (seek to LBA %u never completed)\n", lba);
        return false;
    }
    if (wait_rx_full(MAX_DSA_TICKS) == 0xFFFFFFFFu)
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

/* One SSI sample pair, exactly as JERRY's slave-mode I2S callback takes
 * it: SetSSIWordsXmittedFromButch() advances the head and latches
 * LRXD/RRXD, which the DSP then reads. */
static void ssi_next(int16_t *l, int16_t *r)
{
    p_ssi_xmit();
    *l = (int16_t)*p_lrxd;
    *r = (int16_t)*p_rrxd;
}

/* ------------------------------------------------------------------ */
/* Frequency measurement -- the audio-domain oracle                     */
/* ------------------------------------------------------------------ */

/* Sign changes over `n` samples.  For a square wave that is exactly two
 * per cycle, so freq_hz = crossings * 44100 / (2 * n). */
static uint32_t count_crossings(const int16_t *buf, uint32_t n)
{
    uint32_t i, c = 0;

    for (i = 1; i < n; i++)
        if ((buf[i] < 0) != (buf[i - 1] < 0))
            c++;
    return c;
}

static uint32_t crossings_to_hz(uint32_t crossings, uint32_t n)
{
    if (n < 2u)
        return 0;
    return (crossings * 44100u) / (2u * n);
}

/* Track t occupies [882*t, 882*t + 442] Hz; the next band starts 441 Hz
 * above the top of this one. */
static uint32_t track_lo_hz(uint32_t t) { return 882u * t; }
static uint32_t track_hi_hz(uint32_t t) { return 882u * t + 442u; }

/* Disc LBA of track `t`'s INDEX 01, read back through the same TOC path
 * the CD BIOS uses (absolute MSF, 150-frame lead-in included). */
static uint32_t track_data_lba(uint32_t t)
{
    uint32_t m = p_track_info(t, 0);
    uint32_t s = p_track_info(t, 1);
    uint32_t f = p_track_info(t, 2);
    uint32_t abs = ((m * 60u) + s) * 75u + f;

    return (abs >= 150u) ? (abs - 150u) : 0u;
}

/* ------------------------------------------------------------------ */
/* 1. Preflight: the disc loaded, and its geometry is what we wrote      */
/* ------------------------------------------------------------------ */

/* Everything below silently no-ops if haveCDGoodness is false, so a setup
 * failure would look like a behaviour failure.  This test fails first and
 * unambiguously instead.  It also pins the pregap arithmetic the seek
 * targets depend on: track 1 at LBA 0, every later track 150 sectors past
 * the end of the previous one. */
TEST(preflight_disc_geometry_and_pregaps)
{
    uint32_t t;
    uint32_t expect = 0;

    if (p_num_tracks() != NUM_TRACKS)
        FAIL("disc reports %u tracks, expected %u",
             p_num_tracks(), NUM_TRACKS);

    for (t = 1u; t <= NUM_TRACKS; t++)
    {
        uint32_t lba;

        if (t > 1u)
            expect += PREGAP_SECTORS;
        lba = track_data_lba(t);
        if (lba != expect)
            FAIL("track %u INDEX 01 at LBA %u, expected %u",
                 t, lba, expect);
        expect += AUDIO_SECTORS;
    }
}

/* ------------------------------------------------------------------ */
/* 2. Channel order and sample framing                                  */
/* ------------------------------------------------------------------ */

/* The SSI head is sample-aligned from byte 0 of the seek target sector
 * (unlike the BUTCH FIFO data path, which starts one word in), LRXD is
 * the LEFT channel and RRXD the RIGHT, and the stream crosses sector
 * boundaries linearly.  Runs past 2 sectors' worth of samples so the
 * refill path is covered, on a track whose left and right channels differ
 * in both sign pattern and amplitude. */
TEST(ssi_delivers_left_in_lrxd_right_in_rrxd)
{
    const uint32_t COUNT = SAMPLES_PER_SECT * 2u + 17u;
    uint32_t track = 3u;
    uint32_t n;

    if (!prime_stream(track_data_lba(track)))
        FAIL("could not prime the stream at track %u", track);

    for (n = 0; n < COUNT; n++)
    {
        int16_t gotL, gotR, wantL, wantR;

        ssi_next(&gotL, &gotR);
        pcm_sample(track, n, &wantL, &wantR);

        if (gotL != wantL || gotR != wantR)
            FAIL("sample %u: got L=%d R=%d, expected L=%d R=%d "
                 "(swapped channels would read L=%d R=%d)",
                 n, gotL, gotR, wantL, wantR, wantR, wantL);
    }

    /* The disc must actually distinguish the channels, or the check above
     * cannot fail on a swap.  The amplitudes differ by construction, so
     * this only guards against someone equalising them later. */
    {
        int16_t l, r;

        pcm_sample(track, 0u, &l, &r);
        if (l == r)
            FAIL("disc is mono at sample 0 -- channel assertions are vacuous");
    }
}

/* ------------------------------------------------------------------ */
/* 3. Track identity, measured from the audio alone                     */
/* ------------------------------------------------------------------ */

/* Seeking to track t's INDEX 01 delivers audio in track t's frequency
 * band and in no other track's.  Nothing here looks at an LBA or a disc
 * byte: the decoded audio is the whole oracle, which is what makes it
 * able to catch a seek that lands on the wrong track or inside a pregap.
 * The right channel is checked to be the second harmonic, so a channel
 * swap is caught in the frequency domain too. */
TEST(audio_frequency_identifies_the_seeked_track)
{
    static int16_t bufL[4096];
    static int16_t bufR[4096];
    uint32_t track;

    for (track = 1u; track <= NUM_TRACKS; track++)
    {
        uint32_t n, hzL, hzR, other;

        if (!prime_stream(track_data_lba(track)))
            FAIL("could not prime the stream at track %u", track);

        for (n = 0; n < 4096u; n++)
            ssi_next(&bufL[n], &bufR[n]);

        hzL = crossings_to_hz(count_crossings(bufL, 4096u), 4096u);
        hzR = crossings_to_hz(count_crossings(bufR, 4096u), 4096u);

        if (hzL < track_lo_hz(track) || hzL > track_hi_hz(track))
            FAIL("track %u: measured %u Hz, outside its band %u..%u Hz",
                 track, hzL, track_lo_hz(track), track_hi_hz(track));

        for (other = 1u; other <= NUM_TRACKS; other++)
            if (other != track &&
                hzL >= track_lo_hz(other) && hzL <= track_hi_hz(other))
                FAIL("track %u: measured %u Hz, which is also track %u's band",
                     track, hzL, other);

        /* Right channel is the second harmonic: twice the crossings.
         * The window quantises to 44100/(2*4096) = 5.4 Hz, and that error
         * is doubled by the comparison, so allow 16 Hz. */
        if (hzR < 2u * hzL - 16u || hzR > 2u * hzL + 16u)
            FAIL("track %u: right channel %u Hz, expected ~%u Hz "
                 "(2x left) -- channels swapped or mis-framed",
                 track, hzR, 2u * hzL);
    }
}

/* ------------------------------------------------------------------ */
/* 4. Continuity across a redundant seek (#306 / #307)                  */
/* ------------------------------------------------------------------ */

/* Re-issuing the seek the drive is already serving must not rewind the
 * SSI head.  Before the #307 fix a redundant $12xx reloaded the sector
 * buffer and reset ssiBufPtr to 0, replaying up to a sector (~13 ms) of
 * CD-DA.
 *
 * Two independent assertions:
 *   (a) audio-domain -- the chirp's instantaneous frequency never goes
 *       backwards across the seek, which is true of a continuous stream
 *       and false of any rewind, without reference to an LBA;
 *   (b) exact -- the samples after the seek are the samples that were
 *       next, not ones already delivered. */
TEST(redundant_seek_does_not_rewind_the_audio_head)
{
    /* 8 windows of 8192 samples = 65536 samples, 1.49 s -- inside a
     * 2 s track.  The window is large because the frequency measurement
     * quantises to 44100/(2*8192) = 2.7 Hz; over one window the chirp
     * climbs 8192 * 441/88200 = 41 Hz, comfortably above it. */
#define RW_WINDOWS  8u
#define RW_SAMPLES  8192u
    static int16_t win[RW_WINDOWS][RW_SAMPLES];
    const uint32_t track = 2u;
    uint32_t delivered = 0;
    uint32_t w, n;
    uint32_t target;
    uint32_t firstHz, lastHz, rise;

    target = track_data_lba(track);

    if (!prime_stream(target))
        FAIL("could not prime the stream at track %u", track);

    for (w = 0; w < RW_WINDOWS; w++)
    {
        /* Halfway through, re-issue the identical seek while the stream
         * is running -- the redundant-seek path. */
        if (w == RW_WINDOWS / 2u)
        {
            dsa_seek(target);
            /* The drive still answers a no-op Goto with Found; drain it so
             * the DSA queue does not stay armed. */
            if (wait_rx_full(MAX_DSA_TICKS) != 0xFFFFFFFFu)
                (void)dsa_recv();
        }

        for (n = 0; n < RW_SAMPLES; n++)
        {
            int16_t gotL, gotR, wantL, wantR;

            ssi_next(&gotL, &gotR);
            pcm_sample(track, delivered, &wantL, &wantR);
            /* The sharp assertion: any rewind, of any size down to a
             * single sample, lands here immediately. */
            if (gotL != wantL || gotR != wantR)
                FAIL("window %u sample %u (stream position %u): got L=%d R=%d, "
                     "expected L=%d R=%d -- the head moved",
                     w, n, delivered, gotL, gotR, wantL, wantR);
            win[w][n] = gotL;
            delivered++;
        }
    }

    /* The audio-domain assertion, independent of any LBA or disc byte:
     * the chirp must still be climbing at its known rate after the
     * redundant seek.  This is coarser than the sample check above -- it
     * cannot resolve a rewind smaller than about a window -- but it holds
     * even if the sample values themselves were to change, and it is what
     * a listener would hear. */
    firstHz = crossings_to_hz(count_crossings(win[0], RW_SAMPLES), RW_SAMPLES);
    lastHz  = crossings_to_hz(count_crossings(win[RW_WINDOWS - 1u], RW_SAMPLES),
                              RW_SAMPLES);
    if (lastHz <= firstHz)
        FAIL("chirp did not advance: %u Hz at the start, %u Hz after %u "
             "samples -- the audio head stalled or rewound",
             firstHz, lastHz, delivered);

    /* Expected climb across (RW_WINDOWS-1) windows at 441 Hz per 88200
     * samples; allow a generous band around it so a legitimate change to
     * the chirp constants does not have to move a magic number. */
    rise = lastHz - firstHz;
    {
        uint32_t want = ((RW_WINDOWS - 1u) * RW_SAMPLES * 441u) / TRACK_SAMPLES;

        if (rise < want / 2u || rise > want * 2u)
            FAIL("chirp climbed %u Hz over %u samples, expected about %u Hz "
                 "-- the stream is not advancing at the drive rate",
                 rise, (RW_WINDOWS - 1u) * RW_SAMPLES, want);
    }
#undef RW_WINDOWS
#undef RW_SAMPLES
}

/* ------------------------------------------------------------------ */
/* 5. The pregap is silence, and the tone starts exactly at INDEX 01     */
/* ------------------------------------------------------------------ */

/* Seeking two sectors before track t's data start must deliver exactly
 * 2*588 zero samples -- the only silence anywhere on this disc -- and
 * then phase 0 of track t.  A one-sector or one-sample framing error in
 * the pregap arithmetic moves that boundary and fails here. */
TEST(pregap_is_silent_and_tone_starts_at_index01)
{
    const uint32_t track = 4u;
    const uint32_t lead = 2u;              /* sectors of pregap to read    */
    const uint32_t silent = lead * SAMPLES_PER_SECT;
    uint32_t n;

    if (!prime_stream(track_data_lba(track) - lead))
        FAIL("could not prime the stream in track %u's pregap", track);

    for (n = 0; n < silent; n++)
    {
        int16_t l, r;

        ssi_next(&l, &r);
        if (l != 0 || r != 0)
            FAIL("pregap sample %u is L=%d R=%d, expected silence "
                 "(head landed inside the audio -- pregap arithmetic)",
                 n, l, r);
    }

    for (n = 0; n < 512u; n++)
    {
        int16_t gotL, gotR, wantL, wantR;

        ssi_next(&gotL, &gotR);
        pcm_sample(track, n, &wantL, &wantR);
        if (gotL != wantL || gotR != wantR)
            FAIL("sample %u after the pregap: got L=%d R=%d, expected "
                 "L=%d R=%d -- the tone did not start at INDEX 01",
                 n, gotL, gotR, wantL, wantR);
    }
}

/* ------------------------------------------------------------------ */
/* Symbol resolution                                                    */
/* ------------------------------------------------------------------ */

static bool resolve_symbols(void)
{
    p_open_image  = (bool (*)(const char *))     dlsym(C.handle, "CDIntfOpenImage");
    p_close_image = (void (*)(void))             dlsym(C.handle, "CDIntfCloseImage");
    p_num_tracks  = (uint32_t (*)(void))         dlsym(C.handle, "CDIntfGetNumTracks");
    p_track_info  = (uint8_t (*)(uint32_t, uint32_t))
                                                 dlsym(C.handle, "CDIntfGetTrackInfo");
    p_butch_exec  = (void (*)(uint32_t))         dlsym(C.handle, "BUTCHExec");
    p_seek_state  = (void (*)(uint32_t *, uint32_t *, uint32_t *))
                                    dlsym(C.handle, "CDROMDiagGetSeekWedgeState");
    p_ssi_xmit    = (void (*)(void)) dlsym(C.handle, "SetSSIWordsXmittedFromButch");
    p_lrxd        = (uint16_t *)     dlsym(C.handle, "lrxd");
    p_rrxd        = (uint16_t *)     dlsym(C.handle, "rrxd");

    return p_open_image && p_close_image && p_num_tracks && p_track_info &&
           p_butch_exec && p_seek_state && p_ssi_xmit && p_lrxd && p_rrxd &&
           C.CDROMInit && C.CDROMReset && C.CDROMReadWord && C.CDROMWriteWord;
}

int main(int argc, char *argv[])
{
    char base[512];
    char cue[1024];
    int rc = 1;
    bool opened = false;

    (void)argc; (void)argv;

    TEST_INIT("CD-DA delivery over the SSI (synthetic multi-track disc)");

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

    /* Deterministic per-process scratch dir.  Everything below is
     * synthetic -- this test never touches test/roms/private.
     *
     * Deliberately mkdir()+getpid() rather than mkdtemp(): mkdtemp is not
     * declared under -std=c99 on glibc without a feature-test macro, and
     * building it that way fails on Linux/Clang and the ASan job with
     * "call to undeclared function 'mkdtemp'".  A predictable name is fine
     * here -- this is a scratch dir for a synthetic disc, not a security
     * boundary, and the pid already makes it per-process unique. */
    snprintf(base, sizeof(base), "/tmp/vj_cd_cdda_%ld", (long)getpid());
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

    fprintf(stderr, "synthetic audio disc: %u tracks, %u pregap + %u audio "
                    "sectors each, track t chirps %u..%u Hz\n",
            NUM_TRACKS, PREGAP_SECTORS, AUDIO_SECTORS,
            track_lo_hz(1u), track_hi_hz(NUM_TRACKS));

    RUN_TEST(preflight_disc_geometry_and_pregaps);
    RUN_TEST(ssi_delivers_left_in_lrxd_right_in_rrxd);
    RUN_TEST(audio_frequency_identifies_the_seeked_track);
    RUN_TEST(redundant_seek_does_not_rewind_the_audio_head);
    RUN_TEST(pregap_is_silent_and_tone_starts_at_index01);

    rc = TEST_REPORT();

out:
    if (opened)
        p_close_image();
    vj_core_unload(&C);
    scrub_scratch(base);
    return rc;
}
