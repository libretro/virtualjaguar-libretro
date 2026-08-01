/* jlink_tcp.h — nonblocking TCP endpoint for the JagLink transport.
   Internal to jlink.c; nothing else should include this. */
#ifndef __JLINK_TCP_H__
#define __JLINK_TCP_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 if this build has socket support (JLINK_HAVE_TCP). */
int  JLinkTCPAvailable(void);

/* is_server: listen on port; else connect to host:port (nonblocking —
   the connection completes during JLinkTCPPoll).  Returns 1 on
   successful setup, 0 on immediate failure. */
int  JLinkTCPOpen(int is_server, const char *host, int port);
void JLinkTCPClose(void);
int  JLinkTCPConnected(void);

/* Nonblocking send; on partial send / EAGAIN the byte parks in a small
   pending ring flushed by JLinkTCPPoll.  Silently drops when no peer. */
void JLinkTCPSend(uint8_t b);

/* Nonblocking receive of one byte from the socket; 1 if a byte was
   returned. */
int  JLinkTCPRecv(uint8_t *b);

/* Progress accept/connect, detect disconnect, flush pending TX. */
void JLinkTCPPoll(void);

#ifdef __cplusplus
}
#endif

#endif
