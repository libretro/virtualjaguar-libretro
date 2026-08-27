/* audio_wav_dump.c -- dump the core's raw audio stream to a 48 kHz stereo
 * 16-bit WAV, from an arbitrary point in a game.
 *
 * Every existing audio tool reduces the stream to a statistic (RMS, peak,
 * saturation density).  Those catch loud-broken and silent-broken audio but
 * are blind to a *spectral* complaint -- "a strange parasitic noise running
 * during gameplay" (issue: AvP) is a tone at some frequency sitting under
 * otherwise correct audio, and every one of those statistics reads normal
 * while it plays.  This writes the samples out so they can be listened to or
 * run through an FFT.
 *
 * It reuses the shared harness purely for its CLI: --load-state gets you into
 * gameplay from a RetroArch save, which is where these reports always come
 * from and where no other audio tool can currently go.  The harness installs
 * its own audio callback during harness_load_rom(); we install ours after,
 * and the core keeps whichever was set last.
 *
 * It also reports the per-frame sample cadence, because the other half of a
 * "noise on Windows/Linux but not here" report is the frontend resampler: a
 * core that emits a jittery sample count per frame can whine on a strict
 * resampler and sound clean on a forgiving one, with an identical stream.
 *
 * Usage:
 *   ./test/tools/audio_wav_dump <core> <rom> --out FILE.wav [--frames N]
 *       [--load-state F] [--press F:BTN[:HOLD]] [--option K=V]
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include \
 *      -o test/tools/audio_wav_dump test/tools/audio_wav_dump.c \
 *      test/harness/harness.c -ldl -lm
 */

#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define WAV_RATE 48000

static FILE    *g_fp;
static uint64_t g_frames_written;   /* sample frames, i.e. L+R pairs */

/* Per-video-frame sample cadence. */
#define CAD_MAX 20000
static uint32_t g_cad[CAD_MAX];
static unsigned g_cad_n;
static uint64_t g_cad_last;

static void put32(FILE *f, uint32_t v)
{
   fputc((int)( v        & 0xFF), f);
   fputc((int)((v >>  8) & 0xFF), f);
   fputc((int)((v >> 16) & 0xFF), f);
   fputc((int)((v >> 24) & 0xFF), f);
}

static void put16(FILE *f, uint16_t v)
{
   fputc((int)( v       & 0xFF), f);
   fputc((int)((v >> 8) & 0xFF), f);
}

/* Header is rewritten at close once the true length is known. */
static void wav_header(FILE *f, uint64_t sample_frames)
{
   uint32_t data_bytes = (uint32_t)(sample_frames * 4);
   fwrite("RIFF", 1, 4, f);  put32(f, 36 + data_bytes);
   fwrite("WAVE", 1, 4, f);
   fwrite("fmt ", 1, 4, f);  put32(f, 16);
   put16(f, 1);              /* PCM               */
   put16(f, 2);              /* stereo            */
   put32(f, WAV_RATE);
   put32(f, WAV_RATE * 4);   /* byte rate         */
   put16(f, 4);              /* block align       */
   put16(f, 16);             /* bits per sample   */
   fwrite("data", 1, 4, f);  put32(f, data_bytes);
}

static size_t audio_batch(const int16_t *data, size_t frames)
{
   if (g_fp && frames)
   {
      fwrite(data, sizeof(int16_t) * 2, frames, g_fp);
      g_frames_written += frames;
   }
   return frames;
}

/* Called once per retro_run; bank the samples this frame produced. */
static bool cadence_frame(void *user, unsigned frame)
{
   (void)user; (void)frame;
   if (g_cad_n < CAD_MAX)
      g_cad[g_cad_n++] = (uint32_t)(g_frames_written - g_cad_last);
   g_cad_last = g_frames_written;
   return true;
}

static void audio_sample(int16_t l, int16_t r)
{
   int16_t pair[2];
   pair[0] = l;
   pair[1] = r;
   audio_batch(pair, 1);
}

int main(int argc, char **argv)
{
   harness_config cfg;
   const char *out = NULL;
   void (*set_batch)(size_t (*)(const int16_t *, size_t));
   void (*set_sample)(void (*)(int16_t, int16_t));
   int i;

   /* --out is ours, not the harness's; strip nothing, the harness ignores
    * flags it does not know (which is also why a typo in --option is silent
    * -- see docs/agent/testing.md). */
   for (i = 1; i < argc - 1; i++)
      if (strcmp(argv[i], "--out") == 0)
         out = argv[i + 1];

   if (!out)
   {
      fprintf(stderr,
              "usage: %s <core> <rom> --out FILE.wav [--frames N] "
              "[--load-state F] [--press F:BTN[:HOLD]] [--option K=V]\n",
              argv[0]);
      return 2;
   }

   if (!harness_init_from_args(&cfg, argc, argv))
      return 2;
   if (!harness_load_core(&cfg) || !harness_load_rom(&cfg))
      return 1;

   g_fp = fopen(out, "wb");
   if (!g_fp)
   {
      fprintf(stderr, "audio_wav_dump: cannot write '%s'\n", out);
      harness_shutdown(&cfg);
      return 1;
   }
   wav_header(g_fp, 0);

   /* After harness_load_rom(), so ours is the callback the core keeps. */
   set_batch  = (void (*)(size_t (*)(const int16_t *, size_t)))
                harness_dlsym(&cfg, "retro_set_audio_sample_batch");
   set_sample = (void (*)(void (*)(int16_t, int16_t)))
                harness_dlsym(&cfg, "retro_set_audio_sample");
   if (!set_batch)
   {
      fprintf(stderr, "audio_wav_dump: core has no "
                      "retro_set_audio_sample_batch\n");
      fclose(g_fp);
      harness_shutdown(&cfg);
      return 1;
   }
   cfg.frame_callback = cadence_frame;
   set_batch(audio_batch);
   if (set_sample)
      set_sample(audio_sample);

   harness_run(&cfg);

   fseek(g_fp, 0, SEEK_SET);
   wav_header(g_fp, g_frames_written);
   fclose(g_fp);

   printf("WAV %s  sample_frames=%llu  seconds=%.2f\n", out,
          (unsigned long long)g_frames_written,
          (double)g_frames_written / (double)WAV_RATE);

   if (g_cad_n)
   {
      uint32_t lo = 0xFFFFFFFFu, hi = 0;
      double   sum = 0.0;
      unsigned k;
      /* Skip frame 0: it banks whatever the load/reset emitted. */
      for (k = 1; k < g_cad_n; k++)
      {
         if (g_cad[k] < lo) lo = g_cad[k];
         if (g_cad[k] > hi) hi = g_cad[k];
         sum += (double)g_cad[k];
      }
      if (g_cad_n > 1)
         printf("CADENCE frames=%u  samples/frame min=%u max=%u mean=%.2f "
                "spread=%u\n", g_cad_n - 1, lo, hi,
                sum / (double)(g_cad_n - 1), hi - lo);
   }

   harness_shutdown(&cfg);
   return 0;
}
