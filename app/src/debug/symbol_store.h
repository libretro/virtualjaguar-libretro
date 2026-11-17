// The symbols of the loaded program, shared by every debug tool: the Symbols window lists them, the CPU debugger
// labels its listing and takes a name in "Go to", a breakpoint remembers the name it was set on so it follows the
// function after a relink, and --break / --watch on the command line take names.  No ImGui and no core; the parser is
// symbol_table.*.  UI thread only.
//
// Where the symbols come from, in order: a file chosen by hand (the Symbols window's Load..., or --symbols), the loaded
// content itself, a .cof with the same name beside the content.  Nothing is read until ensure().
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "symbol_table.h"

namespace vj {

class SymbolStore {
public:
    // Reads a whole file; false when it is missing, not a regular file or implausibly large.  Empty: a plain binary
    // read with a 64 MiB cap (App installs one that goes through its own file access).
    using Reader = std::function<bool(const std::string& path, std::vector<unsigned char>& out)>;
    Reader reader;

    // The content the symbols belong to ("" = none).  A changed path forgets a file picked in the window (not a pinned
    // one) and marks the table stale.
    void setContent(const std::string& path);
    const std::string& contentPath() const { return contentPath_; }
    // A file chosen by hand; it wins over the content until the content changes.  `pinned` (--symbols) survives that.
    void setPicked(const std::string& path, bool pinned = false);
    // Forgets a file picked by hand, unless it is pinned (the game it was picked for is gone).
    void dropPicked();
    // The file on disk may have changed (a relink, Reload): the next ensure() reads again.
    void invalidate() { stale_ = true; }
    // Reads the sources if they are stale.  True when there are symbols.
    bool ensure();
    bool stale() const { return stale_; }

    const std::vector<Symbol>& symbols() const { return symbols_; }
    bool empty() const { return symbols_.empty(); }
    const std::string& sourcePath() const { return sourcePath_; }     // where the current symbols came from, "" for none
    const std::string& status() const { return status_; }            // one line for a window: what was read, or why not
    bool statusIsError() const { return statusIsError_; }
    uint64_t generation() const { return generation_; }              // bumps on every read: a cache keyed on it is current

    // ---- name -> address ------------------------------------------------------------------------------------
    // Exact match first, else a case-insensitive one when it is the only one.
    bool find(const std::string& name, uint32_t& value) const;
    // What a person types: "main", "main+$12", "main+12" (the offset is hex), "$802000", "0x802000", "802000".  A name
    // that is also valid hex ("ABCD") is taken as the symbol when one has exactly that spelling.  The result is masked
    // to the 24-bit bus.
    bool resolve(const std::string& expr, uint32_t& addr) const;

    // ---- address -> name ------------------------------------------------------------------------------------
    // The label at exactly `addr`, or null.  Equates (absolute symbols) below $1000 are not labels: `SIZE equ 8` must
    // not name the bus-error vector.
    const Symbol* at(uint32_t addr) const;
    // The closest label at or below `addr`, at most `maxOffset` bytes away.  Equates never count here.
    bool nearest(uint32_t addr, uint32_t maxOffset, std::string& name, uint32_t& offset) const;
    // "main", "main+$12", or "" when nothing is within `maxOffset`.
    std::string describe(uint32_t addr, uint32_t maxOffset = 0x400) const;
    // A disassembly line with the names of the addresses it mentions appended: "jsr $802340" -> "jsr $802340  ; draw".
    // Only $-prefixed values of five to eight hex digits are looked at, so an immediate like #$1234 is left alone.
    std::string annotate(const std::string& text) const;

private:
    void load();
    void adopt(const std::string& path, std::vector<Symbol>& symbols);
    void setStatus(const std::string& m, bool err);
    SymbolParse readSymbols(const std::string& path, std::vector<Symbol>& out) const;

    std::string contentPath_;
    std::string pickedPath_;
    bool pinned_ = false;
    bool stale_ = true;
    std::vector<Symbol> symbols_;
    std::vector<std::pair<uint32_t, uint32_t>> byAddr_;   // (value, index into symbols_), sorted; labels only
    std::string sourcePath_;
    std::string status_;
    bool statusIsError_ = false;
    uint64_t generation_ = 0;
};

// The same path with the extension replaced (a path with none just gains one).
std::string symbolPathWithExtension(const std::string& path, const char* ext);

}  // namespace vj
