/* test/tools/dsp_halt_backtrace.c -- who jumped to the DSP's shutdown
 * routine?
 *
 * Issue #635 (White Men Can't Jump).  In HLE mode the game's DSP program
 * executes ~1,319 opcodes at startup, jumps to an orderly shutdown block
 * at $F1B76A (disable INT_ENA0/1, clear the latches, write $20 to D_CTRL
 * so DSPGO clears), and parks forever in a 3-instruction `jr` loop at
 * $F1B78C.  ~1,275 frames later the 68K posts command $0D to $F1B2C0 and
 * spins for an ack that a stopped DSP can never write.
 *
 * The three transfers that could reach $F1B76A are all REGISTER-INDIRECT
 * (`jump T, (r10)`, `jump T, (r24)`, `jump NZ, (r1)`), so static
 * disassembly cannot name the caller -- the target is computed at
 * runtime.  This tool reads the runtime PC history instead.
 *
 * Why a plain end-of-run dump is sufficient, rather than trapping the
 * 1->0 transition of DSPGO: once the DSP halts it executes nothing more,
 * so its PC ring STOPS being written and preserves the last up-to-1024
 * PCs leading into the shutdown.  Sampling later cannot lose them.  The
 * tool still reports the frame at which DSPGO cleared, and refuses to
 * print a backtrace if the DSP never stopped (in which case the ring is
 * live and the newest entries would be unrelated).
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
 *      -o test/tools/dsp_halt_backtrace test/tools/dsp_halt_backtrace.c \
 *      test/harness/harness.c -ldl -lm
 * Run (HLE mode is the default and is where the bug lives):
 *   ./test/tools/dsp_halt_backtrace ./virtualjaguar_libretro.dylib rom.jag \
 *      --frames 60 [--depth 64]
 * Needs a TEST_EXPORTS=1 core (dsp_control and vjtrace_backtrace are
 * hidden otherwise).
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../harness/harness.h"

/* src/core/vjag_memory.h: enum { UNKNOWN, JAGUAR, DSP, GPU, ... } */
#define WHO_DSP 2

#define MAX_DEPTH 1024

static void (*p_backtrace)(int, uint32_t *, int, int *);
static void (*p_arm)(void);
static uint32_t *p_dsp_control;

static int  prev_running = -1;
static int  halt_frame   = -1;
static uint32_t frames_seen;

/* harness_frame_cb: bool (*)(void *userdata, unsigned frame).  Returning
 * true keeps the run going. */
static bool on_frame(void *userdata, unsigned frame)
{
   int running;

   (void)userdata;
   frames_seen = frame;
   if (!p_dsp_control)
      return true;

   running = (*p_dsp_control & 0x01) ? 1 : 0;
   if (prev_running == 1 && running == 0 && halt_frame < 0)
      halt_frame = (int)frame;
   prev_running = running;
   return true;
}

int main(int argc, char **argv)
{
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   uint32_t pcs[MAX_DEPTH];
   int count = 0, depth = 64, i;

   for (i = 1; i < argc; i++)
   {
      if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc)
         depth = atoi(argv[i + 1]);
   }
   if (depth < 1 || depth > MAX_DEPTH)
   {
      fprintf(stderr, "--depth must be 1..%d\n", MAX_DEPTH);
      return 1;
   }

   cfg.frames = 60;
   cfg.quiet  = 1;
   if (!harness_init_from_args(&cfg, argc, argv))
      return 1;
   if (!cfg.rom_path)
   {
      fprintf(stderr, "dsp_halt_backtrace: no ROM given\n");
      return 1;
   }

   p_backtrace = (void (*)(int, uint32_t *, int, int *))
      harness_dlsym(&cfg, "vjtrace_backtrace");
   p_dsp_control = (uint32_t *)harness_dlsym(&cfg, "dsp_control");
   /* The PC history ring is gated on vjtrace_armed, which defaults OFF --
    * an unarmed core records nothing and the backtrace comes back EMPTY,
    * which reads exactly like "the DSP executed nothing".  Arm it before
    * the ROM loads so the very first DSP instructions are captured; the
    * halt happens within the first frame or two. */
   p_arm = (void (*)(void))harness_dlsym(&cfg, "vjtrace_arm");
   if (p_arm)
      p_arm();
   else
      fprintf(stderr, "dsp_halt_backtrace: WARNING -- vjtrace_arm not "
                      "exported; the backtrace will be empty\n");

   if (!p_backtrace || !p_dsp_control)
   {
      fprintf(stderr, "dsp_halt_backtrace: vjtrace_backtrace/dsp_control not "
                      "exported -- rebuild with `make TEST_EXPORTS=1`\n");
      return 1;
   }

   cfg.frame_callback = on_frame;
   if (!harness_load_rom(&cfg))
      return 1;
   harness_run(&cfg);

   printf("dsp_control=$%08X (DSPGO=%u)\n",
          (unsigned)*p_dsp_control, (unsigned)(*p_dsp_control & 1));

   if (*p_dsp_control & 1)
   {
      /* Refuse rather than mislead: a running DSP keeps overwriting the
       * ring, so the newest entries are wherever it happens to be now --
       * not the path into any shutdown. */
      printf("DSP is STILL RUNNING after %u frames -- no halt to explain.\n",
             (unsigned)frames_seen);
      printf("(backtrace withheld: the ring is live and would show current "
             "execution, not a halt path)\n");
      return 2;
   }

   if (halt_frame >= 0)
      printf("DSPGO cleared during frame %d\n", halt_frame);
   else
      printf("DSPGO was already clear at the first sampled frame "
             "(halt happened before frame 1, or mid-frame before sampling)\n");

   p_backtrace(WHO_DSP, pcs, depth, &count);
   /* vjt_backtrace_ring returns OLDEST first: the last entry is the last
    * instruction executed.  Verified empirically -- the final D_CTRL store
    * at $F1B78A lands in the LAST slot, not the first. */
   printf("\nDSP backtrace, OLDEST first (last entry = last instruction "
          "executed, %d entries):\n", count);
   for (i = 0; i < count; i++)
   {
      const char *tag = "";
      if (pcs[i] == 0x00F1B78C || pcs[i] == 0x00F1B78E
          || pcs[i] == 0x00F1B790)
         tag = "   <- park loop";
      else if (pcs[i] == 0x00F1B782 || pcs[i] == 0x00F1B784
               || pcs[i] == 0x00F1B78A)
         tag = "   <- D_CTRL store (DSPGO=0)";
      else if (pcs[i] == 0x00F1B76A)
         tag = "   <- SHUTDOWN ENTRY (caller is the line ABOVE)";
      printf("  [%3d] $%08X%s\n", i, (unsigned)pcs[i], tag);
   }

   if (count == 0)
      printf("  (empty -- the DSP PC ring recorded nothing; VJ_TRACE may be "
             "disarmed in this build)\n");

   harness_shutdown(&cfg);
   return 0;
}
