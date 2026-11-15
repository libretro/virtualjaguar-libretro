// The command line's debugger requests (--break, --watch), parsed.  A homebrew build script starts the app with them so
// a session comes up already set: `virtualjaguar --break-at-boot --break main --watch score:2:w game.cof`.
// Pure text handling; resolving a name needs the symbols (SymbolStore), which App does at the first load.
#pragma once

#include <string>

namespace vj {

// --break [68k:|gpu:|dsp:]ADDRESS|SYMBOL[+OFFSET]
struct BreakSpec {
    int target = -1;      // VJ_DBG_TARGET_*, or -1: by the address (GPU / DSP local RAM, else the 68000)
    std::string where;    // "$802000", "main", "main+$12": what SymbolStore::resolve takes
};
bool parseBreakSpec(const std::string& text, BreakSpec& out, std::string& err);

// --watch ADDRESS|SYMBOL[:LENGTH][:r|w|rw]     LENGTH is decimal bytes (default 1); the default kind is w
struct WatchSpec {
    std::string where;
    unsigned len = 1;
    unsigned kind = 0;    // VJ_DBG_WATCH_*
};
bool parseWatchSpec(const std::string& text, WatchSpec& out, std::string& err);

}  // namespace vj
