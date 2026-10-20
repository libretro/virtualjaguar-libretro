// Fullscreen menu policy (#1028). No window-system calls: native fullscreen events and F11 share it.
#pragma once
namespace vj { namespace ui {
class FullscreenMenu {
public:
    void update(bool fullscreen, bool autoHide, bool& pinned) {
        const bool active = fullscreen && autoHide;
        if (active == active_) return;
        if (active) { savedPin_ = pinned; pinned = false; }
        else pinned = savedPin_;
        active_ = active;
        revealExtra_ = false;
    }
    void togglePin(bool& pinned) { pinned = !pinned; if (active_) revealExtra_ = true; }
    bool visible(bool native, bool showExtra, bool fullscreen, bool tvBase, bool pinned, bool hot) const {
        if (native) return active_ ? ((showExtra || revealExtra_) && (pinned || hot)) : showExtra;
        return !(fullscreen || tvBase) || pinned || hot;
    }
private:
    bool active_ = false, savedPin_ = true, revealExtra_ = false;
};
} }
