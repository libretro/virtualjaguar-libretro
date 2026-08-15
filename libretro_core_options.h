#ifndef LIBRETRO_CORE_OPTIONS_H__
#define LIBRETRO_CORE_OPTIONS_H__

#include <stdlib.h>
#include <string.h>

#include <libretro.h>
#include <retro_inline.h>

#ifndef HAVE_NO_LANGEXTRA
#include "libretro_core_options_intl.h"
#endif

#define INPUT_OPTIONS        \
{                            \
   { "up",     "Up" },       \
   { "down",   "Down" },     \
   { "left",   "Left" },     \
   { "right",  "Right" },    \
   { "btn_a",  "A" },        \
   { "btn_b",  "B" },        \
   { "btn_c",  "C" },        \
   { "pause",  "Pause" },    \
   { "option", "Option" },   \
   { "num_0",  "Numpad 0" }, \
   { "num_1",  "Numpad 1" }, \
   { "num_2",  "Numpad 2" }, \
   { "num_3",  "Numpad 3" }, \
   { "num_4",  "Numpad 4" }, \
   { "num_5",  "Numpad 5" }, \
   { "num_6",  "Numpad 6" }, \
   { "num_7",  "Numpad 7" }, \
   { "num_8",  "Numpad 8" }, \
   { "num_9",  "Numpad 9" }, \
   { "star",   "Numpad *" }, \
   { "hash",   "Numpad #" }, \
   { "---",    NULL },       \
   { NULL, NULL },           \
}

/*
 ********************************
 * VERSION: 2.0
 ********************************
 *
 * - 2.0: Add support for core options v2 interface
 * - 1.3: Move translations to libretro_core_options_intl.h
 *        - libretro_core_options_intl.h includes BOM and utf-8
 *          fix for MSVC 2010-2013
 *        - Added HAVE_NO_LANGEXTRA flag to disable translations
 *          on platforms/compilers without BOM support
 * - 1.2: Use core options v1 interface when
 *        RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION is >= 1
 *        (previously required RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION == 1)
 * - 1.1: Support generation of core options v0 retro_core_option_value
 *        arrays containing options with a single value
 * - 1.0: First commit
*/

#ifdef __cplusplus
extern "C" {
#endif

/*
 ********************************
 * Core Option Definitions
 ********************************
*/

/* RETRO_LANGUAGE_ENGLISH */

/* Default language:
 * - All other languages must include the same keys and values
 * - Will be used as a fallback in the event that frontend language
 *   is not available
 * - Will be used as a fallback for any missing entries in
 *   frontend language definition */

struct retro_core_option_v2_category option_cats_us[] = {
   {
      "video",
      "Video",
      "Blitter implementation and console video standard."
   },
   {
      "bios_boot",
      "BIOS & Boot",
      "How cartridges boot: emulated (HLE) BIOS services or the real Jaguar boot ROM."
   },
   {
      "cdrom",
      "CD-ROM",
      "Jaguar CD boot path, CD BIOS selection and drive read speed. Only applies to CD content."
   },
   {
      "network",
      "Network Link",
      "JagLink / CatBox serial link emulation for networked games."
   },
   {
      "input",
      "Input",
      "Controller handling shared by both ports."
   },
   {
      "input_p1",
      "Input Port 1",
      "Change input mappings for port 1."
   },
   {
      "input_p2",
      "Input Port 2",
      "Change input mappings for port 2."
   },
   {
      "diagnostics",
      "Diagnostics",
      "Logging and watchdog aids for troubleshooting and bug reports."
   },
   {
      "timing",
      "Timing",
      "Clock speed multipliers, then the experimental hardware-timing models."
   },
   { NULL, NULL, NULL },
};

struct retro_core_option_v2_definition option_defs_us[] = {
   {
      "virtualjaguar_usefastblitter",
      "Blitter",
      NULL,
      "Choose which blitter implementation to use. 'Accurate' is SIMD-accelerated (SSE2 on x86, NEON on ARM) and is the most compatible. 'Fast' is the older blitter; it trades accuracy for extra speed on low-end hardware and breaks some games.",
      NULL,
      "video",
      {
         { "disabled", "Accurate" },
         { "enabled",  "Fast" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_true_color",
      "True Color (Gouraud Precision)",
      NULL,
      "Render gouraud-shaded pixels at full precision (chroma x 24-bit intensity) to reduce banding in 3D games. The game-visible 16-bit framebuffer is unchanged. Applies to CRY 16bpp video modes only.",
      NULL,
      "video",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_internal_resolution",
      "Internal Resolution (Restart Required)",
      NULL,
      "Render internally at a multiple of the Jaguar's native resolution. Applied when content is loaded; changing it mid-game takes effect on restart. The game-visible framebuffer and all emulation timing are unchanged. Combines with True Color.",
      NULL,
      "video",
      {
         { "1x", "1x (native)" },
         { "2x", NULL },
         { NULL, NULL },
      },
      "1x"
   },
   {
      "virtualjaguar_pertitle_defaults",
      "Per-Title Enhancement Defaults",
      NULL,
      "Apply known-safe enhancement presets automatically for recognized games (e.g. internal resolution or true color for titles verified to benefit). A preset only applies to options you have left at their default value -- any option you change yourself always wins. Disable for stock behaviour on every title.",
      NULL,
      "video",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "virtualjaguar_enhancement_hooks",
      "Per-Title Enhancement Hooks",
      NULL,
      "Apply per-game byte patches from the enhancement database to the loaded cartridge image at load time (game-side fixes that no core option can express). Off by default. Each patch verifies the bytes it expects to find and refuses to write anything if they differ, so it can never corrupt a dump it was not written for. Cartridge content only; a change takes effect on restart.",
      NULL,
      "video",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_blit_memo",
      "Blit Memoization (Per-Title)",
      NULL,
      "Skip blits whose inputs are provably unchanged since an identical earlier blit (some titles re-render an identical scene every engine cycle while the player is idle). Output is bit-identical by construction; enabled per title via the enhancement database. Verify mode never skips - it executes every would-be skip and logs any divergence (for validating new titles). Not available for CD content.",
      NULL,
      "video",
      {
         { "disabled", "Disabled" },
         { "enabled",  "Enabled" },
         { "verify",   "Verify (debug, no speedup)" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_crash_detect",
      "Crash Detect",
      NULL,
      "Lightweight runtime watchdog that logs GPU/DSP PC escape, GPU/DSP wedge, and video stall events to the RetroArch log. Helpful when filing bug reports about games that hang or go to a black screen mid-play. Verbose mode also dumps a state heartbeat every 10 seconds.",
      NULL,
      "diagnostics",
      {
         { "enabled",  "Enabled" },
         { "disabled", "Disabled" },
         { "verbose",  "Enabled (verbose / heartbeat)" },
         { NULL, NULL },
      },
      "enabled"
   },
   /* Clock speeds first, then the experimental timing models they interact
    * with -- the toggles below are grouped under the two scales on purpose. */
   {
      "virtualjaguar_m68k_clock_scale",
      "M68K Clock Scale (Overclock)",
      NULL,
      "Run the 68000 at a multiple of its stock ~13.3 MHz. An enhancement, not an accuracy fix: it can smooth framerate-limited games (Doom, AvP, Checkered Flag) but may break titles that depend on stock CPU timing. Timers and bus costs stay at stock speed. If an overclocked game misbehaves, try enabling the timing models below. Report bugs only at 1x.",
      NULL,
      "timing",
      {
         { "0.5x", NULL },
         { "1x",   "1x (stock)" },
         { "1.5x", NULL },
         { "2x",   NULL },
         { "3x",   NULL },
         { NULL, NULL },
      },
      "1x"
   },
   {
      "virtualjaguar_risc_clock_scale",
      "RISC (GPU/DSP) Clock Scale (Overclock)",
      NULL,
      "Run the GPU and DSP at a multiple of their stock ~26.6 MHz. An enhancement, not an accuracy fix: extra cycles can lift GPU-bound framerates. Audio pacing and timers stay at stock speed, so nothing pitch-shifts. May break titles that depend on stock RISC timing. If an overclocked game misbehaves, try enabling the timing models below. Report bugs only at 1x.",
      NULL,
      "timing",
      {
         { "0.5x", NULL },
         { "1x",   "1x (stock)" },
         { "1.5x", NULL },
         { "2x",   NULL },
         { NULL, NULL },
      },
      "1x"
   },
   {
      "virtualjaguar_dram_timing",
      "DRAM Timing (Experimental)",
      NULL,
      "Charge the GPU and 68000 realistic DRAM access time once they leave their local buses, pacing hardware-timed games (Doom-class) closer to real hardware. Each processor pays only its own costs, so relative CPU/GPU timing is preserved. Still being calibrated.",
      NULL,
      "timing",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_gpu_pipeline_timing",
      "GPU Pipeline Timing (Experimental)",
      NULL,
      "Model the GPU's real instruction costs: the single external-memory gateway, the register score-board, and ALU interlocks. The emulated GPU otherwise finishes renders 2-4x faster than silicon, which makes loops paced on render completion (Doom's menus and demo, Hover Strike) run too fast. Still being calibrated.",
      NULL,
      "timing",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_blitter_timing",
      "Blitter Bus Timing (Experimental)",
      NULL,
      "Charge the 68000 the bus time each blit really takes -- on hardware the blitter is the top-priority bus master and freezes the cacheless 68000 while it runs. Zero-time blits let games paced on blit completion (Doom's menus, Hover Strike) run too fast. Still being calibrated.",
      NULL,
      "timing",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_netlink",
      "Network Link (JagLink / CatBox)",
      NULL,
      "Emulates JERRY's serial link port used by networked games (BattleSphere, AirCars, Doom deathmatch). 'Loopback' echoes transmitted bytes back to this console, for testing link-detect menus without a partner. TCP Host listens for a second emulator instance; TCP Client connects to the address in 'Network Link Host'. Localhost/LAN latency recommended.",
      NULL,
      "network",
      {
         { "disabled",   NULL },
         { "loopback",   "Loopback (echo to self)" },
         { "tcp_server", "TCP Host (listen)" },
         { "tcp_client", "TCP Client (connect)" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_netlink_host",
      "Network Link Host (TCP Client)",
      NULL,
      "Address the TCP client connects to -- an IP, a DNS name, or a Bonjour/mDNS name. Easiest LAN setup with no typing at all: name the host machine 'jaghub' (its local hostname), then pick the 'jaghub.local' preset here on each client. Frontends with free-text option entry accept any address directly; in stock RetroArch the alternative is 'From file' with the address on the first line of vj_netlink.txt in the system directory. The VJ_NETLINK_HOST environment variable overrides this option. If you would rather not configure anything, use RetroArch's own netplay instead -- it finds hosts on the LAN by itself and carries the link with this option left disabled.",
      NULL,
      "network",
      {
         { "127.0.0.1",      "127.0.0.1 (localhost)" },
         { "jaghub.local",   "jaghub.local (host machine named 'jaghub' on the LAN)" },
         { "vj_netlink.txt", "From file (vj_netlink.txt in system dir)" },
         { NULL, NULL },
      },
      "127.0.0.1"
   },
   {
      "virtualjaguar_netlink_port",
      "Network Link Port",
      NULL,
      "TCP port for the network link (both sides must match). Overridable with the VJ_NETLINK_PORT environment variable.",
      NULL,
      "network",
      {
         { "42171", NULL },
         { "42172", NULL },
         { "42173", NULL },
         { "42174", NULL },
         { NULL, NULL },
      },
      "42171"
   },
   {
      "virtualjaguar_netlink_wait",
      "Network Link Latency Hiding",
      NULL,
      "Briefly holds each frame until the link partner's reply arrives, so network latency doesn't round every link exchange up to whole video frames. The wait adapts automatically to the measured connection (a few ms on localhost, more on Wi-Fi) and is capped so audio/video pacing survives. Disable only for troubleshooting or benchmarking.",
      NULL,
      "network",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "virtualjaguar_cd_trace",
      "CD Trace (Diagnostic)",
      NULL,
      "Records DSA command/response traffic and seek/FIFO transitions to a bounded ring buffer, dumped to the RetroArch log when the cd_seek_wedge watchdog fires (or on request by test harnesses). Diagnostic only -- intended for troubleshooting Jaguar CD boot/data-transfer bugs, not for normal play. Can also be forced on headlessly via the VJ_CD_TRACE=1 environment variable.",
      NULL,
      "diagnostics",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_bios",
      "BIOS (Cartridges)",
      NULL,
      "Which BIOS a CARTRIDGE boots with. 'HLE' has the core emulate the BIOS setup and services itself, which lets most commercial titles boot faster and skips the boot animation. 'Real' runs the actual Jaguar boot ROM, which some titles require. The boot ROM is built into the core, so neither setting needs a file -- unlike the CD BIOS, the console boot ROM is never loaded from the system directory. Ignored for CD content: there, 'CD Boot Mode' decides and turns the boot ROM on or off to match.",
      NULL,
      "bios_boot",
      {
         { "disabled", "HLE" },
         { "enabled",  "Real" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_jgd",
      "Jaguar GameDrive (Restart)",
      NULL,
      "Emulate the Jaguar GameDrive (JagGD) flash cartridge: its detection/install interface and 1 MB bank switching of up to 16 MB of cart SDRAM. 'Auto' turns it on only for ROM images larger than the 6 MB cartridge window. 'Enabled' forces it on for smaller images too, for GD-locked homebrew that refuses to boot without the cart (equivalent to BigPEmu's Force JGD). Without it, GD-locked titles hang at boot exactly as on a stock console.",
      NULL,
      "bios_boot",
      {
         { "auto",     "Auto (images over 6 MB)" },
         { "disabled", NULL },
         { "enabled",  "Enabled (force, for GD-locked images)" },
         { NULL, NULL },
      },
      "auto"
   },
   {
      "virtualjaguar_pal",
      "PAL (Restart)",
      NULL,
      "Emulate a PAL Jaguar instead of NTSC.",
      NULL,
      "video",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_cd_bios_type",
      "CD BIOS Type (Restart)",
      NULL,
      "Which CD BIOS the real-BIOS boot path uses. 'Retail' is the standard consumer BIOS; 'Developer' is the dev-kit BIOS, which applies less strict disc checks and can boot images the retail BIOS refuses. Both are built into the core, so no files are required. CD BIOS ROM files in the system directory are preferred over the embedded images, and this selection picks which file wins when both types are present. Only has an effect when 'CD Boot Mode' is 'Real BIOS' or 'Auto' -- the HLE boot path never runs a CD BIOS.",
      NULL,
      "cdrom",
      {
         { "retail", "Retail" },
         { "dev",    "Developer" },
         { NULL, NULL },
      },
      "retail"
   },
   {
      "virtualjaguar_cd_boot_mode",
      "CD Boot Mode (Restart)",
      NULL,
      "How Jaguar CD discs boot. This OVERRIDES the 'BIOS (Cartridges)' setting for CD content. 'HLE' emulates the CD BIOS services directly and runs with the console boot ROM off -- fastest and the most broadly compatible. 'Real BIOS' runs an actual CD BIOS and forces the boot ROM on: more faithful, still experimental. It prefers a CD BIOS ROM file from the system directory (searched under several common names, and in Atari - Jaguar / Atari - Jaguar CD / jaguar / jaguarcd sub-folders) and otherwise uses the embedded image chosen by 'CD BIOS Type', so no files are required. 'Auto' is currently identical to 'Real BIOS'. If a real-BIOS mode is chosen but no CD BIOS can be staged at all, the core falls back to HLE rather than failing.",
      NULL,
      "cdrom",
      {
         { "hle",  "HLE (Recommended)" },
         { "auto", "Auto (Real BIOS)" },
         { "bios", "Real BIOS (Included, Experimental)" },
         { NULL, NULL },
      },
      "hle"
   },
   {
      "virtualjaguar_cd_read_speed",
      "CD Read Speed (HLE Boot Mode Only)",
      NULL,
      "Data-transfer rate for Jaguar CD reads in HLE boot mode. '2x' matches the real drive (300 KB/s) and is hardware-accurate. Higher speeds shorten load times but may break timing-sensitive titles (some games rely on the drive rate for code overlays, music cues, and load handshakes); 'Instant' completes each read in one tick and is the most likely to cause hangs. BIOS boot mode always uses the accurate rate. Applied per-read: a transfer already in flight keeps the speed it started with.",
      NULL,
      "cdrom",
      {
         { "1x",      "1x (150 KB/s)" },
         { "2x",      "2x (Accurate)" },
         { "4x",      "4x" },
         { "8x",      "8x" },
         { "instant", "Instant" },
         { NULL, NULL },
      },
      "2x"
   },
   {
      "virtualjaguar_memory_track",
      "Memory Track (Restart)",
      NULL,
      "Emulate the Memory Track save cartridge alongside the CD unit, as on real hardware. CD games detect it and save settings, progress and high scores to its 128 KB NVRAM (stored in the save file). Disable to emulate a console without the cartridge -- games will warn that game information cannot be saved.",
      NULL,
      "cdrom",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "virtualjaguar_alt_inputs",
      "Enable Core Options Remapping",
      NULL,
      "Enabling this option will let you rebind controllers from the core options, removing the 'Controls' menu limitation that makes Numpad 7, 8, 9, * and # impossible to remap.\nNOTE: the 'Controls' menu can still conflict with the core options remapping, if you're using a remap file it is recommended to delete/reset it.",
      NULL,
      "input",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_p2_device",
      "Port 2 > Controller Type",
      "Controller Type",
      "Which peripheral is plugged into controller port 2. 'Atari ST / PS2 Mouse' is the wiring used by the AtariAge and Brewing Academy ST adapters and by PS/2 mouse adapters. 'Amiga Mouse (ST adapter)' is an Amiga mouse plugged into an ST-wired adapter -- this is what an in-game 'Atari / Amiga' selector normally chooses between. 'Amiga Mouse (Amiga adapter)' is the rarer dedicated adapter. A mouse asserts its state in every row scan, exactly as the real row-blind adapter does, so the port-2 RetroPad is disconnected while one is selected.",
      NULL,
      "input_p2",
      {
         { "auto",                "Auto (per-title default)" },
         { "pad",                 "Standard Joypad" },
         { "mouse_st",            "Atari ST / PS2 Mouse" },
         { "mouse_amiga",         "Amiga Mouse (ST adapter)" },
         { "mouse_amiga_adapter", "Amiga Mouse (Amiga adapter)" },
         { NULL, NULL },
      },
      "auto"
   },
   {
      "virtualjaguar_mouse_sensitivity",
      "Port 2 > Mouse Sensitivity",
      "Mouse Sensitivity",
      "Scales mouse movement before it is converted to quadrature pulses. The emulated device can only emit one pulse per controller poll, so raising this past what the game's poll rate can carry adds lag rather than speed.",
      NULL,
      "input_p2",
      {
         { "25",  "25%" },
         { "50",  "50%" },
         { "75",  "75%" },
         { "100", "100%" },
         { "150", "150%" },
         { "200", "200%" },
         { "300", "300%" },
         { "400", "400%" },
         { NULL, NULL },
      },
      "100"
   },
   {
      "virtualjaguar_p1_numpad_to_kb",
      "Port 1 > Numpad Buttons to Keyboard Keys",
      "Numpad Buttons to Keyboard Keys",
      "Map Jaguar numpad 0-9, * and # to keyboard keys. 'Number Row Keys' will use 1234567890-= keys, 'Keypad Keys' will use 0123456789/* keypad keys.",
      NULL,
      "input_p1",
      {
         { "disabled", NULL },
         { "numbers",  "Number Row Keys" },
         { "keypad",   "Keypad Keys" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_p1_retropad_up",
      "Port 1 > RetroPad Up",
      "RetroPad Up",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "up"
   },
   {
      "virtualjaguar_p1_retropad_down",
      "Port 1 > RetroPad Down",
      "RetroPad Down",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "down"
   },
   {
      "virtualjaguar_p1_retropad_left",
      "Port 1 > RetroPad Left",
      "RetroPad Left",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "left"
   },
   {
      "virtualjaguar_p1_retropad_right",
      "Port 1 > RetroPad Right",
      "RetroPad Right",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "right"
   },
   {
      "virtualjaguar_p1_retropad_a",
      "Port 1 > RetroPad A",
      "RetroPad A",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "btn_a"
   },
   {
      "virtualjaguar_p1_retropad_b",
      "Port 1 > RetroPad B",
      "RetroPad B",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "btn_b"
   },
   {
      "virtualjaguar_p1_retropad_x",
      "Port 1 > RetroPad X",
      "RetroPad X",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "num_0"
   },
   {
      "virtualjaguar_p1_retropad_y",
      "Port 1 > RetroPad Y",
      "RetroPad Y",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "btn_c"
   },
   {
      "virtualjaguar_p1_retropad_select",
      "Port 1 > RetroPad Select",
      "RetroPad Select",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "pause"
   },
   {
      "virtualjaguar_p1_retropad_start",
      "Port 1 > RetroPad Start",
      "RetroPad Start",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "option"
   },
   {
      "virtualjaguar_p1_retropad_l1",
      "Port 1 > RetroPad L1",
      "RetroPad L1",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "num_1"
   },
   {
      "virtualjaguar_p1_retropad_r1",
      "Port 1 > RetroPad R1",
      "RetroPad R1",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "num_2"
   },
   {
      "virtualjaguar_p1_retropad_l2",
      "Port 1 > RetroPad L2",
      "RetroPad L2",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "num_3"
   },
   {
      "virtualjaguar_p1_retropad_r2",
      "Port 1 > RetroPad R2",
      "RetroPad R2",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "num_4"
   },
   {
      "virtualjaguar_p1_retropad_l3",
      "Port 1 > RetroPad L3",
      "RetroPad L3",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "num_5"
   },
   {
      "virtualjaguar_p1_retropad_r3",
      "Port 1 > RetroPad R3",
      "RetroPad R3",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "num_6"
   },
   {
      "virtualjaguar_p1_retropad_analog_lu",
      "Port 1 > RetroPad Left Analog Up",
      "RetroPad Left Analog Up",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p1_retropad_analog_ld",
      "Port 1 > RetroPad Left Analog Down",
      "RetroPad Left Analog Down",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p1_retropad_analog_ll",
      "Port 1 > RetroPad Left Analog Left",
      "RetroPad Left Analog Left",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p1_retropad_analog_lr",
      "Port 1 > RetroPad Left Analog Right",
      "RetroPad Left Analog Right",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p1_retropad_analog_ru",
      "Port 1 > RetroPad Right Analog Up",
      "RetroPad Right Analog Up",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p1_retropad_analog_rd",
      "Port 1 > RetroPad Right Analog Down",
      "RetroPad Right Analog Down",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p1_retropad_analog_rl",
      "Port 1 > RetroPad Right Analog Left",
      "RetroPad Right Analog Left",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p1_retropad_analog_rr",
      "Port 1 > RetroPad Right Analog Right",
      "RetroPad Right Analog Right",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p2_numpad_to_kb",
      "Port 2 > Numpad Buttons to Keyboard Keys",
      "Numpad Buttons to Keyboard Keys",
      "Map Jaguar numpad 0-9, * and # to keyboard keys. 'Number Row Keys' will use 1234567890-= keys, 'Keypad Keys' will use 0123456789/* keypad keys.",
      NULL,
      "input_p2",
      {
         { "disabled", NULL },
         { "numbers",  "Number Row Keys" },
         { "keypad",   "Keypad Keys" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_p2_retropad_up",
      "Port 2 > RetroPad Up",
      "RetroPad Up",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "up"
   },
   {
      "virtualjaguar_p2_retropad_down",
      "Port 2 > RetroPad Down",
      "RetroPad Down",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "down"
   },
   {
      "virtualjaguar_p2_retropad_left",
      "Port 2 > RetroPad Left",
      "RetroPad Left",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "left"
   },
   {
      "virtualjaguar_p2_retropad_right",
      "Port 2 > RetroPad Right",
      "RetroPad Right",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "right"
   },
   {
      "virtualjaguar_p2_retropad_a",
      "Port 2 > RetroPad A",
      "RetroPad A",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "btn_a"
   },
   {
      "virtualjaguar_p2_retropad_b",
      "Port 2 > RetroPad B",
      "RetroPad B",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "btn_b"
   },
   {
      "virtualjaguar_p2_retropad_x",
      "Port 2 > RetroPad X",
      "RetroPad X",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "num_0"
   },
   {
      "virtualjaguar_p2_retropad_y",
      "Port 2 > RetroPad Y",
      "RetroPad Y",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "btn_c"
   },
   {
      "virtualjaguar_p2_retropad_select",
      "Port 2 > RetroPad Select",
      "RetroPad Select",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "pause"
   },
   {
      "virtualjaguar_p2_retropad_start",
      "Port 2 > RetroPad Start",
      "RetroPad Start",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "option"
   },
   {
      "virtualjaguar_p2_retropad_l1",
      "Port 2 > RetroPad L1",
      "RetroPad L1",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "num_1"
   },
   {
      "virtualjaguar_p2_retropad_r1",
      "Port 2 > RetroPad R1",
      "RetroPad R1",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "num_2"
   },
   {
      "virtualjaguar_p2_retropad_l2",
      "Port 2 > RetroPad L2",
      "RetroPad L2",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "num_3"
   },
   {
      "virtualjaguar_p2_retropad_r2",
      "Port 2 > RetroPad R2",
      "RetroPad R2",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "num_4"
   },
   {
      "virtualjaguar_p2_retropad_l3",
      "Port 2 > RetroPad L3",
      "RetroPad L3",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "num_5"
   },
   {
      "virtualjaguar_p2_retropad_r3",
      "Port 2 > RetroPad R3",
      "RetroPad R3",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "num_6"
   },
   {
      "virtualjaguar_p2_retropad_analog_lu",
      "Port 2 > RetroPad Left Analog Up",
      "RetroPad Left Analog Up",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p2_retropad_analog_ld",
      "Port 2 > RetroPad Left Analog Down",
      "RetroPad Left Analog Down",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p2_retropad_analog_ll",
      "Port 2 > RetroPad Left Analog Left",
      "RetroPad Left Analog Left",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p2_retropad_analog_lr",
      "Port 2 > RetroPad Left Analog Right",
      "RetroPad Left Analog Right",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p2_retropad_analog_ru",
      "Port 2 > RetroPad Right Analog Up",
      "RetroPad Right Analog Up",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p2_retropad_analog_rd",
      "Port 2 > RetroPad Right Analog Down",
      "RetroPad Right Analog Down",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p2_retropad_analog_rl",
      "Port 2 > RetroPad Right Analog Left",
      "RetroPad Right Analog Left",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p2_retropad_analog_rr",
      "Port 2 > RetroPad Right Analog Right",
      "RetroPad Right Analog Right",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "---"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};

struct retro_core_options_v2 options_us = {
   option_cats_us,
   option_defs_us
};

/*
 ********************************
 * Language Mapping
 ********************************
*/

#ifndef HAVE_NO_LANGEXTRA
struct retro_core_options_v2 *options_intl[RETRO_LANGUAGE_LAST] = {
   &options_us, /* RETRO_LANGUAGE_ENGLISH */
   NULL,        /* RETRO_LANGUAGE_JAPANESE */
   NULL,        /* RETRO_LANGUAGE_FRENCH */
   NULL,        /* RETRO_LANGUAGE_SPANISH */
   NULL,        /* RETRO_LANGUAGE_GERMAN */
   NULL,        /* RETRO_LANGUAGE_ITALIAN */
   NULL,        /* RETRO_LANGUAGE_DUTCH */
   NULL,        /* RETRO_LANGUAGE_PORTUGUESE_BRAZIL */
   NULL,        /* RETRO_LANGUAGE_PORTUGUESE_PORTUGAL */
   NULL,        /* RETRO_LANGUAGE_RUSSIAN */
   NULL,        /* RETRO_LANGUAGE_KOREAN */
   NULL,        /* RETRO_LANGUAGE_CHINESE_TRADITIONAL */
   NULL,        /* RETRO_LANGUAGE_CHINESE_SIMPLIFIED */
   NULL,        /* RETRO_LANGUAGE_ESPERANTO */
   NULL,        /* RETRO_LANGUAGE_POLISH */
   NULL,        /* RETRO_LANGUAGE_VIETNAMESE */
   NULL,        /* RETRO_LANGUAGE_ARABIC */
   NULL,        /* RETRO_LANGUAGE_GREEK */
   NULL,        /* RETRO_LANGUAGE_TURKISH */
   NULL,        /* RETRO_LANGUAGE_SLOVAK */
   NULL,        /* RETRO_LANGUAGE_PERSIAN */
   NULL,        /* RETRO_LANGUAGE_HEBREW */
   NULL,        /* RETRO_LANGUAGE_ASTURIAN */
   NULL,        /* RETRO_LANGUAGE_FINNISH */
};
#endif

/*
 ********************************
 * Functions
 ********************************
*/

/* Handles configuration/setting of core options.
 * Should be called as early as possible - ideally inside
 * retro_set_environment(), and no later than retro_load_game()
 * > We place the function body in the header to avoid the
 *   necessity of adding more .c files (i.e. want this to
 *   be as painless as possible for core devs)
 */

static INLINE void libretro_set_core_options(retro_environment_t environ_cb,
      bool *categories_supported)
{
   unsigned version  = 0;
#ifndef HAVE_NO_LANGEXTRA
   unsigned language = 0;
#endif

   if (!environ_cb || !categories_supported)
      return;

   *categories_supported = false;

   if (!environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version))
      version = 0;

   if (version >= 2)
   {
#ifndef HAVE_NO_LANGEXTRA
      struct retro_core_options_v2_intl core_options_intl;

      core_options_intl.us    = &options_us;
      core_options_intl.local = NULL;

      if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
          (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH))
         core_options_intl.local = options_intl[language];

      *categories_supported = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL,
            &core_options_intl);
#else
      *categories_supported = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2,
            &options_us);
#endif
   }
   else
   {
      size_t i, j;
      size_t option_index              = 0;
      size_t num_options               = 0;
      struct retro_core_option_definition
            *option_v1_defs_us         = NULL;
#ifndef HAVE_NO_LANGEXTRA
      size_t num_options_intl          = 0;
      struct retro_core_option_v2_definition
            *option_defs_intl          = NULL;
      struct retro_core_option_definition
            *option_v1_defs_intl       = NULL;
      struct retro_core_options_intl
            core_options_v1_intl;
#endif
      struct retro_variable *variables = NULL;
      char **values_buf                = NULL;

      /* Determine total number of options */
      while (true)
      {
         if (option_defs_us[num_options].key)
            num_options++;
         else
            break;
      }

      if (version >= 1)
      {
         /* Allocate US array */
         option_v1_defs_us = (struct retro_core_option_definition *)
               calloc(num_options + 1, sizeof(struct retro_core_option_definition));

         /* Copy parameters from option_defs_us array */
         for (i = 0; i < num_options; i++)
         {
            struct retro_core_option_v2_definition *option_def_us = &option_defs_us[i];
            struct retro_core_option_value *option_values         = option_def_us->values;
            struct retro_core_option_definition *option_v1_def_us = &option_v1_defs_us[i];
            struct retro_core_option_value *option_v1_values      = option_v1_def_us->values;

            option_v1_def_us->key           = option_def_us->key;
            option_v1_def_us->desc          = option_def_us->desc;
            option_v1_def_us->info          = option_def_us->info;
            option_v1_def_us->default_value = option_def_us->default_value;

            /* Values must be copied individually... */
            while (option_values->value)
            {
               option_v1_values->value = option_values->value;
               option_v1_values->label = option_values->label;

               option_values++;
               option_v1_values++;
            }
         }

#ifndef HAVE_NO_LANGEXTRA
         if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
             (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH) &&
             options_intl[language])
            option_defs_intl = options_intl[language]->definitions;

         if (option_defs_intl)
         {
            /* Determine number of intl options */
            while (true)
            {
               if (option_defs_intl[num_options_intl].key)
                  num_options_intl++;
               else
                  break;
            }

            /* Allocate intl array */
            option_v1_defs_intl = (struct retro_core_option_definition *)
                  calloc(num_options_intl + 1, sizeof(struct retro_core_option_definition));

            /* Copy parameters from option_defs_intl array */
            for (i = 0; i < num_options_intl; i++)
            {
               struct retro_core_option_v2_definition *option_def_intl = &option_defs_intl[i];
               struct retro_core_option_value *option_values           = option_def_intl->values;
               struct retro_core_option_definition *option_v1_def_intl = &option_v1_defs_intl[i];
               struct retro_core_option_value *option_v1_values        = option_v1_def_intl->values;

               option_v1_def_intl->key           = option_def_intl->key;
               option_v1_def_intl->desc          = option_def_intl->desc;
               option_v1_def_intl->info          = option_def_intl->info;
               option_v1_def_intl->default_value = option_def_intl->default_value;

               /* Values must be copied individually... */
               while (option_values->value)
               {
                  option_v1_values->value = option_values->value;
                  option_v1_values->label = option_values->label;

                  option_values++;
                  option_v1_values++;
               }
            }
         }

         core_options_v1_intl.us    = option_v1_defs_us;
         core_options_v1_intl.local = option_v1_defs_intl;

         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL, &core_options_v1_intl);
#else
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, option_v1_defs_us);
#endif
      }
      else
      {
         /* Allocate arrays */
         variables  = (struct retro_variable *)calloc(num_options + 1,
               sizeof(struct retro_variable));
         values_buf = (char **)calloc(num_options, sizeof(char *));

         if (!variables || !values_buf)
            goto error;

         /* Copy parameters from option_defs_us array */
         for (i = 0; i < num_options; i++)
         {
            const char *key                        = option_defs_us[i].key;
            const char *desc                       = option_defs_us[i].desc;
            const char *default_value              = option_defs_us[i].default_value;
            struct retro_core_option_value *values = option_defs_us[i].values;
            size_t buf_len                         = 3;
            size_t default_index                   = 0;

            values_buf[i] = NULL;

            if (desc)
            {
               size_t num_values = 0;

               /* Determine number of values */
               while (true)
               {
                  if (values[num_values].value)
                  {
                     /* Check if this is the default value */
                     if (default_value)
                        if (strcmp(values[num_values].value, default_value) == 0)
                           default_index = num_values;

                     buf_len += strlen(values[num_values].value);
                     num_values++;
                  }
                  else
                     break;
               }

               /* Build values string */
               if (num_values > 0)
               {
                  buf_len += num_values - 1;
                  buf_len += strlen(desc);

                  values_buf[i] = (char *)calloc(buf_len, sizeof(char));
                  if (!values_buf[i])
                     goto error;

                  strcpy(values_buf[i], desc);
                  strcat(values_buf[i], "; ");

                  /* Default value goes first */
                  strcat(values_buf[i], values[default_index].value);

                  /* Add remaining values */
                  for (j = 0; j < num_values; j++)
                  {
                     if (j != default_index)
                     {
                        strcat(values_buf[i], "|");
                        strcat(values_buf[i], values[j].value);
                     }
                  }
               }
            }

            variables[option_index].key   = key;
            variables[option_index].value = values_buf[i];
            option_index++;
         }

         /* Set variables */
         environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
      }

error:
      /* Clean up */

      if (option_v1_defs_us)
      {
         free(option_v1_defs_us);
         option_v1_defs_us = NULL;
      }

#ifndef HAVE_NO_LANGEXTRA
      if (option_v1_defs_intl)
      {
         free(option_v1_defs_intl);
         option_v1_defs_intl = NULL;
      }
#endif

      if (values_buf)
      {
         for (i = 0; i < num_options; i++)
         {
            if (values_buf[i])
            {
               free(values_buf[i]);
               values_buf[i] = NULL;
            }
         }

         free(values_buf);
         values_buf = NULL;
      }

      if (variables)
      {
         free(variables);
         variables = NULL;
      }
   }
}

#ifdef __cplusplus
}
#endif

#endif
