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
      "speed",
      "Speed",
      "Make the emulator faster. Two kinds, and the difference matters: the fast-forward options cost nothing (identical picture and sound, just less work), while the overclocks change what the game itself computes and can break it."
   },
   {
      "accuracy",
      "Hardware Timing (Experimental)",
      "Make the emulator slower and more like real silicon. The opposite of Speed above: these exist because parts of the machine are modelled as free, which lets some games run too fast. Still being calibrated."
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
      "Render internally at a multiple of the Jaguar's native resolution. Applied when content is loaded; changing it mid-game takes effect on restart. Presentation only: the game-visible framebuffer and all emulation timing are unchanged. Combines with True Color.",
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
      "virtualjaguar_widescreen",
      "Widescreen (Stretch to 16:9)",
      NULL,
      "Report a 16:9 aspect ratio to the frontend instead of the Jaguar's native 4:3, for a cosmetic horizontal stretch -- the console has no wider display mode. Presentation only: the emulated framebuffer is identical either way. Off by default.",
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
      "virtualjaguar_pertitle_defaults",
      "Per-Title Enhancement Defaults",
      NULL,
      "Apply known-safe enhancement presets automatically for recognized games (e.g. internal resolution or true color where a title is verified to benefit). A preset only applies to options you left at their default value; anything you set yourself always wins. Disable for stock behaviour on every title.",
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
      "Apply per-game byte patches from the enhancement database to the loaded cartridge image (game-side fixes that no core option can express). Off by default. Each patch verifies the bytes it expects and writes nothing if they differ, so it cannot corrupt a dump it was not written for. Cartridge content only; takes effect on restart.",
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
   /* Speed category, ordered by what a user should try first: the two
    * free fast-forwards (identical output, less work), then the two
    * overclocks, which change what the game computes and can break it.
    * blit_memo lives here rather than under Video because it belongs to
    * the same free-speedup class -- and because it silently switches off
    * idle-skip, which is undiscoverable from a different category. */
   {
      "virtualjaguar_risc_idle_skip",
      "RISC Idle-Loop Fast-Forward (GPU + DSP)",
      NULL,
      "Fast-forward the GPU and DSP through provably redundant iterations of a wait loop -- the largest single speed-up the core offers (66-87% less DSP interpretation and 60%+ less GPU interpretation on the titles measured). Bit-exact by construction: registers, flags, cycles and instruction count land exactly where interpreting would have left them, so save states, run-ahead and netplay are unaffected. On by default: the corpus sweep behind #708 ran 148 cart images plus 6 CD spot-checks off-vs-on and every single one was byte-identical (framebuffer, audio and savestate hash streams). If a title looks or sounds wrong with it on, turn it off and please report it. IMPORTANT: a non-stock RISC Clock Scale, DRAM Timing, GPU Pipeline Timing or Blit Memoization switches this off entirely, so turning one of those on costs you this speed-up on top of its own cost. The M68K clock scale and Blitter Bus Timing do not affect it.",
      NULL,
      "speed",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "virtualjaguar_blit_memo",
      "Blit Memoization (Per-Title)",
      NULL,
      "Skip blits whose inputs are provably unchanged since an identical earlier blit (some titles re-render the same scene every engine cycle while the player is idle). Output is bit-identical by construction. Enabled per title via the enhancement database; not available for CD content. 'Verify' never skips -- it runs every would-be skip and logs any divergence, for validating new titles. Switches off DSP Idle-Loop Fast-Forward while enabled.",
      NULL,
      "speed",
      {
         { "disabled", "Disabled" },
         { "enabled",  "Enabled" },
         { "verify",   "Verify (debug, no speedup)" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_frameskip",
      "Frameskip",
      NULL,
      "Skip presenting frames to avoid audio buffer under-run (crackling) on hardware too slow to render every frame. 'Auto' skips a frame when the frontend advises an under-run is likely; 'Auto (Threshold)' skips whenever the audio buffer occupancy falls below the chosen percentage (higher = skips earlier and more often). Presentation only: the emulated machine runs every frame in full either way, so save states, run-ahead and netplay are unaffected. Requires frontend support for audio buffer status reporting; without it, all values behave as Disabled.",
      NULL,
      "speed",
      {
         { "disabled",          "Disabled" },
         { "auto",              "Auto" },
         { "auto_threshold_15", "Auto (Threshold 15%)" },
         { "auto_threshold_30", "Auto (Threshold 30%)" },
         { "auto_threshold_45", "Auto (Threshold 45%)" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_frameskip_max",
      "Frameskip Maximum",
      NULL,
      "Cap on how many frames in a row Frameskip may skip before one is always presented, so the screen keeps moving even while the audio buffer stays low. Has no effect while Frameskip is disabled.",
      NULL,
      "speed",
      {
         { "1", NULL },
         { "2", NULL },
         { "3", NULL },
         { "4", NULL },
         { NULL, NULL },
      },
      "3"
   },
   {
      "virtualjaguar_enhancement_profile",
      "Enhancement Profile (Per-Title Defaults)",
      NULL,
      "Decide whether the per-title enhancement database may switch on expensive visual enhancements (Internal Resolution 2x, True Color) by default for recognized games. 'Quality' always applies them. 'Performance' never does. 'Auto' applies them on capable hardware, but suppresses them on 32-bit ARM devices and drops them early in a session if the audio buffer reports the machine cannot keep up (the same signal Frameskip uses). Only database-supplied DEFAULTS are affected: any option you set yourself always wins, whatever the profile says.",
      NULL,
      "speed",
      {
         { "auto",        "Auto" },
         { "quality",     "Quality" },
         { "performance", "Performance" },
         { NULL, NULL },
      },
      "auto"
   },
   {
      "virtualjaguar_m68k_clock_scale",
      "M68K Clock Scale (Overclock)",
      NULL,
      "Run the 68000 at a multiple of its stock ~13.3 MHz. An enhancement, not an accuracy fix, and it helps less often than you would think: AvP and Checkered Flag were both measured and neither gained anything (AvP is locked to one frame per 5 fields; Checkered Flag caps itself in software), because most Jaguar games are paced by a field lock or their own frame cap rather than by CPU speed. It may also break titles that depend on stock CPU timing. Timers and bus costs stay at stock speed. Overclocking the 68000 is the safer of the two scales: it does NOT cost you DSP Idle-Loop Fast-Forward. If an overclocked game misbehaves, try the Hardware Timing options in their own category. Report bugs only at 1x.",
      NULL,
      "speed",
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
      "Run the GPU and DSP at a multiple of their stock ~26.6 MHz. An enhancement, not an accuracy fix: extra cycles can lift GPU-bound framerates. Audio pacing and timers stay at stock speed, so nothing pitch-shifts. May break titles that depend on stock RISC timing; if an overclocked game misbehaves, try the Hardware Timing options in their own category. Report bugs only at 1x. READ THIS FIRST: anything other than 1x switches OFF DSP Idle-Loop Fast-Forward, which is the larger speed-up on most titles -- so on a DSP-bound game this option makes you SLOWER overall, not faster. Try idle-skip on its own before reaching for this. The M68K scale does not have that side effect.",
      NULL,
      "speed",
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
      "Charge the GPU and 68000 realistic DRAM access time once they leave their local buses, pacing hardware-timed games (Doom-class) closer to real hardware. Each processor pays only its own costs, so relative CPU/GPU timing is preserved. Still being calibrated. Switches off DSP Idle-Loop Fast-Forward while enabled.",
      NULL,
      "accuracy",
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
      "Model the GPU's real instruction costs: the single external-memory gateway, the register score-board and ALU interlocks. The emulated GPU otherwise finishes renders 2-4x faster than silicon, which makes loops paced on render completion (Doom's menus and demo, Hover Strike) run too fast. Still being calibrated. Switches off DSP Idle-Loop Fast-Forward while enabled.",
      NULL,
      "accuracy",
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
      "accuracy",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_netlink",
      "Network Link",
      NULL,
      "How this console's serial port reaches another player. 'Automatic' uses your frontend's netplay session when one is running -- nothing to configure -- and otherwise stays idle. 'TCP Host'/'TCP Client' link two emulators directly without netplay; the client picks a host below, and LAN hosts are found automatically. 'Loopback' echoes back to this console, for testing link-detect menus with no partner.",
      NULL,
      "network",
      {
         { "auto",       "Automatic (use netplay when available)" },
         { "disabled",   "Off" },
         { "loopback",   "Loopback (echo to self)" },
         { "tcp_server", "TCP Host (listen)" },
         { "tcp_client", "TCP Client (connect)" },
         { NULL, NULL },
      },
      "auto"
   },
   {
      "virtualjaguar_uart_device",
      "Network Link Device",
      NULL,
      "What is plugged into the serial port. 'JagLink / CatBox' is the raw cable used by BattleSphere, AirCars and Doom. 'Voice Modem' emulates the Jaguar Voice Modem for Ultra Vortek's phone-line versus mode: type 911 on the numpad at the title screen, then one player dials any number and the other answers -- the call rides the Network Link transport selected above.",
      NULL,
      "network",
      {
         { "jaglink",    "JagLink / CatBox (raw cable)" },
         { "voicemodem", "Voice Modem (Ultra Vortek)" },
         { NULL, NULL },
      },
      "jaglink"
   },
   {
      "virtualjaguar_netlink_host",
      "Network Link Host (TCP Client)",
      NULL,
      "Which host to connect to. Hosts running on your LAN appear here automatically within a couple of seconds. 'From file' reads <system>/vj_netlink.txt: one line, the address only, no port -- for example '192.168.1.42' or 'myhost.local'. The port comes from 'Network Link Port'. The VJ_NETLINK_HOST environment variable overrides this option.",
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
      "virtualjaguar_netlink_speed",
      "Network Link Wire Speed (Enhancement)",
      NULL,
      "Clocks the emulated serial port faster than real hardware, so a link game's lockstep exchange finishes inside one video frame instead of spilling into the next -- at authentic speed (Ultra Vortek's Voice Modem mode settles at 19200 baud, about 5.8 ms of wire time each way per frame) you do not see your own move until the round trip completes. A real Voice Modem or JagLink cable is exactly that slow, which is why this stays an opt-out enhancement rather than a fix. 'Auto' (the default) has the two consoles agree the speed-up between themselves at link-up: nothing to match by hand, and if the peer runs an older core, is not in Auto, or never answers, this side quietly stays at authentic timing instead of running ahead alone. Only takes effect over a direct Network Link (TCP host/client): frontend netplay has no channel for the two cores to negotiate over and always runs authentic timing. If a game starts dropping link data, turn this off.",
      NULL,
      "network",
      {
         { "disabled", "Off (authentic hardware timing)" },
         { "auto",     "Auto (negotiated with peer)" },
         { NULL, NULL },
      },
      "auto"
   },
   {
      "virtualjaguar_voice_chat",
      "Voice Chat (Host-Side)",
      NULL,
      "Opt-in voice channel over the Network Link -- the Jaguar Voice Modem's real selling point of simultaneous voice and data. NOT emulation: voice never entered the Jaguar, which only issued audio-path control words. Capture uses the frontend microphone API where available. Off by default so mic capture never starts unasked. Works over TCP Host/Client and over RetroArch netplay when both sides enable the option (auto-negotiated; falls back to data-only if the peer never confirms).",
      NULL,
      "network",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_voice_chat_gate",
      "Voice Chat Transmit Gate",
      NULL,
      "'Open mic' transmits whenever the mic energy exceeds the VAD threshold (works on every frontend including mobile). 'Push to talk' transmits only while the configured keyboard key is held (desktop-oriented -- no RetroPad button is free).",
      NULL,
      "network",
      {
         { "open_mic",     "Open mic (VAD)" },
         { "push_to_talk", "Push to talk (keyboard)" },
         { NULL, NULL },
      },
      "open_mic"
   },
   {
      "virtualjaguar_voice_chat_ptt_key",
      "Voice Chat Push-to-Talk Key",
      NULL,
      "Keyboard key that opens the mic in Push-to-talk mode. Keys already claimed by the Jaguar keypad mapping are omitted.",
      NULL,
      "network",
      {
         { "v",     "V" },
         { "c",     "C" },
         { "space", "Space" },
         { "tab",   "Tab" },
         { "lctrl", "Left Ctrl" },
         { "grave", "Backquote (`)" },
         { NULL, NULL },
      },
      "v"
   },
   {
      "virtualjaguar_voice_chat_volume",
      "Voice Chat Volume",
      NULL,
      "Far-end (and optional local monitor) mix level into the game audio. Kept conservative by default so voice does not clip the DAC mix.",
      NULL,
      "network",
      {
         { "25",  "25%" },
         { "50",  "50%" },
         { "75",  "75%" },
         { "100", "100%" },
         { NULL, NULL },
      },
      "50"
   },
   {
      "virtualjaguar_voice_chat_vad",
      "Voice Chat VAD Threshold",
      NULL,
      "Absolute-average energy gate for Open-mic mode. Raise if background noise keys the mic; lower if soft speech is cut off.",
      NULL,
      "network",
      {
         { "200",  "Low (200)" },
         { "400",  "Medium (400)" },
         { "800",  "High (800)" },
         { "1600", "Very high (1600)" },
         { NULL, NULL },
      },
      "400"
   },
   {
      "virtualjaguar_voice_chat_monitor",
      "Voice Chat Local Monitor",
      NULL,
      "Mix your own mic into the local audio output for a mic check. Does not change what is sent to the peer.",
      NULL,
      "network",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_cd_trace",
      "CD Trace (Diagnostic)",
      NULL,
      "Record CD command/response traffic and seek/FIFO transitions to a bounded ring buffer, dumped to the RetroArch log when the cd_seek_wedge watchdog fires. For troubleshooting Jaguar CD boot and data-transfer bugs, not for normal play. Can also be forced on headlessly with the VJ_CD_TRACE=1 environment variable.",
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
      "Which BIOS a CARTRIDGE boots with. 'HLE' has the core emulate the BIOS setup and services itself: most commercial titles boot faster and the boot animation is skipped. 'Real' runs the actual Jaguar boot ROM, which some titles require. Both boot ROM images are built into the core, so neither setting needs a file. GPU-only/jagcrypt carts (BootIntro demos) turn the real boot ROM on even when this is set to HLE -- they contain no 68K program for HLE to start. Ignored for CD content: 'CD Boot Mode' decides there.",
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
      "virtualjaguar_bios_type",
      "Cart BIOS Type (Restart)",
      NULL,
      "Which console boot ROM a CARTRIDGE uses when 'BIOS (Cartridges)' is Real, or when a GPU-only/jagcrypt cart turns the boot ROM on. 'Series K' is the original Jaguar; 'Model M' is the later revision (patch address $4804) most size-coded BootIntros are built for. Both are built into the core. 'Custom' loads a 128 KB image from the system directory (jagboot.rom, boot.rom, boot0.rom, or a named '[BIOS] Atari Jaguar...' file), identified by checksum and logged, falling back to Series K if none is found. A jagboot_m.rom in the system directory replaces the built-in Model M image. Ignored for CD content.",
      NULL,
      "bios_boot",
      {
         { "k", "Series K" },
         { "m", "Model M" },
         { "custom", "Custom (external file)" },
         { NULL, NULL },
      },
      "k"
   },
   {
      "virtualjaguar_texture_dump",
      "Texture Dump Mode",
      NULL,
      "Write every unique blitter source tile the title uses to <system dir>/vj_texdump/<cart CRC32>/ as a PNG preview plus a manifest row, for HD texture pack authoring. Tiles are identified by a hash of their raw source bytes; the palette is advisory metadata, never identity. Takes effect immediately, no restart needed. Developer-facing: leave disabled for normal play.",
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
      "virtualjaguar_gdb_stub",
      "GDB Debug Stub (Restart)",
      NULL,
      "Open a GDB remote debugging server on localhost so a debugger can inspect the emulated machine. Developer-facing; leave disabled for normal play. By default the server listens only on 127.0.0.1 and is not reachable from another machine; 'GDB Stub: Network Binding' can widen that to your local network, with the security consequences described there. Requires a restart.",
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
      "virtualjaguar_gdb_bind",
      "GDB Stub: Network Binding (Debug)",
      NULL,
      "Which addresses the GDB stub will accept debugger connections from. 'Loopback' (default) accepts only connections from this same machine -- on a phone, tablet or TV that means nothing outside the device can ever reach it. 'LAN' additionally accepts connections from your local network, so you can debug a game running on another device from your computer. SECURITY: the GDB protocol has NO authentication of any kind. While the stub is open, anyone who can reach the port can read and write the emulated machine's memory and control its execution. Only use 'LAN' on a network you trust, only while you are actually debugging, and turn it back off afterwards. Connections from public (non-private) addresses are refused and logged even in 'LAN' mode. Has no effect unless GDB Stub is enabled. Takes effect on content load.",
      NULL,
      "diagnostics",
      {
         { "loopback", "Loopback (this machine only)" },
         { "lan",      "LAN (local network -- see warning)" },
         { NULL, NULL },
      },
      "loopback"
   },
   {
      "virtualjaguar_gdb_port",
      "GDB Stub Port (Restart)",
      NULL,
      "TCP port for the GDB debug stub. Change this only if another program already uses the default. Requires a restart.",
      NULL,
      "diagnostics",
      {
         { "2345", NULL },
         { "2346", NULL },
         { "2347", NULL },
         { "3333", NULL },
         { NULL, NULL },
      },
      "2345"
   },
   {
      "virtualjaguar_gdb_wait",
      "GDB Stub: Halt At Boot (Restart)",
      NULL,
      "Halt the 68000 before its very first instruction and wait for a GDB client to attach, so a boot-time fault can be debugged instead of running to completion before you connect. Only takes effect while the GDB Debug Stub option above is enabled. Requires a restart.",
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
      "virtualjaguar_gdb_halt_timeout",
      "GDB Stub: Halt Timeout",
      NULL,
      "If the machine is halted at a breakpoint with no client activity for this long, resume automatically and log it loudly, so a forgotten debug session does not look like a hang forever. 'Off' means a halt waits indefinitely -- the default, because silently resuming a debugged machine is worse than a freeze for the developers this option is for.",
      NULL,
      "diagnostics",
      {
         { "off", NULL },
         { "30",  "30 seconds" },
         { "60",  "60 seconds" },
         { "300", "5 minutes" },
         { NULL, NULL },
      },
      "off"
   },
   {
      "virtualjaguar_texdump_16bpp",
      "Texture Dump: 16bpp Preview",
      NULL,
      "How 16-bit source tiles are rendered in their preview PNGs. The blitter cannot know whether 16-bit values are CRY or RGB16 -- that is display-time interpretation -- so this only changes the preview image, never the tile's hash. 'Both' writes a -cry and a -rgb PNG per tile.",
      NULL,
      "diagnostics",
      {
         { "cry",  "CRY" },
         { "rgb",  "RGB16" },
         { "both", "Both" },
         { NULL, NULL },
      },
      "cry"
   },
   {
      "virtualjaguar_texture_replace",
      "Texture Replacement",
      NULL,
      "Present community texture-pack art in place of the title's own blitter tiles. Packs live in <system dir>/vj_texpacks/<cart CRC32>/, named by the same hashes Texture Dump Mode writes. Presentation only: the emulated machine, save states and netplay are bit-identical with or without a pack. Shown only when a pack directory exists for the loaded title.",
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
      "virtualjaguar_jgd",
      "Jaguar GameDrive (Restart)",
      NULL,
      "Emulate the Jaguar GameDrive (JagGD) flash cartridge: its detection/install interface and 1 MB bank switching over up to 16 MB of cart SDRAM. 'Auto' turns it on only for ROM images larger than the 6 MB cartridge window. 'Enabled' forces it on for smaller images too, for GD-locked homebrew that refuses to boot without the cart (BigPEmu calls this Force JGD). Without it, GD-locked titles hang at boot exactly as on a stock console.",
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
      "Which CD BIOS the real-BIOS boot path uses. 'Retail' is the standard consumer BIOS; 'Developer' is the dev-kit BIOS, which applies less strict disc checks and can boot images the retail BIOS refuses. Both are built into the core, so no files are required; a CD BIOS ROM file in the system directory is preferred over the built-in image, and this setting picks which file wins when both types are present. Only has an effect when 'CD Boot Mode' is 'Real BIOS' or 'Auto' -- the HLE boot path never runs a CD BIOS.",
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
      "How Jaguar CD discs boot. OVERRIDES the 'BIOS (Cartridges)' setting for CD content. 'HLE' emulates the CD BIOS services directly with the console boot ROM off -- fastest and the most broadly compatible. 'Real BIOS' runs an actual CD BIOS with the boot ROM on: more faithful, and verified clean across all 5 tested FMV titles (Dragon's Lair, Space Ace, BrainDead 13, Blue Lightning, Highlander) in 15,000-frame probes. It prefers a CD BIOS ROM file from the system directory (several common names and the usual Jaguar / Jaguar CD sub-folders are searched) and otherwise uses the built-in image chosen by 'CD BIOS Type', so no files are required. 'Auto' is currently identical to 'Real BIOS'. If no CD BIOS can be staged at all, the core falls back to HLE rather than failing. Audio-only (Red Book) CDs always use the real BIOS regardless of this setting, since HLE has no game code to boot from.",
      NULL,
      "cdrom",
      {
         { "hle",  "HLE (Recommended)" },
         { "auto", "Auto (Real BIOS)" },
         { "bios", "Real BIOS (Included)" },
         { NULL, NULL },
      },
      "hle"
   },
   {
      "virtualjaguar_cd_read_speed",
      "CD Read Speed (HLE Boot Mode Only)",
      NULL,
      "Data-transfer rate for Jaguar CD reads in HLE boot mode. '2x' matches the real drive (300 KB/s) and is hardware-accurate. Higher speeds shorten load times but may break titles that pace code overlays, music cues or load handshakes off the drive rate; 'Instant' completes each read in one tick and is the most likely to hang. Real-BIOS boot always uses the accurate rate. Applied per read: a transfer already in flight keeps the speed it started with.",
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
      "Which peripheral is plugged into controller port 2. " "'6D Controller' is Atari's unreleased six-degrees-of-freedom controller from the Technical Reference V10 -- three translations and three rotations, seven buttons and a Rezero control, over three banks. NO SOFTWARE ANYWHERE READS IT: the device was never shipped and this is a best attempt from the manual alone, unvalidated against any real program. Left stick translates left/right and up/down, right stick yaws and pitches, the L2/R2 triggers are fore/aft thrust and the L/R shoulders roll; A/B/C/D are the usual four face buttons, E/F are the stick clicks, and G/Rezero are Start/Select. Like the other bank-switching types the port stays a RetroPad until an axis actually moves. Note the real controller has NO Pause and NO Option button -- on hardware those come from a joypad plugged into the controller's own passthrough, which has no emulated equivalent, so both are unreachable while it is engaged. If you try this, please report what you find on the issue tracker. " "'Pro Controller' is the retail six-button pad: its X/Y/Z fire buttons and Left/Right shoulder buttons alias onto keypad 9/8/7/4/6 (Atari's own SDK header and developer newsletter, docs/teamtap-procontroller-spike.md section 9 -- the TR10 manual never mentions the device, because there is nothing new for it to document). Selecting this only changes which five RetroPad buttons update those five keypad slots; the port is still an ordinary RetroPad otherwise. Because the aliasing is real hardware behaviour, a title that reads its own keypad -- weapon select, level codes, menu shortcuts -- sees genuine keypad presses from X/Y/Z/L1/R1 while this is selected, so leave it on 'Standard Joypad' unless a game specifically wants the Pro Controller. No detection method was ever published, so no title can be confirmed to require it; see docs/input-devices-user-guide.md. " "'Team Tap (4-player adaptor)' is Atari's four-socket adapter: the pad you already use on this port stays as socket 0, and three more pads appear on RetroArch ports 6, 7 and 8. Everything behind the adapter is an ordinary Jaguar joypad -- the adapter rewrites the row codes so the pads never know it is there -- and titles detect it by reading socket 3, which is the one bit this adds. Known retail support is two titles: White Men Can't Jump, which needs it for 3 and 4 player games, and NBA Jam T.E., where it is optional; homebrew support is unestablished. It is inert for every other title, so leave it off unless you are playing one. Per-port button remapping and 'Numpad to Keyboard' apply to socket 0 only, so remap the extra pads from RetroArch's own Controls menu. " "'Atari ST / PS2 Mouse' is the wiring used by the AtariAge and Brewing Academy ST adapters and by PS/2 mouse adapters. 'Amiga Mouse (ST adapter)' is an Amiga mouse plugged into an ST-wired adapter -- this is what an in-game 'Atari / Amiga' selector normally chooses between. 'Amiga Mouse (Amiga adapter)' is the rarer dedicated adapter. A mouse asserts its state in every row scan, exactly as the real row-blind adapter does, so the port-2 RetroPad is disconnected while one is selected. 'Rotary (Tempest)' is the Tempest spinner: it removes Up and Down and reports wheel rotation on Left/Right instead, and is driven by relative mouse X. Its buttons stay on the RetroPad. Tempest 2000 hides its rotary support behind an unlock -- from SELECT GAME TYPE TO PLAY press Option on controller 1, then press Pause on BOTH controllers at once to reveal CONTROLLER TYPE. The unlock is saved to the game's EEPROM, so it is only needed once. 'Analog Joystick' and 'Driving Controller' are Atari's bank-switching analog device (one protocol, two skins) -- NO RELEASED TITLE reads it, so these exist for homebrew. Driven by the left analog stick (the driving skin also takes the L2/R2 triggers as brake/accelerator); the port stays a RetroPad until the stick actually moves, so a game that probes controller types at boot only sees the analog device if the stick is deflected first. 'Analog Stick (paddle ADC)' is a DIFFERENT device: the 8-bit converter fitted to early Jaguar motherboards, which production consoles do not have. It is the one analog interface a released game reads -- BattleSphere and BattleSphere Gold, which also need their own Gameplay Options > 2nd Controller set to Analog Stick. Driven by the left analog stick, and unlike the bank-switching types it leaves the RetroPad fully connected, because the stick's potentiometers are separate pins from the buttons. Leave it off unless a game asks for it: with no paddle selected the emulated console reports no converter fitted, exactly as real hardware does.",
      NULL,
      "input_p2",
      {
         { "auto",                "Auto (per-title default)" },
         { "pad",                 "Standard Joypad" },
         { "teamtap",             "Team Tap (4-player adaptor)" },
         { "pad_pro",             "Pro Controller (6-button)" },
         { "mouse_st",            "Atari ST / PS2 Mouse" },
         { "mouse_amiga",         "Amiga Mouse (ST adapter)" },
         { "mouse_amiga_adapter", "Amiga Mouse (Amiga adapter)" },
         { "rotary",              "Rotary (Tempest)" },
         { "analog",              "Analog Joystick (bank-switching)" },
         { "driving",             "Driving Controller (bank-switching)" },
         { "paddle",              "Analog Stick (paddle ADC)" },
         { "6d",                  "6D Controller (bank-switching)" },
         { NULL, NULL },
      },
      "auto"
   },
   {
      "virtualjaguar_p1_device",
      "Port 1 > Controller Type",
      "Controller Type",
      "Which peripheral is plugged into controller port 1. " "'6D Controller' is Atari's unreleased six-degrees-of-freedom controller from the Technical Reference V10 -- three translations and three rotations, seven buttons and a Rezero control, over three banks. NO SOFTWARE ANYWHERE READS IT: the device was never shipped and this is a best attempt from the manual alone, unvalidated against any real program. Left stick translates left/right and up/down, right stick yaws and pitches, the L2/R2 triggers are fore/aft thrust and the L/R shoulders roll; A/B/C/D are the usual four face buttons, E/F are the stick clicks, and G/Rezero are Start/Select. Like the other bank-switching types the port stays a RetroPad until an axis actually moves. Note the real controller has NO Pause and NO Option button -- on hardware those come from a joypad plugged into the controller's own passthrough, which has no emulated equivalent, so both are unreachable while it is engaged. If you try this, please report what you find on the issue tracker. " "'Pro Controller' is the retail six-button pad: its X/Y/Z fire buttons and Left/Right shoulder buttons alias onto keypad 9/8/7/4/6 (Atari's own SDK header and developer newsletter, docs/teamtap-procontroller-spike.md section 9 -- the TR10 manual never mentions the device, because there is nothing new for it to document). Selecting this only changes which five RetroPad buttons update those five keypad slots; the port is still an ordinary RetroPad otherwise. Because the aliasing is real hardware behaviour, a title that reads its own keypad -- weapon select, level codes, menu shortcuts -- sees genuine keypad presses from X/Y/Z/L1/R1 while this is selected, so leave it on 'Standard Joypad' unless a game specifically wants the Pro Controller. No detection method was ever published, so no title can be confirmed to require it; see docs/input-devices-user-guide.md. " "'Team Tap (4-player adaptor)' is Atari's four-socket adapter: the pad you already use on this port stays as socket 0, and three more pads appear on RetroArch ports 3, 4 and 5 -- so with one Team Tap on port 1 your four players are on RetroArch ports 1, 3, 4 and 5. Everything behind the adapter is an ordinary Jaguar joypad -- the adapter rewrites the row codes so the pads never know it is there -- and titles detect it by reading socket 3, which is the one bit this adds. Known retail support is two titles: White Men Can't Jump, which needs it for 3 and 4 player games, and NBA Jam T.E., where it is optional; homebrew support is unestablished. It is inert for every other title, so leave it off unless you are playing one. Per-port button remapping and 'Numpad to Keyboard' apply to socket 0 only, so remap the extra pads from RetroArch's own Controls menu. " "'Rotary (Tempest)' is the Tempest spinner: it removes Up and Down and reports wheel rotation on Left/Right instead, and is driven by relative mouse X. Its buttons (A, B, C, Option, Pause and the keypad) stay on the RetroPad, which is what a real rotary has. 'Light Gun' is the port-1 light gun: the Jaguar wires its LP pin to port 1 only, so it is not offered on port 2. Aim with whatever your frontend maps to the light gun (mouse or Wiimote); the trigger reports as the Jaguar's B button, which is what Balloons reads, and Aux A / Aux B / Start / Select reach A / C / Option / Pause. Aiming off-screen stops the aim updating, exactly as a real gun stops seeing the beam. 'Analog Joystick' and 'Driving Controller' are Atari's bank-switching analog device (one protocol, two skins) -- NO RELEASED TITLE reads it, so these exist for homebrew. Driven by the left analog stick (the driving skin also takes the L2/R2 triggers as brake/accelerator); the port stays a RetroPad until the stick actually moves, so a game that probes controller types at boot only sees the analog device if the stick is deflected first. 'Analog Stick (paddle ADC)' is a DIFFERENT device: the 8-bit converter fitted to early Jaguar motherboards, which production consoles do not have. It is the one analog interface a released game reads, though the known consumer (BattleSphere) uses port 2 for it. Driven by the left analog stick, and unlike the bank-switching types it leaves the RetroPad fully connected, because the stick's potentiometers are separate pins from the buttons. Leave it off unless a game asks for it: with no paddle selected the emulated console reports no converter fitted, exactly as real hardware does. There is no per-title default for any of these and there never will be -- selecting a rotary removes Up and Down and a gun repurposes B, so either would break the controls of anyone using a pad. Tempest 2000 hides its rotary support behind an unlock -- from SELECT GAME TYPE TO PLAY press Option on controller 1, then press Pause on BOTH controllers at once to reveal CONTROLLER TYPE. The unlock is saved to the game's EEPROM, so it is only needed once.",
      NULL,
      "input_p1",
      {
         { "auto",     "Auto (per-title default)" },
         { "pad",      "Standard Joypad" },
         { "teamtap",  "Team Tap (4-player adaptor)" },
         { "pad_pro",  "Pro Controller (6-button)" },
         { "rotary",   "Rotary (Tempest)" },
         { "lightgun", "Light Gun" },
         { "analog",   "Analog Joystick (bank-switching)" },
         { "driving",  "Driving Controller (bank-switching)" },
         { "paddle",   "Analog Stick (paddle ADC)" },
         { "6d",       "6D Controller (bank-switching)" },
         { NULL, NULL },
      },
      "auto"
   },
   {
      "virtualjaguar_rotary_sensitivity",
      "Rotary Sensitivity",
      "Rotary Sensitivity",
      "Scales spinner movement before it is converted to quadrature pulses. The emulated encoder can only emit one pulse per controller poll of its row, so raising this past what the game's poll rate can carry adds lag rather than speed.",
      NULL,
      "input",
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
      "virtualjaguar_rotary_id",
      "Rotary Reports Controller Type",
      "Rotary Reports Controller Type",
      "Whether an emulated rotary identifies itself to software as a rotary (diode D23 fitted). Most rotary controllers ever built shipped without the diode and identify as a standard joypad, which is the default here. Tempest 2000 does not read this -- it uses its own CONTROLLER TYPE menu instead.",
      NULL,
      "input",
      {
         { "joypad", "Standard Joypad (no diode -- as most real units)" },
         { "rotary", "Tempest Rotary (diode fitted)" },
         { NULL, NULL },
      },
      "joypad"
   },
   /* Per-axis rotary tuning (#439).  A rotary is a single wheel, so there
    * is one axis and no X/Y split; the arithmetic is the same shared layer
    * the mouse uses (src/jerry/axistune.c).  Defaults are the identity.
    *
    * These are SHARED BY BOTH PORTS, exactly as Rotary Sensitivity already
    * is: two rotaries cannot be tuned independently.  That is a deliberate
    * carry-forward of the existing shape rather than an omission -- the
    * rotary options live in the un-prefixed "input" category precisely
    * because the device is offered on either port. */
   {
      "virtualjaguar_rotary_deadzone",
      "Rotary Dead Zone",
      "Rotary Dead Zone",
      "Discards spinner movement at or below this many host units per poll. A noise gate for a jittery source; movement above the threshold passes at full size.",
      NULL,
      "input",
      {
         { "0", "Off" },
         { "1", "1 unit" },
         { "2", "2 units" },
         { "3", "3 units" },
         { "4", "4 units" },
         { "6", "6 units" },
         { "8", "8 units" },
         { NULL, NULL },
      },
      "0"
   },
   {
      "virtualjaguar_rotary_offset",
      "Rotary Offset",
      "Rotary Offset",
      "Subtracts a constant from every spinner sample. Cancels a source that reports a small non-zero movement while at rest, which would otherwise spin the knob forever with the controls untouched. Applied in host orientation, before the wheel's direction convention.",
      NULL,
      "input",
      {
         { "-4", "-4" },
         { "-3", "-3" },
         { "-2", "-2" },
         { "-1", "-1" },
         { "0",  "Off" },
         { "1",  "+1" },
         { "2",  "+2" },
         { "3",  "+3" },
         { "4",  "+4" },
         { NULL, NULL },
      },
      "0"
   },
   {
      "virtualjaguar_rotary_exponent",
      "Rotary Response Curve",
      "Rotary Response Curve",
      "Response exponent for the spinner, giving finer control at low speed. The curve is anchored at 64 units per poll: below that an exponent above 1.00 attenuates, at and above it movement passes through unchanged. A higher exponent therefore makes the spinner SLOWER overall -- raise Rotary Sensitivity to get the top speed back.",
      NULL,
      "input",
      {
         { "100", "Linear (1.00)" },
         { "125", "1.25" },
         { "150", "1.50" },
         { "175", "1.75" },
         { "200", "2.00" },
         { "250", "2.50" },
         { "300", "3.00" },
         { NULL, NULL },
      },
      "100"
   },
   /* Analog / driving controller tuning (#437).  SHARED BY BOTH PORTS,
    * like the rotary ladder, and applied through the same shared layer
    * (src/jerry/axistune.c) -- but on an ABSOLUTE axis, so the units are
    * ADC counts (127 = full stick deflection, the device's own 8-bit
    * domain) and the dead zone RE-BASES instead of gating: positions
    * inside it read as centred, the edge maps smoothly to centre, and
    * full deflection still reads full scale (see axistune.h, "THE
    * ABSOLUTE-AXIS ANSWER").  Defaults are the exact identity. */
   {
      "virtualjaguar_analog_deadzone_x",
      "Analog Controller Dead Zone (X)",
      "Analog Dead Zone (X)",
      "Stick positions within this many ADC counts of centre (127 = full deflection) read as exactly centred. The rest of the travel is rescaled so the response is smooth at the edge and full deflection still reads full scale.",
      NULL,
      "input",
      {
         { "0",  "Off" },
         { "4",  "4 counts (~3%)" },
         { "8",  "8 counts (~6%)" },
         { "12", "12 counts (~9%)" },
         { "16", "16 counts (~13%)" },
         { "24", "24 counts (~19%)" },
         { "32", "32 counts (~25%)" },
         { NULL, NULL },
      },
      "0"
   },
   {
      "virtualjaguar_analog_deadzone_y",
      "Analog Controller Dead Zone (Y)",
      "Analog Dead Zone (Y)",
      "As Analog Controller Dead Zone (X), for the Y axis (pitch, or accelerator/brake on the driving controller).",
      NULL,
      "input",
      {
         { "0",  "Off" },
         { "4",  "4 counts (~3%)" },
         { "8",  "8 counts (~6%)" },
         { "12", "12 counts (~9%)" },
         { "16", "16 counts (~13%)" },
         { "24", "24 counts (~19%)" },
         { "32", "32 counts (~25%)" },
         { NULL, NULL },
      },
      "0"
   },
   {
      "virtualjaguar_analog_offset_x",
      "Analog Controller Offset (X)",
      "Analog Offset (X)",
      "Subtracts a constant (in ADC counts) from every X sample, in host orientation before any device convention. Cancels a stick that rests off-centre; a centred stick is moved by it, which is the point.",
      NULL,
      "input",
      {
         { "-16", "-16" },
         { "-8",  "-8" },
         { "-4",  "-4" },
         { "-2",  "-2" },
         { "0",   "Off" },
         { "2",   "+2" },
         { "4",   "+4" },
         { "8",   "+8" },
         { "16",  "+16" },
         { NULL, NULL },
      },
      "0"
   },
   {
      "virtualjaguar_analog_offset_y",
      "Analog Controller Offset (Y)",
      "Analog Offset (Y)",
      "As Analog Controller Offset (X), for the Y axis.",
      NULL,
      "input",
      {
         { "-16", "-16" },
         { "-8",  "-8" },
         { "-4",  "-4" },
         { "-2",  "-2" },
         { "0",   "Off" },
         { "2",   "+2" },
         { "4",   "+4" },
         { "8",   "+8" },
         { "16",  "+16" },
         { NULL, NULL },
      },
      "0"
   },
   {
      "virtualjaguar_analog_exponent_x",
      "Analog Controller Response Curve (X)",
      "Analog Response Curve (X)",
      "Response exponent for the X axis, anchored at full deflection: an exponent above 1.00 gives finer control near centre while full deflection still reads full scale. Unlike the mouse/rotary curves this costs no top speed, so there is no paired sensitivity control.",
      NULL,
      "input",
      {
         { "100", "Linear (1.00)" },
         { "125", "1.25" },
         { "150", "1.50" },
         { "175", "1.75" },
         { "200", "2.00" },
         { "250", "2.50" },
         { "300", "3.00" },
         { NULL, NULL },
      },
      "100"
   },
   {
      "virtualjaguar_analog_exponent_y",
      "Analog Controller Response Curve (Y)",
      "Analog Response Curve (Y)",
      "As Analog Controller Response Curve (X), for the Y axis.",
      NULL,
      "input",
      {
         { "100", "Linear (1.00)" },
         { "125", "1.25" },
         { "150", "1.50" },
         { "175", "1.75" },
         { "200", "2.00" },
         { "250", "2.50" },
         { "300", "3.00" },
         { NULL, NULL },
      },
      "100"
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
   /* Per-axis mouse tuning (#439).  Dead zone and offset are in raw host
    * units per poll; the exponent is a percentage of 1.0, anchored at 64
    * units per poll (quadrature.h's saturation point -- see axistune.h).
    * All defaults are the exact identity, so a user who never opens this
    * menu gets the pre-#439 path unchanged. */
   {
      "virtualjaguar_mouse_deadzone_x",
      "Port 2 > Mouse Dead Zone (X)",
      "Mouse Dead Zone (X)",
      "Discards horizontal mouse movement at or below this many host units per poll. A noise gate for a jittery source (or an analog stick mapped to the mouse); a real mouse reports nothing at rest and needs none. Movement above the threshold passes at full size -- the dead zone drops samples, it does not shrink them.",
      NULL,
      "input_p2",
      {
         { "0", "Off" },
         { "1", "1 unit" },
         { "2", "2 units" },
         { "3", "3 units" },
         { "4", "4 units" },
         { "6", "6 units" },
         { "8", "8 units" },
         { NULL, NULL },
      },
      "0"
   },
   {
      "virtualjaguar_mouse_deadzone_y",
      "Port 2 > Mouse Dead Zone (Y)",
      "Mouse Dead Zone (Y)",
      "As Mouse Dead Zone (X), for vertical movement.",
      NULL,
      "input_p2",
      {
         { "0", "Off" },
         { "1", "1 unit" },
         { "2", "2 units" },
         { "3", "3 units" },
         { "4", "4 units" },
         { "6", "6 units" },
         { "8", "8 units" },
         { NULL, NULL },
      },
      "0"
   },
   {
      "virtualjaguar_mouse_offset_x",
      "Port 2 > Mouse Offset (X)",
      "Mouse Offset (X)",
      "Subtracts a constant from every horizontal sample. Cancels a source that reports a small non-zero movement while at rest -- typically an analog stick mapped to the mouse, which otherwise drifts forever. A real mouse reports exactly zero at rest and is unaffected.",
      NULL,
      "input_p2",
      {
         { "-4", "-4" },
         { "-3", "-3" },
         { "-2", "-2" },
         { "-1", "-1" },
         { "0",  "Off" },
         { "1",  "+1" },
         { "2",  "+2" },
         { "3",  "+3" },
         { "4",  "+4" },
         { NULL, NULL },
      },
      "0"
   },
   {
      "virtualjaguar_mouse_offset_y",
      "Port 2 > Mouse Offset (Y)",
      "Mouse Offset (Y)",
      "As Mouse Offset (X), for vertical movement.",
      NULL,
      "input_p2",
      {
         { "-4", "-4" },
         { "-3", "-3" },
         { "-2", "-2" },
         { "-1", "-1" },
         { "0",  "Off" },
         { "1",  "+1" },
         { "2",  "+2" },
         { "3",  "+3" },
         { "4",  "+4" },
         { NULL, NULL },
      },
      "0"
   },
   {
      "virtualjaguar_mouse_exponent_x",
      "Port 2 > Mouse Response Curve (X)",
      "Mouse Response Curve (X)",
      "Response exponent for horizontal movement, giving finer control at low speed. The curve is anchored at 64 units per poll: below that an exponent above 1.00 attenuates, at and above it movement passes through unchanged. Because ordinary movement is well below 64 units, a higher exponent makes the mouse SLOWER overall -- raise Mouse Sensitivity to get the top speed back. These are two different controls.",
      NULL,
      "input_p2",
      {
         { "100", "Linear (1.00)" },
         { "125", "1.25" },
         { "150", "1.50" },
         { "175", "1.75" },
         { "200", "2.00" },
         { "250", "2.50" },
         { "300", "3.00" },
         { NULL, NULL },
      },
      "100"
   },
   {
      "virtualjaguar_mouse_exponent_y",
      "Port 2 > Mouse Response Curve (Y)",
      "Mouse Response Curve (Y)",
      "As Mouse Response Curve (X), for vertical movement.",
      NULL,
      "input_p2",
      {
         { "100", "Linear (1.00)" },
         { "125", "1.25" },
         { "150", "1.50" },
         { "175", "1.75" },
         { "200", "2.00" },
         { "250", "2.50" },
         { "300", "3.00" },
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
