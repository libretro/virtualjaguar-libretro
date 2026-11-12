#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vj::ips {

using Bytes = std::vector<unsigned char>;

bool apply(const Bytes& base, const Bytes& patch, Bytes& result, std::string& error);

struct Cartridge {
    std::string path;
    std::string baseHash;
    std::string outputHash;
    Bytes original;
    Bytes output;
    bool strippedHeader = false;
};

struct BaseRequirements {
    std::optional<uint64_t> size;
    std::string sha256, crc32;
};

bool prepare(const std::string& path, const std::string& patchPath, Cartridge& cart, std::string& error,
             const BaseRequirements& expected = {});
bool matchesHash(const std::string& expected, const std::string& actual);
std::string identity(const std::string& hash);
bool cartridgePath(const std::string& path);
std::string recoveryPath(const std::string& path);
std::string replacedIdentity(const std::string& path, Bytes& bytes);
bool canReplace(const std::string& path);
bool saveCopy(const Cartridge& cart, const std::string& path, std::string& error);
bool replace(const Cartridge& cart, std::string& error);

} // namespace vj::ips
