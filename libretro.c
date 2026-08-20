#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libretro.h>

#include <libretro_core_options.h>
#include <streams/file_stream.h>
#include <compat/posix_string.h>
#include <compat/strl.h>

/* Forward declarations for file stream functions used in CD BIOS loading.
 * These come from libretro-common/streams/file_stream.c. */
RFILE* rfopen(const char *path, const char *mode);
int rfclose(RFILE* stream);
int64_t rfseek(RFILE* stream, int64_t offset, int origin);
int64_t rftell(RFILE* stream);
int64_t rfread(void* buffer, size_t elem_size, size_t elem_count, RFILE* stream);

#include "cheat.h"
#include "crash_detect.h"
#include "perf_iface.h"
#include "vjtrace.h"
#include "crc32.h"
#include "biosdb.h"
#include "bus_arbiter.h"
#include "file.h"
#include "jagbios.h"
#include "jagbios_m.h"
#include "jagcdbios.h"
#include "jagdevcdbios.h"
#include "jaguar.h"
#include "cdintf.h"
#include "cdrom.h"
#include "jagcd_boot.h"
#include "jagcd_hle.h"
#include "dac.h"
#include "dsp.h"
#include "jlink.h"
#include "jlink_discover.h"
#include "jlink_netpacket.h"
#include "uart.h"
#include "joystick.h"
#include "inputdev.h"
#include "settings.h"
#include "shadowfb.h"
#include "blit_memo.h"
#include "texdump.h"
#include "texreplace.h"
#include "tom.h"
#include "blitter.h"
#include "gpu.h"
#include "eeprom.h"
#include "memtrack.h"
#include "jaggd.h"
#include "nvmbios.h"
#include "vjag_memory.h"
#include "state.h"
#include "titledb.h"
#include "titlehook.h"
#include "log.h"
#include "version.h" /* generated; defines CORE_VERSION */

/* Samples (not pairs) handed to the frontend once per field.  These are
 * also the numerator of the advertised sample rate -- see
 * retro_get_system_av_info(). */
#define BUFPAL  1920
#define BUFNTSC 1600
#define BUFMAX 2048

/* File extensions accepted by the core for retro_load_game.
 * Cart types mirror what src/core/file.c::ParseFileType() can identify
 * by sniffing header bytes (sizes/magic), regardless of extension:
 *   j64, jag, rom : standard cart images / JST_ROM / JST_ALPINE
 *   abs           : Removers/aln output, JST_ABS_TYPE1 / TYPE2
 *   cof           : COFF binaries (also routes through JST_ABS_TYPE1)
 *   bin, prg      : conservative headerless raw-homebrew with valid
 *                   68k bootstrap (JST_RAW_BINARY)
 * CD images are path-loaded (need_fullpath); file.c does not sniff them:
 *   cue, cdi, chd : Jaguar CD (CUE/BIN, CDI, CHD).  Bare `iso` is not
 *                   bootable -- see docs/cd-known-issues.md.  CHD needs
 *                   CHSE session tags from a post-2026-08 chdman; see
 *                   docs/jagcd-chd.md. */
#define JAGUAR_VALID_EXTENSIONS "j64|jag|rom|abs|cof|bin|prg|cue|cdi|chd"

/* Framebuffer allocation, in pixels.  Sized for the widest / tallest video
 * mode TOM can be programmed into (TOMWriteWord clamps tomWidth to 1024 and
 * tomHeight to 512), so the buffer never has to be reallocated when the game
 * changes resolution mid-run. */
#define VIDEO_BUFFER_WIDTH   1024
#define VIDEO_BUFFER_HEIGHT  512
#define VIDEO_BUFFER_PIXELS  (VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_HEIGHT)

/* Bounds on what counts as a stale TAIL worth blanking, rather than a sign
 * that TOM and the presented geometry disagree about the video mode.  Both
 * must hold; see the use site in retro_run() for the measurements these are
 * drawn from.  MAX_BLANK_TAIL_ROWS caps the shortfall in absolute rows;
 * MIN_BLANK_COVERAGE_{NUM,DEN} additionally require the frame to be
 * essentially fully rendered, which is the clause that catches the same
 * situation in a short video mode (where a small absolute shortfall can
 * still be most of the screen). */
#define MAX_BLANK_TAIL_ROWS      32
#define MIN_BLANK_COVERAGE_NUM   3
#define MIN_BLANK_COVERAGE_DEN   4

int videoWidth               = 0;
int videoHeight              = 0;
uint32_t *videoBuffer        = NULL;
int game_width               = 0;
int game_height              = 0;

/* Actual videoBuffer allocation in pixels.  VIDEO_BUFFER_PIXELS scaled by
 * N*N when the internal-resolution option is active (N fixed at load; see
 * shadowfb.h).  video_buffer_blank() must cover the real allocation. */
static int video_buffer_alloc_pixels = VIDEO_BUFFER_PIXELS;
/* One-shot latch for the "restart required" notice when the
 * internal-resolution option changes mid-game. */
static int hires_restart_notice_logged = 0;
/* Same one-shot latch for the enhancement-hooks gate (issue #370).  Hooks
 * are applied once at content load; toggling mid-session cannot un-patch
 * (reverting would need a saved original the design deliberately does not
 * keep, and would desync anything running), so the change is reported and
 * takes effect on restart. */
static int hook_restart_notice_logged = 0;

#ifdef VJ_TRACE
/* vjtrace per-session frame counter (see the use site in retro_run()).
 * File-scope, not a retro_run()-local static, so retro_unload_game()/
 * retro_deinit() can reset it: iOS cannot dlclose cores, so a
 * function-local static would keep counting from a previous title
 * instead of restarting the documented frame==1 invariant. */
static uint32_t vjt_frame = 0;
#endif

extern uint16_t eeprom_ram[64];
extern uint16_t cdrom_eeprom_ram[64];
extern uint8_t mtMem[0x20000];
extern uint32_t jaguarMainROMCRC32;
extern void (*eeprom_dirty_cb)(void);

/* Save buffer for RETRO_MEMORY_SAVE_RAM.
 * Regular carts: 128 bytes (64 x 16-bit EEPROM words, big-endian packed),
 *                followed by 128 bytes of CD EEPROM (64 x 16-bit words).
 * Memory Track cart (CRC 0xFDF37F47): mtMem is used directly (128K).
 *
 * The save buffer is kept in sync on every EEPROM write via eeprom_dirty_cb,
 * so frontends that cache the pointer always see current data. */
#define EEPROM_SAVE_SIZE    128  /* 64 x 16-bit words, big-endian */
#define CD_EEPROM_SAVE_SIZE 128  /* CD EEPROM: 64 x 16-bit words */
#define MT_SAVE_SIZE        0x20000  /* 128K Memory Track */
/* CD content carries the EEPROM pair AND a Memory Track, so its save buffer
 * is the two EEPROM banks followed by the MT NVRAM.  Keeping the EEPROMs
 * first means the layout stays a prefix of the cart/CD-EEPROM-only one. */
#define CD_SAVE_SIZE        (EEPROM_SAVE_SIZE + CD_EEPROM_SAVE_SIZE + MT_SAVE_SIZE)
static uint8_t eeprom_save_buf[EEPROM_SAVE_SIZE + CD_EEPROM_SAVE_SIZE + MT_SAVE_SIZE];
#define MT_SAVE_OFFSET      (EEPROM_SAVE_SIZE + CD_EEPROM_SAVE_SIZE)
static void eeprom_pack_save_buf(void);
static void eeprom_unpack_save_buf(void);
static void mt_pack_save_buf(void);

static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_environment_t environ_cb;
retro_audio_sample_batch_t audio_batch_cb;
retro_log_printf_t vj_log_cb = NULL;

static bool libretro_supports_bitmasks = false;
static bool save_data_needs_unpack = false;

/* CD content state. The Tier 1 weak symbols for external_cd_bios[] and
 * cd_bios_loaded_externally are overridden by the strong definitions below. */
static bool jaguar_cd_mode = false;
/* Memory Track presence option (CD only); default on. */
static bool opt_memory_track = true;
static char cd_image_path[4096] = {0};
bool cd_bios_loaded_externally = false;
uint8_t external_cd_bios[0x40000];  /* 256 KB */

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }



#define ANALOG_THRESHOLD 20000
#define BUTTON_NONE 21
#define RETRO_DEVICE_ID_JOYPAD_LU 16
#define RETRO_DEVICE_ID_JOYPAD_LD 17
#define RETRO_DEVICE_ID_JOYPAD_LL 18
#define RETRO_DEVICE_ID_JOYPAD_LR 19
#define RETRO_DEVICE_ID_JOYPAD_RU 20
#define RETRO_DEVICE_ID_JOYPAD_RD 21
#define RETRO_DEVICE_ID_JOYPAD_RL 22
#define RETRO_DEVICE_ID_JOYPAD_RR 23
#define RETROPAD_INPUT_COUNT (RETRO_DEVICE_ID_JOYPAD_RR + 1)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static int jag_retropad[2][RETROPAD_INPUT_COUNT];
static int jag_numpad[2][12];
static int numpad_to_kb[2];
static bool show_input_options = true;
/* Content-type-dependent option visibility.  Both default to visible so
 * the options menu is complete before any content is loaded (the type is
 * unknown then, and the user may be configuring ahead of loading). */
static bool content_loaded         = false;
static bool show_cd_options        = true;
static bool show_cart_bios_option  = true;
/* 16bpp preview interpretation only matters while texture dump is on
 * (#369).  Defaults visible, like the other show_* gates, so the first
 * update_option_visibility() sees a change and hides it while the dump
 * option sits at its disabled default. */
static bool show_texdump_16bpp     = true;
/* Texture replacement (#369 deliverable 2) only means anything when a
 * pack directory exists for the loaded content; hidden otherwise. */
static bool show_texture_replace   = true;
/* Network Link host/port fields (task 3, #467) only mean anything for the
 * direct TCP modes: the client needs somewhere to dial, the server needs a
 * listen port to advertise.  "auto" and "loopback" need neither -- hidden,
 * like the other show_* gates, whenever the resolved mode isn't one that
 * reads them. */
static bool show_netlink_host      = true;
static bool show_netlink_port      = true;
/* One-shot "push every managed key regardless of its cached show_* prev"
 * flag (task 4, #467).  update_option_visibility() normally only
 * re-pushes SET_CORE_OPTIONS_DISPLAY for a group when that group's
 * show_* flag CHANGES since the cached previous value -- cheap, but
 * wrong immediately after a SET_CORE_OPTIONS_V2 rebuild, which resets
 * every option to visible on the frontend side while every show_* prev
 * in this file still matches current state, so nothing would normally
 * re-push and rows that were hidden (CD-only keys, mouse/rotary/analog
 * tuning, texdump/texreplace, per-port remaps) reappear and stay
 * reappeared.  Set this, then call update_option_visibility(); it reads
 * and clears the flag on entry, so the force applies to exactly the next
 * call. */
static int  visibility_force_push  = 0;
static bool enable_alt_inputs = false;
static uint8_t *joypad_buttons[2] = { joypad0Buttons, joypad1Buttons };

/* ---- non-pad input devices (#428/#429) ------------------------------
 *
 * Subclass IDs for RETRO_ENVIRONMENT_SET_CONTROLLER_INFO.  The mouse is a
 * RETRO_DEVICE_MOUSE subclass because it is a relative-motion pointer;
 * the three subclasses are the three adapter/mouse wiring combinations
 * (docs/jaguar-mouse-adapter-mapping.md section 4d), not three different
 * devices.  Port 2 only -- see inputdev.h.
 *
 * The rotary (#436) is also a RETRO_DEVICE_MOUSE subclass, driven by
 * relative X: that is the established libretro spinner convention and it
 * covers real spinner hardware.  RETRO_DEVICE_ANALOG as an alternate
 * rotary source is deliberately out of scope here (design spec Q4) and
 * belongs with #439's shared analog layer.  Rotary is offered on BOTH
 * ports; unlike the mouse it is a genuine matrix device (inputdev.h). */
#define RETRO_DEVICE_JAG_PAD             RETRO_DEVICE_JOYPAD
#define RETRO_DEVICE_JAG_MOUSE_ST        RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 0)
#define RETRO_DEVICE_JAG_MOUSE_AMIGA     RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 1)
#define RETRO_DEVICE_JAG_MOUSE_AMIGA_AD  RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 2)
#define RETRO_DEVICE_JAG_ROTARY          RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 3)
/* The TR10 bank-switching analog / driving controller (#437) is a
 * RETRO_DEVICE_ANALOG subclass: it is a genuine absolute-stick device.
 * A PLAIN RETRO_DEVICE_ANALOG deliberately maps to the standard pad in
 * retro_set_controller_port_device -- users routinely pick "RetroPad
 * w/ Analog" for stick-to-dpad, and silently swapping the Jaguar pad
 * for a bank-switching peripheral no released title reads would break
 * them.  Only the explicit subclasses attach it. */
#define RETRO_DEVICE_JAG_ANALOG          RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 0)
#define RETRO_DEVICE_JAG_DRIVING         RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1)
/* The light gun (#438) is a plain RETRO_DEVICE_LIGHTGUN, not a subclass:
 * there is only one Jaguar gun wiring and frontends bind their gun/mouse
 * pointer to the base type.  Port 1 only -- TR10 puts the LP pin there
 * and nowhere else (inputdev.h). */
#define RETRO_DEVICE_JAG_LIGHTGUN        RETRO_DEVICE_LIGHTGUN

/* Device the frontend explicitly selected via
 * retro_set_controller_port_device, or INPUTDEV_PAD when it never did.
 * An explicit frontend selection outranks the core option (a frontend
 * that sets a port device is making a deliberate statement); otherwise
 * the option decides.  Not serialized: option/frontend derived. */
static InputDevType port_device_frontend[2] = { INPUTDEV_PAD, INPUTDEV_PAD };
static bool         port_device_forced[2]   = { false, false };
/* Device actually attached, so the resolution is logged only when it
 * changes rather than on every check_variables() call. */
static InputDevType port_device_active[2]   = { INPUTDEV_PAD, INPUTDEV_PAD };
static bool         show_mouse_options      = true;
static bool         show_rotary_options     = true;
/* Sensitivity ladders, Q8 (256 == 1.0).  Kept separate because a spinner
 * and a mouse want very different multipliers; the per-port scale is
 * whichever one matches the device actually attached to that port. */
static int32_t      mouse_scale_q8          = 256;
static int32_t      rotary_scale_q8         = 256;

/* Per-axis tuning (#439), resolved from the options and applied to
 * whichever device is actually in each port -- the same "one ladder per
 * device kind, selected by what is plugged in" shape as the scales above.
 * A rotary is one wheel, so it has a single tune and no Y entry.
 *
 * Held as raw ints rather than axis_tune structs because libretro.c has no
 * business knowing that layout; InputDevSetTune() clamps and stores them.
 * Index 0 is X, index 1 is Y, matching INPUTDEV_AXIS_*. */
static int32_t      mouse_deadzone[2]       = { 0, 0 };
static int32_t      mouse_offset[2]         = { 0, 0 };
static int32_t      mouse_exponent_q8[2]    = { 256, 256 };
static int32_t      rotary_deadzone         = 0;
static int32_t      rotary_offset           = 0;
static int32_t      rotary_exponent_q8      = 256;
/* Analog / driving controller ladder (#437).  Shared by both ports like
 * the rotary's, and in ADC counts (127 = full deflection) rather than
 * host units per poll -- the tuning runs in the device's own 8-bit
 * domain, see inputdev_analog_byte().  No sensitivity entry: on an
 * absolute axis a linear gain only trades range for saturation, and the
 * response exponent is the control that actually shapes it. */
static int32_t      analog_deadzone[2]      = { 0, 0 };
static int32_t      analog_offset[2]        = { 0, 0 };
static int32_t      analog_exponent_q8[2]   = { 256, 256 };
static bool         show_analog_options     = true;

/* Has this port actually received non-zero mouse state from the frontend?
 *
 * THE RULE: selecting a mouse must never leave the port with no working
 * input.  A frontend that does not route mouse state to the port (it never
 * called retro_set_controller_port_device with a mouse, the user has no
 * mouse, or the port is mapped to a gamepad) would otherwise lose BOTH
 * devices -- the pad because update_input() suppresses it, and the mouse
 * because nothing ever feeds it.  So the pad suppression is deferred until
 * the mouse has proven live, i.e. until the frontend reports a non-zero
 * delta or a button.  Until then port 2 keeps its RetroPad.
 *
 * Once live it stays suppressed for the rest of the session (or until the
 * device type changes, which clears this): a mouse that has moved once is
 * a mouse the frontend is routing, and letting a pad drive the same six
 * lines at that point is the ambiguity the suppression exists to prevent.
 *
 * A ROTARY PORT DELIBERATELY DOES NOT USE THIS, and that is not an
 * oversight to "fix" later.  The rule exists because a mouse port loses
 * BOTH devices; a rotary port loses neither -- it withholds only U/D/L/R
 * and keeps A/B/C/Option/Pause and the keypad on the RetroPad, so a
 * frontend that routes no mouse state still leaves the port usable.  (In
 * particular Tempest 2000's rotary unlock, Option then Pause on both
 * controllers, is entirely buttons and works with no spinner routed.)
 * Deferring the rotary's four-slot withholding would instead mean an
 * un-serialized flag that test_savestate's exact-replay assertion has to
 * reason around, for a failure mode that cannot occur.
 *
 * DELIBERATELY NOT SERIALIZED, for the same reason inputdev.h gives for
 * the device type and the sensitivity scale: this is a fact about how the
 * HOST is routing input, not about the emulated machine, and restoring it
 * from a state would let a stale state fight the current session.  The
 * asymmetry with the v12 chunk is bounded and benign -- a state saved
 * while the mouse was live loads with port 2's pad un-suppressed until
 * the mouse next moves, i.e. at most a frame of pad contribution, and
 * only if the frontend is routing a pad there at all.
 *
 * File-scope static -- reset in retro_deinit (iOS cannot dlclose a core). */
static bool         inputdev_live[2]        = { false, false };

/* One-shot savestate headroom report (retro_serialize).  File-scope rather
 * than function-local so retro_deinit can put it back. */
static bool         headroom_logged         = false;

static const char *inputdev_type_name(InputDevType t)
{
   switch (t)
   {
      case INPUTDEV_MOUSE_ST:            return "Atari ST / PS2 mouse";
      case INPUTDEV_MOUSE_AMIGA_ADAPTER: return "Amiga mouse (Amiga adapter)";
      case INPUTDEV_MOUSE_AMIGA_ON_ST:   return "Amiga mouse (ST adapter)";
      case INPUTDEV_ROTARY:              return "Tempest rotary";
      case INPUTDEV_ANALOG:              return "Analog joystick (bank-switching)";
      case INPUTDEV_DRIVING:             return "Driving controller (bank-switching)";
      case INPUTDEV_LIGHTGUN:            return "light gun";
      default:                           break;
   }
   return "standard joypad";
}

static bool inputdev_is_mouse_type(InputDevType t)
{
   return (t == INPUTDEV_MOUSE_ST
           || t == INPUTDEV_MOUSE_AMIGA_ADAPTER
           || t == INPUTDEV_MOUSE_AMIGA_ON_ST);
}

static bool inputdev_is_analog_type(InputDevType t)
{
   return (t == INPUTDEV_ANALOG || t == INPUTDEV_DRIVING);
}

/* Push the sensitivity ladder AND the per-axis tuning (#439) that belong to
 * whatever device is currently in `port`.
 *
 * The two are resolved together, in one function called from both paths,
 * because they are selected by the same fact -- what is plugged into the
 * port -- and a second place that picks between the two devices' ladders is
 * exactly how the bug documented in apply_port_device() came about.
 *
 * IT CAN RUN BEFORE THE OPTIONS HAVE EVER BEEN READ.  The frontend may
 * call retro_set_controller_port_device() before retro_load_game(), and
 * that path reaches here without passing through check_variables().  The
 * statics then still hold their initialisers, which are deliberately the
 * IDENTITY (and retro_deinit puts them back), so the worst case is a
 * device that runs untuned until retro_load_game's own check_variables()
 * resolves it a moment later.  Any future static added here must keep that
 * property: its initialiser has to be a safe value, not a sentinel. */
static void apply_port_tuning(int port)
{
   if (inputdev_is_analog_type(InputDevGetType(port)))
   {
      /* The analog device has no sensitivity ladder (see the statics),
       * but the port scale is still pinned to unity so an analog ->
       * mouse/rotary switch cannot inherit a stale multiplier -- the
       * same hygiene the rotary branch applies to its unused Y tune. */
      InputDevSetScale(port, 256);
      InputDevSetTune(port, INPUTDEV_AXIS_X, analog_deadzone[0],
                      analog_offset[0], analog_exponent_q8[0]);
      InputDevSetTune(port, INPUTDEV_AXIS_Y, analog_deadzone[1],
                      analog_offset[1], analog_exponent_q8[1]);
   }
   else if (InputDevGetType(port) == INPUTDEV_ROTARY)
   {
      InputDevSetScale(port, rotary_scale_q8);
      /* A rotary is one wheel and never reads its Y tune, but both axes are
       * set so a rotary -> mouse switch on port 2 cannot inherit a stale Y
       * tune from whatever was configured before. */
      InputDevSetTune(port, INPUTDEV_AXIS_X, rotary_deadzone,
                      rotary_offset, rotary_exponent_q8);
      InputDevSetTune(port, INPUTDEV_AXIS_Y, rotary_deadzone,
                      rotary_offset, rotary_exponent_q8);
   }
   else
   {
      InputDevSetScale(port, mouse_scale_q8);
      InputDevSetTune(port, INPUTDEV_AXIS_X, mouse_deadzone[0],
                      mouse_offset[0], mouse_exponent_q8[0]);
      InputDevSetTune(port, INPUTDEV_AXIS_Y, mouse_deadzone[1],
                      mouse_offset[1], mouse_exponent_q8[1]);
   }
}

static void apply_port_device(int port, InputDevType type)
{
   InputDevSetType(port, type);

   /* Read back: InputDevSetType refuses a mouse on port 1, so the
    * resolved type is whatever it actually accepted. */
   type = InputDevGetType(port);

   if (type != port_device_active[port])
   {
      port_device_active[port] = type;
      /* A new device has to earn the port back (see inputdev_live). */
      inputdev_live[port]      = false;
      /* Re-resolve the ladders for the device now in the port.
       * The rotary and the mouse have separate options because a spinner
       * and a mouse want very different multipliers, and until this line
       * existed the only place that picked between them was
       * check_variables().  So switching a port to "Rotary (Tempest)"
       * from RetroArch's Controls menu -- which reaches
       * retro_set_controller_port_device, not GET_VARIABLE_UPDATE -- left
       * the port running at the OTHER ladder's multiplier until the next
       * core-option change happened to fix it.  Invisible at defaults
       * (both ladders are 256) and self-healing, but real.
       *
       * The statics hold their last resolved values here; on the
       * check_variables() path the loop after the ladders recomputes them
       * and calls apply_port_tuning again anyway, so this is at worst one
       * redundant pass there. */
      apply_port_tuning(port);
      if (type != INPUTDEV_PAD)
         LOG_INF("[input] port %d: %s attached\n", port + 1,
                 inputdev_type_name(type));
      else
         LOG_INF("[input] port %d: standard joypad\n", port + 1);
   }
}

static int number_keys[12] = {
   RETROK_MINUS,
   RETROK_7,
   RETROK_4,
   RETROK_1,
   RETROK_0,
   RETROK_8,
   RETROK_5,
   RETROK_2,
   RETROK_EQUALS,
   RETROK_9,
   RETROK_6,
   RETROK_3
};

static int keypad_keys[12] = {
   RETROK_KP_DIVIDE,
   RETROK_KP7,
   RETROK_KP4,
   RETROK_KP1,
   RETROK_KP0,
   RETROK_KP8,
   RETROK_KP5,
   RETROK_KP2,
   RETROK_KP_MULTIPLY,
   RETROK_KP9,
   RETROK_KP6,
   RETROK_KP3
};

typedef struct {
   int id;
   char value[10];
} JagMapping;

typedef struct {
   const char *suffix;
   unsigned id;
} RetropadOptionMapping;

static JagMapping jag_map[22] = {
   { BUTTON_U,      "up" },
   { BUTTON_D,      "down" },
   { BUTTON_L,      "left" },
   { BUTTON_R,      "right" },
   { BUTTON_A,      "btn_a" },
   { BUTTON_B,      "btn_b" },
   { BUTTON_C,      "btn_c" },
   { BUTTON_PAUSE,  "pause" },
   { BUTTON_OPTION, "option" },
   { BUTTON_0,      "num_0" },
   { BUTTON_1,      "num_1" },
   { BUTTON_2,      "num_2" },
   { BUTTON_3,      "num_3" },
   { BUTTON_4,      "num_4" },
   { BUTTON_5,      "num_5" },
   { BUTTON_6,      "num_6" },
   { BUTTON_7,      "num_7" },
   { BUTTON_8,      "num_8" },
   { BUTTON_9,      "num_9" },
   { BUTTON_s,      "star" },
   { BUTTON_d,      "hash" },
   { BUTTON_NONE,   "---" }
};

static const RetropadOptionMapping retropad_option_map[] = {
   { "_retropad_up",        RETRO_DEVICE_ID_JOYPAD_UP },
   { "_retropad_down",      RETRO_DEVICE_ID_JOYPAD_DOWN },
   { "_retropad_left",      RETRO_DEVICE_ID_JOYPAD_LEFT },
   { "_retropad_right",     RETRO_DEVICE_ID_JOYPAD_RIGHT },
   { "_retropad_a",         RETRO_DEVICE_ID_JOYPAD_A },
   { "_retropad_b",         RETRO_DEVICE_ID_JOYPAD_B },
   { "_retropad_y",         RETRO_DEVICE_ID_JOYPAD_Y },
   { "_retropad_select",    RETRO_DEVICE_ID_JOYPAD_SELECT },
   { "_retropad_start",     RETRO_DEVICE_ID_JOYPAD_START },
   { "_retropad_x",         RETRO_DEVICE_ID_JOYPAD_X },
   { "_retropad_l1",        RETRO_DEVICE_ID_JOYPAD_L },
   { "_retropad_r1",        RETRO_DEVICE_ID_JOYPAD_R },
   { "_retropad_l2",        RETRO_DEVICE_ID_JOYPAD_L2 },
   { "_retropad_r2",        RETRO_DEVICE_ID_JOYPAD_R2 },
   { "_retropad_l3",        RETRO_DEVICE_ID_JOYPAD_L3 },
   { "_retropad_r3",        RETRO_DEVICE_ID_JOYPAD_R3 },
   { "_retropad_analog_lu", RETRO_DEVICE_ID_JOYPAD_LU },
   { "_retropad_analog_ld", RETRO_DEVICE_ID_JOYPAD_LD },
   { "_retropad_analog_ll", RETRO_DEVICE_ID_JOYPAD_LL },
   { "_retropad_analog_lr", RETRO_DEVICE_ID_JOYPAD_LR },
   { "_retropad_analog_ru", RETRO_DEVICE_ID_JOYPAD_RU },
   { "_retropad_analog_rd", RETRO_DEVICE_ID_JOYPAD_RD },
   { "_retropad_analog_rl", RETRO_DEVICE_ID_JOYPAD_RL },
   { "_retropad_analog_rr", RETRO_DEVICE_ID_JOYPAD_RR },
};

static void build_port_option_key(char *key, size_t key_size, unsigned port, const char *suffix)
{
   size_t len;

   len = strlcpy(key, "virtualjaguar_p", key_size);
   if (len >= key_size)
      return;
   snprintf(key + len, key_size - len, "%u", port + 1);
   strlcat(key, suffix, key_size);
}

static int get_button_id(const char *val)
{
   unsigned i;
   for (i = 0; i <= BUTTON_NONE; i++)
   {
      if (!strcmp(jag_map[i].value, val))
         return jag_map[i].id;
   }
   return BUTTON_NONE;
}

/* Resolve the Network Link option to a concrete JLINK_MODE_*.
 *
 * "auto" means netplay-when-live, else idle.  The design doc originally
 * had auto also fall back to "the direct mode last chosen explicitly";
 * that was dropped, because with a single option key there is nowhere to
 * read a previous choice from -- selecting "auto" overwrites it -- so
 * honouring it would need hidden persisted state whose behaviour the user
 * cannot see or predict across restarts.
 *
 * Auto deliberately never dials a discovered peer by itself either; with
 * the Voice Modem that would place a call the user did not initiate. */
static int netlink_resolve_mode(const char *v)
{
   if (!v)
      return JLINK_MODE_DISABLED;
   if (!strcmp(v, "loopback"))   return JLINK_MODE_LOOPBACK;
   if (!strcmp(v, "tcp_server")) return JLINK_MODE_TCP_SERVER;
   if (!strcmp(v, "tcp_client")) return JLINK_MODE_TCP_CLIENT;
   if (!strcmp(v, "auto"))
   {
      if (JLinkMode() == JLINK_MODE_NETPACKET)
         return JLINK_MODE_NETPACKET;
      return JLINK_MODE_DISABLED;
   }
   return JLINK_MODE_DISABLED;
}

static bool update_option_visibility(void)
{
   struct retro_core_option_display option_display;
   struct retro_variable var;
   bool updated = false;
   unsigned i;
   /* Read-and-clear: see visibility_force_push's declaration comment.
    * One-shot so a normal (non-rebuild) call right afterward goes back to
    * the cheap change-only behavior.  Declared here (with the other
    * top-of-block locals, C89) rather than assigned as a statement, so the
    * clear below is the first STATEMENT in the function, after every
    * declaration. */
   int force = visibility_force_push;
   // Show/hide input options
   bool show_input_options_prev = show_input_options;

   visibility_force_push = 0;
   show_input_options = true;

   var.key = "virtualjaguar_alt_inputs";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && !strcmp(var.value, "disabled"))
      show_input_options = false;

   if (force || show_input_options != show_input_options_prev)
   {
      option_display.visible = show_input_options;

      for (i = 0; i < 2; i++)
      {
         unsigned j;
         char key[64];

         build_port_option_key(key, sizeof(key), i, "_numpad_to_kb");
         option_display.key = key;
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

         for (j = 0; j < ARRAY_SIZE(retropad_option_map); j++)
         {
            build_port_option_key(key, sizeof(key), i, retropad_option_map[j].suffix);
            option_display.key = key;
            environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         }
      }

      updated = true;
   }

   /* Show/hide options that only apply to one content type.  Filtering
    * is deliberately skipped until content is loaded: with nothing
    * loaded the type is unknown, and hiding either group would make
    * options unreachable for someone configuring ahead of time. */
   {
      static const char * const cd_only_keys[] = {
         "virtualjaguar_cd_bios_type",
         "virtualjaguar_cd_boot_mode",
         "virtualjaguar_cd_read_speed",
         "virtualjaguar_cd_trace",
         "virtualjaguar_memory_track",
      };
      bool show_cd_prev        = show_cd_options;
      bool show_cart_bios_prev = show_cart_bios_option;

      show_cd_options       = (!content_loaded || jaguar_cd_mode);
      /* The cartridge BIOS setting is ignored for CD content —
       * ResolveBootConfig() lets CD Boot Mode drive showBootROM — so
       * showing it there would advertise a control that does nothing. */
      show_cart_bios_option = (!content_loaded || !jaguar_cd_mode);

      if (force || show_cd_options != show_cd_prev)
      {
         option_display.visible = show_cd_options;
         for (i = 0; i < ARRAY_SIZE(cd_only_keys); i++)
         {
            option_display.key = cd_only_keys[i];
            environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY,
                       &option_display);
         }
         updated = true;
      }

      if (force || show_cart_bios_option != show_cart_bios_prev)
      {
         option_display.visible = show_cart_bios_option;
         option_display.key     = "virtualjaguar_bios";
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY,
                    &option_display);
         option_display.key     = "virtualjaguar_bios_type";
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY,
                    &option_display);
         updated = true;
      }
   }

   /* Mouse sensitivity and the per-axis tuning (#439) only mean anything
    * while a mouse is attached to port 2.  Resolved from the live device
    * rather than the option string so a frontend-set port device
    * (retro_set_controller_port_device) reveals them too. */
   {
      static const char * const mouse_keys[] = {
         "virtualjaguar_mouse_sensitivity",
         "virtualjaguar_mouse_deadzone_x",
         "virtualjaguar_mouse_deadzone_y",
         "virtualjaguar_mouse_offset_x",
         "virtualjaguar_mouse_offset_y",
         "virtualjaguar_mouse_exponent_x",
         "virtualjaguar_mouse_exponent_y",
      };
      bool show_mouse_prev = show_mouse_options;

      show_mouse_options = inputdev_is_mouse_type(InputDevGetType(1));

      if (force || show_mouse_options != show_mouse_prev)
      {
         option_display.visible = show_mouse_options;
         for (i = 0; i < ARRAY_SIZE(mouse_keys); i++)
         {
            option_display.key = mouse_keys[i];
            environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY,
                       &option_display);
         }
         updated = true;
      }
   }

   /* Rotary sensitivity, its per-axis tuning (#439) and the rotary
    * controller-type knob mean nothing unless a rotary is attached to one
    * port or the other (#436). */
   {
      static const char * const rotary_keys[] = {
         "virtualjaguar_rotary_sensitivity",
         "virtualjaguar_rotary_id",
         "virtualjaguar_rotary_deadzone",
         "virtualjaguar_rotary_offset",
         "virtualjaguar_rotary_exponent",
      };
      bool show_rotary_prev = show_rotary_options;

      show_rotary_options = (InputDevGetType(0) == INPUTDEV_ROTARY
                             || InputDevGetType(1) == INPUTDEV_ROTARY);

      if (force || show_rotary_options != show_rotary_prev)
      {
         option_display.visible = show_rotary_options;
         for (i = 0; i < ARRAY_SIZE(rotary_keys); i++)
         {
            option_display.key = rotary_keys[i];
            environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY,
                       &option_display);
         }
         updated = true;
      }
   }

   /* Analog / driving tuning (#437) means nothing unless one of the two
    * types is attached to a port -- same machinery as the rotary gate. */
   {
      static const char * const analog_keys[] = {
         "virtualjaguar_analog_deadzone_x",
         "virtualjaguar_analog_deadzone_y",
         "virtualjaguar_analog_offset_x",
         "virtualjaguar_analog_offset_y",
         "virtualjaguar_analog_exponent_x",
         "virtualjaguar_analog_exponent_y",
      };
      bool show_analog_prev = show_analog_options;

      show_analog_options = (inputdev_is_analog_type(InputDevGetType(0))
                             || inputdev_is_analog_type(InputDevGetType(1)));

      if (force || show_analog_options != show_analog_prev)
      {
         option_display.visible = show_analog_options;
         for (i = 0; i < ARRAY_SIZE(analog_keys); i++)
         {
            option_display.key = analog_keys[i];
            environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY,
                       &option_display);
         }
         updated = true;
      }
   }

   /* The 16bpp preview knob only means anything while texture dump is
    * enabled (#369) -- same machinery as the mouse/rotary gates above. */
   {
      bool show_texdump_prev = show_texdump_16bpp;

      show_texdump_16bpp = false;
      var.key = "virtualjaguar_texture_dump";
      var.value = NULL;
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value
          && !strcmp(var.value, "enabled"))
         show_texdump_16bpp = true;

      if (force || show_texdump_16bpp != show_texdump_prev)
      {
         option_display.visible = show_texdump_16bpp;
         option_display.key     = "virtualjaguar_texdump_16bpp";
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY,
                    &option_display);
         updated = true;
      }
   }

   /* Texture replacement (#369 deliverable 2): only shown when a pack
    * directory exists for the loaded content -- an option that can
    * never do anything is noise. */
   {
      bool show_replace_prev = show_texture_replace;

      show_texture_replace = TexReplacePackAvailable() ? true : false;
      if (force || show_texture_replace != show_replace_prev)
      {
         option_display.visible = show_texture_replace;
         option_display.key     = "virtualjaguar_texture_replace";
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY,
                    &option_display);
         updated = true;
      }
   }

   /* Network Link host/port (task 3, #467): host only means anything for
    * the TCP client (the side that dials out); port only means anything
    * for a mode with a TCP socket of its own at all -- server (listens)
    * or client (connects).  "auto" and "loopback" hide both: auto never
    * asks the user to configure an address (that is the whole point of
    * it), and loopback never leaves this console.  Read raw, like the
    * alt-inputs/texture-dump gates above, rather than through
    * get_variable_pertitle() -- per-title defaults have no business
    * steering a transport choice the user made explicitly. */
   {
      bool show_netlink_host_prev = show_netlink_host;
      bool show_netlink_port_prev = show_netlink_port;
      int  resolved;

      var.key = "virtualjaguar_netlink";
      var.value = NULL;
      environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
      resolved = netlink_resolve_mode(var.value);

      show_netlink_host = (resolved == JLINK_MODE_TCP_CLIENT);
      show_netlink_port = (resolved == JLINK_MODE_TCP_CLIENT
                            || resolved == JLINK_MODE_TCP_SERVER);

      if (force || show_netlink_host != show_netlink_host_prev)
      {
         option_display.visible = show_netlink_host;
         option_display.key     = "virtualjaguar_netlink_host";
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY,
                    &option_display);
         updated = true;
      }
      if (force || show_netlink_port != show_netlink_port_prev)
      {
         option_display.visible = show_netlink_port;
         option_display.key     = "virtualjaguar_netlink_port";
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY,
                    &option_display);
         updated = true;
      }
   }

   return updated;
}

/* Netpacket interface (env 78): registered unconditionally and inert
 * until the frontend starts a netplay session; the session then carries
 * the JagLink byte stream (jlink_netpacket.c), taking over whatever mode
 * the virtualjaguar_netlink option had configured and restoring it on
 * stop.  Broadcast TX makes multi-console (CatNet) sessions work too. */
static const struct retro_netpacket_callback netpacket_cb = {
   JLinkNPStart,
   JLinkNPReceive,
   JLinkNPStop,
   JLinkNPPoll,
   NULL,                /* connected */
   NULL,                /* disconnected */
   "vjag-netlink-1"     /* protocol_version */
};

void retro_set_environment(retro_environment_t cb)
{
   struct retro_vfs_interface_info vfs_iface_info;
   struct retro_core_options_update_display_callback update_display_cb;
   bool option_categories = false;
   bool achievements = true;
   environ_cb = cb;

   cb(RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE, (void *)&netpacket_cb);

   {
      struct retro_log_callback log_iface;
      log_iface.log = NULL;
      if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log_iface))
         vj_log_cb = log_iface.log;
      else
         vj_log_cb = NULL;
   }

   libretro_set_core_options(environ_cb, &option_categories);
   update_display_cb.callback = update_option_visibility;
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK, &update_display_cb);

   vfs_iface_info.required_interface_version = 1;
   vfs_iface_info.iface                      = NULL;
   if (cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs_iface_info))
      filestream_vfs_init(&vfs_iface_info);

   environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS, &achievements);

   /* CD extensions are declared path-loaded (env 65).  DELIBERATELY the
    * inverse of the usual pattern: hybrid cart+disc cores (Genesis Plus GX,
    * PicoDrive, Geargrafx) set need_fullpath=true globally and override
    * their cartridge extensions to false, because that fails safe for THEM
    * on a frontend without this callback.  For this core the safe failure
    * is the other way around: global false + CD-only true degrades, on a
    * frontend without env 65, to exactly the old behavior (the frontend
    * loads the disc image into RAM and we ignore it -- ~400 MB wasted on a
    * .cdi, nothing else lost).  The standard direction would instead cost
    * cartridge soft patching and the per-title DB feed, and break the
    * RAM-loaded (.abs/.cof) reload in retro_load_game, on any frontend
    * below RetroArch 1.9.6.  Measured effect (hover_strike.cdi, 396 MB):
    * RetroArch peak RSS 557 MB -> ~165-180 MB depending on frontend
    * buffering (164 MB and 181 MB both observed across runs).
    * NOTE: no 'iso' entry here -- libretro.h's struct documentation limits
    * override extensions to those in retro_system_info::valid_extensions
    * (JAGUAR_VALID_EXTENSIONS, above) and that list does not include 'iso'.
    * It needs none anyway: is_cd_content in retro_load_game still matches
    * a bare .iso by path, and CDIntfOpenImage (src/cd/cdintf.c) refuses to
    * open one regardless of how it arrived. */
   {
      static const struct retro_system_content_info_override
         content_overrides[] = {
         { "cue|cdi|chd", true /* need_fullpath */, false /* persistent_data */ },
         { NULL, false, false }
      };
      environ_cb(RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE,
                 (void *)content_overrides);
   }
}

/* Last logged link state: -1 = link not open, 0 = open with no peer,
 * 1 = peer attached.  File scope (not a function-local static) so
 * retro_deinit() can reset it -- iOS never dlcloses the core, so a
 * function-local would carry the previous session's state into the next
 * one and swallow the first UP/DOWN edge. */
static int netlink_was_up = -1;

/* Discovered-host option list (task 4, #467).  netlink_last_rebuild_ms
 * paces SET_CORE_OPTIONS_V2 re-registration (a full RetroArch option-
 * manager teardown, see netlink_rebuild_host_options() below);
 * netlink_peers_dirty is a sticky latch set the instant a peer-set change
 * is observed and cleared only once a rebuild actually runs -- see the
 * gating block in retro_run() for why a bare rate-limit check isn't
 * enough.  Both are file scope (not function-local) for the same reason
 * as netlink_was_up: iOS never dlcloses the core, so a function-local
 * static would carry the previous session's pacing/latch state into the
 * next one. */
static uint32_t netlink_last_rebuild_ms = 0;
static int      netlink_peers_dirty     = 0;

/* Last (mode, device, host) narrated to the OSD by netlink_apply()'s
 * mode-resolution messages (task 5, #467).  netlink_apply() runs on
 * EVERY check_variables() call, which fires whenever ANY core option
 * changes -- not just the netlink ones -- so without this dedup the
 * player gets a link toast (and, in tcp_client, a repeat of the device-
 * mismatch warning) for completely unrelated menu edits.  -1 sentinels
 * guarantee the first call after load always narrates.  File scope (not
 * function-local) for the same reason as netlink_was_up above: iOS never
 * dlcloses the core, so a function-local static would carry the previous
 * session's narrated state into the next one and swallow its first
 * message. */
static int  netlink_osd_last_mode      = -1;
static int  netlink_osd_last_device    = -1;
static char netlink_osd_last_host[128] = "";

/* Names for the [NETLINK] log lines.  Not exported: nothing outside this
 * file needs to render a mode. */
static const char *netlink_mode_name(int mode)
{
   switch (mode)
   {
   case JLINK_MODE_DISABLED:   return "disabled";
   case JLINK_MODE_LOOPBACK:   return "loopback";
   case JLINK_MODE_TCP_SERVER: return "tcp_server";
   case JLINK_MODE_TCP_CLIENT: return "tcp_client";
   case JLINK_MODE_NETPACKET:  return "netpacket";
   default: break;
   }
   return "unknown";
}

/* Human-readable device label for OSD text ("voicemodem"/"jaglink" in the
 * log lines are fine for grep, not for a player reading the screen). */
static const char *netlink_device_label(int device)
{
   return device == JLINK_DEVICE_VOICEMODEM ? "Voice Modem" : "JagLink";
}

/* OSD narration.  Mirrors the [NETLINK] log lines so screen and log
 * always agree; fires on transitions only, never per frame. */
static void netlink_osd(const char *fmt, ...)
{
   struct retro_message_ext msg;
   char text[256];
   va_list ap;

   va_start(ap, fmt);
   vsnprintf(text, sizeof(text), fmt, ap);
   va_end(ap);

   memset(&msg, 0, sizeof(msg));
   msg.msg      = text;
   msg.duration = 4000;
   msg.priority = 2;
   msg.level    = RETRO_LOG_INFO;
   msg.target   = RETRO_MESSAGE_TARGET_OSD;
   msg.type     = RETRO_MESSAGE_TYPE_NOTIFICATION;
   msg.progress = -1;
   environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &msg);
}

/* Edge-gate for netlink_check_device_mismatch()'s OSD: the last sel_host
 * value that already got a mismatch toast, so two calls in a row (or
 * repeated rebuilds while nothing changed) don't repeat it.  File scope
 * for the same iOS-no-dlclose reason as netlink_osd_last_host above; reset
 * alongside it in retro_deinit(). */
static char netlink_mismatch_last_host[128] = "";

/* Device-mismatch check for tcp_client (fix wave, PR review finding 3):
 * the discovery beacon carries device type, so if the host about to be
 * dialed (or already selected) is a peer we've seen beaconing the OTHER
 * device, warn -- JagLink and Voice Modem never interoperate and fail
 * silently otherwise (docs/netlink-ux-design.md section 2).
 *
 * Called from BOTH netlink_apply() (immediate: catches a host edit made
 * while the peer table was already populated) AND
 * netlink_rebuild_host_options() (catches the persisted-config load path:
 * netlink_apply()'s mismatch scan runs BEFORE the JLinkDiscStart() call in
 * the same function, and a real JLinkDiscStart() resets the peer table --
 * so at load the apply-time scan always sees an empty table, and the peer
 * only appears ~1s later when discovery hears the beacon; nothing
 * re-evaluates the mismatch at that point unless this second call site
 * does it).  netlink_mismatch_last_host above edge-gates the OSD so the
 * two call sites (or repeated rebuilds) don't double-toast the same
 * condition. */
static void netlink_check_device_mismatch(const char *sel_host)
{
   int my_device;
   int pi, peer_count;
   int mismatched;
   const char *mismatch_label;

   mismatched = 0;
   mismatch_label = "";
   my_device = (JLinkDevice() == JLINK_DEVICE_VOICEMODEM)
               ? JLINK_DISC_DEV_VOICEMODEM : JLINK_DISC_DEV_JAGLINK;
   peer_count = JLinkDiscPeerCount();
   if (peer_count > JLINK_DISC_MAX_PEERS)
      peer_count = JLINK_DISC_MAX_PEERS;

   for (pi = 0; pi < peer_count; pi++)
   {
      const JLinkPeer *peer;

      peer = JLinkDiscPeerAt(pi);
      if (peer && !strcmp(peer->addr, sel_host) && peer->device != my_device)
      {
         mismatched = 1;
         mismatch_label = (peer->device == JLINK_DISC_DEV_VOICEMODEM)
                           ? "Voice Modem" : "JagLink";
         break;
      }
   }

   if (mismatched)
   {
      if (strcmp(sel_host, netlink_mismatch_last_host) != 0)
      {
         netlink_osd("Host is running %s, you are set to %s",
                     mismatch_label, netlink_device_label(JLinkDevice()));
         strncpy(netlink_mismatch_last_host, sel_host,
                 sizeof(netlink_mismatch_last_host) - 1);
         netlink_mismatch_last_host[sizeof(netlink_mismatch_last_host) - 1]
            = '\0';
      }
   }
   else
      netlink_mismatch_last_host[0] = '\0';
}

/* Resolve the address the link will actually dial, in precedence order:
 * VJ_NETLINK_HOST env, else the virtualjaguar_netlink_host option (any
 * string verbatim; the sentinel "vj_netlink.txt" defers to the file), else
 * the first line of <system_dir>/vj_netlink.txt.  out is left empty when
 * nothing resolved -- both callers substitute "127.0.0.1" themselves.
 *
 * Split out of netlink_apply() for #501: netlink_rebuild_host_options()
 * used to hand netlink_check_device_mismatch() the RAW option string,
 * which under the "From file" preset is the literal sentinel
 * "vj_netlink.txt" and can never equal a discovered peer's dotted-quad
 * addr -- so the mismatch warning was dead code for that whole
 * configuration, and under VJ_NETLINK_HOST it compared the option against
 * the peer table while the link dialed something else entirely.  The
 * rebuild path is the one with a populated peer table (see
 * netlink_check_device_mismatch()'s declaration comment), so it has to
 * resolve exactly the same way netlink_apply() does.
 *
 * src/want_file/got_file are the log-line bookkeeping netlink_apply()
 * needs and the rebuild path does not; all three accept NULL. */
static void netlink_resolve_host(char *out, size_t out_len, const char **src,
                                 int *want_file, int *got_file)
{
   const char *env;
   struct retro_variable pvar;

   out[0] = '\0';
   if (src)
      *src = "default";
   if (want_file)
      *want_file = 0;
   if (got_file)
      *got_file = 0;

   env = getenv("VJ_NETLINK_HOST");
   if (env && env[0])
   {
      strncpy(out, env, out_len - 1);
      out[out_len - 1] = '\0';
      if (src)
         *src = "VJ_NETLINK_HOST env";
   }
   if (!out[0])
   {
      pvar.key = "virtualjaguar_netlink_host";
      pvar.value = NULL;
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &pvar) && pvar.value
          && pvar.value[0])
      {
         if (strcmp(pvar.value, "vj_netlink.txt") != 0)
         {
            strncpy(out, pvar.value, out_len - 1);
            out[out_len - 1] = '\0';
            if (src)
               *src = "core option";
         }
         else if (want_file)
            *want_file = 1;
      }
   }
   if (!out[0])
   {
      const char *system_dir = NULL;
      if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir)
          && system_dir)
      {
         char path[1024];
         FILE *f;
         snprintf(path, sizeof(path), "%s/vj_netlink.txt", system_dir);
         f = fopen(path, "r");
         if (f)
         {
            if (fgets(out, (int)out_len, f))
            {
               size_t n = strlen(out);
               while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r'
                                || out[n - 1] == ' '))
                  out[--n] = '\0';
            }
            fclose(f);
            if (out[0])
            {
               if (got_file)
                  *got_file = 1;
               if (src)
                  *src = "vj_netlink.txt";
            }
         }
      }
   }
}

/* Resolve the TCP endpoint for the network link and apply the mode.
 * Host (client mode): VJ_NETLINK_HOST env, else the
 * virtualjaguar_netlink_host option (any string is accepted verbatim so
 * frontends with free-text option entry can supply arbitrary addresses;
 * the sentinel "vj_netlink.txt" defers to the file), else first line of
 * <system_dir>/vj_netlink.txt, else 127.0.0.1.  Port: VJ_NETLINK_PORT
 * env overrides the virtualjaguar_netlink_port option.
 *
 * mode is the resolved JLINK_MODE_* (see netlink_resolve_mode()); opt_value
 * is the RAW option string that produced it, and may be NULL.  The two can
 * disagree on purpose: "auto" with no netplay session live resolves to
 * JLINK_MODE_DISABLED for the link itself (there is nothing yet to carry
 * the emulated UART), but the discovery beacon/listener below is driven by
 * opt_value so a user who picked "auto" or "tcp_client" still sees LAN
 * peers to act on -- discovery never auto-connects, it only populates the
 * list a later task's UI reads. */
static void netlink_apply(int mode, const char *opt_value)
{
   char host[128];
   int port = 42171;
   const char *env;
   struct retro_variable pvar;
   /* Where the address came from, and whether the vj_netlink.txt sentinel
    * actually produced one.  Both exist purely for the log line at the end:
    * this layer used to be completely silent, so a link that never came up
    * looked identical to one that was never configured, and a missing
    * vj_netlink.txt fell back to 127.0.0.1 with nothing said. */
   const char *src = "default";
   int want_file = 0;
   int got_file = 0;
   /* OSD dedup state -- see netlink_osd_last_mode's declaration comment. */
   char narrate_host[128];
   int narrate_device;
   int narrate_changed;

   host[0] = '\0';

   pvar.key = "virtualjaguar_netlink_port";
   pvar.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &pvar) && pvar.value)
   {
      int p = atoi(pvar.value);
      if (p > 0)
         port = p;
   }
   env = getenv("VJ_NETLINK_PORT");
   if (env && env[0] && atoi(env) > 0)
      port = atoi(env);

   pvar.key = "virtualjaguar_netlink_wait";
   pvar.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &pvar) && pvar.value)
      JLinkSetWaitEnabled(strcmp(pvar.value, "disabled") != 0);

   netlink_resolve_host(host, sizeof(host), &src, &want_file, &got_file);

   JLinkSetTCPEndpoint(host[0] ? host : "127.0.0.1", port);
   UARTSetLinkMode(mode);

   /* Whether the resolved (mode, device, host) actually changed since the
    * last narration.  netlink_apply() runs on every check_variables()
    * call, which fires on ANY option change -- not just the netlink ones
    * -- so the LOG_INF lines below are intentionally unconditional (log
    * scrollback is cheap to re-emit), but the netlink_osd() calls are
    * gated on this, or a player using Voice Modem in tcp_client would get
    * a link toast for every unrelated settings tweak.  host only matters
    * for tcp_client (it is not shown in the other messages), so it is
    * excluded from the comparison otherwise -- an unrelated host edit
    * while in another mode must not itself count as a transition. */
   narrate_device = JLinkDevice();
   if (mode == JLINK_MODE_TCP_CLIENT)
   {
      strncpy(narrate_host, host[0] ? host : "127.0.0.1",
              sizeof(narrate_host) - 1);
      narrate_host[sizeof(narrate_host) - 1] = '\0';
   }
   else
      narrate_host[0] = '\0';
   narrate_changed = (mode != netlink_osd_last_mode)
                      || (narrate_device != netlink_osd_last_device)
                      || strcmp(narrate_host, netlink_osd_last_host) != 0;
   if (narrate_changed)
   {
      netlink_osd_last_mode   = mode;
      netlink_osd_last_device = narrate_device;
      strncpy(netlink_osd_last_host, narrate_host,
              sizeof(netlink_osd_last_host) - 1);
      netlink_osd_last_host[sizeof(netlink_osd_last_host) - 1] = '\0';
   }

   /* One line that answers "what did the core actually do with my
    * settings".  Without it the whole link stack is silent, and a session
    * that never connected is indistinguishable from one that was never
    * configured -- which is exactly how a stale core with no Voice Modem
    * option at all read as "the modem is there but won't dial". */
   /* "disabled" here means only the core's OWN TCP link is off, which is
    * the correct setting for frontend netplay -- netpacket is negotiated
    * later, by the frontend, and shows up as a link UP below.  Say so, or
    * the pair of lines reads as a contradiction. */
   if (mode == JLINK_MODE_DISABLED)
   {
      LOG_INF("[NETLINK] built-in TCP link disabled (device=%s) -- frontend "
              "netplay will carry the link if a session is running\n",
              JLinkDevice() == JLINK_DEVICE_VOICEMODEM ? "voicemodem"
                                                       : "jaglink");
      /* This is the failure that motivated the whole feature: a Voice
       * Modem selected with "auto" and no netplay session running does
       * nothing, silently, until the player either starts netplay or
       * picks a direct mode -- say so on screen. */
      if (narrate_changed && JLinkDevice() == JLINK_DEVICE_VOICEMODEM)
         netlink_osd("Voice Modem selected but link is idle -- start "
                     "netplay or pick a host");
   }
   else if (mode == JLINK_MODE_TCP_CLIENT)
   {
      LOG_INF("[NETLINK] mode=%s device=%s peer=%s:%d (address from %s)\n",
              netlink_mode_name(mode),
              JLinkDevice() == JLINK_DEVICE_VOICEMODEM ? "voicemodem"
                                                       : "jaglink",
              host[0] ? host : "127.0.0.1", port, src);
      if (narrate_changed)
      {
         netlink_osd("Network Link: %s (%s) -> %s:%d",
                     netlink_mode_name(mode),
                     netlink_device_label(JLinkDevice()),
                     host[0] ? host : "127.0.0.1", port);

         /* Device mismatch (fix wave, PR review finding 3): see
          * netlink_check_device_mismatch()'s declaration comment for why
          * this call alone does not cover the persisted-config load path,
          * and why netlink_rebuild_host_options() duplicates it. */
         netlink_check_device_mismatch(host[0] ? host : "127.0.0.1");
      }
   }
   else
   {
      LOG_INF("[NETLINK] mode=%s device=%s port=%d\n",
              netlink_mode_name(mode),
              JLinkDevice() == JLINK_DEVICE_VOICEMODEM ? "voicemodem"
                                                       : "jaglink",
              port);
      if (narrate_changed)
         netlink_osd("Network Link: %s (%s), port %d",
                     netlink_mode_name(mode),
                     netlink_device_label(JLinkDevice()), port);
   }

   /* The "From file" preset selected but no usable address in the file is
    * a silent 127.0.0.1 fallback -- the one that cost a debugging session. */
   if (want_file && !got_file)
      LOG_WRN("[NETLINK] 'From file' selected but no address read from "
              "<system>/vj_netlink.txt -- falling back to 127.0.0.1\n");

   if (mode != JLINK_MODE_DISABLED
       && JLinkMode() == JLINK_MODE_DISABLED)
      LOG_ERR("[NETLINK] failed to open %s -- link is DOWN\n",
              netlink_mode_name(mode));

   /* LAN discovery beacon/listener lifecycle (#467).  A host beacons AND
    * listens, so it can see other peers too; a client listens only --
    * neither dials out on its own.  Deliberately NOT started for "auto":
    * the host field it would populate is hidden in auto (see the
    * update_option_visibility() gate), and auto never auto-connects to a
    * discovered peer either (a Voice Modem "auto-dial" would place a call
    * the user did not initiate) -- so in auto the peer table is invisible
    * and unread. Auto is also the option's default, so starting a socket
    * here would open a UDP listener on port 42170 for every user on
    * every load, tripping the OS's Local Network permission prompt for
    * players who never touched the networking options. Anything else
    * (disabled, loopback) has no use for a peer list either, so discovery
    * stops. */
   {
      int device = (JLinkDevice() == JLINK_DEVICE_VOICEMODEM)
                   ? JLINK_DISC_DEV_VOICEMODEM : JLINK_DISC_DEV_JAGLINK;

      if (opt_value && !strcmp(opt_value, "tcp_server"))
      {
         if (!JLinkDiscStart(0 /* listen_only */, device, port))
            LOG_ERR("[NETLINK] LAN discovery failed to bind UDP port %d -- "
                    "host picker will only show the static presets (macOS/"
                    "iOS Local Network permission denied or timed out?)\n",
                    JLINK_DISC_PORT);
      }
      else if (opt_value && !strcmp(opt_value, "tcp_client"))
      {
         if (!JLinkDiscStart(1 /* listen_only */, device, port))
            LOG_ERR("[NETLINK] LAN discovery failed to bind UDP port %d -- "
                    "host picker will only show the static presets (macOS/"
                    "iOS Local Network permission denied or timed out?)\n",
                    JLINK_DISC_PORT);
      }
      else
         JLinkDiscStop();
   }
}

/* Discovered-host option list (task 4, #467): the LAN discovery beacon
 * (jlink_discover.c) finds peers, but libretro core options are a fixed
 * enumeration on every platform -- no core can offer a free-text field --
 * so a peer is useless until it becomes a selectable
 * virtualjaguar_netlink_host value.  Everything below wires that up.
 *
 * retro_core_option_v2_definition::values[] is a fixed-size array INLINE
 * in the struct (libretro.h), not a pointer, so option_defs_us (compiled
 * into libretro_core_options.h, already static storage duration) can be
 * mutated in place -- no separate "static so it outlives the call" array
 * is needed for the value list itself.  The value/label *strings* still
 * need their own static storage: netlink_peer_value/label below, since
 * they are built fresh from JLinkPeer data the frontend does not own. */
static char netlink_peer_value[JLINK_DISC_MAX_PEERS][JLINK_DISC_ADDR_MAX];
static char netlink_peer_label[JLINK_DISC_MAX_PEERS][128];

/* The three presets from libretro_core_options.h (127.0.0.1, jaghub.local,
 * vj_netlink.txt), snapshotted once from the pristine array before the
 * first peer splice.  Every rebuild below reads this copy rather than the
 * live option_defs_us array, which this same function overwrites -- so a
 * second rebuild can never mistake an already-spliced peer entry for a
 * preset. */
static struct retro_core_option_value netlink_host_presets[3];
static int netlink_host_presets_valid = 0;

/* Index of virtualjaguar_netlink_host in option_defs_us, found by key
 * rather than hard-coded so a reorder of the option table can't silently
 * corrupt the wrong option's value list.  Shared by the rebuild and the
 * retro_deinit() restore. */
static int netlink_host_option_index(void)
{
   int i;

   for (i = 0; option_defs_us[i].key; i++)
      if (!strcmp(option_defs_us[i].key, "virtualjaguar_netlink_host"))
         return i;
   return -1;
}

/* Rebuild virtualjaguar_netlink_host's value list from the current LAN
 * discovery peer table and push it to the frontend with a second
 * SET_CORE_OPTIONS_V2.  That is a legal call -- RetroArch's
 * core_option_manager tears down and rebuilds on it (runloop.c),
 * flushing current values to disk first -- but it IS a full teardown, so
 * callers must only invoke this when the peer set actually changed (see
 * the gated call site in retro_run()), never on a timer or per beacon.
 *
 * Values: 127.0.0.1, then one entry per peer labelled "<name> - <addr>"
 * (with " (JagLink)" / " (Voice Modem)" appended when the peer's device
 * differs from ours, so a mismatch is visible instead of silently
 * failing), then jaghub.local and vj_netlink.txt -- the existing presets
 * stay selectable, peers are added, not substituted. Capped at
 * JLINK_DISC_MAX_PEERS entries. */
static void netlink_rebuild_host_options(void)
{
   int idx, i, n, peer_count, my_device;
   char cur_host[128];
   /* What the link actually dials, as opposed to what the picker shows --
    * see netlink_resolve_host() and the mismatch call at the bottom. */
   char resolved_host[128];
   int cur_host_known;
   int cur_host_is_preset;
   int cur_host_found;
   bool options_pushed;

   idx = netlink_host_option_index();
   if (idx < 0)
      return;

   /* Selected-host-expired check (review note from task 4, #467): read
    * the value currently active in the frontend BEFORE the value list is
    * overwritten below.  If it names a discovered peer that is about to
    * drop out of the rebuilt list, RetroArch's core_option_manager resets
    * the option to its first value (127.0.0.1) with no explanation --
    * warn on screen before that silently happens rather than after. */
   cur_host_known     = 0;
   cur_host_is_preset = 0;
   cur_host_found     = 0;
   {
      struct retro_variable cur;

      cur.key = "virtualjaguar_netlink_host";
      cur.value = NULL;
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &cur) && cur.value)
      {
         strncpy(cur_host, cur.value, sizeof(cur_host) - 1);
         cur_host[sizeof(cur_host) - 1] = '\0';
         cur_host_known = 1;
         cur_host_is_preset = !strcmp(cur_host, "127.0.0.1")
                               || !strcmp(cur_host, "jaghub.local")
                               || !strcmp(cur_host, "vj_netlink.txt");
      }
   }

   /* Resolve here, next to the cur_host read and BEFORE the
    * SET_CORE_OPTIONS_V2 push below: after that push the frontend may have
    * reset virtualjaguar_netlink_host (a selected peer that dropped off the
    * LAN is no longer in the value list), so a read afterwards would see a
    * value the running link never dialed. */
   netlink_resolve_host(resolved_host, sizeof(resolved_host), NULL, NULL,
                        NULL);

   if (!netlink_host_presets_valid)
   {
      for (i = 0; i < 3 && option_defs_us[idx].values[i].value; i++)
         netlink_host_presets[i] = option_defs_us[idx].values[i];
      netlink_host_presets_valid = 1;
   }

   n = 0;
   option_defs_us[idx].values[n++] = netlink_host_presets[0]; /* 127.0.0.1 */

   my_device = (JLinkDevice() == JLINK_DEVICE_VOICEMODEM)
               ? JLINK_DISC_DEV_VOICEMODEM : JLINK_DISC_DEV_JAGLINK;

   peer_count = JLinkDiscPeerCount();
   if (peer_count > JLINK_DISC_MAX_PEERS)
      peer_count = JLINK_DISC_MAX_PEERS;

   for (i = 0; i < peer_count; i++)
   {
      const JLinkPeer *peer;
      const char *devtag;
      const char *name;

      peer = JLinkDiscPeerAt(i);
      if (!peer)
         break;

      if (cur_host_known && !cur_host_is_preset
          && !strcmp(peer->addr, cur_host))
         cur_host_found = 1;

      /* Beacon-supplied name is untrusted and may be empty; fall back to
       * the address so the label is never just " - 1.2.3.4". */
      name = peer->name[0] ? peer->name : peer->addr;

      devtag = "";
      if (peer->device != my_device)
         devtag = (peer->device == JLINK_DISC_DEV_VOICEMODEM)
                  ? " (Voice Modem)" : " (JagLink)";

      strncpy(netlink_peer_value[i], peer->addr,
              sizeof(netlink_peer_value[i]) - 1);
      netlink_peer_value[i][sizeof(netlink_peer_value[i]) - 1] = '\0';

      snprintf(netlink_peer_label[i], sizeof(netlink_peer_label[i]),
               "%s - %s%s", name, peer->addr, devtag);

      option_defs_us[idx].values[n].value = netlink_peer_value[i];
      option_defs_us[idx].values[n].label = netlink_peer_label[i];
      n++;
   }

   option_defs_us[idx].values[n++] = netlink_host_presets[1]; /* jaghub.local */
   option_defs_us[idx].values[n++] = netlink_host_presets[2]; /* vj_netlink.txt */

   option_defs_us[idx].values[n].value = NULL;
   option_defs_us[idx].values[n].label = NULL;

   options_pushed = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &options_us);

   /* SET_CORE_OPTIONS_V2 rebuilds RetroArch's whole core_option_manager
    * from these definitions, which carry no visibility field -- every
    * option comes back visible, not just virtualjaguar_netlink_host/port:
    * every group update_option_visibility() manages (CD-only keys,
    * cart-BIOS keys, per-port input remaps, mouse/rotary/analog tuning,
    * texdump/texture-replace) comes back too, and that function normally
    * only re-pushes SET_CORE_OPTIONS_DISPLAY for a group whose show_*
    * flag CHANGED since last call -- after this rebuild none of them did,
    * so nothing would re-push and every hidden row (e.g. mouse/rotary/
    * analog tuning on an ordinary RetroPad session, or the host row
    * itself in tcp_server mode, where discovery also runs) would reappear
    * and stay reappeared for the rest of the session.  Force a full
    * re-push of every managed key from current state -- see
    * visibility_force_push's declaration comment -- rather than
    * special-casing just the two keys this feature owns, so there is
    * still exactly one place that knows which rows are hidden when. */
   visibility_force_push = 1;
   update_option_visibility();

   /* SET_CORE_OPTIONS_V2 returns false on a frontend reporting core
    * options v2 unsupported (RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION <
    * 2) -- option_defs_us above was still mutated in place, but the
    * frontend never saw it, so the picker did NOT gain the peer.  Claiming
    * success here (or in the "Found N" toast below) would be the same
    * silent-failure class this fix wave exists to close. */
   if (options_pushed)
      LOG_INF("[NETLINK] host picker rebuilt: %d discovered peer%s\n",
              peer_count, peer_count == 1 ? "" : "s");
   else
      LOG_WRN("[NETLINK] SET_CORE_OPTIONS_V2 rejected by frontend (core "
              "options v2 unsupported?) -- host picker NOT updated, %d "
              "discovered peer%s not shown\n",
              peer_count, peer_count == 1 ? "" : "s");

   if (cur_host_known && !cur_host_is_preset && !cur_host_found)
   {
      LOG_WRN("[NETLINK] selected host %s dropped off the LAN -- falling "
              "back to 127.0.0.1\n", cur_host);
      netlink_osd("Selected host %s dropped off the LAN -- falling back "
                  "to 127.0.0.1", cur_host);
   }

   /* Only the "found" case gets a toast.  peer_count == 0 means this
    * rebuild was triggered by the last peer expiring, not a new one
    * arriving -- "Found 0 Jaguar host(s)" would tell the player nothing
    * they don't already know from the dropped-selection warning above (if
    * their host was the one that expired) or from the host list simply
    * being back down to the static presets (if it wasn't).  Also gated on
    * options_pushed: if the frontend rejected the rebuild, the picker
    * still shows the old list, so telling the player their host was
    * "Found" would be a lie -- see the LOG_WRN above. */
   if (peer_count > 0 && options_pushed)
      netlink_osd("Found %d Jaguar host(s) on the LAN", peer_count);

   /* Device-mismatch check (fix wave, PR review finding 3): the scan in
    * netlink_apply() runs BEFORE the JLinkDiscStart() call in that same
    * function, and a real JLinkDiscStart() resets the peer table -- so on
    * a persisted-config load (saved tcp_client + a host that turns out to
    * beacon the other device) the apply-time scan always sees an empty
    * table, and the OSD dedup there never re-fires once the peer shows up
    * ~1s later because (mode, device, host) hasn't changed.  This rebuild
    * only runs once the peer table just finished being repopulated from
    * live beacon data, so check again here -- gated on JLinkMode() rather
    * than the option string because JLinkMode() reflects what actually
    * got dialed (a socket-level TCP connect succeeds regardless of the
    * remote's device type; the mismatch is a protocol-level problem this
    * warning exists to surface before it manifests as silence).
    *
    * Compares the RESOLVED host, not cur_host (#501): cur_host is the raw
    * picker value, which under the "From file" preset is the literal
    * sentinel "vj_netlink.txt" -- never equal to a peer's dotted-quad addr,
    * so this whole check used to be dead for that configuration, and under
    * a VJ_NETLINK_HOST override it scanned for the wrong address entirely.
    * cur_host stays raw above because the expired-selection warning is
    * about what the PICKER shows, which is a different question. */
   if (JLinkMode() == JLINK_MODE_TCP_CLIENT)
      netlink_check_device_mismatch(resolved_host[0] ? resolved_host
                                                     : "127.0.0.1");
}

/* Gate for per-title enhancement defaults (issue #368). Read raw (never
 * through get_variable_pertitle()) at the top of check_variables() and once
 * in retro_load_game() before the hires read, so it is never itself
 * substituted by the DB. Defaults to enabled so headless callers/tests that
 * never read the option still get stock behaviour identical to "enabled"
 * with no DB match. */
static bool pertitle_enabled = true;

/* Blit-memo mode the option asked for, remembered because check_variables()
 * runs before ResolveBootConfig() on the load path and so cannot yet tell
 * cartridge from CD content (BlitMemoSetMode refuses the latter). */
static int blit_memo_requested = BLIT_MEMO_OFF;

/* Default value registered for a core option key, from the v2 definitions
 * in option_defs_us[] (libretro_core_options.h). */
static const char *core_option_default(const char *key)
{
   size_t i;
   for (i = 0; option_defs_us[i].key; i++)
      if (!strcmp(option_defs_us[i].key, key))
         return option_defs_us[i].default_value;
   return NULL;
}

/* GET_VARIABLE with per-title defaults (issue #368): when the frontend's
 * value equals the option's registered default (the user never touched it)
 * and the loaded title has a DB entry for this key, substitute the DB
 * value. A user-set non-default value always wins. Logs once per
 * substitution via LOG_INF. */
static bool get_variable_pertitle(struct retro_variable *var)
{
   bool ok = environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, var) && var->value;
   const char *ovr, *def;

   if (!pertitle_enabled)
      return ok;

   ovr = TitleDBOverride(var->key);
   if (!ovr)
      return ok;

   def = core_option_default(var->key);
   if (!ok || (def && !strcmp(var->value, def)))
   {
      LOG_INF("[titledb] %s: %s=%s (option at default)\n",
              TitleDBTitleName(), var->key, ovr);
      var->value = ovr;
      return true;
   }
   return ok;
}

/* Port 2 controller type as the CORE OPTION alone resolves it, with no
 * regard for what the frontend may have set.  Factored out of
 * check_variables() because retro_set_controller_port_device() has to be
 * able to re-resolve the option the moment the frontend releases its claim
 * -- nothing schedules a check_variables() for it, so deferring to "the
 * next one" meant a frontend's routine post-load JOYPAD/NONE assignment
 * detached the mouse for the rest of the session.
 *
 * Called from retro_set_controller_port_device() it can run before
 * retro_load_game(): TitleDBOverride() returns NULL with no title loaded,
 * so it degrades to a plain GET_VARIABLE.  The "auto -> per-title DB" leg
 * of the documented precedence genuinely cannot fire on that early call;
 * the check_variables() during retro_load_game() resolves it properly. */
static InputDevType p2_device_from_option(void)
{
   struct retro_variable var;
   InputDevType p2 = INPUTDEV_PAD;

   var.key   = "virtualjaguar_p2_device";
   var.value = NULL;
   if (get_variable_pertitle(&var) && var.value)
   {
      if (!strcmp(var.value, "mouse_st"))
         p2 = INPUTDEV_MOUSE_ST;
      else if (!strcmp(var.value, "mouse_amiga"))
         p2 = INPUTDEV_MOUSE_AMIGA_ON_ST;
      else if (!strcmp(var.value, "mouse_amiga_adapter"))
         p2 = INPUTDEV_MOUSE_AMIGA_ADAPTER;
      else if (!strcmp(var.value, "rotary"))
         p2 = INPUTDEV_ROTARY;
      else if (!strcmp(var.value, "analog"))
         p2 = INPUTDEV_ANALOG;
      else if (!strcmp(var.value, "driving"))
         p2 = INPUTDEV_DRIVING;
      /* "pad" and "auto" (with no DB row) both mean pad. */
   }

   return p2;
}

/* Port 1 controller type, same contract as p2_device_from_option().
 *
 * Port 1 offers pad, rotary, or the analog / driving controller (#437 --
 * TR10 restricts the bank-switching device to neither socket).  It needed
 * no such helper while it was pad-only -- retro_set_controller_port_device()
 * could hardcode
 * INPUTDEV_PAD when the frontend released the port -- but with a rotary
 * reachable there, that hardcode would re-detach it on a frontend's
 * routine post-load JOYPAD assignment, which is exactly the bug the port-2
 * helper exists to prevent.
 *
 * No titledb row will ever select a rotary (design spec section 4.7):
 * selecting one removes Up and Down from row 0, so a player using a pad on
 * that title would lose menu navigation entirely.  That is a functional
 * break, not a preference, so the rotary is opt-in always. */
static InputDevType p1_device_from_option(void)
{
   struct retro_variable var;
   InputDevType p1 = INPUTDEV_PAD;

   var.key   = "virtualjaguar_p1_device";
   var.value = NULL;
   if (get_variable_pertitle(&var) && var.value)
   {
      if (!strcmp(var.value, "rotary"))
         p1 = INPUTDEV_ROTARY;
      else if (!strcmp(var.value, "analog"))
         p1 = INPUTDEV_ANALOG;
      else if (!strcmp(var.value, "driving"))
         p1 = INPUTDEV_DRIVING;
      else if (!strcmp(var.value, "lightgun"))
         p1 = INPUTDEV_LIGHTGUN;
      /* "pad" and "auto" both mean pad. */
   }

   return p1;
}

/* Per-axis tuning option readers (#439).
 *
 * Deliberately thin: the clamps that decide what is a legal dead zone,
 * offset or exponent live in AxisTuneSet(), one layer down, so a
 * hand-edited config is bounded in exactly one place no matter which
 * option it came through.  These only have to survive a missing or
 * non-numeric string. */
static int32_t read_tune_units(const char *key)
{
   struct retro_variable var;

   /* NO CLAMP HERE, ON PURPOSE.  AxisTuneSet() owns the bounds for both
    * the dead zone and the offset; duplicating them here would give the
    * feature two sets of limits to keep in step.  The exponent reader
    * below does clamp, but only because it MULTIPLIES before handing the
    * value on and an unbounded percent would overflow on the way. */
   var.key   = key;
   var.value = NULL;

   if (get_variable_pertitle(&var) && var.value)
      return (int32_t)atoi(var.value);

   return 0;
}

static int32_t read_tune_exponent(const char *key)
{
   struct retro_variable var;
   int pct = 100;

   var.key   = key;
   var.value = NULL;

   if (get_variable_pertitle(&var) && var.value)
      pct = atoi(var.value);

   /* Percent -> Q8 (256 == 1.0), the same conversion the sensitivity
    * ladders use, and clamped BEFORE the multiply for the same reason
    * they are.  800% is AxisTuneSet's own 2048 ceiling expressed as a
    * percent, so nothing reachable is lost. */
   if (pct < 1)
      pct = 100;
   if (pct > 800)
      pct = 800;

   return (int32_t)((pct * 256) / 100);
}

static void check_variables(void)
{
   unsigned i;
   struct retro_variable var;
   struct retro_variable gate_var;

   gate_var.key = "virtualjaguar_pertitle_defaults";
   gate_var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &gate_var) && gate_var.value)
      pertitle_enabled = (strcmp(gate_var.value, "disabled") != 0);
   else
      pertitle_enabled = true;

   var.key = "virtualjaguar_usefastblitter";
   var.value = NULL;

   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         vjs.useFastBlitter = true;
      else
         vjs.useFastBlitter = false;
   }
   /* Recorded blit-memo post-states belong to one engine; flush the
    * memo whenever the engine identity flips. */
   BlitMemoNotifyEngine(vjs.useFastBlitter ? 1 : 0);

   /* Texture replacement (#369 deliverable 2): raw read (never a
    * per-title default).  Enabling triggers the one-off pack load once
    * the system dir + content CRC are known (retro_load_game). */
   var.key = "virtualjaguar_texture_replace";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      TexReplaceSetEnabled(strcmp(var.value, "enabled") == 0);
   else
      TexReplaceSetEnabled(0);

   var.key = "virtualjaguar_true_color";
   var.value = NULL;
   {
      bool tc_on = false;
      if (get_variable_pertitle(&var) && var.value)
         tc_on = strcmp(var.value, "enabled") == 0;
      /* An active texture-replacement pack presents through the shadow
       * framebuffer, so it forces the SURFACE on even with True Color
       * disabled -- but the Gouraud precision stores (the True Color
       * feature proper) follow the option alone, so a replacement-only
       * activation leaves every non-replaced pixel bit-identical
       * (docs/texture-dump.md, "Replacement pipeline"). */
      ShadowFBSetEnabled((tc_on || texReplaceEnabled) ? 1 : 0);
      ShadowFBSetPrecision(tc_on ? 1 : 0);
   }

   /* Internal resolution is applied ONCE at content load (retro_load_game)
    * because the libretro geometry maximum cannot grow mid-session.  Here
    * we only detect a mid-game change and tell the user it needs a
    * restart (design section 7.1). */
   var.key = "virtualjaguar_internal_resolution";
   var.value = NULL;
   if (get_variable_pertitle(&var) && var.value)
   {
      int hires_want = (strcmp(var.value, "2x") == 0) ? 2 : 1;
      if (hires_want == shadowHiresN)
         hires_restart_notice_logged = 0;
      else if (content_loaded && !hires_restart_notice_logged)
      {
         LOG_INF("[HIRES] internal resolution change to %s takes effect on restart\n",
                 var.value);
         hires_restart_notice_logged = 1;
      }
   }

   /* Enhancement hooks (issue #370) are applied ONCE at content load, so
    * this is compare-and-log only: never re-latch the gate here, or a
    * mid-session toggle would disagree with the bytes actually in ROM.
    * Read raw, like the load-time latch, so a DB row cannot flip its own
    * gate.  Same "takes effect on restart" contract the internal-resolution
    * option already has. */
   {
      struct retro_variable hook_var;
      int hook_want;
      hook_var.key = "virtualjaguar_enhancement_hooks";
      hook_var.value = NULL;
      hook_want = (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &hook_var)
                   && hook_var.value
                   && strcmp(hook_var.value, "enabled") == 0) ? 1 : 0;
      if (hook_want == TitleHookGetEnabled())
         hook_restart_notice_logged = 0;
      else if (content_loaded && !hook_restart_notice_logged)
      {
         LOG_INF("[hooks] enhancement hooks %s takes effect on restart\n",
                 hook_want ? "enabled" : "disabled");
         hook_restart_notice_logged = 1;
      }
   }

   /* Blit memoization (issue #411): off by default, tagged per title in
    * the DB.  BlitMemoSetMode() refuses CD content -- but on the
    * retro_load_game path this call happens BEFORE ResolveBootConfig,
    * so the requested mode is remembered and re-applied there, once
    * cartridge-vs-CD is actually known. */
   var.key = "virtualjaguar_blit_memo";
   var.value = NULL;
   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         blit_memo_requested = BLIT_MEMO_ON;
      else if (strcmp(var.value, "verify") == 0)
         blit_memo_requested = BLIT_MEMO_VERIFY;
      else
         blit_memo_requested = BLIT_MEMO_OFF;
      /* On the load path bootConfig does not exist yet, so applying the
       * mode here would log a mode that the CD check then overrides.
       * retro_load_game applies it after ResolveBootConfig instead. */
      if (content_loaded)
         BlitMemoSetMode(blit_memo_requested);
   }

   /* Texture dump (issue #369): runtime-toggleable, capture is passive
    * so no restart is needed.  Read raw (not through
    * get_variable_pertitle()): dumping is a dev-facing choice, never a
    * per-title default. */
   var.key = "virtualjaguar_texture_dump";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      TexDumpSetEnabled(strcmp(var.value, "enabled") == 0);
   else
      TexDumpSetEnabled(0);

   var.key = "virtualjaguar_texdump_16bpp";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "rgb") == 0)
         TexDumpSet16bppMode(TEXDUMP_16BPP_RGB);
      else if (strcmp(var.value, "both") == 0)
         TexDumpSet16bppMode(TEXDUMP_16BPP_BOTH);
      else
         TexDumpSet16bppMode(TEXDUMP_16BPP_CRY);
   }

   var.key = "virtualjaguar_crash_detect";
   var.value = NULL;
   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "verbose") == 0)
         CrashDetectSetMode(CRASH_DETECT_VERBOSE);
      else if (strcmp(var.value, "disabled") == 0)
         CrashDetectSetMode(CRASH_DETECT_OFF);
      else
         CrashDetectSetMode(CRASH_DETECT_ON);
   }
   else
   {
      CrashDetectSetMode(CRASH_DETECT_ON);
   }

   var.key = "virtualjaguar_cd_trace";
   var.value = NULL;
   if (get_variable_pertitle(&var) && var.value)
      CDTraceSetEnabled(strcmp(var.value, "enabled") == 0);
   else
      CDTraceSetEnabled(0);

   /* Blitter bus time: a blit kicked by the 68K charges the 68K the
    * blit's whole bus duration through the pending-stall channel (the
    * blitter is the top-priority bus master and the cacheless 68K is
    * frozen while it runs).  Fixes render-bound loops pacing faster
    * than hardware — Doom's menu auto-repeat landing inside a normal
    * button tap (#399/#401).  See BlitDurationSysclks() in
    * src/tom/blitter_mmio.c for the model and its deliberate floor. */
   var.key = "virtualjaguar_blitter_timing";
   var.value = NULL;
   vjs.blitterTiming = false;
   if (get_variable_pertitle(&var) && var.value)
      vjs.blitterTiming = (strcmp(var.value, "enabled") == 0);

   /* GPU pipeline/gateway timing (issue #401/#313).  Transient model
    * state must not survive a toggle: a stale gateway timestamp from
    * an earlier enabled period would charge phantom stalls on
    * re-enable. */
   var.key = "virtualjaguar_gpu_pipeline_timing";
   var.value = NULL;
   {
      bool pipeWas = vjs.gpuPipelineTiming;
      vjs.gpuPipelineTiming = false;
      if (get_variable_pertitle(&var) && var.value)
         vjs.gpuPipelineTiming = (strcmp(var.value, "enabled") == 0);
      if (pipeWas != vjs.gpuPipelineTiming)
         GPUPipeTimingReset();
   }

   /* DRAM timing: enabled/disabled only, covering BOTH halves of the
    * symmetric self-cost model (GPU stalls in gpu.c, 68K wait-states
    * in jaguar.c).  The calibration scale is deliberately NOT a core
    * option (manual knobs proved untunable on device) — VJ_DRAM_SCALE
    * overrides it for headless calibration experiments only. */
   var.key = "virtualjaguar_dram_timing";
   var.value = NULL;
   if (get_variable_pertitle(&var) && var.value)
      busArbiter.enabled = (strcmp(var.value, "enabled") == 0);
   else
      busArbiter.enabled = 0;
   /* No charging happens while disabled, so a carry left over from a
    * runtime toggle (or an older savestate) must not leak into the
    * first charged access when the option is re-enabled.  The
    * pending-stall channel is shared with the blitter bus-time model,
    * so it is only cleared when BOTH chargers are off — otherwise this
    * (default) branch would wipe live blitter charges at every option
    * read. */
   if (!busArbiter.enabled)
   {
      busArbiter.m68k_sysclk_carry = 0;
      busArbiter.refresh_clk_carry = 0;
      busArbiter.op_clk_accum = 0;
      if (!vjs.blitterTiming)
         busArbiter.m68k_pending_stall = 0;
   }
   {
      const char *scale_env = getenv("VJ_DRAM_SCALE");
      int dram_scale = (scale_env && scale_env[0]) ? atoi(scale_env) : 1;
      if (dram_scale < 1)
         dram_scale = 1;
      if (dram_scale > 16)
         dram_scale = 16;
      busArbiter.contention_scale = (uint8_t)dram_scale;
   }

   /* Clock-scale enhancement levers (issue #314).  Config, not state:
    * never serialized.  Stored in percent so 1x is an exact integer
    * identity (see jaguar.h).  Defaults to 100 whenever the option is
    * absent so nothing in the test suite ever runs at non-1x. */
   var.key = "virtualjaguar_m68k_clock_scale";
   var.value = NULL;
   m68kClockScalePct = 100;
   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "0.5x") == 0)
         m68kClockScalePct = 50;
      else if (strcmp(var.value, "1.5x") == 0)
         m68kClockScalePct = 150;
      else if (strcmp(var.value, "2x") == 0)
         m68kClockScalePct = 200;
      else if (strcmp(var.value, "3x") == 0)
         m68kClockScalePct = 300;
   }
   /* Drop any carried sub-cycle remainder when the scale (possibly)
    * changed, so a new scale starts from a clean accumulator. */
   M68KClockScaleReset();

   var.key = "virtualjaguar_risc_clock_scale";
   var.value = NULL;
   riscClockScalePct = 100;
   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "0.5x") == 0)
         riscClockScalePct = 50;
      else if (strcmp(var.value, "1.5x") == 0)
         riscClockScalePct = 150;
      else if (strcmp(var.value, "2x") == 0)
         riscClockScalePct = 200;
   }
   /* Drop the GPU's sub-cycle bus-stall remainder when the RISC scale
    * (possibly) changed — mirrors M68KClockScaleReset() above. */
   GPUClockScaleReset();

   if (m68kClockScalePct != 100 || riscClockScalePct != 100)
      LOG_INF("[CLOCK] Non-stock clock scales active: M68K %u%%, RISC %u%% (enhancement mode; timing-sensitive bug reports are only valid at 1x)\n",
              (unsigned)m68kClockScalePct, (unsigned)riscClockScalePct);

   var.key = "virtualjaguar_uart_device";
   var.value = NULL;
   if (get_variable_pertitle(&var) && var.value
       && strcmp(var.value, "voicemodem") == 0)
      JLinkSetDevice(JLINK_DEVICE_VOICEMODEM);
   else
      JLinkSetDevice(JLINK_DEVICE_JAGLINK);

   var.key = "virtualjaguar_netlink";
   var.value = NULL;
   if (get_variable_pertitle(&var) && var.value)
      netlink_apply(netlink_resolve_mode(var.value), var.value);
   else
      netlink_apply(JLINK_MODE_DISABLED, NULL);

   var.key = "virtualjaguar_bios";
   var.value = NULL;

   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         vjs.useJaguarBIOS = true;
      else
         vjs.useJaguarBIOS = false;
   }

   var.key = "virtualjaguar_bios_type";
   var.value = NULL;

   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "m") == 0)
         vjs.biosType = BT_M_SERIES;
      else if (strcmp(var.value, "custom") == 0)
         vjs.biosType = BT_CUSTOM;
      else
         vjs.biosType = BT_K_SERIES;
   }

   var.key = "virtualjaguar_pal";
   var.value = NULL;

   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         vjs.hardwareTypeNTSC = false;
      else
         vjs.hardwareTypeNTSC = true;
   }

   var.key = "virtualjaguar_memory_track";
   var.value = NULL;

   if (get_variable_pertitle(&var) && var.value)
      opt_memory_track = (strcmp(var.value, "disabled") != 0);

   /* Jaguar GameDrive: mode is latched here; activation happens at
    * content load (JGDLoadROM), so mid-game toggles apply on restart. */
   var.key = "virtualjaguar_jgd";
   var.value = NULL;

   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         JGDSetMode(JGD_MODE_ENABLED);
      else if (strcmp(var.value, "disabled") == 0)
         JGDSetMode(JGD_MODE_DISABLED);
      else
         JGDSetMode(JGD_MODE_AUTO);
   }
   else
      JGDSetMode(JGD_MODE_AUTO);

   var.key = "virtualjaguar_cd_bios_type";
   var.value = NULL;

   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "dev") == 0)
         vjs.cdBiosType = CDBIOS_DEV;
      else
         vjs.cdBiosType = CDBIOS_RETAIL;
   }

   var.key = "virtualjaguar_cd_boot_mode";
   var.value = NULL;

   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "hle") == 0)
         vjs.cdBootMode = CDBOOT_HLE;
      else if (strcmp(var.value, "bios") == 0)
         vjs.cdBootMode = CDBOOT_BIOS;
      else
         vjs.cdBootMode = CDBOOT_AUTO;
   }

   var.key = "virtualjaguar_cd_read_speed";
   var.value = NULL;

   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "1x") == 0)
         vjs.cdReadSpeed = CDSPEED_1X;
      else if (strcmp(var.value, "4x") == 0)
         vjs.cdReadSpeed = CDSPEED_4X;
      else if (strcmp(var.value, "8x") == 0)
         vjs.cdReadSpeed = CDSPEED_8X;
      else if (strcmp(var.value, "instant") == 0)
         vjs.cdReadSpeed = CDSPEED_INSTANT;
      else
         vjs.cdReadSpeed = CDSPEED_2X;
   }
   else
      vjs.cdReadSpeed = CDSPEED_2X;

   /* Port 2 controller type (#429).  Precedence, per the design spec:
    *   1. a device the frontend explicitly set via
    *      retro_set_controller_port_device wins until it changes it again;
    *   2. otherwise this option;
    *   3. "auto" resolves through the per-title DB, and falls back to
    *      "pad" -- which is bit-identical to a core without this feature.
    * No titledb row ships in this PR, so "auto" is "pad" for every title
    * today and the mouse is strictly opt-in.
    *
    * Port 1 (#436) follows the same precedence, but offers pad or rotary
    * only and will never get a titledb row -- see p1_device_from_option. */
   {
      InputDevType p1 = p1_device_from_option();
      InputDevType p2 = p2_device_from_option();

      if (port_device_forced[1])
         p2 = port_device_frontend[1];

      apply_port_device(1, p2);

      if (port_device_forced[0])
         p1 = port_device_frontend[0];

      apply_port_device(0, p1);
   }

   var.key   = "virtualjaguar_mouse_sensitivity";
   var.value = NULL;
   if (get_variable_pertitle(&var) && var.value)
   {
      /* Percent -> Q8 (256 == 1.0).  Clamp BEFORE the multiply: the
       * option value comes from the frontend's config file, which a user
       * can hand-edit to anything, and InputDevSetScale's own clamp fires
       * only after this multiply has already overflowed a signed int.
       * 1600% is InputDevSetScale's 4096 ceiling expressed as a percent
       * (4096 * 100 / 256), so nothing reachable is lost. */
      int pct = atoi(var.value);
      if (pct < 1)
         pct = 100;
      if (pct > 1600)
         pct = 1600;
      mouse_scale_q8 = (int32_t)((pct * 256) / 100);
   }
   else
      mouse_scale_q8 = 256;

   var.key   = "virtualjaguar_rotary_sensitivity";
   var.value = NULL;
   if (get_variable_pertitle(&var) && var.value)
   {
      /* Same pre-multiply clamp as the mouse ladder above, for the same
       * hand-edited-config reason. */
      int pct = atoi(var.value);
      if (pct < 1)
         pct = 100;
      if (pct > 1600)
         pct = 1600;
      rotary_scale_q8 = (int32_t)((pct * 256) / 100);
   }
   else
      rotary_scale_q8 = 256;

   /* Per-axis tuning (#439).  Read unconditionally: every one of these
    * defaults to the identity, so a build where nobody touched the menu
    * resolves to exactly the pre-#439 numbers. */
   mouse_deadzone[0]    = read_tune_units("virtualjaguar_mouse_deadzone_x");
   mouse_deadzone[1]    = read_tune_units("virtualjaguar_mouse_deadzone_y");
   mouse_offset[0]      = read_tune_units("virtualjaguar_mouse_offset_x");
   mouse_offset[1]      = read_tune_units("virtualjaguar_mouse_offset_y");
   mouse_exponent_q8[0] = read_tune_exponent("virtualjaguar_mouse_exponent_x");
   mouse_exponent_q8[1] = read_tune_exponent("virtualjaguar_mouse_exponent_y");

   rotary_deadzone      = read_tune_units("virtualjaguar_rotary_deadzone");
   rotary_offset        = read_tune_units("virtualjaguar_rotary_offset");
   rotary_exponent_q8   = read_tune_exponent("virtualjaguar_rotary_exponent");

   /* Analog / driving ladder (#437); units are ADC counts, see the
    * statics.  AxisTuneSet() owns the bounds, same as the others. */
   analog_deadzone[0]    = read_tune_units("virtualjaguar_analog_deadzone_x");
   analog_deadzone[1]    = read_tune_units("virtualjaguar_analog_deadzone_y");
   analog_offset[0]      = read_tune_units("virtualjaguar_analog_offset_x");
   analog_offset[1]      = read_tune_units("virtualjaguar_analog_offset_y");
   analog_exponent_q8[0] = read_tune_exponent("virtualjaguar_analog_exponent_x");
   analog_exponent_q8[1] = read_tune_exponent("virtualjaguar_analog_exponent_y");

   /* One scale and one tuning set per port, picked by what is actually
    * plugged into it -- the two ladders are separate options because a
    * spinner and a mouse want very different multipliers. */
   {
      unsigned port;

      for (port = 0; port < 2; port++)
         apply_port_tuning((int)port);
   }

   var.key   = "virtualjaguar_rotary_id";
   var.value = NULL;
   {
      int reports = (get_variable_pertitle(&var) && var.value
                     && !strcmp(var.value, "rotary")) ? 1 : 0;

      InputDevSetRotaryID(0, reports);
      InputDevSetRotaryID(1, reports);
   }

   var.key = "virtualjaguar_alt_inputs";
   var.value = NULL;
   if (get_variable_pertitle(&var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         enable_alt_inputs = true;
      else
         enable_alt_inputs = false;
   }

   for (i = 0; i < 2; i++)
   {
      unsigned j;
      char key[64];

      /* Initialize all retropad mappings to BUTTON_NONE so unmapped
       * entries don't accidentally trigger BUTTON_U (index 0). */
      for (j = 0; j < RETROPAD_INPUT_COUNT; j++)
         jag_retropad[i][j] = BUTTON_NONE;

      build_port_option_key(key, sizeof(key), i, "_numpad_to_kb");
      var.key   = key;
      var.value = NULL;
      if (get_variable_pertitle(&var) && var.value)
      {
         if (!strcmp(var.value, "disabled"))
            numpad_to_kb[i] = 0;
         else if (!strcmp(var.value, "numbers"))
            numpad_to_kb[i] = 1;
         else if (!strcmp(var.value, "keypad"))
            numpad_to_kb[i] = 2;
      }

      for (j = 0; j < ARRAY_SIZE(retropad_option_map); j++)
      {
         build_port_option_key(key, sizeof(key), i, retropad_option_map[j].suffix);
         var.key   = key;
         var.value = NULL;
         if (get_variable_pertitle(&var) && var.value)
            jag_retropad[i][retropad_option_map[j].id] = get_button_id(var.value);
      }
   }

   update_option_visibility();
}

static void update_input(void)
{
   unsigned i;
   int16_t ret[2];
   unsigned player;
   if (!input_poll_cb)
      return;

   ret[0] = ret[1] = 0;
   input_poll_cb();

   for(i=BUTTON_FIRST;i<=BUTTON_LAST;i++){
       joypad0Buttons[i] = 0x00;
       joypad1Buttons[i] = 0x00;
   }

   if (libretro_supports_bitmasks)
   {
      for (player = 0; player < 2; player++)
         ret[player] = input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
   }
   else
   {
      for (player = 0; player < 2; player++)
      {
         for (i=RETRO_DEVICE_ID_JOYPAD_B; i <= RETRO_DEVICE_ID_JOYPAD_R3; ++i)
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, i))
               ret[player] |= (1 << i);
      }
   }

   if (enable_alt_inputs)
   {
      int16_t analog_val[2][4];

      for (player = 0; player < 2; player++)
      {
         // for buttons remapped to analogs
         analog_val[player][0] = input_state_cb(player, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
         analog_val[player][1] = input_state_cb(player, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
         analog_val[player][2] = input_state_cb(player, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
         analog_val[player][3] = input_state_cb(player, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);

         for (i = RETRO_DEVICE_ID_JOYPAD_B; i <= RETRO_DEVICE_ID_JOYPAD_RR; i++)
         {
            if (jag_retropad[player][i] == BUTTON_NONE)
               continue;

            if (i < RETRO_DEVICE_ID_JOYPAD_LU) // dpad, buttons and triggers
            {
               if (ret[player] & (1 << i))
                  joypad_buttons[player][jag_retropad[player][i]] = 0xff;
            }
            else if (i > RETRO_DEVICE_ID_JOYPAD_R3) // analogs
            {
               switch (i)
               {
                  case RETRO_DEVICE_ID_JOYPAD_LU:
                     if (analog_val[player][0] < -ANALOG_THRESHOLD)
                        joypad_buttons[player][jag_retropad[player][RETRO_DEVICE_ID_JOYPAD_LU]] = 0xff;
                     break;
                  case RETRO_DEVICE_ID_JOYPAD_LD:
                     if (analog_val[player][0] > ANALOG_THRESHOLD)
                        joypad_buttons[player][jag_retropad[player][RETRO_DEVICE_ID_JOYPAD_LD]] = 0xff;
                     break;
                  case RETRO_DEVICE_ID_JOYPAD_LL:
                     if (analog_val[player][1] < -ANALOG_THRESHOLD)
                        joypad_buttons[player][jag_retropad[player][RETRO_DEVICE_ID_JOYPAD_LL]] = 0xff;
                     break;
                  case RETRO_DEVICE_ID_JOYPAD_LR:
                     if (analog_val[player][1] > ANALOG_THRESHOLD)
                        joypad_buttons[player][jag_retropad[player][RETRO_DEVICE_ID_JOYPAD_LR]] = 0xff;
                     break;
                  case RETRO_DEVICE_ID_JOYPAD_RU:
                     if (analog_val[player][2] < -ANALOG_THRESHOLD)
                        joypad_buttons[player][jag_retropad[player][RETRO_DEVICE_ID_JOYPAD_RU]] = 0xff;
                     break;
                  case RETRO_DEVICE_ID_JOYPAD_RD:
                     if (analog_val[player][2] > ANALOG_THRESHOLD)
                        joypad_buttons[player][jag_retropad[player][RETRO_DEVICE_ID_JOYPAD_RD]] = 0xff;
                     break;
                  case RETRO_DEVICE_ID_JOYPAD_RL:
                     if (analog_val[player][3] < -ANALOG_THRESHOLD)
                        joypad_buttons[player][jag_retropad[player][RETRO_DEVICE_ID_JOYPAD_RL]] = 0xff;
                     break;
                  case RETRO_DEVICE_ID_JOYPAD_RR:
                     if (analog_val[player][3] > ANALOG_THRESHOLD)
                        joypad_buttons[player][jag_retropad[player][RETRO_DEVICE_ID_JOYPAD_RR]] = 0xff;
                     break;
               }
            }
         }

         // numpad buttons to keyboard
         if (numpad_to_kb[player] == 1)
         {
            for (i = 0; i < 12; i++)
               if (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, number_keys[i]))
                  joypad_buttons[player][i + 4] = 0xff; // i + 4 because numpad enums start at 4
         }
         else if (numpad_to_kb[player] == 2)
         {
            for (i = 0; i < 12; i++)
               if (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, keypad_keys[i]))
                  joypad_buttons[player][i + 4] = 0xff;
         }
      }
   }
   else
   {
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_UP))
         joypad0Buttons[BUTTON_U] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN))
         joypad0Buttons[BUTTON_D] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT))
         joypad0Buttons[BUTTON_L] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT))
         joypad0Buttons[BUTTON_R] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_A))
         joypad0Buttons[BUTTON_A] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_B))
         joypad0Buttons[BUTTON_B] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_Y))
         joypad0Buttons[BUTTON_C] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_SELECT))
         joypad0Buttons[BUTTON_PAUSE] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_START))
         joypad0Buttons[BUTTON_OPTION] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_X) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_0)? 1 : 0))
         joypad0Buttons[BUTTON_0] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_L) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_1)? 1 : 0))
         joypad0Buttons[BUTTON_1] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_R) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_2)? 1 : 0))
         joypad0Buttons[BUTTON_2] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_L2) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_3)? 1 : 0))
         joypad0Buttons[BUTTON_3] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_R2) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_4)? 1 : 0))
         joypad0Buttons[BUTTON_4] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_L3) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_5)? 1 : 0))
         joypad0Buttons[BUTTON_5] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_R3) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_6)? 1 : 0))
         joypad0Buttons[BUTTON_6] = 0xff;
      if ((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_7)? 1 : 0))
         joypad0Buttons[BUTTON_7] = 0xff;
      if ((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_8)? 1 : 0))
         joypad0Buttons[BUTTON_8] = 0xff;
      if ((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_9)? 1 : 0))
         joypad0Buttons[BUTTON_9] = 0xff;
      if ((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_MINUS)? 1 : 0))
         joypad0Buttons[BUTTON_s] = 0xff;
      if ((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_EQUALS)? 1 : 0))
         joypad0Buttons[BUTTON_d] = 0xff;

      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_UP))
         joypad1Buttons[BUTTON_U] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN))
         joypad1Buttons[BUTTON_D] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT))
         joypad1Buttons[BUTTON_L] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT))
         joypad1Buttons[BUTTON_R] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_A))
         joypad1Buttons[BUTTON_A] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_B))
         joypad1Buttons[BUTTON_B] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_Y))
         joypad1Buttons[BUTTON_C] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_SELECT))
         joypad1Buttons[BUTTON_PAUSE] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_START))
         joypad1Buttons[BUTTON_OPTION] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_X) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_p)? 1 : 0))
         joypad1Buttons[BUTTON_0] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_L) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_q)? 1 : 0))
         joypad1Buttons[BUTTON_1] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_R) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_w)? 1 : 0))
         joypad1Buttons[BUTTON_2] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_L2) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_e)? 1 : 0))
         joypad1Buttons[BUTTON_3] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_R2) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_r)? 1 : 0))
         joypad1Buttons[BUTTON_4] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_L3) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_t)? 1 : 0))
         joypad1Buttons[BUTTON_5] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_R3) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_y)? 1 : 0))
         joypad1Buttons[BUTTON_6] = 0xff;
      if ((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_u)? 1 : 0))
         joypad1Buttons[BUTTON_7] = 0xff;
      if ((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_i)? 1 : 0))
         joypad1Buttons[BUTTON_8] = 0xff;
      if ((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_o)? 1 : 0))
         joypad1Buttons[BUTTON_9] = 0xff;
      if ((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_LEFTBRACKET)? 1 : 0))
         joypad1Buttons[BUTTON_s] = 0xff;
      if ((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_RIGHTBRACKET)? 1 : 0))
         joypad1Buttons[BUTTON_d] = 0xff;
   }

   /* ---- non-pad devices (#428/#429) --------------------------------
    *
    * Runs after every retropad fill path so it cannot be bypassed by one
    * of them.  There are three (the plain bitmask branch, the
    * enable_alt_inputs remap branch including its analog-as-button cases,
    * and the numpad_to_kb sub-path that writes slots 4-15 straight from
    * RETRO_DEVICE_KEYBOARD) and missing any one of them would keep
    * injecting host presses into a port with no pad plugged into it.
    *
    * A mouse port takes NOTHING from the retropad: physically there is no
    * pad there, and leaving the host contribution would let a gamepad and
    * a mouse drive the same six lines at once.
    *
    * The suppression is deferred until the mouse is LIVE -- see
    * inputdev_live: reading the frontend's mouse state first and
    * suppressing only once it has been non-zero guarantees that selecting
    * a mouse can never leave the port with no working input at all.
    *
    * A ROTARY PORT IS DIFFERENT, and this is the branch that matters most
    * to a real user.  A rotary is a modified standard controller: it
    * really does have A, B, C, Option, Pause and the keypad, and those
    * stay wired to the retropad.  Only Up/Down/Left/Right are withheld,
    * because on that controller those four lines ARE the encoder (Up and
    * Down do not exist; Left/Right are Phase 0/Phase 1) -- so the withhold
    * is unconditional here, with no inputdev_live deferral, for the reason
    * spelled out at inputdev_live itself.
    *
    * Keeping the buttons is not a nicety.  Tempest 2000's rotary support
    * is hidden behind an unlock that requires pressing PAUSE ON BOTH
    * CONTROLLERS SIMULTANEOUSLY from the Options screen, and the unlock
    * is persisted in the game's EEPROM save.  Zero the whole array for a
    * rotary port and the user can never turn the feature on at all. */
   if (InputDevAnyAttached())
   {
      for (player = 0; player < 2; player++)
      {
         InputDevType t = InputDevGetType((int)player);
         int32_t  dx, dy;
         uint32_t buttons;

         if (t == INPUTDEV_PAD)
            continue;

         /* Analog / driving controller (#437): an ABSOLUTE device fed
          * from RETRO_DEVICE_ANALOG, not the relative mouse path below.
          *
          * LIVENESS: same rule as the mouse, adapted to an absolute
          * source.  A centred stick is indistinguishable from "no analog
          * routed at all", so the port keeps its RetroPad -- and the
          * device stays entirely inert, controller-type probes included
          * (inputdev.h, ENGAGEMENT) -- until a deflection past the
          * existing stick-to-dpad threshold proves the frontend is
          * routing analog state.  Button presses deliberately do NOT
          * count: they are ambiguous with pad presses.  Once live, the
          * whole pad is suppressed (the physical device has no keypad /
          * Pause / Option -- TR10 gives it A-D, a hat and two axes). */
         if (inputdev_is_analog_type(t))
         {
            int32_t  ax, ay;
            uint32_t sw = 0;

            ax = (int32_t)input_state_cb(player, RETRO_DEVICE_ANALOG,
                                         RETRO_DEVICE_INDEX_ANALOG_LEFT,
                                         RETRO_DEVICE_ID_ANALOG_X);
            ay = (int32_t)input_state_cb(player, RETRO_DEVICE_ANALOG,
                                         RETRO_DEVICE_INDEX_ANALOG_LEFT,
                                         RETRO_DEVICE_ID_ANALOG_Y);

            if (t == INPUTDEV_DRIVING)
            {
               /* Driving skin: steering on stick X; accelerator / brake
                * on the R2 / L2 analog triggers when the frontend
                * reports them (0..32767 each), stick Y otherwise.  The
                * feed convention is +y DOWN (host), which the device
                * flips to TR10's +accelerator, so accel must arrive
                * negative here. */
               int32_t r2 = (int32_t)input_state_cb(player,
                               RETRO_DEVICE_ANALOG,
                               RETRO_DEVICE_INDEX_ANALOG_BUTTON,
                               RETRO_DEVICE_ID_JOYPAD_R2);
               int32_t l2 = (int32_t)input_state_cb(player,
                               RETRO_DEVICE_ANALOG,
                               RETRO_DEVICE_INDEX_ANALOG_BUTTON,
                               RETRO_DEVICE_ID_JOYPAD_L2);

               if (r2 || l2)
                  ay = l2 - r2;
            }

            if (!inputdev_live[player]
                && (ax > ANALOG_THRESHOLD || ax < -ANALOG_THRESHOLD
                    || ay > ANALOG_THRESHOLD || ay < -ANALOG_THRESHOLD))
            {
               inputdev_live[player] = true;
               LOG_INF("[input] port %d: analog controller is live, "
                       "RetroPad released\n", player + 1);
            }

            if (inputdev_live[player])
            {
               /* A/B/C from the same RetroPad slots the pad maps them to
                * (A/B/Y); D -- TR10's fourth button -- on X.  Hat (gear
                * shift Up/Down on the driving skin) from the d-pad. */
               if (ret[player] & (1 << RETRO_DEVICE_ID_JOYPAD_A))
                  sw |= INPUTDEV_SW_A;
               if (ret[player] & (1 << RETRO_DEVICE_ID_JOYPAD_B))
                  sw |= INPUTDEV_SW_B;
               if (ret[player] & (1 << RETRO_DEVICE_ID_JOYPAD_Y))
                  sw |= INPUTDEV_SW_C;
               if (ret[player] & (1 << RETRO_DEVICE_ID_JOYPAD_X))
                  sw |= INPUTDEV_SW_D;
               if (ret[player] & (1 << RETRO_DEVICE_ID_JOYPAD_UP))
                  sw |= INPUTDEV_SW_UP;
               if (ret[player] & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN))
                  sw |= INPUTDEV_SW_DOWN;
               if (ret[player] & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT))
                  sw |= INPUTDEV_SW_LEFT;
               if (ret[player] & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT))
                  sw |= INPUTDEV_SW_RIGHT;

               memset(joypad_buttons[player], 0x00, BUTTON_LAST + 1);
               InputDevFeedAnalog((int)player, ax, ay, sw);
            }
            continue;
         }

         /* A LIGHT GUN IS AN ABSOLUTE POINTER, so it shares nothing with
          * the two relative-motion devices below and returns early.
          *
          * The whole hi-res guardrail (#400) is the two divisions here.
          * RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X/Y are normalised over the
          * frame this core REPORTS, and retro_run publishes
          * game_width = tomWidth * shadowHiresN.  Dividing back down to
          * tomWidth/tomHeight is what makes a 2x session latch the same
          * LPH/LPV as a 1x one; without it every shot would land at
          * roughly twice its intended position.  Nothing downstream of
          * here knows the internal-resolution option exists. */
         if (t == INPUTDEV_LIGHTGUN)
         {
            int32_t nat_w = (shadowHiresN > 0)
                          ? (int32_t)(game_width  / shadowHiresN) : 0;
            int32_t nat_h = (shadowHiresN > 0)
                          ? (int32_t)(game_height / shadowHiresN) : 0;
            int32_t gx    = (int32_t)input_state_cb(player,
                     RETRO_DEVICE_LIGHTGUN, 0,
                     RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X);
            int32_t gy    = (int32_t)input_state_cb(player,
                     RETRO_DEVICE_LIGHTGUN, 0,
                     RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y);
            int32_t col   = 0;
            int32_t rowpx = 0;
            int      off;

            buttons = 0;
            if (input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0,
                               RETRO_DEVICE_ID_LIGHTGUN_TRIGGER))
               buttons |= INPUTDEV_GUN_TRIGGER;
            if (input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0,
                               RETRO_DEVICE_ID_LIGHTGUN_AUX_A))
               buttons |= INPUTDEV_GUN_AUX_A;
            if (input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0,
                               RETRO_DEVICE_ID_LIGHTGUN_AUX_B))
               buttons |= INPUTDEV_GUN_AUX_B;
            if (input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0,
                               RETRO_DEVICE_ID_LIGHTGUN_START))
               buttons |= INPUTDEV_GUN_START;
            if (input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0,
                               RETRO_DEVICE_ID_LIGHTGUN_SELECT))
               buttons |= INPUTDEV_GUN_SELECT;

            /* Off-screen is the "photodiode sees no light" case: no LP
             * pulse, so LPH/LPV keep their last value while the trigger
             * still reports normally (the pin is electrically the same
             * wherever the barrel points).  A frame before the first
             * geometry is published, or a coordinate outside the frame,
             * is treated the same way rather than clamped to an edge --
             * clamping would invent an aim point the player never made. */
            off = input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0,
                                 RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN)
                  ? 1 : 0;

            if (nat_w <= 0 || nat_h <= 0)
               off = 1;
            else
            {
               col   = ((gx + 0x8000) * nat_w) / 0x10000;
               rowpx = ((gy + 0x8000) * nat_h) / 0x10000;
               if (col < 0 || col >= nat_w || rowpx < 0 || rowpx >= nat_h)
                  off = 1;
            }

            InputDevFeedLightgun((int)player, col, rowpx, off, buttons);
            continue;
         }

         dx = (int32_t)input_state_cb(player, RETRO_DEVICE_MOUSE, 0,
                                      RETRO_DEVICE_ID_MOUSE_X);
         dy      = 0;
         buttons = 0;

         if (t == INPUTDEV_ROTARY)
         {
            /* Withhold only the four direction slots; everything from
             * BUTTON_s (4) up is a real switch on a real rotary.  The
             * phases are published into BUTTON_L / BUTTON_R by
             * InputDevClock() on each $F14000 row-0 read. */
            joypad_buttons[player][BUTTON_U] = 0x00;
            joypad_buttons[player][BUTTON_D] = 0x00;
            joypad_buttons[player][BUTTON_L] = 0x00;
            joypad_buttons[player][BUTTON_R] = 0x00;
         }
         else
         {
            dy = (int32_t)input_state_cb(player, RETRO_DEVICE_MOUSE, 0,
                                         RETRO_DEVICE_ID_MOUSE_Y);
            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0,
                               RETRO_DEVICE_ID_MOUSE_LEFT))
               buttons |= INPUTDEV_BTN_LEFT;
            if (input_state_cb(player, RETRO_DEVICE_MOUSE, 0,
                               RETRO_DEVICE_ID_MOUSE_RIGHT))
               buttons |= INPUTDEV_BTN_RIGHT;

            if (dx || dy || buttons)
            {
               if (!inputdev_live[player])
               {
                  inputdev_live[player] = true;
                  LOG_INF("[input] port %d: mouse is live, RetroPad released\n",
                          player + 1);
               }
            }

            if (inputdev_live[player])
               memset(joypad_buttons[player], 0x00, BUTTON_LAST + 1);
         }

         InputDevFeed((int)player, dx, dy, buttons);
      }
   }
}

/************************************
 * libretro implementation
 ************************************/

static struct retro_system_av_info g_av_info;

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "Virtual Jaguar";
   info->library_version  = CORE_VERSION;
   info->need_fullpath    = false;
   info->valid_extensions = JAGUAR_VALID_EXTENSIONS;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   memset(info, 0, sizeof(*info));
   /* Advertised timing (issue #392).  The field rate is derived from the
    * field the core actually paces -- JaguarGetFieldRateHz(), defined
    * once in jaguar.h with its JTRM citations -- instead of the rounded
    * 60/50 that used to be hard-coded here.  A non-interlaced NTSC field
    * is 524 halflines x 31.7778 us = 60.05445 Hz; PAL is 624 x 32.0 us =
    * 50.08013 Hz.  (NOT 59.94 Hz: that is the 262.5-line INTERLACED rate,
    * and 525 halflines would put TOM into interlace -- JTRM Rev 8 p.15.)
    *
    * The declared sample rate has to follow, because retro_run hands the
    * frontend a FIXED batch of BUFNTSC/BUFPAL samples -- 800 / 960 stereo
    * pairs -- exactly once per field, so the true output rate IS
    * pairs x field rate (48043.6 Hz NTSC / 48076.9 Hz PAL).  Advertising
    * a flat 48000 against the corrected fps would over-deliver by ~0.09%
    * forever, draining/overfilling the frontend's audio buffer: the
    * underrun-pop class that test_audio_rate gates at 5 samples/sec.
    * Before this change, 60 x 800 and 50 x 960 were both exactly 48000 --
    * self-consistent only because both numbers were wrong together.
    *
    * 48000 remains the DAC's internal resample target (DAC_AUDIO_RATE);
    * it cancels out of the pitch math (the per-output phase step is
    * ring-samples-captured / output-pairs), so this is an advertised-value
    * change only, with no effect on emulated state or emitted samples. */
   info->timing.fps            = JaguarGetFieldRateHz();
   info->timing.sample_rate    = (double)(vjs.hardwareTypeNTSC
                                          ? (BUFNTSC / 2) : (BUFPAL / 2))
                                 * info->timing.fps;
   info->geometry.base_width   = game_width;
   info->geometry.base_height  = game_height;
   /* Hi-res: the maxima scale by the (load-time-fixed) internal
    * resolution factor; shadowHiresN is 1 when the option is off. */
   info->geometry.max_width    = 652 * shadowHiresN; // Highest value encountered during testing
   /* Must bound every height the core can emit, not the nominal active
    * display.  VDB/VDE are game-programmable, so a title can open a window
    * taller than the nominal 240 NTSC lines (yarc programs VDB=25/VDE=507 =
    * 241 lines), and TOMGetVideoModeHeight() accepts anything up to 256.
    * Advertising 240 for NTSC let the core submit frames taller than the
    * declared maximum, which some video drivers clip or drop.  The nominal
    * size is carried by base_height above; this is the allocation bound. */
   info->geometry.max_height   = 256 * shadowHiresN;
   /* Aspect ratio stays 4/3: Nx changes pixel count, not picture shape. */
   info->geometry.aspect_ratio = 4.0 / 3.0;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   InputDevType type = INPUTDEV_PAD;

   if (port > 1)
      return;

   switch (device)
   {
      case RETRO_DEVICE_JAG_MOUSE_ST:
      case RETRO_DEVICE_MOUSE:  /* a plain mouse means the ST wiring */
         type = INPUTDEV_MOUSE_ST;
         break;
      case RETRO_DEVICE_JAG_MOUSE_AMIGA:
         type = INPUTDEV_MOUSE_AMIGA_ON_ST;
         break;
      case RETRO_DEVICE_JAG_MOUSE_AMIGA_AD:
         type = INPUTDEV_MOUSE_AMIGA_ADAPTER;
         break;
      case RETRO_DEVICE_JAG_ROTARY:
         type = INPUTDEV_ROTARY;
         break;
      case RETRO_DEVICE_JAG_ANALOG:
         type = INPUTDEV_ANALOG;
         break;
      case RETRO_DEVICE_JAG_DRIVING:
         type = INPUTDEV_DRIVING;
         break;
      /* A plain RETRO_DEVICE_ANALOG falls through to the pad on purpose
       * -- see the subclass definitions. */
      case RETRO_DEVICE_JAG_LIGHTGUN:
         type = INPUTDEV_LIGHTGUN;
         break;
      default:
         type = INPUTDEV_PAD;
         break;
   }

   /* A frontend that sets JOYPAD/NONE is releasing its claim, so the core
    * option takes over again -- IMMEDIATELY, not "on the next
    * check_variables()".  Nothing schedules one: check_variables() runs
    * from retro_load_game() and from retro_run() only behind
    * GET_VARIABLE_UPDATE.  RetroArch issues a per-port controller init
    * right after load, so forcing PAD here used to detach the mouse the
    * core option had just attached, for the whole session -- which is why
    * the feature was never seen working in a frontend. */
   port_device_frontend[port] = type;
   port_device_forced[port]   = (type != INPUTDEV_PAD);

   if (type == INPUTDEV_PAD)
      type = (port == 1) ? p2_device_from_option() : p1_device_from_option();

   apply_port_device((int)port, type);
   update_option_visibility();
}

size_t retro_serialize_size(void)
{
   return STATE_SIZE;
}

bool retro_serialize(void *data, size_t size)
{
   uint8_t *buf, *start;
   size_t written;
   uint32_t magic, version, flags, reserved;
   extern uint8_t jerry_ram_8[];
   extern bool lowerField;

   if (!data || size < STATE_SIZE)
      return false;

   start = (uint8_t *)data;
   buf   = start;

   /* Header */
   magic    = STATE_MAGIC;
   version  = STATE_VERSION;
   flags    = 0;
   reserved = 0;
   STATE_SAVE_VAR(buf, magic);
   STATE_SAVE_VAR(buf, version);
   STATE_SAVE_VAR(buf, flags);
   STATE_SAVE_VAR(buf, reserved);

   /* Large memory blocks */
   STATE_SAVE_BUF(buf, jaguarMainRAM, 0x200000);  /* 2 MB main RAM */
   STATE_SAVE_BUF(buf, tomRam8, 0x4000);           /* 16 KB TOM registers */

   STATE_SAVE_BUF(buf, jerry_ram_8, 0x10000);      /* 64 KB JERRY registers */

   /* Jaguar misc state */
   STATE_SAVE_VAR(buf, lowerField);

   /* Module state */
   buf += M68KStateSave(buf);
   buf += GPUStateSave(buf);
   buf += DSPStateSave(buf);
   buf += BlitterStateSave(buf);
   buf += EventStateSave(buf);
   buf += EepromStateSave(buf);
   buf += JERRYStateSave(buf);
   buf += TOMStateSave(buf);
   buf += CDROMStateSave(buf);
   buf += JoystickStateSave(buf);
   buf += MTStateSave(buf);
   buf += NVMBiosStateSave(buf);
   buf += DACStateSave(buf);
   buf += UARTStateSave(buf);

   /* v7: bus-arbiter accumulators (68K DRAM self-cost carry, refresh
    * carry, OP occupancy accumulator, 68K pending-stall deduction). */
   STATE_SAVE_VAR(buf, busArbiter.m68k_sysclk_carry);
   STATE_SAVE_VAR(buf, busArbiter.refresh_clk_carry);
   STATE_SAVE_VAR(buf, busArbiter.op_clk_accum);
   STATE_SAVE_VAR(buf, busArbiter.m68k_pending_stall);
   {
      uint32_t blitterBusy = BlitterTimingGetBusy();
      STATE_SAVE_VAR(buf, blitterBusy);
   }

   /* v8: Jaguar GameDrive chunk (bank pages + SPI engine; all-zero for
    * non-GD content). */
   buf += JGDStateSave(buf);

   /* v11: hi-res shadow-surface epoch (issue #400).  Its wrap clears
    * every cached supersampled block, which is visible in the presented
    * frame, so the wrap phase has to survive a rollback. */
   {
      uint32_t hiresEpoch = ShadowHiresGetEpoch();
      STATE_SAVE_VAR(buf, hiresEpoch);
   }

   /* v12: input-device chunk (src/jerry/inputdev.c).  The quadrature
    * accumulator and phase are machine-visible -- the phase IS what the
    * game reads at $F14000 -- so a state restored without them replays
    * different motion. */
   buf += InputDevStateSave(buf);

   written = (size_t)(buf - start);
   if (written > STATE_SIZE)
      return false;

   /* One-shot headroom report.  The trailing chunks in this function grow
    * every release and the only backstop is the hard fail above, so make
    * the margin visible in a log rather than assumed. */
   {
      if (!headroom_logged)
      {
         headroom_logged = true;
         LOG_INF("[state] v%u payload %lu / %lu bytes (%lu free)\n",
                 (unsigned)STATE_VERSION, (unsigned long)written,
                 (unsigned long)STATE_SIZE,
                 (unsigned long)(STATE_SIZE - written));
      }
   }

   /* Zero-fill remaining bytes for deterministic save states */
   if (written < STATE_SIZE)
      memset(buf, 0, STATE_SIZE - written);

   return true;
}

bool retro_unserialize(const void *data, size_t size)
{
   const uint8_t *buf;
   uint32_t magic, version, flags, reserved;
   extern uint8_t jerry_ram_8[];
   extern bool lowerField;

   /* Floor at the smallest layout any accepted version can occupy; the
    * exact floor for the declared version is enforced below, once the
    * header has been read.  A flat `size < STATE_SIZE` here would
    * reject every state written before the v9 size increase. */
   if (!data || size < STATE_SIZE_V8)
      return false;

   buf = (const uint8_t *)data;

   /* Validate header */
   STATE_LOAD_VAR(buf, magic);
   STATE_LOAD_VAR(buf, version);
   STATE_LOAD_VAR(buf, flags);
   STATE_LOAD_VAR(buf, reserved);

   /* Accept older layouts down to STATE_MIN_VERSION so a core update does
    * not invalidate the user's existing states; per-module loaders skip
    * fields the older version did not carry.  We always WRITE
    * STATE_VERSION, and states newer than we understand are refused. */
   if (magic != STATE_MAGIC
       || version < STATE_MIN_VERSION || version > STATE_VERSION)
      return false;

   if (version >= STATE_VERSION_DAC_I2S_RING && size < STATE_SIZE)
      return false;

   /* Large memory blocks */
   STATE_LOAD_BUF(buf, jaguarMainRAM, 0x200000);
   STATE_LOAD_BUF(buf, tomRam8, 0x4000);

   STATE_LOAD_BUF(buf, jerry_ram_8, 0x10000);

   /* Jaguar misc state */
   STATE_LOAD_VAR(buf, lowerField);

   /* Module state */
   buf += M68KStateLoad(buf);
   buf += GPUStateLoad(buf);
   buf += DSPStateLoad(buf);
   buf += BlitterStateLoad(buf);
   buf += EventStateLoad(buf);
   buf += EepromStateLoad(buf);
   buf += JERRYStateLoad(buf);
   buf += TOMStateLoad(buf);
   buf += CDROMStateLoad(buf, version);
   buf += JoystickStateLoad(buf);
   buf += MTStateLoad(buf, version);
   if (version >= STATE_VERSION_MEMTRACK_OVERRIDE)
      buf += NVMBiosStateLoad(buf);
   else
      NVMBiosReset();
   buf += DACStateLoad(buf, version);
   if (version >= STATE_VERSION_JERRY_UART)
      buf += UARTStateLoad(buf, version);

   if (version >= STATE_VERSION_BUS_ARBITER)
   {
      STATE_LOAD_VAR(buf, busArbiter.m68k_sysclk_carry);
      STATE_LOAD_VAR(buf, busArbiter.refresh_clk_carry);
      STATE_LOAD_VAR(buf, busArbiter.op_clk_accum);
      STATE_LOAD_VAR(buf, busArbiter.m68k_pending_stall);

      if (version >= STATE_VERSION_BLITTER_TIMING)
      {
         uint32_t blitterBusy;
         STATE_LOAD_VAR(buf, blitterBusy);
         BlitterTimingSetBusy(blitterBusy);
      }
      else
         BlitterTimingSetBusy(0);
   }
   else
   {
      busArbiter.m68k_sysclk_carry = 0;
      busArbiter.refresh_clk_carry = 0;
      busArbiter.op_clk_accum = 0;
      busArbiter.m68k_pending_stall = 0;
      BlitterTimingSetBusy(0);
   }

   if (version >= STATE_VERSION_JAGGD)
      buf += JGDStateLoad(buf);
   else
      /* Pre-v8 states carry no GameDrive chunk: reset mapping (identity
       * pages, write protect, idle SPI) — the game re-installs. */
      JGDReset();

   /* v11: hi-res shadow-surface epoch (issue #400).  Applied here rather
    * than after ShadowHiresInvalidate() below only for locality — the
    * invalidate drops cache entries and deliberately leaves the epoch
    * alone, so the order of the two does not matter.  Pre-v11 states
    * carry no epoch: start from a fixed phase so an old state at least
    * replays the same way every time it is loaded. */
   if (version >= STATE_VERSION_HIRES_EPOCH)
   {
      uint32_t hiresEpoch;
      STATE_LOAD_VAR(buf, hiresEpoch);
      ShadowHiresSetEpoch(hiresEpoch);
   }
   else
      ShadowHiresSetEpoch(0);

   /* v12: input-device chunk.  Older states load with the encoders reset
    * and no button held, which is exactly what a pre-v12 core was: the
    * device type itself is option-derived and never serialized, so the
    * user's currently selected mouse stays attached either way. */
   if (version >= STATE_VERSION_INPUT_DEVICES)
      buf += InputDevStateLoad(buf);
   else
      InputDevReset();

   /* tomRam8 was restored raw above; recompute the DRAM/refresh timing
    * that bus_arbiter derives from MEMCON1/MEMCON2 so it matches the
    * loaded state (dram_row_miss/rom_clocks/dram_refresh_clks from
    * MEMCON1, refrate from MEMCON2 — none of those derived fields are
    * themselves serialized, see bus_arbiter.h). */
   bus_arbiter_update_memcon(TOMGetMEMCON1());
   bus_arbiter_update_memcon2(TOMGetMEMCON2());

   JaguarApplyHLEBIOSState();

   /* The true-color shadow framebuffer is a derived cache over RAM
    * contents that were just replaced wholesale; drop every entry
    * (never serialized -- see shadowfb.h).  Ditto the hi-res shadow
    * surface: invalidation cost is per stock word, independent of N. */
   ShadowFBInvalidate();
   ShadowHiresInvalidate();

   /* The blit memo is likewise a derived cache over RAM that was just
    * replaced wholesale (never serialized). */
   BlitMemoFlush();

   /* The 68K->RISC-RAM 16-bit-port latch is deliberately not serialized
    * (see jaguar.c): dropping an unpaired low word is what hardware does.
    * But it must actually be dropped here, or a pending pre-load word
    * could commit against a partner write issued after the load. */
   M68KResetRiscWordLatch();

   return true;
}

/* Cheat codes — the parser and list management live in src/core/cheat.c so
 * they can be unit-tested without the rest of the emulator. Here we just
 * bind them to the Jaguar memory bus and re-apply every frame so games
 * that continuously overwrite the patched location are held to the
 * cheat value. */
static cheat_list_t cheat_list;

static void cheat_write_jaguar(uint32_t addr, uint32_t value,
                               uint8_t size, void *user)
{
   (void)user;
   switch (size)
   {
      case 1: JaguarWriteByte(addr, (uint8_t)value,  UNKNOWN); break;
      case 2: JaguarWriteWord(addr, (uint16_t)value, UNKNOWN); break;
      case 4: JaguarWriteLong(addr, value,           UNKNOWN); break;
      default:
         LOG_WRN("[Virtual Jaguar] cheat: unsupported write size %u at 0x%06X\n",
               (unsigned)size, (unsigned)(addr & 0xFFFFFFU));
         break;
   }
}

void retro_cheat_reset(void)
{
   cheat_list_reset(&cheat_list);
}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   cheat_list_set(&cheat_list, index, enabled, code);
}

static void cheat_apply_all(void)
{
   cheat_list_apply(&cheat_list, cheat_write_jaguar, NULL);
}

/* Case-insensitive extension test on a path. */
static bool has_extension(const char *path, const char *ext)
{
   const char *dot;
   if (!path || !ext)
      return false;
   dot = strrchr(path, '.');
   if (!dot)
      return false;
   return strcasecmp(dot + 1, ext) == 0;
}

/* CRC32s of known-good 256 KB CD BIOS dumps.  The developer BIOS has
 * $FFFFFFFF at the $404 run-address slot, so it can only be recognized
 * by checksum -- a header check alone always rejects it. */
#define CD_BIOS_CRC_RETAIL 0x687068D5u
#define CD_BIOS_CRC_DEV    0x55A0669Cu

/* Try to load a 256 KB CD BIOS image from the given path.
 * Returns true on success and sets cd_bios_loaded_externally.
 * A file that exists but fails validation bumps *rejected so the
 * caller's final warning can distinguish "no file" from "bad file". */
static bool try_load_cd_bios_file(const char *path, int *rejected)
{
   RFILE   *f;
   int64_t  size;
   uint32_t run_addr;
   uint32_t crc;

   f = rfopen(path, "rb");
   if (!f)
      return false;

   rfseek(f, 0, SEEK_END);
   size = rftell(f);
   rfseek(f, 0, SEEK_SET);

   if (size != 0x40000)
   {
      LOG_DBG("[CD-BIOS]   wrong size (%lld, need 262144): %s\n",
              (long long)size, path);
      rfclose(f);
      (*rejected)++;
      return false;
   }

   if (rfread(external_cd_bios, 1, 0x40000, f) != 0x40000)
   {
      LOG_DBG("[CD-BIOS]   short read (need 262144): %s\n", path);
      rfclose(f);
      (*rejected)++;
      return false;
   }
   rfclose(f);

   /* Known dumps are accepted by checksum, no header check needed. */
   crc = (uint32_t)crc32_calcCheckSum(external_cd_bios, 0x40000);
   if (crc == CD_BIOS_CRC_RETAIL)
   {
      LOG_INF("[CD-BIOS] using external %s (recognized retail CD BIOS, crc32=%08X)\n",
              path, (unsigned)crc);
      cd_bios_loaded_externally = true;
      return true;
   }
   if (crc == CD_BIOS_CRC_DEV)
   {
      LOG_INF("[CD-BIOS] using external %s (recognized developer CD BIOS, crc32=%08X)\n",
              path, (unsigned)crc);
      cd_bios_loaded_externally = true;
      return true;
   }

   /* Unknown checksum: fall back to the header sanity check so genuine
    * revisions/regions we don't have CRCs for still load. */
   run_addr = ((uint32_t)external_cd_bios[0x404] << 24)
            | ((uint32_t)external_cd_bios[0x405] << 16)
            | ((uint32_t)external_cd_bios[0x406] <<  8)
            |  (uint32_t)external_cd_bios[0x407];

   if (run_addr < 0x800000 || run_addr > 0x840000)
   {
      LOG_DBG("[CD-BIOS]   bad run addr $%08X (crc32=%08X): %s\n",
              (unsigned)run_addr, (unsigned)crc, path);
      (*rejected)++;
      return false;
   }

   LOG_WRN("[CD-BIOS] %s is an unrecognized CD BIOS revision "
           "(crc32=%08X, plausible run=$%06X) -- accepting it, but if boot "
           "black-screens right after this line, this file is the prime "
           "suspect: verify it is a genuine Jaguar CD BIOS dump\n",
           path, (unsigned)crc, (unsigned)run_addr);
   LOG_INF("[CD-BIOS] using external %s (run=$%06X)\n",
           path, (unsigned)run_addr);
   cd_bios_loaded_externally = true;
   return true;
}

/* Search common CD BIOS filenames in the system directory (and a handful
 * of well-known sub-directories used by Provenance/RetroArch front-ends). */
static bool load_external_cd_bios(void)
{
   /* Filenames conventionally used for each 'CD BIOS Type', plus generic
    * names that could hold either image.  The name drives the SEARCH ORDER
    * only; the contents are validated by try_load_cd_bios_file(), which
    * checksums the image and logs which revision it actually is.  So a
    * mislabelled file is still loaded (its name only decided when it was
    * tried), but the log names the real revision rather than the label.
    *
    * The selected type's names are searched FIRST.  The previous code used
    * one flat list with the retail names ahead of the developer ones, so a
    * user with both files installed always got retail no matter what the
    * option said. */
   static const char *retail_names[] = {
      "[BIOS] Atari Jaguar CD (World).j64",
      "[BIOS] Atari Jaguar CD (World).rom",
      "[BIOS] Atari Jaguar CD (World).bin",
      NULL
   };
   static const char *dev_names[] = {
      "[BIOS] Atari Jaguar Developer CD (World).j64",
      "[BIOS] Atari Jaguar Developer CD (World).rom",
      "[BIOS] Atari Jaguar Developer CD (World).bin",
      NULL
   };
   static const char *generic_names[] = {
      "jaguarcd_bios.bin",
      "jagcd_bios.bin",
      "jaguarcd.bin",
      "jagcd.bin",
      "Jaguar CD BIOS.rom",
      "Jaguar CD BIOS.bin",
      NULL
   };
   const char **name_groups[3];
   static const char *sub_dirs[] = {
      "",
      "Atari - Jaguar",
      "Atari - Jaguar CD",
      "jaguar",
      "jaguarcd",
      NULL
   };
   const char *system_dir = NULL;
   int s, i, g;
   int rejected = 0;

   if (!environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir)
       || !system_dir)
   {
      LOG_WRN("[CD-BIOS] No system directory available\n");
      return false;
   }

   /* Selected type first, generic names second, the other type last (a
    * lone file of the "wrong" type still beats falling back to embedded —
    * the user put it there on purpose). */
   if (vjs.cdBiosType == CDBIOS_DEV)
   {
      name_groups[0] = dev_names;
      name_groups[2] = retail_names;
   }
   else
   {
      name_groups[0] = retail_names;
      name_groups[2] = dev_names;
   }
   name_groups[1] = generic_names;

   LOG_INF("[CD-BIOS] Searching for CD BIOS in: %s (preferring %s)\n",
           system_dir, (vjs.cdBiosType == CDBIOS_DEV) ? "developer" : "retail");

   for (g = 0; g < 3; g++)
   {
      for (s = 0; sub_dirs[s]; s++)
      {
         for (i = 0; name_groups[g][i]; i++)
         {
            char path[4096];
            if (sub_dirs[s][0])
               snprintf(path, sizeof(path), "%s/%s/%s",
                        system_dir, sub_dirs[s], name_groups[g][i]);
            else
               snprintf(path, sizeof(path), "%s/%s",
                        system_dir, name_groups[g][i]);

            if (try_load_cd_bios_file(path, &rejected))
               return true;
         }
      }
   }

   if (rejected > 0)
      LOG_WRN("[CD-BIOS] no usable CD BIOS in %s: %d candidate file(s) "
              "rejected (size or header); using embedded BIOS\n",
              system_dir, rejected);
   else
      LOG_WRN("[CD-BIOS] CD BIOS not found in %s\n", system_dir);
   return false;
}

/* Stage the CD BIOS for the real-BIOS boot path: prefer an external ROM
 * file from the system directory (users may carry a different BIOS
 * revision), else fall back to an embedded CD BIOS so real-BIOS boot
 * works with zero files.  Which embedded image is used follows the
 * 'CD BIOS Type' option: retail (default) or the developer BIOS, which
 * skips some of the retail BIOS's disc checks.
 *
 * An external file always wins over both — it is the user explicitly
 * supplying a revision, so the type selection does not override it. */
static void stage_cd_bios(void)
{
   if (load_external_cd_bios())
      return;

   if (vjs.cdBiosType == CDBIOS_DEV)
   {
      memcpy(external_cd_bios, jaguarDevCDBootROM, 0x40000);
      cd_bios_loaded_externally = true;
      LOG_INF("[CD-BIOS] using embedded developer CD BIOS\n");
      return;
   }

   memcpy(external_cd_bios, jaguarCDBootROM, 0x40000);
   cd_bios_loaded_externally = true;
   LOG_INF("[CD-BIOS] using embedded retail CD BIOS\n");
}

static void cart_bios_type_from_path(const char *path)
{
   const char *base;
   const char *slash;
   const char *bslash;
   size_t n;

   if (!path || !path[0])
      return;
   slash = strrchr(path, '/');
   bslash = strrchr(path, '\\');
   if (bslash && (!slash || bslash > slash))
      slash = bslash;
   base = slash ? slash + 1 : path;
   n = strlen(base);
   if (n == 6 && (base[0] == 'v' || base[0] == 'V') &&
         (base[1] == 'j' || base[1] == 'J'))
      vjs.biosType = BT_K_SERIES;
   else if (n >= 7 && base[n - 6] == '_' &&
         (base[n - 5] == 'M' || base[n - 5] == 'm'))
      vjs.biosType = BT_M_SERIES;
   else if (n >= 7 && base[n - 6] == '_' &&
         (base[n - 5] == 'K' || base[n - 5] == 'k'))
      vjs.biosType = BT_K_SERIES;
}

static void apply_cart_bios_autodetect(const struct retro_game_info *info)
{
   struct retro_variable var;
   const char *def;

   if (!info)
      return;
   /* info->data is guaranteed for cart content: retro_get_system_info
    * sets need_fullpath=false, and CD content -- the only path-loaded
    * class (CONTENT_INFO_OVERRIDE) -- takes the is_cd_content branch and
    * never reaches here (test_memory_map asserts both).  The NULL check
    * is defensive against non-compliant frontends only.
    *
    * The BIOS enable is forced even over an explicit user 'HLE' setting:
    * a GPU-only/jagcrypt cart has no 68K program at $802000 for HLE to
    * start, so honouring 'HLE' would be a guaranteed black screen.  The
    * K/M *type* hint below, by contrast, does defer to user/titledb
    * values. */
   if (info->data && info->size > 0 &&
         JaguarCartNeedsBIOS((const uint8_t *)info->data,
            (uint32_t)info->size))
   {
      vjs.useJaguarBIOS = true;
      /* Filename K/M hint only when bios_type is still at its registered
       * default (or unset). User-set and titledb values already applied
       * via check_variables() / get_variable_pertitle() win. */
      var.key = "virtualjaguar_bios_type";
      var.value = NULL;
      def = core_option_default(var.key);
      if (!get_variable_pertitle(&var) || !var.value ||
            !def || strcmp(var.value, def) == 0)
         cart_bios_type_from_path(info->path);
      LOG_INF("[BOOT] GPU-only cart -- real boot ROM enabled (jagcrypt)\n");
   }
}

/* Try to load a 128 KB cart boot ROM image from the given path directly
 * into the boot ROM window at $E00000.  Returns true on success.  Any
 * checksum is accepted -- unlike the CD BIOS loader, a custom cart boot
 * ROM has no header sanity check to fall back on, and "load whatever the
 * user pointed us at" is the whole point of the 'Custom' option.  The
 * identification is purely informational (logged), never a gate. */
static bool try_load_cart_boot_rom_file(const char *path)
{
   RFILE   *f;
   int64_t  size;
   uint32_t crc;
   const char *name;

   f = rfopen(path, "rb");
   if (!f)
      return false;

   rfseek(f, 0, SEEK_END);
   size = rftell(f);
   rfseek(f, 0, SEEK_SET);

   if (size != 0x20000)
   {
      LOG_DBG("[BOOT]   wrong size (%lld, need 131072): %s\n",
              (long long)size, path);
      rfclose(f);
      return false;
   }

   if (rfread(jagMemSpace + 0xE00000, 1, 0x20000, f) != 0x20000)
   {
      LOG_DBG("[BOOT]   short read (need 131072): %s\n", path);
      rfclose(f);
      return false;
   }
   rfclose(f);

   name = BIOSDBIdentify(jagMemSpace + 0xE00000, 0x20000, &crc);
   if (name == BIOSDB_UNKNOWN_NAME)
      LOG_WRN("[BOOT] %s is an unrecognized cart boot ROM image "
              "(crc %08x) -- loading anyway, custom images are the point\n",
              path, (unsigned)crc);
   LOG_INF("[BOOT] external cart boot ROM %s: %s (crc %08x)\n",
           path, name, (unsigned)crc);
   return true;
}

/* Search common cart boot ROM filenames in the system directory (and a
 * handful of well-known sub-directories), for 'Cart BIOS Type' = Custom.
 * Mirrors load_external_cd_bios()'s sub-directory list minus the
 * CD-specific entries.  Filename priority is the outer loop -- a
 * jagboot.rom several sub-directories down still beats a boot0.rom in the
 * system directory root, because the filename is the user's explicit
 * signal of which image they mean for us to use. */
static bool load_external_cart_boot_rom(void)
{
   static const char *names[] = {
      "jagboot.rom",
      "boot.rom",
      "boot0.rom",
      "[BIOS] Atari Jaguar (World).j64",
      "[BIOS] Atari Jaguar Stubulator '94 (World).j64",
      "[BIOS] Atari Jaguar Stubulator '93 (World).j64",
      NULL
   };
   static const char *sub_dirs[] = {
      "",
      "Atari - Jaguar",
      "jaguar",
      NULL
   };
   const char *system_dir = NULL;
   int i, s;

   if (!environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir)
       || !system_dir)
   {
      LOG_WRN("[BOOT] No system directory available for custom cart boot ROM search\n");
      return false;
   }

   for (i = 0; names[i]; i++)
   {
      for (s = 0; sub_dirs[s]; s++)
      {
         char path[4096];
         if (sub_dirs[s][0])
            snprintf(path, sizeof(path), "%s/%s/%s",
                     system_dir, sub_dirs[s], names[i]);
         else
            snprintf(path, sizeof(path), "%s/%s", system_dir, names[i]);

         if (try_load_cart_boot_rom_file(path))
            return true;
      }
   }

   return false;
}

static void stage_cart_boot_rom(void)
{
   const uint8_t *src;
   const char *system_dir;
   RFILE *f;
   char path[4096];

   if (vjs.biosType == BT_CUSTOM)
   {
      if (load_external_cart_boot_rom())
         return;

      LOG_WRN("[BOOT] Custom cart boot ROM selected but no usable file "
              "found -- falling back to embedded Series K\n");
      memcpy(jagMemSpace + 0xE00000, jaguarBootROM, 0x20000);
      LOG_INF("[BOOT] cart boot ROM: Series K (fallback)\n");
      return;
   }

   src = (vjs.biosType == BT_M_SERIES) ? jaguarBootROM_M : jaguarBootROM;
   system_dir = NULL;
   f = NULL;

   if (vjs.biosType == BT_M_SERIES &&
         environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) &&
         system_dir)
   {
      snprintf(path, sizeof(path), "%s/jagboot_m.rom", system_dir);
      f = rfopen(path, "rb");
      if (f)
      {
         if (rfread(jagMemSpace + 0xE00000, 1, 0x20000, f) == 0x20000)
         {
            rfclose(f);
            LOG_INF("[BOOT] using external Model-M boot ROM %s\n", path);
            return;
         }
         rfclose(f);
      }
   }

   memcpy(jagMemSpace + 0xE00000, src, 0x20000);
   LOG_INF("[BOOT] cart boot ROM: %s\n",
         (vjs.biosType == BT_M_SERIES) ? "Model M" : "Series K");
}

/* Fill the entire framebuffer allocation with opaque black.
 *
 * TOM only writes the rows of the presented frame that fall inside its
 * visible window, and the border-fill path in TOMExecHalfline writes
 * `tomWidth` pixels -- which is 0 until the game programs a TOM video
 * register ($F00028-$F0004F).  Until then, any presented row above VDB gets
 * no pixels written at all, so whatever this buffer holds is what the
 * frontend displays.  It has to be a defined value, and opaque black is the
 * correct one: it is both what a display shows with no signal and what the
 * border colour resolves to in that window (TOMReset zeroes BORD1/BORD2, and
 * any write that makes them non-zero also makes tomWidth non-zero, so the
 * border path is live by then).
 *
 * Fill the entire allocation rather than videoWidth * videoHeight: the
 * geometry can grow to a wider stride later (320x240 -> 326x240), and rows
 * re-laid-out at the larger pitch reach past the smaller region.  Covering
 * all of it keeps every presentable pixel defined and the X byte consistent
 * no matter which geometry is in force.
 *
 * Both retro_load_game and retro_reset go through here: a reset re-enters
 * exactly the same window (TOMReset puts tomWidth back to 0), so the two
 * must agree on the value. */
static void video_buffer_blank(void)
{
   int i;

   if (!videoBuffer)
      return;

   for (i = 0; i < video_buffer_alloc_pixels; ++i)
      videoBuffer[i] = 0xFF000000;
}

bool retro_load_game(const struct retro_game_info *info)
{
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   bool is_cd_content;

   struct retro_input_descriptor desc[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "A" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Numpad 0" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "C" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "Numpad 1" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "Numpad 3" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "Numpad 5" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Numpad 2" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "Numpad 4" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "Numpad 6" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Pause" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Option" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X,  "Left Analog X" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y,  "Left Analog Y" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "A" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Numpad 0" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "C" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "Numpad 1" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "Numpad 3" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "Numpad 5" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Numpad 2" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "Numpad 4" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "Numpad 6" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Pause" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Option" },
      { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X,  "Left Analog X" },
      { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y,  "Left Analog Y" },
      { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

      { 0 },
   };

   if (!info)
      return false;

   is_cd_content = info->path && (has_extension(info->path, "cue")
                                  || has_extension(info->path, "cdi")
                                  || has_extension(info->path, "chd")
                                  || has_extension(info->path, "iso"));

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

   /* Report that save states are deterministic (no quirks).
    * This enables run-ahead and netplay in RetroArch. */
   {
      uint64_t serialization_quirks = 0;
      environ_cb(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS, &serialization_quirks);
   }

   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      //fprintf(stderr, "Pixel format XRGB8888 not supported by platform, cannot use.\n");
      return false;
   }

   /* Feed the per-title DB the loaded content so option reads below (and in
    * check_variables()) can match by CRC (issue #368). On a frontend that
    * honours the env-65 content-info override set up in retro_set_environment,
    * CD content (.cue/.cdi/.chd) arrives here with info->data == NULL -- it is
    * path-loaded, not read into memory -- so the info->data guard below
    * already excludes it. On a frontend without env 65, CD content instead
    * arrives with info->data holding the whole disc image; the explicit
    * !is_cd_content guard covers that fallback so the CRC is never computed
    * over disc bytes either way. v1 only covers cartridge CRCs, and hashing
    * a disc image would find nothing this table knows about while risking a
    * collision handing a CD title some cartridge's per-title overrides. */
   if (info->data && !is_cd_content)
   {
      TitleDBSetContent((const uint8_t *)info->data, info->size);
      /* A patched ROM (RetroArch soft patching, or a pre-patched dump)
       * hashes differently from its retail base, so it matches no row and
       * silently loses that title's enhancement defaults.  Say so (#409). */
      if (!TitleDBTitleName())
         LOG_INF("[titledb] no per-title entry for CRC32 $%08X -- patched or "
                 "unlisted content; enhancement defaults not applied (see "
                 "docs/rom-patches.md)\n", (unsigned)TitleDBContentCRC());
   }
   else
      TitleDBSetContent(NULL, 0);

   /* Texture dump (#369): dumps land under the system directory, keyed
    * by the content CRC the DB just latched.  The path is set here once
    * so a mid-session enable via check_variables() has somewhere to
    * write without needing environ_cb of its own. */
   {
      const char *texdump_sys_dir = NULL;
      if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &texdump_sys_dir)
          && texdump_sys_dir)
         TexDumpSetBasePath(texdump_sys_dir);
      else
         TexDumpSetBasePath(NULL);
      /* Texture replacement (#369 deliverable 2): same base, packs read
       * from <system dir>/vj_texpacks/<CRC32>/.  ContentLoaded probes
       * availability (option visibility) and performs the one-off pack
       * load if the option was already on; a loaded pack then forces
       * the shadow framebuffer it presents through. */
      TexReplaceSetBasePath(texdump_sys_dir);
      TexReplaceContentLoaded();
      if (texReplaceEnabled)
         ShadowFBSetEnabled(1);
   }

   /* Raw gate read (never through get_variable_pertitle()) so the hires
    * read just below -- which runs before check_variables() -- is already
    * gated correctly even on a frontend that hasn't called
    * check_variables() yet. */
   {
      struct retro_variable gate_var;
      gate_var.key = "virtualjaguar_pertitle_defaults";
      gate_var.value = NULL;
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &gate_var) && gate_var.value)
         pertitle_enabled = (strcmp(gate_var.value, "disabled") != 0);
      else
         pertitle_enabled = true;
   }

   /* Enhancement-hook gate (issue #370), latched HERE and nowhere else.
    * Read raw for the same reason the gate above is: otherwise a DB row
    * could carry {"virtualjaguar_enhancement_hooks","enabled"} in its own
    * pairs[] and defeat "off by default" for its own title.  Defaults to
    * disabled, including on a frontend that does not answer the query, so
    * headless callers and tests get stock behaviour unless they ask.
    * check_variables() only compares-and-logs; it must never re-latch, or a
    * mid-session toggle would change what a later read sees. */
   {
      struct retro_variable hook_var;
      hook_var.key = "virtualjaguar_enhancement_hooks";
      hook_var.value = NULL;
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &hook_var) && hook_var.value)
         TitleHookSetEnabled(strcmp(hook_var.value, "enabled") == 0);
      else
         TitleHookSetEnabled(0);
      hook_restart_notice_logged = 0;
   }

   /* Internal resolution (hi-res Stage 1, see shadowfb.h): read ONCE at
    * content load -- SET_GEOMETRY cannot grow past the advertised maximum,
    * so N is fixed for the session and mid-game option changes only apply
    * on restart (design section 7.1). */
   {
      struct retro_variable hires_var;
      int hires_n = 1;
      hires_var.key = "virtualjaguar_internal_resolution";
      hires_var.value = NULL;
      if (get_variable_pertitle(&hires_var)
          && hires_var.value && strcmp(hires_var.value, "2x") == 0)
         hires_n = 2;
      ShadowHiresSetN(hires_n);
      hires_restart_notice_logged = 0;
   }

   videoWidth           = 320;
   videoHeight          = 240;
   video_buffer_alloc_pixels =
      VIDEO_BUFFER_PIXELS * shadowHiresN * shadowHiresN;
   videoBuffer  = (uint32_t *)calloc(sizeof(uint32_t), video_buffer_alloc_pixels);
   if (!videoBuffer && shadowHiresN > 1)
   {
      /* Nx framebuffer allocation failed: log and run at 1x (scope
       * fence: allocation failure -> log + run at 1x). */
      LOG_WRN("[HIRES] %dx framebuffer allocation failed; running at 1x\n",
              shadowHiresN);
      ShadowHiresShutdown();
      video_buffer_alloc_pixels = VIDEO_BUFFER_PIXELS;
      videoBuffer = (uint32_t *)calloc(sizeof(uint32_t), video_buffer_alloc_pixels);
   }
   sampleBuffer = (uint16_t *)malloc(BUFMAX * sizeof(uint16_t));

   if (!videoBuffer || !sampleBuffer)
   {
      free(videoBuffer);
      free(sampleBuffer);
      videoBuffer = NULL;
      sampleBuffer = NULL;
      TitleDBSetContent(NULL, 0);
      return false;
   }
   memset(sampleBuffer, 0, BUFMAX * sizeof(uint16_t));

   game_width           = 320 * shadowHiresN;
   game_height          = 240 * shadowHiresN;

   // Emulate BIOS
   vjs.hardwareTypeNTSC = true;
   vjs.useJaguarBIOS    = false;
   vjs.biosType         = BT_K_SERIES;
   vjs.cdBiosType       = CDBIOS_RETAIL;
   vjs.cdBootMode       = CDBOOT_HLE;
   vjs.cdReadSpeed      = CDSPEED_2X;

   check_variables();

   /* Always identify the exact build in the frontend log: version, short
    * git rev (+"-dirty" for uncommitted trees), and timestamp for DEBUG
    * builds.  Answers "which binary is this?" in every bug report and
    * stops test runs against a stale core from going unnoticed. */
   LOG_INF("[Virtual Jaguar] build: %s\n", CORE_VERSION);

   /* Register EEPROM dirty callback so the save buffer stays in sync */
   eeprom_dirty_cb = eeprom_pack_save_buf;
   mt_dirty_cb     = mt_pack_save_buf;

   /* Detect CD content (CUE/CDI/CHD/ISO) and stage a CD BIOS (external file
    * if present, embedded otherwise) so ResolveBootConfig can pick the
    * right boot strategy. */
   jaguar_cd_mode            = false;
   jaguarMemTrackInserted    = false;
   cd_image_path[0]          = '\0';
   cd_bios_loaded_externally = false;

   if (is_cd_content)
   {
      jaguar_cd_mode = true;
      /* Hardware has the Memory Track cart plugged in alongside the CD
       * unit (user-selectable; some titles behave differently with one
       * present). */
      jaguarMemTrackInserted = opt_memory_track;
      strncpy(cd_image_path, info->path, sizeof(cd_image_path) - 1);
      cd_image_path[sizeof(cd_image_path) - 1] = '\0';

      if (vjs.cdBootMode != CDBOOT_HLE)
         stage_cd_bios();
   }
   else
      apply_cart_bios_autodetect(info);

   /* Resolve boot configuration — single source of truth for which
    * strategy (cart / HLE / real BIOS) we will dispatch to below. */
   ResolveBootConfig(&bootConfig, jaguar_cd_mode, cd_bios_loaded_externally,
                     vjs.cdBootMode, vjs.useJaguarBIOS);
   vjs.useJaguarBIOS = bootConfig.showBootROM;

   /* check_variables() ran above, before bootConfig existed, so the blit
    * memo could not tell cartridge from CD content then.  Re-apply the
    * requested mode now that it can: BlitMemoSetMode() forces CD content
    * back to OFF, which keeps blitMemoMode zero and short-circuits the
    * write hooks instead of charging CD titles for a memo that can never
    * hit. */
   BlitMemoSetMode(blit_memo_requested);

   /* Open the disc image BEFORE JaguarInit() so CDROMInit -> CDIntfInit ->
    * CDIntfIsImageLoaded sees the disc and haveCDGoodness is set correctly. */
   if (jaguar_cd_mode)
   {
      LOG_INF("[CD] Opening disc image: %s\n", cd_image_path);
      if (!CDIntfOpenImage(cd_image_path))
      {
         LOG_ERR("[CD] CDIntfOpenImage failed for: %s\n", cd_image_path);
         free(videoBuffer);
         videoBuffer = NULL;
         free(sampleBuffer);
         sampleBuffer = NULL;
         TitleDBSetContent(NULL, 0);
         return false;
      }
      LOG_INF("[CD] Disc image opened OK\n");
   }

   JaguarInit();                                             // set up hardware
   CrashDetectReset();                                       // zero per-game watchdog state
   stage_cart_boot_rom();

   JaguarSetScreenPitch(videoWidth * shadowHiresN);
   JaguarSetScreenBuffer(videoBuffer);

   /* Seed the framebuffer.  See video_buffer_blank() for why opaque black
    * and why the whole allocation. */
   video_buffer_blank();

   /* Dispatch through the selected boot strategy (cart / HLE / real BIOS).
    * The cart strategy handles the existing JaguarLoadFile + JaguarReset
    * flow; the CD strategies handle their own boot sequencing. */
   if (!bootConfig.strategy || !bootConfig.strategy->boot(info))
   {
      LOG_ERR("[Virtual Jaguar] unsupported or invalid content format\n");
      if (jaguar_cd_mode)
         CDIntfCloseImage();
      JaguarDone();
      free(videoBuffer);
      videoBuffer = NULL;
      free(sampleBuffer);
      sampleBuffer = NULL;
      TitleDBSetContent(NULL, 0);
      return false;
   }

   /* For RAM-loaded executables (.abs/.cof/JagServer), JaguarReset()
    * randomizes RAM and destroys the loaded program.  The cart and CD
    * boot strategies handle their own JaguarReset() ordering internally
    * so the post-boot state is preserved.  We only need to do an extra
    * reset+reload here for the RAM-loaded path. */
   if (!jaguarCartInserted && !jaguar_cd_mode)
   {
      JaguarReset();
      if (!JaguarLoadFile((uint8_t*)info->data, info->size))
      {
         LOG_ERR("[Virtual Jaguar] failed to reload RAM-loaded content\n");
         JaguarDone();
         free(videoBuffer);
         videoBuffer = NULL;
         free(sampleBuffer);
         sampleBuffer = NULL;
         TitleDBSetContent(NULL, 0);
         return false;
      }
   }

   /* Per-title enhancement hooks (issue #370): the single trigger.  Both
    * boot strategies have finished (JaguarLoadFile + JaguarReset for the
    * cart path), so the cart window is populated and nothing later
    * rewrites it.  Gated off by default; refuses on anything but a
    * cartridge, and refuses any hook whose expect[] bytes are not present.
    * Cart ROM is not serialized and JaguarReset() never touches it, so
    * this never needs re-applying -- not after retro_reset(), not after
    * unserialize. */
   TitleHookApplyROM();

   /* Advertise the Jaguar memory map so frontends (RetroArch, etc.) can
    * resolve emulated addresses to host buffers. Required for rcheevos.
    *
    * rcheevos defines one logical system-RAM region for RC_CONSOLE_ATARI_JAGUAR:
    * $000000-$1FFFFF (see rcheevos consoleinfo). RetroAchievements addresses for
    * Jaguar are authored in that space. GPU-style paths mirror 2 MiB within
    * $000000-$7FFFFF in JaguarReadByte, but M68K direct access in this core is
    * only linear $000000-$1FFFFF without those mirrors (m68k_read_memory_*). */
   {
      static struct retro_memory_descriptor descs[1];
      static struct retro_memory_map memmap;

      memset(descs, 0, sizeof(descs));
      descs[0].flags     = RETRO_MEMDESC_SYSTEM_RAM | RETRO_MEMDESC_BIGENDIAN;
      descs[0].ptr       = jaguarMainRAM;
      descs[0].start     = 0x000000;
      descs[0].len       = 0x200000;
      descs[0].addrspace = "RAM";

      memmap.descriptors     = descs;
      memmap.num_descriptors = 1;
      environ_cb(RETRO_ENVIRONMENT_SET_MEMORY_MAPS, &memmap);
   }

   /* The frontend will load .srm data into our save buffer (returned by
    * retro_get_memory_data) after this function returns but before the
    * first retro_run(). We unpack it on the first frame. */
   save_data_needs_unpack = true;

   /* Content type is now known — refresh which options apply to it. */
   content_loaded = true;
   update_option_visibility();

   /* Memory Track NVM BIOS module: on hardware the CD BIOS boot installs
    * it in RAM before the game runs; do the same after the boot strategy
    * has set RAM up. */
   NVMBiosReset();
   if (jaguarMemTrackInserted)
      NVMBiosInstall();

   return true;
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
   (void)game_type;
   (void)info;
   (void)num_info;
   return false;
}

void retro_unload_game(void)
{
   retro_cheat_reset();
   CDIntfCloseImage();
   jaguar_cd_mode    = false;
   jaguarMemTrackInserted = false;
   cd_image_path[0]  = '\0';
   /* Content type is unknown again — restore the full option list. */
   content_loaded    = false;
   update_option_visibility();
   JaguarDone();

#ifdef VJ_TRACE
   /* Next title's frame 1 must be ring/field-CSV frame 1, not a
    * continuation of this session's count (see vjt_frame's decl). */
   vjt_frame = 0;
#endif

   if (videoBuffer)
      free(videoBuffer);
   videoBuffer = NULL;
   if (sampleBuffer)
      free(sampleBuffer);
   sampleBuffer = NULL;

   /* Reset all module state so a subsequent retro_load_game in the same
    * process (iOS cannot dlclose cores) starts clean. */
   videoWidth = 0;
   videoHeight = 0;
   game_width = 0;
   game_height = 0;

   /* Hi-res: N is per-load; drop the shadow surface so the next load
    * re-reads the option from scratch (see shadowfb.h). */
   ShadowHiresShutdown();
   /* Texture dump (#369): dumps are keyed by content CRC, so a title
    * boundary closes the manifest and logs the session summary; the
    * next load's check_variables() re-enables (and re-allocates) if the
    * option is still on. */
   TexDumpShutdown();
   /* Texture replacement (#369): packs are per-CRC too -- free the map
    * and log the session summary; the next load's check_variables() +
    * TexReplaceContentLoaded() reload if the option is still on. */
   TexReplaceShutdown();
   video_buffer_alloc_pixels = VIDEO_BUFFER_PIXELS;
   hires_restart_notice_logged = 0;

   /* The next option read must not see the previous title's CRC/match. */
   TitleDBSetContent(NULL, 0);
   /* Same for the hook gate and any test-installed hook array: the next
    * load re-latches the gate from the option (iOS cannot dlclose cores,
    * so statics must be reset here, not left to process teardown). */
   TitleHookSetEnabled(0);
   TitleDBSetHooksForTest(NULL, 0);
   hook_restart_notice_logged = 0;

   eeprom_dirty_cb = NULL;
   mt_dirty_cb     = NULL;
   save_data_needs_unpack = false;
   memset(eeprom_save_buf, 0, sizeof(eeprom_save_buf));

   memset(jag_retropad, 0, sizeof(jag_retropad));
   memset(jag_numpad, 0, sizeof(jag_numpad));
   numpad_to_kb[0] = 0;
   numpad_to_kb[1] = 0;
   show_input_options = true;
   enable_alt_inputs = false;
   content_loaded = false;
   show_cd_options = true;
   show_cart_bios_option = true;
}

unsigned retro_get_region(void)
{
   return vjs.hardwareTypeNTSC ? RETRO_REGION_NTSC : RETRO_REGION_PAL;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

/* Pack eeprom_ram[] and cdrom_eeprom_ram[] into the save buffer
 * (big-endian byte order).  Called on every EEPROM write via
 * eeprom_dirty_cb so the buffer is always up-to-date for frontends
 * that cache the pointer. */
static void eeprom_pack_save_buf(void)
{
   unsigned i;
   for (i = 0; i < 64; i++)
   {
      eeprom_save_buf[(i * 2) + 0] = eeprom_ram[i] >> 8;
      eeprom_save_buf[(i * 2) + 1] = eeprom_ram[i] & 0xFF;
   }
   /* CD EEPROM follows cart EEPROM in the save buffer */
   for (i = 0; i < 64; i++)
   {
      eeprom_save_buf[EEPROM_SAVE_SIZE + (i * 2) + 0] = cdrom_eeprom_ram[i] >> 8;
      eeprom_save_buf[EEPROM_SAVE_SIZE + (i * 2) + 1] = cdrom_eeprom_ram[i] & 0xFF;
   }
   /* Memory Track NVRAM follows both EEPROM banks (CD content only). */
   if (jaguar_cd_mode)
      memcpy(eeprom_save_buf + MT_SAVE_OFFSET, mtMem, MT_SAVE_SIZE);
}

/* Mirror the Memory Track into the save buffer without repacking the EEPROMs
 * -- MT writes are frequent enough during a save that the full pack would be
 * wasteful, and the EEPROM banks are unaffected by them. */
static void mt_pack_save_buf(void)
{
   memcpy(eeprom_save_buf + MT_SAVE_OFFSET, mtMem, MT_SAVE_SIZE);
}

/* Unpack the save buffer back into eeprom_ram[] and cdrom_eeprom_ram[].
 * Called once after the frontend loads .srm data. */
static void eeprom_unpack_save_buf(void)
{
   unsigned i;
   for (i = 0; i < 64; i++)
      eeprom_ram[i] = ((uint16_t)eeprom_save_buf[(i * 2) + 0] << 8)
                    | eeprom_save_buf[(i * 2) + 1];
   for (i = 0; i < 64; i++)
      cdrom_eeprom_ram[i] =
            ((uint16_t)eeprom_save_buf[EEPROM_SAVE_SIZE + (i * 2) + 0] << 8)
          |  eeprom_save_buf[EEPROM_SAVE_SIZE + (i * 2) + 1];
   if (jaguar_cd_mode)
      memcpy(mtMem, eeprom_save_buf + MT_SAVE_OFFSET, MT_SAVE_SIZE);
}

void *retro_get_memory_data(unsigned type)
{
   if (type == RETRO_MEMORY_SYSTEM_RAM)
      return jaguarMainRAM;
   if (type == RETRO_MEMORY_SAVE_RAM)
   {
      /* Memory Track cart uses 128K NVRAM directly */
      if (jaguarMainROMCRC32 == 0xFDF37F47)
         return mtMem;
      /* Regular carts: return the pre-packed save buffer */
      return eeprom_save_buf;
   }
   return NULL;
}

size_t retro_get_memory_size(unsigned type)
{
   if (type == RETRO_MEMORY_SYSTEM_RAM)
      return 0x200000;
   if (type == RETRO_MEMORY_SAVE_RAM)
   {
      if (jaguarMainROMCRC32 == 0xFDF37F47)
         return MT_SAVE_SIZE;
      /* CD discs share the cart EEPROM with their CD-side EEPROM bank
       * (128 + 128 = 256 bytes).  Cart-only loads expose just the cart
       * EEPROM so existing per-game saves remain compatible. */
      /* CD: cart EEPROM + CD EEPROM + Memory Track NVRAM. */
      if (jaguar_cd_mode)
         return CD_SAVE_SIZE;
      return EEPROM_SAVE_SIZE;
   }
   return 0;
}

void retro_init(void)
{
   unsigned level = 18;

   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);

   /* Per-subsystem timing counters, rendered by the frontend's own
    * performance-counter UI (issue #510).  This is the only profiling route
    * that reaches a device we cannot attach a profiler to -- everything in
    * docs/profiling.md is host-side.  Inert when the frontend declines. */
   VJPerfInit(environ_cb);

   if (environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL))
      libretro_supports_bitmasks = true;

   /* Controller types the frontend may assign per port (#428/#429/#436).
    * Port 1 offers pad or rotary: the ST/Amiga mouse adapter is a
    * vendor-documented PORT 2 device, every title known to read one reads
    * port 2, and a port-1 mouse would be a configuration no software
    * supports -- but TR10 documents the rotary matrix for both ports and
    * Tempest 2000's CONTROLLER TYPE menu carries independent P1 and P2
    * toggles, so the rotary is offered on both.  The light gun (#438)
    * is port 1 only for the mirror-image reason: TR10 wires the light-pen
    * input to that socket and nowhere else. */
   {
      static const struct retro_controller_description port1_devices[] = {
         { "Standard Joypad", RETRO_DEVICE_JAG_PAD },
         { "Rotary (Tempest)", RETRO_DEVICE_JAG_ROTARY },
         { "Analog Joystick (bank-switching)", RETRO_DEVICE_JAG_ANALOG },
         { "Driving Controller (bank-switching)", RETRO_DEVICE_JAG_DRIVING },
         { "Light Gun", RETRO_DEVICE_JAG_LIGHTGUN },
      };
      static const struct retro_controller_description port2_devices[] = {
         { "Standard Joypad",             RETRO_DEVICE_JAG_PAD },
         { "Atari ST / PS2 Mouse",        RETRO_DEVICE_JAG_MOUSE_ST },
         { "Amiga Mouse (ST adapter)",    RETRO_DEVICE_JAG_MOUSE_AMIGA },
         { "Amiga Mouse (Amiga adapter)", RETRO_DEVICE_JAG_MOUSE_AMIGA_AD },
         { "Rotary (Tempest)",            RETRO_DEVICE_JAG_ROTARY },
         { "Analog Joystick (bank-switching)", RETRO_DEVICE_JAG_ANALOG },
         { "Driving Controller (bank-switching)", RETRO_DEVICE_JAG_DRIVING },
      };
      static const struct retro_controller_info ports[] = {
         { port1_devices, 5 },
         { port2_devices, 7 },
         { NULL, 0 },
      };

      environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *)ports);
   }

   /* Reset all bus-arbiter state (iOS cannot dlclose cores, so statics
    * persist across loads).  Must run before check_variables() applies
    * the core option — retro_load_game calls that after retro_init. */
   bus_arbiter_init();

   CrashDetectInit();

#ifdef VJ_TRACE
   vjtrace_init();
#endif
}

void retro_deinit(void)
{
   libretro_supports_bitmasks = false;

   /* Dump totals to the frontend log before going inert.  RetroArch shows
    * the same numbers in its UI, but a log line is what can actually be
    * retrieved from a locked-down tvOS box. */
   VJPerfDeinit();

   /* Belt-and-suspenders: shut down emulator subsystems if the frontend
    * calls deinit without unload first (videoBuffer != NULL means a game
    * was loaded and never unloaded). */
   if (videoBuffer)
   {
      retro_cheat_reset();
      JaguarDone();
      free(videoBuffer);
   }
   videoBuffer = NULL;
   if (sampleBuffer)
      free(sampleBuffer);
   sampleBuffer = NULL;

   /* Free the true-color and hi-res shadow buffers and reset all module
    * statics (iOS cannot dlclose cores, so statics persist across
    * loads). */
   ShadowFBShutdown();
   ShadowHiresShutdown();
   BlitMemoShutdown();
   /* Texture dump (#369): close the manifest, log the session summary,
    * free the dedupe set and reset every static. */
   TexDumpShutdown();
   show_texdump_16bpp = true;
   /* Texture replacement (#369): free the pack map, reset every static. */
   TexReplaceShutdown();
   show_texture_replace = true;
   /* Non-pad input devices: encoders, phases, attach mask, armed flag and
    * the option-derived type/scale all go back to their load-time values
    * (iOS cannot dlclose a core). */
   InputDevShutdown();
   port_device_frontend[0] = INPUTDEV_PAD;
   port_device_frontend[1] = INPUTDEV_PAD;
   port_device_forced[0]   = false;
   port_device_forced[1]   = false;
   port_device_active[0]   = INPUTDEV_PAD;
   port_device_active[1]   = INPUTDEV_PAD;
   inputdev_live[0]        = false;
   inputdev_live[1]        = false;
   /* 68K register traceback rings (#540): off is the shipped default and a
    * resident core must go back to it.  A harness that armed the flag
    * cannot dlclose the core on iOS, so leaving it set would make the next
    * game silently pay 16 register stores per emulated 68K instruction
    * with nothing in the log to explain where the frame rate went. */
   startM68KTracing        = false;
   /* Link-state edge tracker: same reason as the block above -- a resident
    * core would otherwise start the next session believing the previous
    * one's peer was still attached and skip the first UP edge. */
   netlink_was_up          = -1;
   /* OSD narration dedup (task 5, #467): same iOS-no-dlclose reasoning --
    * a resident core would otherwise believe the new session's first
    * mode/device/host is identical to the previous session's last one and
    * stay silent instead of narrating it. */
   netlink_osd_last_mode      = -1;
   netlink_osd_last_device    = -1;
   netlink_osd_last_host[0]   = '\0';
   /* Device-mismatch OSD dedup (fix wave, PR review finding 3): same
    * iOS-no-dlclose reasoning -- a resident core would otherwise believe
    * the new session already warned about a host it has never seen. */
   netlink_mismatch_last_host[0] = '\0';
   /* LAN discovery (#467): close the beacon/listener socket, drop any
    * peers it found (JLinkDiscStop() only closes the socket -- the peer
    * array survives it, and a resident core would otherwise carry stale
    * peers from an unrelated ROM into the next session), and reset the
    * host/port visibility gates -- same iOS-no-dlclose reasoning as the
    * rest of this function. */
   JLinkDiscStop();
   JLinkDiscPeersReset();
   /* Host option value list (task 4, #467): JLinkDiscPeersReset() above
    * clears the runtime peer table, but option_defs_us's live value list
    * is a separate copy this session may have spliced peers into.
    * Without restoring it, a resident core (iOS never dlcloses) would
    * have retro_set_environment() republish last session's stale peers
    * for a fresh session where discovery may not even be running yet. */
   netlink_last_rebuild_ms = 0;
   netlink_peers_dirty     = 0;
   /* One-shot force-push latch (see its declaration comment): a rebuild
    * mid-session could leave it set to 1 if retro_deinit() ran between
    * the flag being set and update_option_visibility() consuming it --
    * not reachable today (netlink_rebuild_host_options() sets and
    * consumes it back-to-back with no yield in between), but resetting
    * it costs nothing and matches every other static in this function. */
   visibility_force_push  = 0;
   if (netlink_host_presets_valid)
   {
      int host_idx = netlink_host_option_index();
      if (host_idx >= 0)
      {
         option_defs_us[host_idx].values[0] = netlink_host_presets[0];
         option_defs_us[host_idx].values[1] = netlink_host_presets[1];
         option_defs_us[host_idx].values[2] = netlink_host_presets[2];
         option_defs_us[host_idx].values[3].value = NULL;
         option_defs_us[host_idx].values[3].label = NULL;
      }
   }
   show_netlink_host       = true;
   show_netlink_port       = true;
   show_mouse_options      = true;
   show_rotary_options     = true;
   mouse_scale_q8          = 256;
   rotary_scale_q8         = 256;
   /* #439's tuning statics go back to the identity here too -- iOS cannot
    * dlclose a core, so anything left set would leak into the next load. */
   mouse_deadzone[0]       = 0;
   mouse_deadzone[1]       = 0;
   mouse_offset[0]         = 0;
   mouse_offset[1]         = 0;
   mouse_exponent_q8[0]    = 256;
   mouse_exponent_q8[1]    = 256;
   rotary_deadzone         = 0;
   rotary_offset           = 0;
   rotary_exponent_q8      = 256;
   analog_deadzone[0]      = 0;
   analog_deadzone[1]      = 0;
   analog_offset[0]        = 0;
   analog_offset[1]        = 0;
   analog_exponent_q8[0]   = 256;
   analog_exponent_q8[1]   = 256;
   show_analog_options     = true;
   headroom_logged         = false;
   video_buffer_alloc_pixels = VIDEO_BUFFER_PIXELS;
   hires_restart_notice_logged = 0;

   /* Per-title enhancement defaults DB (#368): clear the cached CRC match
    * and re-arm the gate for the next load. */
   TitleDBSetContent(NULL, 0);
   pertitle_enabled = true;
   blit_memo_requested = BLIT_MEMO_OFF;

   /* Per-title enhancement hooks (#370): the gate is re-latched from the
    * option on every load, so it re-arms OFF here. */
   TitleHookSetEnabled(0);
   TitleDBSetHooksForTest(NULL, 0);
   hook_restart_notice_logged = 0;

   eeprom_dirty_cb = NULL;
   mt_dirty_cb     = NULL;
   save_data_needs_unpack = false;
   memset(eeprom_save_buf, 0, sizeof(eeprom_save_buf));
   videoWidth = 0;
   videoHeight = 0;
   game_width = 0;
   game_height = 0;
   memset(jag_retropad, 0, sizeof(jag_retropad));
   memset(jag_numpad, 0, sizeof(jag_numpad));
   numpad_to_kb[0] = 0;
   numpad_to_kb[1] = 0;
   show_input_options = true;
   enable_alt_inputs = false;
   content_loaded = false;
   show_cd_options = true;
   show_cart_bios_option = true;
#ifdef VJ_TRACE
   /* Belt-and-suspenders, matching retro_unload_game() -- see vjt_frame's
    * decl. */
   vjt_frame = 0;
   /* Paired with vjtrace_init() in retro_init(): frees the ring (fixes a
    * leak the sanitizer job caught -- 33,554,432 bytes = the default
    * 1<<20-record ring, calloc'd once and never freed) and resets every
    * other vjtrace static, so a later retro_init() on the same process
    * (iOS cannot dlclose cores) re-allocates cleanly instead of hitting
    * vjtrace_init()'s "if (ring) return" early-out with a dangling cap.
    * Any harness's own ring dump (trace_probe_finish() and friends) has
    * already run by this point -- see harness_shutdown(), which calls
    * retro_unload_game() then retro_deinit() last. */
   vjtrace_shutdown();
#endif
}

void retro_reset(void)
{
   JaguarReset();
   CrashDetectReset();
   BlitMemoFlush();

   /* Console reset re-runs the CD BIOS boot on hardware, which reinstalls
    * the Memory Track NVM module. */
   NVMBiosReset();
   if (jaguarMemTrackInserted)
      NVMBiosInstall();

   /* Re-blank the framebuffer, or the reset presents the PREVIOUS session's
    * pixels.  TOMReset puts tomWidth back to 0, and the border-fill path in
    * TOMExecHalfline writes tomWidth pixels -- so until the game reprograms
    * a TOM video register ($F00028-$F0004F) the rows above VDB get no pixels
    * written at all and keep what was on screen before the reset.  That is
    * the same window, and the same seed, retro_load_game establishes on a
    * fresh load; a reset has to go through it too. */
   video_buffer_blank();
}

#ifdef DEBUG_PRESENTATION
static unsigned dbg_frame_counter = 0;

static void dbg_dump_frame(void)
{
   const uint32_t *fb = videoBuffer;
   unsigned nb = 0;
   unsigned i;
   uint32_t row0_first = 0, row_mid_first = 0, row_last_first = 0;
   if (!fb) { LOG_INF("[DBG] frame %u videoBuffer=NULL\n", dbg_frame_counter); return; }
   /* Sample 3 row starts and count nonblack across whole framebuffer */
   row0_first = fb[0];
   if (game_height > 0)
   {
      row_mid_first  = fb[(game_height / 2) * game_width];
      row_last_first = fb[(game_height - 1) * game_width];
   }
   for (i = 0; i < (unsigned)(game_width * game_height); i++)
      if (fb[i] & 0x00FFFFFF) nb++;
   LOG_INF("[DBG] frame %u: tom=%ux%u game=%ux%u screenPitch=%u videoBuffer=%p\n"
           "      pixels[0]=0x%08X mid=0x%08X last=0x%08X nonblack=%u/%u\n"
           "      ltxd=0x%04X rtxd=0x%04X dsp_running=%d\n",
           dbg_frame_counter, tomWidth, tomHeight, game_width, game_height,
           screenPitch, (void *)fb, row0_first, row_mid_first, row_last_first,
           nb, (unsigned)(game_width * game_height),
           ltxd ? *ltxd : 0xFFFF, rtxd ? *rtxd : 0xFFFF,
           DSPIsRunning() ? 1 : 0);
}
#endif

void retro_run(void)
{
   bool updated = false;

#ifdef VJ_TRACE
   /* Stamp the frame number BEFORE the machine runs, so every event
    * emitted during this retro_run carries the number of the frame it
    * belongs to.  Ticking at the END instead (where this used to live)
    * left machine events stamped with the PREVIOUS frame while events a
    * harness emits from its post-run frame hook carried the current
    * one -- two different corrections needed to align one ring.  The
    * first retro_run is frame 1, matching the harness frame counter;
    * events emitted during retro_load_game/retro_init, before any
    * retro_run, carry frame 0.  retro_run has no early return, so this
    * runs exactly once per frame. */
   vjtrace_frame_tick(++vjt_frame);
#endif

   /* Blit memo: advance the shadow-restamp dedupe epoch. */
   if (blitMemoMode)
      BlitMemoFrame();

   /* Texture dump (#369): advance the manifest frame number. */
   if (texDumpEnabled)
      TexDumpFrame();

   /* On the first frame, unpack save data that the frontend loaded
    * into our RETRO_MEMORY_SAVE_RAM buffer after retro_load_game(). */
   if (save_data_needs_unpack)
   {
      save_data_needs_unpack = false;
      if (jaguarMainROMCRC32 != 0xFDF37F47)
         eeprom_unpack_save_buf();
      /* Memory Track: mtMem was written directly, no unpack needed. */
   }

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      check_variables();

   /* Apply pending geometry change BEFORE rendering this frame.  TOM's
    * scanline renderer reads tomWidth (pixels per row) and screenPitch
    * (line stride) live; if tomWidth grew but screenPitch is stale, later
    * rows overwrite the tail of earlier rows and the framebuffer comes out
    * scrambled.  Frontends that re-allocate the texture on SET_GEOMETRY
    * (iOS Metal) can also drop the next video_cb if the geometry change
    * arrives after the frame is already submitted at the wrong size.
    * Latching pitch + advertising new geometry up front keeps tomWidth and
    * screenPitch in sync for the entire frame. */
   if ((tomWidth != videoWidth || tomHeight != videoHeight) && tomWidth > 0 && tomHeight > 0)
   {
#ifdef DEBUG_PRESENTATION
      LOG_INF("[DBG] frame %u: GEOMETRY CHANGE %ux%u -> %ux%u (applied pre-render)\n",
              dbg_frame_counter, videoWidth, videoHeight, tomWidth, tomHeight);
#endif
      /* videoWidth/videoHeight track TOM in STOCK units (the change
       * detector above must keep firing on stock transitions); the
       * presented geometry and pitch are the stock size scaled by the
       * load-time internal resolution factor (1 when off). */
      videoWidth = tomWidth, videoHeight = tomHeight;
      game_width = tomWidth * shadowHiresN, game_height = tomHeight * shadowHiresN;

      JaguarSetScreenPitch(game_width);

      retro_get_system_av_info(&g_av_info);
      environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &g_av_info);
   }

   /* Service the network link: progress TCP connect/accept, drain the
    * socket into the transport ring, then let the UART start an RX
    * frame for anything that arrived.  FrameTick refills the per-frame
    * reply-wait budget. */
   JLinkFrameTick();
   JLinkPoll();
   UARTPoll();

   /* Log link up/down edges.  JLinkConnected() already abstracts TCP and
    * netpacket, so this reports the one fact a user debugging "it says it
    * has a modem but won't dial" needs and previously could not get from
    * anywhere: whether a peer is actually on the other end.  Edge-only --
    * a per-frame line would flood the log. */
   {
      int up = (JLinkMode() != JLINK_MODE_DISABLED) && JLinkConnected();
      if (JLinkMode() == JLINK_MODE_DISABLED)
         netlink_was_up = -1;
      else if (up != netlink_was_up)
      {
         if (up)
         {
            LOG_INF("[NETLINK] link UP (%s, device=%s)\n",
                    netlink_mode_name(JLinkMode()),
                    JLinkDevice() == JLINK_DEVICE_VOICEMODEM ? "voicemodem"
                                                             : "jaglink");
            netlink_osd("Jaguar link connected (%s)",
                        netlink_device_label(JLinkDevice()));
         }
         else if (netlink_was_up == 1)
         {
            LOG_WRN("[NETLINK] link DOWN (%s) -- peer disconnected\n",
                    netlink_mode_name(JLinkMode()));
            netlink_osd("Jaguar link lost");
         }
         else
            LOG_INF("[NETLINK] %s open, waiting for peer...\n",
                    netlink_mode_name(JLinkMode()));
         netlink_was_up = up;
      }
   }

   /* Rebuild the host picker only when the peer set actually changed --
    * never on a timer.  A second SET_CORE_OPTIONS_V2 tears down and
    * rebuilds RetroArch's whole option manager (runloop.c), so doing it
    * per beacon would thrash the menu under the user's thumb.
    *
    * JLinkDiscConsumeChanged() both reads AND CLEARS the flag.  A bare
    * "flag && rate_limit_ok" (as sketched in the task brief) drops any
    * change that lands inside the 2s cooldown -- the flag is gone, and
    * the peer that triggered it is never rebuilt in; the next flag to
    * fire is likely that same peer's 10s expiry, which publishes a list
    * that never showed it at all.  The sticky latch below fixes that:
    * any change latches netlink_peers_dirty, and only the rate limit
    * gates the rebuild itself, so a change is delayed by at most 2s,
    * never lost.  JLinkDiscStart() (jlink_discover.c) is already
    * idempotent on repeated netlink_apply() calls with unchanged
    * parameters, so this rebuild's own SET_CORE_OPTIONS_V2 -- even if it
    * causes the frontend to re-signal a variables update -- cannot wipe
    * the peer table out from under itself. */
   {
      uint32_t disc_now = JLinkNowMs();
      if (JLinkDiscConsumeChanged())
         netlink_peers_dirty = 1;
      if (netlink_peers_dirty
          && (uint32_t)(disc_now - netlink_last_rebuild_ms) >= 2000)
      {
         netlink_rebuild_host_options();
         netlink_last_rebuild_ms = disc_now;
         netlink_peers_dirty     = 0;
      }
   }

   update_input();

   /* Hi-res: advance the shadow surface's frame epoch (no-op when off;
    * see shadowfb.h, design section 3.4). */
   ShadowHiresFrameTick();

   DACPrepareFrame(vjs.hardwareTypeNTSC == 1 ? BUFNTSC : BUFPAL);
   JaguarExecuteNew();
   cheat_apply_all();
   SoundCallback(NULL, sampleBuffer, vjs.hardwareTypeNTSC == 1 ? BUFNTSC : BUFPAL);

   /* Give every presented row defined content.
    *
    * TOM renders one row per even halfline in [topVisible, bottomVisible),
    * but the presented height comes from TOMGetVideoModeHeight(), derived
    * independently from VDB/VDE.  When the two disagree the tail rows are
    * never written and keep whatever was last drawn there -- frozen stale
    * pixels that persist while the rest of the screen animates.  Alien vs
    * Predator in-game is the reported case (#178): VDB=40, VDE=2047, VP=523
    * gives 236 rendered rows against a presented height of 240, so rows
    * 236-239 held a brown bar left over from an earlier VDB=28 frame.
    *
    * Opaque black, not the border colour: these rows sit BELOW the visible
    * field (bottomVisible is the field bottom), so they are blanking lines,
    * not border lines.  The border branch in TOMExecHalfline already covers
    * rows that are inside the field but outside the active display window.
    *
    * The reverse mismatch also exists -- a narrow window (e.g. VDB=38,
    * VDE=100 -> 34 rendered rows, presented height 31) writes past the
    * presented height.  That is harmless here (the allocation is 1024x512
    * and the extra rows are simply not shown) and is left alone. */
   {
      uint32_t written = TOMGetWrittenRowExtent();
      /* Hi-res: TOM reports rows in STOCK units.  The decision below is
       * made in stock units so it fires on exactly the same frames as a
       * 1x run (box-replication identity, design section 7.3); only the
       * blanked row range is scaled to Nx. */
      uint32_t hires_n = (uint32_t)shadowHiresN;
      uint32_t stock_height = (uint32_t)game_height / hires_n;

      /* Only blank a genuine TAIL.  A large shortfall does not mean "these
       * rows are stale", it means TOM and the presented geometry disagree
       * about what mode we are in -- our window model has failed, and
       * erasing the last good frame is worse than leaving it up.
       *
       * The bound is measured, not a guess.  Across the 115-ROM corpus every
       * SUSTAINED gap is small: 4 rows (Evolution, and Alien vs Predator --
       * the reported case), 7 (Cannon Fodder, Gorf 2000), 8 (Bubsy), 14
       * (Pitfall), 15, 17 (Sensible Soccer).  The one pathological case is
       * DEMO1C (PD) at 102 rows on a single frame as it switches to a
       * 328x135 mode and then stops rendering entirely -- blanking there
       * erased a visible colour field and left a black screen.  32 leaves
       * generous headroom over the observed maximum while excluding that
       * class by a wide margin.
       *
       * The coverage clause is what makes this hold at any frame height: in
       * a short mode a shortfall well inside 32 rows can still be most of
       * the screen (height 40 with 8 rows written is the DEMO1C shape at
       * small scale), and requiring the frame to be >= 3/4 rendered rejects
       * it.  Measured coverage separates the two classes by a wide margin --
       * every real tail above is 93-98% rendered, DEMO1C's transition frame
       * is 24%.
       *
       * written == 0 (TOM addressed no rows at all) is the degenerate form
       * of the same thing; the coverage test already rejects it, but keep it
       * explicit so the intent survives a future tweak of the bounds. */
      if (written > 0 && written < stock_height
          && stock_height - written <= MAX_BLANK_TAIL_ROWS
          && written * MIN_BLANK_COVERAGE_DEN
             >= stock_height * MIN_BLANK_COVERAGE_NUM)
      {
         uint32_t row;
         uint32_t col;
         for (row = written * hires_n; row < (uint32_t)game_height; row++)
         {
            uint32_t *line = videoBuffer + (row * (uint32_t)game_width);
            for (col = 0; col < (uint32_t)game_width; col++)
               line[col] = 0xFF000000;
         }
      }
   }

   /* Runtime watchdog: looks for GPU/DSP PC escape, GPU/DSP wedge,
    * and video stall. Fires LOG_WRN/LOG_ERR via vj_log_cb so the
    * signature shows up in the RetroArch log without extra wiring.
    * Off-mode short-circuits in CrashDetectFrameTick, so the cost
    * when disabled is one indirect function call per frame. */
   CrashDetectFrameTick(videoBuffer, (unsigned)game_width, (unsigned)game_height);

   video_cb(videoBuffer, game_width, game_height, game_width << 2);

#ifdef DEBUG_PRESENTATION
   if (dbg_frame_counter < 5
       || dbg_frame_counter == 60
       || dbg_frame_counter == 600
       || dbg_frame_counter == 1200
       || dbg_frame_counter == 1800
       || dbg_frame_counter == 3600
       || (dbg_frame_counter % 120) == 0)
      dbg_dump_frame();
   dbg_frame_counter++;
#endif
}
