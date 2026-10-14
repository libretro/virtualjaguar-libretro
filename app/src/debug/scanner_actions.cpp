#include "scanner_actions.h"

#include <cstdio>

#include "cheat_scan.h"

namespace vj {

std::string scanCheatCode(uint32_t addr, int64_t value, unsigned width) {
    uint8_t b[4] = {0, 0, 0, 0};
    CheatScan::encode(value, width, b);
    return memoryCheatToCode(addr, b, width);
}

bool scanAddCheat(CheatManager& cm, const DebugSession& session, uint32_t addr, int64_t value, unsigned width,
                  const std::string& desc, std::string& err) {
    if (width != 1 && width != 2 && width != 4) {
        err = "bad value width";
        return false;
    }
    if (!session.enabled()) {
        err = session.disabledReason();
        return false;
    }
    if (cm.blockedNow()) {
        err = "Cheats are disabled in hardcore mode";
        return false;
    }
    if ((uint64_t)addr + width > DebugSession::kDramSize) {
        err = "cheats can only write the 2 MB main RAM";
        return false;
    }
    std::string d = desc;
    if (d.empty()) {
        char b[40];
        std::snprintf(b, sizeof b, "Scanner %08X", (unsigned)addr);
        d = b;
    }
    return cm.addUser(d, scanCheatCode(addr, value, width), err);
}

bool scanPoke(DebugSession& session, uint32_t addr, int64_t value, unsigned width, std::string& err) {
    if (width != 1 && width != 2 && width != 4) {
        err = "bad value width";
        return false;
    }
    uint8_t b[4] = {0, 0, 0, 0};
    CheatScan::encode(value, width, b);
    int rc = session.poke(addr, b, width);
    if (rc < 0) {
        err = DebugSession::errorText(rc);
        return false;
    }
    return true;
}

}  // namespace vj
