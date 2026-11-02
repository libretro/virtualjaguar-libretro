// The core's option table without a game (#991 Task 19): CoreHost::probeOptions() runs retro_set_environment with only the option
// commands answered, so the settings screens list every option from the first frame, and the game that loads later sees the core
// exactly as if nothing had been probed.
//
//   vj_core_options_probe_test [rom.j64]
//
// Part 1 (no game): the table is there (118 options in 10 categories at the time of writing), the display rules ran (a gated row
// is hidden), a write with no game is stored and reported, a second probe pushes nothing, and the core never loaded anything.
// Part 1b (the emulation thread, #967): the thread started after the probe publishes the probed table at once, so the UI reads it from
// its first frame, and a write made from the UI thread reaches both copies.
// Part 2 (with a ROM): the visibility of every option after the FIRST load that follows the probe is the visibility after a second
// load of the same game -- the probe must not leave the core's display latches ("hidden" already) where the load's own display pass
// finds no change and never hides the row again.
#include "core_host.h"
#include "emu_thread.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <utility>
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

// The emulation thread's client: no game, so it only owns the core and idles.
struct IdleClient : EmuThread::Client {
    CoreHost& host = CoreHost::instance();
    void threadStarted() override { host.setOwner(std::this_thread::get_id()); }
    void threadStopping() override { host.setOwner(std::thread::id()); }
    bool canRun() override { return false; }
    double framePeriod() override { return 1.0 / 60.0; }
    double audioQueuedMs() override { return -1.0; }
    void runFrame() override {}
    void afterCommands() override { host.publishSnapshot(); }
};

static const OptionDef* findDef(CoreHost& h, const std::string& key) {
    for (const OptionDef& d : h.optionDefs())
        if (d.key == key) return &d;
    return nullptr;
}

static std::map<std::string, bool> visibility(CoreHost& h) {
    std::map<std::string, bool> m;
    for (const OptionDef& d : h.optionDefs()) m[d.key] = h.optionVisible(d.key);
    return m;
}

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path(ec) / ("vj-core-options-probe-" + std::to_string((long long)std::time(nullptr)));
    fs::create_directories(tmp / "saves", ec);
    fs::create_directories(tmp / "system", ec);
    CoreHost& h = CoreHost::instance();
    h.setDirectories((tmp / "system").string() + "/", (tmp / "saves").string() + "/");
    h.inputPoll = [] {};
    h.inputState = [](unsigned, unsigned, unsigned, unsigned) -> int16_t { return 0; };
    std::vector<std::pair<std::string, std::string>> changed;
    h.onOptionChanged = [&](const std::string& k, const std::string& v, bool erased) { if (!erased) changed.push_back({k, v}); };

    // ---- part 1: no game ---------------------------------------------------------------------------------------------------------
    CHECK(h.optionDefs().empty() && h.optionCategories().empty() && h.optionPushes() == 0);
    h.probeOptions();
    CHECK(h.optionDefs().size() >= 100);
    CHECK(h.optionCategories().size() == 10);
    CHECK(!h.isLoaded());
    const unsigned pushes = h.optionPushes();
    CHECK(pushes >= 1);
    h.probeOptions();   // once is all it takes
    CHECK(h.optionPushes() == pushes && h.optionDefs().size() >= 100 && !h.isLoaded());

    const std::string res = "virtualjaguar_internal_resolution";
    const OptionDef* d = findDef(h, res);
    CHECK(d != nullptr && d->categoryKey == "video");
    if (d) CHECK(!d->values.empty() && h.optionValue(*d) == d->defaultValue);

    // The display pass ran: libretro.c update_option_visibility() hides the Network Link host and port while the mode is its default
    // (auto), the texture-dump preview knob while the dump is off, and the voice chat sub-options while voice chat is off; options
    // with no gate stay visible; with no content loaded the CD-only and cartridge-only groups both stay (the type is unknown).
    CHECK(!h.optionVisible("virtualjaguar_netlink_host"));
    CHECK(!h.optionVisible("virtualjaguar_netlink_port"));
    CHECK(!h.optionVisible("virtualjaguar_texdump_16bpp"));
    CHECK(!h.optionVisible("virtualjaguar_voice_chat_gate"));
    CHECK(h.optionVisible(res) && h.optionVisible("virtualjaguar_netlink"));
    CHECK(h.optionVisible("virtualjaguar_cd_bios_type") && h.optionVisible("virtualjaguar_bios"));

    // A write with no game is kept, reported (the app saves it to config.ini there) and read back; the next load reads it.
    h.setOptionValue(res, "2x");
    CHECK(h.optionValueFor(res, "") == "2x");
    CHECK(changed.size() == 1 && changed[0].first == res && changed[0].second == "2x");
    if (d) CHECK(h.optionValue(*d) == "2x");
    CHECK(h.optionPushes() == pushes && !h.isLoaded());
    h.resetOptionToDefault(res);

    // ---- part 1b: the emulation thread starts after the probe -------------------------------------------------------------------
    {
        EmuThread emu;
        IdleClient client;
        h.post = [&emu](std::function<void()> fn) { emu.post(std::move(fn)); };
        emu.setPaused(true);
        CHECK(emu.start(EmuThread::Mode::Threaded, client));
        CHECK(h.threaded() && !h.onOwner());
        h.pinSnapshot();
        CHECK(h.optionDefs().size() >= 100 && h.optionCategories().size() == 10 && h.optionPushes() == pushes);   // the UI's copy, from the first pin
        CHECK(!h.optionVisible("virtualjaguar_netlink_host") && h.optionVisible(res));
        CHECK(h.optionValueFor(res, "") == "");
        changed.clear();
        h.setOptionValue(res, "2x");   // off the owner: the UI's copy now, the config write here, the core's copy at the boundary
        CHECK(h.optionValueFor(res, "") == "2x" && changed.size() == 1 && changed[0].second == "2x");
        CHECK(emu.call([&] { return h.optionValueFor(res, ""); }) == "2x");
        h.resetOptionToDefault(res);
        CHECK(h.optionValueFor(res, "") == "" && emu.call([&] { return h.optionValueFor(res, ""); }) == "");
        CHECK(emu.stop());
        h.post = nullptr;
        CHECK(!h.threaded() && h.onOwner() && !h.isLoaded());
        changed.clear();
    }

    // ---- part 2: the first game after a probe sees what the second one does ------------------------------------------------------
    if (argc > 1) {
        const std::string rom = argv[1];
        std::string err;
        CHECK(h.loadContent(rom, err));
        CHECK(h.isLoaded() && h.optionPushes() > pushes);   // startSession re-pushed the table
        const std::map<std::string, bool> first = visibility(h);
        CHECK(!first.empty());
        // The load's gates: content is a cartridge, so the CD-only group is hidden, and the netlink rows stay hidden.
        CHECK(first.count("virtualjaguar_netlink_host") && !first.at("virtualjaguar_netlink_host"));
        CHECK(first.count("virtualjaguar_cd_bios_type") && !first.at("virtualjaguar_cd_bios_type"));
        h.unloadContent();
        CHECK(h.loadContent(rom, err));
        const std::map<std::string, bool> second = visibility(h);
        size_t diff = 0;
        for (const auto& kv : first) {
            auto it = second.find(kv.first);
            if (it == second.end() || it->second != kv.second) {
                diff++;
                std::fprintf(stderr, "  visibility differs: %s first=%d second=%d\n", kv.first.c_str(), kv.second,
                             it == second.end() ? -1 : (int)it->second);
            }
        }
        CHECK(diff == 0 && first.size() == second.size());
        h.unloadContent();
    }
    // A probe after a game is a no-op: the options are known, and a session may be running.
    h.probeOptions();
    CHECK(!h.isLoaded());

    std::fprintf(stderr, "core_options_probe_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
