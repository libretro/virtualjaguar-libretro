#ifndef VJ_LOG_H
#define VJ_LOG_H

#include <stdio.h>
#include <stdarg.h>
#include "libretro.h"

#ifdef __cplusplus
extern "C" {
#endif

extern retro_log_printf_t vj_log_cb;

static INLINE void vj_log_stderr(const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}

#ifdef __cplusplus
}
#endif

#define VJ_LOG(level, ...) do { \
   if (vj_log_cb) vj_log_cb(level, __VA_ARGS__); \
   else vj_log_stderr(__VA_ARGS__); \
} while (0)

/* LOG_DBG compiles out of release builds (Makefile adds -DNDEBUG unless
 * DEBUG=1).  VJ_DEBUG_LOG opts it back in.  INF/WRN/ERR stay live.
 * Call-site arguments are format strings and register reads -- no
 * required side effects -- so eliding evaluation is safe. */
#if defined(NDEBUG) && !defined(VJ_DEBUG_LOG)
#define LOG_DBG(...) do { } while (0)
#else
#define LOG_DBG(...) VJ_LOG(RETRO_LOG_DEBUG, __VA_ARGS__)
#endif
#define LOG_INF(...) VJ_LOG(RETRO_LOG_INFO,  __VA_ARGS__)
#define LOG_WRN(...) VJ_LOG(RETRO_LOG_WARN,  __VA_ARGS__)
#define LOG_ERR(...) VJ_LOG(RETRO_LOG_ERROR, __VA_ARGS__)

#endif
