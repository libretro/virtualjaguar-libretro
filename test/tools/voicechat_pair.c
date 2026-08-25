/* voicechat_pair.c — two-process UDP voice-chat transport check (#485).
 *
 * Roles:
 *   --role recv  listen on discovery port, enable voice, wait for energy
 *   --role send  send keepalive + loud frames to peer
 *
 * Does not load a ROM; links voicechat + jlink_discover directly so the
 * side-channel wire path is exercised without RetroArch.
 *
 * Usage: voicechat_pair --role recv|send --port N [--host ADDR]
 * Exit: 0 PASS, 1 FAIL, 2 bind busy (shell retries).
 */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "voicechat.h"
#include "jlink_discover.h"
#include "jlink.h"

/* Stubs: voicechat only needs these for FrameTick peer seeding; we drive
 * OnRaw / Encode / MixInto directly. */
int JLinkMode(void) { return 2; } /* TCP_SERVER-ish */
const char *JLinkGetTCPHost(void) { return "127.0.0.1"; }
uint32_t JLinkNowMs(void)
{
   static uint32_t t = 1000;
   return t += 16;
}

static int role_recv(int port, unsigned timeout_ms)
{
   uint16_t mix[960]; /* 480 pairs */
   unsigned i;
   unsigned waited = 0;
   int energy_ok = 0;
   char envport[32];

   memset(mix, 0, sizeof(mix));

   snprintf(envport, sizeof(envport), "%d", port);
   setenv("VJ_DISC_PORT", envport, 1);

   VoiceChatReset();
   VoiceChatSetEnabled(1);
   VoiceChatSetVolume(100);
   VoiceChatSetSenderId(0x11111111u);

   if (!JLinkDiscStart(1 /* listen_only */, JLINK_DISC_DEV_JAGLINK, 42171)) {
      fprintf(stderr, "voicechat_pair recv: DiscStart failed\n");
      return 2;
   }
   JLinkDiscSetRawHandler(VoiceChatOnRaw);

   while (waited < timeout_ms) {
      JLinkDiscPoll(JLinkNowMs());
      if (VoiceChatJitterCount() > 0) {
         VoiceChatMixInto(mix, 480);
         {
            /* Treat non-zero mixed samples as energy present. */
            int16_t *s = (int16_t *)mix;
            long sum = 0;
            for (i = 0; i < 960; i++) {
               int v = s[i];
               if (v < 0)
                  v = -v;
               sum += v;
            }
            if (sum / 960 > 100)
               energy_ok = 1;
         }
         if (energy_ok)
            break;
      }
      usleep(10000);
      waited += 10;
   }

   JLinkDiscStop();
   if (!energy_ok) {
      fprintf(stderr, "voicechat_pair recv: no far-end energy after %ums\n",
              timeout_ms);
      return 1;
   }
   printf("voicechat_pair recv: PASS (energy ok)\n");
   return 0;
}

static int role_send(const char *host, int port, unsigned nframes)
{
   /* Dedicated socket (ephemeral bind) -- do NOT share the receiver's
    * SO_REUSEPORT discovery bind. Same-host unicast to a REUSEPORT group
    * can land on the sender's own socket (see #552 / jlink.c notes). */
   int16_t loud[VC_FRAME_SAMPLES];
   uint8_t mulaw[VC_FRAME_SAMPLES];
   uint8_t pkt[VC_PKT_LEN];
   unsigned i, f;
   size_t n;
   int sock;
   struct sockaddr_in to;
   struct addrinfo hints, *res = NULL;
   char portstr[16];

   for (i = 0; i < VC_FRAME_SAMPLES; i++)
      loud[i] = 8000;

   sock = (int)socket(AF_INET, SOCK_DGRAM, 0);
   if (sock < 0) {
      fprintf(stderr, "voicechat_pair send: socket failed\n");
      return 1;
   }

   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_DGRAM;
   snprintf(portstr, sizeof(portstr), "%d", port);
   if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) {
      fprintf(stderr, "voicechat_pair send: resolve %s failed\n", host);
      close(sock);
      return 1;
   }
   memcpy(&to, res->ai_addr, sizeof(to));
   freeaddrinfo(res);

   for (f = 0; f < nframes; f++) {
      for (i = 0; i < VC_FRAME_SAMPLES; i++)
         mulaw[i] = VoiceChatMuLawEncode(loud[i]);
      n = VoiceChatEncodePkt(pkt, sizeof(pkt), 0, (uint16_t)f,
                             0x22222222u, mulaw);
      if (!n || sendto(sock, (const char *)pkt, (int)n, 0,
                       (struct sockaddr *)&to, sizeof(to)) < 0) {
         fprintf(stderr, "voicechat_pair send: send failed frame %u\n", f);
         close(sock);
         return 1;
      }
      usleep(20000);
   }

   close(sock);
   printf("voicechat_pair send: PASS (%u frames)\n", nframes);
   return 0;
}

int main(int argc, char **argv)
{
   const char *role = NULL;
   const char *host = "127.0.0.1";
   int port = 42170;
   int i;

   for (i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "--role") && i + 1 < argc)
         role = argv[++i];
      else if (!strcmp(argv[i], "--host") && i + 1 < argc)
         host = argv[++i];
      else if (!strcmp(argv[i], "--port") && i + 1 < argc)
         port = atoi(argv[++i]);
   }
   if (!role) {
      fprintf(stderr, "usage: voicechat_pair --role recv|send --port N "
              "[--host ADDR]\n");
      return 1;
   }
   if (!strcmp(role, "recv"))
      return role_recv(port, 3000);
   if (!strcmp(role, "send"))
      return role_send(host, port, 30);
   fprintf(stderr, "unknown role %s\n", role);
   return 1;
}
