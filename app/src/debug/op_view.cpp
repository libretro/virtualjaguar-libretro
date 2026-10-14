#include "op_view.h"

#include <cstdio>

namespace vj {

namespace {
bool peekReader(void* ctx, unsigned addr, uint8_t* dst, unsigned n) {
    const vj_debug_iface* i = static_cast<const vj_debug_iface*>(ctx);
    return i && i->peek && i->peek(addr, dst, n) == (int)n;
}
}  // namespace

void OpViewModel::clear() {
    objs_.clear();
    waiting_ = have_ = false;
    sel_ = -1;
    autoSelected_ = false;
    seenSerial_ = false;
    haveImage_ = false;
    imgErr_.clear();
    haveEdit_ = false;
    img_ = OpImage();
    imgSerial_++;
}

void OpViewModel::arm(DebugSession& s) {
    const vj_debug_iface* i = s.iface();
    if (!s.enabled() || !i || !i->op_snapshot_arm) return;
    i->op_snapshot_arm();
    waiting_ = true;
    arms_++;
}

void OpViewModel::select(int idx) {
    if (idx < 0 || idx >= (int)objs_.size()) {
        sel_ = -1;
    } else {
        sel_ = idx;
        selAddr_ = objs_[(size_t)idx].address;
    }
    haveEdit_ = false;
    imageDirty_ = true;
}

void OpViewModel::absorb(DebugSession& s) {
    const vj_debug_iface* i = s.iface();
    std::vector<vj_debug_op_obj> buf((size_t)VJ_DBG_OP_MAX_OBJECTS);
    unsigned hl = 0;
    int n = i->op_snapshot_get(buf.data(), VJ_DBG_OP_MAX_OBJECTS, &hl);
    if (n < 0) return;                       // armed, no walk finished yet
    objs_ = opDecodeAll(buf.data(), n);
    snapshotCount_ = (size_t)n;
    if (followLinks && n > 0) {
        std::vector<OpObject> walk = opWalkList(&peekReader, const_cast<vj_debug_iface*>(i), objs_[0].address,
                                                (unsigned)VJ_DBG_OP_MAX_OBJECTS);
        size_t room = (size_t)VJ_DBG_OP_MAX_OBJECTS > objs_.size() ? (size_t)VJ_DBG_OP_MAX_OBJECTS - objs_.size() : 0;
        for (OpObject& w : walk) {
            if (room == 0) break;
            bool inSnap = false;
            for (size_t k = 0; k < snapshotCount_; k++)
                if (objs_[k].address == w.address) { inSnap = true; break; }
            if (inSnap) continue;
            if (w.warning.empty() && w.isBitmap()) w.warning = "read from RAM after the frame: HEIGHT / DATA may be advanced";
            objs_.push_back(std::move(w));
            room--;
        }
    }
    halfline_ = hl;
    waiting_ = false;
    have_ = true;
    // Keep the selection on the same object when it is still there.
    if (sel_ >= 0) {
        const int total = (int)objs_.size();
        int found = -1;
        if (sel_ < total && objs_[(size_t)sel_].address == selAddr_) found = sel_;
        for (int k = 0; found < 0 && k < total; k++)
            if (objs_[(size_t)k].address == selAddr_) found = k;
        sel_ = found;
    }
    // Opening the window shows something: the first bitmap the snapshot walked (or any drawable one).
    if (sel_ < 0 && !autoSelected_) {
        int pick = -1;
        for (size_t k = 0; k < objs_.size() && pick < 0; k++)
            if (objs_[k].drawable()) pick = (int)k;
        if (pick >= 0) {
            autoSelected_ = true;
            select(pick);
        }
    }
    imageDirty_ = true;
}

bool OpViewModel::update(DebugSession& s) {
    if (!s.enabled() || !s.iface() || !s.iface()->op_snapshot_get) return false;
    bool changed = false;
    if (waiting_) {
        absorb(s);
        if (!waiting_) changed = true;
    }
    uint64_t ser = s.refreshSerial();
    if (!seenSerial_ || ser != lastSerial_) {
        bool first = !seenSerial_;
        seenSerial_ = true;
        lastSerial_ = ser;
        if (first && !have_ && !waiting_) arm(s);          // opening the window: take a first snapshot
        else if (live && !waiting_) arm(s);
        imageDirty_ = true;                                  // DRAM may have moved under the preview
    }
    if (imageDirty_) {
        rerender(s);
        changed = true;
    }
    return changed;
}

void OpViewModel::rerender(DebugSession& s) {
    imageDirty_ = false;
    const OpObject* o = selectedObject();
    OpImage fresh;
    std::string err;
    bool ok = false;
    if (!o) err = "";
    else if (!o->isBitmap()) err = "";
    else ok = opRender(s.iface(), *o, render, fresh, err);
    bool differs = ok != haveImage_ || (ok && (fresh.w != img_.w || fresh.h != img_.h || fresh.rgba != img_.rgba ||
                                              fresh.note != img_.note || fresh.mode != img_.mode));
    haveImage_ = ok;
    imgErr_ = ok ? std::string() : err;
    if (ok) img_ = std::move(fresh);
    if (differs) imgSerial_++;
}

bool OpViewModel::paint(DebugSession& s, int x, int y, uint32_t value, std::string& status) {
    const OpObject* o = selectedObject();
    if (!o || !haveImage_) { status = "select a drawable bitmap first"; return false; }
    OpPixelPatch p;
    if (!opPixelPatch(s.iface(), *o, img_, x, y, value, p)) { status = "that pixel cannot be read"; return false; }
    int rc = s.poke(p.addr, p.bytes, p.n);
    if (rc < 0) {
        status = std::string("not written: ") + DebugSession::errorText(rc);
        return false;
    }
    edit_.addr = p.addr;
    edit_.n = p.n;
    for (unsigned k = 0; k < 4; k++) edit_.bytes[k] = p.bytes[k];
    haveEdit_ = true;
    char b[96];
    std::snprintf(b, sizeof b, "wrote %u byte%s at %06X", p.n, p.n == 1 ? "" : "s", p.addr);
    status = b;
    imageDirty_ = true;   // the poke also bumped the session serial; redraw from DRAM
    rerender(s);
    return true;
}

OpScreenGeom OpViewModel::readGeom(const DebugSession& s, unsigned frameW, unsigned frameH) const {
    OpScreenGeom g;
    g.frameW = frameW;
    g.frameH = frameH;
    unsigned v = 0;
    if (s.readU(0xF00038, 2, v)) g.hdb1 = v & 0x7FF;
    if (s.readU(0xF00046, 2, v)) g.vdb = v & 0x7FF;
    if (s.readU(0xF0003E, 2, v)) g.vp = v & 0x7FF;
    if (s.readU(0xF00028, 2, v)) g.vmode = v;
    return g;
}

}  // namespace vj
