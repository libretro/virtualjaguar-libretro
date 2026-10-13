/*
 * vj_frontend_ext.h -- private core -> frontend event channel (ABI 1).
 *
 * Shared, verbatim, by the core (C89) and the standalone app (C++17).  It
 * rides on libretro's own environment callback using the id range libretro.h
 * reserves for frontend-internal commands (RETRO_ENVIRONMENT_PRIVATE), so no
 * other frontend ever sees a command it understands.  See
 * docs/app-frontend-extension.md for the full contract.
 *
 * THE RULES
 *
 *   - Opt-in handshake.  Once per retro_init() the core calls VJ_ENV_HELLO.
 *     Only a frontend that knows this header answers true (and fills
 *     caps_out).  RetroArch and every other frontend answers false; the core
 *     then latches the channel off and never calls VJ_ENV_EVENT at all, so
 *     under RetroArch the whole feature costs one branch per would-be event.
 *   - Synchronous, on the core's thread.  Everything is issued from the thread
 *     that called retro_load_game() / retro_run() (the emulation thread), and
 *     the frontend's handler runs inline.  VJ_ENV_EVENT comes from retro_run()
 *     except for the load-time VJ_EVT_BIOS_* events, which come from inside
 *     retro_load_game() (or a disk-control insert).  Nothing is queued, nothing is called
 *     from a helper thread.
 *   - Pointers (struct vj_event.s, struct vj_env_hello.core_version) are valid
 *     for the duration of the call only.  Copy what you keep.
 *   - Events are informational.  Existing log lines are unchanged and remain
 *     the fallback for a frontend that never answered HELLO.
 *   - The frontend's answer to VJ_ENV_EVENT is ignored.
 *
 *   - Queries.  A frontend that sets VJ_CAP_BIOS may also be asked, while the
 *     core stages a BIOS (inside retro_load_game() or a disk-control insert,
 *     never from a helper thread), which BIOS FILE to use (VJ_ENV_GET_BIOS_PATH) and what
 *     a no-content boot should enter (VJ_ENV_GET_BOOT_TARGET).  Both are
 *     optional: a refusal (false, or an empty answer) leaves the core's own
 *     logic -- system-folder search, then the built-in image -- exactly as it
 *     was.  The core validates whatever file it is handed (size, header or
 *     checksum) and falls back the same way if it does not pass.
 *
 * Adding an event type, a command id, a capability bit or a trailing struct
 * field is compatible; anything else bumps VJ_FRONTEND_EXT_ABI.
 */
#ifndef VJ_FRONTEND_EXT_H
#define VJ_FRONTEND_EXT_H

#include <libretro.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VJ_FRONTEND_EXT_ABI 1u

/* Command ids: 0x5600 + n, with the private bit set. */
#define VJ_ENV_BASE   (0x5600 | RETRO_ENVIRONMENT_PRIVATE)
#define VJ_ENV_HELLO  (VJ_ENV_BASE + 0)   /* data: struct vj_env_hello*  */
#define VJ_ENV_EVENT  (VJ_ENV_BASE + 1)   /* data: const struct vj_event* */
#define VJ_ENV_GET_BIOS_PATH   (VJ_ENV_BASE + 2)   /* data: struct vj_env_bios_path*; needs VJ_CAP_BIOS */
#define VJ_ENV_GET_BOOT_TARGET (VJ_ENV_BASE + 3)   /* data: unsigned* (VJ_BOOT_*);    needs VJ_CAP_BIOS */

/* Capability bits the frontend may report in caps_out. */
#define VJ_CAP_EVENTS 0x1u   /* will accept VJ_ENV_EVENT (always set by an answering frontend) */
#define VJ_CAP_BIOS   0x2u   /* answers VJ_ENV_GET_BIOS_PATH / VJ_ENV_GET_BOOT_TARGET */

/* BIOS roles, for VJ_ENV_GET_BIOS_PATH and the VJ_EVT_BIOS_* events. */
#define VJ_BIOS_CART_BOOT 1u   /* the 128 KB console boot ROM at $E00000 */
#define VJ_BIOS_CD        2u   /* the 256 KB Jaguar CD BIOS (mapped at $800000) */

/* What a no-content boot (retro_load_game(NULL)) enters. */
#define VJ_BOOT_CD_BIOS        0u   /* the CD BIOS with the CD unit attached (the default) */
#define VJ_BOOT_CART_BOOT_ROM  1u   /* the cartridge boot ROM against an empty slot */

struct vj_env_bios_path
{
   unsigned role;          /* in:  VJ_BIOS_*                                              */
   const char *path;       /* out: file to use, valid for the call only; NULL/"" = no preference */
};

struct vj_env_hello
{
   unsigned abi;                 /* in:  VJ_FRONTEND_EXT_ABI the core speaks   */
   const char *core_version;     /* in:  core version string (call-scoped)     */
   unsigned caps_out;            /* out: VJ_CAP_* bits; frontend returns true  */
};

enum vj_event_type
{
   VJ_EVT_LINK_UP = 1,      /* a = 1 voice modem / 0 JagLink; s = "<mode>" or, as a TCP client, "<mode> <host>" */
   VJ_EVT_LINK_DOWN,        /* s = mode name ("tcp_client", "tcp_server", ...)                                */
   VJ_EVT_LINK_STATS,       /* a = hops/s, b = mean hop ms x100, c = slipped hops, d = timeouts                */
   VJ_EVT_WIRE_SPEED,       /* a = multiplier (1 = stock), b = 1 confirmed / 0 back to stock                   */
   VJ_EVT_CRASH_WATCHDOG,   /* s = signature ("gpu_pc_escape", "video_stall", ...); a = frame number, b = the
                               PC / hash / B_COUNT the line reports (signature specific), c,d = extras       */
   VJ_EVT_BIOS_STAGED,      /* a = VJ_BIOS_* role, b = CRC32 of the image the core will run, c = where it came
                               from (VJ_BIOS_SRC_*), s = the file path, or the built-in image's name           */
   VJ_EVT_BIOS_REJECTED     /* a = role, s = the path the frontend named; the core did not accept it (wrong
                               size or not a BIOS) and fell back to its own search                            */
};

#define VJ_BIOS_SRC_BUILTIN   0u   /* the image compiled into the core                       */
#define VJ_BIOS_SRC_SYSTEM    1u   /* found by the core's own search of the system directory */
#define VJ_BIOS_SRC_FRONTEND  2u   /* the file the frontend named (VJ_ENV_GET_BIOS_PATH)     */

struct vj_event
{
   unsigned type;           /* enum vj_event_type */
   unsigned a, b, c, d;     /* meaning per type, 0 when unused */
   const char *s;           /* per type, NULL when unused (call-scoped) */
};

/* ---- core-side plumbing (src/core/vj_frontend_ext.c) ---------------------- */
/* Called from retro_init(): performs the HELLO and latches the result. */
void VJExtInit(retro_environment_t env_cb, const char *core_version);
/* Called from retro_deinit(): drops the latch (iOS never dlcloses the core). */
void VJExtDeinit(void);
/* Nonzero once a frontend answered HELLO. */
int  VJExtActive(void);
/* True when the answering frontend reported every bit of `cap`. */
int  VJExtHasCap(unsigned cap);
/* Ask the frontend which file to use for `role` (VJ_ENV_GET_BIOS_PATH): copies the path into
 * buf and returns 1, or returns 0 (inactive, no VJ_CAP_BIOS, refused, empty, or too long for buf). */
int  VJExtBiosPath(unsigned role, char *buf, unsigned size);
/* What a no-content boot should enter: VJ_BOOT_CD_BIOS unless an answering frontend says otherwise. */
unsigned VJExtBootTarget(void);
/* No-ops (one branch) while inactive. */
void VJExtEmit(unsigned type, unsigned a, unsigned b, unsigned c, unsigned d,
               const char *s);
/* Test hook: events actually delivered since the last VJExtInit(). */
unsigned VJExtSentCount(void);

#ifdef __cplusplus
}
#endif

#endif /* VJ_FRONTEND_EXT_H */
