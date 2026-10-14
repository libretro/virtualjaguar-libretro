// The Object list window's model: the snapshot lifecycle (arm -> wait for the walk -> decode), the
// selection, the rendered preview and the pixel painter.  No ImGui and no GL, so it is tested against
// the mock core interface (app/tests/debug_op_test.cpp); op_ui.cpp is the thin ImGui layer on top.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "debug_session.h"
#include "op_decode.h"
#include "op_render.h"

namespace vj {

class OpViewModel {
public:
    // ---- settings (the UI edits these) -------------------------------------------------------
    bool live = true;                 // re-arm the snapshot at the session's refresh cadence
    // The core's snapshot is the first OP walk after arming -- an early half-line, so a list gated by
    // branch objects shows only the part above the first gate.  With this on, the rest of the list is
    // found by following the links through DRAM (marked fromRam: those values are read after the frame).
    bool followLinks = true;
    OpRenderOptions render;

    // ---- snapshot lifecycle ----------------------------------------------------------------
    // Arms a one-shot capture; the objects arrive after the emulation completes a display-list walk
    // (the next frame).  Paused emulation: stays waiting() until a frame runs.
    void arm(DebugSession& s);
    // Once per UI frame.  Polls a pending capture, and on a refresh tick (session serial) re-arms (live)
    // and re-renders the selected object from DRAM.  True when objects or the preview changed.
    bool update(DebugSession& s);
    bool waiting() const { return waiting_; }
    bool haveSnapshot() const { return have_; }
    unsigned halfline() const { return halfline_; }
    size_t snapshotCount() const { return snapshotCount_; }   // objects[0..n) are the core's snapshot, the rest fromRam
    unsigned armCount() const { return arms_; }
    const std::vector<OpObject>& objects() const { return objs_; }
    void clear();                     // drops everything (content closed, tools forced shut)

    // ---- selection / preview -----------------------------------------------------------------------
    int selected() const { return sel_; }
    void select(int idx);
    const OpObject* selectedObject() const { return sel_ >= 0 && sel_ < (int)objs_.size() ? &objs_[(size_t)sel_] : nullptr; }
    // The selected bitmap rendered, or null (not a bitmap / not drawable: see imageError()).
    const OpImage* image() const { return haveImage_ ? &img_ : nullptr; }
    const std::string& imageError() const { return imgErr_; }
    uint64_t imageSerial() const { return imgSerial_; }      // bumps whenever the pixels change
    void invalidateImage() { imageDirty_ = true; }           // after the UI changed a render option
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
    // TOM registers HDB1 / VDB / VP / VMODE (peeks), with the framebuffer size filled in by the caller.
    OpScreenGeom readGeom(const DebugSession& s, unsigned frameW, unsigned frameH) const;

private:
    void rerender(DebugSession& s);
    void absorb(DebugSession& s);

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
    OpImage img_;
    bool haveImage_ = false;
    std::string imgErr_;
    uint64_t imgSerial_ = 0;
    Edit edit_;
    bool haveEdit_ = false;
};

}  // namespace vj
