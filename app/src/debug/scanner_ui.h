// The Cheat scanner window (Tools > Cheat scanner...).  The search itself is cheat_scan.*, what a result
// turns into is scanner_actions.*; this file is only the ImGui.
//
// Wiring (the Tools host, WP1 phase B, owns the DebugSession and calls these):
//   registerScannerTool(session)                       once, replaces the greyed "scanner" placeholder
//   drawScanner(session, app.cheats, app.host)         once per UI frame; draws nothing while closed
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace vj {

class CheatManager;
class CoreHost;
class DebugSession;

void registerScannerTool(DebugSession& session);
void drawScanner(DebugSession& session, CheatManager& cheats, CoreHost& host);

// Window state, for the Tools menu and tests.
bool scannerIsOpen();
void scannerSetOpen(bool open);

// Test hooks: drive the window's state without mouse input (the headless ImGui smoke test uses them).
namespace scanner_test {
void setOptions(int widthIdx, bool isSigned, bool aligned);   // before the first scan
void setMode(int firstModeIdx, int nextModeIdx);              // indices into the two mode lists, in menu order
void setValues(const char* v1, const char* v2);
void scan(DebugSession& session, CoreHost& host);              // "Start search" / "Next scan"
void undo();
void newSearch();
size_t candidates();
const std::string& message();                                  // the status line under the controls
size_t shownRows();
bool liveValue(size_t row, int64_t& v);                        // the live column's value for that row (false: no such row, or "--")
unsigned liveReads();                                          // how many times the 2 MB Dram buffer was copied out
bool beginEdit(size_t row, const char* text);                  // as if the value of that row was double-clicked (false: no value read yet)
bool openAdd(size_t row, const char* desc);                    // as if "Add as cheat" was pressed on that row (false: no value read yet)
}  // namespace scanner_test

}  // namespace vj
