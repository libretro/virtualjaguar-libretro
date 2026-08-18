/*
 * test/tools/test_lightgun.c -- light gun (#438) end-to-end verification.
 *
 * WHAT THIS PROVES, AND WHY IT IS SHAPED THIS WAY
 * ==============================================
 * The core synthesises LPH ($F00008) / LPV ($F0000A) from an aim point
 * (src/tom/tom.c, TOMLightgunHalfline).  Two independent things can be
 * wrong with that: the values themselves, and the meaning a real game
 * assigns to them.  So there are two halves here.
 *
 * PART A -- no ROM needed, always runs.  Attaches a gun, aims at a known
 * native framebuffer pixel and reads the latched registers straight out of
 * tomRam8.  Checks the three properties that have nothing to do with any
 * particular game:
 *   A1  LPH is affine in the aim column with slope PWIDTH, and LPV is
 *       affine in the aim row with slope 2 (half-lines).
 *   A2  an off-screen shot does NOT relatch -- the registers keep their
 *       last value, which is what a real gun pointed away from the CRT
 *       does (no light, no LP pulse).
 *   A3  THE HI-RES GUARDRAIL (#400): the same normalised aim point latches
 *       byte-identical LPH/LPV at 1x and at internal_resolution=2x.  The
 *       enhancement path must be invisible to the emulated machine, and an
 *       aim that shifts when a user turns 2x on is exactly the bug class
 *       #400 was.
 *
 * PART B -- needs Balloons (Matthias Domin, 2003), the one confirmed
 * light gun title; exits 77 (skip) when it is not in the private corpus.
 * Boots it, drives its two-target CALIBRATION screen with the gun, then
 * checks two things in gameplay:
 *   B1  the crosshair the game itself computes from LPH/LPV lands on the
 *       object coordinate that the pixel we aimed at actually occupies,
 *       across the FULL width of the screen.
 *   B2  aiming at a balloon and pulling the trigger registers a hit.
 *
 * B1 IS THE ASSERTION THAT DISCRIMINATES THE LPH ENCODING, and that is why
 * it sweeps the whole width instead of sampling one point.  TOM encodes HC
 * as bit 10 = "second half-line" plus a 0..HP offset, so raw HC values jump
 * 845 -> 1024 mid-line.  Balloons decodes with a plain
 * `(LPH - calibration_LPH) / PWIDTH` and no bit-10 handling, so an encoded
 * LPH would put a ~60-pixel step in the middle of its playfield: the left
 * half would pass and the right half would fail.  A single-point check
 * would have shipped the wrong encoding.  See the comment on
 * TOMLightgunHalfline for the decision this test settles.
 *
 * The object-space <-> framebuffer-column link used by B1 is
 * `col = startPos + XPOS`, with `startPos = (HDB1 - leftVisibleHC)/PWIDTH`
 * -- the same expression every scanline renderer in tom.c opens with, and
 * the OP writes object pixels into the line buffer at index XPOS.  HDB1
 * and PWIDTH are read live from tomRam8; leftVisibleHC is the one constant
 * restated here, which pins it rather than assuming it.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 $(INCFLAGS) -o test/tools/test_lightgun \
 *      test/tools/test_lightgun.c test/harness/harness.c -ldl -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#include "../harness/harness.h"
#include "libretro.h"

#define BALLOONS_ROM  "test/roms/private/ROMS/BALLOONS.BIN"
#define DEFAULT_CORE  "./virtualjaguar_libretro.dylib"
#define PUBLIC_ROM    "test/roms/yarc.j64"

/* TOM register offsets within tomRam8 (src/tom/tom.c). */
#define R_LPH    0x08
#define R_LPV    0x0A
#define R_VMODE  0x28
#define R_HDB1   0x38
#define R_VDB    0x46
#define R_VP     0x3E

/* src/tom/tom.c DEFAULT_LEFT_VISIBLE_HC / _PAL.  Restated, not derived:
 * if either constant moves, this test is meant to notice. */
#define LEFT_VISIBLE_HC_NTSC  (208 - 16 - (1 * 4))
#define LEFT_VISIBLE_HC_PAL   (208 - 16 - (-3 * 4))

/* Balloons RAM map, from a byte-level disassembly of the ROM (loaded at
 * $4000 as a JST_RAW_BINARY).  Word-sized and big-endian unless noted. */
#define B_LPH_COPY   0x7E50  /* raw LPH as the game latched it this field  */
#define B_LPV_COPY   0x7E52
#define B_GUN_X      0x7E54  /* decoded crosshair X, object pixels         */
#define B_GUN_Y      0x7E56  /* decoded crosshair Y, half-lines            */
#define B_BALLOON_X  0x7E46  /* balloon X variable (object X = this + $3C) */
#define B_BALLOON_Y  0x7E48  /* balloon rise counter; reset to $118 on hit */
#define B_CAL1       0x7E66  /* LPH captured at calibration target 1       */
#define B_CAL2       0x7E68  /* ... and at target 2                        */
#define B_OBJ_BALL_X 0x4E10  /* live sprite table: balloon X / Y           */
#define B_OBJ_BALL_Y 0x4E12
#define B_OBJ_CROSSX 0x4E28  /* ... crosshair X / Y, written from LPH/LPV  */
#define B_OBJ_CROSSY 0x4E2A
#define B_OBJ_CALX   0x4E78  /* calibration screen: target X / Y           */
#define B_OBJ_CALY   0x4E7A

/* Balloons' own hardcoded decode constants, needed to predict what it
 * should compute.  `+$30` is the object X it assumes the FIRST calibration
 * target's hotspot sits at; `-8` is its vertical fudge.  Both are the
 * game's, not ours -- we reproduce them, we do not compensate for them. */
#define B_CAL_HOTSPOT_X  0x30
#define B_Y_FUDGE        8
/* Aiming at or below this half-line makes Balloons quit (it jumps to
 * $4710, clears VIDEN and returns).  Every aim point here stays above it,
 * and the test asserts the program is still running so that "quit" can
 * never be misread as "missed". */
#define B_QUIT_HALFLINE  0x1BE

/* ------------------------------------------------------------------ *
 * gun state fed to the core through the harness input hook
 * ------------------------------------------------------------------ */

typedef struct {
   int32_t col, row;     /* native framebuffer pixel being aimed at */
   int32_t nat_w, nat_h; /* native geometry to normalise against    */
   int     trigger;
   int     offscreen;
} gun_state;

static gun_state gun;

/* libretro light gun coordinates are normalised over the frame the core
 * REPORTS, [-0x8000, 0x7fff].  Aim at the CENTRE of the wanted pixel so
 * the core's own floor division lands back on it exactly. */
static int16_t norm_axis(int32_t pix, int32_t n)
{
   long v;

   if (n <= 0)
      return 0;

   v = (long)(((2 * (long)pix + 1) * 0x10000L) / (2L * (long)n)) - 0x8000L;
   if (v < -0x8000L)
      v = -0x8000L;
   if (v > 0x7FFFL)
      v = 0x7FFFL;

   return (int16_t)v;
}

static int16_t gun_cb(void *ud, unsigned port, unsigned dev, unsigned idx,
                      unsigned id)
{
   (void)ud;
   (void)idx;

   if (port != 0 || dev != RETRO_DEVICE_LIGHTGUN)
      return 0;

   switch (id)
   {
      case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X:
         return norm_axis(gun.col, gun.nat_w);
      case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y:
         return norm_axis(gun.row, gun.nat_h);
      case RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN:
         return (int16_t)(gun.offscreen ? 1 : 0);
      case RETRO_DEVICE_ID_LIGHTGUN_TRIGGER:
         return (int16_t)(gun.trigger ? 1 : 0);
      default:
         break;
   }

   return 0;
}

/* ------------------------------------------------------------------ *
 * core state access
 * ------------------------------------------------------------------ */

typedef struct {
   const uint8_t *tom;
   const uint8_t *ram;
   const int     *game_w;
   const int     *game_h;
   const int     *hires_n;
} core_syms;

static int core_syms_bind(harness_config *cfg, core_syms *s)
{
   s->tom     = (const uint8_t *)harness_dlsym(cfg, "tomRam8");
   /* jaguarMainRAM is a POINTER into jagMemSpace (src/core/vjag_memory.c),
    * not an array -- dlsym yields the address of the pointer itself. */
   {
      uint8_t **pp = (uint8_t **)harness_dlsym(cfg, "jaguarMainRAM");
      s->ram = pp ? (const uint8_t *)*pp : NULL;
   }
   s->game_w  = (const int *)harness_dlsym(cfg, "game_width");
   s->game_h  = (const int *)harness_dlsym(cfg, "game_height");
   s->hires_n = (const int *)harness_dlsym(cfg, "shadowHiresN");

   if (!s->tom || !s->ram || !s->game_w || !s->game_h || !s->hires_n)
   {
      fprintf(stderr, "test_lightgun: missing test-ABI symbols "
                      "(build with TEST_EXPORTS=1)\n");
      return 0;
   }

   return 1;
}

static uint16_t rd16(const uint8_t *base, uint32_t off)
{
   return (uint16_t)((base[off] << 8) | base[off + 1]);
}

static int tom_pwidth(const core_syms *s)
{
   return (int)(((rd16(s->tom, R_VMODE) & 0x0E00) >> 9) + 1);
}

static int tom_left_hc(const core_syms *s)
{
   /* TOMReset programs VP = 523 for NTSC and 623 for PAL; no title in the
    * corpus reprograms it, and Balloons certainly does not (it writes VI,
    * VDB, HDB1 and HDE only).  Deriving the mode from VP rather than from
    * a flag keeps this test free of the core's settings struct layout. */
   return (rd16(s->tom, R_VP) > 560) ? LEFT_VISIBLE_HC_PAL
                                     : LEFT_VISIBLE_HC_NTSC;
}

/* Framebuffer column of object-space X, i.e. the renderers' own
 * `startPos = (HDB1 - leftVisibleHC) / PWIDTH` offset. */
static int tom_start_pos(const core_syms *s)
{
   int16_t d = (int16_t)(rd16(s->tom, R_HDB1) - (int16_t)tom_left_hc(s));
   return (int)(d / (int16_t)tom_pwidth(s));
}

/* First visible half-line, mirroring TOMGetTopVisible()'s live path.  Only
 * the reprogrammed-VDB case can occur here: every ROM this test drives has
 * written its own VDB long before any assertion runs, and the assertions
 * check that (they compare against a value derived from this). */
static int tom_top_visible(const core_syms *s)
{
   return (int)rd16(s->tom, R_VDB);
}

/* ------------------------------------------------------------------ *
 * result plumbing
 * ------------------------------------------------------------------ */

static harness_result results[HARNESS_MAX_RESULTS];
static char           detail[HARNESS_MAX_RESULTS][192];
static unsigned       nresults;
static int            failures;

static void record(int ok, const char *name, const char *fmt, ...)
{
   va_list ap;

   if (nresults >= HARNESS_MAX_RESULTS)
      return;

   va_start(ap, fmt);
   vsnprintf(detail[nresults], sizeof(detail[0]), fmt, ap);
   va_end(ap);

   results[nresults].status = ok ? "PASS" : "FAIL";
   results[nresults].name   = name;
   results[nresults].detail = detail[nresults];
   nresults++;

   if (!ok)
      failures++;
}

/* ------------------------------------------------------------------ *
 * PART A -- register-level checks, any ROM
 * ------------------------------------------------------------------ */

typedef struct {
   core_syms syms;
   int       bound;
   /* Sampled once the machine has settled. */
   int       pwidth;
   int       left_hc;
   int       top_vis;
   uint16_t  lph[4];
   uint16_t  lpv[4];
   uint16_t  lph_frozen;
   uint16_t  lpv_frozen;
   int       step;
   int       aim_cols[4];
   int       aim_rows[4];
} partA;

/* Aim columns are deliberately spread across the screen, including one
 * past the half-line boundary, for the reason the file header gives. */
static bool partA_frame(void *ud, unsigned frame)
{
   partA *a = (partA *)ud;
   int    settle = 90;
   int    slot;

   if (!a->bound)
      return true;

   if ((int)frame < settle)
   {
      gun.offscreen = 1;
      return true;
   }

   /* Normalise against the frame the core is CURRENTLY reporting: a title
    * reprograms its display window during boot, and normalising against a
    * stale width lands the aim a pixel or two off. */
   if (*a->syms.hires_n > 0 && *a->syms.game_w > 0)
   {
      gun.nat_w = *a->syms.game_w / *a->syms.hires_n;
      gun.nat_h = *a->syms.game_h / *a->syms.hires_n;
   }

   if (a->pwidth == 0)
   {
      a->pwidth  = tom_pwidth(&a->syms);
      a->left_hc = tom_left_hc(&a->syms);
      a->top_vis = tom_top_visible(&a->syms);
      a->aim_cols[0] = 16;
      a->aim_cols[1] = (int)gun.nat_w / 4;
      a->aim_cols[2] = (int)gun.nat_w / 2;
      a->aim_cols[3] = ((int)gun.nat_w * 3) / 4;
   }

   /* Four aim points, three frames each: frame N sets the aim, the value
    * is sampled two frames later so the latch has certainly happened. */
   slot = ((int)frame - settle) / 3;

   if (slot < 4)
   {
      gun.offscreen = 0;
      gun.col = a->aim_cols[slot];
      gun.row = a->aim_rows[slot];

      if ((((int)frame - settle) % 3) == 2)
      {
         a->lph[slot] = rd16(a->syms.tom, R_LPH);
         a->lpv[slot] = rd16(a->syms.tom, R_LPV);
      }
      return true;
   }

   /* Then go off-screen and confirm the registers stop moving. */
   gun.offscreen = 1;
   gun.col = 0;
   gun.row = 0;

   if (slot == 4)
   {
      a->lph_frozen = rd16(a->syms.tom, R_LPH);
      a->lpv_frozen = rd16(a->syms.tom, R_LPV);
   }
   else if (slot > 5)
      return false;

   return true;
}

/* run_partA fills *out_lph/-lpv with the four samples so the caller can
 * compare a 1x pass against a 2x pass. */
static int run_partA(const char *core, const char *rom, int hires2x,
                     uint16_t *out_lph, uint16_t *out_lpv, int report)
{
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   partA          a;
   int            i;
   int            ok = 1;
   int            nat_w, nat_h;

   memset(&a, 0, sizeof(a));

   cfg.core_path  = core;
   cfg.rom_path   = rom;
   cfg.frames     = 130;
   cfg.quiet      = 1;
   cfg.input_callback      = gun_cb;
   cfg.frame_callback      = partA_frame;
   cfg.frame_callback_data = &a;

   harness_set_option(&cfg, "virtualjaguar_p1_device", "lightgun");
   if (hires2x)
      harness_set_option(&cfg, "virtualjaguar_internal_resolution", "2x");

   /* A load or bind failure has to be recorded as a FAILURE, not just
    * returned: main() maps "no failures" to exit 77 when Balloons is
    * absent, so a silent 0 here would report a broken core as a skip. */
   if (!harness_load_core(&cfg))
   {
      if (report)
         record(0, "lightgun_transform", "could not load core %s", core);
      return 0;
   }
   if (!harness_load_rom(&cfg))
   {
      if (report)
         record(0, "lightgun_transform", "could not load ROM %s", rom);
      harness_shutdown(&cfg);
      return 0;
   }
   if (!core_syms_bind(&cfg, &a.syms))
   {
      if (report)
         record(0, "lightgun_transform",
                "test-ABI symbols missing (needs TEST_EXPORTS=1)");
      harness_shutdown(&cfg);
      return 0;
   }
   a.bound = 1;

   /* Native geometry: what libretro.c divides the hi-res factor out of. */
   nat_w = (*a.syms.hires_n > 0) ? (*a.syms.game_w / *a.syms.hires_n) : 0;
   nat_h = (*a.syms.hires_n > 0) ? (*a.syms.game_h / *a.syms.hires_n) : 0;
   if (nat_w <= 0 || nat_h <= 0)
   {
      nat_w = 320;
      nat_h = 240;
   }
   gun.nat_w = nat_w;
   gun.nat_h = nat_h;

   a.aim_cols[0] = 16;
   a.aim_cols[1] = nat_w / 4;
   a.aim_cols[2] = nat_w / 2;
   a.aim_cols[3] = (nat_w * 3) / 4;
   a.aim_rows[0] = 20;
   a.aim_rows[1] = 40;
   a.aim_rows[2] = 60;
   a.aim_rows[3] = 80;

   gun.offscreen = 1;
   gun.trigger   = 0;

   harness_run(&cfg);

   /* Re-read the geometry the core actually reported; it can change once
    * the title programs its own display window. */
   if (*a.syms.hires_n > 0 && *a.syms.game_w > 0)
   {
      nat_w = *a.syms.game_w / *a.syms.hires_n;
      nat_h = *a.syms.game_h / *a.syms.hires_n;
   }

   if (report)
   {
      /* PRECONDITION, not an assertion about the feature: tom_top_visible()
       * reads VDB raw, while the core's TOMGetTopVisible() substitutes a
       * mode fallback when VDB is 0, is still at the shared reset value of
       * 38, or exceeds VP.  They agree only outside those cases.  Every ROM
       * this test drives programs its own VDB long before frame 90, so a
       * hit here means the ROM changed, not that the transform broke --
       * fail loudly rather than let it surface as a phantom offset. */
      {
         uint16_t vdb = rd16(a.syms.tom, R_VDB);
         uint16_t vp  = rd16(a.syms.tom, R_VP);

         if (vdb == 0 || vdb == 38 || (vp != 0 && vdb > vp))
         {
            ok = 0;
            record(0, "lightgun_transform",
                   "PRECONDITION: %s left VDB=%u (VP=%u), which the core's "
                   "TOMGetTopVisible() replaces with a fallback -- pick a "
                   "ROM that programs its own display window",
                   rom, vdb, vp);
         }
      }

      /* A1: LPH affine in column with slope PWIDTH; LPV affine in row
       * with slope 2 (half-lines). */
      for (i = 0; i < 4; i++)
      {
         int want_h = a.left_hc + a.aim_cols[i] * a.pwidth;
         int want_v = a.top_vis + a.aim_rows[i] * 2;

         if ((int)a.lph[i] != want_h || (int)a.lpv[i] != want_v)
         {
            ok = 0;
            record(0, "lightgun_transform",
                   "col %d row %d: LPH=%u (want %d) LPV=%u (want %d), "
                   "pwidth=%d leftHC=%d topVis=%d",
                   a.aim_cols[i], a.aim_rows[i], a.lph[i], want_h,
                   a.lpv[i], want_v, a.pwidth, a.left_hc, a.top_vis);
            break;
         }
      }
      if (ok)
         record(1, "lightgun_transform",
                "LPH = leftHC(%d) + col*pwidth(%d), LPV = topVis(%d) + 2*row, "
                "4 points across the width", a.left_hc, a.pwidth, a.top_vis);

      /* A2: off-screen freezes the latch. */
      record(a.lph_frozen == a.lph[3] && a.lpv_frozen == a.lpv[3],
             "lightgun_offscreen",
             "off-screen kept LPH=%u LPV=%u (last on-screen %u/%u)",
             a.lph_frozen, a.lpv_frozen, a.lph[3], a.lpv[3]);
      if (!(a.lph_frozen == a.lph[3] && a.lpv_frozen == a.lpv[3]))
         ok = 0;
   }

   for (i = 0; i < 4; i++)
   {
      out_lph[i] = a.lph[i];
      out_lpv[i] = a.lpv[i];
   }

   harness_shutdown(&cfg);
   return ok;
}

/* ------------------------------------------------------------------ *
 * PART B -- Balloons
 * ------------------------------------------------------------------ */

enum {
   PH_SETTLE = 0,
   PH_CAL1,
   PH_CAL2,
   PH_SWEEP,
   PH_HUNT,
   PH_SHOOT,
   PH_DONE
};

typedef struct {
   core_syms syms;
   int       bound;
   int       phase;
   unsigned  phase_frame;
   int       nat_w, nat_h;
   int       start_pos;
   int       top_vis;
   int       pwidth;
   int       cal_col;          /* column aimed at for calibration target 1 */
   /* B1 sweep */
   int       sweep_idx;
   int       sweep_cols[24];
   int       sweep_n;
   int       sweep_bad;
   int       sweep_bad_col;
   int       sweep_bad_got;
   int       sweep_bad_want;
   int       sweep_bad_goty;
   int       sweep_bad_wanty;
   int       sweep_checked;
   /* B2 hit */
   int       hit_registered;
   int       shots_fired;
   int       prev_balloon_y;
   int       quit_detected;
   int       cal1_done;
   int       cal2_done;
   /* Diagnostics so a failed hit is attributable: a balloon that never
    * entered the aimable band reads exactly like a broken transform. */
   int       hunt_last_objx;
   int       hunt_last_objy;
   int       hunt_frames;
} partB;

static void gun_aim(int col, int row)
{
   gun.col       = col;
   gun.row       = row;
   gun.offscreen = 0;
}

/* Hold for two frames then release for three: Balloons edge-detects the
 * pad (its scan at $4A42 keeps an "newly pressed this field" mask), so a
 * held trigger fires exactly once. */
static int pulse_trigger(unsigned t)
{
   unsigned m = t % 5;
   return (m < 2) ? 1 : 0;
}

static bool partB_frame(void *ud, unsigned frame)
{
   partB         *b = (partB *)ud;
   const uint8_t *ram;
   uint16_t       viden;

   (void)frame;

   if (!b->bound)
      return true;

   ram   = b->syms.ram;
   viden = (uint16_t)(rd16(b->syms.tom, R_VMODE) & 1);

   if (getenv("VJ_LG_DEBUG") && (frame % 15) == 0)
      fprintf(stderr, "f%-5u ph%d aim=%d,%d trg=%d off=%d LPH=%04X LPV=%04X "
                      "gLPH=%04X gLPV=%04X gx=%d gy=%d cal1=%04X cal2=%04X "
                      "calX=%d objBX=%d objBY=%d crX=%d crY=%d viden=%u\n",
              frame, b->phase, gun.col, gun.row, gun.trigger, gun.offscreen,
              rd16(b->syms.tom, R_LPH), rd16(b->syms.tom, R_LPV),
              rd16(ram, B_LPH_COPY), rd16(ram, B_LPV_COPY),
              (int16_t)rd16(ram, B_GUN_X), (int16_t)rd16(ram, B_GUN_Y),
              rd16(ram, B_CAL1), rd16(ram, B_CAL2),
              (int16_t)rd16(ram, B_OBJ_CALX),
              (int16_t)rd16(ram, B_OBJ_BALL_X), (int16_t)rd16(ram, B_OBJ_BALL_Y),
              (int16_t)rd16(ram, B_OBJ_CROSSX), (int16_t)rd16(ram, B_OBJ_CROSSY),
              viden);

   /* Balloons turns the display off on its way out of main() -- if that
    * ever happens mid-test, stop and say so rather than reporting a miss. */
   if (b->phase > PH_SETTLE && !viden)
   {
      b->quit_detected = 1;
      return false;
   }

   b->phase_frame++;

   switch (b->phase)
   {
      case PH_SETTLE:
         gun.offscreen = 1;
         gun.trigger   = 0;
         if (b->phase_frame < 90)
            return true;

         b->pwidth    = tom_pwidth(&b->syms);
         b->start_pos = tom_start_pos(&b->syms);
         b->top_vis   = tom_top_visible(&b->syms);
         if (*b->syms.hires_n > 0 && *b->syms.game_w > 0)
         {
            b->nat_w = *b->syms.game_w / *b->syms.hires_n;
            b->nat_h = *b->syms.game_h / *b->syms.hires_n;
            gun.nat_w = b->nat_w;
            gun.nat_h = b->nat_h;
         }
         /* Aim at the object X Balloons ASSUMES its first calibration
          * target's hotspot occupies.  Everything the game decodes later
          * is relative to the LPH captured here, so this one choice is
          * what makes its crosshair agree with its own sprite table. */
         b->cal_col  = b->start_pos + B_CAL_HOTSPOT_X;

         /* Sweep the full visible width.  The point past the half-line
          * boundary is the whole reason this is a sweep -- see the header. */
         {
            int k;
            b->sweep_n = 21;
            for (k = 0; k < b->sweep_n; k++)
               b->sweep_cols[k] = (k * (b->nat_w - 1)) / (b->sweep_n - 1);
         }

         b->phase       = PH_CAL1;
         b->phase_frame = 0;
         return true;

      case PH_CAL1:
      {
         int row = (rd16(ram, B_OBJ_CALY) - b->top_vis) / 2;
         if (row < 0)
            row = 0;
         gun_aim(b->cal_col, row);
         gun.trigger = pulse_trigger(b->phase_frame);

         if (rd16(ram, B_CAL1) != 0)
         {
            b->cal1_done   = 1;
            b->phase       = PH_CAL2;
            b->phase_frame = 0;
         }
         else if (b->phase_frame > 400)
            return false;
         return true;
      }

      case PH_CAL2:
      {
         /* Target 2 is moved to object X $17C; aim at it for realism --
          * the game only uses this sample for its wrap-detect. */
         int col = b->start_pos + (int)rd16(ram, B_OBJ_CALX);
         int row = (rd16(ram, B_OBJ_CALY) - b->top_vis) / 2;

         if (col < 0)
            col = 0;
         if (col >= b->nat_w)
            col = b->nat_w - 1;
         if (row < 0)
            row = 0;

         gun_aim(col, row);
         gun.trigger = pulse_trigger(b->phase_frame);

         if (rd16(ram, B_CAL2) != 0)
         {
            b->cal2_done   = 1;
            b->phase       = PH_SWEEP;
            b->phase_frame = 0;
            b->sweep_idx   = 0;
         }
         else if (b->phase_frame > 400)
            return false;
         return true;
      }

      case PH_SWEEP:
      {
         /* Let the game finish switching screens first -- it redraws the
          * whole playfield between the second calibration shot and its
          * first gameplay LPH read, and the crosshair object still holds
          * calibration-screen values until then.  Then four frames per
          * sample: aim, two fields for the latch and the game's own
          * decode, read on the last. */
         int settle = 45;
         int slot;
         int col;
         int row = 40;

         gun.trigger = 0;
         gun_aim(b->sweep_cols[0], row);

         if ((int)b->phase_frame < settle)
            return true;

         slot = ((int)b->phase_frame - settle) / 4;

         if (slot >= b->sweep_n)
         {
            b->phase       = PH_HUNT;
            b->phase_frame = 0;
            return true;
         }

         col = b->sweep_cols[slot];
         gun_aim(col, row);

         if ((((int)b->phase_frame - settle) % 4) == 3)
         {
            int got_x  = (int16_t)rd16(ram, B_OBJ_CROSSX);
            int got_y  = (int16_t)rd16(ram, B_OBJ_CROSSY);
            int want_x = col - b->start_pos;
            int want_y = b->top_vis + row * 2 - B_Y_FUDGE;

            b->sweep_checked++;
            if (!b->sweep_bad && (got_x != want_x || got_y != want_y))
            {
               b->sweep_bad       = 1;
               b->sweep_bad_col   = col;
               b->sweep_bad_got   = got_x;
               b->sweep_bad_want  = want_x;
               b->sweep_bad_goty  = got_y;
               b->sweep_bad_wanty = want_y;
            }
         }
         return true;
      }

      case PH_HUNT:
      {
         /* Wait until the balloon has risen into the band where BOTH the
          * whole 48x128 half-line hit box and our aim point stay above
          * Balloons' own quit threshold. */
         int      obj_x = (int16_t)rd16(ram, B_OBJ_BALL_X);
         int      obj_y = (int16_t)rd16(ram, B_OBJ_BALL_Y);
         int      aim_hl, col, row;

         gun.trigger   = 0;
         gun.offscreen = 1;

         b->hunt_last_objx = obj_x;
         b->hunt_last_objy = obj_y;
         b->hunt_frames++;

         if (b->phase_frame > 1200)
            return false;

         /* Aim ~1/3 into the box in both axes. */
         aim_hl = obj_y + 40;
         if (aim_hl + B_Y_FUDGE >= B_QUIT_HALFLINE - 8)
            return true;

         row = (aim_hl + B_Y_FUDGE - b->top_vis) / 2;
         col = obj_x + 24 + b->start_pos;

         if (row < 0 || row >= b->nat_h)
            return true;
         if (col < 0 || col >= b->nat_w)
            return true;

         gun_aim(col, row);
         b->prev_balloon_y = (int)rd16(ram, B_BALLOON_Y);
         b->phase          = PH_SHOOT;
         b->phase_frame    = 0;
         return true;
      }

      case PH_SHOOT:
      {
         int y = (int)rd16(ram, B_BALLOON_Y);

         gun.trigger = pulse_trigger(b->phase_frame);
         if (b->phase_frame == 1)
            b->shots_fired++;

         /* $4724 (respawn) is the ONLY thing that raises this counter; it
          * otherwise falls by a random 1..7 every field.  Called on a hit
          * and on natural expiry, and PH_HUNT only shoots well before
          * expiry, so a jump upward here is the hit. */
         if (y > b->prev_balloon_y + 20)
         {
            b->hit_registered = 1;
            b->phase          = PH_DONE;
            return false;
         }
         b->prev_balloon_y = y;

         if (b->phase_frame > 30)
         {
            /* Missed (or the balloon moved out from under the shot) --
             * go round again on the next balloon. */
            b->phase       = PH_HUNT;
            b->phase_frame = 0;
            if (b->shots_fired >= 6)
               return false;
         }
         return true;
      }

      default:
         break;
   }

   return true;
}

static int run_partB(const char *core, const char *rom)
{
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   partB          b;
   int            i;

   memset(&b, 0, sizeof(b));

   cfg.core_path  = core;
   cfg.rom_path   = rom;
   cfg.frames     = 6000;
   cfg.quiet      = 1;
   cfg.input_callback      = gun_cb;
   cfg.frame_callback      = partB_frame;
   cfg.frame_callback_data = &b;

   harness_set_option(&cfg, "virtualjaguar_p1_device", "lightgun");

   if (!harness_load_core(&cfg))
   {
      record(0, "balloons_calibration", "could not load core %s", core);
      return 0;
   }
   if (!harness_load_rom(&cfg))
   {
      record(0, "balloons_calibration", "could not load ROM %s", rom);
      harness_shutdown(&cfg);
      return 0;
   }
   if (!core_syms_bind(&cfg, &b.syms))
   {
      record(0, "balloons_calibration",
             "test-ABI symbols missing (needs TEST_EXPORTS=1)");
      harness_shutdown(&cfg);
      return 0;
   }
   b.bound = 1;
   b.nat_w = 320;
   b.nat_h = 240;
   gun.nat_w = b.nat_w;
   gun.nat_h = b.nat_h;
   gun.offscreen = 1;

   /* Sweep columns are laid out after the geometry is known (PH_SETTLE),
    * but the count is fixed here. */
   b.sweep_n = 21;

   harness_run(&cfg);

   record(b.cal1_done && b.cal2_done, "balloons_calibration",
          "cal1=$%04X cal2=$%04X (both targets accepted: %s)",
          rd16(b.syms.ram, B_CAL1), rd16(b.syms.ram, B_CAL2),
          (b.cal1_done && b.cal2_done) ? "yes" : "NO");

   record(!b.quit_detected, "balloons_still_running",
          "%s", b.quit_detected
                ? "Balloons cleared VIDEN and exited -- aim went below its "
                  "$1BE half-line quit threshold"
                : "display stayed enabled for the whole run");

   if (b.sweep_bad)
      record(0, "balloons_crosshair_tracks",
             "col %d row 40 -> game computed object (%d,%d), want (%d,%d) "
             "(%d point(s) checked, startPos=%d topVis=%d)",
             b.sweep_bad_col, b.sweep_bad_got, b.sweep_bad_goty,
             b.sweep_bad_want, b.sweep_bad_wanty,
             b.sweep_checked, b.start_pos, b.top_vis);
   else
      record(b.sweep_checked > 0, "balloons_crosshair_tracks",
             "%d point(s) across the width; game-computed crosshair == the "
             "aimed pixel exactly (startPos=%d, topVis=%d, pwidth=%d)",
             b.sweep_checked, b.start_pos, b.top_vis, b.pwidth);

   if (b.hit_registered)
      record(1, "balloons_hit", "hit registered after %d shot(s)",
             b.shots_fired);
   else
      record(0, "balloons_hit",
             "no hit after %d shot(s); last balloon object (%d,%d), aimable "
             "band is object Y < %d half-lines and object X in [%d,%d), "
             "%d frame(s) spent waiting for one",
             b.shots_fired, b.hunt_last_objx, b.hunt_last_objy,
             B_QUIT_HALFLINE - 8 - B_Y_FUDGE - 40,
             -b.start_pos - 24, b.nat_w - b.start_pos - 24,
             b.hunt_frames);

   i = (b.cal1_done && b.cal2_done && !b.quit_detected
        && b.sweep_checked > 0 && !b.sweep_bad && b.hit_registered) ? 1 : 0;

   harness_shutdown(&cfg);
   return i;
}

/* ------------------------------------------------------------------ */

static int file_exists(const char *p)
{
   FILE *f = fopen(p, "rb");
   if (!f)
      return 0;
   fclose(f);
   return 1;
}

int main(int argc, char **argv)
{
   /* argv[2] lets the Makefile hand over whatever scripts/find-rom.sh
    * located, so a differently-spelled corpus is not a silent skip. */
   const char *core = (argc > 1) ? argv[1] : DEFAULT_CORE;
   const char *ball = (argc > 2) ? argv[2] : BALLOONS_ROM;
   const char *pub  = PUBLIC_ROM;
   uint16_t    lph1[4], lpv1[4], lph2[4], lpv2[4];
   int         have_balloons;
   int         i, same = 1;
   harness_config rep = HARNESS_CONFIG_DEFAULT;

   have_balloons = file_exists(ball);

   memset(lph1, 0, sizeof(lph1));
   memset(lpv1, 0, sizeof(lpv1));
   memset(lph2, 0, sizeof(lph2));
   memset(lpv2, 0, sizeof(lpv2));

   if (file_exists(pub))
   {
      run_partA(core, pub, 0, lph1, lpv1, 1);
      run_partA(core, pub, 1, lph2, lpv2, 0);

      for (i = 0; i < 4; i++)
         if (lph1[i] != lph2[i] || lpv1[i] != lpv2[i])
            same = 0;

      if (same)
         record(1, "lightgun_hires_identity",
                "1x and 2x latch identical LPH/LPV for the same normalised "
                "aim (first %u/%u, last %u/%u)",
                lph1[0], lpv1[0], lph1[3], lpv1[3]);
      else
         record(0, "lightgun_hires_identity",
                "2x moved the aim: 1x %u/%u vs 2x %u/%u",
                lph1[3], lpv1[3], lph2[3], lpv2[3]);
   }
   else
      fprintf(stderr, "test_lightgun: %s missing, skipping part A\n", pub);

   if (have_balloons)
      run_partB(core, ball);

   rep.quiet = 0;
   harness_report(&rep, results, nresults);

   if (failures)
      return 1;

   if (!have_balloons)
   {
      fprintf(stderr, "test_lightgun: %s not present -- ROM-based checks "
                      "skipped\n", ball);
      return 77;
   }

   return 0;
}
