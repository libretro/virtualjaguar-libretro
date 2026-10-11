/*
 * test/tools/cart_boot_probe.c — one-ROM cartridge boot probe for the
 * cartridge compatibility matrix (test/tools/cart_boot_matrix.sh).
 *
 * Runs a cartridge headlessly for N frames and emits one machine-parseable
 * line the sweep script classifies:
 *
 *   CARTPROBE rom="<path>" frames=<rendered> w=<W> h=<H> pc_valid=<0|1> \
 *       pc=$<FINALPC> \
 *       nonblack_max_pct=<peak %% of any frame lit> lit_frames=<frames >1%% lit> \
 *       motion=<distinct sampled frame hashes> \
 *       audio_nonsilent=<samples> audio_onset=<frame|-1> \
 *       bios_ran=<0|1> handoff=<frame|-1> final_op=$<WORD> halt_frames=<n> \
 *       scored_from=<frame> scored_frames=<n> scored_lit_frames=<n> \
 *       scored_motion=<n> scored_audio_nonsilent=<samples>
 *
 * The first block of fields covers the whole run.  The second block is the
 * BOOT-ROM-RELATIVE view (issue #852): under --bios the Jaguar boot ROM plays
 * a ~490-frame logo animation + jingle before it hands the 68K to the cart,
 * and that animation is identical for every title, so scoring it as the
 * title's "video moving" / "audio" is meaningless.  The probe therefore finds
 * the handoff itself and restarts the scored counters there:
 *
 *   bios_ran     1 if --bios was given AND the 68K was executing in the boot
 *                ROM window ($E00000-$E3FFFF) at the end of frame 1.  Headerless
 *                RAM-loaded executables (e.g. .jag homebrew) never run the boot
 *                ROM even under --bios: bios_ran=0 and nothing is excluded.
 *   handoff      frame at whose end the 68K (or, via the pcQueue ring, one of
 *                its last 1024 instructions) was in cartridge space
 *                ($800000-$DFFFFF); 0 when bios_ran=0 (no boot phase); -1 when
 *                the boot ROM ran but no handoff was ever seen.
 *   final_op     the opcode word at the final PC.  $60FE is BRA.S * -- the boot
 *                ROM's own halt loop (it executes ILLEGAL, NOP, BRA.S * when it
 *                rejects the cartridge).
 *   halt_frames  consecutive frames the 68K has been parked on a $60FE loop.
 *   scored_*     the counters restarted at the handoff (== whole-run values
 *                whenever there is no boot phase or no handoff was seen).
 *
 * With --post-handoff N the run stops N frames after the handoff, with
 * --frames as the hard cap, and stops early once the boot ROM has sat in its
 * halt loop for PROBE_PARK_FRAMES frames (a rejected cart never gets better).
 * Crash-watchdog signatures are not handoff-relative (see cart_classify.sh).
 *
 * Deliberately does NOT classify.  The stage taxonomy, the crash-watchdog
 * signature greps, and the honesty rules ("boots headlessly" is not
 * "completed the game") live in one place: the sweep script.  Watchdog
 * signatures reach the script through this probe's stderr because the
 * harness forwards core WARN/ERR log lines by default.
 *
 * Needs the wide test ABI (make TEST_EXPORTS=1) for m68k_get_reg.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./src -I./libretro-common/include \
 *      -o test/tools/cart_boot_probe test/tools/cart_boot_probe.c \
 *      test/harness/harness.c -ldl -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../harness/harness.h"

/* src/m68000/m68kinterface.h: D0-D7, A0-A7, then PC.  The enum is part of
 * the UAE core's stable interface; 16 is M68K_REG_PC.  (Same pattern as
 * test/tools/cd_wedge_probe.c.) */
#define PROBE_M68K_REG_PC 16

/* 68K address windows.  The boot ROM lives at $E00000 (the core maps the
 * embedded image there); cartridge space is $800000-$DFFFFF. */
#define PROBE_BOOT_LO  0xE00000u
#define PROBE_BOOT_HI  0xE3FFFFu
#define PROBE_CART_LO  0x800000u
#define PROBE_CART_HI  0xDFFFFFu
/* BRA.S * -- the boot ROM's halt loop after it rejects the cartridge. */
#define PROBE_OP_HALT  0x60FEu
/* Consecutive frames parked on PROBE_OP_HALT before the probe calls it a
 * rejected cart and stops.  cart_boot_matrix.sh requires the same count. */
#define PROBE_PARK_FRAMES 30u
/* pcQueue (src/core/jaguar.c) is a 1024-entry ring of the last 68K PCs. */
#define PROBE_PCQ_LEN 0x400u

typedef struct {
    unsigned last_nonblack;      /* non-black pixels in most recent frame */
    unsigned max_nonblack;       /* peak across the run — a title mid-fade
                                    on the final frame still counts */
    unsigned lit_frames;         /* frames with >~1%% non-black pixels */
    unsigned last_w, last_h;
    unsigned frames;             /* frames with a non-duped video callback */
    unsigned distinct_hashes;    /* frames whose sampled hash differed from
                                    the previous frame's — motion evidence */
    uint32_t prev_hash;
    int      have_prev;
} probe_video_state;

/* Whole-run and since-handoff views of the same stream. */
typedef struct {
    probe_video_state whole;
    probe_video_state scored;
} probe_video_pair;

static void probe_video_track(probe_video_state *st, unsigned width,
                              unsigned height, unsigned nonblack,
                              uint32_t hash)
{
    /* Scale the sampled count back up to an approximate full-frame count. */
    st->last_nonblack = nonblack * 16u;
    if (st->last_nonblack > st->max_nonblack)
        st->max_nonblack = st->last_nonblack;
    if (st->last_nonblack * 100u > width * height)
        st->lit_frames++;
    st->frames++;
    st->last_w = width;
    st->last_h = height;
    if (st->have_prev && hash != st->prev_hash)
        st->distinct_hashes++;
    st->prev_hash = hash;
    st->have_prev = 1;
}

static void probe_video_cb(void *userdata, const void *data,
                           unsigned width, unsigned height, size_t pitch)
{
    probe_video_pair *pair = (probe_video_pair *)userdata;
    const uint8_t *rows;
    uint32_t hash;
    unsigned nonblack, x, y, step;

    if (!data || !width || !height)
        return;

    rows = (const uint8_t *)data;
    hash = 2166136261u;
    nonblack = 0;
    /* Sample every 4th pixel of every 4th row: cheap, and plenty to tell
     * "black screen" from "rendering" and frame A from frame B. */
    step = 4;
    for (y = 0; y < height; y += step) {
        const uint32_t *px = (const uint32_t *)(rows + y * pitch);
        for (x = 0; x < width; x += step) {
            uint32_t v = px[x] & 0x00FFFFFFu;
            if (v != 0)
                nonblack++;
            hash ^= v;
            hash *= 16777619u;
        }
    }
    probe_video_track(&pair->whole, width, height, nonblack, hash);
    probe_video_track(&pair->scored, width, height, nonblack, hash);
}

/* Boot-ROM handoff tracking, driven from the per-frame callback. */
typedef struct {
    harness_config *cfg;
    probe_video_pair *video;
    unsigned int (*get_reg)(void *, int);
    const uint8_t *memspace;     /* jagMemSpace: big-endian bytes */
    const uint32_t *pc_queue;    /* last PROBE_PCQ_LEN 68K PCs */
    unsigned post_handoff;       /* 0 = run to --frames */
    int      bios_ran;
    int      handoff;            /* -1 unseen, else frame */
    unsigned scored_from;
    unsigned audio_base;         /* cfg.audio.total_nonsilent at handoff */
    unsigned long final_pc;
    unsigned final_op;
    unsigned halt_frames;
    unsigned long halt_pc;
} probe_boot_state;

static int probe_in_cart(unsigned long pc)
{
    return pc >= PROBE_CART_LO && pc <= PROBE_CART_HI;
}

static void probe_mark_handoff(probe_boot_state *b, unsigned frame, int fresh)
{
    b->handoff = (int)frame;
    b->scored_from = frame;
    if (fresh) {
        /* Restart the scored counters: frames after this one are the title's. */
        memset(&b->video->scored, 0, sizeof(b->video->scored));
        b->audio_base = b->cfg->audio.total_nonsilent;
    }
}

static bool probe_frame_cb(void *userdata, unsigned frame)
{
    probe_boot_state *b = (probe_boot_state *)userdata;
    unsigned long pc = b->get_reg(NULL, PROBE_M68K_REG_PC) & 0xFFFFFFu;

    b->final_pc = pc;
    b->final_op = (pc + 1 < PROBE_BOOT_HI && b->memspace)
        ? (unsigned)((b->memspace[pc] << 8) | b->memspace[pc + 1]) : 0xFFFFu;

    if (frame == 1) {
        b->bios_ran = b->cfg->use_bios &&
                      pc >= PROBE_BOOT_LO && pc <= PROBE_BOOT_HI;
        if (!b->bios_ran)
            probe_mark_handoff(b, 0, 0);   /* no boot phase: score it all */
    }

    if (b->handoff < 0) {
        int in_cart = probe_in_cart(pc);
        unsigned i;

        /* A title that copies itself to RAM straight away could be missed by
         * an end-of-frame PC check; the pcQueue ring widens the window to
         * its last 1024 instructions. */
        for (i = 0; !in_cart && b->pc_queue && i < PROBE_PCQ_LEN; i++)
            if (probe_in_cart(b->pc_queue[i] & 0xFFFFFFu))
                in_cart = 1;
        if (in_cart) {
            probe_mark_handoff(b, frame, 1);
        } else if (b->final_op == PROBE_OP_HALT) {
            if (b->halt_frames && b->halt_pc == pc)
                b->halt_frames++;
            else
                b->halt_frames = 1;
            b->halt_pc = pc;
            if (b->halt_frames >= PROBE_PARK_FRAMES)
                return false;   /* rejected: the boot ROM halted itself */
        } else {
            b->halt_frames = 0;
        }
    }

    if (b->post_handoff && b->handoff >= 0 &&
        frame >= (unsigned)b->handoff + b->post_handoff)
        return false;
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    probe_video_pair vpair;
    probe_boot_state boot;
    probe_video_state *vstate = &vpair.whole;
    probe_video_state *sstate = &vpair.scored;
    unsigned int (*get_reg)(void *, int) = NULL;
    unsigned long final_pc = 0;
    int pc_valid = 0;
    unsigned total_px, i;
    double nonblack_pct = 0.0;
    harness_result res;

    memset(&vpair, 0, sizeof(vpair));
    memset(&boot, 0, sizeof(boot));
    boot.handoff = -1;

    cfg.frames = 600;
    cfg.quiet = 1;
    if (!harness_init_from_args(&cfg, argc, argv))
        return 2;
    /* The harness skips flags it does not know; --post-handoff is ours. */
    for (i = 1; (int)i + 1 < argc; i++)
        if (strcmp(argv[i], "--post-handoff") == 0)
            boot.post_handoff = (unsigned)strtoul(argv[i + 1], NULL, 10);
    cfg.video_callback = probe_video_cb;
    cfg.video_callback_data = &vpair;

    if (!harness_load_rom(&cfg)) {
        printf("CARTPROBE rom=\"%s\" load_fail=1\n",
               cfg.rom_path ? cfg.rom_path : "?");
        harness_shutdown(&cfg);
        return 3;
    }

    get_reg = (unsigned int (*)(void *, int))
        harness_dlsym(&cfg, "m68k_get_reg");
    if (get_reg) {
        boot.cfg = &cfg;
        boot.video = &vpair;
        boot.get_reg = get_reg;
        boot.memspace = (const uint8_t *)harness_dlsym(&cfg, "jagMemSpace");
        boot.pc_queue = (const uint32_t *)harness_dlsym(&cfg, "pcQueue");
        cfg.frame_callback = probe_frame_cb;
        cfg.frame_callback_data = &boot;
    }

    harness_run(&cfg);

    if (get_reg) {
        final_pc = get_reg(NULL, PROBE_M68K_REG_PC) & 0xFFFFFFu;
        pc_valid = 1;
    }

    total_px = vstate->last_w * vstate->last_h;
    if (total_px)
        nonblack_pct = 100.0 * (double)vstate->max_nonblack / (double)total_px;
    if (nonblack_pct > 100.0)
        nonblack_pct = 100.0;

    printf("CARTPROBE rom=\"%s\" frames=%u w=%u h=%u pc_valid=%d pc=$%06lX "
           "nonblack_max_pct=%.1f lit_frames=%u motion=%u "
           "audio_nonsilent=%u audio_onset=%d "
           "bios_ran=%d handoff=%d final_op=$%04X halt_frames=%u "
           "scored_from=%u scored_frames=%u scored_lit_frames=%u "
           "scored_motion=%u scored_audio_nonsilent=%u\n",
           cfg.rom_path, cfg.video.total_frames_rendered,
           vstate->last_w, vstate->last_h, pc_valid, final_pc,
           nonblack_pct, vstate->lit_frames, vstate->distinct_hashes,
           cfg.audio.total_nonsilent, cfg.audio.first_audio_frame,
           boot.bios_ran, boot.handoff, boot.final_op, boot.halt_frames,
           boot.scored_from, sstate->frames, sstate->lit_frames,
           sstate->distinct_hashes,
           cfg.audio.total_nonsilent - boot.audio_base);

    res.status = "INFO";
    res.name = "cart_boot_probe";
    res.detail = "see CARTPROBE line";
    harness_report(&cfg, &res, 1);
    harness_shutdown(&cfg);
    return 0;
}
