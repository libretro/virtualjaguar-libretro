// The state the emulation thread publishes for the UI thread (#967, docs/app-emulation-thread-design.md section 3):
// the per-frame snapshot CoreHost pins once per UI iteration, and the core-event stream -- what the core said inside
// retro_run() and the option writes made on the emulation thread -- which CoreHost::pinSnapshot() applies on the UI
// thread, in order.
#pragma once

#include "libretro.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace vj {

struct OptionValue {
    std::string value;
    std::string label;
};

struct OptionDef {
    std::string key;
    std::string desc;            // flat-menu title
    std::string descCategorized; // title when shown under a category (may be empty)
    std::string info;
    std::string infoCategorized;
    std::string categoryKey;
    std::string defaultValue;
    std::vector<OptionValue> values;
};

struct OptionCategory {
    std::string key;
    std::string desc;
    std::string info;
};

struct OsdMessage {
    std::string text;
    uint32_t durationMs = 3000;
    int level = RETRO_LOG_INFO;
};

struct DiscInfo {
    bool available = false;   // a CD session: CD content, or the CD BIOS with no disc (count is then 0)
    bool ejected = false;
    unsigned count = 0;
    unsigned index = 0;
    std::vector<std::string> labels;
};

// What SET_CORE_OPTIONS_V2 / _DISPLAY last said, plus the app's locks: the part of the option state the core writes.
struct OptionTable {
    std::vector<OptionCategory> categories;
    std::vector<OptionDef> defs;
    std::map<std::string, bool> visibility;
    std::map<std::string, std::string> locked;
};

// The per-frame state the emulation thread publishes for the UI thread (CoreHost::publishSnapshot / pinSnapshot).
struct EmuSnapshot {
    std::shared_ptr<const OptionTable> options;   // replaced only when the table changed
    unsigned optionPushes = 0;
    double fps = 0, sampleRate = 0;
    float aspect = 4.0f / 3.0f;
    DiscInfo disc;
    std::string contentPath, contentName;   // the disc in the tray (CoreHost::contentPath); with `disc`
    unsigned discSwaps = 0;
};

// One thing the core said, or one option write made on the emulation thread, on its way to the UI thread.
struct CoreEvent {
    enum class Kind : uint8_t {
        Log,             // a line the core logged: level, text
        Event,           // a vj_event (vj_frontend_ext.h): type, a..d, text = its string (copied; the core's is call-scoped)
        UserOption,      // a saved option value written on the owner: text = key, value, erased = reset to default
        SessionOption,   // a session override set or cleared on the owner: text = key, value, erased = cleared
    };
    Kind kind = Kind::Log;
    uint32_t epoch = 0;              // CoreHost::eventEpochBump() in force on the emulation thread when it was queued
    int level = RETRO_LOG_INFO;      // Log
    unsigned type = 0, a = 0, b = 0, c = 0, d = 0;   // Event
    std::string text;
    std::string value;
    bool erased = false;
};

// Multi-producer, single-consumer, in order.  Log lines are bounded (the oldest go first); events and option writes
// are never dropped (a lost LINK_UP fails a netplay session, a lost option write loses the user's choice).
class CoreEventQueue {
public:
    static constexpr size_t kMaxLogs = 512;

    void push(CoreEvent e) {
        std::lock_guard<std::mutex> l(mu_);
        if (e.kind == CoreEvent::Kind::Log && logs_ >= kMaxLogs) {
            for (auto it = q_.begin(); it != q_.end(); ++it) {
                if (it->kind == CoreEvent::Kind::Log) {
                    q_.erase(it);
                    logs_--;
                    dropped_++;
                    break;
                }
            }
        }
        if (e.kind == CoreEvent::Kind::Log) logs_++;
        q_.push_back(std::move(e));
    }
    std::vector<CoreEvent> take() {
        std::lock_guard<std::mutex> l(mu_);
        std::vector<CoreEvent> out(std::make_move_iterator(q_.begin()), std::make_move_iterator(q_.end()));
        q_.clear();
        logs_ = 0;
        return out;
    }
    // A content switch: what the previous game said is stale; the user's option writes are not.
    void dropLogsAndEvents() {
        std::lock_guard<std::mutex> l(mu_);
        std::deque<CoreEvent> keep;
        for (CoreEvent& e : q_)
            if (e.kind == CoreEvent::Kind::UserOption || e.kind == CoreEvent::Kind::SessionOption) keep.push_back(std::move(e));
        q_.swap(keep);
        logs_ = 0;
    }
    size_t droppedLogs() const {
        std::lock_guard<std::mutex> l(mu_);
        return dropped_;
    }

private:
    mutable std::mutex mu_;
    std::deque<CoreEvent> q_;
    size_t logs_ = 0, dropped_ = 0;
};

}  // namespace vj
