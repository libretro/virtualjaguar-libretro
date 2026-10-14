/*
 * vjdebug.h -- the core side of the app's debug interface.
 *
 * The contract (struct vj_debug_iface) is src/debug/vj_debug_iface.h, shared
 * verbatim with the app; this header is only the core-internal plumbing.
 * Design: docs/app-debug-tools.md, docs/app-frontend-extension.md.
 *
 * Nothing here runs unless the frontend answered VJ_HELLO with VJ_CAP_DEBUG
 * (src/core/vj_frontend_ext.c hands the interface over once, right after
 * the HELLO).  RetroArch never does.  Even then, a detached interface costs
 * the emulation nothing: attach() stores a pointer, and the only hot-path
 * additions are one global test per OPProcessList call and one per blitter
 * launch.
 */
#ifndef VJDEBUG_H
#define VJDEBUG_H

#include "vj_debug_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The interface the frontend is handed (static lifetime). */
const struct vj_debug_iface *VJDebugIface(void);

/* retro_load_game() succeeded / retro_unload_game() or retro_deinit():
 * peek/poke refuse (VJ_DBG_ERR_STATE) while no content is loaded, and an
 * unload drops any attached host, its breakpoints, the OP snapshot and the
 * blitter log (iOS never dlcloses the core: every static is reset here). */
void VJDebugSetLoaded(int loaded);
void VJDebugReset(void);

/* Emulated frames since content load (vj_debug_iface.frame_number); the
 * blitter log stamps each launch with it. */
extern unsigned vjDebugFrame;

/* Nonzero while an app host is attached. */
int VJDebugAttached(void);

#ifdef __cplusplus
}
#endif

#endif /* VJDEBUG_H */
