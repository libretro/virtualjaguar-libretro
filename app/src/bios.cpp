#include "bios.h"

#include <cstdio>
#include <vector>

#include <SDL3/SDL.h>

#include "paths.h"

namespace vj {
namespace bios {

const char* roleLabel(Role r) { return r == CartBoot ? "Cart boot ROM" : r == Cd ? "CD BIOS" : ""; }
const char* roleKey(Role r) { return r == CartBoot ? "bios.cart" : "bios.cd"; }
uint64_t roleBytes(Role r) { return r == CartBoot ? 0x20000u : r == Cd ? 0x40000u : 0u; }

Selection loadSelection(const Config& cfg) {
    Selection s;
    s.cart = cfg.get(roleKey(CartBoot), "");
    s.cd = cfg.get(roleKey(Cd), "");
    return s;
}

void storeSelection(Config& cfg, const Selection& s) {
    for (Role r : {CartBoot, Cd}) {
        if (s.of(r).empty()) cfg.erase(roleKey(r));
        else cfg.set(roleKey(r), s.of(r));
    }
}

Role roleOf(const lib::BiosInfo& b, uint64_t size) {
    if (b.role == lib::BiosRole::CartBoot && size == roleBytes(CartBoot)) return CartBoot;
    if (b.role == lib::BiosRole::CdBios && size == roleBytes(Cd)) return Cd;
    return None;
}

BootInfo bootInfo(const lib::BiosInfo& b, uint64_t size) {
    BootInfo i;
    switch (b.role) {
    case lib::BiosRole::CartBoot:
        if (size != roleBytes(CartBoot)) {
            i.text = "The core only accepts a boot ROM of exactly 131072 bytes; this file is " + std::to_string((unsigned long long)size) + ".";
        } else {
            i.kind = BootKind::CartRom;
            i.text = "Boots the console through this boot ROM with an empty cartridge slot: the Jaguar logo animation, "
                     "then it waits for a cartridge, as the hardware does.";
        }
        break;
    case lib::BiosRole::CdBios:
        if (size != roleBytes(Cd)) {
            i.text = "The core only accepts a CD BIOS of exactly 262144 bytes; this file is " + std::to_string((unsigned long long)size) + ".";
        } else {
            i.kind = BootKind::CdBios;
            i.text = "Boots the Jaguar CD player with this BIOS and no disc: its logo and the screen that asks for a disc "
                     "(and the Memory Track manager).";
        }
        break;
    case lib::BiosRole::MemoryTrack:
        if (b.recognized && size == 0x20000) {
            i.kind = BootKind::MemoryTrack;
            i.text = "Runs the Memory Track cartridge's own manager program, as if it were plugged into the cartridge slot.";
        } else {
            i.text = "The core only treats the known Memory Track cartridge dump as a Memory Track (this file's checksum is "
                     "not that one), so it cannot be booted. To manage saves, boot the CD BIOS: it comes up with the "
                     "emulated Memory Track plugged in.";
        }
        break;
    default:
        i.text = "This file is not a boot ROM or a CD BIOS, so there is nothing to boot.";
        break;
    }
    return i;
}

std::string describeStaged(unsigned source, const std::string& where, uint32_t crc) {
    char hex[16];
    std::snprintf(hex, sizeof hex, "%08X", crc);
    if (source == SrcBuiltin) return "built-in " + where + " (CRC32 " + hex + ")";
    lib::BiosInfo k = lib::classifyBios("x", 0, crc);
    std::string name = k.recognized ? k.title + ", " : std::string();
    std::string base = where;
    size_t sl = base.find_last_of("/\\");
    if (sl != std::string::npos) base = base.substr(sl + 1);
    return std::string(source == SrcFile ? "your file " : "system-folder file ") + base + " (" + name + "CRC32 " + hex + ")";
}

// ---- importing -------------------------------------------------------------------------------------------

namespace {

// The file's bytes, but only when it has exactly a BIOS's size: a dropped disc image is never read into memory.
bool loadSmall(const std::string& path, std::vector<unsigned char>& data) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE) return false;
    if ((uint64_t)info.size != roleBytes(CartBoot) && (uint64_t)info.size != roleBytes(Cd)) return false;
    if (!readFile(path, data)) return false;
    return data.size() == roleBytes(CartBoot) || data.size() == roleBytes(Cd);
}

std::string baseOf(const std::string& path) {
    size_t sl = path.find_last_of("/\\");
    return sl == std::string::npos ? path : path.substr(sl + 1);
}

}  // namespace

Role importRole(const std::string& path) {
    std::vector<unsigned char> data;
    if (!loadSmall(path, data)) return None;
    return roleOf(lib::classifyBios(path, data.size(), lib::crc32Update(0, data.data(), data.size())), data.size());
}

Imported importFile(const std::string& src, const std::string& systemDir, bool trustSize) {
    Imported out;
    std::vector<unsigned char> data;
    if (systemDir.empty()) {
        out.message = "No system folder to import into.";
        return out;
    }
    if (!loadSmall(src, data)) {
        out.message = "That is not a BIOS the core can use (a console boot ROM is exactly 128 KiB, a CD BIOS 256 KiB).";
        return out;
    }
    const uint32_t crc = lib::crc32Update(0, data.data(), data.size());
    out.role = roleOf(lib::classifyBios(src, data.size(), crc), data.size());
    std::string name = baseOf(src);
    if (out.role == None && trustSize) {
        out.role = data.size() == roleBytes(Cd) ? Cd : CartBoot;
        name = std::string("[BIOS] Atari Jaguar ") + (out.role == Cd ? "CD " : "") + "- " + name;   // so the scan finds it
    }
    if (out.role == None) {
        out.message = "That file does not look like a Jaguar boot ROM or CD BIOS.";
        return out;
    }
    std::string dir = systemDir;
    if (dir.back() != '/' && dir.back() != '\\') dir += '/';
    dir += "bios/";
    SDL_CreateDirectory(dir.c_str());
    std::string stem = name, ext;
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
        stem = name.substr(0, dot);
        ext = name.substr(dot);
    }
    for (int n = 1; n < 100; n++) {
        const std::string candidate = dir + (n == 1 ? name : stem + " (" + std::to_string(n) + ")" + ext);
        std::vector<unsigned char> have;
        if (fileExists(candidate)) {
            if (readFile(candidate, have) && have == data) {   // the same file again: use it
                out.ok = true;
                out.path = candidate;
                out.message = std::string("Already imported: ") + roleLabel(out.role) + " " + baseOf(candidate) + ".";
                return out;
            }
            continue;
        }
        if (!writeFileAtomic(candidate, data.data(), data.size())) {
            out.message = "Could not copy the file into " + dir;
            return out;
        }
        out.ok = true;
        out.path = candidate;
        out.message = std::string("Imported the ") + roleLabel(out.role) + " as " + baseOf(candidate) + ".";
        return out;
    }
    out.message = "Too many copies with that name in " + dir;
    return out;
}

}  // namespace bios
}  // namespace vj
