#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <optional>

#include "content_server.h"
#include "library_service.h"

namespace vj {

class Config;

// UI state is owned by the main thread. The worker delivers one result through pump().
class ContentServerTab {
public:
    ContentServerTab(std::string name, std::string origin, ecs::Protocol protocol, std::string root);
    ~ContentServerTab() { stop(); }
    void stop();
    void pump();
    void connect();
    void browse(int library, bool append = false);
    void selectGame(int game);
    void loadAddons();
    void download(int game);
    void downloadAddon(const ecs::Addon& addon, bool boot);
    ArtView cover(const ecs::Game& game, LibraryService& library);
    void clearCovers();
    void cancel() { cancel_.store(true); }

    const std::string name, origin;
    const ecs::Protocol protocol;
    bool opened = false;
    std::string libraryId;
    std::string credentialKey() const;
    bool busy = false, downloading = false, rememberToken = true;
    bool launchReady = false;
    int selectedLibrary = -1, selectedGame = -1, system = 0;
    char token[2048] = {};
    char search[257] = {};
    std::string message, credentialNotice, nextCursor, readyPath;
    std::vector<ecs::Library> libraries;
    std::vector<ecs::Game> games;
    std::vector<ecs::Addon> addons;
    bool addonsLoaded = false;
    struct PatchLaunch {
        std::string base, patch;
        ecs::Addon addon;
    };
    std::optional<PatchLaunch> patchReady;
    uint64_t received() const { return received_.load(); }
    uint64_t total() const { return total_.load(); }

private:
    friend class ContentServerBrowser;
    enum class Kind { Libraries, Games, Addons, Download, AddonDownload, Error };
    struct Result {
        Kind kind = Kind::Error;
        std::vector<ecs::Library> libraries;
        ecs::Page page;
        std::vector<ecs::Addon> addons;
        std::optional<PatchLaunch> patch;
        std::string path, error;
        bool append = false;
    };
    void start(std::function<Result()> task);
    struct Cover {
        ArtView view;
        uint64_t used = 0;
    };
    std::string root_, activeSearch_, activeSystem_, activeToken_;
    std::map<std::string, Cover> covers_;
    uint64_t coverSequence_ = 0;
    std::function<void(unsigned)> destroyTexture_;
    Worker artWorker_;
    std::atomic<bool> artCancel_{false};
    std::string pendingCover_;
    struct CoverResult {
        int w = 0, h = 0;
        std::vector<unsigned char> rgba;
    };
    std::optional<CoverResult> coverResult_;
    Worker worker_;
    std::atomic<bool> cancel_{false};
    std::atomic<uint64_t> received_{0}, total_{0};
    std::mutex mutex_;
    std::optional<Result> result_;
};

class ContentServerBrowser {
public:
    void init(Config& config, const std::string& dataRoot, bool persistSettings);
    void stop();
    bool add();
    bool forget(size_t index);
    void save();
    void saveToken(ContentServerTab& tab);

    bool show = false;
    int protocol = 0;
    char name[128] = {};
    char address[512] = {};
    std::string message;
    std::vector<std::unique_ptr<ContentServerTab>> tabs;

private:
    Config* config_ = nullptr;
    std::string root_;
    bool persist_ = false;
};

class App;
void drawContentServers(App& app);

} // namespace vj
