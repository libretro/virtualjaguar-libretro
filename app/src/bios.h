// Choosing and booting the BIOS (#838, #833): which file the core runs as the console boot ROM and the Jaguar CD
// BIOS, what a "Boot" on a BIOS tile does, importing a BIOS file, and how the current choice is described.
// No ImGui and no core here: the decisions are plain functions, so the tests can pin them down; core_host.cpp
// hands the choice to the core (VJ_ENV_GET_BIOS_PATH, src/core/vj_frontend_ext.h) and bios_ui.cpp draws it.
//
// The model: a BIOS role is either Auto (the core's own default: a file in the system folder under one of the
// names it searches, else the image built into the core) or one chosen file, from anywhere on disk and under
// any name.  The chosen file is stored per role in config.ini and handed to the core at every load; the core
// still checks it (exact size, CD header / checksum) and falls back to Auto if it does not pass.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "config.h"
#include "library.h"

namespace vj {
namespace bios {

enum Role : unsigned { None = 0, CartBoot = 1, Cd = 2 };   // = VJ_BIOS_CART_BOOT / VJ_BIOS_CD
enum Source : unsigned { SrcBuiltin = 0, SrcSystem = 1, SrcFile = 2 };   // = VJ_BIOS_SRC_*  (core_host.cpp asserts it)

const char* roleLabel(Role r);            // "Cart boot ROM", "CD BIOS"
const char* roleKey(Role r);              // config key: "bios.cart", "bios.cd"
uint64_t roleBytes(Role r);               // the one size the core accepts: 131072 / 262144

// The persisted choice, one file path per role; "" = Auto.
struct Selection {
    std::string cart, cd;
    const std::string& of(Role r) const { return r == CartBoot ? cart : cd; }
    std::string& of(Role r) { return r == CartBoot ? cart : cd; }
    bool operator==(const Selection& o) const { return cart == o.cart && cd == o.cd; }
};
Selection loadSelection(const Config& cfg);
void storeSelection(Config& cfg, const Selection& s);

// Which role a scanned BIOS / system file can fill: a console boot ROM or a CD BIOS whose size is exactly what
// the core accepts.  Memory Track and other system files, and wrong-sized files, are None.
Role roleOf(const lib::BiosInfo& b, uint64_t size);

// ---- the choosable files ---------------------------------------------------------------------------------
// One row of the BIOS combo: `path` is what gets stored, `label` is what is shown.  Two rows never share a
// label (#932): the same BIOS dump in several folders is the normal case, and identical labels are identical
// ImGui IDs, so a click on the second copy chose the first.  A label that would repeat gets its folder appended.
struct Pick {
    std::string path;
    std::string label;
    std::string id;      // fileIdentity(path): compare this, not the path, to ask "is it the chosen one?"
};
// The file's identity on disk (the real path when it exists, else the normalised absolute one).  Cached for a
// couple of seconds per path, so the per-frame draw code never hits the disk for it.
std::string fileIdentity(const std::string& path);
// Every scanned BIOS file the core accepts for `role`, in scan order.  A file reached twice (a library folder
// that is also, or sits inside, the system folder, or a symlinked folder) is listed once.
std::vector<Pick> picksFor(const std::vector<lib::Entry>& entries, Role role);
// Do the two paths name the same file (same text, or the same file on disk through a link / other spelling)?
bool samePath(const std::string& a, const std::string& b);

// What "Boot" on a tile does: a cart boot ROM boots the console with an empty cartridge slot, a CD BIOS boots
// the CD player with no disc, the known Memory Track dump is loaded as a cartridge (it carries the manager).
enum class BootKind { None, CartRom, CdBios, MemoryTrack };
struct BootInfo {
    BootKind kind = BootKind::None;
    std::string text;        // ok: what happens.  not ok: why it cannot be booted
};
BootInfo bootInfo(const lib::BiosInfo& b, uint64_t size);

// "Built-in Series K boot ROM", "your file jagcd.bin (Jaguar CD BIOS (retail), CRC32 687068D5)", ... from what the
// core reported staging (VJ_EVT_BIOS_STAGED): source is VJ_BIOS_SRC_*, where is the path or built-in name.
std::string describeStaged(unsigned source, const std::string& where, uint32_t crc);

// ---- importing -----------------------------------------------------------------------------------------
// Is `path` a BIOS the app should import rather than run as a game (dropped on the window)?  Exactly 128 or
// 256 KiB and recognised as a boot ROM / CD BIOS by checksum or name.  Reads at most the file's first 512 KiB.
Role importRole(const std::string& path);
struct Imported {
    bool ok = false;
    Role role = None;
    std::string path;        // the copy in the BIOS folder (or the file itself when it already was there)
    std::string message;     // for the status line / toast
};
// Copies `src` into <systemDir>/bios/ under its own file name (a numbered one if a different file has it),
// never over anything.  An identical copy that is already there is reused.  `trustSize` is for a file the user
// picked in "Import BIOS file...": one that nothing recognises is taken by its size (128 KiB = boot ROM, 256 KiB =
// CD BIOS) and given a "[BIOS] Atari Jaguar ..." name so the library scan lists it afterwards.
Imported importFile(const std::string& src, const std::string& systemDir, bool trustSize = false);

}  // namespace bios
}  // namespace vj
