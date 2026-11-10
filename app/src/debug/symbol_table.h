// Symbol tables from Jaguar executables (#1195): the parser and the search behind the Symbols window.  No ImGui and no
// core, so it unit-tests on bytes (app/tests/debug_symbols_test.cpp, with real rmac / rln output in tests/fixtures).
//
// Formats, as rln writes them (rln.c, WriteOutputFile):
//   COFF  $0150  header: symbol offset at $08, symbol count at $0C.  12-byte records (string offset, type, value), then
//                a string table whose first long is its own size.
//   ABS   $601B  36-byte header, text, data, then 14-byte records (8-character name, type, value).
// A standard COFF table of 18-byte records (GNU ld) is accepted too.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vj {

enum class SymbolKind { Text, Data, Bss, Abs };

struct Symbol {
    std::string name;
    uint32_t value = 0;
    SymbolKind kind = SymbolKind::Abs;
};

enum class SymbolParse {
    Ok,             // symbols filled (never empty)
    NotExecutable,  // not a format that carries symbols
    NoSymbols,      // the right format, but the file holds no table (linked without rln -s or -l)
    Malformed,      // the table is there but does not fit the file
};

SymbolParse parseSymbols(const uint8_t* data, size_t size, std::vector<Symbol>& out);

const char* symbolKindName(SymbolKind k);

// Which processor a breakpoint on this address belongs to (VJ_DBG_TARGET_*), by local RAM (docs/jtrm-gpu-dsp.md):
// GPU $F03000-$F03FFF, DSP $F1B000-$F1CFFF, otherwise the 68000.  GPU code run from main RAM needs an explicit choice.
int symbolTarget(uint32_t value);

// Whether this value can be a program counter: inside the 24-bit bus and even.  Equates often are neither.
bool symbolCanBreak(uint32_t value);

// The search box: case-insensitive on the name, or on the value's hex digits (a leading $ or 0x is ignored).
bool symbolMatches(const Symbol& s, const std::string& query);

}  // namespace vj
