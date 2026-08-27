/*
 * gdb_determinism_probe.c -- Phase 2 determinism gate (issue #652).
 *
 * docs/gdb-stub-design.md: "With the stub enabled but no breakpoint
 * armed, emulation is bit-identical to the stub being disabled. This is
 * a test, not a claim." Prints one savestate digest at --frames; compare
 * the output across three runs of this tool:
 *
 *   ./gdb_determinism_probe core rom --frames 300 \
 *       --option virtualjaguar_gdb_stub=disabled
 *   ./gdb_determinism_probe core rom --frames 300 \
 *       --option virtualjaguar_gdb_stub=enabled --option virtualjaguar_gdb_port=22346
 *   ./gdb_determinism_probe core rom --frames 300 \
 *       --option virtualjaguar_gdb_stub=enabled --option virtualjaguar_gdb_port=22347 \
 *       --attach-port 22347
 *
 * The third run's --attach-port spawns a background thread that opens a
 * raw RSP client connection, negotiates QStartNoAckMode exactly like a
 * real `gdb` attach, and holds the socket open (arming zero breakpoints)
 * for the rest of the run -- this is the "enabled, attached, zero
 * breakpoints" configuration the design doc's determinism gate names.
 * It has to be a background thread, not something done synchronously
 * inside the frame callback: the stub's non-blocking per-frame poll
 * (GDBTargetServicePoll(), called from the TOP of retro_run()) only
 * advances once per retro_run() call, so a client that blocks on recv()
 * from the SAME thread that drives the harness's retro_run() loop would
 * deadlock -- nothing would ever call retro_run() again to produce the
 * reply it is waiting for. Two threads sidesteps that entirely: the main
 * thread keeps calling retro_run() every frame regardless of what the
 * attach thread is doing.
 *
 * No libretro.py/RETRO_ENVIRONMENT_GET_PERF_INTERFACE dependency, unlike
 * a Python-driven attach -- this tool uses the same dlopen-based harness
 * every other test/tools digest probe does.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include \
 *      -o test/tools/gdb_determinism_probe test/tools/gdb_determinism_probe.c \
 *      test/harness/harness.c -ldl -lm -lpthread
 */

#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef size_t (*serialize_size_fn)(void);
typedef bool (*serialize_fn)(void *, size_t);

typedef struct {
    harness_config *cfg;
    serialize_size_fn ssize;
    serialize_fn      ser;
    unsigned          digest_frame;
    int               did_digest;
    int               failures;
} gd_state;

typedef struct {
    int  port;
    int  attached;   /* set by the thread once the handshake succeeds */
    int  failed;
} gd_attach_ctx;

static unsigned char gd_cksum(const char *p, size_t len)
{
    unsigned sum = 0;
    size_t i;
    for (i = 0; i < len; i++)
        sum += (unsigned char)p[i];
    return (unsigned char)sum;
}

/* Runs on its own thread: connect, send an unframed ack (as a real
 * client would after receiving OUR previous replies -- harmless here
 * since we've sent nothing yet), then qSupported and QStartNoAckMode,
 * blocking on recv() after each -- safe here because the MAIN thread is
 * concurrently driving retro_run() every frame, which is what lets the
 * stub's non-blocking per-frame poll actually produce those replies.
 * Retries the connect for a few seconds in case this thread wins the
 * race against the harness's own retro_load_game() (which is what opens
 * the listener). Then holds the socket open, arming nothing, until the
 * process exits. */
static void *gd_attach_thread(void *arg)
{
    gd_attach_ctx *ctx = (gd_attach_ctx *)arg;
    struct sockaddr_in addr;
    int fd = -1;
    char buf[512];
    char pkt[64];
    int n, i, tries;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((unsigned short)ctx->port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    for (tries = 0; tries < 100; tries++) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd >= 0 && connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
            break;
        if (fd >= 0)
            close(fd);
        fd = -1;
        usleep(20000);
    }

    if (fd < 0) {
        ctx->failed = 1;
        return NULL;
    }

    send(fd, "+", 1, 0);

    for (i = 0; i < 2; i++) {
        const char *payload = (i == 0) ? "qSupported:multiprocess+" : "QStartNoAckMode";
        size_t plen = strlen(payload);

        snprintf(pkt, sizeof(pkt), "$%s#%02x", payload, gd_cksum(payload, plen));
        send(fd, pkt, strlen(pkt), 0);

        n = (int)recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            ctx->failed = 1;
            close(fd);
            return NULL;
        }
    }

    ctx->attached = 1;

    /* Hold the connection open for the rest of the run -- arming
     * nothing -- by blocking on a recv() that the harness process's
     * exit (closing every fd) is what ultimately ends. A real client
     * would sit here too, between commands. */
    recv(fd, buf, sizeof(buf), 0);
    close(fd);
    return NULL;
}

static bool gd_frame(void *ud, unsigned frame)
{
    gd_state *st = (gd_state *)ud;
    size_t sz;
    uint8_t *buf;
    uint64_t h;
    size_t i;

    if (frame != st->digest_frame)
        return true;

    sz = st->ssize();
    if (sz == 0) {
        fprintf(stderr, "frame %u: retro_serialize_size() == 0\n", frame);
        st->failures++;
        return true;
    }
    buf = (uint8_t *)malloc(sz);
    if (!buf || !st->ser(buf, sz)) {
        fprintf(stderr, "frame %u: retro_serialize failed\n", frame);
        st->failures++;
        free(buf);
        return true;
    }
    h = 1469598103934665603ULL;
    for (i = 0; i < sz; i++) {
        h ^= (uint64_t)buf[i];
        h *= 1099511628211ULL;
    }
    printf("STATE_DIGEST frame=%u size=%zu fnv=%016llx\n",
           frame, sz, (unsigned long long)h);
    fflush(stdout);
    st->did_digest = 1;
    free(buf);
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    gd_state st;
    gd_attach_ctx actx;
    pthread_t attach_tid;
    int have_attach_thread = 0;
    int i;

    memset(&st, 0, sizeof(st));
    memset(&actx, 0, sizeof(actx));
    actx.port = -1;
    cfg.frames = 300;

    /* --attach-port is this tool's own flag, not the shared harness's --
     * strip it before handing argv to the harness so it does not choke
     * on an unrecognized option. */
    {
        char **filtered = (char **)malloc(sizeof(char *) * (size_t)argc);
        int fc = 0;

        filtered[fc++] = argv[0];
        for (i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--attach-port") == 0 && i + 1 < argc) {
                actx.port = atoi(argv[++i]);
            } else {
                filtered[fc++] = argv[i];
            }
        }

        if (!harness_init_from_args(&cfg, fc, filtered)) {
            free(filtered);
            return 1;
        }
        free(filtered);
    }

    if (!cfg.rom_path) {
        fprintf(stderr, "gdb_determinism_probe: no ROM given\n");
        return 1;
    }

    st.cfg = &cfg;
    st.digest_frame = cfg.frames;
    cfg.frame_callback      = gd_frame;
    cfg.frame_callback_data = &st;

    if (!harness_load_rom(&cfg))
        return 1;

    st.ssize = (serialize_size_fn)harness_dlsym(&cfg, "retro_serialize_size");
    st.ser   = (serialize_fn)harness_dlsym(&cfg, "retro_serialize");
    if (!st.ssize || !st.ser)
        return 1;

    if (actx.port > 0) {
        if (pthread_create(&attach_tid, NULL, gd_attach_thread, &actx) == 0)
            have_attach_thread = 1;
        else
            fprintf(stderr, "gdb_determinism_probe: pthread_create failed\n");
    }

    harness_run(&cfg);
    harness_shutdown(&cfg);

    if (!st.did_digest || st.failures) {
        fprintf(stderr, "gdb_determinism_probe: digest=%d failures=%d\n",
                st.did_digest, st.failures);
        return 1;
    }
    if (have_attach_thread) {
        if (!actx.attached) {
            fprintf(stderr, "gdb_determinism_probe: attach thread never completed "
                    "its handshake within the run (failed=%d)\n", actx.failed);
            return 1;
        }
        fprintf(stderr, "gdb_determinism_probe: attached to 127.0.0.1:%d "
                "(zero breakpoints) for the rest of the run\n", actx.port);
    }
    return 0;
}
