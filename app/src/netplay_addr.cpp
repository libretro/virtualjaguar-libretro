// This machine's network addresses, for the Netplay window ("tell your partner to join one of
// these").  IPv4 only: the core's link sockets are IPv4.
#include "netplay.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <iphlpapi.h>
#else
#  include <arpa/inet.h>
#  include <ifaddrs.h>
#  include <net/if.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

namespace vj {

namespace {

// 0 = private (RFC 1918) -- almost certainly the LAN; 1 = anything else routable.
int rankOf(const std::string& ip) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (std::sscanf(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return 2;
    if (a == 10) return 0;
    if (a == 192 && b == 168) return 0;
    if (a == 172 && b >= 16 && b <= 31) return 0;
    return 1;
}

bool usable(unsigned a, unsigned b) {
    if (a == 127) return false;              // loopback
    if (a == 169 && b == 254) return false;  // link-local (no DHCP lease)
    if (a == 0) return false;
    return true;
}

}  // namespace

std::vector<LanAddress> Netplay::lanAddresses() {
    std::vector<LanAddress> out;
#ifdef _WIN32
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buf;
    ULONG rc = ERROR_BUFFER_OVERFLOW;
    for (int tries = 0; tries < 3 && rc == ERROR_BUFFER_OVERFLOW; tries++) {
        buf.assign(size, 0);
        rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                  nullptr, (IP_ADAPTER_ADDRESSES*)buf.data(), &size);
    }
    if (rc == NO_ERROR) {
        for (IP_ADAPTER_ADDRESSES* a = (IP_ADAPTER_ADDRESSES*)buf.data(); a; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            char name[256] = "";
            WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1, name, sizeof name - 1, nullptr, nullptr);
            for (IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next) {
                if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
                const sockaddr_in* sin = (const sockaddr_in*)u->Address.lpSockaddr;
                const unsigned char* q = (const unsigned char*)&sin->sin_addr;
                if (!usable(q[0], q[1])) continue;
                char ip[32];
                std::snprintf(ip, sizeof ip, "%u.%u.%u.%u", q[0], q[1], q[2], q[3]);
                out.push_back({name, ip});
            }
        }
    }
#else
    struct ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (struct ifaddrs* a = list; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
            if (!(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK)) continue;
            const sockaddr_in* sin = (const sockaddr_in*)a->ifa_addr;
            const unsigned char* q = (const unsigned char*)&sin->sin_addr;
            if (!usable(q[0], q[1])) continue;
            char ip[32];
            std::snprintf(ip, sizeof ip, "%u.%u.%u.%u", q[0], q[1], q[2], q[3]);
            out.push_back({a->ifa_name ? a->ifa_name : "", ip});
        }
        freeifaddrs(list);
    }
#endif
    std::stable_sort(out.begin(), out.end(),
                     [](const LanAddress& x, const LanAddress& y) { return rankOf(x.ip) < rankOf(y.ip); });
    return out;
}

std::string Netplay::machineName() {
#ifdef _WIN32
    WSADATA w;
    bool up = WSAStartup(MAKEWORD(2, 2), &w) == 0;
#endif
    char name[256] = "";
    int rc = gethostname(name, sizeof name - 1);
#ifdef _WIN32
    if (up) WSACleanup();
#endif
    return rc == 0 ? std::string(name) : std::string();
}

}  // namespace vj
