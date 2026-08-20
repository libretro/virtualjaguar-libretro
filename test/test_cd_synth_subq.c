/*
 * test_cd_synth_subq.c -- BUTCH Q-subcode serial window (SUBDATA), pinned
 * against a SYNTHETIC audio disc.  Issue #291 (VLM stays muted).
 *
 * THE CONSUMER IS THE ORACLE
 * --------------------------
 * The contract asserted here is not an invention of this emulator: it was
 * recovered at runtime from the retail CD BIOS VLM's own DSP subcode
 * handler ($F1BD42..$F1BE02, docs/vlm-audio-cd-plan.md section 8.8).  That
 * handler polls the low word of SUBDATA ($DFFF1A) and requires
 *
 *     bits 15..8  the current Q-channel byte
 *     bit  4      "valid"
 *     bits  3..0  byte sequence number 0..11 within the Q frame
 *
 * assembles the 12 bytes in sequence order, CRCs ALL 12 with
 * CRC-16/CCITT (poly $1021, init 0) and requires the residual $1D0F --
 * the standard residue of a Red Book Q frame whose stored CRC is
 * inverted.  Bit 30 of the first Q word (CONTROL bit 2, "data track") is
 * the VLM's mute gate: $40000000 at VLM init means "assume data, stay
 * muted", and only a CRC-valid audio-control frame clears it.  Every
 * assertion below is that contract, re-implemented independently.
 *
 * INVARIANTS PINNED (each with a documented negative control)
 * ----------------------------------------------------------
 *   1. Disarmed silence -- without an SBCNTRL arm write, SUBDATA reads
 *      return the RAM-backed $0000 they always did (pre-#291 behavior;
 *      guards every data-disc title, none of which arms subcode).
 *      NEGATIVE CONTROL: drop `subQArmed` from the read gate in
 *      CDROMReadWord() (src/cd/cdrom.c) -> test 1 fails.
 *   2. Word format and pacing -- armed and playing, every read shows
 *      bit 4 set, a sequence number that steps 0..11 cyclically, and
 *      exactly 49 streamed samples per byte (588 samples per sector /
 *      12 bytes, so Q stays locked to the audio).
 *      NEGATIVE CONTROL: serve `subQSeq` without the $10 valid bit ->
 *      tests 2 and 3 fail.
 *   3. Frame content -- an aligned 12-byte frame is a mode-1 (ADR 1)
 *      position frame, all BCD: audio CONTROL nibble (bit "data track"
 *      CLEAR -- the unmute bit), the seeked track's number, INDEX 01,
 *      zero relative time at the track start, absolute time = LBA+150,
 *      and a CRC that yields the $1D0F residual the DSP demands.
 *      NEGATIVE CONTROL: store the CRC non-inverted in CDROMBuildSubQ()
 *      -> test 3 fails (residual becomes $0000, not $1D0F).
 *   4. Pregap -- two sectors before a track's INDEX 01 the frame says
 *      INDEX 00, relative time counting DOWN, control still audio.
 *   5. Q advances with playback -- after streaming N sectors' worth of
 *      samples, the absolute position has advanced by N.
 *      NEGATIVE CONTROL: never rebuild the frame on sequence wrap ->
 *      test 5 fails (position frozen).
 *   6. Stop mutes the window -- after $0200 Stop, reads fall back to
 *      $0000 (subcode exists only while the disc streams).
 *   7. The position bytes are really BCD -- tests 3..5 collect within a
 *      few sectors of a low LBA, so every position field is single
 *      digit, and BCD == binary below 10.  Test 7 collects 50 sectors
 *      into track 3, where absolute time is 00:10:50, and compares the
 *      RAW BYTES against ref_bcd() directly -- never round-tripped
 *      through from_bcd(), which is exactly what hides the bug.
 *      NEGATIVE CONTROL: stub SubQBCD() (src/cd/cdrom.c) to
 *      `return (uint8_t)v;` -> test 7 fails, and only test 7 (issue
 *      #330: before this test that sabotage left the file 6/6 green).
 *
 * Build: make test/test_cd_synth_subq      (needs TEST_EXPORTS=1)
 * Run:   DYLD_LIBRARY_PATH=. test/test_cd_synth_subq
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

/* ------------------------------------------------------------------ */
/* BUTCH register offsets (CDROMReadWord/CDROMWriteWord mask to & $FF)   */
/* ------------------------------------------------------------------ */

#define R_BUTCH_LO    0x02u
#define R_DS_DATA     0x0Au
#define R_I2CNTRL_LO  0x12u
#define R_SBCNTRL_LO  0x16u          /* SBCNTRL ($14) low word            */
#define R_SUBDATA_LO  0x1Au          /* SUBDATA ($18) low word ($DFFF1A)  */

#define ST_RX_FULL       0x2000u
#define I2S_DATA_ENABLE  0x0001u

#define SUBQ_VALID       0x0010u
#define SUBQ_SEQ_MASK    0x000Fu
#define SUBQ_BYTES       12u
#define SAMPLES_PER_BYTE 49u         /* 588 samples per sector / 12       */

#define CALLER 0u

/* ------------------------------------------------------------------ */
/* Synthetic disc geometry (same shape as test_cd_synth_cdda.c)         */
/* ------------------------------------------------------------------ */

#define SECTOR_BYTES      2352u
#define SAMPLES_PER_SECT  (SECTOR_BYTES / 4u)
#define NUM_TRACKS        3u
#define AUDIO_SECTORS     150u                     /* 2 s per track       */
#define PREGAP_SECTORS    150u

#define MAX_SEEK_TICKS   200000u
#define MAX_DSA_TICKS      4096u

/* ------------------------------------------------------------------ */
/* Reference Q math -- independent re-implementation of the contract    */
/* ------------------------------------------------------------------ */

static uint16_t ref_crc16(const uint8_t *d, uint32_t len)
{
    uint16_t crc = 0;
    uint32_t i, b;

    for (i = 0; i < len; i++)
    {
        crc ^= (uint16_t)((uint16_t)d[i] << 8);
        for (b = 0; b < 8; b++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
    }
    return crc;
}

static uint8_t ref_bcd(uint32_t v)
{
    return (uint8_t)(((v / 10u) << 4) | (v % 10u));
}

static uint32_t from_bcd(uint8_t b)
{
    return ((uint32_t)(b >> 4)) * 10u + (uint32_t)(b & 0x0Fu);
}

/* Absolute frame count encoded in Q bytes 7..9 (AMIN/ASEC/AFRAME). */
static uint32_t q_abs_frames(const uint8_t *q)
{
    return (from_bcd(q[7]) * 60u + from_bcd(q[8])) * 75u + from_bcd(q[9]);
}

/* ------------------------------------------------------------------ */
/* Disc construction: constant non-zero square tone per track           */
/* ------------------------------------------------------------------ */

static void track_bin_name(char *out, size_t outLen, const char *dir,
                           uint32_t track)
{
    snprintf(out, outLen, "%s/track%02u.bin", dir, (unsigned)track);
}

static bool write_track_bin(const char *dir, uint32_t track)
{
    char path[1024];
    uint8_t *bin;
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

    /* A ~441 Hz square wave: the subcode contract never looks at the
     * PCM, it only has to be a playing stream. */
    for (n = 0; n < AUDIO_SECTORS * SAMPLES_PER_SECT; n++)
    {
        size_t o = gapLen + (size_t)n * 4u;
        int16_t v = ((n / 50u) & 1u) ? (int16_t)-16000 : (int16_t)16000;

        bin[o + 0] = (uint8_t)((uint16_t)v & 0xFFu);
        bin[o + 1] = (uint8_t)(((uint16_t)v >> 8) & 0xFFu);
        bin[o + 2] = bin[o + 0];
        bin[o + 3] = bin[o + 1];
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
/* BUTCH driving helpers (identical moves to test_cd_synth_cdda.c)      */
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

/* Reset + I2S enable + seek, leaving the drive playing at `lba`.  Does
 * NOT arm subcode -- tests do that explicitly so test 1 can assert the
 * disarmed default. */
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

static void arm_subcode(void)
{
    /* The VLM writes $00F2 to SBCNTRL's low word before Play; any
     * nonzero value arms capture. */
    C.CDROMWriteWord(R_SBCNTRL_LO, 0x00F2u, CALLER);
}

static uint16_t subq_read(void)
{
    return C.CDROMReadWord(R_SUBDATA_LO, CALLER);
}

/* Stream one stereo sample (the serializer's clock). */
static void ssi_step(void)
{
    p_ssi_xmit();
}

/* Stream until the serializer has just wrapped to seq 0, then collect
 * one aligned 12-byte frame.  Returns false if no wrap/valid data shows
 * up in a generous window. */
static bool collect_frame(uint8_t *q)
{
    uint32_t guard;
    uint16_t w;
    uint32_t seq;

    /* Get onto a seq 11 -> 0 boundary. */
    for (guard = 0; guard < 4u * SAMPLES_PER_BYTE * SUBQ_BYTES; guard++)
    {
        w = subq_read();
        if ((w & SUBQ_VALID) && (w & SUBQ_SEQ_MASK) == 11u)
            break;
        ssi_step();
    }
    if (guard >= 4u * SAMPLES_PER_BYTE * SUBQ_BYTES)
        return false;
    while ((subq_read() & SUBQ_SEQ_MASK) == 11u)
        ssi_step();

    /* Now at seq 0 of a fresh frame: read each byte as it goes by. */
    for (seq = 0; seq < SUBQ_BYTES; seq++)
    {
        w = subq_read();
        if (!(w & SUBQ_VALID) || (w & SUBQ_SEQ_MASK) != seq)
            return false;
        q[seq] = (uint8_t)(w >> 8);
        while ((subq_read() & SUBQ_SEQ_MASK) == seq && seq != SUBQ_BYTES - 1u)
            ssi_step();
    }
    return true;
}

/* Disc LBA of track t's INDEX 01, via the same TOC path the BIOS uses. */
static uint32_t track_data_lba(uint32_t t)
{
    uint32_t m = p_track_info(t, 0);
    uint32_t s = p_track_info(t, 1);
    uint32_t f = p_track_info(t, 2);
    uint32_t abs = ((m * 60u) + s) * 75u + f;

    return (abs >= 150u) ? (abs - 150u) : 0u;
}

/* ------------------------------------------------------------------ */
/* 1. Disarmed reads stay zero (the pre-#291 contract for every title)  */
/* ------------------------------------------------------------------ */

TEST(disarmed_subdata_reads_return_zero)
{
    uint32_t n;

    if (!prime_stream(track_data_lba(1u)))
        FAIL("could not prime the stream");

    for (n = 0; n < 3u * SAMPLES_PER_SECT; n++)
    {
        uint16_t w = subq_read();

        if (w != 0)
            FAIL("sample %u: disarmed SUBDATA read returned $%04X, "
                 "expected $0000", n, w);
        ssi_step();
    }
}

/* ------------------------------------------------------------------ */
/* 2. Word format and pacing                                            */
/* ------------------------------------------------------------------ */

TEST(word_format_valid_bit_and_49_sample_pacing)
{
    uint32_t n, runLen = 0, runsChecked = 0;
    uint16_t prev;

    if (!prime_stream(track_data_lba(1u)))
        FAIL("could not prime the stream");
    arm_subcode();

    ssi_step();                       /* first sample builds the frame */
    prev = subq_read();
    if (!(prev & SUBQ_VALID))
        FAIL("armed+playing read has no valid bit: $%04X", prev);

    for (n = 0; n < 4u * SAMPLES_PER_SECT; n++)
    {
        uint16_t w = subq_read();

        if (!(w & SUBQ_VALID))
            FAIL("sample %u: valid bit dropped mid-stream ($%04X)", n, w);
        if ((w & SUBQ_SEQ_MASK) > 11u)
            FAIL("sample %u: sequence %u out of range", n,
                 (unsigned)(w & SUBQ_SEQ_MASK));

        if ((w & SUBQ_SEQ_MASK) != (prev & SUBQ_SEQ_MASK))
        {
            uint32_t want = ((prev & SUBQ_SEQ_MASK) + 1u) % SUBQ_BYTES;

            if ((w & SUBQ_SEQ_MASK) != want)
                FAIL("sequence jumped %u -> %u (expected %u)",
                     (unsigned)(prev & SUBQ_SEQ_MASK),
                     (unsigned)(w & SUBQ_SEQ_MASK), want);
            /* Interior runs must be exactly 49 samples.  (The first run
             * after arming can be partial; skip it.) */
            if (runsChecked > 0 && runLen != SAMPLES_PER_BYTE)
                FAIL("sequence %u held for %u samples, expected %u",
                     (unsigned)(prev & SUBQ_SEQ_MASK), runLen,
                     SAMPLES_PER_BYTE);
            runsChecked++;
            runLen = 0;
        }
        runLen++;
        prev = w;
        ssi_step();
    }
    if (runsChecked < 2u * SUBQ_BYTES)
        FAIL("only %u sequence transitions in 4 sectors", runsChecked);
}

/* ------------------------------------------------------------------ */
/* 3. Frame content: BCD position, audio control, $1D0F CRC residual    */
/* ------------------------------------------------------------------ */

TEST(frame_is_valid_adr1_position_with_1d0f_residual)
{
    uint8_t q[SUBQ_BYTES];
    uint32_t d = track_data_lba(2u);

    if (!prime_stream(d))
        FAIL("could not prime the stream at track 2");
    arm_subcode();

    if (!collect_frame(q))
        FAIL("no aligned Q frame within the collection window");

    /* The one check the VLM's DSP actually enforces before unmuting: */
    if (ref_crc16(q, SUBQ_BYTES) != 0x1D0Fu)
        FAIL("CRC residual over 12 bytes is $%04X, the DSP requires $1D0F "
             "(CRC must be stored inverted)",
             ref_crc16(q, SUBQ_BYTES));

    if (q[0] != 0x01u)
        FAIL("CONTROL/ADR byte $%02X, expected $01 (audio, ADR 1; bit 6 "
             "set here is the mute bit)", q[0]);
    if (q[1] != ref_bcd(2u))
        FAIL("track byte $%02X, expected $02", q[1]);
    if (q[2] != 0x01u)
        FAIL("index byte $%02X, expected $01 at INDEX 01", q[2]);
    if (q[6] != 0x00u)
        FAIL("ZERO byte is $%02X", q[6]);

    /* Position: the frame was collected within a few sectors of the
     * seek target, so relative time is tiny and absolute time is
     * near d+150.  Frame-exact equality is deliberately not asserted --
     * collect_frame() streams an unspecified number of sectors while
     * aligning. */
    {
        uint32_t rel = (from_bcd(q[3]) * 60u + from_bcd(q[4])) * 75u
                       + from_bcd(q[5]);
        uint32_t abs = q_abs_frames(q);

        if (rel > 10u)
            FAIL("relative position %u frames from INDEX 01, expected <=10",
                 rel);
        if (abs < d + 150u || abs > d + 160u)
            FAIL("absolute position %u, expected %u..%u",
                 abs, d + 150u, d + 160u);
        if (abs != d + 150u + rel)
            FAIL("absolute (%u) and relative (%u) positions disagree "
                 "about the seek target %u", abs, rel, d);
    }
}

/* ------------------------------------------------------------------ */
/* 4. Pregap: INDEX 00, relative time counts down, control still audio  */
/* ------------------------------------------------------------------ */

TEST(pregap_frame_has_index_zero_and_audio_control)
{
    uint8_t q[SUBQ_BYTES];
    uint32_t d = track_data_lba(3u);

    if (!prime_stream(d - 20u))       /* 20 sectors inside track 3's pregap */
        FAIL("could not prime the stream in track 3's pregap");
    arm_subcode();

    if (!collect_frame(q))
        FAIL("no aligned Q frame within the collection window");

    if (ref_crc16(q, SUBQ_BYTES) != 0x1D0Fu)
        FAIL("pregap frame CRC residual $%04X != $1D0F",
             ref_crc16(q, SUBQ_BYTES));
    if (q[0] != 0x01u)
        FAIL("pregap CONTROL/ADR $%02X, expected $01 -- a data-control "
             "pregap would keep the VLM muted", q[0]);
    if (q[1] != ref_bcd(3u))
        FAIL("pregap track byte $%02X, expected $03", q[1]);
    if (q[2] != 0x00u)
        FAIL("pregap index byte $%02X, expected $00", q[2]);

    {
        uint32_t rel = (from_bcd(q[3]) * 60u + from_bcd(q[4])) * 75u
                       + from_bcd(q[5]);

        if (rel < 5u || rel > 20u)
            FAIL("pregap relative countdown %u, expected 5..20 "
                 "(20 sectors out, minus alignment drift)", rel);
    }
}

/* ------------------------------------------------------------------ */
/* 5. Q advances with playback                                          */
/* ------------------------------------------------------------------ */

TEST(q_position_advances_one_sector_per_588_samples)
{
    uint8_t q1[SUBQ_BYTES], q2[SUBQ_BYTES];
    uint32_t a1, a2, n;

    if (!prime_stream(track_data_lba(1u)))
        FAIL("could not prime the stream");
    arm_subcode();

    if (!collect_frame(q1))
        FAIL("no first frame");
    a1 = q_abs_frames(q1);

    for (n = 0; n < 5u * SAMPLES_PER_SECT; n++)
        ssi_step();

    if (!collect_frame(q2))
        FAIL("no second frame");
    a2 = q_abs_frames(q2);

    /* 5 sectors streamed between the collections, plus up to ~2 sectors
     * of alignment inside each collect_frame(). */
    if (a2 <= a1)
        FAIL("Q position did not advance (%u -> %u)", a1, a2);
    if (a2 - a1 < 5u || a2 - a1 > 9u)
        FAIL("Q advanced %u sectors over ~5 sectors of streaming", a2 - a1);
}

/* ------------------------------------------------------------------ */
/* 6. Stop mutes the window                                             */
/* ------------------------------------------------------------------ */

TEST(stop_returns_subdata_to_zero)
{
    uint32_t n;
    uint16_t w;

    if (!prime_stream(track_data_lba(1u)))
        FAIL("could not prime the stream");
    arm_subcode();

    ssi_step();
    w = subq_read();
    if (!(w & SUBQ_VALID))
        FAIL("no valid subcode before the stop ($%04X)", w);

    dsa_send(0x0200u);                /* Stop */
    if (wait_rx_full(MAX_DSA_TICKS) == 0xFFFFFFFFu)
        FAIL("no response to $0200 Stop");
    (void)dsa_recv();

    for (n = 0; n < 4u; n++)
    {
        ssi_step();
        w = subq_read();
        if (w != 0)
            FAIL("SUBDATA still live after Stop: $%04X", w);
    }
}

/* ------------------------------------------------------------------ */
/* 7. BCD is real: raw bytes at a DOUBLE-DIGIT position (issue #330)    */
/*                                                                      */
/* Tests 3-5 collect within a few sectors of a low LBA, so every        */
/* position field is single-digit -- and BCD == binary below 10.        */
/* Stubbing SubQBCD() to `return (uint8_t)v;` therefore left all six    */
/* green.  This test seeks 50 sectors into track 3, where absolute      */
/* time is 00:10:50, and compares the RAW BYTE against ref_bcd()        */
/* DIRECTLY.  from_bcd() is deliberately not used anywhere below --     */
/* round-tripping through the decoder is what hides the bug.            */
/*                                                                      */
/* Two independent discriminators:                                      */
/*   ASEC is exactly 10 by construction -> BCD $10 vs binary $0A.       */
/*   AFRAME and the track-relative FRAME land in [50, 50+SLOP]; the     */
/*   BCD images of that window ($50..$70) and its binary images         */
/*   ($32..$46) are disjoint, which the tightness guard below asserts   */
/*   at runtime rather than merely claiming in a comment.               */
/* NEGATIVE CONTROL: SubQBCD() -> `return (uint8_t)v;` -> this fails.   */
/* ------------------------------------------------------------------ */

/* 50 sectors past track 3's INDEX 01 -- absolute 00:10:50, and the
 * frame fields stay clear of the 75-frame second boundary. */
#define BCD_PROBE_OFFSET 50u
/* Alignment drift budget for collect_frame() (tests 4 and 5 observe
 * 5-20 and 5-9 frames).  50+20 = 70 < 75, so no second rollover, and
 * 800+20 = 820 keeps ASEC on 10 (825 would be the boundary). */
#define BCD_PROBE_SLOP   20u

TEST(raw_bytes_are_bcd_at_double_digit_position)
{
    uint8_t  q[SUBQ_BYTES];
    uint32_t target, abs_lo, exp_amin, exp_asec, v, w;
    bool     framed, relmatch;

    target   = track_data_lba(3u) + BCD_PROBE_OFFSET;
    abs_lo   = target + 150u;
    exp_amin = abs_lo / 4500u;
    exp_asec = (abs_lo / 75u) % 60u;

    /* PRECONDITION: if the fixture ever stops reaching a double-digit
     * field, this test has silently stopped testing anything -- which
     * is the exact hole #330 documents.  Fail loudly instead. */
    if (exp_asec < 10u)
        FAIL("fixture no longer reaches a double-digit field (ASEC=%u); "
             "this test can no longer distinguish BCD from binary",
             exp_asec);
    /* exp_asec is exact by construction (computed from `target`).  This
     * guard says only that alignment slop cannot carry the collected
     * frame into the NEXT second, which would break both the ASEC
     * equality and the AFRAME window below. */
    if (((abs_lo + BCD_PROBE_SLOP) / 75u) % 60u != exp_asec)
        FAIL("alignment slop of %u frames can cross a second boundary "
             "(ASEC %u -> %u)", BCD_PROBE_SLOP, exp_asec,
             ((abs_lo + BCD_PROBE_SLOP) / 75u) % 60u);
    /* TIGHTNESS: the admissible BCD images and the binary images of the
     * same window must be disjoint, or a matching byte would prove
     * nothing about the encoding. */
    for (v = BCD_PROBE_OFFSET; v <= BCD_PROBE_OFFSET + BCD_PROBE_SLOP; v++)
        for (w = BCD_PROBE_OFFSET; w <= BCD_PROBE_OFFSET + BCD_PROBE_SLOP; w++)
            if (ref_bcd(v) == (uint8_t)w)
                FAIL("frame-field window %u..%u cannot distinguish BCD from "
                     "binary: ref_bcd(%u) == $%02X == binary %u",
                     BCD_PROBE_OFFSET, BCD_PROBE_OFFSET + BCD_PROBE_SLOP,
                     v, ref_bcd(v), w);

    if (!prime_stream(target))
        FAIL("could not prime the stream %u sectors into track 3",
             BCD_PROBE_OFFSET);
    arm_subcode();
    if (!collect_frame(q))
        FAIL("no aligned Q frame within the collection window");

    /* Frame integrity first, so a raw-byte mismatch below means the
     * ENCODING is wrong, not that we grabbed a torn frame. */
    if (ref_crc16(q, SUBQ_BYTES) != 0x1D0Fu)
        FAIL("CRC residual $%04X != $1D0F", ref_crc16(q, SUBQ_BYTES));
    if (q[1] != ref_bcd(3u))
        FAIL("collected a frame for track byte $%02X, expected $03 -- the "
             "seek did not land inside track 3", q[1]);

    /* --- the assertions that fail under the sabotage --- */

    /* AMIN: exact.  ASEC: exact AND >= 10, so BCD($10) != binary($0A). */
    if (q[7] != ref_bcd(exp_amin))
        FAIL("AMIN byte $%02X, expected $%02X (ref_bcd(%u)) -- raw byte, "
             "not decoded", q[7], ref_bcd(exp_amin), exp_amin);
    if (q[8] != ref_bcd(exp_asec))
        FAIL("ASEC byte $%02X, expected $%02X (ref_bcd(%u)); $%02X is the "
             "BINARY encoding -- SubQBCD() is not encoding",
             q[8], ref_bcd(exp_asec), exp_asec, (uint8_t)exp_asec);

    /* AFRAME and the track-relative FRAME both sit in
     * [OFFSET, OFFSET+SLOP], every member >= 10.  Match the raw byte
     * against the ref_bcd() of each admissible value -- still a direct
     * raw-byte comparison, never a decode. */
    framed = false; relmatch = false;
    for (v = BCD_PROBE_OFFSET; v <= BCD_PROBE_OFFSET + BCD_PROBE_SLOP; v++)
    {
        if (q[9] == ref_bcd(v))
            framed = true;
        if (q[5] == ref_bcd(v))
            relmatch = true;
    }
    if (!framed)
        FAIL("AFRAME byte $%02X is not ref_bcd(v) for any v in %u..%u",
             q[9], BCD_PROBE_OFFSET, BCD_PROBE_OFFSET + BCD_PROBE_SLOP);
    if (!relmatch)
        FAIL("relative FRAME byte $%02X is not ref_bcd(v) for any v in "
             "%u..%u", q[5], BCD_PROBE_OFFSET,
             BCD_PROBE_OFFSET + BCD_PROBE_SLOP);
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

    return p_open_image && p_close_image && p_num_tracks && p_track_info &&
           p_butch_exec && p_seek_state && p_ssi_xmit &&
           C.CDROMInit && C.CDROMReset && C.CDROMReadWord && C.CDROMWriteWord;
}

int main(int argc, char *argv[])
{
    char base[512];
    char cue[1024];
    int rc = 1;
    bool opened = false;

    (void)argc; (void)argv;

    TEST_INIT("BUTCH Q-subcode serial window (synthetic audio disc)");

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

    snprintf(base, sizeof(base), "/tmp/vj_cd_subq_%ld", (long)getpid());
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

    C.CDROMInit();
    C.CDROMReset();

    RUN_TEST(disarmed_subdata_reads_return_zero);
    RUN_TEST(word_format_valid_bit_and_49_sample_pacing);
    RUN_TEST(frame_is_valid_adr1_position_with_1d0f_residual);
    RUN_TEST(pregap_frame_has_index_zero_and_audio_control);
    RUN_TEST(q_position_advances_one_sector_per_588_samples);
    RUN_TEST(stop_returns_subdata_to_zero);
    RUN_TEST(raw_bytes_are_bcd_at_double_digit_position);

    rc = TEST_REPORT();

out:
    if (opened)
        p_close_image();
    vj_core_unload(&C);
    scrub_scratch(base);
    return rc;
}
