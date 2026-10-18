// CoreHost with an owner thread and the REAL core (#967): what the UI thread may read and write while the emulation
// thread owns the core, and what it may not.
//
// usage: vj_core_host_thread_test <rom.j64>
#include "core_host.h"
#include "emu_thread.h"

#include "libretro.h"

extern "C" {
#include "joystick.h"
}

#include <atomic>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace vj;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        g_checks++;                                                              \
        if (!(cond)) {                                                           \
            g_fail++;                                                            \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

namespace {

struct HostClient : EmuThread::Client {
    CoreHost& host = CoreHost::instance();
    void threadStarted() override { host.setOwner(std::this_thread::get_id()); }
    void threadStopping() override { host.setOwner(std::thread::id()); }
    bool canRun() override { return host.isLoaded(); }
    double framePeriod() override { return 1.0 / 60.0; }
    double audioQueuedMs() override { return -1.0; }
    void runFrame() override {
        host.runFrame();
        host.publishSnapshot();
    }
    std::atomic<bool> quiet{false};   // true: the batch-end publish is off, so only the host's own publishes count
    void afterCommands() override {
        if (!quiet.load()) host.publishSnapshot();
    }
};

const OptionDef* findDef(CoreHost& host, const std::string& key) {
    for (const OptionDef& d : host.optionDefs())
        if (d.key == key) return &d;
    return nullptr;
}

std::string valueOf(CoreHost& host, const std::string& key) {
    const OptionDef* d = findDef(host, key);
    return d ? host.optionValue(*d) : std::string("<missing>");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vj_core_host_thread_test <rom.j64>\n");
        return 2;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec) / ("vj-core-host-thread-" + std::to_string((long long)std::time(nullptr)));
    fs::create_directories(tmp / "saves", ec);
    fs::create_directories(tmp / "system", ec);
    const std::thread::id uiThread = std::this_thread::get_id();
    CoreHost& host = CoreHost::instance();
    host.setDirectories((tmp / "system").string() + "/", (tmp / "saves").string() + "/");
    host.inputPoll = [] {};
    host.inputState = [](unsigned, unsigned, unsigned, unsigned) -> int16_t { return 0; };
    std::vector<std::string> changed;   // onOptionChanged calls (the app writes config.ini there)
    std::thread::id changedOn;
    host.onOptionChanged = [&](const std::string& k, const std::string& v, bool erased) {
        changed.push_back(k + "=" + (erased ? std::string("<erased>") : v));
        changedOn = std::this_thread::get_id();
    };
    const std::string rom = argv[1];
    const std::string key = "virtualjaguar_crash_detect";

    // ---- inline: no owner, everything as before ----------------------------------------------------
    CHECK(!host.threaded() && host.onOwner());
    std::string err;
    CHECK(host.loadContent(rom, err));
    host.runFrame();
    CHECK(host.frameCount() == 1);

    // ---- threaded --------------------------------------------------------------------------------
    EmuThread emu;
    HostClient client;
    host.post = [&emu](std::function<void()> fn) { emu.post(std::move(fn)); };
    emu.setPaused(true);   // frames only through advance()
    CHECK(emu.start(EmuThread::Mode::Threaded, client));
    CHECK(host.threaded() && !host.onOwner());
    host.pinSnapshot();

    // Reads off the owner come from the pinned snapshot.
    const OptionDef* def = findDef(host, key);
    CHECK(def != nullptr);
    CHECK(host.fps() > 59.0 && host.fps() < 61.0);
    CHECK(host.sampleRate() > 40000.0);
    const std::vector<OptionDef>* pinned = &host.optionDefs();
    const size_t pinnedCount = pinned->size();
    CHECK(emu.advance(5));
    CHECK(pinned->size() == pinnedCount);   // the pinned table stays valid while frames run, until the next pin

    // A write: the UI sees it at once, the config is written on the UI thread, the core's copy has it after the boundary.
    std::string other;
    if (def)
        for (const OptionValue& v : def->values)
            if (v.value != host.optionValue(*def)) {
                other = v.value;
                break;
            }
    CHECK(!other.empty());
    host.setOptionValue(key, other);
    CHECK(valueOf(host, key) == other);
    CHECK(!changed.empty() && changed.back() == key + "=" + other && changedOn == std::this_thread::get_id());
    CHECK(emu.call([&] { return valueOf(host, key); }) == other);   // posted before the call, so applied first
    host.resetOptionToDefault(key);
    CHECK(changed.back() == key + "=<erased>");
    CHECK(emu.call([&] { return host.userOptions().count(key) == 0; }));

    // A session override (--option, Netplay): visible to the UI at once, written to the core's copy at the boundary,
    // never to the UI's mirror from the emulation thread (the owner-side step is core-only).
    host.overrideOption(key, other);
    CHECK(host.optionOverridden(key) && valueOf(host, key) == other);
    CHECK(emu.call([&] { return host.optionOverridden(key) && valueOf(host, key) == other; }));
    host.clearOverride(key);
    CHECK(!host.optionOverridden(key));
    CHECK(emu.call([&] { return !host.optionOverridden(key); }));
    // R16: the visible-option count reads the same table as everything else off the owner.
    CHECK(host.visibleOptionCount() > 0);
    CHECK(host.visibleOptionCount() == emu.call([&] { return host.visibleOptionCount(); }));

    // OSD messages pushed on the emulation thread are taken here.
    host.takeMessages();
    emu.call([&] { host.pushMessage("from the emulation thread"); });
    std::vector<OsdMessage> msgs = host.takeMessages();
    CHECK(msgs.size() == 1 && msgs[0].text == "from the emulation thread");

    // Queued writers: no violation.
    const unsigned v0 = host.ownerViolations();
    host.sendKeyEvent(false, 0, 0, 0);
    host.setControllerDevice(0, RETRO_DEVICE_JOYPAD);
    host.discSetEject(false);
    emu.call([] {});
    CHECK(host.ownerViolations() == v0);

    // Core calls off the owner are refused and counted.
    const uint64_t f0 = host.frameCount();
    host.runFrame();
    host.reset();
    CHECK(!host.saveStateToFile((tmp / "x.state").string(), err));
    (void)host.frame();
    CHECK(host.frameCount() == f0);
    CHECK(host.ownerViolations() >= v0 + 4);
    CHECK(emu.advance(2) && host.frameCount() == f0 + 2);   // the owner runs them

    // A reload in a call: after the next pin the UI sees the new option push, never the previous table.
    // The batch-end publish is switched off (and the thread is paused, so no frame publishes either): the load has
    // to publish by itself before call() returns, with no wait between the call and the pin.
    const unsigned pushes = host.optionPushes();
    client.quiet.store(true);
    CHECK(emu.call([&] { return host.loadContent(rom, err); }));
    host.pinSnapshot();
    CHECK(host.optionPushes() > pushes);
    CHECK(findDef(host, key) != nullptr);
    CHECK(host.fps() > 59.0 && host.fps() < 61.0);
    CHECK(host.sampleRate() > 40000.0);
    CHECK(host.aspect() > 1.0f);

    // An unload through call() publishes too: the disc list is empty and the options are still the core's table.
    emu.call([&] { host.unloadContent(); });
    host.pinSnapshot();
    CHECK(!host.discInfo().available);
    client.quiet.store(false);

    // Threaded with no `post`: queued writers are dropped and counted, never recursed into or run on this thread.
    CHECK(emu.call([&] { return host.loadContent(rom, err); }));
    host.pinSnapshot();
    host.post = nullptr;
    const unsigned v1 = host.ownerViolations();
    host.sendKeyEvent(false, 0, 0, 0);
    host.setControllerDevice(0, RETRO_DEVICE_JOYPAD);
    host.discSetEject(false);
    host.discSetIndex(0);
    host.setOptionValue(key, other);
    host.overrideOption(key, other);
    CHECK(host.ownerViolations() == v1 + 6);
    host.resetOptionToDefault(key);
    host.clearOverride(key);
    host.post = [&emu](std::function<void()> fn) { emu.post(std::move(fn)); };

    // A lock whose value moves while the emulation thread owns the core (the Pro Controller, #1017): refreshLocks()
    // from the UI thread is queued, re-reads the rule on the owner, reaches the core through GET_VARIABLE_UPDATE
    // ahead of the next frame's input, and the UI's snapshot shows the new locked value after the publish + pin.
    // What the core reads back: RetroPad X is keypad 0 on a standard pad and keypad 9 (Pro X) under pad_pro.
    {
        static std::atomic<bool> pro{false}, pressX{false};
        const std::string dev = "virtualjaguar_p1_device";
        // The hooks the emulation thread reads are swapped on it (as the reload that follows needs them).
        CHECK(emu.call([&] {
            host.optionLock = [dev](const std::string& k, std::string& v) {
                if (k != dev) return false;
                v = pro.load() ? "pad_pro" : "auto";
                return true;
            };
            host.inputState = [](unsigned port, unsigned device, unsigned, unsigned id) -> int16_t {
                if (port != 0 || device != RETRO_DEVICE_JOYPAD || !pressX.load()) return 0;
                const int16_t x = (int16_t)(1 << RETRO_DEVICE_ID_JOYPAD_X);
                return id == RETRO_DEVICE_ID_JOYPAD_MASK ? x : (id == RETRO_DEVICE_ID_JOYPAD_X ? 1 : 0);
            };
            return host.loadContent(rom, err);   // the definitions push takes the lock
        }));
        host.pinSnapshot();
        CHECK(host.optionLocked(dev) && valueOf(host, dev) == "auto");
        auto keypad = [&](int slot) { return emu.call([slot] { return joypad0Buttons[slot] != 0; }); };
        const unsigned v2 = host.ownerViolations();
        pressX.store(true);
        CHECK(emu.advance(2));
        CHECK(keypad(BUTTON_0) && !keypad(BUTTON_9));   // negative control: a standard pad
        pro.store(true);
        host.refreshLocks();                            // UI thread: queued
        host.setControllerDevice(0, RETRO_DEVICE_JOYPAD);
        CHECK(emu.advance(1));                          // the queued work runs before this frame reads the pad
        CHECK(keypad(BUTTON_9) && !keypad(BUTTON_0));
        host.pinSnapshot();
        CHECK(valueOf(host, dev) == "pad_pro");         // the UI's copy of the locked value followed
        CHECK(emu.call([&] { return valueOf(host, dev); }) == "pad_pro");
        pro.store(false);
        host.refreshLocks();
        CHECK(emu.advance(1));
        CHECK(keypad(BUTTON_0) && !keypad(BUTTON_9));
        host.pinSnapshot();
        CHECK(valueOf(host, dev) == "auto");
        CHECK(host.ownerViolations() == v2);
        pressX.store(false);
        emu.call([&] {
            host.optionLock = nullptr;
            host.inputState = [](unsigned, unsigned, unsigned, unsigned) -> int16_t { return 0; };
        });
    }

    // ---- phase 2: the core-event stream ----
    {
        std::vector<std::thread::id> logOn;
        std::vector<std::string> lines;
        host.onUiEvent = [&](const CoreEvent& e) {
            if (e.kind != CoreEvent::Kind::Log) return;
            logOn.push_back(std::this_thread::get_id());
            lines.push_back(e.text);
        };
        std::string lerr;
        CHECK(emu.call([&] { return host.loadContent(rom, lerr); }));   // the core logs while it loads
        CHECK(lines.empty());                                           // nothing before the pin
        host.pinSnapshot();
        CHECK(!lines.empty());
        bool allUi = true;
        for (std::thread::id t : logOn) allUi = allUi && t == uiThread;
        CHECK(allUi);                                                   // never on the emulation thread
        host.onUiEvent = nullptr;
    }
    {
        // An option write made on the owner (inside a call) reaches the mirror and the config on the UI thread, at
        // the next pin -- never on the emulation thread.
        changed.clear();
        changedOn = std::thread::id();
        emu.call([&] { host.setOptionValue(key, "disabled"); });
        CHECK(changed.empty());
        host.pinSnapshot();
        CHECK(changed.size() == 1 && changed[0] == std::string(key) + "=disabled");
        CHECK(changedOn == uiThread);
        CHECK(host.userOptions().count(key) && host.userOptions().at(key) == "disabled");
        emu.call([&] { host.resetOptionToDefault(key); });
        host.pinSnapshot();
        CHECK(changed.size() == 2 && changed[1] == std::string(key) + "=<erased>");
        CHECK(host.userOptions().count(key) == 0);
        emu.call([&] { host.overrideOption(key, "enabled"); });
        host.pinSnapshot();
        CHECK(host.optionOverridden(key));
        emu.call([&] { host.clearOverride(key); });
        host.pinSnapshot();
        CHECK(!host.optionOverridden(key));
    }
    {
        // The epoch rides the command queue: a call queued after the bump sees it.
        const uint32_t e1 = host.eventEpochBump();
        const uint32_t e2 = host.eventEpochBump();
        CHECK(e2 > e1);
        CHECK(emu.call([&] { return host.eventEpoch(); }) == e2);
    }
    {
        // A reload drops what the previous game said but keeps the user's option writes.
        changed.clear();
        std::string lerr;
        emu.call([&] {
            host.setOptionValue(key, "disabled");
            return host.loadContent(rom, lerr);
        });
        host.pinSnapshot();
        CHECK(changed.size() == 1 && changed[0] == std::string(key) + "=disabled");
    }

    emu.call([&] { host.unloadContent(); });
    CHECK(emu.stop());
    host.post = nullptr;
    CHECK(!host.threaded() && host.onOwner());
    fs::remove_all(tmp, ec);   // our own temp dir only
    std::printf("core_host_thread_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
