/* headless flicker detector — captures sliding window of frames,
 * computes per-pixel temporal stddev, flicker score timeline,
 * histogram, downsampled spatial flicker map.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <dlfcn.h>
#include "libretro-common/include/libretro.h"

#define WIN 16
#define WARMUP 60
#define MAX_FRAMES 8192
#define MAX_W 1024
#define MAX_H 512
#define DS_W 32
#define DS_H 24

static unsigned cur_frame = 0;
static unsigned total_frames = 600;
static const char *out_prefix = "/tmp/flicker";
static const char *label = "rom";
static int use_bios = 0;
static unsigned vw = 320, vh = 240;

#define MAX_RANGES 16
struct range { unsigned start, end; unsigned id; };
static struct range presses[MAX_RANGES];
static unsigned n_presses = 0;

/* sliding window: store luminance only (uint8_t) per pixel */
static uint8_t window[WIN][MAX_W * MAX_H];
static int win_filled = 0;
static int win_idx = 0;

/* spatial accumulator (downsampled) */
static double spatial_var_sum[DS_W * DS_H];
static unsigned spatial_count = 0;

/* per-frame flicker score (mean stddev) */
static double frame_score[MAX_FRAMES];
/* histogram counts per frame at thresholds {1,8,32,96,128} */
static unsigned hist_counts[MAX_FRAMES][5];

static unsigned cur_w = 0, cur_h = 0;

static inline uint8_t lum_xrgb8888(uint32_t px) {
   /* rough Y' = (R+2G+B)/4 — fast approx */
   unsigned r = (px >> 16) & 0xFF;
   unsigned g = (px >> 8) & 0xFF;
   unsigned b = px & 0xFF;
   return (uint8_t)((r + (g << 1) + b) >> 2);
}

static void compute_window_stats(void) {
   /* Compute per-pixel stddev across WIN frames in window.
    * For efficiency we compute mean, then sumsqdiff, then stddev.
    * Output: mean stddev across frame, histogram counts, and
    * accumulate into spatial map.
    */
   unsigned npix = cur_w * cur_h;
   double total = 0.0;
   unsigned counts[5] = {0,0,0,0,0};
   /* tile sums for spatial */
   double tile_sum[DS_W * DS_H];
   unsigned tile_n[DS_W * DS_H];
   memset(tile_sum, 0, sizeof tile_sum);
   memset(tile_n, 0, sizeof tile_n);

   unsigned p;
   for (p = 0; p < npix; p++) {
      unsigned k;
      unsigned sum = 0;
      for (k = 0; k < WIN; k++) sum += window[k][p];
      double mean = sum / (double)WIN;
      double sq = 0.0;
      for (k = 0; k < WIN; k++) {
         double d = (double)window[k][p] - mean;
         sq += d * d;
      }
      double std = sqrt(sq / WIN);
      total += std;
      if (std > 1.0)   counts[0]++;
      if (std > 8.0)   counts[1]++;
      if (std > 32.0)  counts[2]++;
      if (std > 96.0)  counts[3]++;
      if (std > 128.0) counts[4]++;
      /* spatial bucket */
      unsigned y = p / cur_w;
      unsigned x = p - y * cur_w;
      unsigned tx = (x * DS_W) / cur_w;
      unsigned ty = (y * DS_H) / cur_h;
      if (tx >= DS_W) tx = DS_W - 1;
      if (ty >= DS_H) ty = DS_H - 1;
      tile_sum[ty * DS_W + tx] += std;
      tile_n[ty * DS_W + tx]++;
   }
   double mean_std = total / npix;
   if (cur_frame < MAX_FRAMES) {
      frame_score[cur_frame] = mean_std;
      hist_counts[cur_frame][0] = counts[0];
      hist_counts[cur_frame][1] = counts[1];
      hist_counts[cur_frame][2] = counts[2];
      hist_counts[cur_frame][3] = counts[3];
      hist_counts[cur_frame][4] = counts[4];
   }
   /* accumulate spatial map */
   unsigned i;
   for (i = 0; i < DS_W * DS_H; i++) {
      if (tile_n[i] > 0) {
         spatial_var_sum[i] += tile_sum[i] / tile_n[i];
      }
   }
   spatial_count++;
}

static void video_refresh(const void *data, unsigned w, unsigned h, size_t pitch) {
   if (!data) return;
   if (w > MAX_W || h > MAX_H) return;
   cur_w = w; cur_h = h;
   vw = w; vh = h;
   /* fill current slot in sliding window */
   uint8_t *slot = window[win_idx];
   unsigned y, x;
   for (y = 0; y < h; y++) {
      const uint32_t *row = (const uint32_t*)((const uint8_t*)data + y * pitch);
      uint8_t *out = slot + y * w;
      for (x = 0; x < w; x++) {
         out[x] = lum_xrgb8888(row[x]);
      }
   }
   win_idx = (win_idx + 1) % WIN;
   if (win_idx == 0) win_filled = 1;
   /* once filled and past warmup, compute stats this frame */
   if (win_filled && cur_frame >= WARMUP) {
      compute_window_stats();
   }
}

static void as(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t ab(const int16_t *d, size_t f) { (void)d; return f; }
static void ip(void) {}
static int16_t is_cb(unsigned p, unsigned d, unsigned i, unsigned id) {
   (void)p; (void)d; (void)i;
   unsigned k;
   for (k = 0; k < n_presses; k++) {
      if (cur_frame >= presses[k].start && cur_frame <= presses[k].end && id == presses[k].id) return 1;
   }
   return 0;
}
static void lp(enum retro_log_level lv, const char *f, ...) { (void)lv; (void)f; }
static struct retro_log_callback lcb = { lp };
static bool env(unsigned cmd, void *data) {
   switch (cmd) {
   case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: *(struct retro_log_callback*)data = lcb; return true;
   case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return true;
   case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
   case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char**)data = "/tmp"; return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE: {
      struct retro_variable *x = data;
      if (x->key && !strcmp(x->key, "virtualjaguar_bios")) { x->value = use_bios ? "enabled" : "disabled"; return true; }
      x->value = NULL; return false;
   }
   default: return true;
   }
}

static void write_outputs(void) {
   /* stats files: <prefix>_<label>.txt + ppm spatial map */
   char path[1024];
   snprintf(path, sizeof path, "%s_%s.txt", out_prefix, label);
   FILE *f = fopen(path, "w");
   if (!f) return;
   fprintf(f, "# Flicker report for %s\n", label);
   fprintf(f, "frames=%u  res=%ux%u  warmup=%u  window=%u\n", total_frames, vw, vh, WARMUP, WIN);
   /* timeline: ~30 samples */
   unsigned start = WARMUP + WIN;
   unsigned end = (cur_frame < MAX_FRAMES) ? cur_frame : MAX_FRAMES - 1;
   if (end <= start) end = start + 1;
   unsigned step = (end - start) / 30;
   if (step < 1) step = 1;
   fprintf(f, "\n## Timeline (frame: mean_stddev, px>1, px>8, px>32, px>96, px>128)\n");
   double max_score = 0.0; unsigned max_frame = start;
   double total_score = 0.0; unsigned n = 0;
   unsigned fr;
   for (fr = start; fr <= end; fr++) {
      if (frame_score[fr] > max_score) { max_score = frame_score[fr]; max_frame = fr; }
      total_score += frame_score[fr]; n++;
   }
   for (fr = start; fr <= end; fr += step) {
      fprintf(f, "frame %4u: %7.3f  %6u %6u %6u %6u %6u\n",
              fr, frame_score[fr],
              hist_counts[fr][0], hist_counts[fr][1], hist_counts[fr][2],
              hist_counts[fr][3], hist_counts[fr][4]);
   }
   fprintf(f, "\nmean_score_over_run=%.4f  peak_score=%.4f@frame%u\n",
           total_score / (n ? n : 1), max_score, max_frame);

   /* rolling mean (window=16 frames) */
   fprintf(f, "\n## Rolling mean (16-frame window) — peaks indicate bursts\n");
   unsigned roll_step = (end - start) / 30;
   if (roll_step < 1) roll_step = 1;
   for (fr = start + 16; fr <= end; fr += roll_step) {
      double s = 0.0; unsigned k;
      for (k = 0; k < 16; k++) s += frame_score[fr - k];
      fprintf(f, "frame %4u: rolling=%7.3f\n", fr, s / 16.0);
   }

   /* spatial top-N regions */
   fprintf(f, "\n## Spatial flicker map (downsampled %ux%u)\n", DS_W, DS_H);
   double smap[DS_W * DS_H];
   unsigned i;
   double smax = 0.0;
   for (i = 0; i < DS_W * DS_H; i++) {
      smap[i] = spatial_count > 0 ? spatial_var_sum[i] / spatial_count : 0.0;
      if (smap[i] > smax) smax = smap[i];
   }
   /* find top 3 tiles */
   unsigned top[3] = {0,0,0};
   double topv[3] = {-1.0, -1.0, -1.0};
   for (i = 0; i < DS_W * DS_H; i++) {
      double v = smap[i];
      if (v > topv[0]) { topv[2]=topv[1]; top[2]=top[1]; topv[1]=topv[0]; top[1]=top[0]; topv[0]=v; top[0]=i; }
      else if (v > topv[1]) { topv[2]=topv[1]; top[2]=top[1]; topv[1]=v; top[1]=i; }
      else if (v > topv[2]) { topv[2]=v; top[2]=i; }
   }
   for (i = 0; i < 3; i++) {
      unsigned t = top[i];
      unsigned ty = t / DS_W, tx = t - ty * DS_W;
      unsigned px = (tx * vw) / DS_W;
      unsigned py = (ty * vh) / DS_H;
      unsigned pw = vw / DS_W;
      unsigned ph = vh / DS_H;
      fprintf(f, "rank %u: tile(%u,%u)  bbox=(x=%u,y=%u,w=%u,h=%u)  score=%.3f\n",
              i + 1, tx, ty, px, py, pw, ph, topv[i]);
   }
   fclose(f);

   /* PPM spatial map (DS_W x DS_H, scaled to 0-255 by max) */
   snprintf(path, sizeof path, "%s_%s.ppm", out_prefix, label);
   f = fopen(path, "w");
   if (!f) return;
   fprintf(f, "P3\n%d %d\n255\n", DS_W, DS_H);
   for (i = 0; i < DS_W * DS_H; i++) {
      unsigned v = smax > 0 ? (unsigned)(smap[i] * 255.0 / smax) : 0;
      if (v > 255) v = 255;
      fprintf(f, "%u %u %u ", v, v / 2, 0);
      if ((i + 1) % DS_W == 0) fputc('\n', f);
   }
   fclose(f);
   fprintf(stderr, "[%s] mean=%.3f peak=%.3f@%u  -> %s_%s.{txt,ppm}\n",
           label, total_score / (n ? n : 1), max_score, max_frame, out_prefix, label);
}

int main(int argc, char **argv) {
   if (argc < 3) {
      fprintf(stderr, "usage: %s <core> <rom> [--frames N] [--prefix P] [--label L] [--bios] [--press-X A-B]\n", argv[0]);
      return 1;
   }
   int i;
   for (i = 3; i < argc; i++) {
      if (!strcmp(argv[i], "--frames") && i + 1 < argc) total_frames = atoi(argv[++i]);
      else if (!strcmp(argv[i], "--prefix") && i + 1 < argc) out_prefix = argv[++i];
      else if (!strcmp(argv[i], "--label") && i + 1 < argc) label = argv[++i];
      else if (!strcmp(argv[i], "--bios")) use_bios = 1;
      else if (i + 1 < argc && !strncmp(argv[i], "--press-", 8) && n_presses < MAX_RANGES) {
         unsigned id = 0;
         if (!strcmp(argv[i], "--press-a")) id = RETRO_DEVICE_ID_JOYPAD_A;
         else if (!strcmp(argv[i], "--press-b")) id = RETRO_DEVICE_ID_JOYPAD_B;
         else if (!strcmp(argv[i], "--press-start")) id = RETRO_DEVICE_ID_JOYPAD_START;
         else if (!strcmp(argv[i], "--press-up")) id = RETRO_DEVICE_ID_JOYPAD_UP;
         else if (!strcmp(argv[i], "--press-down")) id = RETRO_DEVICE_ID_JOYPAD_DOWN;
         else continue;
         unsigned a = 0, b = 0;
         if (sscanf(argv[++i], "%u-%u", &a, &b) == 2) {
            presses[n_presses].start = a; presses[n_presses].end = b;
            presses[n_presses].id = id; n_presses++;
         }
      }
   }
   if (total_frames >= MAX_FRAMES) total_frames = MAX_FRAMES - 1;

   void *h = dlopen(argv[1], RTLD_NOW);
   if (!h) { fprintf(stderr, "%s\n", dlerror()); return 1; }
   void (*set_env)(retro_environment_t) = dlsym(h, "retro_set_environment");
   void (*init)(void) = dlsym(h, "retro_init");
   void (*set_v)(retro_video_refresh_t) = dlsym(h, "retro_set_video_refresh");
   void (*set_a)(retro_audio_sample_t) = dlsym(h, "retro_set_audio_sample");
   void (*set_ab)(retro_audio_sample_batch_t) = dlsym(h, "retro_set_audio_sample_batch");
   void (*set_ip)(retro_input_poll_t) = dlsym(h, "retro_set_input_poll");
   void (*set_is)(retro_input_state_t) = dlsym(h, "retro_set_input_state");
   bool (*lg)(const struct retro_game_info*) = dlsym(h, "retro_load_game");
   void (*ulg)(void) = dlsym(h, "retro_unload_game");
   void (*run)(void) = dlsym(h, "retro_run");
   set_env(env); init(); set_v(video_refresh); set_a(as); set_ab(ab); set_ip(ip); set_is(is_cb);
   FILE *f = fopen(argv[2], "rb");
   if (!f) { fprintf(stderr, "open rom failed: %s\n", argv[2]); return 1; }
   fseek(f, 0, SEEK_END); size_t sz = ftell(f); fseek(f, 0, SEEK_SET);
   uint8_t *rd = malloc(sz);
   if (fread(rd, 1, sz, f) != sz) { return 1; }
   fclose(f);
   struct retro_game_info gi = {0};
   gi.path = argv[2]; gi.data = rd; gi.size = sz;
   if (!lg(&gi)) { fprintf(stderr, "load failed\n"); return 1; }
   for (cur_frame = 0; cur_frame < total_frames; cur_frame++) run();
   ulg();
   write_outputs();
   return 0;
}
