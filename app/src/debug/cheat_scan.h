// Cheat scanner engine: a Cheat-Engine / RetroArch style "search the RAM for a value" state machine.
//
// Pure logic -- no ImGui, no SDL, no core, no DebugSession: every call is handed the RAM image to look at
// (a pointer and a size), so it unit-tests on a byte vector (app/tests/debug_scan_test.cpp) and runs the
// same against the core's real RAM (app/tests/debug_scan_e2e.cpp).  The window is scanner_ui.cpp.
//
// A search is
//   begin()  a new search: fixes the value width (1/2/4 bytes, big-endian like the 68000 sees them),
//            signedness and alignment, optionally an address window, and makes the first pass
//   next()   narrows the candidate set against the RAM as it is NOW, comparing with the value each
//            candidate had at the previous pass ("previous")
//   undo()   puts the candidate set AND the previous-values snapshot back one pass
// The full candidate set is always kept (a bitset over the RAM, one bit per address); results() pages
// through it, so a UI that shows only the first 1000 loses nothing.
//
// Semantics worth knowing
//   * Width, signedness, alignment and the address window are fixed by begin(); change one and you start over.
//   * A candidate is an address a whole value fits at (addr + width <= size).  Aligned means addr % width == 0.
//   * Signedness only changes the ordering modes (Greater, Less, Range, Increased, Decreased) and what the
//     values mean; Equal / NotEqual / Changed / Unchanged / IncreasedBy / DecreasedBy are bit patterns.
//   * Increased / Decreased are ordinary comparisons with the previous value (a counter that wrapped
//     255 -> 0 counts as decreased).  IncreasedBy / DecreasedBy are modular at the width: 255 -> 1 is
//     "increased by 2", so wrapping counters are still found.
//   * The first pass has no previous value, so only Unknown, Equal, NotEqual, Greater, Less and Range are
//     allowed there.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vj {

enum class ScanMode {
    Unknown,       // keep every address (first pass: "unknown initial value")
    Equal,         // == value
    NotEqual,      // != value
    Greater,       // > value
    Less,          // < value
    Range,         // value1 <= x <= value2
    Changed,       // != previous
    Unchanged,     // == previous
    Increased,     // > previous
    Decreased,     // < previous
    IncreasedBy,   // (x - previous) mod 2^width == value
    DecreasedBy,   // (previous - x) mod 2^width == value
};

struct ScanSpec {
    ScanMode mode = ScanMode::Unknown;
    int64_t value = 0;     // Equal / NotEqual / Greater / Less / the delta of IncreasedBy / DecreasedBy / Range low
    int64_t value2 = 0;    // Range high
};

struct ScanOptions {
    unsigned width = 1;        // bytes: 1, 2 or 4
    bool isSigned = false;
    bool aligned = true;       // addr % width == 0
    uint32_t first = 0;        // address window [first, last); last == 0 means "to the end of the image"
    uint32_t last = 0;
};

class CheatScan {
public:
    static constexpr size_t kMaxUndo = 16;      // passes that can be undone (each keeps a copy of the image)

    // ---- the search -------------------------------------------------------------------------
    // False with `err` set when the options or the spec are invalid for a first pass.  The image is copied.
    bool begin(const uint8_t* ram, size_t size, const ScanOptions& o, const ScanSpec& s, std::string* err);
    // Narrow against the RAM now.  `size` must be the size the search began with.
    bool next(const uint8_t* ram, size_t size, const ScanSpec& s, std::string* err);
    bool canUndo() const { return hist_.size() > 1; }
    bool undo();
    void reset();                                  // forget the search
    bool active() const { return !hist_.empty(); }

    // ---- state ------------------------------------------------------------------------------------
    const ScanOptions& options() const { return opt_; }
    size_t count() const { return active() ? hist_.back().count : 0; }
    size_t passes() const { return hist_.size(); }       // 1 after begin()
    size_t imageSize() const { return size_; }
    bool isCandidate(uint32_t addr) const;
    // Up to `max` candidate addresses in ascending order, skipping the first `skip` of them.
    void results(size_t skip, size_t max, std::vector<uint32_t>& out) const;
    // The value `addr` had at the last pass (0 if there is no search or it is out of range) ...
    int64_t previous(uint32_t addr) const;
    // ... and in any image the caller holds (live RAM): same width / signedness as the search.
    int64_t valueIn(const uint8_t* ram, size_t size, uint32_t addr) const;

    // ---- value helpers (also used by the window and the tests) -------------------------------------------
    static const char* modeName(ScanMode m);                // "Increased by", ...
    static bool modeNeedsValue(ScanMode m);                 // Equal ... Range, IncreasedBy, DecreasedBy
    static bool modeNeedsSecondValue(ScanMode m) { return m == ScanMode::Range; }
    static bool modeNeedsPrevious(ScanMode m);              // not allowed on the first pass
    // The smallest / largest value of a width and signedness.
    static int64_t minValue(unsigned width, bool isSigned);
    static int64_t maxValue(unsigned width, bool isSigned);
    // Big-endian decode / encode of 1, 2 or 4 bytes.
    static int64_t decode(const uint8_t* p, unsigned width, bool isSigned);
    static void encode(int64_t v, unsigned width, uint8_t* out);    // the low `width` bytes of v, big-endian
    // "12", "-12", "0x1F", "$1F" (a leading '-' allowed); false on anything else or overflow.
    static bool parseNumber(const std::string& s, int64_t& out);
    static std::string formatValue(int64_t v, unsigned width, bool hex);

private:
    struct Pass {
        std::vector<uint64_t> bits;     // candidate bitset over [0, size)
        std::vector<uint8_t> snap;      // the image at this pass: the next pass's "previous"
        size_t count = 0;
    };
    bool validSpec(const ScanSpec& s, bool first, std::string* err) const;
    void filter(const Pass& from, const uint8_t* ram, const ScanSpec& s, Pass& to) const;
    template <ScanMode M>
    void filterMode(const Pass& from, const uint8_t* ram, const ScanSpec& s, Pass& to) const;

    ScanOptions opt_;
    size_t size_ = 0;
    std::vector<Pass> hist_;            // hist_.back() is the current state
};

}  // namespace vj
