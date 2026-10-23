// SHA-256 (FIPS 180-4) for verifying downloaded cheat packs against the manifest's
// "sha256" before anything is installed.  No dependencies, no exceptions, and the
// big-endian loads/stores are byte-wise so it is correct on either host byte order.
#pragma once

#include <cstddef>
#include <string>

namespace vj {

// Lower-case hex digest (64 characters) of `size` bytes at `data`.  `data` may be
// null when `size` is 0.
std::string sha256Hex(const void* data, size_t size);

} // namespace vj
