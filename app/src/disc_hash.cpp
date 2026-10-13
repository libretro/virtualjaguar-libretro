#include "disc_hash.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include <libchdr/chd.h>
#include <libchdr/cdrom.h>

#include "library.h"

namespace vj {
namespace lib {

namespace {

#ifndef CDROM_SESSION_METADATA_TAG
#define CDROM_SESSION_METADATA_TAG CHD_MAKE_TAG('C', 'H', 'S', 'E')
#endif

const int kMaxTracks = 99;

struct ChdTrack {
    int number = 0;
    bool audio = false;
    int frames = 0;
    int pregap = 0;
    bool virtualPregap = false;
};

struct ChdCloser {
    chd_file* f;
    ~ChdCloser() {
        if (f) chd_close(f);
    }
};

}  // namespace

bool crcOfChdTrack1(const std::string& path, uint32_t& crc, const std::atomic<bool>* cancel) {
    chd_file* chd = nullptr;
    if (chd_open(path.c_str(), CHD_OPEN_READ, nullptr, &chd) != CHDERR_NONE || !chd) return false;
    ChdCloser closer{chd};
    const chd_header* head = chd_get_header(chd);
    if (!head || head->hunkbytes == 0 || (head->hunkbytes % CD_FRAME_SIZE) != 0) return false;

    // Track entries in file order, and whether any session metadata exists (as DetermineCHDTrackSessions does).
    std::vector<ChdTrack> tracks;
    bool sawChse = false;
    for (uint32_t i = 0;; i++) {
        char md[256];
        uint32_t tag = 0;
        md[0] = '\0';
        if (chd_get_metadata(chd, CHDMETATAG_WILDCARD, i, md, sizeof md, nullptr, &tag, nullptr) != CHDERR_NONE) break;
        if (tag == CDROM_SESSION_METADATA_TAG) {
            int sess = 1;
            if (std::sscanf(md, "SESSION:%d", &sess) == 1 && sess >= 1) sawChse = true;
        } else if (tag == CDROM_TRACK_METADATA2_TAG || tag == CDROM_TRACK_METADATA_TAG) {
            if ((int)tracks.size() >= kMaxTracks) return false;
            int tn = 0, frames = 0, pregap = 0, postgap = 0;
            char type[16] = "", sub[16] = "", pgtype[16] = "", pgsub[16] = "";
            bool ok;
            if (tag == CDROM_TRACK_METADATA2_TAG) {
                ok = std::sscanf(md, CDROM_TRACK_METADATA2_FORMAT, &tn, type, sub, &frames, &pregap, pgtype, pgsub, &postgap) == 8;
            } else {
                ok = std::sscanf(md, CDROM_TRACK_METADATA_FORMAT, &tn, type, sub, &frames) == 4;
                pregap = 0;
            }
            if (!ok || tn != (int)tracks.size() + 1 || frames < 0 || pregap < 0) return false;
            ChdTrack t;
            t.number = tn;
            t.audio = std::strcmp(type, "AUDIO") == 0;
            t.frames = frames;
            t.pregap = pregap;
            t.virtualPregap = pgtype[0] == 'V';
            tracks.push_back(t);
        }
    }
    if (tracks.empty()) return false;
    bool allAudio = true;
    for (const ChdTrack& t : tracks) allAudio = allAudio && t.audio;
    // The core refuses these (an old chdman flattened Jaguar CD's two sessions and wrote no CHSE).
    if (!sawChse && allAudio && tracks.size() >= 2) return false;

    // Track 1 is the first stored track: frame 0.  A virtual pregap is not part of a .bin; the core still
    // addresses the data after it at frame0 + (sector - startLBA), so start there.
    const ChdTrack& t1 = tracks[0];
    const uint32_t first = t1.virtualPregap ? (uint32_t)t1.pregap : 0u;
    if (first >= (uint32_t)t1.frames) return false;

    const uint32_t framesPerHunk = head->hunkbytes / CD_FRAME_SIZE;
    std::vector<uint8_t> hunk(head->hunkbytes);
    int64_t loaded = -1;
    uint32_t c = 0;
    for (uint32_t rel = first; rel < (uint32_t)t1.frames; rel++) {
        const uint32_t hunkNum = rel / framesPerHunk;
        if ((int64_t)hunkNum != loaded) {
            if (cancel && cancel->load()) return false;
            if (chd_read(chd, hunkNum, hunk.data()) != CHDERR_NONE) return false;
            loaded = hunkNum;
        }
        uint8_t sector[2352];
        std::memcpy(sector, hunk.data() + (size_t)(rel % framesPerHunk) * CD_FRAME_SIZE, 2352);
        if (t1.audio) {   // CHD audio is host-order PCM; .bin files are word-swapped
            for (int b = 0; b < 2352; b += 2) std::swap(sector[b], sector[b + 1]);
        }
        c = crc32Update(c, sector, sizeof sector);
    }
    crc = c;
    return true;
}

namespace {

uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

const uint8_t kCdiTrackMarker[20] = {0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
                                     0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};

}  // namespace

bool crcOfCdiTrack1(const std::string& path, uint32_t& crc, const std::atomic<bool>* cancel) {
    SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
    if (!io) return false;
    struct Closer {
        SDL_IOStream* io;
        ~Closer() { SDL_CloseIO(io); }
    } closer{io};
    auto rd = [&](void* d, size_t n) { return SDL_ReadIO(io, d, n) == n; };
    auto skip = [&](int64_t n) { return SDL_SeekIO(io, n, SDL_IO_SEEK_CUR) >= 0; };

    const int64_t size = SDL_GetIOSize(io);
    uint8_t trailer[8];
    if (size < 16 || SDL_SeekIO(io, size - 8, SDL_IO_SEEK_SET) < 0 || !rd(trailer, 8)) return false;
    const uint32_t version = le32(trailer), hdrOff = le32(trailer + 4);
    if (version != 0x80000004u && version != 0x80000005u && version != 0x80000006u) return false;   // V2 / V3 / V3.5
    const int64_t at = version == 0x80000006u ? size - (int64_t)hdrOff : (int64_t)hdrOff;
    if (at < 0 || at >= size || SDL_SeekIO(io, at, SDL_IO_SEEK_SET) < 0) return false;

    uint8_t b2[2];
    if (!rd(b2, 2) || (b2[0] | (b2[1] << 8)) < 1) return false;   // sessions
    if (!rd(b2, 2) || (b2[0] | (b2[1] << 8)) < 1) return false;   // tracks in session 1

    // The first track entry, parsed the way ParseCDI does.
    uint8_t w[4], marker[20], nameLen;
    if (!rd(w, 4)) return false;
    if (le32(w) != 0 && !skip(8)) return false;
    if (!rd(marker, 20) || std::memcmp(marker, kCdiTrackMarker, 20) != 0) return false;
    if (!skip(4) || !rd(&nameLen, 1) || !skip(nameLen) || !skip(19)) return false;
    if (!rd(w, 4)) return false;
    if (!skip(le32(w) == 0x80000000u ? 10 : 2)) return false;
    uint8_t td[0x57];
    if (!rd(td, sizeof td)) return false;
    const uint32_t pregap = le32(td + 0x00), length = le32(td + 0x04), mode = le32(td + 0x0e), code = le32(td + 0x36);
    // Only raw 2352-byte audio is laid out as the Redump track-1 bin is.
    if (mode != 0 || code != 2 || length == 0 || length > 90u * 60u * 75u) return false;

    const int64_t base = (int64_t)pregap * 2352;   // track 1 starts at file offset 0; the pregap is stored first
    if (base + (int64_t)length * 2352 > size) return false;
    if (SDL_SeekIO(io, base, SDL_IO_SEEK_SET) < 0) return false;
    std::vector<uint8_t> buf(2352u * 64u);
    uint32_t c = 0;
    uint32_t left = length;
    while (left) {
        if (cancel && cancel->load()) return false;
        const uint32_t n = std::min<uint32_t>(left, 64);
        if (!rd(buf.data(), (size_t)n * 2352)) return false;
        c = crc32Update(c, buf.data(), (size_t)n * 2352);
        left -= n;
    }
    crc = c;
    return true;
}

}  // namespace lib
}  // namespace vj
