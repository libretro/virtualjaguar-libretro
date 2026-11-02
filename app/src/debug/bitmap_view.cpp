#include "bitmap_view.h"
#include <algorithm>
#include <cmath>

namespace vj {
unsigned bitmapRowBytes(unsigned width, unsigned depth) {
    if (!width || width > 4096 || depth > 5) return 0;
    const unsigned bits = depth == 5 ? 32 : opBitsPerPixel(depth);
    return (width * bits + 7) / 8;
}
OpObject bitmapObject(const BitmapSettings& q) {
    OpObject o;
    o.type = OpType::Bitmap;
    o.havePhrase1 = true;
    o.phrases = 2;
    o.data = q.address;
    o.bpp = opBitsPerPixel(q.depth);
    o.depthCode = q.depth;
    o.pixelsPerPhrase = q.depth == 5 ? 2 : (o.bpp ? 64 / o.bpp : 0);
    o.pitch = 1;
    o.dwidth = o.iwidth = o.iwidthEff = (bitmapRowBytes(q.width, q.depth) + 7) / 8;
    o.widthPixels = q.width;
    const unsigned row = bitmapRowBytes(q.width, q.depth);
    const unsigned rows = row && q.address < q.end && q.end <= 0x1000000 ? (q.end - q.address) / row : 0;
    o.height = q.height ? std::min(q.height, rows) : rows;
    o.p0 = ((uint64_t)(q.address >> 3) << 43) | ((uint64_t)o.height << 14);
    o.p1 = ((uint64_t)q.depth << 12) | ((uint64_t)1 << 15) |
           ((uint64_t)o.dwidth << 18) | ((uint64_t)o.iwidth << 28);
    return o;
}
bool bitmapRender(const vj_debug_iface* iface, const BitmapSettings& q, OpImage& out, std::string& err) {
    out = OpImage();
    err.clear();
    if (!bitmapRowBytes(q.width, q.depth) || q.address >= q.end || q.end > 0x1000000 ||
        (q.depth <= 3 && q.clutAddress > 0x1000000 - 512)) {
        err = "Invalid bitmap width, depth, address range or CLUT address";
        return false;
    }
    OpObject obj = bitmapObject(q);
    if (!obj.height) { err = "The range has no complete rows at this width"; return false; }
    OpRenderOptions opt;
    opt.color = q.color;
    opt.rowStrideBytes = bitmapRowBytes(q.width, q.depth);
    opt.sourceEnd = q.end;
    opt.clutAddress = q.clutAddress;
    const bool ok = opRender(iface, obj, opt, out, err);
    if (ok && q.height && q.height > obj.height) {
        out.truncated = true;
        out.note += " Range ends before requested height.";
    }
    return ok;
}

BitmapGuess bitmapGuess(const vj_debug_iface* iface, unsigned address, unsigned length, unsigned depth) {
    BitmapGuess out;
    if (!iface || !iface->peek || address >= 0x1000000 || depth > 5) return out;
    const unsigned n = std::min({length, 65536u, 0x1000000u - address});
    if (n < 256) { out.note = "Too little data to guess (need 256 bytes)"; return out; }
    std::vector<uint8_t> data(n);
    if (iface->peek(address, data.data(), n) != (int)n) { out.note = "Sample could not be read"; return out; }
    unsigned even[256] = {}, odd[256] = {}, distinct = 0;
    for (unsigned j = 0; j + 1 < n; j += 2) { even[data[j]]++; odd[data[j + 1]]++; }
    double difference = 0;
    for (unsigned j = 0; j < 256; j++) {
        if (even[j] || odd[j]) distinct++;
        difference += std::abs((double)even[j] - odd[j]);
    }
    if (distinct < 4) { out.note = "Uniform data: no reliable width or depth hint"; return out; }
    difference /= 2.0 * (n / 2);
    if (difference < 0.15) out.hint = BitmapDepthHint::Indexed8;
    else if (difference > 0.35) out.hint = BitmapDepthHint::Color16;
    const unsigned bits = depth == 5 ? 32 : opBitsPerPixel(depth);
    const unsigned maxStride = std::min({4096u, n / 4, bitmapRowBytes(4096, depth)});
    for (unsigned stride = 1; stride <= maxStride; stride++) {
        if (stride * 8 % bits) continue;
        const unsigned count = std::min(n - stride, 2048u);
        double error = 0;
        // Spread comparisons over the sample (do not alias a repeated row by stepping a fixed power of two).
        for (unsigned j = 0; j < count; j++) {
            const unsigned at = (unsigned)((uint64_t)j * (n - stride) / count);
            error += std::abs((int)data[at] - (int)data[at + stride]);
        }
        double score = 1.0 - error / (count * 255.0);
        if (stride % 8 == 0) score += 0.002;
        score -= stride * 0.000001; // equal harmonics: prefer the fundamental
        out.candidates.push_back({stride * 8 / bits, stride, score});
    }
    std::sort(out.candidates.begin(), out.candidates.end(), [](const BitmapCandidate& a, const BitmapCandidate& b) {
        return a.score > b.score;
    });
    if (out.candidates.size() > 5) out.candidates.resize(5);
    out.note = "Heuristic from up to 64 KiB; widths use the selected depth";
    return out;
}

void BitmapViewModel::changed() {
    gen_++; dirty_ = true; result_ = BitmapResult(); guesses_ = BitmapGuess(); imageSerial_++;
}
void BitmapViewModel::clear() {
    changed();
    // Queued jobs retain their own shared state; a closed window cannot adopt their answers.
    render_ = CoreJob<RenderReply>(); guess_ = CoreJob<GuessReply>();
    serial_ = ~0ull;
}
void BitmapViewModel::guess(DebugSession& s) {
    if (!s.enabled()) return;
    const auto q = settings;
    const auto gen = gen_, ep = s.views().epoch();
    if (guess_.pending()) return;
    guess_.submit(s, [q, gen, ep](const vj_debug_iface* iface) {
        GuessReply r; r.gen = gen; r.epoch = ep;
        r.result = bitmapGuess(iface, q.address, q.end > q.address ? q.end - q.address : 0, q.depth);
        return r;
    });
}
void BitmapViewModel::update(DebugSession& s) {
    if (epoch_ != s.views().epoch()) { clear(); epoch_ = s.views().epoch(); }
    RenderReply r;
    if (render_.take(r) && r.gen == gen_ && r.epoch == epoch_) {
        result_ = std::move(r.result); imageSerial_++;
    }
    GuessReply g;
    if (guess_.take(g) && g.gen == gen_ && g.epoch == epoch_) guesses_ = std::move(g.result);
    if (!s.enabled()) { clear(); return; }
    if (serial_ != s.refreshSerial()) { dirty_ = true; serial_ = s.refreshSerial(); }
    if (!dirty_ || render_.pending()) return;
    dirty_ = false;
    const auto q = settings;
    const auto gen = gen_, ep = epoch_;
    render_.submit(s, [q, gen, ep](const vj_debug_iface* iface) {
        RenderReply r; r.gen = gen; r.epoch = ep;
        r.result.ok = bitmapRender(iface, q, r.result.image, r.result.error);
        return r;
    });
}
} // namespace vj
