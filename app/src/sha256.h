// SHA-256 (FIPS 180-4) for verifying downloaded files and cheat packs against a manifest's
// "sha256" before anything is installed.  No dependencies, no exceptions, and the
// big-endian loads/stores are byte-wise so it is correct on either host byte order.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace vj {

class Sha256 {
public:
    void update(const void* data, size_t size);
    std::string hexDigest() const;

private:
    uint32_t state_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    unsigned char buffer_[64] = {};
    size_t buffered_ = 0;
    uint64_t size_ = 0;
};

// Lower-case hex digest (64 characters) of `size` bytes at `data`.  `data` may be
// null when `size` is 0.
std::string sha256Hex(const void* data, size_t size);

} // namespace vj
