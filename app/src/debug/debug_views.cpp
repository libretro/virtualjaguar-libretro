// DebugViews (debug_views.h): the request table, the per-kind fills, the epoch.
#include "debug_views.h"

#include <algorithm>
#include <cstdio>

#include "debug_session.h"   // DebugSession::kDramSize

namespace vj {

bool DebugViews::equal(const ViewRequest& a, const ViewRequest& b) {
    return a.kind == b.kind && a.addr == b.addr && a.len == b.len && a.target == b.target && a.rows == b.rows &&
           a.since == b.since;
}

DebugViews::Id DebugViews::set(Id id, const ViewRequest& in) {
    ViewRequest r = in;
    r.len = std::min(r.len, kMaxMemoryBytes);
    r.rows = std::min(r.rows, kMaxDisasmRows);
    std::lock_guard<std::mutex> lk(mu_);
    auto it = id == kNoId ? slots_.end() : slots_.find(id);
    if (it == slots_.end()) {
        if (id == kNoId) id = nextId_++;
        else if (id >= nextId_) nextId_ = id + 1;   // keep allocated ids unique after a caller-chosen one
        it = slots_.emplace(id, Slot{}).first;
        it->second.req = r;
        it->second.rev = 1;
        return id;
    }
    Slot& s = it->second;
    if (equal(s.req, r)) return id;
    s.req = r;
    s.rev++;
    s.front = ViewBuffer{};   // serial 0: unreadable and pending until the next fill
    return id;
}

void DebugViews::clear(Id id) {
    std::lock_guard<std::mutex> lk(mu_);
    slots_.erase(id);
}

void DebugViews::setDisasm(DisasmFn fn) {
    std::lock_guard<std::mutex> lk(mu_);
    disasm_ = std::move(fn);
}

bool DebugViews::read(Id id, ViewBuffer& out) const {
    const uint64_t ep = epoch();
    std::lock_guard<std::mutex> lk(mu_);
    auto it = slots_.find(id);
    if (it == slots_.end() || it->second.front.serial == 0 || it->second.front.epoch != ep) return false;
    out = it->second.front;
    return true;
}

size_t DebugViews::count() const {
    std::lock_guard<std::mutex> lk(mu_);
    return slots_.size();
}

bool DebugViews::pending(uint64_t serial) const {
    const uint64_t ep = epoch();
    std::lock_guard<std::mutex> lk(mu_);
    for (const auto& kv : slots_)
        if (stale(kv.second, serial, ep)) return true;
    return false;
}

uint64_t DebugViews::serialOf(Id id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = slots_.find(id);
    return it == slots_.end() ? 0 : it->second.front.serial;
}

void DebugViews::bumpEpoch() { epoch_.fetch_add(1); }

uint64_t DebugViews::epoch() const { return epoch_.load(); }

static std::string hexWords(const unsigned char* b, unsigned n) {
    std::string s;
    char t[4];
    for (unsigned i = 0; i < n; i++) {
        if (i && (i & 1) == 0) s += ' ';
        std::snprintf(t, sizeof t, "%02X", b[i]);
        s += t;
    }
    return s;
}

// Builds b.req's answer.  Runs with no lock held, on the emulation thread.
void DebugViews::fillOne(const vj_debug_iface* iface, const DisasmFn& dfn, ViewBuffer& b) {
    const ViewRequest& r = b.req;
    switch (r.kind) {
        case ViewRequest::Kind::Memory:
        case ViewRequest::Kind::Dram: {
            const unsigned addr = r.kind == ViewRequest::Kind::Dram ? 0 : r.addr;
            const unsigned len = r.kind == ViewRequest::Kind::Dram ? DebugSession::kDramSize : r.len;
            if (!iface->peek || len == 0) return;
            b.bytes.resize(len);
            if (iface->peek(addr, b.bytes.data(), len) == (int)len) b.ok = true;
            else b.bytes.clear();
            return;
        }
        case ViewRequest::Kind::Regs:
            if (iface->get_regs) b.ok = iface->get_regs(r.target, &b.regs) == 1;
            return;
        case ViewRequest::Kind::Disasm: {
            if (dfn) {
                b.lines = dfn(iface, r.target, r.addr, r.rows);
                b.ok = true;
                return;
            }
            if (r.target != VJ_DBG_TARGET_GPU && r.target != VJ_DBG_TARGET_DSP) return;   // the 68000 needs the DisasmFn
            if (!iface->disasm_risc) return;
            unsigned a = r.addr;
            for (unsigned i = 0; i < r.rows; i++) {
                DisasmLine l;
                char out[128];
                unsigned len = 2;
                l.addr = a;
                if (iface->disasm_risc(r.target, a, out, sizeof out, &len)) {
                    if (len < 2 || len > 6) len = 2;
                    l.len = len;
                    l.text = out;
                    unsigned char raw[8] = {0};
                    if (iface->peek && iface->peek(a, raw, len) == (int)len) l.bytes = hexWords(raw, len);
                    a += len;
                } else {
                    a += 2;
                }
                b.lines.push_back(std::move(l));
            }
            b.ok = true;
            return;
        }
        case ViewRequest::Kind::OpSnapshot: {
            if (!iface->op_snapshot_get) return;
            std::vector<vj_debug_op_obj> buf(VJ_DBG_OP_MAX_OBJECTS);
            unsigned halfline = 0;
            const int n = iface->op_snapshot_get(buf.data(), VJ_DBG_OP_MAX_OBJECTS, &halfline);
            b.opCount = n;
            b.halfline = halfline;
            if (n > 0) b.objs.assign(buf.begin(), buf.begin() + std::min(n, (int)VJ_DBG_OP_MAX_OBJECTS));
            b.ok = true;
            return;
        }
        case ViewRequest::Kind::BlitLog: {
            if (!iface->blit_log_head || !iface->blit_log_get) return;
            // As blit::LogTracker::sync: launches that fell out of the ring are counted lost, never replayed.
            const unsigned head = iface->blit_log_head();
            b.blitHead = head;
            b.blitLost = 0;
            b.ok = true;
            if (head < r.since) return;   // the log was switched off and on again: its numbering restarted
            const unsigned oldest = head > VJ_DBG_BLIT_LOG_SIZE ? head - VJ_DBG_BLIT_LOG_SIZE : 0;
            const unsigned start = std::max(r.since, oldest);
            b.blitLost = start - r.since;
            for (unsigned i = start; i < head; i++) {
                vj_debug_blit e;
                if (iface->blit_log_get(i, &e)) b.blits.push_back(e);
                else b.blitLost++;
            }
            return;
        }
    }
}

size_t DebugViews::fill(const vj_debug_iface* iface, uint64_t serial) {
    if (!iface) return 0;
    struct Job {
        Id id;
        ViewRequest req;
        uint64_t rev;
    };
    std::vector<Job> jobs;
    DisasmFn dfn;
    {
        std::lock_guard<std::mutex> lk(mu_);
        const uint64_t ep = epoch();
        for (const auto& kv : slots_)
            if (stale(kv.second, serial, ep)) jobs.push_back({kv.first, kv.second.req, kv.second.rev});
        if (!jobs.empty()) dfn = disasm_;
    }
    size_t installed = 0;
    for (const Job& j : jobs) {
        ViewBuffer b;
        b.req = j.req;
        b.epoch = epoch();   // read BEFORE the fill: a content switch during it leaves the buffer unreadable
        b.serial = serial;
        b.frame = iface->frame_number ? iface->frame_number() : 0;
        fillOne(iface, dfn, b);
        std::lock_guard<std::mutex> lk(mu_);
        auto it = slots_.find(j.id);
        if (it == slots_.end() || it->second.rev != j.rev) continue;
        it->second.front = std::move(b);
        installed++;
    }
    return installed;
}

}  // namespace vj
