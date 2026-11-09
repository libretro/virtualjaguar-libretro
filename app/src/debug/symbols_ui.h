// The Symbols window (Tools > Symbols..., #1195): the names and hex values from a .cof / .abs, a search box, and a
// breakpoint or a jump from any row.  The parsing and the search are in symbol_table.* and unit-tested.
//
// Where the symbols come from, in order: a file picked in the window, the loaded content itself, a .cof with the same
// name beside the content.  Nothing is read until the window opens.
//
// Wiring (App owns the call site): addDebugTools() fills SymbolsUiContext, so this file needs neither the core host
// nor the other tool windows.
#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace vj {

class DebugSession;

struct SymbolsUiContext {
    std::function<std::string()> contentPath;                  // "" with no content
    std::function<void()> pickFile;                            // opens a dialog; answers via symbolsToolFilePicked
    std::function<void(int target, unsigned addr)> showCpu;    // "show in the CPU debugger"
    std::function<void(unsigned addr)> showMemory;             // "show in the memory viewer"
};

void registerSymbolsTool(DebugSession& s, const SymbolsUiContext& ctx);
void drawSymbolsTool(DebugSession& s);

// The pickFile dialog's answer.  Any thread (SDL may call back off the UI thread); the next draw loads it.
void symbolsToolFilePicked(const std::string& path);

// Test hooks: the actions behind the window's controls, and what it would show.
namespace symbols_test {
size_t rowCount();                                 // rows left after the search
const std::string& rowName(size_t row);
void setSearch(const std::string& query);
void toggleBreakpoint(DebugSession& s, size_t row);
const std::string& status();
bool statusIsError();
}  // namespace symbols_test

}  // namespace vj
