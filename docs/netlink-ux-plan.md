# Network Link UX Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make JagLink/CatBox/Voice Modem setup comprehensible and near-zero-config: an `auto` link mode that uses netplay when a session is live, LAN host discovery that replaces impossible text entry, and OSD narration of link state.

**Architecture:** A new dependency-free UDP beacon module (`src/jerry/jlink_discover.c`) splits cleanly into pure codec + peer-table logic (unit-testable with no sockets) and a thin socket layer driven from the existing per-frame `JLinkPoll()`. `libretro.c` gains an `auto` mode resolver, per-mode option visibility through the already-registered update-display callback, a discovered-host option list rebuilt via `SET_CORE_OPTIONS_V2`, and `SET_MESSAGE_EXT` narration.

**Tech Stack:** C89 (GNU89), BSD sockets, libretro core options v2.

**Spec:** [`docs/netlink-ux-design.md`](netlink-ux-design.md)

## Global Constraints

- **C89/GNU89 strict.** All declarations at top of block, before any statement. No `for (int i…)`, no compound literals, no designated initializers, no VLAs. Verify each touched C file with `bash scripts/c89-lint.sh <file>`.
- **Build:** `DEVELOPER_DIR=/Library/Developer/CommandLineTools make -j$(getconf _NPROCESSORS_ONLN) TEST_EXPORTS=1`
- **Never `rm`/`cp`/`mv` bare** — the user's shell aliases them to `-i` and they hang with no TTY. Use `command rm -f`, `command cp -f`, or `trash`.
- **New exported symbols must be added to BOTH `exports-test.list` (Mach-O) and `link-test.T` (GNU ld).** A symbol in only one is silently NULL from `harness_dlsym` on the other platform. `scripts/check-export-lists.py` runs inside `make test` and will fail the build.
- **New test binaries** must be added to the `test:` prerequisite list in the Makefile (around line 997) *and* invoked in the recipe.
- **Discovery port is 42170**, deliberately outside the `virtualjaguar_netlink_port` option range (42171–42174).
- **Discovery state is host-side and never serialized into savestates**, matching the existing rule for jlink sockets and modem session state.
- Commit messages use conventional commits: `feat(netlink):`, `fix(netlink):`, `test(netlink):`, `docs:`.

---

### Task 1: Beacon codec and peer table (pure logic, no sockets)

Splitting the wire format and peer bookkeeping away from sockets means the
tricky parts — truncation, bad magic, expiry, dedupe, capacity — are testable
with no network at all, and the socket layer in Task 2 becomes trivial.

**Files:**
- Create: `src/jerry/jlink_discover.h`
- Create: `src/jerry/jlink_discover.c`
- Create: `test/test_jlink_discover.c`
- Modify: `Makefile` (add build rule + test list entry)
- Modify: `Makefile.common` (add `jlink_discover.o` to sources)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `size_t JLinkDiscEncode(uint8_t *buf, size_t cap, int device, int port, const char *name)` → bytes written (40) or 0 on failure
  - `int JLinkDiscDecode(const uint8_t *buf, size_t len, int *device, int *port, char *name, size_t name_cap)` → 1 on valid packet, 0 otherwise
  - `void JLinkDiscPeersReset(void)`
  - `int JLinkDiscPeerSeen(const char *addr, const char *name, int device, int port, uint32_t now_ms)` → 1 if the peer set changed
  - `int JLinkDiscPeerExpire(uint32_t now_ms)` → 1 if any peer expired
  - `int JLinkDiscPeerCount(void)`
  - `const JLinkPeer *JLinkDiscPeerAt(int i)` → NULL when out of range
  - constants `JLINK_DISC_PORT` (42170), `JLINK_DISC_PKT_LEN` (40), `JLINK_DISC_MAX_PEERS` (8), `JLINK_DISC_EXPIRE_MS` (10000), `JLINK_DISC_DEV_JAGLINK` (0), `JLINK_DISC_DEV_VOICEMODEM` (1)

- [ ] **Step 1: Write the header**

```c
/* jlink_discover.h -- LAN discovery beacon for the Jaguar network link.
   Split in two halves on purpose: the codec + peer table below are pure
   logic with no sockets, so every awkward case (truncated packet, wrong
   magic, expiry, capacity) is unit-testable without a network.  The
   socket layer lives in the same .c file behind JLINK_DISC_HAVE_NET. */
#ifndef __JLINK_DISCOVER_H__
#define __JLINK_DISCOVER_H__

#include <stdint.h>
#include <stddef.h>

/* Outside the virtualjaguar_netlink_port option range (42171-42174) so a
   discovery socket can never collide with a link socket. */
#define JLINK_DISC_PORT        42170
#define JLINK_DISC_VERSION     1
#define JLINK_DISC_PKT_LEN     40
#define JLINK_DISC_NAME_MAX    32   /* 31 chars + NUL */
#define JLINK_DISC_ADDR_MAX    46   /* INET6_ADDRSTRLEN */
#define JLINK_DISC_MAX_PEERS   8
#define JLINK_DISC_EXPIRE_MS   10000

#define JLINK_DISC_DEV_JAGLINK    0
#define JLINK_DISC_DEV_VOICEMODEM 1

typedef struct
{
   char     name[JLINK_DISC_NAME_MAX];
   char     addr[JLINK_DISC_ADDR_MAX];
   int      device;
   int      port;
   uint32_t last_seen_ms;
} JLinkPeer;

size_t JLinkDiscEncode(uint8_t *buf, size_t cap, int device, int port,
                       const char *name);
int    JLinkDiscDecode(const uint8_t *buf, size_t len, int *device,
                       int *port, char *name, size_t name_cap);

void   JLinkDiscPeersReset(void);
int    JLinkDiscPeerSeen(const char *addr, const char *name, int device,
                         int port, uint32_t now_ms);
int    JLinkDiscPeerExpire(uint32_t now_ms);
int    JLinkDiscPeerCount(void);
const JLinkPeer *JLinkDiscPeerAt(int i);

#endif
```

- [ ] **Step 2: Write the failing test**

Create `test/test_jlink_discover.c`:

```c
#include <stdio.h>
#include <string.h>
#include "jlink_discover.h"

static int failures = 0;

static void check(int cond, const char *what)
{
   if (!cond) { printf("FAIL: %s\n", what); failures++; }
   else        printf("  ok: %s\n", what);
}

static void test_roundtrip(void)
{
   uint8_t buf[64];
   int dev = -1, port = -1;
   char name[JLINK_DISC_NAME_MAX];
   size_t n;

   n = JLinkDiscEncode(buf, sizeof(buf), JLINK_DISC_DEV_VOICEMODEM,
                       42171, "jaghub");
   check(n == JLINK_DISC_PKT_LEN, "encode writes exactly 40 bytes");
   check(JLinkDiscDecode(buf, n, &dev, &port, name, sizeof(name)) == 1,
         "decode accepts its own packet");
   check(dev == JLINK_DISC_DEV_VOICEMODEM, "device survives round-trip");
   check(port == 42171, "port survives round-trip");
   check(strcmp(name, "jaghub") == 0, "name survives round-trip");
}

static void test_rejects_bad(void)
{
   uint8_t buf[64];
   int dev, port;
   char name[JLINK_DISC_NAME_MAX];

   JLinkDiscEncode(buf, sizeof(buf), JLINK_DISC_DEV_JAGLINK, 42171, "x");

   check(JLinkDiscDecode(buf, JLINK_DISC_PKT_LEN - 1, &dev, &port,
                         name, sizeof(name)) == 0,
         "truncated packet rejected");
   buf[0] = 'X';
   check(JLinkDiscDecode(buf, JLINK_DISC_PKT_LEN, &dev, &port,
                         name, sizeof(name)) == 0,
         "wrong magic rejected");
   buf[0] = 'V'; buf[4] = 99;
   check(JLinkDiscDecode(buf, JLINK_DISC_PKT_LEN, &dev, &port,
                         name, sizeof(name)) == 0,
         "wrong version rejected");
}

static void test_name_never_unterminated(void)
{
   uint8_t buf[64];
   int dev, port;
   char name[JLINK_DISC_NAME_MAX];
   int i;

   JLinkDiscEncode(buf, sizeof(buf), JLINK_DISC_DEV_JAGLINK, 42171,
                   "0123456789012345678901234567890123456789");
   /* Stomp every name byte so nothing in the field is NUL. */
   for (i = 8; i < JLINK_DISC_PKT_LEN; i++)
      buf[i] = 'A';
   check(JLinkDiscDecode(buf, JLINK_DISC_PKT_LEN, &dev, &port,
                         name, sizeof(name)) == 1,
         "full-width name still decodes");
   check(name[JLINK_DISC_NAME_MAX - 1] == '\0',
         "decoded name is always NUL-terminated");
}

static void test_peer_table(void)
{
   JLinkDiscPeersReset();
   check(JLinkDiscPeerCount() == 0, "table starts empty");

   check(JLinkDiscPeerSeen("192.168.1.2", "a", 0, 42171, 1000) == 1,
         "first sighting reports a change");
   check(JLinkDiscPeerCount() == 1, "peer added");
   check(JLinkDiscPeerSeen("192.168.1.2", "a", 0, 42171, 2000) == 0,
         "refresh of a known peer reports no change");
   check(JLinkDiscPeerCount() == 1, "refresh does not duplicate");

   check(JLinkDiscPeerSeen("192.168.1.3", "b", 1, 42172, 2000) == 1,
         "second peer reports a change");
   check(JLinkDiscPeerCount() == 2, "two peers tracked");

   check(JLinkDiscPeerExpire(2000 + JLINK_DISC_EXPIRE_MS) == 1,
         "expiry of the stale peer reports a change");
   check(JLinkDiscPeerCount() == 1, "stale peer dropped, fresh one kept");
   check(strcmp(JLinkDiscPeerAt(0)->addr, "192.168.1.3") == 0,
         "surviving peer is the fresh one");
   check(JLinkDiscPeerAt(5) == NULL, "out-of-range index returns NULL");
}

static void test_capacity(void)
{
   char addr[JLINK_DISC_ADDR_MAX];
   int i;

   JLinkDiscPeersReset();
   for (i = 0; i < JLINK_DISC_MAX_PEERS + 4; i++)
   {
      sprintf(addr, "10.0.0.%d", i + 1);
      JLinkDiscPeerSeen(addr, "n", 0, 42171, 1000);
   }
   check(JLinkDiscPeerCount() == JLINK_DISC_MAX_PEERS,
         "table caps at JLINK_DISC_MAX_PEERS");
}

int main(void)
{
   test_roundtrip();
   test_rejects_bad();
   test_name_never_unterminated();
   test_peer_table();
   test_capacity();
   if (failures) { printf("test_jlink_discover: %d FAILURE(S)\n", failures); return 1; }
   printf("test_jlink_discover: all passed\n");
   return 0;
}
```

- [ ] **Step 3: Add the build rule and run the test to see it fail**

Add to `Makefile`, next to the other `test/test_jlink*` rules (~line 1548):

```make
test/test_jlink_discover: test/test_jlink_discover.c src/jerry/jlink_discover.c src/jerry/jlink_discover.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) -Itest \
		-o $@ test/test_jlink_discover.c src/jerry/jlink_discover.c
```

Run: `DEVELOPER_DIR=/Library/Developer/CommandLineTools make test/test_jlink_discover`
Expected: FAIL — `src/jerry/jlink_discover.c` does not exist yet.

- [ ] **Step 4: Implement the codec and peer table**

Create `src/jerry/jlink_discover.c` (codec + table only; sockets land in Task 2):

```c
/* jlink_discover.c -- LAN discovery beacon.  See jlink_discover.h. */
#include "jlink_discover.h"
#include <string.h>
#include <stdio.h>

static JLinkPeer discPeers[JLINK_DISC_MAX_PEERS];
static int       discPeerCount = 0;

size_t JLinkDiscEncode(uint8_t *buf, size_t cap, int device, int port,
                       const char *name)
{
   size_t n;

   if (!buf || cap < JLINK_DISC_PKT_LEN)
      return 0;

   memset(buf, 0, JLINK_DISC_PKT_LEN);
   buf[0] = 'V'; buf[1] = 'J'; buf[2] = 'A'; buf[3] = 'G';
   buf[4] = (uint8_t)JLINK_DISC_VERSION;
   buf[5] = (uint8_t)(device ? JLINK_DISC_DEV_VOICEMODEM
                             : JLINK_DISC_DEV_JAGLINK);
   buf[6] = (uint8_t)((port >> 8) & 0xFF);
   buf[7] = (uint8_t)(port & 0xFF);

   if (name)
   {
      n = strlen(name);
      if (n > JLINK_DISC_NAME_MAX - 1)
         n = JLINK_DISC_NAME_MAX - 1;
      memcpy(buf + 8, name, n);
   }
   return JLINK_DISC_PKT_LEN;
}

int JLinkDiscDecode(const uint8_t *buf, size_t len, int *device,
                    int *port, char *name, size_t name_cap)
{
   if (!buf || len < JLINK_DISC_PKT_LEN)
      return 0;
   if (buf[0] != 'V' || buf[1] != 'J' || buf[2] != 'A' || buf[3] != 'G')
      return 0;
   if (buf[4] != JLINK_DISC_VERSION)
      return 0;

   if (device)
      *device = (buf[5] == JLINK_DISC_DEV_VOICEMODEM)
                ? JLINK_DISC_DEV_VOICEMODEM : JLINK_DISC_DEV_JAGLINK;
   if (port)
      *port = ((int)buf[6] << 8) | (int)buf[7];

   if (name && name_cap > 0)
   {
      size_t copy = name_cap - 1;
      /* The sender NUL-pads, but a hostile or corrupt packet need not:
         copy a bounded span and terminate ourselves, never strncpy from
         a field that may have no NUL in it. */
      if (copy > JLINK_DISC_NAME_MAX - 1)
         copy = JLINK_DISC_NAME_MAX - 1;
      memcpy(name, buf + 8, copy);
      name[copy] = '\0';
   }
   return 1;
}

void JLinkDiscPeersReset(void)
{
   memset(discPeers, 0, sizeof(discPeers));
   discPeerCount = 0;
}

int JLinkDiscPeerSeen(const char *addr, const char *name, int device,
                      int port, uint32_t now_ms)
{
   int i;

   if (!addr || !addr[0])
      return 0;

   for (i = 0; i < discPeerCount; i++)
   {
      if (strcmp(discPeers[i].addr, addr) == 0
          && discPeers[i].port == port)
      {
         discPeers[i].last_seen_ms = now_ms;
         discPeers[i].device       = device;
         return 0;   /* known peer refreshed -- option list unchanged */
      }
   }

   if (discPeerCount >= JLINK_DISC_MAX_PEERS)
      return 0;

   memset(&discPeers[discPeerCount], 0, sizeof(JLinkPeer));
   strncpy(discPeers[discPeerCount].addr, addr, JLINK_DISC_ADDR_MAX - 1);
   if (name)
      strncpy(discPeers[discPeerCount].name, name, JLINK_DISC_NAME_MAX - 1);
   discPeers[discPeerCount].device       = device;
   discPeers[discPeerCount].port         = port;
   discPeers[discPeerCount].last_seen_ms = now_ms;
   discPeerCount++;
   return 1;
}

int JLinkDiscPeerExpire(uint32_t now_ms)
{
   int i = 0, changed = 0;

   while (i < discPeerCount)
   {
      /* Unsigned subtraction so a wrapped millisecond clock cannot make
         a fresh peer look ancient. */
      if ((uint32_t)(now_ms - discPeers[i].last_seen_ms)
          >= JLINK_DISC_EXPIRE_MS)
      {
         if (i < discPeerCount - 1)
            memmove(&discPeers[i], &discPeers[i + 1],
                    sizeof(JLinkPeer) * (size_t)(discPeerCount - i - 1));
         discPeerCount--;
         memset(&discPeers[discPeerCount], 0, sizeof(JLinkPeer));
         changed = 1;
         continue;
      }
      i++;
   }
   return changed;
}

int JLinkDiscPeerCount(void)
{
   return discPeerCount;
}

const JLinkPeer *JLinkDiscPeerAt(int i)
{
   if (i < 0 || i >= discPeerCount)
      return NULL;
   return &discPeers[i];
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `DEVELOPER_DIR=/Library/Developer/CommandLineTools make test/test_jlink_discover && ./test/test_jlink_discover`
Expected: `test_jlink_discover: all passed`

- [ ] **Step 6: Lint and wire into the suite**

Run: `bash scripts/c89-lint.sh src/jerry/jlink_discover.c`
Expected: `C89 lint passed`

Add `src/jerry/jlink_discover.o` to the source list in `Makefile.common` next to `jlink_tcp.o`. Add `test/test_jlink_discover` to the `test:` prerequisite list (~line 997), and invoke it in the recipe next to `./test/test_jlink`:

```make
	./test/test_jlink_discover
```

- [ ] **Step 7: Commit**

```bash
git add src/jerry/jlink_discover.c src/jerry/jlink_discover.h test/test_jlink_discover.c Makefile Makefile.common
git commit -m "feat(netlink): discovery beacon codec and peer table"
```

---

### Task 2: UDP socket layer and per-frame integration

**Files:**
- Modify: `src/jerry/jlink_discover.c` (append socket layer)
- Modify: `src/jerry/jlink_discover.h` (append socket API)
- Modify: `src/jerry/jlink.c` (drive from `JLinkPoll`)
- Create: `test/tools/netlink_discover_pair.sh`
- Modify: `Makefile` (invoke the pair test)

**Interfaces:**
- Consumes: everything from Task 1.
- Produces:
  - `int  JLinkDiscStart(int listen_only, int device, int link_port)` → 1 on success
  - `void JLinkDiscStop(void)`
  - `int  JLinkDiscPoll(uint32_t now_ms)` → 1 if the peer set changed this call
  - `int  JLinkDiscActive(void)`
  - `int  JLinkDiscConsumeChanged(void)` -- returns and clears the "peer set changed" flag; Task 4 consumes this
  - `uint32_t JLinkNowMs(void)` (exposed from `jlink.c`, declared in `jlink.h`)

- [ ] **Step 1: Write the failing pair test**

Create `test/tools/netlink_discover_pair.sh`:

```bash
#!/usr/bin/env bash
# netlink_discover_pair.sh -- two cores on one host: the server beacons,
# the client's peer table must populate.  SO_REUSEADDR/SO_REUSEPORT on the
# listener is what makes two instances on one machine work at all, which
# is exactly how every other netlink test runs.
set -u
CORE="${1:?usage: netlink_discover_pair.sh <core>}"
BIN="$(dirname "$0")/netlink_discover_probe"
if [ ! -x "$BIN" ]; then
    echo "netlink_discover_pair: $BIN not built" >&2
    exit 1
fi
"$BIN" "$CORE" --role beacon &
BPID=$!
sleep 1
"$BIN" "$CORE" --role listen --expect 1
rc=$?
kill "$BPID" 2>/dev/null; wait "$BPID" 2>/dev/null
if [ "$rc" -eq 0 ]; then
    echo "netlink_discover_pair: PASS"
else
    echo "netlink_discover_pair: FAIL (listener saw no peer)" >&2
fi
exit "$rc"
```

Run: `bash test/tools/netlink_discover_pair.sh ./virtualjaguar_libretro.dylib`
Expected: FAIL — `netlink_discover_probe` not built.

- [ ] **Step 2: Append the socket API to the header**

```c
/* Socket layer.  listen_only = 1 for client/auto (listen but never
   beacon); 0 for a host (beacon AND listen, so a host still sees peers).
   link_port is the TCP port advertised in the beacon. */
int  JLinkDiscStart(int listen_only, int device, int link_port);
void JLinkDiscStop(void);
int  JLinkDiscPoll(uint32_t now_ms);
int  JLinkDiscActive(void);
```

- [ ] **Step 3: Implement the socket layer**

Append to `src/jerry/jlink_discover.c`, guarded like `jlink_tcp.c` does:

```c
/* Guard mirrors jlink_tcp.c verbatim: discovery has exactly the same
   platform surface as the TCP transport, and divergent guards are how one
   builds and the other does not. */
#if defined(_WIN32) || defined(__unix__) || defined(__APPLE__) || \
    defined(__linux__) || defined(__ANDROID__)
#define JLINK_DISC_HAVE_NET 1
#endif

#ifdef JLINK_DISC_HAVE_NET

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

static int      discSock       = -1;
static int      discListenOnly = 1;
static int      discDevice     = 0;
static int      discLinkPort   = 0;
static uint32_t discLastBeacon = 0;
static char     discSelfName[JLINK_DISC_NAME_MAX];

int JLinkDiscStart(int listen_only, int device, int link_port)
{
   struct sockaddr_in sa;
   int one = 1;

   JLinkDiscStop();
   JLinkDiscPeersReset();

   discSock = (int)socket(AF_INET, SOCK_DGRAM, 0);
   if (discSock < 0)
      return 0;

   /* Two cores on ONE machine is the normal dev/test layout (see
      netlink_pair_test.sh, uv_modem_game_test.sh).  Without these the
      second instance cannot bind and discovery silently does nothing on
      exactly the setup used to test it. */
   setsockopt(discSock, SOL_SOCKET, SO_REUSEADDR,
              (const char *)&one, sizeof(one));
#ifdef SO_REUSEPORT
   setsockopt(discSock, SOL_SOCKET, SO_REUSEPORT,
              (const char *)&one, sizeof(one));
#endif
   setsockopt(discSock, SOL_SOCKET, SO_BROADCAST,
              (const char *)&one, sizeof(one));

   memset(&sa, 0, sizeof(sa));
   sa.sin_family      = AF_INET;
   sa.sin_addr.s_addr = htonl(INADDR_ANY);
   sa.sin_port        = htons((unsigned short)JLINK_DISC_PORT);
   if (bind(discSock, (struct sockaddr *)&sa, sizeof(sa)) != 0)
   {
      JLinkDiscStop();
      return 0;
   }

#ifdef _WIN32
   { u_long nb = 1; ioctlsocket(discSock, FIONBIO, &nb); }
#else
   fcntl(discSock, F_SETFL, fcntl(discSock, F_GETFL, 0) | O_NONBLOCK);
#endif

   discListenOnly = listen_only;
   discDevice     = device;
   discLinkPort   = link_port;
   discLastBeacon = 0;

   discSelfName[0] = '\0';
#ifdef _WIN32
   { DWORD n = JLINK_DISC_NAME_MAX - 1; GetComputerNameA(discSelfName, &n); }
#else
   if (gethostname(discSelfName, JLINK_DISC_NAME_MAX - 1) != 0)
      discSelfName[0] = '\0';
   discSelfName[JLINK_DISC_NAME_MAX - 1] = '\0';
#endif
   if (!discSelfName[0])
      strcpy(discSelfName, "jaguar");
   return 1;
}

void JLinkDiscStop(void)
{
   if (discSock >= 0)
   {
#ifdef _WIN32
      closesocket(discSock);
#else
      close(discSock);
#endif
   }
   discSock = -1;
}

int JLinkDiscActive(void)
{
   return discSock >= 0;
}

int JLinkDiscPoll(uint32_t now_ms)
{
   uint8_t  pkt[JLINK_DISC_PKT_LEN];
   struct sockaddr_in from;
   char     addr[JLINK_DISC_ADDR_MAX];
   char     name[JLINK_DISC_NAME_MAX];
   int      dev, port, changed = 0;
   socklen_t flen;
   int      n;

   if (discSock < 0)
      return 0;

   if (!discListenOnly
       && (discLastBeacon == 0 || (uint32_t)(now_ms - discLastBeacon) >= 1000))
   {
      struct sockaddr_in to;
      uint8_t out[JLINK_DISC_PKT_LEN];
      memset(&to, 0, sizeof(to));
      to.sin_family      = AF_INET;
      to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
      to.sin_port        = htons((unsigned short)JLINK_DISC_PORT);
      if (JLinkDiscEncode(out, sizeof(out), discDevice, discLinkPort,
                          discSelfName))
         sendto(discSock, (const char *)out, JLINK_DISC_PKT_LEN, 0,
                (struct sockaddr *)&to, sizeof(to));
      discLastBeacon = now_ms;
   }

   for (;;)
   {
      flen = sizeof(from);
      memset(&from, 0, sizeof(from));
      n = (int)recvfrom(discSock, (char *)pkt, sizeof(pkt), 0,
                        (struct sockaddr *)&from, &flen);
      if (n <= 0)
         break;
      if (!JLinkDiscDecode(pkt, (size_t)n, &dev, &port, name, sizeof(name)))
         continue;
      /* Ignore our own beacon.  Matched on name+port, not source IP:
         the same machine appears under different addresses depending on
         which interface the broadcast came back through. */
      if (!discListenOnly && port == discLinkPort
          && strcmp(name, discSelfName) == 0)
         continue;
      addr[0] = '\0';
      strncpy(addr, inet_ntoa(from.sin_addr), JLINK_DISC_ADDR_MAX - 1);
      addr[JLINK_DISC_ADDR_MAX - 1] = '\0';
      if (JLinkDiscPeerSeen(addr, name, dev, port, now_ms))
         changed = 1;
   }

   if (JLinkDiscPeerExpire(now_ms))
      changed = 1;
   return changed;
}

#else  /* no networking */

int  JLinkDiscStart(int a, int b, int c) { (void)a; (void)b; (void)c; return 0; }
void JLinkDiscStop(void) {}
int  JLinkDiscPoll(uint32_t t) { (void)t; return 0; }
int  JLinkDiscActive(void) { return 0; }

#endif
```

- [ ] **Step 4: Build the probe tool and run the pair test**

Create `test/tools/netlink_discover_probe.c` — a direct driver of the module (no core needed, but it takes the core path for CLI symmetry with the other netlink tools):

```c
#include <stdio.h>
#include <stdlib.h>      /* atoi */
#include <string.h>
#include "jlink_discover.h"

#ifdef _WIN32
#include <windows.h>
static uint32_t now_ms(void) { return (uint32_t)GetTickCount(); }
#else
#include <sys/time.h>
/* Wall clock, NOT clock(): clock() measures CPU time, and this probe
   spins, so a CPU-time clock would race ahead of the 10 s peer expiry
   this test exists to exercise. */
static uint32_t now_ms(void)
{
   struct timeval tv;
   gettimeofday(&tv, NULL);
   return (uint32_t)((uint32_t)tv.tv_sec * 1000u
                     + (uint32_t)(tv.tv_usec / 1000));
}
#endif

int main(int argc, char **argv)
{
   int listen_only = 1, expect = 0, i, spins;
   for (i = 1; i < argc; i++)
   {
      if (!strcmp(argv[i], "--role") && i + 1 < argc)
         listen_only = strcmp(argv[++i], "beacon") ? 1 : 0;
      else if (!strcmp(argv[i], "--expect") && i + 1 < argc)
         expect = atoi(argv[++i]);
   }
   if (!JLinkDiscStart(listen_only, 0, 42171))
   {
      printf("discover_probe: start failed\n");
      return 2;
   }
   for (spins = 0; spins < 600; spins++)
   {
      JLinkDiscPoll(now_ms());
      if (expect && JLinkDiscPeerCount() >= expect)
      {
         printf("discover_probe: saw %d peer(s)\n", JLinkDiscPeerCount());
         JLinkDiscStop();
         return 0;
      }
   }
   JLinkDiscStop();
   return expect ? 1 : 0;
}
```

Add the Makefile rule:

```make
test/tools/netlink_discover_probe: test/tools/netlink_discover_probe.c src/jerry/jlink_discover.c
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) -Itest \
		-o $@ test/tools/netlink_discover_probe.c src/jerry/jlink_discover.c
```

Run: `DEVELOPER_DIR=/Library/Developer/CommandLineTools make test/tools/netlink_discover_probe && bash test/tools/netlink_discover_pair.sh ./virtualjaguar_libretro.dylib`
Expected: `netlink_discover_pair: PASS`

- [ ] **Step 5: Drive discovery from JLinkPoll**

In `src/jerry/jlink.c`, `#include "jlink_discover.h"` and inside `JLinkPoll()`, before the existing TCP handling, add:

```c
   if (JLinkDiscActive())
      jlinkDiscChanged |= JLinkDiscPoll(JLinkNowMs());
```

with a file-scope `static int jlinkDiscChanged = 0;` and an accessor
`int JLinkDiscConsumeChanged(void)` that returns and clears it, declared in
`jlink.h`.

**There is no existing `JLinkNowMs()` — you must add it.** `jlink.c` has
`static long long JLinkNowUsec(void)`, in *microseconds*, and it is defined
only inside the `JLINK_HAVE_WAIT` platform ladder. Add next to it:

```c
uint32_t JLinkNowMs(void)
{
#ifdef JLINK_HAVE_WAIT
   return (uint32_t)(JLinkNowUsec() / 1000LL);
#else
   /* No wait helper on this platform means no sockets either, so
      discovery is inert here and a frozen clock is harmless. */
   return 0;
#endif
}
```

Declare it in `jlink.h`. Note `JLinkNowUsec` is `static` and defined twice
(once per platform branch) — `JLinkNowMs` must be placed after both
definitions, not inside either branch.

- [ ] **Step 6: Lint, wire into the suite, verify**

```bash
bash scripts/c89-lint.sh src/jerry/jlink_discover.c
bash scripts/c89-lint.sh src/jerry/jlink.c
```

Add `test/tools/netlink_discover_probe` to the `test:` prerequisites and
`bash test/tools/netlink_discover_pair.sh ./$(TARGET)` to the recipe after
`netlink_pair_test.sh`.

Run: `DEVELOPER_DIR=/Library/Developer/CommandLineTools make TEST_EXPORTS=1 test > /tmp/t.log 2>&1; echo "EXIT=$?"; grep -E "discover|netlink_pair|voicemodem_pair|uv_modem" /tmp/t.log`
Expected: `EXIT=0`, discovery PASS, and every pre-existing netlink test still PASS.

> Capture the exit code without a pipe. `${PIPESTATUS[0]}` is a bashism and this repo's shell is zsh — piping `make test` into `tail` throws away both the exit status and the results you need to read.

- [ ] **Step 7: Commit**

```bash
git add src/jerry/jlink_discover.c src/jerry/jlink_discover.h src/jerry/jlink.c src/jerry/jlink.h test/tools/netlink_discover_probe.c test/tools/netlink_discover_pair.sh Makefile
git commit -m "feat(netlink): UDP discovery beacon and listener"
```

---

### Task 3: `auto` link mode and per-mode option visibility

**Files:**
- Modify: `libretro_core_options.h` (add `auto` value, retitle, categorize)
- Modify: `libretro.c` (resolver in `netlink_apply` path; visibility callback)
- Modify: `test/tools/test_option_visibility.c`

**Interfaces:**
- Consumes: `JLinkDiscStart/Stop` from Task 2.
- Produces: `static int netlink_resolve_mode(const char *opt_value)` in `libretro.c`.

- [ ] **Step 1: Write the failing visibility test**

Extend `test/tools/test_option_visibility.c` with a case asserting that with
`virtualjaguar_netlink=auto` the keys `virtualjaguar_netlink_host` and
`virtualjaguar_netlink_port` are hidden, with `tcp_client` both are shown, and
with `tcp_server` only the port is shown.

Run: `DEVELOPER_DIR=/Library/Developer/CommandLineTools make test/tools/test_option_visibility && ./test/tools/test_option_visibility ./virtualjaguar_libretro.dylib`
Expected: FAIL — `auto` is not a recognised value yet.

- [ ] **Step 2: Add the `auto` value**

In `libretro_core_options.h`, change the `virtualjaguar_netlink` entry:

```c
      "virtualjaguar_netlink",
      "Network Link",
      NULL,
      "How this console's serial port reaches another player. 'Automatic' uses your frontend's netplay session when one is running -- nothing to configure, no addresses to type -- and otherwise stays idle. 'TCP Host'/'TCP Client' link two emulators directly without netplay; the client picks a host below, and hosts on your LAN are found automatically. 'Loopback' echoes back to this console for testing link-detect menus with no partner.",
      NULL,
      "network",
      {
         { "auto",       "Automatic (use netplay when available)" },
         { "disabled",   "Off" },
         { "loopback",   "Loopback (echo to self)" },
         { "tcp_server", "TCP Host (listen)" },
         { "tcp_client", "TCP Client (connect)" },
         { NULL, NULL },
      },
      "auto"
```

- [ ] **Step 3: Implement the resolver**

In `libretro.c`, replace the inline `strcmp` ladder in `check_variables()`:

```c
/* Resolve the Network Link option to a concrete JLINK_MODE_*.
 *
 * "auto" means netplay-when-live, else idle.  The design doc originally
 * had auto also fall back to "the direct mode last chosen explicitly";
 * that was dropped, because with a single option key there is nowhere to
 * read a previous choice from -- selecting "auto" overwrites it -- so
 * honouring it would need hidden persisted state whose behaviour the user
 * cannot see or predict across restarts.
 *
 * Auto deliberately never dials a discovered peer by itself either; with
 * the Voice Modem that would place a call the user did not initiate. */
static int netlink_resolve_mode(const char *v)
{
   if (!v)
      return JLINK_MODE_DISABLED;
   if (!strcmp(v, "loopback"))   return JLINK_MODE_LOOPBACK;
   if (!strcmp(v, "tcp_server")) return JLINK_MODE_TCP_SERVER;
   if (!strcmp(v, "tcp_client")) return JLINK_MODE_TCP_CLIENT;
   if (!strcmp(v, "auto"))
   {
      if (JLinkMode() == JLINK_MODE_NETPACKET)
         return JLINK_MODE_NETPACKET;
      return JLINK_MODE_DISABLED;
   }
   return JLINK_MODE_DISABLED;
}
```

Start discovery whenever the resolved mode is `TCP_SERVER` (beacon+listen) or
`TCP_CLIENT`/`auto` (listen only); stop it otherwise.

- [ ] **Step 4: Implement per-mode visibility**

In the existing `update_option_visibility` path, hide
`virtualjaguar_netlink_host` unless the mode is `tcp_client`, and
`virtualjaguar_netlink_port` unless it is `tcp_server` or `tcp_client`.

- [ ] **Step 5: Verify**

Run: `./test/tools/test_option_visibility ./virtualjaguar_libretro.dylib`
Expected: PASS.
Run: `bash scripts/c89-lint.sh libretro.c` → `C89 lint passed`

- [ ] **Step 6: Commit**

```bash
git add libretro.c libretro_core_options.h test/tools/test_option_visibility.c
git commit -m "feat(netlink): automatic link mode and per-mode option visibility"
```

---

### Task 4: Discovered-host option list

**Files:**
- Modify: `libretro.c`

**Interfaces:**
- Consumes: `JLinkDiscPeerCount/At` (Task 1), `JLinkDiscConsumeChanged` (Task 2).
- Produces: `static void netlink_rebuild_host_options(void)`.

- [ ] **Step 1: Implement the rebuild**

Add a file-scope static beside `netlink_was_up`, and reset it in
`retro_deinit()` alongside that one (iOS never dlcloses the core):

```c
static uint32_t netlink_last_rebuild_ms = 0;
```

Then in `retro_run`, next to the link-state edge block:

```c
   /* Rebuild the host picker only when the peer set actually changed --
    * never on a timer.  A second SET_CORE_OPTIONS_V2 tears down and
    * rebuilds RetroArch's whole option manager (runloop.c), so doing it
    * per beacon would thrash the menu under the user's thumb. */
   {
      uint32_t disc_now = JLinkNowMs();
      if (JLinkDiscConsumeChanged()
          && (uint32_t)(disc_now - netlink_last_rebuild_ms) >= 2000)
      {
         netlink_rebuild_host_options();
         netlink_last_rebuild_ms = disc_now;
      }
   }
```

`JLinkNowMs()` is the helper added in Task 2; it is declared in `jlink.h`,
which `libretro.c` already includes.

`netlink_rebuild_host_options()` builds a `retro_core_option_v2_definition`
array whose `virtualjaguar_netlink_host` values are: `127.0.0.1`, one entry
per peer labelled `"<name> - <addr>"` (append `" (JagLink)"` or
`" (Voice Modem)"` when the peer's device differs from ours), then
`jaghub.local` and `vj_netlink.txt`. Cap at `JLINK_DISC_MAX_PEERS` entries;
the array is `static` so it outlives the call, as the frontend keeps pointers.

- [ ] **Step 2: Verify no regression**

Run: `DEVELOPER_DIR=/Library/Developer/CommandLineTools make TEST_EXPORTS=1 test > /tmp/t.log 2>&1; echo "EXIT=$?"`
Expected: `EXIT=0`.

Manual: two RetroArch instances, one `tcp_server`, one `tcp_client`; the
client's Network Link Host list gains the host within ~2 s.

- [ ] **Step 3: Commit**

```bash
git add libretro.c
git commit -m "feat(netlink): populate host picker from LAN discovery"
```

---

### Task 5: OSD narration

**Files:**
- Modify: `libretro.c`

**Interfaces:**
- Consumes: the resolver and edge tracker from Tasks 2–3.
- Produces: `static void netlink_osd(const char *fmt, ...)`.

- [ ] **Step 1: Implement the helper**

```c
/* OSD narration.  Mirrors the [NETLINK] log lines so screen and log
 * always agree; fires on transitions only, never per frame. */
static void netlink_osd(const char *fmt, ...)
{
   struct retro_message_ext msg;
   char text[256];
   va_list ap;

   va_start(ap, fmt);
   vsnprintf(text, sizeof(text), fmt, ap);
   va_end(ap);

   memset(&msg, 0, sizeof(msg));
   msg.msg      = text;
   msg.duration = 4000;
   msg.priority = 2;
   msg.level    = RETRO_LOG_INFO;
   msg.target   = RETRO_MESSAGE_TARGET_OSD;
   msg.type     = RETRO_MESSAGE_TYPE_NOTIFICATION;
   msg.progress = -1;
   environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &msg);
}
```

- [ ] **Step 2: Call it at each transition**

Emit at: mode resolution after load; link UP; link DOWN; peer-set change
(`"Found %d Jaguar host(s) on the LAN"`); device mismatch; and the idle case
`"Voice Modem selected but link is idle -- start netplay or pick a host"`.

- [ ] **Step 3: Verify**

Run: `bash scripts/c89-lint.sh libretro.c` → `C89 lint passed`
Run the suite; expect `EXIT=0`.
Manual: confirm each message appears once, in RetroArch, and matches the log.

- [ ] **Step 4: Commit**

```bash
git add libretro.c
git commit -m "feat(netlink): narrate link state on the OSD"
```

---

### Task 6: Documentation

**Files:**
- Modify: `libretro_core_options.h` (host option description)
- Modify: `docs/netlink-design.md`
- Modify: `docs/voice-modem.md`

- [ ] **Step 1: Shorten the host description and state the file format**

```c
      "virtualjaguar_netlink_host",
      "Network Link Host (TCP Client)",
      NULL,
      "Which host to connect to. Hosts running on your LAN appear here automatically within a couple of seconds. 'From file' reads <system>/vj_netlink.txt: one line, the address only, no port -- for example '192.168.1.42' or 'myhost.local'. The port comes from 'Network Link Port'. The VJ_NETLINK_HOST environment variable overrides this option.",
```

- [ ] **Step 2: Update the design docs**

Add a transport-selection section to `docs/netlink-design.md` covering the
three modes, the beacon wire format (referencing
[`docs/netlink-ux-design.md`](netlink-ux-design.md)), and the iOS Bonjour
constraint. Update the troubleshooting table in `docs/voice-modem.md` for
`auto` and the new OSD strings.

- [ ] **Step 3: Commit**

```bash
git add libretro_core_options.h docs/netlink-design.md docs/voice-modem.md
git commit -m "docs(netlink): document link modes and vj_netlink.txt format"
```

---

## Self-Review

**Spec coverage:** option model → Task 3; discovery protocol → Tasks 1–2;
OSD → Task 5; dynamic host list → Task 4; documentation → Task 6; testing
table → covered across Tasks 1, 2, 3 (`test_option_visibility`) and the
existing suite re-run in every task.

**Deferred by design, per the spec's "not in scope":** mDNS, WAN discovery,
netpacket takeover changes, modem/JagLink wire-format changes.

**Follow-up ticket to file (not part of this plan):** upstream RetroArch PR
adding `_vjaglink._tcp` to `NSBonjourServices` in the iOS and tvOS
`Info.plist`, without which mDNS from this core can never work on those
platforms.
