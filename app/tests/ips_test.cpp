#include "core_host.h"
#include "ips.h"
#include "library_lists.h"
#include "paths.h"
#include "sha256.h"
#include "titledb.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <initializer_list>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace vj;
using ips::Bytes;

namespace {

int failures = 0;
#define CHECK(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while (false)

Bytes patch(std::initializer_list<unsigned char> records, bool eof = true) {
    Bytes bytes = {'P', 'A', 'T', 'C', 'H'};
    bytes.insert(bytes.end(), records);
    if (eof) bytes.insert(bytes.end(), {'E', 'O', 'F'});
    return bytes;
}

void parserTests() {
    const Bytes base = {1, 2, 3};
    Bytes out;
    std::string error;
    CHECK(ips::apply(base, patch({
        0, 0, 1, 0, 2, 0x10, 0x20,      // literal
        0, 0, 2, 0, 0, 0, 3, 0x42,     // overlapping RLE with growth
        0, 0, 7, 0, 1, 0x50             // zero-fill the gap
    }), out, error));
    CHECK(out == (Bytes{1, 0x10, 0x42, 0x42, 0x42, 0, 0, 0x50}));
    CHECK(base == (Bytes{1, 2, 3}));

    auto truncate = patch({});
    truncate.insert(truncate.end(), {0, 0, 2});
    CHECK(ips::apply(base, truncate, out, error));
    CHECK(out == (Bytes{1, 2}));
    truncate.back() = 5;
    CHECK(ips::apply(base, truncate, out, error));
    CHECK(out == (Bytes{1, 2, 3, 0, 0}));
    CHECK(ips::apply(base, patch({0xff, 0xff, 0xff, 0, 1, 7}), out, error));
    CHECK(out.size() == 0x1000000 && out.back() == 7);

    const Bytes bad[] = {
        {'I', 'P', 'S', '3', '2', 'E', 'O', 'F'},
        patch({}, false),
        patch({0, 0}, false),
        patch({0, 0, 1, 0}, false),
        patch({0, 0, 1, 0, 3, 7}, false),
        patch({0, 0, 1, 0, 0, 0, 2}, false),
        patch({0, 0, 1, 0, 0, 0, 0, 7}),
        patch({0xff, 0xff, 0xff, 0, 2, 7, 8}),
        patch({0xff, 0xff, 0xff, 0, 0, 0, 2, 7}),
        {'P', 'A', 'T', 'C', 'H', 'E', 'O', 'F', 0},
        {'P', 'A', 'T', 'C', 'H', 'E', 'O', 'F', 0, 0, 0},
        patch({0, 0, 1, 0, 1, 7, 0, 0, 2, 0}, false) // valid record followed by a bad one
    };
    for (const auto& invalid : bad) {
        out = {0xaa};
        CHECK(!ips::apply(base, invalid, out, error));
        CHECK(out == (Bytes{0xaa}));
    }
}

bool put(const std::string& path, const Bytes& bytes) {
    return SDL_SaveFile(path.c_str(), bytes.data(), bytes.size());
}

void setSaveByte(CoreHost& host, unsigned char value) {
    uint8_t* memory = nullptr;
    size_t size = 0;
    if (host.memoryRegion(RETRO_MEMORY_SAVE_RAM, memory, size) && size) {
        memory[0] = value;
        CHECK(host.flushSaveRam());
    } else {
        CHECK(false);
    }
}

Bytes get(const std::string& path) {
    Bytes bytes;
    CHECK(readFile(path, bytes));
    return bytes;
}

void fileAndLoaderTests(const fs::path& folder) {
    const auto file = [&](const char* name) { return (folder / name).u8string(); };
    const std::string rom = file("original.j64");
    const std::string ipsFile = file("test.ips");
    const std::string backup = ips::recoveryPath(rom);
    const std::string saveDir = folder.u8string() + "/";
    std::string error;

    // A synthetic, bootable cartridge with a copier header and a 68K self-loop.
    Bytes payload(1024 * 1024);
    std::fill_n(payload.begin() + 0x400, 4, 4);
    payload[0x405] = 0x80;
    payload[0x406] = 0x20;
    payload[0x2000] = 0x60;
    payload[0x2001] = 0xfe;
    Bytes headered(512, 0xff);
    headered.insert(headered.end(), payload.begin(), payload.end());
    CHECK(put(rom, headered));
    CHECK(put(ipsFile, patch({0, 0x30, 0, 0, 1, 0x99})));

    CoreHost& host = CoreHost::instance();
    host.setDirectories(saveDir, saveDir);
    CHECK(host.loadContent(rom, error));
    CHECK(host.contentName() == "original");
    const std::string baseSave = host.saveRamPath();
    const std::string baseState = lib::stateFileName(host.contentName(), 1);
    const auto baseCrc = TitleDBContentCRC();
    setSaveByte(host, 0x31);
    CHECK(host.saveStateToFile(saveDir + baseState, error));
    const Bytes baseSaveBytes = get(baseSave);

    ips::Cartridge cart;
    CHECK(ips::prepare(rom, ipsFile, cart, error));
    ips::Cartridge verified;
    const ips::BaseRequirements matching{cart.original.size(), cart.baseHash, ""};
    CHECK(ips::prepare(rom, ipsFile, verified, error, matching));
    CHECK(verified.output == cart.output);
    ips::Cartridge mismatched;
    CHECK(!ips::prepare(rom, ipsFile, mismatched, error, {cart.original.size() + 1, "", ""}));
    CHECK(!ips::prepare(rom, ipsFile, mismatched, error, {cart.original.size(), std::string(64, '0'), ""}));
    char crc[9];
    std::snprintf(crc, sizeof crc, "%08x",
                  static_cast<unsigned>(SDL_crc32(0, cart.original.data(), cart.original.size())));
    CHECK(ips::prepare(rom, ipsFile, verified, error, {cart.original.size(), "", crc}));
    crc[0] = crc[0] == '0' ? '1' : '0';
    CHECK(!ips::prepare(rom, ipsFile, mismatched, error, {cart.original.size(), "", crc}));
    CHECK(mismatched.output.empty());

    payload[0x3000] = 0x99;
    CHECK(cart.strippedHeader && cart.original == headered && cart.output == payload);
    CHECK(cart.baseHash == sha256Hex(headered.data(), headered.size()));
    std::string upperHash = cart.baseHash;
    for (char& ch : upperHash) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    CHECK(ips::matchesHash(upperHash, cart.baseHash));
    CHECK(!ips::matchesHash(upperHash + "0", cart.baseHash));
    CHECK(!ips::matchesHash(cart.outputHash, cart.baseHash));

    CHECK(put(ipsFile, patch({0, 0, 0, 0, 2, 0x60, 0x1b})));
    ips::Cartridge rejected;
    CHECK(!ips::prepare(rom, ipsFile, rejected, error));
    CHECK(rejected.output.empty());
    CHECK(!ips::saveCopy(cart, file("missing/copy.j64"), error));
    CHECK(!ips::saveCopy(cart, rom, error));
    CHECK(get(rom) == headered);
    CHECK(host.isLoaded() && TitleDBContentCRC() == baseCrc && host.saveRamPath() == baseSave);

    const std::string patchedName = ips::identity(cart.outputHash);
    const std::string copied = file((patchedName + ".j64").c_str());
    CHECK(ips::saveCopy(cart, copied, error));
    CHECK(get(copied) == payload);
    CHECK(!ips::saveCopy(cart, copied, error));
    CHECK(get(copied) == payload && get(rom) == headered);

    CHECK(host.loadContent(rom, error, &cart.output, patchedName));
    CHECK(host.suppliedCartridge() && *host.suppliedCartridge() == payload);
    const auto patchedCrc = TitleDBContentCRC();
    const std::string patchedSave = host.saveRamPath();
    CHECK(patchedCrc != baseCrc && host.contentName() == patchedName);
    const auto patchedState = lib::stateFileName(host.contentName(), 1);
    CHECK(patchedSave != baseSave && patchedState != baseState);
    setSaveByte(host, 0x52);
    CHECK(host.saveStateToFile(saveDir + patchedState, error));
    CHECK(get(baseSave) == baseSaveBytes && get(patchedSave) != baseSaveBytes);
    CHECK(get(rom) == headered);
    CHECK(host.loadContent(copied, error));
    CHECK(host.saveRamPath() == patchedSave && TitleDBContentCRC() == patchedCrc);
    uint8_t* saveRam = nullptr;
    size_t saveSize = 0;
    CHECK(host.memoryRegion(RETRO_MEMORY_SAVE_RAM, saveRam, saveSize) && saveSize && saveRam[0] == 0x52);
    CHECK(host.loadStateFromFile(saveDir + patchedState, error));

    CHECK(put(backup, Bytes{9}));
    CHECK(!ips::replace(cart, error));
    CHECK(get(rom) == headered && get(backup) == (Bytes{9}));
    CHECK(SDL_RemovePath(backup.c_str()));
    Bytes changed = headered;
    changed.back() = 1;
    CHECK(put(rom, changed));
    CHECK(!ips::replace(cart, error));
    CHECK(get(rom) == changed && !fileExists(backup));
    CHECK(host.isLoaded() && TitleDBContentCRC() == patchedCrc && host.saveRamPath() == patchedSave);
    CHECK(put(rom, headered));

#ifndef _WIN32
    if (geteuid() != 0) {
        // The cartridge itself stays writable, but staging a sibling file fails.
        const auto mode = fs::status(folder).permissions();
        fs::permissions(folder, mode & ~fs::perms::owner_write);
        CHECK(ips::canReplace(rom));
        CHECK(!ips::replace(cart, error));
        CHECK(get(rom) == headered && !fileExists(backup));
        CHECK(host.isLoaded() && TitleDBContentCRC() == patchedCrc && host.saveRamPath() == patchedSave);
        fs::permissions(folder, mode);
    }
#endif
    CHECK(ips::replace(cart, error));
    CHECK(get(backup) == headered && get(rom) == payload);
    Bytes replaced;
    CHECK(ips::replacedIdentity(rom, replaced) == patchedName && replaced == payload);
    CHECK(host.loadContent(rom, error, &replaced, patchedName));
    CHECK(host.saveRamPath() == patchedSave && TitleDBContentCRC() == patchedCrc);
    host.unloadContent();

    CHECK(put(rom, headered));  // restoring the original returns to the base save namespace
    CHECK(ips::replacedIdentity(rom, replaced).empty());
    CHECK(host.loadContent(rom, error));
    CHECK(host.saveRamPath() == baseSave && TitleDBContentCRC() == baseCrc);
    host.unloadContent();
}

} // namespace

int main() {
    parserTests();
    std::error_code ec;
    const fs::path folder = fs::temp_directory_path(ec) / ("vj-ips-test-" + std::to_string(SDL_GetPerformanceCounter()));
    if (ec || !fs::create_directory(folder, ec)) return 1;
    fileAndLoaderTests(folder);
    fs::remove_all(folder, ec);
    return failures || ec ? 1 : 0;
}
