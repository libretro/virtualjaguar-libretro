// The symbol table parser (app/src/debug/symbol_table.*, #1195): real rmac / rln output (tests/fixtures/symbols), then
// damaged and synthetic tables.  No window, no core.
#include "symbol_table.h"

#include "vj_debug_iface.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace vj;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        g_checks++;                                                                \
        if (!(cond)) {                                                             \
            g_fail++;                                                              \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                          \
    } while (0)

using Bytes = std::vector<uint8_t>;

static Bytes fixture(const char* name) {
    Bytes out;
    const std::string path = std::string(VJ_SYMBOL_FIXTURES) + "/" + name;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        uint8_t buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
        std::fclose(f);
    }
    if (out.empty()) std::fprintf(stderr, "FAIL missing fixture %s\n", path.c_str());
    CHECK(!out.empty());
    return out;
}

static const Symbol* find(const std::vector<Symbol>& v, const std::string& name) {
    for (const Symbol& s : v)
        if (s.name == name) return &s;
    return nullptr;
}

static bool has(const std::vector<Symbol>& v, const std::string& name, uint32_t value, SymbolKind kind) {
    const Symbol* s = find(v, name);
    return s && s->value == value && s->kind == kind;
}

static void put16(Bytes& b, uint32_t v) {
    b.push_back((uint8_t)(v >> 8));
    b.push_back((uint8_t)v);
}
static void put32(Bytes& b, uint32_t v) {
    put16(b, v >> 16);
    put16(b, v & 0xFFFF);
}
static void set32(Bytes& b, size_t at, uint32_t v) {
    b[at] = (uint8_t)(v >> 24);
    b[at + 1] = (uint8_t)(v >> 16);
    b[at + 2] = (uint8_t)(v >> 8);
    b[at + 3] = (uint8_t)v;
}

// A $0150 file holding only `table` at the end of a 20-byte header.
static Bytes coffWith(uint32_t count, const Bytes& table) {
    Bytes b;
    put16(b, 0x0150);
    put16(b, 0);
    put32(b, 0);
    put32(b, 20);
    put32(b, count);
    put16(b, 0);
    put16(b, 3);
    b.insert(b.end(), table.begin(), table.end());
    return b;
}

static SymbolParse parse(const Bytes& b, std::vector<Symbol>& out) { return parseSymbols(b.data(), b.size(), out); }

static void testRealCoff() {
    std::vector<Symbol> v;
    CHECK(parse(fixture("globals.cof"), v) == SymbolParse::Ok);
    CHECK(v.size() == 11);
    // The first record's string offset is rln's 0; its name is really at 4.
    CHECK(has(v, "_TEXT_E", 0x4018, SymbolKind::Text));
    CHECK(has(v, "_DATA_E", 0x4028, SymbolKind::Data));
    CHECK(has(v, "_BSS_E", 0x4030, SymbolKind::Bss));
    CHECK(has(v, "_start", 0x4000, SymbolKind::Text));
    CHECK(has(v, "main_loop", 0x400A, SymbolKind::Text));
    CHECK(has(v, "a_rather_long_symbol_name_past_eight_chars", 0x4012, SymbolKind::Text));
    CHECK(has(v, "frame_table", 0x4018, SymbolKind::Data));
    CHECK(has(v, "frame_count", 0x4028, SymbolKind::Bss));
    // rmac gives .gpu / .org labels and equates the absolute type: only the value says where they run.
    CHECK(has(v, "gpu_blit", 0xF03000, SymbolKind::Abs));
    CHECK(has(v, "gpu_end", 0xF03004, SymbolKind::Abs));
    CHECK(has(v, "BORDER_COLOR", 0xF0002A, SymbolKind::Abs));
    CHECK(find(v, "local_label") == nullptr);

    CHECK(parse(fixture("locals.cof"), v) == SymbolParse::Ok);
    CHECK(v.size() == 12);
    CHECK(has(v, "_TEXT_E", 0x4018, SymbolKind::Text));
    CHECK(has(v, "local_label", 0x4010, SymbolKind::Text));

    // Linked without -s or -l: the header still claims 11 symbols, and the file ends at the table's offset.
    CHECK(parse(fixture("stripped.cof"), v) == SymbolParse::NoSymbols);
    CHECK(v.empty());
}

static void testRealAbs() {
    std::vector<Symbol> v;
    CHECK(parse(fixture("symbols.abs"), v) == SymbolParse::Ok);
    CHECK(v.size() == 10);   // 11 records: rln writes the first one's name blank in this format
    CHECK(has(v, "_DATA_E", 0x4028, SymbolKind::Data));
    CHECK(has(v, "_BSS_E", 0x4030, SymbolKind::Bss));
    CHECK(has(v, "_start", 0x4000, SymbolKind::Text));
    CHECK(has(v, "main_loo", 0x400A, SymbolKind::Text));   // eight characters is all the format holds
    CHECK(has(v, "frame_ta", 0x4018, SymbolKind::Data));
    CHECK(has(v, "gpu_blit", 0xF03000, SymbolKind::Abs));
}

static void testNotSymbols() {
    std::vector<Symbol> v;
    const uint8_t rom[] = {0x60, 0x1A, 0, 0, 0, 0};
    CHECK(parseSymbols(rom, sizeof rom, v) == SymbolParse::NotExecutable);
    CHECK(parseSymbols(nullptr, 0, v) == SymbolParse::NotExecutable);
    const uint8_t one[] = {0x01};
    CHECK(parseSymbols(one, 1, v) == SymbolParse::NotExecutable);
    const uint8_t shortCoff[] = {0x01, 0x50, 0, 3};
    CHECK(parseSymbols(shortCoff, sizeof shortCoff, v) == SymbolParse::Malformed);
    const uint8_t shortAbs[] = {0x60, 0x1B, 0, 3};
    CHECK(parseSymbols(shortAbs, sizeof shortAbs, v) == SymbolParse::Malformed);
}

static void testDamagedCoff() {
    std::vector<Symbol> v;
    const Bytes good = fixture("globals.cof");

    Bytes b = good;
    set32(b, 12, 0xFFFFFFFFu);   // a count that overflows 32-bit arithmetic
    CHECK(parse(b, v) == SymbolParse::Malformed);
    CHECK(v.empty());

    b = good;
    set32(b, 8, 0xFFFFFFF0u);    // an offset past the file
    CHECK(parse(b, v) == SymbolParse::NoSymbols);

    b = good;
    set32(b, 0xD0 + 12, 0x7FFFFFFFu);   // the second record's string offset, past the table
    CHECK(parse(b, v) == SymbolParse::Malformed);

    b = good;
    b.back() = 'x';              // the last name loses its terminator
    CHECK(parse(b, v) == SymbolParse::Malformed);

    // Every truncation and every single-byte corruption stays inside the buffer (run under ASan to see it fail).
    for (size_t n = 0; n < good.size(); n++) parseSymbols(good.data(), n, v);
    for (size_t i = 0; i < good.size(); i++) {
        b = good;
        b[i] ^= 0xFF;
        parse(b, v);
    }
    CHECK(true);
}

static void testDamagedAbs() {
    std::vector<Symbol> v;
    const Bytes good = fixture("symbols.abs");

    Bytes b = good;
    set32(b, 14, 0x7FFFFFFFu);   // a size past the file: whole records are still read
    CHECK(parse(b, v) == SymbolParse::Ok);
    CHECK(v.size() == 10);

    b = good;
    b.pop_back();                // a torn last record is dropped
    CHECK(parse(b, v) == SymbolParse::Ok);
    CHECK(find(v, "BORDER_C") == nullptr);
    CHECK(find(v, "gpu_blit") != nullptr);

    b = good;
    set32(b, 2, 0xFFFFFFFFu);    // text larger than the file
    CHECK(parse(b, v) == SymbolParse::Malformed);

    b = good;
    set32(b, 14, 0);             // no table claimed
    CHECK(parse(b, v) == SymbolParse::NoSymbols);

    for (size_t n = 0; n < good.size(); n++) parseSymbols(good.data(), n, v);
    for (size_t i = 0; i < good.size(); i++) {
        b = good;
        b[i] ^= 0xFF;
        parse(b, v);
    }
    CHECK(true);
}

// Another record owning offset 4 means the first really is unnamed; undefined and debug records are not addresses.
static void testCoffRecordRules() {
    Bytes t;
    put32(t, 0); put32(t, 0x05000000); put32(t, 0x1000);   // unnamed, because the next record owns offset 4
    put32(t, 4); put32(t, 0x05000000); put32(t, 0x2000);
    put32(t, 9); put32(t, 0x01000000); put32(t, 0);        // global, undefined
    put32(t, 9); put32(t, 0x64000000); put32(t, 0x3000);   // debug
    put32(t, 9); put32(t, 0x06000000); put32(t, 0x4000);
    put32(t, 14);
    for (char c : std::string("name")) t.push_back((uint8_t)c);
    t.push_back(0);
    for (char c : std::string("nam2")) t.push_back((uint8_t)c);
    t.push_back(0);
    std::vector<Symbol> v;
    CHECK(parse(coffWith(5, t), v) == SymbolParse::Ok);
    CHECK(v.size() == 2);
    CHECK(has(v, "name", 0x2000, SymbolKind::Text));
    CHECK(has(v, "nam2", 0x4000, SymbolKind::Data));

    // A table of nothing but undefined symbols has nothing to show.
    Bytes u;
    put32(u, 4); put32(u, 0x01000000); put32(u, 0);
    put32(u, 7);
    u.push_back('a'); u.push_back('b'); u.push_back(0);
    CHECK(parse(coffWith(1, u), v) == SymbolParse::NoSymbols);
}

static void put18(Bytes& t, const char* name8, uint32_t strOffset, uint32_t value, int section, uint8_t cls, uint8_t aux) {
    if (name8) {
        std::string n(name8);
        n.resize(8, '\0');
        t.insert(t.end(), n.begin(), n.end());
    } else {
        put32(t, 0);
        put32(t, strOffset);
    }
    put32(t, value);
    put16(t, (uint32_t)section & 0xFFFF);
    put16(t, 0);
    t.push_back(cls);
    t.push_back(aux);
}

// Standard COFF, synthesized from the format (no such linker in the toolchain): section numbers name the kind.
static void testStandardCoff() {
    Bytes b;
    put16(b, 0x0150);
    put16(b, 3);          // sections
    put32(b, 0);
    put32(b, 20 + 120);   // symbols after the three section headers
    put32(b, 7);
    put16(b, 0);          // no optional header
    put16(b, 3);
    for (uint32_t flags : {0x20u, 0x40u, 0x80u}) {
        for (int i = 0; i < 36; i++) b.push_back(0);
        put32(b, flags);
    }
    put18(b, ".file", 0, 0, -2, 103, 0);          // debug
    put18(b, ".text", 0, 0x4000, 1, 3, 1);        // a section's entry, with one aux record after it
    put18(b, "auxaux", 0, 0xDEAD, 1, 2, 0);       // the aux record: must be skipped, not read as a symbol
    put18(b, "short", 0, 0x4004, 1, 2, 0);
    put18(b, nullptr, 4, 0x5000, 2, 2, 0);        // a long name, via the strings
    put18(b, "bssname8", 0, 0x6000, 3, 3, 0);     // exactly eight characters: no terminator
    put18(b, "EQU", 0, 0xF00000, -1, 2, 0);
    const std::string longName = "a_long_standard_name";
    put32(b, (uint32_t)(4 + longName.size() + 1));
    b.insert(b.end(), longName.begin(), longName.end());
    b.push_back(0);
    std::vector<Symbol> v;
    CHECK(parse(b, v) == SymbolParse::Ok);
    CHECK(v.size() == 4);
    CHECK(has(v, "short", 0x4004, SymbolKind::Text));
    CHECK(has(v, longName, 0x5000, SymbolKind::Data));
    CHECK(has(v, "bssname8", 0x6000, SymbolKind::Bss));
    CHECK(has(v, "EQU", 0xF00000, SymbolKind::Abs));
    CHECK(find(v, "auxaux") == nullptr);
}

static void testTargets() {
    CHECK(symbolTarget(0x004000) == VJ_DBG_TARGET_M68K);
    CHECK(symbolTarget(0x802000) == VJ_DBG_TARGET_M68K);
    CHECK(symbolTarget(0xF02FFF) == VJ_DBG_TARGET_M68K);
    CHECK(symbolTarget(0xF03000) == VJ_DBG_TARGET_GPU);
    CHECK(symbolTarget(0xF03FFF) == VJ_DBG_TARGET_GPU);
    CHECK(symbolTarget(0xF04000) == VJ_DBG_TARGET_M68K);
    CHECK(symbolTarget(0xF1AFFF) == VJ_DBG_TARGET_M68K);
    CHECK(symbolTarget(0xF1B000) == VJ_DBG_TARGET_DSP);
    CHECK(symbolTarget(0xF1CFFF) == VJ_DBG_TARGET_DSP);
    CHECK(symbolTarget(0xF1D000) == VJ_DBG_TARGET_M68K);   // wave table ROM
}

static void testCanBreak() {
    CHECK(symbolCanBreak(0x4000));
    CHECK(symbolCanBreak(0xF03000));
    CHECK(symbolCanBreak(0xFFFFFE));
    CHECK(!symbolCanBreak(0x4001));
    CHECK(!symbolCanBreak(0x1000000));
    CHECK(!symbolCanBreak(0xDEADBEEE));
}

static void testSearch() {
    Symbol s;
    s.name = "Main_Loop";
    s.value = 0x00F03ABC;
    CHECK(symbolMatches(s, ""));
    CHECK(symbolMatches(s, "main"));
    CHECK(symbolMatches(s, "LOOP"));
    CHECK(!symbolMatches(s, "mainloop"));
    CHECK(symbolMatches(s, "f03"));
    CHECK(symbolMatches(s, "3ABC"));
    CHECK(symbolMatches(s, "$f03a"));
    CHECK(symbolMatches(s, "0xF03A"));
    CHECK(!symbolMatches(s, "$"));
    CHECK(!symbolMatches(s, "0x"));
    CHECK(!symbolMatches(s, "$999"));
    s.value = 0x4000;
    CHECK(symbolMatches(s, "004000"));   // the six digits the window shows
}

int main() {
    testRealCoff();
    testRealAbs();
    testNotSymbols();
    testDamagedCoff();
    testDamagedAbs();
    testCoffRecordRules();
    testStandardCoff();
    testTargets();
    testCanBreak();
    testSearch();
    std::printf("debug_symbols_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
