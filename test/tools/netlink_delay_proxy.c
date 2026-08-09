/* netlink_delay_proxy.c — TCP relay that adds fixed one-way delay.
 *
 *   netlink_delay_proxy --listen PORT --connect HOST:PORT --delay-ms N
 *
 * Accepts ONE client, connects upstream, and relays both directions,
 * holding every chunk for N milliseconds before forwarding.  Simulates
 * Wi-Fi/LAN latency on localhost so netlink latency behavior can be
 * reproduced headlessly (2*N ms round trip).  Exits when either side
 * closes.  POSIX only — test tooling, not shipped.
 */
#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define QCAP 4096          /* queued chunks per direction */
#define CHUNK 2048

typedef struct
{
    unsigned char buf[CHUNK];
    int len;
    long long due_usec;
} chunk_t;

typedef struct
{
    chunk_t q[QCAP];
    int head, count;
} cqueue_t;

static long long now_usec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}

static int tcp_nodelay(int fd)
{
    int one = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

/* Pump one direction: read available data into the queue, write out
   everything whose delay has elapsed.  Returns 0 on EOF/error. */
static int pump(int from, int to, cqueue_t *q, long long delay_usec,
                int readable)
{
    long long now = now_usec();
    if (readable && q->count < QCAP)
    {
        chunk_t *c = &q->q[(q->head + q->count) % QCAP];
        ssize_t n = recv(from, c->buf, CHUNK, 0);
        if (n <= 0)
            return 0;
        c->len = (int)n;
        c->due_usec = now + delay_usec;
        q->count++;
    }
    while (q->count > 0)
    {
        chunk_t *c = &q->q[q->head];
        ssize_t n;
        if (c->due_usec > now)
            break;
        n = send(to, c->buf, (size_t)c->len, 0);
        if (n <= 0)
            return 0;
        if (n < c->len)
        {
            memmove(c->buf, c->buf + n, (size_t)(c->len - n));
            c->len -= (int)n;
            break;
        }
        q->head = (q->head + 1) % QCAP;
        q->count--;
    }
    return 1;
}

int main(int argc, char **argv)
{
    int listen_port = 0, up_port = 0, delay_ms = 0;
    char up_host[128];
    int lfd, cfd = -1, ufd = -1, i;
    struct sockaddr_in sa;
    int one = 1;
    static cqueue_t q_c2u, q_u2c;

    up_host[0] = '\0';
    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--listen") && i + 1 < argc)
            listen_port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--connect") && i + 1 < argc)
        {
            const char *colon = strchr(argv[++i], ':');
            if (!colon) { fprintf(stderr, "bad --connect\n"); return 1; }
            memcpy(up_host, argv[i], (size_t)(colon - argv[i]));
            up_host[colon - argv[i]] = '\0';
            up_port = atoi(colon + 1);
        }
        else if (!strcmp(argv[i], "--delay-ms") && i + 1 < argc)
            delay_ms = atoi(argv[++i]);
    }
    if (!listen_port || !up_host[0] || !up_port)
    {
        fprintf(stderr, "usage: %s --listen PORT --connect HOST:PORT "
                        "--delay-ms N\n", argv[0]);
        return 1;
    }

    lfd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = htons((unsigned short)listen_port);
    if (bind(lfd, (struct sockaddr *)&sa, sizeof(sa)) < 0
        || listen(lfd, 1) < 0)
    {
        perror("listen");
        return 1;
    }
    fprintf(stderr, "proxy: listening on %d -> %s:%d (+%d ms each way)\n",
            listen_port, up_host, up_port, delay_ms);

    cfd = accept(lfd, NULL, NULL);
    if (cfd < 0) { perror("accept"); return 1; }
    close(lfd);
    tcp_nodelay(cfd);

    ufd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)up_port);
    if (inet_pton(AF_INET, up_host, &sa.sin_addr) != 1)
    {
        fprintf(stderr, "proxy: bad upstream host\n");
        return 1;
    }
    if (connect(ufd, (struct sockaddr *)&sa, sizeof(sa)) < 0)
    {
        perror("connect");
        return 1;
    }
    tcp_nodelay(ufd);
    fprintf(stderr, "proxy: relaying\n");

    for (;;)
    {
        fd_set rd;
        struct timeval tv;
        int maxfd = (cfd > ufd ? cfd : ufd), rv;
        FD_ZERO(&rd);
        if (q_c2u.count < QCAP) FD_SET(cfd, &rd);
        if (q_u2c.count < QCAP) FD_SET(ufd, &rd);
        tv.tv_sec = 0;
        tv.tv_usec = 500;   /* wake often enough to release due chunks */
        rv = select(maxfd + 1, &rd, NULL, NULL, &tv);
        if (rv < 0)
        {
            if (errno == EINTR) continue;
            break;
        }
        if (!pump(cfd, ufd, &q_c2u, (long long)delay_ms * 1000,
                  FD_ISSET(cfd, &rd)))
            break;
        if (!pump(ufd, cfd, &q_u2c, (long long)delay_ms * 1000,
                  FD_ISSET(ufd, &rd)))
            break;
    }
    close(cfd);
    close(ufd);
    return 0;
}
