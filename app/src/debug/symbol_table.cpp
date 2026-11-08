// See symbol_table.h.  Every offset comes from an untrusted file: the sums are 64-bit and checked before each read.
#include "symbol_table.h"

#include "vj_debug_iface.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace vj {

namespace {

uint32_t be16(const uint8_t* p) { return ((uint32_t)p[0] << 8) | p[1]; }
uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

std::string fixedName(const uint8_t* p, size_t cap) {
    size_t n = 0;
    while (n < cap && p[n]) n++;
    return std::string((const char*)p, n);
}

// The string table: a long holding its own total size, then NUL-terminated names.
struct Strings {
    const uint8_t* base = nullptr;
    uint32_t size = 0;
    // False when `off` is outside the table or its name has no terminator.
    bool get(uint32_t off, std::string& out) const {
        if (off < 4 || off >= size) return false;
        const void* end = std::memchr(base + off, 0, size - off);
        if (!end) return false;
        out.assign((const char*)base + off, (const char*)end);
        return true;
    }
};

// A table of `count` records of `recordSize` at `offset`, followed by a string table.  `exact` asks for the strings to
// end the file, which is what tells the two record sizes apart.
bool tableFits(const uint8_t* data, size_t size, uint32_t offset, uint32_t count, unsigned recordSize, bool exact,
               Strings& strings) {
    const uint64_t end = (uint64_t)offset + (uint64_t)count * recordSize;
    strings = Strings();
    if (end > size) return false;
    if (end == size) return recordSize == 18;   // standard COFF may have short names only; rln always writes strings
    if (end + 4 > size) return false;
    const uint32_t n = be32(data + end);
    if (n < 4 || end + n > size) return false;
    if (exact && end + n != size) return false;
    strings.base = data + end;
    strings.size = n;
    return true;
}

// rln's records: long string offset, long type, long value.  rln takes the first name's offset before it reserves the
// size field, so the first record says 0 while its name sits at 4 (OSTAdd in rln.c).
SymbolParse parseCoff12(const uint8_t* table, uint32_t count, const Strings& strings, std::vector<Symbol>& out) {
    bool firstNameAtFour = count > 0 && be32(table) == 0;
    for (uint32_t i = 1; firstNameAtFour && i < count; i++)
        if (be32(table + i * 12) == 4) firstNameAtFour = false;   // another symbol owns that string: the first is unnamed

    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* r = table + i * 12;
        uint32_t idx = be32(r);
        const uint32_t type = be32(r + 4);
        if (type & 0xF0000000u) continue;   // debug (stab) record, not an address
        const uint32_t segment = type & 0x0E000000u;
        if (segment == 0) continue;         // undefined
        Symbol s;
        s.value = be32(r + 8);
        s.kind = segment == 0x04000000u ? SymbolKind::Text
               : segment == 0x06000000u ? SymbolKind::Data
               : segment == 0x08000000u ? SymbolKind::Bss
                                        : SymbolKind::Abs;
        if (idx == 0) {
            if (i != 0 || !firstNameAtFour) continue;   // unnamed
            idx = 4;
        }
        if (!strings.get(idx, s.name)) return SymbolParse::Malformed;
        if (!s.name.empty()) out.push_back(s);
    }
    return SymbolParse::Ok;
}

// Standard COFF: 8-byte name (or zero + string offset), long value, short section, short type, class, aux count.
SymbolParse parseCoff18(const uint8_t* data, size_t size, const uint8_t* table, uint32_t count, const Strings& strings,
                        std::vector<Symbol>& out) {
    enum { kExternal = 2, kStatic = 3, kLabel = 6 };
    const uint32_t sections = be16(data + 2);
    const uint64_t sectionTable = 20ull + be16(data + 16);
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* r = table + (uint64_t)i * 18;
        const int section = (int16_t)be16(r + 12);
        const unsigned cls = r[16], aux = r[17];
        i += aux;
        if (section == 0 || section == -2) continue;                                  // undefined, debug
        if (cls != kExternal && cls != kStatic && cls != kLabel) continue;            // .file, .bf and friends
        if (cls == kStatic && aux > 0) continue;                                      // a section's own entry
        Symbol s;
        s.value = be32(r + 8);
        s.kind = SymbolKind::Abs;
        if (section > 0 && (uint32_t)section <= sections) {
            const uint64_t flagsAt = sectionTable + (uint64_t)(section - 1) * 40 + 36;
            if (flagsAt + 4 > size) return SymbolParse::Malformed;
            const uint32_t flags = be32(data + flagsAt);
            s.kind = (flags & 0x20) ? SymbolKind::Text
                   : (flags & 0x40) ? SymbolKind::Data
                   : (flags & 0x80) ? SymbolKind::Bss
                                    : SymbolKind::Abs;
        }
        if (be32(r) == 0) {
            if (!strings.get(be32(r + 4), s.name)) return SymbolParse::Malformed;
        } else {
            s.name = fixedName(r, 8);
        }
        if (!s.name.empty()) out.push_back(s);
    }
    return SymbolParse::Ok;
}

SymbolParse parseCoff(const uint8_t* data, size_t size, std::vector<Symbol>& out) {
    if (size < 20) return SymbolParse::Malformed;
    const uint32_t offset = be32(data + 8), count = be32(data + 12);
    // rln without -s or -l: the header still counts the symbols, but the file ends where the table would start.
    if (count == 0 || offset == 0 || offset >= size) return SymbolParse::NoSymbols;
    Strings strings;
    const uint8_t* table = data + offset;
    if (tableFits(data, size, offset, count, 12, true, strings)) return parseCoff12(table, count, strings, out);
    if (tableFits(data, size, offset, count, 18, true, strings)) return parseCoff18(data, size, table, count, strings, out);
    if (tableFits(data, size, offset, count, 12, false, strings)) return parseCoff12(table, count, strings, out);
    if (tableFits(data, size, offset, count, 18, false, strings)) return parseCoff18(data, size, table, count, strings, out);
    return SymbolParse::Malformed;
}

SymbolParse parseAbs(const uint8_t* data, size_t size, std::vector<Symbol>& out) {
    enum { kHeader = 36, kRecord = 14, kDefined = 0x8000, kText = 0x0200, kData = 0x0400, kBss = 0x0100 };
    if (size < kHeader) return SymbolParse::Malformed;
    const uint64_t offset = (uint64_t)kHeader + be32(data + 2) + be32(data + 6);
    const uint64_t claimed = be32(data + 14);
    if (offset > size) return SymbolParse::Malformed;
    // rln's size field counts debug records it then skips, so it can run past the file: trust whole records only.
    const uint64_t count = std::min(claimed, (uint64_t)size - offset) / kRecord;
    if (count == 0) return SymbolParse::NoSymbols;
    for (uint64_t i = 0; i < count; i++) {
        const uint8_t* r = data + offset + i * kRecord;
        const uint32_t type = be16(r + 8);
        if (!(type & kDefined)) continue;
        Symbol s;
        s.name = fixedName(r, 8);
        s.value = be32(r + 10);
        s.kind = (type & kText) ? SymbolKind::Text
               : (type & kData) ? SymbolKind::Data
               : (type & kBss)  ? SymbolKind::Bss
                                : SymbolKind::Abs;
        if (!s.name.empty()) out.push_back(s);
    }
    return SymbolParse::Ok;
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

}  // namespace

SymbolParse parseSymbols(const uint8_t* data, size_t size, std::vector<Symbol>& out) {
    out.clear();
    if (!data || size < 2) return SymbolParse::NotExecutable;
    SymbolParse r;
    const uint32_t magic = be16(data);
    if (magic == 0x0150) r = parseCoff(data, size, out);
    else if (magic == 0x601B) r = parseAbs(data, size, out);
    else return SymbolParse::NotExecutable;
    if (r != SymbolParse::Ok) {
        out.clear();
        return r;
    }
    return out.empty() ? SymbolParse::NoSymbols : SymbolParse::Ok;
}

const char* symbolKindName(SymbolKind k) {
    switch (k) {
        case SymbolKind::Text: return "text";
        case SymbolKind::Data: return "data";
        case SymbolKind::Bss: return "bss";
        case SymbolKind::Abs: return "abs";
    }
    return "";
}

int symbolTarget(uint32_t value) {
    if (value >= 0xF03000u && value <= 0xF03FFFu) return VJ_DBG_TARGET_GPU;
    if (value >= 0xF1B000u && value <= 0xF1CFFFu) return VJ_DBG_TARGET_DSP;
    return VJ_DBG_TARGET_M68K;
}

bool symbolCanBreak(uint32_t value) { return value <= 0xFFFFFFu && (value & 1) == 0; }

bool symbolMatches(const Symbol& s, const std::string& query) {
    if (query.empty()) return true;
    const std::string q = lower(query);
    if (lower(s.name).find(q) != std::string::npos) return true;
    size_t skip = 0;
    if (q[0] == '$') skip = 1;
    else if (q.rfind("0x", 0) == 0) skip = 2;
    if (skip == q.size()) return false;
    char hex[16];
    std::snprintf(hex, sizeof hex, "%06x", s.value);
    return std::strstr(hex, q.c_str() + skip) != nullptr;
}

}  // namespace vj
