#include "op_view.h"

#include <cstdio>
#include <cstring>

namespace vj {

namespace {
bool peekReader(void* ctx, unsigned addr, uint8_t* dst, unsigned n) {
    const vj_debug_iface* i = static_cast<const vj_debug_iface*>(ctx);
    return i && i->peek && i->peek(addr, dst, n) == (int)n;
}

// A big-endian word of the geometry buffer at byte offset `at`.
unsigned beWord(const uint8_t* b, unsigned at) { return ((unsigned)b[at] << 8) | b[at + 1]; }
}  // namespace

OpStep opStepFor(const OpStepGate& g) {
    if (!g.loaded || !g.paused || g.halted || g.netplay) return OpStep::None;
    // Threaded: EmuThread::advance() gives up when a halt begins, so an attached session is fine.  Inline, a frame run
    // from the draw hook that then halts would start a second ImGui frame: attached and inFrame refuse.
    if (g.threaded) return OpStep::Advance;
    return !g.attached && !g.inFrame ? OpStep::Inline : OpStep::None;
}

void OpViewModel::showPreview(DebugSession& s, const OpObject& obj, const OpRenderOptions& opt) {
    clear();
    epochSet_ = true;
    epochSeen_ = s.views().epoch();
    live = false;
    render = opt;
    setObjects({obj});
    select(0);
}

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
    renderGen_++;   // results still on their way belong to the old content
    walkGen_++;
    pendingSnap_ = PendingSnap();
    cry_.clear();
    cryOk_ = cryTried_ = false;
    geomOk_ = false;
    geomSerial_ = 0;
}

void OpViewModel::setObjects(std::vector<OpObject> objs) {
    objs_ = std::move(objs);
    snapshotCount_ = objs_.size();
    have_ = true;
    waiting_ = false;
    sel_ = -1;
    haveEdit_ = false;
    imageDirty_ = true;
    renderGen_++;
    walkGen_++;
}

void OpViewModel::ensureViews(DebugSession& s) {
    if (snapId_ == DebugViews::kNoId) {
        ViewRequest r;
        r.kind = ViewRequest::Kind::OpSnapshot;
        r.since = (unsigned)armGen_;
        snapId_ = s.views().set(DebugViews::kNoId, r);
    }
    if (geomId_ == DebugViews::kNoId) {
        ViewRequest r;
        r.kind = ViewRequest::Kind::Memory;
        r.addr = kGeomAddr;
        r.len = kGeomLen;
        geomId_ = s.views().set(DebugViews::kNoId, r);
    }
}

void OpViewModel::releaseViews(DebugSession& s) {
    if (snapId_ != DebugViews::kNoId) {
        // Posted, not cleared here: an arm still queued would re-create the request after a clear from this thread.
        // The lane is FIFO, so this lands behind it (inline it runs at once).  The views live in the session.
        DebugViews* v = &s.views();
        const DebugViews::Id id = snapId_;
        s.postCore([v, id] { v->clear(id); });
    }
    if (geomId_ != DebugViews::kNoId) s.views().clear(geomId_);
    snapId_ = geomId_ = DebugViews::kNoId;
    waiting_ = false;
    seenSerial_ = false;
    geomOk_ = false;
    geomSerial_ = 0;
    walkGen_++;
}

void OpViewModel::arm(DebugSession& s) {
    if (!s.enabled()) return;
    ensureViews(s);   // the posted lambda below must never allocate an id this model does not know
    const uint64_t gen = ++armGen_;
    waiting_ = true;
    arms_++;
    // The buffer is reset AFTER the arm has applied on the emulation thread, so one filled before the arm can never
    // be mistaken for its answer.  Two arms queued together reset it twice; only the newer `since` is absorbed.
    DebugSession* sp = &s;
    const DebugViews::Id id = snapId_;
    s.postCore([sp, id, gen] {
        sp->core().opSnapshotArm();
        ViewRequest r;
        r.kind = ViewRequest::Kind::OpSnapshot;
        r.since = (unsigned)gen;
        sp->views().set(id, r);
    });
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
    renderGen_++;
}

bool OpViewModel::absorbable(const ViewBuffer& b) const {
    return waiting_ && b.ok && b.req.kind == ViewRequest::Kind::OpSnapshot && b.req.since == (unsigned)armGen_ &&
           b.opCount >= 0;
}

// Takes the finished snapshot out of its buffer.  Empty (an older core, or the walk found no list) and followLinks:
// the RAM walk runs as a job and the snapshot is committed when it comes back; otherwise at once.
void OpViewModel::absorb(DebugSession& s, ViewBuffer& b) {
    std::vector<OpObject> objs = opDecodeAll(b.objs.data(), b.opCount);
    waiting_ = false;
    if (followLinks && b.opCount == 0) {
        // The core's snapshot is the whole display list (both arms of every branch).  Only when it came back
        // empty, follow the links through DRAM from OLP; those objects are read after the frame (fromRam), so
        // HEIGHT / DATA may already be advanced.
        pendingSnap_.objs = std::move(objs);
        pendingSnap_.count = 0;
        pendingSnap_.halfline = b.halfline;
        const uint64_t gen = ++walkGen_;
        const uint64_t ep = s.views().epoch();
        walk_.submit(s, [gen, ep](const vj_debug_iface* i) {
            WalkOut o;
            o.gen = gen;
            o.epoch = ep;
            uint8_t w[2];
            if (i && i->peek && i->peek(0xF00020, w, 2) == 2) {   // OLP is LO / HI word (op.c OPGetListPointer)
                const unsigned lo = beWord(w, 0);
                if (i->peek(0xF00022, w, 2) == 2)
                    o.objs = opWalkList(&peekReader, const_cast<vj_debug_iface*>(i), lo | (beWord(w, 0) << 16),
                                        (unsigned)VJ_DBG_OP_MAX_OBJECTS);
            }
            return o;
        });
        applyWalk(s);   // inline it has already run
        return;
    }
    commit(std::move(objs), (size_t)b.opCount, b.halfline);
}

bool OpViewModel::applyWalk(DebugSession& s) {
    WalkOut w;
    if (!walk_.take(w)) return false;
    if (w.gen != walkGen_ || w.epoch != s.views().epoch()) return false;   // a newer snapshot, or another core
    for (OpObject& o : w.objs) {
        if (o.warning.empty() && o.isBitmap()) o.warning = "read from RAM after the frame: HEIGHT / DATA may be advanced";
        pendingSnap_.objs.push_back(std::move(o));
    }
    commit(std::move(pendingSnap_.objs), pendingSnap_.count, pendingSnap_.halfline);
    pendingSnap_ = PendingSnap();
    return true;
}

void OpViewModel::commit(std::vector<OpObject> objs, size_t count, unsigned halfline) {
    walkGen_++;   // a fallback walk still on its way belongs to an older snapshot
    objs_ = std::move(objs);
    snapshotCount_ = count;
    halfline_ = halfline;
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
    renderGen_++;
}

// The registers the screen mapping needs ($F00028..$F00047), adopted only for the request, the size and a serial
// not seen yet.
void OpViewModel::adoptGeom(DebugSession& s) {
    ViewBuffer b;
    if (geomId_ == DebugViews::kNoId || !s.views().read(geomId_, b)) return;
    if (b.req.kind != ViewRequest::Kind::Memory || b.req.addr != kGeomAddr || b.req.len != kGeomLen) return;
    if (geomOk_ && b.serial == geomSerial_) return;
    geomSerial_ = b.serial;
    geomOk_ = b.ok && b.bytes.size() == kGeomLen;
    if (geomOk_) std::memcpy(geom_, b.bytes.data(), kGeomLen);
}

OpScreenGeom OpViewModel::readGeom(unsigned frameW, unsigned frameH) const {
    OpScreenGeom g;
    g.frameW = frameW;
    g.frameH = frameH;
    if (!geomOk_) return g;
    g.vmode = beWord(geom_, 0x00);
    g.hdb1 = beWord(geom_, 0x10) & 0x7FF;
    g.vp = beWord(geom_, 0x16) & 0x7FF;
    g.vdb = beWord(geom_, 0x1E) & 0x7FF;
    return g;
}

bool OpViewModel::update(DebugSession& s, bool running) {
    if (!s.enabled()) return false;
    bool changed = false;
    const uint64_t ep = s.views().epoch();
    if (!epochSet_) {
        epochSet_ = true;
        epochSeen_ = ep;
    } else if (ep != epochSeen_) {   // a new core / content: nothing adopted before belongs to it
        epochSeen_ = ep;
        clear();
        changed = true;
    }
    ensureViews(s);
    s.wakeViews();
    adoptGeom(s);

    // The armed snapshot.  Read only while one is awaited (the buffer carries up to a whole display list).
    if (waiting_) {
        ViewBuffer b;
        if (s.views().read(snapId_, b)) {
            if (absorbable(b)) {
                absorb(s, b);
                changed = true;
            } else if (running && b.ok && b.opCount == -1 && b.req.since == (unsigned)armGen_) {
                // Armed and still empty although frames run: ask again.  Changing a field OpSnapshot ignores makes
                // the request pending without moving any serial (the other windows' change highlights and the
                // live re-arm stay as they are); inline the copier answers at once, threaded after the next frame.
                ViewRequest r = b.req;
                r.addr = ++pollGen_;
                snapId_ = s.views().set(snapId_, r);
                s.wakeViews();   // the model stays self-contained (testArmedEmptyAsksAgainOnlyWhileRunning); wakeViews() posts at most one fill
                if (s.views().read(snapId_, b) && absorbable(b)) {
                    absorb(s, b);
                    changed = true;
                }
            }
        }
    }
    if (applyWalk(s)) changed = true;

    bool doArm = false;
    const uint64_t ser = s.refreshSerial();
    if (!seenSerial_ || ser != lastSerial_) {
        const bool first = !seenSerial_;
        seenSerial_ = true;
        lastSerial_ = ser;
        doArm = !waiting_ && ((first && !have_) || live);   // opening the window: take a first snapshot
        imageDirty_ = true;                                  // DRAM may have moved under the preview
    }
    if (applyRender(s)) changed = true;
    if (imageDirty_) {
        startRender(s);
        if (applyRender(s)) changed = true;   // inline it is already there
        changed = true;
    }
    const OpObject* sel = selectedObject();
    if (!cryTried_ && sel && sel->isBitmap()) {
        cryTried_ = true;
        std::vector<uint32_t> t;
        if (s.buildCryTable(t) && t.size() == 0x10000) {
            cry_ = std::move(t);
            cryOk_ = true;
        }
    }
    if (doArm) arm(s);   // last: waiting() is true right after the first update()
    return changed;
}

void OpViewModel::showImage(bool ok, OpImage fresh, const std::string& err) {
    bool differs = ok != haveImage_ || (ok && (fresh.w != img_.w || fresh.h != img_.h || fresh.rgba != img_.rgba ||
                                              fresh.note != img_.note || fresh.mode != img_.mode));
    haveImage_ = ok;
    imgErr_ = ok ? std::string() : err;
    if (ok) img_ = std::move(fresh);
    if (differs) imgSerial_++;
}

void OpViewModel::startRender(DebugSession& s) {
    const OpObject* o = selectedObject();
    if (!o || !o->isBitmap()) {   // nothing to draw: no error either
        imageDirty_ = false;
        renderGen_++;             // a render still on its way is for another object
        showImage(false, OpImage(), std::string());
        return;
    }
    if (render_.pending()) return;   // one at a time; the object stays dirty and goes out when the last has run
    imageDirty_ = false;
    const uint64_t gen = ++renderGen_;
    const uint64_t ep = s.views().epoch();
    const OpObject obj = *o;
    const OpRenderOptions opt = render;
    render_.submit(s, [obj, opt, gen, ep](const vj_debug_iface* i) {
        RenderOut r;
        r.gen = gen;
        r.epoch = ep;
        r.ok = opRender(i, obj, opt, r.img, r.err);
        return r;
    });
}

bool OpViewModel::applyRender(DebugSession& s) {
    RenderOut r;
    if (!render_.take(r)) return false;
    if (r.gen != renderGen_ || r.epoch != s.views().epoch()) return false;   // another object, or another core
    showImage(r.ok, std::move(r.img), r.err);
    return true;
}

bool OpViewModel::paint(DebugSession& s, int x, int y, uint32_t value, std::string& status) {
    const OpObject* o = selectedObject();
    if (!o || !haveImage_) { status = "select a drawable bitmap first"; return false; }
    // One trip to the emulation thread: read the byte(s) the pixel shares, check the target is RAM, write.
    OpPixelPatch p;
    enum { NotRun, NoPixel, NotRam, Wrote } stage = NotRun;
    int rc = 0;
    s.callCore([&] {
        if (!opPixelPatch(s.core().iface(), *o, img_, x, y, value, p)) { stage = NoPixel; return; }
        // The painter writes RAM only, whatever the memory viewer's register-write switches say.
        bool ram = false;
        for (const vj_debug_region& r : s.regions())
            if (p.addr >= r.start && p.addr + p.n <= r.start + r.len) {
                ram = (r.flags & VJ_DBG_REGION_RAM) && !(r.flags & (VJ_DBG_REGION_REG | VJ_DBG_REGION_ROM | VJ_DBG_REGION_BLOCKED));
                break;
            }
        if (!ram) { stage = NotRam; return; }
        rc = s.poke(p.addr, p.bytes, p.n);
        stage = Wrote;
    });
    if (stage == NotRun) { status = "not written: the core did not answer"; return false; }
    if (stage == NoPixel) { status = "that pixel cannot be read"; return false; }
    if (stage == NotRam) {
        status = "not written: the painter only writes RAM";
        return false;
    }
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
    renderGen_++;         // a render posted before the poke shows the old pixels: drop it
    startRender(s);
    applyRender(s);       // inline the new pixels are already there
    return true;
}

}  // namespace vj
