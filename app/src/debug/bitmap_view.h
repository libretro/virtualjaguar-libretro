// Raw memory images reuse the OP pixel decoder. Core work runs inside a CoreJob.
#pragma once
#include "core_job.h"
#include "op_render.h"
namespace vj {
struct BitmapSettings {
    unsigned address = 0, end = 0x200000;
    unsigned width = 320, height = 200, depth = 3; // height 0 = complete rows to range end
    unsigned clutAddress = 0xF00400;
    OpColorMode color = OpColorMode::Auto;
};
unsigned bitmapRowBytes(unsigned width, unsigned depth);
OpObject bitmapObject(const BitmapSettings& q);
bool bitmapRender(const vj_debug_iface*, const BitmapSettings&, OpImage&, std::string&);
enum class BitmapDepthHint { Unknown, Indexed8, Color16 };
struct BitmapCandidate { unsigned width = 0, stride = 0; double score = 0; };
struct BitmapGuess {
    std::vector<BitmapCandidate> candidates;
    BitmapDepthHint hint = BitmapDepthHint::Unknown;
    std::string note;
};
// At most 64 KiB, strides 1..4096 bytes; no memory read crosses the supplied range.
BitmapGuess bitmapGuess(const vj_debug_iface*, unsigned address, unsigned length, unsigned depth);
struct BitmapResult { OpImage image; std::string error; bool ok = false; };
class BitmapViewModel {
public:
    BitmapSettings settings;
    const BitmapResult& result() const { return result_; }
    const BitmapGuess& guesses() const { return guesses_; }
    uint64_t imageSerial() const { return imageSerial_; }
    void changed();
    void clear();
    void guess(DebugSession&);
    void update(DebugSession&);
private:
    struct RenderReply { BitmapResult result; uint64_t gen = 0, epoch = 0; };
    struct GuessReply { BitmapGuess result; uint64_t gen = 0, epoch = 0; };
    CoreJob<RenderReply> render_;
    CoreJob<GuessReply> guess_;
    BitmapResult result_;
    BitmapGuess guesses_;
    uint64_t gen_ = 0, imageSerial_ = 0, epoch_ = ~0ull, serial_ = ~0ull;
    bool dirty_ = true;
};
} // namespace vj
