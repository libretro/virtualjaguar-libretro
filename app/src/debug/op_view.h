// The Object list window's model: the snapshot lifecycle (arm -> wait for the walk -> decode), the
// selection, the rendered preview and the pixel painter.  No ImGui and no GL, so it is tested against
// the mock core interface (app/tests/debug_op_test.cpp); op_ui.cpp is the thin ImGui layer on top.
//
// The model never calls the core's interface from the UI thread: the snapshot and the screen registers are
// published view buffers (DebugViews), the arm, the preview render and the fallback list walk run on the emulation
// thread (DebugSession::postCore / CoreJob), and the painter is one callCore.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core_job.h"
#include "debug_session.h"
#include "debug_views.h"
#include "op_decode.h"
#include "op_render.h"

namespace vj {

// How the window's "Step a frame" runs a frame so a pending snapshot can finish while paused.
enum class OpStep { None, Inline, Advance };
struct OpStepGate {
    bool threaded, paused, halted, attached, inFrame, loaded, netplay;
};
// Advance: threaded && paused && !halted && loaded && !netplay (advance() gives up when a halt begins, so an attached
// session no longer blocks it).  Inline: !threaded && paused && !attached && !halted && !inFrame && loaded && !netplay
// (a frame run from inside the draw hook that then halts would start a second ImGui frame).  None otherwise.
OpStep opStepFor(const OpStepGate& g);

class OpViewModel {
public:
    // ---- settings (the UI edits these) -------------------------------------------------------
    bool live = true;                 // re-arm the snapshot at the session's refresh cadence
    // The core's snapshot is the whole display list at the top of the next field (both arms of every branch).
    // Fallback only: when it comes back empty, walk the list from OLP through DRAM instead (objects marked
    // fromRam: their HEIGHT / DATA are read after the frame and may be advanced).
    bool followLinks = true;
    OpRenderOptions render;

    // ---- snapshot lifecycle ----------------------------------------------------------------
    // Arms a one-shot capture (posted to the emulation thread); the objects arrive after the emulation completes a
    // display-list walk (the next frame).  Paused emulation: stays waiting() until a frame runs.
    void arm(DebugSession& s);
    // Once per UI frame.  Reads the published snapshot / register buffers, and on a refresh tick (session serial)
    // re-arms (live) and re-renders the selected object from DRAM.  `running`: frames are being emulated, so an armed
    // snapshot that is still empty is worth asking the copier about again.  True when objects or the preview changed.
    bool update(DebugSession& s, bool running = true);
    bool waiting() const { return waiting_; }
    bool haveSnapshot() const { return have_; }
    unsigned halfline() const { return halfline_; }
    size_t snapshotCount() const { return snapshotCount_; }   // the objects the core's snapshot held (0 when the fromRam fallback is showing)
    unsigned armCount() const { return arms_; }
    const std::vector<OpObject>& objects() const { return objs_; }
    void clear();                     // drops everything (content closed, tools forced shut)
    // Shows a hand-built list instead of a captured one (tests; the next absorbed snapshot replaces it).
    void setObjects(std::vector<OpObject> objs);

    // ---- selection / preview -----------------------------------------------------------------------
    int selected() const { return sel_; }
    void select(int idx);
    // Installs a synthetic bitmap bound to this session/content epoch, without arming a snapshot.
    void showPreview(DebugSession& s, const OpObject& obj, const OpRenderOptions& opt);
    const OpObject* selectedObject() const { return sel_ >= 0 && sel_ < (int)objs_.size() ? &objs_[(size_t)sel_] : nullptr; }
    // The selected bitmap rendered, or null (not a bitmap / not drawable: see imageError()).
    const OpImage* image() const { return haveImage_ ? &img_ : nullptr; }
    const std::string& imageError() const { return imgErr_; }
    uint64_t imageSerial() const { return imgSerial_; }      // bumps whenever the pixels change
    void invalidateImage() { imageDirty_ = true; renderGen_++; }   // after the UI changed a render option
    OpColorMode resolvedMode() const { return haveImage_ ? img_.mode : OpColorMode::Cry; }

    // ---- editing --------------------------------------------------------------------------------------
    // Stores `value` into preview pixel (x, y) through DebugSession::poke (RAM only: the policy and the
    // read-only cartridge refusal come from there).  True on success; `status` says what happened either way.
    bool paint(DebugSession& s, int x, int y, uint32_t value, std::string& status);
    struct Edit {
        unsigned addr = 0;
        unsigned n = 0;
        uint8_t bytes[4] = {0, 0, 0, 0};
    };
    bool hasLastEdit() const { return haveEdit_; }
    const Edit& lastEdit() const { return edit_; }

    // ---- screen mapping ---------------------------------------------------------------------------------
    // TOM registers HDB1 / VDB / VP / VMODE from the last adopted $F00028 view buffer, with the framebuffer size
    // filled in by the caller.  Default geometry until update() has adopted one.
    OpScreenGeom readGeom(unsigned frameW, unsigned frameH) const;

    // ---- views / colour ---------------------------------------------------------------------------------------
    void releaseViews(DebugSession& s);   // the window closed or the tools are off: drop both requests and what was adopted
    // The 64K CRY -> 0x00RRGGBB table for the paint controls, or null until update() has built it (one callCore per
    // epoch, the first time a bitmap is selected).
    const std::vector<uint32_t>* cryTable() const { return cryOk_ ? &cry_ : nullptr; }

private:
    struct RenderOut {
        uint64_t gen = 0, epoch = 0;
        bool ok = false;
        OpImage img;
        std::string err;
    };
    struct WalkOut {
        uint64_t gen = 0, epoch = 0;
        std::vector<OpObject> objs;
    };
    struct PendingSnap {   // a decoded, empty snapshot waiting for the fallback RAM walk
        std::vector<OpObject> objs;
        size_t count = 0;
        unsigned halfline = 0;
    };
    static constexpr unsigned kGeomAddr = 0xF00028, kGeomLen = 0x20;

    void ensureViews(DebugSession& s);
    bool absorbable(const ViewBuffer& b) const;
    void absorb(DebugSession& s, ViewBuffer& b);
    void commit(std::vector<OpObject> objs, size_t count, unsigned halfline);
    bool applyWalk(DebugSession& s);
    void adoptGeom(DebugSession& s);
    void startRender(DebugSession& s);
    bool applyRender(DebugSession& s);
    void showImage(bool ok, OpImage fresh, const std::string& err);

    std::vector<OpObject> objs_;
    bool waiting_ = false, have_ = false;
    unsigned halfline_ = 0, arms_ = 0;
    size_t snapshotCount_ = 0;
    int sel_ = -1;
    bool autoSelected_ = false;
    unsigned selAddr_ = 0;
    uint64_t lastSerial_ = 0;
    bool seenSerial_ = false;
    bool imageDirty_ = true;
    DebugViews::Id snapId_ = DebugViews::kNoId, geomId_ = DebugViews::kNoId;
    uint64_t armGen_ = 0;   // bumps per arm; the snapshot request's `since` carries it, so only the answer to the newest arm is taken
    unsigned pollGen_ = 0;
    bool epochSet_ = false;
    uint64_t epochSeen_ = 0;
    CoreJob<RenderOut> render_;
    uint64_t renderGen_ = 0;
    CoreJob<WalkOut> walk_;
    uint64_t walkGen_ = 0;
    PendingSnap pendingSnap_;
    std::vector<uint32_t> cry_;
    bool cryOk_ = false, cryTried_ = false;
    uint8_t geom_[kGeomLen] = {0};
    bool geomOk_ = false;
    uint64_t geomSerial_ = 0;
    OpImage img_;
    bool haveImage_ = false;
    std::string imgErr_;
    uint64_t imgSerial_ = 0;
    Edit edit_;
    bool haveEdit_ = false;
};

}  // namespace vj
