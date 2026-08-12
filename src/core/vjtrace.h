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
 *
 * PERFORMANCE NOTE (VJ_TRACE is coupled 1:1 to TEST_EXPORTS=1, so this
 * taxes every test-mode build, not just callers that use vjtrace
 * directly): the GPU/DSP per-instruction PC-history hooks in GPUExec()/
 * DSPExec() (see vjtrace_pchist_gpu()/vjtrace_pchist_dsp() below)
 * measured a ~6-8% wall-clock throughput regression on `make
 * BENCH_PROFILE=1 benchmark` versus a build with no vjtrace hooks at
 * all (task-3-report.md has the full before/after numbers). A plain
 * function call and an inlined macro that writes the ring arrays
 * directly were both measured -- 11 paired, interleaved samples put
 * them within 0.02% of each other, i.e. statistically indistinguishable
 * on this hardware -- so the cost is the per-instruction ring write
 * itself, not call overhead, and the simpler function-call form was
 * kept. Any wall-clock-derived assertion is measurably tighter in a
 * VJ_TRACE build than in production: `test/test_frontend_pacing.c`'s
 * default invocation (no `--max-fastest-frame-fraction` override, see
 * Makefile:1163) asserts the fastest observed frame beats 0.5x the
 * frame period (8.333 ms at 60 fps) -- a real, load-sensitive margin
 * (the Makefile's own comment at line 1173-1178 notes it "would flake
 * on loaded CI runners: observed locally 11.3 ms vs the 8.3 ms limit
 * under parallel builds", which is why the *other* two invocations
 * defuse it with `--max-fastest-frame-fraction 100`). Measured passing
 * with room at commit 2b5d132 (fastest frame 4.326 ms, 52% of the
 * 8.333 ms limit) -- not a confirmed regression -- but the margin is
 * real and shared with every other per-instruction VJ_TRACE hook added
 * in the future; re-check this assertion specifically (not just `make
 * test`'s overall exit code) after adding another one. */
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

/* GPU/DSP PC history rings (0x400 entries each), fed from the per-
 * instruction top of GPUExec()/DSPExec() via a plain function call.
 * Read back via vjtrace_backtrace(). An inlined-macro form that writes
 * the ring arrays directly (no call) was measured against this and
 * found statistically indistinguishable -- see the PERFORMANCE NOTE
 * above -- so this is the simpler of the two working designs, kept per
 * that measurement rather than assumed. */
#define VJT_PCHIST_CAP 0x400

void vjtrace_pchist_gpu(uint32_t pc);
void vjtrace_pchist_dsp(uint32_t pc);

/* Fills out[] with up to maxn PCs from the requested processor's PC
 * history ring, oldest first / newest last, and sets *count to the
 * number of entries actually written.  who is one of the vjag_memory.h
 * enum values (M68K, GPU, DSP); any other value yields *count = 0.
 *
 * GPU/DSP are fill-tracked: *count is min(maxn, VJT_PCHIST_CAP, entries
 * actually pushed since the ring was last cleared/emulation start), so
 * a backtrace requested before either processor has executed
 * VJT_PCHIST_CAP instructions returns only real entries -- never
 * zero-filled or stale fabricated history.
 *
 * M68K reads the existing pcQueue/pcQPtr ring in src/core/jaguar.c
 * rather than a duplicate (see M68KInstructionHook(), jaguar.c:332).
 * KNOWN LIMITATION: that ring has no fill counter of its own, and
 * adding one means touching jaguar.c's per-instruction hot path, which
 * is out of scope for this module. vjtrace_backtrace() therefore
 * assumes it is full and can return zero/stale-placeholder entries for
 * the first VJT_PCHIST_CAP 68K instructions of emulation (pcQueue is a
 * plain BSS array, so unwritten slots read as PC=0). In practice
 * M68KInstructionHook() runs unconditionally every 68K instruction from
 * the very first one executed (it is not gated by VJ_TRACE), so this
 * window closes within roughly VJT_PCHIST_CAP instructions of core
 * boot -- a few dozen microseconds -- and does not recur afterward. */
void vjtrace_backtrace(int who, uint32_t *out, int maxn, int *count);

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
