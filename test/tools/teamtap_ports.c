/*
 * test/tools/teamtap_ports.c -- Team Tap frontend plumbing + savestate (#513).
 *
 * WHAT THIS COVERS THAT joymatrix_identity DOES NOT
 * =================================================
 * joymatrix_identity proves the DECODE: the right row codes reach the right
 * sockets, the detect bit answers both ways, and nothing moves with no tap
 * selected.  It does that by writing joypad0Buttons[] directly and running
 * zero frames, which is exactly what makes its digests reproducible -- and
 * exactly why it never executes either of the two paths below.
 *
 * 1. THE FRONTEND -> SOCKET FILL.  update_teamtap_input() in libretro.c maps
 *    frontend ports 3-5 to Jaguar port 1's tap sockets 1-3, and ports 6-8 to
 *    Jaguar port 2's.  Nothing else exercises teamtap_user[][], the socket
 *    loop, or teamtap_map[].  Without this, "the decode works" and "the
 *    feature works for a user" are different claims and only the first has
 *    evidence.
 *
 * 2. THE SAVESTATE CHUNK.  JoystickTeamTapStateSave() is the LAST thing
 *    retro_serialize writes, so nothing after it can be misaligned by a
 *    chunk that writes zero bytes -- test_state_compat and
 *    test_runahead_determinism both pass with a save/load that carries
 *    nothing at all, because both run with no tap and all-zero sockets.
 *    Requirement: what a pad behind the adapter holds is machine-visible at
 *    $F14000 the instant a title selects its row code, so it has to survive
 *    a state round trip or run-ahead and netplay diverge (cf. #400, #479).
 *
 * Both run against the committed test/roms/yarc.j64, so this is a CI test,
 * unlike test/tools/teamtap_rom_probe (private corpus).
 *
 * USAGE
 *   make TEST_EXPORTS=1 test/tools/teamtap_ports
 *   ./test/tools/teamtap_ports ./virtualjaguar_libretro.dylib test/roms/yarc.j64
 *
 * Exit codes: 0 = pass, 1 = fail, 2 = harness error.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../harness/harness.h"
#include "../../src/jerry/joystick.h"
#include "state.h"
#include "libretro.h"

#define TAP_TEST_FRAMES 40
/* The scripted press has to still be held on the LAST frame: update_input()
 * clears every slot at the top of each frame and refills from the current
 * frame's input, so what the arrays hold after harness_run() is the final
 * frame's state and nothing earlier. */
#define PRESS_FIRST     1
#define PRESS_HOLD      (TAP_TEST_FRAMES + 8)

typedef bool (*serialize_fn)(void *data, size_t size);
typedef bool (*unserialize_fn)(const void *data, size_t size);

static harness_result results[8];
static char           details[8][192];
static unsigned       num_results;

static void check(int ok, const char *name, const char *fmt, ...)
{
   va_list ap;

   va_start(ap, fmt);
   vsnprintf(details[num_results], sizeof(details[0]), fmt, ap);
   va_end(ap);

   results[num_results].status = ok ? "PASS" : "FAIL";
   results[num_results].name   = name;
   results[num_results].detail = details[num_results];
   num_results++;
}

int main(int argc, char **argv)
{
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   uint8_t *p0, *p1;
   void    *state;
   serialize_fn   ser;
   unserialize_fn unser;
   unsigned s1_up, s2_up, s3_up, p2s1_up, s1_a;
   int all_ok;
   unsigned i;

   cfg.frames = TAP_TEST_FRAMES;
   if (!harness_init_from_args(&cfg, argc, argv))
      return 2;

   if (!cfg.rom_path)
      cfg.rom_path = "test/roms/yarc.j64";

   /* Taps on BOTH ports, so the port-2 half of teamtap_user[][] is
    * exercised too -- a transposed row there would otherwise only show up
    * as "player 6 does nothing", in a frontend, months later. */
   harness_set_option(&cfg, "virtualjaguar_p1_device", "teamtap");
   harness_set_option(&cfg, "virtualjaguar_p2_device", "teamtap");

   /* One distinct button per socket, so a fill that lands in the wrong
    * socket cannot be mistaken for a pass. */
   harness_press(&cfg, 2, RETRO_DEVICE_ID_JOYPAD_UP,   PRESS_FIRST, PRESS_HOLD);
   harness_press(&cfg, 3, RETRO_DEVICE_ID_JOYPAD_DOWN, PRESS_FIRST, PRESS_HOLD);
   harness_press(&cfg, 4, RETRO_DEVICE_ID_JOYPAD_A,    PRESS_FIRST, PRESS_HOLD);
   harness_press(&cfg, 5, RETRO_DEVICE_ID_JOYPAD_B,    PRESS_FIRST, PRESS_HOLD);

   if (!harness_load_rom(&cfg))
   {
      harness_shutdown(&cfg);
      return 2;
   }

   p0    = (uint8_t *)harness_dlsym(&cfg, "joypad0Buttons");
   p1    = (uint8_t *)harness_dlsym(&cfg, "joypad1Buttons");
   ser   = (serialize_fn)harness_dlsym(&cfg, "retro_serialize");
   unser = (unserialize_fn)harness_dlsym(&cfg, "retro_unserialize");

   if (!p0 || !p1 || !ser || !unser)
   {
      fprintf(stderr,
              "teamtap_ports: missing test-ABI symbols (joypad0Buttons / "
              "joypad1Buttons / retro_serialize / retro_unserialize).  "
              "Build with TEST_EXPORTS=1.\n");
      harness_shutdown(&cfg);
      return 2;
   }

   harness_run(&cfg);

   /* ---- 1. frontend port -> tap socket ------------------------------ */

   s1_up   = p0[1 * JOYPAD_SOCKET_SLOTS + BUTTON_U];
   s2_up   = p0[2 * JOYPAD_SOCKET_SLOTS + BUTTON_D];
   s3_up   = p0[3 * JOYPAD_SOCKET_SLOTS + BUTTON_A];
   p2s1_up = p1[1 * JOYPAD_SOCKET_SLOTS + BUTTON_B];

   check(s1_up && s2_up && s3_up,
         "teamtap_port_to_socket_p1",
         "frontend ports 3/4/5 -> Jaguar port 1 sockets 1/2/3: "
         "Up=%u Down=%u A=%u (all must be non-zero)",
         s1_up, s2_up, s3_up);

   check(p2s1_up != 0,
         "teamtap_port_to_socket_p2",
         "frontend port 6 -> Jaguar port 2 socket 1: B=%u (must be non-zero)",
         p2s1_up);

   /* Socket 0 must NOT have picked any of them up.  A fill that ignored
    * the socket index would set socket 0 and satisfy nothing else here --
    * frontend ports 3-6 are not port 1's own pad. */
   check(p0[BUTTON_U] == 0 && p0[BUTTON_D] == 0 && p0[BUTTON_A] == 0
         && p1[BUTTON_B] == 0,
         "teamtap_socket0_untouched",
         "socket 0 of both ports saw none of ports 3-6's presses "
         "(p0 Up=%u Down=%u A=%u, p1 B=%u)",
         p0[BUTTON_U], p0[BUTTON_D], p0[BUTTON_A], p1[BUTTON_B]);

   /* ---- 2. savestate round trip ------------------------------------- */

   state = malloc(STATE_SIZE);
   if (!state)
   {
      harness_shutdown(&cfg);
      return 2;
   }

   /* Seed a distinctive pattern across every tap slot of both ports, so a
    * chunk that saves the wrong range (or nothing) cannot survive by
    * accident.  Socket 0 is left as the run left it. */
   for (i = JOYPAD_SOCKET_SLOTS; i < JOYPAD_BUTTON_SLOTS; i++)
   {
      p0[i] = (uint8_t)(0x40 + (i & 0x3F));
      p1[i] = (uint8_t)(0x80 + (i & 0x3F));
   }
   s1_a = p0[1 * JOYPAD_SOCKET_SLOTS + BUTTON_A];

   if (!ser(state, STATE_SIZE))
   {
      fprintf(stderr, "teamtap_ports: retro_serialize failed\n");
      free(state);
      harness_shutdown(&cfg);
      return 2;
   }

   memset(p0 + JOYPAD_SOCKET_SLOTS, 0,
          JOYPAD_BUTTON_SLOTS - JOYPAD_SOCKET_SLOTS);
   memset(p1 + JOYPAD_SOCKET_SLOTS, 0,
          JOYPAD_BUTTON_SLOTS - JOYPAD_SOCKET_SLOTS);

   if (!unser(state, STATE_SIZE))
   {
      fprintf(stderr, "teamtap_ports: retro_unserialize failed\n");
      free(state);
      harness_shutdown(&cfg);
      return 2;
   }

   {
      int restored = 1;
      unsigned bad = 0;

      for (i = JOYPAD_SOCKET_SLOTS; i < JOYPAD_BUTTON_SLOTS; i++)
      {
         if (p0[i] != (uint8_t)(0x40 + (i & 0x3F))
             || p1[i] != (uint8_t)(0x80 + (i & 0x3F)))
         {
            restored = 0;
            bad++;
         }
      }

      check(restored,
            "teamtap_state_roundtrip",
            "all %d tap slots per port survive serialize/unserialize "
            "(%u wrong; socket 1 A read back 0x%02X, seeded 0x%02X)",
            (int)(JOYPAD_BUTTON_SLOTS - JOYPAD_SOCKET_SLOTS), bad,
            p0[1 * JOYPAD_SOCKET_SLOTS + BUTTON_A], s1_a);
   }

   /* Socket 0 has to come back too -- the mid-blob joystick chunk and the
    * trailing tap chunk are separate writes, and a bug that swapped their
    * ranges would restore the tap sockets while corrupting socket 0. */
   check(p0[JOYPAD_SOCKET_SLOTS - 1] != 0xFF,
         "teamtap_socket0_chunk_intact",
         "socket 0's last slot after the round trip: 0x%02X "
         "(the two chunks did not overlap)",
         p0[JOYPAD_SOCKET_SLOTS - 1]);

   free(state);

   all_ok = 1;
   for (i = 0; i < num_results; i++)
      if (strcmp(results[i].status, "PASS") != 0)
         all_ok = 0;

   harness_report(&cfg, results, num_results);
   harness_shutdown(&cfg);

   return all_ok ? 0 : 1;
}
