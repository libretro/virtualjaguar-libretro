/* voicechat.h — host-side voice channel over the netlink discovery socket.
 *
 * Not emulation: voice never entered the Jaguar. This sits entirely
 * outside the emulated machine (issue #485). See docs/voice-chat-design.md.
 *
 * Pure codec/framing/jitter/VAD are unit-testable without sockets; the
 * send/receive path rides jlink_discover's raw datagram seam.
 */
#ifndef __VOICECHAT_H__
#define __VOICECHAT_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VC_MAGIC_0           'V'
#define VC_MAGIC_1           'J'
#define VC_MAGIC_2           'V'
#define VC_MAGIC_3           'C'
#define VC_VERSION           1
#define VC_FLAG_KEEPALIVE    0x01
#define VC_RATE_HZ           8000
#define VC_FRAME_SAMPLES     160   /* 20 ms at 8 kHz */
#define VC_HDR_LEN           12
#define VC_PKT_LEN           (VC_HDR_LEN + VC_FRAME_SAMPLES)
#define VC_UPSAMPLE          6     /* 48000 / 8000 */
#define VC_PEER_ADDR_MAX     46
#define VC_MAX_SPEAKERS      3     /* concurrent far-end mix (#585) */

enum
{
   VC_GATE_OPEN_MIC = 0,
   VC_GATE_PTT      = 1
};

/* Optional mic read: fill up to num mono int16 samples; return count or
 * -1 if unavailable. Registered by libretro.c from the frontend mic API. */
typedef int (*VoiceChatMicReadFn)(int16_t *samples, size_t num);

/* Optional netpacket TX sink (#585). When set and the link is in
 * JLINK_MODE_NETPACKET, VoiceChatSendFrame routes through this instead
 * of the discovery UDP socket. Keeps voicechat.c free of a hard
 * dependency on jlink_netpacket.c (unit tests link voicechat alone). */
typedef int (*VoiceChatNetSendFn)(const uint8_t *pkt, size_t len);

/* ---- Pure logic (unit-testable) -------------------------------------- */

uint8_t VoiceChatMuLawEncode(int16_t pcm);
int16_t VoiceChatMuLawDecode(uint8_t mulaw);

/* Encode a full packet into buf (capacity >= VC_PKT_LEN). Returns length
 * written, or 0 if capacity is too small. */
size_t VoiceChatEncodePkt(uint8_t *buf, size_t cap,
                          uint8_t flags, uint16_t seq, uint32_t senderId,
                          const uint8_t mulaw[VC_FRAME_SAMPLES]);

/* Decode. Returns 1 on success and fills outs; 0 on reject. */
int VoiceChatDecodePkt(const uint8_t *buf, size_t len,
                       uint8_t *flags, uint16_t *seq, uint32_t *senderId,
                       uint8_t mulaw[VC_FRAME_SAMPLES]);

/* Absolute-average energy of pcm[0..n); used by VAD. */
unsigned VoiceChatFrameEnergy(const int16_t *pcm, unsigned n);

/* ---- Configuration / lifecycle --------------------------------------- */

void VoiceChatReset(void);
void VoiceChatSetEnabled(int enabled);
int  VoiceChatEnabled(void);
void VoiceChatSetGate(int gate);          /* VC_GATE_* */
void VoiceChatSetPTTKey(unsigned retrok); /* RETROK_* value; 0 = none */
unsigned VoiceChatPTTKey(void);
void VoiceChatSetVolume(unsigned pct);    /* 0..100 */
void VoiceChatSetVadThreshold(unsigned thresh);
void VoiceChatSetMonitor(int enabled);
void VoiceChatSetMicRead(VoiceChatMicReadFn fn);
void VoiceChatSetNetSend(VoiceChatNetSendFn fn);
void VoiceChatSetSenderId(uint32_t id);   /* tests; 0 = lazy random */

/* Peer address for outbound unicast (dotted-quad or hostname). Cleared
 * on reset; also learned from inbound datagrams. */
void VoiceChatSetPeerAddr(const char *addr);
const char *VoiceChatPeerAddr(void);

/* ---- Per-frame / wire ------------------------------------------------ */

/* ptt_down: 1 while the configured PTT key is held (ignored in open_mic).
 * Pulls mic, gates, packetizes, sends; drains discovery via caller. */
void VoiceChatFrameTick(int ptt_down);

/* Raw datagram handler (magic VJVC). Learns peer from from_addr. */
void VoiceChatOnRaw(const uint8_t *buf, size_t len,
                    const char *from_addr, int from_port);

/* Mix far-end (and optional local monitor) into interleaved stereo
 * sampleBuffer (uint16_t L,R pairs). Saturating add; never touches
 * emulated state. */
void VoiceChatMixInto(uint16_t *buf, unsigned pairs);

/* Test hooks: push decoded PCM into the jitter buffer; pop one 8 kHz
 * sample (-1 if empty). Default stream uses senderId 0. */
void VoiceChatJitterPush(const int16_t pcm[VC_FRAME_SAMPLES], uint16_t seq);
void VoiceChatJitterPushFrom(uint32_t senderId,
                             const int16_t pcm[VC_FRAME_SAMPLES],
                             uint16_t seq);
int  VoiceChatJitterPop(int16_t *out);
unsigned VoiceChatJitterCount(void);
unsigned VoiceChatActiveSpeakers(void);

#ifdef __cplusplus
}
#endif

#endif
