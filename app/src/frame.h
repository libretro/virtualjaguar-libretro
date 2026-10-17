// One video frame as the app handles it: XRGB8888, tight pitch (width * 4 bytes).  Shared by CoreHost (the core's
// last frame), FrameExchange (frames handed to the UI thread) and VideoOut (the GL upload).
#pragma once

#include <cstdint>
#include <vector>

namespace vj {

struct Frame {
    unsigned width = 0;
    unsigned height = 0;
    std::vector<uint32_t> pixels;
    uint64_t serial = 0;  // bumps every time new pixels arrive (a NULL "dupe" frame does not)
};

}  // namespace vj
