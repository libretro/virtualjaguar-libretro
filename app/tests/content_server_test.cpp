#include "content_server.h"
#include "content_server_browser.h"
#include "config.h"
#include "m3u.h"
#include "secret_store.h"
#include "sha256.h"

#include <chrono>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
using namespace vj;
using neuro::Json;
static int failures = 0;
#define CHECK(value) do { if (!(value)) { ++failures; std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); } } while (0)

static bool rejects(const std::function<void()>& task) {
    try { task(); return false; } catch (const std::exception&) { return true; }
}

static std::string bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

struct Fixture {
    std::map<std::string, std::string> responses;
    int status = 200;
    bool cancelDuringWrite = false, truncate = false;
    std::atomic<bool> cancel{false};
    std::vector<std::string> paths;
    ecs::Transport transport() {
        return [this](const std::string& url, http::Response& response, uint64_t limit,
                      const std::atomic<bool>*, const http::Options& options) {
            CHECK(url.rfind("http://localhost:8473/api/v1/", 0) == 0);
            CHECK(options.bearerToken == "fixture-token");
            CHECK(!options.followRedirects);
            const std::string path = url.substr(std::string("http://localhost:8473").size());
            paths.push_back(path);
            CHECK(responses.count(path));
            const std::string& body = responses[path];
            response.status = status;
            if (options.write) {
                const size_t size = body.size() - (truncate && !body.empty() ? 1 : 0);
                CHECK(size <= limit);
                if (cancelDuringWrite) cancel.store(true);
                if (!options.write(body.data(), size)) { response.error = "cancelled"; return false; }
                response.bytesReceived = size;
            } else response.body.assign(body.begin(), body.end());
            return true;
        };
    }
    ecs::Client client() { return ecs::Client("http://LOCALHOST:8473/", "fixture-token", transport()); }
};

static Json parse(const std::string& value) {
    Json result;
    std::string error;
    CHECK(Json::parse(value, result, error));
    return result;
}

static ecs::Game item() { return {"game-1", "Fixture game", "item", "com.provenance.jaguar", Json::object()}; }

static std::string manifest(const std::string& path, const std::string& content, const std::string& hash = "") {
    return "{\"files\":[{\"id\":\"file-1\",\"kind\":\"rom\",\"relativePath\":" + neuro::jsonQuote(path) +
           ",\"required\":true,\"size\":" + std::to_string(content.size()) + ",\"sha256\":\"" +
           (hash.empty() ? sha256Hex(content.data(), content.size()) : hash) +
           "\",\"contentPath\":\"https://untrusted.invalid/steal\"}]}";
}

static void rommGatewayContract() {
    // The gateway now advertises RomM slugs, but serves ECS archives instead of the
    // direct RomM per-file download contract. Detect its platform marker before browsing.
    const ecs::Client gateway("http://localhost:8080", "gateway-fixture-token",
        [](const std::string& url, http::Response& response, uint64_t,
           const std::atomic<bool>*, const http::Options& options) {
            CHECK(url == "http://localhost:8080/api/platforms");
            CHECK(options.bearerToken == "gateway-fixture-token" && !options.followRedirects);
            const std::string body = R"([{"id":7,"name":"Atari Jaguar","slug":"jaguar","rom_count":4,"ecs_system_id":"com.provenance.jaguar"}])";
            response.status = 200;
            response.body.assign(body.begin(), body.end());
            return true;
        }, ecs::Protocol::RomM);
    CHECK(rejects([&] { gateway.libraries(); }));
}

static void rommArtworkPaths() {
    unsigned requests = 0;
    const ecs::Client client("http://localhost:8080", "romm-fixture-token",
        [&](const std::string&, http::Response&, uint64_t,
            const std::atomic<bool>*, const http::Options&) {
            ++requests;
            return false;
        }, ecs::Protocol::RomM);
    auto game = item();
    for (const char* path : {"https://another-server.invalid/cover.png", "//another-server.invalid/cover.png",
                             "/assets/romm/resources/../cover.png", "/assets/romm/resources/%2e%2e/cover.png"}) {
        game.artwork = path;
        CHECK(client.artwork(game, nullptr).empty());
    }
    CHECK(requests == 0);
}

static void persistedServerIdentity(const fs::path& root) {
    Config config;
    config.load((root / "legacy-servers.ini").u8string());
    config.set("ecs.server.0", "http://localhost:8473");
    secret::useMemoryBackend();
    constexpr const char* service = "io.github.libretro.VirtualJaguar.ContentServer";
    CHECK(secret::active().set(service, "http://localhost:8473", "old-ecs-token") == secret::Status::Ok);

    ContentServerBrowser manager;
    manager.init(config, root.u8string() + "/", true);
    CHECK(manager.tabs.size() == 1);
    if (manager.tabs.size() != 1) return;
    CHECK(std::string(manager.tabs.front()->token) == "old-ecs-token");

    std::snprintf(manager.name, sizeof manager.name, "%s", "Direct RomM");
    std::snprintf(manager.address, sizeof manager.address, "%s", "HTTP://LOCALHOST:8473/");
    CHECK(!manager.add()); // Equivalent ECS origins resolve to the same tab.
    manager.protocol = 1;
    CHECK(manager.add()); // The other protocol has a separate credential slot.
    if (manager.tabs.size() != 2) return;
    std::snprintf(manager.tabs.back()->token, sizeof manager.tabs.back()->token, "%s", "direct-romm-token");
    manager.saveToken(*manager.tabs.back());

    CHECK(manager.forget(1));
    std::string token;
    CHECK(secret::active().get(service, "romm:http://localhost:8473", token) == secret::Status::NotFound);
    CHECK(secret::active().get(service, "http://localhost:8473", token) == secret::Status::Ok);
    CHECK(token == "old-ecs-token");
    CHECK(config.withPrefix("ecs.server.").empty());
    CHECK(bytes(root / "legacy-servers.ini").find("-token") == std::string::npos);
}

static void tests(const fs::path& root) {
    rommGatewayContract();
    rommArtworkPaths();
    persistedServerIdentity(root);
    CHECK(ecs::normalizeOrigin("HTTP://Example.COM:080/") == "http://example.com");
    CHECK(ecs::normalizeOrigin("https://[::1]:8473/") == "https://[::1]:8473");
    for (const char* url : {"file:///tmp", "http://user:secret@host", "http://host/ui", "http://host?token=x", "http://host:99999", "http://host\\evil"})
        CHECK(rejects([&] { ecs::normalizeOrigin(url); }));
    for (const char* path : {"../bad.j64", "/tmp/bad", "C:/bad", "a\\bad", "a/../bad", "a//bad", "CON.bin", "a./b", "a/", "game.j64:stream"})
        CHECK(!ecs::safeRelativePath(path));
    CHECK(ecs::safeRelativePath("disc/Game (USA).cue"));

    std::string longBytes(1000, 'x');
    Sha256 hash;
    for (size_t i = 0; i < longBytes.size(); i += 7) hash.update(longBytes.data() + i, std::min(size_t(7), longBytes.size() - i));
    CHECK(hash.hexDigest() == sha256Hex(longBytes.data(), longBytes.size()));
    CHECK(sha256Hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    Fixture fixture;
    fixture.responses["/api/v1/capabilities"] = "{\"protocol\":{\"major\":1}}";
    fixture.responses["/api/v1/browse-libraries"] = "{\"items\":[{\"id\":\"lib\",\"name\":\"Shelf\"}],\"nextCursor\":null}";
    CHECK(fixture.client().libraries().size() == 1);
    fixture.status = 401;
    CHECK(rejects([&] { fixture.client().libraries(); }));
    fixture.status = 200;
    fixture.responses["/api/v1/capabilities"] = "{\"protocol\":{\"major\":2}}";
    CHECK(rejects([&] { fixture.client().libraries(); }));
    fixture.responses["/api/v1/browse?category=games&limit=100&libraryId=lib&systemId=com.provenance.jaguar&search=hello%20world&cursor=next%2Bpage"] =
        "{\"items\":[{\"id\":\"game-1\",\"name\":\"Fixture\",\"entityType\":\"item\",\"systemId\":\"com.provenance.jaguar\",\"item\":{}}],\"nextCursor\":null}";
    const ecs::Page page = fixture.client().games("lib", "hello world", "com.provenance.jaguar", "next+page");
    CHECK(page.games.size() == 1 && page.nextCursor.empty());

    const std::string data = "generated-cartridge";
    fixture.responses["/api/v1/items/game-1/manifest"] = manifest("Game.j64", data);
    fixture.responses["/api/v1/files/file-1/content"] = data;
    const std::string local = fixture.client().download(item(), root.u8string(), &fixture.cancel);
    CHECK(bytes(fs::u8path(local)) == data);
    CHECK(fixture.paths.back() == "/api/v1/files/file-1/content");
    CHECK(fixture.client().download(item(), root.u8string(), &fixture.cancel) == local);
    ecs::Game other = item();
    other.id = "different-game";
    fixture.responses["/api/v1/items/different-game/manifest"] = manifest("Game.j64", data);
    const std::string otherLocal = fixture.client().download(other, root.u8string(), &fixture.cancel);
    CHECK(fs::u8path(local).stem() != fs::u8path(otherLocal).stem());
    std::string withBios = manifest("Game.j64", data);
    withBios.resize(withBios.size() - 2);
    const std::string biosHash = sha256Hex("bios", 4);
    withBios += ",{\"id\":\"bios\",\"kind\":\"bios\",\"required\":true,\"relativePath\":\"Firmware.rom\",\"size\":4,\"sha256\":\"" + biosHash + "\"}]}";
    fixture.responses["/api/v1/items/game-1/manifest"] = withBios;
    fixture.responses["/api/v1/files/bios/content"] = "bios";
    const std::string withFirmware = fixture.client().download(item(), root.u8string(), &fixture.cancel);
    CHECK(fs::u8path(withFirmware).filename() == fs::u8path(local).filename());
    CHECK(bytes(fs::u8path(withFirmware).parent_path() / "Firmware.rom") == "bios");
    fixture.responses["/api/v1/items/game-1/manifest"] = manifest("Game.j64", data);

    Fixture firstDownload, secondDownload;
    firstDownload.responses = fixture.responses;
    secondDownload.responses = fixture.responses;
    firstDownload.responses["/api/v1/items/concurrent/manifest"] = manifest("Game.j64", data);
    secondDownload.responses["/api/v1/items/concurrent/manifest"] = manifest("Game.j64", data);
    ecs::Game concurrent = item();
    concurrent.id = "concurrent";
    std::string firstPath, secondPath;
    bool firstOk = true, secondOk = true;
    const auto downloadTogether = [&](Fixture& input, std::string& output, bool& ok) {
        try { output = input.client().download(concurrent, root.u8string(), &input.cancel); }
        catch (const std::exception&) { ok = false; }
    };
    std::thread first(downloadTogether, std::ref(firstDownload), std::ref(firstPath), std::ref(firstOk));
    std::thread second(downloadTogether, std::ref(secondDownload), std::ref(secondPath), std::ref(secondOk));
    first.join();
    second.join();
    CHECK(firstOk && secondOk && firstPath == secondPath);
    // A modified cache entry is never launched as the verified downloaded version.
    { std::ofstream changed(fs::u8path(local)); changed << "modified"; }
    const std::string repaired = fixture.client().download(item(), root.u8string(), &fixture.cancel);
    CHECK(repaired != local && bytes(fs::u8path(repaired)) == data);
    fixture.responses["/api/v1/items/game-1/manifest"] = manifest("../escape.j64", data);
    CHECK(rejects([&] { fixture.client().download(item(), root.u8string(), &fixture.cancel); }));
    fixture.responses["/api/v1/items/game-1/manifest"] = manifest("Game.j64", data, std::string(64, '0'));
    CHECK(rejects([&] { fixture.client().download(item(), root.u8string(), &fixture.cancel); }));
    fixture.responses["/api/v1/items/game-1/manifest"] = manifest("Game.j64", data);
    fixture.truncate = true;
    CHECK(rejects([&] { fixture.client().download(item(), root.u8string(), &fixture.cancel); }));
    fixture.truncate = false;
    fixture.cancelDuringWrite = true;
    CHECK(rejects([&] { fixture.client().download(item(), root.u8string(), &fixture.cancel); }));
    fixture.cancelDuringWrite = false;
    fixture.cancel.store(false);

    ecs::Game source{"cue", "Game", "source", "com.provenance.jaguarcd", parse("{\"name\":\"Game.cue\",\"groupFileCount\":2}")};
    const std::string cue = "FILE \"../tracks/Track.bin\" BINARY\n TRACK 01 MODE1/2352\n INDEX 01 00:00:00\n";
    fixture.responses["/api/v1/sources/cue/group"] = "{\"error\":null,\"files\":[{\"id\":\"cue\",\"relativePath\":\"disc/Game.cue\",\"size\":" +
        std::to_string(cue.size()) + ",\"availability\":\"local\"},{\"id\":\"track\",\"relativePath\":\"tracks/Track.bin\",\"size\":3,\"availability\":\"local\"}]}";
    fixture.responses["/api/v1/sources/cue/content"] = cue;
    fixture.responses["/api/v1/sources/track/content"] = "bin";
    const std::string disc = fixture.client().download(source, root.u8string(), &fixture.cancel);
    CHECK(bytes(fs::u8path(disc)) == cue);
    CHECK(bytes(fs::u8path(disc).parent_path().parent_path() / "tracks/Track.bin") == "bin");

    // The loader keys states by the mounted disc's stem, including after a swap.
    // Two sets (and two same-named discs in one set) must never share that key.
    const auto downloadSet = [&](const std::string& id, bool rootPlaylist = false) {
        const std::string prefix = rootPlaylist ? "" : "../";
        const std::vector<std::pair<std::string, std::string>> files = {
            {rootPlaylist ? "Game.m3u" : "playlists/Game.m3u", "\xEF\xBB\xBF#EXTM3U\r\n" + prefix +
                "first/Disc 1.chd|First\r\n" + prefix + "second/Disc 1.chd\r\n" + prefix + "disc/Game.cue\r\n" + prefix + "disc/Game.iso\r\n"},
            {"first/Disc 1.chd", "generated-disc-one"},
            {"second/Disc 1.chd", "generated-disc-two"},
            {"disc/Game.cue", cue}, {"tracks/Track.bin", "bin"}, {"disc/Game.iso", "generated-iso"}};
        std::string body = "{\"files\":[";
        for (size_t i = 0; i < files.size(); ++i) {
            const std::string fileId = "set-file-" + std::to_string(i);
            const auto& file = files[i];
            if (i) body += ",";
            body += "{\"id\":" + neuro::jsonQuote(fileId) + ",\"kind\":\"rom\",\"required\":true,\"relativePath\":" +
                neuro::jsonQuote(file.first) + ",\"size\":" + std::to_string(file.second.size()) + ",\"sha256\":\"" +
                sha256Hex(file.second.data(), file.second.size()) + "\"}";
            fixture.responses["/api/v1/files/" + fileId + "/content"] = file.second;
        }
        fixture.responses["/api/v1/items/" + id + "/manifest"] = body + "]}";
        ecs::Game game = item();
        game.id = id;
        return fs::u8path(fixture.client().download(game, root.u8string(), &fixture.cancel));
    };
    const fs::path firstSet = downloadSet("multi-disc-one");
    const fs::path secondSet = downloadSet("multi-disc-two", true);
    CHECK(downloadSet("multi-disc-one") == firstSet); // rewritten descriptors participate in cache verification
    const auto firstDiscs = parseM3u(bytes(firstSet), "");
    const auto secondDiscs = parseM3u(bytes(secondSet), "");
    CHECK(firstDiscs.size() == 4 && secondDiscs.size() == 4);
    for (size_t i = 0; i < std::min(firstDiscs.size(), secondDiscs.size()); ++i) {
        const fs::path firstDisc = firstSet.parent_path() / fs::u8path(firstDiscs[i]);
        const fs::path secondDisc = secondSet.parent_path() / fs::u8path(secondDiscs[i]);
        CHECK(fs::exists(firstDisc) && fs::exists(secondDisc));
        CHECK(firstDisc.stem() != secondDisc.stem());
        CHECK(bytes(firstDisc) == bytes(secondDisc));
        for (size_t j = 0; j < i; ++j) CHECK(firstDisc.stem() != fs::u8path(firstDiscs[j]).stem());
        if (firstDisc.extension() == ".cue") {
            CHECK(bytes(firstDisc) == cue);
            CHECK(bytes(firstDisc.parent_path().parent_path() / "tracks/Track.bin") == "bin");
        }
    }
    const std::string unsafe = "FILE \"../../secrets.bin\" BINARY\n";
    fixture.responses["/api/v1/sources/cue/group"] = "{\"error\":null,\"files\":[{\"id\":\"cue\",\"relativePath\":\"disc/Game.cue\",\"size\":" + std::to_string(unsafe.size()) + ",\"availability\":\"local\"}]}";
    fixture.responses["/api/v1/sources/cue/content"] = unsafe;
    CHECK(rejects([&] { fixture.client().download(source, root.u8string(), &fixture.cancel); }));
    for (const auto& entry : fs::recursive_directory_iterator(root))
        CHECK(entry.path().filename().string().rfind(".download-", 0) != 0);
}

static int network(const std::string& origin, const std::string& rommOrigin, const std::string& offlineOrigin,
                   const fs::path& root) {
    Config config;
    config.load((root / "config.ini").u8string());
    secret::useMemoryBackend();
    ContentServerBrowser manager;
    manager.init(config, root.u8string() + "/", true);
    const auto add = [&](const std::string& address, const char* name, const char* token, int protocol) {
        std::snprintf(manager.address, sizeof manager.address, "%s", address.c_str());
        std::snprintf(manager.name, sizeof manager.name, "%s", name);
        manager.protocol = protocol;
        if (!manager.add()) throw std::runtime_error(manager.message);
        auto& tab = *manager.tabs.back();
        std::snprintf(tab.token, sizeof tab.token, "%s", token);
        manager.saveToken(tab);
    };
    add(origin, "ECS fixture", "fixture-token", 0);
    add(rommOrigin, "RomM fixture", "romm-fixture-token", 1);
    add(offlineOrigin, "Offline fixture", "offline-fixture-token", 0);
    auto& browser = *manager.tabs[0];
    auto& romm = *manager.tabs[1];
    auto& offline = *manager.tabs[2];
    std::snprintf(romm.search, sizeof romm.search, "%s", "Jag + CD");
    offline.connect();
    romm.connect();
    browser.connect();
    const auto wait = [](ContentServerTab& state) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (state.busy && std::chrono::steady_clock::now() < deadline) {
            state.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return !state.busy;
    };
    if (!wait(browser) || browser.games.empty()) { std::fprintf(stderr, "FAIL browse: %s\n", browser.message.c_str()); return 1; }
    LibraryService art;
    art.createTexture = [](const void* rgba, int w, int h) {
        const auto* pixels = static_cast<const unsigned char*>(rgba);
        CHECK(w == 1 && h == 1 && pixels[0] == 0x20 && pixels[3] == 0xff);
        return 1u;
    };
    art.destroyTexture = [](unsigned texture) { CHECK(texture == 1); };
    const auto coverDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    unsigned texture = 0;
    while (!(texture = browser.cover(browser.games.front(), art).tex) && std::chrono::steady_clock::now() < coverDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(texture == 1);
    browser.browse(0, true);
    if (!wait(browser) || !browser.nextCursor.empty() || browser.games.size() != 2) return 2;
    browser.download(0);
    if (!wait(browser) || !browser.launchReady) { std::fprintf(stderr, "FAIL download: %s\n", browser.message.c_str()); return 3; }
    const std::string managedLaunch = browser.readyPath;
    browser.download(1);
    if (!wait(browser) || !browser.launchReady) return 4;
    CHECK(bytes(fs::u8path(browser.readyPath)) == bytes(fs::u8path(managedLaunch)));
    CHECK(fs::u8path(browser.readyPath).stem() != fs::u8path(managedLaunch).stem());
    CHECK(wait(offline) && !offline.message.empty() && offline.games.empty());
    CHECK(wait(romm) && romm.games.size() == 1 && romm.games.front().id == browser.games.front().id);
    CHECK(romm.nextCursor == "1" && browser.nextCursor.empty() && browser.games.size() == 2);
    CHECK(romm.selectedLibrary == 0 && !romm.games.empty() && romm.games.front().artwork.empty());
    romm.browse(0, true);
    CHECK(wait(romm) && romm.games.size() == 2 && romm.nextCursor.empty());
    if (romm.games.size() != 2) return 5;
    const ecs::Client rommClient(rommOrigin, "romm-fixture-token", http::get, ecs::Protocol::RomM);
    CHECK(rommClient.artwork(romm.games[1], nullptr).empty()); // missing advertised cover is harmless
    auto offOriginCover = romm.games[1];
    offOriginCover.artwork = "https://untrusted.invalid/secret";
    CHECK(rommClient.artwork(offOriginCover, nullptr).empty());
    romm.download(0);
    if (!wait(romm) || !romm.launchReady) {
        std::fprintf(stderr, "FAIL RomM download: %s\n", romm.message.c_str());
        return 6;
    }
    const std::string rommLaunch = romm.readyPath;
    CHECK(bytes(fs::u8path(rommLaunch)) == bytes(fs::u8path(managedLaunch)));
    CHECK(fs::u8path(rommLaunch).stem() != fs::u8path(managedLaunch).stem());
    romm.system = 1;
    romm.browse(0);
    CHECK(wait(romm) && romm.games.size() == 1 && romm.games.front().systemId == "com.provenance.jaguarcd");
    CHECK(!romm.games.empty() && !rommClient.artwork(romm.games.front(), nullptr).empty());
    romm.download(0);
    CHECK(wait(romm) && romm.launchReady);
    CHECK(bytes(fs::u8path(romm.readyPath)) == "FILE \"Track.bin\" BINARY\n  TRACK 01 MODE1/2352\n");
    CHECK(bytes(fs::u8path(romm.readyPath).parent_path() / "Track.bin") == "track");
    manager.save();
    Config restoredConfig;
    restoredConfig.load((root / "config.ini").u8string());
    ContentServerBrowser restored;
    restored.init(restoredConfig, root.u8string() + "/", true);
    CHECK(restored.tabs.size() == 3);
    if (restored.tabs.size() != 3) return 7;
    auto& restoredEcs = *restored.tabs[0];
    restoredEcs.connect();
    CHECK(wait(restoredEcs) && restoredEcs.games.size() == 1);
    CHECK(std::string(restoredEcs.token) == "fixture-token");
    CHECK(restored.tabs[1]->name == "RomM fixture" && std::string(restored.tabs[1]->token) == "romm-fixture-token");
    CHECK(std::string(restored.tabs[1]->search) == "Jag + CD" && restored.tabs[1]->system == 1);
    restored.stop();
    CHECK(bytes(root / "config.ini").find("fixture-token") == std::string::npos);
    http::Options options;
    options.bearerToken = "fixture-token";
    http::Response response;
    CHECK(http::get(origin + "/redirect", response, 1024, nullptr, options));
    CHECK(response.status == 302); // credentials never follow redirects, even with the default setting
    options.bearerToken.clear();
    CHECK(http::get(origin + "/redirect", response, 1024, nullptr, options));
    CHECK(response.status == 200 && std::string(response.body.begin(), response.body.end()) == "public");
    options.bearerToken = "invalid\r\nInjected: header";
    CHECK(!http::get(origin + "/public", response, 1024, nullptr, options));
    std::printf("LAUNCH %s\n", managedLaunch.c_str());
    std::printf("SOURCE_LAUNCH %s\n", browser.readyPath.c_str());
    std::printf("ROMM_LAUNCH %s\n", rommLaunch.c_str());
    CHECK(manager.forget(0));
    CHECK(manager.tabs.size() == 2 && manager.tabs[0]->origin == rommOrigin);
    CHECK(manager.tabs[0]->games.size() == 1);
    std::string saved;
    CHECK(secret::active().get("io.github.libretro.VirtualJaguar.ContentServer", origin, saved) == secret::Status::NotFound);
    return failures ? 1 : 0;
}

int main(int argc, char** argv) {
    const fs::path root = fs::temp_directory_path() / ("vj-ecs-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    int result = 1;
    try {
        if (argc == 5 && std::string(argv[1]) == "--servers") result = network(argv[2], argv[3], argv[4], root);
        else { tests(root); result = failures ? 1 : 0; std::printf("Content server checks: %d failures\n", failures); }
    } catch (const std::exception& error) { std::fprintf(stderr, "FAIL %s\n", error.what()); }
    // Integration driver owns the published game until its headless launch finishes.
    if (argc != 5) fs::remove_all(root);
    else std::printf("CACHE %s\n", root.u8string().c_str());
    return result;
}
