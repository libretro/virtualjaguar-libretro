// CRC32 of a disc image's first track, the number the Redump dat lists ("Atari - Jaguar CD.dat" has one rom
// per game: the Track 01 .bin).  Pure file reading: no ImGui, no GL, no core state.  .cue sets are hashed
// by the scanner (library.cpp); these cover the single-file formats.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace vj {
namespace lib {

// .chd: the raw 2352-byte sectors of track 1 exactly as the Redump .bin has them.  Mirrors the core
// (ParseCHD / CDIntfReadBlockCHD in src/cd/cdintf.c): 2448-byte frames with the subcode dropped, tracks padded
// to 4 frames, AUDIO frames byte-swapped back to .bin order, a virtual pregap (PGTYPE V*) left out.  A CHD the
// core refuses (several audio tracks and no CHSE session metadata) is not hashed.
// Returns false when the file is not a hashable CD CHD (or on cancel); `crc` is untouched then.
bool crcOfChdTrack1(const std::string& path, uint32_t& crc, const std::atomic<bool>* cancel);

// .cdi: best effort.  Track 1's sectors straight from the descriptor layout (file offset 0, INDEX 01 at the
// descriptor's pregap).  The core repairs rips that are not laid out like the descriptors say; this does not,
// so such a rip simply fails to match and falls back to its name.
bool crcOfCdiTrack1(const std::string& path, uint32_t& crc, const std::atomic<bool>* cancel);

}  // namespace lib
}  // namespace vj
