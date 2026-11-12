#include "ips.h"

#include "paths.h"
#include "sha256.h"
#include "file.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace vj::ips {
namespace fs = std::filesystem;
namespace {

constexpr size_t kMaxRom = 0x1000000;
constexpr size_t kMaxPatch = 2 * kMaxRom;

unsigned bigEndian(const unsigned char* p, int count) {
    unsigned value = 0;
    while (count--) value = (value << 8) | *p++;
    return value;
}

bool fail(std::string& error, const char* message) {
    error = message;
    return false;
}

bool readBounded(const std::string& path, size_t limit, Bytes& bytes) {
    std::error_code ec;
    if (!fs::is_regular_file(fs::u8path(path), ec)) return false;
    SDL_IOStream* stream = SDL_IOFromFile(path.c_str(), "rb");
    if (!stream) return false;
    const Sint64 size = SDL_GetIOSize(stream);
    bool ok = false;
    if (size > 0 && static_cast<Uint64>(size) <= limit) {
        bytes.resize(static_cast<size_t>(size));
        unsigned char extra;
        ok = SDL_ReadIO(stream, bytes.data(), bytes.size()) == bytes.size()
            && SDL_ReadIO(stream, &extra, 1) == 0 && SDL_GetIOStatus(stream) == SDL_IO_STATUS_EOF;
    }
    return SDL_CloseIO(stream) && ok;
}

// Publish a complete file in the same directory. A hard link gives new files
// exclusive publication without ever exposing a partial copy.
class StagedFile {
public:
    ~StagedFile() {
        if (dir.empty()) return;
        std::error_code ec;
        fs::remove(file, ec);
        fs::remove(dir, ec);
    }

    bool write(const std::string& destination, const Bytes& bytes) {
        const auto parent = fs::u8path(destination).parent_path();
        for (int attempt = 0; attempt < 32; ++attempt) {
            auto candidate = parent / (".vj-ips-" + std::to_string(SDL_GetPerformanceCounter()) + "-" +
                                        std::to_string(attempt));
            std::error_code ec;
            if (fs::create_directory(candidate, ec)) {
                dir = std::move(candidate);
                file = dir / "cartridge";
                SDL_IOStream* stream = SDL_IOFromFile(file.u8string().c_str(), "wb");
                if (!stream) return false;
                const bool written = SDL_SaveFile_IO(stream, bytes.data(), bytes.size(), false) && SDL_FlushIO(stream);
                return SDL_CloseIO(stream) && written;
            }
            if (ec && ec != std::errc::file_exists) return false;
        }
        return false;
    }

    bool setPermissions(fs::perms permissions) const {
        std::error_code ec;
        fs::permissions(file, permissions, fs::perm_options::replace, ec);
        return !ec;
    }

    bool publishNew(const std::string& destination) const {
        std::error_code ec;
        fs::create_hard_link(file, fs::u8path(destination), ec);
        return !ec;
    }

    bool replace(const std::string& destination) const {
        return SDL_RenamePath(file.u8string().c_str(), destination.c_str());
    }

private:
    fs::path dir;
    fs::path file;
};

} // namespace

bool apply(const Bytes& base, const Bytes& patch, Bytes& result, std::string& error) {
    if (base.empty() || base.size() > kMaxRom)
        return fail(error, "Base cartridge size is out of range");
    if (patch.size() < 8 || patch.size() > kMaxPatch || std::memcmp(patch.data(), "PATCH", 5))
        return fail(error, "Invalid IPS header or patch size");

    Bytes output = base;
    size_t pos = 5;
    while (patch.size() - pos >= 3) {
        if (std::memcmp(patch.data() + pos, "EOF", 3) == 0) {
            pos += 3;
            const size_t remaining = patch.size() - pos;
            if (remaining == 3) {
                const size_t size = bigEndian(patch.data() + pos, 3);
                if (size == 0 || size > kMaxRom) return fail(error, "Invalid IPS final size");
                output.resize(size);
            } else if (remaining != 0) {
                return fail(error, "Unexpected data after IPS EOF");
            }
            result.swap(output);
            return true;
        }
        const size_t offset = bigEndian(patch.data() + pos, 3);
        pos += 3;
        if (patch.size() - pos < 2) return fail(error, "Truncated IPS record length");
        size_t count = bigEndian(patch.data() + pos, 2);
        pos += 2;
        const bool rle = count == 0;
        if (rle) {
            if (patch.size() - pos < 3) return fail(error, "Truncated IPS RLE record");
            count = bigEndian(patch.data() + pos, 2);
            pos += 2;
            if (count == 0) return fail(error, "Empty IPS RLE record");
        } else if (count > patch.size() - pos) {
            return fail(error, "Truncated IPS record data");
        }
        if (count > kMaxRom - offset) return fail(error, "IPS record exceeds the 16 MiB cartridge limit");
        if (offset + count > output.size()) output.resize(offset + count);
        if (rle) {
            std::fill_n(output.begin() + offset, count, patch[pos++]);
        } else {
            std::copy_n(patch.begin() + pos, count, output.begin() + offset);
            pos += count;
        }
    }
    return fail(error, "Missing IPS EOF");
}

bool cartridgePath(const std::string& path) {
    const std::string ext = pathExt(path);
    return ext == "j64" || ext == "jag" || ext == "rom" || ext == "bin";
}

bool prepare(const std::string& path, const std::string& patchPath, Cartridge& cart, std::string& error,
             const BaseRequirements& expected) {
    Cartridge prepared;
    prepared.path = path;
    if (!cartridgePath(path) || !readBounded(path, kMaxRom + 512, prepared.original))
        return fail(error, "Choose a readable Jaguar cartridge (.j64, .jag, .rom or .bin)");
    const size_t skip = DetectPrependedHeaderSize(prepared.original.data(), static_cast<uint32_t>(prepared.original.size()));
    Bytes payload(prepared.original.begin() + skip, prepared.original.end());
    if (!JaguarIsHeaderlessCartridge(payload.data(), payload.size()))
        return fail(error, "The base is not a supported Jaguar cartridge");
    prepared.baseHash = sha256Hex(prepared.original.data(), prepared.original.size());
    if ((expected.size && *expected.size != prepared.original.size()) ||
            !matchesHash(expected.sha256, prepared.baseHash))
        return fail(error, "The downloaded cartridge does not match the expected patch base");
    if (!expected.crc32.empty()) {
        char crc[9];
        std::snprintf(crc, sizeof crc, "%08x",
                      static_cast<unsigned>(SDL_crc32(0, prepared.original.data(), prepared.original.size())));
        if (expected.crc32 != crc)
            return fail(error, "The downloaded cartridge does not match the expected base CRC32");
    }
    Bytes patch;
    if (!readBounded(patchPath, kMaxPatch, patch))
        return fail(error, "Could not read the IPS file (32 MiB maximum)");
    if (!apply(payload, patch, prepared.output, error)) return false;
    if (!JaguarIsHeaderlessCartridge(prepared.output.data(), prepared.output.size()))
        return fail(error, "The patched bytes do not form a supported Jaguar cartridge");
    prepared.strippedHeader = skip != 0;
    prepared.outputHash = sha256Hex(prepared.output.data(), prepared.output.size());
    cart = std::move(prepared);
    return true;
}

bool matchesHash(const std::string& expected, const std::string& actual) {
    if (expected.empty()) return true;
    return std::equal(expected.begin(), expected.end(), actual.begin(), actual.end(),
                      [](char a, char b) { return a == b || (a >= 'A' && a <= 'F' && a + ('a' - 'A') == b); });
}

std::string identity(const std::string& hash) {
    return "IPS-" + hash;
}

std::string recoveryPath(const std::string& path) {
    return path + ".ips-original";
}

std::string replacedIdentity(const std::string& path, Bytes& bytes) {
    bytes.clear();
    if (!cartridgePath(path) || !fileExists(recoveryPath(path))) return {};
    Bytes original, current;
    if (!readBounded(path, kMaxRom, current) || !readBounded(recoveryPath(path), kMaxRom + 512, original)
            || current == original)
        return {};
    auto name = identity(sha256Hex(current.data(), current.size()));
    bytes.swap(current);
    return name;
}

bool canReplace(const std::string& path) {
    std::error_code ec;
    const auto status = fs::symlink_status(fs::u8path(path), ec);
    const auto writable = fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
    if (!fs::is_regular_file(status) || (status.permissions() & writable) == fs::perms::none)
        return false;
    auto backup = fs::symlink_status(fs::u8path(recoveryPath(path)), ec);
    if (backup.type() != fs::file_type::not_found) return false;
    SDL_IOStream* stream = SDL_IOFromFile(path.c_str(), "r+b");
    return stream && SDL_CloseIO(stream);
}

bool saveCopy(const Cartridge& cart, const std::string& path, std::string& error) {
    StagedFile staged;
    if (cart.output.empty() || !staged.write(path, cart.output) || !staged.publishNew(path))
        return fail(error, "Could not save a new copy. Choose a writable folder without an existing copy.");
    return true;
}

bool replace(const Cartridge& cart, std::string& error) {
    if (cart.output.empty() || cart.output == cart.original)
        return fail(error, "There are no patched bytes to replace the original with");
    if (!canReplace(cart.path))
        return fail(error, "Replacement requires a writable regular file without an existing recovery copy");
    Bytes current;
    if (!readBounded(cart.path, kMaxRom + 512, current) || current != cart.original)
        return fail(error, "The original changed since the patch was prepared");
    StagedFile recovery, patched;
    const auto backup = recoveryPath(cart.path);
    if (!recovery.write(backup, cart.original) || !patched.write(cart.path, cart.output))
        return fail(error, "Could not stage the patched cartridge and recovery copy");
    std::error_code ec;
    const auto permissions = fs::status(fs::u8path(cart.path), ec).permissions();
    if (ec) return fail(error, "Could not read original cartridge permissions");
    // Keep the original file's permissions on both published files.
    if (!recovery.setPermissions(permissions) || !patched.setPermissions(permissions))
        return fail(error, "Could not preserve original cartridge permissions");
    if (!recovery.publishNew(backup))
        return fail(error, "Could not publish the recovery copy. Original left intact.");
    if (!readBounded(cart.path, kMaxRom + 512, current) || current != cart.original
            || !fs::is_regular_file(fs::symlink_status(fs::u8path(cart.path), ec))
            || !patched.replace(cart.path)) {
        fs::remove(fs::u8path(backup), ec);
        return fail(error, "The original changed or could not be replaced");
    }
    return true;
}

} // namespace vj::ips
