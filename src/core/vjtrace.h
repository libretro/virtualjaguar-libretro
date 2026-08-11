/* vjtrace.h -- core-side flight-recorder trace facility.
 *
 * Dev-build only (compiled when VJ_TRACE is defined, which the Makefile
 * sets automatically inside the TEST_EXPORTS=1 branch).  In shipped
 * builds every VJT_* macro below compiles to nothing and vjtrace.c's
 * body is entirely #ifdef'd out, so there is zero cost and zero
 * exported symbols (see docs/vjtrace-design.md).
 *
 * The event enum, vjtrace_ev, and vjtrace_counters_t are declared
 * OUTSIDE the VJ_TRACE guard so that offline analyzer tools (which are
 * never built with VJ_TRACE) can still include this header for the
 * types used by the binary dump format.
 */
#ifndef __VJTRACE_H__
#define __VJTRACE_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Event types.  addr/value meaning is documented per-type. */
enum
{
   VJT_EV_NONE = 0,
   VJT_EV_IRQ_ASSERT,     /* addr = src: 0 video,1 gpu,2 obj,3 timer,4 jerry */
   VJT_EV_IRQ_DISPATCH,   /* 68K autovector entry */
   VJT_EV_GPU_GO,         /* value = G_PC at start */
   VJT_EV_GPU_STOP,       /* value = PC at stop */
   VJT_EV_OP_LIST_START,  /* addr = OLP, value = halfline */
   VJT_EV_OP_OBJECT,      /* addr = object phrase addr, value = type 0-7 */
   VJT_EV_OP_GPU_OBJ,
   VJT_EV_OP_BRANCH,      /* value = branch target */
   VJT_EV_BLIT_CMD,       /* addr = B_CMD value, value = A1_BASE */
   VJT_EV_INPUT_EDGE,     /* host-injected: addr = pad, value = bits */
   VJT_EV_WATCH_RD,
   VJT_EV_WATCH_WR,
   VJT_EV_SNAPSHOT,
   VJT_EV_MARK,
   VJT_EV__COUNT
};

/* 32 bytes with alignment. */
typedef struct
{
   uint64_t seq;
   uint32_t frame;
   uint16_t halfline;
   uint8_t type;
   uint8_t who;
   uint32_t pc;
   uint32_t addr;
   uint32_t value;
} vjtrace_ev;

typedef struct
{
   uint64_t ev[VJT_EV__COUNT];   /* one cumulative counter per event type */
} vjtrace_counters_t;

#ifdef VJ_TRACE

/* alloc ring; cap from env VJ_TRACE_RING, default 1<<20 */
void vjtrace_init(void);
/* sets current frame for emit stamping */
void vjtrace_frame_tick(uint32_t frame);
void vjtrace_emit(uint8_t type, uint8_t who, uint32_t addr, uint32_t value);
/* rw: 1=r 2=w 3=rw; ret idx or -1 */
int vjtrace_watch_add(uint32_t lo, uint32_t hi, unsigned rw);
void vjtrace_watch_clear(void);
void vjtrace_watch_check(uint32_t addr, uint32_t value, uint32_t who, int is_write);
/* binary ring dump; see docs/vjtrace-design.md for the format */
int vjtrace_dump(const char *path);

extern vjtrace_counters_t vjtrace_counters;
extern uint32_t vjtrace_nwatch;

#define VJT_EMIT(t, w, a, v)   vjtrace_emit((uint8_t)(t), (uint8_t)(w), (a), (v))
#define VJT_WATCH_WR(a, v, w)  do { if (vjtrace_nwatch) vjtrace_watch_check((a), (v), (w), 1); } while (0)
#define VJT_WATCH_RD(a, v, w)  do { if (vjtrace_nwatch) vjtrace_watch_check((a), (v), (w), 0); } while (0)

#else /* !VJ_TRACE */

#define VJT_EMIT(t, w, a, v)   do { } while (0)
#define VJT_WATCH_WR(a, v, w)  do { } while (0)
#define VJT_WATCH_RD(a, v, w)  do { } while (0)

#endif /* VJ_TRACE */

#ifdef __cplusplus
}
#endif

#endif /* __VJTRACE_H__ */
