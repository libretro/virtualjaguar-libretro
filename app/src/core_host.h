// The libretro host: everything the app needs to run the (statically linked)
// core through the standard retro_* API, and nothing about windows, GL or SDL
// audio.  It implements the environment calls libretro.c makes, owns the core
// option table (definitions, categories, user values, visibility), converts the
// video callback into a tight XRGB8888 frame, hands audio to a sink, and
// persists SRAM / save states.
//
// Reference: test/harness/harness.c is the headless dlopen frontend this
// follows; the patterns (deep-copying option pushes, SET_CORE_OPTIONS_DISPLAY
// reset-on-push, opt-in answers) are the same, but this host links statically
// and answers the way RetroArch does.
//
// The core keeps its state in globals, so there is exactly one host; it is a
// singleton.  Inline, every method must be called from one thread; with an owner thread (setOwner, #967) see the
// threading block below for what the other thread may call.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "libretro.h"
#include "frame.h"
#include "vj_frontend_ext.h"   // the core's private event channel
#include "vj_debug_iface.h"     // ...and its debug interface (docs/app-debug-tools.md)

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

struct ControllerType {
    std::string desc;
    unsigned id = 0;
};

struct DiscInfo {
    bool available = false;
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

// The per-frame state the emulation thread publishes for the UI thread (#967, design section 3).  Phase 1 holds what
// the UI reads every frame; phase 2 adds events, achievements and debug views and moves it to its own file.
struct EmuSnapshot {
    std::shared_ptr<const OptionTable> options;   // replaced only when the table changed
    unsigned optionPushes = 0;
    double fps = 0, sampleRate = 0;
    float aspect = 4.0f / 3.0f;
    DiscInfo disc;
};

class CoreHost {
public:
    static CoreHost& instance();

    // ---- configuration (set before the first load) -----------------------
    void setDirectories(const std::string& systemDir, const std::string& saveDir);
    // Where a game's .srm is read from when saveDir has none yet (read-only; instance.h).
    void setSaveFallbackDir(const std::string& dir) { saveFallbackDir_ = dir; }
    void setLogVerbose(bool v) { logVerbose_ = v; }
    // Every line the core logs, at any level (even those --verbose would hide), already
    // formatted and without the trailing newline.  Called on the emulation thread.
    std::function<void(int level, const char* text)> onLog;
    // Called for every audio batch the core emits (interleaved L/R int16).
    std::function<void(const int16_t*, size_t frames)> onAudio;
    // Per-frame input.  Port/device/index/id exactly as retro_input_state_t.
    std::function<int16_t(unsigned, unsigned, unsigned, unsigned)> inputState;
    std::function<void()> inputPoll;
    // ---- the core's private event channel (src/core/vj_frontend_ext.h, docs/app-frontend-extension.md) ----
    // The core says HELLO once per retro_init; this host answers (unless announceExtensions is off, which
    // makes it behave like RetroArch: no events are ever sent and consumers fall back to log scraping).
    // Events arrive synchronously inside runFrame(), on the emulation thread.
    bool announceExtensions = true;
    unsigned crashEvents() const { return crashEvents_.load(); }   // crash-watchdog events since the content loaded
    std::function<void(const vj_event&)> onEvent;   // string pointers are valid for the call only
    // True once the core's HELLO was answered in the current session: the events are then authoritative
    // and the matching log lines need no parsing.
    bool eventsActive() const { return helloSeen_; }
    uint64_t eventCount() const { return eventsSeen_; }

    // ---- BIOS selection (VJ_ENV_GET_BIOS_PATH / VJ_ENV_GET_BOOT_TARGET, vj_frontend_ext.h) ----
    // Which BIOS FILE the core uses for a role (VJ_BIOS_CART_BOOT / VJ_BIOS_CD), and what a no-content boot
    // enters.  An empty path is "no preference": the core searches its system folder, then uses its built-in
    // image.  The core validates the file (size, CD header / checksum) and falls back the same way if it
    // fails; staged() says what it ended up running.  Takes effect at the next load.
    void setBiosPath(unsigned role, const std::string& path) {
        if (role >= 1 && role <= 2) biosPath_[role] = path;
    }
    const std::string& biosPath(unsigned role) const { return biosPath_[role >= 1 && role <= 2 ? role : 0]; }
    void setBootTarget(unsigned target) { bootTarget_ = target; }   // VJ_BOOT_CD_BIOS / VJ_BOOT_CART_BOOT_ROM
    unsigned bootTarget() const { return bootTarget_; }
    // Off: the HELLO answer leaves VJ_CAP_BIOS out, as RetroArch would (the core never asks).
    bool announceBios = true;
    // What the core reported staging for a role in the current session (VJ_EVT_BIOS_STAGED / _REJECTED).
    struct StagedBios {
        bool seen = false;
        uint32_t crc = 0;
        unsigned source = 0;       // VJ_BIOS_SRC_*
        std::string where;         // the file path, or the built-in image's name
        bool rejected = false;     // a file the app named was refused (VJ_EVT_BIOS_REJECTED)
        std::string rejectedPath;
    };
    const StagedBios& stagedBios(unsigned role) const { return staged_[role >= 1 && role <= 2 ? role : 0]; }

    // ---- debug tools (VJ_CAP_DEBUG / VJ_ENV_SET_DEBUG_IFACE, vj_debug_iface.h) ----
    // The core hands over its debug interface once per session, right after the HELLO.  Off: the HELLO
    // answer leaves VJ_CAP_DEBUG out, as RetroArch would (the core never sends it).
    bool announceDebug = true;
    // The interface of the current session, or null (before the first load, after an unload, or when the
    // core never sent it).  Valid until the next unload; every call through it is made on this thread.
    const vj_debug_iface* debugIface() const { return debugIface_; }
    // Called when the interface arrives (non-null) and just before it goes away on an unload (null).
    std::function<void(const vj_debug_iface*)> onDebugIface;
    // True while the core is inside retro_run().  A debugger halt parks the core there and runs the UI,
    // so anything that would re-enter the core (a state load, a reset, another load, an option change)
    // is refused while this is set.
    bool inFrame() const {
        noteOffOwner("inFrame");   // the UI asks DebugSession::halted() instead (App::haltedNow)
        return inFrame_;
    }

    // Audio buffer occupancy for the core's frameskip / auto-enhancement logic.
    std::function<void(bool& active, unsigned& occupancy, bool& underrun)> audioBufferStatus;
    // What the host wants from GET_AUDIO_VIDEO_ENABLE this frame.
    bool wantVideo = true;

    // ---- content ----------------------------------------------------------
    // Full cycle: unload any previous game, retro_deinit, set_environment,
    // retro_init, retro_load_game, then load SRAM.  Safe to call repeatedly.
    bool loadContent(const std::string& path, std::string& err);
    // Boot the console with an empty cartridge slot (SET_SUPPORT_NO_GAME).
    bool loadNoContent(std::string& err);
    void unloadContent();
    bool isLoaded() const { return loaded_; }
    const std::string& contentPath() const { return contentPath_; }
    const std::string& contentName() const { return contentName_; }  // file stem; "" with no content

    void runFrame();
    void reset();

    // ---- AV ---------------------------------------------------------------
    double fps() const { return onOwner() ? avInfo_.timing.fps : ui_.fps; }
    double sampleRate() const { return onOwner() ? avInfo_.timing.sample_rate : ui_.sampleRate; }
    float aspect() const { return onOwner() ? aspectLive() : ui_.aspect; }
    // The core's last frame.  Emulation thread only when threaded: the UI reads frames from FrameExchange.
    const Frame& frame() const {
        noteOffOwner("frame");
        return frame_;
    }
    uint64_t frameCount() const { return frameCount_.load(); }
    const char* coreName() const { return sysInfo_.library_name ? sysInfo_.library_name : ""; }
    const char* coreVersion() const { return sysInfo_.library_version ? sysInfo_.library_version : ""; }
    // The core's valid_extensions list ("j64|jag|..."); safe before any load.
    static std::string validExtensions();
    unsigned optionPushes() const { return onOwner() ? optionPushes_ : ui_.optionPushes; }
    unsigned minAudioLatencyMs() const { return minAudioLatencyMs_.load(); }

    // ---- keyboard device presence (libretro SET_KEYBOARD_CALLBACK) --------
    // After every load the host announces a keyboard (see finishLoad); the input
    // end-to-end test switches this off to prove the keypad needs the announcement.
    bool announceKeyboard = true;
    void sendKeyEvent(bool down, unsigned retroKey, uint32_t character, uint16_t mods);

    // ---- options ----------------------------------------------------------
    const std::vector<OptionCategory>& optionCategories() const { return onOwner() ? categories_ : uiTable().categories; }
    const std::vector<OptionDef>& optionDefs() const { return onOwner() ? defs_ : uiTable().defs; }
    bool optionVisible(const std::string& key) const;
    // Options the app owns (its Controls tab already does what they do): when this rule says a
    // key is locked, its value is forced -- over saved and --option values alike -- and the
    // option is hidden from the options UI.  An empty `value` means "the option's default".
    // Set before the first load; evaluated when the core pushes its definitions.
    std::function<bool(const std::string& key, std::string& value)> optionLock;
    bool optionLocked(const std::string& key) const { return (onOwner() ? locked_ : uiTable().locked).count(key) != 0; }
    // A session override (overrideOption(): --option, or the Netplay window) is in force and decides what the
    // core reads, so a value picked in Settings is saved but does not show until the override ends.
    bool optionOverridden(const std::string& key) const { return (onOwner() ? sessionValues_ : uiSession_).count(key) != 0; }
    std::string optionValue(const OptionDef& d) const;  // user value if valid, else default
    // The value the core will read for `key`, without needing the definitions (so it works before the first load):
    // a session override, else the saved value, else `fallback`.  Strict lists only: the caller names a value it knows.
    std::string optionValueFor(const std::string& key, const std::string& fallback) const {
        const std::map<std::string, std::string>& session = onOwner() ? sessionValues_ : uiSession_;
        const std::map<std::string, std::string>& user = onOwner() ? userValues_ : uiUser_;
        auto s = session.find(key);
        if (s != session.end()) return s->second;
        auto u = user.find(key);
        return u != user.end() ? u->second : fallback;
    }
    // Set by the UI.  Marks the variable-update flag and runs the core's
    // update-display callback (as RetroArch does after a menu change).
    void setOptionValue(const std::string& key, const std::string& value);
    void resetOptionToDefault(const std::string& key);
    // Persistent overrides (loaded from / saved to the app config).
    const std::map<std::string, std::string>& userOptions() const { return onOwner() ? userValues_ : uiUser_; }
    void loadUserOptions(const std::vector<std::pair<std::string, std::string>>& kv);
    std::function<void(const std::string& key, const std::string& value, bool erased)> onOptionChanged;
    // Applied without being persisted: e.g. the smoke test forces a value.
    void overrideOption(const std::string& key, const std::string& value);
    // Drops an overrideOption() value (the saved value, or the default, applies again).
    void clearOverride(const std::string& key);
    // Overrides naming an option the core never registered, or a value outside
    // its list, are otherwise silently ignored (the run would measure the
    // default).  Valid only after the definitions were pushed (i.e. after a load).
    // `sessionOnly` = the --option overrides; false = the saved config.ini ones.
    std::vector<std::string> checkOverrides(bool sessionOnly) const;
    unsigned visibleOptionCount() const;

    // ---- memory (achievements) --------------------------------------------
    // The map the core published with SET_MEMORY_MAPS (valid while content is
    // loaded); null/empty before the first load.
    const retro_memory_map* memoryMap() const { return memMap_.num_descriptors ? &memMap_ : nullptr; }
    // retro_get_memory_data/size for RETRO_MEMORY_*; false when the region is absent.
    bool memoryRegion(unsigned id, uint8_t*& data, size_t& size) const;

    // ---- messages ---------------------------------------------------------
    std::vector<OsdMessage> takeMessages();
    void pushMessage(const std::string& text, uint32_t ms = 3000, int level = RETRO_LOG_INFO);

    // ---- save states ------------------------------------------------------
    bool saveStateToFile(const std::string& path, std::string& err);
    bool loadStateFromFile(const std::string& path, std::string& err);

    // ---- SRAM / EEPROM / Memory Track ------------------------------------
    // Flushes the core's save memory to <saveDir>/<name>.srm if it changed.
    bool flushSaveRam(bool force = false);
    std::string saveRamPath() const;

    // ---- controllers / discs ---------------------------------------------
    const std::vector<std::vector<ControllerType>>& controllerPorts() const { return controllerPorts_; }
    void setControllerDevice(unsigned port, unsigned device);
    DiscInfo discInfo();
    void discSetEject(bool eject);
    void discSetIndex(unsigned idx);

    // ---- threading (#967, docs/app-emulation-thread-design.md) -------------------------------------
    // Inline (no owner, the default): every method runs on its caller's thread, exactly as before.
    // Threaded: setOwner() names the emulation thread, the only thread that calls into the core.  From any other
    // thread (the UI):
    //   * the option, AV and disc readers answer from the snapshot pinned by pinSnapshot() (the emulation thread
    //     publishSnapshot()s after each frame and each command batch);
    //   * option values are mirrored: a write updates the UI's copy and calls onOptionChanged (config.ini) right
    //     there, and reaches the core's copy through `post` at the next frame boundary.  The UI's copy
    //     (uiUser_ / uiSession_) is written only on the UI thread, plus -- inside an EmuThread::call(), while the
    //     UI thread waits -- by the owner-side branch of the same method;
    //   * setControllerDevice, discSetEject / discSetIndex and sendKeyEvent are queued through `post`;
    //   * the fields only load / unload write (isLoaded, contentPath / contentName, stagedBios, controllerPorts,
    //     coreName / coreVersion, eventsActive, debugIface) are read directly: they change only inside an
    //     EmuThread::call(), while the UI thread waits;
    //   * anything else (runFrame, loads, states, SRAM, memory, frame(), inFrame()) is a bug: it is counted in
    //     ownerViolations() and logged once per method, and the calls into the core are refused.
    void setOwner(std::thread::id owner);
    bool threaded() const { return owner_.load() != std::thread::id(); }
    bool onOwner() const {
        const std::thread::id o = owner_.load();
        return o == std::thread::id() || o == std::this_thread::get_id();
    }
    std::function<void(std::function<void()>)> post;   // the emulation thread's queue (App: EmuThread::post)
    void publishSnapshot();   // emulation thread: after each frame and each command batch
    void pinSnapshot();       // UI thread, once per UI iteration; what the readers return stays valid until the next pin
    unsigned ownerViolations() const { return ownerViolations_.load(); }

private:
    CoreHost() = default;
    CoreHost(const CoreHost&) = delete;

    // C callbacks
    static bool envCb(unsigned cmd, void* data);
    static void videoCb(const void* data, unsigned w, unsigned h, size_t pitch);
    static void audioSampleCb(int16_t l, int16_t r);
    static size_t audioBatchCb(const int16_t* data, size_t frames);
    static void inputPollCb();
    static int16_t inputStateCb(unsigned port, unsigned device, unsigned index, unsigned id);

    bool env(unsigned cmd, void* data);
    void traceOption(const std::string& key, const char* value, const char* why);
    void captureOptions(const retro_core_options_v2* o);
    bool startSession(std::string& err);
    bool finishLoad(const std::string& label, std::string& err);
    void stopSession();
    void loadSaveRam();

    std::string systemDir_, saveDir_, saveFallbackDir_;
    std::string saveFallbackPath() const;
    bool logVerbose_ = false;

    std::string biosPath_[3];   // by role (index 0 unused)
    unsigned bootTarget_ = 0;
    StagedBios staged_[3];
    bool helloSeen_ = false;    // the core's VJ_ENV_HELLO was answered this session
    const vj_debug_iface* debugIface_ = nullptr;
    bool inFrame_ = false;      // inside retro_run()
    uint64_t eventsSeen_ = 0;
    bool initialized_ = false;  // retro_init has run (and retro_deinit has not)
    bool loaded_ = false;
    std::string contentPath_, contentName_;
    std::vector<unsigned char> contentData_;  // kept alive until unload

    retro_system_info sysInfo_{};
    retro_system_av_info avInfo_{};
    Frame frame_;
    std::atomic<uint64_t> frameCount_{0};
    uint64_t serial_ = 0;

    // options
    std::vector<OptionCategory> categories_;
    std::vector<OptionDef> defs_;
    unsigned optionPushes_ = 0;
    std::map<std::string, std::string> locked_;         // optionLock results, by key
    std::map<std::string, std::string> userValues_;     // persisted overrides
    std::map<std::string, std::string> sessionValues_;  // non-persisted overrides
    std::map<std::string, bool> visibility_;
    std::map<std::string, std::string> optionTrace_;
    bool variableUpdate_ = false;
    retro_core_options_update_display_callback_t updateDisplayCb_ = nullptr;

    // misc env state
    std::vector<OsdMessage> messages_;
    std::atomic<unsigned> crashEvents_{0};
    std::string contentOverrideExts_;  // "cue|cdi|chd" (need_fullpath override)
    bool supportsNoGame_ = false;
    std::atomic<unsigned> minAudioLatencyMs_{0};
    retro_keyboard_event_t keyboardCb_ = nullptr;
    std::vector<std::vector<ControllerType>> controllerPorts_;
    retro_disk_control_ext_callback disk_{};
    bool diskRegistered_ = false;

    // SET_MEMORY_MAPS copy
    std::vector<retro_memory_descriptor> memDescs_;
    retro_memory_map memMap_{};

    // SRAM bookkeeping
    uint32_t lastSaveHash_ = 0;
    bool saveHashValid_ = false;

    // threading (#967)
    float aspectLive() const;
    DiscInfo discInfoLive();
    const OptionTable& uiTable() const;
    bool noteOffOwner(const char* what) const;   // true (counted, logged once) when called off the owner
    void queue(std::function<void()> fn);         // through `post`, or now when there is none
    void applyUserValue(const std::string& key, const std::string* value);   // null = erase; the core's copy only
    void applyOverride(const std::string& key, const std::string* value);    // null = clear; the core's copy only
    std::atomic<std::thread::id> owner_{std::thread::id()};
    mutable std::atomic<unsigned> ownerViolations_{0};
    mutable std::mutex violationMu_;
    mutable std::set<std::string> violationSeen_;
    std::mutex snapMu_;
    EmuSnapshot pub_;   // under snapMu_: the emulation thread's latest
    EmuSnapshot ui_;    // the UI thread's pin
    std::map<std::string, std::string> uiUser_, uiSession_;   // threaded: the UI's option values
    unsigned optSerial_ = 1, pubOptSerial_ = 0;   // bumped on every change to the option table
    bool discDirty_ = true;
    std::mutex msgMu_;   // messages_
};

}  // namespace vj
