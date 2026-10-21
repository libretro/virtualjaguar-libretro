// Pure logic behind the Memory viewer/editor and the Palette (CLUT) viewer (debug tools WP2).
// No ImGui and no SDL: everything here runs against a DebugSession (so against the mock core
// interface in app/tests/debug_mock.h) and unit-tests headless.
//
//   formatting     big-endian 8/16/32-bit cells, ASCII column, hex <-> value parsing
//   regions        lookup by address, row clamping at a region end, go-to-address parsing
//   change marks   ChangeTracker: which bytes changed since the last refresh (fading)
//   search         SearchJob: hex bytes or text, incremental (a 6 MB cartridge never stalls the UI)
//   write policy   decideWrite(): what the user may write where, and WHY not; planWrite() adds
//                  the CLUT mirror; writeBytes() applies it through the session
//   colour         JTRM RGB16 (RBG 556) and CRY <-> 0xRRGGBB, the CLUT window's swatch maths
//
// All addresses are the 24-bit Jaguar bus; multi-byte values are host `unsigned`; memory bytes
// are big-endian bus order, exactly as the 68000 sees them (vj_debug_iface.h).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "debug_session.h"
#include "vj_debug_iface.h"

namespace vj {
namespace memview {

constexpr unsigned kBusSize = 0x1000000u;
constexpr unsigned kDramSize = 0x200000u;
constexpr unsigned kClutA = 0xF00400u, kClutB = 0xF00600u, kClutEntries = 256;
constexpr unsigned kClutEnd = 0xF00800u;   // one past the second table

// ---- parsing / formatting ---------------------------------------------------------------------
// Hex with an optional "$" or "0x" prefix and surrounding blanks; 1..8 digits.
bool parseHex(const std::string& s, unsigned& v);
// A bus address: parseHex, then <= $FFFFFF.
bool parseAddress(const std::string& s, unsigned& addr);
// Hex bytes, "DE AD be ef", "DEADBEEF" or "0xDE,0xAD"; an even number of digits, at least one byte.
bool parseHexBytes(const std::string& s, std::vector<uint8_t>& out);
// One cell's text typed by the user: hex digits only, at most 2*bytes of them (leading zeros free).
bool parseCell(const std::string& s, unsigned bytes, unsigned& v);

unsigned decodeBE(const uint8_t* p, unsigned bytes);          // bytes = 1, 2, 4
void encodeBE(uint8_t* p, unsigned bytes, unsigned v);
std::string hexCell(unsigned v, unsigned bytes);              // "0A", "0A0B", "0A0B0C0D"
char asciiChar(uint8_t b);                                    // printable ASCII, else '.'
// "00001000  00 01 02 03 ...  |....|" for `n` bytes at `addr`, cells of `bytes` (n a multiple of bytes
// is not required: a trailing partial cell is shown byte by byte).
std::string formatDumpLine(unsigned addr, const uint8_t* data, unsigned n, unsigned bytes, bool ascii);

// ---- regions -----------------------------------------------------------------------------------
// Index of the region holding `addr`, or -1.
int regionIndexAt(const std::vector<vj_debug_region>& regions, unsigned addr);
// How many of `want` bytes starting at `addr` lie inside the region (0 when addr is outside it).
unsigned clampToRegion(const vj_debug_region& r, unsigned addr, unsigned want);
// Rows needed to show the region at `perRow` bytes per row.
unsigned rowCount(const vj_debug_region& r, unsigned perRow);
// Bytes per row for a window with `availablePx` of room when one `bytes`-wide cell takes `cellPx`: a
// power-of-two number of cells, at most 16 bytes in all, at least one cell.  A multiple of `bytes`.
unsigned bytesPerRowFor(unsigned bytes, float availablePx, float cellPx);
// Go-to box: a hex address (optionally "$"/"0x"), which must be inside some region.  `idx` = that region.
// Returns false and fills `err` otherwise.
bool resolveGoto(const std::vector<vj_debug_region>& regions, const std::string& text, unsigned& addr, int& idx,
                 std::string& err);
// The 24-bit address a big-endian 32-bit pointer value refers to.
inline unsigned pointerTarget(unsigned be32value) { return be32value & (kBusSize - 1); }
// "RAM" / "ROM" / "registers" / "blocked" for a status line.
const char* regionKindText(const vj_debug_region& r);

// ---- change marks -------------------------------------------------------------------------------
// Tracks one window of bytes between refreshes.  update() is called with the bytes just read; a byte
// that differs from the previous read of the SAME address is "changed" and stays marked for `hold`
// updates.  Bytes that were not in the previous window (scrolled in, a different region, a go-to) have
// no baseline and are never marked, so moving the view does not light everything up.
class ChangeTracker {
public:
    explicit ChangeTracker(unsigned hold = 8) : hold_(hold) {}
    // `advance` = this is a real refresh (compare, count a tick); false = only the window moved (scroll, go-to).
    void update(unsigned addr, const uint8_t* data, unsigned n, bool advance = true);
    void reset();                                   // forget the baseline
    bool changed(unsigned addr) const;              // marked now
    unsigned window() const { return (unsigned)cur_.size(); }
    unsigned base() const { return base_; }
    unsigned updates() const { return tick_; }

private:
    unsigned hold_;
    unsigned base_ = 0;
    unsigned tick_ = 0;
    std::vector<uint8_t> cur_;
    std::vector<unsigned> lastChange_;   // tick_ of the last change, 0 = never
};

// ---- search ----------------------------------------------------------------------------------------
// Builds the needle for a text search (the bytes of the string; `ignoreCase` is applied when matching).
std::vector<uint8_t> textNeedle(const std::string& s);

// Looks for `needle` in one region, starting AFTER `from` (so "find next" advances) and wrapping once.
// Incremental: call step() until done().  A match never straddles the wrap or the region end.
class SearchJob {
public:
    SearchJob(const DebugSession& s, const vj_debug_region& region, unsigned from, std::vector<uint8_t> needle,
              bool ignoreCase = false);
    // Examines up to `budgetBytes` more bytes of the region.  Returns done().  The whole budget runs inside ONE
    // DebugSession::callCore (the reads nest on the emulation thread), not one wait per 64 KiB chunk.
    bool step(unsigned budgetBytes);
    bool done() const { return done_; }
    bool found() const { return found_; }
    unsigned address() const { return foundAddr_; }
    bool readFailed() const { return readFailed_; }
    float progress() const;                         // 0..1

private:
    struct Seg {
        unsigned start, end;   // [start, end) bus addresses
    };
    bool matchAt(const uint8_t* p) const;
    void stepOnCore(unsigned budgetBytes);
    const DebugSession& s_;
    std::vector<uint8_t> needle_;
    bool icase_;
    std::vector<Seg> segs_;
    size_t seg_ = 0;
    unsigned pos_ = 0;
    unsigned total_ = 0, scanned_ = 0;
    bool done_ = false, found_ = false, readFailed_ = false;
    unsigned foundAddr_ = 0;
};

// ---- the write policy -----------------------------------------------------------------------------------------
struct WriteDecision {
    bool ok = false;
    int err = 0;               // VJ_DBG_ERR_* when !ok
    std::string reason;        // for the status line: names the register / region and says what to do
};

// The registers whose write starts or stops something.  The app never sets VJ_DBG_POKE_DANGEROUS, so
// they stay refused whatever "Allow register writes" says (the core enforces the same set).
struct BlockedRegister {
    unsigned addr;
    unsigned len;
    const char* name;
    const char* why;
};
const BlockedRegister* blockedRegisters(int* n);
// The blocked register overlapping [addr, addr+n), else null.  BUTCH is covered by the region flag.
const BlockedRegister* blockedRegisterAt(unsigned addr, unsigned n);

// Can [addr, addr+n) be written?  All-or-nothing across the whole range.  RAM regions are free, except the
// CLUT ($F00400-$F007FF), which is treated as a register (colour-table edits are deliberate); ROM and BUTCH
// never; registers only with `allowRegisterWrites` and never the blocked set.
WriteDecision decideWrite(const std::vector<vj_debug_region>& regions, unsigned addr, unsigned n,
                          bool allowRegisterWrites);

struct WriteOp {
    unsigned addr;
    std::vector<uint8_t> data;
};
// What actually goes to the iface for a user write: the bytes themselves, plus the other CLUT table for
// any byte in $F00400-$F007FF (TOM mirrors them -- JTRM v8 "writing to either address range writes to
// both tables" -- and the interface does not promise to).  Runs are coalesced and sorted by address.
std::vector<WriteOp> planWrite(unsigned addr, const uint8_t* data, unsigned n);

// decideWrite (with the session's allowRegisterWrites) + planWrite + session.poke.  Returns n on success or a
// VJ_DBG_ERR_*; `why` (optional) gets the user-facing reason.
int writeBytes(DebugSession& s, unsigned addr, const uint8_t* data, unsigned n, std::string* why = nullptr);
int writeCell(DebugSession& s, unsigned addr, unsigned bytes, unsigned v, std::string* why = nullptr);

// ---- the palette ---------------------------------------------------------------------------------------------------
enum class ColorMode { Cry, Rgb16 };

// JTRM v8 (VMODE MODE 3): "Bits [0-5] are green, bits [6-10] are blue and bits [11-15] are red"
// (RBG 556, RRRRR BBBBB GGGGGG).  Expands exactly like the renderer's RGB16ToRGB32 table: no bit
// replication, so rgb888ToRgb16(rgb16ToRgb888(v)) == v for every v.  Returns 0x00RRGGBB.
uint32_t rgb16ToRgb888(unsigned v16);
unsigned rgb888ToRgb16(uint32_t rgb);

// CRY -> RGB through the core's own table (iface->cry_to_rgb), plus the inverse by nearest match.
class CryCodec {
public:
    // (Re)builds the 65536-entry table when `iface` differs from the last build.  False without cry_to_rgb.
    // Emulation-thread side (and debug_memory_unit); the windows use buildFrom().
    bool build(const vj_debug_iface* iface);
    // The table through DebugSession::buildCryTable (one callCore), once per s.views().epoch(); a refusal is
    // remembered for the epoch, so a core without cry_to_rgb does not retry every frame.
    bool buildFrom(DebugSession& s);
    bool ready() const { return !table_.empty(); }
    uint32_t decode(unsigned v16) const { return table_.empty() ? 0 : table_[v16 & 0xFFFF]; }
    // The 16-bit CRY value whose colour is nearest to `rgb` (ties: the lowest value).  decode(encode(c))
    // is the nearest reproducible colour, and for a colour that came out of decode() it is that colour.
    unsigned encode(uint32_t rgb) const;

private:
    const vj_debug_iface* built_ = nullptr;
    std::vector<uint32_t> table_;
    uint64_t epoch_ = ~0ull;   // buildFrom(): the epoch the table (or the refusal) is for
    bool refused_ = false;
};

// 0x00RRGGBB for a raw CLUT entry in the chosen mode.
uint32_t swatchColor(ColorMode m, unsigned raw16, const CryCodec& cry);
// The raw CLUT entry for an edited colour.
unsigned encodeSwatch(ColorMode m, uint32_t rgb, const CryCodec& cry);
// CLUT entry `i` (0..255): table A address, table B address.
inline unsigned clutAddrA(unsigned i) { return kClutA + 2 * (i & 0xFF); }
inline unsigned clutAddrB(unsigned i) { return kClutB + 2 * (i & 0xFF); }

}  // namespace memview
}  // namespace vj
